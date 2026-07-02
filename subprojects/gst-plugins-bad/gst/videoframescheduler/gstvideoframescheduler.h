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

#ifndef __GST_VIDEO_FRAME_SCHEDULER_H__
#define __GST_VIDEO_FRAME_SCHEDULER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_FRAME_SCHEDULER (gst_video_frame_scheduler_get_type())
G_DECLARE_FINAL_TYPE (GstVideoFrameScheduler, gst_video_frame_scheduler, GST, VIDEO_FRAME_SCHEDULER,
    GstBaseTransform)

struct _GstVideoFrameScheduler
{
  GstBaseTransform parent;

  GstClockTime interval;
  GstClockTime start_time;
  GstClockTime prev_time;
  GstClockID wait_clock_id;
  GstClockID interval_clock_id;
  GstClock *clock;
  GstClock *system_clock;

  GAsyncQueue *render_queue;

  GArray *frames_diff_list;
  gint64 processed_frames;
  gint64 dropped_frames;
  gboolean frames_diff_list_updated;
  gboolean reset_requested;
};

GST_ELEMENT_REGISTER_DECLARE (videoframescheduler);

G_END_DECLS
#endif /* __GST_VIDEO_FRAME_SCHEDULER_H__ */
