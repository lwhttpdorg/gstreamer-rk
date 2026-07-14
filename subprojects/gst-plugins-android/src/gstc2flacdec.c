/*
 * gst-plugins-android — c2flacdec: GstAudioDecoder bridging Codec2's
 * c2.android.flac.decoder SW component (libstagefright_flacdec over libFLAC).
 *
 * FLAC config = the 'fLaC' stream marker + STREAMINFO (+ any metadata blocks),
 * which flacparse exposes as the caps "streamheader" array (same mechanism as
 * Opus's OpusHead). We forward that blob once as a FLAG_CODEC_CONFIG work;
 * C2SoftFlacDec runs it through libFLAC's metadata parser. Each subsequent
 * buffer from flacparse is one parsed FLAC frame.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "gstc2flacdec.h"
#include "gstc2common.h"

#include <gst/audio/audio.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_c2_flac_dec_debug);
#define GST_CAT_DEFAULT gst_c2_flac_dec_debug

struct _GstC2FlacDec
{
  GstAudioDecoder parent;
  GstC2Component *c2;
  gint            rate;
  gint            channels;
  gboolean        negotiated;
};

/* flacparse src pad: framed FLAC, with the header sequence in streamheader. */
#define GST_C2_FLAC_DEC_SINK_CAPS                                      \
    "audio/x-flac, framed = (boolean) true, "                          \
    "channels = (int) [1, 8], rate = (int) [1, 655350]"

#define GST_C2_FLAC_DEC_SRC_CAPS                                       \
    "audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "          \
    "layout = (string) interleaved, "                                  \
    "rate = (int) [1, 655350], channels = (int) [1, 8]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_FLAC_DEC_SINK_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_FLAC_DEC_SRC_CAPS));

G_DEFINE_TYPE (GstC2FlacDec, gst_c2_flac_dec, GST_TYPE_AUDIO_DECODER);

static gboolean
gst_c2_flac_dec_start (GstAudioDecoder * dec)
{
  GstC2FlacDec *self = GST_C2_FLAC_DEC (dec);
  if (self->c2) {
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  self->c2 = gst_c2_component_new (GST_C2_CODEC_FLAC);
  if (!self->c2) {
    GST_ERROR_OBJECT (self, "failed to create c2.android.flac.decoder");
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
gst_c2_flac_dec_stop (GstAudioDecoder * dec)
{
  GstC2FlacDec *self = GST_C2_FLAC_DEC (dec);
  if (self->c2) {
    gst_c2_component_stop (self->c2);
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  return TRUE;
}

static gboolean
gst_c2_flac_dec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstC2FlacDec *self = GST_C2_FLAC_DEC (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint channels = 2, rate = 44100;
  const GValue *streamheader;
  GstBuffer *hdr = NULL;

  gst_structure_get_int (s, "channels", &channels);
  gst_structure_get_int (s, "rate", &rate);

  /* The FLAC header sequence ('fLaC' + STREAMINFO + metadata blocks) lives in
   * the caps streamheader array — concatenate all entries so libFLAC's
   * metadata parser sees a complete header section. */
  streamheader = gst_structure_get_value (s, "streamheader");
  if (GST_VALUE_HOLDS_ARRAY (streamheader) &&
      gst_value_array_get_size (streamheader) > 0) {
    GstBuffer *blob = gst_buffer_new ();
    guint i, n = gst_value_array_get_size (streamheader);
    for (i = 0; i < n; i++) {
      const GValue *v = gst_value_array_get_value (streamheader, i);
      if (GST_VALUE_HOLDS_BUFFER (v))
        blob = gst_buffer_append (blob, gst_buffer_ref (gst_value_get_buffer (v)));
    }
    hdr = blob;
  }

  if (hdr) {
    GstMapInfo map;
    if (gst_buffer_map (hdr, &map, GST_MAP_READ)) {
      gst_c2_component_configure (self->c2, rate, channels, map.data, map.size);
      gst_buffer_unmap (hdr, &map);
    }
    gst_buffer_unref (hdr);
  } else {
    gst_c2_component_configure (self->c2, rate, channels, NULL, 0);
  }

  self->rate = rate;
  self->channels = channels;
  self->negotiated = FALSE;
  return TRUE;
}

/* Drain ready PCM from the bridge FIFO into GstAudioDecoder (shared pattern). */
static GstFlowReturn
gst_c2_flac_dec_push_ready (GstAudioDecoder * dec)
{
  GstC2FlacDec *self = GST_C2_FLAC_DEC (dec);
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
gst_c2_flac_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * in_buf)
{
  GstC2FlacDec *self = GST_C2_FLAC_DEC (dec);
  GstMapInfo in = { 0 };
  GstFlowReturn ret;

  if (!in_buf) {
    gst_c2_component_drain (self->c2);
    return gst_c2_flac_dec_push_ready (dec);
  }

  /* flacparse may re-push the header buffers in-band; the config was already
   * submitted from streamheader in set_format, so drop header-flagged ones. */
  if (GST_BUFFER_FLAG_IS_SET (in_buf, GST_BUFFER_FLAG_HEADER))
    return gst_audio_decoder_finish_frame (dec, NULL, 1);

  if (!gst_buffer_map (in_buf, &in, GST_MAP_READ))
    return GST_FLOW_ERROR;

  ret = gst_c2_component_decode (self->c2,
      in.data, in.size,
      GST_BUFFER_PTS_IS_VALID (in_buf) ? GST_BUFFER_PTS (in_buf) : GST_CLOCK_TIME_NONE,
      FALSE);
  gst_buffer_unmap (in_buf, &in);

  if (ret != GST_FLOW_OK)
    return ret;

  return gst_c2_flac_dec_push_ready (dec);
}

static void
gst_c2_flac_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstC2FlacDec *self = GST_C2_FLAC_DEC (dec);
  (void) hard;
  if (self->c2)
    gst_c2_component_flush (self->c2);
}

static void
gst_c2_flac_dec_class_init (GstC2FlacDecClass * klass)
{
  GstElementClass     *gec = (GstElementClass *) klass;
  GstAudioDecoderClass *gad = (GstAudioDecoderClass *) klass;

  gad->start         = GST_DEBUG_FUNCPTR (gst_c2_flac_dec_start);
  gad->stop          = GST_DEBUG_FUNCPTR (gst_c2_flac_dec_stop);
  gad->set_format    = GST_DEBUG_FUNCPTR (gst_c2_flac_dec_set_format);
  gad->handle_frame  = GST_DEBUG_FUNCPTR (gst_c2_flac_dec_handle_frame);
  gad->flush         = GST_DEBUG_FUNCPTR (gst_c2_flac_dec_flush);

  gst_element_class_add_static_pad_template (gec, &sink_template);
  gst_element_class_add_static_pad_template (gec, &src_template);
  gst_element_class_set_static_metadata (gec,
      "Codec2 FLAC audio decoder (Android SW)",
      "Codec/Decoder/Audio",
      "Decodes FLAC audio via the Android 16 Codec2 SW component (c2.android.flac.decoder)",
      "gst-plugins-android contributors");
}

static void
gst_c2_flac_dec_init (GstC2FlacDec * self)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable    (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER (self), TRUE);
}

gboolean
gst_c2_flac_dec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_c2_flac_dec_debug, "c2flacdec", 0,
      "Codec2 FLAC audio decoder");
  return gst_element_register (plugin, "c2flacdec",
      GST_RANK_PRIMARY - 1, GST_TYPE_C2_FLAC_DEC);
}
