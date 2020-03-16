/* GStreamer
 * Copyright (C) <2014> Stian Selnes <stian@pexip.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_RTP_H261_DEPAY_H__
#define __GST_RTP_H261_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_H261_DEPAY (gst_rtp_h261_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpH261Depay, gst_rtp_h261_depay, GST, RTP_H261_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpH261Depay
{
  GstRTPBaseDepayload depayload;

  GstAdapter *adapter;
  gboolean start;
  guint8 leftover;
};

G_END_DECLS
#endif /* __GST_RTP_H261_DEPAY_H__ */
