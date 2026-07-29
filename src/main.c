#include "glib.h"
#include "glibconfig.h"
#include "gst/gstbus.h"
// #include "gst/gstclock.h"
#include "gst/gstelement.h"
// #include "gst/gstformat.h"
#include "gst/gstmessage.h"
// #include "gst/gstutils.h"
#include <gst/gst.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug_probes.h"
#include "stream_components.h"
#include "socket_handlers.h"

// static gboolean debug_timer(gpointer user_data)
// {
//     GstElement *pipeline = GST_ELEMENT(user_data);

//     gint64 pos = GST_CLOCK_TIME_NONE;

//     if (gst_element_query_position(
//             pipeline,
//             GST_FORMAT_TIME,
//             &pos))
//     {
//         g_print("Position: %" GST_TIME_FORMAT "\n",
//                 GST_TIME_ARGS(pos));
//     } else {
//         g_print("Could not poll position.\n");
//     }


//     return G_SOURCE_CONTINUE;  // keep running
// }

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


int main(int argc, char *argv[]) {
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

    // g_timeout_add(100, debug_timer, components.pipeline);
    
    /* Set up socket stuff */
    setup_socket(components);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* Free resources */
    gst_object_unref(bus);
    gst_element_set_state(components.pipeline, GST_STATE_NULL);
    gst_object_unref(components.pipeline);


    close_socket();
    g_print("Exiting.\n");
    exit(0);
}
