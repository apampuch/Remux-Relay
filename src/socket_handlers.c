#include "glib.h"
#include "glibconfig.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstformat.h"
#include "gst/gstsegment.h"
#include "gst/gstutils.h"
#include <jansson.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "socket_handlers.h"

#define SOCKET_PATH "/sockets/relay.sock"

static gboolean accept_callback(GIOChannel *source, GIOCondition condition, gpointer data);
static gboolean client_callback(GIOChannel *source, GIOCondition condition, gpointer data);

struct sockaddr_un server_addr;
int server_socket;

void setup_socket(StreamComponents components) {
    // zero the memory
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

    // setup io channel and callback
    GIOChannel *channel = g_io_channel_unix_new(server_socket);
    g_io_add_watch(channel, G_IO_IN, accept_callback, components.pipeline);

    listen(server_socket, 5);
}

void close_socket() {
    close(server_socket);
    unlink(server_addr.sun_path);
}

static gboolean accept_callback(GIOChannel *source, GIOCondition condition, gpointer data) {
    // setup client socket

    // keeping all this in case it's possible to just connect directly to this without the bridge
    int client_socket;
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t clen = sizeof(client_addr);

    int fd = g_io_channel_unix_get_fd(source);
    client_socket = accept(fd, (struct sockaddr *) &client_addr, &clen);

    GIOChannel *client_channel = g_io_channel_unix_new(client_socket);

    g_io_add_watch(
        client_channel,
        G_IO_IN | G_IO_HUP | G_IO_ERR,
        client_callback,
        data
    );

    g_print("Accepted client connection.\n");

    return TRUE;
}

static gboolean client_callback(GIOChannel *source, GIOCondition condition, gpointer data) {
    // return FALSE removes it from the loop
    if (condition & G_IO_HUP) {
        fprintf(stderr, "Client disconnected.\n");
        return FALSE;
    } else if (condition & G_IO_ERR) {
        fprintf(stderr, "Client error.");
        return FALSE;
    }

    // anything else is G_IO_IN, so read from the socket
    GError *socket_error = NULL;
    gchar *line = NULL;
    gsize line_len;

    GIOStatus status = g_io_channel_read_line(source,
        &line,
        &line_len,
        NULL,
        &socket_error);

    if (status == G_IO_STATUS_ERROR) {
        fprintf(stderr, "Message error.\n");
        return TRUE;
    }

    // we might not have a full line with a newline at the end
    // maybe check for that

    GstElement *pipeline = data;
    json_t *root;
    json_error_t json_error;

    // load json
    root = json_loads(line, 0, &json_error);

    // check json for errors
    if (!root)
    {
        fprintf(stderr, "Error with json:\n%s", line);
        return TRUE;
    }

    if (!json_is_object(root)) {
        fprintf(stderr, "error: root is not an array\n");
        json_decref(root);
        return TRUE;
    }
    
    // debug print to make sure we got it
    g_print("%s\n", json_dumps(root, JSON_COMPACT));

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

        // WebRTC basically won't let us seek while paused, so we set the state to playing after a seek
        GstState current_state, pending_state, effective_state;

        gst_element_get_state(pipeline, &current_state, &pending_state, GST_SECOND);

        if (current_state != GST_STATE_PLAYING) {
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
        }

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
            GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_BEFORE,
            new_position
        );

        g_print("Seek to %" GST_TIME_FORMAT " successful.\n", GST_TIME_ARGS(seek_time));
        snprintf(response_buf, sizeof(response_buf), "Seek to %" GST_TIME_FORMAT " successful.\n", GST_TIME_ARGS(seek_time));
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

    // append newline
    strcat(response_buf, "\n");

    GError *write_error = NULL;
    gsize bytes_written;

    status = g_io_channel_write_chars(
        source,
        response_buf,
        -1,
        &bytes_written,
        &write_error
    );

    // how tf do I handle write errors, if at all
    if (write_error) {
        fprintf(stderr,"Error writing message to socket.");
    }

    g_io_channel_flush(source, &write_error );


    // write(client_socket, response_buf, strlen(response_buf));
    json_decref(root);
    return TRUE;
}
