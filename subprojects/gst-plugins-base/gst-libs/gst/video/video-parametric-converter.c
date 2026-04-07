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
 * SECTION:videoparametricconverter
 * @title: GstVideoParametricConverter
 * @short_description: Conversion between PARAMETRIC and standard video formats
 *
 * This object converts video frames between a %GST_VIDEO_FORMAT_PARAMETRIC
 * format and any standard format understood by #GstVideoConverter.
 *
 * The conversion proceeds through an intermediate standard format obtained via
 * gst_video_format_parametric_get_unpack_format().  For PARAMETRIC input, each
 * row is unpacked into an intermediate buffer using
 * gst_video_format_parametric_unpack(), then #GstVideoConverter processes
 * intermediate to output.  For PARAMETRIC output, #GstVideoConverter processes
 * input to intermediate, then each row is packed using
 * gst_video_format_parametric_pack().
 *
 * PARAMETRIC to PARAMETRIC conversion is not supported in this version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "video-parametric-converter.h"
#include "video-info-parametric.h"
#include "video-converter.h"
#include "video-info.h"

struct _GstVideoParametricConverter
{
  GstVideoInfo in_info;         /* caller's original input info */
  GstVideoInfo out_info;        /* caller's original output info */
  gboolean parametric_in;
  gboolean parametric_out;
  /* Standard format staging buffer between parametric pack/unpack and the
   * inner converter.  Inherits all metadata from the parametric side. */
  GstVideoInfo intermediate_info;
  guint8 *intermediate_data;    /* size == intermediate_info.size */
  GstVideoConverter *inner_convert;
  gboolean async_tasks;         /* whether inner_convert uses async tasks */
};

/**
 * gst_video_parametric_converter_new_with_pool:
 * @in_info: input #GstVideoInfo
 * @out_info: output #GstVideoInfo
 * @config: (transfer full) (nullable): configuration options for the inner
 *   #GstVideoConverter, or %NULL to use defaults
 * @pool: (nullable): task pool for parallel conversion, or %NULL
 *
 * Like gst_video_parametric_converter_new() but allows supplying a task pool.
 *
 * Returns: a new #GstVideoParametricConverter, or %NULL on failure.
 *
 * Since: 1.30
 */
GstVideoParametricConverter *
gst_video_parametric_converter_new_with_pool (const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstStructure * config, GstTaskPool * pool)
{
  GstVideoParametricConverter *convert;
  GstVideoInfo fmt_info;
  GstVideoInfo intermediate_info;
  GstVideoFormat unpack_fmt;
  const GstVideoInfo *parametric_info;
  const GstVideoInfo *conv_in;
  const GstVideoInfo *conv_out;
  GstVideoConverter *inner_convert;
  guint8 *intermediate_data;
  gboolean parametric_in;
  gboolean parametric_out;

  g_return_val_if_fail (in_info != NULL, NULL);
  g_return_val_if_fail (out_info != NULL, NULL);

  parametric_in =
      (GST_VIDEO_INFO_FORMAT (in_info) == GST_VIDEO_FORMAT_PARAMETRIC);
  parametric_out =
      (GST_VIDEO_INFO_FORMAT (out_info) == GST_VIDEO_FORMAT_PARAMETRIC);

  if (!parametric_in && !parametric_out) {
    GST_WARNING ("neither side is PARAMETRIC; use GstVideoConverter directly");
    if (config)
      gst_structure_free (config);
    return NULL;
  }

  if (parametric_in && parametric_out) {
    /* TODO: implement direct unpack->pack path for PARAMETRIC->PARAMETRIC */
    GST_WARNING ("PARAMETRIC->PARAMETRIC conversion is not yet supported");
    if (config)
      gst_structure_free (config);
    return NULL;
  }

  parametric_info = parametric_in ? in_info : out_info;

  unpack_fmt = gst_video_format_parametric_get_unpack_format (parametric_info);
  if (unpack_fmt == GST_VIDEO_FORMAT_UNKNOWN) {
    GST_WARNING ("cannot determine unpack format for PARAMETRIC info");
    if (config)
      gst_structure_free (config);
    return NULL;
  }

  /* Build the intermediate info by starting from the parametric side to
   * inherit all caps-negotiated metadata (fps, par, interlace mode,
   * colorimetry, multiview, field order, etc.), then overlay only the
   * format-specific fields from the unpack format.  The PARAMETRIC extensions
   * must not be inherited since the intermediate is a standard format. */
  gst_video_info_init (&fmt_info);
  gst_video_info_set_format (&fmt_info, unpack_fmt,
      parametric_info->width, parametric_info->height);

  intermediate_info = *parametric_info;
  intermediate_info.ABI.abi.extensions = NULL;
  intermediate_info.finfo = fmt_info.finfo;
  intermediate_info.size = fmt_info.size;
  memcpy (intermediate_info.stride, fmt_info.stride, sizeof (fmt_info.stride));
  memcpy (intermediate_info.offset, fmt_info.offset, sizeof (fmt_info.offset));
  intermediate_info.colorimetry = fmt_info.colorimetry;
  /* Unpacked component values always use full color range. */
  intermediate_info.colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;

  intermediate_data = g_malloc (intermediate_info.size);
  if (!intermediate_data) {
    if (config)
      gst_structure_free (config);
    return NULL;
  }

  conv_in = parametric_in ? &intermediate_info : in_info;
  conv_out = parametric_out ? &intermediate_info : out_info;

  /* Config ownership is transferred to gst_video_converter_new*. */
  if (pool) {
    inner_convert =
        gst_video_converter_new_with_pool (conv_in, conv_out, config, pool);
  } else {
    inner_convert = gst_video_converter_new (conv_in, conv_out, config);
  }

  if (!inner_convert) {
    GST_WARNING ("failed to create inner GstVideoConverter");
    g_free (intermediate_data);
    return NULL;
  }

  convert = g_new0 (GstVideoParametricConverter, 1);
  convert->in_info = *in_info;
  convert->out_info = *out_info;
  convert->parametric_in = parametric_in;
  convert->parametric_out = parametric_out;
  convert->intermediate_info = intermediate_info;
  convert->intermediate_data = intermediate_data;
  convert->inner_convert = inner_convert;

  gst_structure_get_boolean (gst_video_converter_get_config (inner_convert),
      GST_VIDEO_CONVERTER_OPT_ASYNC_TASKS, &convert->async_tasks);

  return convert;
}

/**
 * gst_video_parametric_converter_new:
 * @in_info: input #GstVideoInfo
 * @out_info: output #GstVideoInfo
 * @config: (transfer full) (nullable): configuration options, or %NULL
 *
 * Creates a new converter for conversion between a %GST_VIDEO_FORMAT_PARAMETRIC
 * format and any standard format supported by #GstVideoConverter.  At least one
 * of @in_info or @out_info must describe a PARAMETRIC format.
 *
 * Returns: a new #GstVideoParametricConverter, or %NULL on failure.
 *
 * Since: 1.30
 */
GstVideoParametricConverter *
gst_video_parametric_converter_new (const GstVideoInfo * in_info,
    const GstVideoInfo * out_info, GstStructure * config)
{
  return gst_video_parametric_converter_new_with_pool (in_info, out_info,
      config, NULL);
}

/**
 * gst_video_parametric_converter_free:
 * @convert: a #GstVideoParametricConverter
 *
 * Frees @convert and all associated resources.
 *
 * Since: 1.30
 */
void
gst_video_parametric_converter_free (GstVideoParametricConverter * convert)
{
  g_return_if_fail (convert != NULL);

  gst_video_converter_free (convert->inner_convert);
  g_free (convert->intermediate_data);
  g_free (convert);
}

/**
 * gst_video_parametric_converter_frame:
 * @convert: a #GstVideoParametricConverter
 * @src: the source #GstVideoFrame
 * @dest: the destination #GstVideoFrame
 *
 * Converts @src into @dest.  One of the frames must use a
 * %GST_VIDEO_FORMAT_PARAMETRIC format matching the info @convert was created
 * with.
 *
 * Returns: %TRUE on success, %FALSE if a pack or unpack operation failed.
 *
 * Since: 1.30
 */
gboolean
gst_video_parametric_converter_frame (GstVideoParametricConverter * convert,
    const GstVideoFrame * src, GstVideoFrame * dest)
{
  g_return_val_if_fail (convert != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  if (convert->parametric_in) {
    GstVideoFrame intermediate_frame = { 0 };
    gint width = GST_VIDEO_INFO_WIDTH (&src->info);
    gint height = GST_VIDEO_INFO_HEIGHT (&src->info);
    gsize istride = convert->intermediate_info.stride[0];

    intermediate_frame.info = convert->intermediate_info;
    intermediate_frame.data[0] = convert->intermediate_data;

    for (gint y = 0; y < height; y++) {
      if (!gst_video_format_parametric_unpack (&src->info,
              GST_VIDEO_PACK_FLAG_NONE,
              convert->intermediate_data + y * istride,
              (const gpointer *) src->data, src->info.stride, 0, y, width)) {
        GST_WARNING ("parametric_unpack failed at row %d", y);
        return FALSE;
      }
    }

    gst_video_converter_frame (convert->inner_convert, &intermediate_frame,
        dest);
    if (convert->async_tasks)
      gst_video_converter_frame_finish (convert->inner_convert);
  } else {
    GstVideoFrame intermediate_frame = { 0 };
    gint width = GST_VIDEO_INFO_WIDTH (&dest->info);
    gint height = GST_VIDEO_INFO_HEIGHT (&dest->info);
    gsize istride = convert->intermediate_info.stride[0];

    intermediate_frame.info = convert->intermediate_info;
    intermediate_frame.data[0] = convert->intermediate_data;

    gst_video_converter_frame (convert->inner_convert, src,
        &intermediate_frame);
    /* Ensure conversion into intermediate_data is complete before packing,
     * in case the inner converter was configured with async tasks. */
    if (convert->async_tasks)
      gst_video_converter_frame_finish (convert->inner_convert);

    for (gint y = 0; y < height; y++) {
      if (!gst_video_format_parametric_pack (&dest->info,
              GST_VIDEO_PACK_FLAG_NONE,
              convert->intermediate_data + y * istride, istride,
              (gpointer *) dest->data, dest->info.stride, y, width)) {
        GST_WARNING ("parametric_pack failed at row %d", y);
        return FALSE;
      }
    }
  }

  return TRUE;
}

/**
 * gst_video_parametric_converter_get_in_info:
 * @convert: a #GstVideoParametricConverter
 *
 * Returns: the input #GstVideoInfo @convert was created with.
 *
 * Since: 1.30
 */
const GstVideoInfo *
gst_video_parametric_converter_get_in_info (GstVideoParametricConverter *
    convert)
{
  g_return_val_if_fail (convert != NULL, NULL);

  return &convert->in_info;
}

/**
 * gst_video_parametric_converter_get_out_info:
 * @convert: a #GstVideoParametricConverter
 *
 * Returns: the output #GstVideoInfo @convert was created with.
 *
 * Since: 1.30
 */
const GstVideoInfo *
gst_video_parametric_converter_get_out_info (GstVideoParametricConverter *
    convert)
{
  g_return_val_if_fail (convert != NULL, NULL);

  return &convert->out_info;
}
