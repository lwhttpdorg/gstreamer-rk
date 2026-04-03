/* GStreamer
 * Copyright (C) <2026> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
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
 * SECTION:video-info-ext
 * @title: GstVideoInfoExtensions
 * @short_description: Reference-counted extension container for GstVideoInfo
 *
 * #GstVideoInfoExtensions is a #GstMiniObject that carries optional extended
 * parameters for a #GstVideoInfo.
 *
 * The extension mechanism is intentionally not tied to any specific format.
 * New extension payloads (e.g. for future format specifications) are added as
 * additional nullable fields in the struct and exposed through new typed
 * accessor functions.
 *
 * Current payloads:
 * - #GstGenericVideoParams — parametric description for %GST_VIDEO_FORMAT_GENERIC
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video-info-ext-priv.h"

static GstVideoInfoExtensions *
_ext_copy (GstVideoInfoExtensions * ext)
{
  GstVideoInfoExtensions *copy = gst_video_info_extensions_new_priv ();

  if (ext->generic_params)
    copy->generic_params =
        g_memdup2 (ext->generic_params, sizeof (GstGenericVideoParams));

  return copy;
}

static void
_ext_free (GstVideoInfoExtensions * ext)
{
  g_free (ext->generic_params);
  g_free (ext);
}

GST_DEFINE_MINI_OBJECT_TYPE (GstVideoInfoExtensions, gst_video_info_extensions);

GstVideoInfoExtensions *
gst_video_info_extensions_new_priv (void)
{
  GstVideoInfoExtensions *ext = g_new0 (GstVideoInfoExtensions, 1);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (ext), 0,
      GST_TYPE_VIDEO_INFO_EXTENSIONS,
      (GstMiniObjectCopyFunction) _ext_copy,
      NULL, (GstMiniObjectFreeFunction) _ext_free);

  return ext;
}

const GstGenericVideoParams *
gst_video_info_extensions_get_generic_params_priv (const
    GstVideoInfoExtensions * ext)
{
  return ext->generic_params;
}

void
gst_video_info_extensions_set_generic_params_priv (GstVideoInfoExtensions *
    ext, const GstGenericVideoParams * params)
{
  g_free (ext->generic_params);
  ext->generic_params =
      params ? g_memdup2 (params, sizeof (GstGenericVideoParams)) : NULL;
}
