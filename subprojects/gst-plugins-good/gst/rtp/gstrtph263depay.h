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

#ifndef __GST_RTP_H263_DEPAY_H__
#define __GST_RTP_H263_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_H263_DEPAY (gst_rtp_h263_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpH263Depay, gst_rtp_h263_depay, GST, RTP_H263_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpH263Depay
{
  GstRTPBaseDepayload depayload;

  guint8 offset;	/* offset to apply to next payload */
  guint8 leftover;	/* leftover from previous payload (if offset != 0) */
  gboolean psc_I;       /* Picture-Coding-Type == I from Picture Start Code packet */
  GstAdapter *adapter;
  gboolean start;
};

G_END_DECLS

#endif /* __GST_RTP_H263_DEPAY_H__ */

