/* GStreamer
 * Copyright (C) 2025 David Maseda Neira <david.maseda@cinfo.es>
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
 * SECTION:element-cudazoom
 * @title: cudazoom
 * @short_description: Zoom into a specific region of a video using CUDA
 *
 * cudazoom allows you to zoom into a specific region of a video frame.
 * You can specify either a center point with zoom factor, or explicit
 * source region coordinates (ROI - Region of Interest).
 *
 * ## Example launch line
 * ```
 * gst-launch-1.0 videotestsrc ! cudaupload ! cudazoom zoom-factor=2.0 center-x=320 center-y=240 ! cudadownload ! autovideosink
 * ```
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gst/cuda/gstcudautils.h>
#include <gst/cuda/gstcudamemory.h>
#include "gstcudazoom.h"
#include "gstcudaconverter.h"

GST_DEBUG_CATEGORY_STATIC (gst_cuda_zoom_debug);
#define GST_CAT_DEFAULT gst_cuda_zoom_debug

#define GST_CUDA_ZOOM_FORMATS \
    "{ I420, YV12, NV12, NV21, P010_10LE, P012_LE, P016_LE, I420_10LE, I420_12LE, Y444, " \
    "Y444_10LE, Y444_12LE, Y444_16LE, BGRA, RGBA, RGBx, BGRx, ARGB, ABGR, RGB, " \
    "BGR, BGR10A2_LE, RGB10A2_LE, Y42B, I422_10LE, I422_12LE, RGBP, BGRP, GBR, " \
    "GBRA, GBR_10LE, GBR_12LE, GBR_16LE, VUYA }"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_ZOOM_FORMATS))
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY, GST_CUDA_ZOOM_FORMATS))
    );

enum
{
  PROP_0,
  PROP_ZOOM_FACTOR,
  PROP_CENTER_X,
  PROP_CENTER_Y,
  PROP_SRC_X,
  PROP_SRC_Y,
  PROP_SRC_WIDTH,
  PROP_SRC_HEIGHT,
  PROP_USE_ROI,
};

#define DEFAULT_ZOOM_FACTOR 1.0
#define DEFAULT_CENTER_X -1
#define DEFAULT_CENTER_Y -1
#define DEFAULT_SRC_X 0
#define DEFAULT_SRC_Y 0
#define DEFAULT_SRC_WIDTH -1
#define DEFAULT_SRC_HEIGHT -1
#define DEFAULT_USE_ROI FALSE

static void gst_cuda_zoom_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cuda_zoom_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_cuda_zoom_dispose (GObject * object);
static void gst_cuda_zoom_finalize (GObject * object);
static gboolean gst_cuda_zoom_set_info (GstCudaBaseTransform * btrans,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info);
static GstFlowReturn gst_cuda_zoom_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

#define gst_cuda_zoom_parent_class parent_class
G_DEFINE_TYPE (GstCudaZoom, gst_cuda_zoom, GST_TYPE_CUDA_BASE_TRANSFORM);

static void
gst_cuda_zoom_class_init (GstCudaZoomClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *trans_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstCudaBaseTransformClass *btrans_class =
      GST_CUDA_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_cuda_zoom_set_property;
  gobject_class->get_property = gst_cuda_zoom_get_property;
  gobject_class->dispose = gst_cuda_zoom_dispose;
  gobject_class->finalize = gst_cuda_zoom_finalize;

  g_object_class_install_property (gobject_class, PROP_ZOOM_FACTOR,
      g_param_spec_float ("zoom-factor", "Zoom Factor",
          "Zoom factor (when not using ROI mode)", 0.1, 10.0,
          DEFAULT_ZOOM_FACTOR,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CENTER_X,
      g_param_spec_int ("center-x", "Center X",
          "X coordinate of zoom center (-1 for frame center)", -1, G_MAXINT,
          DEFAULT_CENTER_X,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_CENTER_Y,
      g_param_spec_int ("center-y", "Center Y",
          "Y coordinate of zoom center (-1 for frame center)", -1, G_MAXINT,
          DEFAULT_CENTER_Y,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SRC_X,
      g_param_spec_int ("src-x", "Source X",
          "X coordinate of source region (ROI mode)", 0, G_MAXINT,
          DEFAULT_SRC_X,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SRC_Y,
      g_param_spec_int ("src-y", "Source Y",
          "Y coordinate of source region (ROI mode)", 0, G_MAXINT,
          DEFAULT_SRC_Y,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SRC_WIDTH,
      g_param_spec_int ("src-width", "Source Width",
          "Width of source region (-1 for auto, ROI mode)", -1, G_MAXINT,
          DEFAULT_SRC_WIDTH,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_SRC_HEIGHT,
      g_param_spec_int ("src-height", "Source Height",
          "Height of source region (-1 for auto, ROI mode)", -1, G_MAXINT,
          DEFAULT_SRC_HEIGHT,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_USE_ROI,
      g_param_spec_boolean ("use-roi", "Use ROI",
          "Use explicit ROI coordinates instead of center+zoom",
          DEFAULT_USE_ROI,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class,
      "CUDA Zoom Effect", "Filter/Effect/Video",
      "Zoom into a specific region of video using CUDA",
      "David Maseda Neira <david.maseda@cinfo.es>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  trans_class->transform = GST_DEBUG_FUNCPTR (gst_cuda_zoom_transform);
  trans_class->passthrough_on_same_caps = FALSE;

  btrans_class->set_info = GST_DEBUG_FUNCPTR (gst_cuda_zoom_set_info);

  GST_DEBUG_CATEGORY_INIT (gst_cuda_zoom_debug, "cudazoom", 0,
      "CUDA Zoom Effect");
}

static void
gst_cuda_zoom_init (GstCudaZoom * self)
{
  self->zoom_factor = DEFAULT_ZOOM_FACTOR;
  self->center_x = DEFAULT_CENTER_X;
  self->center_y = DEFAULT_CENTER_Y;
  self->src_x = DEFAULT_SRC_X;
  self->src_y = DEFAULT_SRC_Y;
  self->src_width = DEFAULT_SRC_WIDTH;
  self->src_height = DEFAULT_SRC_HEIGHT;
  self->use_roi = DEFAULT_USE_ROI;
  g_mutex_init (&self->lock);
}

static void
gst_cuda_zoom_dispose (GObject * object)
{
  GstCudaZoom *self = GST_CUDA_ZOOM (object);

  gst_clear_object (&self->converter);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_cuda_zoom_finalize (GObject * object)
{
  GstCudaZoom *self = GST_CUDA_ZOOM (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cuda_zoom_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCudaZoom *self = GST_CUDA_ZOOM (object);

  g_mutex_lock (&self->lock);
  switch (prop_id) {
    case PROP_ZOOM_FACTOR:
      self->zoom_factor = g_value_get_float (value);
      break;
    case PROP_CENTER_X:
      self->center_x = g_value_get_int (value);
      break;
    case PROP_CENTER_Y:
      self->center_y = g_value_get_int (value);
      break;
    case PROP_SRC_X:
      self->src_x = g_value_get_int (value);
      break;
    case PROP_SRC_Y:
      self->src_y = g_value_get_int (value);
      break;
    case PROP_SRC_WIDTH:
      self->src_width = g_value_get_int (value);
      break;
    case PROP_SRC_HEIGHT:
      self->src_height = g_value_get_int (value);
      break;
    case PROP_USE_ROI:
      self->use_roi = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&self->lock);
}

static void
gst_cuda_zoom_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCudaZoom *self = GST_CUDA_ZOOM (object);

  g_mutex_lock (&self->lock);
  switch (prop_id) {
    case PROP_ZOOM_FACTOR:
      g_value_set_float (value, self->zoom_factor);
      break;
    case PROP_CENTER_X:
      g_value_set_int (value, self->center_x);
      break;
    case PROP_CENTER_Y:
      g_value_set_int (value, self->center_y);
      break;
    case PROP_SRC_X:
      g_value_set_int (value, self->src_x);
      break;
    case PROP_SRC_Y:
      g_value_set_int (value, self->src_y);
      break;
    case PROP_SRC_WIDTH:
      g_value_set_int (value, self->src_width);
      break;
    case PROP_SRC_HEIGHT:
      g_value_set_int (value, self->src_height);
      break;
    case PROP_USE_ROI:
      g_value_set_boolean (value, self->use_roi);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  g_mutex_unlock (&self->lock);
}

static gboolean
gst_cuda_zoom_set_info (GstCudaBaseTransform * btrans,
    GstCaps * incaps, GstVideoInfo * in_info, GstCaps * outcaps,
    GstVideoInfo * out_info)
{
  GstCudaZoom *self = GST_CUDA_ZOOM (btrans);
  gint src_x, src_y, src_w, src_h;
  gint width, height;

  gst_clear_object (&self->converter);

  self->converter = gst_cuda_converter_new (in_info, out_info,
      btrans->context, nullptr);
  if (!self->converter) {
    GST_ERROR_OBJECT (self, "Failed to create converter");
    return FALSE;
  }

  width = GST_VIDEO_INFO_WIDTH (in_info);
  height = GST_VIDEO_INFO_HEIGHT (in_info);

  g_mutex_lock (&self->lock);
  if (self->use_roi) {
    src_x = self->src_x;
    src_y = self->src_y;
    src_w = self->src_width > 0 ? self->src_width : width;
    src_h = self->src_height > 0 ? self->src_height : height;
  } else {
    gint cx = self->center_x >= 0 ? self->center_x : width / 2;
    gint cy = self->center_y >= 0 ? self->center_y : height / 2;
    src_w = (gint) (width / self->zoom_factor);
    src_h = (gint) (height / self->zoom_factor);
    src_x = cx - src_w / 2;
    src_y = cy - src_h / 2;
  }
  g_mutex_unlock (&self->lock);

  src_x = MAX (0, MIN (src_x, width));
  src_y = MAX (0, MIN (src_y, height));
  src_w = MAX (1, MIN (src_w, width - src_x));
  src_h = MAX (1, MIN (src_h, height - src_y));

  g_object_set (self->converter,
      "src-x", src_x,
      "src-y", src_y, "src-width", src_w, "src-height", src_h, nullptr);

  GST_DEBUG_OBJECT (self, "Zoom region: %d,%d %dx%d", src_x, src_y, src_w,
      src_h);

  return TRUE;
}

static GstFlowReturn
gst_cuda_zoom_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstCudaZoom *self = GST_CUDA_ZOOM (trans);
  GstCudaBaseTransform *btrans = GST_CUDA_BASE_TRANSFORM (trans);
  GstVideoFrame in_frame, out_frame;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean sync_done = FALSE;

  if (!gst_video_frame_map (&in_frame, &btrans->in_info, inbuf,
          (GstMapFlags) (GST_MAP_READ | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (self, "Failed to map input frame");
    return GST_FLOW_ERROR;
  }

  if (!gst_video_frame_map (&out_frame, &btrans->out_info, outbuf,
          (GstMapFlags) (GST_MAP_WRITE | GST_VIDEO_FRAME_MAP_FLAG_NO_REF))) {
    GST_ERROR_OBJECT (self, "Failed to map output frame");
    gst_video_frame_unmap (&in_frame);
    return GST_FLOW_ERROR;
  }

  if (!gst_cuda_converter_convert_frame (self->converter, &in_frame, &out_frame,
          btrans->
          stream ? gst_cuda_stream_get_handle (btrans->stream) : nullptr,
          &sync_done)) {
    GST_ERROR_OBJECT (self, "Failed to convert frame");
    ret = GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  return ret;
}
