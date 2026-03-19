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

#ifndef __VIDEO_INFO_GENERIC_H__
#define __VIDEO_INFO_GENERIC_H__

#include <gst/gst.h>
#include <gst/video/video-prelude.h>
#include <gst/video/video-info-ext.h>
#include <gst/video/video-format.h>

G_BEGIN_DECLS

typedef struct _GstVideoInfo GstVideoInfo;

/**
 * GstComponentType:
 * @GST_COMPONENT_TYPE_GRAYSCALE: Monochrome component
 * @GST_COMPONENT_TYPE_LUMA: Luma component (Y)
 * @GST_COMPONENT_TYPE_CHROMA_BLUE: Chroma blue component (U/Cb)
 * @GST_COMPONENT_TYPE_CHROMA_RED: Chroma red component (V/Cr)
 * @GST_COMPONENT_TYPE_RED: Red component
 * @GST_COMPONENT_TYPE_GREEN: Green component
 * @GST_COMPONENT_TYPE_BLUE: Blue component
 * @GST_COMPONENT_TYPE_ALPHA: Transparency component
 * @GST_COMPONENT_TYPE_DEPTH: Depth component
 * @GST_COMPONENT_TYPE_DISPARITY: Disparity component
 * @GST_COMPONENT_TYPE_PALETTE: Palette index component
 * @GST_COMPONENT_TYPE_FILTER: Filter array component (e.g. Bayer pattern)
 * @GST_COMPONENT_TYPE_PADDED: Padding component (unused data)
 * @GST_COMPONENT_TYPE_CYAN: Cyan component
 * @GST_COMPONENT_TYPE_MAGENTA: Magenta component
 * @GST_COMPONENT_TYPE_YELLOW: Yellow component
 * @GST_COMPONENT_TYPE_KEY: Key component (black in CMYK)
 *
 * Semantic type of a video component in a %GST_VIDEO_FORMAT_GENERIC format.
 *
 * Since: 1.30
 */
typedef enum {
  GST_COMPONENT_TYPE_GRAYSCALE,
  GST_COMPONENT_TYPE_LUMA,
  GST_COMPONENT_TYPE_CHROMA_BLUE,
  GST_COMPONENT_TYPE_CHROMA_RED,
  GST_COMPONENT_TYPE_RED,
  GST_COMPONENT_TYPE_GREEN,
  GST_COMPONENT_TYPE_BLUE,
  GST_COMPONENT_TYPE_ALPHA,
  GST_COMPONENT_TYPE_DEPTH,
  GST_COMPONENT_TYPE_DISPARITY,
  GST_COMPONENT_TYPE_PALETTE,
  GST_COMPONENT_TYPE_FILTER,
  GST_COMPONENT_TYPE_PADDED,
  GST_COMPONENT_TYPE_CYAN,
  GST_COMPONENT_TYPE_MAGENTA,
  GST_COMPONENT_TYPE_YELLOW,
  GST_COMPONENT_TYPE_KEY,
} GstComponentType;

/**
 * GstComponentNumericalFormat:
 * @GST_COMPONENT_FORMAT_UNSIGNED_INT: Unsigned integer values
 * @GST_COMPONENT_FORMAT_FLOAT: floating point
 * @GST_COMPONENT_FORMAT_COMPLEX_NUMBER: Complex number (floating point)
 * @GST_COMPONENT_FORMAT_SIGNED_INT: Signed integer values
 *
 * Numerical encoding of a component value in a %GST_VIDEO_FORMAT_GENERIC format.
 *
 * Since: 1.30
 */
typedef enum {
  GST_COMPONENT_FORMAT_UNSIGNED_INT,
  GST_COMPONENT_FORMAT_FLOAT,
  GST_COMPONENT_FORMAT_COMPLEX_NUMBER,
  GST_COMPONENT_FORMAT_SIGNED_INT,
} GstComponentNumericalFormat;

/**
 * GstInterleaveType:
 * @GST_INTERLEAVE_TYPE_INTERLEAVE: All components of each pixel are stored
 *   together
 * @GST_INTERLEAVE_TYPE_COMPONENT: All values of each component are stored
 *   together (planar)
 * @GST_INTERLEAVE_TYPE_MIXED_INTERLEAVE: Luma plane(s) followed by an
 *   interleaved chroma plane (semi-planar)
 * @GST_INTERLEAVE_TYPE_ROW_INTERLEAVE: Complete rows of each component
 *   stored sequentially within each row.
 * @GST_INTERLEAVE_TYPE_TILE_COMPONENT: Frame divided into tiles, within each
 *   tile all component planes are stored sequentially.
 * @GST_INTERLEAVE_TYPE_MULTI_Y: Multiple luma samples interleaved with shared
 *   chroma values per pixel group (e.g. YUYV 4:2:2).
 *
 * Memory arrangement of components in a %GST_VIDEO_FORMAT_GENERIC format.
 *
 * Since: 1.30
 */
typedef enum {
  GST_INTERLEAVE_TYPE_INTERLEAVE,
  GST_INTERLEAVE_TYPE_COMPONENT,
  GST_INTERLEAVE_TYPE_MIXED_INTERLEAVE,
  GST_INTERLEAVE_TYPE_ROW_INTERLEAVE,
  GST_INTERLEAVE_TYPE_TILE_COMPONENT,
  GST_INTERLEAVE_TYPE_MULTI_Y,
} GstInterleaveType;

/**
 * GstComponentDefinition:
 * @type: Semantic type of this component
 * @depth: Bit depth (number of bits per value)
 * @format: Numerical encoding format
 * @align_size: Component alignment in bytes
 *
 * Describes one component in a %GST_VIDEO_FORMAT_GENERIC pixel layout.
 * Components are listed in memory layout order.
 *
 * Since: 1.30
 */
typedef struct {
  GstComponentType            type;
  guint                       depth;
  GstComponentNumericalFormat format;
  guint                       align_size;
} GstComponentDefinition;

/**
 * GST_GENERIC_VIDEO_MAX_COMPONENTS:
 *
 * Maximum number of components supported in a #GstGenericVideoParams
 * component_definition array.
 *
 * Since: 1.30
 */
#define GST_GENERIC_VIDEO_MAX_COMPONENTS 16

/**
 * GstGenericVideoParams:
 * @version: Structure version
 * @num_components: Number of valid entries in @component_definition
 * @component_definition: Per-component descriptions in memory layout order
 * @sampling_type: Chroma subsampling
 * @interleave_type: Component memory arrangement
 * @block_size: Block alignment in bytes (0 = no blocking)
 * @components_little_endian: Byte order for multi-byte component values
 * @components_pad_lsb: When align_size > depth — %TRUE: padding at LSB (value
 *   occupies MSBs); %FALSE: padding at MSB (value occupies LSBs)
 * @block_pad_lsb: Padding bit location within blocks (%TRUE = LSB, %FALSE = MSB)
 * @block_little_endian: Byte order within blocks
 * @block_reversed: Reverse component order within blocks
 * @pixel_size: Total bytes per pixel (0 = auto-calculate from component depths)
 * @row_align_size: Row stride alignment in bytes (0 = no alignment)
 * @num_tile_cols: Number of tile columns (1 = no tiling)
 * @num_tile_rows: Number of tile rows (1 = no tiling)
 *
 * Parametric description of a %GST_VIDEO_FORMAT_GENERIC video format.
 * Obtained via gst_video_info_get_generic_params().
 *
 * Since: 1.30
 */
typedef struct {
  guint                  version;
  guint                  num_components;
  GstComponentDefinition component_definition[GST_GENERIC_VIDEO_MAX_COMPONENTS];
  guint                  sampling_type;
  GstInterleaveType      interleave_type;
  guint                  block_size;
  gboolean               components_little_endian;
  gboolean               components_pad_lsb;
  gboolean               block_pad_lsb;
  gboolean               block_little_endian;
  gboolean               block_reversed;
  guint                  pixel_size;
  guint                  row_align_size;
  guint                  num_tile_cols;
  guint                  num_tile_rows;
} GstGenericVideoParams;

/**
 * gst_video_format_generic_get_unpack_format:
 * @info: a #GstVideoInfo with format %GST_VIDEO_FORMAT_GENERIC
 *
 * Returns the canonical format that gst_video_format_generic_unpack() will
 * produce for @info: %GST_VIDEO_FORMAT_ARGB64 for RGB colour models,
 * %GST_VIDEO_FORMAT_AYUV64 for YCbCr and grayscale, or
 * %GST_VIDEO_FORMAT_UNKNOWN if the colour model cannot be determined.
 *
 * Returns: the unpack format, or %GST_VIDEO_FORMAT_UNKNOWN on error.
 * Since: 1.30
 */
GST_VIDEO_API
GstVideoFormat gst_video_format_generic_get_unpack_format (const GstVideoInfo * info);

G_END_DECLS

#endif /* __VIDEO_INFO_GENERIC_H__ */

