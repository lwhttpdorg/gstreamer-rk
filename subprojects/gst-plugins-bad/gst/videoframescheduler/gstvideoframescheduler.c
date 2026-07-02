/*
 * (c) 2025 Inbok Kim <inbok.kim@lge.com>
 *
 * This file is made available under the terms of the GNU Lesser General Public License,
 * version 2.1 or any later version. You may redistribute and/or modify it under those terms.
 *
 * This software is provided as-is, without any warranty; without even the implied warranty
 * of merchantability or fitness for a particular purpose. For details, refer to the GNU LGPL.
 *
 * A copy of the license should be included with this source; if not, see:
 * https://www.gnu.org/licenses/lgpl-2.1.html
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoframescheduler.h"
#include <gst/video/video.h>

struct _FramesDiff
{
  gint64 processed_frames;
  gint64 dropped_frames;
};
typedef struct _FramesDiff FramesDiff;

GST_DEBUG_CATEGORY_STATIC (video_frame_scheduler_debug);
#define GST_CAT_DEFAULT video_frame_scheduler_debug

#define MAX_RENDER_QUEUE_LENGTH 2

static GstStaticPadTemplate gst_video_frame_scheduler_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstStaticPadTemplate gst_video_frame_scheduler_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw(ANY)")
    );

static GstFlowReturn gst_video_frame_scheduler_transform_ip (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean gst_video_frame_rate_scheduler_start (GstBaseTransform * trans);
static gboolean gst_video_frame_rate_scheduler_stop (GstBaseTransform * trans);
static GstCaps *gst_video_frame_rate_scheduler_fixate_caps (GstBaseTransform * trans,
   GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static gboolean gst_video_frame_rate_scheduler_sink_event (GstBaseTransform * trans, GstEvent * event);
static gboolean gst_video_frame_rate_scheduler_src_event (GstBaseTransform * trans, GstEvent * event);

static gboolean gst_video_frame_rate_scheduler_reset (GstVideoFrameScheduler * videoframescheduler);
static gboolean gst_video_frame_rate_scheduler_clock_cb (GstClock * clock,
    GstClockTime time, GstClockID id, gpointer user_data);
static void gst_video_frame_rate_scheduler_request_reset_if_needed (GstVideoFrameScheduler * videoframescheduler);

#define gst_video_frame_scheduler_parent_class parent_class
G_DEFINE_TYPE (GstVideoFrameScheduler, gst_video_frame_scheduler, GST_TYPE_BASE_TRANSFORM);

static void
gst_video_frame_scheduler_class_init (GstVideoFrameSchedulerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseTransformClass *base_class = GST_BASE_TRANSFORM_CLASS (klass);

  gst_base_transform_set_passthrough (base_class, TRUE);
  base_class->transform_ip = GST_DEBUG_FUNCPTR (gst_video_frame_scheduler_transform_ip);
  base_class->start = GST_DEBUG_FUNCPTR (gst_video_frame_rate_scheduler_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_video_frame_rate_scheduler_stop);
  base_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_video_frame_rate_scheduler_fixate_caps);
  base_class->sink_event = GST_DEBUG_FUNCPTR (gst_video_frame_rate_scheduler_sink_event);
  base_class->src_event = GST_DEBUG_FUNCPTR (gst_video_frame_rate_scheduler_src_event);

  gst_element_class_set_static_metadata (element_class,
      "Video frame scheduler", "Scheduler/Video",
      "Video frame scheduler",
      "Inbok Kim <inbok.kim@lge.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_video_frame_scheduler_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_video_frame_scheduler_src_template);
}

static void
gst_video_frame_scheduler_init (GstVideoFrameScheduler * videoframescheduler)
{
  videoframescheduler->interval = GST_CLOCK_TIME_NONE;
  videoframescheduler->start_time = GST_CLOCK_TIME_NONE;
  videoframescheduler->prev_time = GST_CLOCK_TIME_NONE;
  videoframescheduler->wait_clock_id = NULL;
  videoframescheduler->interval_clock_id = NULL;
  videoframescheduler->clock = NULL;
  videoframescheduler->system_clock = NULL;
  videoframescheduler->render_queue = NULL;
  videoframescheduler->frames_diff_list = NULL;
  videoframescheduler->processed_frames = -1;
  videoframescheduler->dropped_frames = -1;
  videoframescheduler->reset_requested = FALSE;

  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (videoframescheduler), TRUE);
}

static GstFlowReturn
gst_video_frame_scheduler_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstVideoFrameScheduler *videoframescheduler = GST_VIDEO_FRAME_SCHEDULER (trans);

  if (videoframescheduler->reset_requested) {
    gst_video_frame_rate_scheduler_reset (videoframescheduler);
    return GST_FLOW_OK;
  }

  if (videoframescheduler->clock == NULL) {
    videoframescheduler->clock = gst_element_get_clock (videoframescheduler);
  } else {
    if (videoframescheduler->interval_clock_id == NULL) {
      videoframescheduler->start_time = gst_clock_get_time (videoframescheduler->clock);
      if (GST_CLOCK_TIME_IS_VALID (videoframescheduler->start_time)) {
        videoframescheduler->interval_clock_id = gst_clock_new_periodic_id (videoframescheduler->clock,
            videoframescheduler->start_time, videoframescheduler->interval);
        gst_clock_id_wait_async (videoframescheduler->interval_clock_id,
            gst_video_frame_rate_scheduler_clock_cb, videoframescheduler, NULL);
        videoframescheduler->prev_time = videoframescheduler->start_time;
      }
    }

    while (videoframescheduler->render_queue != NULL &&
        videoframescheduler->interval_clock_id != NULL) {
      if (g_async_queue_length (videoframescheduler->render_queue) < MAX_RENDER_QUEUE_LENGTH) {
        gst_buffer_ref (buf);
        g_async_queue_push (videoframescheduler->render_queue, buf);

        return GST_BASE_TRANSFORM_FLOW_DROPPED;
      }

      if (videoframescheduler->system_clock != NULL) {
        videoframescheduler->wait_clock_id = gst_clock_new_single_shot_id (videoframescheduler->system_clock,
            gst_clock_get_time (videoframescheduler->system_clock) + videoframescheduler->interval * 2);
        if (videoframescheduler->wait_clock_id != NULL) {
          GstClockReturn ret = gst_clock_id_wait (videoframescheduler->wait_clock_id, NULL);
          gst_clock_id_unref (videoframescheduler->wait_clock_id);
          videoframescheduler->wait_clock_id = NULL;
          if (ret != GST_CLOCK_UNSCHEDULED) {
            gst_video_frame_rate_scheduler_reset (videoframescheduler);
            break;
          }
        }
      } else {
        gst_video_frame_rate_scheduler_reset (videoframescheduler);
        break;
      }
    }
  }

  return GST_FLOW_OK;
}

static gboolean
gst_video_frame_rate_scheduler_start (GstBaseTransform * trans)
{
  GstVideoFrameScheduler *videoframescheduler = GST_VIDEO_FRAME_SCHEDULER (trans);

  gst_video_frame_rate_scheduler_reset (videoframescheduler);

  if (videoframescheduler->system_clock == NULL) {
    videoframescheduler->system_clock = gst_system_clock_obtain ();
    g_assert (videoframescheduler->system_clock != NULL);
  }

  if (videoframescheduler->render_queue == NULL) {
    videoframescheduler->render_queue = g_async_queue_new ();
    g_assert (videoframescheduler->render_queue != NULL);
  }

  if (videoframescheduler->frames_diff_list == NULL) {
    videoframescheduler->frames_diff_list = g_array_new (FALSE, FALSE, sizeof (FramesDiff));
    g_assert (videoframescheduler->frames_diff_list != NULL);
  }

  return TRUE;
}

static gboolean
gst_video_frame_rate_scheduler_stop (GstBaseTransform * trans)
{
  GstVideoFrameScheduler *videoframescheduler = GST_VIDEO_FRAME_SCHEDULER (trans);

  gst_video_frame_rate_scheduler_reset (videoframescheduler);

  if (videoframescheduler->system_clock != NULL) {
    gst_object_unref (videoframescheduler->system_clock);
    videoframescheduler->system_clock = NULL;
  }

  if (videoframescheduler->render_queue != NULL) {
    g_async_queue_unref (videoframescheduler->render_queue);
    videoframescheduler->render_queue = NULL;
  }

  if (videoframescheduler->frames_diff_list != NULL) {
    videoframescheduler->frames_diff_list = g_array_free (videoframescheduler->frames_diff_list, TRUE);
    videoframescheduler->frames_diff_list= NULL;
  }

  return TRUE;
}

static GstCaps *
gst_video_frame_rate_scheduler_fixate_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstVideoFrameScheduler *videoframescheduler = GST_VIDEO_FRAME_SCHEDULER (trans);

  GstStructure *s;
  gint num, denom;

  s = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_fraction (s, "framerate", &num, &denom)) {
    videoframescheduler->interval = (((GstClockTime)denom * GST_SECOND) / ((GstClockTime)num));
  }

  return othercaps;
}

static gboolean
gst_video_frame_rate_scheduler_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoFrameScheduler *videoframescheduler = GST_VIDEO_FRAME_SCHEDULER (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_video_frame_rate_scheduler_reset (videoframescheduler);
      break;

    case GST_EVENT_EOS:
      gst_video_frame_rate_scheduler_reset (videoframescheduler);
      break;

    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);
}

static gboolean
gst_video_frame_rate_scheduler_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoFrameScheduler *videoframescheduler = GST_VIDEO_FRAME_SCHEDULER (trans);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_QOS:
    {
      GstQOSType type;
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &type, &proportion, &diff, &timestamp);
      break;
    }

    case GST_EVENT_CUSTOM_UPSTREAM:
    {
      GstStructure *structure = gst_event_get_structure (event);
      if (structure != NULL) {
        gchar *name = gst_structure_get_name (structure);
        if (strcmp (name, "qos-stats") == 0) {
          gint64 processed_frames, dropped_frames;
          gst_structure_get_int64 (structure, "processed-frames", &processed_frames);
          gst_structure_get_int64 (structure, "dropped-frames", &dropped_frames);
          if (videoframescheduler->processed_frames > 0 &&
              videoframescheduler->dropped_frames > 0) {
            FramesDiff diff = {
                .processed_frames = processed_frames - videoframescheduler->processed_frames,
                .dropped_frames = dropped_frames - videoframescheduler->dropped_frames
            };
            g_array_append_vals (videoframescheduler->frames_diff_list, &diff, 1);
            gst_video_frame_rate_scheduler_request_reset_if_needed (videoframescheduler);
          }

          videoframescheduler->processed_frames = processed_frames;
          videoframescheduler->dropped_frames = dropped_frames;
        }
      }

      break;
    }

    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);
}

static gboolean
gst_video_frame_rate_scheduler_reset (GstVideoFrameScheduler * videoframescheduler)
{
  videoframescheduler->start_time = GST_CLOCK_TIME_NONE;
  videoframescheduler->prev_time = GST_CLOCK_TIME_NONE;

  if (videoframescheduler->wait_clock_id) {
    gst_clock_id_unschedule (videoframescheduler->wait_clock_id);
    gst_clock_id_unref (videoframescheduler->wait_clock_id);
    videoframescheduler->wait_clock_id = NULL;
  }

  if (videoframescheduler->interval_clock_id) {
    gst_clock_id_unschedule (videoframescheduler->interval_clock_id);
    gst_clock_id_unref (videoframescheduler->interval_clock_id);
    videoframescheduler->interval_clock_id = NULL;
  }

  if (videoframescheduler->clock != NULL) {
    gst_object_unref (videoframescheduler->clock);
    videoframescheduler->clock = NULL;
  }

  if (videoframescheduler->render_queue != NULL) {
    GstBuffer *buf = NULL;
    while ((buf = g_async_queue_try_pop (videoframescheduler->render_queue)) != NULL) {
      gst_buffer_unref (buf);
      buf = NULL;
    }
  }

  if (videoframescheduler->frames_diff_list != NULL) {
    g_array_set_size (videoframescheduler->frames_diff_list, 0);
  }

  videoframescheduler->processed_frames = -1;
  videoframescheduler->dropped_frames = -1;
  videoframescheduler->reset_requested = FALSE;

  return TRUE;
}

static gboolean gst_video_frame_rate_scheduler_clock_cb (GstClock * clock,
    GstClockTime time, GstClockID id, gpointer user_data)
{
  GstVideoFrameScheduler *videoframescheduler = GST_VIDEO_FRAME_SCHEDULER (user_data);
  GstClockTimeDiff diff = GST_CLOCK_DIFF (videoframescheduler->prev_time, time);

  if (videoframescheduler->interval_clock_id != NULL) {
    gst_clock_id_wait_async (videoframescheduler->interval_clock_id,
        gst_video_frame_rate_scheduler_clock_cb, videoframescheduler, NULL);
    videoframescheduler->prev_time = time;
  }

  if (videoframescheduler->render_queue != NULL) {
    GstBuffer *buf = NULL;
    if ((buf = g_async_queue_try_pop (videoframescheduler->render_queue)) != NULL) {
      if (videoframescheduler->wait_clock_id != NULL) {
        gst_clock_id_unschedule (videoframescheduler->wait_clock_id);
      }

      gst_pad_push (GST_BASE_TRANSFORM_SRC_PAD (videoframescheduler), buf);
    }
  }

  return TRUE;
}

static void
gst_video_frame_rate_scheduler_request_reset_if_needed (GstVideoFrameScheduler * videoframescheduler)
{
  gint64 total_processed_frames = 0;
  gint64 total_dropped_frames = 0;

  if (videoframescheduler->frames_diff_list->len > 10) { // FIX ME
    g_array_remove_index (videoframescheduler->frames_diff_list, 0);
  } else {
    return;
  }

  for (guint i = 0; i < videoframescheduler->frames_diff_list->len; i++) {
    FramesDiff diff = g_array_index (videoframescheduler->frames_diff_list, FramesDiff, i);
    total_processed_frames += diff.processed_frames;
    total_dropped_frames += diff.dropped_frames;
  }

  if (total_dropped_frames / total_processed_frames > 0.1) { // FIX ME
    videoframescheduler->reset_requested = TRUE;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (video_frame_scheduler_debug, "videoframescheduler", 0,
      "Video frame scheduler");

 return gst_element_register (plugin, "videoframescheduler", GST_RANK_NONE,
        GST_TYPE_VIDEO_FRAME_SCHEDULER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videoframescheduler,
    "Video frame scheduler",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
