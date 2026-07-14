/* GStreamer
 * Copyright (C) 2025 Piotr Brzeziński <piotr@centricular.com>
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

#ifndef __GST_COREAUDIOCTX_H__
#define __GST_COREAUDIOCTX_H__

#include <CoreAudio/CoreAudio.h>
#include <AudioToolbox/AudioToolbox.h>
#include "gstcoreaudioutils.h"

G_BEGIN_DECLS

struct _GstCoreAudioCtx
{
  GstCoreAudioDeviceMode mode;
  guint64 buffer_time;
  guint64 latency_time;

  AudioUnit unit;
  AudioDeviceID device_id;

  gboolean prepared;
  guint32 frames_per_packet;
  AudioStreamBasicDescription *selected_format;
  AudioChannelLayout *selected_layout;

  /* For now we only support the current OS provided format. 
   * Switching is possible but affects the whole OS, so let's not do it for now */
  AudioStreamBasicDescription *hw_format;
  AudioChannelLayout *hw_layout;

  /* src mode only */
  UInt32 abl_buf_size;
  AudioBufferList *abl;

  /* All the resampling stuff for src mode */
  GstAudioConverter *conv;
  GstAudioInfo conv_in_info;
  GstAudioInfo conv_out_info;
  GByteArray *input_fifo;
  gsize input_fifo_bytes;
  GByteArray *output_fifo;
  gsize output_fifo_bytes;

  /* Handling device disconnection and/or other changes */
  gboolean running;
  gboolean should_stop;
  gboolean listeners_attached;
};

typedef struct _GstCoreAudioCtx GstCoreAudioCtx;

GstCoreAudioCtx * gst_coreaudio_ctx_new (GstCoreAudioDeviceMode mode, const gchar * device_uid);
gboolean gst_coreaudio_ctx_prepare (GstCoreAudioCtx * ctx, AudioStreamBasicDescription * asbd, AudioChannelLayout *layout, guint32 requested_fpp);
gboolean gst_coreaudio_ctx_start (GstCoreAudioCtx * ctx, AURenderCallbackStruct callback);
gboolean gst_coreaudio_ctx_stop (GstCoreAudioCtx * ctx);
void gst_coreaudio_ctx_free (GstCoreAudioCtx * ctx);

gboolean gst_coreaudio_ctx_render (GstCoreAudioCtx * ctx,
  const AudioTimeStamp * in_timestamp, UInt32 in_frames,
  gpointer *out_data, gsize *out_bytes);

gboolean gst_coreaudio_ctx_is_sink (GstCoreAudioCtx * ctx);

G_END_DECLS

#endif /* __GST_COREAUDIOCTX_H__ */
