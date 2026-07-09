/*
 * gst-plugins-android — c2mp3dec: GstAudioDecoder bridging Codec2's
 * c2.android.mp3.decoder SW component (vendored PacketVideo pvmp3 decoder).
 *
 * MP3 is headerless: there is no codec_data/CSD. Each buffer from mpegaudioparse
 * is one MP3 frame (with its own 4-byte frame header), fed straight to the
 * decoder. So this element is the simplest of the set — just decode + pull,
 * with the shared in-flight/drain bridge.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "gstc2mp3dec.h"
#include "gstc2common.h"

#include <gst/audio/audio.h>

GST_DEBUG_CATEGORY_STATIC (gst_c2_mp3_dec_debug);
#define GST_CAT_DEFAULT gst_c2_mp3_dec_debug

struct _GstC2Mp3Dec
{
  GstAudioDecoder parent;
  GstC2Component *c2;
  gint            rate;
  gint            channels;
  gboolean        negotiated;
};

#define GST_C2_MP3_DEC_SINK_CAPS                                       \
    "audio/mpeg, mpegversion = (int) 1, layer = (int) [1, 3], "        \
    "parsed = (boolean) true, "                                        \
    "rate = (int) [8000, 48000], channels = (int) [1, 2]"

#define GST_C2_MP3_DEC_SRC_CAPS                                        \
    "audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "          \
    "layout = (string) interleaved, "                                  \
    "rate = (int) [8000, 48000], channels = (int) [1, 2]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_MP3_DEC_SINK_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_MP3_DEC_SRC_CAPS));

G_DEFINE_TYPE (GstC2Mp3Dec, gst_c2_mp3_dec, GST_TYPE_AUDIO_DECODER);

static gboolean
gst_c2_mp3_dec_start (GstAudioDecoder * dec)
{
  GstC2Mp3Dec *self = GST_C2_MP3_DEC (dec);
  if (self->c2) {
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  self->c2 = gst_c2_component_new (GST_C2_CODEC_MP3);
  if (!self->c2) {
    GST_ERROR_OBJECT (self, "failed to create c2.android.mp3.decoder");
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
gst_c2_mp3_dec_stop (GstAudioDecoder * dec)
{
  GstC2Mp3Dec *self = GST_C2_MP3_DEC (dec);
  if (self->c2) {
    gst_c2_component_stop (self->c2);
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  return TRUE;
}

static gboolean
gst_c2_mp3_dec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstC2Mp3Dec *self = GST_C2_MP3_DEC (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint channels = 2, rate = 44100;

  gst_structure_get_int (s, "channels", &channels);
  gst_structure_get_int (s, "rate", &rate);

  /* MP3 is headerless — no codec_data. The decoder derives rate/channels from
   * each frame header; we just pass the hints. */
  gst_c2_component_configure (self->c2, rate, channels, NULL, 0);

  self->rate = rate;
  self->channels = channels;
  self->negotiated = FALSE;
  return TRUE;
}

static GstFlowReturn
gst_c2_mp3_dec_push_ready (GstAudioDecoder * dec)
{
  GstC2Mp3Dec *self = GST_C2_MP3_DEC (dec);
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
gst_c2_mp3_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * in_buf)
{
  GstC2Mp3Dec *self = GST_C2_MP3_DEC (dec);
  GstMapInfo in = { 0 };
  GstFlowReturn ret;

  if (!in_buf) {
    gst_c2_component_drain (self->c2);
    return gst_c2_mp3_dec_push_ready (dec);
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

  return gst_c2_mp3_dec_push_ready (dec);
}

static void
gst_c2_mp3_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstC2Mp3Dec *self = GST_C2_MP3_DEC (dec);
  (void) hard;
  if (self->c2)
    gst_c2_component_flush (self->c2);
}

static void
gst_c2_mp3_dec_class_init (GstC2Mp3DecClass * klass)
{
  GstElementClass     *gec = (GstElementClass *) klass;
  GstAudioDecoderClass *gad = (GstAudioDecoderClass *) klass;

  gad->start         = GST_DEBUG_FUNCPTR (gst_c2_mp3_dec_start);
  gad->stop          = GST_DEBUG_FUNCPTR (gst_c2_mp3_dec_stop);
  gad->set_format    = GST_DEBUG_FUNCPTR (gst_c2_mp3_dec_set_format);
  gad->handle_frame  = GST_DEBUG_FUNCPTR (gst_c2_mp3_dec_handle_frame);
  gad->flush         = GST_DEBUG_FUNCPTR (gst_c2_mp3_dec_flush);

  gst_element_class_add_static_pad_template (gec, &sink_template);
  gst_element_class_add_static_pad_template (gec, &src_template);
  gst_element_class_set_static_metadata (gec,
      "Codec2 MP3 audio decoder (Android SW)",
      "Codec/Decoder/Audio",
      "Decodes MP3 audio via the Android 16 Codec2 SW component (c2.android.mp3.decoder)",
      "gst-plugins-android contributors");
}

static void
gst_c2_mp3_dec_init (GstC2Mp3Dec * self)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable    (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER (self), TRUE);
}

gboolean
gst_c2_mp3_dec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_c2_mp3_dec_debug, "c2mp3dec", 0,
      "Codec2 MP3 audio decoder");
  return gst_element_register (plugin, "c2mp3dec",
      GST_RANK_PRIMARY - 1, GST_TYPE_C2_MP3_DEC);
}
