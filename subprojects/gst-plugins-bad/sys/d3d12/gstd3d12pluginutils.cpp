/* GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <vector>

#define _XM_NO_INTRINSICS_
#include <DirectXMath.h>

/* *INDENT-OFF* */
using namespace DirectX;
/* *INDENT-ON* */

/* Enable this once we use debug print in this file */
#if 0
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT ensure_debug_category()
static GstDebugCategory *
ensure_debug_category (void)
{
  static GstDebugCategory *cat = nullptr;

  GST_D3D12_CALL_ONCE_BEGIN {
    cat = _gst_debug_category_new ("d3d12pluginutils", 0, "d3d12pluginutils");
  } GST_D3D12_CALL_ONCE_END;

  return cat;
}
#endif /* GST_DISABLE_GST_DEBUG */
#endif

GType
gst_d3d12_sampling_method_get_type (void)
{
  static GType type = 0;
  static const GEnumValue methods[] = {
    {GST_D3D12_SAMPLING_METHOD_NEAREST,
        "Nearest Neighbour", "nearest-neighbour"},
    {GST_D3D12_SAMPLING_METHOD_BILINEAR,
        "Bilinear", "bilinear"},
    {GST_D3D12_SAMPLING_METHOD_LINEAR_MINIFICATION,
        "Linear minification, point magnification", "linear-minification"},
    {GST_D3D12_SAMPLING_METHOD_ANISOTROPIC, "Anisotropic", "anisotropic"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12SamplingMethod", methods);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

struct SamplingMethodMap
{
  GstD3D12SamplingMethod method;
  D3D12_FILTER filter;
};

static const SamplingMethodMap sampling_method_map[] = {
  {GST_D3D12_SAMPLING_METHOD_NEAREST, D3D12_FILTER_MIN_MAG_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_BILINEAR, D3D12_FILTER_MIN_MAG_MIP_LINEAR},
  {GST_D3D12_SAMPLING_METHOD_LINEAR_MINIFICATION,
      D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT},
  {GST_D3D12_SAMPLING_METHOD_ANISOTROPIC, D3D12_FILTER_ANISOTROPIC},
};

D3D12_FILTER
gst_d3d12_sampling_method_to_native (GstD3D12SamplingMethod method)
{
  for (guint i = 0; i < G_N_ELEMENTS (sampling_method_map); i++) {
    if (sampling_method_map[i].method == method)
      return sampling_method_map[i].filter;
  }

  return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

GType
gst_d3d12_msaa_mode_get_type (void)
{
  static GType type = 0;
  static const GEnumValue msaa_mode[] = {
    {GST_D3D12_MSAA_DISABLED, "Disabled", "disabled"},
    {GST_D3D12_MSAA_2X, "2x MSAA", "2x"},
    {GST_D3D12_MSAA_4X, "4x MSAA", "4x"},
    {GST_D3D12_MSAA_8X, "8x MSAA", "8x"},
    {0, nullptr, nullptr},
  };

  GST_D3D12_CALL_ONCE_BEGIN {
    type = g_enum_register_static ("GstD3D12MSAAMode", msaa_mode);
  } GST_D3D12_CALL_ONCE_END;

  return type;
}

gboolean
gst_d3d12_need_transform (gfloat rotation_x, gfloat rotation_y,
    gfloat rotation_z, gfloat scale_x, gfloat scale_y)
{
  const gfloat min_diff = 0.00001f;

  if (!XMScalarNearEqual (rotation_x, 0.0f, min_diff) ||
      !XMScalarNearEqual (rotation_y, 0.0f, min_diff) ||
      !XMScalarNearEqual (rotation_z, 0.0f, min_diff) ||
      !XMScalarNearEqual (scale_x, 1.0f, min_diff) ||
      !XMScalarNearEqual (scale_y, 1.0f, min_diff)) {
    return TRUE;
  }

  return FALSE;
}

gboolean
gst_d3d12_is_windows_10_or_greater (void)
{
  static gboolean ret = FALSE;
  GST_D3D12_CALL_ONCE_BEGIN {
    ret = g_win32_check_windows_version (10, 0, 0, G_WIN32_OS_ANY);
  } GST_D3D12_CALL_ONCE_END;

  return ret;
}

void
gst_d3d12_calculate_sample_desc_for_msaa (GstD3D12Device * device,
    DXGI_FORMAT format, GstD3D12MSAAMode msaa_mode, DXGI_SAMPLE_DESC * desc)
{
  desc->Count = 1;
  desc->Quality = 0;

  auto device_handle = gst_d3d12_device_get_device_handle (device);

  UINT sample_count = 1;
  switch (msaa_mode) {
    case GST_D3D12_MSAA_2X:
      sample_count = 2;
      break;
    case GST_D3D12_MSAA_4X:
      sample_count = 4;
      break;
    case GST_D3D12_MSAA_8X:
      sample_count = 8;
      break;
    default:
      break;
  }

  D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS feature_data = { };
  feature_data.Format = format;
  feature_data.SampleCount = sample_count;

  while (feature_data.SampleCount > 1) {
    auto hr =
        device_handle->CheckFeatureSupport
        (D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
        &feature_data, sizeof (feature_data));
    if (SUCCEEDED (hr) && feature_data.NumQualityLevels > 0)
      break;

    feature_data.SampleCount /= 2;
  }

  if (feature_data.SampleCount > 1 && feature_data.NumQualityLevels > 0) {
    desc->Count = feature_data.SampleCount;
    desc->Quality = feature_data.NumQualityLevels - 1;
  }
}

GstCaps *
gst_d3d12_caps_remove_format_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          NULL);
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

GstCaps *
gst_d3d12_caps_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_from_string (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
        gst_structure_set (st, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
      }
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

GstCaps *
gst_d3d12_caps_remove_format_and_rangify_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;
  GstCapsFeatures *feature =
      gst_caps_features_new_single_static_str
      (GST_CAPS_FEATURE_MEMORY_D3D12_MEMORY);

  res = gst_caps_new_empty ();

  n = gst_caps_get_size (caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure (caps, i);
    f = gst_caps_get_features (caps, i);

    /* If this is already expressed by the existing caps
     * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full (res, st, f))
      continue;

    st = gst_structure_copy (st);
    /* Only remove format info for the cases when we can actually convert */
    if (!gst_caps_features_is_any (f)
        && gst_caps_features_is_equal (f, feature)) {
      gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, nullptr);
      /* if pixel aspect ratio, make a range of it */
      if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
        gst_structure_set (st, "pixel-aspect-ratio",
            GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, nullptr);
      }
      gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
          nullptr);
    }

    gst_caps_append_structure_full (res, st, gst_caps_features_copy (f));
  }
  gst_caps_features_free (feature);

  return res;
}

/*
 * This is an incomplete matrix of in formats and a score for the prefered output
 * format.
 *
 *         out: RGB24   RGB16  ARGB  AYUV  YUV444  YUV422 YUV420 YUV411 YUV410  PAL  GRAY
 *  in
 * RGB24          0      2       1     2     2       3      4      5      6      7    8
 * RGB16          1      0       1     2     2       3      4      5      6      7    8
 * ARGB           2      3       0     1     4       5      6      7      8      9    10
 * AYUV           3      4       1     0     2       5      6      7      8      9    10
 * YUV444         2      4       3     1     0       5      6      7      8      9    10
 * YUV422         3      5       4     2     1       0      6      7      8      9    10
 * YUV420         4      6       5     3     2       1      0      7      8      9    10
 * YUV411         4      6       5     3     2       1      7      0      8      9    10
 * YUV410         6      8       7     5     4       3      2      1      0      9    10
 * PAL            1      3       2     6     4       6      7      8      9      0    10
 * GRAY           1      4       3     2     1       5      6      7      8      9    0
 *
 * PAL or GRAY are never prefered, if we can we would convert to PAL instead
 * of GRAY, though
 * less subsampling is prefered and if any, preferably horizontal
 * We would like to keep the alpha, even if we would need to to colorspace conversion
 * or lose depth.
 */
#define SCORE_FORMAT_CHANGE       1
#define SCORE_DEPTH_CHANGE        1
#define SCORE_ALPHA_CHANGE        1
#define SCORE_CHROMA_W_CHANGE     1
#define SCORE_CHROMA_H_CHANGE     1
#define SCORE_PALETTE_CHANGE      1

#define SCORE_COLORSPACE_LOSS     2     /* RGB <-> YUV */
#define SCORE_DEPTH_LOSS          4     /* change bit depth */
#define SCORE_ALPHA_LOSS          8     /* lose the alpha channel */
#define SCORE_CHROMA_W_LOSS      16     /* vertical subsample */
#define SCORE_CHROMA_H_LOSS      32     /* horizontal subsample */
#define SCORE_PALETTE_LOSS       64     /* convert to palette format */
#define SCORE_COLOR_LOSS        128     /* convert to GRAY */

#define COLORSPACE_MASK (GST_VIDEO_FORMAT_FLAG_YUV | \
                         GST_VIDEO_FORMAT_FLAG_RGB | GST_VIDEO_FORMAT_FLAG_GRAY)
#define ALPHA_MASK      (GST_VIDEO_FORMAT_FLAG_ALPHA)
#define PALETTE_MASK    (GST_VIDEO_FORMAT_FLAG_PALETTE)

void
gst_d3d12_video_format_score_value (const GstVideoFormatInfo * in_info,
    const GValue * val, gint * min_loss, const GstVideoFormatInfo ** out_info)
{
  const gchar *fname;
  const GstVideoFormatInfo *t_info;
  guint in_flags, t_flags;
  gint loss;

  fname = g_value_get_string (val);
  t_info = gst_video_format_get_info (gst_video_format_from_string (fname));
  if (!t_info || t_info->format == GST_VIDEO_FORMAT_UNKNOWN)
    return;

  /* accept input format immediately without loss */
  if (in_info == t_info) {
    *min_loss = 0;
    *out_info = t_info;
    return;
  }

  loss = SCORE_FORMAT_CHANGE;

  in_flags = GST_VIDEO_FORMAT_INFO_FLAGS (in_info);
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  in_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  t_flags = GST_VIDEO_FORMAT_INFO_FLAGS (t_info);
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_LE;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_COMPLEX;
  t_flags &= ~GST_VIDEO_FORMAT_FLAG_UNPACK;

  if ((t_flags & PALETTE_MASK) != (in_flags & PALETTE_MASK)) {
    loss += SCORE_PALETTE_CHANGE;
    if (t_flags & PALETTE_MASK)
      loss += SCORE_PALETTE_LOSS;
  }

  if ((t_flags & COLORSPACE_MASK) != (in_flags & COLORSPACE_MASK)) {
    loss += SCORE_COLORSPACE_LOSS;
    if (t_flags & GST_VIDEO_FORMAT_FLAG_GRAY)
      loss += SCORE_COLOR_LOSS;
  }

  if ((t_flags & ALPHA_MASK) != (in_flags & ALPHA_MASK)) {
    loss += SCORE_ALPHA_CHANGE;
    if (in_flags & ALPHA_MASK)
      loss += SCORE_ALPHA_LOSS;
  }

  if ((in_info->h_sub[1]) != (t_info->h_sub[1])) {
    loss += SCORE_CHROMA_H_CHANGE;
    if ((in_info->h_sub[1]) < (t_info->h_sub[1]))
      loss += SCORE_CHROMA_H_LOSS;
  }
  if ((in_info->w_sub[1]) != (t_info->w_sub[1])) {
    loss += SCORE_CHROMA_W_CHANGE;
    if ((in_info->w_sub[1]) < (t_info->w_sub[1]))
      loss += SCORE_CHROMA_W_LOSS;
  }

  if ((in_info->bits) != (t_info->bits)) {
    loss += SCORE_DEPTH_CHANGE;
    if ((in_info->bits) > (t_info->bits))
      loss += SCORE_DEPTH_LOSS + (in_info->bits - t_info->bits);
  }

  if (loss < *min_loss) {
    *out_info = t_info;
    *min_loss = loss;
  }
}
