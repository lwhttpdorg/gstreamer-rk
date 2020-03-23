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


#ifndef __GST_SMPTE_H__
#define __GST_SMPTE_H__

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#include "gstmask.h"

#define GST_TYPE_SMPTE (gst_smpte_get_type())
G_DECLARE_FINAL_TYPE (GstSMPTE, gst_smpte, GST, SMPTE, GstElement)

struct _GstSMPTE {
  GstElement     element;

  /* pads */
  GstPad        *srcpad,
                *sinkpad1,
                *sinkpad2;
  GstCollectPads *collect;
  gboolean        send_stream_start;

  /* properties */
  gint           type;
  gint           border;
  gint           depth;
  guint64        duration;
  gboolean       invert;

  /* negotiated format */
  gint           width;
  gint           height;
  gint           fps_num;
  gint           fps_denom;
  GstVideoInfo   vinfo1;
  GstVideoInfo   vinfo2;

  /* state of the effect */
  gint           position;
  gint           end_position;
  GstMask       *mask;
};

GST_ELEMENT_REGISTER_DECLARE (smpte);

G_END_DECLS
#endif /* __GST_SMPTE_H__ */
