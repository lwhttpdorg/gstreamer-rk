/*
 * GStreamer gstreamer-analyticsoverlaylabels
 * Copyright (C) 2026 Collabora Ltd
 *
 * gstanalyticsoverlaylabels.c
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
 * SECTION:gstanalyticsoverlaylabels
 * @title: GstAnalyticsOverlayLabelsMeta
 * @short_description: Deferred overlay labels for downstream placement
 *
 * The #GstAnalyticsOverlayLabelsMeta lets an overlay element defer its labels
 * for a downstream compositor to place globally — avoiding claimed regions (see
 * #GstAnalyticsClaimedRegionsMeta) and each other — and render. Each label
 * carries its text, colour, the anchor feature it belongs to and a preferred
 * position; coordinates are in the negotiated frame's pixel space and follow the
 * frame across scale/crop transforms via the meta's transform function.
 *
 * Since: 1.30
 */

#include "gstanalyticsoverlaylabels.h"

#include <gst/video/video.h>

static gboolean
gst_analytics_overlay_labels_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstAnalyticsOverlayLabelsMeta *lmeta = (GstAnalyticsOverlayLabelsMeta *) meta;

  lmeta->num_labels = 0;
  lmeta->labels = NULL;

  return TRUE;
}

static void
gst_analytics_overlay_labels_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstAnalyticsOverlayLabelsMeta *lmeta = (GstAnalyticsOverlayLabelsMeta *) meta;
  gsize i;

  for (i = 0; i < lmeta->num_labels; i++)
    g_free (lmeta->labels[i].text);
  g_free (lmeta->labels);
  lmeta->labels = NULL;
  lmeta->num_labels = 0;
}

/* Append a copy of @label (duplicating its text) to @meta. */
static void
gst_analytics_overlay_labels_meta_append (GstAnalyticsOverlayLabelsMeta * meta,
    const GstAnalyticsOverlayLabel * label)
{
  GstAnalyticsOverlayLabel *dst;

  meta->labels = g_renew (GstAnalyticsOverlayLabel, meta->labels,
      meta->num_labels + 1);
  dst = &meta->labels[meta->num_labels];
  *dst = *label;
  dst->text = g_strdup (label->text);
  meta->num_labels++;
}

static gboolean
gst_analytics_overlay_labels_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAnalyticsOverlayLabelsMeta *smeta = (GstAnalyticsOverlayLabelsMeta *) meta;
  GstAnalyticsOverlayLabelsMeta *dmeta;
  gsize i;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    dmeta = gst_buffer_add_analytics_overlay_labels_meta (dest);
    if (!dmeta)
      return FALSE;
    for (i = 0; i < smeta->num_labels; i++)
      gst_analytics_overlay_labels_meta_append (dmeta, &smeta->labels[i]);
    return TRUE;
  } else if (GST_VIDEO_META_TRANSFORM_IS_MATRIX (type)) {
    /* Map each label's anchor and preferred rects, clipping to the output
     * frame. Drop a label whose anchor falls entirely outside; if only its
     * preferred box clips out, fall back to the anchor (the compositor re-places
     * labels from the anchor anyway). */
    GstVideoMetaTransformMatrix *trans = data;

    dmeta = gst_buffer_add_analytics_overlay_labels_meta (dest);
    if (!dmeta)
      return FALSE;
    for (i = 0; i < smeta->num_labels; i++) {
      GstAnalyticsOverlayLabel out = smeta->labels[i];
      GstVideoRectangle anchor = { smeta->labels[i].anchor_x,
        smeta->labels[i].anchor_y, smeta->labels[i].anchor_w,
        smeta->labels[i].anchor_h
      };
      GstVideoRectangle pref = { smeta->labels[i].preferred_x,
        smeta->labels[i].preferred_y, smeta->labels[i].preferred_w,
        smeta->labels[i].preferred_h
      };

      if (!gst_video_meta_transform_matrix_rectangle_clipped (trans, &anchor))
        continue;

      out.anchor_x = anchor.x;
      out.anchor_y = anchor.y;
      out.anchor_w = anchor.w;
      out.anchor_h = anchor.h;

      if (gst_video_meta_transform_matrix_rectangle_clipped (trans, &pref)) {
        out.preferred_x = pref.x;
        out.preferred_y = pref.y;
        out.preferred_w = pref.w;
        out.preferred_h = pref.h;
      } else {
        out.preferred_x = anchor.x;
        out.preferred_y = anchor.y;
        out.preferred_w = anchor.w;
        out.preferred_h = anchor.h;
      }

      gst_analytics_overlay_labels_meta_append (dmeta, &out);
    }
    return TRUE;
  } else if (type == gst_video_meta_transform_scale_get_quark ()) {
    GstVideoMetaTransform *trans = data;
    gint in_w = GST_VIDEO_INFO_WIDTH (trans->in_info);
    gint in_h = GST_VIDEO_INFO_HEIGHT (trans->in_info);
    gint out_w = GST_VIDEO_INFO_WIDTH (trans->out_info);
    gint out_h = GST_VIDEO_INFO_HEIGHT (trans->out_info);

    if (in_w < 1)
      in_w = 1;
    if (in_h < 1)
      in_h = 1;

    dmeta = gst_buffer_add_analytics_overlay_labels_meta (dest);
    if (!dmeta)
      return FALSE;
    for (i = 0; i < smeta->num_labels; i++) {
      GstAnalyticsOverlayLabel out = smeta->labels[i];
      out.anchor_x = (gint) ((gint64) smeta->labels[i].anchor_x * out_w / in_w);
      out.anchor_y = (gint) ((gint64) smeta->labels[i].anchor_y * out_h / in_h);
      out.anchor_w = (gint) ((gint64) smeta->labels[i].anchor_w * out_w / in_w);
      out.anchor_h = (gint) ((gint64) smeta->labels[i].anchor_h * out_h / in_h);
      out.preferred_x =
          (gint) ((gint64) smeta->labels[i].preferred_x * out_w / in_w);
      out.preferred_y =
          (gint) ((gint64) smeta->labels[i].preferred_y * out_h / in_h);
      out.preferred_w =
          (gint) ((gint64) smeta->labels[i].preferred_w * out_w / in_w);
      out.preferred_h =
          (gint) ((gint64) smeta->labels[i].preferred_h * out_h / in_h);
      gst_analytics_overlay_labels_meta_append (dmeta, &out);
    }
    return TRUE;
  }

  return FALSE;
}

/**
 * gst_analytics_overlay_labels_meta_api_get_type: (skip)
 *
 * Since: 1.30
 */
GType
gst_analytics_overlay_labels_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = {
    GST_META_TAG_VIDEO_STR,
    GST_META_TAG_VIDEO_SIZE_STR,
    NULL
  };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstAnalyticsOverlayLabelsMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

/**
 * gst_analytics_overlay_labels_meta_get_info: (skip)
 *
 * Since: 1.30
 */
const GstMetaInfo *
gst_analytics_overlay_labels_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_analytics_overlay_labels_meta_api_get_type (),
        "GstAnalyticsOverlayLabelsMeta",
        sizeof (GstAnalyticsOverlayLabelsMeta),
        gst_analytics_overlay_labels_meta_init,
        gst_analytics_overlay_labels_meta_free,
        gst_analytics_overlay_labels_meta_transform);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}

/**
 * gst_buffer_add_analytics_overlay_labels_meta:
 * @buffer: a writable #GstBuffer
 *
 * Adds a #GstAnalyticsOverlayLabelsMeta to @buffer, or returns the existing one
 * so successive producers append to a shared list.
 *
 * Returns: (transfer none): the #GstAnalyticsOverlayLabelsMeta
 *
 * Since: 1.30
 */
GstAnalyticsOverlayLabelsMeta *
gst_buffer_add_analytics_overlay_labels_meta (GstBuffer * buffer)
{
  GstAnalyticsOverlayLabelsMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  meta = gst_buffer_get_analytics_overlay_labels_meta (buffer);
  if (meta)
    return meta;

  return (GstAnalyticsOverlayLabelsMeta *) gst_buffer_add_meta (buffer,
      gst_analytics_overlay_labels_meta_get_info (), NULL);
}

/**
 * gst_buffer_get_analytics_overlay_labels_meta:
 * @buffer: a #GstBuffer
 *
 * Gets the #GstAnalyticsOverlayLabelsMeta from @buffer.
 *
 * Returns: (nullable) (transfer none): the meta, or %NULL if there is none
 *
 * Since: 1.30
 */
GstAnalyticsOverlayLabelsMeta *
gst_buffer_get_analytics_overlay_labels_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  return (GstAnalyticsOverlayLabelsMeta *) gst_buffer_get_meta (buffer,
      GST_ANALYTICS_OVERLAY_LABELS_META_API_TYPE);
}

/**
 * gst_analytics_overlay_labels_meta_add_label:
 * @meta: a #GstAnalyticsOverlayLabelsMeta
 * @text: the label text (copied)
 * @color: the text colour as 0xAARRGGBB
 * @anchor_x: left edge of the labelled feature
 * @anchor_y: top edge of the labelled feature
 * @anchor_w: width of the labelled feature (0 for a point)
 * @anchor_h: height of the labelled feature (0 for a point)
 * @preferred_x: left edge of the preferred label position
 * @preferred_y: top edge of the preferred label position
 * @preferred_w: width of the label box
 * @preferred_h: height of the label box
 * @kind: which fallback-candidate generator to use
 * @owner: a #GQuark naming the producing element
 * @priority: importance of the label
 *
 * Appends a deferred label to @meta. @text is copied.
 *
 * Since: 1.30
 */
void
gst_analytics_overlay_labels_meta_add_label (GstAnalyticsOverlayLabelsMeta *
    meta, const gchar * text, guint32 color, gint anchor_x, gint anchor_y,
    gint anchor_w, gint anchor_h, gint preferred_x, gint preferred_y,
    gint preferred_w, gint preferred_h, GstAnalyticsLabelCandidateKind kind,
    GQuark owner, gint priority)
{
  GstAnalyticsOverlayLabel label;

  g_return_if_fail (meta != NULL);

  label.text = (gchar *) text;  /* append() duplicates it */
  label.color = color;
  label.anchor_x = anchor_x;
  label.anchor_y = anchor_y;
  label.anchor_w = anchor_w;
  label.anchor_h = anchor_h;
  label.preferred_x = preferred_x;
  label.preferred_y = preferred_y;
  label.preferred_w = preferred_w;
  label.preferred_h = preferred_h;
  label.kind = kind;
  label.priority = priority;
  label.owner = owner;

  gst_analytics_overlay_labels_meta_append (meta, &label);
}

/**
 * gst_analytics_overlay_labels_meta_get_size:
 * @meta: a #GstAnalyticsOverlayLabelsMeta
 *
 * Returns: the number of labels in @meta
 *
 * Since: 1.30
 */
gsize
gst_analytics_overlay_labels_meta_get_size (GstAnalyticsOverlayLabelsMeta *
    meta)
{
  g_return_val_if_fail (meta != NULL, 0);

  return meta->num_labels;
}

/**
 * gst_analytics_overlay_labels_meta_get_label:
 * @meta: a #GstAnalyticsOverlayLabelsMeta
 * @index: index of the label, less than the value returned by
 *   gst_analytics_overlay_labels_meta_get_size()
 *
 * Returns: (transfer none): the label at @index
 *
 * Since: 1.30
 */
const GstAnalyticsOverlayLabel *
gst_analytics_overlay_labels_meta_get_label (GstAnalyticsOverlayLabelsMeta *
    meta, gsize index)
{
  g_return_val_if_fail (meta != NULL, NULL);
  g_return_val_if_fail (index < meta->num_labels, NULL);

  return &meta->labels[index];
}
