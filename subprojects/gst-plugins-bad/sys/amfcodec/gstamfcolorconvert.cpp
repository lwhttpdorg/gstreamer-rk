/* GStreamer
 * Copyright (C) 2026 The GStreamer contributors.
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
 * SECTION:element-amfcolorconvert
 * @title: amfcolorconvert
 * @short_description: AMD AMF based color space and pixel format converter
 *
 * Wraps AMF's `AMFVideoConverter` component (the same component the
 * AMF runtime exposes to ffmpeg's `vpp_amf` filter). The element
 * supports color space, pixel format and (optionally) size
 * conversion on AMD GPUs.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamfcolorconvert.h"
#include "gstamfbasefilter.h"
#include "gstamfutils.h"

#include <components/VideoConverter.h>
#include <components/ColorSpace.h>
#include <core/Factory.h>

#include <string>

#ifdef G_OS_WIN32
#include <gst/d3d11/gstd3d11.h>
#endif

GST_DEBUG_CATEGORY_STATIC (gst_amf_color_convert_debug);
#define GST_CAT_DEFAULT gst_amf_color_convert_debug

/* *INDENT-OFF* */
using namespace amf;
/* *INDENT-ON* */

#define GST_AMF_CC_FORMATS \
    "{ NV12, P010_10LE, BGRA, RGBA, BGRx, RGBx, I420, YV12, YUY2, GRAY8 }"

/* Class init data attached at registration time. */
typedef struct _GstAmfColorConvertClassData
{
  GstCaps *sink_caps;
  GstCaps *src_caps;
  gint64 adapter_luid;
  guint device_index;
} GstAmfColorConvertClassData;

enum
{
  PROP_0,
  PROP_SCALE_TYPE,
  PROP_GAMMA_MODE,
  PROP_PRIMARIES_MODE,
  PROP_KEEP_ASPECT_RATIO,
  PROP_FILL,
  PROP_ADD_BORDERS,
  PROP_BORDER_COLOR,
};

#define DEFAULT_SCALE_TYPE          AMF_VIDEO_CONVERTER_SCALE_BILINEAR
#define DEFAULT_GAMMA_MODE          GST_VIDEO_GAMMA_MODE_NONE
#define DEFAULT_PRIMARIES_MODE      GST_VIDEO_PRIMARIES_MODE_NONE
#define DEFAULT_KEEP_ASPECT_RATIO   FALSE
#define DEFAULT_FILL                FALSE
#define DEFAULT_BORDER_COLOR        0x00000000  /* opaque black, packed AMFColor */

typedef struct _GstAmfColorConvert GstAmfColorConvert;
typedef struct _GstAmfColorConvertClass GstAmfColorConvertClass;

struct _GstAmfColorConvert
{
  GstAmfBaseFilter parent;

  /* GObject properties (read/write under object lock). */
  gint scale_type;
  GstVideoGammaMode gamma_mode;
  GstVideoPrimariesMode primaries_mode;
  gboolean keep_aspect_ratio;
  gboolean fill;
  guint border_color;
};

struct _GstAmfColorConvertClass
{
  GstAmfBaseFilterClass parent_class;
};

#define gst_amf_color_convert_parent_class color_convert_parent_class
static GTypeClass *color_convert_parent_class = NULL;

static void gst_amf_color_convert_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_amf_color_convert_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstCaps *gst_amf_color_convert_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_amf_color_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);

static const wchar_t *gst_amf_color_convert_get_component_id (GstAmfBaseFilter *
    self);
static gboolean gst_amf_color_convert_configure_component (GstAmfBaseFilter *
    self, AMFComponent * comp, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info);

static void gst_amf_cc_transfer_colorimetry_from_input (GstBaseTransform *
    trans, GstCaps * in_caps, GstCaps * out_caps);

/* ============================================================
 *   Property GTypes
 * ============================================================ */

#define GST_TYPE_AMF_CC_SCALE_TYPE (gst_amf_cc_scale_type_get_type())
static GType
gst_amf_cc_scale_type_get_type (void)
{
  static GType type = 0;
  static const GEnumValue values[] = {
    {AMF_VIDEO_CONVERTER_SCALE_BILINEAR, "Bilinear", "bilinear"},
    {AMF_VIDEO_CONVERTER_SCALE_BICUBIC, "Bicubic", "bicubic"},
    {0, nullptr, nullptr}
  };

  if (g_once_init_enter (&type)) {
    GType t = g_enum_register_static ("GstAmfColorConvertScaleType", values);
    g_once_init_leave (&type, t);
  }
  return type;
}

/* ============================================================
 *   Class init / instance init
 * ============================================================ */

static void
gst_amf_color_convert_class_init (GstAmfColorConvertClass * klass,
    gpointer data)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstAmfBaseFilterClass *base_class = GST_AMF_BASE_FILTER_CLASS (klass);
  GstAmfColorConvertClassData *cdata = (GstAmfColorConvertClassData *) data;

  color_convert_parent_class = (GTypeClass *) g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_amf_color_convert_set_property;
  gobject_class->get_property = gst_amf_color_convert_get_property;

  g_object_class_install_property (gobject_class, PROP_SCALE_TYPE,
      g_param_spec_enum ("scale-type", "Scale type",
          "Scaling algorithm to use when resizing",
          GST_TYPE_AMF_CC_SCALE_TYPE, DEFAULT_SCALE_TYPE,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_GAMMA_MODE,
      g_param_spec_enum ("gamma-mode", "Gamma mode",
          "Gamma conversion mode", GST_TYPE_VIDEO_GAMMA_MODE,
          DEFAULT_GAMMA_MODE,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PRIMARIES_MODE,
      g_param_spec_enum ("primaries-mode", "Primaries Mode",
          "Primaries conversion mode", GST_TYPE_VIDEO_PRIMARIES_MODE,
          DEFAULT_PRIMARIES_MODE,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_KEEP_ASPECT_RATIO,
      g_param_spec_boolean ("keep-aspect-ratio", "Keep aspect ratio",
          "Keep the original aspect ratio when scaling and fill padding "
          "(AMF KeepAspectRatio and Fill)",
          DEFAULT_KEEP_ASPECT_RATIO,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FILL,
      g_param_spec_boolean ("fill", "Fill",
          "Fill area outside of region of interest with border-color",
          DEFAULT_FILL,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Alias for both keep-aspect-ratio and fill settings for concistency",
          DEFAULT_KEEP_ASPECT_RATIO,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_BORDER_COLOR,
      g_param_spec_uint ("border-color", "Border color",
          "Border color when add-borders or both keep-aspect-ratio and fill are enabled (RGBA, 0xAARRGGBB)",
          0, G_MAXUINT, DEFAULT_BORDER_COLOR,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  gst_element_class_set_static_metadata (element_class,
      "AMD AMF Video Color Converter",
      "Filter/Converter/Video/Scaler/Colorspace/Hardware",
      "Convert color space and pixel format using AMD AMF",
      "GStreamer AMF contributors");

  trans_class->passthrough_on_same_caps = FALSE;
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_amf_color_convert_transform_caps);
  trans_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_amf_color_convert_fixate_caps);

  base_class->get_component_id =
      GST_DEBUG_FUNCPTR (gst_amf_color_convert_get_component_id);
  base_class->configure_component =
      GST_DEBUG_FUNCPTR (gst_amf_color_convert_configure_component);

  gst_type_mark_as_plugin_api (GST_TYPE_AMF_CC_SCALE_TYPE,
      (GstPluginAPIFlags) 0);

  /* Stash for instance init. We cannot rely on a per-class GValue
   * passed by g_type_register_static(): we keep the cdata pointer
   * itself on the class via type qdata. */
  g_type_set_qdata (G_TYPE_FROM_CLASS (klass),
      g_quark_from_static_string ("gst-amf-cc-cdata"), cdata);
}

static void
gst_amf_color_convert_init (GstAmfColorConvert * self)
{
  GstAmfColorConvertClassData *cdata = (GstAmfColorConvertClassData *)
      g_type_get_qdata (G_OBJECT_TYPE (self),
      g_quark_from_static_string ("gst-amf-cc-cdata"));

  if (cdata) {
    gst_amf_base_filter_set_subclass_data (GST_AMF_BASE_FILTER (self),
        cdata->adapter_luid, cdata->device_index);
  }

  self->scale_type = DEFAULT_SCALE_TYPE;
  self->gamma_mode = DEFAULT_GAMMA_MODE;
  self->primaries_mode = DEFAULT_PRIMARIES_MODE;
  self->keep_aspect_ratio = DEFAULT_KEEP_ASPECT_RATIO;
  self->fill = DEFAULT_FILL;
  self->border_color = DEFAULT_BORDER_COLOR;
}

static void
gst_amf_color_convert_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAmfColorConvert *self = (GstAmfColorConvert *) object;

  switch (prop_id) {
    case PROP_SCALE_TYPE:
      self->scale_type = g_value_get_enum (value);
      break;
    case PROP_GAMMA_MODE:
      self->gamma_mode = (GstVideoGammaMode) g_value_get_enum (value);
      break;
    case PROP_PRIMARIES_MODE:
      self->primaries_mode = (GstVideoPrimariesMode) g_value_get_enum (value);
      break;
    case PROP_KEEP_ASPECT_RATIO:
      self->keep_aspect_ratio = g_value_get_boolean (value);
      break;
    case PROP_FILL:
      self->fill = g_value_get_boolean (value);
      break;
    case PROP_ADD_BORDERS:
      self->keep_aspect_ratio = g_value_get_boolean (value);
      self->fill = g_value_get_boolean (value);
      break;
    case PROP_BORDER_COLOR:
      self->border_color = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_amf_color_convert_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAmfColorConvert *self = (GstAmfColorConvert *) object;

  switch (prop_id) {
    case PROP_SCALE_TYPE:
      g_value_set_enum (value, self->scale_type);
      break;
    case PROP_GAMMA_MODE:
      g_value_set_enum (value, self->gamma_mode);
      break;
    case PROP_PRIMARIES_MODE:
      g_value_set_enum (value, self->primaries_mode);
      break;
    case PROP_KEEP_ASPECT_RATIO:
      g_value_set_boolean (value, self->keep_aspect_ratio);
      break;
    case PROP_FILL:
      g_value_set_boolean (value, self->fill);
      break;
    case PROP_ADD_BORDERS:
      g_value_set_boolean (value, self->keep_aspect_ratio && self->fill);
      break;
    case PROP_BORDER_COLOR:
      g_value_set_uint (value, self->border_color);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* ============================================================
 *   Caps negotiation
 * ============================================================ */

static AMF_VIDEO_CONVERTER_COLOR_PROFILE_ENUM
gst_amf_cc_color_profile_from_colorimetry (const GstVideoColorimetry * cinfo)
{
  switch (cinfo->matrix) {
    case GST_VIDEO_COLOR_MATRIX_BT601:
      if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255)
        return AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_601;
      return AMF_VIDEO_CONVERTER_COLOR_PROFILE_601;
    case GST_VIDEO_COLOR_MATRIX_BT709:
      if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255)
        return AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_709;
      return AMF_VIDEO_CONVERTER_COLOR_PROFILE_709;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      if (cinfo->range == GST_VIDEO_COLOR_RANGE_0_255)
        return AMF_VIDEO_CONVERTER_COLOR_PROFILE_FULL_2020;
      return AMF_VIDEO_CONVERTER_COLOR_PROFILE_2020;
    default:
      break;
  }
  return AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN;
}

static AMF_COLOR_RANGE_ENUM
gst_amf_cc_range_from_gst (GstVideoColorRange range)
{
  switch (range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      return AMF_COLOR_RANGE_FULL;
    case GST_VIDEO_COLOR_RANGE_16_235:
      return AMF_COLOR_RANGE_STUDIO;
    default:
      break;
  }
  return AMF_COLOR_RANGE_UNDEFINED;
}

static gboolean
gst_amf_cc_subsampling_unchanged (const GstVideoInfo * in_info,
    const GstVideoInfo * out_info)
{
  guint i;
  const GstVideoFormatInfo *in_format, *out_format;

  if (GST_VIDEO_INFO_N_COMPONENTS (in_info) !=
      GST_VIDEO_INFO_N_COMPONENTS (out_info))
    return FALSE;

  in_format = in_info->finfo;
  out_format = out_info->finfo;

  for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (in_info); i++) {
    if (GST_VIDEO_FORMAT_INFO_W_SUB (in_format,
            i) != GST_VIDEO_FORMAT_INFO_W_SUB (out_format, i))
      return FALSE;
    if (GST_VIDEO_FORMAT_INFO_H_SUB (in_format,
            i) != GST_VIDEO_FORMAT_INFO_H_SUB (out_format, i))
      return FALSE;
  }

  return TRUE;
}

static void
gst_amf_cc_transfer_colorimetry_from_input (GstBaseTransform * trans,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstStructure *out_caps_s = gst_caps_get_structure (out_caps, 0);
  GstStructure *in_caps_s = gst_caps_get_structure (in_caps, 0);
  gboolean have_colorimetry =
      gst_structure_has_field (out_caps_s, "colorimetry");
  gboolean have_chroma_site =
      gst_structure_has_field (out_caps_s, "chroma-site");

  if (have_colorimetry && have_chroma_site)
    return;

  {
    GstVideoInfo in_info, out_info;
    const GValue *in_colorimetry =
        gst_structure_get_value (in_caps_s, "colorimetry");

    if (!gst_video_info_from_caps (&in_info, in_caps)) {
      GST_WARNING_OBJECT (trans,
          "Failed to convert sink pad caps to video info");
      return;
    }
    if (!gst_video_info_from_caps (&out_info, out_caps)) {
      GST_WARNING_OBJECT (trans,
          "Failed to convert src pad caps to video info");
      return;
    }

    if (!have_colorimetry && in_colorimetry != NULL) {
      if ((GST_VIDEO_INFO_IS_YUV (&out_info)
              && GST_VIDEO_INFO_IS_YUV (&in_info))
          || (GST_VIDEO_INFO_IS_RGB (&out_info)
              && GST_VIDEO_INFO_IS_RGB (&in_info))
          || (GST_VIDEO_INFO_IS_GRAY (&out_info)
              && GST_VIDEO_INFO_IS_GRAY (&in_info))) {
        gst_structure_set_value (out_caps_s, "colorimetry", in_colorimetry);
      } else {
        gchar *colorimetry_str;

        out_info.colorimetry.primaries = in_info.colorimetry.primaries;
        out_info.colorimetry.transfer = in_info.colorimetry.transfer;

        colorimetry_str =
            gst_video_colorimetry_to_string (&out_info.colorimetry);
        gst_caps_set_simple (out_caps, "colorimetry", G_TYPE_STRING,
            colorimetry_str, NULL);
        g_free (colorimetry_str);
      }
    }

    if (!have_chroma_site && GST_VIDEO_INFO_IS_YUV (&out_info)) {
      if (GST_VIDEO_INFO_IS_YUV (&in_info)) {
        const GValue *in_chroma_site =
            gst_structure_get_value (in_caps_s, "chroma-site");
        if (in_chroma_site != NULL
            && gst_amf_cc_subsampling_unchanged (&in_info, &out_info))
          gst_structure_set_value (out_caps_s, "chroma-site", in_chroma_site);
      }
    }
  }
}

/* gst_caps_intersect_first() requires GstCapsFeatures to match exactly between
 * structures. Upstream videoconvert often adds meta features (e.g. overlay
 * composition) that plain pad templates from gst_caps_from_string() do not
 * carry — intersection would be empty and the pipeline fails NOT_NEGOTIATED.
 * We only transform caps we can feed through AMF host buffers and normalize
 * features to system memory for intersection with our templates (same idea as
 * gst_video_convert_is_supported_caps_features in gstvideoconvertscale). */
static gboolean
gst_amf_cc_is_supported_caps_features (const GstCapsFeatures * features)
{
  guint n_features;

  if (!features || gst_caps_features_is_any (features))
    return FALSE;

  n_features = gst_caps_features_get_size (features);
  for (guint i = 0; i < n_features; i++) {
    const gchar *feature = gst_caps_features_get_nth (features, i);

    if (!g_strcmp0 (feature, GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY))
      continue;
    if (!g_strcmp0 (feature, GST_CAPS_FEATURE_FORMAT_INTERLACED))
      continue;
    if (!g_strcmp0 (feature,
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION))
      continue;
#ifdef G_OS_WIN32
    if (!g_strcmp0 (feature, GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY))
      continue;
#endif

    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_amf_color_convert_caps_remove_format_and_size_info (GstCaps * caps)
{
  GstStructure *st;
  GstCaps *res;
  gint i, n;

  res = gst_caps_new_empty ();
  n = gst_caps_get_size (caps);

  for (i = 0; i < n; i++) {
    GstCapsFeatures *feat = gst_caps_get_features (caps, i);

    st = gst_structure_copy (gst_caps_get_structure (caps, i));

    if (!gst_amf_cc_is_supported_caps_features (feat)) {
      gst_caps_append_structure_full (res, st, gst_caps_features_copy (feat));
      continue;
    }

    gst_structure_set (st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    gst_structure_remove_fields (st, "format", "colorimetry", "chroma-site",
        NULL);
    if (gst_structure_has_field (st, "pixel-aspect-ratio")) {
      gst_structure_set (st, "pixel-aspect-ratio", GST_TYPE_FRACTION_RANGE,
          1, G_MAXINT, G_MAXINT, 1, NULL);
    }

#ifdef G_OS_WIN32
    if (feat
        && gst_caps_features_contains (feat,
            GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY)) {
      gst_caps_append_structure_full (res, st,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
    } else {
#endif
      gst_caps_append_structure_full (res, st,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_SYSTEM_MEMORY, NULL));
#ifdef G_OS_WIN32
    }
#endif
  }

  return res;
}

static GstCaps *
gst_amf_color_convert_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *tmp, *result;

  tmp = gst_amf_color_convert_caps_remove_format_and_size_info (caps);

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, result);

  return result;
}

static GstCaps *
gst_amf_color_convert_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstVideoInfo vinfo;
  GstCaps *want, *inter;

  /* gst_caps_fixate() picks the first structure in our broad AMF template.
   * Registration order often prefers RGBA at tiny default dimensions, which
   * real sinks (autovideosink) will not negotiate together with HD/YUV input,
   * yielding NOT_NEGOTIATED. Prefer passthrough (same format & WxH as sink)
   * whenever that appears in @othercaps; fall back only if AMF cannot do it. */
  if (direction == GST_PAD_SINK && gst_video_info_from_caps (&vinfo, caps)) {
    want = gst_video_info_to_caps (&vinfo);
    inter = gst_caps_intersect_full (othercaps, want, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (want);

    if (inter && !gst_caps_is_empty (inter)) {
      inter = gst_caps_make_writable (inter);
      inter = gst_caps_fixate (inter);
      gst_amf_cc_transfer_colorimetry_from_input (trans, caps, inter);
      gst_caps_unref (othercaps);
      GST_DEBUG_OBJECT (trans,
          "fixated src caps toward passthrough %" GST_PTR_FORMAT, inter);
      return inter;
    }
    if (inter)
      gst_caps_unref (inter);
  }

  othercaps = gst_caps_make_writable (othercaps);
  othercaps = gst_caps_fixate (othercaps);

  if (direction == GST_PAD_SINK)
    gst_amf_cc_transfer_colorimetry_from_input (trans, caps, othercaps);

  GST_DEBUG_OBJECT (trans, "fixated src caps (fallback) %" GST_PTR_FORMAT,
      othercaps);

  return othercaps;
}

/* ============================================================
 *   AMF component wiring
 * ============================================================ */

static const wchar_t *
gst_amf_color_convert_get_component_id (GstAmfBaseFilter * self)
{
  return AMFVideoConverter;
}

static AMF_SURFACE_FORMAT
gst_amf_cc_video_format_to_amf (GstVideoFormat fmt)
{
  /* Same table as the base class but specific to this element's
   * advertised pad templates. Kept here so we do not export the base
   * class's internal map. */
  switch (fmt) {
    case GST_VIDEO_FORMAT_NV12:
      return AMF_SURFACE_NV12;
    case GST_VIDEO_FORMAT_P010_10LE:
      return AMF_SURFACE_P010;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      return AMF_SURFACE_BGRA;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      return AMF_SURFACE_RGBA;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      return AMF_SURFACE_YUV420P;
    case GST_VIDEO_FORMAT_YUY2:
      return AMF_SURFACE_YUY2;
    case GST_VIDEO_FORMAT_GRAY8:
      return AMF_SURFACE_GRAY8;
    default:
      break;
  }
  return AMF_SURFACE_UNKNOWN;
}

static gboolean
gst_amf_color_convert_configure_component (GstAmfBaseFilter * filter,
    AMFComponent * comp, const GstVideoInfo * in_info,
    const GstVideoInfo * out_info)
{
  GstAmfColorConvert *self = (GstAmfColorConvert *) filter;
  AMF_SURFACE_FORMAT out_amf_fmt;
  AMFSize out_size;
  AMF_RESULT result;

  out_amf_fmt =
      gst_amf_cc_video_format_to_amf (GST_VIDEO_INFO_FORMAT (out_info));
  if (out_amf_fmt == AMF_SURFACE_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Output format %s is not supported by AMF",
        gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (out_info)));
    return FALSE;
  }

  result = comp->SetProperty (AMF_VIDEO_CONVERTER_OUTPUT_FORMAT,
      (amf_int64) out_amf_fmt);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set output format");

#ifdef G_OS_WIN32
  /* Keep converter output in DX11 memory when we have a D3D11 device.
   * Default is AMF_MEMORY_UNKNOWN (follow input).  AMD's SimpleConverter
   * sample always sets this explicitly; without it AMF may produce surfaces
   * that need Convert(HOST) before the GPU copy path can use them. */
  if (gst_amf_base_filter_get_device (filter)) {
    result = comp->SetProperty (AMF_VIDEO_CONVERTER_MEMORY_TYPE,
        (amf_int64) amf::AMF_MEMORY_DX11);
    if (result != AMF_OK)
      GST_WARNING_OBJECT (self, "Failed to set converter memory type");

    result = comp->SetProperty (AMF_VIDEO_CONVERTER_COMPUTE_DEVICE,
        (amf_int64) amf::AMF_MEMORY_DX11);
    if (result != AMF_OK)
      GST_WARNING_OBJECT (self, "Failed to set converter compute device");
  }
#endif

  out_size.width = GST_VIDEO_INFO_WIDTH (out_info);
  out_size.height = GST_VIDEO_INFO_HEIGHT (out_info);
  result = comp->SetProperty (AMF_VIDEO_CONVERTER_OUTPUT_SIZE, out_size);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set output size");

  result = comp->SetProperty (AMF_VIDEO_CONVERTER_SCALE,
      (amf_int64) self->scale_type);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set scale type");

  {
    const GstVideoColorimetry *in_cinfo = &in_info->colorimetry;
    const GstVideoColorimetry *out_cinfo = &out_info->colorimetry;
    AMF_VIDEO_CONVERTER_COLOR_PROFILE_ENUM color_profile;
    AMF_COLOR_RANGE_ENUM in_range, out_range;

    result =
        comp->SetProperty (AMF_VIDEO_CONVERTER_INPUT_TRANSFER_CHARACTERISTIC,
        (amf_int64) gst_video_transfer_function_to_iso (in_cinfo->transfer));
    if (result != AMF_OK)
      GST_WARNING_OBJECT (self, "Failed to set input transfer");

    result = comp->SetProperty (AMF_VIDEO_CONVERTER_INPUT_COLOR_PRIMARIES,
        (amf_int64) gst_video_color_primaries_to_iso (in_cinfo->primaries));
    if (result != AMF_OK)
      GST_WARNING_OBJECT (self, "Failed to set input primaries");

    in_range = gst_amf_cc_range_from_gst (in_cinfo->range);
    if (in_range != AMF_COLOR_RANGE_UNDEFINED) {
      result = comp->SetProperty (AMF_VIDEO_CONVERTER_INPUT_COLOR_RANGE,
          (amf_int64) in_range);
      if (result != AMF_OK)
        GST_WARNING_OBJECT (self, "Failed to set input color range");
    }

    color_profile = gst_amf_cc_color_profile_from_colorimetry (out_cinfo);
    if (color_profile != AMF_VIDEO_CONVERTER_COLOR_PROFILE_UNKNOWN) {
      result = comp->SetProperty (AMF_VIDEO_CONVERTER_COLOR_PROFILE,
          (amf_int64) color_profile);
      if (result != AMF_OK)
        GST_WARNING_OBJECT (self, "Failed to set color profile");
    }

    out_range = gst_amf_cc_range_from_gst (out_cinfo->range);
    if (out_range != AMF_COLOR_RANGE_UNDEFINED) {
      result = comp->SetProperty (AMF_VIDEO_CONVERTER_OUTPUT_COLOR_RANGE,
          (amf_int64) out_range);
      if (result != AMF_OK)
        GST_WARNING_OBJECT (self, "Failed to set output color range");
    }

    if (self->primaries_mode != GST_VIDEO_PRIMARIES_MODE_NONE) {
      result = comp->SetProperty (AMF_VIDEO_CONVERTER_OUTPUT_COLOR_PRIMARIES,
          (amf_int64) gst_video_color_primaries_to_iso (out_cinfo->primaries));
      if (result != AMF_OK)
        GST_WARNING_OBJECT (self, "Failed to set output primaries");
    }

    if (self->gamma_mode == GST_VIDEO_GAMMA_MODE_REMAP) {
      result =
          comp->SetProperty (AMF_VIDEO_CONVERTER_OUTPUT_TRANSFER_CHARACTERISTIC,
          (amf_int64) gst_video_transfer_function_to_iso (out_cinfo->transfer));
      if (result != AMF_OK)
        GST_WARNING_OBJECT (self, "Failed to set output transfer");
    }
  }

  result = comp->SetProperty (AMF_VIDEO_CONVERTER_KEEP_ASPECT_RATIO,
      (amf_bool) self->keep_aspect_ratio);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set keep-aspect-ratio");

  result = comp->SetProperty (AMF_VIDEO_CONVERTER_FILL, (amf_bool) self->fill);
  if (result != AMF_OK)
    GST_WARNING_OBJECT (self, "Failed to set fill");

  if (self->keep_aspect_ratio && self->fill) {
    AMFColor color;
    color.r = (self->border_color >> 16) & 0xff;
    color.g = (self->border_color >> 8) & 0xff;
    color.b = (self->border_color >> 0) & 0xff;
    color.a = (self->border_color >> 24) & 0xff;
    result = comp->SetProperty (AMF_VIDEO_CONVERTER_FILL_COLOR, color);
    if (result != AMF_OK)
      GST_WARNING_OBJECT (self, "Failed to set border-color");
  }

  return TRUE;
}

/* ============================================================
 *   GType registration
 * ============================================================ */

static GstCaps *
gst_amf_color_convert_build_template_caps (AMFComponent * comp,
    gboolean is_input)
{
  /* Probe what the AMF component actually supports for this
   * direction. The component's IO caps tell us min/max width/height
   * and the supported AMF surface formats; we map the latter back
   * to GstVideoFormat strings.
   *
   * If anything goes wrong we return a generous default so registration
   * does not fail. */
  GstCaps *caps;
  AMFCapsPtr amf_caps;
  AMFIOCapsPtr io_caps;
  AMF_RESULT result;
  amf_int32 min_w = 1, max_w = 8192;
  amf_int32 min_h = 1, max_h = 8192;
  std::string formats;

  result = comp->GetCaps (&amf_caps);
  if (result != AMF_OK)
    goto fallback;

  if (is_input)
    result = amf_caps->GetInputCaps (&io_caps);
  else
    result = amf_caps->GetOutputCaps (&io_caps);
  if (result != AMF_OK)
    goto fallback;

  io_caps->GetWidthRange (&min_w, &max_w);
  io_caps->GetHeightRange (&min_h, &max_h);

  if (max_w <= 0)
    max_w = 8192;
  if (max_h <= 0)
    max_h = 8192;
  if (min_w <= 0)
    min_w = 1;
  if (min_h <= 0)
    min_h = 1;

  {
    amf_int32 num_fmt = io_caps->GetNumOfFormats ();
    gboolean first = TRUE;
    formats = "{ ";
    for (amf_int32 i = 0; i < num_fmt; i++) {
      AMF_SURFACE_FORMAT fmt;
      amf_bool native;
      const char *name = nullptr;

      if (io_caps->GetFormatAt (i, &fmt, &native) != AMF_OK)
        continue;

      switch (fmt) {
        case AMF_SURFACE_NV12:
          name = "NV12";
          break;
        case AMF_SURFACE_P010:
          name = "P010_10LE";
          break;
        case AMF_SURFACE_BGRA:
          name = "BGRA";
          break;
        case AMF_SURFACE_RGBA:
          name = "RGBA";
          break;
        case AMF_SURFACE_YUV420P:
          name = "I420";
          break;
        case AMF_SURFACE_YUY2:
          name = "YUY2";
          break;
        case AMF_SURFACE_GRAY8:
          name = "GRAY8";
          break;
        default:
          break;
      }
      if (!name)
        continue;
      if (!first)
        formats += ", ";
      formats += name;
      first = FALSE;
    }
    if (first) {
      goto fallback;
    }
    formats += " }";
  }

  {
    gchar *caps_str = g_strdup_printf ("video/x-raw, format = %s, "
        "width = (int) [ %d, %d ], height = (int) [ %d, %d ]",
        formats.c_str (), min_w, max_w, min_h, max_h);
    caps = gst_caps_from_string (caps_str);
    g_free (caps_str);
  }
  if (!caps)
    goto fallback;

  {
#ifdef G_OS_WIN32
    GstCaps *d3d11_caps = gst_caps_copy (caps);
    for (guint j = 0; j < gst_caps_get_size (d3d11_caps); j++) {
      gst_caps_set_features (d3d11_caps, j,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
    }
    gst_caps_append (d3d11_caps, caps);
    return d3d11_caps;
#else
    return caps;
#endif
  }

fallback:
  {
    GstCaps *sys_caps =
        gst_caps_from_string ("video/x-raw, format = " GST_AMF_CC_FORMATS
        ", " "width = (int) [ 1, 8192 ], height = (int) [ 1, 8192 ]");
#ifdef G_OS_WIN32
    GstCaps *d3d11_caps = gst_caps_copy (sys_caps);
    for (guint j = 0; j < gst_caps_get_size (d3d11_caps); j++) {
      gst_caps_set_features (d3d11_caps, j,
          gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_D3D11_MEMORY, NULL));
    }
    gst_caps_append (d3d11_caps, sys_caps);
    return d3d11_caps;
#else
    return sys_caps;
#endif
  }
}

void
gst_amf_color_convert_register (GstPlugin * plugin, GstDevice * device,
    gpointer context, guint rank)
{
  AMFContext *amf_context = (AMFContext *) context;
  AMFFactory *factory = (AMFFactory *) gst_amf_get_factory ();
  AMFComponentPtr comp;
  AMF_RESULT result;
  GstAmfColorConvertClassData *cdata;

  GST_DEBUG_CATEGORY_INIT (gst_amf_color_convert_debug, "amfcolorconvert", 0,
      "amfcolorconvert");

  if (!factory)
    return;

  /* Probe the AMFVideoConverter to learn the actual format/size
   * ranges this GPU supports. */
  result = factory->CreateComponent (amf_context, AMFVideoConverter, &comp);
  if (result != AMF_OK) {
    GST_WARNING_OBJECT (device, "Failed to create AMFVideoConverter, result %"
        GST_AMF_RESULT_FORMAT, GST_AMF_RESULT_ARGS (result));
    return;
  }

  cdata = g_new0 (GstAmfColorConvertClassData, 1);
  cdata->sink_caps =
      gst_amf_color_convert_build_template_caps (comp.GetPtr (), TRUE);
  cdata->src_caps =
      gst_amf_color_convert_build_template_caps (comp.GetPtr (), FALSE);
#ifdef G_OS_WIN32
  if (GST_IS_D3D11_DEVICE (device)) {
    GstD3D11Device *d3ddev = GST_D3D11_DEVICE (device);
    g_object_get (d3ddev, "adapter-luid", &cdata->adapter_luid, nullptr);
  }
#endif

  GST_MINI_OBJECT_FLAG_SET (cdata->sink_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (cdata->src_caps,
      GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  GType type;
  gchar *type_name = g_strdup ("GstAmfColorConvert");
  gchar *feature_name = g_strdup ("amfcolorconvert");
  gint index = 0;

  GTypeInfo type_info = {
    sizeof (GstAmfColorConvertClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_amf_color_convert_class_init,
    nullptr,
    cdata,
    sizeof (GstAmfColorConvert),
    0,
    (GInstanceInitFunc) gst_amf_color_convert_init,
  };

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstAmfDevice%dColorConvert", index);
    feature_name = g_strdup_printf ("amfdevice%dcolorconvert", index);
  }

  type = g_type_register_static (GST_TYPE_AMF_BASE_FILTER, type_name,
      &type_info, (GTypeFlags) 0);

  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING_OBJECT (device, "Failed to register element '%s'",
        feature_name);

  g_free (type_name);
  g_free (feature_name);
}
