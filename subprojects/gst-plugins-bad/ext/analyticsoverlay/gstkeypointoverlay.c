/* GStreamer keypoint overlay
 * Copyright (C) <2026> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstkeypointoverlay.c
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
 * SECTION:element-keypointoverlay
 * @title: keypointoverlay
 * @see_also: #GstKeypointOverlay
 *
 * This element creates a graphical representation of the analytics keypoint
 * metadata attached to video streams and overlays graphics above the video.
 *
 * The keypoint overlay element monitors video streams for
 * @GstAnalyticsRelationMeta and queries @GstAnalyticsKeypointMtd. Retrieved
 * keypoints are then used to generate an overlay highlighting detected keypoints.
 *
 * Keypoints are rendered as circles with optional confidence labels.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 ... ! keypointoverlay keypoint-color=0xFFFF0000 keypoint-radius=5.0 ! ...
 * ]| This pipeline creates an overlay representing keypoint detection results.
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstkeypointoverlay.h"

struct _GstKeypointOverlay
{
  GstAnalyticsOverlay parent;

  /* properties */
  guint keypoint_color;
  gdouble keypoint_radius;
  gboolean draw_labels;
  guint labels_color;
  gdouble labels_stroke_width;
  gdouble labels_outline_ofs;
  gboolean draw_skeleton;
  guint skeleton_color;
  gdouble skeleton_line_width;
  gchar *semantic_tag;
};

GST_DEBUG_CATEGORY_STATIC (keypointoverlay_debug);
#define GST_CAT_DEFAULT keypointoverlay_debug

enum
{
  PROP_KEYPOINT_COLOR = 1,
  PROP_KEYPOINT_RADIUS,
  PROP_DRAW_LABELS,
  PROP_LABELS_COLOR,
  PROP_DRAW_SKELETON,
  PROP_SKELETON_COLOR,
  PROP_SKELETON_LINE_WIDTH,
  PROP_SEMANTIC_TAG,
  _PROP_COUNT
};

#define VIDEO_FORMATS GST_VIDEO_OVERLAY_COMPOSITION_BLEND_FORMATS
#define KEYPOINT_OVERLAY_CAPS GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (KEYPOINT_OVERLAY_CAPS)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (KEYPOINT_OVERLAY_CAPS)
    );

G_DEFINE_TYPE (GstKeypointOverlay, gst_keypoint_overlay,
    GST_TYPE_ANALYTICS_OVERLAY);

#define parent_class gst_keypoint_overlay_parent_class

GST_ELEMENT_REGISTER_DEFINE (keypointoverlay, "keypointoverlay",
    GST_RANK_NONE, GST_TYPE_KEYPOINT_OVERLAY);

static void gst_keypoint_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_keypoint_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_keypoint_overlay_start (GstBaseTransform * trans);

static void gst_keypoint_overlay_finalize (GObject * object);

static gint gst_keypoint_overlay_get_font_size (GstAnalyticsOverlay * self);

static void gst_keypoint_overlay_render (GstAnalyticsOverlay * self,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsOverlayCairoCtx * ctx);

static void
gst_keypoint_overlay_render_keypoint (GstKeypointOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * cairo_ctx, gint x, gint y, gfloat confidence);

static void
gst_keypoint_overlay_render_label (GstKeypointOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * cairo_ctx, gint x, gint y, gfloat confidence);

static void
gst_keypoint_overlay_render_skeleton (GstKeypointOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * cairo_ctx,
    gint x1, gint y1, gint x2, gint y2);

static void
gst_keypoint_overlay_class_init (GstKeypointOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *basetransform_class;
  GstAnalyticsOverlayClass *overlay_class;

  GST_DEBUG_CATEGORY_INIT (keypointoverlay_debug,
      "analytics_overlay_keypoint", 0, "Keypoint overlay");

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_keypoint_overlay_set_property;
  gobject_class->get_property = gst_keypoint_overlay_get_property;
  gobject_class->finalize = gst_keypoint_overlay_finalize;

  /**
   * GstKeypointOverlay:keypoint-color
   *
   * Keypoint circle color
   * ARGB format (ex. 0xFFFF0000 for red)
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_KEYPOINT_COLOR,
      g_param_spec_uint ("keypoint-color",
          "Keypoint color",
          "Color (ARGB) to use for keypoint circles",
          0, G_MAXUINT, 0xFFFF0000,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstKeypointOverlay:keypoint-radius
   *
   * Keypoint circle radius in pixels
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_KEYPOINT_RADIUS,
      g_param_spec_double ("keypoint-radius",
          "Keypoint radius",
          "Radius of keypoint circles in pixels",
          1.0, 20.0, 3.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstKeypointOverlay:draw-labels
   *
   * Control confidence labels drawing
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_DRAW_LABELS,
      g_param_spec_boolean ("draw-labels",
          "Draw labels",
          "Draw confidence value labels for keypoints",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstKeypointOverlay:labels-color
   *
   * Control labels color
   * Format ARGB (ex. 0xFFFF0000 for red)
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_LABELS_COLOR,
      g_param_spec_uint ("labels-color",
          "Labels color",
          "Color (ARGB) to use for confidence labels",
          0, G_MAXUINT, 0xFFFFFFFF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstKeypointOverlay:draw-skeleton
   *
   * Draw skeleton connections between keypoints in groups
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_DRAW_SKELETON,
      g_param_spec_boolean ("draw-skeleton",
          "Draw skeleton",
          "Draw skeleton connections between keypoints",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstKeypointOverlay:skeleton-color
   *
   * Skeleton line color
   * ARGB format (ex. 0xFF00FF00 for green)
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_SKELETON_COLOR,
      g_param_spec_uint ("skeleton-color",
          "Skeleton color",
          "Color (ARGB) to use for skeleton lines",
          0, G_MAXUINT, 0xFF00FF00,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstKeypointOverlay:skeleton-line-width
   *
   * Skeleton line width in pixels
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_SKELETON_LINE_WIDTH,
      g_param_spec_double ("skeleton-line-width",
          "Skeleton line width",
          "Width of skeleton lines in pixels",
          1.0, 10.0, 2.0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstKeypointOverlay:semantic-tag
   *
   * Filter keypoint groups by semantic tag prefix
   * When set, only draws groups whose semantic tag starts with this value
   * (ex. "hand-21-kps" matches "hand-21-kps-left" and "hand-21-kps-right")
   * When NULL (default), draws all keypoints individually without grouping
   *
   * Since: 1.30
   */
  g_object_class_install_property (gobject_class, PROP_SEMANTIC_TAG,
      g_param_spec_string ("semantic-tag",
          "Semantic tag",
          "Filter groups by semantic tag prefix (NULL = draw all keypoints individually)",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class,
      "Keypoint Overlay",
      "Analyzer/Visualization/Video",
      "Overlay a visual representation of keypoint analytics metadata on video",
      "Daniel Morin");

  basetransform_class = (GstBaseTransformClass *) klass;
  basetransform_class->start = GST_DEBUG_FUNCPTR (gst_keypoint_overlay_start);

  overlay_class = GST_ANALYTICS_OVERLAY_CLASS (klass);
  overlay_class->get_font_size = gst_keypoint_overlay_get_font_size;
  overlay_class->render = gst_keypoint_overlay_render;
}

static void
gst_keypoint_overlay_finalize (GObject * object)
{
  GstKeypointOverlay *overlay = GST_KEYPOINT_OVERLAY (object);

  g_free (overlay->semantic_tag);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_keypoint_overlay_init (GstKeypointOverlay * overlay)
{
  overlay->keypoint_color = 0xFFFF0000;
  overlay->keypoint_radius = 3.0;
  overlay->draw_labels = TRUE;
  overlay->labels_color = 0xFFFFFFFF;
  overlay->draw_skeleton = FALSE;
  overlay->skeleton_color = 0xFF00FF00;
  overlay->skeleton_line_width = 2.0;
  overlay->semantic_tag = NULL;
  overlay->labels_stroke_width = 1.0;

  GST_DEBUG_CATEGORY_INIT (keypointoverlay_debug,
      "analytics_overlay_keypoint", 0, "Keypoint overlay");
}

static void
gst_keypoint_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstKeypointOverlay *overlay;
  overlay = GST_KEYPOINT_OVERLAY (object);

  switch (prop_id) {
    case PROP_KEYPOINT_COLOR:
      overlay->keypoint_color = g_value_get_uint (value);
      break;
    case PROP_KEYPOINT_RADIUS:
      overlay->keypoint_radius = g_value_get_double (value);
      break;
    case PROP_DRAW_LABELS:
      overlay->draw_labels = g_value_get_boolean (value);
      break;
    case PROP_LABELS_COLOR:
      overlay->labels_color = g_value_get_uint (value);
      break;
    case PROP_DRAW_SKELETON:
      overlay->draw_skeleton = g_value_get_boolean (value);
      break;
    case PROP_SKELETON_COLOR:
      overlay->skeleton_color = g_value_get_uint (value);
      break;
    case PROP_SKELETON_LINE_WIDTH:
      overlay->skeleton_line_width = g_value_get_double (value);
      break;
    case PROP_SEMANTIC_TAG:
      g_free (overlay->semantic_tag);
      overlay->semantic_tag = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_keypoint_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstKeypointOverlay *kp_overlay = GST_KEYPOINT_OVERLAY (object);

  switch (prop_id) {
    case PROP_KEYPOINT_COLOR:
      g_value_set_uint (value, kp_overlay->keypoint_color);
      break;
    case PROP_KEYPOINT_RADIUS:
      g_value_set_double (value, kp_overlay->keypoint_radius);
      break;
    case PROP_DRAW_LABELS:
      g_value_set_boolean (value, kp_overlay->draw_labels);
      break;
    case PROP_LABELS_COLOR:
      g_value_set_uint (value, kp_overlay->labels_color);
      break;
    case PROP_DRAW_SKELETON:
      g_value_set_boolean (value, kp_overlay->draw_skeleton);
      break;
    case PROP_SKELETON_COLOR:
      g_value_set_uint (value, kp_overlay->skeleton_color);
      break;
    case PROP_SKELETON_LINE_WIDTH:
      g_value_set_double (value, kp_overlay->skeleton_line_width);
      break;
    case PROP_SEMANTIC_TAG:
      g_value_set_string (value, kp_overlay->semantic_tag);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gint
gst_keypoint_overlay_get_font_size (GstAnalyticsOverlay * self)
{
  return 8000;
}

static gboolean
gst_keypoint_overlay_start (GstBaseTransform * trans)
{
  GstKeypointOverlay *self = GST_KEYPOINT_OVERLAY (trans);
  PangoFontDescription *desc;

  if (!GST_BASE_TRANSFORM_CLASS (parent_class)->start (trans))
    return FALSE;

  desc =
      pango_context_get_font_description
      (gst_analytics_overlay_get_pango_context (GST_ANALYTICS_OVERLAY (self)));
  self->labels_outline_ofs = gst_analytics_overlay_compute_label_ofs (desc);
  GST_DEBUG_OBJECT (self, "labels_outline_offset %f", self->labels_outline_ofs);

  return TRUE;
}

static void
gst_keypoint_overlay_render_keypoint (GstKeypointOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * cairo_ctx, gint x, gint y, gfloat confidence)
{
  GstVideoInfo *in_info =
      gst_analytics_overlay_get_in_info (GST_ANALYTICS_OVERLAY (overlay));
  guint32 color = overlay->keypoint_color;

  /* Only draw keypoint that is inside the video */
  gint maxw = GST_VIDEO_INFO_WIDTH (in_info) - 1;
  gint maxh = GST_VIDEO_INFO_HEIGHT (in_info) - 1;

  if (x < 0 || y < 0 || x > maxw || y > maxh) {
    GST_TRACE_OBJECT (overlay,
        "keypoint (%d,%d) is outside the frame and not rendered.", x, y);
    return;
  }

  /* Set color: convert ARGB to RGBA doubles (0.0-1.0) */
  cairo_set_source_rgba (cairo_ctx->cr, ((color >> 16) & 0xFF) / 255.0, /* Red */
      ((color >> 8) & 0xFF) / 255.0,    /* Green */
      (color & 0xFF) / 255.0,   /* Blue */
      ((color >> 24) & 0xFF) / 255.0);  /* Alpha */

  /* Draw filled circle */
  cairo_arc (cairo_ctx->cr, x, y, overlay->keypoint_radius, 0, 2 * G_PI);
  cairo_fill (cairo_ctx->cr);

  /* Draw optional confidence label */
  if (overlay->draw_labels) {
    gst_keypoint_overlay_render_label (overlay, cairo_ctx, x, y, confidence);
  }
}

static void
gst_keypoint_overlay_render_label (GstKeypointOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * cairo_ctx, gint x, gint y, gfloat confidence)
{
  PangoLayout *pango_layout =
      gst_analytics_overlay_get_pango_layout (GST_ANALYTICS_OVERLAY (overlay));
  PangoRectangle ink_rect, logical_rect;
  gchar *label;
  guint32 label_color = overlay->labels_color;

  cairo_save (cairo_ctx->cr);

  /* Format confidence as "0.95" */
  label = g_strdup_printf ("%.2f", confidence);

  /* Set text color: convert ARGB to RGBA doubles */
  cairo_set_source_rgba (cairo_ctx->cr,
      ((label_color >> 16) & 0xFF) / 255.0,
      ((label_color >> 8) & 0xFF) / 255.0,
      (label_color & 0xFF) / 255.0, ((label_color >> 24) & 0xFF) / 255.0);

  cairo_set_line_width (cairo_ctx->cr, overlay->labels_stroke_width);

  /* Layout text */
  pango_layout_set_markup (pango_layout, label, strlen (label));
  pango_layout_get_pixel_extents (pango_layout, &ink_rect, &logical_rect);

  /* Position label to the right of keypoint, vertically centered */
  cairo_move_to (cairo_ctx->cr,
      x + overlay->keypoint_radius + overlay->labels_outline_ofs,
      y - (gdouble) logical_rect.height / 2.0);

  /* Render as stroked outline */
  pango_cairo_layout_path (cairo_ctx->cr, pango_layout);
  cairo_stroke (cairo_ctx->cr);

  g_free (label);
  cairo_restore (cairo_ctx->cr);
}

static void
gst_keypoint_overlay_render_skeleton (GstKeypointOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * cairo_ctx, gint x1, gint y1, gint x2, gint y2)
{
  GstVideoInfo *in_info =
      gst_analytics_overlay_get_in_info (GST_ANALYTICS_OVERLAY (overlay));
  guint32 color = overlay->skeleton_color;

  /* Clamp coordinates to video bounds */
  gint maxw = GST_VIDEO_INFO_WIDTH (in_info) - 1;
  gint maxh = GST_VIDEO_INFO_HEIGHT (in_info) - 1;
  x1 = CLAMP (x1, 0, maxw);
  y1 = CLAMP (y1, 0, maxh);
  x2 = CLAMP (x2, 0, maxw);
  y2 = CLAMP (y2, 0, maxh);

  /* Set color: convert ARGB to RGBA doubles (0.0-1.0) */
  cairo_set_source_rgba (cairo_ctx->cr, ((color >> 16) & 0xFF) / 255.0, /* Red */
      ((color >> 8) & 0xFF) / 255.0,    /* Green */
      (color & 0xFF) / 255.0,   /* Blue */
      ((color >> 24) & 0xFF) / 255.0);  /* Alpha */

  cairo_set_line_width (cairo_ctx->cr, overlay->skeleton_line_width);

  /* Draw line */
  cairo_move_to (cairo_ctx->cr, x1, y1);
  cairo_line_to (cairo_ctx->cr, x2, y2);
  cairo_stroke (cairo_ctx->cr);
}

static void
gst_keypoint_overlay_render (GstAnalyticsOverlay * self,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsOverlayCairoCtx * ctx)
{
  GstKeypointOverlay *overlay = GST_KEYPOINT_OVERLAY (self);
  gpointer state = NULL;
  GstAnalyticsMtd rlt_mtd;
  GstAnalyticsKeypointMtd *kp_mtd;
  gint x, y, z;
  gfloat confidence;
  GstAnalyticsKeypointDimensions dimension;
  gint keypoint_count = 0;

  GST_INFO_OBJECT (overlay,
      "semantic_tag=%s, draw_skeleton=%d",
      overlay->semantic_tag ? overlay->semantic_tag : "(null)",
      overlay->draw_skeleton);

  /* Choose rendering mode based on semantic_tag property */
  if (overlay->semantic_tag) {
    /* Mode 1: Iterate groups, filter by semantic tag, draw keypoints + skeleton */
    GST_DEBUG_OBJECT (overlay,
        "Rendering groups with semantic tag prefix: '%s'",
        overlay->semantic_tag);

    GstAnalyticsMtdType group_type = gst_analytics_group_mtd_get_mtd_type ();
    gint group_count = 0;

    GST_DEBUG_OBJECT (overlay,
        "Got group type: %p, starting iteration", (void *) group_type);

    while (gst_analytics_relation_meta_iterate (rmeta, &state, group_type,
            &rlt_mtd)) {

      GST_DEBUG_OBJECT (overlay,
          "Iterator returned: id=%u, meta=%p", rlt_mtd.id, rlt_mtd.meta);

      /* Validate group metadata */
      if (!rlt_mtd.meta) {
        GST_WARNING_OBJECT (overlay, "Group has NULL meta, skipping");
        continue;
      }

      GstAnalyticsGroupMtd *group_mtd = (GstAnalyticsGroupMtd *) & rlt_mtd;

      GST_DEBUG_OBJECT (overlay, "Getting semantic tag for group");

      /* Filter by semantic tag prefix */
      if (!gst_analytics_group_mtd_semantic_tag_has_prefix (group_mtd,
              overlay->semantic_tag)) {
        GST_TRACE_OBJECT (overlay,
            "Skipping group not matching prefix '%s'", overlay->semantic_tag);
        continue;
      }

      group_count++;
      GST_DEBUG_OBJECT (overlay, "Processing group #%d with tag '%s'",
          group_count, overlay->semantic_tag);

      /* Get all keypoints in the group */
      gsize member_count = gst_analytics_group_mtd_get_member_count (group_mtd);
      GST_DEBUG_OBJECT (overlay, "Group has %zu members", member_count);

      /* Iterate and render keypoints in group */
      for (gsize i = 0; i < member_count; i++) {
        GstAnalyticsMtd member = { 0 };

        if (!gst_analytics_group_mtd_get_member (group_mtd, i, &member)) {
          GST_WARNING_OBJECT (overlay, "Failed to get group member %zu", i);
          continue;
        }

        /* Check if member is a keypoint */
        if (gst_analytics_mtd_get_mtd_type (&member) !=
            gst_analytics_keypoint_mtd_get_mtd_type ()) {
          continue;
        }

        kp_mtd = (GstAnalyticsKeypointMtd *) & member;

        /* Extract position and confidence */
        if (!gst_analytics_keypoint_mtd_get_position (kp_mtd, &x, &y, &z,
                &dimension)) {
          GST_WARNING_OBJECT (overlay,
              "Failed to get keypoint position for member %zu", i);
          continue;
        }

        if (!gst_analytics_keypoint_mtd_get_confidence (kp_mtd, &confidence)) {
          confidence = 1.0f;
        }

        keypoint_count++;
        GST_TRACE_OBJECT (overlay,
            "Keypoint #%d at (%d, %d) confidence %.2f", keypoint_count, x, y,
            confidence);

        /* Render keypoint */
        gst_keypoint_overlay_render_keypoint (overlay, ctx, x, y, confidence);
      }

      /* Draw skeleton connections if enabled */
      if (overlay->draw_skeleton) {
        GST_DEBUG_OBJECT (overlay, "Drawing skeleton for group");

        /* Iterate through all keypoints to find skeleton relations */
        for (gsize i = 0; i < member_count; i++) {
          GstAnalyticsMtd member_i = { 0 };
          GstAnalyticsKeypointMtd *kp_i;
          gint x1, y1, z1;
          GstAnalyticsKeypointDimensions dim1;

          /* Get keypoint i */
          if (!gst_analytics_group_mtd_get_member (group_mtd, i, &member_i)) {
            continue;
          }

          if (gst_analytics_mtd_get_mtd_type (&member_i) !=
              gst_analytics_keypoint_mtd_get_mtd_type ()) {
            continue;
          }

          kp_i = (GstAnalyticsKeypointMtd *) & member_i;
          if (!gst_analytics_keypoint_mtd_get_position (kp_i, &x1, &y1, &z1,
                  &dim1)) {
            continue;
          }

          /* Find all keypoints related to this one */
          gpointer relation_state = NULL;
          GstAnalyticsMtd related = { 0 };

          while (gst_analytics_relation_meta_get_direct_related (rmeta,
                  member_i.id, GST_ANALYTICS_REL_TYPE_RELATE_TO,
                  gst_analytics_keypoint_mtd_get_mtd_type (),
                  &relation_state, &related)) {

            GstAnalyticsKeypointMtd *kp_related =
                (GstAnalyticsKeypointMtd *) & related;
            gint x2, y2, z2;
            GstAnalyticsKeypointDimensions dim2;

            /* Get position of related keypoint */
            if (gst_analytics_keypoint_mtd_get_position (kp_related, &x2, &y2,
                    &z2, &dim2)) {
              /* Draw skeleton line (only draw once by checking id order) */
              if (member_i.id < related.id) {
                gst_keypoint_overlay_render_skeleton (overlay, ctx, x1,
                    y1, x2, y2);
                GST_TRACE_OBJECT (overlay,
                    "Drew skeleton line between keypoint %u and %u",
                    member_i.id, related.id);
              }
            }
          }
        }
      }
    }

    GST_INFO_OBJECT (overlay,
        "Found %d groups with %d total keypoints in buffer", group_count,
        keypoint_count);
  } else {
    /* Mode 2: Iterate all keypoints directly (not through groups) */
    GstAnalyticsMtdType keypoint_type =
        gst_analytics_keypoint_mtd_get_mtd_type ();
    GST_DEBUG_OBJECT (overlay,
        "Rendering all keypoints individually (no semantic tag filter)");

    while (gst_analytics_relation_meta_iterate (rmeta, &state, keypoint_type,
            &rlt_mtd)) {
      kp_mtd = (GstAnalyticsKeypointMtd *) & rlt_mtd;

      /* Extract position and confidence */
      if (!gst_analytics_keypoint_mtd_get_position (kp_mtd, &x, &y, &z,
              &dimension)) {
        GST_WARNING_OBJECT (overlay, "Failed to get keypoint position");
        continue;
      }

      if (!gst_analytics_keypoint_mtd_get_confidence (kp_mtd, &confidence)) {
        GST_DEBUG_OBJECT (overlay, "Using default confidence 1.0");
        confidence = 1.0f;
      }

      keypoint_count++;
      GST_TRACE_OBJECT (overlay,
          "Keypoint #%d at (%d, %d) with confidence %.2f", keypoint_count, x,
          y, confidence);

      /* Render keypoint (uses only x, y) */
      gst_keypoint_overlay_render_keypoint (overlay, ctx, x, y, confidence);
    }

    GST_INFO_OBJECT (overlay, "Found %d keypoints in buffer", keypoint_count);
  }
}
