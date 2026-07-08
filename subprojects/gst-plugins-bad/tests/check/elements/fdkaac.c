/* GStreamer unit test for fdk-aac elements
 *
 * Copyright (C) 2022 Tim-Philipp Müller <tim centricular com>
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

#include <gst/check/check.h>
#include <gst/app/app.h>
#include <gst/audio/audio.h>
#include <gst/pbutils/pbutils.h>

typedef struct
{
  const gchar *base_profile;
  const gchar *profile;
  guint8 buf_hdr[16];
  guint8 codec_data[16];
  gsize codec_data_len;
} AacSample;

static AacSample
encode_with_caps (const gchar * caps_str)
{
  GstElement *pipe, *sink;
  gchar *pipeline_str;
  GError *err = NULL;
  GstSample *sample;
  AacSample aac_sample = { NULL, };

  GST_INFO ("Encoding to %s", caps_str);

  pipeline_str =
      g_strdup_printf ("audiotestsrc ! fdkaacenc ! appsink name=sink caps=%s",
      caps_str);

  pipe = gst_parse_launch (pipeline_str, &err);
  g_free (pipeline_str);
  fail_if (err != NULL, "Error creating pipeline: %s", err->message);

  sink = gst_bin_get_by_name (GST_BIN (pipe), "sink");

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  sample = gst_app_sink_pull_preroll (GST_APP_SINK (sink));
  fail_unless (sample != NULL);
  {
    GstCaps *caps = gst_sample_get_caps (sample);
    GstStructure *s = gst_caps_get_structure (caps, 0);
    const GValue *val;
    GstMapInfo map;
    GstBuffer *buf;

    GST_INFO ("Got caps %" GST_PTR_FORMAT, caps);
    aac_sample.base_profile =
        g_intern_string (gst_structure_get_string (s, "base-profile"));
    aac_sample.profile =
        g_intern_string (gst_structure_get_string (s, "profile"));

    val = gst_structure_get_value (s, "codec_data");
    if (val != NULL) {
      buf = gst_value_get_buffer (val);
      gst_buffer_map (buf, &map, GST_MAP_READ);
      g_assert_cmpuint (map.size, <=, 16);
      memcpy (aac_sample.codec_data, map.data, map.size);
      aac_sample.codec_data_len = map.size;
      gst_buffer_unmap (buf, &map);
    } else {
      aac_sample.codec_data_len = 0;
    }

    buf = gst_sample_get_buffer (sample);
    gst_buffer_map (buf, &map, GST_MAP_READ);
    g_assert_cmpuint (map.size, >=, sizeof (aac_sample.buf_hdr));
    memcpy (aac_sample.buf_hdr, map.data, sizeof (aac_sample.buf_hdr));
    gst_buffer_unmap (buf, &map);
  }
  gst_sample_unref (sample);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (sink);
  gst_object_unref (pipe);

  return aac_sample;
}

typedef enum
{
  AAC_AOT_UNKNOWN = -1,
  AAC_AOT_MAIN = 1,
  AAC_AOT_LC = 2,
  AAC_AOT_SSR = 3,
  AAC_AOT_LTP = 4,
  AAC_AOT_SBR = 5,
  AAC_AOT_LD = 23,
  AAC_AOT_PS = 29,
} AacAOT;

static AacAOT
aac_codec_data_get_aot (const guint8 * codec_data, gsize codec_data_len)
{
  AacAOT aot = AAC_AOT_UNKNOWN;

  if (codec_data_len >= 2) {
    guint8 hdr_aot = codec_data[0] >> 3;        // 5 bits
    switch (hdr_aot) {
      case AAC_AOT_MAIN:
      case AAC_AOT_LC:
      case AAC_AOT_SSR:
      case AAC_AOT_LTP:
      case AAC_AOT_SBR:
      case AAC_AOT_LD:
      case AAC_AOT_PS:
        aot = hdr_aot;
        break;
      default:
        break;
    }
  }

  return aot;
}

GST_START_TEST (test_fdkaacenc_adts)
{
  // LC profile
  {
    AacSample aac;

    aac = encode_with_caps ("audio/mpeg,profile=lc,stream-format=adts");
    fail_unless_equals_string (aac.base_profile, "lc");
    fail_unless_equals_string (aac.profile, "lc");
    fail_unless (aac.codec_data_len == 0);

    // ADTS sync
    fail_unless_equals_int (GST_READ_UINT16_BE (aac.buf_hdr) & 0xfff0, 0xfff0);

    // MPEG version (0 = MPEG-4)
    fail_unless_equals_int (aac.buf_hdr[1] & 0x08, 0);

    // AOT minus 1 (2=LC)
    fail_unless_equals_int ((aac.buf_hdr[2] & 0xC0) >> 6, 2 - 1);
  }

  // HE-AACv1 profile
  {
    AacSample aac;

    aac = encode_with_caps ("audio/mpeg,profile=he-aac-v1,stream-format=adts");
    fail_unless_equals_string (aac.base_profile, "lc");
    fail_unless_equals_string (aac.profile, "he-aac-v1");
    fail_unless (aac.codec_data_len == 0);

    // ADTS sync
    fail_unless_equals_int (GST_READ_UINT16_BE (aac.buf_hdr) & 0xfff0, 0xfff0);

    // MPEG version (0 = MPEG-4)
    fail_unless_equals_int (aac.buf_hdr[1] & 0x08, 0);

    // AOT minus 1 (2=LC)
    fail_unless_equals_int ((aac.buf_hdr[2] & 0xC0) >> 6, 2 - 1);
  }

  // HE-AACv2 profile
  {
    AacSample aac;

    aac = encode_with_caps ("audio/mpeg,profile=he-aac-v2,stream-format=adts");
    fail_unless_equals_string (aac.base_profile, "lc");
    fail_unless_equals_string (aac.profile, "he-aac-v2");
    fail_unless (aac.codec_data_len == 0);

    // ADTS sync
    fail_unless_equals_int (GST_READ_UINT16_BE (aac.buf_hdr) & 0xfff0, 0xfff0);

    // MPEG version (0 = MPEG-4)
    fail_unless_equals_int (aac.buf_hdr[1] & 0x08, 0);

    // AOT minus 1 (2=LC)
    fail_unless_equals_int ((aac.buf_hdr[2] & 0xC0) >> 6, 2 - 1);
  }
}

GST_END_TEST;

GST_START_TEST (test_fdkaacenc_raw)
{
  const gchar *profile;
  AacAOT aot;

  // LC profile
  {
    AacSample aac;

    aac = encode_with_caps ("audio/mpeg,profile=lc,stream-format=raw");
    fail_unless_equals_string (aac.base_profile, "lc");
    fail_unless_equals_string (aac.profile, "lc");
    fail_unless (aac.codec_data_len == 2);
    profile =
        gst_codec_utils_aac_get_profile (aac.codec_data, aac.codec_data_len);
    fail_unless_equals_string (profile, "lc");
    aot = aac_codec_data_get_aot (aac.codec_data, aac.codec_data_len);
    fail_unless_equals_int (aot, AAC_AOT_LC);
  }

  // HE-AACv1 profile
  {
    AacSample aac;

    aac = encode_with_caps ("audio/mpeg,profile=he-aac-v1,stream-format=raw");
    // For raw we use non-backwards compatible hierarchical signaling, so
    // caps should not contain a base-profile field
    fail_if (aac.base_profile != NULL);
    fail_unless (aac.profile != NULL);
    fail_unless (aac.codec_data_len > 2);
    profile =
        gst_codec_utils_aac_get_profile (aac.codec_data, aac.codec_data_len);
    fail_unless_equals_string (profile, "lc");  // FIXME: is this right for non-BC hierarchical signaling?
    aot = aac_codec_data_get_aot (aac.codec_data, aac.codec_data_len);
    fail_unless_equals_int (aot, AAC_AOT_SBR);
  }

  // HE-AACv2 profile
  {
    AacSample aac;

    aac = encode_with_caps ("audio/mpeg,profile=he-aac-v2,stream-format=raw");
    // For raw we use non-backwards compatible hierarchical signaling, so
    // caps should not contain a base-profile field
    fail_if (aac.base_profile != NULL);
    fail_unless (aac.profile != NULL);
    fail_unless (aac.codec_data_len > 2);
    profile =
        gst_codec_utils_aac_get_profile (aac.codec_data, aac.codec_data_len);
    fail_unless_equals_string (profile, "lc");  // FIXME: is this right for non-BC hierarchical signaling?
    aot = aac_codec_data_get_aot (aac.codec_data, aac.codec_data_len);
    fail_unless_equals_int (aot, AAC_AOT_PS);
  }
}

GST_END_TEST;

static void
run_clipping_check (const gchar * launch_line, guint64 samples_in,
    guint64 frame_samples, guint rate)
{
  GstElement *pipe, *sink, *enc;
  GstClockTime min_latency;
  GError *err = NULL;
  GstSample *sample;
  guint64 samples_out = 0, trim_start_total = 0, trim_end_total = 0;
  gboolean in_start_trims = TRUE, in_end_trims = FALSE;

  pipe = gst_parse_launch (launch_line, &err);
  fail_if (err != NULL, "Error creating pipeline: %s", err->message);
  sink = gst_bin_get_by_name (GST_BIN (pipe), "sink");

  gst_element_set_state (pipe, GST_STATE_PLAYING);

  while ((sample = gst_app_sink_pull_sample (GST_APP_SINK (sink)))) {
    GstBuffer *buf = gst_sample_get_buffer (sample);
    GstAudioClippingMeta *cmeta = gst_buffer_get_audio_clipping_meta (buf);

    samples_out += frame_samples;

    /* Padding trims must be a contiguous suffix of the stream */
    if (in_end_trims)
      fail_unless (cmeta && cmeta->end > 0, "unclipped buffer after trim_end");

    if (cmeta) {
      fail_unless_equals_int (cmeta->format, GST_FORMAT_DEFAULT);
      fail_unless (cmeta->start + cmeta->end <= frame_samples);

      if (cmeta->start) {
        /* Priming trims must be a packed contiguous prefix of the stream:
         * only the last one may clip less than a full frame */
        fail_unless (in_start_trims,
            "unexpected trim_start after unclipped samples");
        trim_start_total += cmeta->start;
        if (cmeta->start < frame_samples)
          in_start_trims = FALSE;
      } else {
        in_start_trims = FALSE;
      }
      if (cmeta->end) {
        in_end_trims = TRUE;
        trim_end_total += cmeta->end;
      }
    } else {
      in_start_trims = FALSE;
    }

    gst_sample_unref (sample);
  }

  /* The encoder delay, as reported through the latency, must be clipped
   * at the start and the padding added when draining at the end, so that
   * exactly the input samples remain */
  enc = gst_bin_get_by_name (GST_BIN (pipe), "enc");
  gst_audio_encoder_get_latency (GST_AUDIO_ENCODER (enc), &min_latency, NULL);
  gst_object_unref (enc);
  fail_unless (min_latency > 0);
  fail_unless_equals_uint64 (trim_start_total,
      gst_util_uint64_scale_round (min_latency, rate, GST_SECOND));
  fail_unless_equals_uint64 (samples_out - trim_start_total - trim_end_total,
      samples_in);

  gst_element_set_state (pipe, GST_STATE_NULL);
  gst_object_unref (sink);
  gst_object_unref (pipe);
}

GST_START_TEST (test_fdkaacenc_clipping)
{
  /* AAC-LC: 1024 samples/frame, the delay spans multiple frames */
  run_clipping_check ("audiotestsrc samplesperbuffer=1000 num-buffers=10 "
      "! audio/x-raw,rate=48000,channels=2 ! fdkaacenc name=enc "
      "! appsink name=sink sync=false caps=audio/mpeg,profile=lc",
      10 * 1000, 1024, 48000);

  /* HE-AACv1: 2048 samples/frame, SBR delay included in the trims */
  run_clipping_check ("audiotestsrc samplesperbuffer=500 num-buffers=4 "
      "! audio/x-raw,rate=48000,channels=2 ! fdkaacenc name=enc "
      "! appsink name=sink sync=false caps=audio/mpeg,profile=he-aac-v1",
      4 * 500, 2048, 48000);

  /* Input shorter than the delay: the last buffer carries both the
   * remaining trim_start and the trim_end padding */
  run_clipping_check ("audiotestsrc samplesperbuffer=10 num-buffers=1 "
      "! audio/x-raw,rate=48000,channels=2 ! fdkaacenc name=enc "
      "! appsink name=sink sync=false caps=audio/mpeg,profile=he-aac-v1",
      10, 2048, 48000);
}

GST_END_TEST;

static Suite *
fdkaac_suite (void)
{
  Suite *s = suite_create ("fdk-aac");
  TCase *tc_chain_enc = tcase_create ("fdkaacenc");

  suite_add_tcase (s, tc_chain_enc);
  tcase_add_test (tc_chain_enc, test_fdkaacenc_adts);
  tcase_add_test (tc_chain_enc, test_fdkaacenc_raw);
  tcase_add_test (tc_chain_enc, test_fdkaacenc_clipping);

  return s;
}

GST_CHECK_MAIN (fdkaac);
