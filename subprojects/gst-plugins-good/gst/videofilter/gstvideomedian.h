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


#ifndef __GST_VIDEO_MEDIAN_H__
#define __GST_VIDEO_MEDIAN_H__


#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_MEDIAN (gst_video_median_get_type())
G_DECLARE_FINAL_TYPE (GstVideoMedian, gst_video_median, GST, VIDEO_MEDIAN,
    GstVideoFilter)

typedef enum
{
  GST_VIDEO_MEDIAN_SIZE_5 = 5,
  GST_VIDEO_MEDIAN_SIZE_9 = 9
} GstVideoMedianSize;

struct _GstVideoMedian {
  GstVideoFilter parent;

  GstVideoMedianSize filtersize;
  gboolean lum_only;
};

GST_ELEMENT_REGISTER_DECLARE (videomedian);

G_END_DECLS

#endif /* __GST_VIDEO_MEDIAN_H__ */
