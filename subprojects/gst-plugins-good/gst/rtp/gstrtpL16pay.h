/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_RTP_L16_PAY_H__
#define __GST_RTP_L16_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbaseaudiopayload.h>

#include "gstrtpchannels.h"

G_BEGIN_DECLS

#define GST_TYPE_RTP_L16_PAY (gst_rtp_L16_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpL16Pay, gst_rtp_L16_pay, GST, RTP_L16_PAY,
    GstRTPBaseAudioPayload)

struct _GstRtpL16Pay
{
  GstRTPBaseAudioPayload payload;

  GstAudioInfo info;
  const GstRTPChannelOrder *order;
};

G_END_DECLS

#endif /* __GST_RTP_L16_PAY_H__ */
