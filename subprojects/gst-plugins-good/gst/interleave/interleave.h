/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2007 Andy Wingo <wingo at pobox.com>
 *                    2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * interleave.c: interleave samples, mostly based on adder
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

#ifndef __INTERLEAVE_H__
#define __INTERLEAVE_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>

G_BEGIN_DECLS

#define GST_TYPE_INTERLEAVE (gst_interleave_get_type())
G_DECLARE_FINAL_TYPE (GstInterleave, gst_interleave, GST, INTERLEAVE,
    GstElement)

typedef void (*GstInterleaveFunc) (gpointer out, gpointer in, guint stride, guint nframes);

struct _GstInterleave
{
  GstElement element;

  /*< private >*/
  GstCollectPads *collect;

  gint channels;
  gint padcounter;
  gint rate;
  gint width;

  GValueArray *channel_positions;
  GValueArray *input_channel_positions;
  gboolean channel_positions_from_input;

  gint default_channels_ordering_map[64];
  guint64 channel_mask;

  GstCaps *sinkcaps;
  gint configured_sinkpads_counter;

  GstClockTime timestamp;
  guint64 offset;

  GstEvent *pending_segment;

  GstInterleaveFunc func;

  GstPad *src;

  gboolean send_stream_start;
};

G_END_DECLS

#endif /* __INTERLEAVE_H__ */
