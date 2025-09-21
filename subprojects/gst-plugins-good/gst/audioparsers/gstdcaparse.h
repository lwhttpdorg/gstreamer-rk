/* GStreamer DCA parser
 * Copyright (C) 2010 Tim-Philipp Müller <tim centricular net>
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

#ifndef __GST_DCA_PARSE_H__
#define __GST_DCA_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS

#define GST_TYPE_DCA_PARSE (gst_dca_parse_get_type())
G_DECLARE_FINAL_TYPE (GstDcaParse, gst_dca_parse, GST, DCA_PARSE, GstBaseParse)

#define DCA_MIN_FRAMESIZE 96
#define DCA_MAX_FRAMESIZE 18725 /* 16384*16/14 */

/**
 * GstDcaParse:
 *
 * The opaque GstDcaParse object
 */
struct _GstDcaParse {
  GstBaseParse baseparse;

  /*< private >*/
  gint                  rate;
  gint                  channels;
  gint                  depth;
  gint                  endianness;
  gint                  block_size;
  gint                  frame_size;

  gboolean              sent_codec_tag;

  guint32               last_sync;

  GstPadChainFunction   baseparse_chainfunc;
};

G_END_DECLS

#endif /* __GST_DCA_PARSE_H__ */
