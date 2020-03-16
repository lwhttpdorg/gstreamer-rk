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

#ifndef __GST_FLX_DECODER_H__
#define __GST_FLX_DECODER_H__

#include <gst/gst.h>

#include <gst/base/gstadapter.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbytewriter.h>
#include "flx_color.h"

G_BEGIN_DECLS

typedef enum {
  GST_FLXDEC_READ_HEADER,
  GST_FLXDEC_PLAYING,
} GstFlxDecState;

struct _GstFlxDec {
  GstElement element;

  GstPad *sinkpad, *srcpad;

  GstSegment segment;
  gboolean need_segment;

  gboolean active, new_meta;

  guint8 *delta_data, *frame_data;
  GstAdapter *adapter;
  gsize size;
  GstFlxDecState state;
  gint64 frame_time;
  gint64 next_time;
  gint64 duration;

  FlxColorSpaceConverter *converter;

  FlxHeader hdr;
};

#define GST_TYPE_FLXDEC (gst_flxdec_get_type())
G_DECLARE_FINAL_TYPE (GstFlxDec, gst_flxdec, GST, FLXDEC, GstElement)

GST_ELEMENT_REGISTER_DECLARE (flxdec);

G_END_DECLS

#endif /* __GST_FLX_DECODER_H__ */
