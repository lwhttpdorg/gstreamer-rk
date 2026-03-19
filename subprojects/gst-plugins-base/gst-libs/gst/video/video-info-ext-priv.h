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

#ifndef __VIDEO_INFO_EXT_PRIV_H__
#define __VIDEO_INFO_EXT_PRIV_H__

#include "video-info-ext.h"
#include "video-info-generic.h"

G_BEGIN_DECLS

/*
 * GstVideoInfoExtensions.
 *
 * The struct is intentionally not public. All external access goes through
 * typed accessors.
 *
 * New extension payloads should be added here as nullable pointers. Existing
 * payloads are never removed (ABI).
 */
struct _GstVideoInfoExtensions
{
  GstMiniObject          mini_object;

  GstGenericVideoParams *generic_params;
};

typedef struct _GstVideoInfo GstVideoInfo;

GstVideoInfoExtensions * gst_video_info_extensions_new_priv (void);

const GstGenericVideoParams *
gst_video_info_extensions_get_generic_params_priv (const GstVideoInfoExtensions * ext);

void gst_video_info_extensions_set_generic_params_priv (GstVideoInfoExtensions * ext,
    const GstGenericVideoParams * params);

gboolean gst_video_info_generic_fill_info_priv (GstVideoInfo * info,
    const GstStructure * structure);

void gst_video_info_generic_fill_caps_priv (const GstGenericVideoParams * params,
    GstCaps * caps);

G_END_DECLS

#endif /* __VIDEO_INFO_EXT_PRIV_H__ */
