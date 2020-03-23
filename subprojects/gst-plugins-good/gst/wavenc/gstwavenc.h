/* GStreamer
 * Copyright (C) 2002, Iain Holmes <iain@prettypeople.org>
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


#ifndef __GST_WAV_ENC_H__
#define __GST_WAV_ENC_H__


#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_WAVENC (gst_wavenc_get_type())
G_DECLARE_FINAL_TYPE (GstWavEnc, gst_wavenc, GST, WAVENC, GstElement)

struct _GstWavEnc {
  GstElement element;

  GstPad    *sinkpad;
  GstPad    *srcpad;

  GstTagList *tags;
  GstToc    *toc;
  GList     *cues;
  GList     *labls;
  GList     *notes;

  /* useful audio data */
  GstAudioFormat audio_format;
  guint16    format;
  guint      width;
  guint      rate;
  guint      channels;
  guint64    channel_mask;
  GstAudioChannelPosition srcPos[64];
  GstAudioChannelPosition destPos[64];
  
  /* data sizes */
  guint64    audio_length;
  guint32    meta_length;

  gboolean   use_rf64;
  gboolean   sent_header;
  gboolean   finished_properly;
};

GST_ELEMENT_REGISTER_DECLARE (wavenc);

G_END_DECLS

#endif /* __GST_WAV_ENC_H__ */
