/*
 * gst-plugins-android — c2iamfdec: IAMF (Immersive Audio Model & Formats)
 * audio decoder, backed by AOM's libiamf reference decoder.
 *
 * Why this one is different
 * -------------------------
 * The other five elements in this plugin wrap Android 16 Codec2 SW components.
 * AOSP *names* a `c2.android.iamf.decoder`, but in android-16.0.0_r1 it is an
 * empty stub (Android.bp srcs:[], process() is a no-op). So IAMF cannot follow
 * the same "wrap a C2 component" pattern; instead we link AOM's libiamf
 * (BSD-3-Clause-Clear) — vendored under iamf/upstream/ — and drive it here.
 *
 * Input model
 * -----------
 * There is no GStreamer IAMF demuxer/parser, and IAMF's descriptors live
 * in-band. So we take the raw IAMF OBU bytestream (audio/x-iamf, e.g. straight
 * from filesrc) and feed it to libiamf's incremental, consume-as-you-go API:
 *
 *     IAMF_decoder_configure(dec, buf, len, &consumed)   // once: descriptor OBUs
 *     IAMF_decoder_decode   (dec, buf, len, &consumed, pcm)  // loop: temporal units
 *     IAMF_decoder_decode   (dec, NULL, 0, &consumed, pcm)   // EOS: flush delay
 *
 * We accumulate input in a GstAdapter and advance by `consumed` after each
 * call, leaving any partial trailing OBU for the next buffer. Output is
 * interleaved S16, rendered to a selectable sound system (default stereo) at
 * IAMF's canonical 48 kHz (libiamf does not expose the native rate before the
 * first decode, so we pin the output rate to keep caps deterministic).
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "gstc2iamfdec.h"

#include <gst/audio/audio.h>
#include <gst/base/gstadapter.h>
#include <string.h>

#include "IAMF_decoder.h"
#include "IAMF_defines.h"

GST_DEBUG_CATEGORY_STATIC (gst_c2_iamf_dec_debug);
#define GST_CAT_DEFAULT gst_c2_iamf_dec_debug

/* IAMF is canonically 48 kHz, 16-bit interleaved for our output. */
#define IAMF_OUT_RATE      48000
#define IAMF_OUT_BPS       2          /* bytes per sample (S16) */

#define DEFAULT_SOUND_SYSTEM  0       /* SOUND_SYSTEM_A == 0+2+0 stereo */
#define DEFAULT_BINAURAL      FALSE

enum
{
  PROP_0,
  PROP_SOUND_SYSTEM,
  PROP_BINAURAL,
};

struct _GstC2IamfDec
{
  GstAudioDecoder parent;

  IAMF_DecoderHandle dec;
  GstAdapter        *adapter;

  /* properties */
  gint      sound_system;       /* IAMF_SoundSystem value (-1..12) */
  gboolean  binaural;

  /* negotiated state */
  gboolean  configured;         /* descriptor OBUs parsed */
  gboolean  negotiated;         /* output caps set */
  gint      channels;
  gint      rate;
  guint     max_frame_size;     /* samples/channel — sizes the pcm scratch */

  guint8   *pcm;                /* interleaved S16 scratch */
  gsize     pcm_size;
};

#define GST_C2_IAMF_DEC_SINK_CAPS  "audio/x-iamf"

/* libiamf renders up to 24 channels (9+10+3); size positions generously. */
#define GST_C2_IAMF_MAX_CHANNELS 24

#define GST_C2_IAMF_DEC_SRC_CAPS                                       \
    "audio/x-raw, format = (string) " GST_AUDIO_NE (S16) ", "          \
    "layout = (string) interleaved, "                                  \
    "rate = (int) [ 1, 192000 ], "                                     \
    "channels = (int) [ 1, 24 ]"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS (GST_C2_IAMF_DEC_SINK_CAPS));

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS (GST_C2_IAMF_DEC_SRC_CAPS));

G_DEFINE_TYPE (GstC2IamfDec, gst_c2_iamf_dec, GST_TYPE_AUDIO_DECODER);

/* ------------------------------------------------------------------------- */

/* Best-effort GStreamer channel positions for the IAMF interleave order of the
 * common sound systems. Unmapped layouts fall back to NONE (unpositioned) so
 * caps still negotiate; mono/stereo use the GStreamer default order. */
static gboolean
iamf_fill_positions (gint ss, gint ch, GstAudioChannelPosition * pos)
{
  switch (ss) {
    case 1:                     /* SOUND_SYSTEM_B: 0+5+0 (5.1): L R C LFE Ls Rs */
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      pos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      pos[3] = GST_AUDIO_CHANNEL_POSITION_LFE1;
      pos[4] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
      pos[5] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
      return ch == 6;
    case 8:                     /* SOUND_SYSTEM_I: 0+7+0 (7.1): L R C LFE Lss Rss Lrs Rrs */
      pos[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      pos[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      pos[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      pos[3] = GST_AUDIO_CHANNEL_POSITION_LFE1;
      pos[4] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
      pos[5] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
      pos[6] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
      pos[7] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      return ch == 8;
    default:
      break;
  }
  return FALSE;
}

static gboolean
gst_c2_iamf_dec_set_output_format (GstC2IamfDec * self)
{
  GstAudioInfo info;
  GstAudioChannelPosition pos[GST_C2_IAMF_MAX_CHANNELS];
  gint ch = self->channels;

  gst_audio_info_init (&info);

  if (!self->binaural && ch > 2 && iamf_fill_positions (self->sound_system, ch, pos)) {
    gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, self->rate, ch, pos);
  } else if (ch > 2) {
    /* Unknown multichannel layout: present as unpositioned. */
    gint i;
    for (i = 0; i < ch && i < GST_C2_IAMF_MAX_CHANNELS; i++)
      pos[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
    gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, self->rate, ch, pos);
  } else {
    /* mono / stereo (incl. binaural): default GStreamer positions. */
    gst_audio_info_set_format (&info, GST_AUDIO_FORMAT_S16, self->rate, ch, NULL);
  }

  return gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (self), &info);
}

/* ------------------------------------------------------------------------- */

static gboolean
gst_c2_iamf_dec_open_decoder (GstC2IamfDec * self)
{
  self->dec = IAMF_decoder_open ();
  if (!self->dec) {
    GST_ERROR_OBJECT (self, "IAMF_decoder_open failed");
    return FALSE;
  }
  IAMF_decoder_set_bit_depth (self->dec, IAMF_OUT_BPS * 8);
  IAMF_decoder_set_sampling_rate (self->dec, IAMF_OUT_RATE);
  /* 0 == no loudness normalization (preserve original levels). */
  IAMF_decoder_set_normalization_loudness (self->dec, 0.0f);

  if (self->binaural) {
    IAMF_decoder_output_layout_set_binaural (self->dec);
  } else {
    IAMF_decoder_output_layout_set_sound_system (self->dec,
        (IAMF_SoundSystem) self->sound_system);
  }
  return TRUE;
}

static gboolean
gst_c2_iamf_dec_start (GstAudioDecoder * dec)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (dec);

  if (!self->adapter)
    self->adapter = gst_adapter_new ();
  else
    gst_adapter_clear (self->adapter);

  self->configured = FALSE;
  self->negotiated = FALSE;
  self->channels = 0;
  self->rate = IAMF_OUT_RATE;
  self->max_frame_size = 0;

  return gst_c2_iamf_dec_open_decoder (self);
}

static gboolean
gst_c2_iamf_dec_stop (GstAudioDecoder * dec)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (dec);

  if (self->dec) {
    IAMF_decoder_close (self->dec);
    self->dec = NULL;
  }
  if (self->adapter)
    gst_adapter_clear (self->adapter);
  g_clear_pointer (&self->pcm, g_free);
  self->pcm_size = 0;
  return TRUE;
}

static gboolean
gst_c2_iamf_dec_set_format (GstAudioDecoder * dec, GstCaps * caps)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (dec);
  GstStructure *s;
  const GValue *cd_val;
  GstBuffer *cd_buf;

  /* When IAMF comes from a container (MP4/fMP4), the descriptor OBUs (IA
   * Sequence Header, Codec Config, Audio Element, Mix Presentation) are
   * delivered out-of-band in codec_data rather than in-band in the stream.
   * Feed them into the adapter first so IAMF_decoder_configure() sees them
   * before the first temporal unit arrives in handle_frame(). */
  s = gst_caps_get_structure (caps, 0);
  cd_val = gst_structure_get_value (s, "codec_data");
  if (cd_val && GST_VALUE_HOLDS_BUFFER (cd_val)) {
    cd_buf = gst_value_get_buffer (cd_val);
    if (cd_buf && gst_buffer_get_size (cd_buf) > 0) {
      GST_INFO_OBJECT (self,
          "set_format: injecting %" G_GSIZE_FORMAT " bytes of codec_data as IAMF descriptors",
          gst_buffer_get_size (cd_buf));
      /* Reset decoder state for format changes (e.g. track switch). */
      if (self->configured) {
        IAMF_decoder_close (self->dec);
        self->dec = NULL;
        self->configured = FALSE;
        self->negotiated = FALSE;
        gst_adapter_clear (self->adapter);
        gst_c2_iamf_dec_open_decoder (self);
      }
      gst_adapter_push (self->adapter, gst_buffer_ref (cd_buf));
    }
  }
  return TRUE;
}

/* GstAudioDecoder's default getcaps proxies the downstream audio/x-raw
 * rate/channels onto the sink media type (reporting
 * "audio/x-iamf, rate=..., channels=..."). The IAMF typefinder only suggests a
 * bare "audio/x-iamf", which then fails the accept-caps subset check and breaks
 * decodebin/playbin autoplugging. Return the plain sink template instead — the
 * real format is discovered in-band, not from caps. */
static GstCaps *
gst_c2_iamf_dec_getcaps (GstAudioDecoder * dec, GstCaps * filter)
{
  GstCaps *tmpl, *res;

  tmpl = gst_pad_get_pad_template_caps (GST_AUDIO_DECODER_SINK_PAD (dec));
  if (filter) {
    res = gst_caps_intersect_full (filter, tmpl, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmpl);
  } else {
    res = tmpl;
  }
  return res;
}

/* Raw .iamf has no parser/demuxer, so a bare `filesrc ! c2iamfdec` delivers a
 * BYTES segment — which GstAudioDecoder cannot operate on. Rewrite it to an
 * open TIME segment so the base class drives us normally; output timestamps
 * then come from the decoded sample counts. */
static gboolean
gst_c2_iamf_dec_sink_event (GstAudioDecoder * dec, GstEvent * event)
{
  if (GST_EVENT_TYPE (event) == GST_EVENT_SEGMENT) {
    const GstSegment *seg = NULL;
    gst_event_parse_segment (event, &seg);
    if (seg && seg->format != GST_FORMAT_TIME) {
      GstSegment tseg;
      GstEvent *tev;
      gst_segment_init (&tseg, GST_FORMAT_TIME);
      tev = gst_event_new_segment (&tseg);
      gst_event_unref (event);
      event = tev;
    }
  }
  return GST_AUDIO_DECODER_CLASS (gst_c2_iamf_dec_parent_class)->sink_event (dec, event);
}

/* Flush the decoder's internal delay at EOS by issuing a single NULL decode
 * (mirrors the libiamf reference CLI, which calls this exactly once — looping
 * on it never terminates because the decoder keeps emitting a tail frame). */
static GstFlowReturn
gst_c2_iamf_dec_drain_delay (GstC2IamfDec * self)
{
  GstAudioDecoder *dec = GST_AUDIO_DECODER (self);
  uint32_t rsize = 0;
  int samples;

  if (!self->configured || !self->pcm)
    return GST_FLOW_OK;

  samples = IAMF_decoder_decode (self->dec, NULL, 0, &rsize, self->pcm);
  if (samples > 0) {
    gsize bytes = (gsize) IAMF_OUT_BPS * samples * self->channels;
    return gst_audio_decoder_finish_frame (dec,
        gst_buffer_new_memdup (self->pcm, bytes), 1);
  }
  return GST_FLOW_OK;
}

/* Configure-once then decode-loop over whatever is in the adapter, advancing
 * by the bytes libiamf reports consumed and leaving the remainder buffered. */
static GstFlowReturn
gst_c2_iamf_dec_process (GstC2IamfDec * self, gboolean draining)
{
  GstAudioDecoder *dec = GST_AUDIO_DECODER (self);
  GstFlowReturn ret = GST_FLOW_OK;
  gsize avail = gst_adapter_available (self->adapter);
  const guint8 *data;
  gsize used = 0;

  if (avail == 0)
    return GST_FLOW_OK;

  data = gst_adapter_map (self->adapter, avail);
  if (!data)
    return GST_FLOW_ERROR;

  /* 1. Parse descriptor OBUs exactly once. */
  if (!self->configured) {
    uint32_t rsize = 0;
    int r = IAMF_decoder_configure (self->dec, data, (uint32_t) avail, &rsize);
    if (r == IAMF_OK) {
      IAMF_StreamInfo *info = IAMF_decoder_get_stream_info (self->dec);
      self->max_frame_size = (info && info->max_frame_size) ? info->max_frame_size : 8192;
      self->channels = self->binaural
          ? IAMF_layout_binaural_channels_count ()
          : IAMF_layout_sound_system_channels_count ((IAMF_SoundSystem) self->sound_system);
      if (self->channels <= 0)
        self->channels = 2;
      self->rate = IAMF_OUT_RATE;
      self->pcm_size = (gsize) IAMF_OUT_BPS * self->max_frame_size * self->channels;
      self->pcm = g_realloc (self->pcm, self->pcm_size);
      self->configured = TRUE;
      used += rsize;
      GST_INFO_OBJECT (self,
          "configured: %d channels, %d Hz, max_frame_size %u (consumed %u descriptor bytes)",
          self->channels, self->rate, self->max_frame_size, rsize);
    } else if (r == IAMF_ERR_BUFFER_TOO_SMALL) {
      /* Need more descriptor bytes; keep them buffered for the next buffer. */
      gst_adapter_unmap (self->adapter);
      if (draining)
        GST_WARNING_OBJECT (self, "EOS before a complete IAMF descriptor");
      return GST_FLOW_OK;
    } else {
      gst_adapter_unmap (self->adapter);
      GST_ELEMENT_ERROR (self, STREAM, DECODE, (NULL),
          ("IAMF_decoder_configure failed: %d", r));
      return GST_FLOW_ERROR;
    }
  }

  /* 2. First output: fix the output caps. */
  if (!self->negotiated) {
    if (!gst_c2_iamf_dec_set_output_format (self)) {
      gst_adapter_unmap (self->adapter);
      return GST_FLOW_NOT_NEGOTIATED;
    }
    self->negotiated = TRUE;
  }

  /* 3. Decode complete temporal units while data remains. */
  while (used < avail) {
    uint32_t rsize = 0;
    int samples = IAMF_decoder_decode (self->dec, data + used,
        (int32_t) (avail - used), &rsize, self->pcm);

    if (samples > 0) {
      gsize bytes = (gsize) IAMF_OUT_BPS * samples * self->channels;
      ret = gst_audio_decoder_finish_frame (dec,
          gst_buffer_new_memdup (self->pcm, bytes), 1);
    }

    used += rsize;

    if (samples == IAMF_ERR_INVALID_STATE) {
      GST_WARNING_OBJECT (self, "IAMF decoder needs reconfiguration");
      break;
    }
    /* No forward progress (need more data) or hard error: stop for now. */
    if (samples < 0 || rsize == 0)
      break;
    if (ret != GST_FLOW_OK)
      break;
  }

  gst_adapter_unmap (self->adapter);
  if (used > 0)
    gst_adapter_flush (self->adapter, used);
  return ret;
}

static GstFlowReturn
gst_c2_iamf_dec_handle_frame (GstAudioDecoder * dec, GstBuffer * in_buf)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (dec);
  GstFlowReturn ret;

  if (!in_buf) {
    /* EOS drain: flush buffered bytes, then the decoder's internal delay. */
    ret = gst_c2_iamf_dec_process (self, TRUE);
    if (ret == GST_FLOW_OK)
      ret = gst_c2_iamf_dec_drain_delay (self);
    return ret;
  }

  gst_adapter_push (self->adapter, gst_buffer_ref (in_buf));
  return gst_c2_iamf_dec_process (self, FALSE);
}

static void
gst_c2_iamf_dec_flush (GstAudioDecoder * dec, gboolean hard)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (dec);
  (void) hard;

  if (self->adapter)
    gst_adapter_clear (self->adapter);

  /* Reopen the decoder so its internal state is reset for the new segment.
   * Caps stay as already negotiated (same stream), so keep self->negotiated. */
  if (self->dec) {
    IAMF_decoder_close (self->dec);
    self->dec = NULL;
  }
  self->configured = FALSE;
  gst_c2_iamf_dec_open_decoder (self);
}

/* ------------------------------------------------------------------------- */

static void
gst_c2_iamf_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (object);
  switch (prop_id) {
    case PROP_SOUND_SYSTEM:
      self->sound_system = g_value_get_int (value);
      break;
    case PROP_BINAURAL:
      self->binaural = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_c2_iamf_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (object);
  switch (prop_id) {
    case PROP_SOUND_SYSTEM:
      g_value_set_int (value, self->sound_system);
      break;
    case PROP_BINAURAL:
      g_value_set_boolean (value, self->binaural);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_c2_iamf_dec_finalize (GObject * object)
{
  GstC2IamfDec *self = GST_C2_IAMF_DEC (object);
  if (self->dec) {
    IAMF_decoder_close (self->dec);
    self->dec = NULL;
  }
  g_clear_object (&self->adapter);
  g_clear_pointer (&self->pcm, g_free);
  G_OBJECT_CLASS (gst_c2_iamf_dec_parent_class)->finalize (object);
}

static void
gst_c2_iamf_dec_class_init (GstC2IamfDecClass * klass)
{
  GObjectClass         *gobj = (GObjectClass *) klass;
  GstElementClass      *gec  = (GstElementClass *) klass;
  GstAudioDecoderClass *gad  = (GstAudioDecoderClass *) klass;

  gobj->set_property = gst_c2_iamf_dec_set_property;
  gobj->get_property = gst_c2_iamf_dec_get_property;
  gobj->finalize     = gst_c2_iamf_dec_finalize;

  /* IAMF_SoundSystem: -1 invalid, 0 = A (stereo), 1 = B (5.1), 8 = I (7.1),
   * 12 = MONO, etc. See include/IAMF_defines.h. Default stereo. */
  g_object_class_install_property (gobj, PROP_SOUND_SYSTEM,
      g_param_spec_int ("sound-system", "Sound system",
          "IAMF output sound system / loudspeaker layout "
          "(0=stereo, 1=5.1, 8=7.1, 12=mono; see IAMF spec). Ignored if binaural=true.",
          -1, 12, DEFAULT_SOUND_SYSTEM,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobj, PROP_BINAURAL,
      g_param_spec_boolean ("binaural", "Binaural",
          "Render to a binaural (headphone) stereo downmix instead of a sound system.",
          DEFAULT_BINAURAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gad->start        = GST_DEBUG_FUNCPTR (gst_c2_iamf_dec_start);
  gad->stop         = GST_DEBUG_FUNCPTR (gst_c2_iamf_dec_stop);
  gad->set_format   = GST_DEBUG_FUNCPTR (gst_c2_iamf_dec_set_format);
  gad->handle_frame = GST_DEBUG_FUNCPTR (gst_c2_iamf_dec_handle_frame);
  gad->flush        = GST_DEBUG_FUNCPTR (gst_c2_iamf_dec_flush);
  gad->sink_event   = GST_DEBUG_FUNCPTR (gst_c2_iamf_dec_sink_event);
  gad->getcaps      = GST_DEBUG_FUNCPTR (gst_c2_iamf_dec_getcaps);

  gst_element_class_add_static_pad_template (gec, &sink_template);
  gst_element_class_add_static_pad_template (gec, &src_template);
  gst_element_class_set_static_metadata (gec,
      "IAMF immersive audio decoder (AOM libiamf)",
      "Codec/Decoder/Audio",
      "Decodes IAMF (Immersive Audio Model & Formats) via AOM's libiamf "
      "reference decoder (the AOSP Codec2 IAMF component is a stub)",
      "gst-plugins-android contributors");
}

static void
gst_c2_iamf_dec_init (GstC2IamfDec * self)
{
  self->sound_system = DEFAULT_SOUND_SYSTEM;
  self->binaural = DEFAULT_BINAURAL;
  self->adapter = gst_adapter_new ();

  /* IAMF descriptors are in-band, so we do not require upstream caps. */
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), FALSE);
  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER (self), TRUE);
}

/* ------------------------------------------------------------------------- */

/* Typefinder: a raw IAMF stream opens with an IA Sequence Header OBU
 * (obu_type == 31, i.e. top 5 bits of byte 0) whose payload begins with the
 * ASCII ia_code "iamf". Match both for a confident suggestion so that
 * `filesrc ! decodebin` and bare `filesrc ! c2iamfdec` negotiate cleanly. */
static void
gst_c2_iamf_type_find (GstTypeFind * tf, gpointer unused)
{
  const guint8 *d;
  (void) unused;

  d = gst_type_find_peek (tf, 0, 12);
  if (!d)
    return;
  if ((d[0] >> 3) != 31)        /* not an IA Sequence Header OBU */
    return;
  /* "iamf" sits just past the OBU header + leb128 size (offset 2 for the
   * single-byte size case); scan a small window to be tolerant. */
  for (gint i = 1; i + 4 <= 12; i++) {
    if (d[i] == 'i' && d[i + 1] == 'a' && d[i + 2] == 'm' && d[i + 3] == 'f') {
      GstCaps *caps = gst_caps_new_empty_simple ("audio/x-iamf");
      gst_type_find_suggest (tf, GST_TYPE_FIND_LIKELY, caps);
      gst_caps_unref (caps);
      return;
    }
  }
}

gboolean
gst_c2_iamf_dec_register (GstPlugin * plugin)
{
  GstCaps *tf_caps;

  GST_DEBUG_CATEGORY_INIT (gst_c2_iamf_dec_debug, "c2iamfdec", 0,
      "IAMF immersive audio decoder (libiamf)");

  tf_caps = gst_caps_new_empty_simple ("audio/x-iamf");
  if (!gst_type_find_register (plugin, "audio/x-iamf", GST_RANK_PRIMARY,
          gst_c2_iamf_type_find, "iamf", tf_caps, NULL, NULL)) {
    GST_WARNING ("failed to register audio/x-iamf typefinder");
  }
  gst_caps_unref (tf_caps);

  return gst_element_register (plugin, "c2iamfdec",
      GST_RANK_PRIMARY, GST_TYPE_C2_IAMF_DEC);
}
