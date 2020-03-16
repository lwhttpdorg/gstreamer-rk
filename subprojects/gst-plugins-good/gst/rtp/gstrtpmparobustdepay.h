/* GStreamer
 * Copyright (C) <2010> Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * Copyright (C) <2010> Nokia Corporation
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

#ifndef __GST_RTP_MPA_ROBUST_DEPAY_H__
#define __GST_RTP_MPA_ROBUST_DEPAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasedepayload.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbytewriter.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_MPA_ROBUST_DEPAY (gst_rtp_mpa_robust_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpMPARobustDepay, gst_rtp_mpa_robust_depay,
    GST, RTP_MPA_ROBUST_DEPAY, GstRTPBaseDepayload)

struct _GstRtpMPARobustDepay
{
  GstRTPBaseDepayload depayload;

  GstAdapter *adapter;
  gboolean    has_descriptor;

  /* last interleave index */
  gint        last_ii;
  /* last interleave cycle count */
  gint        last_icc;
  /* buffers pending deinterleaving */
  GstBuffer   *deinter[256];

  /* ADU buffers pending MP3 transformation */
  GQueue      *adu_frames;
  GList       *cur_adu_frame;
  gint         offset;
  gint         size;
  GstByteWriter *mp3_frame;
};

G_END_DECLS

#endif /* __GST_RTP_MPA_ROBUST_DEPAY_H__ */
