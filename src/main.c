#include "glib.h"
#include "glibconfig.h"
#include "gst/gstbus.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstformat.h"
#include "gst/gstmessage.h"
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

#include "debug_probes.h"
#include "stream_components.h"

#define SOCKET_PATH "/sockets/relay.sock"

static gboolean
debug_timer(gpointer user_data)
{
    GstElement *pipeline = GST_ELEMENT(user_data);

    gint64 pos = GST_CLOCK_TIME_NONE;

    if (gst_element_query_position(
            pipeline,
            GST_FORMAT_TIME,
            &pos))
    {
        g_print("Position: %" GST_TIME_FORMAT "\n",
                GST_TIME_ARGS(pos));
    } else {
        g_print("Could not poll position.\n");
    }


    return G_SOURCE_CONTINUE;  // keep running
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
            new_position = current_position - seek_time;
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
    add_probes(&components);

    // start playing
    gst_element_set_state(components.pipeline, GST_STATE_PLAYING);

    // add bus watch to watch for signals
    bus = gst_element_get_bus (components.pipeline);
    gst_bus_add_watch(bus, bus_callback, NULL);

    // setup io channel and callback
    GIOChannel *channel = g_io_channel_unix_new(server_socket);
    g_io_add_watch(channel, G_IO_IN, socket_callback, components.pipeline);

    // g_timeout_add(100, debug_timer, components.pipeline);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* Free resources */
    gst_object_unref(bus);
    gst_element_set_state(components.pipeline, GST_STATE_NULL);
    gst_object_unref(components.pipeline);

    close(server_socket);
    unlink(server_addr.sun_path);

    g_print("Exiting.\n");
    exit(0);
}
