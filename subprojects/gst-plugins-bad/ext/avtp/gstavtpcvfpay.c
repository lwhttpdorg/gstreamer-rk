/*
 * GStreamer AVTP Plugin
 * Copyright (C) 2019 Intel Corporation
 * Copyright (c) 2021, Fastree3D
 * Adrian Fiergolski <Adrian.Fiergolski@fastree3d.com>
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
 * SECTION:element-avtpcvfpay
 * @see_also: avtpcvfdepay
 *
 * Payload compressed video (H.264 and MJPEG) into AVTPDUs according
 * to IEEE 1722-2016. For detailed information see
 * https://standards.ieee.org/standard/1722-2016.html.
 *
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch-1.0 videotestsrc ! x264enc ! avtpcvfpay ! avtpsink
 * ]| This example pipeline will payload H.264 video. Refer to the avtpcvfdepay
 * example to depayload and play the AVTP stream.
 * </refsect2>
 */

#include <avtp.h>
#include <avtp_cvf.h>

#include "gstavtpcvfpay.h"

GST_DEBUG_CATEGORY_STATIC (avtpcvfpay_debug);
#define GST_CAT_DEFAULT avtpcvfpay_debug

/* prototypes */

static GstStateChangeReturn gst_avtp_cvf_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_avtp_cvf_pay_new_caps (GstAvtpVfPayBase * avtpvfpaybase,
    GstCaps * caps);
static gboolean gst_avtp_cvf_pay_prepare_avtp_packets (GstAvtpVfPayBase *
    avtpvfpaybase, GstBuffer * buffer, GPtrArray * avtp_packets);

enum
{
  PROP_0,
};

#define AVTP_CVF_COMMON_HEADER_SIZE (sizeof(struct avtp_stream_pdu))
#define AVTP_CVF_H264_EXTRA_HEADER_SIZE (sizeof(guint32))
#define AVTP_CVF_H264_HEADER_SIZE (AVTP_CVF_COMMON_HEADER_SIZE + AVTP_CVF_H264_EXTRA_HEADER_SIZE)
#define AVTP_CVF_MJPEG_EXTRA_HEADER_SIZE 8
#define AVTP_CVF_MJPEG_HEADER_SIZE (AVTP_CVF_COMMON_HEADER_SIZE + AVTP_CVF_MJPEG_EXTRA_HEADER_SIZE)
#define FU_A_TYPE 28

/* JPEG markers used when parsing input buffers */
#define JPEG_MARKER_SOF       0xC0
#define JPEG_MARKER_DQT       0xDB
#define JPEG_MARKER_SOS       0xDA
#define JPEG_MARKER_DRI       0xDD
#define JPEG_MARKER_EOI       0xD9
#define FU_A_HEADER_SIZE (sizeof(guint16))

#define NRI_MASK            0x60
#define NRI_SHIFT           5
#define START_SHIFT         7
#define END_SHIFT           6
#define NAL_TYPE_MASK       0x1f
#define FIRST_NAL_VCL_TYPE  0x01
#define LAST_NAL_VCL_TYPE   0x05
#define NAL_LEN_SIZE_MASK   0x03

/* pad templates */

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "stream-format = (string) avc, alignment = (string) au; image/jpeg")
    );

/* class initialization */

#define gst_avtp_cvf_pay_parent_class parent_class
G_DEFINE_TYPE (GstAvtpCvfPay, gst_avtp_cvf_pay, GST_TYPE_AVTP_VF_PAY_BASE);
GST_ELEMENT_REGISTER_DEFINE (avtpcvfpay, "avtpcvfpay", GST_RANK_NONE,
    GST_TYPE_AVTP_CVF_PAY);

static void
gst_avtp_cvf_pay_class_init (GstAvtpCvfPayClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstAvtpVfPayBaseClass *avtpvfpaybase_class =
      GST_AVTP_VF_PAY_BASE_CLASS (klass);

  gst_element_class_add_static_pad_template (gstelement_class, &sink_template);

  gst_element_class_set_static_metadata (gstelement_class,
      "AVTP Compressed Video Format (CVF) payloader",
      "Codec/Payloader/Network/AVTP",
      "Payload-encode compressed video into CVF AVTPDU (IEEE 1722)",
      "Ederson de Souza <ederson.desouza@intel.com>");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_avtp_cvf_change_state);

  avtpvfpaybase_class->new_caps = GST_DEBUG_FUNCPTR (gst_avtp_cvf_pay_new_caps);
  avtpvfpaybase_class->prepare_avtp_packets =
      GST_DEBUG_FUNCPTR (gst_avtp_cvf_pay_prepare_avtp_packets);

  GST_DEBUG_CATEGORY_INIT (avtpcvfpay_debug, "avtpcvfpay",
      0, "debug category for avtpcvfpay element");
}

static void
gst_avtp_cvf_pay_init (GstAvtpCvfPay * avtpcvfpay)
{
  avtpcvfpay->header = NULL;
  avtpcvfpay->header_size = AVTP_CVF_H264_HEADER_SIZE;
  avtpcvfpay->format_subtype = AVTP_CVF_FORMAT_SUBTYPE_H264;
  avtpcvfpay->nal_length_size = 0;
}

static gboolean
gst_avtp_cvf_pay_reset_header (GstAvtpCvfPay * avtpcvfpay, guint64 streamid)
{
  GstMapInfo map;
  struct avtp_stream_pdu *pdu;
  guint header_size;
  gint res GST_UNUSED_ASSERT;

  header_size = avtpcvfpay->format_subtype == AVTP_CVF_FORMAT_SUBTYPE_MJPEG ?
      AVTP_CVF_MJPEG_HEADER_SIZE : AVTP_CVF_H264_HEADER_SIZE;

  if (avtpcvfpay->header != NULL) {
    gst_buffer_unref (avtpcvfpay->header);
    avtpcvfpay->header = NULL;
  }

  avtpcvfpay->header = gst_buffer_new_allocate (NULL, header_size, NULL);
  if (avtpcvfpay->header == NULL) {
    GST_ERROR_OBJECT (avtpcvfpay, "Could not allocate buffer");
    return FALSE;
  }

  gst_buffer_map (avtpcvfpay->header, &map, GST_MAP_WRITE);
  pdu = (struct avtp_stream_pdu *) map.data;

  res = avtp_cvf_pdu_init (pdu, avtpcvfpay->format_subtype);
  g_assert (res == 0);

  res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_STREAM_ID, streamid);
  g_assert (res == 0);

  /* MJPEG payload header (8 bytes) will be filled per-frame in
   * prepare_avtp_packets_mjpeg; avtp_cvf_pdu_init already zeroed them. */

  gst_buffer_unmap (avtpcvfpay->header, &map);

  avtpcvfpay->header_size = header_size;

  return TRUE;
}

static GstStateChangeReturn
gst_avtp_cvf_change_state (GstElement * element, GstStateChange transition)
{
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (element);
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpcvfpay);
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
    if (!gst_avtp_cvf_pay_reset_header (avtpcvfpay, avtpbasepayload->streamid))
      return GST_STATE_CHANGE_FAILURE;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    return ret;
  }

  if (transition == GST_STATE_CHANGE_READY_TO_NULL) {
    gst_clear_buffer (&avtpcvfpay->header);
  }

  return ret;
}

static void
gst_avtp_cvf_pay_extract_nals (GstAvtpCvfPay * avtpcvfpay,
    GstBuffer * buffer, GPtrArray * nals)
{
  /* The buffer may have more than one NAL. They are grouped together, and before
   * each NAL there are some bytes that indicate how big is the NAL */

  gsize size, offset = 0;
  GstMapInfo map;
  guint8 *data;
  gboolean res;

  if (G_UNLIKELY (avtpcvfpay->nal_length_size == 0)) {
    GST_ERROR_OBJECT (avtpcvfpay,
        "Can't extract NAL units without nal length size. Missing codec_data caps?");
    goto end;
  }

  res = gst_buffer_map (buffer, &map, GST_MAP_READ);
  if (!res) {
    GST_ERROR_OBJECT (avtpcvfpay, "Could not map buffer");
    goto end;
  }

  size = map.size;
  data = map.data;

  while (size > avtpcvfpay->nal_length_size) {
    gint i;
    guint nal_len = 0;
    GstBuffer *nal;

    /* Gets NAL length */
    for (i = 0; i < avtpcvfpay->nal_length_size; i++) {
      nal_len = (nal_len << 8) + data[i];
    }

    if (nal_len == 0) {
      GST_WARNING_OBJECT (avtpcvfpay, "Invalid NAL unit size: 0");
      break;
    }

    offset += avtpcvfpay->nal_length_size;
    data += avtpcvfpay->nal_length_size;
    size -= avtpcvfpay->nal_length_size;

    if (G_UNLIKELY (size < nal_len)) {
      GST_WARNING_OBJECT (avtpcvfpay,
          "Got incomplete NAL: NAL len %u, buffer len %zu", nal_len, size);
      nal_len = size;
    }

    nal = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_ALL, offset, nal_len);
    GST_BUFFER_PTS (nal) = GST_BUFFER_PTS (buffer);
    GST_BUFFER_DTS (nal) = GST_BUFFER_DTS (buffer);
    g_ptr_array_add (nals, nal);

    offset += nal_len;
    data += nal_len;
    size -= nal_len;
  }

  gst_buffer_unmap (buffer, &map);

end:
  /* This function consumes the buffer, and all references to it are in the
   * extracted nals, so we can release the reference to the buffer itself */
  gst_buffer_unref (buffer);

  GST_LOG_OBJECT (avtpcvfpay, "Extracted %u NALu's from buffer", nals->len);
}

static gboolean
gst_avtp_cvf_pay_is_nal_vcl (GstAvtpCvfPay * avtpcvfpay, GstBuffer * nal)
{
  guint8 nal_header, nal_type;

  gst_buffer_extract (nal, 0, &nal_header, 1);
  nal_type = nal_header & NAL_TYPE_MASK;

  return nal_type >= FIRST_NAL_VCL_TYPE && nal_type <= LAST_NAL_VCL_TYPE;
}

static GstBuffer *
gst_avtpcvpay_fragment_nal (GstAvtpCvfPay * avtpcvfpay, GstBuffer * nal,
    gsize * offset, gboolean * last_fragment)
{
  GstAvtpVfPayBase *avtpvfpaybase = GST_AVTP_VF_PAY_BASE (avtpcvfpay);
  GstBuffer *fragment_header, *fragment;
  guint8 nal_header, nal_type, nal_nri, fu_indicator, fu_header;
  gsize available, nal_size, fragment_size, remaining;
  GstMapInfo map;

  nal_size = gst_buffer_get_size (nal);

  /* If NAL + header will be smaller than MTU, nothing to fragment */
  if (*offset == 0
      && (nal_size + AVTP_CVF_H264_HEADER_SIZE) <= avtpvfpaybase->mtu) {
    *last_fragment = TRUE;
    *offset = nal_size;
    GST_DEBUG_OBJECT (avtpcvfpay,
        "Generated fragment with size %" G_GSIZE_FORMAT, nal_size);
    return gst_buffer_ref (nal);
  }

  /* We're done with this buffer */
  if (*offset == nal_size) {
    return NULL;
  }

  *last_fragment = FALSE;

  /* Remaining size is smaller than MTU, so this is the last fragment */
  remaining = nal_size - *offset + AVTP_CVF_H264_HEADER_SIZE + FU_A_HEADER_SIZE;
  if (remaining <= avtpvfpaybase->mtu) {
    *last_fragment = TRUE;
  }

  fragment_header = gst_buffer_new_allocate (NULL, FU_A_HEADER_SIZE, NULL);
  if (G_UNLIKELY (fragment_header == NULL)) {
    GST_ERROR_OBJECT (avtpcvfpay, "Could not allocate memory for buffer");
    return NULL;
  }

  /* NAL header info is spread to all FUs */
  gst_buffer_extract (nal, 0, &nal_header, 1);
  nal_type = nal_header & NAL_TYPE_MASK;
  nal_nri = (nal_header & NRI_MASK) >> NRI_SHIFT;

  fu_indicator = (nal_nri << NRI_SHIFT) | FU_A_TYPE;
  fu_header = ((*offset == 0) << START_SHIFT) |
      ((*last_fragment == TRUE) << END_SHIFT) | nal_type;

  gst_buffer_map (fragment_header, &map, GST_MAP_WRITE);
  map.data[0] = fu_indicator;
  map.data[1] = fu_header;
  gst_buffer_unmap (fragment_header, &map);

  available =
      avtpvfpaybase->mtu - AVTP_CVF_H264_HEADER_SIZE -
      gst_buffer_get_size (fragment_header);

  /* NAL unit header is not sent, but spread into FU indicator and header,
   * and reconstructed on depayloader */
  if (*offset == 0)
    *offset = 1;

  fragment_size =
      available < (nal_size - *offset) ? available : (nal_size - *offset);

  fragment =
      gst_buffer_append (fragment_header, gst_buffer_copy_region (nal,
          GST_BUFFER_COPY_MEMORY, *offset, fragment_size));

  *offset += fragment_size;

  GST_DEBUG_OBJECT (avtpcvfpay,
      "Generated fragment with size %" G_GSIZE_FORMAT, fragment_size);

  return fragment;
}

/*
 * Scan a JPEG buffer to find the parameters needed for RFC 2435 MJPEG
 * packetization: image type (0=4:2:2, 1=4:2:0), dimensions, restart interval,
 * quantization table locations, and the offset of the entropy-coded scan data.
 */
typedef struct
{
  guint8 type;                  /* 0 = 4:2:2, 1 = 4:2:0; bit 6 set if DRI present */
  guint8 width;                 /* image width  in 8-pixel multiples (RFC 2435) */
  guint8 height;                /* image height in 8-pixel multiples (RFC 2435) */
  guint16 dri;                  /* restart interval (MCUs), 0 if none */
  guint8 precision;             /* qtable precision bitmask: bit0=luma, bit1=chroma */
  gsize qt_offset[2];           /* byte offsets of luma/chroma DQT data in buffer */
  guint8 qt_size[2];            /* table sizes: 64 (8-bit) or 128 (16-bit) */
  gsize scan_offset;            /* first byte of entropy-coded scan data */
} AvtpMjpegScanInfo;

static gboolean
gst_avtp_cvf_pay_scan_jpeg (GstAvtpCvfPay * avtpcvfpay, const guint8 * data,
    gsize size, AvtpMjpegScanInfo * info)
{
  gsize off = 0;
  gboolean sof_found = FALSE, dqt_found = FALSE;

  memset (info, 0, sizeof (*info));

  if (size < 2 || data[0] != 0xFF || data[1] != 0xD8) {
    GST_WARNING_OBJECT (avtpcvfpay, "Buffer does not start with JPEG SOI");
    return FALSE;
  }
  off = 2;

  while (off + 2 <= size) {
    guint8 marker;
    guint16 len;

    if (data[off] != 0xFF) {
      GST_WARNING_OBJECT (avtpcvfpay,
          "Expected 0xFF marker byte at offset %" G_GSIZE_FORMAT, off);
      return FALSE;
    }
    off++;
    marker = data[off++];

    /* Standalone markers: SOI, TEM, RST0-7 */
    if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7))
      continue;

    if (marker == JPEG_MARKER_EOI)
      break;

    /* All remaining markers carry a 2-byte length */
    if (off + 2 > size)
      break;
    len = (data[off] << 8) | data[off + 1];
    if (len < 2 || off + len > size)
      break;

    if (marker == JPEG_MARKER_SOF) {
      guint16 h, w;
      guint8 ncomp, y_samp;

      if (len < 17) {
        off += len;
        continue;
      }
      if (data[off + 2] != 8) {
        GST_WARNING_OBJECT (avtpcvfpay,
            "Only 8-bit precision JPEG is supported");
        off += len;
        continue;
      }
      h = (data[off + 3] << 8) | data[off + 4];
      w = (data[off + 5] << 8) | data[off + 6];
      ncomp = data[off + 7];
      if (ncomp != 3) {
        GST_WARNING_OBJECT (avtpcvfpay,
            "Only 3-component JPEG is supported, got %u", ncomp);
        off += len;
        continue;
      }
      /*
       * Determine RFC 2435 type from the ratio between Y (comp 0) and
       * Cb (comp 1) sampling factors rather than checking absolute values.
       * This handles any normalization an encoder may choose:
       *   type 0 (4:2:2): Y_h = 2*Cb_h, Y_v = Cb_v
       *   type 1 (4:2:0): Y_h = 2*Cb_h, Y_v = 2*Cb_v
       */
      y_samp = data[off + 9];   /* Y (comp 0) sampling factors */
      {
        guint8 cb_samp = data[off + 12];        /* Cb (comp 1) sampling factors */
        guint8 y_h = (y_samp >> 4) & 0xf;
        guint8 y_v = y_samp & 0xf;
        guint8 cb_h = (cb_samp >> 4) & 0xf;
        guint8 cb_v = cb_samp & 0xf;

        if (cb_h == 0 || cb_v == 0) {
          GST_WARNING_OBJECT (avtpcvfpay,
              "Invalid Cb sampling factors 0x%02x in JPEG SOF", cb_samp);
          return FALSE;
        }
        if (y_h == 2 * cb_h && y_v == cb_v)
          info->type = 0;       /* 4:2:2 */
        else if (y_h == 2 * cb_h && y_v == 2 * cb_v)
          info->type = 1;       /* 4:2:0 */
        else {
          GST_WARNING_OBJECT (avtpcvfpay,
              "Unsupported JPEG subsampling: Y=0x%02x Cb=0x%02x "
              "(RFC 2435 requires 4:2:2 or 4:2:0); add "
              "'videoconvert ! video/x-raw,format=I420' before jpegenc",
              y_samp, cb_samp);
          return FALSE;
        }
      }
      info->width = (guint8) (GST_ROUND_UP_8 (w > 0 ? w : 8) / 8);
      info->height = (guint8) (GST_ROUND_UP_8 (h > 0 ? h : 8) / 8);
      sof_found = TRUE;

    } else if (marker == JPEG_MARKER_DQT) {
      gsize toff = off + 2;
      gsize tend = off + len;

      while (toff < tend) {
        guint8 id_prec, id;
        guint tab_size;

        if (toff >= size)
          break;
        id_prec = data[toff++];
        id = id_prec & 0x0f;
        tab_size = ((id_prec >> 4) & 0x0f) ? 128 : 64;
        if (toff + tab_size > tend)
          break;
        if (id < 2) {
          info->qt_offset[id] = toff;
          info->qt_size[id] = (guint8) tab_size;
          if (tab_size == 128)
            info->precision |= (1 << id);
        }
        toff += tab_size;
      }
      dqt_found = TRUE;

    } else if (marker == JPEG_MARKER_DRI) {
      if (len >= 4)
        info->dri = (data[off + 2] << 8) | data[off + 3];

    } else if (marker == JPEG_MARKER_SOS) {
      /* scan data starts immediately after the SOS segment */
      info->scan_offset = off + len;
      if (info->dri)
        info->type |= 0x40;     /* signal DRI presence via type bit 6 */
      return sof_found && dqt_found
          && info->qt_size[0] > 0 && info->qt_size[1] > 0;
    }

    off += len;
  }

  return FALSE;
}

static gboolean
gst_avtp_cvf_pay_prepare_avtp_packets_h264 (GstAvtpVfPayBase * avtpvfpaybase,
    GstBuffer * buffer, GPtrArray * avtp_packets)
{
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpvfpaybase);
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (avtpvfpaybase);
  GPtrArray *nals;
  GstBuffer *header, *nal;
  GstMapInfo map;
  gint i;

  /* Get all NALs inside buffer */
  nals = g_ptr_array_new ();
  gst_avtp_cvf_pay_extract_nals (avtpcvfpay, buffer, nals);

  for (i = 0; i < nals->len; i++) {
    guint64 avtp_time, h264_time;
    gboolean last_fragment;
    GstBuffer *fragment;
    gsize offset;

    nal = g_ptr_array_index (nals, i);
    GST_LOG_OBJECT (avtpcvfpay,
        "Preparing AVTP packets for NAL whose size is %" G_GSIZE_FORMAT,
        gst_buffer_get_size (nal));

    /* Calculate timestamps. Note that we do it twice, one using DTS as base,
     * the other using PTS - using code inherited from avtpbasepayload.
     * Also worth noting: `avtpbasepayload->latency` is updated after
     * first call to gst_avtp_base_payload_calc_ptime, so we MUST call
     * it before using the latency value */
    h264_time = gst_avtp_base_payload_calc_ptime (avtpbasepayload, nal);

    avtp_time =
        gst_element_get_base_time (GST_ELEMENT (avtpcvfpay)) +
        gst_segment_to_running_time (&avtpbasepayload->segment, GST_FORMAT_TIME,
        GST_BUFFER_DTS_OR_PTS (nal)) + avtpbasepayload->mtt +
        avtpbasepayload->tu + avtpbasepayload->processing_deadline +
        avtpbasepayload->latency;

    offset = 0;
    while ((fragment =
            gst_avtpcvpay_fragment_nal (avtpcvfpay, nal, &offset,
                &last_fragment))) {
      GstBuffer *packet;
      struct avtp_stream_pdu *pdu;
      gint res GST_UNUSED_ASSERT;

      /* Copy header to reuse common fields and change what is needed */
      header = gst_buffer_copy (avtpcvfpay->header);
      gst_buffer_map (header, &map, GST_MAP_WRITE);
      pdu = (struct avtp_stream_pdu *) map.data;

      /* Stream data len includes AVTP H264 header len as this is part of
       * the payload too. It's just the uint32_t with the h264 timestamp*/
      res =
          avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN,
          gst_buffer_get_size (fragment) + sizeof (uint32_t));
      g_assert (res == 0);

      res =
          avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_SEQ_NUM,
          avtpbasepayload->seqnum++);
      g_assert (res == 0);

      /* Although AVTP_TIMESTAMP is only set on the very last fragment, IEEE 1722
       * doesn't mention such need for H264_TIMESTAMP. So, we set it for all
       * fragments */
      res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_H264_TIMESTAMP, h264_time);
      g_assert (res == 0);
      res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_H264_PTV, 1);
      g_assert (res == 0);

      /* Only last fragment should have M, AVTP_TS and TV fields set */
      if (last_fragment) {
        gboolean M;

        res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_TV, 1);
        g_assert (res == 0);

        res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_TIMESTAMP, avtp_time);
        g_assert (res == 0);

        /* Set M only if last NAL and it is a VCL NAL */
        M = (i == nals->len - 1)
            && gst_avtp_cvf_pay_is_nal_vcl (avtpcvfpay, nal);
        res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_M, M);
        g_assert (res == 0);

        if (M) {
          GST_LOG_OBJECT (avtpcvfpay, "M packet sent, PTS: %" GST_TIME_FORMAT
              " DTS: %" GST_TIME_FORMAT " AVTP_TS: %" GST_TIME_FORMAT
              " H264_TS: %" GST_TIME_FORMAT "\navtp_time: %" G_GUINT64_FORMAT
              " h264_time: %" G_GUINT64_FORMAT, GST_TIME_ARGS (h264_time),
              GST_TIME_ARGS (avtp_time), GST_TIME_ARGS (avtp_time & 0xffffffff),
              GST_TIME_ARGS (h264_time & 0xffffffff), avtp_time, h264_time);
        }
      }

      packet = gst_buffer_append (header, fragment);

      /* Keep original timestamps */
      GST_BUFFER_PTS (packet) = GST_BUFFER_PTS (nal);
      GST_BUFFER_DTS (packet) = GST_BUFFER_DTS (nal);

      g_ptr_array_add (avtp_packets, packet);

      gst_buffer_unmap (header, &map);
    }

    gst_buffer_unref (nal);
  }

  g_ptr_array_free (nals, TRUE);

  GST_LOG_OBJECT (avtpcvfpay, "Prepared %u AVTP packets", avtp_packets->len);

  return TRUE;
}

static gboolean
gst_avtp_cvf_pay_prepare_avtp_packets_mjpeg (GstAvtpVfPayBase * avtpvfpaybase,
    GstBuffer * buffer, GPtrArray * avtp_packets)
{
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpvfpaybase);
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (avtpvfpaybase);
  GstMapInfo map;
  AvtpMjpegScanInfo scan_info;
  gsize scan_data_size, frag_offset = 0;
  guint8 qt_data[256];          /* luma + chroma tables, max 2 × 128 bytes */
  guint qt_data_size;
  guint64 avtp_time;

  if (!gst_buffer_map (buffer, &map, GST_MAP_READ)) {
    GST_ERROR_OBJECT (avtpcvfpay, "Could not map input buffer");
    gst_buffer_unref (buffer);
    return FALSE;
  }

  if (!gst_avtp_cvf_pay_scan_jpeg (avtpcvfpay, map.data, map.size, &scan_info)) {
    GST_WARNING_OBJECT (avtpcvfpay, "Could not parse JPEG buffer, dropping it");
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
    return TRUE;                /* non-fatal: just skip this frame */
  }

  scan_data_size =
      map.size > scan_info.scan_offset ? map.size - scan_info.scan_offset : 0;
  if (scan_data_size == 0) {
    GST_WARNING_OBJECT (avtpcvfpay, "Empty scan data, dropping buffer");
    gst_buffer_unmap (buffer, &map);
    gst_buffer_unref (buffer);
    return TRUE;
  }

  /* Copy quantization tables into a contiguous array: luma first, then chroma */
  qt_data_size = scan_info.qt_size[0] + scan_info.qt_size[1];
  memcpy (qt_data, map.data + scan_info.qt_offset[0], scan_info.qt_size[0]);
  memcpy (qt_data + scan_info.qt_size[0],
      map.data + scan_info.qt_offset[1], scan_info.qt_size[1]);

  gst_avtp_base_payload_calc_ptime (avtpbasepayload, buffer);
  avtp_time =
      gst_element_get_base_time (GST_ELEMENT (avtpcvfpay)) +
      gst_segment_to_running_time (&avtpbasepayload->segment, GST_FORMAT_TIME,
      GST_BUFFER_DTS_OR_PTS (buffer)) + avtpbasepayload->mtt +
      avtpbasepayload->tu + avtpbasepayload->processing_deadline +
      avtpbasepayload->latency;

  do {
    GstBuffer *header, *fragment_data, *packet;
    GstMapInfo hdr_map;
    struct avtp_stream_pdu *pdu;
    guint8 *mjpeg_hdr;
    gsize avail_for_scan, frag_size;
    gboolean last_fragment;
    gint res GST_UNUSED_ASSERT;

    /*
     * How much scan data fits in this packet after all headers?
     * The pre-allocated header buffer holds AVTP_CVF_MJPEG_HEADER_SIZE bytes.
     * The first fragment additionally carries optional DRI header (4 bytes if
     * dri > 0) and the quantization table header (4 + qt_data_size bytes).
     */
    avail_for_scan = avtpvfpaybase->mtu - AVTP_CVF_MJPEG_HEADER_SIZE;
    if (frag_offset == 0) {
      if (scan_info.dri)
        avail_for_scan -= 4;    /* DRI marker header */
      avail_for_scan -= 4 + qt_data_size;       /* quant table header + data */
    }

    if ((gssize) avail_for_scan <= 0) {
      GST_ERROR_OBJECT (avtpcvfpay,
          "MTU too small to carry MJPEG headers + any scan data");
      break;
    }

    frag_size = MIN (scan_data_size - frag_offset, avail_for_scan);
    last_fragment = (frag_offset + frag_size >= scan_data_size);

    /* Build the optional-extras + scan-data fragment buffer */
    if (frag_offset == 0) {
      GstBuffer *extras;
      GstMapInfo ex_map;
      gsize extra_size = (scan_info.dri ? 4 : 0) + 4 + qt_data_size;
      guint8 *p;

      extras = gst_buffer_new_allocate (NULL, extra_size, NULL);
      gst_buffer_map (extras, &ex_map, GST_MAP_WRITE);
      p = ex_map.data;

      if (scan_info.dri) {
        /*
         * RFC 2435 §3.1.7 restart marker header:
         * restart_interval(16b) | F=1(1b) | L=1(1b) | count=0x3FFF(14b)
         */
        p[0] = scan_info.dri >> 8;
        p[1] = scan_info.dri & 0xFF;
        p[2] = 0xFF;
        p[3] = 0xFF;
        p += 4;
      }

      /* RFC 2435 §3.1.8 quantization table header: MBZ | precision | length */
      p[0] = 0;
      p[1] = scan_info.precision;
      p[2] = (guint8) (qt_data_size >> 8);
      p[3] = (guint8) (qt_data_size & 0xFF);
      p += 4;
      memcpy (p, qt_data, qt_data_size);

      gst_buffer_unmap (extras, &ex_map);
      fragment_data = gst_buffer_append (extras,
          gst_buffer_copy_region (buffer, GST_BUFFER_COPY_MEMORY,
              scan_info.scan_offset + frag_offset, frag_size));
    } else {
      fragment_data = gst_buffer_copy_region (buffer, GST_BUFFER_COPY_MEMORY,
          scan_info.scan_offset + frag_offset, frag_size);
    }

    /* Fill in AVTP CVF header fields */
    header = gst_buffer_copy (avtpcvfpay->header);
    gst_buffer_map (header, &hdr_map, GST_MAP_WRITE);
    pdu = (struct avtp_stream_pdu *) hdr_map.data;

    /*
     * RFC 2435 §3.1 MJPEG main header (8 bytes after the AVTP common header):
     *   type_specific (8b) | fragment_offset (24b) | type (8b) |
     *   Q (8b) | width (8b) | height (8b)
     * Q = 255: signals that custom quantization tables follow in the first
     *           fragment of every frame.
     */
    mjpeg_hdr = hdr_map.data + AVTP_CVF_COMMON_HEADER_SIZE;
    mjpeg_hdr[0] = 0;           /* type_specific */
    mjpeg_hdr[1] = (frag_offset >> 16) & 0xFF;
    mjpeg_hdr[2] = (frag_offset >> 8) & 0xFF;
    mjpeg_hdr[3] = frag_offset & 0xFF;
    mjpeg_hdr[4] = scan_info.type;
    mjpeg_hdr[5] = 255;         /* Q = 255: custom qtables */
    mjpeg_hdr[6] = scan_info.width;
    mjpeg_hdr[7] = scan_info.height;

    res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_STREAM_DATA_LEN,
        AVTP_CVF_MJPEG_PAYLOAD_HEADER_SIZE +
        gst_buffer_get_size (fragment_data));
    g_assert (res == 0);

    res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_SEQ_NUM,
        avtpbasepayload->seqnum++);
    g_assert (res == 0);

    if (last_fragment) {
      res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_TV, 1);
      g_assert (res == 0);
      res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_TIMESTAMP, avtp_time);
      g_assert (res == 0);
      res = avtp_cvf_pdu_set (pdu, AVTP_CVF_FIELD_M, 1);
      g_assert (res == 0);
    }

    packet = gst_buffer_append (header, fragment_data);
    GST_BUFFER_PTS (packet) = GST_BUFFER_PTS (buffer);
    GST_BUFFER_DTS (packet) = GST_BUFFER_DTS (buffer);
    g_ptr_array_add (avtp_packets, packet);

    gst_buffer_unmap (header, &hdr_map);

    frag_offset += frag_size;
  } while (frag_offset < scan_data_size);

  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  GST_LOG_OBJECT (avtpcvfpay, "Prepared %u AVTP MJPEG packets",
      avtp_packets->len);
  return TRUE;
}

static gboolean
gst_avtp_cvf_pay_prepare_avtp_packets (GstAvtpVfPayBase * avtpvfpaybase,
    GstBuffer * buffer, GPtrArray * avtp_packets)
{
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (avtpvfpaybase);
  if (avtpcvfpay->format_subtype == AVTP_CVF_FORMAT_SUBTYPE_MJPEG)
    return gst_avtp_cvf_pay_prepare_avtp_packets_mjpeg (avtpvfpaybase,
        buffer, avtp_packets);

  return gst_avtp_cvf_pay_prepare_avtp_packets_h264 (avtpvfpaybase,
      buffer, avtp_packets);
}

static gboolean
gst_avtp_cvf_pay_new_caps (GstAvtpVfPayBase * avtpvfpaybase, GstCaps * caps)
{
  GstAvtpBasePayload *avtpbasepayload = GST_AVTP_BASE_PAYLOAD (avtpvfpaybase);
  GstAvtpCvfPay *avtpcvfpay = GST_AVTP_CVF_PAY (avtpvfpaybase);
  const GValue *value;
  GstStructure *str;
  const gchar *media_type;
  GstBuffer *buffer;
  GstMapInfo map;

  str = gst_caps_get_structure (caps, 0);
  media_type = gst_structure_get_name (str);

  if (g_str_equal (media_type, "video/x-h264")) {
    avtpcvfpay->format_subtype = AVTP_CVF_FORMAT_SUBTYPE_H264;
  } else if (g_str_equal (media_type, "image/jpeg")) {
    avtpcvfpay->format_subtype = AVTP_CVF_FORMAT_SUBTYPE_MJPEG;
    avtpcvfpay->nal_length_size = 0;
  } else {
    GST_ERROR_OBJECT (avtpcvfpay, "Unsupported caps: %s", media_type);
    return FALSE;
  }

  if (!gst_avtp_cvf_pay_reset_header (avtpcvfpay, avtpbasepayload->streamid))
    return FALSE;

  /* MJPEG doesn't have codec_data, so we are done */
  if (avtpcvfpay->format_subtype == AVTP_CVF_FORMAT_SUBTYPE_MJPEG) 
    return TRUE;

  if ((value = gst_structure_get_value (str, "codec_data"))) {
    guint8 *data;
    gsize size;

    buffer = gst_value_get_buffer (value);
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    data = map.data;
    size = map.size;

    if (G_UNLIKELY (size < 7)) {
      GST_ERROR_OBJECT (avtpcvfpay, "avcC size %" G_GSIZE_FORMAT " < 7", size);
      goto error;
    }
    if (G_UNLIKELY (data[0] != 1)) {
      GST_ERROR_OBJECT (avtpcvfpay, "avcC version %u != 1", data[0]);
      goto error;
    }

    /* Number of bytes in front of NAL units marking their size */
    avtpcvfpay->nal_length_size = (data[4] & NAL_LEN_SIZE_MASK) + 1;
    GST_DEBUG_OBJECT (avtpcvfpay, "Got NAL length from caps: %u",
        avtpcvfpay->nal_length_size);

    gst_buffer_unmap (buffer, &map);
  }

  return TRUE;

error:
  gst_buffer_unmap (buffer, &map);
  return FALSE;
}
