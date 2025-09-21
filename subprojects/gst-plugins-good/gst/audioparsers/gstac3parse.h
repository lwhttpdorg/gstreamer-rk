/* GStreamer AC3 parser
 * Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2009 Mark Nauwelaerts <mnauw users sf net>
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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

#ifndef __GST_AC3_PARSE_H__
#define __GST_AC3_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS


#define GST_TYPE_AC3_PARSE (gst_ac3_parse_get_type())
G_DECLARE_FINAL_TYPE (GstAc3Parse, gst_ac3_parse, GST, AC3_PARSE, GstBaseParse)

enum {
  GST_AC3_PARSE_ALIGN_NONE,
  GST_AC3_PARSE_ALIGN_FRAME,
  GST_AC3_PARSE_ALIGN_IEC61937,
};

/**
 * GstAc3Parse:
 *
 * The opaque GstAc3Parse object
 */
struct _GstAc3Parse {
  GstBaseParse baseparse;

  /*< private >*/
  gint                  sample_rate;
  gint                  channels;
  gint                  blocks;
  gboolean              eac;
  gboolean              sent_codec_tag;
  gint                  align;
  GstPadChainFunction   baseparse_chainfunc;
};

G_END_DECLS

#endif /* __GST_AC3_PARSE_H__ */
