/* GStreamer
 * Copyright (C) <2007> Nokia Corporation
 * Copyright (C) <2007> Collabora Ltd
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

#ifndef __GST_RTP_G723_PAY_H__
#define __GST_RTP_G723_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbaseaudiopayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_G723_PAY (gst_rtp_g723_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRTPG723Pay, gst_rtp_g723_pay, GST, RTP_G723_PAY,
    GstRTPBasePayload)

struct _GstRTPG723Pay
{
  GstRTPBasePayload payload;

  GstAdapter  *adapter;
  GstClockTime duration;
  GstClockTime timestamp;
  gboolean discont;
};

G_END_DECLS

#endif /* __GST_RTP_G723_PAY_H__ */
