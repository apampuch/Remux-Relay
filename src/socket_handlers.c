#include "glib.h"
#include "glibconfig.h"
#include "gst/gstclock.h"
#include "gst/gstelement.h"
#include "gst/gstformat.h"
#include "gst/gstsegment.h"
#include "gst/gstutils.h"
#include "dirent.h"
#include "stream_components.h"
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
static gboolean playing_position_update(gpointer data);

struct sockaddr_un server_addr;
int server_socket;

typedef struct {
    StreamComponents *components;
    GIOChannel *client_channel;
} ClientData;

// eventually add mkv and maybe webm to this when support for those is added
const char *VALID_VIDEO_TYPES[] = {"mp4"};
const int VALID_TYPES_LEN = 1;

gboolean ends_with(const char *str, const char *suffix)
{
    size_t len = strlen(str);
    size_t suffix_len = strlen(suffix);

    if (suffix_len > len)
        return FALSE;

    return strcmp(str + len - suffix_len, suffix) == 0;
}

void setup_socket(StreamComponents *components) {
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
    g_io_add_watch(channel, G_IO_IN, accept_callback, components);

    listen(server_socket, 5);
}

void close_socket() {
    close(server_socket);
    unlink(server_addr.sun_path);
}

// returns TRUE if the json has an id property and if its value is a number
// returns FALSE if not
gboolean check_for_id(json_t *j) {
    json_t *val = json_object_get(j, "id");

    if (!val) {
        return FALSE;
    } else if (!json_is_number(val)) {
        return FALSE;
    } else {
        return TRUE;
    }
}

static gboolean accept_callback(GIOChannel *source, GIOCondition condition, gpointer data) {
    // make the ClientData struct to pass multiple pointers in
    ClientData *cd = calloc(1, sizeof(*cd));

    cd->components = (StreamComponents*) data;

    // keeping all this in case it's possible to just connect directly to this without the bridge
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(client_addr));
    socklen_t clen = sizeof(client_addr);

    int fd = g_io_channel_unix_get_fd(source);
    int client_fd = accept(fd, (struct sockaddr *) &client_addr, &clen);

    cd->client_channel = g_io_channel_unix_new(client_fd);

    g_io_add_watch(
        cd->client_channel,
        G_IO_IN | G_IO_HUP | G_IO_ERR,
        client_callback,
        data
    );

    g_timeout_add(
        500, 
        playing_position_update, 
        cd
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

    StreamComponents *components = data;
    GstElement *pipeline = components->pipeline;
    json_t *root;
    json_error_t json_error;

    // load json
    root = json_loads(line, 0, &json_error);

    // g_print("Incoming json: %s\n", line);

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
    // g_print("%s\n", json_dumps(root, JSON_COMPACT));

    // parse the actual command
    json_t *cmd_obj = json_object_get(root, "command");

    if (!json_is_string(cmd_obj)) {
        fprintf(stderr, "error: command is not a string.\n");
        json_decref(root);
        return TRUE;
    }

    const char *cmd = json_string_value(cmd_obj);
    char response_buf[256];

    // get the id check out of the way
    if (!check_for_id(root)) {
        fprintf(stderr, "Toggle json has no id element.");
        return TRUE;
    }

    gint64 id = json_integer_value(json_object_get(root, "id"));

    /* find the command */
    if (strcmp(cmd, "play") == 0) {
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        snprintf(response_buf, sizeof(response_buf), "Playing.");
    } else if (strcmp(cmd, "pause") == 0) {
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        snprintf(response_buf, sizeof(response_buf), "Paused.");
    } else if (strcmp(cmd, "get_duration") == 0) {
        gint64 duration = 0;

        gst_element_query_duration(pipeline, GST_FORMAT_TIME, &duration);

        // convert to milliseconds
        duration /= GST_MSECOND;

        snprintf(response_buf, sizeof(response_buf), "{\"id\": %ld, \"stream_duration\": %ld}", id, duration);
    } else if (strcmp(cmd, "toggle") == 0) {
        GstState current_state, pending_state, effective_state;

        gst_element_get_state(pipeline, &current_state, &pending_state, GST_SECOND);

        if (pending_state != GST_STATE_VOID_PENDING) 
            effective_state = pending_state;
        else
            effective_state = current_state;

        if (effective_state == GST_STATE_PLAYING) {
            gst_element_set_state(pipeline, GST_STATE_PAUSED);
            snprintf(response_buf, sizeof(response_buf), "{\"id\": %ld, \"new_state\": \"paused\"}", id);
        } else {
            gst_element_set_state(pipeline, GST_STATE_PLAYING);
            snprintf(response_buf, sizeof(response_buf), "{\"id\": %ld, \"new_state\": \"playing\"}", id);
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

        // printf("%u\n", current_state);

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
            GST_SEEK_FLAG_KEY_UNIT | GST_SEEK_FLAG_SNAP_BEFORE | GST_SEEK_FLAG_SEGMENT,
            new_position
        );

        // convert seek time to back milliseconds before sending
        seek_time /= GST_MSECOND;

        snprintf(response_buf, sizeof(response_buf), "{\"id\": %ld, \"seek_pos\": \"%ld\"}", id, seek_time);
    } else if (strcmp(cmd, "set_file") == 0) {
        // get the file arg
        json_t *sub_cmd_obj = json_object_get(root, "filename");

        if (!json_is_string(sub_cmd_obj)) {
            fprintf(stderr, "error: command is not a string\n");
            json_decref(root);
            return TRUE;
        }

        // TODO send the duration of the new file

    } else if (strcmp(cmd, "list_files") == 0) {
        // get all the files and send their names in a json array
        json_t *response_root = json_object();
        json_t *paths = json_array();
        json_object_set_new(response_root, "id", json_integer(id));

        // open videos folder
        DIR *videos_dir = opendir("/videos");

        if (videos_dir == NULL) {
            perror("Could not open videos directory.");
            return 1;
        }

        struct dirent *entry;

        // iterate through dir, filter for files with the correct extensions
        while ((entry = readdir(videos_dir)) != NULL) {
            for (int i=0; i<VALID_TYPES_LEN; i++) {
                if (ends_with(entry->d_name, VALID_VIDEO_TYPES[i])) {
                    json_array_append_new(paths, json_string(entry->d_name));
                }
            }
        }

        // send to the json and response buffer
        json_object_set_new(response_root, "paths", paths);

        char *dump = json_dumps(response_root, 0);
        snprintf(response_buf, sizeof(response_buf), "%s", dump);
        free(dump);

        json_decref(paths);
        json_decref(response_root);
    } else if (strcmp(cmd, "get_play_state") == 0) {
        // this seems to be unused? it doesn't return anything
        GstState current_state;
        GstState pending_state;

        gst_element_get_state(pipeline, &current_state, &pending_state, GST_SECOND);
    } else {
        snprintf(response_buf, sizeof(response_buf), "{\"id\": %ld, \"error\": \"command %s is not valid\"}", id, cmd);
    }

    GError *write_error = NULL;
    gsize bytes_written;

    // write the response to the socket
    status = g_io_channel_write_chars(
        source,
        response_buf,
        -1,
        &bytes_written,
        &write_error
    );

    // how tf do I handle write errors, if at all
    if (write_error) {
        fprintf(stderr, "Error writing message to socket.");
    }

    g_io_channel_flush(source, &write_error );

    // write(client_socket, response_buf, strlen(response_buf));
    json_decref(root);
    return TRUE;
}

static gboolean playing_position_update(gpointer data) {
    ClientData *cd = (ClientData*) data;

    gint64 play_pos;
    char response_buf[256];

    // get the timestamp
    gst_element_query_position(cd->components->pipeline, GST_FORMAT_TIME, &play_pos);

    // convert to milliseconds
    play_pos /= GST_MSECOND;

    // build the json
    json_t *root = json_object();
    json_object_set_new(root, "update_type", json_string("position"));
    json_object_set_new(root, "new_position", json_integer(play_pos));

    snprintf(response_buf, sizeof(response_buf), "%s", json_dumps(root, JSON_COMPACT));

    GError *write_error = NULL;
    gsize bytes_written;
    
    GIOStatus status = g_io_channel_write_chars(
        cd->client_channel,
        response_buf,
        -1,
        &bytes_written,
        &write_error
    );

    if (write_error) {
        fprintf(stderr,"Socket write error: %s\n", write_error->message);
    }

    GError *flush_error = NULL;
    g_io_channel_flush(cd->client_channel, &write_error);

    if (flush_error) {
        fprintf(stderr, "Flush error: %s\n", flush_error->message);
    }

    // g_print(
    // "write status=%d bytes=%zu\n",
    //     status,
    //     bytes_written
    // );

    json_decref(root);

    return TRUE;
}
