/* GStreamer object detection overlay
 * Copyright (C) <2023> Collabora Ltd.
 *  @author: Aaron Boxer <aaron.boxer@collabora.com>
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstobjectdetectionoverlay.c
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
 * SECTION:element-objectdetectionoverlay
 * @title: objectdetectionoverlay
 * @see_also: #GstObjectDetectionOverlay
 *
 * This element create a graphical representation of the analytics object
 * detection metadata attached to video stream and overlay graphics above the
 * video.
 *
 * The object detection overlay element monitor video stream for
 * @GstAnalyticsRelationMeta and query @GstAnalyticsODMtd. Retrieved
 * @GstAnalyticsODMtd are then used to generate an overlay highlighing objects
 * detected.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 multifilesrc location=/onnx-models/images/bus.jpg ! jpegdec ! videoconvert ! onnxinference execution-provider=cpu model-file=/onnx-models/models/ssd_mobilenet_v1_coco.onnx ! ssdobjectdetector label-file=/onnx-models/labels/COCO_classes.txt ! objectdetectionoverlay object-detection-outline-color=0xFF0000FF draw-labels=true ! videoconvertscale ! imagefreeze ! autovideosink
 * ]| This pipeline create an overlay representing results of an object detetion
 * analysis.
 *
 * Since: 1.24
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <math.h>

#include "gstobjectdetectionoverlay.h"

struct _GstObjectDetectionOverlay
{
  GstAnalyticsOverlay parent;

  /* properties */
  guint od_outline_color;
  guint od_outline_stroke_width;
  gboolean draw_labels;
  gboolean draw_tracking_labels;
  gboolean filled_box;
  guint labels_color;
  gdouble labels_stroke_width;
  gdouble labels_outline_ofs;
  gboolean tracking_outline_colors;
};

#define ROTATION_EPSILON 0.001f /* Threshold to consider there's a rotation */

GST_DEBUG_CATEGORY_STATIC (objectdetectionoverlay_debug);
#define GST_CAT_DEFAULT objectdetectionoverlay_debug

enum
{
  PROP_OD_OUTLINE_COLOR = 1,
  PROP_DRAW_LABELS,
  PROP_DRAW_TRACKING_LABELS,
  PROP_LABELS_COLOR,
  PROP_FILLED_BOX,
  PROP_TRACKING_OUTLINE_COLORS,
  _PROP_COUNT
};

#define VIDEO_FORMATS GST_VIDEO_OVERLAY_COMPOSITION_BLEND_FORMATS
#define OBJECT_DETECTION_OVERLAY_CAPS GST_VIDEO_CAPS_MAKE (VIDEO_FORMATS)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (OBJECT_DETECTION_OVERLAY_CAPS)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (OBJECT_DETECTION_OVERLAY_CAPS)
    );

G_DEFINE_TYPE (GstObjectDetectionOverlay,
    gst_object_detection_overlay, GST_TYPE_ANALYTICS_OVERLAY);

#define parent_class gst_object_detection_overlay_parent_class

GST_ELEMENT_REGISTER_DEFINE (objectdetectionoverlay, "objectdetectionoverlay",
    GST_RANK_NONE, GST_TYPE_OBJECT_DETECTION_OVERLAY);

static void gst_object_detection_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_object_detection_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_object_detection_overlay_start (GstBaseTransform * trans);

static void gst_object_detection_overlay_finalize (GObject * object);

static void
gst_object_detection_overlay_render (GstAnalyticsOverlay * self,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsOverlayCairoCtx * ctx);

static void
gst_object_detection_overlay_render_boundingbox (GstObjectDetectionOverlay
    * overlay, GstAnalyticsOverlayCairoCtx * cairo_ctx,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsODMtd * od_mtd);

static void
gst_object_detection_overlay_render_text_annotation (GstObjectDetectionOverlay
    * overlay, GstAnalyticsOverlayCairoCtx * cairo_ctx,
    GstAnalyticsODMtd * od_mtd, const gchar * annotation);

static void
    gst_object_detection_overlay_render_tracking_text_annotation
    (GstObjectDetectionOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * ctx,
    GstAnalyticsRelationMeta * rmeta, const GstAnalyticsODMtd * od_mtd);


static void
gst_object_detection_overlay_class_init (GstObjectDetectionOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *basetransform_class;
  GstAnalyticsOverlayClass *overlay_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_object_detection_overlay_set_property;
  gobject_class->get_property = gst_object_detection_overlay_get_property;
  gobject_class->finalize = gst_object_detection_overlay_finalize;

  /**
   * GstObjectDetectionOverlay:object-detection-outline-color
   *
   * Object Detetion Overlay outline color
   * ARGB format (ex. 0xFFFF0000 for red)
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class, PROP_OD_OUTLINE_COLOR,
      g_param_spec_uint ("object-detection-outline-color",
          "Object detection outline color",
          "Color (ARGB) to use for object detection overlay outline",
          0, G_MAXUINT, 0xFFFFFFFF,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstObjectDetectionOverlay:draw-labels
   *
   * Control labels drawing
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class, PROP_DRAW_LABELS,
      g_param_spec_boolean ("draw-labels",
          "Draw labels",
          "Draw object labels",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstObjectDetectionOverlay:draw-tracking-labels
   *
   * Control tracking labels drawing
   *
   * Since: 1.28
   */
  g_object_class_install_property (gobject_class, PROP_DRAW_TRACKING_LABELS,
      g_param_spec_boolean ("draw-tracking-labels",
          "Draw tracking labels",
          "Draw object tracking labels",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstObjectDetectionOverlay:labels-color
   *
   * Control labels color
   * Format ARGB (ex. 0xFFFF0000 for red)
   *
   * Since: 1.24
   */
  g_object_class_install_property (gobject_class, PROP_LABELS_COLOR,
      g_param_spec_uint ("labels-color",
          "Labels color",
          "Color (ARGB) to use for object labels",
          0, G_MAXUINT, 0xFFFFFF, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   /**
   * GstObjectDetectionOverlay:filled-box
   *
   * Draw filled-box in the region where the object is detected is masked.
   * Filling color will be based on object-detection-outline-color.
   *
   * Since: 1.28
   */
  g_object_class_install_property (gobject_class, PROP_FILLED_BOX,
      g_param_spec_boolean ("filled-box",
          "Filled box",
          "Draw a filled box",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

   /**
   * GstObjectDetectionOverlay:tracking-outline-colors
   *
   * In the presence of tracking information, each object will get its
   * own color, ignores object-detection-outline-color
   *
   * Since: 1.28
   */
  g_object_class_install_property (gobject_class, PROP_TRACKING_OUTLINE_COLORS,
      g_param_spec_boolean ("tracking-outline-colors",
          "Tracking outline colors",
          "In the presence of tracking information, each object will get"
          " its own color, ignores object-detection-outline-color",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class = (GstElementClass *) klass;

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
  gst_element_class_set_static_metadata (element_class,
      "Object Detection Overlay",
      "Analyzer/Visualization/Video",
      "Overlay a visual representation of analytics metadata on the video",
      "Daniel Morin");

  basetransform_class = (GstBaseTransformClass *) klass;
  basetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_object_detection_overlay_start);

  overlay_class = GST_ANALYTICS_OVERLAY_CLASS (klass);
  overlay_class->render = gst_object_detection_overlay_render;
  /* get_font_size not overridden: OD uses the default 10000 */
}

static void
gst_object_detection_overlay_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_object_detection_overlay_init (GstObjectDetectionOverlay * overlay)
{
  overlay->od_outline_color = 0xFFFFFFFF;
  overlay->draw_labels = TRUE;
  overlay->draw_tracking_labels = TRUE;
  overlay->labels_color = 0xFFFFFFFF;
  overlay->filled_box = FALSE;
  overlay->labels_stroke_width = 1.0;
  overlay->od_outline_stroke_width = 2;
  overlay->tracking_outline_colors = TRUE;

  GST_DEBUG_CATEGORY_INIT (objectdetectionoverlay_debug,
      "analytics_overlay_od", 0, "Object detection overlay");
}

static void
gst_object_detection_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstObjectDetectionOverlay *overlay;
  overlay = GST_OBJECT_DETECTION_OVERLAY (object);

  switch (prop_id) {
    case PROP_OD_OUTLINE_COLOR:
      overlay->od_outline_color = g_value_get_uint (value);
      break;
    case PROP_DRAW_LABELS:
      overlay->draw_labels = g_value_get_boolean (value);
      break;
    case PROP_DRAW_TRACKING_LABELS:
      overlay->draw_tracking_labels = g_value_get_boolean (value);
      break;
    case PROP_LABELS_COLOR:
      overlay->labels_color = g_value_get_uint (value);
      break;
    case PROP_FILLED_BOX:
      overlay->filled_box = g_value_get_boolean (value);
      break;
    case PROP_TRACKING_OUTLINE_COLORS:
      overlay->tracking_outline_colors = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_object_detection_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstObjectDetectionOverlay *od_overlay = GST_OBJECT_DETECTION_OVERLAY (object);

  switch (prop_id) {
    case PROP_OD_OUTLINE_COLOR:
      g_value_set_uint (value, od_overlay->od_outline_color);
      break;
    case PROP_DRAW_LABELS:
      g_value_set_boolean (value, od_overlay->draw_labels);
      break;
    case PROP_DRAW_TRACKING_LABELS:
      g_value_set_boolean (value, od_overlay->draw_tracking_labels);
      break;
    case PROP_LABELS_COLOR:
      g_value_set_uint (value, od_overlay->labels_color);
      break;
    case PROP_FILLED_BOX:
      g_value_set_boolean (value, od_overlay->filled_box);
      break;
    case PROP_TRACKING_OUTLINE_COLORS:
      g_value_set_boolean (value, od_overlay->tracking_outline_colors);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_object_detection_overlay_start (GstBaseTransform * trans)
{
  GstObjectDetectionOverlay *self = GST_OBJECT_DETECTION_OVERLAY (trans);
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

/*
 * HSV version using golden angle distribution around the color wheel.
 * This ensures maximum color separation by dividing the color wheel optimally.
 * Sequence: 0°, 180°, 90°, 270°, 45°, 225°, 135°, 315°, etc.
 *
 * Returns RGB color as uint32_t in format 0x00RRGGBB
 */
static guint32
generate_track_color_hsv (guint32 track_id)
{
  gfloat h = 0.0f;
  gfloat increment = 0.5f;
  /* Fixed saturation and value for consistent appearance */
  const gfloat S = 0.85f;       /* High saturation for vivid colors */
  const gfloat V = 0.95f;       /* High value for bright colors */

  /* Start from 1 to avoid special case */
  track_id++;

  /* Calculate hue using bit-reversal pattern for optimal distribution */
  /* Gives us the sequence: 0, 0.5, 0.25, 0.75, 0.125, 0.625, 0.375, 0.875, .. */
  while (track_id > 1) {
    if (track_id & 1) {
      h += increment;
    }
    track_id >>= 1;
    increment *= 0.5f;
  }

  /* Keep hue in [0, 1) range */
  while (h >= 1.0f)
    h -= 1.0f;

  /* Convert HSV to RGB */
  int hi = (int) (h * 6.0f);
  gfloat f = h * 6.0f - hi;
  gfloat p = V * (1.0f - S);
  gfloat q = V * (1.0f - f * S);
  gfloat t = V * (1.0f - (1.0f - f) * S);

  gfloat r, g, b;
  switch (hi % 6) {
    case 0:
      r = V;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = V;
      b = p;
      break;
    case 2:
      r = p;
      g = V;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = V;
      break;
    case 4:
      r = t;
      g = p;
      b = V;
      break;
    case 5:
      r = V;
      g = p;
      b = q;
      break;
    default:
      r = g = b = 0;
      break;
  }

  guint8 r8 = (guint8) (r * 255.0f);
  guint8 g8 = (guint8) (g * 255.0f);
  guint8 b8 = (guint8) (b * 255.0f);

  return ((guint32) r8 << 16) | ((guint32) g8 << 8) | (guint32) b8 | 0xFF000000;
}

static void
gst_object_detection_overlay_render_boundingbox (GstObjectDetectionOverlay
    * overlay, GstAnalyticsOverlayCairoCtx * ctx,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsODMtd * od_mtd)
{
  GstVideoInfo *in_info =
      gst_analytics_overlay_get_in_info (GST_ANALYTICS_OVERLAY (overlay));
  gint x, y, w, h;
  gfloat r = 0.0f;
  gfloat _dummy;
  gint maxw = GST_VIDEO_INFO_WIDTH (in_info) - 1;
  gint maxh = GST_VIDEO_INFO_HEIGHT (in_info) - 1;
  GstAnalyticsTrackingMtd tracking_mtd;
  guint32 color;

  cairo_save (ctx->cr);
  gst_analytics_od_mtd_get_oriented_location (od_mtd, &x, &y, &w, &h, &r,
      &_dummy);

  x = CLAMP (x, 0, maxw);
  y = CLAMP (y, 0, maxh);
  w = CLAMP (w, 0, maxw - x);
  h = CLAMP (h, 0, maxh - y);

  if (overlay->tracking_outline_colors &&
      gst_analytics_relation_meta_get_direct_related (rmeta, od_mtd->id,
          GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &tracking_mtd)) {
    guint64 tid;

    gst_analytics_tracking_mtd_get_info (&tracking_mtd, &tid, NULL, NULL, NULL);

    color = generate_track_color_hsv (tid & 0xFFFFFFF);
  } else {
    color = overlay->od_outline_color;
  }

  /* Set bounding box stroke color and width */
  cairo_set_source_rgba (ctx->cr,
      ((color >> 16) & 0xFF) / 255.0,
      ((color >> 8) & 0xFF) / 255.0,
      (color & 0xFF) / 255.0, ((color >> 24) & 0xFF) / 255.0);
  cairo_set_line_width (ctx->cr, overlay->od_outline_stroke_width);

  /* draw bounding box */
  if (fabsf (r) < ROTATION_EPSILON) {
    /* Fast path: axis-aligned rectangle */
    cairo_rectangle (ctx->cr, x, y, w, h);
  } else {
    /* Rotated path: calculate 4 corners and draw */
    gfloat xc = x + w / 2.0f;
    gfloat yc = y + h / 2.0f;
    gfloat cos_r = cosf (r);
    gfloat sin_r = sinf (r);

    /* Corner offsets (pre-rotation, relative to center) */
    gfloat corners[4][2] = {
      {-w / 2.0f, -h / 2.0f},   /* top-left */
      {w / 2.0f, -h / 2.0f},    /* top-right */
      {w / 2.0f, h / 2.0f},     /* bottom-right */
      {-w / 2.0f, h / 2.0f}     /* bottom-left */
    };

    /* Draw rotated box */
    for (int i = 0; i < 4; i++) {
      gfloat dx = corners[i][0];
      gfloat dy = corners[i][1];
      gfloat rx = dx * cos_r - dy * sin_r + xc;
      gfloat ry = dx * sin_r + dy * cos_r + yc;

      if (i == 0)
        cairo_move_to (ctx->cr, rx, ry);
      else
        cairo_line_to (ctx->cr, rx, ry);
    }
    cairo_close_path (ctx->cr);
  }

  if (overlay->filled_box == FALSE)
    cairo_stroke (ctx->cr);
  else
    cairo_fill (ctx->cr);

  cairo_restore (ctx->cr);
}

static void
gst_object_detection_overlay_render_text_annotation (GstObjectDetectionOverlay
    * overlay, GstAnalyticsOverlayCairoCtx * ctx,
    GstAnalyticsODMtd * od_mtd, const gchar * annotation)
{
  GstVideoInfo *in_info =
      gst_analytics_overlay_get_in_info (GST_ANALYTICS_OVERLAY (overlay));
  PangoLayout *pango_layout =
      gst_analytics_overlay_get_pango_layout (GST_ANALYTICS_OVERLAY (overlay));
  PangoRectangle ink_rect, logical_rect;
  gint x, y, w, h;
  gfloat _dummy;
  gint maxw = GST_VIDEO_INFO_WIDTH (in_info) - 1;
  gint maxh = GST_VIDEO_INFO_HEIGHT (in_info) - 1;

  cairo_save (ctx->cr);
  gst_analytics_od_mtd_get_location (od_mtd, &x, &y, &w, &h, &_dummy);

  x = CLAMP (x, 0, maxw);
  y = CLAMP (y, 0, maxh);
  w = CLAMP (w, 0, maxw - x);
  h = CLAMP (h, 0, maxh - y);

  /* Set label strokes color and width */
  cairo_set_source_rgba (ctx->cr,
      ((overlay->labels_color >> 16) & 0xFF) / 255.0,
      ((overlay->labels_color >> 8) & 0xFF) / 255.0,
      ((overlay->labels_color) & 0xFF) / 255.0,
      ((overlay->labels_color >> 24) & 0xFF) / 255.0);
  cairo_set_line_width (ctx->cr, overlay->labels_stroke_width);

  pango_layout_set_markup (pango_layout, annotation, strlen (annotation));
  pango_layout_get_pixel_extents (pango_layout, &ink_rect, &logical_rect);
  GST_DEBUG_OBJECT (overlay, "logical_rect:(%d,%d),%dx%d", logical_rect.x,
      logical_rect.y, logical_rect.width, logical_rect.height);
  GST_DEBUG_OBJECT (overlay, "ink_rect:(%d,%d),%dx%d", ink_rect.x, ink_rect.y,
      ink_rect.width, ink_rect.height);
  cairo_move_to (ctx->cr, x + overlay->labels_outline_ofs,
      y - logical_rect.height - overlay->labels_outline_ofs);

  pango_cairo_layout_path (ctx->cr, pango_layout);
  cairo_stroke (ctx->cr);
  cairo_restore (ctx->cr);
}

static void
    gst_object_detection_overlay_render_tracking_text_annotation
    (GstObjectDetectionOverlay * overlay,
    GstAnalyticsOverlayCairoCtx * ctx,
    GstAnalyticsRelationMeta * rmeta, const GstAnalyticsODMtd * od_mtd)
{
  GstVideoInfo *in_info =
      gst_analytics_overlay_get_in_info (GST_ANALYTICS_OVERLAY (overlay));
  PangoLayout *pango_layout =
      gst_analytics_overlay_get_pango_layout (GST_ANALYTICS_OVERLAY (overlay));
  GstAnalyticsMtd tracking_mtd;
  guint64 tid;
  PangoRectangle ink_rect, logical_rect;
  gint x, y, w, h;
  gint maxw = GST_VIDEO_INFO_WIDTH (in_info) - 1;
  gint maxh = GST_VIDEO_INFO_HEIGHT (in_info) - 1;

  gchar *annotation;

  if (!gst_analytics_relation_meta_get_direct_related (rmeta, od_mtd->id,
          GST_ANALYTICS_REL_TYPE_RELATE_TO,
          gst_analytics_tracking_mtd_get_mtd_type (), NULL, &tracking_mtd))
    return;

  gst_analytics_od_mtd_get_location (od_mtd, &x, &y, &w, &h, NULL);
  gst_analytics_tracking_mtd_get_info (&tracking_mtd, &tid, NULL, NULL, NULL);

  cairo_save (ctx->cr);
  x = CLAMP (x, 0, maxw);
  y = CLAMP (y, 0, maxh);
  w = CLAMP (w, 0, maxw - x);
  h = CLAMP (h, 0, maxh - y);

  /* Set label strokes color and width */
  cairo_set_source_rgba (ctx->cr,
      ((overlay->labels_color >> 16) & 0xFF) / 255.0,
      ((overlay->labels_color >> 8) & 0xFF) / 255.0,
      ((overlay->labels_color) & 0xFF) / 255.0,
      ((overlay->labels_color >> 24) & 0xFF) / 255.0);

  cairo_set_line_width (ctx->cr, overlay->labels_stroke_width);

  annotation = g_strdup_printf ("Track: %" G_GUINT64_FORMAT, tid);
  pango_layout_set_markup (pango_layout, annotation, strlen (annotation));
  g_free (annotation);

  pango_layout_get_pixel_extents (pango_layout, &ink_rect, &logical_rect);

  GST_LOG_OBJECT (overlay, "logical_rect:(%d,%d),%dx%d", logical_rect.x,
      logical_rect.y, logical_rect.width, logical_rect.height);
  GST_LOG_OBJECT (overlay, "ink_rect:(%d,%d),%dx%d", ink_rect.x, ink_rect.y,
      ink_rect.width, ink_rect.height);
  cairo_move_to (ctx->cr, x + overlay->labels_outline_ofs,
      y + h - logical_rect.height - overlay->labels_outline_ofs);

  pango_cairo_layout_path (ctx->cr, pango_layout);
  cairo_stroke (ctx->cr);
  cairo_restore (ctx->cr);
}

static void
gst_object_detection_overlay_render (GstAnalyticsOverlay * self,
    GstAnalyticsRelationMeta * rmeta, GstAnalyticsOverlayCairoCtx * ctx)
{
  GstObjectDetectionOverlay *overlay = GST_OBJECT_DETECTION_OVERLAY (self);
  gpointer state = NULL;
  GstAnalyticsMtd rlt_mtd;
  GstAnalyticsODMtd *od_mtd;
  gint x, y, w, h;
  gfloat loc_confi_lvl;
  gboolean success;
  gchar str_buf[5];

  /* Get quark represent object detection metadata type */
  GstAnalyticsMtdType rlt_type = gst_analytics_od_mtd_get_mtd_type ();
  while (gst_analytics_relation_meta_iterate (rmeta, &state, rlt_type,
          &rlt_mtd)) {
    od_mtd = (GstAnalyticsODMtd *) & rlt_mtd;
    GST_DEBUG_OBJECT (overlay, "buffer contain OD mtd");

    /* Quark representing the type of the object detected by OD */
    GQuark od_obj_type = gst_analytics_od_mtd_get_obj_type (od_mtd);

    /* Find classification metadata attached to object detection metadata */
    GstAnalyticsMtd cls_rlt_mtd;
    success = gst_analytics_relation_meta_get_direct_related (rmeta,
        gst_analytics_mtd_get_id (
            (GstAnalyticsMtd *) od_mtd),
        GST_ANALYTICS_REL_TYPE_RELATE_TO,
        gst_analytics_cls_mtd_get_mtd_type (), NULL, &cls_rlt_mtd);

    gst_object_detection_overlay_render_boundingbox (overlay, ctx, rmeta,
        od_mtd);

    if (overlay->draw_labels) {
      if (success) {
        /* Use associated classification analytics-meta */
        g_snprintf (str_buf, sizeof (str_buf), "%04.2f",
            gst_analytics_cls_mtd_get_level (
                (GstAnalyticsClsMtd *) & cls_rlt_mtd, 0));

        od_obj_type = gst_analytics_cls_mtd_get_quark (&cls_rlt_mtd, 0);
      } else {
        /* Use basic class type directly on OD.
         * Here we want the confidence level of the bbox but to retrieve
         * we need to also retrieve the bbox location. */
        gst_analytics_od_mtd_get_location (od_mtd, &x, &y, &w, &h,
            &loc_confi_lvl);
        GST_TRACE_OBJECT (overlay,
            "obj {type: %s loc:[(%u,%u)-(%ux%u)] @ %f}",
            g_quark_to_string (od_obj_type), x, y, w, h, loc_confi_lvl);

        g_snprintf (str_buf, sizeof (str_buf), "%04.2f", loc_confi_lvl);
      }
      gchar *text = g_strdup_printf ("%s (c=%s)",
          g_quark_to_string (od_obj_type), str_buf);

      gst_object_detection_overlay_render_text_annotation (overlay, ctx,
          od_mtd, text);

      g_free (text);
    }

    if (overlay->draw_tracking_labels)
      gst_object_detection_overlay_render_tracking_text_annotation (overlay,
          ctx, rmeta, od_mtd);
  }
}
