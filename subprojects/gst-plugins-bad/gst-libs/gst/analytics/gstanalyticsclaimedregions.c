/*
 * GStreamer gstreamer-analyticsclaimedregions
 * Copyright (C) 2026 Collabora Ltd
 *
 * gstanalyticsclaimedregions.c
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
 * SECTION:gstanalyticsclaimedregions
 * @title: GstAnalyticsClaimedRegionsMeta
 * @short_description: Cross-element overlay region coordination
 *
 * Composable overlay elements need to avoid drawing on top of each other. The
 * #GstAnalyticsClaimedRegionsMeta is a per-buffer list of the frame regions an
 * element has drawn into: each element that renders content *claims* the regions
 * it used, and downstream overlay elements *read* the claims to steer clear.
 *
 * The regions are expressed in the negotiated frame's pixel coordinate space and
 * follow the frame across scale/crop/aspect transforms via the meta's transform
 * function (modelled on #GstVideoRegionOfInterestMeta), so coordinating elements
 * may sit on either side of a scaler.
 *
 * Since: 1.30
 */

#include "gstanalyticsclaimedregions.h"

#include <gst/video/video.h>

static gboolean
gst_analytics_claimed_regions_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstAnalyticsClaimedRegionsMeta *cmeta =
      (GstAnalyticsClaimedRegionsMeta *) meta;

  cmeta->num_regions = 0;
  cmeta->regions = NULL;

  return TRUE;
}

static void
gst_analytics_claimed_regions_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstAnalyticsClaimedRegionsMeta *cmeta =
      (GstAnalyticsClaimedRegionsMeta *) meta;

  g_free (cmeta->regions);
  cmeta->regions = NULL;
  cmeta->num_regions = 0;
}

/* Append a region to @cmeta, growing its array by one. */
static void
gst_analytics_claimed_regions_meta_append (GstAnalyticsClaimedRegionsMeta *
    meta, const GstAnalyticsClaimedRegion * region)
{
  meta->regions = g_renew (GstAnalyticsClaimedRegion, meta->regions,
      meta->num_regions + 1);
  meta->regions[meta->num_regions] = *region;
  meta->num_regions++;
}

static gboolean
gst_analytics_claimed_regions_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstAnalyticsClaimedRegionsMeta *smeta =
      (GstAnalyticsClaimedRegionsMeta *) meta;
  GstAnalyticsClaimedRegionsMeta *dmeta;
  gsize i;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    /* Plain copy: carry every region verbatim. */
    dmeta = gst_buffer_add_analytics_claimed_regions_meta (dest);
    if (!dmeta)
      return FALSE;
    for (i = 0; i < smeta->num_regions; i++)
      gst_analytics_claimed_regions_meta_append (dmeta, &smeta->regions[i]);
    return TRUE;
  } else if (GST_VIDEO_META_TRANSFORM_IS_MATRIX (type)) {
    /* Scale + crop + letterbox: map each region and clip to the output frame,
     * dropping regions that fall entirely outside it. */
    GstVideoMetaTransformMatrix *trans = data;

    dmeta = gst_buffer_add_analytics_claimed_regions_meta (dest);
    if (!dmeta)
      return FALSE;
    for (i = 0; i < smeta->num_regions; i++) {
      GstVideoRectangle rect = { smeta->regions[i].x, smeta->regions[i].y,
        smeta->regions[i].w, smeta->regions[i].h
      };
      if (gst_video_meta_transform_matrix_rectangle_clipped (trans, &rect)) {
        GstAnalyticsClaimedRegion out = smeta->regions[i];
        out.x = rect.x;
        out.y = rect.y;
        out.w = rect.w;
        out.h = rect.h;
        gst_analytics_claimed_regions_meta_append (dmeta, &out);
      }
    }
    return TRUE;
  } else if (type == gst_video_meta_transform_scale_get_quark ()) {
    /* Simple in/out ratio scale (used e.g. by glcolorscale). */
    GstVideoMetaTransform *trans = data;
    gint in_w = GST_VIDEO_INFO_WIDTH (trans->in_info);
    gint in_h = GST_VIDEO_INFO_HEIGHT (trans->in_info);
    gint out_w = GST_VIDEO_INFO_WIDTH (trans->out_info);
    gint out_h = GST_VIDEO_INFO_HEIGHT (trans->out_info);

    if (in_w < 1)
      in_w = 1;
    if (in_h < 1)
      in_h = 1;

    dmeta = gst_buffer_add_analytics_claimed_regions_meta (dest);
    if (!dmeta)
      return FALSE;
    for (i = 0; i < smeta->num_regions; i++) {
      GstAnalyticsClaimedRegion out = smeta->regions[i];
      out.x = (gint) ((gint64) smeta->regions[i].x * out_w / in_w);
      out.y = (gint) ((gint64) smeta->regions[i].y * out_h / in_h);
      out.w = (gint) ((gint64) smeta->regions[i].w * out_w / in_w);
      out.h = (gint) ((gint64) smeta->regions[i].h * out_h / in_h);
      gst_analytics_claimed_regions_meta_append (dmeta, &out);
    }
    return TRUE;
  }

  /* Unknown transform: not handled (the meta is dropped rather than carried
   * with stale coordinates). */
  return FALSE;
}

/**
 * gst_analytics_claimed_regions_meta_api_get_type: (skip)
 *
 * Since: 1.30
 */
GType
gst_analytics_claimed_regions_meta_api_get_type (void)
{
  static GType type = 0;
  /* Tagged size-sensitive so scalers invoke our transform to rescale the
   * regions into their output coordinate space. */
  static const gchar *tags[] = {
    GST_META_TAG_VIDEO_STR,
    GST_META_TAG_VIDEO_SIZE_STR,
    NULL
  };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstAnalyticsClaimedRegionsMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

/**
 * gst_analytics_claimed_regions_meta_get_info: (skip)
 *
 * Since: 1.30
 */
const GstMetaInfo *
gst_analytics_claimed_regions_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (gst_analytics_claimed_regions_meta_api_get_type (),
        "GstAnalyticsClaimedRegionsMeta",
        sizeof (GstAnalyticsClaimedRegionsMeta),
        gst_analytics_claimed_regions_meta_init,
        gst_analytics_claimed_regions_meta_free,
        gst_analytics_claimed_regions_meta_transform);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}

/**
 * gst_buffer_add_analytics_claimed_regions_meta:
 * @buffer: a writable #GstBuffer
 *
 * Adds a #GstAnalyticsClaimedRegionsMeta to @buffer. If one is already present
 * it is returned, so successive producers append to a shared list via
 * gst_analytics_claimed_regions_meta_add_region().
 *
 * Returns: (transfer none): the #GstAnalyticsClaimedRegionsMeta
 *
 * Since: 1.30
 */
GstAnalyticsClaimedRegionsMeta *
gst_buffer_add_analytics_claimed_regions_meta (GstBuffer * buffer)
{
  GstAnalyticsClaimedRegionsMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  meta = gst_buffer_get_analytics_claimed_regions_meta (buffer);
  if (meta)
    return meta;

  return (GstAnalyticsClaimedRegionsMeta *) gst_buffer_add_meta (buffer,
      gst_analytics_claimed_regions_meta_get_info (), NULL);
}

/**
 * gst_buffer_get_analytics_claimed_regions_meta:
 * @buffer: a #GstBuffer
 *
 * Gets the #GstAnalyticsClaimedRegionsMeta from @buffer.
 *
 * Returns: (nullable) (transfer none): the meta, or %NULL if there is none
 *
 * Since: 1.30
 */
GstAnalyticsClaimedRegionsMeta *
gst_buffer_get_analytics_claimed_regions_meta (GstBuffer * buffer)
{
  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  return (GstAnalyticsClaimedRegionsMeta *) gst_buffer_get_meta (buffer,
      GST_ANALYTICS_CLAIMED_REGIONS_META_API_TYPE);
}

/**
 * gst_analytics_claimed_regions_meta_add_region:
 * @meta: a #GstAnalyticsClaimedRegionsMeta
 * @x: left edge in frame pixels
 * @y: top edge in frame pixels
 * @w: width in pixels
 * @h: height in pixels
 * @kind: how strongly the region should be honoured:
 *   %GST_ANALYTICS_CLAIM_KIND_OCCLUDE for a hard claim downstream overlays must
 *   not draw over, or %GST_ANALYTICS_CLAIM_KIND_AVOID for a soft claim they
 *   prefer to steer around but may use when no better placement exists.
 * @owner: a #GQuark naming the producing element
 * @priority: importance of the claim
 *
 * Appends a claimed region to @meta.
 *
 * Since: 1.30
 */
void
gst_analytics_claimed_regions_meta_add_region (GstAnalyticsClaimedRegionsMeta *
    meta, gint x, gint y, gint w, gint h, GstAnalyticsClaimKind kind,
    GQuark owner, gint priority)
{
  GstAnalyticsClaimedRegion region;

  g_return_if_fail (meta != NULL);

  region.x = x;
  region.y = y;
  region.w = w;
  region.h = h;
  region.kind = kind;
  region.priority = priority;
  region.owner = owner;

  gst_analytics_claimed_regions_meta_append (meta, &region);
}

/**
 * gst_analytics_claimed_regions_meta_get_size:
 * @meta: a #GstAnalyticsClaimedRegionsMeta
 *
 * Returns: the number of regions in @meta
 *
 * Since: 1.30
 */
gsize
gst_analytics_claimed_regions_meta_get_size (GstAnalyticsClaimedRegionsMeta *
    meta)
{
  g_return_val_if_fail (meta != NULL, 0);

  return meta->num_regions;
}

/**
 * gst_analytics_claimed_regions_meta_get_region:
 * @meta: a #GstAnalyticsClaimedRegionsMeta
 * @index: index of the region, less than the value returned by
 *   gst_analytics_claimed_regions_meta_get_size()
 *
 * Returns: (transfer none): the region at @index
 *
 * Since: 1.30
 */
const GstAnalyticsClaimedRegion *
gst_analytics_claimed_regions_meta_get_region (GstAnalyticsClaimedRegionsMeta *
    meta, gsize index)
{
  g_return_val_if_fail (meta != NULL, NULL);
  g_return_val_if_fail (index < meta->num_regions, NULL);

  return &meta->regions[index];
}
