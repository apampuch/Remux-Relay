#include "debug_probes.h"

#include "glib.h"
#include "gst/gstclock.h"
#include "gst/gstpad.h"
#include "gst/gstpipeline.h"

// static GstClockTime last_pts = GST_CLOCK_TIME_NONE;
// static GstPadProbeReturn pts_probe(
//     GstPad *pad,
//     GstPadProbeInfo *info,
//     gpointer user_data)
// {
//     if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER))
//         return GST_PAD_PROBE_OK;

//     GstBuffer *buf = GST_PAD_PROBE_INFO_BUFFER(info);
//     GstClockTime pts = GST_BUFFER_PTS(buf);

//     if (last_pts != GST_CLOCK_TIME_NONE &&
//         pts < last_pts)
//     {
//         g_print(
//             "PTS WENT BACKWARDS! old=%" GST_TIME_FORMAT
//             " new=%" GST_TIME_FORMAT "\n",
//             GST_TIME_ARGS(last_pts),
//             GST_TIME_ARGS(pts));
//     }

//     last_pts = pts;

//     return GST_PAD_PROBE_OK;
// }

static void on_consumer_pipeline_created(
    GstElement *sink,
    gchar *consumer_id,
    GstPipeline *pipeline,
    gpointer user_data)
{
    static int consumer_count = 0;

    consumer_count++;

    g_print(
        "Consumer pipeline #%d created: %s\n",
        consumer_count,
        consumer_id);

    g_print(
        "pipeline ptr=%p type=%s\n",
        pipeline,
        G_OBJECT_TYPE_NAME(pipeline));

    g_print("Starting recurse\n");

    GstIterator *it =
        gst_bin_iterate_recurse(GST_BIN(pipeline));

    g_print("Iterator=%p\n", it);

    GValue item = G_VALUE_INIT;

    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK)
    {
        g_print("Iterator item\n");
        GstElement *e = GST_ELEMENT(g_value_get_object(&item));

        g_print(
            "ELEMENT: %s (%s)\n",
            GST_ELEMENT_NAME(e),
            G_OBJECT_TYPE_NAME(e));

        g_value_reset(&item);
    }
    g_print("Iterator done\n");

    g_print(
        "Num children: %u\n",
        GST_BIN_NUMCHILDREN(GST_BIN(pipeline)));

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

int add_probes(StreamComponents *components) {
    // // add pad probe 
    // GstPad *pts_pad = gst_element_get_static_pad(components->video_parse, "src");
    // gst_pad_add_probe(
    //     pts_pad,
    //     GST_PAD_PROBE_TYPE_BUFFER,
    //     pts_probe,
    //     NULL,
    //     NULL);

    g_signal_connect(
        components->whip_sink,
        "consumer-pipeline-created",
        G_CALLBACK(on_consumer_pipeline_created),
        NULL);

    // make whip sink forward child messages
    g_object_set(
        components->whip_sink,
        "message-forward",
        TRUE,
        NULL);

    // GstPad *segment_src_pad =
    //     gst_element_get_static_pad(
    //         components->video_parse,
    //         "src");

    // gst_pad_add_probe(
    //     segment_src_pad,
    //     GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
    //     segment_probe,
    //     NULL,
    //     NULL);

    // GstPad *segment_sink_pad =
    //     gst_element_get_static_pad(
    //         components->video_parse,
    //         "sink");

    // gst_pad_add_probe(
    //     segment_sink_pad,
    //     GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
    //     segment_probe,
    //     NULL,
    //     NULL);

    return 0;
}
