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
#include "config.h"
#endif

#include "gstcoreaudiosink.h"
#include "gstcoreaudiosrc.h"
#include "gstcoreaudiodeviceprovider.h"

GST_DEBUG_CATEGORY (gst_coreaudio_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  guint rank = GST_RANK_NONE;
  gboolean ret = TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_coreaudio_debug, "coreaudio", 0,
      "CoreAudio elements");

  ret |=
      gst_element_register (plugin, "coreaudiosrc", rank,
      GST_TYPE_COREAUDIO_SRC);
  ret |=
      gst_element_register (plugin, "coreaudiosink", rank,
      GST_TYPE_COREAUDIO_SINK);
  ret |=
      gst_device_provider_register (plugin, "coreaudiodeviceprovider", rank,
      GST_TYPE_COREAUDIO_DEVICE_PROVIDER);

  return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    coreaudio,
    "CoreAudio-based elements for GStreamer",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
