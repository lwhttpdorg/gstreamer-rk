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


#ifndef __GST_SMPTE_ALPHA_H__
#define __GST_SMPTE_ALPHA_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>


G_BEGIN_DECLS

#include "gstmask.h"

#define GST_TYPE_SMPTE_ALPHA (gst_smpte_alpha_get_type())
G_DECLARE_FINAL_TYPE (GstSMPTEAlpha, gst_smpte_alpha, GST, SMPTE_ALPHA,
    GstVideoFilter)

struct _GstSMPTEAlpha {
  GstVideoFilter element;

  /* properties */
  gint           type;
  gint           border;
  gint           depth;
  gdouble        position;
  gboolean       invert;

  /* negotiated format */
  GstVideoFormat in_format, out_format;
  gint           width;
  gint           height;

  /* state of the effect */
  GstMask       *mask;

  /* processing function */
  void (*process) (GstSMPTEAlpha * smpte, const GstVideoFrame * in, GstVideoFrame * out,
    GstMask * mask, gint border, gint pos);
};

GST_ELEMENT_REGISTER_DECLARE (smptealpha);

G_END_DECLS

#endif /* __GST_SMPTE_ALPHA_H__ */
