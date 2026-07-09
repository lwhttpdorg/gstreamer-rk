/*
 * gst-plugins-android — c2aacdec: GstAudioDecoder bridging Codec2's
 * c2.android.aac.decoder SW component.
 *
 * Caps mirror gst-plugins-bad/ext/faad/gstfaad.c — both raw and ADTS framings
 * are accepted at the sink. set_format() parses codec_data (AudioSpecificConfig)
 * when present and passes it through to C2 as CSD; for ADTS streams the
 * decoder picks profile / rate / channels from each ADTS header.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "gstc2aacdec.h"
#include "gstc2common.h"

#include <gst/audio/audio.h>
#include <gst/pbutils/codec-utils.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_c2_aac_dec_debug);
#define GST_CAT_DEFAULT gst_c2_aac_dec_debug

struct _GstC2AacDec
{
  GstAudioDecoder parent;
  GstC2Component *c2;
  gint            rate;
  gint            channels;
  gboolean        negotiated;
};

/* We advertise stream-format=raw ONLY. aacparse upstream will transparently
 * de-frame ADTS/LOAS and hand us bare AAC access units plus the
 * AudioSpecificConfig as codec_data — exactly what C2SoftAacDec's RAW path
 * (aacDecoder_ConfigRaw + raw AUs) wants on upstream fdk-aac. This keeps all
 * the framing/transport handling in aacparse and out of the decoder. */
#define GST_C2_AAC_DEC_SINK_CAPS                                       \
    "audio/mpeg, "                                                     \
    "mpegversion = (int) {2, 4}, "                                     \
    "stream-format = (string) raw, "                                   \
    "channels = (int) [1, 8], rate = (int) [8000, 96000]"

#define GST_C2_AAC_DEC_SRC_CAPS                                        \
    "audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "          \
    "layout = (string) interleaved, "                                  \
    "rate = (int) [7350, 96000], channels = (int) [1, 8]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_AAC_DEC_SINK_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_AAC_DEC_SRC_CAPS));

G_DEFINE_TYPE (GstC2AacDec, gst_c2_aac_dec, GST_TYPE_AUDIO_DECODER);

static gboolean
gst_c2_aac_dec_start (GstAudioDecoder * dec)
{
  GstC2AacDec *self = GST_C2_AAC_DEC (dec);
  if (self->c2) {
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  self->c2 = gst_c2_component_new (GST_C2_CODEC_AAC);
  if (!self->c2) {
    GST_ERROR_OBJECT (self, "failed to create c2.android.aac.decoder");
    return FALSE;
  }
  if (!gst_c2_component_start (self->c2)) {
    GST_ERROR_OBJECT (self, "C2 start failed");
    return FALSE;
  }
  self->negotiated = FALSE;
  self->rate = self->channels = 0;
  return TRUE;
}

static gboolean
gst_c2_aac_dec_stop (GstAudioDecoder * dec)
{
  GstC2AacDec *self = GST_C2_AAC_DEC (dec);
  if (self->c2) {
    gst_c2_component_stop (self->c2);
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  return TRUE;
}

static gboolean
gst_c2_aac_dec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstC2AacDec *self = GST_C2_AAC_DEC (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint channels = 2, rate = 48000;
  const GValue *codec_data_v;
  GstBuffer *codec_data = NULL;

  gst_structure_get_int (s, "channels", &channels);
  gst_structure_get_int (s, "rate", &rate);

  codec_data_v = gst_structure_get_value (s, "codec_data");
  if (codec_data_v && GST_VALUE_HOLDS_BUFFER (codec_data_v))
    codec_data = gst_value_get_buffer (codec_data_v);

  if (codec_data) {
    GstMapInfo map;
    if (gst_buffer_map (codec_data, &map, GST_MAP_READ)) {
      /* Parse profile/rate/channels out of the AudioSpecificConfig so the
       * caps hints to C2 are accurate even before the first ADTS sync. */
      guint8 audio_object_type = 0, sr_idx = 0, ch_cfg = 0;
      if (map.size >= 2) {
        audio_object_type = (map.data[0] >> 3) & 0x1f;
        sr_idx = ((map.data[0] & 0x07) << 1) | ((map.data[1] >> 7) & 0x01);
        ch_cfg = (map.data[1] >> 3) & 0x0f;
        (void) audio_object_type;
        if (sr_idx < 13) {
          static const gint sr_table[13] = {
            96000, 88200, 64000, 48000, 44100, 32000,
            24000, 22050, 16000, 12000, 11025, 8000, 7350
          };
          rate = sr_table[sr_idx];
        }
        if (ch_cfg >= 1 && ch_cfg <= 7) channels = ch_cfg == 7 ? 8 : ch_cfg;
      }
      gst_c2_component_configure (self->c2, rate, channels, map.data, map.size);
      gst_buffer_unmap (codec_data, &map);
    }
  } else {
    /* ADTS / no codec_data: just hint, ADTS sync words drive the rest. */
    gst_c2_component_configure (self->c2, rate, channels, NULL, 0);
  }

  self->rate = rate;
  self->channels = channels;
  self->negotiated = FALSE;
  return TRUE;
}

/* Drain ready PCM from the bridge FIFO into GstAudioDecoder (see the matching
 * comment in gstc2opusdec.c). AAC has a one-frame decoder delay, so output
 * lags input by a frame and the tail is flushed at EOS via drain(). */
static GstFlowReturn
gst_c2_aac_dec_push_ready (GstAudioDecoder * dec)
{
  GstC2AacDec *self = GST_C2_AAC_DEC (dec);
  guint8 *pcm = NULL;
  gsize pcm_size = 0;
  gint out_rate = 0, out_channels = 0;
  GstFlowReturn ret = GST_FLOW_OK;

  while (gst_c2_component_pull (self->c2, &pcm, &pcm_size, &out_rate, &out_channels)) {
    if (pcm_size == 0) { g_free (pcm); continue; }

    if (!self->negotiated && out_rate > 0 && out_channels > 0) {
      GstAudioInfo info;
      gst_audio_info_init (&info);
      gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, out_rate, out_channels, NULL);
      if (!gst_audio_decoder_set_output_format (dec, &info)) {
        g_free (pcm);
        return GST_FLOW_NOT_NEGOTIATED;
      }
      self->negotiated = TRUE;
      self->rate = out_rate;
      self->channels = out_channels;
    }

    GstBuffer *out_buf = gst_buffer_new_wrapped (pcm, pcm_size);
    ret = gst_audio_decoder_finish_frame (dec, out_buf, 1);
    if (ret != GST_FLOW_OK)
      break;
  }
  return ret;
}

static GstFlowReturn
gst_c2_aac_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * in_buf)
{
  GstC2AacDec *self = GST_C2_AAC_DEC (dec);
  GstMapInfo  in = { 0 };
  GstFlowReturn ret;

  if (!in_buf) {
    gst_c2_component_drain (self->c2);
    return gst_c2_aac_dec_push_ready (dec);
  }

  if (!gst_buffer_map (in_buf, &in, GST_MAP_READ))
    return GST_FLOW_ERROR;

  ret = gst_c2_component_decode (self->c2,
      in.data, in.size,
      GST_BUFFER_PTS_IS_VALID (in_buf) ? GST_BUFFER_PTS (in_buf) : GST_CLOCK_TIME_NONE,
      FALSE);
  gst_buffer_unmap (in_buf, &in);

  if (ret != GST_FLOW_OK)
    return ret;

  return gst_c2_aac_dec_push_ready (dec);
}

static void
gst_c2_aac_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstC2AacDec *self = GST_C2_AAC_DEC (dec);
  (void) hard;
  if (self->c2)
    gst_c2_component_flush (self->c2);
}

static void
gst_c2_aac_dec_class_init (GstC2AacDecClass * klass)
{
  GstElementClass     *gec = (GstElementClass *) klass;
  GstAudioDecoderClass *gad = (GstAudioDecoderClass *) klass;

  gad->start         = GST_DEBUG_FUNCPTR (gst_c2_aac_dec_start);
  gad->stop          = GST_DEBUG_FUNCPTR (gst_c2_aac_dec_stop);
  gad->set_format    = GST_DEBUG_FUNCPTR (gst_c2_aac_dec_set_format);
  gad->handle_frame  = GST_DEBUG_FUNCPTR (gst_c2_aac_dec_handle_frame);
  gad->flush         = GST_DEBUG_FUNCPTR (gst_c2_aac_dec_flush);

  gst_element_class_add_static_pad_template (gec, &sink_template);
  gst_element_class_add_static_pad_template (gec, &src_template);
  gst_element_class_set_static_metadata (gec,
      "Codec2 AAC audio decoder (Android SW)",
      "Codec/Decoder/Audio",
      "Decodes AAC audio via the Android 16 Codec2 SW component (c2.android.aac.decoder)",
      "gst-plugins-android contributors");
}

static void
gst_c2_aac_dec_init (GstC2AacDec * self)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable    (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER (self), TRUE);
}

gboolean
gst_c2_aac_dec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_c2_aac_dec_debug, "c2aacdec", 0,
      "Codec2 AAC audio decoder");
  return gst_element_register (plugin, "c2aacdec",
      GST_RANK_PRIMARY - 1, GST_TYPE_C2_AAC_DEC);
}
