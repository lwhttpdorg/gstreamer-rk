/* GStreamer
 *
 * Copyright (c) 2008,2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (c) 2008-2017 Collabora Ltd
 *  @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *  @author: Vincent Penquerc'h <vincent.penquerch@collabora.com>
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

#ifndef __GST_FLV_MUX_H__
#define __GST_FLV_MUX_H__

#include <gst/gst.h>
#include <gst/base/gstaggregator.h>

G_BEGIN_DECLS

#define GST_TYPE_FLV_MUX_PAD (gst_flv_mux_pad_get_type())
G_DECLARE_FINAL_TYPE (GstFlvMuxPad, gst_flv_mux_pad, GST, FLV_MUX_PAD,
    GstAggregatorPad)
#define GST_FLV_MUX_PAD_CAST(obj) ((GstFlvMuxPad *)obj)

struct _GstFlvMuxPad
{
  GstAggregatorPad aggregator_pad;

  guint codec;
  guint rate;
  guint width;
  guint channels;
  GstBuffer *codec_data;

  guint bitrate;

  GstClockTime last_timestamp;
  GstClockTime pts;
  GstClockTime dts;

  gboolean info_changed;
  gboolean drop_deltas;
};


#define GST_TYPE_FLV_MUX (gst_flv_mux_get_type())
G_DECLARE_FINAL_TYPE (GstFlvMux, gst_flv_mux, GST, FLV_MUX, GstAggregator)
#define GST_FLV_MUX_CAST(obj) ((GstFlvMux *)obj)

typedef enum
{
  GST_FLV_MUX_STATE_HEADER,
  GST_FLV_MUX_STATE_DATA
} GstFlvMuxState;

struct _GstFlvMux {
  GstAggregator   aggregator;

  GstPad         *srcpad;

  /* <private> */
  GstFlvMuxState state;
  GstFlvMuxPad *audio_pad;
  GstFlvMuxPad *video_pad;
  gboolean streamable;
  gchar *metadatacreator;
  gchar *encoder;
  gboolean skip_backwards_streams;
  gboolean enforce_increasing_timestamps;

  GstTagList *tags;
  gboolean new_metadata;
  GList *index;
  guint64 byte_count;
  GstClockTime duration;
  GstClockTime first_timestamp;
  guint64 last_dts;

  gboolean sent_header;
};

G_END_DECLS

#endif /* __GST_FLV_MUX_H__ */
