/* GStreamer
 * Copyright © 2025 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Author: Sreerenj Balachandran <sreerenj@amazon.com>
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

#include "gstcodecdecoder.h"
#include <gst/video/gstvideodecoder.h>

#define GST_TYPE_CODEC_DEBUG_FLAGS (gst_codec_debug_flags_get_type())
GType
gst_codec_debug_flags_get_type (void)
{
  static GType type = 0;
  if (!type) {
    static const GFlagsValue vals[] = {
      {GST_CODEC_DEBUG_NONE, "None", "none"},
      {GST_CODEC_DEBUG_SEQUENCE_HDRS, "Dump Sequence HDRs", "sequence-headers"},
      {GST_CODEC_DEBUG_PICTURE_HDRS, "Dump Picture HDRs", "picture-headers"},
      {GST_CODEC_DEBUG_RAW_FRAME, "Dump Raw Frame", "raw-frame"},
      {0, NULL, NULL}
    };
    type = g_flags_register_static ("GstCodecDebugFlags", vals);
  }
  return type;
}

enum
{
  PROP_0,
  PROP_DEBUG_MODE,
};

#define parent_class gst_codec_decoder_parent_class
G_DEFINE_TYPE (GstCodecDecoder, gst_codec_decoder, GST_TYPE_VIDEO_DECODER);

static void
gst_codec_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCodecDecoder *self = (GstCodecDecoder *) object;

  switch (prop_id) {
    case GST_CODEC_DEBUG_NONE:
      break;
    case PROP_DEBUG_MODE:
      self->debug_mode = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_codec_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCodecDecoder *self = (GstCodecDecoder *) object;

  switch (prop_id) {
    case GST_CODEC_DEBUG_NONE:
      break;
    case PROP_DEBUG_MODE:
      g_value_set_flags (value, self->debug_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_codec_decoder_class_init (GstCodecDecoderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_codec_decoder_set_property;
  gobject_class->get_property = gst_codec_decoder_get_property;

  g_object_class_install_property (gobject_class,
      PROP_DEBUG_MODE,
      g_param_spec_flags ("debug-mode", "Debug Mode",
          "Bitmask of debug dump modes",
          GST_TYPE_CODEC_DEBUG_FLAGS,
          GST_CODEC_DEBUG_NONE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

static void
gst_codec_decoder_init (GstCodecDecoder * self)
{
  self->debug_mode = GST_CODEC_DEBUG_NONE;
}
