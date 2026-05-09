/* GStreamer
 * Copyright (C) <2025> Fluendo S.A.
 *   Author: Diego Nieto <dnieto@fluendo.com>
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

/**
 * SECTION:element-h266alphadecodebin
 * @title: Wrapper to decode H266 alpha using a custom decoder
 *
 * Use two vvdec custom instance in order to decode H266 alpha channel.
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gsth266alphadecodebin.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h266, codec-alpha = (boolean) true")
    );

struct _GstH266AlphaDecodeBin
{
  GstAlphaDecodeBin parent;
};

#define gst_h266_alpha_decode_bin_parent_class parent_class
G_DEFINE_TYPE (GstH266AlphaDecodeBin, gst_h266_alpha_decode_bin,
    GST_TYPE_ALPHA_DECODE_BIN);

GST_ELEMENT_REGISTER_DEFINE (h266_alpha_decode_bin, "h266alphadecodebin",
    GST_RANK_PRIMARY + GST_ALPHA_DECODE_BIN_RANK_OFFSET,
    GST_TYPE_H266_ALPHA_DECODE_BIN);

static gchar *
find_decoder (void)
{
  GList *features;
  GList *iter;
  gchar *decoder_name = NULL;

  features = gst_element_factory_list_get_elements
      (GST_ELEMENT_FACTORY_TYPE_DECODER | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO,
      GST_RANK_MARGINAL);

  if (!features)
    return NULL;

  for (iter = features; iter; iter = g_list_next (iter)) {
    GstPluginFeature *f = GST_PLUGIN_FEATURE (iter->data);
    const gchar *name;

    name = gst_plugin_feature_get_name (f);
    if ((!g_strrstr (name, "h266") && !g_strrstr (name, "vvdec"))
        || g_strrstr (name, "h266alphadecodebin")) {
      continue;
    } else {
      decoder_name = g_strdup (name);
      break;
    }
  }
  gst_plugin_feature_list_free (features);

  return decoder_name;
}

static void
gst_h266_alpha_decode_bin_class_init (GstH266AlphaDecodeBinClass * klass)
{
  GstAlphaDecodeBinClass *adbin_class = (GstAlphaDecodeBinClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;

  adbin_class->decoder_name = find_decoder ();

  gst_element_class_add_static_pad_template (element_class, &sink_template);

  gst_element_class_set_static_metadata (element_class,
      "H266 Alpha Decoder", "Codec/Decoder/Video",
      "Wrapper bin to decode H266 with alpha stream.",
      "Diego Nieto <dnieto@fluendo.com>");
}

static void
gst_h266_alpha_decode_bin_init (GstH266AlphaDecodeBin * self)
{
}
