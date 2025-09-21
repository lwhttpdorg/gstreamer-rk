/* GStreamer RTP KLV Depayloader
 * Copyright (C) 2014-2015 Tim-Philipp Müller <tim@centricular.com>>
 * Copyright (C) 2014-2015 Centricular Ltd
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

#ifndef __GST_RTP_KLV_DEPAY_H__
#define __GST_RTP_KLV_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_KLV_DEPAY (gst_rtp_klv_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpKlvDepay, gst_rtp_klv_depay, GST, RTP_KLV_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpKlvDepay
{
  GstRTPBaseDepayload depayload;

  GstAdapter *adapter;
  gboolean    resync;
  gint        last_marker_seq;   /* -1 if unset, otherwise 0-G_MAXUINT16 */
  gint64      last_rtp_ts;       /* -1 if unset, otherwise 0-G_MAXUINT32 */
};

G_END_DECLS

#endif /* __GST_RTP_KLV_DEPAY_H__ */
