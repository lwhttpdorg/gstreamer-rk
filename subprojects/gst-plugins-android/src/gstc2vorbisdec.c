/*
 * gst-plugins-android — c2vorbisdec: GstAudioDecoder bridging Codec2's
 * c2.android.vorbis.decoder SW component (vendored Tremolo fixed-point lib).
 *
 * Vorbis specifics handled here (see C2SoftVorbisDec::process):
 *  - Config = TWO header packets fed as separate FLAG_CODEC_CONFIG works:
 *    the identification header (type 0x01) and the setup/codebooks header
 *    (type 0x05). The comment header (type 0x03) is NOT consumed and must be
 *    dropped. oggdemux exposes all three via the caps "streamheader" array.
 *  - Every AUDIO packet must carry a trailing 4-byte little-endian int32
 *    "numPageFrames"; the component does `inSize -= 4` unconditionally and uses
 *    it for end-of-page trimming. We append -1 (= "no trimming, emit all").
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "gstc2vorbisdec.h"
#include "gstc2common.h"

#include <gst/audio/audio.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_c2_vorbis_dec_debug);
#define GST_CAT_DEFAULT gst_c2_vorbis_dec_debug

struct _GstC2VorbisDec
{
  GstAudioDecoder parent;
  GstC2Component *c2;
  gint            rate;
  gint            channels;
  gboolean        negotiated;
  gboolean        headers_sent;   /* id+setup submitted (from streamheader) */
};

#define GST_C2_VORBIS_DEC_SINK_CAPS  "audio/x-vorbis"

#define GST_C2_VORBIS_DEC_SRC_CAPS                                     \
    "audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "          \
    "layout = (string) interleaved, "                                  \
    "rate = (int) [1, 655350], channels = (int) [1, 8]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_VORBIS_DEC_SINK_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_VORBIS_DEC_SRC_CAPS));

G_DEFINE_TYPE (GstC2VorbisDec, gst_c2_vorbis_dec, GST_TYPE_AUDIO_DECODER);

/* A Vorbis header packet starts with a type byte (1/3/5) + the "vorbis" magic. */
static inline gboolean
gst_c2_is_vorbis_header (const guint8 * d, gsize n)
{
  return n > 6 && (d[0] == 0x01 || d[0] == 0x03 || d[0] == 0x05) &&
      memcmp (d + 1, "vorbis", 6) == 0;
}

static gboolean
gst_c2_vorbis_dec_start (GstAudioDecoder * dec)
{
  GstC2VorbisDec *self = GST_C2_VORBIS_DEC (dec);
  if (self->c2) {
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  self->c2 = gst_c2_component_new (GST_C2_CODEC_VORBIS);
  if (!self->c2) {
    GST_ERROR_OBJECT (self, "failed to create c2.android.vorbis.decoder");
    return FALSE;
  }
  if (!gst_c2_component_start (self->c2)) {
    GST_ERROR_OBJECT (self, "C2 start failed");
    return FALSE;
  }
  self->negotiated = FALSE;
  self->headers_sent = FALSE;
  self->rate = self->channels = 0;
  return TRUE;
}

static gboolean
gst_c2_vorbis_dec_stop (GstAudioDecoder * dec)
{
  GstC2VorbisDec *self = GST_C2_VORBIS_DEC (dec);
  if (self->c2) {
    gst_c2_component_stop (self->c2);
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  return TRUE;
}

/* Submit a header buffer if it is the id (1) or setup (5) packet; drop comment (3). */
static void
gst_c2_vorbis_submit_header (GstC2VorbisDec * self, GstBuffer * buf)
{
  GstMapInfo map;
  if (!gst_buffer_map (buf, &map, GST_MAP_READ))
    return;
  if (map.size > 6 && (map.data[0] == 0x01 || map.data[0] == 0x05) &&
      memcmp (map.data + 1, "vorbis", 6) == 0) {
    gst_c2_component_queue_csd (self->c2, map.data, map.size);
  }
  gst_buffer_unmap (buf, &map);
}

static gboolean
gst_c2_vorbis_dec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstC2VorbisDec *self = GST_C2_VORBIS_DEC (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint channels = 2, rate = 48000;
  const GValue *streamheader;

  gst_structure_get_int (s, "channels", &channels);
  gst_structure_get_int (s, "rate", &rate);
  gst_c2_component_configure (self->c2, rate, channels, NULL, 0);

  /* oggdemux exposes [id(1), comment(3), setup(5)] in streamheader; feed id +
   * setup as two CSD works, skip comment. */
  streamheader = gst_structure_get_value (s, "streamheader");
  if (GST_VALUE_HOLDS_ARRAY (streamheader)) {
    guint i, n = gst_value_array_get_size (streamheader);
    for (i = 0; i < n; i++) {
      const GValue *v = gst_value_array_get_value (streamheader, i);
      if (GST_VALUE_HOLDS_BUFFER (v))
        gst_c2_vorbis_submit_header (self, gst_value_get_buffer (v));
    }
    self->headers_sent = TRUE;
  }

  self->rate = rate;
  self->channels = channels;
  self->negotiated = FALSE;
  return TRUE;
}

static GstFlowReturn
gst_c2_vorbis_dec_push_ready (GstAudioDecoder * dec)
{
  GstC2VorbisDec *self = GST_C2_VORBIS_DEC (dec);
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
gst_c2_vorbis_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * in_buf)
{
  GstC2VorbisDec *self = GST_C2_VORBIS_DEC (dec);
  GstMapInfo in = { 0 };
  GstFlowReturn ret;
  guint8 *pkt;

  if (!in_buf) {
    gst_c2_component_drain (self->c2);
    return gst_c2_vorbis_dec_push_ready (dec);
  }

  if (!gst_buffer_map (in_buf, &in, GST_MAP_READ))
    return GST_FLOW_ERROR;

  /* Vorbis header packets: configured already (from streamheader). If they
   * also arrive in-band, drop them. If no streamheader was present, feed the
   * id/setup ones here as a fallback. */
  if (gst_c2_is_vorbis_header (in.data, in.size)) {
    if (!self->headers_sent &&
        (in.data[0] == 0x01 || in.data[0] == 0x05)) {
      gst_c2_component_queue_csd (self->c2, in.data, in.size);
    }
    gst_buffer_unmap (in_buf, &in);
    return gst_audio_decoder_finish_frame (dec, NULL, 1);
  }

  /* Audio packet: append the mandatory 4-byte LE numPageFrames trailer (-1 =
   * no end-of-page trimming). */
  pkt = (guint8 *) g_malloc (in.size + 4);
  memcpy (pkt, in.data, in.size);
  pkt[in.size + 0] = 0xFF;
  pkt[in.size + 1] = 0xFF;
  pkt[in.size + 2] = 0xFF;
  pkt[in.size + 3] = 0xFF;   /* int32 little-endian -1 */

  ret = gst_c2_component_decode (self->c2, pkt, in.size + 4,
      GST_BUFFER_PTS_IS_VALID (in_buf) ? GST_BUFFER_PTS (in_buf) : GST_CLOCK_TIME_NONE,
      FALSE);
  g_free (pkt);
  gst_buffer_unmap (in_buf, &in);

  if (ret != GST_FLOW_OK)
    return ret;

  return gst_c2_vorbis_dec_push_ready (dec);
}

static void
gst_c2_vorbis_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstC2VorbisDec *self = GST_C2_VORBIS_DEC (dec);
  (void) hard;
  if (self->c2)
    gst_c2_component_flush (self->c2);
}

static void
gst_c2_vorbis_dec_class_init (GstC2VorbisDecClass * klass)
{
  GstElementClass     *gec = (GstElementClass *) klass;
  GstAudioDecoderClass *gad = (GstAudioDecoderClass *) klass;

  gad->start         = GST_DEBUG_FUNCPTR (gst_c2_vorbis_dec_start);
  gad->stop          = GST_DEBUG_FUNCPTR (gst_c2_vorbis_dec_stop);
  gad->set_format    = GST_DEBUG_FUNCPTR (gst_c2_vorbis_dec_set_format);
  gad->handle_frame  = GST_DEBUG_FUNCPTR (gst_c2_vorbis_dec_handle_frame);
  gad->flush         = GST_DEBUG_FUNCPTR (gst_c2_vorbis_dec_flush);

  gst_element_class_add_static_pad_template (gec, &sink_template);
  gst_element_class_add_static_pad_template (gec, &src_template);
  gst_element_class_set_static_metadata (gec,
      "Codec2 Vorbis audio decoder (Android SW)",
      "Codec/Decoder/Audio",
      "Decodes Vorbis audio via the Android 16 Codec2 SW component (c2.android.vorbis.decoder)",
      "gst-plugins-android contributors");
}

static void
gst_c2_vorbis_dec_init (GstC2VorbisDec * self)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable    (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER (self), TRUE);
}

gboolean
gst_c2_vorbis_dec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_c2_vorbis_dec_debug, "c2vorbisdec", 0,
      "Codec2 Vorbis audio decoder");
  return gst_element_register (plugin, "c2vorbisdec",
      GST_RANK_PRIMARY - 1, GST_TYPE_C2_VORBIS_DEC);
}
