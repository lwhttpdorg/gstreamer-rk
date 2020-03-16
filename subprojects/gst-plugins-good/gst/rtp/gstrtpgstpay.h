/* GStreamer
 * Copyright (C) <2010> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_RTP_GST_PAY_H__
#define __GST_RTP_GST_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_GST_PAY (gst_rtp_gst_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpGSTPay, gst_rtp_gst_pay, GST, RTP_GST_PAY,
    GstRTPBasePayload)

struct _GstRtpGSTPay
{
  GstRTPBasePayload payload;

  GstBufferList *pending_buffers;
  GstAdapter *adapter;
  guint8 flags;
  guint8 etype;

  guint8 current_CV; /* CV field of incoming caps*/
  guint8 next_CV;

  gchar *stream_id;
  GstTagList *taglist;
  guint config_interval;
  GstClockTime last_config;
  gboolean force_config;
};

G_END_DECLS

#endif /* __GST_RTP_GST_PAY_H__ */
