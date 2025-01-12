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

/**
 * SECTION:element-qml6swsink
 *
 * qml6swsink provides a way to render a video stream as a Qml object inside
 * the Qml scene graph when no OpenGL acceleration is available.  This is
 * achieved by using the QML painter interface to paint a buffer into an
 * element.
 *
 * When an OpenGL environment is available, (#qml6glsink) should be used
 * instead.
 *
 * Since: 1.26
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstqt6elements.h"
#include "gstqml6swsink.h"

#define GST_CAT_DEFAULT gst_debug_qml6_sw_sink
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

static GstFlowReturn gst_qml6_sw_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf);
static gboolean gst_qml6_sw_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static gboolean gst_qml6_sw_sink_stop (GstBaseSink * bsink);
static void gst_qml6_sw_sink_finalize (GObject * object);
static void gst_qml6_sw_sink_set_property (GObject * object, guint prop_id,
                                           const GValue * value, GParamSpec * pspec);
static void gst_qml6_sw_sink_get_property (GObject * object, guint prop_id,
                                           GValue * value, GParamSpec * pspec);

static GstStaticPadTemplate gst_qt_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
    "format = (string) { BGRA }, "
    "width = " GST_VIDEO_SIZE_RANGE ", "
    "height = " GST_VIDEO_SIZE_RANGE ", "
    "framerate = " GST_VIDEO_FPS_RANGE));

#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_PAR_N               0
#define DEFAULT_PAR_D               1

enum
{
  ARG_0,
  PROP_WIDGET,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
};

#define gst_qml6_sw_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstQml6SWSink, gst_qml6_sw_sink,
    GST_TYPE_VIDEO_SINK, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
        "qtswsink", 0, "Qt SW Video Sink"));
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (qml6swsink, "qml6swsink",
    GST_RANK_NONE, GST_TYPE_QML6_SW_SINK, qt6_element_init (plugin));

static void
gst_qml6_sw_sink_class_init (GstQml6SWSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_qml6_sw_sink_set_property;
  gobject_class->get_property = gst_qml6_sw_sink_get_property;
  gobject_class->finalize = gst_qml6_sw_sink_finalize;

  gst_element_class_set_metadata (gstelement_class, "Qt6 SW Video Sink",
      "Sink/Video", "A video sink that renders to a QQuickPaintedItem for Qt6",
      "Rouven Czerwinski <entwicklung@pengutronix.de>");

  g_object_class_install_property (gobject_class, PROP_WIDGET,
      g_param_spec_pointer ("widget", "QQuickPaintedItem",
          "The QQuickPaintedItem to place in the object hierarchy",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", DEFAULT_PAR_N, DEFAULT_PAR_D,
          G_MAXINT, 1, 1, 1,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_add_static_pad_template (gstelement_class, &gst_qt_sink_template);

  gstbasesink_class->set_caps = gst_qml6_sw_sink_set_caps;
  gstbasesink_class->stop = gst_qml6_sw_sink_stop;

  gstvideosink_class->show_frame = gst_qml6_sw_sink_show_frame;
}

static void
gst_qml6_sw_sink_init (GstQml6SWSink * qt_sink)
{
  qt_sink->widget = QSharedPointer<Qt6SWVideoItemInterface>();
  if (qt_sink->widget)
    qt_sink->widget->setSink (GST_ELEMENT_CAST (qt_sink));
  return;
}

static GstFlowReturn
gst_qml6_sw_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstQml6SWSink *qt_sink = GST_QML6_SW_SINK (vsink);

  GST_TRACE_OBJECT (qt_sink, "rendering %" GST_PTR_FORMAT, buf);

  if (qt_sink->widget)
    return qt_sink->widget->setBuffer(buf);

  return GST_FLOW_OK;
}

static void
gst_qml6_sw_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstQml6SWSink *qt_sink = GST_QML6_SW_SINK (object);

  switch (prop_id) {
    case PROP_WIDGET: {
      Qt6SWVideoItem *qt_item = static_cast<Qt6SWVideoItem *> (g_value_get_pointer (value));
      if (qt_item) {
        qt_sink->widget = qt_item->getInterface();
        if (qt_sink->widget) {
          qt_sink->widget->setSink (GST_ELEMENT_CAST (qt_sink));
        }
      } else {
        qt_sink->widget.clear();
      }
      break;
    }
    case PROP_FORCE_ASPECT_RATIO:
      g_return_if_fail (qt_sink->widget);
      qt_sink->widget->setForceAspectRatio (g_value_get_boolean (value));
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      g_return_if_fail (qt_sink->widget);
      qt_sink->widget->setDAR (gst_value_get_fraction_numerator (value),
          gst_value_get_fraction_denominator (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_qml6_sw_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstQml6SWSink *qt_sink = GST_QML6_SW_SINK (object);

  switch (prop_id) {
    case PROP_WIDGET:
      /* This is not really safe - the app needs to be
       * sure the widget is going to be kept alive or
       * this can crash */
      if (qt_sink->widget)
        g_value_set_pointer (value, qt_sink->widget->videoItem());
      else
        g_value_set_pointer (value, NULL);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      if (qt_sink->widget)
        g_value_set_boolean (value, qt_sink->widget->getForceAspectRatio ());
      else
        g_value_set_boolean (value, DEFAULT_FORCE_ASPECT_RATIO);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      if (qt_sink->widget) {
        gint num, den;
        qt_sink->widget->getDAR (&num, &den);
        gst_value_set_fraction (value, num, den);
      } else {
        gst_value_set_fraction (value, DEFAULT_PAR_N, DEFAULT_PAR_D);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_qml6_sw_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstQml6SWSink *qt_sink = GST_QML6_SW_SINK (bsink);

  GST_DEBUG_OBJECT (qt_sink, "set caps with %" GST_PTR_FORMAT, caps);

  if (!gst_video_info_from_caps (&qt_sink->v_info, caps))
    return FALSE;

  if (!qt_sink->widget)
    return FALSE;

  return qt_sink->widget->setCaps(caps);
}

static gboolean
gst_qml6_sw_sink_stop (GstBaseSink * bsink)
{
  return TRUE;
}

static void
gst_qml6_sw_sink_finalize (GObject * object)
{
  GstQml6SWSink *qt_sink = GST_QML6_SW_SINK (object);

  qt_sink->widget.clear();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}
