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

#ifndef __GST_COREAUDIOSRC_H__
#define __GST_COREAUDIOSRC_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstcoreaudioutils.h"
#include "gstcoreaudioringbuf.h"

G_BEGIN_DECLS

#define GST_TYPE_COREAUDIO_SRC (gst_coreaudio_src_get_type ())
G_DECLARE_FINAL_TYPE (GstCoreAudioSrc,
    gst_coreaudio_src, GST, COREAUDIO_SRC, GstAudioBaseSrc);

struct _GstCoreAudioSrc
{
  GstAudioBaseSrc parent;

  GstCoreAudioRbuf *ringbuf;

  gchar *device;
};

G_END_DECLS

#endif /* __GST_COREAUDIOSRC_H__ */
