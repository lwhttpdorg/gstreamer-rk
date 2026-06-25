/* GStreamer analytics overlay base class
 * Copyright (C) <2026> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticsoverlay.c
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
 * SECTION:GstAnalyticsOverlay
 * @title: GstAnalyticsOverlay
 * @short_description: Abstract base class for Cairo/Pango-based analytics
 * overlays
 *
 * GstAnalyticsOverlay provides the full-frame Cairo canvas, Pango font
 * lifecycle, caps negotiation, stream event handling, and overlay composition
 * for subclasses that render analytics metadata via Cairo.
 *
 * Subclasses must implement the `render()` vfunc. Optionally they may override
 * `get_font_size()` to change the Pango font size (default: 10000).
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstanalyticsoverlay.h"

#define MINIMUM_TEXT_OUTLINE_OFFSET 1.0
#define DEFAULT_FONT_SIZE 10000
#define DEFAULT_EXPIRE_OVERLAY GST_SECOND

GST_DEBUG_CATEGORY_STATIC (analyticsoverlay_debug);
#define GST_CAT_DEFAULT analyticsoverlay_debug

typedef struct _GstAnalyticsOverlayPrivate GstAnalyticsOverlayPrivate;

struct _GstAnalyticsOverlayPrivate
{
  cairo_matrix_t cairo_matrix;
  gsize render_len;

  /* stream metrics */
  GstVideoInfo *in_info;
  GMutex stream_event_mutex;
  gboolean flushing;
  gboolean eos;

  /* composition */
  gboolean attach_compo_to_buffer;
  GstBuffer *canvas;
  gint canvas_length;
  GstVideoOverlayComposition *composition;

  /* timing */
  GstClockTime expire_overlay;
  GstClockTime last_composition_update;

  /* Pango */
  PangoContext *pango_context;
  PangoLayout *pango_layout;
};

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstAnalyticsOverlay, gst_analytics_overlay,
    GST_TYPE_VIDEO_FILTER)
#define GST_ANALYTICS_OVERLAY_GET_PRIVATE(self)                                \
  ((GstAnalyticsOverlayPrivate *)gst_analytics_overlay_get_instance_private(   \
      GST_ANALYTICS_OVERLAY(self)))
     enum
     { PROP_EXPIRE_OVERLAY = 1, _PROP_COUNT };

     static void gst_analytics_overlay_finalize (GObject * object);
     static gboolean gst_analytics_overlay_start (GstBaseTransform * trans);
     static gboolean gst_analytics_overlay_stop (GstBaseTransform * trans);
     static gboolean gst_analytics_overlay_sink_event (GstBaseTransform * trans,
    GstEvent * event);
     static gboolean gst_analytics_overlay_set_info (GstVideoFilter * filter,
    GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info);
     static GstFlowReturn
         gst_analytics_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame);

#define VIDEO_FORMATS GST_VIDEO_OVERLAY_COMPOSITION_BLEND_FORMATS
#define ANALYTICS_OVERLAY_CAPS GST_VIDEO_CAPS_MAKE(VIDEO_FORMATS)

     static GstStaticCaps sw_template_caps =
         GST_STATIC_CAPS (ANALYTICS_OVERLAY_CAPS);

     static gboolean gst_analytics_overlay_can_handle_caps (GstCaps * incaps)
{
  gboolean ret;
  GstCaps *caps;

  caps = gst_static_caps_get (&sw_template_caps);
  ret = gst_caps_is_subset (incaps, caps);
  gst_caps_unref (caps);

  return ret;
}

static gboolean
gst_analytics_overlay_negotiate (GstAnalyticsOverlay * overlay, GstCaps * caps)
{
  GstAnalyticsOverlayPrivate *priv =
      GST_ANALYTICS_OVERLAY_GET_PRIVATE (overlay);
  GstBaseTransform *basetransform = GST_BASE_TRANSFORM (overlay);
  gboolean upstream_has_meta = FALSE;
  gboolean caps_has_meta = FALSE;
  gboolean alloc_has_meta = FALSE;
  gboolean attach = FALSE;
  gboolean ret = TRUE;
  guint width, height;
  GstCapsFeatures *f;
  GstCaps *overlay_caps;
  GstQuery *query;
  guint alloc_index;
  GstPad *srcpad = basetransform->srcpad;
  GstPad *sinkpad = basetransform->sinkpad;

  GST_DEBUG_OBJECT (overlay, "performing negotiation");

  /* Clear any pending reconfigure to avoid negotiating twice */
  gst_pad_check_reconfigure (sinkpad);

  /* Check if upstream caps have meta */
  if ((f = gst_caps_get_features (caps, 0))) {
    GST_DEBUG_OBJECT (overlay, "upstream has caps");
    upstream_has_meta =
        gst_caps_features_contains (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);
  }

  /* Initialize dimensions */
  width = GST_VIDEO_INFO_WIDTH (priv->in_info);
  height = GST_VIDEO_INFO_HEIGHT (priv->in_info);
  GST_DEBUG_OBJECT (overlay, "initial dims: %ux%u", width, height);

  if (upstream_has_meta) {
    overlay_caps = gst_caps_ref (caps);
  } else {
    GstCaps *peercaps;

    /* BaseTransform requires caps for the allocation query to work */
    overlay_caps = gst_caps_copy (caps);
    f = gst_caps_get_features (overlay_caps, 0);
    gst_caps_features_add (f,
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION);

    /* Then check if downstream accept overlay composition in caps */
    peercaps = gst_pad_peer_query_caps (srcpad, overlay_caps);
    caps_has_meta = !gst_caps_is_empty (peercaps);
    gst_caps_unref (peercaps);

    GST_DEBUG_OBJECT (overlay, "caps have overlay meta %d", caps_has_meta);
  }

  if (upstream_has_meta || caps_has_meta) {
    /* Send caps immediately, it's needed by GstBaseTransform to get a reply
     * from allocation query */
    GST_BASE_TRANSFORM_CLASS (gst_analytics_overlay_parent_class)
        ->set_caps (basetransform, caps, overlay_caps);
    ret = gst_pad_set_caps (srcpad, overlay_caps);

    /* First check if the allocation meta has composition */
    query = gst_query_new_allocation (overlay_caps, FALSE);

    if (!gst_pad_peer_query (srcpad, query)) {
      /* no problem, we use the query defaults */
      GST_DEBUG_OBJECT (overlay, "ALLOCATION query failed");

      /* In case we were flushing, mark reconfigure and fail this method,
       * will make it retry */
      if (priv->flushing)
        ret = FALSE;
    }

    alloc_has_meta =
        gst_query_find_allocation_meta (query,
        GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, &alloc_index);

    GST_DEBUG_OBJECT (overlay, "sink alloc has overlay meta %d",
        alloc_has_meta);

    if (alloc_has_meta) {
      const GstStructure *params;

      gst_query_parse_nth_allocation_meta (query, alloc_index, &params);
      if (params) {
        if (gst_structure_get (params, "width", G_TYPE_UINT, &width, "height",
                G_TYPE_UINT, &height, NULL)) {
          GST_DEBUG_OBJECT (overlay, "received window size: %dx%d", width,
              height);
          g_assert (width != 0 && height != 0);
        }
      }
    }

    gst_query_unref (query);
  }

  /* Update render size if needed */
  priv->canvas_length = width * height;

  /* For backward compatibility, we will prefer blitting if downstream
   * allocation does not support the meta. In other case we will prefer
   * attaching, and will fail the negotiation in the unlikely case we are
   * forced to blit, but format isn't supported. */

  if (upstream_has_meta) {
    attach = TRUE;
  } else if (caps_has_meta) {
    if (alloc_has_meta) {
      attach = TRUE;
    } else {
      /* Don't attach unless we cannot handle the format */
      attach = !gst_analytics_overlay_can_handle_caps (caps);
    }
  } else {
    ret = gst_analytics_overlay_can_handle_caps (caps);
  }

  /* If we attach, then pick the overlay caps */
  if (attach) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, overlay_caps);
    /* Caps were already sent */
  } else if (ret) {
    GST_DEBUG_OBJECT (overlay, "Using caps %" GST_PTR_FORMAT, caps);
    GST_BASE_TRANSFORM_CLASS (gst_analytics_overlay_parent_class)
        ->set_caps (basetransform, caps, caps);
    ret = gst_pad_set_caps (srcpad, caps);
  }

  priv->attach_compo_to_buffer = attach;

  if (attach) {
    GST_BASE_TRANSFORM_CLASS (gst_analytics_overlay_parent_class)
        ->passthrough_on_same_caps = FALSE;
  }

  if (!ret) {
    GST_DEBUG_OBJECT (overlay, "negotiation failed, schedule reconfigure");
    gst_pad_mark_reconfigure (srcpad);
  }

  gst_caps_unref (overlay_caps);

  return ret;
}

static gboolean
gst_analytics_overlay_on_sinkpad_caps_event (GstAnalyticsOverlay * overlay,
    GstCaps * caps)
{
  GstAnalyticsOverlayPrivate *priv =
      GST_ANALYTICS_OVERLAY_GET_PRIVATE (overlay);
  gboolean ret = FALSE;

  if (!gst_video_info_from_caps (priv->in_info, caps))
    goto invalid_caps;

  ret = gst_analytics_overlay_negotiate (overlay, caps);
  GST_VIDEO_FILTER (overlay)->negotiated = ret;

  if (!priv->attach_compo_to_buffer &&
      !gst_analytics_overlay_can_handle_caps (caps)) {
    GST_DEBUG_OBJECT (overlay, "unsupported caps %" GST_PTR_FORMAT, caps);
    ret = FALSE;
  }

  return ret;

invalid_caps:{
    GST_DEBUG_OBJECT (overlay, "could not parse caps");
    return FALSE;
  }
}

static void
gst_analytics_overlay_create_cairo_ctx (GstAnalyticsOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * ctx, guint8 * data)
{
  GstAnalyticsOverlayPrivate *priv =
      GST_ANALYTICS_OVERLAY_GET_PRIVATE (overlay);

  ctx->cairo_matrix = &priv->cairo_matrix;
  ctx->surface =
      cairo_image_surface_create_for_data (data, CAIRO_FORMAT_ARGB32,
      GST_VIDEO_INFO_WIDTH (priv->in_info),
      GST_VIDEO_INFO_HEIGHT (priv->in_info),
      GST_VIDEO_INFO_WIDTH (priv->in_info) * 4);
  ctx->cr = cairo_create (ctx->surface);

  /* clear surface */
  cairo_set_operator (ctx->cr, CAIRO_OPERATOR_CLEAR);
  cairo_paint (ctx->cr);
  cairo_set_operator (ctx->cr, CAIRO_OPERATOR_OVER);

  /* apply transformations */
  cairo_set_matrix (ctx->cr, ctx->cairo_matrix);
  cairo_save (ctx->cr);
}

static void
gst_analytics_overlay_destroy_cairo_ctx (GstAnalyticsOverlayCairoCtx * ctx)
{
  cairo_restore (ctx->cr);
  cairo_destroy (ctx->cr);
  cairo_surface_destroy (ctx->surface);
}

/**
 * gst_analytics_overlay_compute_label_ofs:
 * @desc: a #PangoFontDescription
 *
 * Compute the text-outline offset from a font description.
 * Subclasses that draw labels call this from their start() override after
 * chaining up to the base.
 *
 * Returns: the outline offset in pixels (at least MINIMUM_TEXT_OUTLINE_OFFSET)
 */
gdouble
gst_analytics_overlay_compute_label_ofs (PangoFontDescription * desc)
{
  gint font_size = pango_font_description_get_size (desc) / PANGO_SCALE;
  gdouble ofs = (gdouble) font_size / 15.0;
  return MAX (ofs, MINIMUM_TEXT_OUTLINE_OFFSET);
}

/**
 * gst_analytics_overlay_get_pango_layout:
 * @self: a #GstAnalyticsOverlay
 *
 * Returns: (transfer none): the #PangoLayout owned by the base class
 */
PangoLayout *
gst_analytics_overlay_get_pango_layout (GstAnalyticsOverlay * self)
{
  GstAnalyticsOverlayPrivate *priv = GST_ANALYTICS_OVERLAY_GET_PRIVATE (self);
  return priv->pango_layout;
}

/**
 * gst_analytics_overlay_get_pango_context:
 * @self: a #GstAnalyticsOverlay
 *
 * Returns: (transfer none): the #PangoContext owned by the base class
 */
PangoContext *
gst_analytics_overlay_get_pango_context (GstAnalyticsOverlay * self)
{
  GstAnalyticsOverlayPrivate *priv = GST_ANALYTICS_OVERLAY_GET_PRIVATE (self);
  return priv->pango_context;
}

/**
 * gst_analytics_overlay_get_in_info:
 * @self: a #GstAnalyticsOverlay
 *
 * Returns: (transfer none): the input #GstVideoInfo
 */
GstVideoInfo *
gst_analytics_overlay_get_in_info (GstAnalyticsOverlay * self)
{
  GstAnalyticsOverlayPrivate *priv = GST_ANALYTICS_OVERLAY_GET_PRIVATE (self);
  return priv->in_info;
}

static void
gst_analytics_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAnalyticsOverlayPrivate *priv = GST_ANALYTICS_OVERLAY_GET_PRIVATE (object);

  switch (prop_id) {
    case PROP_EXPIRE_OVERLAY:
      priv->expire_overlay = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_analytics_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAnalyticsOverlayPrivate *priv = GST_ANALYTICS_OVERLAY_GET_PRIVATE (object);

  switch (prop_id) {
    case PROP_EXPIRE_OVERLAY:
      g_value_set_uint64 (value, priv->expire_overlay);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_analytics_overlay_finalize (GObject * object)
{
  gst_analytics_overlay_stop (GST_BASE_TRANSFORM (object));

  G_OBJECT_CLASS (gst_analytics_overlay_parent_class)->finalize (object);
}

static gboolean
gst_analytics_overlay_start (GstBaseTransform * trans)
{
  GstAnalyticsOverlay *overlay = GST_ANALYTICS_OVERLAY (trans);
  GstAnalyticsOverlayPrivate *priv =
      GST_ANALYTICS_OVERLAY_GET_PRIVATE (overlay);
  GstAnalyticsOverlayClass *klass = GST_ANALYTICS_OVERLAY_GET_CLASS (overlay);
  PangoFontDescription *desc;
  PangoFontMap *fontmap;
  gint font_size;

  fontmap = pango_cairo_font_map_new ();
  priv->pango_context =
      pango_font_map_create_context (PANGO_FONT_MAP (fontmap));
  g_object_unref (fontmap);
  priv->pango_layout = pango_layout_new (priv->pango_context);
  desc = pango_context_get_font_description (priv->pango_context);

  font_size = (klass->get_font_size != NULL) ? klass->get_font_size (overlay)
      : DEFAULT_FONT_SIZE;

  pango_font_description_set_size (desc, font_size);
  pango_font_description_set_weight (desc, PANGO_WEIGHT_ULTRALIGHT);
  pango_context_set_font_description (priv->pango_context, desc);
  pango_layout_set_alignment (priv->pango_layout, PANGO_ALIGN_LEFT);

  return TRUE;
}

static gboolean
gst_analytics_overlay_stop (GstBaseTransform * trans)
{
  GstAnalyticsOverlayPrivate *priv = GST_ANALYTICS_OVERLAY_GET_PRIVATE (trans);

  g_clear_object (&priv->pango_layout);
  g_clear_object (&priv->pango_context);
  gst_clear_buffer (&priv->canvas);

  return TRUE;
}

static gboolean
gst_analytics_overlay_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  gboolean ret = FALSE;
  GST_DEBUG_OBJECT (trans, "received sink event %s",
      GST_EVENT_TYPE_NAME (event));

  GstAnalyticsOverlay *overlay = GST_ANALYTICS_OVERLAY (trans);
  GstAnalyticsOverlayPrivate *priv =
      GST_ANALYTICS_OVERLAY_GET_PRIVATE (overlay);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;
      gst_event_parse_caps (event, &caps);
      ret = gst_analytics_overlay_on_sinkpad_caps_event (overlay, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_EOS:
      g_mutex_lock (&priv->stream_event_mutex);
      GST_INFO_OBJECT (overlay, "EOS");
      priv->eos = TRUE;
      g_mutex_unlock (&priv->stream_event_mutex);
      ret = GST_BASE_TRANSFORM_CLASS (gst_analytics_overlay_parent_class)
          ->sink_event (trans, event);
      break;
    case GST_EVENT_FLUSH_START:
      g_mutex_lock (&priv->stream_event_mutex);
      GST_INFO_OBJECT (overlay, "Flush start");
      priv->flushing = TRUE;
      g_mutex_unlock (&priv->stream_event_mutex);
      ret = GST_BASE_TRANSFORM_CLASS (gst_analytics_overlay_parent_class)
          ->sink_event (trans, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      g_mutex_lock (&priv->stream_event_mutex);
      GST_INFO_OBJECT (overlay, "Flush stop");
      priv->eos = FALSE;
      priv->flushing = FALSE;
      g_mutex_unlock (&priv->stream_event_mutex);
      ret = GST_BASE_TRANSFORM_CLASS (gst_analytics_overlay_parent_class)
          ->sink_event (trans, event);
      break;
    default:
      ret = GST_BASE_TRANSFORM_CLASS (gst_analytics_overlay_parent_class)
          ->sink_event (trans, event);
      break;
  }

  return ret;
}

static gboolean
gst_analytics_overlay_set_info (GstVideoFilter * filter,
    GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstAnalyticsOverlayPrivate *priv = GST_ANALYTICS_OVERLAY_GET_PRIVATE (filter);

  GST_DEBUG_OBJECT (filter, "set_info incaps:%" GST_PTR_FORMAT, incaps);
  GST_DEBUG_OBJECT (filter, "set_info outcaps:%" GST_PTR_FORMAT, outcaps);

  filter->in_info = *in_info;
  filter->out_info = *out_info;

  cairo_matrix_init_scale (&priv->cairo_matrix, 1, 1);
  priv->render_len =
      GST_VIDEO_INFO_WIDTH (in_info) * GST_VIDEO_INFO_HEIGHT (in_info) * 4;

  return TRUE;
}

static GstFlowReturn
gst_analytics_overlay_transform_frame_ip (GstVideoFilter * filter,
    GstVideoFrame * frame)
{
  GstBaseTransform *baset = GST_BASE_TRANSFORM (filter);
  GstAnalyticsOverlay *overlay = GST_ANALYTICS_OVERLAY (filter);
  GstAnalyticsOverlayClass *klass = GST_ANALYTICS_OVERLAY_GET_CLASS (overlay);
  GstAnalyticsOverlayPrivate *priv =
      GST_ANALYTICS_OVERLAY_GET_PRIVATE (overlay);
  GstVideoOverlayRectangle *rectangle = NULL;
  GstClockTime rt = GST_CLOCK_TIME_NONE;

  GST_DEBUG_OBJECT (filter, "buffer writeable=%d",
      gst_buffer_is_writable (frame->buffer));

  g_mutex_lock (&priv->stream_event_mutex);
  if (priv->eos || priv->flushing) {
    g_mutex_unlock (&priv->stream_event_mutex);
    return GST_FLOW_EOS;
  }
  g_mutex_unlock (&priv->stream_event_mutex);

  if (baset->have_segment)
    rt = gst_segment_to_running_time (&baset->segment, GST_FORMAT_TIME,
        GST_BUFFER_PTS (frame->buffer));

  GstAnalyticsRelationMeta *rmeta = (GstAnalyticsRelationMeta *)
      gst_buffer_get_meta (GST_BUFFER (frame->buffer),
      GST_ANALYTICS_RELATION_META_API_TYPE);

  if (rmeta) {
    GST_DEBUG_OBJECT (filter, "received buffer with analytics relation meta");

    GstBuffer *buffer;
    GstMapInfo map;
    GstAnalyticsOverlayCairoCtx cairo_ctx;

    buffer = gst_buffer_new_and_alloc (priv->render_len);
    gst_buffer_add_video_meta (buffer, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB,
        GST_VIDEO_INFO_WIDTH (priv->in_info),
        GST_VIDEO_INFO_HEIGHT (priv->in_info));

    gst_buffer_replace (&priv->canvas, buffer);
    gst_buffer_unref (buffer);

    gst_buffer_map (priv->canvas, &map, GST_MAP_READWRITE);
    memset (map.data, 0, priv->render_len);

    gst_analytics_overlay_create_cairo_ctx (overlay, &cairo_ctx, map.data);

    if (priv->composition)
      gst_video_overlay_composition_unref (priv->composition);

    priv->composition = gst_video_overlay_composition_new (NULL);

    priv->last_composition_update = rt;

    if (klass->render)
      klass->render (overlay, rmeta, &cairo_ctx);

    gst_analytics_overlay_destroy_cairo_ctx (&cairo_ctx);

    gst_buffer_unmap (priv->canvas, &map);

    rectangle =
        gst_video_overlay_rectangle_new_raw (priv->canvas, 0, 0,
        GST_VIDEO_INFO_WIDTH (priv->in_info),
        GST_VIDEO_INFO_HEIGHT (priv->in_info),
        GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
    gst_video_overlay_composition_add_rectangle (priv->composition, rectangle);
    gst_video_overlay_rectangle_unref (rectangle);

  } else {
    if (rt != GST_CLOCK_TIME_NONE &&
        priv->expire_overlay != GST_CLOCK_TIME_NONE &&
        priv->last_composition_update != GST_CLOCK_TIME_NONE &&
        priv->composition &&
        priv->last_composition_update + priv->expire_overlay <= rt) {
      gst_video_overlay_composition_unref (priv->composition);
      priv->composition = NULL;
    }
  }

  if (priv->composition) {
    GST_DEBUG_OBJECT (filter, "have composition");

    if (priv->attach_compo_to_buffer) {
      GST_DEBUG_OBJECT (filter, "attach");
      gst_buffer_add_video_overlay_composition_meta (frame->buffer,
          priv->composition);
    } else {
      gst_video_overlay_composition_blend (priv->composition, frame);
    }
  }

  return GST_FLOW_OK;
}

static void
gst_analytics_overlay_init (GstAnalyticsOverlay * overlay)
{
  GstAnalyticsOverlayPrivate *priv =
      GST_ANALYTICS_OVERLAY_GET_PRIVATE (overlay);

  priv->in_info = &GST_VIDEO_FILTER (overlay)->in_info;
  priv->attach_compo_to_buffer = TRUE;
  priv->canvas = NULL;
  priv->composition = NULL;
  priv->flushing = FALSE;
  priv->eos = FALSE;
  priv->expire_overlay = DEFAULT_EXPIRE_OVERLAY;
  priv->last_composition_update = GST_CLOCK_TIME_NONE;
  priv->pango_context = NULL;
  priv->pango_layout = NULL;

  g_mutex_init (&priv->stream_event_mutex);
}

static void
gst_analytics_overlay_class_init (GstAnalyticsOverlayClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *videofilter_class = (GstVideoFilterClass *) klass;

  GST_DEBUG_CATEGORY_INIT (analyticsoverlay_debug, "analyticsoverlay", 0,
      "Analytics overlay base class");

  gobject_class->set_property = gst_analytics_overlay_set_property;
  gobject_class->get_property = gst_analytics_overlay_get_property;
  gobject_class->finalize = gst_analytics_overlay_finalize;

  /**
   * GstAnalyticsOverlay:expire-overlay
   *
   * Re-uses the last overlay for the specified amount of time before
   * expiring it (in ns), MAX for never
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_EXPIRE_OVERLAY,
      g_param_spec_uint64 ("expire-overlay", "Expire overlay",
          "Re-uses the last overlay for the specified amount of time before"
          " expiring it (in ns), MAX for never",
          0, GST_CLOCK_TIME_NONE, DEFAULT_EXPIRE_OVERLAY,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  basetransform_class->passthrough_on_same_caps = FALSE;
  basetransform_class->start = GST_DEBUG_FUNCPTR (gst_analytics_overlay_start);
  basetransform_class->stop = GST_DEBUG_FUNCPTR (gst_analytics_overlay_stop);
  basetransform_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_analytics_overlay_sink_event);

  videofilter_class->set_info =
      GST_DEBUG_FUNCPTR (gst_analytics_overlay_set_info);
  videofilter_class->transform_frame_ip =
      GST_DEBUG_FUNCPTR (gst_analytics_overlay_transform_frame_ip);

  /* Subclasses must implement `render()` */
  klass->render = NULL;
  /* get_font_size() is optional, NULL means use DEFAULT_FONT_SIZE */
  klass->get_font_size = NULL;

  gst_type_mark_as_plugin_api (GST_TYPE_ANALYTICS_OVERLAY, 0);
}
