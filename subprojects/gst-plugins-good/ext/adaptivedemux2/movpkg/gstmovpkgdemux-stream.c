/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Copyright (C) 2024-2025 Collabora Ltd.
 *  @author: Jordan Yelloz <jordan.yelloz@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstadaptivedemux.h"
#include "gstmovpkgdemux-stream.h"
#include "movpkg-streaminfo.h"
#include "hls/m3u8.h"

#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_EXTERN (gst_movpkg_demux_debug);
#define GST_CAT_DEFAULT gst_movpkg_demux_debug

struct _GstMovpkgDemuxStream
{
  GstAdaptiveDemux2Stream parent;
  gchar *base_uri;
  GstMovpkgPersistentStreamInfo *info;
  GPtrArray *fragments;
  gsize max_fragment_bitrate;
  gsize total_size;
  guint current_fragment;
  GstClockTime duration;
  GstHLSRenditionStream *rendition;
};

#define GST_MOVPKG_DEMUX_STREAM_IS_FORWARD(s) \
    ((GST_ADAPTIVE_DEMUX2_STREAM_CAST (s)->demux->segment.rate > 0))

#define GST_MOVPKG_DEMUX_STREAM_DIRECTION_NAME(s) \
    (GST_MOVPKG_DEMUX_STREAM_IS_FORWARD (s) ? "forward" : "backward")

static GstFlowReturn
gst_movpkg_demux_stream_advance_fragment (GstAdaptiveDemux2Stream * stream);
static GstFlowReturn
gst_movpkg_demux_stream_data_received (GstAdaptiveDemux2Stream * stream,
    GstBuffer * buffer);
static GstFlowReturn
gst_movpkg_demux_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream);
static void gst_movpkg_demux_stream_create_tracks (GstAdaptiveDemux2Stream *
    stream);
static GstFlowReturn
gst_movpkg_demux_stream_seek (GstAdaptiveDemux2Stream * stream,
    gboolean forward, GstSeekFlags flags, GstClockTimeDiff ts,
    GstClockTimeDiff * final_ts);

static void gst_movpkg_demux_stream_finalize (GObject * object);

#define gst_movpkg_demux_stream_parent_class parent_class

G_DEFINE_TYPE (GstMovpkgDemuxStream, gst_movpkg_demux_stream,
    GST_TYPE_ADAPTIVE_DEMUX2_STREAM);

static void
gst_movpkg_demux_stream_class_init (GstMovpkgDemuxStreamClass * klass)
{
  GObjectClass *oclass = (GObjectClass *) klass;
  GstAdaptiveDemux2StreamClass *adsclass =
      GST_ADAPTIVE_DEMUX2_STREAM_CLASS (klass);

  oclass->finalize = gst_movpkg_demux_stream_finalize;

  adsclass->update_fragment_info = gst_movpkg_demux_stream_update_fragment_info;
  adsclass->create_tracks = gst_movpkg_demux_stream_create_tracks;

  adsclass->advance_fragment = gst_movpkg_demux_stream_advance_fragment;
  adsclass->stream_seek = gst_movpkg_demux_stream_seek;
  adsclass->data_received = gst_movpkg_demux_stream_data_received;
}

static void
gst_movpkg_demux_stream_init (GstMovpkgDemuxStream * stream)
{
  stream->current_fragment = 0;
  stream->fragments = g_ptr_array_new ();
  stream->duration = GST_CLOCK_TIME_NONE;
}

static guint
compute_bitrate (gsize byte_count, GstClockTime duration)
{
  return gst_util_uint64_scale_round (byte_count * 8, GST_SECOND, duration);
}

static void
gst_movpkg_demux_stream_create_tracks (GstAdaptiveDemux2Stream * stream)
{
  GstMovpkgDemuxStream *self = GST_MOVPKG_DEMUX_STREAM (stream);
  GstHLSRenditionStream *rendition = self->rendition;

  GstTagList *rendition_tags = gst_tag_list_new_empty ();
  if (rendition) {
    const gchar *language_code =
        rendition->lang ? gst_tag_get_language_code (rendition->lang) : NULL;
    if (language_code) {
      gst_tag_list_add (rendition_tags, GST_TAG_MERGE_APPEND,
          GST_TAG_LANGUAGE_CODE, language_code, NULL);
    } else if (rendition->lang) {
      gst_tag_list_add (rendition_tags, GST_TAG_MERGE_APPEND,
          GST_TAG_LANGUAGE_CODE, rendition->lang, NULL);
    }
    if (rendition->name) {
      gst_tag_list_add (rendition_tags, GST_TAG_MERGE_APPEND, GST_TAG_TITLE,
          rendition->name, NULL);
    }
  }

  GstCaps *stream_caps = gst_caps_new_empty ();
  guint n_streams = gst_stream_collection_get_size (stream->stream_collection);
  for (guint i = 0; i < n_streams; i++) {
    GstStream *gst_stream =
        gst_stream_collection_get_stream (stream->stream_collection, i);
    GstStreamType stream_type = gst_stream_get_stream_type (gst_stream);
    const gchar *upstream_stream_id = gst_stream_get_stream_id (gst_stream);
    const gchar *stream_type_name = gst_stream_type_get_name (stream_type);
    gchar *stream_id = g_strdup_printf ("%s-%d-%s", stream_type_name, i,
        GST_OBJECT_NAME (stream));

    GstCaps *caps = gst_stream_get_caps (gst_stream);
    gst_caps_append (stream_caps, gst_caps_copy (caps));

    GstTagList *stream_tags = gst_stream_get_tags (gst_stream);
    GstTagList *tags = gst_tag_list_merge (stream_tags, rendition_tags,
        GST_TAG_MERGE_KEEP);
    gst_tag_list_add (tags, GST_TAG_MERGE_APPEND,
        GST_TAG_BITRATE, compute_bitrate (self->total_size, self->duration),
        GST_TAG_MAXIMUM_BITRATE, (guint) self->max_fragment_bitrate, NULL);

    GstStreamFlags flags = stream_type == GST_STREAM_TYPE_TEXT ?
        GST_STREAM_FLAG_SPARSE : GST_STREAM_FLAG_NONE;
    GstAdaptiveDemuxTrack *track = gst_adaptive_demux_track_new (stream->demux,
        stream_type, flags, stream_id, caps, tags);
    track->upstream_stream_id = g_strdup (upstream_stream_id);
    if (!gst_adaptive_demux2_stream_add_track (stream, track)) {
      GST_ERROR_OBJECT (stream, "failed to add track");
    }

    stream->stream_type |= stream_type;

    gst_clear_tag_list (&stream_tags);
    g_clear_pointer (&track, gst_adaptive_demux_track_unref);
    g_clear_pointer (&stream_id, g_free);
  }

  gst_adaptive_demux2_stream_set_caps (stream, stream_caps);
  gst_adaptive_demux2_stream_set_tags (stream, rendition_tags);
}

static GstFlowReturn
gst_movpkg_demux_stream_advance_fragment (GstAdaptiveDemux2Stream * stream)
{
  GstMovpkgDemuxStream *self = GST_MOVPKG_DEMUX_STREAM_CAST (stream);
  gboolean forward = GST_MOVPKG_DEMUX_STREAM_IS_FORWARD (stream);
  GST_LOG_OBJECT (self, "advance: %s %d",
      GST_MOVPKG_DEMUX_STREAM_DIRECTION_NAME (stream), self->current_fragment);
  if (forward) {
    if (self->current_fragment >= self->fragments->len) {
      GST_LOG_OBJECT (self, "no more fragments at +%d", self->current_fragment);
      return GST_FLOW_EOS;
    } else {
      self->current_fragment++;
      return GST_FLOW_OK;
    }
  } else {
    if (self->current_fragment <= 0) {
      GST_LOG_OBJECT (self, "no more fragments at -%d", self->current_fragment);
      return GST_FLOW_EOS;
    } else {
      self->current_fragment--;
      return GST_FLOW_OK;
    }
  }
}

static GstFlowReturn
gst_movpkg_demux_stream_data_received (GstAdaptiveDemux2Stream *
    stream, GstBuffer * buffer)
{
  GstFlowReturn push_result =
      gst_adaptive_demux2_stream_push_buffer (stream, buffer);
  switch (push_result) {
    case GST_FLOW_EOS:
      // This allows reverse playback to work in some cases where the inner
      // demuxer doesn't do a good job
      return GST_FLOW_OK;
    default:
      return push_result;
  }
}

static void
gst_movpkg_demux_stream_finalize (GObject * obj)
{
  GstMovpkgDemuxStream *self = GST_MOVPKG_DEMUX_STREAM_CAST (obj);
  g_clear_pointer (&self->base_uri, g_free);
  g_clear_pointer (&self->fragments, g_ptr_array_unref);
  g_clear_pointer (&self->info, gst_movpkg_stream_info_free);
  g_clear_pointer (&self->rendition, gst_hls_rendition_stream_unref);
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static GstFlowReturn
gst_movpkg_demux_stream_update_fragment_info (GstAdaptiveDemux2Stream * stream)
{
  GstMovpkgDemuxStream *self = GST_MOVPKG_DEMUX_STREAM (stream);
  if (self->current_fragment >= self->fragments->len) {
    return GST_FLOW_EOS;
  }
  GstMovpkgMediaSegment *movpkg_segment =
      g_ptr_array_index (self->fragments, self->current_fragment);
  if (movpkg_segment == NULL) {
    return GST_FLOW_EOS;
  }

  GstAdaptiveDemux2StreamFragment *fragment = &stream->fragment;
  gst_adaptive_demux2_stream_fragment_clear (fragment);

  fragment->uri = gst_uri_join_strings (self->base_uri, movpkg_segment->path);
  fragment->stream_time = movpkg_segment->time;
  fragment->duration = movpkg_segment->duration;

  GstMovpkgMediaInitSegment *init = gst_movpkg_stream_info_find_init_segment
      (self->info, movpkg_segment->sequence_number);
  if (init) {
    fragment->header_uri = gst_uri_join_strings (self->base_uri, init->path);
  } else {
    GST_DEBUG_OBJECT (self, "no matching init segment for sequence %ld",
        movpkg_segment->sequence_number);
  }

  return GST_FLOW_OK;
}

static guint
find_fragment_containing (GstMovpkgDemuxStream * self, gboolean forward,
    GstClockTime time, GstClockTimeDiff * final_ts)
{
  if (forward) {
    for (gint i = 0; i < self->fragments->len; i++) {
      GstMovpkgMediaSegment *movpkg_segment =
          g_ptr_array_index (self->fragments, i);
      GstClockTime end = movpkg_segment->time + movpkg_segment->duration;
      if (time < end) {
        GST_LOG_OBJECT (self,
            "request to seek forward to %" GST_TIMEP_FORMAT " found fragment [%"
            GST_TIMEP_FORMAT "=>%" GST_TIMEP_FORMAT "]", &time,
            &movpkg_segment->time, &end);
        *final_ts = time;
        return i;
      }
    }
  } else {
    for (gint i = self->fragments->len - 1; i >= 0; i--) {
      GstMovpkgMediaSegment *movpkg_segment =
          g_ptr_array_index (self->fragments, i);
      GstClockTime end = movpkg_segment->time + movpkg_segment->duration;
      if (time > movpkg_segment->time) {
        GST_LOG_OBJECT (self,
            "request to seek backward to %" GST_TIMEP_FORMAT
            " found fragment [%" GST_TIMEP_FORMAT "=>%" GST_TIMEP_FORMAT "]",
            &time, &movpkg_segment->time, &end);
        *final_ts = time;
        return MAX (0, i);
      }
    }
  }

  *final_ts = 0;
  return 0;
}

GstFlowReturn
gst_movpkg_demux_stream_seek (GstAdaptiveDemux2Stream * stream,
    gboolean forward, GstSeekFlags flags, GstClockTimeDiff ts,
    GstClockTimeDiff * final_ts)
{
  GstMovpkgDemuxStream *self = GST_MOVPKG_DEMUX_STREAM (stream);
  GST_DEBUG_OBJECT (self, "seeking to %" GST_STIMEP_FORMAT, &ts);
  self->current_fragment =
      find_fragment_containing (self, forward, ts, final_ts);
  stream->discont = TRUE;
  return GST_FLOW_OK;
}

static void
add_stream_segment (const GValue * item, GstMovpkgDemuxStream * self)
{
  GstMovpkgMediaSegment *segment = g_value_get_pointer (item);
  gsize bitrate = compute_bitrate (segment->length, segment->duration);
  self->max_fragment_bitrate = MAX (self->max_fragment_bitrate, bitrate);
  self->total_size += segment->length;
  g_ptr_array_add (self->fragments, segment);
  GstClockTime end = segment->time + segment->duration;
  if (GST_CLOCK_TIME_IS_VALID (self->duration)) {
    self->duration = MAX (self->duration, end);
  } else {
    self->duration = end;
  }
}

GstAdaptiveDemux2Stream *
gst_movpkg_demux_stream_new (const gchar * name, const gchar * base_path,
    GstMovpkgPersistentStreamInfo * info, GstHLSRenditionStream * rendition)
{
  GstMovpkgDemuxStream *self =
      g_object_new (GST_TYPE_MOVPKG_DEMUX_STREAM, "name", name, NULL);
  GstAdaptiveDemux2Stream *stream = GST_ADAPTIVE_DEMUX2_STREAM_CAST (self);
  self->info = info;
  self->base_uri = gst_filename_to_uri (base_path, NULL);
  self->rendition = rendition ? gst_hls_rendition_stream_ref (rendition) : NULL;
  self->max_fragment_bitrate = 0;
  self->total_size = 0;

  GMutex mutex;
  g_mutex_init (&mutex);
  guint32 cookie = 0;
  GstIterator *it = gst_movpkg_stream_info_iter_segments (info, &mutex, &cookie,
      NULL);
  while (gst_iterator_foreach (it,
          (GstIteratorForeachFunction) add_stream_segment, self) ==
      GST_ITERATOR_RESYNC) {
    gst_iterator_resync (it);
  }
  gst_iterator_free (it);

  GST_LOG_OBJECT (self, "have %u fragments", self->fragments->len);

  stream->pending_tracks = TRUE;

  return stream;
}

GstClockTime
gst_movpkg_demux_stream_get_duration (GstMovpkgDemuxStream * self)
{
  return self->duration;
}
