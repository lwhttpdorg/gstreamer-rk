/* GStreamer
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_IMAGE_FREEZE_H__
#define __GST_IMAGE_FREEZE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_IMAGE_FREEZE (gst_image_freeze_get_type())
G_DECLARE_FINAL_TYPE (GstImageFreeze, gst_image_freeze, GST, IMAGE_FREEZE,
    GstElement)

struct _GstImageFreeze
{
  GstElement parent;

  /* < private > */
  GstPad *sinkpad;
  GstPad *srcpad;

  GMutex lock;
  GstBuffer *buffer;
  GstCaps *buffer_caps, *current_caps;

  gboolean negotiated_framerate;
  gint fps_n, fps_d;

  GstSegment segment;
  gboolean need_segment;
  guint seqnum;

  gint num_buffers;
  gint num_buffers_left;

  gboolean allow_replace;

  gboolean is_live;
  gboolean blocked;
  GCond blocked_cond;
  GstClockID clock_id;

  guint64 offset;

  gboolean flushing;
  /* Indicates EOS received via send_event() */
  gboolean direct_eos;
};

GST_ELEMENT_REGISTER_DECLARE (imagefreeze);

G_END_DECLS

#endif /* __GST_IMAGE_FREEZE_H__ */
