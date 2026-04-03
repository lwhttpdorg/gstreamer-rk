/* Synthetic tests for indexed-map scaling helpers */

#include <errno.h>
#include <stdio.h>

#include <glib.h>

#include <gst/analytics/gstanalyticsindexmap-private.h>

#define HI_RES_SIZE 1024
#define LO_RES_SIZE 128
#define OUTLINE_HALF_THICKNESS 4

typedef struct
{
  gint x;
  gint y;
} Point;

static inline guint8 *
pixel_at (guint8 * image, gsize stride, gint x, gint y)
{
  return image + (gsize) y *stride + x;
}

static inline const guint8 *
const_pixel_at (const guint8 * image, gsize stride, gint x, gint y)
{
  return image + (gsize) y *stride + x;
}

static void
fill_rect (guint8 * image, gsize stride, gint x0, gint y0, gint x1, gint y1)
{
  for (gint y = y0; y <= y1; y++) {
    for (gint x = x0; x <= x1; x++)
      *pixel_at (image, stride, x, y) = 0xff;
  }
}

static void
fill_circle (guint8 * image, gsize stride, gint cx, gint cy, gint radius)
{
  gint radius_sq = radius * radius;

  for (gint y = cy - radius; y <= cy + radius; y++) {
    for (gint x = cx - radius; x <= cx + radius; x++) {
      gint dx = x - cx;
      gint dy = y - cy;

      if (dx * dx + dy * dy <= radius_sq)
        *pixel_at (image, stride, x, y) = 0xff;
    }
  }
}

static void
fill_ellipse (guint8 * image, gsize stride, gint cx, gint cy, gint rx, gint ry)
{
  gdouble rx_sq = (gdouble) rx * rx;
  gdouble ry_sq = (gdouble) ry * ry;

  for (gint y = cy - ry; y <= cy + ry; y++) {
    for (gint x = cx - rx; x <= cx + rx; x++) {
      gdouble dx = x - cx;
      gdouble dy = y - cy;
      gdouble value = (dx * dx) / rx_sq + (dy * dy) / ry_sq;

      if (value <= 1.0)
        *pixel_at (image, stride, x, y) = 0xff;
    }
  }
}

static gboolean
point_in_polygon (gint x, gint y, const Point * points, gsize point_count)
{
  gboolean inside = FALSE;

  for (gsize i = 0, j = point_count - 1; i < point_count; j = i++) {
    gboolean intersects;

    intersects = ((points[i].y > y) != (points[j].y > y)) &&
        (x < (points[j].x - points[i].x) * (y - points[i].y) /
        (gdouble) (points[j].y - points[i].y) + points[i].x);

    if (intersects)
      inside = !inside;
  }

  return inside;
}

static void
fill_polygon (guint8 * image, gsize stride,
    const Point * points, gsize point_count)
{
  gint min_x = points[0].x;
  gint min_y = points[0].y;
  gint max_x = points[0].x;
  gint max_y = points[0].y;

  for (gsize i = 1; i < point_count; i++) {
    min_x = MIN (min_x, points[i].x);
    min_y = MIN (min_y, points[i].y);
    max_x = MAX (max_x, points[i].x);
    max_y = MAX (max_y, points[i].y);
  }

  for (gint y = min_y; y <= max_y; y++) {
    for (gint x = min_x; x <= max_x; x++) {
      if (point_in_polygon (x, y, points, point_count))
        *pixel_at (image, stride, x, y) = 0xff;
    }
  }
}

static void
create_shape_scene (guint8 * image, gsize stride)
{
  static const Point polygon[] = {
    {580, 580}, {670, 470}, {790, 500}, {850, 610},
    {810, 750}, {700, 820}, {590, 760}, {540, 660}
  };

  fill_rect (image, stride, 64, 80, 240, 240);
  fill_circle (image, stride, 390, 220, 110);
  fill_ellipse (image, stride, 760, 240, 150, 95);
  fill_polygon (image, stride, polygon, G_N_ELEMENTS (polygon));
}

static void
downscale_average (const guint8 * src, gsize src_stride,
    guint8 * dst, gsize dst_stride)
{
  const gsize factor = HI_RES_SIZE / LO_RES_SIZE;

  for (gsize y = 0; y < LO_RES_SIZE; y++) {
    guint8 *dst_row = dst + y * dst_stride;

    for (gsize x = 0; x < LO_RES_SIZE; x++) {
      guint sum = 0;

      for (gsize by = 0; by < factor; by++) {
        const guint8 *src_row = src + (y * factor + by) * src_stride;

        for (gsize bx = 0; bx < factor; bx++)
          sum += src_row[x * factor + bx];
      }

      dst_row[x] = (guint8) (sum / (factor * factor));
    }
  }
}

static void
make_outline_band (const guint8 * filled, gsize stride,
    guint8 * outline, gsize outline_stride)
{
  for (gint y = 0; y < HI_RES_SIZE; y++) {
    for (gint x = 0; x < HI_RES_SIZE; x++) {
      gboolean center = *const_pixel_at (filled, stride, x, y) != 0;
      gboolean mark = FALSE;

      for (gint ny = MAX (0, y - OUTLINE_HALF_THICKNESS);
          ny <= MIN (HI_RES_SIZE - 1, y + OUTLINE_HALF_THICKNESS) && !mark;
          ny++) {
        for (gint nx = MAX (0, x - OUTLINE_HALF_THICKNESS);
            nx <= MIN (HI_RES_SIZE - 1, x + OUTLINE_HALF_THICKNESS); nx++) {
          gboolean neighbor = *const_pixel_at (filled, stride, nx, ny) != 0;

          if (neighbor != center) {
            mark = TRUE;
            break;
          }
        }
      }

      if (mark)
        *pixel_at (outline, outline_stride, x, y) = 0xff;
    }
  }
}

static guint
count_boundary_pixels_outside_outline (const guint8 * projected,
    gsize projected_stride, const guint8 * outline, gsize outline_stride,
    guint * boundary_count)
{
  guint misses = 0;
  guint count = 0;

  for (gint y = 1; y < HI_RES_SIZE - 1; y++) {
    for (gint x = 1; x < HI_RES_SIZE - 1; x++) {
      guint8 value = *const_pixel_at (projected, projected_stride, x, y);
      gboolean is_boundary =
          value != *const_pixel_at (projected, projected_stride, x - 1, y) ||
          value != *const_pixel_at (projected, projected_stride, x + 1, y) ||
          value != *const_pixel_at (projected, projected_stride, x, y - 1) ||
          value != *const_pixel_at (projected, projected_stride, x, y + 1);

      if (!is_boundary)
        continue;

      count++;
      if (*const_pixel_at (outline, outline_stride, x, y) == 0)
        misses++;
    }
  }

  *boundary_count = count;
  return misses;
}

static void
write_pgm (const guint8 * pixels, gsize width, gsize height,
    gsize stride, const gchar * path)
{
  FILE *f = fopen (path, "wb");

  if (!f) {
    g_warning ("write_pgm: cannot open %s: %s", path, g_strerror (errno));
    return;
  }

  fprintf (f, "P5\n%zu %zu\n255\n", width, height);
  for (gsize y = 0; y < height; y++)
    fwrite (pixels + y * stride, 1, width, f);

  fclose (f);
  g_test_message ("wrote %s", path);
}

static gboolean
should_dump_outputs (void)
{
  const gchar *dump = g_getenv ("INDEX_MAP_TEST_DUMP");

  return dump != NULL && dump[0] != '\0' && g_strcmp0 (dump, "0") != 0;
}

static void
test_nearest_preserves_labels (void)
{
  const guint8 src[] = { 1, 10, 20, 30 };
  guint8 dst[64] = { 0, };

  g_assert_true (gst_analytics_index_map_nearest_scale_uint8 (src,
          2, 2, 2, dst, 8, 8, 8));

  for (gsize i = 0; i < G_N_ELEMENTS (dst); i++)
    g_assert_true (dst[i] == 1 || dst[i] == 10 || dst[i] == 20 || dst[i] == 30);
}

static void
test_bilinear_projection_stays_in_outline_band (void)
{
  guint8 *filled = g_malloc0 ((gsize) HI_RES_SIZE * HI_RES_SIZE);
  guint8 *outline = g_malloc0 ((gsize) HI_RES_SIZE * HI_RES_SIZE);
  guint8 *downscaled = g_malloc0 ((gsize) LO_RES_SIZE * LO_RES_SIZE);
  guint8 *projected = g_malloc0 ((gsize) HI_RES_SIZE * HI_RES_SIZE);
  guint boundary_count = 0;
  guint misses;

  create_shape_scene (filled, HI_RES_SIZE);
  make_outline_band (filled, HI_RES_SIZE, outline, HI_RES_SIZE);
  downscale_average (filled, HI_RES_SIZE, downscaled, LO_RES_SIZE);

  if (should_dump_outputs ()) {
    gchar *p;

    p = g_test_build_filename (G_TEST_BUILT, "index-map-filled.pgm", NULL);
    write_pgm (filled, HI_RES_SIZE, HI_RES_SIZE, HI_RES_SIZE, p);
    g_free (p);

    p = g_test_build_filename (G_TEST_BUILT, "index-map-outline.pgm", NULL);
    write_pgm (outline, HI_RES_SIZE, HI_RES_SIZE, HI_RES_SIZE, p);
    g_free (p);

    p = g_test_build_filename (G_TEST_BUILT, "index-map-downscaled.pgm", NULL);
    write_pgm (downscaled, LO_RES_SIZE, LO_RES_SIZE, LO_RES_SIZE, p);
    g_free (p);
  }

  g_assert_true (gst_analytics_index_map_bilinear_threshold_scale_uint8
      (downscaled, LO_RES_SIZE, LO_RES_SIZE, LO_RES_SIZE, projected,
          HI_RES_SIZE, HI_RES_SIZE, HI_RES_SIZE, 128));

  if (should_dump_outputs ()) {
    gchar *p =
        g_test_build_filename (G_TEST_BUILT, "index-map-projected.pgm", NULL);
    write_pgm (projected, HI_RES_SIZE, HI_RES_SIZE, HI_RES_SIZE, p);
    g_free (p);
  }

  misses = count_boundary_pixels_outside_outline (projected, HI_RES_SIZE,
      outline, HI_RES_SIZE, &boundary_count);

  g_assert_cmpuint (boundary_count, >, 2000);
  g_assert_cmpuint (misses, ==, 0);

  g_free (projected);
  g_free (downscaled);
  g_free (outline);
  g_free (filled);
}

gint
main (gint argc, gchar ** argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/index-map/nearest-preserves-labels",
      test_nearest_preserves_labels);
  g_test_add_func
      ("/index-map/bilinear-projection-stays-in-outline-band",
      test_bilinear_projection_stays_in_outline_band);

  return g_test_run ();
}
