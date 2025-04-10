/* GStreamer
 * Copyright (C) 2023 Collabora Ltd
 * Copyright (C) 2024 Intel Corporation
 *
 * gstanalyticsimagemtd.c
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

#include "gstanalyticsimagemtd.h"

#include <gst/video/video.h>
#include <math.h>

/**
 * SECTION:gstanalyticsimagemtd
 * @title: GstAnalyticsImageMtd
 * @short_description: An analytics metadata for image based data
 * @symbols:
 * - GstAnalyticsImageMtd
 * @see_also: #GstAnalyticsMtd, #GstAnalyticsRelationMeta
 *
 * This type of metadata holds the buffer which represent an image data.
 *
 * Since: 1.28
 */

typedef struct _GstAnalyticsImageMtdData GstAnalyticsImageMtdData;

struct _GstAnalyticsImageMtdData
{
  GQuark type;                  /* Quark of the class */
  GstBuffer *image_buffer;
};

static void
gst_analytics_image_mtd_meta_clear (GstBuffer * buffer, GstAnalyticsMtd * mtd)
{
  GstAnalyticsImageMtdData *data;
  data = gst_analytics_relation_meta_get_mtd_data (mtd->meta, mtd->id);
  g_assert (data != NULL);
  g_return_if_fail (data->image_buffer != NULL);
  gst_clear_buffer (&data->image_buffer);
}

static gboolean
gst_analytics_image_mtd_transform (GstBuffer * transbuf,
    GstAnalyticsMtd * transmtd, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAnalyticsImageMtdData *mtddata;
  if (GST_META_TRANSFORM_IS_COPY (type)) {
    mtddata = gst_analytics_relation_meta_get_mtd_data (transmtd->meta,
        transmtd->id);
    gst_buffer_ref (mtddata->image_buffer);
  } else if (GST_VIDEO_META_TRANSFORM_IS_SCALE (type)) {
    return FALSE;               /* Scaling is not supported */
  }
  return TRUE;
}

static const GstAnalyticsMtdImpl image_impl = {
  "image",
  gst_analytics_image_mtd_transform,
  gst_analytics_image_mtd_meta_clear
};

GstAnalyticsMtdType
gst_analytics_image_mtd_get_mtd_type (void)
{
  return (GstAnalyticsMtdType) & image_impl;
}

/**
 * gst_analytics_image_get_image_buffer:
 * @instance: instance
 * @image_buffer: (out): image buffer
 *
 * Retrieve image buffer.
 *
 * Returns: Image buffer on success, otherwise NULL.
 *
 * Since: 1.28
 */
GstBuffer *
gst_analytics_image_mtd_get_image_buffer (const GstAnalyticsImageMtd * instance)
{
  GstAnalyticsImageMtdData *data;

  data = gst_analytics_relation_meta_get_mtd_data (instance->meta,
      instance->id);
  g_return_val_if_fail (data != NULL, NULL);

  return gst_buffer_ref (data->image_buffer);
}

/**
 * gst_analytics_image_mtd_get_obj_type:
 * @handle: Instance handle
 *
 * Quark of the image type representing kind of data contained in the image buffer.
 *
 * Returns: Quark different from on success and 0 on failure.
 *
 * Since: 1.28
 */
GQuark
gst_analytics_image_mtd_get_image_type (const GstAnalyticsImageMtd * handle)
{
  GstAnalyticsImageMtdData *data;
  g_return_val_if_fail (handle != NULL, 0);
  data = gst_analytics_relation_meta_get_mtd_data (handle->meta, handle->id);
  g_return_val_if_fail (data != NULL, 0);
  return data->type;
}

/**
 * gst_analytics_relation_meta_add_image_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add image data
 * @type: Type of data contained in the image buffer
 * @image_buffer: buffer containing the image data
 * @w: Width of the image buffer
 * @h: Height of the image buffer
 * @format: Format of the image buffer
 * @image_mtd: (out)(nullable): Handle updated with newly created image mtd
 *
 * Add an analytics image based metadata to the @instance.
 *
 * Returns: TRUE if successful, otherwise FALSE.
 *
 * Since: 1.28
 */
gboolean
gst_analytics_relation_meta_add_image_mtd (GstAnalyticsRelationMeta *
    instance, GQuark type, GstBuffer * image_buffer, gint w, gint h,
    GstVideoFormat format, GstAnalyticsImageMtd * image_mtd)
{
  g_return_val_if_fail (instance != NULL, FALSE);
  g_return_val_if_fail (image_buffer != NULL, FALSE);
  gsize size = sizeof (GstAnalyticsImageMtdData);
  GstAnalyticsImageMtdData *image_mtd_data = (GstAnalyticsImageMtdData *)
      gst_analytics_relation_meta_add_mtd (instance, &image_impl, size,
      image_mtd);
  if (image_mtd_data) {
    image_mtd_data->image_buffer = image_buffer;
    gst_buffer_add_video_meta (image_mtd_data->image_buffer,
        GST_VIDEO_FLAG_NONE, format, w, h);
    gst_buffer_ref (image_mtd_data->image_buffer);

    image_mtd_data->type = type;
  }
  return image_mtd_data != NULL;
}

/**
 * gst_analytics_relation_meta_add_image_vmeta_mtd:
 * @instance: Instance of #GstAnalyticsRelationMeta where to add image data,
 * @type: Type of data contained in the image buffer
 * @image_buffer: buffer containing the image data, buffer already has video meta
 * @image_mtd: (out)(nullable): Handle updated with newly created image mtd
 *
 * Add an analytics image based metadata to the @instance.
 *
 * Returns: TRUE if successful, otherwise FALSE.
 *
 * Since: 1.28
 */
gboolean
gst_analytics_relation_meta_add_image_vmeta_mtd (GstAnalyticsRelationMeta *
    instance, GQuark type, GstBuffer * image_buffer,
    GstAnalyticsImageMtd * image_mtd)
{
  g_return_val_if_fail (instance != NULL, FALSE);
  g_return_val_if_fail (image_buffer != NULL, FALSE);
  gsize size = sizeof (GstAnalyticsImageMtdData);
  GstAnalyticsImageMtdData *image_mtd_data = (GstAnalyticsImageMtdData *)
      gst_analytics_relation_meta_add_mtd (instance, &image_impl, size,
      image_mtd);
  if (image_mtd_data) {
    image_mtd_data->image_buffer = image_buffer;
    gst_buffer_ref (image_mtd_data->image_buffer);
    image_mtd_data->type = type;
  }
  return image_mtd_data != NULL;
}

/**
 * gst_analytics_relation_meta_get_image_mtd:
 * @meta: Instance of #GstAnalyticsRelationMeta
 * @an_meta_id: Id of #GstAnalyticsImageMtd instance to retrieve
 * @rlt: (out caller-allocates)(not nullable): Will be filled with relatable
 *    meta
 *
 * Fill @rlt if a analytics-meta with id == @an_meta_id exist in @meta instance,
 * otherwise this method return FALSE and @rlt is invalid.
 *
 * Returns: TRUE if successful.
 *
 * Since: 1.28
 */
gboolean
gst_analytics_relation_meta_get_image_mtd (GstAnalyticsRelationMeta * meta,
    guint an_meta_id, GstAnalyticsImageMtd * rlt)
{
  return gst_analytics_relation_meta_get_mtd (meta, an_meta_id,
      gst_analytics_image_mtd_get_mtd_type (), (GstAnalyticsImageMtd *) rlt);
}
