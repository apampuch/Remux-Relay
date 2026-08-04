#ifndef STREAM_COMPONENTS_H
#define STREAM_COMPONENTS_H

#include "gst/gstelement.h"
typedef struct _StreamComponents {
    GstElement *pipeline, *source, *demux;
    GstElement *video_queue;
    GstElement *video_identity;
    GstElement *video_parse;
    GstElement *video_rate;
    GstElement *audio_queue;
    GstElement *audio_identity;
    GstElement *audio_parse;
    GstElement *whip_sink;
} StreamComponents;

int setup_pipeline(StreamComponents *components, char *filename);

#endif
