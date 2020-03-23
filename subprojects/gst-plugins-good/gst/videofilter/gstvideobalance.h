/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_VIDEO_BALANCE_H__
#define __GST_VIDEO_BALANCE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_BALANCE (gst_video_balance_get_type())
G_DECLARE_FINAL_TYPE (GstVideoBalance, gst_video_balance, GST, VIDEO_BALANCE,
    GstVideoFilter)

/**
 * GstVideoBalance:
 *
 * Opaque data structure.
 */
struct _GstVideoBalance {
  GstVideoFilter videofilter;

  /* < private > */

  /* channels for interface */
  GList *channels;

  /* properties */
  gdouble contrast;
  gdouble brightness;
  gdouble hue;
  gdouble saturation;

  /* tables */
  guint8 tabley[256];
  guint8 *tableu[256];
  guint8 *tablev[256];

  void (*process) (GstVideoBalance *balance, GstVideoFrame *frame);
};

GST_ELEMENT_REGISTER_DECLARE (videobalance);

G_END_DECLS

#endif /* __GST_VIDEO_BALANCE_H__ */
