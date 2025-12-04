/*
 * GStreamer
 * Copyright (C) 2025 Teus Groenewoud <teus@hotmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-magnify
 * @title: magnify
 * @see_also: geometrictransform
 *
 * Magnify is a geometric image transform element. It adds a magnification at
 * the given center point.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v videotestsrc ! magnify ! videoconvert ! autovideosink
 * ]|
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <math.h>

#include "gstmagnify.h"
#include "geometricmath.h"

GST_DEBUG_CATEGORY_STATIC (gst_magnify_debug);
#define GST_CAT_DEFAULT gst_magnify_debug

enum
{
  PROP_0,
  PROP_ZOOM,
  PROP_RADIUS_PX,
  PROP_MODE,
};

#define DEFAULT_ZOOM 3.0
#define DEFAULT_RADIUS_PX 100
#define DEFAULT_MODE MAGNIFY_RADIUS_MODE_NORMALIZED

#define gst_magnify_parent_class parent_class
G_DEFINE_TYPE (GstMagnify, gst_magnify, GST_TYPE_CIRCLE_GEOMETRIC_TRANSFORM);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (magnify, "magnify", GST_RANK_NONE,
    GST_TYPE_MAGNIFY, GST_DEBUG_CATEGORY_INIT (gst_magnify_debug, "magnify", 0,
        "magnify"));

#define GST_TYPE_MAGNIFY_RADIUS_MODE (gst_magnify_radius_mode_get_type())
static GType
gst_magnify_radius_mode_get_type (void)
{
  static GType magnify_radius_mode_type = 0;

  static const GEnumValue magnify_radius_mode[] = {
    {MAGNIFY_RADIUS_MODE_NORMALIZED,
          "Normalized: use normalized dimensions for radius. Uses radius property",
        "normalized"},
    {MAGNIFY_RADIUS_MODE_ABSOLUTE,
          "Absolute: use absolute pixel dimensions for radius. Uses radius-px property",
        "absolute"},
    {0, NULL, NULL},
  };

  if (!magnify_radius_mode_type) {
    magnify_radius_mode_type =
        g_enum_register_static ("GstMagnifyRadiusMode", magnify_radius_mode);
  }
  return magnify_radius_mode_type;
}

static void
gst_magnify_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstMagnify *magnify;
  GstGeometricTransform *gt;

  gt = GST_GEOMETRIC_TRANSFORM_CAST (object);
  magnify = GST_MAGNIFY_CAST (object);

  GST_OBJECT_LOCK (gt);
  switch (prop_id) {
    case PROP_ZOOM:
    {
      gdouble v = g_value_get_double (value);
      if (v != magnify->zoom) {
        magnify->zoom = v;
        gst_geometric_transform_set_need_remap (gt);
      }
    }
      break;
    case PROP_RADIUS_PX:
    {
      guint radius_px = g_value_get_uint (value);
      if (radius_px != magnify->radius_px) {
        magnify->radius_px = radius_px;
      }
    }
      break;
    case PROP_MODE:
    {
      GstMagnifyRadiusMode mode = g_value_get_enum (value);
      if (mode != magnify->mode) {
        magnify->mode = mode;
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (gt);
}

static void
gst_magnify_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMagnify *magnify;

  magnify = GST_MAGNIFY_CAST (object);

  switch (prop_id) {
    case PROP_ZOOM:
      g_value_set_double (value, magnify->zoom);
      break;
    case PROP_RADIUS_PX:
      g_value_set_uint (value, magnify->radius_px);
      break;
    case PROP_MODE:
      g_value_set_enum (value, magnify->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
magnify_map_absolute (GstGeometricTransform * gt, gint x, gint y,
    gdouble * in_x, gdouble * in_y)
{
  GstCircleGeometricTransform *cgt = GST_CIRCLE_GEOMETRIC_TRANSFORM_CAST (gt);
  GstMagnify *magnify = GST_MAGNIFY_CAST (gt);

  gdouble width = gt->width;
  gdouble height = gt->height;

  gdouble center_x = width * cgt->x_center;
  gdouble center_y = height * cgt->y_center;

  gdouble dx = x - center_x;
  gdouble dy = y - center_y;

  gdouble distance = sqrt (dx * dx + dy * dy);
  gdouble radius = magnify->radius_px;

  if (distance < radius) {
    /* inside radius: apply magnification */
    *in_x = center_x + dx * (1.0 / magnify->zoom);
    *in_y = center_y + dy * (1.0 / magnify->zoom);
  } else {
    /* outside radius: no magnification */
    *in_x = x;
    *in_y = y;
  }

  GST_DEBUG_OBJECT (magnify, "Mapped %d %d into %lf %lf", x, y, *in_x, *in_y);

  return TRUE;
}

static gboolean
magnify_map_normalized (GstGeometricTransform * gt, gint x, gint y,
    gdouble * in_x, gdouble * in_y)
{
  GstCircleGeometricTransform *cgt = GST_CIRCLE_GEOMETRIC_TRANSFORM_CAST (gt);
  GstMagnify *magnify = GST_MAGNIFY_CAST (gt);

  gdouble norm_x, norm_y;
  gdouble r;
  gdouble scale;

  gdouble width = gt->width;
  gdouble height = gt->height;

  /* normalize in ((-1.0, -1.0), (1.0, 1.0) and translate the center */
  norm_x = 2.0 * (x / width - cgt->x_center);
  norm_y = 2.0 * (y / height - cgt->y_center);

  /* calculate radius, normalize to 1 for future convenience */
  r = sqrt (0.5 * (norm_x * norm_x + norm_y * norm_y));

  /* zoom in the center region only - no smooth step */
  scale = (r < cgt->radius) ? 1.0 / magnify->zoom : 1.0;

  norm_x *= scale;
  norm_y *= scale;

  /* unnormalize */
  *in_x = (0.5 * norm_x + cgt->x_center) * width;
  *in_y = (0.5 * norm_y + cgt->y_center) * height;

  GST_DEBUG_OBJECT (magnify, "Mapped %d %d into %lf %lf", x, y, *in_x, *in_y);

  return TRUE;
}

static gboolean
magnify_map (GstGeometricTransform * gt, gint x, gint y, gdouble * in_x,
    gdouble * in_y)
{
  GstMagnify *magnify = GST_MAGNIFY_CAST (gt);
  if (magnify->mode == MAGNIFY_RADIUS_MODE_NORMALIZED) {
    return magnify_map_normalized (gt, x, y, in_x, in_y);
  } else {
    return magnify_map_absolute (gt, x, y, in_x, in_y);
  }
}

static void
gst_magnify_class_init (GstMagnifyClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstGeometricTransformClass *gstgt_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstgt_class = (GstGeometricTransformClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "magnify",
      "Transform/Effect/Video",
      "Adds a magnification at the given center point",
      "Teus Groenewoud <teus@hotmail.com>");

  gobject_class->set_property = gst_magnify_set_property;
  gobject_class->get_property = gst_magnify_get_property;

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("radius-mode", "radius-mode",
          "Geometric mode for radius",
          GST_TYPE_MAGNIFY_RADIUS_MODE,
          DEFAULT_MODE,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ZOOM,
      g_param_spec_double ("zoom", "zoom",
          "Zoom of the magnify effect",
          1.0, 100.0, DEFAULT_ZOOM,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RADIUS_PX,
      g_param_spec_uint ("radius-px", "radius-px",
          "Radius of the magnify effect in pixels",
          1, G_MAXUINT, DEFAULT_RADIUS_PX,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstgt_class->map_func = magnify_map;
}

static void
gst_magnify_init (GstMagnify * filter)
{
  GstGeometricTransform *gt = GST_GEOMETRIC_TRANSFORM (filter);

  filter->mode = DEFAULT_MODE;
  filter->zoom = DEFAULT_ZOOM;
  filter->radius_px = DEFAULT_RADIUS_PX;

  gt->off_edge_pixels = GST_GT_OFF_EDGES_PIXELS_CLAMP;
}
