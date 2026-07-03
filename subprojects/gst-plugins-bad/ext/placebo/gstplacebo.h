/*
 * placebo gstreamer plugin
 * Copyright (C) 2025 Martin Rodriguez Reboredo <yakoyoku@gmail.com>
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

#ifndef _GST_PLACEBO_H_
#define _GST_PLACEBO_H_

#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "placeboapi.h"

#define GST_TYPE_PLACEBO            (gst_placebo_get_type())
#define GST_PLACEBO(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLACEBO,GstPlacebo))
#define GST_IS_PLACEBO(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLACEBO))
#define GST_PLACEBO_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_PLACEBO,GstPlaceboClass))
#define GST_IS_PLACEBO_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_PLACEBO))
#define GST_PLACEBO_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_PLACEBO,GstPlaceboClass))

typedef struct _GstPlacebo GstPlacebo;
typedef struct _GstPlaceboClass GstPlaceboClass;

struct _GstPlacebo
{
  GstBaseTransform filter;

  GstPlaceboAPI *api;

  /* properties */
  gchar *hook;
  gchar *hook_location;
  gboolean refresh_shader;
  gboolean refresh_location;

  GstCaps              *in_caps;
  GstVideoInfo          in_info;
  GstCaps              *out_caps;
  GstVideoInfo          out_info;
};

struct _GstPlaceboClass
{
  GstBaseTransformClass filter_class;
};

GType gst_placebo_get_type (void);

GST_ELEMENT_REGISTER_DECLARE (placebo);

#endif /* _GST_PLACEBO_H_ */
