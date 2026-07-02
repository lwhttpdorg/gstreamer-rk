/*
 * Copyright 2026 Edward Hervey <edward@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbasetsmuxav1.h"
#include <gst/base/gstbitreader.h>
#include <gst/base/gstbitwriter.h>
#include <gst/mpegts/mpegts.h>
#include <gst/pbutils/pbutils.h>
#include <gst/codecparsers/gstav1parser.h>
#include <string.h>

#define GST_CAT_DEFAULT gst_base_ts_mux_debug

#define AV1_VIDEO_DESCRIPTOR_TAG 0x80

static gboolean
gst_av1_detect_hdr_wcg (GstStructure * s, guint8 * hdr_wcg_idc)
{
  const gchar *colorimetry;
  const gchar *mdi_str;
  const gchar *cll_str;

  if (!s || !hdr_wcg_idc)
    return FALSE;

  colorimetry = gst_structure_get_string (s, "colorimetry");

  /* Check for SDR (BT.709) */
  if (colorimetry && g_strcmp0 (colorimetry, "bt709") == 0) {
    *hdr_wcg_idc = 0;           /* SDR */
    return TRUE;
  }

  /* Check for WCG (BT.2020) */
  if (colorimetry && g_strstr_len (colorimetry, -1, "2020") != NULL) {
    /* BT.2020 container exceeds BT.709 */
    *hdr_wcg_idc = 1;           /* WCG only */
    return TRUE;
  }

  mdi_str = gst_structure_get_string (s, "mastering-display-info");
  cll_str = gst_structure_get_string (s, "content-light-level");
  /* If mastering-display-info or content-light-level is present,
   * it indicates HDR capability but we don't have specific HDR/WCG ID
   * mapping from AV1 spec, so use "no indication" (3) */
  if (mdi_str || cll_str) {
    /* Could be HDR, WCG, or both - spec allows 3 for this case */
    *hdr_wcg_idc = 3;
    return TRUE;
  }

  return TRUE;
}



GstMpegtsDescriptor *
gst_av1_create_video_descriptor (GstCaps * caps)
{
  GstBuffer *codec_data = NULL;
  GstStructure *s;
  guint8 hdr_wcg_idc = 0;
  GstMapInfo map;
  GstMpegtsDescriptor *desc = NULL;

  if (!caps)
    return NULL;

  s = gst_caps_get_structure (caps, 0);
  if (!s)
    return NULL;

  if (!gst_av1_detect_hdr_wcg (s, &hdr_wcg_idc)) {
    return NULL;
  }

  const GValue *codec_data_value = gst_structure_get_value (s, "codec_data");
  if (codec_data_value) {
    codec_data = gst_value_get_buffer (codec_data_value);
    /* Need a writable version */
    codec_data = gst_buffer_copy_deep (codec_data);
  } else {
    codec_data = gst_codec_utils_av1_create_av1c_from_caps (caps);
  }
  if (codec_data == NULL) {
    return NULL;
  }

  if (gst_buffer_map (codec_data, &map, GST_MAP_READWRITE)) {
    /* The AV1 descriptor contains the same content as the ISOBMFF AV1 Codec
     * Configuration Record, except that some reserved bits are used for
     * signalling HDR/WGC */
    map.data[3] = (map.data[3] & 0x3f) | (hdr_wcg_idc << 6);

    desc =
        gst_mpegts_descriptor_from_custom (AV1_VIDEO_DESCRIPTOR_TAG, map.data,
        map.size);
    gst_buffer_unmap (codec_data, &map);
  }
  gst_buffer_unref (codec_data);

  return desc;
}


GstBuffer *
gst_base_ts_mux_av1_prepare (GstBuffer * buf, GstBaseTsMuxPad * pad,
    GstBaseTsMux * mux)
{
  GstAV1Parser *parser = pad->prepare_data;
  GstAV1OBU obu;
  GstMapInfo in_map, out_map;
  GstBuffer *out_buf = NULL;
  guint8 *out_data;
  gsize out_size, in_size;
  gsize in_pos, out_pos;
  guint32 consumed;
  GstAV1ParserResult res;
  const guint8 *in_data;

  if (!buf || !parser)
    return NULL;

  if (!gst_buffer_map (buf, &in_map, GST_MAP_READ))
    return NULL;

  in_size = in_map.size;
  in_data = in_map.data;

  gst_av1_parser_reset (parser, FALSE);
  in_pos = 0;
  out_size = in_size;

  /* First loop to compute output size */
  while (in_pos < in_size) {
    memset (&obu, 0, sizeof (obu));

    res = gst_av1_parser_identify_one_obu (parser, in_data + in_pos,
        in_size - in_pos, &obu, &consumed);

    if (res != GST_AV1_PARSER_OK) {
      GST_ERROR_OBJECT (mux, "Failed to parse AV1 OBU: %d", res);
      goto error;
    }

    if (consumed == 0) {
      GST_ERROR_OBJECT (mux, "Parser consumed 0 bytes, something is wrong");
      goto error;
    }

    /* 3 extra bytes for the startcode before the OBU */
    out_size += 3;

    int k = 0;
    while (k < consumed) {
      /* Escape 0x0000{00,01,02,03} */
      if (k + 2 < consumed && in_data[in_pos + k] == 0x00 &&
          in_data[in_pos + k + 1] == 0x00 &&
          (in_data[in_pos + k + 2] <= 0x03)) {
        /* One extra byte for the escape byte */
        out_size += 1;
        k += 2;
      } else {
        k += 1;
      }
    }

    in_pos += consumed;
  }

  out_buf = gst_buffer_new_and_alloc (out_size);
  if (!out_buf)
    goto error;

  if (!gst_buffer_map (out_buf, &out_map, GST_MAP_WRITE))
    goto error;

  /* Second loop to actually write the data */
  gst_av1_parser_reset (parser, FALSE);
  out_data = out_map.data;
  out_pos = 0;
  in_pos = 0;

  while (in_pos < in_size) {
    memset (&obu, 0, sizeof (obu));

    res = gst_av1_parser_identify_one_obu (parser, in_data + in_pos,
        in_size - in_pos, &obu, &consumed);

    if (res != GST_AV1_PARSER_OK) {
      GST_ERROR_OBJECT (mux, "Failed to parse AV1 OBU: %d", res);
      goto error;
    }

    if (consumed == 0) {
      GST_ERROR_OBJECT (mux, "Parser consumed 0 bytes, something is wrong");
      goto error;
    }

    out_data[out_pos++] = 0x00;
    out_data[out_pos++] = 0x00;
    out_data[out_pos++] = 0x01;

    int k = 0;
    while (k < consumed) {
      /* Escape 0x0000{00,01,02,03} */
      if (k + 2 < consumed && in_data[in_pos + k] == 0x00 &&
          in_data[in_pos + k + 1] == 0x00
          && (in_data[in_pos + k + 2] <= 0x03)) {
        out_data[out_pos++] = in_data[in_pos + k++];
        out_data[out_pos++] = in_data[in_pos + k++];
        out_data[out_pos++] = 0x03;
      } else {
        out_data[out_pos++] = in_data[in_pos + k++];
      }
    }

    in_pos += consumed;
  }

  gst_buffer_unmap (out_buf, &out_map);
  gst_buffer_unmap (buf, &in_map);

  gst_buffer_copy_into (out_buf, buf,
      GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);

  GST_DEBUG_OBJECT (mux,
      "AV1 buffer prepared: %" G_GSIZE_FORMAT " -> %" G_GSIZE_FORMAT " bytes (%"
      G_GSIZE_FORMAT " OBUs)", in_size, gst_buffer_get_size (out_buf), in_pos);

  return out_buf;

error:
  gst_buffer_unmap (buf, &in_map);
  if (out_buf) {
    gst_buffer_unmap (out_buf, &out_map);
    gst_buffer_unref (out_buf);
  }
  return NULL;
}
