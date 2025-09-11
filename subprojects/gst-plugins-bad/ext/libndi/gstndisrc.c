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

#include "gstndisrc.h"

#include <gst/video/video.h>
#include <ndi/ndi.h>

struct _GstNdiSrc
{
  GstPushSrc parent;
};

G_DEFINE_TYPE (GstNdiSrc, gst_ndi_src, GST_TYPE_PUSH_SRC);
GST_ELEMENT_REGISTER_DEFINE (ndi_src, "ndisrc", GST_RANK_NONE,
    GST_NDI_TYPE_SRC);
GST_DEBUG_CATEGORY_STATIC (gst_ndi_src_debug);
#define GST_CAT_DEFAULT gst_ndi_src_debug

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES ("ANY", GST_VIDEO_FORMATS_ALL))
    );

static void
gst_ndi_src_class_init (GstNdiSrcClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class, "VideoLAN NDI Source",
      "Source/Video", "Reads frames from a NDI device",
      "Michael Gruner <michael.gruner@ridgerun.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  GST_DEBUG_CATEGORY_INIT (gst_ndi_src_debug, "ndisrc", 0,
      "VideoLAN NDI Source");
}

static void
gst_ndi_src_init (GstNdiSrc * self)
{
  GST_DEBUG_OBJECT (self, "initializing NDI src");
}
