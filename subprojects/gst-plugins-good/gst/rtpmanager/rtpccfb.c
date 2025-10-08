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
#include "rtpccfb.h"
#include <gst/base/gstbytewriter.h>

/* The middle 32 bits of a NTP timestamp as per RFC 8888 Section 3.1. and
 * RFC 3550 Section 4. */
#define COMPACT_NTP(t) t >> 16 & 0xFFFFFFFF

typedef struct
{
  guint16 seqnum;
  guint8 ecn_cp;
  guint32 arrival_time;
} RecvPacket;

GST_DEBUG_CATEGORY_EXTERN (rtp_session_debug);
#define GST_CAT_DEFAULT rtp_session_debug

struct _RTPCCFBManager
{
  GObject object;

  guint mtu;
  GstClockTime feedback_interval;
  GstClockTime next_feedback_send_time;

  GHashTable *recv_packets;
  GQueue *reports;
};

G_DEFINE_TYPE (RTPCCFBManager, rtp_ccfb_manager, G_TYPE_OBJECT);

typedef struct
{
  GArray *packets;
  guint16 seqnum_min;
  guint16 seqnum_max;
} SSRCPackets;

static SSRCPackets *
ssrc_packets_new (void)
{
  SSRCPackets *packets = g_new0 (SSRCPackets, 1);
  packets->packets = g_array_new (FALSE, FALSE, sizeof (RecvPacket));

  return packets;
}

static void
ssrc_packets_free (SSRCPackets * packets)
{
  g_clear_pointer (&packets->packets, g_array_unref);
  g_clear_pointer (&packets, g_free);
}

static void
rtp_ccfb_manager_init (RTPCCFBManager * ccfb)
{
  ccfb->feedback_interval = GST_CLOCK_TIME_NONE;

  ccfb->recv_packets =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) ssrc_packets_free);
  ccfb->reports = g_queue_new ();
}

static void
rtp_ccfb_manager_finalize (GObject * object)
{
  RTPCCFBManager *ccfb = RTP_CCFB_MANAGER (object);

  g_clear_pointer (&ccfb->recv_packets, g_hash_table_unref);
  g_queue_free_full (ccfb->reports, (GDestroyNotify) g_hash_table_unref);

  G_OBJECT_CLASS (rtp_ccfb_manager_parent_class)->finalize (object);
}

static void
rtp_ccfb_manager_class_init (RTPCCFBManagerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  gobject_class->finalize = rtp_ccfb_manager_finalize;
}

RTPCCFBManager *
rtp_ccfb_manager_new (guint mtu)
{
  RTPCCFBManager *ccfb = g_object_new (RTP_TYPE_CCFB_MANAGER, NULL);

  rtp_ccfb_manager_set_mtu (ccfb, mtu);

  return ccfb;
}

void
rtp_ccfb_manager_set_mtu (RTPCCFBManager * ccfb, guint mtu)
{
  ccfb->mtu = mtu;
}

void
rtp_ccfb_manager_set_feedback_interval (RTPCCFBManager * ccfb,
    GstClockTime feedback_interval)
{
  ccfb->feedback_interval = feedback_interval;
}

GstClockTime
rtp_ccfb_manager_get_feedback_interval (RTPCCFBManager * ccfb)
{
  return ccfb->feedback_interval;
}

static gint
_recv_packets_sort (gconstpointer a, gconstpointer b)
{
  guint res;
  RecvPacket *packeta = (RecvPacket *) a;
  RecvPacket *packetb = (RecvPacket *) b;

  res = gst_rtp_buffer_compare_seqnum (packetb->seqnum, packeta->seqnum);
  if (res == 0)
    res = packeta->arrival_time - packetb->arrival_time;

  return res;
}

static void
create_ssrc_report (gpointer key, gpointer value, gpointer user_data)
{
  GHashTable *report = user_data;
  SSRCPackets *ssrc_packets = value;
  GArray *recv_packets = ssrc_packets->packets;
  RecvPacket *prev;
  guint16 prev_seqnum;
  guint i;

  if (recv_packets->len == 0) {
    return;
  }

  g_array_sort (recv_packets, _recv_packets_sort);

  /* Consolidate duplicates */
  prev = &g_array_index (recv_packets, RecvPacket, 0);
  for (i = 1; i < recv_packets->len;) {
    RecvPacket *cur = &g_array_index (recv_packets, RecvPacket, i);

    if (prev->seqnum == cur->seqnum) {
      if (cur->ecn_cp == 0x3) {
        /* If any of the copies of the duplicated packet are ECN-CE marked, then
         * an ECN-CE mark MUST be reported for that packet. */
        prev->ecn_cp = 0x3;
      }
      GST_DEBUG ("Removing duplicate packet #%u", cur->seqnum);
      g_array_remove_index (recv_packets, i);
    } else {
      prev = cur;
      ++i;
    }
  }

  g_hash_table_insert (report, key, g_array_copy (recv_packets));

  /* Remove received (and just reported) packets from the head of recv_packets. */
  prev = &g_array_index (recv_packets, RecvPacket, 0);
  prev_seqnum = prev->seqnum;
  g_array_remove_index (recv_packets, 0);
  while (recv_packets->len > 0) {
    RecvPacket *packet = &g_array_index (recv_packets, RecvPacket, 0);
    if (gst_rtp_buffer_compare_seqnum (prev_seqnum, packet->seqnum) == 1) {
      ++prev_seqnum;
      g_array_remove_index (recv_packets, 0);
      continue;
    }
    break;
  }

  /* Update min and max seqnum */
  if (recv_packets->len > 0) {
    ssrc_packets->seqnum_min =
        g_array_index (recv_packets, RecvPacket, 0).seqnum;
    ssrc_packets->seqnum_max =
        g_array_index (recv_packets, RecvPacket, recv_packets->len - 1).seqnum;
  }
}

static void
rtp_ccfb_manager_create_report (RTPCCFBManager * ccfb)
{
  GHashTable *report;

  report =
      g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL,
      (GDestroyNotify) g_array_unref);
  g_hash_table_foreach (ccfb->recv_packets, create_ssrc_report, report);

  g_queue_push_tail (ccfb->reports, report);
}

typedef struct
{
  GstByteWriter writer;
  guint32 report_ts;
} WriteData;

static void
generate_report_block (gpointer key, gpointer value, gpointer user_data)
{
  guint ssrc = GPOINTER_TO_UINT (key);
  GArray *recv_packets = value;
  WriteData *data = user_data;
  RecvPacket *first, *last;
  guint i;
  guint16 seqnum;
  guint num_metric_blocks = 0;

  first = &g_array_index (recv_packets, RecvPacket, 0);
  last = &g_array_index (recv_packets, RecvPacket, recv_packets->len - 1);

  gst_byte_writer_put_uint32_be (&data->writer, ssrc);

  seqnum = first->seqnum;
  for (i = 0; i < recv_packets->len; ++i) {
    RecvPacket *pkt = &g_array_index (recv_packets, RecvPacket, i);
    guint16 metric_block;
    guint16 ato;

    if (i == 0) {
      gint diff = gst_rtp_buffer_compare_seqnum (first->seqnum, last->seqnum);
      g_assert (diff >= 0);

      /* Write begin_seq and num_reports. */
      gst_byte_writer_put_uint16_be (&data->writer, first->seqnum);
      gst_byte_writer_put_uint16_be (&data->writer, diff + 1);
    }

    while (pkt->seqnum != seqnum) {
      /* Report packets that have not been received yet. */
      gst_byte_writer_put_uint16_be (&data->writer, 0);
      ++num_metric_blocks;
      ++seqnum;
    }

    /* Calculate arrival time offset. */
    if (pkt->arrival_time > data->report_ts) {
      ato = 0x1FFF;
    } else {
      ato = (data->report_ts - pkt->arrival_time) / 64;
      ato = MIN (ato, 0x1FFE);
    }

    metric_block = 0x8000 | (pkt->ecn_cp << 13) | ato;
    gst_byte_writer_put_uint16_be (&data->writer, metric_block);
    ++num_metric_blocks;

    seqnum = pkt->seqnum + 1;
  }

  if (num_metric_blocks % 2) {
    /* Add padding. */
    gst_byte_writer_put_uint16_be (&data->writer, 0);
  }
}

GstBuffer *
rtp_ccfb_manager_get_feedback (RTPCCFBManager * ccfb, guint sender_ssrc,
    guint64 ntptime)
{
  GstBuffer *buf = NULL;
  GHashTable *report;
  GstRTCPBuffer rtcp = GST_RTCP_BUFFER_INIT;
  GstRTCPPacket packet;
  guint fcilen;
  WriteData data;
  guint8 *report_data;

  data.report_ts = COMPACT_NTP (ntptime);

  do {
    report = g_queue_pop_head (ccfb->reports);
    if (!report) {
      break;
    }

    gst_byte_writer_init (&data.writer);

    g_hash_table_foreach (report, generate_report_block, &data);
    g_clear_pointer (&report, g_hash_table_unref);

    gst_byte_writer_put_uint32_be (&data.writer, data.report_ts);

    buf = gst_rtcp_buffer_new (ccfb->mtu);

    gst_rtcp_buffer_map (buf, GST_MAP_READWRITE, &rtcp);
    gst_rtcp_buffer_add_packet (&rtcp, GST_RTCP_TYPE_RTPFB, &packet);

    gst_rtcp_packet_fb_set_type (&packet, GST_RTCP_RTPFB_TYPE_CCFB);
    gst_rtcp_packet_fb_set_sender_ssrc (&packet, sender_ssrc);

    fcilen = gst_byte_writer_get_size (&data.writer);
    g_assert (fcilen % sizeof (guint32) == 0);
    /* CCFB feedback packet has no media source SSRC field. */
    fcilen -= sizeof (guint32);

    report_data = gst_byte_writer_reset_and_get_data (&data.writer);

    if (gst_rtcp_packet_fb_set_fci_length (&packet, fcilen / sizeof (guint32))) {
      gst_rtcp_packet_fb_set_media_ssrc (&packet,
          GST_READ_UINT32_BE (report_data));
      memcpy (gst_rtcp_packet_fb_get_fci (&packet),
          report_data + sizeof (guint32), fcilen);
      gst_rtcp_buffer_unmap (&rtcp);
    } else {
      GST_WARNING ("Unable to set fci len %d", fcilen);
      gst_rtcp_buffer_unmap (&rtcp);
      /* Skip this report and continue with the next one. */
      gst_clear_buffer (&buf);
    }

    g_clear_pointer (&report_data, g_free);
  } while (buf == NULL);

  return buf;
}

static gboolean
rtp_ccfb_manager_check_rtcp_size (RTPCCFBManager * ccfb, guint32 new_ssrc,
    guint16 new_seqnum)
{
  guint size = 3 * sizeof (guint32);    /* RTCP header + sender SSRC + report timestamp */
  GHashTableIter it;
  gpointer key, value;
  gboolean needs_new_report_block = TRUE;

  g_hash_table_iter_init (&it, ccfb->recv_packets);
  while (g_hash_table_iter_next (&it, &key, &value)) {
    SSRCPackets *packets = value;
    if (packets->packets->len > 0) {
      guint ssrc = GPOINTER_TO_UINT (key);
      guint16 first_seqnum = packets->seqnum_min;
      guint16 last_seqnum = packets->seqnum_max;
      guint n_reports;

      if (ssrc == new_ssrc) {
        if (gst_rtp_buffer_compare_seqnum (first_seqnum, new_seqnum) < 0) {
          first_seqnum = new_seqnum;
        } else if (gst_rtp_buffer_compare_seqnum (last_seqnum, new_seqnum) > 0) {
          last_seqnum = new_seqnum;
        }
        needs_new_report_block = FALSE;
      }

      n_reports = gst_rtp_buffer_compare_seqnum (first_seqnum, last_seqnum) + 1;

      size += 2 * sizeof (guint32);     /* stream SSRC + begin_seq and num_reports */
      size += n_reports * sizeof (guint16);     /* reports */
      size += (n_reports % 2) * sizeof (guint16);       /* padding (if needed) */
    }
  }

  if (needs_new_report_block) {
    /* Minimal new report block with one packet */
    size += 3 * sizeof (guint32);
  }

  return size <= ccfb->mtu;
}

gboolean
rtp_ccfb_manager_recv_packet (RTPCCFBManager * ccfb, RTPPacketInfo * pinfo)
{
  gboolean send_feedback = FALSE;
  SSRCPackets *packets;
  RecvPacket packet = { 0 };

  g_assert (pinfo->is_list == FALSE);

  if (!rtp_ccfb_manager_check_rtcp_size (ccfb, pinfo->ssrc, pinfo->seqnum)) {
    GST_LOG ("Generating feedback because report RTCP packet is too large");
    rtp_ccfb_manager_create_report (ccfb);
    send_feedback = TRUE;
  }

  packets =
      g_hash_table_lookup (ccfb->recv_packets, GUINT_TO_POINTER (pinfo->ssrc));
  if (!packets) {
    packets = ssrc_packets_new ();
    g_hash_table_insert (ccfb->recv_packets, GUINT_TO_POINTER (pinfo->ssrc),
        packets);
  }

  packet.seqnum = pinfo->seqnum;
  packet.arrival_time = COMPACT_NTP (pinfo->ntpnstime);
  packet.ecn_cp = pinfo->ecn_cp;

  g_array_append_val (packets->packets, packet);
  if (packets->packets->len == 1) {
    packets->seqnum_min = packet.seqnum;
    packets->seqnum_max = packet.seqnum;
  } else {
    if (gst_rtp_buffer_compare_seqnum (packets->seqnum_min, packet.seqnum) < 0) {
      packets->seqnum_min = packet.seqnum;
    }
    if (gst_rtp_buffer_compare_seqnum (packets->seqnum_max, packet.seqnum) > 0) {
      packets->seqnum_max = packet.seqnum;
    }
  }

  GST_LOG ("Received: ssrc: %u, seqnum: %u, arrival_time: %" GST_TIME_FORMAT
      ", ecn: %d%d",
      pinfo->ssrc, packet.seqnum, GST_TIME_ARGS (pinfo->ntpnstime),
      packet.ecn_cp & 0x2 ? 1 : 0, packet.ecn_cp & 0x1 ? 1 : 0);

  if (GST_CLOCK_TIME_IS_VALID (ccfb->feedback_interval)) {
    if (!GST_CLOCK_TIME_IS_VALID (ccfb->next_feedback_send_time)) {
      ccfb->next_feedback_send_time =
          pinfo->running_time + ccfb->feedback_interval;
    }

    if (pinfo->running_time >= ccfb->next_feedback_send_time) {
      GST_LOG ("Generating feedback: Exceeded feedback interval %"
          GST_TIME_FORMAT, GST_TIME_ARGS (ccfb->feedback_interval));
      rtp_ccfb_manager_create_report (ccfb);
      send_feedback = TRUE;

      while (pinfo->running_time >= ccfb->next_feedback_send_time) {
        ccfb->next_feedback_send_time += ccfb->feedback_interval;
      }
    }
  } else if (pinfo->marker) {
    GST_LOG ("Generating feedback because of marker packet");
    rtp_ccfb_manager_create_report (ccfb);
    send_feedback = TRUE;
  }

  return send_feedback;
}
