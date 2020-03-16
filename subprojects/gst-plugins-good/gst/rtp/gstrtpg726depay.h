/* GStreamer
 * Copyright (C) 2005 Edgard Lima <edgard.lima@gmail.com>
 * Copyright (C) 2008 Axis Communications AB <dev-gstreamer@axis.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more
 */

#ifndef __GST_RTP_G726_DEPAY_H__
#define __GST_RTP_G726_DEPAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_G726_DEPAY (gst_rtp_g726_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpG726Depay, gst_rtp_g726_depay, GST, RTP_G726_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpG726Depay
{
  GstRTPBaseDepayload depayload;

  gboolean aal2;
  gboolean force_aal2;
  gint bitrate;
  guint block_align;
};

G_END_DECLS
#endif /* __GST_RTP_G726_DEPAY_H__ */
