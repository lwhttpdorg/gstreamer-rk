/*
 * GStreamer gstreamer-analyticsclaimedregions
 * Copyright (C) 2026 Collabora Ltd
 *
 * gstanalyticsclaimedregions.h
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

#ifndef __GST_ANALYTICS_CLAIMED_REGIONS_H__
#define __GST_ANALYTICS_CLAIMED_REGIONS_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>

G_BEGIN_DECLS

/**
 * GstAnalyticsClaimKind:
 * @GST_ANALYTICS_CLAIM_KIND_OCCLUDE: a hard claim; downstream overlays must not
 *   draw over this region (e.g. another overlay's rendered, opaque content).
 * @GST_ANALYTICS_CLAIM_KIND_AVOID: a soft claim; downstream overlays prefer not
 *   to draw here but may when no better location exists (e.g. a segmentation
 *   mask or an outline-only box).
 *
 * How strongly a claimed region should be honoured by downstream overlays.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_ANALYTICS_CLAIM_KIND_OCCLUDE = 0,
  GST_ANALYTICS_CLAIM_KIND_AVOID = 1,
} GstAnalyticsClaimKind;

/**
 * GstAnalyticsClaimedRegion:
 * @x: left edge of the region, in frame pixels
 * @y: top edge of the region, in frame pixels
 * @w: width of the region, in pixels
 * @h: height of the region, in pixels
 * @kind: how strongly the region should be honoured
 * @priority: importance of the claim; a consumer respects claims of priority
 *   greater than or equal to its own and may overdraw lower-priority ones
 * @owner: a #GQuark naming the producing element, so a consumer can skip its
 *   own claims
 *
 * A single rectangular region an overlay element claimed on a frame.
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsClaimedRegion
{
  gint x;
  gint y;
  gint w;
  gint h;
  GstAnalyticsClaimKind kind;
  gint priority;
  GQuark owner;
} GstAnalyticsClaimedRegion;

/**
 * GstAnalyticsClaimedRegionsMeta:
 * @meta: parent #GstMeta
 * @num_regions: number of regions in @regions
 * @regions: (array length=num_regions): the claimed regions
 *
 * A #GstMeta carrying the frame regions overlay elements have drawn into, so
 * composable overlays can avoid occluding each other. Regions are expressed in
 * the negotiated frame's pixel coordinate space and are rescaled/cropped with
 * the frame by the meta's transform (like #GstVideoRegionOfInterestMeta).
 *
 * Since: 1.30
 */
typedef struct _GstAnalyticsClaimedRegionsMeta
{
  GstMeta meta;

  gsize num_regions;
  GstAnalyticsClaimedRegion *regions;
} GstAnalyticsClaimedRegionsMeta;

/**
 * GST_ANALYTICS_CLAIMED_REGIONS_META_API_TYPE:
 *
 * The claimed-regions meta API type.
 *
 * Since: 1.30
 */
#define GST_ANALYTICS_CLAIMED_REGIONS_META_API_TYPE \
  (gst_analytics_claimed_regions_meta_api_get_type())

/**
 * GST_ANALYTICS_CLAIMED_REGIONS_META_INFO: (skip)
 *
 * The claimed-regions meta API info.
 *
 * Since: 1.30
 */
#define GST_ANALYTICS_CLAIMED_REGIONS_META_INFO \
  (gst_analytics_claimed_regions_meta_get_info())

GST_ANALYTICS_META_API
GType gst_analytics_claimed_regions_meta_api_get_type (void);

GST_ANALYTICS_META_API
const GstMetaInfo *gst_analytics_claimed_regions_meta_get_info (void);

GST_ANALYTICS_META_API
GstAnalyticsClaimedRegionsMeta *
gst_buffer_add_analytics_claimed_regions_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
GstAnalyticsClaimedRegionsMeta *
gst_buffer_get_analytics_claimed_regions_meta (GstBuffer * buffer);

GST_ANALYTICS_META_API
void gst_analytics_claimed_regions_meta_add_region (
    GstAnalyticsClaimedRegionsMeta * meta, gint x, gint y, gint w, gint h,
    GstAnalyticsClaimKind kind, GQuark owner, gint priority);

GST_ANALYTICS_META_API
gsize gst_analytics_claimed_regions_meta_get_size (
    GstAnalyticsClaimedRegionsMeta * meta);

GST_ANALYTICS_META_API
const GstAnalyticsClaimedRegion *
gst_analytics_claimed_regions_meta_get_region (
    GstAnalyticsClaimedRegionsMeta * meta, gsize index);

G_END_DECLS

#endif /* __GST_ANALYTICS_CLAIMED_REGIONS_H__ */
