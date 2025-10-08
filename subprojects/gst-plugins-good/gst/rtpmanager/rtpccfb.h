/* GStreamer
 * Copyright (C) 2025 Collabora Ltd.
 *   @author: Jakub Adam <jakub.adam@collabora.com>
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

#ifndef __RTP_CCFB_H__
#define __RTP_CCFB_H__

#include "rtpstats.h"

G_DECLARE_FINAL_TYPE (RTPCCFBManager, rtp_ccfb_manager, RTP, CCFB_MANAGER, GObject)
#define RTP_TYPE_CCFB_MANAGER (rtp_ccfb_manager_get_type())

RTPCCFBManager * rtp_ccfb_manager_new (guint mtu);

void rtp_ccfb_manager_set_mtu (RTPCCFBManager * ccfb, guint mtu);

void rtp_ccfb_manager_set_feedback_interval (RTPCCFBManager * ccfb,
    GstClockTime feedback_interval);

GstClockTime rtp_ccfb_manager_get_feedback_interval (RTPCCFBManager * ccfb);

gboolean rtp_ccfb_manager_recv_packet (RTPCCFBManager * ccfb,
    RTPPacketInfo * pinfo);

GstBuffer * rtp_ccfb_manager_get_feedback (RTPCCFBManager * ccfb,
    guint sender_ssrc, guint64 ntptime);

#endif /* __RTP_CCFB_H__ */
