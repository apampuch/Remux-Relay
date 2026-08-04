#include "glib.h"
#include "glibconfig.h"
// #include "gst/gstclock.h"
#include "gst/gstelement.h"
// #include "gst/gstformat.h"
// #include "gst/gstmessage.h"
// #include "gst/gstutils.h"
#include <stddef.h>
#include <gst/gst.h>
// #include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// #include "debug_probes.h"
#include "stream_components.h"
#include "socket_handlers.h"

// static gboolean debug_timer(gpointer user_data)
// {
//     GstElement *pipeline = GST_ELEMENT(user_data);
//
//     gint64 pos = GST_CLOCK_TIME_NONE;
//
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
//
//
//     return G_SOURCE_CONTINUE;  // keep running
// }




int main(int argc, char *argv[]) {
    /* Set up gstreamer stuff */
    StreamComponents *components = calloc(1, sizeof(StreamComponents));
    // GstMessage *msg;

    // initialize gstreamer
    gst_init(&argc, &argv);

    // setup_pipeline(&components);
    // add_probes(&components);

    // g_timeout_add(100, debug_timer, components.pipeline);

    /* Set up socket stuff */
    setup_socket(components);

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(loop);

    /* Free resources */
    gst_element_set_state(components->pipeline, GST_STATE_NULL);
    g_clear_object(&components->pipeline);
    // gst_object_unref(components->pipeline);

    close_socket();
    g_print("Exiting.\n");
    exit(0);
}
