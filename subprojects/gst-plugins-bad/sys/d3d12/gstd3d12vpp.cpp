/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include <gst/video/video.h>
#include <gst/base/gstbasetransform.h>
#include "gstd3d12vpp.h"
#include "gstd3d12pluginutils.h"
#include <directx/d3dx12.h>
#include <wrl.h>
#include <string.h>
#include <string>
#include <set>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_d3d12_vpp_debug);
#define GST_CAT_DEFAULT gst_d3d12_vpp_debug

struct GstD3D12VppClassData
{
  ~GstD3D12VppClassData ()
  {
    gst_clear_caps (&sink_caps);
    gst_clear_caps (&src_caps);
    g_free (desc);
  }

  GstCaps *sink_caps = nullptr;
  GstCaps *src_caps = nullptr;
  gint64 adapter_luid = 0;
  gchar *desc = nullptr;
};


enum
{
  PROP_ADAPTER_LUID = 1,
  PROP_ADD_BORDERS,
};

#define DEFAULT_ADD_BORDERS TRUE

typedef struct _GstD3D12Vpp
{
  GstBaseTransform parent;

  gint64 luid;
  GstD3D12Device *device;
  GstD3D12VideoProc *vp;
  GstVideoInfo in_info;
  GstVideoInfo out_info;
  GstD3D12FenceDataPool *fence_data_pool;
  GstBufferPool *fallback_pool;

  D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS in_args;
  D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS out_args;

  gboolean add_borders;
} GstD3D12Vpp;

typedef struct _GstD3D12VppClass
{
  GstBaseTransformClass parent_class;
  gint64 adapter_luid;
} GstD3D12VppClass;

static inline GstD3D12Vpp *
GST_D3D12_VPP (gpointer ptr)
{
  return (GstD3D12Vpp *) ptr;
}

static inline GstD3D12VppClass *
GST_D3D12_VPP_GET_CLASS (gpointer ptr)
{
  return G_TYPE_INSTANCE_GET_CLASS (ptr,
      G_TYPE_FROM_INSTANCE (ptr), GstD3D12VppClass);
}

static GstElementClass *parent_class = nullptr;

static void gst_d3d12_vpp_finalize (GObject * object);
static void gst_d3d12_vpp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_d3d12_vpp_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_d3d12_vpp_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_d3d12_vpp_start (GstBaseTransform * trans);
static gboolean gst_d3d12_vpp_stop (GstBaseTransform * trans);
static gboolean gst_d3d12_vpp_query (GstBaseTransform * trans,
    GstPadDirection direction, GstQuery * query);
static GstCaps *gst_d3d12_vpp_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static GstCaps *gst_d3d12_vpp_fixate_caps (GstBaseTransform *
    base, GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_d3d12_vpp_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_d3d12_vpp_decide_allocation (GstBaseTransform *
    trans, GstQuery * query);
static gboolean gst_d3d12_vpp_set_caps (GstBaseTransform * trans,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_d3d12_vpp_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

static void
gst_d3d12_vpp_class_init (GstD3D12VppClass * klass, gpointer data)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  auto cdata = (GstD3D12VppClassData *) data;

  object_class->finalize = gst_d3d12_vpp_finalize;
  object_class->set_property = gst_d3d12_vpp_set_property;
  object_class->get_property = gst_d3d12_vpp_get_property;

  g_object_class_install_property (object_class, PROP_ADAPTER_LUID,
      g_param_spec_int64 ("adapter-luid", "Adapter LUID",
          "DXGI Adapter LUID (Locally Unique Identifier)",
          G_MININT64, G_MAXINT64, 0, (GParamFlags) (GST_PARAM_DOC_SHOW_DEFAULT |
              G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (object_class, PROP_ADD_BORDERS,
      g_param_spec_boolean ("add-borders", "Add Borders",
          "Add black borders if necessary to keep the display aspect ratio",
          DEFAULT_ADD_BORDERS, (GParamFlags) (GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_d3d12_vpp_set_context);

  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  std::string long_name = "Direct3D12 " + std::string (cdata->desc) + " VPP";

  gst_element_class_set_metadata (element_class, long_name.c_str (),
      "Filter/Converter/Scaler/Video/Hardware",
      "Performs resizing and colorspace conversion using Direct3D12 "
      "Video Processor", "Seungha Yang <seungha@centricular.com>");

  auto pad_templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
      cdata->sink_caps);
  gst_element_class_add_pad_template (element_class, pad_templ);
  pad_templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      cdata->src_caps);
  gst_element_class_add_pad_template (element_class, pad_templ);

  trans_class->passthrough_on_same_caps = TRUE;

  trans_class->start = GST_DEBUG_FUNCPTR (gst_d3d12_vpp_start);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_d3d12_vpp_stop);
  trans_class->query = GST_DEBUG_FUNCPTR (gst_d3d12_vpp_query);
  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_d3d12_vpp_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_d3d12_vpp_fixate_caps);
  trans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_vpp_propose_allocation);
  trans_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_d3d12_vpp_decide_allocation);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_d3d12_vpp_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_d3d12_vpp_transform);

  klass->adapter_luid = cdata->adapter_luid;
  delete cdata;
}

static void
gst_d3d12_vpp_init (GstD3D12Vpp * self)
{
  auto klass = GST_D3D12_VPP_GET_CLASS (self);

  self->fence_data_pool = gst_d3d12_fence_data_pool_new ();
  self->add_borders = DEFAULT_ADD_BORDERS;
  self->luid = klass->adapter_luid;
}

static void
gst_d3d12_vpp_finalize (GObject * object)
{
  auto self = GST_D3D12_VPP (object);

  gst_clear_object (&self->vp);
  gst_clear_object (&self->device);
  gst_clear_object (&self->fence_data_pool);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_d3d12_vpp_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  auto self = GST_D3D12_VPP (object);

  switch (prop_id) {
    case PROP_ADD_BORDERS:
      self->add_borders = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_vpp_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  auto self = GST_D3D12_VPP (object);

  switch (prop_id) {
    case PROP_ADAPTER_LUID:
      g_value_set_int64 (value, self->luid);
      break;
    case PROP_ADD_BORDERS:
      g_value_set_boolean (value, self->add_borders);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_d3d12_vpp_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_D3D12_VPP (element);

  gst_d3d12_handle_set_context_for_adapter_luid (element,
      context, self->luid, &self->device);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_d3d12_vpp_start (GstBaseTransform * trans)
{
  auto self = GST_D3D12_VPP (trans);

  if (!gst_d3d12_ensure_element_data_for_adapter_luid (GST_ELEMENT_CAST (self),
          self->luid, &self->device)) {
    GST_ERROR_OBJECT (self, "Failed to get D3D12 device");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_d3d12_vpp_stop (GstBaseTransform * trans)
{
  auto self = GST_D3D12_VPP (trans);

  if (self->fallback_pool) {
    gst_buffer_pool_set_active (self->fallback_pool, FALSE);
    gst_clear_object (&self->fallback_pool);
  }

  gst_clear_object (&self->vp);
  gst_clear_object (&self->device);

  return TRUE;
}

static gboolean
gst_d3d12_vpp_query (GstBaseTransform * trans, GstPadDirection direction,
    GstQuery * query)
{
  auto self = GST_D3D12_VPP (trans);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_d3d12_handle_context_query (GST_ELEMENT (self), query,
              self->device)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (trans, direction,
      query);
}

static GstCaps *
gst_d3d12_vpp_transform_caps (GstBaseTransform *
    trans, GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  auto ret = gst_d3d12_caps_remove_format_and_rangify_size_info (caps);

  if (filter) {
    auto tmp = gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = tmp;
  }

  GST_DEBUG_OBJECT (trans, "transformed %" GST_PTR_FORMAT " into %"
      GST_PTR_FORMAT, caps, ret);

  return ret;
}

static void
gst_d3d12_vpp_fixate_format (GstBaseTransform * trans, GstCaps * caps,
    GstCaps * result)
{
  GstStructure *ins, *outs;
  const gchar *in_format;
  const GstVideoFormatInfo *in_info, *out_info = nullptr;
  gint min_loss = G_MAXINT;
  guint i, capslen;

  ins = gst_caps_get_structure (caps, 0);
  in_format = gst_structure_get_string (ins, "format");
  if (!in_format) {
    return;
  }

  GST_DEBUG_OBJECT (trans, "source format %s", in_format);

  in_info =
      gst_video_format_get_info (gst_video_format_from_string (in_format));
  if (!in_info)
    return;

  outs = gst_caps_get_structure (result, 0);

  capslen = gst_caps_get_size (result);
  GST_DEBUG ("iterate %d structures", capslen);
  for (i = 0; i < capslen; i++) {
    GstStructure *tests;
    const GValue *format;

    tests = gst_caps_get_structure (result, i);
    format = gst_structure_get_value (tests, "format");

    /* should not happen */
    if (format == nullptr)
      continue;

    if (GST_VALUE_HOLDS_LIST (format)) {
      gint j, len;

      len = gst_value_list_get_size (format);
      GST_DEBUG_OBJECT (trans, "have %d formats", len);
      for (j = 0; j < len; j++) {
        const GValue *val;

        val = gst_value_list_get_value (format, j);
        if (G_VALUE_HOLDS_STRING (val)) {
          gst_d3d12_video_format_score_value (in_info,
              val, &min_loss, &out_info);
          if (min_loss == 0)
            break;
        }
      }
    } else if (G_VALUE_HOLDS_STRING (format)) {
      gst_d3d12_video_format_score_value (in_info,
          format, &min_loss, &out_info);
    }
  }
  if (out_info)
    gst_structure_set (outs, "format", G_TYPE_STRING,
        GST_VIDEO_FORMAT_INFO_NAME (out_info), nullptr);
}

static gboolean
subsampling_unchanged (GstVideoInfo * in_info, GstVideoInfo * out_info)
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
transfer_colorimetry_from_input (GstBaseTransform * trans, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstStructure *out_caps_s = gst_caps_get_structure (out_caps, 0);
  GstStructure *in_caps_s = gst_caps_get_structure (in_caps, 0);
  gboolean have_colorimetry =
      gst_structure_has_field (out_caps_s, "colorimetry");
  gboolean have_chroma_site =
      gst_structure_has_field (out_caps_s, "chroma-site");

  /* If the output already has colorimetry and chroma-site, stop,
   * otherwise try and transfer what we can from the input caps */
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

    if (!have_colorimetry && in_colorimetry != nullptr) {
      if ((GST_VIDEO_INFO_IS_YUV (&out_info)
              && GST_VIDEO_INFO_IS_YUV (&in_info))
          || (GST_VIDEO_INFO_IS_RGB (&out_info)
              && GST_VIDEO_INFO_IS_RGB (&in_info))
          || (GST_VIDEO_INFO_IS_GRAY (&out_info)
              && GST_VIDEO_INFO_IS_GRAY (&in_info))) {
        /* Can transfer the colorimetry intact from the input if it has it */
        gst_structure_set_value (out_caps_s, "colorimetry", in_colorimetry);
      } else {
        gchar *colorimetry_str;

        /* Changing between YUV/RGB - forward primaries and transfer function, but use
         * default range and matrix.
         * the primaries is used for conversion between RGB and XYZ (CIE 1931 coordinate).
         * the transfer function could be another reference (e.g., HDR)
         */
        out_info.colorimetry.primaries = in_info.colorimetry.primaries;
        out_info.colorimetry.transfer = in_info.colorimetry.transfer;

        colorimetry_str =
            gst_video_colorimetry_to_string (&out_info.colorimetry);
        gst_caps_set_simple (out_caps, "colorimetry", G_TYPE_STRING,
            colorimetry_str, nullptr);
        g_free (colorimetry_str);
      }
    }

    /* Only YUV output needs chroma-site. If the input was also YUV and had the same chroma
     * subsampling, transfer the siting. If the sub-sampling is changing, then the planes get
     * scaled anyway so there's no real reason to prefer the input siting. */
    if (!have_chroma_site && GST_VIDEO_INFO_IS_YUV (&out_info)) {
      if (GST_VIDEO_INFO_IS_YUV (&in_info)) {
        const GValue *in_chroma_site =
            gst_structure_get_value (in_caps_s, "chroma-site");
        if (in_chroma_site != nullptr
            && subsampling_unchanged (&in_info, &out_info))
          gst_structure_set_value (out_caps_s, "chroma-site", in_chroma_site);
      }
    }
  }
}

static GstCaps *
gst_d3d12_vpp_get_fixed_format (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstCaps *result;

  result = gst_caps_intersect (othercaps, caps);
  if (gst_caps_is_empty (result)) {
    gst_caps_unref (result);
    result = gst_caps_copy (othercaps);
  }

  gst_d3d12_vpp_fixate_format (trans, caps, result);

  /* fixate remaining fields */
  result = gst_caps_fixate (result);

  if (direction == GST_PAD_SINK) {
    if (gst_caps_is_subset (caps, result)) {
      gst_caps_replace (&result, caps);
    } else {
      /* Try and preserve input colorimetry / chroma information */
      transfer_colorimetry_from_input (trans, caps, result);
    }
  }

  return result;
}

static GstCaps *
gst_d3d12_vpp_fixate_size (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  auto self = GST_D3D12_VPP (base);
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = G_VALUE_INIT, tpar = G_VALUE_INIT;
  gboolean rotate = FALSE;

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);
  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION_RANGE);
      gst_value_set_fraction_range_full (&tpar, 1, G_MAXINT, G_MAXINT, 1);
      to_par = &tpar;
    }
  } else {
    gint from_par_n, from_par_d;

    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;

      from_par_n = from_par_d = 1;
    } else {
      from_par_n = gst_value_get_fraction_numerator (from_par);
      from_par_d = gst_value_get_fraction_denominator (from_par);
    }

    if (!to_par) {
      gint to_par_n, to_par_d;

      if (rotate) {
        to_par_n = from_par_d;
        to_par_d = from_par_n;
      } else {
        to_par_n = from_par_n;
        to_par_d = from_par_d;
      }

      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, to_par_n, to_par_d);
      to_par = &tpar;

      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          to_par_n, to_par_d, nullptr);
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail (gst_value_is_fixed (from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);

    gst_structure_get_int (outs, "width", &w);
    gst_structure_get_int (outs, "height", &h);

    /* swap dimensions when it's rotated */
    if (rotate) {
      gint _tmp = from_w;
      from_w = from_h;
      from_h = _tmp;

      _tmp = from_par_n;
      from_par_n = from_par_d;
      from_par_d = _tmp;
    }

    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      guint n, d;

      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        if (gst_video_calculate_display_ratio (&n, &d, from_w, from_h,
                from_par_n, from_par_d, w, h)) {
          GST_DEBUG_OBJECT (base, "fixating to_par to %dx%d", n, d);
          if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
            gst_structure_fixate_field_nearest_fraction (outs,
                "pixel-aspect-ratio", n, d);
          else if (n != d)
            gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
                n, d, nullptr);
        }
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT (base, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      GstStructure *tmp;
      gint set_w, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "height is fixed (%d)", h);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the width that is nearest to the
       * width with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        w = (guint) gst_util_uint64_scale_int_round (h, num, den);
        gst_structure_fixate_field_nearest_int (outs, "width", w);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input width */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "width", G_TYPE_INT, set_w,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              nullptr);
        goto done;
      }

      /* Otherwise scale the width to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, nullptr);

      goto done;
    } else if (w) {
      GstStructure *tmp;
      gint set_h, set_par_n, set_par_d;

      GST_DEBUG_OBJECT (base, "width is fixed (%d)", w);

      /* If the PAR is fixed too, there's not much to do
       * except choosing the height that is nearest to the
       * height with the same DAR */
      if (gst_value_is_fixed (to_par)) {
        to_par_n = gst_value_get_fraction_numerator (to_par);
        to_par_d = gst_value_get_fraction_denominator (to_par);

        GST_DEBUG_OBJECT (base, "PAR is fixed %d/%d", to_par_n, to_par_d);

        if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
                to_par_n, &num, &den)) {
          GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
              ("Error calculating the output scaled size - integer overflow"));
          goto done;
        }

        h = (guint) gst_util_uint64_scale_int_round (w, den, num);
        gst_structure_fixate_field_nearest_int (outs, "height", h);

        goto done;
      }

      /* The PAR is not fixed and it's quite likely that we can set
       * an arbitrary PAR. */

      /* Check if we can keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* Might have failed but try to keep the DAR nonetheless by
       * adjusting the PAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }
      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      /* Check if the adjusted PAR is accepted */
      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "height", G_TYPE_INT, set_h,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, set_par_n, set_par_d,
              nullptr);
        goto done;
      }

      /* Otherwise scale the height to the new PAR and check if the
       * adjusted with is accepted. If all that fails we can't keep
       * the DAR */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scale sized - integer overflow"));
        goto done;
      }

      h = (guint) gst_util_uint64_scale_int_round (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, nullptr);

      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height (because of interlacing) */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, nullptr);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, nullptr);
        goto done;
      }

      /* If all this failed, keep the dimensions with the DAR that was closest
       * to the correct DAR. This changes the DAR but there's not much else to
       * do here.
       */
      if (set_w * ABS (set_h - h) < ABS (f_w - w) * f_h) {
        f_h = set_h;
        f_w = set_w;
      }
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, nullptr);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed but passthrough is not possible */

      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);

      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free (tmp);
        goto done;
      }

      if (!gst_structure_has_field (tmp, "pixel-aspect-ratio"))
        gst_structure_set_value (tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction (tmp, "pixel-aspect-ratio",
          to_par_n, to_par_d);
      gst_structure_get_fraction (tmp, "pixel-aspect-ratio", &set_par_n,
          &set_par_d);
      gst_structure_free (tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, nullptr);

        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, nullptr);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (base, CORE, NEGOTIATION, (nullptr),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint) gst_util_uint64_scale_int_round (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, nullptr);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, nullptr);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int_round (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);

      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, nullptr);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, nullptr);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, nullptr);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, nullptr);
    }
  }

done:
  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);

  return othercaps;
}

static GstCaps *
gst_d3d12_vpp_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GST_DEBUG_OBJECT (trans,
      "trying to fixate othercaps %" GST_PTR_FORMAT " based on caps %"
      GST_PTR_FORMAT, othercaps, caps);

  auto format = gst_d3d12_vpp_get_fixed_format (trans, direction, caps,
      othercaps);

  if (gst_caps_is_empty (format)) {
    GST_ERROR_OBJECT (trans, "Could not convert formats");
    return format;
  }

  /* convert mode is "all" or "size" here */
  othercaps = gst_d3d12_vpp_fixate_size (trans, direction, caps, othercaps);

  if (gst_caps_get_size (othercaps) == 1) {
    guint i;
    const gchar *format_fields[] = { "format", "colorimetry", "chroma-site" };
    GstStructure *format_struct = gst_caps_get_structure (format, 0);
    GstStructure *fixated_struct;

    othercaps = gst_caps_make_writable (othercaps);
    fixated_struct = gst_caps_get_structure (othercaps, 0);

    for (i = 0; i < G_N_ELEMENTS (format_fields); i++) {
      if (gst_structure_has_field (format_struct, format_fields[i])) {
        gst_structure_set (fixated_struct, format_fields[i], G_TYPE_STRING,
            gst_structure_get_string (format_struct, format_fields[i]),
            nullptr);
      } else {
        gst_structure_remove_field (fixated_struct, format_fields[i]);
      }
    }
  }
  gst_caps_unref (format);

  GST_DEBUG_OBJECT (trans, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static gboolean
gst_d3d12_vpp_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  auto filter = GST_D3D12_VPP (trans);
  auto self = GST_D3D12_VPP (trans);
  GstVideoInfo info;
  GstBufferPool *pool = nullptr;
  GstCaps *caps;
  guint n_pools, i;
  guint size;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (trans,
          decide_query, query)) {
    return FALSE;
  }

  /* passthrough, we're done */
  if (!decide_query)
    return TRUE;

  gst_query_parse_allocation (query, &caps, nullptr);
  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (filter, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  n_pools = gst_query_get_n_allocation_pools (query);
  for (i = 0; i < n_pools; i++) {
    gst_query_parse_nth_allocation_pool (query, i, &pool, nullptr, nullptr,
        nullptr);
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, filter->device))
          gst_clear_object (&pool);
      }
    }
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (filter->device);

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  auto d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!d3d12_params) {
    d3d12_params = gst_d3d12_allocation_params_new (filter->device, &info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, D3D12_HEAP_FLAG_NONE);
  } else {
    gst_d3d12_allocation_params_set_resource_flags (d3d12_params,
        D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);
    gst_d3d12_allocation_params_unset_resource_flags (d3d12_params,
        D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE);
  }

  gst_buffer_pool_config_set_d3d12_allocation_params (config, d3d12_params);
  gst_d3d12_allocation_params_free (d3d12_params);

  /* size will be updated by d3d12 buffer pool */
  gst_buffer_pool_config_set_params (config, caps, info.size, 0, 0);

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (filter, "failed to set config");
    gst_object_unref (pool);
    return FALSE;
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, nullptr);

  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  gst_query_add_allocation_pool (query, pool, size, 0, 0);

  gst_object_unref (pool);

  return TRUE;
}

static gboolean
gst_d3d12_vpp_decide_allocation (GstBaseTransform * trans, GstQuery * query)
{
  auto self = GST_D3D12_VPP (trans);
  GstCaps *outcaps = nullptr;
  GstBufferPool *pool = nullptr;
  guint size, min = 0, max = 0;
  GstStructure *config;
  gboolean update_pool = FALSE;
  GstVideoInfo info;

  gst_query_parse_allocation (query, &outcaps, nullptr);

  if (!outcaps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, outcaps)) {
    GST_ERROR_OBJECT (self, "Invalid caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  GstD3D12Format device_format;
  if (!gst_d3d12_device_get_format (self->device,
          GST_VIDEO_INFO_FORMAT (&info), &device_format)) {
    GST_ERROR_OBJECT (self, "Couldn't get device foramt");
    return FALSE;
  }

  size = GST_VIDEO_INFO_SIZE (&info);
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    if (pool) {
      if (!GST_IS_D3D12_BUFFER_POOL (pool)) {
        gst_clear_object (&pool);
      } else {
        auto dpool = GST_D3D12_BUFFER_POOL (pool);
        if (!gst_d3d12_device_is_equal (dpool->device, self->device))
          gst_clear_object (&pool);
      }
    }

    update_pool = TRUE;
  }

  if (!pool)
    pool = gst_d3d12_buffer_pool_new (self->device);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  D3D12_RESOURCE_FLAGS resource_flags =
      D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  if ((device_format.format_flags & GST_D3D12_FORMAT_FLAG_OUTPUT_UAV)
      == GST_D3D12_FORMAT_FLAG_OUTPUT_UAV) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  if ((device_format.support1 & D3D12_FORMAT_SUPPORT1_RENDER_TARGET) ==
      D3D12_FORMAT_SUPPORT1_RENDER_TARGET) {
    resource_flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  }

  auto d3d12_params =
      gst_buffer_pool_config_get_d3d12_allocation_params (config);
  if (!d3d12_params) {
    d3d12_params = gst_d3d12_allocation_params_new (self->device, &info,
        GST_D3D12_ALLOCATION_FLAG_DEFAULT, resource_flags,
        D3D12_HEAP_FLAG_SHARED);
  } else {
    gst_d3d12_allocation_params_set_resource_flags (d3d12_params,
        resource_flags);
  }

  gst_buffer_pool_config_set_d3d12_allocation_params (config, d3d12_params);
  gst_d3d12_allocation_params_free (d3d12_params);

  gst_buffer_pool_config_set_params (config, outcaps, size, min, max);
  gst_buffer_pool_set_config (pool, config);

  /* d3d12 buffer pool will update buffer size based on allocated texture,
   * get size from config again */
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr, nullptr);
  gst_structure_free (config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (trans,
      query);
}

static void
gst_d3d12_vpp_do_align (DXGI_FORMAT format, guint * width, guint * height)
{
  guint w = *width;
  guint h = *height;

  UINT width_align =
      D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetWidthAlignment (format);
  UINT height_align =
      D3D12_PROPERTY_LAYOUT_FORMAT_TABLE::GetHeightAlignment (format);

  if (width_align > 1)
    *width = GST_ROUND_UP_N (w, width_align);

  if (height_align > 1)
    *height = GST_ROUND_UP_N (h, width_align);
}

static gboolean
gst_d3d12_vpp_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  auto self = GST_D3D12_VPP (trans);
  gint from_dar_n, from_dar_d, to_dar_n, to_dar_d;
  gint border_offset_x = 0;
  gint border_offset_y = 0;
  gboolean need_flip = FALSE;
  gint in_width, in_height, in_par_n, in_par_d;
  gint out_width, out_height;
  gint borders_w, borders_h;
  GstStructure *config;

  if (!gst_video_info_from_caps (&self->in_info, incaps)) {
    GST_ERROR_OBJECT (self, "Invalid input caps %" GST_PTR_FORMAT, incaps);
    return FALSE;
  }

  if (!gst_video_info_from_caps (&self->out_info, outcaps)) {
    GST_ERROR_OBJECT (self, "Invalid output caps %" GST_PTR_FORMAT, outcaps);
    return FALSE;
  }

  gst_clear_object (&self->vp);
  if (self->fallback_pool) {
    gst_buffer_pool_set_active (self->fallback_pool, FALSE);
    gst_clear_object (&self->fallback_pool);
  }

  auto in_info = &self->in_info;
  auto out_info = &self->out_info;
  in_width = in_info->width;
  in_height = in_info->height;
  in_par_n = in_info->par_n;
  in_par_d = in_info->par_d;

  if (!gst_util_fraction_multiply (in_width,
          in_height, in_par_n, in_par_d, &from_dar_n, &from_dar_d)) {
    from_dar_n = from_dar_d = -1;
  }

  if (!gst_util_fraction_multiply (out_info->width,
          out_info->height, out_info->par_n, out_info->par_d, &to_dar_n,
          &to_dar_d)) {
    to_dar_n = to_dar_d = -1;
  }

  borders_w = borders_h = 0;
  if (to_dar_n != from_dar_n || to_dar_d != from_dar_d) {
    if (self->add_borders) {
      gint n, d, to_h, to_w;

      if (from_dar_n != -1 && from_dar_d != -1
          && gst_util_fraction_multiply (from_dar_n, from_dar_d,
              out_info->par_d, out_info->par_n, &n, &d)) {
        to_h = gst_util_uint64_scale_int (out_info->width, d, n);
        if (to_h <= out_info->height) {
          borders_h = out_info->height - to_h;
          borders_w = 0;
        } else {
          to_w = gst_util_uint64_scale_int (out_info->height, n, d);
          g_assert (to_w <= out_info->width);
          borders_h = 0;
          borders_w = out_info->width - to_w;
        }
      } else {
        GST_WARNING_OBJECT (self, "Can't calculate borders");
      }
    } else {
      GST_INFO_OBJECT (self, "Display aspect ratio update %d/%d -> %d/%d",
          from_dar_n, from_dar_d, to_dar_n, to_dar_d);
    }
  }

  auto in_format = GST_VIDEO_INFO_FORMAT (in_info);
  auto out_format = GST_VIDEO_INFO_FORMAT (out_info);

  GST_DEBUG_OBJECT (self, "Setup convert with format %s -> %s",
      gst_video_format_to_string (in_format),
      gst_video_format_to_string (out_format));

  /* if present, these must match */
  if (in_info->interlace_mode != out_info->interlace_mode) {
    GST_ERROR_OBJECT (self, "input and output formats do not match");
    return FALSE;
  }

  if (in_width == out_info->width && in_height == out_info->height
      && in_info->finfo == out_info->finfo && borders_w == 0 &&
      borders_h == 0 && !need_flip) {
    gst_base_transform_set_passthrough (trans, TRUE);
  } else {
    gst_base_transform_set_passthrough (trans, FALSE);

    GstD3D12Format in_dev_format;
    GstD3D12Format out_dev_format;
    DXGI_COLOR_SPACE_TYPE in_space;
    DXGI_COLOR_SPACE_TYPE out_space;

    gst_d3d12_device_get_format (self->device, in_format, &in_dev_format);
    gst_d3d12_device_get_format (self->device, out_format, &out_dev_format);
    gst_d3d12_dxgi_color_space_from_video_info (in_info, &in_space);
    gst_d3d12_dxgi_color_space_from_video_info (out_info, &out_space);

    auto in_dxgi_format = in_dev_format.dxgi_format;
    auto out_dxgi_format = out_dev_format.dxgi_format;

    guint in_aligned_width = in_info->width;
    guint in_aligned_height = in_info->height;
    guint out_aligned_width = out_info->width;
    guint out_aligned_height = out_info->height;

    gst_d3d12_vpp_do_align (in_dxgi_format,
        &in_aligned_width, &in_aligned_height);
    gst_d3d12_vpp_do_align (out_dxgi_format,
        &out_aligned_width, &out_aligned_height);

    auto device = gst_d3d12_device_get_device_handle (self->device);
    ComPtr < ID3D12VideoDevice > video_device;
    device->QueryInterface (IID_PPV_ARGS (&video_device));

    D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT vp_support = { };
    vp_support.InputSample.Width = in_aligned_width;
    vp_support.InputSample.Height = in_aligned_height;
    vp_support.InputSample.Format.Format = in_dxgi_format;
    vp_support.InputSample.Format.ColorSpace = in_space;
    vp_support.InputFieldType = D3D12_VIDEO_FIELD_TYPE_NONE;
    vp_support.InputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;
    vp_support.InputFrameRate.Numerator = 30;
    vp_support.InputFrameRate.Denominator = 1;
    vp_support.OutputFormat.Format = out_dxgi_format;
    vp_support.OutputFormat.ColorSpace = out_space;
    vp_support.OutputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;
    vp_support.OutputFrameRate = vp_support.InputFrameRate;

    auto hr = video_device->CheckFeatureSupport
        (D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &vp_support,
        sizeof (vp_support));
    /* Try other colorspace if current color space is not supported */
    if (FAILED (hr)
        || (vp_support.SupportFlags &
            D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)
        != D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) {
      GST_DEBUG_OBJECT (self, "Current configuration is not supported");
      bool try_again = false;
      if (GST_VIDEO_INFO_IS_RGB (in_info) &&
          in_space != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
        in_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        try_again = true;
      } else if (GST_VIDEO_INFO_IS_YUV (in_info) &&
          in_space != DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709) {
        in_space = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
        try_again = true;
      }

      if (try_again) {
        vp_support.InputSample.Format.ColorSpace = in_space;
        GST_DEBUG_OBJECT (self,
            "Checking support with updated input color space");
        hr = video_device->CheckFeatureSupport
            (D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &vp_support,
            sizeof (vp_support));
      }
    }

    if (FAILED (hr)
        || (vp_support.SupportFlags &
            D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)
        != D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) {
      bool try_again = false;
      if (GST_VIDEO_INFO_IS_RGB (out_info) &&
          out_space != DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709) {
        out_space = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        try_again = true;
      } else if (GST_VIDEO_INFO_IS_YUV (out_info) &&
          out_space != DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709) {
        out_space = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
        try_again = true;
      }

      if (try_again) {
        vp_support.OutputFormat.ColorSpace = out_space;
        GST_DEBUG_OBJECT (self,
            "Checking support with updated output color space");
        hr = video_device->CheckFeatureSupport
            (D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &vp_support,
            sizeof (vp_support));
      }
    }

    if (FAILED (hr)
        || (vp_support.SupportFlags &
            D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)
        != D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) {
      GST_ERROR_OBJECT (self, "Conversion not supported");
      return FALSE;
    }

    guint border_offset_x = borders_w / 2;
    guint border_offset_y = borders_h / 2;
    gst_d3d12_vpp_do_align (out_dxgi_format,
        &border_offset_x, &border_offset_y);
    D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS in_args = { };
    in_args.Transform.SourceRectangle.left = 0;
    in_args.Transform.SourceRectangle.top = 0;
    in_args.Transform.SourceRectangle.right = in_aligned_width;
    in_args.Transform.SourceRectangle.bottom = in_aligned_height;
    in_args.Transform.DestinationRectangle.left = border_offset_x;
    in_args.Transform.DestinationRectangle.top = border_offset_y;
    in_args.Transform.DestinationRectangle.right =
        out_aligned_width - border_offset_x;
    in_args.Transform.DestinationRectangle.bottom =
        out_aligned_height - border_offset_y;
    in_args.Transform.Orientation = D3D12_VIDEO_PROCESS_ORIENTATION_DEFAULT;
    in_args.Flags = D3D12_VIDEO_PROCESS_INPUT_STREAM_FLAG_NONE;
    in_args.RateInfo.OutputIndex = 0;
    in_args.RateInfo.InputFrameOrField = 0;
    in_args.AlphaBlending.Enable = FALSE;
    in_args.AlphaBlending.Alpha = 1.0;

    self->in_args = in_args;

    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS out_args = { };
    out_args.TargetRectangle.left = 0;
    out_args.TargetRectangle.top = 0;
    out_args.TargetRectangle.right = out_aligned_width;
    out_args.TargetRectangle.bottom = out_aligned_height;

    self->out_args = out_args;

    auto min_width = in_args.Transform.DestinationRectangle.right -
        in_args.Transform.DestinationRectangle.left;
    auto min_height = in_args.Transform.DestinationRectangle.bottom -
        in_args.Transform.DestinationRectangle.top;

    D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC in_desc = { };
    D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC out_desc = { };

    in_desc.Format = in_dev_format.dxgi_format;
    in_desc.ColorSpace = in_space;
    in_desc.SourceAspectRatio.Numerator = 1;
    in_desc.SourceAspectRatio.Denominator = 1;
    in_desc.DestinationAspectRatio = in_desc.SourceAspectRatio;
    in_desc.FrameRate.Numerator = 30;
    in_desc.FrameRate.Denominator = 1;
    in_desc.SourceSizeRange.MinWidth = in_aligned_width;
    in_desc.SourceSizeRange.MinHeight = in_aligned_height;
    in_desc.SourceSizeRange.MaxWidth = in_aligned_width;
    in_desc.SourceSizeRange.MaxHeight = in_aligned_height;
    in_desc.DestinationSizeRange.MinWidth = min_width;
    in_desc.DestinationSizeRange.MinHeight = min_height;
    in_desc.DestinationSizeRange.MaxWidth = out_aligned_width;
    in_desc.DestinationSizeRange.MaxHeight = out_aligned_height;
    in_desc.EnableOrientation = FALSE;
    in_desc.FilterFlags = D3D12_VIDEO_PROCESS_FILTER_FLAG_NONE;
    in_desc.StereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;
    in_desc.FieldType = D3D12_VIDEO_FIELD_TYPE_NONE;
    in_desc.DeinterlaceMode = D3D12_VIDEO_PROCESS_DEINTERLACE_FLAG_NONE;
    in_desc.EnableAlphaBlending = FALSE;
    in_desc.LumaKey = { };
    in_desc.NumPastFrames = 0;
    in_desc.NumFutureFrames = 0;
    in_desc.EnableAutoProcessing = FALSE;

    out_desc.Format = out_dev_format.dxgi_format;
    out_desc.ColorSpace = out_space;
    out_desc.AlphaFillMode = D3D12_VIDEO_PROCESS_ALPHA_FILL_MODE_OPAQUE;
    out_desc.AlphaFillModeSourceStreamIndex = 0;
    if (GST_VIDEO_INFO_IS_RGB (out_info)) {
      out_desc.BackgroundColor[0] = 0;
      out_desc.BackgroundColor[1] = 0;
      out_desc.BackgroundColor[2] = 0;
      out_desc.BackgroundColor[3] = 1;
    } else {
      out_desc.BackgroundColor[0] = 0;
      out_desc.BackgroundColor[1] = 0.5;
      out_desc.BackgroundColor[2] = 0.5;
      out_desc.BackgroundColor[3] = 1;
    }
    out_desc.FrameRate.Numerator = 30;
    out_desc.FrameRate.Denominator = 1;
    out_desc.EnableStereo = FALSE;

    self->vp = gst_d3d12_video_proc_new (self->device,
        1, &in_desc, &out_desc, nullptr);
    if (!self->vp) {
      GST_ERROR_OBJECT (self, "Couldn't create video processor");
      return FALSE;
    }
  }

  return TRUE;
}

static GstBuffer *
gst_d3d12_vpp_upload (GstD3D12Vpp * self, GstBuffer * buf)
{
  auto mem = gst_buffer_peek_memory (buf, 0);
  if (gst_is_d3d12_memory (mem)) {
    auto cmem = GST_D3D12_MEMORY_CAST (mem);
    if (gst_d3d12_device_is_equal (cmem->device, self->device))
      return gst_buffer_ref (buf);
  }

  if (!self->fallback_pool) {
    auto pool = gst_d3d12_buffer_pool_new (self->device);
    auto caps = gst_video_info_to_caps (&self->in_info);
    auto config = gst_buffer_pool_get_config (pool);

    gst_buffer_pool_config_set_params (config, caps, self->in_info.size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (self, "failed to set config");
      gst_object_unref (pool);
      return nullptr;
    }

    if (!gst_buffer_pool_set_active (pool, TRUE)) {
      GST_ERROR_OBJECT (self, "Couldn't set active");
      gst_object_unref (pool);
      return nullptr;
    }

    self->fallback_pool = pool;
  }

  GstBuffer *outbuf = nullptr;
  gst_buffer_pool_acquire_buffer (self->fallback_pool, &outbuf, nullptr);
  if (!outbuf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
    return nullptr;
  }

  GstVideoFrame in_frame, out_frame;

  if (!gst_video_frame_map (&in_frame, &self->in_info, buf, GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Couldn't map input buffer");
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  if (!gst_video_frame_map (&out_frame, &self->in_info, outbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (self, "Couldn't map output buffer");
    gst_video_frame_unmap (&in_frame);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  auto ret = gst_video_frame_copy (&out_frame, &in_frame);
  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!ret) {
    GST_ERROR_OBJECT (self, "Couldn't copy frame");
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  return outbuf;
}

static GstFlowReturn
gst_d3d12_vpp_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  auto self = GST_D3D12_VPP (trans);

  auto uploaded = gst_d3d12_vpp_upload (self, inbuf);
  if (!uploaded)
    return GST_FLOW_ERROR;

  GstMapInfo in_map, out_map;
  auto in_mem = gst_buffer_peek_memory (uploaded, 0);
  auto out_mem = gst_buffer_peek_memory (outbuf, 0);
  auto in_cmem = GST_D3D12_MEMORY_CAST (in_mem);
  auto out_cmem = GST_D3D12_MEMORY_CAST (out_mem);

  ComPtr < ID3D12Fence > fence;
  guint64 in_fence_val = 0;

  gst_memory_map (in_mem, &in_map, GST_MAP_READ_D3D12);
  gst_memory_map (out_mem, &out_map, GST_MAP_WRITE_D3D12);

  gst_d3d12_memory_get_fence (in_cmem, &fence, &in_fence_val);

  self->in_args.InputStream->pTexture2D = (ID3D12Resource *) in_map.data;
  self->out_args.OutputStream->pTexture2D = (ID3D12Resource *) out_map.data;

  GstD3D12FenceData *fence_data;
  gst_d3d12_fence_data_pool_acquire (self->fence_data_pool, &fence_data);

  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (uploaded));

  guint64 out_fence_val = 0;
  ID3D12Fence *in_fences[] = { fence.Get () };
  ComPtr < ID3D12Fence > out_fence;
  auto ret = gst_d3d12_video_proc_execute (self->vp,
      fence ? 1 : 0, in_fences, &in_fence_val, 1, &self->in_args,
      &self->out_args, fence_data, &out_fence, &out_fence_val);
  gst_memory_unmap (out_mem, &out_map);
  gst_memory_unmap (in_mem, &in_map);
  if (!ret) {
    GST_ERROR_OBJECT (self, "Couldn't convert frame");
    return GST_FLOW_ERROR;
  }

  gst_d3d12_memory_set_fence (out_cmem, out_fence.Get (), out_fence_val, FALSE);

  return GST_FLOW_OK;
}

/* *INDENT-OFF* */
struct VPPTestFormats
{
  DXGI_FORMAT dxgi_format;
  bool is_yuv;
  std::string format;
};

static const VPPTestFormats test_formats[] = {
  { DXGI_FORMAT_AYUV, true, "VUYA" },
  { DXGI_FORMAT_Y410, true, "Y410" },
  { DXGI_FORMAT_Y416, true, "Y412" },
  { DXGI_FORMAT_Y416, true, "Y416" },
  { DXGI_FORMAT_NV12, true, "NV12" },
  { DXGI_FORMAT_P010, true, "P010_10LE" },
  { DXGI_FORMAT_P016, true, "P012_LE" },
  { DXGI_FORMAT_P016, true, "P016_LE" },
  { DXGI_FORMAT_YUY2, true, "YUY2" },
  { DXGI_FORMAT_Y210, true, "Y210" },
  { DXGI_FORMAT_Y216, true, "Y216" },
  { DXGI_FORMAT_R16G16B16A16_UNORM, false, "RGBA64_LE" },
  { DXGI_FORMAT_R10G10B10A2_UNORM, false, "RGB10A2_LE" },
  { DXGI_FORMAT_R8G8B8A8_UNORM, false, "RGBA" },
  { DXGI_FORMAT_B8G8R8A8_UNORM, false, "BGRA" },
};

void
gst_d3d12_vpp_register (GstPlugin * plugin, GstD3D12Device * device,
  ID3D12VideoDevice * video_device, guint rank)
{
  GST_DEBUG_CATEGORY_INIT (gst_d3d12_vpp_debug, "d3d12vpp", 0, "d3d12vpp");

  if (!gst_d3d12_device_get_cmd_queue (device,
        D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS)) {
    GST_INFO_OBJECT (device, "Video processor not supported");
    return;
  }

  std::set<std::string> in_formats;
  std::set<std::string> out_formats;
  guint min_width = G_MAXUINT32;
  guint min_height = G_MAXUINT32;
  guint max_width = 0;
  guint max_height = 0;

  for (guint i = 0; i < G_N_ELEMENTS (test_formats); i++) {
    for (guint j = 0; j < G_N_ELEMENTS (test_formats); j++) {
      D3D12_FEATURE_DATA_VIDEO_PROCESS_SUPPORT vp_support = { };
      vp_support.InputSample.Width = 640;
      vp_support.InputSample.Height = 640;
      vp_support.InputSample.Format.Format = test_formats[i].dxgi_format;
      vp_support.InputSample.Format.ColorSpace = test_formats[i].is_yuv ?
          DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 :
          DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      vp_support.InputFieldType = D3D12_VIDEO_FIELD_TYPE_NONE;
      vp_support.InputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;
      vp_support.InputFrameRate.Numerator = 30;
      vp_support.InputFrameRate.Denominator = 1;
      vp_support.OutputFormat.Format = test_formats[j].dxgi_format;
      vp_support.OutputFormat.ColorSpace = test_formats[j].is_yuv ?
          DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709 :
          DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
      vp_support.OutputStereoFormat = D3D12_VIDEO_FRAME_STEREO_FORMAT_NONE;
      vp_support.OutputFrameRate = vp_support.InputFrameRate;

      auto hr = video_device->CheckFeatureSupport
          (D3D12_FEATURE_VIDEO_PROCESS_SUPPORT, &vp_support,
          sizeof (vp_support));
      if (SUCCEEDED (hr) &&
          (vp_support.SupportFlags & D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED)
          == D3D12_VIDEO_PROCESS_SUPPORT_FLAG_SUPPORTED) {
        in_formats.insert (test_formats[i].format);
        out_formats.insert (test_formats[j].format);
        if (vp_support.ScaleSupport.OutputSizeRange.MaxWidth > max_width)
          max_width = vp_support.ScaleSupport.OutputSizeRange.MaxWidth;
        if (vp_support.ScaleSupport.OutputSizeRange.MaxHeight > max_height)
          max_height = vp_support.ScaleSupport.OutputSizeRange.MaxHeight;
        if (vp_support.ScaleSupport.OutputSizeRange.MinWidth < min_width)
          min_width = vp_support.ScaleSupport.OutputSizeRange.MinWidth;
        if (vp_support.ScaleSupport.OutputSizeRange.MinWidth < min_height)
          min_height = vp_support.ScaleSupport.OutputSizeRange.MinHeight;
      }
    }
  }

  if (in_formats.empty () || out_formats.empty ()) {
    GST_INFO_OBJECT (device, "Couldn't find supported conversion");
    return;
  }

  min_width = MAX (1, min_width);
  min_height = MAX (1, min_height);

  if (min_width >= max_width || min_height >= max_height) {
    GST_DEBUG_OBJECT (device, "Scaling is not supported");
    return;
  }

  std::string sink_caps_str =
      "video/x-raw(memory:D3D12Memory), format = (string) ";
  std::string format_str;
  for (const auto &it : in_formats) {
    if (!format_str.empty ())
      format_str += ", ";
    format_str += it;
  }

  if (in_formats.size () > 1)
    sink_caps_str += "{ " + format_str + "}";
  else
    sink_caps_str += format_str;

  format_str.clear ();
  std::string src_caps_str =
    "video/x-raw(memory:D3D12Memory), format = (string) ";

  for (const auto &it : out_formats) {
    if (!format_str.empty ())
      format_str += ", ";
    format_str += it;
  }

  if (out_formats.size () > 1)
    src_caps_str += "{ " + format_str + "}";
  else
    src_caps_str += format_str;

  auto sink_caps = gst_caps_from_string (sink_caps_str.c_str ());
  auto src_caps = gst_caps_from_string (src_caps_str.c_str ());

  gst_caps_set_simple (sink_caps,
      "width", GST_TYPE_INT_RANGE, min_width, max_width,
      "height", GST_TYPE_INT_RANGE, min_height, max_height, nullptr);
  gst_caps_set_simple (src_caps,
      "width", GST_TYPE_INT_RANGE, min_width, max_width,
      "height", GST_TYPE_INT_RANGE, min_height, max_height, nullptr);

  GST_MINI_OBJECT_FLAG_SET (sink_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);
  GST_MINI_OBJECT_FLAG_SET (src_caps, GST_MINI_OBJECT_FLAG_MAY_BE_LEAKED);

  auto cdata = new GstD3D12VppClassData ();
  cdata->sink_caps = sink_caps;
  cdata->src_caps = src_caps;

  g_object_get (device, "adapter-luid", &cdata->adapter_luid,
      "description", &cdata->desc, nullptr);

  GType type;
  gchar *type_name;
  gchar *feature_name;
  guint index = 0;
  GTypeInfo type_info = {
    sizeof (GstD3D12VppClass),
    nullptr,
    nullptr,
    (GClassInitFunc) gst_d3d12_vpp_class_init,
    nullptr,
    nullptr,
    sizeof (GstD3D12Vpp),
    0,
    (GInstanceInitFunc) gst_d3d12_vpp_init,
  };

  type_info.class_data = cdata;
  type_name = g_strdup ("GstD3D12Vpp");
  feature_name = g_strdup ("d3d12vpp");

  while (g_type_from_name (type_name)) {
    index++;
    g_free (type_name);
    g_free (feature_name);
    type_name = g_strdup_printf ("GstD3D12Device%dVpp", index);
    feature_name = g_strdup_printf ("d3d12device%dvpp", index);
  }

  type = g_type_register_static (GST_TYPE_BASE_TRANSFORM,
      type_name, &type_info, (GTypeFlags) 0);

  /* make lower rank than default device */
  if (rank > 0 && index != 0)
    rank--;

  if (index != 0)
    gst_element_type_set_skip_documentation (type);

  if (!gst_element_register (plugin, feature_name, rank, type))
    GST_WARNING ("Failed to register plugin '%s'", type_name);

  g_free (type_name);
  g_free (feature_name);
}
/* *INDENT-ON* */
