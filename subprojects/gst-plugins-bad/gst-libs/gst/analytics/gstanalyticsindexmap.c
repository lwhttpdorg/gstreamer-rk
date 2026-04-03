/* GStreamer indexed-map scaling helpers
 * Copyright (C) 2026 Collabora Ltd.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstanalyticsindexmap-private.h"

typedef struct
{
  gsize width;
  gsize height;
  gsize stride;
} GstIndexMapLayout;

static gboolean
gst_index_map_layout_is_valid (const guint8 * src, GstIndexMapLayout src_layout,
    guint8 * dst, GstIndexMapLayout dst_layout)
{
  gsize src_len;
  gsize dst_len;

  if (src == NULL || dst == NULL)
    return FALSE;

  if (src_layout.width == 0 || src_layout.height == 0 ||
      dst_layout.width == 0 || dst_layout.height == 0)
    return FALSE;

  if (src_layout.stride < src_layout.width ||
      dst_layout.stride < dst_layout.width)
    return FALSE;

  if (src_layout.height > G_MAXSIZE / src_layout.stride ||
      dst_layout.height > G_MAXSIZE / dst_layout.stride)
    return FALSE;

  src_len = src_layout.stride * src_layout.height;
  dst_len = dst_layout.stride * dst_layout.height;

  return src_len > 0 && dst_len > 0;
}

gboolean
gst_analytics_index_map_nearest_scale_uint8 (const guint8 * src,
    gsize src_width,
    gsize src_height,
    gsize src_stride,
    guint8 * dst, gsize dst_width, gsize dst_height, gsize dst_stride)
{
  GstIndexMapLayout src_layout = { src_width, src_height, src_stride };
  GstIndexMapLayout dst_layout = { dst_width, dst_height, dst_stride };

  if (!gst_index_map_layout_is_valid (src, src_layout, dst, dst_layout))
    return FALSE;

  for (gsize y = 0; y < dst_height; y++) {
    const guint8 *src_row = src + ((y * src_height) / dst_height) * src_stride;
    guint8 *dst_row = dst + y * dst_stride;

    for (gsize x = 0; x < dst_width; x++)
      dst_row[x] = src_row[(x * src_width) / dst_width];
  }

  return TRUE;
}

gboolean
gst_analytics_index_map_bilinear_threshold_scale_uint8 (const guint8 * src,
    gsize src_width,
    gsize src_height,
    gsize src_stride,
    guint8 * dst,
    gsize dst_width, gsize dst_height, gsize dst_stride, guint8 threshold)
{
  GstIndexMapLayout src_layout = { src_width, src_height, src_stride };
  GstIndexMapLayout dst_layout = { dst_width, dst_height, dst_stride };

  if (!gst_index_map_layout_is_valid (src, src_layout, dst, dst_layout))
    return FALSE;

  for (gsize y = 0; y < dst_height; y++) {
    gdouble my = (((gdouble) y + 0.5) * src_height / dst_height) - 0.5;
    guint8 *dst_row = dst + y * dst_stride;
    gsize y0, y1;
    gdouble ty;

    if (my < 0.0)
      my = 0.0;
    if (my > (gdouble) src_height - 1.0)
      my = (gdouble) src_height - 1.0;

    y0 = (gsize) my;
    y1 = MIN (y0 + 1, src_height - 1);
    ty = my - (gdouble) y0;

    for (gsize x = 0; x < dst_width; x++) {
      gdouble mx = (((gdouble) x + 0.5) * src_width / dst_width) - 0.5;
      gsize x0, x1;
      gdouble tx;
      gdouble w00, w10, w01, w11;
      gdouble value;

      if (mx < 0.0)
        mx = 0.0;
      if (mx > (gdouble) src_width - 1.0)
        mx = (gdouble) src_width - 1.0;

      x0 = (gsize) mx;
      x1 = MIN (x0 + 1, src_width - 1);
      tx = mx - (gdouble) x0;

      w00 = (1.0 - tx) * (1.0 - ty);
      w10 = tx * (1.0 - ty);
      w01 = (1.0 - tx) * ty;
      w11 = tx * ty;

      value = src[y0 * src_stride + x0] * w00 +
          src[y0 * src_stride + x1] * w10 +
          src[y1 * src_stride + x0] * w01 + src[y1 * src_stride + x1] * w11;

      dst_row[x] = value >= threshold ? 0xff : 0x00;
    }
  }

  return TRUE;
}
