/* GStreamer plugin for forward error correction
 * Copyright (C) 2017 Pexip
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: Mikhail Fludkov <misha@pexip.com>
 */

#ifndef __GST_RTP_RED_ENC_H__
#define __GST_RTP_RED_ENC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_RED_ENC (gst_rtp_red_enc_get_type())
G_DECLARE_FINAL_TYPE (GstRtpRedEnc, gst_rtp_red_enc, GST, RTP_RED_ENC,
    GstElement)

struct _GstRtpRedEnc {
  GstElement parent;
  GstPad *srcpad;
  GstPad *sinkpad;

  gint pt;
  guint num_sent;
  guint distance;
  gboolean allow_no_red_blocks;

  GQueue *rtp_history;
  gboolean send_caps;
  gboolean is_current_caps_red;
  guint8 twcc_ext_id;

  gboolean ignoring_extension_warned;
};

G_END_DECLS

#endif /* __GST_RTP_RED_ENC_H__ */
