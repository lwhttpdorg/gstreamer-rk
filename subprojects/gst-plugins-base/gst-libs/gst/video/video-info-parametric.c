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
 * SECTION:video-info-parametric
 * @title: Generic Video Format
 * @short_description: Caps parsing, serialization and pack/unpack for
 *   %GST_VIDEO_FORMAT_PARAMETRIC
 *
 * This module implements support for the parametric
 * %GST_VIDEO_FORMAT_PARAMETRIC video format. The format layout is
 * parametric (per-instance) rather than fixed (per-format).
 *
 * Use gst_video_info_init_from_caps_extended() to parse caps carrying
 * format=PARAMETRIC, and gst_video_info_get_parametric_params() to access the
 * resulting #GstVideoParametricParams.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "video-info-parametric.h"
#include "video-info-ext-priv.h"
#include "video-info.h"

typedef enum
{
  PARAMETRIC_COLOR_RGB,
  PARAMETRIC_COLOR_YCBCR,
  PARAMETRIC_COLOR_GRAYSCALE,
  PARAMETRIC_COLOR_UNKNOWN,
} GenericColorModel;

static GenericColorModel
detect_color_model (const GstVideoParametricParams * params)
{
  gboolean has_r = FALSE, has_g = FALSE, has_b = FALSE;
  gboolean has_luma = FALSE, has_gray = FALSE, has_cb = FALSE, has_cr = FALSE;
  guint i;

  for (i = 0; i < params->num_components; i++) {
    switch (params->component_definition[i].type) {
      case GST_VIDEO_COMPONENT_TYPE_RED:
        has_r = TRUE;
        break;
      case GST_VIDEO_COMPONENT_TYPE_GREEN:
        has_g = TRUE;
        break;
      case GST_VIDEO_COMPONENT_TYPE_BLUE:
        has_b = TRUE;
        break;
      case GST_VIDEO_COMPONENT_TYPE_LUMA:
        has_luma = TRUE;
        break;
      case GST_VIDEO_COMPONENT_TYPE_GRAYSCALE:
        has_gray = TRUE;
        break;
      case GST_VIDEO_COMPONENT_TYPE_CHROMA_BLUE:
        has_cb = TRUE;
        break;
      case GST_VIDEO_COMPONENT_TYPE_CHROMA_RED:
        has_cr = TRUE;
        break;
      default:
        break;
    }
  }

  if (has_r && has_g && has_b)
    return PARAMETRIC_COLOR_RGB;
  if (has_luma && has_cb && has_cr)
    return PARAMETRIC_COLOR_YCBCR;
  if (has_gray || has_luma)
    return PARAMETRIC_COLOR_GRAYSCALE;
  return PARAMETRIC_COLOR_UNKNOWN;
}

static gboolean
component_type_from_string (const gchar * s, GstVideoComponentType * out)
{
  static const struct
  {
    const gchar *name;
    GstVideoComponentType type;
  } map[] = {
    {"grayscale", GST_VIDEO_COMPONENT_TYPE_GRAYSCALE},
    {"luma", GST_VIDEO_COMPONENT_TYPE_LUMA},
    {"chroma-blue", GST_VIDEO_COMPONENT_TYPE_CHROMA_BLUE},
    {"chroma-red", GST_VIDEO_COMPONENT_TYPE_CHROMA_RED},
    {"red", GST_VIDEO_COMPONENT_TYPE_RED},
    {"green", GST_VIDEO_COMPONENT_TYPE_GREEN},
    {"blue", GST_VIDEO_COMPONENT_TYPE_BLUE},
    {"alpha", GST_VIDEO_COMPONENT_TYPE_ALPHA},
    {"depth", GST_VIDEO_COMPONENT_TYPE_DEPTH},
    {"disparity", GST_VIDEO_COMPONENT_TYPE_DISPARITY},
    {"palette", GST_VIDEO_COMPONENT_TYPE_PALETTE},
    {"filter", GST_VIDEO_COMPONENT_TYPE_FILTER},
    {"padded", GST_VIDEO_COMPONENT_TYPE_PADDED},
    {"cyan", GST_VIDEO_COMPONENT_TYPE_CYAN},
    {"magenta", GST_VIDEO_COMPONENT_TYPE_MAGENTA},
    {"yellow", GST_VIDEO_COMPONENT_TYPE_YELLOW},
    {"key", GST_VIDEO_COMPONENT_TYPE_KEY},
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
    GstVideoComponentNumericalFormat * out)
{
  if (g_str_equal (s, "unsigned-int")) {
    *out = GST_VIDEO_COMPONENT_FORMAT_UNSIGNED_INT;
    return TRUE;
  }
  if (g_str_equal (s, "float")) {
    *out = GST_VIDEO_COMPONENT_FORMAT_FLOAT;
    return TRUE;
  }
  if (g_str_equal (s, "complex-number")) {
    *out = GST_VIDEO_COMPONENT_FORMAT_COMPLEX_NUMBER;
    return TRUE;
  }
  if (g_str_equal (s, "signed-int")) {
    *out = GST_VIDEO_COMPONENT_FORMAT_SIGNED_INT;
    return TRUE;
  }
  return FALSE;
}

static gboolean
interleave_type_from_string (const gchar * s, GstVideoInterleaveType * out)
{
  if (g_str_equal (s, "interleave")) {
    *out = GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE;
    return TRUE;
  }
  if (g_str_equal (s, "component")) {
    *out = GST_VIDEO_INTERLEAVE_TYPE_COMPONENT;
    return TRUE;
  }
  if (g_str_equal (s, "mixed-interleave")) {
    *out = GST_VIDEO_INTERLEAVE_TYPE_MIXED_INTERLEAVE;
    return TRUE;
  }
  if (g_str_equal (s, "row-interleave")) {
    *out = GST_VIDEO_INTERLEAVE_TYPE_ROW_INTERLEAVE;
    return TRUE;
  }
  if (g_str_equal (s, "tile-component")) {
    *out = GST_VIDEO_INTERLEAVE_TYPE_TILE_COMPONENT;
    return TRUE;
  }
  if (g_str_equal (s, "multi-Y")) {
    *out = GST_VIDEO_INTERLEAVE_TYPE_MULTI_Y;
    return TRUE;
  }
  return FALSE;
}

static const gchar *
component_type_to_string (GstVideoComponentType type)
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
component_format_to_string (GstVideoComponentNumericalFormat fmt)
{
  switch (fmt) {
    case GST_VIDEO_COMPONENT_FORMAT_UNSIGNED_INT:
      return "unsigned-int";
    case GST_VIDEO_COMPONENT_FORMAT_FLOAT:
      return "float";
    case GST_VIDEO_COMPONENT_FORMAT_COMPLEX_NUMBER:
      return "complex-number";
    case GST_VIDEO_COMPONENT_FORMAT_SIGNED_INT:
      return "signed-int";
    default:
      return NULL;
  }
}

static const gchar *
interleave_type_to_string (GstVideoInterleaveType type)
{
  switch (type) {
    case GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE:
      return "interleave";
    case GST_VIDEO_INTERLEAVE_TYPE_COMPONENT:
      return "component";
    case GST_VIDEO_INTERLEAVE_TYPE_MIXED_INTERLEAVE:
      return "mixed-interleave";
    case GST_VIDEO_INTERLEAVE_TYPE_ROW_INTERLEAVE:
      return "row-interleave";
    case GST_VIDEO_INTERLEAVE_TYPE_TILE_COMPONENT:
      return "tile-component";
    case GST_VIDEO_INTERLEAVE_TYPE_MULTI_Y:
      return "multi-Y";
    default:
      return NULL;
  }
}

/* Byte size of one component value. */
static inline guint
comp_bytes (const GstVideoParametricComponent * comp)
{
  return comp->align_size > 0 ? comp->align_size : (comp->depth + 7) / 8;
}

/* Align a byte count to a given alignment (0 = no alignment). */
static inline gsize
align_stride (gsize bytes, guint align)
{
  return align > 0 ? ((bytes + align - 1) / align) * align : bytes;
}

/* Assign an aligned stride to *out, guarding against overflow into gint. */
static inline gboolean
assign_stride (gint * out, gsize raw)
{
  if (G_UNLIKELY (raw > (gsize) G_MAXINT)) {
    GST_ERROR ("stride overflow: %" G_GSIZE_FORMAT, raw);
    return FALSE;
  }
  *out = (gint) raw;
  return TRUE;
}

static guint32
read_comp_val (const guint8 * src, guint bytes, guint depth,
    gboolean little_endian, gboolean pad_lsb)
{
  guint32 raw = 0;

  switch (bytes) {
    case 1:
      raw = src[0];
      break;
    case 2:
      raw = little_endian ? GST_READ_UINT16_LE (src) : GST_READ_UINT16_BE (src);
      break;
    case 4:
      raw = little_endian ? GST_READ_UINT32_LE (src) : GST_READ_UINT32_BE (src);
      break;
    default:
      return 0;
  }

  /* pad_lsb: value is in MSBs; shift down to extract it.
   * Guard against a misconfigured align_size smaller than depth requires. */
  if (pad_lsb) {
    if (G_UNLIKELY (bytes * 8 < depth))
      return 0;
    raw >>= (bytes * 8 - depth);
  } else {
    raw &= (1u << depth) - 1;
  }

  return raw;
}

static void
write_comp_val (guint8 * dst, guint bytes, guint depth, guint32 val,
    gboolean little_endian, gboolean pad_lsb)
{
  guint32 raw;

  val &= (1u << depth) - 1;
  /* Guard against a misconfigured align_size smaller than depth requires. */
  if (G_UNLIKELY (pad_lsb && bytes * 8 < depth))
    return;
  raw = pad_lsb ? val << (bytes * 8 - depth) : val;

  switch (bytes) {
    case 1:
      dst[0] = (guint8) raw;
      break;
    case 2:
      if (little_endian)
        GST_WRITE_UINT16_LE (dst, (guint16) raw);
      else
        GST_WRITE_UINT16_BE (dst, (guint16) raw);
      break;
    case 4:
      if (little_endian)
        GST_WRITE_UINT32_LE (dst, raw);
      else
        GST_WRITE_UINT32_BE (dst, raw);
      break;
    default:
      break;
  }
}

/* Scale a depth-bit value to 16-bit.
 * Without TRUNCATE_RANGE and depth >= 8, MSBs are replicated into LSBs so
 * that the maximum value maps exactly to 0xffff. */
static guint16
scale_to_16bit (guint32 val, guint depth, gboolean truncate)
{
  g_return_val_if_fail (depth > 0 && depth <= 16, 0);
  if (depth == 16)
    return (guint16) val;
  if (truncate || depth < 8)
    return (guint16) (val << (16 - depth));
  return (guint16) ((val << (16 - depth)) | (val >> (2 * depth - 16)));
}

static guint32
scale_from_16bit (guint16 val, guint depth)
{
  g_return_val_if_fail (depth > 0 && depth <= 16, 0);
  if (depth == 16)
    return val;
  return (guint32) (val >> (16 - depth));
}

/* Map a component -> the channel index in ARGB64/AYUV64 (A=0, R/Y=1, G/Cb=2, B/Cr=3). */
static void
map_comp_to_channel (GstVideoComponentType type, guint16 val16,
    guint16 chans[4])
{
  switch (type) {
    case GST_VIDEO_COMPONENT_TYPE_ALPHA:
      chans[0] = val16;
      break;
    case GST_VIDEO_COMPONENT_TYPE_RED:
    case GST_VIDEO_COMPONENT_TYPE_LUMA:
    case GST_VIDEO_COMPONENT_TYPE_GRAYSCALE:
      chans[1] = val16;
      break;
    case GST_VIDEO_COMPONENT_TYPE_GREEN:
    case GST_VIDEO_COMPONENT_TYPE_CHROMA_BLUE:
      chans[2] = val16;
      break;
    case GST_VIDEO_COMPONENT_TYPE_BLUE:
    case GST_VIDEO_COMPONENT_TYPE_CHROMA_RED:
      chans[3] = val16;
      break;
    default:
      break;
  }
}

/* Inverse of map_comp_to_channel: channel index -> component value. */
static guint16
channel_from_comp_type (GstVideoComponentType type, const guint16 * pixel)
{
  switch (type) {
    case GST_VIDEO_COMPONENT_TYPE_ALPHA:
      return pixel[0];
    case GST_VIDEO_COMPONENT_TYPE_RED:
    case GST_VIDEO_COMPONENT_TYPE_LUMA:
    case GST_VIDEO_COMPONENT_TYPE_GRAYSCALE:
      return pixel[1];
    case GST_VIDEO_COMPONENT_TYPE_GREEN:
    case GST_VIDEO_COMPONENT_TYPE_CHROMA_BLUE:
      return pixel[2];
    case GST_VIDEO_COMPONENT_TYPE_BLUE:
    case GST_VIDEO_COMPONENT_TYPE_CHROMA_RED:
      return pixel[3];
    default:
      return 0;
  }
}

static gboolean
validate_params_for_pack_unpack (const GstVideoParametricParams * params)
{
  guint i;

  if (params->sampling_type != 0) {
    GST_ERROR ("pack/unpack only supports sampling_type=0");
    return FALSE;
  }

  for (i = 0; i < params->num_components; i++) {
    if (params->component_definition[i].format !=
        GST_VIDEO_COMPONENT_FORMAT_UNSIGNED_INT) {
      GST_ERROR ("pack/unpack only supports unsigned-int components");
      return FALSE;
    }
    if (params->component_definition[i].depth == 0 ||
        params->component_definition[i].depth > 16) {
      GST_ERROR ("pack/unpack only supports component depth 1-16");
      return FALSE;
    }
  }

  return TRUE;
}

/* Parses PARAMETRIC-specific caps fields, computes stride/offset/size, and
 * attaches a GstVideoInfoExtensions carrying the resulting
 * GstVideoParametricParams to @info. Returns TRUE on success. */
gboolean
gst_video_info_parametric_fill_info_priv (GstVideoInfo * info,
    const GstStructure * structure)
{
  GstVideoParametricParams params = { 0, };
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
  if (n == 0 || n > GST_VIDEO_PARAMETRIC_MAX_COMPONENTS) {
    GST_ERROR ("component-definition array size %u out of range [1, %u]",
        n, GST_VIDEO_PARAMETRIC_MAX_COMPONENTS);
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
    params.interleave_type = GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE;
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

      case GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE:
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

        if (!assign_stride (&info->stride[0],
                align_stride ((gsize) width * psize, row_align)))
          return FALSE;
        info->offset[0] = 0;
        info->size = (gsize) info->stride[0] * height;
        break;
      }

      case GST_VIDEO_INTERLEAVE_TYPE_COMPONENT:
      {
        /* One plane per component (planar). */
        gsize offset = 0;

        if (n > GST_VIDEO_MAX_PLANES) {
          GST_ERROR ("too many components (%u) for component interleave "
              "(max %d)", n, GST_VIDEO_MAX_PLANES);
          return FALSE;
        }

        for (i = 0; i < n; i++) {
          if (!assign_stride (&info->stride[i],
                  align_stride (
                      (gsize) width *
                      comp_bytes (&params.component_definition[i]), row_align)))
            return FALSE;
          info->offset[i] = offset;
          offset += (gsize) info->stride[i] * height;
        }
        info->size = offset;
        break;
      }

      case GST_VIDEO_INTERLEAVE_TYPE_MIXED_INTERLEAVE:
      {
        /* Luma plane + interleaved chroma plane. */
        guint luma_bytes = 0, cb_bytes = 0, cr_bytes = 0;
        guint luma_count = 0;
        gsize offset = 0;

        for (i = 0; i < n; i++) {
          switch (params.component_definition[i].type) {
            case GST_VIDEO_COMPONENT_TYPE_LUMA:
              luma_bytes = comp_bytes (&params.component_definition[i]);
              luma_count++;
              break;
            case GST_VIDEO_COMPONENT_TYPE_CHROMA_BLUE:
              cb_bytes = comp_bytes (&params.component_definition[i]);
              break;
            case GST_VIDEO_COMPONENT_TYPE_CHROMA_RED:
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
        if (!assign_stride (&info->stride[0],
                align_stride ((gsize) width * luma_bytes, row_align)))
          return FALSE;
        info->offset[0] = 0;
        offset = (gsize) info->stride[0] * height;

        /* Interleaved chroma plane */
        if (!assign_stride (&info->stride[1],
                align_stride ((gsize) width * (cb_bytes + cr_bytes),
                    row_align)))
          return FALSE;
        info->offset[1] = offset;
        info->size = offset + (gsize) info->stride[1] * height;
        break;
      }

      case GST_VIDEO_INTERLEAVE_TYPE_ROW_INTERLEAVE:
      {
        /* Single plane, each row contains all component rows side-by-side. */
        gsize row_bytes = 0;

        for (i = 0; i < n; i++)
          row_bytes +=
              (gsize) width *comp_bytes (&params.component_definition[i]);

        if (!assign_stride (&info->stride[0],
                align_stride (row_bytes, row_align)))
          return FALSE;
        info->offset[0] = 0;
        info->size = (gsize) info->stride[0] * height;
        break;
      }

      case GST_VIDEO_INTERLEAVE_TYPE_MULTI_Y:
      {
        /* Single plane, pixel group contains multiple luma + shared chroma. */
        guint num_luma = 0;
        gsize group_bytes = 0;

        for (i = 0; i < n; i++) {
          if (params.component_definition[i].type ==
              GST_VIDEO_COMPONENT_TYPE_LUMA)
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

        if (!assign_stride (&info->stride[0],
                align_stride ((width / num_luma) * group_bytes, row_align)))
          return FALSE;
        info->offset[0] = 0;
        info->size = (gsize) info->stride[0] * height;
        break;
      }

      case GST_VIDEO_INTERLEAVE_TYPE_TILE_COMPONENT:
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
          if (!assign_stride (&info->stride[i],
                  align_stride (
                      (gsize) tile_w *
                      comp_bytes (&params.component_definition[i]), row_align)))
            return FALSE;
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
  gst_video_info_extensions_set_parametric_params_priv (ext, &params);
  info->ABI.abi.extensions = ext;

  return TRUE;
}

/* Serializes @params into @caps as PARAMETRIC-specific caps fields
 * (component-definition, sampling-type, interleave-type, and optional fields). */
void
gst_video_info_parametric_fill_caps_priv (const GstVideoParametricParams *
    params, GstCaps * caps)
{
  GValue comp_array = G_VALUE_INIT;
  guint i;

  g_value_init (&comp_array, GST_TYPE_ARRAY);

  for (i = 0; i < params->num_components; i++) {
    const GstVideoParametricComponent *cd = &params->component_definition[i];
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
 * gst_video_format_parametric_get_unpack_format:
 * @info: a #GstVideoInfo with format %GST_VIDEO_FORMAT_PARAMETRIC
 *
 * Returns the canonical format produced by gst_video_format_parametric_unpack():
 * %GST_VIDEO_FORMAT_ARGB64 for RGB, %GST_VIDEO_FORMAT_AYUV64 for YCbCr and
 * grayscale, or %GST_VIDEO_FORMAT_UNKNOWN if the colour model is unrecognised.
 *
 * Returns: the unpack format.
 * Since: 1.30
 */
GstVideoFormat
gst_video_format_parametric_get_unpack_format (const GstVideoInfo * info)
{
  const GstVideoParametricParams *params;
  GenericColorModel cm;

  g_return_val_if_fail (info != NULL, GST_VIDEO_FORMAT_UNKNOWN);

  params = gst_video_info_get_parametric_params (info);
  if (!params)
    return GST_VIDEO_FORMAT_UNKNOWN;

  cm = detect_color_model (params);
  switch (cm) {
    case PARAMETRIC_COLOR_RGB:
      return GST_VIDEO_FORMAT_ARGB64;
    case PARAMETRIC_COLOR_YCBCR:
    case PARAMETRIC_COLOR_GRAYSCALE:
      return GST_VIDEO_FORMAT_AYUV64;
    default:
      return GST_VIDEO_FORMAT_UNKNOWN;
  }
}

/**
 * gst_video_format_parametric_unpack: (skip)
 * @info: a #GstVideoInfo with format %GST_VIDEO_FORMAT_PARAMETRIC
 * @flags: #GstVideoPackFlags
 * @dest: (out caller-allocates): destination buffer for unpacked pixels
 * @data: (array fixed-size=4) (element-type gpointer): source plane pointers
 * @stride: (array fixed-size=4): source plane strides
 * @width: number of pixels to unpack
 *
 * Unpacks @width pixels from row @y (starting at column @x) into @dest as
 * ARGB64 or AYUV64 (determined by gst_video_format_parametric_get_unpack_format()).
 * Supports %GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE and %GST_VIDEO_INTERLEAVE_TYPE_COMPONENT,
 * unsigned-int components with depth 1–16, and sampling_type=0 only.
 *
 * Returns: %TRUE on success.
 * Since: 1.30
 */
gboolean
gst_video_format_parametric_unpack (const GstVideoInfo * info,
    GstVideoPackFlags flags,
    gpointer dest,
    const gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint x, gint y, gint width)
{
  const GstVideoParametricParams *params;
  GenericColorModel cm;
  gboolean truncate;
  guint16 *d;
  guint n, i;
  gint px;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (stride != NULL, FALSE);
  g_return_val_if_fail (width > 0, FALSE);

  params = gst_video_info_get_parametric_params (info);
  if (!params)
    return FALSE;

  if (!validate_params_for_pack_unpack (params))
    return FALSE;

  cm = detect_color_model (params);
  if (cm == PARAMETRIC_COLOR_UNKNOWN) {
    GST_ERROR ("unpack: unrecognised colour model");
    return FALSE;
  }

  truncate = (flags & GST_VIDEO_PACK_FLAG_TRUNCATE_RANGE) != 0;
  d = (guint16 *) dest;
  n = params->num_components;

  switch (params->interleave_type) {
    case GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE:
    {
      gsize psize = 0;
      const guint8 *src;

      if (params->pixel_size > 0)
        psize = params->pixel_size;
      else if (params->block_size > 0)
        psize = params->block_size;
      else
        for (i = 0; i < n; i++)
          psize += comp_bytes (&params->component_definition[i]);

      if (G_UNLIKELY (data[0] == NULL)) {
        GST_ERROR ("unpack: plane 0 is NULL");
        return FALSE;
      }
      src = (const guint8 *) data[0] + y * stride[0] + x * psize;

      for (px = 0; px < width; px++) {
        const guint8 *csrc = src;
        guint16 chans[4];

        /* Default alpha=opaque; default chroma=midpoint for YCbCr. */
        chans[0] = 0xffff;
        chans[1] = 0;
        chans[2] = (cm != PARAMETRIC_COLOR_RGB) ? 0x8000 : 0;
        chans[3] = (cm != PARAMETRIC_COLOR_RGB) ? 0x8000 : 0;

        for (i = 0; i < n; i++) {
          const GstVideoParametricComponent *cd =
              &params->component_definition[i];
          guint bytes = comp_bytes (cd);
          guint32 raw = read_comp_val (csrc, bytes, cd->depth,
              params->components_little_endian, params->components_pad_lsb);
          map_comp_to_channel (cd->type,
              scale_to_16bit (raw, cd->depth, truncate), chans);
          csrc += bytes;
        }

        d[px * 4 + 0] = chans[0];
        d[px * 4 + 1] = chans[1];
        d[px * 4 + 2] = chans[2];
        d[px * 4 + 3] = chans[3];
        src += psize;
      }
      break;
    }

    case GST_VIDEO_INTERLEAVE_TYPE_COMPONENT:
    {
      if (n > GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("unpack: too many components for component interleave");
        return FALSE;
      }

      for (i = 0; i < n; i++) {
        if (G_UNLIKELY (data[i] == NULL)) {
          GST_ERROR ("unpack: plane %u is NULL", i);
          return FALSE;
        }
      }

      for (px = 0; px < width; px++) {
        guint16 chans[4];

        chans[0] = 0xffff;
        chans[1] = 0;
        chans[2] = (cm != PARAMETRIC_COLOR_RGB) ? 0x8000 : 0;
        chans[3] = (cm != PARAMETRIC_COLOR_RGB) ? 0x8000 : 0;

        for (i = 0; i < n; i++) {
          const GstVideoParametricComponent *cd =
              &params->component_definition[i];
          guint bytes = comp_bytes (cd);
          const guint8 *csrc = (const guint8 *) data[i]
              + y * stride[i] + (x + px) * bytes;
          guint32 raw = read_comp_val (csrc, bytes, cd->depth,
              params->components_little_endian, params->components_pad_lsb);
          map_comp_to_channel (cd->type,
              scale_to_16bit (raw, cd->depth, truncate), chans);
        }

        d[px * 4 + 0] = chans[0];
        d[px * 4 + 1] = chans[1];
        d[px * 4 + 2] = chans[2];
        d[px * 4 + 3] = chans[3];
      }
      break;
    }

    default:
      GST_ERROR ("unpack: unsupported interleave type %d",
          params->interleave_type);
      return FALSE;
  }

  return TRUE;
}

/**
 * gst_video_format_parametric_pack: (skip)
 * @info: a #GstVideoInfo with format %GST_VIDEO_FORMAT_PARAMETRIC
 * @flags: #GstVideoPackFlags
 * @src: source buffer of canonical pixels (ARGB64 or AYUV64)
 * @sstride: stride of @src in bytes
 * @data: (array fixed-size=4) (element-type gpointer): destination plane pointers
 * @stride: (array fixed-size=4): destination plane strides
 * @y: destination row
 * @width: number of pixels to pack
 *
 * Packs @width pixels from @src into row @y of the planes in @data.
 * @src must be in the format returned by
 * gst_video_format_parametric_get_unpack_format().
 * Supports %GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE and %GST_VIDEO_INTERLEAVE_TYPE_COMPONENT,
 * unsigned-int components with depth 1–16, and sampling_type=0 only.
 *
 * Returns: %TRUE on success.
 * Since: 1.30
 */
gboolean
gst_video_format_parametric_pack (const GstVideoInfo * info,
    GstVideoPackFlags flags,
    const gpointer src, gint sstride,
    gpointer data[GST_VIDEO_MAX_PLANES],
    const gint stride[GST_VIDEO_MAX_PLANES], gint y, gint width)
{
  const GstVideoParametricParams *params;
  const guint16 *s;
  guint n, i;
  gint px;

  g_return_val_if_fail (info != NULL, FALSE);
  g_return_val_if_fail (src != NULL, FALSE);
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (stride != NULL, FALSE);
  g_return_val_if_fail (width > 0, FALSE);

  params = gst_video_info_get_parametric_params (info);
  if (!params)
    return FALSE;

  if (!validate_params_for_pack_unpack (params))
    return FALSE;

  if (detect_color_model (params) == PARAMETRIC_COLOR_UNKNOWN) {
    GST_ERROR ("pack: unrecognised colour model");
    return FALSE;
  }

  s = (const guint16 *) src;
  n = params->num_components;

  switch (params->interleave_type) {
    case GST_VIDEO_INTERLEAVE_TYPE_INTERLEAVE:
    {
      gsize psize = 0;
      guint8 *dst;

      if (params->pixel_size > 0)
        psize = params->pixel_size;
      else if (params->block_size > 0)
        psize = params->block_size;
      else
        for (i = 0; i < n; i++)
          psize += comp_bytes (&params->component_definition[i]);

      if (G_UNLIKELY (data[0] == NULL)) {
        GST_ERROR ("pack: plane 0 is NULL");
        return FALSE;
      }
      dst = (guint8 *) data[0] + y * stride[0];

      for (px = 0; px < width; px++) {
        const guint16 *spx = s + px * 4;
        guint8 *cdst = dst + px * psize;

        for (i = 0; i < n; i++) {
          const GstVideoParametricComponent *cd =
              &params->component_definition[i];
          guint bytes = comp_bytes (cd);
          guint16 val16 = channel_from_comp_type (cd->type, spx);
          write_comp_val (cdst, bytes, cd->depth,
              scale_from_16bit (val16, cd->depth),
              params->components_little_endian, params->components_pad_lsb);
          cdst += bytes;
        }
      }
      break;
    }

    case GST_VIDEO_INTERLEAVE_TYPE_COMPONENT:
    {
      if (n > GST_VIDEO_MAX_PLANES) {
        GST_ERROR ("pack: too many components for component interleave");
        return FALSE;
      }

      for (i = 0; i < n; i++) {
        if (G_UNLIKELY (data[i] == NULL)) {
          GST_ERROR ("pack: plane %u is NULL", i);
          return FALSE;
        }
      }

      for (px = 0; px < width; px++) {
        const guint16 *spx = s + px * 4;

        for (i = 0; i < n; i++) {
          const GstVideoParametricComponent *cd =
              &params->component_definition[i];
          guint bytes = comp_bytes (cd);
          guint8 *cdst = (guint8 *) data[i] + y * stride[i] + px * bytes;
          guint16 val16 = channel_from_comp_type (cd->type, spx);
          write_comp_val (cdst, bytes, cd->depth,
              scale_from_16bit (val16, cd->depth),
              params->components_little_endian, params->components_pad_lsb);
        }
      }
      break;
    }

    default:
      GST_ERROR ("pack: unsupported interleave type %d",
          params->interleave_type);
      return FALSE;
  }

  return TRUE;
}
