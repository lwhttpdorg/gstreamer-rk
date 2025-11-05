#pragma once

#include <gst/gst.h>
#include "gstadaptivedemux-stream.h"
#include "movpkg-streaminfo.h"
#include "hls/m3u8.h"

G_BEGIN_DECLS

#define GST_TYPE_MOVPKG_DEMUX_STREAM (gst_movpkg_demux_stream_get_type())
G_DECLARE_FINAL_TYPE (GstMovpkgDemuxStream, gst_movpkg_demux_stream,
    GST, MOVPKG_DEMUX_STREAM, GstAdaptiveDemux2Stream)
#define GST_MOVPKG_DEMUX_STREAM_CAST(obj) ((GstMovpkgDemuxStream *) (obj))

GstAdaptiveDemux2Stream *
gst_movpkg_demux_stream_new (const gchar                   * name,
                             const gchar                   * base_path,
                             GstMovpkgPersistentStreamInfo * info,
                             GstHLSRenditionStream         * rendition);

GstClockTime
gst_movpkg_demux_stream_get_duration (GstMovpkgDemuxStream * self);

G_END_DECLS
