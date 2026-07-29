
#include "gst/gstpipeline.h"
#include "gst/gstutils.h"

#include "stream_components.h"

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
        g_print("Invalid pad type %s\n", new_pad_type);
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

int setup_stream_components(StreamComponents *components) {
    // build the source, sink, filters
    components->source          = gst_element_factory_make("filesrc", "source");
    components->demux           = gst_element_factory_make("qtdemux", "demux");
    components->video_queue     = gst_element_factory_make("queue2", "video_queue");
    components->video_identity  = gst_element_factory_make("identity", "video_identity");
    components->video_parse     = gst_element_factory_make("h264parse", "video_parse");
    components->video_rate      = gst_element_factory_make("videorate", "video_rate");
    components->audio_queue     = gst_element_factory_make("queue2", "audio_queue");
    components->audio_identity  = gst_element_factory_make("identity", "audio_identity");
    components->audio_parse     = gst_element_factory_make("opusparse", "audio_parse");
    components->whip_sink       = gst_element_factory_make("whipclientsink", "sink");

    components->pipeline = gst_pipeline_new("main-pipeline");

    if (!components->pipeline       ||
        !components->source         ||
        !components->demux          ||
        !components->video_queue    ||
        !components->video_identity ||
        !components->video_parse    ||
        !components->video_rate     ||
        !components->audio_queue    ||
        !components->audio_identity ||
        !components->audio_parse    ||
        !components->whip_sink
    ) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    }

    // build the pipeline
    gst_bin_add_many(
        GST_BIN(components->pipeline), 
        components->source,
        components->demux,
        components->video_queue,
        components->video_identity,
        components->video_parse,
        components->video_rate,
        components->audio_queue,
        components->audio_identity,
        components->audio_parse,
        components->whip_sink,
        NULL
    );

    // link relevant elements, not pads 
    gboolean try_link;

    try_link = gst_element_link(components->source,
                    components->demux);

    if (!try_link) {
        g_printerr ("Source and demux could not be linked.\n");
        gst_object_unref (components->pipeline);
        return -1;
    }

    try_link = gst_element_link_many(
        components->video_queue,
        components->video_identity,
        components->video_parse,
        components->whip_sink,
        NULL);

    if (!try_link) {
        g_printerr ("Video components could not be linked.\n");
        gst_object_unref (components->pipeline);
        return -1;
    }

    try_link = gst_element_link_many(
        components->audio_queue,
        components->audio_identity,
        components->audio_parse,
        components->whip_sink,
        NULL);

    if (!try_link) {
        g_printerr ("Audio components could not be linked.\n");
        gst_object_unref (components->pipeline);
        return -1;
    }
        
    // configure queues for livestreaming
    g_object_set(
        components->video_queue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", 500 * GST_MSECOND,
        // "leaky", 2,  // apparently queue2 doesn't have a leaky property
        NULL
    );
    g_object_set(
        components->audio_queue,
        "max-size-buffers", 0,
        "max-size-bytes", 0,
        "max-size-time", 500 * GST_MSECOND,
        NULL
    );

    // configure identity filters
    g_object_set(
        components->video_identity,
        "sync", TRUE,
        "signal-handoffs", FALSE,
        // "single-segment", TRUE,
        NULL
    );
    g_object_set(
        components->audio_identity,
        "sync", TRUE,
        "signal-handoffs", FALSE,
        // "single-segment", TRUE,
        NULL
    );

    // inject SPS/PPS
    g_object_set(
        components->video_parse,
        "config-interval", 1,
        NULL
    );

    // make it drop duplicate frames to maintain a constant rate
    g_object_set(components->video_rate, "drop-only", TRUE, NULL);

    // add async handling to whipclientsink
    gst_util_set_object_arg(
        G_OBJECT(components->whip_sink),
        "async-handling",
        "true"
    );

    // fix bitrates for whip sink
    // TODO make it possible to set this in docker somehow
    g_object_set(components->whip_sink,
        "max-bitrate", 12000000,
        NULL); // 12 Mbps ceiling

    g_object_set(components->whip_sink,
        "start-bitrate", 10000000,
        NULL); // start near actual rate

    // set the file 
    g_object_set(components->source, "location", "/videos/test.mp4", NULL);
    
    // set pipeline latency
    g_object_set(
        components->pipeline,
        "latency",
        50 * GST_MSECOND,
        NULL
    );

    // set the whip endpoint
    GObject *signaller = NULL;
    g_object_get(
        components->whip_sink,
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
    g_object_set(components->whip_sink, "congestion-control", 0, NULL);

    /* Connect to the pad-added signal */
    g_signal_connect(components->demux, "pad-added", G_CALLBACK (pad_added_handler), components);

    return 0;
}
