/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later
 * version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

/**
 * SECTION:element-avtpcvfdepay
 * @see_also: avtpcvfpay
 *
 * De-payload CVF AVTPDUs into compressed video (H.264 and MJPEG) according
 * to IEEE 1722-2016. For detailed information see
 * https://standards.ieee.org/standard/1722-2016.html.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 avtpsrc ! avtpcvfdepay ! decodebin ! videoconvert ! autovideosink
 * ]| This example pipeline will de-payload H.264 video from the AVTPDUs, decode
 * and play them. Refer to the avtpcvfpay example to payload H.264 and send the
 * AVTP stream.
 * </refsect2>
 */

#include <avtp.h>
#include <avtp_cvf.h>
#include <gst/audio/audio-format.h>
#include <arpa/inet.h>

#include "gstavtpcvfdepay.h"

GST_DEBUG_CATEGORY_STATIC (avtpcvfdepay_debug);
#define GST_CAT_DEFAULT avtpcvfdepay_debug

/* prototypes */

static GstFlowReturn gst_avtp_cvf_depay_process (GstAvtpBaseDepayload *
    avtpbasedepayload, GstBuffer * buffer);
static gboolean gst_avtp_cvf_depay_push_caps (GstAvtpVfDepayBase * avtpvfdepay);
static void gst_avtp_cvf_depay_get_M (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, gboolean * M);
static void gst_avtp_cvf_depay_finalize (GObject * object);

static gboolean
gst_avtp_cvf_depay_subtype_is_compatible (GstAvtpCvfDepay * avtpcvfdepay,
    guint8 format_subtype)
{
  GstAvtpBaseDepayload *base = GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay);
  GstCaps *candidate, *allowed;
  gboolean compatible;

  if (format_subtype == AVTP_CVF_FORMAT_SUBTYPE_MJPEG) {
    candidate = gst_caps_new_empty_simple ("image/jpeg");
  } else {
    candidate = gst_caps_new_simple ("video/x-h264",
        "stream-format", G_TYPE_STRING, "avc",
        "alignment", G_TYPE_STRING, "au", NULL);
  }

  allowed = gst_pad_get_allowed_caps (base->srcpad);
  compatible = allowed != NULL && !gst_caps_is_empty (allowed)
      && gst_caps_can_intersect (allowed, candidate);

  GST_LOG_OBJECT (avtpcvfdepay,
      "Subtype %u compatibility check: %d", format_subtype, compatible);

  if (allowed)
    gst_caps_unref (allowed);
  gst_caps_unref (candidate);

  return compatible;
}

#define AVTP_CVF_COMMON_HEADER_SIZE (sizeof(struct avtp_stream_pdu))
#define AVTP_CVF_H264_HEADER_SIZE (AVTP_CVF_COMMON_HEADER_SIZE + sizeof(guint32))
#define AVTP_CVF_MJPEG_EXTRA_HEADER_SIZE 8
#define AVTP_CVF_MJPEG_HEADER_SIZE (AVTP_CVF_COMMON_HEADER_SIZE + AVTP_CVF_MJPEG_EXTRA_HEADER_SIZE)
#define AVTP_CVF_FORMAT_SUBTYPE_UNKNOWN 0xff
#define FU_A_HEADER_SIZE (sizeof(guint16))
#define STAP_A_TYPE 24
#define STAP_B_TYPE 25
#define MTAP16_TYPE 26
#define MTAP24_TYPE 27
#define FU_A_TYPE   28
#define FU_B_TYPE   29

#define NRI_MASK        0x60
#define NRI_SHIFT       5
#define START_MASK      0x80
#define START_SHIFT     7
#define END_MASK        0x40
#define END_SHIFT       6
#define NAL_TYPE_MASK   0x1f

/* pad templates */

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264,"
        "  stream-format = (string) avc, alignment = (string) au; image/jpeg")
    );

#define gst_avtp_cvf_depay_parent_class parent_class
G_DEFINE_TYPE (GstAvtpCvfDepay, gst_avtp_cvf_depay,
    GST_TYPE_AVTP_VF_DEPAY_BASE);
GST_ELEMENT_REGISTER_DEFINE (avtpcvfdepay, "avtpcvfdepay", GST_RANK_NONE,
    GST_TYPE_AVTP_CVF_DEPAY);

static void
gst_avtp_cvf_depay_class_init (GstAvtpCvfDepayClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAvtpBaseDepayloadClass *avtpbasedepayload_class =
      GST_AVTP_BASE_DEPAYLOAD_CLASS (klass);
  GstAvtpVfDepayBaseClass *avtpvfdepaybase_class =
      GST_AVTP_VF_DEPAY_BASE_CLASS (klass);

  gobject_class->finalize = gst_avtp_cvf_depay_finalize;

  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class,
      "AVTP Compressed Video Format (CVF) depayloader",
      "Codec/Depayloader/Network/AVTP",
      "Extracts compressed video from CVF AVTPDUs",
      "Ederson de Souza <ederson.desouza@intel.com>");

  avtpbasedepayload_class->process = gst_avtp_cvf_depay_process;

  avtpvfdepaybase_class->depay_push_caps =
      GST_DEBUG_FUNCPTR (gst_avtp_cvf_depay_push_caps);

  GST_DEBUG_CATEGORY_INIT (avtpcvfdepay_debug, "avtpcvfdepay",
      0, "debug category for avtpcvfdepay element");
}

static void
gst_avtp_cvf_depay_finalize (GObject * object)
{
  GstAvtpCvfDepay *avtpcvfdepay = GST_AVTP_CVF_DEPAY (object);
  gint i;

  for (i = 0; i < 255; i++) {
    g_free (avtpcvfdepay->qtables[i]);
    avtpcvfdepay->qtables[i] = NULL;
  }

  if (avtpcvfdepay->fragments != NULL) {
    gst_buffer_unref (avtpcvfdepay->fragments);
    avtpcvfdepay->fragments = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_avtp_cvf_depay_init (GstAvtpCvfDepay * avtpcvfdepay)
{
  avtpcvfdepay->fragments = NULL;
  avtpcvfdepay->seqnum = 0;
  avtpcvfdepay->format_subtype = AVTP_CVF_FORMAT_SUBTYPE_UNKNOWN;
  memset (avtpcvfdepay->qtables, 0, sizeof (avtpcvfdepay->qtables));
}

static gboolean
gst_avtp_cvf_depay_push_caps (GstAvtpVfDepayBase * avtpvfdepay)
{
  GstAvtpBaseDepayload *avtpbasedepayload =
      GST_AVTP_BASE_DEPAYLOAD (avtpvfdepay);
  GstAvtpCvfDepay *avtpcvfdepay = GST_AVTP_CVF_DEPAY (avtpvfdepay);
  GstBuffer *codec_data;
  GstEvent *event;
  GstMapInfo map;
  GstCaps *caps;

  GST_DEBUG_OBJECT (avtpcvfdepay, "Setting src pad caps");

  if (G_UNLIKELY (avtpcvfdepay->format_subtype ==
          AVTP_CVF_FORMAT_SUBTYPE_UNKNOWN)) {
    GST_WARNING_OBJECT (avtpcvfdepay,
        "Cannot push caps before receiving a valid CVF format subtype");
    return FALSE;
  }

  if (avtpcvfdepay->format_subtype == AVTP_CVF_FORMAT_SUBTYPE_MJPEG) {
    caps = gst_caps_new_empty_simple ("image/jpeg");
    event = gst_event_new_caps (caps);
    gst_caps_unref (caps);
    return gst_pad_push_event (avtpbasedepayload->srcpad, event);
  }

  /* Send simple codec data, with only the NAL size len, no SPS/PPS.
   * Below, 7 is the minimal codec_data size, when no SPS/PPS is sent */
  codec_data = gst_buffer_new_allocate (NULL, 7, NULL);
  gst_buffer_map (codec_data, &map, GST_MAP_READWRITE);

  memset (map.data, 0, map.size);
  map.data[0] = 1;              /* version */
  map.data[4] = 0x03 | 0xfc;    /* nal len size (4) - 1. Other 6 bits are 1 */
  map.data[5] = 0xe0;           /* first 3 bits are 1 */
  gst_buffer_unmap (codec_data, &map);

  caps = gst_pad_get_pad_template_caps (avtpbasedepayload->srcpad);
  caps = gst_caps_make_writable (caps);
  gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);

  event = gst_event_new_caps (caps);

  gst_buffer_unref (codec_data);
  gst_caps_unref (caps);

  return gst_pad_push_event (avtpbasedepayload->srcpad, event);
}

static GstFlowReturn
gst_avtp_cvf_depay_push_and_discard (GstAvtpCvfDepay * avtpcvfdepay)
{
  GstAvtpVfDepayBase *avtpvfdepaybase = GST_AVTP_VF_DEPAY_BASE (avtpcvfdepay);
  GstFlowReturn ret = GST_FLOW_OK;

  /* Push everything we have, hopefully decoder can handle it */
  if (avtpvfdepaybase->out_buffer != NULL) {
    GST_DEBUG_OBJECT (avtpcvfdepay, "Pushing incomplete buffers");

    ret = gst_avtp_vf_depay_base_push (GST_AVTP_VF_DEPAY_BASE (avtpcvfdepay));
  }

  /* Discard any incomplete fragments */
  if (avtpcvfdepay->fragments != NULL) {
    GST_DEBUG_OBJECT (avtpcvfdepay, "Discarding incomplete fragments");
    gst_buffer_unref (avtpcvfdepay->fragments);
    avtpcvfdepay->fragments = NULL;
  }

  return ret;
}

static gboolean
gst_avtp_cvf_depay_validate_avtpdu (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, gboolean * lost_packet, guint8 * format_subtype)
{
  GstAvtpBaseDepayload *avtpbasedepayload =
      GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay);
  struct avtp_stream_pdu *pdu;
  gboolean result = FALSE;
  guint64 val;
  guint val32;
  gint r GST_UNUSED_ASSERT;

  if (G_UNLIKELY (map->size < AVTP_CVF_COMMON_HEADER_SIZE)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Incomplete AVTP header, expected it to have size of %zd, got %zd",
        AVTP_CVF_COMMON_HEADER_SIZE, map->size);
    goto end;
  }

  pdu = (struct avtp_stream_pdu *) map->data;

  r = avtp_pdu_get ((struct avtp_common_pdu *) pdu, AVTP_FIELD_SUBTYPE, &val32);
  g_assert (r == 0);
  if (val32 != AVTP_SUBTYPE_CVF) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header subtype %d, expected %d", val32,
        AVTP_SUBTYPE_CVF);
    goto end;
  }

  r = avtp_pdu_get ((struct avtp_common_pdu *) pdu, AVTP_FIELD_VERSION, &val32);
  g_assert (r == 0);
  if (G_UNLIKELY (val32 != 0)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header version %d, expected %d", val32, 0);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_SV, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != 1)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header stream valid %" G_GUINT64_FORMAT
        ", expected %d", val, 1);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_STREAM_ID, &val);
  g_assert (r == 0);
  if (val != avtpbasedepayload->streamid) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header stream id 0x%" G_GINT64_MODIFIER
        "x, expected 0x%" G_GINT64_MODIFIER "x", val,
        avtpbasedepayload->streamid);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_FORMAT, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != AVTP_CVF_FORMAT_RFC)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header format %" G_GUINT64_FORMAT ", expected %d", val,
        AVTP_CVF_FORMAT_RFC);
    goto end;
  }

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_FORMAT_SUBTYPE, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != AVTP_CVF_FORMAT_SUBTYPE_H264 &&
          val != AVTP_CVF_FORMAT_SUBTYPE_MJPEG)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Unsupported AVTP header format subtype %" G_GUINT64_FORMAT, val);
    goto end;
  }
  *format_subtype = val;

  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (map->size < sizeof (*pdu) + val)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "AVTP packet size %" G_GSIZE_FORMAT " too small, expected at least %"
        G_GUINT64_FORMAT, map->size - AVTP_CVF_H264_HEADER_SIZE,
        sizeof (*pdu) + val);
    goto end;
  }

  if (G_UNLIKELY (*format_subtype == AVTP_CVF_FORMAT_SUBTYPE_H264
          && val < sizeof (guint32))) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "H264 stream_data_length too small: %" G_GUINT64_FORMAT, val);
    goto end;
  }

  if (G_UNLIKELY (*format_subtype == AVTP_CVF_FORMAT_SUBTYPE_MJPEG
          && val < AVTP_CVF_MJPEG_PAYLOAD_HEADER_SIZE)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "MJPEG stream_data_length too small: %" G_GUINT64_FORMAT, val);
    goto end;
  }

  *lost_packet = FALSE;
  r = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_SEQ_NUM, &val);
  g_assert (r == 0);
  if (G_UNLIKELY (val != avtpcvfdepay->seqnum)) {
    GST_INFO_OBJECT (avtpcvfdepay,
        "Unexpected AVTP header seq num %" G_GUINT64_FORMAT ", expected %u",
        val, avtpcvfdepay->seqnum);

    avtpcvfdepay->seqnum = val;
    /* This is not a reason to drop the packet, but it may be a good moment
     * to push everything we have - maybe we lost the M packet? */
    *lost_packet = TRUE;
  }
  avtpcvfdepay->seqnum++;

  result = TRUE;

end:
  return result;
}

static guint8
gst_avtp_cvf_depay_get_nal_type (GstMapInfo * map)
{
  struct avtp_stream_pdu *pdu;
  struct avtp_cvf_h264_payload *pay;
  guint8 nal_header, nal_type;

  pdu = (struct avtp_stream_pdu *) map->data;
  pay = (struct avtp_cvf_h264_payload *) pdu->avtp_payload;
  nal_header = pay->h264_data[0];
  nal_type = nal_header & NAL_TYPE_MASK;

  return nal_type;
}

static void
gst_avtp_cvf_depay_get_avtp_timestamps (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, GstClockTime * pts, GstClockTime * dts)
{
  GstAvtpBaseDepayload *base = GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay);
  struct avtp_stream_pdu *pdu;
  guint64 avtp_time, h264_time, tv, ptv;
  gint res GST_UNUSED_ASSERT;

  *pts = GST_CLOCK_TIME_NONE;
  *dts = GST_CLOCK_TIME_NONE;

  pdu = (struct avtp_stream_pdu *) map->data;

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_TV, &tv);
  g_assert (res == 0);

  if (tv == 1) {
    res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_TIMESTAMP, &avtp_time);
    g_assert (res == 0);

    *dts = gst_avtp_base_depayload_tstamp_to_ptime (base, avtp_time,
        base->last_dts);
  }

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_H264_PTV, &ptv);
  g_assert (res == 0);

  if (ptv == 1) {
    res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_H264_TIMESTAMP, &h264_time);
    g_assert (res == 0);

    *pts = gst_avtp_base_depayload_tstamp_to_ptime (base, h264_time,
        base->last_dts);
  }
}

static void
gst_avtp_cvf_depay_get_mjpeg_timestamps (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, GstClockTime * pts, GstClockTime * dts)
{
  GstAvtpBaseDepayload *base = GST_AVTP_BASE_DEPAYLOAD (avtpcvfdepay);
  struct avtp_stream_pdu *pdu;
  guint64 avtp_time, tv;
  gint res GST_UNUSED_ASSERT;

  *pts = GST_CLOCK_TIME_NONE;
  *dts = GST_CLOCK_TIME_NONE;

  pdu = (struct avtp_stream_pdu *) map->data;

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_TV, &tv);
  g_assert (res == 0);

  if (tv == 1) {
    res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_TIMESTAMP, &avtp_time);
    g_assert (res == 0);

    *pts = gst_avtp_base_depayload_tstamp_to_ptime (base, avtp_time,
        base->last_dts);
    *dts = *pts;
  }
}

/* -----------------------------------------------------------------------
 * RFC 2435 JPEG header reconstruction — ported from gstrtpjpegdepay.c
 * ----------------------------------------------------------------------- */

static const int zigzag[] = {
  0, 1, 8, 16, 9, 2, 3, 10,
  17, 24, 32, 25, 18, 11, 4, 5,
  12, 19, 26, 33, 40, 48, 41, 34,
  27, 20, 13, 6, 7, 14, 21, 28,
  35, 42, 49, 56, 57, 50, 43, 36,
  29, 22, 15, 23, 30, 37, 44, 51,
  58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63
};

static const int jpeg_luma_quantizer[64] = {
  16, 11, 10, 16, 24, 40, 51, 61,
  12, 12, 14, 19, 26, 58, 60, 55,
  14, 13, 16, 24, 40, 57, 69, 56,
  14, 17, 22, 29, 51, 87, 80, 62,
  18, 22, 37, 56, 68, 109, 103, 77,
  24, 35, 55, 64, 81, 104, 113, 92,
  49, 64, 78, 87, 103, 121, 120, 101,
  72, 92, 95, 98, 112, 100, 103, 99
};

static const int jpeg_chroma_quantizer[64] = {
  17, 18, 24, 47, 99, 99, 99, 99,
  18, 21, 26, 66, 99, 99, 99, 99,
  24, 26, 56, 99, 99, 99, 99, 99,
  47, 66, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99,
  99, 99, 99, 99, 99, 99, 99, 99
};

static void
MakeTables (gint Q, guint8 qtable[128])
{
  gint i;
  guint factor;

  factor = CLAMP (Q, 1, 99);

  if (Q < 50)
    Q = 5000 / factor;
  else
    Q = 200 - factor * 2;

  for (i = 0; i < 64; i++) {
    gint lq = (jpeg_luma_quantizer[zigzag[i]] * Q + 50) / 100;
    gint cq = (jpeg_chroma_quantizer[zigzag[i]] * Q + 50) / 100;

    qtable[i] = CLAMP (lq, 1, 255);
    qtable[i + 64] = CLAMP (cq, 1, 255);
  }
}

static const guint8 lum_dc_codelens[] = {
  0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0
};

static const guint8 lum_dc_symbols[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static const guint8 lum_ac_codelens[] = {
  0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d
};

static const guint8 lum_ac_symbols[] = {
  0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
  0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
  0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
  0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
  0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
  0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};

static const guint8 chm_dc_codelens[] = {
  0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0
};

static const guint8 chm_dc_symbols[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static const guint8 chm_ac_codelens[] = {
  0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77
};

static const guint8 chm_ac_symbols[] = {
  0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
  0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
  0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
  0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
  0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
  0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
  0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
  0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
  0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
  0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa
};

static guint8 *
MakeQuantHeader (guint8 * p, guint8 * qt, gint size, gint tableNo)
{
  *p++ = 0xff;
  *p++ = 0xdb;                  /* DQT */
  *p++ = 0;
  *p++ = size + 3;
  *p++ = tableNo;
  memcpy (p, qt, size);

  return (p + size);
}

static guint8 *
MakeHuffmanHeader (guint8 * p, const guint8 * codelens, int ncodes,
    const guint8 * symbols, int nsymbols, int tableNo, int tableClass)
{
  *p++ = 0xff;
  *p++ = 0xc4;                  /* DHT */
  *p++ = 0;
  *p++ = 3 + ncodes + nsymbols;
  *p++ = (tableClass << 4) | tableNo;
  memcpy (p, codelens, ncodes);
  p += ncodes;
  memcpy (p, symbols, nsymbols);
  p += nsymbols;

  return (p);
}

static guint8 *
MakeDRIHeader (guint8 * p, guint16 dri)
{
  *p++ = 0xff;
  *p++ = 0xdd;                  /* DRI */
  *p++ = 0x0;
  *p++ = 4;
  *p++ = dri >> 8;
  *p++ = dri & 0xff;

  return (p);
}

static guint
MakeHeaders (guint8 * p, int type, int width, int height, guint8 * qt,
    guint precision, guint16 dri)
{
  guint8 *start = p;
  gint size;

  *p++ = 0xff;
  *p++ = 0xd8;                  /* SOI */

  size = ((precision & 1) ? 128 : 64);
  p = MakeQuantHeader (p, qt, size, 0);
  qt += size;

  size = ((precision & 2) ? 128 : 64);
  p = MakeQuantHeader (p, qt, size, 1);
  qt += size;

  if (dri != 0)
    p = MakeDRIHeader (p, dri);

  *p++ = 0xff;
  *p++ = 0xc0;                  /* SOF */
  *p++ = 0;
  *p++ = 17;
  *p++ = 8;                     /* 8-bit precision */
  *p++ = height >> 8;
  *p++ = height;
  *p++ = width >> 8;
  *p++ = width;
  *p++ = 3;                     /* number of components */
  *p++ = 0;                     /* comp 0 */
  if ((type & 0x3f) == 0)
    *p++ = 0x21;                /* hsamp=2, vsamp=1 (4:2:2) */
  else
    *p++ = 0x22;                /* hsamp=2, vsamp=2 (4:2:0) */
  *p++ = 0;                     /* quant table 0 */
  *p++ = 1;                     /* comp 1 */
  *p++ = 0x11;                  /* hsamp=1, vsamp=1 */
  *p++ = 1;                     /* quant table 1 */
  *p++ = 2;                     /* comp 2 */
  *p++ = 0x11;
  *p++ = 1;                     /* quant table 1 */

  p = MakeHuffmanHeader (p, lum_dc_codelens,
      sizeof (lum_dc_codelens), lum_dc_symbols, sizeof (lum_dc_symbols), 0, 0);
  p = MakeHuffmanHeader (p, lum_ac_codelens,
      sizeof (lum_ac_codelens), lum_ac_symbols, sizeof (lum_ac_symbols), 0, 1);
  p = MakeHuffmanHeader (p, chm_dc_codelens,
      sizeof (chm_dc_codelens), chm_dc_symbols, sizeof (chm_dc_symbols), 1, 0);
  p = MakeHuffmanHeader (p, chm_ac_codelens,
      sizeof (chm_ac_codelens), chm_ac_symbols, sizeof (chm_ac_symbols), 1, 1);

  *p++ = 0xff;
  *p++ = 0xda;                  /* SOS */
  *p++ = 0;
  *p++ = 12;
  *p++ = 3;                     /* 3 components */
  *p++ = 0;                     /* comp 0 */
  *p++ = 0;                     /* huffman table 0 */
  *p++ = 1;                     /* comp 1 */
  *p++ = 0x11;                  /* huffman table 1 */
  *p++ = 2;                     /* comp 2 */
  *p++ = 0x11;
  *p++ = 0;                     /* first DCT coeff */
  *p++ = 63;                    /* last DCT coeff */
  *p++ = 0;                     /* successive approx. */

  return (p - start);
}

/* -----------------------------------------------------------------------
 * AVTP CVF MJPEG depayloader (IEEE 1722-2016 §8.4 / RFC 2435)
 * ----------------------------------------------------------------------- */

static GstFlowReturn
gst_avtp_cvf_depay_handle_mjpeg (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * avtpdu, GstMapInfo * map)
{
  GstAvtpVfDepayBase *avtpvfdepaybase = GST_AVTP_VF_DEPAY_BASE (avtpcvfdepay);
  struct avtp_stream_pdu *pdu;
  guint64 stream_data_len;
  guint32 fragment_offset;
  guint8 type, Q, width, height;
  const guint8 *mjpeg_hdr;
  gsize scan_offset;            /* byte within this packet where scan data starts */
  gsize scan_size;
  GstClockTime pts, dts;
  gboolean M;
  GstFlowReturn ret = GST_FLOW_OK;
  gint res GST_UNUSED_ASSERT;

  pdu = (struct avtp_stream_pdu *) map->data;

  res =
      avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN, &stream_data_len);
  g_assert (res == 0);

  if (G_UNLIKELY (stream_data_len < AVTP_CVF_MJPEG_PAYLOAD_HEADER_SIZE)) {
    GST_DEBUG_OBJECT (avtpcvfdepay, "MJPEG stream_data_length too small");
    return GST_FLOW_OK;
  }

  /* Parse the RFC 2435 main header (8 bytes after the AVTP common header) */
  mjpeg_hdr = map->data + AVTP_CVF_COMMON_HEADER_SIZE;
  fragment_offset = ((guint32) mjpeg_hdr[1] << 16) |
      ((guint32) mjpeg_hdr[2] << 8) | mjpeg_hdr[3];
  type = mjpeg_hdr[4];
  Q = mjpeg_hdr[5];
  width = mjpeg_hdr[6];
  height = mjpeg_hdr[7];

  /* scan_offset starts right after the 8-byte MJPEG main header */
  scan_offset = AVTP_CVF_MJPEG_HEADER_SIZE;

  gst_avtp_cvf_depay_get_M (avtpcvfdepay, map, &M);

  /* ----- Handle first fragment of a new frame ----- */
  if (fragment_offset == 0) {
    guint8 hdr_buf[1024];
    guint hdr_size;
    guint16 dri = 0;
    guint8 precision = 0;
    guint8 *qtable = NULL;
    gboolean own_qtable = FALSE;

    /* Free any previously incomplete frame */
    if (avtpcvfdepay->fragments != NULL) {
      gst_buffer_unref (avtpcvfdepay->fragments);
      avtpcvfdepay->fragments = NULL;
    }

    /* Optional restart marker header: present when bit 6 of type is set */
    if (type & 0x40) {
      if (G_UNLIKELY (scan_offset + 4 > map->size)) {
        GST_DEBUG_OBJECT (avtpcvfdepay, "Buffer too small for DRI header");
        return GST_FLOW_OK;
      }
      dri = ((guint16) map->data[scan_offset] << 8) |
          map->data[scan_offset + 1];
      /* bytes [2] and [3] are F/L/count — ignore them */
      scan_offset += 4;
    }

    /* Optional quantization table header: present when Q >= 128 */
    if (Q >= 128) {
      guint16 qt_len;

      if (G_UNLIKELY (scan_offset + 4 > map->size)) {
        GST_DEBUG_OBJECT (avtpcvfdepay, "Buffer too small for quant header");
        return GST_FLOW_OK;
      }
      /* mbz(8) | precision(8) | length(16) */
      precision = map->data[scan_offset + 1];
      qt_len = ((guint16) map->data[scan_offset + 2] << 8) |
          map->data[scan_offset + 3];
      scan_offset += 4;

      if (G_UNLIKELY (qt_len == 0 || scan_offset + qt_len > map->size)) {
        GST_DEBUG_OBJECT (avtpcvfdepay, "Invalid quant table size %u", qt_len);
        return GST_FLOW_OK;
      }

      /* Cache qtable for this Q slot so we can reuse it on subsequent frames */
      g_free (avtpcvfdepay->qtables[Q - 128]);
      avtpcvfdepay->qtables[Q - 128] = g_memdup2 (map->data + scan_offset,
          qt_len);
      qtable = avtpcvfdepay->qtables[Q - 128];
      scan_offset += qt_len;
    } else {
      /* Q < 128: derive tables from standard quantizers */
      if (avtpcvfdepay->qtables[Q] == NULL) {
        avtpcvfdepay->qtables[Q] = g_malloc (128);
        MakeTables (Q, avtpcvfdepay->qtables[Q]);
      }
      qtable = avtpcvfdepay->qtables[Q];
      own_qtable = FALSE;       /* shared; do not free */
      (void) own_qtable;
    }

    if (G_UNLIKELY (qtable == NULL)) {
      GST_WARNING_OBJECT (avtpcvfdepay, "No quantization table available");
      return GST_FLOW_OK;
    }

    /* Reconstruct JPEG headers (SOI + DQT + [DRI] + SOF + DHT + SOS) */
    hdr_size = MakeHeaders (hdr_buf, type, width * 8, height * 8, qtable,
        precision, dri);

    avtpcvfdepay->fragments = gst_buffer_new_allocate (NULL, hdr_size, NULL);
    gst_buffer_fill (avtpcvfdepay->fragments, 0, hdr_buf, hdr_size);
  } else {
    /* Non-first fragment: we must already be accumulating a frame */
    if (G_UNLIKELY (avtpcvfdepay->fragments == NULL)) {
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "Dropping MJPEG fragment with offset %u, no frame in progress",
          fragment_offset);
      return GST_FLOW_OK;
    }
  }

  /* Append the scan data from this packet */
  if (G_LIKELY (scan_offset < map->size)) {
    scan_size = map->size - scan_offset;
    avtpcvfdepay->fragments = gst_buffer_append (avtpcvfdepay->fragments,
        gst_buffer_copy_region (avtpdu, GST_BUFFER_COPY_MEMORY,
            scan_offset, scan_size));
  }

  if (!M)
    return GST_FLOW_OK;

  /* Last fragment received: append EOI and push */
  {
    static const guint8 eoi[2] = { 0xFF, 0xD9 };
    GstBuffer *eoi_buf = gst_buffer_new_allocate (NULL, 2, NULL);
    gst_buffer_fill (eoi_buf, 0, eoi, 2);
    avtpcvfdepay->fragments =
        gst_buffer_append (avtpcvfdepay->fragments, eoi_buf);
  }

  gst_avtp_cvf_depay_get_mjpeg_timestamps (avtpcvfdepay, map, &pts, &dts);
  GST_BUFFER_PTS (avtpcvfdepay->fragments) = pts;
  GST_BUFFER_DTS (avtpcvfdepay->fragments) = dts;

  avtpvfdepaybase->out_buffer = avtpcvfdepay->fragments;
  avtpcvfdepay->fragments = NULL;

  ret = gst_avtp_vf_depay_base_push (GST_AVTP_VF_DEPAY_BASE (avtpcvfdepay));

  return ret;
}

static GstFlowReturn
gst_avtp_cvf_depay_internal_push (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * buffer, gboolean M)
{
  GstAvtpVfDepayBase *avtpvfdepaybase = GST_AVTP_VF_DEPAY_BASE (avtpcvfdepay);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (avtpcvfdepay,
      "Adding buffer of size %" G_GSIZE_FORMAT " (nalu size %"
      G_GSIZE_FORMAT ") to out_buffer", gst_buffer_get_size (buffer),
      gst_buffer_get_size (buffer) - sizeof (guint32));

  if (avtpvfdepaybase->out_buffer) {
    avtpvfdepaybase->out_buffer =
        gst_buffer_append (avtpvfdepaybase->out_buffer, buffer);
  } else {
    avtpvfdepaybase->out_buffer = buffer;
  }

  /* We only truly push to decoder when we get the last video buffer */
  if (M) {
    ret = gst_avtp_vf_depay_base_push (GST_AVTP_VF_DEPAY_BASE (avtpcvfdepay));
  }

  return ret;
}

static void
gst_avtp_cvf_depay_get_M (GstAvtpCvfDepay * avtpcvfdepay, GstMapInfo * map,
    gboolean * M)
{
  struct avtp_stream_pdu *pdu;
  guint64 val;
  gint res GST_UNUSED_ASSERT;

  pdu = (struct avtp_stream_pdu *) map->data;

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_M, &val);
  g_assert (res == 0);

  *M = val;
}

static void
gst_avtp_cvf_depay_get_nalu_size (GstAvtpCvfDepay * avtpcvfdepay,
    GstMapInfo * map, guint16 * nalu_size)
{
  struct avtp_stream_pdu *pdu;
  guint64 val;
  gint res GST_UNUSED_ASSERT;

  pdu = (struct avtp_stream_pdu *) map->data;

  res = avtp_cvf_pdu_get (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN, &val);
  g_assert (res == 0);

  /* We need to discount the H.264 header field */
  *nalu_size = val - sizeof (guint32);
}

static GstFlowReturn
gst_avtp_cvf_depay_process_last_fragment (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * avtpdu, GstMapInfo * map, gsize offset, gsize nalu_size,
    guint8 nri, guint8 nal_type)
{
  GstBuffer *nal;
  GstMapInfo map_nal;
  GstClockTime pts, dts;
  gboolean M;
  GstFlowReturn ret = GST_FLOW_OK;

  if (G_UNLIKELY (avtpcvfdepay->fragments == NULL)) {
    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Received final fragment, but no start fragment received. Dropping it.");
    goto end;
  }

  gst_buffer_copy_into (avtpcvfdepay->fragments, avtpdu,
      GST_BUFFER_COPY_MEMORY, offset, nalu_size);

  /* Allocate buffer to keep the nal_header (1 byte) and the NALu size (4 bytes) */
  nal = gst_buffer_new_allocate (NULL, 4 + 1, NULL);
  if (G_UNLIKELY (nal == NULL)) {
    GST_ERROR_OBJECT (avtpcvfdepay, "Could not allocate buffer");
    ret = GST_FLOW_ERROR;
    goto end;
  }

  gst_buffer_map (nal, &map_nal, GST_MAP_READWRITE);
  /* Add NAL size. Extra 1 counts the nal_header */
  nalu_size = gst_buffer_get_size (avtpcvfdepay->fragments) + 1;
  map_nal.data[0] = nalu_size >> 24;
  map_nal.data[1] = nalu_size >> 16;
  map_nal.data[2] = nalu_size >> 8;
  map_nal.data[3] = nalu_size;

  /* Finally, add the nal_header */
  map_nal.data[4] = (nri << 5) | nal_type;

  gst_buffer_unmap (nal, &map_nal);

  nal = gst_buffer_append (nal, avtpcvfdepay->fragments);

  gst_avtp_cvf_depay_get_avtp_timestamps (avtpcvfdepay, map, &pts, &dts);
  GST_BUFFER_PTS (nal) = pts;
  GST_BUFFER_DTS (nal) = dts;

  gst_avtp_cvf_depay_get_M (avtpcvfdepay, map, &M);
  ret = gst_avtp_cvf_depay_internal_push (avtpcvfdepay, nal, M);

  avtpcvfdepay->fragments = NULL;

end:
  return ret;
}

static GstFlowReturn
gst_avtp_cvf_depay_handle_fu_a (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * avtpdu, GstMapInfo * map)
{
  GstFlowReturn ret = GST_FLOW_OK;
  struct avtp_stream_pdu *pdu;
  struct avtp_cvf_h264_payload *pay;
  guint8 fu_header, fu_indicator, nal_type, start, end, nri;
  guint16 nalu_size;
  gsize offset;

  if (G_UNLIKELY (map->size - AVTP_CVF_H264_HEADER_SIZE < 2)) {
    GST_ERROR_OBJECT (avtpcvfdepay,
        "Buffer too small to contain fragment headers, size: %"
        G_GSIZE_FORMAT, map->size - AVTP_CVF_H264_HEADER_SIZE);
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    goto end;
  }

  pdu = (struct avtp_stream_pdu *) map->data;
  pay = (struct avtp_cvf_h264_payload *) pdu->avtp_payload;
  fu_indicator = pay->h264_data[0];
  nri = (fu_indicator & NRI_MASK) >> NRI_SHIFT;

  GST_DEBUG_OBJECT (avtpcvfdepay, "Fragment indicator - NRI: %u", nri);

  fu_header = pay->h264_data[1];
  nal_type = fu_header & NAL_TYPE_MASK;
  start = (fu_header & START_MASK) >> START_SHIFT;
  end = (fu_header & END_MASK) >> END_SHIFT;

  GST_DEBUG_OBJECT (avtpcvfdepay,
      "Fragment header - type: %u start: %u end: %u", nal_type, start, end);

  if (G_UNLIKELY (start && end)) {
    GST_ERROR_OBJECT (avtpcvfdepay,
        "Invalid fragment header - 'start' and 'end' bits set");
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    goto end;
  }

  /* Size and offset also ignores the FU_HEADER and FU_INDICATOR fields,
   * hence the "sizeof(guint8) * 2" */
  offset = AVTP_CVF_H264_HEADER_SIZE + sizeof (guint8) * 2;
  gst_avtp_cvf_depay_get_nalu_size (avtpcvfdepay, map, &nalu_size);
  nalu_size -= sizeof (guint8) * 2;

  if (start) {
    if (G_UNLIKELY (avtpcvfdepay->fragments != NULL)) {
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "Received starting fragment, but previous one is not complete. Dropping old fragment");
      ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
      if (ret != GST_FLOW_OK)
        goto end;
    }

    avtpcvfdepay->fragments =
        gst_buffer_copy_region (avtpdu, GST_BUFFER_COPY_MEMORY, offset,
        nalu_size);
  }

  if (!start && !end) {
    if (G_UNLIKELY (avtpcvfdepay->fragments == NULL)) {
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "Received intermediate fragment, but no start fragment received. Dropping it.");
      ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
      goto end;
    }
    gst_buffer_copy_into (avtpcvfdepay->fragments, avtpdu,
        GST_BUFFER_COPY_MEMORY, offset, nalu_size);
  }

  if (end) {
    ret =
        gst_avtp_cvf_depay_process_last_fragment (avtpcvfdepay, avtpdu, map,
        offset, nalu_size, nri, nal_type);
  }

end:
  return ret;
}

static GstFlowReturn
gst_avtp_cvf_depay_handle_single_nal (GstAvtpCvfDepay * avtpcvfdepay,
    GstBuffer * avtpdu, GstMapInfo * map)
{
  GstClockTime pts, dts;
  GstMapInfo map_nal;
  guint16 nalu_size;
  GstBuffer *nal;
  gboolean M;

  GST_DEBUG_OBJECT (avtpcvfdepay, "Handling single NAL unit");

  if (avtpcvfdepay->fragments != NULL) {
    GstFlowReturn ret;

    GST_DEBUG_OBJECT (avtpcvfdepay,
        "Received single NAL unit, but previous fragment is incomplete. Dropping fragment.");
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    if (ret != GST_FLOW_OK)
      return ret;
  }

  gst_avtp_cvf_depay_get_avtp_timestamps (avtpcvfdepay, map, &pts, &dts);
  gst_avtp_cvf_depay_get_nalu_size (avtpcvfdepay, map, &nalu_size);
  gst_avtp_cvf_depay_get_M (avtpcvfdepay, map, &M);

  /* Four is the number of bytes containing NALu size just before the NALu */
  nal = gst_buffer_new_allocate (NULL, 4, NULL);
  gst_buffer_map (nal, &map_nal, GST_MAP_READWRITE);

  /* Add NAL size just before the NAL itself (4 bytes before it) */
  map_nal.data[0] = map_nal.data[1] = 0;
  map_nal.data[2] = nalu_size >> 8;
  map_nal.data[3] = nalu_size & 0xff;
  gst_buffer_unmap (nal, &map_nal);

  gst_buffer_copy_into (nal, avtpdu, GST_BUFFER_COPY_MEMORY,
      AVTP_CVF_H264_HEADER_SIZE, nalu_size);
  GST_BUFFER_PTS (nal) = pts;
  GST_BUFFER_DTS (nal) = dts;

  return gst_avtp_cvf_depay_internal_push (avtpcvfdepay, nal, M);
}

static GstFlowReturn
gst_avtp_cvf_depay_process (GstAvtpBaseDepayload * avtpbasedepayload,
    GstBuffer * buffer)
{
  GstAvtpCvfDepay *avtpcvfdepay = GST_AVTP_CVF_DEPAY (avtpbasedepayload);
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean lost_packet;
  guint8 format_subtype;
  GstMapInfo map;

  gst_buffer_map (buffer, &map, GST_MAP_READ);

  if (!gst_avtp_cvf_depay_validate_avtpdu (avtpcvfdepay, &map, &lost_packet,
          &format_subtype)) {
    GST_DEBUG_OBJECT (avtpcvfdepay, "Invalid AVTPDU buffer, dropping it");
    goto end;
  }

  if (format_subtype != avtpcvfdepay->format_subtype) {
    GstAvtpVfDepayBase *avtpvfdepay = GST_AVTP_VF_DEPAY_BASE (avtpcvfdepay);

    if (!gst_avtp_cvf_depay_subtype_is_compatible (avtpcvfdepay,
            format_subtype)) {
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "Dropping packet for incompatible subtype %u", format_subtype);
      goto end;
    }

    avtpcvfdepay->format_subtype = format_subtype;

    if (!gst_avtp_cvf_depay_push_caps (avtpvfdepay)) {
      GST_WARNING_OBJECT (avtpcvfdepay,
          "Failed to push caps for subtype %u", format_subtype);
      goto end;
    }
  }
  if (lost_packet) {
    ret = gst_avtp_cvf_depay_push_and_discard (avtpcvfdepay);
    if (ret != GST_FLOW_OK)
      goto end;
  }

  if (format_subtype == AVTP_CVF_FORMAT_SUBTYPE_MJPEG) {
    ret = gst_avtp_cvf_depay_handle_mjpeg (avtpcvfdepay, buffer, &map);
    goto end;
  }

  switch (gst_avtp_cvf_depay_get_nal_type (&map)) {
    case STAP_A_TYPE:
    case STAP_B_TYPE:
    case MTAP16_TYPE:
    case MTAP24_TYPE:
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "AVTP aggregation packets not supported, dropping it");
      break;
    case FU_A_TYPE:
      ret = gst_avtp_cvf_depay_handle_fu_a (avtpcvfdepay, buffer, &map);
      break;
    case FU_B_TYPE:
      GST_DEBUG_OBJECT (avtpcvfdepay,
          "AVTP fragmentation FU-B packets not supported, dropping it");
      break;
    default:
      ret = gst_avtp_cvf_depay_handle_single_nal (avtpcvfdepay, buffer, &map);
      break;
  }

end:
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  return ret;
}
