/* 
 * GStreamer
 * Copyright (C) 2007-2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_AUDIO_CHEB_BAND_H__
#define __GST_AUDIO_CHEB_BAND_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>

#include "audiofxbaseiirfilter.h"

G_BEGIN_DECLS

#define GST_TYPE_AUDIO_CHEB_BAND (gst_audio_cheb_band_get_type())
G_DECLARE_FINAL_TYPE (GstAudioChebBand, gst_audio_cheb_band,
    GST, AUDIO_CHEB_BAND, GstAudioFXBaseIIRFilter)

struct _GstAudioChebBand
{
  GstAudioFXBaseIIRFilter parent;

  gint mode;
  gint type;
  gint poles;
  gfloat lower_frequency;
  gfloat upper_frequency;
  gfloat ripple;

  /* < private > */
  GMutex lock;
};

GST_ELEMENT_REGISTER_DECLARE (audiochebband);

G_END_DECLS
#endif /* __GST_AUDIO_CHEB_BAND_H__ */
