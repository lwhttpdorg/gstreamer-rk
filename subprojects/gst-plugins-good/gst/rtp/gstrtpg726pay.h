/* GStreamer
 * Copyright (C) 2005 Edgard Lima <edgard.lima@gmail.com>
 * Copyright (C) 2007,2008 Axis Communications <dev-gstreamer@axis.com>
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

#ifndef __GST_RTP_G726_PAY_H__
#define __GST_RTP_G726_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbaseaudiopayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_G726_PAY (gst_rtp_g726_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpG726Pay, gst_rtp_g726_pay, GST, RTP_G726_PAY,
    GstRTPBaseAudioPayload)

struct _GstRtpG726Pay
{
  GstRTPBaseAudioPayload audiopayload;

  gboolean aal2;
  gboolean force_aal2;
  gint bitrate;
};

G_END_DECLS
#endif /* __GST_RTP_G726_PAY_H__ */
