#include "glib-object.h"
#include "glib.h"
#include "glibconfig.h"
#include "gst/gstbin.h"
#include "gst/gstbus.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstelementfactory.h"
#include "gst/gstformat.h"
#include "gst/gstmessage.h"
#include "gst/gstpad.h"
#include "gst/gstpipeline.h"
#include "gst/gstsegment.h"
#include "gst/gstutils.h"
#include <jansson.h>
#include <gst/gst.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

typedef struct _StreamComponents {
    GstElement *pipeline, *source, *demux;
    GstElement *video_queue, *video_identity, *video_parse;
    GstElement *audio_queue, *audio_identity, *audio_parse;
    GstElement *whip_sink;
} StreamComponents;

static volatile sig_atomic_t keep_running = 1;

static void stop_running(int dummy) {
    keep_running = 0;
}

static gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data) {
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("EOS\n");
            break;

          case GST_MESSAGE_ERROR: {
            GError *err = NULL;
            gchar *debug_info = NULL;

            gst_message_parse_error(
                msg,
                &err,
                &debug_info
            );

            g_printerr(
                "Error from %s: %s\n",
                GST_OBJECT_NAME(msg->src),
                err->message
            );

            g_printerr(
                "Debug info: %s\n",
                debug_info ? debug_info : "none"
            );

            g_clear_error(&err);
            g_free(debug_info);

            break;
        }
    }
    
    return TRUE;
}

static gboolean socket_callback(GIOChannel *source, GIOCondition condition, gpointer data) {
    GstElement *pipeline = data;
    char buf[1024];
    json_t *root;
    json_error_t error;
    
    // setup client socket
    int fd = g_io_channel_unix_get_fd(source);
    int client_socket;
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t clen = sizeof(client_addr);
    client_socket = accept(fd, (struct sockaddr *) &client_addr, &clen);

    // load json
    ssize_t read_result = read(client_socket, buf, sizeof(buf));

    if (read_result < 0) {
        perror("read");
        close(client_socket);
        return TRUE;
    }
    buf[read_result] = '\0';

    root = json_loads(buf, 0, &error);

    // check json for errors
    if (!root)
    {
        g_print("Error with json:\n%s", buf);
        return TRUE;
    }

    if (!json_is_object(root)) {
        fprintf(stderr, "error: root is not an array\n");
        json_decref(root);
        return TRUE;
    }
    
    // parse the actual command
    json_t *cmd_obj = json_object_get(root, "command");

    if (!json_is_string(cmd_obj)) {
        fprintf(stderr, "error: command is not a string.\n");
        json_decref(root);
        return TRUE;
    }

    const char *cmd = json_string_value(cmd_obj);
    char response_buf[256];

    /* find the command */
    if (strcmp(cmd, "play") == 0) {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        snprintf(response_buf, sizeof(response_buf), "Playing.");
    } else if (strcmp(cmd, "pause") == 0) {
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        snprintf(response_buf, sizeof(response_buf), "Paused.");
    } else if (strcmp(cmd, "seek") == 0) {
        // get the seek arg
        json_t *sub_cmd_obj = json_object_get(root, "seek");

        if (!json_is_integer(sub_cmd_obj)) {
            fprintf(stderr, "error: time is not an integer.\n");
            json_decref(root);
            return TRUE;
        }

        // seconds for now, pass in nanoseconds later
        gint64 seek_time = json_integer_value(sub_cmd_obj) * GST_SECOND;

        gst_element_seek_simple(
            pipeline,
            GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_BEFORE,
            seek_time
        );

        snprintf(response_buf, sizeof(response_buf), "Seek to %" GST_TIME_FORMAT " successful.", GST_TIME_ARGS(seek_time));
    } else if (strcmp(cmd, "set_file") == 0) {
        // get the file arg
        json_t *sub_cmd_obj = json_object_get(root, "filename");

        if (!json_is_string(sub_cmd_obj)) {
            fprintf(stderr, "error: command is not a string\n");
            json_decref(root);
            return TRUE;
        }
    } else if (strcmp(cmd, "set_subs") == 0) {
        // get the file arg
        ;
    } else {
        snprintf(response_buf, sizeof(response_buf), "error: command %s is not valid\n", cmd);
    }

    write(client_socket, response_buf, strlen(response_buf));
    close(client_socket);
    json_decref(root);
    return TRUE;
}

static void pad_added_handler(GstElement * src, GstPad * new_pad, StreamComponents * data) {
    StreamComponents *components = (StreamComponents *)data;

    GstCaps *caps = gst_pad_query_caps(new_pad, NULL);

    GstStructure *str = gst_caps_get_structure(caps, 0);

    const gchar *new_pad_type = gst_structure_get_name(str);

    GstPad *sink_pad = NULL;

    // TODO maybe also check the suffix to make sure our codecs are good as well
    if (g_str_has_prefix(new_pad_type, "video/")) {
        sink_pad = gst_element_get_static_pad(components->video_queue, "sink");
    } else if (g_str_has_prefix(new_pad_type, "audio/")) {
        sink_pad = gst_element_get_static_pad(components->audio_queue, "sink");
    } else {
        g_print("Invalid pad type %s", new_pad_type);
        goto pad_handler_exit;
    }

    // if already linked, we're done
    if (gst_pad_is_linked (sink_pad)) {
        g_print ("We are already linked. Ignoring.\n");
        goto pad_handler_exit;
    }

    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        g_print ("Type is '%s' but link failed.\n", new_pad_type);
    }

pad_handler_exit:
    /* Unreference the new pad's caps, if we got them */
    if (caps != NULL)
        gst_caps_unref (caps);

    /* Unreference the sink pad */
    if (sink_pad)
        gst_object_unref (sink_pad);
}

int main(int argc, char *argv[]) {
    /* Set up socket stuff */
    int server_socket;
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    // make the socket
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);

    server_addr.sun_family = AF_UNIX;
    // TODO make this go in another directory
    strcpy(server_addr.sun_path, "unix_socket");

    int slen = sizeof(server_addr);
    
    // unlink before binding just in case there's one that didn't clear
    unlink(server_addr.sun_path);
    int bind_success = bind(server_socket, (struct sockaddr *) &server_addr, slen);

    if (bind_success != 0) {
        perror("Bind not successful:");
        close(server_socket);
        exit(1);
    }

    listen(server_socket, 5);

    /* Set up gstreamer stuff */
    StreamComponents components;
    GstBus *bus;
    // GstMessage *msg;

    // initialize gstreamer
    gst_init(&argc, &argv);

    // build the source, sink, filters
    components.source           = gst_element_factory_make("filesrc", "source");
    components.demux            = gst_element_factory_make("matroskademux", "demux");
    components.video_queue      = gst_element_factory_make("queue2", "video_queue");
    components.video_identity   = gst_element_factory_make("identity", "video_identity");
    components.video_parse      = gst_element_factory_make("h264parse", "video_parse");
    components.audio_queue      = gst_element_factory_make("queue2", "audio_queue");
    components.audio_identity   = gst_element_factory_make("identity", "audio_identity");
    components.audio_parse      = gst_element_factory_make("opusparse", "audio_parse");
    components.whip_sink        = gst_element_factory_make("whipclientsink", "sink");

    components.pipeline = gst_pipeline_new("main-pipeline");

    if (!components.pipeline ||
        !components.source ||
        !components.demux ||
        !components.video_queue ||
        !components.video_parse ||
        !components.audio_queue ||
        !components.audio_parse ||
        !components.whip_sink
    ) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    }

    // build the pipeline
    gst_bin_add_many(
        GST_BIN(components.pipeline), 
        components.source,
        components.demux,
        components.video_queue,
        components.video_identity,
        components.video_parse,
        components.audio_queue,
        components.audio_identity,
        components.audio_parse,
        components.whip_sink,
        NULL
    );

    // link relevant elements, not pads 
    gboolean try_link;

    try_link = gst_element_link(components.source,
                    components.demux);

    if (!try_link) {
        g_printerr ("Source and demux could not be linked.\n");
        gst_object_unref (components.pipeline);
        return -1;
    }

    try_link = gst_element_link_many(
        components.video_queue,
        components.video_identity,
        components.video_parse,
        components.whip_sink,
        NULL);

    if (!try_link) {
        g_printerr ("Video components could not be linked.\n");
        gst_object_unref (components.pipeline);
        return -1;
    }

    try_link = gst_element_link_many(
        components.audio_queue,
        components.audio_identity,
        components.audio_parse,
        components.whip_sink,
        NULL);

    if (!try_link) {
        g_printerr ("Audio components could not be linked.\n");
        gst_object_unref (components.pipeline);
        return -1;
    }
        
    // configure queues for livestreaming
    g_object_set(
        components.video_queue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", 500 * GST_MSECOND,
        "leaky", 2,
        NULL
    );
    g_object_set(
        components.audio_queue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", 500 * GST_MSECOND,
        "leaky", 2,
        NULL
    );

    // configure identity filters
    g_object_set(
        components.video_identity,
        // "sync", TRUE,
        "single-segment", TRUE,
        NULL
    );
    g_object_set(
        components.audio_identity,
        // "sync", TRUE,
        "single-segment", TRUE,
        NULL
    );

    // inject SPS/PPS
    g_object_set(
        components.video_parse,
        "config-interval", -1,
        NULL
    );

    // add async handling to whipclientsink
    gst_util_set_object_arg(
        G_OBJECT(components.whip_sink),
        "async-handling",
        "true"
    );

    // set the file 
    g_object_set(components.source, "location", "/videos/test.mkv", NULL);
    
    // set pipeline latency
    g_object_set(
        components.pipeline,
        "latency",
        500,
        NULL
    );

    // set the whip endpoint
    GObject *signaller = NULL;
    g_object_get(
        components.whip_sink,
        "signaller",
        &signaller,
        NULL
    );

    g_object_set(
        signaller,
        "whip-endpoint",
        "http://mediamtx:8889/stream/whip",
        NULL
    );

    g_object_unref(signaller);

    // set congestion control
    g_object_set(components.whip_sink, "congestion-control", 2, NULL);

    /* Connect to the pad-added signal */
    g_signal_connect(components.demux, "pad-added", G_CALLBACK (pad_added_handler), &components);

    // start playing
    gst_element_set_state(components.pipeline, GST_STATE_PLAYING);

    // add bus watch to watch for signals
    bus = gst_element_get_bus (components.pipeline);
    gst_bus_add_watch(bus, bus_callback, NULL);

    // setup io channel and callback
    GIOChannel *channel = g_io_channel_unix_new(server_socket);
    g_io_add_watch(channel, G_IO_IN, socket_callback, components.pipeline);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* Free resources */
    gst_object_unref(bus);
    gst_element_set_state(components.pipeline, GST_STATE_NULL);
    gst_object_unref(components.pipeline);

    close(server_socket);
    unlink(server_addr.sun_path);
    exit(0);
}
