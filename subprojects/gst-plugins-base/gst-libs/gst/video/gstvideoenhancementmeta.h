/* GStreamer Video enhancement meta
 *  Copyright (C) <2024> V-Nova International Limited
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

#ifndef __GST_VIDEO_ENHANCEMENT_H__
#define __GST_VIDEO_ENHANCEMENT_H__

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GST_VIDEO_ENHANCEMENT_META_API_TYPE:
 *
 * Gets the video enhancement meta API info
 *
 * Since: 1.30
 */
#define GST_VIDEO_ENHANCEMENT_META_API_TYPE (gst_video_enhancement_meta_api_get_type())

/**
 * GST_VIDEO_ENHANCEMENT_META_INFO:
 *
 * Gets the video enhancement meta info
 *
 * Since: 1.30
 */
#define GST_VIDEO_ENHANCEMENT_META_INFO  (gst_video_enhancement_meta_get_info())

typedef struct _GstVideoEnhancementMeta GstVideoEnhancementMeta;

/**
 * GstVideoEnhancementMeta:
 * @meta: parent #GstMeta
 * @caps: a #GstCaps describing the enhancement data
 * @enhancement_data: the enhancemnent data
 *
 * Video enhancement data for enhancement codecs
 *
 * Since: 1.30
 */
struct _GstVideoEnhancementMeta {
  GstMeta meta;

  GstCaps *caps;
  GstBuffer *enhancement_data;
};

GST_VIDEO_API
GType gst_video_enhancement_meta_api_get_type (void);

GST_VIDEO_API
const GstMetaInfo * gst_video_enhancement_meta_get_info (void);

GST_VIDEO_API
GstVideoEnhancementMeta * gst_buffer_get_video_enhancement_meta (GstBuffer *buffer,
    GstCaps * enhancement_caps);

GST_VIDEO_API
GstVideoEnhancementMeta * gst_buffer_add_video_enhancement_meta (GstBuffer *buffer,
    GstCaps *enhancement_caps, GstBuffer *enhancement_data);

G_END_DECLS

#endif /* __GST_VIDEO_ENHANCEMENT_H__ */
