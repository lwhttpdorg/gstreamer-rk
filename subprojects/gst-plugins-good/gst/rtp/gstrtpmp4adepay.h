/* GStreamer
 * Copyright (C) <2007> Nokia Corporation (contact <stefan.kost@nokia.com>)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License version 2 as published by the Free Software Foundation.
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

#ifndef __GST_RTP_MP4A_DEPAY_H__
#define __GST_RTP_MP4A_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_MP4A_DEPAY (gst_rtp_mp4a_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpMP4ADepay, gst_rtp_mp4a_depay, GST, RTP_MP4A_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpMP4ADepay
{
  GstRTPBaseDepayload depayload;
  GstAdapter *adapter;
  guint8 numSubFrames;
  guint frame_len;

  gboolean framed;
};

G_END_DECLS

#endif /* __GST_RTP_MP4A_DEPAY_H__ */

