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

#ifndef __VIDEO_INFO_EXT_H__
#define __VIDEO_INFO_EXT_H__

#include <gst/gst.h>
#include <gst/video/video-prelude.h>

G_BEGIN_DECLS

/**
 * GstVideoInfoExtensions:
 *
 * An opaque, reference-counted (#GstMiniObject) container for extended
 * #GstVideoInfo parameters.
 *
 * Use typed accessor functions to retrieve specific extension payloads,
 * e.g. gst_video_info_get_generic_params().
 *
 * Since: 1.30
 */
typedef struct _GstVideoInfoExtensions GstVideoInfoExtensions;

GST_VIDEO_API
GType gst_video_info_extensions_get_type (void);

/**
 * GST_TYPE_VIDEO_INFO_EXTENSIONS:
 *
 * The #GType for #GstVideoInfoExtensions.
 *
 * Since: 1.30
 */
#define GST_TYPE_VIDEO_INFO_EXTENSIONS (gst_video_info_extensions_get_type ())

G_END_DECLS

#endif /* __VIDEO_INFO_EXT_H__ */

