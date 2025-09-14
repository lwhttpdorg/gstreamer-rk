/*
 * GStreamer VideoLAN NDI video source.
 *
 * Copyright (c) 2025 Michael Gruner <michael.gruner@ridgerun.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvlndisrc.h"
#include "vlndi.h"

#include <ndi/ndi.h>

struct _GstVlNdiSrc
{
  GstPushSrc parent;

  gchar *host;
  guint port;
  gint capture_timeout;
  gboolean stop;

  ndi_recv_ctx_t ctx;
  ndi_packet_t *pkt;
  GMutex pkt_mutex;
  GCond pkt_cond;
};

G_DEFINE_TYPE (GstVlNdiSrc, gst_vl_ndi_src, GST_TYPE_PUSH_SRC);
GST_ELEMENT_REGISTER_DEFINE (vl_ndi_src, "vlndisrc", GST_RANK_NONE,
    GST_VL_TYPE_NDI_SRC);
GST_DEBUG_CATEGORY_STATIC (gst_vl_ndi_src_debug);
#define GST_CAT_DEFAULT gst_vl_ndi_src_debug

static GstStaticPadTemplate src_factory =
    GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-speedhq ; video/x-h264 ; video/x-h265")
    );

enum
{
  PROP_0,
  PROP_HOST,
  PROP_PORT,
  PROP_CAPTURE_TIMEOUT,
};

#define PROP_HOST_DEFAULT "127.0.0.1"
#define PROP_PORT_DEFAULT 5960
#define PROP_PORT_MIN 0
#define PROP_PORT_MAX G_MAXUINT16
#define PROP_CAPTURE_TIMEOUT_DEFAULT 500
#define PROP_CAPTURE_TIMEOUT_MIN -1
#define PROP_CAPTURE_TIMEOUT_MAX G_MAXINT

static void gst_vl_ndi_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_vl_ndi_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_vl_ndi_src_finalize (GObject * object);

static gboolean gst_vl_ndi_src_start (GstBaseSrc * basesrc);
static gboolean gst_vl_ndi_src_stop (GstBaseSrc * basesrc);
static gboolean gst_vl_ndi_src_unlock (GstBaseSrc * src);
static gboolean gst_vl_ndi_src_unlock_stop (GstBaseSrc * src);

static GstFlowReturn gst_vl_ndi_src_create (GstPushSrc * src, GstBuffer ** buf);

static gint gst_vl_ndi_src_callback (ndi_packet_t * ndi_packet,
    GstVlNdiSrc * self);

static void
gst_vl_ndi_src_class_init (GstVlNdiSrcClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *src_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *push_class = GST_PUSH_SRC_CLASS (klass);

  object_class->set_property = gst_vl_ndi_src_set_property;
  object_class->get_property = gst_vl_ndi_src_get_property;
  object_class->finalize = gst_vl_ndi_src_finalize;

  g_object_class_install_property (object_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "Host IP address. Must be set in the NULL state.", PROP_HOST_DEFAULT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PORT,
      g_param_spec_uint ("port", "Port",
          "Port of the NDI device. Must be set in the NULL state.",
          PROP_PORT_MIN, PROP_PORT_MAX, PROP_PORT_DEFAULT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_CAPTURE_TIMEOUT,
      g_param_spec_int ("capture-timeout", "Capture Timeout",
          "Capture timeout in milliseconds to wait for a packet. Use -1 to wait forever.",
          PROP_CAPTURE_TIMEOUT_MIN, PROP_CAPTURE_TIMEOUT_MAX,
          PROP_CAPTURE_TIMEOUT_DEFAULT,
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  gst_element_class_set_static_metadata (element_class, "VideoLAN NDI Source",
      "Source/Video", "Reads frames from an NDI device",
      "Michael Gruner <michael.gruner@ridgerun.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  src_class->start = GST_DEBUG_FUNCPTR (gst_vl_ndi_src_start);
  src_class->stop = GST_DEBUG_FUNCPTR (gst_vl_ndi_src_stop);
  src_class->unlock = GST_DEBUG_FUNCPTR (gst_vl_ndi_src_unlock);
  src_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_vl_ndi_src_unlock_stop);

  push_class->create = GST_DEBUG_FUNCPTR (gst_vl_ndi_src_create);

  GST_DEBUG_CATEGORY_INIT (gst_vl_ndi_src_debug, "vlndisrc", 0,
      "VideoLAN NDI Source");
}

static void
gst_vl_ndi_src_init (GstVlNdiSrc * self)
{
  GstBaseSrc *bsrc = GST_BASE_SRC (self);

  GST_DEBUG_OBJECT (self, "initializing NDI src");

  self->host = g_strdup (PROP_HOST_DEFAULT);
  self->port = PROP_PORT_DEFAULT;
  self->capture_timeout = PROP_CAPTURE_TIMEOUT_DEFAULT;
  self->stop = FALSE;

  self->ctx = NULL;
  self->pkt = NULL;
  g_mutex_init (&self->pkt_mutex);
  g_cond_init (&self->pkt_cond);

  gst_base_src_set_live (bsrc, TRUE);
  gst_base_src_set_format (bsrc, GST_FORMAT_TIME);
}

static gint
gst_vl_ndi_src_callback (ndi_packet_t * ndi_packet, GstVlNdiSrc * self)
{
  gint ret = 0;

  g_return_val_if_fail (GST_VL_IS_NDI_SRC (self), -1);
  g_return_val_if_fail (ndi_packet, -1);

  switch (ndi_packet->type) {
    case NDI_DATA_VIDEO:{
      ndi_packet_video_t *video = (ndi_packet_video_t *) ndi_packet->packet;
      GST_LOG_OBJECT (self,
          "video data received (%dx%d) %lu %" GST_FOURCC_FORMAT, video->width,
          video->height, video->size, GST_FOURCC_ARGS (video->fourcc));
      break;
    }
    case NDI_DATA_AUDIO:{
      ndi_packet_audio_t *audio = (ndi_packet_audio_t *) ndi_packet->packet;
      GST_LOG_OBJECT (self, "audio data received (%d samples)",
          audio->num_samples);
      break;
    }
    case NDI_DATA_METADATA:{
      ndi_packet_metadata_t *meta =
          (ndi_packet_metadata_t *) ndi_packet->packet;
      GST_LOG_OBJECT (self, "metadata received: %.*s", (gint) meta->size,
          meta->data);
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "unknown NDI packet type received: %d",
          ndi_packet->type);
      ret = -1;
      break;
  }

  g_mutex_lock (&self->pkt_mutex);
  // Need to copy here, libndi will free the packet after we return from the cb
  vl_ndi_packet_free (self->pkt);
  self->pkt = vl_ndi_packet_copy_deep (ndi_packet);
  g_cond_signal (&self->pkt_cond);
  g_mutex_unlock (&self->pkt_mutex);

  return ret;
}

static GstFlowReturn
gst_vl_ndi_src_create (GstPushSrc * src, GstBuffer ** buf)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (src);
  GstFlowReturn ret = GST_FLOW_ERROR;
  gint status = 0;
  gint timeout = 0;
  ndi_packet_t *pkt = NULL;
  ndi_packet_video_t *video = NULL;
  gboolean stop = FALSE;

  GST_OBJECT_LOCK (self);
  timeout = self->capture_timeout;
  GST_OBJECT_UNLOCK (self);

  *buf = NULL;

  while (!*buf && !stop) {
    if (!ndi_recv_is_connected (self->ctx)) {
      GST_ERROR_OBJECT (self, "unexpected NDI disconnection");
      goto out;
    }

    status = ndi_recv_wait_capture (self->ctx, timeout);
    if (status < 0) {
      GST_ERROR_OBJECT (self, "failed to capture from the NDI device: %d",
          status);
      goto out;
    }

    g_mutex_lock (&self->pkt_mutex);
    while (!self->pkt && !self->stop) {
      g_cond_wait (&self->pkt_cond, &self->pkt_mutex);
    }
    pkt = self->pkt;
    self->pkt = NULL;
    stop = self->stop;
    g_mutex_unlock (&self->pkt_mutex);

    if (pkt->type != NDI_DATA_VIDEO || stop) {
      vl_ndi_packet_free (pkt);
      continue;
    }

    video = (ndi_packet_video_t *) pkt->packet;
    *buf =
        gst_buffer_new_wrapped_full (0, video->data, video->size, 0,
        video->size, pkt, (GDestroyNotify) vl_ndi_packet_free);
  }

  ret = GST_FLOW_OK;

out:
  return ret;
}

static gboolean
gst_vl_ndi_src_start (GstBaseSrc * basesrc)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (basesrc);
  ndi_opts opts = { 0 };
  const gboolean non_blocking = FALSE;
  gint status = 0;
  gboolean ret = FALSE;

  GST_INFO_OBJECT (self, "starting");

  GST_OBJECT_LOCK (self);
  opts.host = self->host;
  opts.port = self->port;
  opts.initial_tally_state = NDI_TALLY_LIVE;
  GST_OBJECT_UNLOCK (self);

  self->ctx =
      ndi_recv_create (&opts, (ndi_data_cb) gst_vl_ndi_src_callback, self,
      non_blocking);
  if (!self->ctx) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("failed to create the NDI context"), ("errno is: %u", errno));
    ret = FALSE;
    goto out;
  }

  status = ndi_recv_connect (self->ctx);
  if (status < 0) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("failed to connect to the NDI device"), ("errno is: %u", errno));
    goto free_ctx;
  }

  status = ndi_recv_send_metadata (self->ctx);
  if (status < 0) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("failed to send the metadata to the NDI device"), ("errno is: %u",
            errno));
    goto free_ctx;
  }

  ret = TRUE;
  goto out;

free_ctx:
  ndi_recv_close (self->ctx);
  self->ctx = NULL;

out:
  return ret;
}

static gboolean
gst_vl_ndi_src_unlock (GstBaseSrc * src)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (src);

  GST_DEBUG_OBJECT (self, "requested to unlock");

  g_mutex_lock (&self->pkt_mutex);
  self->stop = TRUE;
  g_cond_signal (&self->pkt_cond);
  g_mutex_unlock (&self->pkt_mutex);

  return TRUE;
}

static gboolean
gst_vl_ndi_src_unlock_stop (GstBaseSrc * src)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (src);

  GST_DEBUG_OBJECT (self, "request to unlock finished");

  g_mutex_lock (&self->pkt_mutex);
  self->stop = FALSE;
  g_mutex_unlock (&self->pkt_mutex);

  return TRUE;
}

static gboolean
gst_vl_ndi_src_stop (GstBaseSrc * basesrc)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (basesrc);
  gboolean ret = TRUE;

  GST_INFO_OBJECT (self, "stopping");

  if (ndi_recv_close (self->ctx) < 0) {
    GST_ERROR_OBJECT (self, "failed to close the NDI context");
    ret = FALSE;
  }
  self->ctx = NULL;

  return ret;
}

static void
gst_vl_ndi_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_PORT:
      self->port = g_value_get_uint (value);
      break;
    case PROP_HOST:
      g_free (self->host);
      self->host = g_value_dup_string (value);
      break;
    case PROP_CAPTURE_TIMEOUT:
      self->capture_timeout = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_vl_ndi_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (object);

  GST_OBJECT_LOCK (self);

  switch (prop_id) {
    case PROP_PORT:
      g_value_set_uint (value, self->port);
      break;
    case PROP_HOST:
      g_value_set_string (value, self->host);
      break;
    case PROP_CAPTURE_TIMEOUT:
      g_value_set_int (value, self->capture_timeout);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (self);
}

static void
gst_vl_ndi_src_finalize (GObject * object)
{
  GstVlNdiSrc *self = GST_VL_NDI_SRC (object);

  g_free (self->host);

  g_mutex_clear (&self->pkt_mutex);
  g_cond_clear (&self->pkt_cond);

  G_OBJECT_CLASS (gst_vl_ndi_src_parent_class)->finalize (object);
}
