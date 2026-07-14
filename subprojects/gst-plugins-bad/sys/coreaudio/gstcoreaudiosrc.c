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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstcoreaudiosrc.h"
#include "gstcoreaudioutils.h"

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_COREAUDIO_STATIC_CAPS));

enum
{
  PROP_0,
  PROP_DEVICE,
};

GST_DEBUG_CATEGORY_STATIC (gst_coreaudio_src_debug);
#define GST_CAT_DEFAULT gst_coreaudio_src_debug

#define gst_coreaudio_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCoreAudioSrc, gst_coreaudio_src,
    GST_TYPE_AUDIO_BASE_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_coreaudio_src_debug, "coreaudiosrc", 0,
        "CoreAudio audio source"));

static void
gst_coreaudio_src_set_device (GstCoreAudioSrc * self, const gchar * device_id)
{
  gst_coreaudio_rbuf_set_device (self->ringbuf, GST_COREAUDIO_DEVICE_MODE_SRC,
      device_id);
}

static void
gst_coreaudio_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCoreAudioSrc *self = GST_COREAUDIO_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_free (self->device);
      self->device = g_strdup (g_value_get_string (value));
      gst_coreaudio_src_set_device (self, self->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_coreaudio_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCoreAudioSrc *self = GST_COREAUDIO_SRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
      g_value_set_string (value, self->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_coreaudio_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstCoreAudioSrc *self = GST_COREAUDIO_SRC (src);

  return gst_coreaudio_rbuf_get_caps (self->ringbuf);
}

static GstAudioRingBuffer *
gst_coreaudio_src_create_ringbuffer (GstAudioBaseSrc * src)
{
  GstCoreAudioSrc *self = GST_COREAUDIO_SRC (src);

  return GST_AUDIO_RING_BUFFER (self->ringbuf);
}

static void
gst_coreaudio_src_finalize (GObject * object)
{
  GstCoreAudioSrc *self = GST_COREAUDIO_SRC (object);

  g_free (self->device);
  gst_object_unref (self->ringbuf);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_coreaudio_src_init (GstCoreAudioSrc * self)
{
  self->ringbuf = gst_coreaudio_rbuf_new (self);
  gst_coreaudio_src_set_device (self, NULL);
}

static void
gst_coreaudio_src_class_init (GstCoreAudioSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);
  GstAudioBaseSrcClass *audiobasesrc_class = GST_AUDIO_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_coreaudio_src_set_property;
  gobject_class->get_property = gst_coreaudio_src_get_property;
  gobject_class->finalize = gst_coreaudio_src_finalize;

  basesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_coreaudio_src_get_caps);
  audiobasesrc_class->create_ringbuffer =
      GST_DEBUG_FUNCPTR (gst_coreaudio_src_create_ringbuffer);

  g_object_class_install_property (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device UID",
          "Audio device UID as provided by CoreAudio",
          NULL,
          (GParamFlags) (GST_PARAM_MUTABLE_PLAYING | G_PARAM_READWRITE |
              G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class,
      "CoreAudio Audio Source", "Source/Audio/Hardware",
      "Stream from audio devices using the CoreAudio API",
      "Piotr Brzeziński <piotr@centricular.com>");
}
