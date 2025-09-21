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


#ifndef __GST_Y4MENCODE_H__
#define __GST_Y4MENCODE_H__


#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_Y4M_ENCODE (gst_y4m_encode_get_type())
G_DECLARE_FINAL_TYPE (GstY4mEncode, gst_y4m_encode, GST, Y4M_ENCODE,
    GstVideoEncoder)

struct _GstY4mEncode {
  GstVideoEncoder parent;

  /* caps information */
  GstVideoInfo info;

  /* output buffer layout */
  GstVideoInfo out_info;

  const gchar *colorspace;
  /* state information */
  gboolean header;
  gboolean padded;
};

GST_ELEMENT_REGISTER_DECLARE (y4menc);

G_END_DECLS

#endif /* __GST_Y4MENCODE_H__ */
