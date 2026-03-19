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
 * SECTION:video-info-generic
 * @title: Generic Video Format
 * @short_description: Caps parsing, serialization and pack/unpack for
 *   %GST_VIDEO_FORMAT_GENERIC
 *
 * This module implements support for the parametric
 * %GST_VIDEO_FORMAT_GENERIC video format. The format layout is
 * parametric (per-instance) rather than fixed (per-format).
 *
 * Use gst_video_info_init_from_caps_extended() to parse caps carrying
 * format=GENERIC, and gst_video_info_get_generic_params() to access the
 * resulting #GstGenericVideoParams.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video-info-generic.h"
#include "video-info-ext-priv.h"
#include "video-info.h"

static gboolean
component_type_from_string (const gchar * s, GstComponentType * out)
{
  static const struct
  {
    const gchar *name;
    GstComponentType type;
  } map[] = {
    {"grayscale", GST_COMPONENT_TYPE_GRAYSCALE},
    {"luma", GST_COMPONENT_TYPE_LUMA},
    {"chroma-blue", GST_COMPONENT_TYPE_CHROMA_BLUE},
    {"chroma-red", GST_COMPONENT_TYPE_CHROMA_RED},
    {"red", GST_COMPONENT_TYPE_RED},
    {"green", GST_COMPONENT_TYPE_GREEN},
    {"blue", GST_COMPONENT_TYPE_BLUE},
    {"alpha", GST_COMPONENT_TYPE_ALPHA},
    {"depth", GST_COMPONENT_TYPE_DEPTH},
    {"disparity", GST_COMPONENT_TYPE_DISPARITY},
    {"palette", GST_COMPONENT_TYPE_PALETTE},
    {"filter", GST_COMPONENT_TYPE_FILTER},
    {"padded", GST_COMPONENT_TYPE_PADDED},
    {"cyan", GST_COMPONENT_TYPE_CYAN},
    {"magenta", GST_COMPONENT_TYPE_MAGENTA},
    {"yellow", GST_COMPONENT_TYPE_YELLOW},
    {"key", GST_COMPONENT_TYPE_KEY},
  };

  for (guint i = 0; i < G_N_ELEMENTS (map); i++) {
    if (g_str_equal (s, map[i].name)) {
      *out = map[i].type;
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean
component_format_from_string (const gchar * s,
    GstComponentNumericalFormat * out)
{
  if (g_str_equal (s, "unsigned-int")) {
    *out = GST_COMPONENT_FORMAT_UNSIGNED_INT;
    return TRUE;
  }
  if (g_str_equal (s, "float")) {
    *out = GST_COMPONENT_FORMAT_FLOAT;
    return TRUE;
  }
  if (g_str_equal (s, "complex-number")) {
    *out = GST_COMPONENT_FORMAT_COMPLEX_NUMBER;
    return TRUE;
  }
  if (g_str_equal (s, "signed-int")) {
    *out = GST_COMPONENT_FORMAT_SIGNED_INT;
    return TRUE;
  }
  return FALSE;
}

static gboolean
interleave_type_from_string (const gchar * s, GstInterleaveType * out)
{
  if (g_str_equal (s, "interleave")) {
    *out = GST_INTERLEAVE_TYPE_INTERLEAVE;
    return TRUE;
  }
  if (g_str_equal (s, "component")) {
    *out = GST_INTERLEAVE_TYPE_COMPONENT;
    return TRUE;
  }
  if (g_str_equal (s, "mixed-interleave")) {
    *out = GST_INTERLEAVE_TYPE_MIXED_INTERLEAVE;
    return TRUE;
  }
  if (g_str_equal (s, "row-interleave")) {
    *out = GST_INTERLEAVE_TYPE_ROW_INTERLEAVE;
    return TRUE;
  }
  if (g_str_equal (s, "tile-component")) {
    *out = GST_INTERLEAVE_TYPE_TILE_COMPONENT;
    return TRUE;
  }
  if (g_str_equal (s, "multi-Y")) {
    *out = GST_INTERLEAVE_TYPE_MULTI_Y;
    return TRUE;
  }
  return FALSE;
}

static const gchar *
component_type_to_string (GstComponentType type)
{
  static const gchar *names[] = {
    "grayscale", "luma", "chroma-blue", "chroma-red",
    "red", "green", "blue", "alpha",
    "depth", "disparity", "palette", "filter",
    "padded", "cyan", "magenta", "yellow", "key",
  };
  if ((guint) type < G_N_ELEMENTS (names))
    return names[type];
  return NULL;
}

static const gchar *
component_format_to_string (GstComponentNumericalFormat fmt)
{
  switch (fmt) {
    case GST_COMPONENT_FORMAT_UNSIGNED_INT:
      return "unsigned-int";
    case GST_COMPONENT_FORMAT_FLOAT:
      return "float";
    case GST_COMPONENT_FORMAT_COMPLEX_NUMBER:
      return "complex-number";
    case GST_COMPONENT_FORMAT_SIGNED_INT:
      return "signed-int";
    default:
      return NULL;
  }
}

static const gchar *
interleave_type_to_string (GstInterleaveType type)
{
  switch (type) {
    case GST_INTERLEAVE_TYPE_INTERLEAVE:
      return "interleave";
    case GST_INTERLEAVE_TYPE_COMPONENT:
      return "component";
    case GST_INTERLEAVE_TYPE_MIXED_INTERLEAVE:
      return "mixed-interleave";
    case GST_INTERLEAVE_TYPE_ROW_INTERLEAVE:
      return "row-interleave";
    case GST_INTERLEAVE_TYPE_TILE_COMPONENT:
      return "tile-component";
    case GST_INTERLEAVE_TYPE_MULTI_Y:
      return "multi-Y";
    default:
      return NULL;
  }
}

/* Byte size of one component value. */
static inline guint
comp_bytes (const GstComponentDefinition * comp)
{
  return comp->align_size > 0 ? comp->align_size : (comp->depth + 7) / 8;
}

/* Align a byte count to a given alignment (0 = no alignment). */
static inline gsize
align_stride (gsize bytes, guint align)
{
  return align > 0 ? ((bytes + align - 1) / align) * align : bytes;
}

/* Parses GENERIC-specific caps fields, computes stride/offset/size, and
 * attaches a GstVideoInfoExtensions carrying the resulting
 * GstGenericVideoParams to @info. Returns TRUE on success. */
gboolean
gst_video_info_generic_fill_info_priv (GstVideoInfo * info,
    const GstStructure * structure)
{
  GstGenericVideoParams params = { 0, };
  GstVideoInfoExtensions *ext;
  const GValue *comp_array;
  const gchar *s;
  guint n, i;
  guint width = info->width;
  guint height = info->height;

  /* component-definition */
  comp_array = gst_structure_get_value (structure, "component-definition");
  if (!comp_array || !GST_VALUE_HOLDS_ARRAY (comp_array)) {
    GST_ERROR ("missing or invalid 'component-definition' field");
    return FALSE;
  }

  n = gst_value_array_get_size (comp_array);
  if (n == 0 || n > GST_GENERIC_VIDEO_MAX_COMPONENTS) {
    GST_ERROR ("component-definition array size %u out of range [1, %u]",
        n, GST_GENERIC_VIDEO_MAX_COMPONENTS);
    return FALSE;
  }

  for (i = 0; i < n; i++) {
    const GValue *entry = gst_value_array_get_value (comp_array, i);
    const GstStructure *cs;
    const gchar *type_s, *format_s;
    guint depth = 0, align_size = 0;

    if (!GST_VALUE_HOLDS_STRUCTURE (entry)) {
      GST_ERROR ("component-definition[%u] is not a GstStructure", i);
      return FALSE;
    }
    cs = gst_value_get_structure (entry);

    type_s = gst_structure_get_string (cs, "type");
    if (!type_s) {
      GST_ERROR ("component-definition[%u]: missing 'type'", i);
      return FALSE;
    }
    if (!component_type_from_string (type_s,
            &params.component_definition[i].type)) {
      GST_ERROR ("component-definition[%u]: unknown type '%s'", i, type_s);
      return FALSE;
    }

    if (!gst_structure_get_uint (cs, "depth", &depth) || depth == 0) {
      GST_ERROR ("component-definition[%u]: missing or zero 'depth'", i);
      return FALSE;
    }
    params.component_definition[i].depth = depth;

    format_s = gst_structure_get_string (cs, "format");
    if (!format_s) {
      GST_ERROR ("component-definition[%u]: missing 'format'", i);
      return FALSE;
    }
    if (!component_format_from_string (format_s,
            &params.component_definition[i].format)) {
      GST_ERROR ("component-definition[%u]: unknown format '%s'", i, format_s);
      return FALSE;
    }

    gst_structure_get_uint (cs, "align_size", &align_size);
    params.component_definition[i].align_size = align_size;
  }
  params.num_components = n;

  /* sampling-type */
  if (!gst_structure_get_uint (structure, "sampling-type",
          &params.sampling_type)) {
    GST_ERROR ("missing 'sampling-type' field");
    return FALSE;
  }

  /* Only full-sampling (sampling_type=0) is supported in this phase.
   * Subsampling (4:2:2, 4:2:0, 4:1:1) will be added in a later phase. */
  if (params.sampling_type != 0) {
    GST_ERROR ("sampling-type %u not yet supported; only 0 (no subsampling) "
        "is supported", params.sampling_type);
    return FALSE;
  }

  /* interleave-type */
  s = gst_structure_get_string (structure, "interleave-type");
  if (s) {
    if (!interleave_type_from_string (s, &params.interleave_type)) {
      GST_ERROR ("unknown interleave-type '%s'", s);
      return FALSE;
    }
  } else {
    params.interleave_type = GST_INTERLEAVE_TYPE_INTERLEAVE;
  }

  /* optional fields */
  gst_structure_get_uint (structure, "block-size", &params.block_size);
  gst_structure_get_boolean (structure, "components-little-endian",
      &params.components_little_endian);
  gst_structure_get_boolean (structure, "components-pad-lsb",
      &params.components_pad_lsb);
  gst_structure_get_boolean (structure, "block-pad-lsb", &params.block_pad_lsb);
  gst_structure_get_boolean (structure, "block-little-endian",
      &params.block_little_endian);
  gst_structure_get_boolean (structure, "block-reversed",
      &params.block_reversed);
  gst_structure_get_uint (structure, "pixel-size", &params.pixel_size);
  gst_structure_get_uint (structure, "row-align-size", &params.row_align_size);

  params.num_tile_cols = 1;
  params.num_tile_rows = 1;
  gst_structure_get_uint (structure, "num-tile-cols", &params.num_tile_cols);
  gst_structure_get_uint (structure, "num-tile-rows", &params.num_tile_rows);
  if (params.num_tile_cols == 0)
    params.num_tile_cols = 1;
  if (params.num_tile_rows == 0)
    params.num_tile_rows = 1;

  params.version = 1;

  /* stride, offset and size calculation */
  {
    guint row_align = params.row_align_size;

    switch (params.interleave_type) {

      case GST_INTERLEAVE_TYPE_INTERLEAVE:
      {
        /* Single plane, all components packed per pixel. */
        guint psize;

        if (params.pixel_size > 0) {
          psize = params.pixel_size;
        } else if (params.block_size > 0) {
          psize = params.block_size;
        } else {
          psize = 0;
          for (i = 0; i < n; i++)
            psize += comp_bytes (&params.component_definition[i]);
        }

        info->stride[0] =
            (gint) align_stride ((gsize) width * psize, row_align);
        info->offset[0] = 0;
        info->size = (gsize) info->stride[0] * height;
        break;
      }

      case GST_INTERLEAVE_TYPE_COMPONENT:
      {
        /* One plane per component (planar). */
        gsize offset = 0;

        if (n > GST_VIDEO_MAX_PLANES) {
          GST_ERROR ("too many components (%u) for component interleave "
              "(max %d)", n, GST_VIDEO_MAX_PLANES);
          return FALSE;
        }

        for (i = 0; i < n; i++) {
          info->stride[i] = (gint) align_stride (
              (gsize) width * comp_bytes (&params.component_definition[i]),
              row_align);
          info->offset[i] = offset;
          offset += (gsize) info->stride[i] * height;
        }
        info->size = offset;
        break;
      }

      case GST_INTERLEAVE_TYPE_MIXED_INTERLEAVE:
      {
        /* Luma plane + interleaved chroma plane. */
        guint luma_bytes = 0, cb_bytes = 0, cr_bytes = 0;
        guint luma_count = 0;
        gsize offset = 0;

        for (i = 0; i < n; i++) {
          switch (params.component_definition[i].type) {
            case GST_COMPONENT_TYPE_LUMA:
              luma_bytes = comp_bytes (&params.component_definition[i]);
              luma_count++;
              break;
            case GST_COMPONENT_TYPE_CHROMA_BLUE:
              cb_bytes = comp_bytes (&params.component_definition[i]);
              break;
            case GST_COMPONENT_TYPE_CHROMA_RED:
              cr_bytes = comp_bytes (&params.component_definition[i]);
              break;
            default:
              break;
          }
        }

        if (luma_count == 0 || cb_bytes == 0 || cr_bytes == 0) {
          GST_ERROR ("mixed-interleave requires luma + chroma-blue + "
              "chroma-red components");
          return FALSE;
        }

        /* Luma plane */
        info->stride[0] =
            (gint) align_stride ((gsize) width * luma_bytes, row_align);
        info->offset[0] = 0;
        offset = (gsize) info->stride[0] * height;

        /* Interleaved chroma plane */
        info->stride[1] = (gint) align_stride (
            (gsize) width * (cb_bytes + cr_bytes), row_align);
        info->offset[1] = offset;
        info->size = offset + (gsize) info->stride[1] * height;
        break;
      }

      case GST_INTERLEAVE_TYPE_ROW_INTERLEAVE:
      {
        /* Single plane, each row contains all component rows side-by-side. */
        gsize row_bytes = 0;

        for (i = 0; i < n; i++)
          row_bytes +=
              (gsize) width *comp_bytes (&params.component_definition[i]);

        info->stride[0] = (gint) align_stride (row_bytes, row_align);
        info->offset[0] = 0;
        info->size = (gsize) info->stride[0] * height;
        break;
      }

      case GST_INTERLEAVE_TYPE_MULTI_Y:
      {
        /* Single plane, pixel group contains multiple luma + shared chroma. */
        guint num_luma = 0;
        gsize group_bytes = 0;

        for (i = 0; i < n; i++) {
          if (params.component_definition[i].type == GST_COMPONENT_TYPE_LUMA)
            num_luma++;
          group_bytes += comp_bytes (&params.component_definition[i]);
        }

        if (num_luma == 0) {
          GST_ERROR ("multi-Y requires at least one luma component");
          return FALSE;
        }
        if (width % num_luma != 0) {
          GST_ERROR ("multi-Y: width %u must be divisible by num_luma %u",
              width, num_luma);
          return FALSE;
        }

        info->stride[0] =
            (gint) align_stride ((width / num_luma) * group_bytes, row_align);
        info->offset[0] = 0;
        info->size = (gsize) info->stride[0] * height;
        break;
      }

      case GST_INTERLEAVE_TYPE_TILE_COMPONENT:
      {
        /* Planar within each tile. */
        guint tile_w, tile_h;
        gsize tile_size = 0;

        if (n > GST_VIDEO_MAX_PLANES) {
          GST_ERROR ("too many components (%u) for tile-component interleave "
              "(max %d)", n, GST_VIDEO_MAX_PLANES);
          return FALSE;
        }

        tile_w = (width + params.num_tile_cols - 1) / params.num_tile_cols;
        tile_h = (height + params.num_tile_rows - 1) / params.num_tile_rows;

        for (i = 0; i < n; i++) {
          info->stride[i] = (gint) align_stride (
              (gsize) tile_w * comp_bytes (&params.component_definition[i]),
              row_align);
          /* offsets are intra-tile, absolute tile addressing requires GstVideoMeta */
          info->offset[i] = 0;
          tile_size += (gsize) info->stride[i] * tile_h;
        }
        info->size = tile_size * params.num_tile_cols * params.num_tile_rows;
        break;
      }

      default:
        GST_ERROR ("unhandled interleave_type %d", params.interleave_type);
        return FALSE;
    }
  }

  ext = gst_video_info_extensions_new_priv ();
  gst_video_info_extensions_set_generic_params_priv (ext, &params);
  info->ABI.abi.extensions = ext;

  return TRUE;
}

/* Serializes @params into @caps as GENERIC-specific caps fields
 * (component-definition, sampling-type, interleave-type, and optional fields). */
void
gst_video_info_generic_fill_caps_priv (const GstGenericVideoParams * params,
    GstCaps * caps)
{
  GValue comp_array = G_VALUE_INIT;
  guint i;

  g_value_init (&comp_array, GST_TYPE_ARRAY);

  for (i = 0; i < params->num_components; i++) {
    const GstComponentDefinition *cd = &params->component_definition[i];
    GstStructure *cs;
    GValue cs_val = G_VALUE_INIT;

    cs = gst_structure_new ("component",
        "type", G_TYPE_STRING, component_type_to_string (cd->type),
        "depth", G_TYPE_UINT, cd->depth,
        "format", G_TYPE_STRING, component_format_to_string (cd->format), NULL);

    if (cd->align_size > 0)
      gst_structure_set (cs, "align_size", G_TYPE_UINT, cd->align_size, NULL);

    g_value_init (&cs_val, GST_TYPE_STRUCTURE);
    gst_value_set_structure (&cs_val, cs);
    gst_value_array_append_value (&comp_array, &cs_val);
    g_value_unset (&cs_val);
    gst_structure_free (cs);
  }

  gst_caps_set_value (caps, "component-definition", &comp_array);
  g_value_unset (&comp_array);

  gst_caps_set_simple (caps,
      "sampling-type", G_TYPE_UINT, params->sampling_type,
      "interleave-type", G_TYPE_STRING,
      interleave_type_to_string (params->interleave_type), NULL);

  if (params->block_size > 0)
    gst_caps_set_simple (caps, "block-size", G_TYPE_UINT, params->block_size,
        NULL);
  if (params->components_little_endian)
    gst_caps_set_simple (caps, "components-little-endian", G_TYPE_BOOLEAN,
        TRUE, NULL);
  if (params->components_pad_lsb)
    gst_caps_set_simple (caps, "components-pad-lsb", G_TYPE_BOOLEAN, TRUE,
        NULL);
  if (params->block_pad_lsb)
    gst_caps_set_simple (caps, "block-pad-lsb", G_TYPE_BOOLEAN, TRUE, NULL);
  if (params->block_little_endian)
    gst_caps_set_simple (caps, "block-little-endian", G_TYPE_BOOLEAN, TRUE,
        NULL);
  if (params->block_reversed)
    gst_caps_set_simple (caps, "block-reversed", G_TYPE_BOOLEAN, TRUE, NULL);
  if (params->pixel_size > 0)
    gst_caps_set_simple (caps, "pixel-size", G_TYPE_UINT, params->pixel_size,
        NULL);
  if (params->row_align_size > 0)
    gst_caps_set_simple (caps, "row-align-size", G_TYPE_UINT,
        params->row_align_size, NULL);
  if (params->num_tile_cols > 1)
    gst_caps_set_simple (caps, "num-tile-cols", G_TYPE_UINT,
        params->num_tile_cols, NULL);
  if (params->num_tile_rows > 1)
    gst_caps_set_simple (caps, "num-tile-rows", G_TYPE_UINT,
        params->num_tile_rows, NULL);
}

/**
 * gst_video_format_generic_get_unpack_format:
 * @info: a #GstVideoInfo with format %GST_VIDEO_FORMAT_GENERIC
 *
 * Returns the canonical format produced by gst_video_format_generic_unpack():
 * %GST_VIDEO_FORMAT_ARGB64 for RGB, %GST_VIDEO_FORMAT_AYUV64 for YCbCr and
 * grayscale, or %GST_VIDEO_FORMAT_UNKNOWN if the colour model is unrecognised.
 *
 * Returns: the unpack format.
 * Since: 1.30
 */
GstVideoFormat
gst_video_format_generic_get_unpack_format (const GstVideoInfo * info)
{
  const GstGenericVideoParams *params;
  gboolean has_r = FALSE, has_g = FALSE, has_b = FALSE;
  gboolean has_luma = FALSE, has_gray = FALSE, has_cb = FALSE, has_cr = FALSE;
  guint i;

  g_return_val_if_fail (info != NULL, GST_VIDEO_FORMAT_UNKNOWN);

  params = gst_video_info_get_generic_params (info);
  if (!params)
    return GST_VIDEO_FORMAT_UNKNOWN;

  for (i = 0; i < params->num_components; i++) {
    switch (params->component_definition[i].type) {
      case GST_COMPONENT_TYPE_RED:
        has_r = TRUE;
        break;
      case GST_COMPONENT_TYPE_GREEN:
        has_g = TRUE;
        break;
      case GST_COMPONENT_TYPE_BLUE:
        has_b = TRUE;
        break;
      case GST_COMPONENT_TYPE_LUMA:
        has_luma = TRUE;
        break;
      case GST_COMPONENT_TYPE_GRAYSCALE:
        has_gray = TRUE;
        break;
      default:
        break;
    }
  }

  if (has_r && has_g && has_b)
    return GST_VIDEO_FORMAT_ARGB64;
  if (has_gray || has_luma)
    return GST_VIDEO_FORMAT_AYUV64;
  return GST_VIDEO_FORMAT_UNKNOWN;
}
