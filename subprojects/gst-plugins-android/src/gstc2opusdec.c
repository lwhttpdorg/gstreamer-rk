/*
 * gst-plugins-android — c2opusdec: GstAudioDecoder bridging Codec2's
 * c2.android.opus.decoder SW component.
 *
 * Skeleton mirrors gst-plugins-base/ext/opus/gstopusdec.c. Differences:
 *   - decode body is gst_c2_component_decode() instead of opus_multistream_decode()
 *   - the OpusHead stream-header is forwarded to C2 as codec_data (AOSP code
 *     wraps it in a C2StreamCsdInfo internally on first work).
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "gstc2opusdec.h"
#include "gstc2common.h"

#include <gst/audio/audio.h>
#include <gst/pbutils/codec-utils.h>

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_c2_opus_dec_debug);
#define GST_CAT_DEFAULT gst_c2_opus_dec_debug

struct _GstC2OpusDec
{
  GstAudioDecoder parent;
  GstC2Component *c2;
  gint            rate;
  gint            channels;
  gboolean        negotiated;
};

#define GST_C2_OPUS_DEC_SINK_CAPS                                          \
    "audio/x-opus, channel-mapping-family = (int) 0; "                     \
    "audio/x-opus, channel-mapping-family = (int) [1, 255], "              \
    "channels = (int) [1, 255], stream-count = (int) [1, 255], "           \
    "coupled-count = (int) [0, 255]"

#define GST_C2_OPUS_DEC_SRC_CAPS                                           \
    "audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "              \
    "layout = (string) interleaved, "                                      \
    "rate = (int) { 48000, 24000, 16000, 12000, 8000 }, "                  \
    "channels = (int) [1, 8]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_OPUS_DEC_SINK_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_C2_OPUS_DEC_SRC_CAPS));

G_DEFINE_TYPE (GstC2OpusDec, gst_c2_opus_dec, GST_TYPE_AUDIO_DECODER);

static gboolean
gst_c2_opus_dec_start (GstAudioDecoder * dec)
{
  GstC2OpusDec *self = GST_C2_OPUS_DEC (dec);
  if (self->c2) {
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  self->c2 = gst_c2_component_new (GST_C2_CODEC_OPUS);
  if (!self->c2) {
    GST_ERROR_OBJECT (self, "failed to create c2.android.opus.decoder");
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
gst_c2_opus_dec_stop (GstAudioDecoder * dec)
{
  GstC2OpusDec *self = GST_C2_OPUS_DEC (dec);
  if (self->c2) {
    gst_c2_component_stop (self->c2);
    gst_c2_component_free (self->c2);
    self->c2 = NULL;
  }
  return TRUE;
}

/* AOSP's C2SoftOpusDec expects its codec-specific data either as 3 separate
 * input buffers (OpusHead, then two 8-byte int64 ns values for codec delay and
 * seek pre-roll) or as a single "unified" CSD blob carrying all three behind
 * AOPUS markers. We build the unified blob — it's the cleanest single-shot
 * config. Layout (see frameworks/av .../foundation/OpusHeader.cpp):
 *   "AOPUSHDR" <u64 LE size> <OpusHead bytes>
 *   "AOPUSDLY" <u64 LE 8>    <i64 LE codec-delay ns>
 *   "AOPUSPRL" <u64 LE 8>    <i64 LE seek-preroll ns>
 */
#define GST_C2_OPUS_PREROLL_NS  (80LL * 1000000LL)   /* 80 ms, the Opus standard */

static void
gst_c2_put_u64le (guint8 * p, guint64 v)
{
  for (int i = 0; i < 8; i++)
    p[i] = (guint8) (v >> (8 * i));
}

/* Returns a newly-allocated unified CSD (caller g_free's), *out_len set. */
static guint8 *
gst_c2_build_opus_csd (const guint8 * head, gsize head_size, gsize * out_len)
{
  guint16 preskip;
  gint64 codec_delay_ns, preroll_ns;
  gsize total;
  guint8 *csd, *p;

  if (head_size < 19)
    return NULL;

  /* OpusHead pre-skip: little-endian uint16 at byte offset 10, in 48 kHz samples. */
  preskip = (guint16) (head[10] | (head[11] << 8));
  codec_delay_ns = (gint64) preskip * 1000000000LL / 48000LL;
  preroll_ns = GST_C2_OPUS_PREROLL_NS;

  total = (8 + 8 + head_size) + (8 + 8 + 8) + (8 + 8 + 8);
  csd = g_malloc (total);
  p = csd;

  memcpy (p, "AOPUSHDR", 8); p += 8;
  gst_c2_put_u64le (p, head_size); p += 8;
  memcpy (p, head, head_size); p += head_size;

  memcpy (p, "AOPUSDLY", 8); p += 8;
  gst_c2_put_u64le (p, 8); p += 8;
  gst_c2_put_u64le (p, (guint64) codec_delay_ns); p += 8;

  memcpy (p, "AOPUSPRL", 8); p += 8;
  gst_c2_put_u64le (p, 8); p += 8;
  gst_c2_put_u64le (p, (guint64) preroll_ns); p += 8;

  *out_len = total;
  return csd;
}

static gboolean
gst_c2_opus_dec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstC2OpusDec *self = GST_C2_OPUS_DEC (dec);
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint channels = 2, rate = 48000;
  const GValue *streamheader;
  GstBuffer *opus_head = NULL;

  gst_structure_get_int (s, "channels", &channels);
  gst_structure_get_int (s, "rate", &rate);

  /* OpusHead lives in the streamheader: array of buffers, first one = OpusHead.
   * We forward it as codec_data so AOSP's SoftOpus dec can pick channel count,
   * preskip and gain straight from the bytes. */
  streamheader = gst_structure_get_value (s, "streamheader");
  if (GST_VALUE_HOLDS_ARRAY (streamheader)
      && gst_value_array_get_size (streamheader) > 0) {
    const GValue *v = gst_value_array_get_value (streamheader, 0);
    if (GST_VALUE_HOLDS_BUFFER (v))
      opus_head = gst_value_get_buffer (v);
  }

  if (opus_head) {
    GstMapInfo map;
    if (gst_buffer_map (opus_head, &map, GST_MAP_READ)) {
      gsize csd_len = 0;
      guint8 *csd = gst_c2_build_opus_csd (map.data, map.size, &csd_len);
      if (csd) {
        gst_c2_component_configure (self->c2, rate, channels, csd, csd_len);
        g_free (csd);
      } else {
        /* Fall back to the raw OpusHead (legacy single-header path). */
        gst_c2_component_configure (self->c2, rate, channels, map.data, map.size);
      }
      gst_buffer_unmap (opus_head, &map);
    }
  } else {
    /* No header explicitly — pass channels/rate only, decoder will see CSD
     * on first frame for some packet types. */
    gst_c2_component_configure (self->c2, rate, channels, NULL, 0);
  }

  self->rate = rate;
  self->channels = channels;
  self->negotiated = FALSE;   /* finalised after first decoded frame */
  return TRUE;
}

/* Drain every PCM buffer currently ready in the bridge FIFO, negotiating the
 * output format on the first one, and hand each to GstAudioDecoder. Returns the
 * last finish_frame() flow (or GST_FLOW_OK if nothing was ready — the input
 * frame then stays pending and is associated with a later output, which is the
 * correct behaviour for a decoder with pipeline delay). */
static GstFlowReturn
gst_c2_opus_dec_push_ready (GstAudioDecoder * dec)
{
  GstC2OpusDec *self = GST_C2_OPUS_DEC (dec);
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
gst_c2_opus_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * in_buf)
{
  GstC2OpusDec *self = GST_C2_OPUS_DEC (dec);
  GstMapInfo  in = { 0 };
  GstFlowReturn ret;

  if (!in_buf) {
    /* EOS / drain: flush the decoder's pipeline-delay tail, then push it out. */
    gst_c2_component_drain (self->c2);
    return gst_c2_opus_dec_push_ready (dec);
  }

  if (!gst_buffer_map (in_buf, &in, GST_MAP_READ))
    return GST_FLOW_ERROR;

  /* oggdemux forwards the two Opus setup packets (OpusHead, OpusTags) as
   * ordinary buffers; they are NOT audio and opus_multistream_decode would
   * reject them (OPUS_INVALID_PACKET). The configuration was already taken
   * from streamheader in set_format, so drop any header packet here — exactly
   * as the stock opusdec does. */
  if (in.size >= 8 &&
      (memcmp (in.data, "OpusHead", 8) == 0 ||
       memcmp (in.data, "OpusTags", 8) == 0)) {
    gst_buffer_unmap (in_buf, &in);
    return gst_audio_decoder_finish_frame (dec, NULL, 1);
  }

  ret = gst_c2_component_decode (self->c2,
      in.data, in.size,
      GST_BUFFER_PTS_IS_VALID (in_buf) ? GST_BUFFER_PTS (in_buf) : GST_CLOCK_TIME_NONE,
      FALSE);
  gst_buffer_unmap (in_buf, &in);

  if (ret != GST_FLOW_OK)
    return ret;

  return gst_c2_opus_dec_push_ready (dec);
}

static void
gst_c2_opus_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstC2OpusDec *self = GST_C2_OPUS_DEC (dec);
  (void) hard;
  if (self->c2)
    gst_c2_component_flush (self->c2);
}

static void
gst_c2_opus_dec_class_init (GstC2OpusDecClass * klass)
{
  GstElementClass     *gec = (GstElementClass *) klass;
  GstAudioDecoderClass *gad = (GstAudioDecoderClass *) klass;

  gad->start         = GST_DEBUG_FUNCPTR (gst_c2_opus_dec_start);
  gad->stop          = GST_DEBUG_FUNCPTR (gst_c2_opus_dec_stop);
  gad->set_format    = GST_DEBUG_FUNCPTR (gst_c2_opus_dec_set_format);
  gad->handle_frame  = GST_DEBUG_FUNCPTR (gst_c2_opus_dec_handle_frame);
  gad->flush         = GST_DEBUG_FUNCPTR (gst_c2_opus_dec_flush);

  gst_element_class_add_static_pad_template (gec, &sink_template);
  gst_element_class_add_static_pad_template (gec, &src_template);
  gst_element_class_set_static_metadata (gec,
      "Codec2 Opus audio decoder (Android SW)",
      "Codec/Decoder/Audio",
      "Decodes Opus audio via the Android 16 Codec2 SW component (c2.android.opus.decoder)",
      "gst-plugins-android contributors");
}

static void
gst_c2_opus_dec_init (GstC2OpusDec * self)
{
  /* Tell GstAudioDecoder we drain naturally and need set_format to run. */
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable    (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER (self), TRUE);
}

gboolean
gst_c2_opus_dec_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_c2_opus_dec_debug, "c2opusdec", 0,
      "Codec2 Opus audio decoder");
  return gst_element_register (plugin, "c2opusdec",
      GST_RANK_PRIMARY - 1, GST_TYPE_C2_OPUS_DEC);
}
