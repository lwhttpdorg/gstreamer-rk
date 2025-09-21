/* GstRtpDtmfDepay
 *
 * Copyright (C) 2008 Collabora Limited
 * Copyright (C) 2008 Nokia Corporation
 *   Contact: Youness Alaoui <youness.alaoui@collabora.co.uk>
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

#ifndef __GST_RTP_DTMF_DEPAY_H__
#define __GST_RTP_DTMF_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>

#include "gstdtmfcommon.h"

G_BEGIN_DECLS

#define GST_TYPE_RTP_DTMF_DEPAY (gst_rtp_dtmf_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpDTMFDepay, gst_rtp_dtmf_depay, GST, RTP_DTMF_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpDTMFDepay
{
  /*< private >*/
  GstRTPBaseDepayload depayload;
  double sample;
  guint32 previous_ts;
  guint16 previous_duration;
  GstClockTime first_gst_ts;
  guint unit_time;
  guint max_duration;
};

GST_ELEMENT_REGISTER_DECLARE (rtpdtmfdepay);

G_END_DECLS
#endif /* __GST_RTP_DTMF_DEPAY_H__ */
