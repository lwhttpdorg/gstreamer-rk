/*
 * GStreamer
 * Copyright (C) 2024 Rouven Czerwinski <entwicklung@pengutronix.de>
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
#ifndef GSTQML6SWSINK_H_
#define GSTQML6SWSINK_H_

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/video.h>
#include "qt6switem.h"

typedef struct _GstQml6SWSinkPrivate GstQml6SWSinkPrivate;

G_BEGIN_DECLS

#define GST_TYPE_QML6_SW_SINK (gst_qml6_sw_sink_get_type())
G_DECLARE_FINAL_TYPE (GstQml6SWSink, gst_qml6_sw_sink, GST, QML6_SW_SINK, GstVideoSink)
#define GST_QML6_SW_SINK_CAST(obj) ((GstQml6SWSink*)(obj))

/**
 * GstQml6SWSink:
 *
 * Opaque #GstQml6SWSink object
 */
struct _GstQml6SWSink
{
  /* <private> */
  GstVideoSink          parent;

  GstVideoInfo          v_info;
  GstBufferPool        *pool;
  QSharedPointer<Qt6SWVideoItemInterface> widget;
};

GstQml6SWSink *    gst_qml6_sw_sink_new (void);

G_END_DECLS

#endif /* __GSTQML6SWSINK_H__ */
