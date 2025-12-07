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

#ifndef _GST_PLACEBO_API_H_
#define _GST_PLACEBO_API_H_

#include <gst/gstcontext.h>
#include <gst/gstquery.h>
#include <gst/gstobject.h>
#include <gst/video/video-info.h>

#include <libplacebo/gpu.h>
#include <libplacebo/log.h>
#include <libplacebo/options.h>
#include <libplacebo/renderer.h>

#define GST_TYPE_PLACEBO_API            (gst_placebo_api_get_type())
#define GST_PLACEBO_API(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLACEBO_API,GstPlaceboAPI))
#define GST_IS_PLACEBO_API(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLACEBO_API))
#define GST_PLACEBO_API_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_PLACEBO_API,GstPlaceboAPIClass))
#define GST_IS_PLACEBO_API_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_PLACEBO_API))
#define GST_PLACEBO_API_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_PLACEBO_API,GstPlaceboAPIClass))

typedef struct _GstPlacebo GstPlacebo;
typedef struct _GstPlaceboAPI GstPlaceboAPI;
typedef struct _GstPlaceboAPIClass GstPlaceboAPIClass;

struct _GstPlaceboAPI
{
  GstObject object;

  GstPlacebo *placebo;

  GRecMutex context_lock;
  pl_log log;
  pl_gpu gpu;
  pl_renderer renderer;
  pl_options opts;
  const struct pl_hook *hooks[2];
  const struct pl_hook *source_hook;
  const struct pl_hook *location_hook;
};

struct _GstPlaceboAPIClass
{
  GstObjectClass object_class;

  void (*clear) (GstPlaceboAPI * api);
  gboolean (*configure) (GstPlaceboAPI * api);
  gboolean (*start) (GstPlaceboAPI * api);
  gboolean (*stop) (GstPlaceboAPI * api);
  gboolean (*ensure_element_data) (GstPlaceboAPI * api);
  gboolean (*find_local_context) (GstPlaceboAPI * api);
  gboolean (*handle_context_query) (GstPlaceboAPI * api, GstPadDirection direction, GstQuery * query);
  void (*handle_set_context) (GstPlaceboAPI * api, GstContext * context);
  gboolean (*decide_allocation) (GstPlaceboAPI * api, GstQuery * query);
  gboolean (*set_caps) (GstPlaceboAPI * api, GstCaps * in_caps, GstCaps * out_caps);
  gboolean (*map_frame) (GstPlaceboAPI * api, GstBuffer * buf, struct pl_frame *frame, const GstVideoInfo * info, GstMapFlags flags);
  void (*unmap_frame) (GstPlaceboAPI * api, struct pl_frame *frame);
  GstFlowReturn (*render) (GstPlaceboAPI * api, struct pl_frame *frame, struct pl_frame *target);
};

GType gst_placebo_api_get_type (void);

void
gst_placebo_api_clear (GstPlaceboAPI * api);

gboolean
gst_placebo_api_configure (GstPlaceboAPI * api);

gboolean
gst_placebo_api_start (GstPlaceboAPI * api);

gboolean
gst_placebo_api_stop (GstPlaceboAPI * api);

gboolean
gst_placebo_api_ensure_element_data (GstPlaceboAPI * api);

gboolean
gst_placebo_api_find_local_context (GstPlaceboAPI * api);

gboolean
gst_placebo_api_handle_context_query (GstPlaceboAPI * api, GstPadDirection direction, GstQuery * query);

void
gst_placebo_api_handle_set_context (GstPlaceboAPI * api, GstContext * context);

gboolean
gst_placebo_api_decide_allocation (GstPlaceboAPI * api, GstQuery * query);

gboolean
gst_placebo_api_set_caps (GstPlaceboAPI * api, GstCaps * in_caps, GstCaps * out_caps);

gboolean
gst_placebo_api_map_frame (GstPlaceboAPI * api, GstBuffer * buf, struct pl_frame *frame, const GstVideoInfo * info, GstMapFlags flags);

void
gst_placebo_api_unmap_frame (GstPlaceboAPI * api, struct pl_frame *frame);

GstFlowReturn
gst_placebo_api_render (GstPlaceboAPI * api, struct pl_frame *frame, struct pl_frame *target);

#endif /* _GST_PLACEBO_API_H_ */
