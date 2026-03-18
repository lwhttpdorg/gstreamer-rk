/* GStreamer analytics overlay base class
 * Copyright (C) <2026> Collabora Ltd.
 *  @author: Daniel Morin <daniel.morin@collabora.com>
 *
 * gstanalyticsoverlay.h
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

#ifndef __GST_ANALYTICS_OVERLAY_H__
#define __GST_ANALYTICS_OVERLAY_H__

#include <gst/video/gstvideofilter.h>
#include <gst/analytics/analytics.h>
#include <pango/pangocairo.h>

G_BEGIN_DECLS

/*
 * GstAnalyticsOverlayCairoCtx:
 *
 * Shared Cairo context passed to the render().
 */
typedef struct {
  cairo_t          *cr;
  cairo_surface_t  *surface;
  guint8           *data;
  cairo_matrix_t   *cairo_matrix;
} GstAnalyticsOverlayCairoCtx;

#define GST_TYPE_ANALYTICS_OVERLAY \
  (gst_analytics_overlay_get_type())

G_DECLARE_DERIVABLE_TYPE (GstAnalyticsOverlay, gst_analytics_overlay,
    GST, ANALYTICS_OVERLAY, GstVideoFilter)

struct _GstAnalyticsOverlayClass {
  GstVideoFilterClass parent_class;

  /*
   * GstAnalyticsOverlayClass::get_font_size:
   *
   * Return the Pango font size to use when initializing the
   * Pango context in start(). If NULL the default of 10000 is used.
   */
  gint (*get_font_size) (GstAnalyticsOverlay *self);

  /*
   * GstAnalyticsOverlayClass::render:
   *
   * Render metadata-specific graphics into the full-frame Cairo canvas.
   * Called from transform_frame_ip() whenever a GstAnalyticsRelationMeta is
   * found on the buffer.
   *
   * The canvas buffer is already allocated, zeroed, and mapped. The Cairo
   * context is already created and cleared. The subclass must not free or
   * replace the canvas or the context.
   */
  void (*render) (GstAnalyticsOverlay        *self,
                  GstAnalyticsRelationMeta   *rmeta,
                  GstAnalyticsOverlayCairoCtx *ctx);

  gpointer _gst_reserved[GST_PADDING];
};

/* helper to compute the text-outline offset from a font description.
 * Subclasses that draw labels call this from their start() override after
 * chaining up to the base, using the pango_context already initialized by the
 * base. */
gdouble gst_analytics_overlay_compute_label_ofs (PangoFontDescription *desc);

/* Accessor functions for private fields needed by subclasses in their render()
 * vfunc. */
PangoLayout  *gst_analytics_overlay_get_pango_layout  (GstAnalyticsOverlay *self);
PangoContext *gst_analytics_overlay_get_pango_context (GstAnalyticsOverlay *self);
GstVideoInfo *gst_analytics_overlay_get_in_info       (GstAnalyticsOverlay *self);

G_END_DECLS
#endif /* __GST_ANALYTICS_OVERLAY_H__ */
