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

#ifndef __GST_RTP_H263P_PAY_H__
#define __GST_RTP_H263P_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_H263P_PAY (gst_rtp_h263p_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpH263PPay, gst_rtp_h263p_pay, GST, RTP_H263P_PAY,
    GstRTPBasePayload)

typedef enum
{
  GST_FRAGMENTATION_MODE_NORMAL = 0,
  GST_FRAGMENTATION_MODE_SYNC   = 1
} GstFragmentationMode;

struct _GstRtpH263PPay
{
  GstRTPBasePayload    payload;

  GstAdapter          *adapter;
  GstClockTime         first_timestamp;
  GstClockTime         first_duration;
  GstFragmentationMode fragmentation_mode;
};

G_END_DECLS

#endif /* __GST_RTP_H263P_PAY_H__ */
