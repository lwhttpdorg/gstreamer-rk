/*
 * gst-plugins-android — C-callable glue between the C++ Codec2 stack and
 * the per-codec GstAudioDecoder elements.
 *
 * The two GstAudioDecoder elements (c2opusdec, c2aacdec) hold an opaque
 * GstC2Component pointer and drive it via this API. All C++/Codec2 details
 * are confined to gstc2common.cc; the elements stay pure C.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_COMMON_H_
#define GST_C2_COMMON_H_

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct GstC2Component GstC2Component;

/* Logical codec the bridge is asked to instantiate. */
typedef enum {
  GST_C2_CODEC_OPUS,
  GST_C2_CODEC_AAC,
  GST_C2_CODEC_FLAC,
  GST_C2_CODEC_VORBIS,
  GST_C2_CODEC_MP3,
} GstC2Codec;

/* Create/destroy a component instance. Returns NULL on failure (logged). */
GstC2Component * gst_c2_component_new (GstC2Codec codec);
void             gst_c2_component_free (GstC2Component *self);

/* Configure the input format. Called from set_format().
 *   codec_data: optional codec-specific data (Opus header / AAC AudioSpecificConfig)
 *   sample_rate, channels: hints from upstream; the decoder will overwrite
 *     these with whatever it actually decodes on first frame.
 * Returns TRUE on success. */
gboolean gst_c2_component_configure (GstC2Component *self,
                                     gint sample_rate,
                                     gint channels,
                                     const guint8 *codec_data,
                                     gsize codec_data_size);

/* Start the underlying SimpleC2Component (transition to RUNNING). */
gboolean gst_c2_component_start (GstC2Component *self);

/* Stop and reset the component (drop pending work, return to LOADED). */
gboolean gst_c2_component_stop  (GstC2Component *self);

/* Submit one encoded frame. Queues it on the C2 component and collects any
 * outputs that have become ready into an internal FIFO. Does NOT block on this
 * specific frame's output: SW decoders with pipeline delay (e.g. AAC) emit an
 * earlier frame's PCM when a later frame is queued, so output lags input.
 * Call gst_c2_component_pull() repeatedly afterwards to drain ready PCM. */
GstFlowReturn gst_c2_component_decode (GstC2Component *self,
                                       const guint8  *in,
                                       gsize          in_size,
                                       GstClockTime   in_pts_ns,
                                       gboolean       eos);

/* Pop one decoded PCM buffer from the ready FIFO.
 * Returns TRUE and sets *out_pcm (g_malloc'd, transfer full) + *out_size +
 * *out_rate/*out_channels when a buffer was available; FALSE (empty FIFO)
 * otherwise. Caller g_free()s *out_pcm. Loop until it returns FALSE. */
gboolean gst_c2_component_pull (GstC2Component *self,
                                guint8       **out_pcm,
                                gsize         *out_size,
                                gint          *out_rate,
                                gint          *out_channels);

/* Submit one codec-config buffer as its own FLAG_CODEC_CONFIG work (no param
 * setting). Vorbis needs this twice (identification + setup headers), which the
 * component detects by content; produces no PCM. Returns TRUE on success. */
gboolean gst_c2_component_queue_csd (GstC2Component *self,
                                     const guint8 *data, gsize size);

/* Drain the decoder's pipeline-delay tail at EOS: signals end-of-stream to the
 * component and collects all remaining outputs into the FIFO. Pull afterwards. */
void gst_c2_component_drain (GstC2Component *self);

/* Flush internal state without changing component state (e.g. after seek).
 * Drops buffered outputs and in-flight works. */
void gst_c2_component_flush (GstC2Component *self);

/* For diagnostics. */
const gchar * gst_c2_component_get_name (GstC2Component *self);

G_END_DECLS

#endif
