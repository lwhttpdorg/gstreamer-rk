/* GStreamer SBC audio parser
 * Copyright (C) 2012 Collabora Ltd. <tim.muller@collabora.co.uk>
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

#ifndef __GST_SBC_PARSE_H_INCLUDED__
#define __GST_SBC_PARSE_H_INCLUDED__


#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS

#define GST_TYPE_SBC_PARSE (gst_sbc_parse_get_type())
G_DECLARE_FINAL_TYPE (GstSbcParse, gst_sbc_parse, GST, SBC_PARSE, GstBaseParse)
#define GST_SBC_PARSE_CAST(obj)       ((GstSbcParse *)(obj))

typedef enum {
  GST_SBC_CHANNEL_MODE_INVALID = -1,
  GST_SBC_CHANNEL_MODE_MONO = 0,
  GST_SBC_CHANNEL_MODE_DUAL = 1,
  GST_SBC_CHANNEL_MODE_STEREO = 2,
  GST_SBC_CHANNEL_MODE_JOINT_STEREO = 3
} GstSbcChannelMode;

typedef enum {
  GST_SBC_ALLOCATION_METHOD_INVALID = -1,
  GST_SBC_ALLOCATION_METHOD_LOUDNESS = 0,
  GST_SBC_ALLOCATION_METHOD_SNR = 1
} GstSbcAllocationMethod;

struct _GstSbcParse {
  GstBaseParse baseparse;

  /* current output format */
  GstSbcAllocationMethod  alloc_method;
  GstSbcChannelMode       ch_mode;
  gint                    rate;
  gint                    n_blocks;
  gint                    n_subbands;
  gint                    bitpool;

  gboolean                sent_codec_tag;
};

G_END_DECLS

#endif /* __GST_SBC_PARSE_H_INCLUDED__ */
