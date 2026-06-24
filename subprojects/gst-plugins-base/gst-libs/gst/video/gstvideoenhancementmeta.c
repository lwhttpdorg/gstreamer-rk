/* GStreamer enhancement meta
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoenhancementmeta.h"

/**
 * SECTION:gstvideoenhancementmeta
 * @title: GstMeta for Video enhancement
 * @short_description: Video enhancement related GstMeta
 *
 */

#define GST_CAT_DEFAULT videoenhancementmeta_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static gboolean
gst_video_enhancement_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoEnhancementMeta *emeta = (GstVideoEnhancementMeta *) meta;

  emeta->enhancement_data = NULL;
  emeta->caps = NULL;

  return TRUE;
}

static void
gst_video_enhancement_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoEnhancementMeta *emeta = (GstVideoEnhancementMeta *) meta;

  gst_caps_replace (&emeta->caps, NULL);
  g_clear_pointer (&emeta->enhancement_data, gst_buffer_unref);
}

static gboolean
gst_video_enhancement_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoEnhancementMeta *dmeta, *smeta;

  smeta = (GstVideoEnhancementMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    GstMetaTransformCopy *copy = data;

    if (!copy->region) {
      /* only copy if the complete data is copied as well */
      dmeta = (GstVideoEnhancementMeta *) gst_buffer_add_meta (dest,
          GST_VIDEO_ENHANCEMENT_META_INFO, NULL);

      if (!dmeta)
        return FALSE;

      GST_TRACE ("copying enhancement metadata");

      g_clear_pointer (&dmeta->enhancement_data, gst_buffer_unref);
      dmeta->enhancement_data = gst_buffer_ref (smeta->enhancement_data);
      gst_caps_replace (&dmeta->caps, smeta->caps);
    }
  } else {
    /* return FALSE, if transform type is not supported */
    return FALSE;
  }
  return TRUE;
}


/**
 * gst_video_enhancement_meta_api_get_type:
 *
 * Gets the #GType of the enhancement meta API.
 *
 * Returns: the #GType of the enhancement meta API.
 *
 * Since: 1.30
 */
GType
gst_video_enhancement_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { "video", NULL };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstVideoEnhancementMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

/**
 * gst_video_enhancement_meta_get_info:
 *
 * Gets the #GstMetaInfo of the enhancement meta.
 *
 * Returns: (transfer none) : the #GstVideoEnhancementMeta of the enhancement meta.
 *
 * Since: 1.30
 */
const GstMetaInfo *
gst_video_enhancement_meta_get_info (void)
{
  static const GstMetaInfo *enhancement_meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & enhancement_meta_info)) {
    GST_DEBUG_CATEGORY_INIT (videoenhancementmeta_debug, "videoenhancementmeta",
        0, "Video Enhancement Metadata");

    const GstMetaInfo *meta =
        gst_meta_register (GST_VIDEO_ENHANCEMENT_META_API_TYPE,
        "GstVideoEnhancementMeta",
        sizeof (GstVideoEnhancementMeta),
        (GstMetaInitFunction) gst_video_enhancement_meta_init,
        (GstMetaFreeFunction) gst_video_enhancement_meta_free,
        gst_video_enhancement_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & enhancement_meta_info,
        (GstMetaInfo *) meta);
  }
  return enhancement_meta_info;
}

/**
 * gst_buffer_get_video_enhancement_meta:
 * @buffer: a #GstBuffer
 * @enhancement_caps: (transfer none)(nullable): a #GstCaps to filter on
 *
 * Find the #GstVideoEnhancementMeta on @buffer with the lowest @id.
 *
 * Buffers can contain multiple #GstVideoEnhancementMeta metadata items when dealing
 * with multiview buffers.
 *
 * Returns: (transfer none) (nullable): the matching
 * #GstVideoEnhancementMeta with lowest id (usually 0) or %NULL when
 * there is no such metadata on @buffer.
 *
 * Since: 1.30
 */
GstVideoEnhancementMeta *
gst_buffer_get_video_enhancement_meta (GstBuffer * buffer,
    GstCaps * enhancement_caps)
{
  const GstMetaInfo *info = GST_VIDEO_ENHANCEMENT_META_INFO;
  gpointer state = NULL;
  GstVideoEnhancementMeta *meta = NULL;

  while ((meta =
          (GstVideoEnhancementMeta *) gst_buffer_iterate_meta_filtered (buffer,
              &state, info->api))) {
    if (enhancement_caps == NULL)
      return meta;

    if (gst_caps_is_equal (meta->caps, enhancement_caps))
      return meta;
  }

  return NULL;
}

/**
 * gst_buffer_add_video_enhancement_meta:
 * @buffer: a #GstBuffer
 * @caps: (transfer none): Caps describing the format of the enhancement data
 * @enhancement_data: (transfer none): the parsed ENHANCEMENT enhancement data
 *
 * Attaches GstVideoEnhancementMeta metadata to @buffer.
 *
 * Returns: (transfer none): the #GstVideoEnhancementMeta on @buffer.
 *
 * Since: 1.30
 */
GstVideoEnhancementMeta *
gst_buffer_add_video_enhancement_meta (GstBuffer * buffer,
    GstCaps * enhancement_caps, GstBuffer * enhancement_data)
{
  GstVideoEnhancementMeta *meta;
  g_return_val_if_fail (GST_IS_CAPS (enhancement_caps), NULL);

  meta = (GstVideoEnhancementMeta *) gst_buffer_add_meta (buffer,
      GST_VIDEO_ENHANCEMENT_META_INFO, NULL);
  if (!meta)
    return NULL;

  gst_caps_replace (&meta->caps, enhancement_caps);
  g_clear_pointer (&meta->enhancement_data, gst_buffer_unref);
  meta->enhancement_data = gst_buffer_ref (enhancement_data);

  return meta;
}
