/* Farsight
 * Copyright (C) 2006 Marcel Moreaux <marcelm@spacelabs.nl>
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


#ifndef __GSTRTPDVPAY_H__
#define __GSTRTPDVPAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_DV_PAY (gst_rtp_dv_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRTPDVPay, gst_rtp_dv_pay, GST, RTP_DV_PAY,
    GstRTPBasePayload)

typedef enum
{
  GST_DV_PAY_MODE_VIDEO,
  GST_DV_PAY_MODE_BUNDLED,
  GST_DV_PAY_MODE_AUDIO
} GstDVPayMode;

struct _GstRTPDVPay
{
  GstRTPBasePayload payload;

  gboolean negotiated;
  GstDVPayMode mode;
};

G_END_DECLS

#endif /* __GSTRTPDVPAY_H__ */
