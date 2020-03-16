/*
 * gstrtpvp8depay.h - Header for GstRtpVP8Depay
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

#ifndef __GST_RTP_VP8_DEPAY_H__
#define __GST_RTP_VP8_DEPAY_H__

#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_VP8_DEPAY (gst_rtp_vp8_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpVP8Depay, gst_rtp_vp8_depay, GST, RTP_VP8_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpVP8Depay
{
  GstRTPBaseDepayload parent;
  GstAdapter *adapter;
  gboolean started;

  gboolean caps_sent;
  /* In between pictures, we might store GstRTPPacketLost events instead
   * of forwarding them immediately, we check upon reception of a new
   * picture id whether a gap was introduced, in which case we do forward
   * the event. This is to avoid forwarding spurious lost events for FEC
   * packets.
   */
  gboolean stop_lost_events;
  GstEvent *last_lost_event;
  gboolean waiting_for_keyframe;
  gint last_profile;
  gint last_width;
  gint last_height;
  guint last_picture_id;

  gboolean wait_for_keyframe;
  gboolean request_keyframe;
  gboolean last_pushed_was_lost_event;
};

G_END_DECLS

#endif /* #ifndef __GST_RTP_VP8_DEPAY_H__ */
