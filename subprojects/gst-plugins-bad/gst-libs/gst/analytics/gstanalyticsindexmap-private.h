/* GStreamer indexed-map scaling helpers
 * Copyright (C) 2026 Collabora Ltd.
 */

#ifndef __GST_ANALYTICS_INDEX_MAP_PRIVATE_H__
#define __GST_ANALYTICS_INDEX_MAP_PRIVATE_H__

#include <gst/gst.h>
#include <gst/analytics/analytics-meta-prelude.h>

G_BEGIN_DECLS

GST_ANALYTICS_META_API
gboolean gst_analytics_index_map_nearest_scale_uint8 (const guint8 * src,
    gsize src_width,
    gsize src_height,
    gsize src_stride,
    guint8 * dst,
    gsize dst_width,
    gsize dst_height,
    gsize dst_stride);

GST_ANALYTICS_META_API
gboolean gst_analytics_index_map_bilinear_threshold_scale_uint8 (
    const guint8 * src,
    gsize src_width,
    gsize src_height,
    gsize src_stride,
    guint8 * dst,
    gsize dst_width,
    gsize dst_height,
    gsize dst_stride,
    guint8 threshold);

G_END_DECLS

#endif /* __GST_ANALYTICS_INDEX_MAP_PRIVATE_H__ */
