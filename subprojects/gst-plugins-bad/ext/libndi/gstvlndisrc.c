/*
 * GStreamer VideoLAN NDI video source.
 *
 * Copyright (c) 2025 Michael Gruner <michael.gruner@ridgerun.com>
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
#  include "config.h"
#endif

#include "gstvlndisrc.h"

#include <gst/video/video.h>
#include <ndi/ndi.h>

struct _GstVlNdiSrc
{
  GstPushSrc parent;

  gchar *host;
  guint port;
};

G_DEFINE_TYPE (GstVlNdiSrc, gst_vl_ndi_src, GST_TYPE_PUSH_SRC);
GST_ELEMENT_REGISTER_DEFINE (vl_ndi_src, "vlndisrc", GST_RANK_NONE,
    GST_VL_TYPE_NDI_SRC);
GST_DEBUG_CATEGORY_STATIC (gst_vl_ndi_src_debug);
#define GST_CAT_DEFAULT gst_vl_ndi_src_debug

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS_ALL))
    );

enum
{
  PROP_0,
  PROP_HOST,
  PROP_PORT,
};

#define PROP_HOST_DEFAULT "127.0.0.1"
#define PROP_PORT_DEFAULT 5960
#define PROP_PORT_MIN 0
#define PROP_PORT_MAX G_MAXUINT16

static void gst_vl_ndi_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vl_ndi_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vl_ndi_src_finalize (GObject * object);

static void
gst_vl_ndi_src_class_init (GstVlNdiSrcClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = gst_vl_ndi_src_set_property;
  object_class->get_property = gst_vl_ndi_src_get_property;
  object_class->finalize = gst_vl_ndi_src_finalize;

  g_object_class_install_property (object_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "Host IP address. Must be set in the NULL state.", PROP_HOST_DEFAULT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PORT,
      g_param_spec_uint ("port", "Port",
          "Port of the NDI device. Must be set in the NULL state.",
          PROP_PORT_MIN, PROP_PORT_MAX, PROP_PORT_DEFAULT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  gst_element_class_set_static_metadata (element_class, "VideoLAN NDI Source",
      "Source/Video", "Reads frames from an NDI device",
      "Michael Gruner <michael.gruner@ridgerun.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  GST_DEBUG_CATEGORY_INIT (gst_vl_ndi_src_debug, "vlndisrc", 0,
      "VideoLAN NDI Source");
}

static void
gst_vl_ndi_src_init (GstVlNdiSrc * self)
{
  GST_DEBUG_OBJECT (self, "initializing NDI src");

  self->host = g_strdup (PROP_HOST_DEFAULT);
  self->port = PROP_PORT_DEFAULT;
}

static void
gst_vl_ndi_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_PORT:
      self->port = g_value_get_uint (value);
      break;
    case PROP_HOST:
      g_free (self->host);
      self->host = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_vl_ndi_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_PORT:
      g_value_set_uint (value, self->port);
      break;
    case PROP_HOST:
      g_value_set_string (value, self->host);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_vl_ndi_src_finalize (GObject * object)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (object);

  g_free (self->host);

  G_OBJECT_CLASS (gst_vl_ndi_src_parent_class)->finalize (object);
}
