/*
 * GStreamer gstreamer-analyticsoverlaylabels
 * Copyright (C) 2026 Collabora Ltd
 *
 * gstanalyticsoverlaylabels.h
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

#ifndef __GST_ANALYTICS_OVERLAY_LABELS_H__
#define __GST_ANALYTICS_OVERLAY_LABELS_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsLabelCandidateKind:
 * @GST_ANALYTICS_LABEL_CANDIDATE_BOX: the label is anchored to a bounding box;
 *   fallback positions ring the box.
 * @GST_ANALYTICS_LABEL_CANDIDATE_POINT: the label is anchored to a point (e.g. a
 *   keypoint); fallback positions ring the point.
 *
 * How a downstream compositor should generate fallback positions when a deferred
 * label's preferred location is taken.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_ANALYTICS_LABEL_CANDIDATE_BOX = 0,
  GST_ANALYTICS_LABEL_CANDIDATE_POINT = 1,
} GstAnalyticsLabelCandidateKind;

/**
 * GstAnalyticsOverlayLabel:
 * @text: the label text to draw
 * @color: the text colour, as 0xAARRGGBB
 * @anchor_x: left edge of the labelled feature, in frame pixels
 * @anchor_y: top edge of the labelled feature, in frame pixels
 * @anchor_w: width of the labelled feature (0 for a point feature)
 * @anchor_h: height of the labelled feature (0 for a point feature)
 * @preferred_x: left edge of the preferred label position, in frame pixels
 * @preferred_y: top edge of the preferred label position, in frame pixels
 * @preferred_w: width of the label box (also gives the label size)
 * @preferred_h: height of the label box
 * @kind: which fallback-candidate generator to use
 * @priority: importance of the label, for global ordering and avoidance
 * @owner: a #GQuark naming the producing element
 *
 * A label an overlay element deferred for a downstream compositor to place and
 * render, together with its anchor feature and preferred position.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsOverlayLabel
{
  gchar *text;
  guint32 color;
  gint anchor_x;
  gint anchor_y;
  gint anchor_w;
  gint anchor_h;
  gint preferred_x;
  gint preferred_y;
  gint preferred_w;
  gint preferred_h;
  GstAnalyticsLabelCandidateKind kind;
  gint priority;
  GQuark owner;
} GstAnalyticsOverlayLabel;

/**
 * GstAnalyticsOverlayLabelsMeta:
 * @meta: parent #GstMeta
 * @num_labels: number of labels in @labels
 * @labels: (array length=num_labels): the deferred labels
 *
 * A #GstMeta carrying labels an overlay element deferred for a downstream
 * compositor to place globally (avoiding claimed regions and each other) and
 * render. Coordinates are in the negotiated frame's pixel space and are rescaled
 * with the frame by the meta's transform.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsOverlayLabelsMeta
{
  GstMeta meta;

  gsize num_labels;
  GstAnalyticsOverlayLabel *labels;
} GstAnalyticsOverlayLabelsMeta;

/**
 * GST_ANALYTICS_OVERLAY_LABELS_META_API_TYPE:
 *
 * The overlay-labels meta API type.
 *
 * Since: 1.30
 */
#define GST_ANALYTICS_OVERLAY_LABELS_META_API_TYPE \
  (gst_analytics_overlay_labels_meta_api_get_type())

/**
 * GST_ANALYTICS_OVERLAY_LABELS_META_INFO: (skip)
 *
 * The overlay-labels meta API info.
 *
 * Since: 1.30
 */
#define GST_ANALYTICS_OVERLAY_LABELS_META_INFO \
  (gst_analytics_overlay_labels_meta_get_info())

GST_ANALYTICS_META_API
GType gst_analytics_overlay_labels_meta_api_get_type (void);

GST_ANALYTICS_META_API
const GstMetaInfo *gst_analytics_overlay_labels_meta_get_info (void);

GST_ANALYTICS_META_API
GstAnalyticsOverlayLabelsMeta *
gst_buffer_add_analytics_overlay_labels_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
GstAnalyticsOverlayLabelsMeta *
gst_buffer_get_analytics_overlay_labels_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
void gst_analytics_overlay_labels_meta_add_label (
    GstAnalyticsOverlayLabelsMeta * meta, const gchar * text, guint32 color,
    gint anchor_x, gint anchor_y, gint anchor_w, gint anchor_h,
    gint preferred_x, gint preferred_y, gint preferred_w, gint preferred_h,
    GstAnalyticsLabelCandidateKind kind, GQuark owner, gint priority);

GST_ANALYTICS_META_API
gsize gst_analytics_overlay_labels_meta_get_size (
    GstAnalyticsOverlayLabelsMeta * meta);

GST_ANALYTICS_META_API
const GstAnalyticsOverlayLabel *
gst_analytics_overlay_labels_meta_get_label (
    GstAnalyticsOverlayLabelsMeta * meta, gsize index);

G_END_DECLS

#endif /* __GST_ANALYTICS_OVERLAY_LABELS_H__ */
