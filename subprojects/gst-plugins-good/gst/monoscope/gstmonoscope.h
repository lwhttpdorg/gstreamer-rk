/* GStreamer monoscope visualisation element
 * Copyright (C) <2002> Richard Boulton <richard@tartarus.org>
 * Copyright (C) <2006> Tim-Philipp Müller <tim centricular net>
 * Copyright (C) <2006> Wim Taymans <wim at fluendo dot com>
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

#ifndef __GST_MONOSCOPE__
#define __GST_MONOSCOPE__

G_BEGIN_DECLS

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#define GST_TYPE_MONOSCOPE (gst_monoscope_get_type())
G_DECLARE_FINAL_TYPE (GstMonoscope, gst_monoscope, GST, MONOSCOPE, GstElement)

struct _GstMonoscope
{
  GstElement element;

  /* pads */
  GstPad      *sinkpad;
  GstPad      *srcpad;

  GstAdapter  *adapter;

  guint64      next_ts;             /* expected timestamp of the next frame */
  guint64      frame_duration;      /* video frame duration    */
  gint         rate;                /* sample rate             */
  guint        bps;                 /* bytes per sample        */
  guint        spf;                 /* samples per video frame */
  GstBufferPool *pool;

  GstSegment   segment;
  gboolean     segment_pending;

  /* QoS stuff *//* with LOCK */
  gdouble      proportion;
  GstClockTime earliest_time;

  /* video state */
  gint         fps_num;
  gint         fps_denom;
  gint         width;
  gint         height;
  guint        outsize;

  /* visualisation state */
  struct monoscope_state *visstate;
};

GST_ELEMENT_REGISTER_DECLARE (monoscope);

G_END_DECLS

#endif /* __GST_MONOSCOPE__ */

