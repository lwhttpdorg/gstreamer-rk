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

#ifndef __GST_FEC_H__
#define __GST_FEC_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_ULPFEC_ENC (gst_rtp_ulpfec_enc_get_type())
G_DECLARE_FINAL_TYPE (GstRtpUlpFecEnc, gst_rtp_ulpfec_enc,
    GST, RTP_ULPFEC_ENC, GstElement)

struct _GstRtpUlpFecEnc {
  GstElement parent;
  GstPad *srcpad;
  GstPad *sinkpad;
  guint8 twcc_ext_id;

  GHashTable *ssrc_to_ctx;

  /* properties */
  guint pt;
  guint32 ssrc;
  guint percentage;
  guint percentage_important;
  gboolean multipacket;
  guint num_packets_protected;
};

typedef struct {
  guint ssrc;

  GstElement *parent;
  GstPad *srcpad;

  /* settings */
  guint pt;
  guint percentage;
  guint percentage_important;
  gboolean multipacket;
  gboolean mux_seq;

  guint num_packets_protected;
  guint16 seqnum;
  guint seqnum_offset;
  guint num_packets_received;
  guint num_packets_fec;
  guint fec_nth;
  GQueue packets_buf;

  gdouble budget;
  gdouble budget_inc;
  gdouble budget_important;
  gdouble budget_inc_important;

  GArray *info_arr;
  GArray *scratch_buf;

  guint fec_packets;
  guint fec_packet_idx;
} GstRtpUlpFecEncStreamCtx;

G_END_DECLS

#endif /* __GST_FEC_H__ */
