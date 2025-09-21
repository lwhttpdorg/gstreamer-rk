/*
 * gstrtpvp8pay.h - Header for GstRtpVP8Pay
 * Copyright (C) 2011 Sjoerd Simons <sjoerd@luon.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __GST_RTP_VP8_PAY_H__
#define __GST_RTP_VP8_PAY_H__

#include <gst/rtp/gstrtpbasepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_VP8_PAY (gst_rtp_vp8_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpVP8Pay, gst_rtp_vp8_pay, GST, RTP_VP8_PAY,
    GstRTPBasePayload)

typedef enum _GstVP8RtpPayPictureIDMode GstVP8RtpPayPictureIDMode;

enum _GstVP8RtpPayPictureIDMode {
  VP8_PAY_NO_PICTURE_ID,
  VP8_PAY_PICTURE_ID_7BITS,
  VP8_PAY_PICTURE_ID_15BITS,
};

struct _GstRtpVP8Pay
{
  GstRTPBasePayload parent;
  gboolean is_keyframe;
  gint n_partitions;
  /* Treat frame header & tag & partition size block as the first partition,
   * folowed by max. 8 data partitions. last offset is the end of the buffer */
  guint partition_offset[10];
  guint partition_size[9];
  GstVP8RtpPayPictureIDMode picture_id_mode;
  gint picture_id_offset;
  gint picture_id;
  gboolean temporal_scalability_fields_present;
  guint8 tl0picidx;
};

G_END_DECLS

#endif /* #ifndef __GST_RTP_VP8_PAY_H__ */
