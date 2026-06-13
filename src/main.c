#include "glib-object.h"
#include "glib.h"
#include "glibconfig.h"
#include "gst/gstbin.h"
#include "gst/gstbus.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstformat.h"
#include "gst/gstmessage.h"
#include "gst/gstpad.h"
#include "gst/gstpipeline.h"
#include "gst/gstsegment.h"
#include "gst/gstutils.h"
#include <jansson.h>
#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "stream_components.h"

#define SOCKET_PATH "/sockets/relay.sock"


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
    } else if (strcmp(cmd, "toggle") == 0) {
        GstState current_state, pending_state, effective_state;

        gst_element_get_state(pipeline, &current_state, &pending_state, GST_SECOND);

        if (pending_state != GST_STATE_VOID_PENDING) 
            effective_state = pending_state;
        else
            effective_state = current_state;

        if (effective_state == GST_STATE_PLAYING) {
            gst_element_set_state(pipeline, GST_STATE_PAUSED);
            snprintf(response_buf, sizeof(response_buf), "Paused.");
        } else {
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
            snprintf(response_buf, sizeof(response_buf), "Playing.");
        }

    } else if (strcmp(cmd, "seek") == 0) {
        /*
          Seek must have two more options:
          seek_time: integer in milliseconds
          seek_type: str that equals forward, backward, or absolute

          seek_type changes how seek_time works
          forward or backward moves the current position forward or backward that much
          absolute sets the current position to that time
        */

        // get the seek arg
        json_t *seek_time_json = json_object_get(root, "seek_time");
        json_t *seek_type_json = json_object_get(root, "seek_type");

        if (!json_is_integer(seek_time_json)) {
            fprintf(stderr, "error: seek time is not an integer.\n");
            json_decref(root);
            return TRUE;
        }

        if (!json_is_string(seek_type_json)) {
            fprintf(stderr, "error: seek type is not an string.\n");
            json_decref(root);
            return TRUE;
        }

        const char *seek_type = json_string_value(seek_type_json);
        gint64 seek_time = json_integer_value(seek_time_json) * GST_MSECOND;

        gint64 current_position;
        gint64 new_position;
        gst_element_query_position(pipeline, GST_FORMAT_TIME, &current_position);

        if (strcmp(seek_type, "forward") == 0) {
            new_position = current_position + seek_time;
        } else if (strcmp(seek_type, "backward") == 0) {
            new_position = current_position + seek_time;
        } else if (strcmp(seek_type, "absolute") == 0) {
            new_position = seek_time;
        } else {
            g_printerr("Invalid seek type.");
        }

        gint64 duration;
        gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration);

        // cap to duration
        if (new_position > duration)
            new_position = duration;

        // cap to 0
        if (new_position < 0)
            new_position = 0;

        gst_element_seek_simple(
            pipeline,
            GST_FORMAT_TIME,
            GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_BEFORE,
            new_position
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
    } else if (strcmp(cmd, "get_play_state") == 0) {
        GstState current_state;
        GstState pending_state;

        gst_element_get_state(pipeline, &current_state, &pending_state, GST_SECOND);
    } else {
        snprintf(response_buf, sizeof(response_buf), "error: command %s is not valid\n", cmd);
    }

    write(client_socket, response_buf, strlen(response_buf));
    close(client_socket);
    json_decref(root);
    return TRUE;
}

static GstClockTime last_pts = GST_CLOCK_TIME_NONE;
static GstPadProbeReturn pts_probe(
    GstPad *pad,
    GstPadProbeInfo *info,
    gpointer user_data)
{
    if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER))
        return GST_PAD_PROBE_OK;

    GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
    GstClockTime pts = GST_BUFFER_PTS(buf);

    if (last_pts != GST_CLOCK_TIME_NONE &&
        pts < last_pts)
    {
        g_print(
            "PTS WENT BACKWARDS! old=%" GST_TIME_FORMAT
            " new=%" GST_TIME_FORMAT "\n",
            GST_TIME_ARGS(last_pts),
            GST_TIME_ARGS(pts));
    }

    last_pts = pts;

    return GST_PAD_PROBE_OK;
}

static void on_consumer_pipeline_created(
    GstElement *sink,
    gchar *consumer_id,
    GstPipeline *pipeline,
    gpointer user_data)
{
    g_print("Consumer pipeline created: %s\n", consumer_id);

    GstIterator *it =
        gst_bin_iterate_elements(GST_BIN(pipeline));

    GValue item = G_VALUE_INIT;

    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK)
    {
        GstElement *e = GST_ELEMENT(g_value_get_object(&item));

        g_print(
            "  %s (%s)\n",
            GST_ELEMENT_NAME(e),
            G_OBJECT_TYPE_NAME(e));

        g_value_reset(&item);
    }

    gst_iterator_free(it);
}

static GstPadProbeReturn segment_probe(
    GstPad *pad,
    GstPadProbeInfo *info,
    gpointer user_data)
{
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM)
    {
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);

        switch (GST_EVENT_TYPE(event))
        {
        case GST_EVENT_SEGMENT:
        {
            const GstSegment *seg;
            gst_event_parse_segment(event, &seg);

            g_print(
                "SEGMENT: start=%" GST_TIME_FORMAT
                " time=%" GST_TIME_FORMAT   
                " base=%" GST_TIME_FORMAT
                "\n",
                GST_TIME_ARGS(seg->start),
                GST_TIME_ARGS(seg->time),
                GST_TIME_ARGS(seg->base));
            break;
        }

        case GST_EVENT_FLUSH_START:
            g_print("FLUSH_START\n");
            break;

        case GST_EVENT_FLUSH_STOP:
            g_print("FLUSH_STOP\n");
            break;

        default:
            break;
        }
    }

    return GST_PAD_PROBE_OK;
}


int main(int argc, char *argv[]) {
    /* Set up socket stuff */
    int server_socket;
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    // make the socket
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);

    server_addr.sun_family = AF_UNIX;
    strcpy(server_addr.sun_path, SOCKET_PATH);

    int slen = sizeof(server_addr);
    
    // unlink before binding just in case there's one that didn't clear
    unlink(server_addr.sun_path);
    int bind_success = bind(server_socket, (struct sockaddr *) &server_addr, slen);

    // may need to chmod the socket
    chmod(SOCKET_PATH, 0666);

    if (bind_success != 0) {
        perror("Bind not successful: ");
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

    setup_stream_components(&components);

    g_object_set(
        components.whip_sink,
        "message-forward",
        TRUE,
        NULL);

    GstPad *segment_pad =
        gst_element_get_static_pad(
            components.video_parse,
            "sink");

    gst_pad_add_probe(
        segment_pad,
        GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
        segment_probe,
        NULL,
        NULL);

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
