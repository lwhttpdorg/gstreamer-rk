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

#ifndef __GST_VIDEO_PARAMETRIC_CONVERTER_H__
#define __GST_VIDEO_PARAMETRIC_CONVERTER_H__

#include <gst/video/video-prelude.h>
#include <gst/video/video-info.h>
#include <gst/video/video-frame.h>

G_BEGIN_DECLS

typedef struct _GstVideoParametricConverter GstVideoParametricConverter;

GST_VIDEO_API
GstVideoParametricConverter * gst_video_parametric_converter_new (
    const GstVideoInfo * in_info,
    const GstVideoInfo * out_info,
    GstStructure       * config);

GST_VIDEO_API
GstVideoParametricConverter * gst_video_parametric_converter_new_with_pool (
    const GstVideoInfo * in_info,
    const GstVideoInfo * out_info,
    GstStructure       * config,
    GstTaskPool        * pool);

GST_VIDEO_API
void gst_video_parametric_converter_free (
    GstVideoParametricConverter * convert);

GST_VIDEO_API
gboolean gst_video_parametric_converter_frame (
    GstVideoParametricConverter * convert,
    const GstVideoFrame         * src,
    GstVideoFrame               * dest);

GST_VIDEO_API
const GstVideoInfo * gst_video_parametric_converter_get_in_info (
    GstVideoParametricConverter * convert);

GST_VIDEO_API
const GstVideoInfo * gst_video_parametric_converter_get_out_info (
    GstVideoParametricConverter * convert);

G_END_DECLS

#endif /* __GST_VIDEO_PARAMETRIC_CONVERTER_H__ */
