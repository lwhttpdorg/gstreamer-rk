/*
 * placebo gstreamer plugin
 * Copyright (C) 2025 Martin Rodriguez Reboredo <yakoyoku@gmail.com>
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
 * SECTION:element-placebo
 * @title: placebo
 *
 * Libplacebo filter for high quality accelerated rendering.
 *
 * ## Examples
 * |[
 * gst-launch-1.0 gltestsrc ! placebo hook-location="cool.hook" ! glimagesink
 * ]|
 * Both FBO (Frame Buffer Object) and GLSL (OpenGL Shading Language) are required.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gstmeta.h>

#include <gio/gio.h>

#include "gstplacebo.h"
#include "placeboapi.h"
#ifdef HAVE_VULKAN
#include "placebovulkan.h"
#endif
#ifdef HAVE_GL
#include "placebogl.h"
#endif

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_placebo_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, GST_VIDEO_FORMATS_ALL)
      ", texture-target = (string) 2D ; "
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, GST_VIDEO_FORMATS_ALL)
      ", texture-target = (string) 2D"
    ));
static GstStaticPadTemplate gst_placebo_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE, GST_VIDEO_FORMATS_ALL)
      ", texture-target = (string) 2D ; "
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_GL_MEMORY, GST_VIDEO_FORMATS_ALL)
      ", texture-target = (string) 2D"
    ));
/* *INDENT-ON* */

enum
{
  PROP_0,
  PROP_HOOK,
  PROP_HOOK_LOCATION,
  PROP_LAST,
};

enum
{
  SIGNAL_0,
  SIGNAL_CREATE_SHADER,
  SIGNAL_LAST,
};

static guint gst_placebo_signals[SIGNAL_LAST] = { 0 };

#define GST_CAT_DEFAULT gst_placebo_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY_STATIC (gst_gl_debug);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_placebo_debug, "placebo", 0, "placebo element");
#define gst_placebo_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstPlacebo, gst_placebo,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);
GST_ELEMENT_REGISTER_DEFINE (placebo, "placebo",
    GST_RANK_NONE, GST_TYPE_PLACEBO);

static void gst_placebo_finalize (GObject * object);
static void gst_placebo_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_placebo_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_placebo_start (GstBaseTransform * bt);
static gboolean gst_placebo_stop (GstBaseTransform * bt);
static gboolean gst_placebo_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query);
static void gst_placebo_set_context (GstElement * element,
    GstContext * context);
static GstCaps *gst_placebo_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_placebo_transform_meta (GstBaseTransform * bt,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf);
static GstCaps *gst_placebo_fixate_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_placebo_decide_allocation (GstBaseTransform * bt,
    GstQuery * query);
static gboolean gst_placebo_set_caps (GstBaseTransform * bt, GstCaps * in_caps,
    GstCaps * out_caps);
static GstStateChangeReturn gst_placebo_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_placebo_transform (GstBaseTransform * bt,
    GstBuffer * inbuf, GstBuffer * outbuf);

static GQuark _size_quark;
static GQuark _scale_quark;

static void
gst_placebo_class_init (GstPlaceboClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstbasetransform_class = GST_BASE_TRANSFORM_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  _size_quark = g_quark_from_static_string (GST_META_TAG_VIDEO_SIZE_STR);
  _scale_quark = gst_video_meta_transform_scale_get_quark ();

  gobject_class->finalize = gst_placebo_finalize;
  gobject_class->set_property = gst_placebo_set_property;
  gobject_class->get_property = gst_placebo_get_property;

  g_object_class_install_property (gobject_class, PROP_HOOK,
      g_param_spec_string ("hook", "Hook Source",
          "MPV hook source", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HOOK_LOCATION,
      g_param_spec_string ("hook-location", "Hook File",
          "MPV hook file", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));


  gst_element_class_set_static_metadata (gstelement_class,
      "Libplacebo video filter", "Filter/Effect/Video",
      "Perform operations with a GLSL shader",
      "Martin Rodriguez Reboredo <yakoyoku@gmail.com>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_placebo_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_placebo_src_template);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_placebo_change_state);
  gstelement_class->set_context = GST_DEBUG_FUNCPTR (gst_placebo_set_context);

  gstbasetransform_class->start = GST_DEBUG_FUNCPTR (gst_placebo_start);
  gstbasetransform_class->stop = GST_DEBUG_FUNCPTR (gst_placebo_stop);
  gstbasetransform_class->query = GST_DEBUG_FUNCPTR (gst_placebo_query);
  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_placebo_transform_caps);
  gstbasetransform_class->transform_meta =
      GST_DEBUG_FUNCPTR (gst_placebo_transform_meta);
  gstbasetransform_class->fixate_caps =
      GST_DEBUG_FUNCPTR (gst_placebo_fixate_caps);
  gstbasetransform_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_placebo_decide_allocation);
  gstbasetransform_class->set_caps = GST_DEBUG_FUNCPTR (gst_placebo_set_caps);
  gstbasetransform_class->transform = GST_DEBUG_FUNCPTR (gst_placebo_transform);
}

static void
gst_placebo_init (GstPlacebo * placebo)
{
}

static void
gst_placebo_finalize (GObject * object)
{
  GstPlacebo *placebo = GST_PLACEBO (object);

  g_free (placebo->hook);
  placebo->hook = NULL;
  gst_clear_object (&placebo->api);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_placebo_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPlacebo *placebo = GST_PLACEBO (object);

  switch (prop_id) {
    case PROP_HOOK:
      GST_OBJECT_LOCK (placebo);
      g_free (placebo->hook);
      placebo->hook = g_value_dup_string (value);
      placebo->refresh_shader = TRUE;
      GST_OBJECT_UNLOCK (placebo);
      break;
    case PROP_HOOK_LOCATION:
      GST_OBJECT_LOCK (placebo);
      g_free (placebo->hook);
      placebo->hook_location = g_value_dup_string (value);
      placebo->refresh_location = TRUE;
      GST_OBJECT_UNLOCK (placebo);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_placebo_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPlacebo *placebo = GST_PLACEBO (object);

  switch (prop_id) {
    case PROP_HOOK:
      GST_OBJECT_LOCK (placebo);
      g_value_set_string (value, placebo->hook);
      GST_OBJECT_UNLOCK (placebo);
      break;
    case PROP_HOOK_LOCATION:
      GST_OBJECT_LOCK (placebo);
      g_value_set_string (value, placebo->hook_location);
      GST_OBJECT_UNLOCK (placebo);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_placebo_start (GstBaseTransform * bt)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);
  return gst_placebo_api_start (placebo->api);
}

static gboolean
gst_placebo_stop (GstBaseTransform * bt)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);
  placebo->refresh_shader = TRUE;
  placebo->refresh_location = TRUE;
  return gst_placebo_api_stop (placebo->api);
}

static gboolean
gst_placebo_query (GstBaseTransform * bt,
    GstPadDirection direction, GstQuery * query)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      if (direction == GST_PAD_SINK) {
        gst_placebo_api_find_local_context (placebo->api);
        if (gst_base_transform_is_passthrough (bt))
          return gst_pad_peer_query (GST_BASE_TRANSFORM_SRC_PAD (bt), query);
      }
      break;
    }
    case GST_QUERY_CONTEXT:
    {
      if (gst_placebo_api_handle_context_query (placebo->api, direction, query))
        return TRUE;
      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->query (bt, direction, query);
}

static gboolean
_contains_vulkan_image (GstCapsFeatures * features)
{
#ifdef HAVE_VULKAN
  return gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_VULKAN_IMAGE);
#else
  return FALSE;
#endif
}

static gboolean
_contains_gl_memory (GstCapsFeatures * features)
{
#ifdef HAVE_GL
  return gst_caps_features_contains (features,
      GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
#else
  return FALSE;
#endif
}

static GstCaps *
gst_placebo_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);
  GstCaps *res = NULL;

  if (!placebo->api) {
    if (direction == GST_PAD_SRC) {
      gst_caps_replace (&placebo->in_caps, caps);
    } else if (direction == GST_PAD_SINK) {
      gst_caps_replace (&placebo->out_caps, caps);
    }
    if (placebo->in_caps && placebo->out_caps) {
      GstCaps *caps = gst_caps_intersect (placebo->in_caps, placebo->out_caps);
      for (int i = 0; i < gst_caps_get_size (caps); ++i) {
        GstCapsFeatures *features = gst_caps_get_features (caps, i);
        if (_contains_vulkan_image (features)) {
#ifdef HAVE_VULKAN
          placebo->api = GST_PLACEBO_API (gst_placebo_vulkan_new (placebo));
#endif
          break;
        } else if (_contains_gl_memory (features)) {
#ifdef HAVE_GL
          placebo->api = GST_PLACEBO_API (gst_placebo_gl_new (placebo));
#endif
          break;
        }
      }
      gst_caps_unref (caps);
    }
  }

  if (filter)
    res = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
  else
    res = gst_caps_ref (caps);

  return res;
}

static gboolean
gst_placebo_transform_meta (GstBaseTransform * bt,
    GstBuffer * outbuf, GstMeta * meta, GstBuffer * inbuf)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);
  const GstMetaInfo *info = meta->info;
  gboolean should_copy = TRUE;
  const gchar **tags, **curr;
  const gchar *valid_tags[] = {
    GST_META_TAG_VIDEO_STR,
    GST_META_TAG_VIDEO_ORIENTATION_STR,
    GST_META_TAG_VIDEO_SIZE_STR,
    GST_META_TAG_VIDEO_COLORSPACE_STR,
    NULL
  };

  tags = gst_meta_api_type_get_tags (info->api);
  should_copy = TRUE;
  if (tags) {
    for (curr = tags; *curr; ++curr) {
      if (!g_strv_contains (valid_tags, *curr)) {
        should_copy = FALSE;
        break;
      }
    }
  }
  // should_copy = gst_meta_api_type_tags_contain_only (info->api, valid_tags);
  /* Cant handle the tags in this meta, let the parent class handle it */
  if (!should_copy) {
    return GST_BASE_TRANSFORM_CLASS (parent_class)->transform_meta (bt,
        outbuf, meta, inbuf);
  }
  /* This meta is size sensitive, try to transform it accordingly */
  if (gst_meta_api_type_has_tag (info->api, _size_quark)) {
    GstVideoMetaTransform trans = { &placebo->in_info, &placebo->out_info };
    if (info->transform_func)
      info->transform_func (outbuf, meta, inbuf, _scale_quark, &trans);
    return FALSE;
  }

  /* No need to transform, we can safely copy this meta */
  return TRUE;
}

static GstCaps *
gst_placebo_fixate_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, }, tpar = {
    0,
  };
  othercaps = gst_caps_make_writable (othercaps);
  othercaps = gst_caps_truncate (othercaps);
  GST_DEBUG_OBJECT (bt, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);
  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);
  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");
  /* If we're fixating from the sinkpad we always set the PAR and
   * assume that missing PAR on the sinkpad means 1/1 and
   * missing PAR on the srcpad means undefined
   */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);
      to_par = &tpar;
    }
  } else {
    if (!to_par) {
      g_value_init (&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&tpar, 1, 1);

      to_par = &tpar;
      gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
          NULL);
    }
    if (!from_par) {
      g_value_init (&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction (&fpar, 1, 1);
      from_par = &fpar;
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
    /* if both width and height are already fixed, we can't do anything
     * about it anymore */
    if (w && h) {
      GST_DEBUG_OBJECT (bt, "dimensions already set to %dx%d, not fixating",
          w, h);
      if (!gst_value_is_fixed (to_par)) {
        GST_DEBUG_OBJECT (bt, "fixating to_par to %dx%d", 1, 1);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio"))
          gst_structure_fixate_field_nearest_fraction (outs,
              "pixel-aspect-ratio", 1, 1);
      }
      goto done;
    }
    /* Calculate input DAR */
    if (!gst_util_fraction_multiply (from_w, from_h, from_par_n, from_par_d,
            &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }
    GST_DEBUG_OBJECT (bt, "Input DAR is %d/%d", from_dar_n, from_dar_d);
    /* If either width or height are fixed there's not much we
     * can do either except choosing a height or width and PAR
     * that matches the DAR as good as possible
     */
    if (h) {
      gint num, den;
      GST_DEBUG_OBJECT (bt, "height is fixed (%d)", h);
      if (!gst_value_is_fixed (to_par)) {
        /* (shortcut) copy-paste (??) of videoscale seems to aim for 1/1,
         * so let's make it so ...
         * especially if following code assumes fixed */
        GST_DEBUG_OBJECT (bt, "fixating to_par to 1x1");
        gst_structure_fixate_field_nearest_fraction (outs,
            "pixel-aspect-ratio", 1, 1);
        to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");
      }
      /* PAR is fixed, choose the height that is nearest to the
       * height with the same DAR */
      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);
      GST_DEBUG_OBJECT (bt, "PAR is fixed %d/%d", to_par_n, to_par_d);
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
              to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }
      w = (guint) gst_util_uint64_scale_int (h, num, den);
      gst_structure_fixate_field_nearest_int (outs, "width", w);
      goto done;
    } else if (w) {
      gint num, den;
      GST_DEBUG_OBJECT (bt, "width is fixed (%d)", w);
      if (!gst_value_is_fixed (to_par)) {
        /* (shortcut) copy-paste (??) of videoscale seems to aim for 1/1,
         * so let's make it so ...
         * especially if following code assumes fixed */
        GST_DEBUG_OBJECT (bt, "fixating to_par to 1x1");
        gst_structure_fixate_field_nearest_fraction (outs,
            "pixel-aspect-ratio", 1, 1);
        to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");
      }
      /* PAR is fixed, choose the height that is nearest to the
       * height with the same DAR */
      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);
      GST_DEBUG_OBJECT (bt, "PAR is fixed %d/%d", to_par_n, to_par_d);
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_d,
              to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }
      h = (guint) gst_util_uint64_scale_int (w, den, num);
      gst_structure_fixate_field_nearest_int (outs, "height", h);
      goto done;
    } else if (gst_value_is_fixed (to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;
      to_par_n = gst_value_get_fraction_numerator (to_par);
      to_par_d = gst_value_get_fraction_denominator (to_par);
      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, to_par_n,
              to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      /* This might have failed but try to scale the width
       * to keep the DAR nonetheless */
      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &set_w);
      gst_structure_free (tmp);
      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
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
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_free (tmp);
      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, set_h, NULL);
        goto done;
      }
      /* If all this failed, keep the height that was nearest to the original
       * height and the nearest possible width. This changes the DAR but
       * there's not much else to do here.
       */
      gst_structure_set (outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
          f_h, NULL);
      goto done;
    } else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;
      /* width, height and PAR are not fixed */
      /* First try to keep the height and width as good as possible
       * and scale PAR */
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", from_h);
      gst_structure_get_int (tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int (tmp, "width", from_w);
      gst_structure_get_int (tmp, "width", &set_w);
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_h, set_w,
              &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
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
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }
      /* Otherwise try to scale width to keep the DAR with the set
       * PAR and height */
      if (!gst_util_fraction_multiply (from_dar_n, from_dar_d, set_par_d,
              set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR (bt, CORE, NEGOTIATION, (NULL),
            ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }
      w = (guint) gst_util_uint64_scale_int (set_h, num, den);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "width", w);
      gst_structure_get_int (tmp, "width", &tmp2);
      gst_structure_free (tmp);
      if (tmp2 == w) {
        gst_structure_set (outs, "width", G_TYPE_INT, tmp2, "height",
            G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }
      /* ... or try the same with the height */
      h = (guint) gst_util_uint64_scale_int (set_w, den, num);
      tmp = gst_structure_copy (outs);
      gst_structure_fixate_field_nearest_int (tmp, "height", h);
      gst_structure_get_int (tmp, "height", &tmp2);
      gst_structure_free (tmp);
      if (tmp2 == h) {
        gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
            G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
            set_par_n != set_par_d)
          gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
              set_par_n, set_par_d, NULL);
        goto done;
      }
      /* If all fails we can't keep the DAR and take the nearest values
       * for everything from the first try */
      gst_structure_set (outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field (outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
        gst_structure_set (outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
    }

  }
done:
  othercaps = gst_caps_fixate (othercaps);
  GST_DEBUG_OBJECT (bt, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);
  if (from_par == &fpar)
    g_value_unset (&fpar);
  if (to_par == &tpar)
    g_value_unset (&tpar);
  return othercaps;
}

static void
gst_placebo_set_context (GstElement * element, GstContext * context)
{
  GstPlacebo *placebo = GST_PLACEBO (element);
  gst_placebo_api_handle_set_context (placebo->api, context);
  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_placebo_decide_allocation (GstBaseTransform * bt, GstQuery * query)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);
  gboolean res = gst_placebo_api_decide_allocation (placebo->api, query);
  return res && GST_BASE_TRANSFORM_CLASS (parent_class)->decide_allocation (bt,
      query);
}

static gboolean
gst_placebo_set_caps (GstBaseTransform * bt,
    GstCaps * in_caps, GstCaps * out_caps)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);

  if (!gst_video_info_from_caps (&placebo->in_info, in_caps))
    return FALSE;
  if (!gst_video_info_from_caps (&placebo->out_info, out_caps))
    return FALSE;
  gst_caps_replace (&placebo->in_caps, in_caps);
  gst_caps_replace (&placebo->out_caps, out_caps);

  return gst_placebo_api_set_caps (placebo->api, in_caps, out_caps);
}

static GstStateChangeReturn
gst_placebo_change_state (GstElement * element, GstStateChange transition)
{
  GstPlacebo *placebo = GST_PLACEBO (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      ret = gst_placebo_api_ensure_element_data (placebo->api);
      if (ret != GST_STATE_CHANGE_SUCCESS)
        return ret;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_placebo_api_find_local_context (placebo->api);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_placebo_api_clear (placebo->api);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_placebo_color_space_info (struct pl_frame *frame, const GstVideoInfo * info)
{
  switch (GST_VIDEO_INFO_COLORIMETRY (info).matrix) {
    case GST_VIDEO_COLOR_MATRIX_RGB:
      frame->repr.sys = PL_COLOR_SYSTEM_RGB;
      break;
    case GST_VIDEO_COLOR_MATRIX_FCC:
      frame->repr.sys = PL_COLOR_SYSTEM_UNKNOWN;        // missing
      break;
    case GST_VIDEO_COLOR_MATRIX_BT709:
      frame->repr.sys = PL_COLOR_SYSTEM_BT_709;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT601:
      frame->repr.sys = PL_COLOR_SYSTEM_BT_601;
      break;
    case GST_VIDEO_COLOR_MATRIX_SMPTE240M:
      frame->repr.sys = PL_COLOR_SYSTEM_SMPTE_240M;
      break;
    case GST_VIDEO_COLOR_MATRIX_BT2020:
      frame->repr.sys = PL_COLOR_SYSTEM_BT_2020_C;      //return PL_COLOR_SYSTEM_BT_2020_NC;
      break;
    case GST_VIDEO_COLOR_MATRIX_UNKNOWN:
      G_GNUC_FALLTHROUGH;
    default:
      frame->repr.sys = PL_COLOR_SYSTEM_UNKNOWN;
      break;
  }

  switch (GST_VIDEO_INFO_COLORIMETRY (info).primaries) {
    case GST_VIDEO_COLOR_PRIMARIES_BT709:
      frame->color.primaries = PL_COLOR_PRIM_BT_709;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT470M:
      frame->color.primaries = PL_COLOR_PRIM_BT_470M;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT470BG:
      frame->color.primaries = PL_COLOR_PRIM_BT_601_625;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE170M:
      frame->color.primaries = PL_COLOR_PRIM_BT_601_525;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTE240M:
      frame->color.primaries = PL_COLOR_PRIM_BT_601_525;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_FILM:
      frame->color.primaries = PL_COLOR_PRIM_FILM_C;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_BT2020:
      frame->color.primaries = PL_COLOR_PRIM_BT_2020;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_ADOBERGB:
      frame->color.primaries = PL_COLOR_PRIM_ADOBE;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTEST428:
      frame->color.primaries = PL_COLOR_PRIM_CIE_1931;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTERP431:
      frame->color.primaries = PL_COLOR_PRIM_DCI_P3;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_SMPTEEG432:
      frame->color.primaries = PL_COLOR_PRIM_DISPLAY_P3;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_EBU3213:
      frame->color.primaries = PL_COLOR_PRIM_EBU_3213;
      break;
    case GST_VIDEO_COLOR_PRIMARIES_UNKNOWN:
      G_GNUC_FALLTHROUGH;
    default:
      frame->color.primaries = PL_COLOR_PRIM_UNKNOWN;
      break;
  }

  switch (GST_VIDEO_INFO_COLORIMETRY (info).range) {
    case GST_VIDEO_COLOR_RANGE_0_255:
      frame->repr.levels = PL_COLOR_LEVELS_FULL;
      break;
    case GST_VIDEO_COLOR_RANGE_16_235:
      frame->repr.levels = PL_COLOR_LEVELS_LIMITED;
      break;
    case GST_VIDEO_COLOR_RANGE_UNKNOWN:
      G_GNUC_FALLTHROUGH;
    default:
      frame->repr.levels = PL_COLOR_LEVELS_UNKNOWN;
      break;
  }

  switch (GST_VIDEO_INFO_COLORIMETRY (info).transfer) {
    case GST_VIDEO_TRANSFER_GAMMA10:
      frame->color.transfer = PL_COLOR_TRC_LINEAR;
      break;
    case GST_VIDEO_TRANSFER_GAMMA18:
      frame->color.transfer = PL_COLOR_TRC_GAMMA18;
      break;
    case GST_VIDEO_TRANSFER_GAMMA20:
      frame->color.transfer = PL_COLOR_TRC_GAMMA20;
      break;
    case GST_VIDEO_TRANSFER_GAMMA22:
      frame->color.transfer = PL_COLOR_TRC_GAMMA22;
      break;
    case GST_VIDEO_TRANSFER_BT709:
      frame->color.transfer = PL_COLOR_TRC_BT_1886;     // EOTF != OETF
      break;
    case GST_VIDEO_TRANSFER_SMPTE240M:
      frame->color.transfer = PL_COLOR_TRC_BT_1886;     // EOTF != OETF
      break;
    case GST_VIDEO_TRANSFER_SRGB:
      frame->color.transfer = PL_COLOR_TRC_SRGB;
      break;
    case GST_VIDEO_TRANSFER_GAMMA28:
      frame->color.transfer = PL_COLOR_TRC_GAMMA28;
      break;
    case GST_VIDEO_TRANSFER_LOG100:
      frame->color.transfer = PL_COLOR_TRC_UNKNOWN;     // missing
      break;
    case GST_VIDEO_TRANSFER_LOG316:
      frame->color.transfer = PL_COLOR_TRC_UNKNOWN;     // missing
      break;
    case GST_VIDEO_TRANSFER_BT2020_12:
      frame->color.transfer = PL_COLOR_TRC_BT_1886;     // EOTF != OETF
      break;
    case GST_VIDEO_TRANSFER_ADOBERGB:
      frame->color.transfer = PL_COLOR_TRC_UNKNOWN;     // missing
      break;
    case GST_VIDEO_TRANSFER_BT2020_10:
      frame->color.transfer = PL_COLOR_TRC_BT_1886;     // EOTF != OETF
      break;
    case GST_VIDEO_TRANSFER_SMPTE2084:
      frame->color.transfer = PL_COLOR_TRC_PQ;
      break;
    case GST_VIDEO_TRANSFER_ARIB_STD_B67:
      frame->color.transfer = PL_COLOR_TRC_HLG;
      break;
    case GST_VIDEO_TRANSFER_BT601:
      frame->color.transfer = PL_COLOR_TRC_BT_1886;     // EOTF != OETF
      break;
    case GST_VIDEO_TRANSFER_UNKNOWN:
      G_GNUC_FALLTHROUGH;
    default:
      frame->color.transfer = PL_COLOR_TRC_UNKNOWN;
      break;
  }
}

static gboolean
gst_placebo_prepare_frame (GstPlacebo * placebo, GstBuffer * buf,
    const GstVideoInfo * info, struct pl_frame *frame)
{
  g_assert (GST_VIDEO_INFO_N_PLANES (info) <= 4);
  *frame = (struct pl_frame) {
    .num_planes = GST_VIDEO_INFO_N_PLANES (info),
    .crop = {
          .x0 = 0,
          .y0 = 0,
          .x1 = GST_VIDEO_INFO_WIDTH (info),
          .y1 = GST_VIDEO_INFO_HEIGHT (info),
        },
    .repr = {
          .alpha =
          GST_VIDEO_INFO_HAS_ALPHA (info) ? PL_ALPHA_INDEPENDENT :
          PL_ALPHA_NONE,
          // For sake of simplicity, just use the first component's depth as
          // the authoritative color depth for the whole image. Usually, this
          // will be overwritten by more specific information when using e.g.
          // `pl_map_avframe`, but for the sake of e.g. users wishing to map
          // hwaccel frames manually, this is a good default.
          .bits.color_depth = GST_VIDEO_INFO_COMP_DEPTH (info, 0),
        },
    .color = pl_color_space_unknown,
  };
  gst_placebo_color_space_info (frame, info);
  // pl_color_space_from_avframe(&out->color, frame);
  // if (frame->colorspace == AVCOL_SPC_ICTCP &&
  // frame->color_trc == AVCOL_TRC_ARIB_STD_B67)
  // {
  // libav* makes no distinction between PQ and HLG ICtCp, so we need
  // to manually fix it in the case that we have HLG ICtCp data.
  // frame->repr.sys = PL_COLOR_SYSTEM_BT_2100_HLG;
  // } else if (strncmp(desc->name, "xyz", 3) == 0) {
  // libav* handles this as a special case, but doesn't provide an
  // explicit flag for it either, so we have to resort to this ugly
  // hack...
  // frame->repr.sys = PL_COLOR_SYSTEM_XYZ;
  // } else
  if (GST_VIDEO_INFO_IS_RGB (info)) {
    frame->repr.sys = PL_COLOR_SYSTEM_RGB;
    // frame->repr.levels = PL_COLOR_LEVELS_FULL; // libav* ignores levels for RGB
  } else if (!pl_color_system_is_ycbcr_like (frame->repr.sys)) {
    // libav* likes leaving this as UNKNOWN (or even RGB) for YCbCr frames,
    // which confuses libplacebo since we infer UNKNOWN as RGB. To get
    // around this, explicitly infer a suitable colorspace.
    frame->repr.sys =
        pl_color_system_guess_ycbcr (GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info));
  }
  // if ((sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE))) {
  // frame->profile = (struct pl_icc_profile) {
  // .data = sd->data,
  // .len = sd->size,
  // };
  // Needed to ensure profile uniqueness
  // pl_icc_profile_compute_signature(&frame->profile);
  // }
  // if ((sd = av_frame_get_side_data(frame, AV_FRAME_DATA_DISPLAYMATRIX))) {
  // double rot = av_display_rotation_get((const int32_t *) sd->data);
  // frame->rotation = pl_rotation_normalize(4.5 - rot / 90.0);
  // }
// #ifdef PL_HAVE_LAV_FILM_GRAIN
  // if ((sd = av_frame_get_side_data(frame, AV_FRAME_DATA_FILM_GRAIN_PARAMS)))
  // pl_film_grain_from_av(&frame->film_grain, (AVFilmGrainParams *) sd->data);
// #endif // HAVE_LAV_FILM_GRAIN
  for (int p = 0; p < frame->num_planes; p++) {
    struct pl_plane *plane = &frame->planes[p];
    // Fill in the component mapping array
    for (int c = 0; c < GST_VIDEO_INFO_N_COMPONENTS (info); c++) {
      if (GST_VIDEO_INFO_COMP_PLANE (info, c) == p)
        plane->component_mapping[plane->components++] = c;
    }
    // Clear the superfluous components
    for (int c = plane->components; c < 4; c++)
      plane->component_mapping[c] = PL_CHANNEL_NONE;
  }
  // Only set the chroma location for definitely subsampled images, makes no
  // sense otherwise
  // if (desc->log2_chroma_w || desc->log2_chroma_h) {
  // enum pl_chroma_location loc = pl_chroma_from_av(frame->chroma_location);
  // pl_frame_set_chroma_location(out, loc);
  // }
  return TRUE;
}

static gboolean
_maybe_recompile_shader (GstPlacebo * placebo)
{
  gsize n = 0;
  if (placebo->refresh_shader) {
    gsize size = placebo->hook ? strlen (placebo->hook) : 0;
    if (placebo->api->source_hook)
      pl_mpv_user_shader_destroy (&placebo->api->source_hook);
    if (size)
      placebo->api->source_hook =
          pl_mpv_user_shader_parse (placebo->api->gpu, placebo->hook, size);
    placebo->refresh_shader = FALSE;
  }
  if (placebo->refresh_location) {
    GFile *file = NULL;
    GFileInfo *info = NULL;
    GFileInputStream *istream = NULL;
    GBytes *res = NULL;
    GError *error = NULL;
    const gchar *data = NULL;
    gsize len = 35648;

    if (placebo->api->location_hook)
      pl_mpv_user_shader_destroy (&placebo->api->location_hook);
    if (placebo->hook_location && strlen (placebo->hook_location))
      file = g_file_new_for_path (placebo->hook_location);
    if (file && (istream = g_file_read (file, NULL, &error))) {
      if ((info =
              g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_SIZE,
                  G_FILE_QUERY_INFO_NONE, NULL, NULL))) {
        len = g_file_info_get_size (info);
        g_object_unref (info);
      }
      if ((res =
              g_input_stream_read_bytes (G_INPUT_STREAM (istream), len, NULL,
                  &error))) {
        data = g_bytes_get_data (res, &len);
      }
      if (data && len)
        placebo->api->location_hook =
            pl_mpv_user_shader_parse (placebo->api->gpu, data, len);
      placebo->refresh_location = FALSE;
    }
    g_bytes_unref (res);
    if (istream)
      g_input_stream_close (G_INPUT_STREAM (istream), NULL, NULL);
    if (file)
      g_object_unref (file);
  }
  if (placebo->api->source_hook) {
    placebo->api->hooks[n] = placebo->api->source_hook;
    ++n;
  }
  if (placebo->api->location_hook) {
    placebo->api->hooks[n] = placebo->api->location_hook;
    ++n;
  }
  placebo->api->opts->params.hooks = placebo->api->hooks;
  placebo->api->opts->params.num_hooks = n;
  return n != 0;
}

static GstFlowReturn
gst_placebo_transform (GstBaseTransform * bt, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstPlacebo *placebo = GST_PLACEBO (bt);
  GstFlowReturn res = GST_FLOW_OK;
  gboolean mapped = TRUE;
  GstVideoFrame in_frame, out_frame;
  struct pl_frame frame, target;

  if (!gst_placebo_prepare_frame (placebo, inbuf, &placebo->in_info, &frame)
      || !gst_placebo_prepare_frame (placebo, outbuf, &placebo->out_info,
          &target))
    return GST_FLOW_CUSTOM_ERROR;
  frame.user_data = &in_frame;
  target.user_data = &out_frame;

  if (placebo->refresh_shader || placebo->refresh_location)
    _maybe_recompile_shader (placebo);

  mapped &=
      gst_placebo_api_map_frame (placebo->api, inbuf, &frame, &placebo->in_info,
      GST_MAP_READ);
  mapped &=
      gst_placebo_api_map_frame (placebo->api, outbuf, &target,
      &placebo->out_info, GST_MAP_WRITE);

  if (mapped)
    res = gst_placebo_api_render (placebo->api, &frame, &target);

  gst_placebo_api_unmap_frame (placebo->api, &frame);
  gst_placebo_api_unmap_frame (placebo->api, &target);

  return res;
}
