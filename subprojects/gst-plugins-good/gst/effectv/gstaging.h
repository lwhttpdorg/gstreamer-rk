/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2002 FUKUCHI Kentarou
 *
 * AgingTV - film-aging effect.
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

#ifndef __GST_AGING_H__
#define __GST_AGING_H__

#include <gst/gst.h>

#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_AGINGTV (gst_agingtv_get_type())
G_DECLARE_FINAL_TYPE (GstAgingTV, gst_agingtv, GST, AGINGTV, GstVideoFilter)

typedef struct _scratch
{
  gint life;
  gint x;
  gint dx;
  gint init;
} scratch;
#define SCRATCH_MAX 20

struct _GstAgingTV
{
  GstVideoFilter videofilter;

  /* < private > */
  gboolean color_aging;
  gboolean pits;
  gboolean dusts;

  gint coloraging_state;

  scratch scratches[SCRATCH_MAX];
  gint scratch_lines;

  gint dust_interval;
  gint pits_interval;

};

G_END_DECLS

#endif /* __GST_AGING_H__ */

