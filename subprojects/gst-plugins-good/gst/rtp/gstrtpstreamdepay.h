/* GStreamer
 * Copyright (C) 2013 Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_RTP_STREAM_DEPAY_H__
#define __GST_RTP_STREAM_DEPAY_H__

#include <gst/gst.h>
#include <gst/base/gstbaseparse.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_STREAM_DEPAY (gst_rtp_stream_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpStreamDepay, gst_rtp_stream_depay,
    GST, RTP_STREAM_DEPAY, GstBaseParse)

struct _GstRtpStreamDepay
{
  GstBaseParse parent;
};

G_END_DECLS

#endif
