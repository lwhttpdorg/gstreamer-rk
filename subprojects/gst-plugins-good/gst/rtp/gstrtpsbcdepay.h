/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012  Collabora Ltd.
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __GST_RTP_SBC_DEPAY_H
#define __GST_RTP_SBC_DEPAY_H

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/rtp/gstrtpbasedepayload.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_SBC_DEPAY (gst_rtp_sbc_depay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpSbcDepay, gst_rtp_sbc_depay, GST, RTP_SBC_DEPAY,
    GstRTPBaseDepayload)

struct _GstRtpSbcDepay
{
  GstRTPBaseDepayload base;

  int rate;
  GstAdapter *adapter;
  gboolean ignore_timestamps;

  /* Timestamp tracking when ignoring input timestamps */
  GstAudioStreamAlign *stream_align;
};

G_END_DECLS
#endif
