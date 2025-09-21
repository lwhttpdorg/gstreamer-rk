/* GStreamer RTP LDAC payloader
 * Copyright (C) 2020 Asymptotic <sanchayan@asymptotic.io>
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

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasepayload.h>
#include <gst/rtp/gstrtpbuffer.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_LDAC_PAY (gst_rtp_ldac_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpLdacPay, gst_rtp_ldac_pay, GST, RTP_LDAC_PAY,
    GstRTPBasePayload)

struct _GstRtpLdacPay {
  GstRTPBasePayload base;
  guint8 frame_count;
};

G_END_DECLS
