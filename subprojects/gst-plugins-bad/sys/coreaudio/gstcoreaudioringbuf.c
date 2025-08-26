/* GStreamer
 * Copyright (C) 2025 Piotr Brzeziński <piotr@centricular.com>
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

/*
 * This is a GstAudioRingBuffer implementation for CoreAudio-based
 * elements. The ringbuffer itself does not directly interact with
 * CoreAudio, but rather manages and calls into GstCoreAudioCtx instances
 * which handle the underlying AudioUnits and other CoreAudio specifics.
 *
 * The ringbuffer runs two threads:
  * - rbuf_loop: main processing loop, handles:
  *     - commands from the parent element (start/stop/acquire/prop changes etc.)
  *     - monitoring status of the context (e.g. device disconnects)
  *     - consuming/producing silence when no context is running
  * - ctx_loop: handles device setup requests from rbuf_loop:
  *     - will create and prepare (warmup) GstCoreAudioCtx instances
  *     - can do so synchronously (at startup) or asynchronously (on device change
  *       when the element is already running)
  *
  * In addition to that, CoreAudio will call into sink_callback/source_callback
  * from its own thread when it has or needs data to render.
  *
  * Communication between the two ringbuffer threads and the outside world is done
  * via a command queue. Calls from outside will land in the rbuf_loop queue and will be
  * processed in order of arrival, all in the same thread. rbuf_loop might send requests
  * to ctx_loop to create new contexts when needed. Those will be processed in background,
  * possibly waiting for device connection to finish when needed, and then sent back to the
  * main rbuf_loop via an UPDATE_DEVICE command to swap in the new context on the fly.
  *
  * The above architecture is heavily inspired by the Windows GstWasapi2Rbuf implementation,
  * trying to achieve similar goals of avoiding race conditions and unnecessary locking
  * by processing most requests in a single thread, while allowing seamless device changes
  * and handling any underlying errors without disrupting other parts of the pipeline.
*/

#include "gstcoreaudioringbuf.h"
#include "gstcoreaudiocontext.h"
#include <mach/mach_time.h>
#include <sys/event.h>

GST_DEBUG_CATEGORY_STATIC (gst_coreaudio_rbuf_debug);
#define GST_CAT_DEFAULT gst_coreaudio_rbuf_debug

#define gst_coreaudio_rbuf_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstCoreAudioRbuf, gst_coreaudio_rbuf,
    GST_TYPE_AUDIO_RING_BUFFER,
    GST_DEBUG_CATEGORY_INIT (gst_coreaudio_rbuf_debug, "coreaudiosrc", 0,
        "CoreAudio audio source"));

static inline void
command_init (CommandData * cmd, CommandType type)
{
  cmd->type = type;
  cmd->success = TRUE;
  cmd->completed = FALSE;
  g_mutex_init (&cmd->mutex);
  g_cond_init (&cmd->cond);
}

static inline void
command_cleanup (CommandData * cmd)
{
  g_mutex_clear (&cmd->mutex);
  g_cond_clear (&cmd->cond);
}

static inline gboolean
command_wait (CommandData * cmd)
{
  g_mutex_lock (&cmd->mutex);
  while (!cmd->completed) {
    g_cond_wait (&cmd->cond, &cmd->mutex);
  }
  g_mutex_unlock (&cmd->mutex);

  return cmd->success;
}

static inline void
command_signal_done (CommandData * cmd, gboolean success)
{
  g_mutex_lock (&cmd->mutex);
  cmd->success = success;
  cmd->completed = TRUE;
  g_cond_signal (&cmd->cond);
  g_mutex_unlock (&cmd->mutex);
}

typedef struct
{
  CommandData base;

  gchar *device_uid;
  GstCoreAudioDeviceMode mode;
} CommandSetDevice;

typedef struct
{
  CommandData base;

  GstCoreAudioCtx *ctx;
  gchar *device_id;
} CommandUpdateDevice;

typedef struct
{
  CommandData base;

  GstCaps *caps;
} CommandGetCaps;

typedef struct
{
  CommandData base;

  GstAudioRingBufferSpec *spec;
} CommandAcquire;

typedef struct
{
  CommandData base;

  CtxRequest *request;
} CommandCreateCtx;

static inline CommandSetDevice *
command_set_device_new (gchar * device_uid, GstCoreAudioDeviceMode mode)
{
  CommandSetDevice *cmd = g_new0 (CommandSetDevice, 1);
  command_init (&cmd->base, COMMAND_SET_DEVICE);
  cmd->device_uid = device_uid;
  cmd->mode = mode;
  return cmd;
}

static inline CommandUpdateDevice *
command_update_device_new (gchar * device_uid, GstCoreAudioCtx * ctx)
{
  CommandUpdateDevice *cmd = g_new0 (CommandUpdateDevice, 1);
  command_init (&cmd->base, COMMAND_UPDATE_DEVICE);
  cmd->device_id = device_uid;
  cmd->ctx = ctx;
  return cmd;
}

static inline CommandGetCaps *
command_get_caps_new ()
{
  CommandGetCaps *cmd = g_new0 (CommandGetCaps, 1);
  command_init (&cmd->base, COMMAND_GET_CAPS);
  return cmd;
}

static inline CommandAcquire *
command_acquire_new (GstAudioRingBufferSpec * spec)
{
  CommandAcquire *cmd = g_new0 (CommandAcquire, 1);
  command_init (&cmd->base, COMMAND_ACQUIRE);
  cmd->spec = spec;
  return cmd;
}

static inline CommandCreateCtx *
command_create_ctx_new (CtxRequest * req)
{
  CommandCreateCtx *cmd = g_new0 (CommandCreateCtx, 1);
  command_init (&cmd->base, COMMAND_CREATE_CTX);
  cmd->request = req;
  return cmd;
}

static inline CommandData *
command_new (CommandType type)
{
  CommandData *cmd = g_new0 (CommandData, 1);
  command_init (cmd, type);
  return cmd;
}

static inline void
command_free (CommandData * cmd)
{
  if (cmd) {
    command_cleanup (cmd);
    g_free (cmd);
  }
}

static const gchar *
command_type_to_string (CommandType type)
{
  switch (type) {
    case COMMAND_SET_DEVICE:
      return "SetDevice";
    case COMMAND_UPDATE_DEVICE:
      return "UpdateDevice";
    case COMMAND_GET_CAPS:
      return "GetCaps";
    case COMMAND_ACQUIRE:
      return "Acquire";
    case COMMAND_CREATE_CTX:
      return "CreateCtx";
    case COMMAND_SHUTDOWN:
      return "Shutdown";
    case COMMAND_OPEN:
      return "Open";
    case COMMAND_CLOSE:
      return "Close";
    case COMMAND_RELEASE:
      return "Release";
    case COMMAND_START:
      return "Start";
    case COMMAND_STOP:
      return "Stop";
    default:
      return "Unknown";
  }
}

static void
gst_coreaudio_rbuf_loop_push_command (GstCoreAudioRbuf * self,
    CommandData * cmd)
{
  struct kevent kev;

  g_mutex_lock (&self->rbuf_lock);
  gst_vec_deque_push_tail (self->rbuf_queue, cmd);
  g_mutex_unlock (&self->rbuf_lock);

  EV_SET (&kev, RBUF_EVT_COMMAND, EVFILT_USER, 0, NOTE_TRIGGER, 0, NULL);
  kevent (self->rbuf_kqueue, &kev, 1, NULL, 0, NULL);

  GST_DEBUG_OBJECT (self, "Pushed command %s",
      command_type_to_string (cmd->type));
}

static void
gst_coreaudio_rbuf_ctx_loop_push_command (GstCoreAudioRbuf * self,
    CommandData * cmd)
{
  g_mutex_lock (&self->ctx_lock);
  gst_vec_deque_push_tail (self->ctx_queue, cmd);
  g_cond_signal (&self->ctx_cond);
  g_mutex_unlock (&self->ctx_lock);
}

static CommandCreateCtx *
make_request_cmd (GstCoreAudioRbuf * rbuf, gchar * device_uid,
    GstCoreAudioDeviceMode mode, guint32 requested_fpp,
    AudioStreamBasicDescription * format, AudioChannelLayout * layout)
{
  CtxRequest *req = g_new0 (CtxRequest, 1);
  req->rbuf = rbuf ? gst_object_ref (rbuf) : NULL;
  req->device_uid = device_uid;
  req->mode = mode;
  req->frames_per_packet = requested_fpp;
  req->ctx = NULL;
  req->format = format ? asbd_copy (format) : NULL;
  req->layout = layout ? aclayout_copy (layout) : NULL;

  return command_create_ctx_new (req);
}

static GstCoreAudioCtx *
gst_coreaudio_rbuf_ctx_loop_request_ctx (GstCoreAudioRbuf * self,
    gchar * device_uid, GstCoreAudioDeviceMode mode, guint32 requested_fpp,
    AudioStreamBasicDescription * format, AudioChannelLayout * layout)
{
  GstCoreAudioCtx *ctx = NULL;
  CommandCreateCtx *cmd;
  gboolean success;

  GST_DEBUG_OBJECT (self, "Requesting ctx for device %s, mode %d",
      DEVICE_UID_STR (device_uid), mode);

  cmd =
      make_request_cmd (NULL, device_uid, mode, requested_fpp, format, layout);
  gst_coreaudio_rbuf_ctx_loop_push_command (self, (CommandData *) cmd);
  success = command_wait (&cmd->base);

  if (success) {
    ctx = cmd->request->ctx;
    GST_DEBUG_OBJECT (self, "Received ctx %p for device %s", ctx,
        DEVICE_UID_STR (device_uid));
  } else {
    GST_ERROR_OBJECT (self, "Failed to create context for device %s",
        DEVICE_UID_STR (device_uid));
  }

  g_free (cmd->request);
  command_free (&cmd->base);
  return ctx;
}

static void
gst_coreaudio_rbuf_ctx_loop_request_ctx_async (GstCoreAudioRbuf * self,
    gchar * device_uid, GstCoreAudioDeviceMode mode, guint32 requested_fpp,
    AudioStreamBasicDescription * asbd, AudioChannelLayout * layout)
{
  CommandCreateCtx *cmd = make_request_cmd (self, device_uid, mode,
      requested_fpp, asbd, layout);

  /* This will come back to main loop as an UpdateDevice command */
  gst_coreaudio_rbuf_ctx_loop_push_command (self, (CommandData *) cmd);
}

static void
gst_coreaudio_rbuf_ctx_loop (GstCoreAudioRbuf * self)
{
  gboolean loop_running = TRUE;
  CommandData *cmd = NULL;

  GST_DEBUG_OBJECT (self, "Ctx loop starting");

  /* TODO: in the future this could also handle e.g. following the default device
   * or reconnecting to a previously disconnected device etc.
   * An AudioUnit will usually try autoswitch once a device is disconnected,
   * but we detect that and shut down the context because it breaks more than it helps. */

  while (loop_running) {
    g_mutex_lock (&self->ctx_lock);
    while (gst_vec_deque_is_empty (self->ctx_queue) && loop_running) {
      g_cond_wait (&self->ctx_cond, &self->ctx_lock);
    }

    while ((cmd = (CommandData *) gst_vec_deque_pop_head (self->ctx_queue))) {
      const gchar *cmd_type = command_type_to_string (cmd->type);
      GST_LOG_OBJECT (self, "Processing rbuf ctx command %s", cmd_type);

      switch (cmd->type) {
        case COMMAND_SHUTDOWN:
          loop_running = FALSE;
          command_signal_done (cmd, TRUE);
          break;
        case COMMAND_CREATE_CTX:{
          CommandCreateCtx *req_cmd = (CommandCreateCtx *) cmd;
          CtxRequest *req;
          GstCoreAudioCtx *ctx;
          gboolean ret = TRUE;

          g_mutex_unlock (&self->ctx_lock);

          req = req_cmd->request;
          GST_DEBUG_OBJECT (self,
              "Processing ctx request for device %s, mode %d",
              DEVICE_UID_STR (req->device_uid), req->mode);

          ctx = gst_coreaudio_ctx_new (req->mode, req->device_uid);

          if (!ctx) {
            GST_ERROR_OBJECT (self,
                "Couldn't create context for device %s mode %d",
                DEVICE_UID_STR (req->device_uid), req->mode);
            ret = FALSE;
          } else if (!req->format) {
            req->ctx = ctx;
            GST_DEBUG_OBJECT (self, "Created context %p without format", ctx);
          } else if (gst_coreaudio_ctx_prepare (ctx, req->format, req->layout,
                  req->frames_per_packet)) {
            req->ctx = ctx;
            GST_DEBUG_OBJECT (self, "Created context %p", ctx);
          } else {
            GST_ERROR_OBJECT (self,
                "Couldn't prepare context for device %s mode %d",
                DEVICE_UID_STR (req->device_uid), req->mode);
            gst_coreaudio_ctx_free (ctx);
            ret = FALSE;
          }

          /* Sync requests wait for the ret value, async ones don't care */
          command_signal_done (&req_cmd->base, ret);

          if (req->rbuf) {
            /* For an async request, send update-device command back to rbuf loop */
            CommandUpdateDevice *up_cmd =
                command_update_device_new (req->device_uid, req->ctx);

            GST_DEBUG ("Sending UpdateDevice to rbuf with ctx %p", req->ctx);

            gst_coreaudio_rbuf_loop_push_command (req->rbuf, &up_cmd->base);
            command_wait (&up_cmd->base);
            command_free (&up_cmd->base);
            gst_object_unref (req->rbuf);

            /* The caller (rbuf loop) won't wait / free the command in this case */
            command_free (&req_cmd->base);
          }

          g_mutex_lock (&self->ctx_lock);
          break;
        }
        default:
          GST_WARNING_OBJECT (self, "Unknown rbuf ctx command %s", cmd_type);
          command_signal_done (cmd, FALSE);
          break;
      }
    }

    g_mutex_unlock (&self->ctx_lock);
  }

  GST_DEBUG_OBJECT (self, "Ctx loop exiting");
}

static GstCoreAudioCtx *
gst_coreaudio_rbuf_create_ctx (GstCoreAudioRbuf * self)
{
  return gst_coreaudio_rbuf_ctx_loop_request_ctx (self, self->device_uid,
      self->mode, self->frames_per_packet, self->format, self->layout);
}

static void
gst_coreaudio_rbuf_create_ctx_async (GstCoreAudioRbuf * self)
{
  gst_coreaudio_rbuf_ctx_loop_request_ctx_async (self, self->device_uid,
      self->mode, self->frames_per_packet, self->format, self->layout);
}

/* Those are all warnings for now because we always enable the fallback timer.
 * Should be configurable in the future. */
static void
gst_coreaudio_rbuf_post_open_warn (GstCoreAudioRbuf * self,
    const gchar * device_id)
{
  gpointer *parent = g_weak_ref_get (&self->parent_element);

  if (!parent) {
    GST_ERROR_OBJECT (self, "No parent anymore");
    return;
  }

  GST_ELEMENT_WARNING (parent, RESOURCE, OPEN_READ_WRITE, (NULL),
      ("Failed to open CoreAudio device with UID %s",
          DEVICE_UID_STR (device_id)));

  g_object_unref (parent);
}

static void
gst_coreaudio_rbuf_post_device_warn (GstCoreAudioRbuf * self,
    const gchar * device_id)
{
  gpointer *parent = g_weak_ref_get (&self->parent_element);

  if (!parent) {
    GST_ERROR_OBJECT (self, "No parent anymore");
    return;
  }

  if (self->mode == GST_COREAUDIO_DEVICE_MODE_SINK) {
    GST_ELEMENT_WARNING (parent, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Failed to write to CoreAudio device UID %s",
            DEVICE_UID_STR (device_id)));
  } else {
    GST_ELEMENT_WARNING (parent, RESOURCE, OPEN_READ_WRITE, (NULL),
        ("Failed to read from CoreAudio device UID %s",
            DEVICE_UID_STR (device_id)));
  }

  g_object_unref (parent);
}

static void
gst_coreaudio_rbuf_start_fallback_timer (GstCoreAudioRbuf * self)
{
  struct kevent ev;

  GstAudioRingBuffer *rbuf = GST_AUDIO_RING_BUFFER (self);
  gint period_frames;
  guint64 period_ns, period_abs;

  if (self->fallback_timer_active)
    return;

  GST_DEBUG_OBJECT (self, "Starting fallback timer");

  period_frames = rbuf->spec.segsize / GST_AUDIO_INFO_BPF (&rbuf->spec.info);
  period_ns =
      (1000000000ULL * period_frames) / GST_AUDIO_INFO_RATE (&rbuf->spec.info);
  period_abs = nanoseconds_to_mach_absolute_time (period_ns);

  /* NOTE_MACHTIME + NOTE_CRITICAL is the highest res timer we can get on macOS */
  EV_SET (&ev, RBUF_EVT_FALLBACK_TIMER, EVFILT_TIMER,
      EV_ADD, NOTE_MACHTIME | NOTE_CRITICAL, period_abs, NULL);
  kevent (self->rbuf_kqueue, &ev, 1, NULL, 0, NULL);

  self->fallback_frames_processed = 0;
  self->fallback_start_time = mach_absolute_time ();
  self->fallback_timer_active = TRUE;
}

static void
gst_coreaudio_rbuf_stop_fallback_timer (GstCoreAudioRbuf * self)
{
  struct kevent ev;

  if (!self->fallback_timer_active)
    return;

  GST_DEBUG_OBJECT (self, "Stopping fallback timer");

  EV_SET (&ev, RBUF_EVT_FALLBACK_TIMER, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
  kevent (self->rbuf_kqueue, &ev, 1, NULL, 0, NULL);
  self->fallback_timer_active = FALSE;
}

static void
gst_coreaudio_rbuf_start_monitor_timer (GstCoreAudioRbuf * self)
{
  struct kevent ev;

  if (self->monitor_timer_active)
    return;

  GST_DEBUG_OBJECT (self, "Starting monitor timer");

  /* 15ms = 15000us time to watch for device disconnects and other errors.
   * Those will be indicated by the context based on CoreAudio events for now,
   * but in the future we might want to manually monitor e.g. sample count
   * in case CoreAudio itself fails completely */
  EV_SET (&ev, RBUF_EVT_MONITOR_TIMER, EVFILT_TIMER,
      EV_ADD, NOTE_USECONDS | NOTE_CRITICAL, 15000, NULL);
  kevent (self->rbuf_kqueue, &ev, 1, NULL, 0, NULL);
  self->monitor_timer_active = TRUE;
}

static void
gst_coreaudio_rbuf_stop_monitor_timer (GstCoreAudioRbuf * self)
{
  struct kevent ev;

  if (!self->monitor_timer_active)
    return;

  GST_DEBUG_OBJECT (self, "Stopping monitor timer");

  EV_SET (&ev, RBUF_EVT_MONITOR_TIMER, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
  kevent (self->rbuf_kqueue, &ev, 1, NULL, 0, NULL);
  self->monitor_timer_active = FALSE;
}

static OSStatus
gst_coreaudio_rbuf_sink_callback (GstCoreAudioRbuf * self,
    AudioUnitRenderActionFlags * ioActionFlags,
    const AudioTimeStamp * inTimeStamp,
    UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList * bufferList)
{
  guint8 *readptr;
  gint readseg;
  gint len;
  gint stream_idx = 0;
  gint remaining = bufferList->mBuffers[stream_idx].mDataByteSize;
  gint offset = 0;
  GstAudioRingBuffer *rbuf = GST_AUDIO_RING_BUFFER (self);

  if (self->ctx->should_stop) {
    GST_DEBUG_OBJECT (self, "Context about to stop, not rendering");
    return noErr;
  }

  GST_LOG_OBJECT (self, "out sample position %f frames %u",
      inTimeStamp->mSampleTime, inNumberFrames);

  while (remaining) {
    if (!gst_audio_ring_buffer_prepare_read (rbuf, &readseg, &readptr, &len))
      return 0;

    len -= self->segoffset;

    if (len > remaining)
      len = remaining;

    memcpy ((char *) bufferList->mBuffers[stream_idx].mData + offset,
        readptr + self->segoffset, len);

    self->segoffset += len;
    offset += len;
    remaining -= len;

    if ((gint) self->segoffset == rbuf->spec.segsize) {
      /* osxaudio had more precise timing around here, FIXME */
      gst_audio_ring_buffer_clear (rbuf, readseg);
      gst_audio_ring_buffer_advance (rbuf, 1);
      self->segoffset = 0;
    }
  }

  return noErr;
}

static OSStatus
gst_coreaudio_rbuf_src_callback (GstCoreAudioRbuf * self,
    AudioUnitRenderActionFlags * ioActionFlags,
    const AudioTimeStamp * inTimeStamp,
    UInt32 inBusNumber, UInt32 inNumberFrames, AudioBufferList * bufferList)
{
  GstAudioRingBuffer *rbuf = GST_AUDIO_RING_BUFFER (self);
  gboolean ret;
  guint8 *writeptr;
  gint writeseg, len, offset = 0;
  gpointer audio_data = NULL;
  gsize audio_bytes_left;

  GST_LOG_OBJECT (self, "in sample position %f frames %u flags %u bus %u",
      inTimeStamp->mSampleTime, inNumberFrames, *ioActionFlags, inBusNumber);

  if (self->ctx->should_stop) {
    GST_DEBUG_OBJECT (self, "Context about to stop, not rendering");
    return noErr;
  }

  ret = gst_coreaudio_ctx_render (self->ctx, inTimeStamp, inNumberFrames,
      &audio_data, &audio_bytes_left);

  if (!ret) {
    GST_WARNING_OBJECT (self,
        "Failed to get audio from source, stopping context");
    self->ctx->should_stop = TRUE;
    return 1;
  }

  GST_DEBUG_OBJECT (self, "Got %lu bytes from source", audio_bytes_left);

  while (audio_bytes_left) {
    if (!gst_audio_ring_buffer_prepare_read (rbuf, &writeseg, &writeptr, &len)) {
      GST_WARNING_OBJECT (self, "No space in ringbuffer");
      return 0;
    }

    len -= self->segoffset;
    if (len > audio_bytes_left)
      len = audio_bytes_left;

    memcpy (writeptr + self->segoffset, (char *) audio_data + offset, len);

    self->segoffset += len;
    offset += len;
    audio_bytes_left -= len;

    if (self->segoffset == rbuf->spec.segsize) {
      gst_audio_ring_buffer_advance (rbuf, 1);
      GST_TRACE_OBJECT (self, "Advanced ringbuffer, seg %d", writeseg);
      self->segoffset = 0;
    }
  }

  GST_DEBUG_OBJECT (self, "Consumed %d bytes from source", offset);

  return noErr;
}

static gboolean
gst_coreaudio_rbuf_process_acquire (GstCoreAudioRbuf * self,
    GstAudioRingBufferSpec * spec)
{
  GstAudioRingBuffer *buf = GST_AUDIO_RING_BUFFER (self);
  GstCaps *spec_caps;
  GstStructure *caps_struct;
  guint32 preferred_fpp;
  guint64 channel_mask = 0;

  spec_caps = gst_audio_info_to_caps (&spec->info);
  if (!spec_caps) {
    GST_ERROR_OBJECT (self, "No caps in spec audioinfo?");
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Rbuf acquire, caps %" GST_PTR_FORMAT, spec_caps);
  asbd_clear (&self->format);
  aclayout_clear (&self->layout);

  spec->segsize =
      (spec->latency_time * GST_AUDIO_INFO_RATE (&spec->info) /
      G_USEC_PER_SEC) * GST_AUDIO_INFO_BPF (&spec->info);
  spec->segtotal = spec->buffer_time / spec->latency_time;
  preferred_fpp = spec->segsize / GST_AUDIO_INFO_BPF (&spec->info);

  caps_struct = gst_caps_get_structure (spec_caps, 0);
  gst_structure_get (caps_struct, "channel-mask", GST_TYPE_BITMASK,
      &channel_mask, NULL);

  if (self->ctx) {
    if (!self->ctx->prepared) {
      AudioStreamBasicDescription *format;
      AudioChannelLayout *channel_layout;

      format = asbd_from_audio_info (&spec->info);
      channel_layout =
          aclayout_from_channels_and_mask (spec->info.channels, channel_mask);

      if (!gst_coreaudio_ctx_prepare (self->ctx, format, channel_layout,
              preferred_fpp)) {
        GST_ERROR_OBJECT (self, "Couldn't prepare context for given format");
        gst_clear_caps (&spec_caps);
        aclayout_free (channel_layout);
        return FALSE;
      }

      aclayout_free (channel_layout);
    }

    self->format = asbd_copy (self->ctx->selected_format);
    self->layout = aclayout_copy (self->ctx->selected_layout);

    /* If prepare() couldn't set fpp low enough, we might need to adjust */
    self->frames_per_packet = self->ctx->frames_per_packet;
    if (self->frames_per_packet != preferred_fpp) {
      guint64 new_latency_time;

      GST_DEBUG_OBJECT (self,
          "Device frames_per_packet %u differs from requested %u, adjusting segsize",
          self->frames_per_packet, preferred_fpp);

      new_latency_time =
          self->frames_per_packet * G_USEC_PER_SEC /
          GST_AUDIO_INFO_RATE (&spec->info);
      spec->segsize =
          self->frames_per_packet * GST_AUDIO_INFO_BPF (&spec->info);
      spec->segtotal = MAX (spec->buffer_time / new_latency_time, 2);

      GST_INFO_OBJECT (self, "New segsize %u, segtotal %u", spec->segsize,
          spec->segtotal);
    }
  } else {
    self->format = asbd_from_audio_info (&spec->info);
    self->layout =
        aclayout_from_channels_and_mask (spec->info.channels, channel_mask);
  }

  gst_clear_caps (&spec_caps);

  buf->size = spec->segtotal * spec->segsize;
  buf->memory = g_malloc0 (buf->size);
  gst_audio_format_info_fill_silence (buf->spec.info.finfo, buf->memory,
      buf->size);

  return TRUE;
}

static gboolean
gst_coreaudio_rbuf_process_start (GstCoreAudioRbuf * self,
    gboolean reset_offset)
{
  AURenderCallback callback_func;
  AURenderCallbackStruct callback;

  if (self->running) {
    GST_DEBUG_OBJECT (self, "Already running");
    return TRUE;
  }

  self->is_first = TRUE;
  if (reset_offset)
    self->segoffset = 0;

  if (!self->ctx) {
    GST_WARNING_OBJECT (self, "Starting without a device context!");
    gst_coreaudio_rbuf_start_fallback_timer (self);
  } else {
    callback_func = gst_coreaudio_ctx_is_sink (self->ctx) ?
        (AURenderCallback) gst_coreaudio_rbuf_sink_callback :
        (AURenderCallback) gst_coreaudio_rbuf_src_callback;
    callback.inputProc = callback_func;
    callback.inputProcRefCon = self;

    if (!gst_coreaudio_ctx_start (self->ctx, callback)) {
      GST_WARNING_OBJECT (self, "Failed to start device context");
      gst_coreaudio_rbuf_post_open_warn (self, self->device_uid);
      gst_coreaudio_rbuf_start_fallback_timer (self);
    }
  }

  gst_coreaudio_rbuf_start_monitor_timer (self);
  self->running = TRUE;

  return TRUE;
}

static void
gst_coreaudio_rbuf_discard_frames (GstCoreAudioRbuf * self, guint frames)
{
  GstAudioRingBuffer *rb = GST_AUDIO_RING_BUFFER_CAST (self);
  guint len = frames * GST_AUDIO_INFO_BPF (&rb->spec.info);

  while (len > 0) {
    gint seg, avail, to_consume;
    guint8 *ptr;

    if (!gst_audio_ring_buffer_prepare_read (rb, &seg, &ptr, &avail))
      return;

    avail -= self->segoffset;
    to_consume = MIN ((gint) len, avail);

    self->segoffset += to_consume;
    len -= to_consume;

    if (self->segoffset == rb->spec.segsize) {
      gst_audio_ring_buffer_clear (rb, seg);
      gst_audio_ring_buffer_advance (rb, 1);
      self->segoffset = 0;
    }
  }
}

static void
gst_coreaudio_rbuf_insert_silence_frames (GstCoreAudioRbuf * self, guint frames)
{
  GstAudioRingBuffer *rb = GST_AUDIO_RING_BUFFER_CAST (self);
  guint bpf = GST_AUDIO_INFO_BPF (&rb->spec.info);
  guint len = frames * bpf;

  while (len > 0) {
    gint segment, avail, to_write;
    guint8 *writeptr;

    if (!gst_audio_ring_buffer_prepare_read (rb, &segment, &writeptr, &avail))
      break;

    avail -= self->segoffset;
    to_write = MIN ((gint) len, avail);

    gst_audio_format_info_fill_silence (rb->spec.info.finfo,
        writeptr + self->segoffset, to_write);

    self->segoffset += to_write;
    len -= to_write;

    if (self->segoffset == rb->spec.segsize) {
      gst_audio_ring_buffer_advance (rb, 1);
      self->segoffset = 0;
    }
  }
}

static void
gst_coreaudio_rbuf_process_fallback_timer (GstCoreAudioRbuf * self)
{
  guint64 now = mach_absolute_time ();
  guint64 elapsed = now - self->fallback_start_time;
  guint64 elapsed_ns = mach_absolute_time_to_nanoseconds (elapsed);
  GstAudioRingBuffer *rbuf = GST_AUDIO_RING_BUFFER (self);
  guint64 expected_frames, delta;

  expected_frames =
      (elapsed_ns * GST_AUDIO_INFO_RATE (&rbuf->spec.info)) / 1000000000ULL;

  delta = expected_frames - self->fallback_frames_processed;
  if (delta > 0) {
    if (self->mode == GST_COREAUDIO_DEVICE_MODE_SINK) {
      GST_TRACE_OBJECT (self, "Fallback: discarding %llu frames", delta);
      gst_coreaudio_rbuf_discard_frames (self, delta);
    } else {
      GST_TRACE_OBJECT (self, "Fallback: inserting %llu silence frames", delta);
      gst_coreaudio_rbuf_insert_silence_frames (self, delta);
    }

    self->fallback_frames_processed += delta;
  }
}

static void
gst_coreaudio_rbuf_loop (GstCoreAudioRbuf * self)
{
  CommandData *cmd;
  gboolean loop_running = TRUE;
  struct kevent events[1], *ev;
  int nevents;

  GST_DEBUG_OBJECT (self, "Rbuf loop starting");

  while (loop_running) {
    nevents = kevent (self->rbuf_kqueue, NULL, 0, events, 1, NULL);
    g_assert (nevents <= 1);
    if (nevents < 0) {
      if (errno == EINTR)
        continue;
      GST_ERROR_OBJECT (self, "Error retrieving event in event loop: %s",
          g_strerror (errno));
      break;
    }

    ev = &events[0];
    g_assert (ev->filter == EVFILT_USER || ev->filter == EVFILT_TIMER);

    switch (ev->ident) {
      case RBUF_EVT_COMMAND:
        GST_TRACE_OBJECT (self, "Rbuf command event");
        /* handled below */
        break;
      case RBUF_EVT_FALLBACK_TIMER:
        GST_TRACE_OBJECT (self, "Rbuf fallback timer event");
        if (!self->running || !self->fallback_timer_active)
          break;

        gst_coreaudio_rbuf_process_fallback_timer (self);
        break;
      case RBUF_EVT_MONITOR_TIMER:
        GST_TRACE_OBJECT (self, "Rbuf monitor timer event");
        if (!self->running || !self->monitor_timer_active)
          break;

        if (self->ctx && !self->ctx->running && !self->fallback_timer_active) {
          /* If context was running but got shut down for some reason ... */
          GST_WARNING_OBJECT (self,
              "Context shutdown detected, starting fallback");
          gst_coreaudio_rbuf_start_fallback_timer (self);
          gst_coreaudio_rbuf_post_device_warn (self, self->device_uid);
        } else if (self->ctx && self->ctx->running && self->ctx->should_stop) {
          /* ... or it's still running but indicated we should shut it down manually. 
           * e.g. CoreAudio indicated that the device died, but it's not MT-safe 
           * so we need to call stop() from outside to avoid deadlocks. */
          GST_WARNING_OBJECT (self,
              "Stopping context due to a problem indicated by CoreAudio");
          gst_coreaudio_ctx_stop (self->ctx);
          gst_coreaudio_rbuf_start_fallback_timer (self);
          gst_coreaudio_rbuf_post_device_warn (self, self->device_uid);
        }
        break;
      default:
        GST_WARNING_OBJECT (self, "Unknown event ident %lu", ev->ident);
        break;
    }

    g_mutex_lock (&self->rbuf_lock);
    while ((cmd = (CommandData *) gst_vec_deque_pop_head (self->rbuf_queue))) {
      const gchar *cmd_type = command_type_to_string (cmd->type);
      GST_LOG_OBJECT (self, "Processing rbuf command %s", cmd_type);

      switch (cmd->type) {
        case COMMAND_SET_DEVICE:{
          CommandSetDevice *scmd = (CommandSetDevice *) cmd;
          g_free (self->device_uid);
          self->device_uid = g_strdup (scmd->device_uid);
          self->mode = scmd->mode;

          GST_DEBUG_OBJECT (self, "Set device to %s, mode %d",
              DEVICE_UID_STR (self->device_uid), self->mode);

          if (self->opened) {
            GST_DEBUG_OBJECT (self,
                "Already opened, requesting new context async");
            gst_coreaudio_rbuf_create_ctx_async (self);
          }

          command_signal_done (cmd, TRUE);
          break;
        }
        case COMMAND_UPDATE_DEVICE:{
          CommandUpdateDevice *ucmd = (CommandUpdateDevice *) cmd;
          if (self->opened) {
            GST_DEBUG_OBJECT (self, "Got new context for device %s: %p",
                DEVICE_UID_STR (ucmd->device_id), ucmd->ctx);

            if (self->ctx) {
              gst_coreaudio_ctx_stop (self->ctx);
              gst_coreaudio_ctx_free (self->ctx);
            }

            self->ctx = ucmd->ctx;
            if (self->ctx != NULL && !self->ctx->prepared && self->format) {
              if (!gst_coreaudio_ctx_prepare (self->ctx, self->format,
                      self->layout, self->frames_per_packet)) {
                GST_ERROR_OBJECT (self,
                    "Failed to prepare new context for existing format");
                gst_coreaudio_ctx_free (self->ctx);
                self->ctx = NULL;
              }
            }

            if (self->ctx) {
              self->frames_per_packet = self->ctx->frames_per_packet;
            } else {
              gst_coreaudio_rbuf_post_open_warn (self, ucmd->device_id);
            }

            if (self->running) {
              self->running = FALSE;
              /* If this fails it'll just fall back to consuming/producing silence */
              gst_coreaudio_rbuf_process_start (self, FALSE);
            }
          } else {
            GST_DEBUG_OBJECT (self,
                "Not opened anymore, ignoring update device from async ctx request");
          }

          /* This command is sent by the ctx loop, it doesn't care about the ret value */
          command_signal_done (cmd, TRUE);
          break;
        }
        case COMMAND_OPEN:{
          self->ctx = gst_coreaudio_rbuf_create_ctx (self);

          if (self->ctx) {
            /* Expose general device caps for now, then choose more specific ones in acquire() */
            GstCaps *ctx_caps = asbd_to_caps (self->ctx->hw_format,
                self->ctx->hw_layout, gst_coreaudio_ctx_is_sink (self->ctx),
                TRUE);
            gst_caps_replace (&self->allowed_caps, ctx_caps);
            gst_caps_unref (ctx_caps);
          } else {
            gst_caps_replace (&self->allowed_caps,
                gst_static_caps_get (&template_caps));
            gst_coreaudio_rbuf_post_open_warn (self, self->device_uid);
          }

          self->opened = TRUE;
          command_signal_done (cmd, TRUE);
          GST_DEBUG_OBJECT (self, "Opened device");
          break;
        }
        case COMMAND_CLOSE:{
          asbd_clear (&self->format);
          gst_clear_caps (&self->allowed_caps);

          if (self->ctx) {
            gst_coreaudio_ctx_free (self->ctx);
            self->ctx = NULL;
          }

          self->opened = FALSE;

          command_signal_done (cmd, TRUE);
          GST_DEBUG_OBJECT (self, "Closed device");
          break;
        }
        case COMMAND_ACQUIRE:{
          CommandAcquire *acmd = (CommandAcquire *) cmd;

          if (!self->ctx) {
            self->ctx = gst_coreaudio_rbuf_create_ctx (self);
            if (!self->ctx) {
              GST_WARNING_OBJECT (self, "Running with no device context!");
            }
          }

          if (!gst_coreaudio_rbuf_process_acquire (self, acmd->spec)) {
            GST_ERROR_OBJECT (self, "Failed to acquire ringbuffer");
            command_signal_done (cmd, FALSE);
            break;
          }

          /* Caps now have fixed SR and format */
          gst_clear_caps (&self->allowed_caps);
          self->allowed_caps =
              asbd_to_caps (self->format, self->layout, FALSE, FALSE);

          self->opened = true;
          command_signal_done (cmd, TRUE);
          break;
        }
        case COMMAND_RELEASE:{
          GstAudioRingBuffer *buf = GST_AUDIO_RING_BUFFER (self);
          g_clear_pointer (&buf->memory, g_free);
          command_signal_done (cmd, TRUE);
          break;
        }
        case COMMAND_START:{
          gboolean res = gst_coreaudio_rbuf_process_start (self, TRUE);
          command_signal_done (cmd, res);
          break;
        }
        case COMMAND_STOP:{
          gboolean res = TRUE;

          if (self->ctx)
            res = gst_coreaudio_ctx_stop (self->ctx);

          self->running = FALSE;
          self->is_first = TRUE;
          self->segoffset = 0;

          gst_coreaudio_rbuf_stop_fallback_timer (self);
          gst_coreaudio_rbuf_stop_monitor_timer (self);

          command_signal_done (cmd, res);
          break;
        }
        case COMMAND_SHUTDOWN:{
          CommandData *ctx_cmd = command_new (COMMAND_SHUTDOWN);

          gst_coreaudio_rbuf_ctx_loop_push_command (self, ctx_cmd);
          command_wait (ctx_cmd);
          command_free (ctx_cmd);

          loop_running = FALSE;
          command_signal_done (cmd, TRUE);
          break;
        }
        case COMMAND_GET_CAPS:{
          CommandGetCaps *gcmd = (CommandGetCaps *) cmd;

          if (self->allowed_caps) {
            gcmd->caps = gst_caps_ref (self->allowed_caps);
          }

          command_signal_done (cmd, TRUE);
          break;
        }
        default:
          GST_ERROR_OBJECT (self, "Unknown rbuf command %d", cmd->type);
          command_signal_done (cmd, FALSE);
          break;
      }
      GST_LOG_OBJECT (self, "Finished command %s", cmd_type);
    }

    g_mutex_unlock (&self->rbuf_lock);
  }

  GST_DEBUG_OBJECT (self, "Rbuf loop exiting");
}

void
gst_coreaudio_rbuf_set_device (GstCoreAudioRbuf * rbuf,
    GstCoreAudioDeviceMode mode, const gchar * device_uid)
{
  CommandSetDevice *cmd = command_set_device_new (g_strdup (device_uid), mode);

  gst_coreaudio_rbuf_loop_push_command (rbuf, &cmd->base);
  command_wait (&cmd->base);
  command_free (&cmd->base);
}

GstCaps *
gst_coreaudio_rbuf_get_caps (GstCoreAudioRbuf * rbuf)
{
  GstCaps *caps = NULL;
  CommandGetCaps *cmd = command_get_caps_new ();

  gst_coreaudio_rbuf_loop_push_command (rbuf, &cmd->base);
  command_wait (&cmd->base);
  caps = cmd->caps;
  command_free (&cmd->base);

  GST_DEBUG_OBJECT (rbuf, "Got caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_coreaudio_rbuf_open_device (GstAudioRingBuffer * buf)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Open device");

  CommandData *cmd = command_new (COMMAND_OPEN);
  gst_coreaudio_rbuf_loop_push_command (self, cmd);
  ret = command_wait (cmd);
  command_free (cmd);

  return ret;
}

static gboolean
gst_coreaudio_rbuf_close_device (GstAudioRingBuffer * buf)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Close device");

  CommandData *cmd = command_new (COMMAND_CLOSE);
  gst_coreaudio_rbuf_loop_push_command (self, cmd);
  ret = command_wait (cmd);
  command_free (cmd);

  return ret;
}

static gboolean
gst_coreaudio_rbuf_acquire (GstAudioRingBuffer * buf,
    GstAudioRingBufferSpec * spec)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Acquire ringbuf");

  CommandAcquire *cmd = command_acquire_new (spec);
  gst_coreaudio_rbuf_loop_push_command (self, &cmd->base);
  ret = command_wait (&cmd->base);
  command_free (&cmd->base);

  return ret;
}

static gboolean
gst_coreaudio_rbuf_release (GstAudioRingBuffer * buf)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Release ringbuf");

  CommandData *cmd = command_new (COMMAND_RELEASE);
  gst_coreaudio_rbuf_loop_push_command (self, cmd);
  ret = command_wait (cmd);
  command_free (cmd);

  return ret;
}

static gboolean
gst_coreaudio_rbuf_do_start (GstCoreAudioRbuf * self)
{
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Start");
  CommandData *cmd = command_new (COMMAND_START);
  gst_coreaudio_rbuf_loop_push_command (self, cmd);
  ret = command_wait (cmd);
  command_free (cmd);

  return ret;
}

static gboolean
gst_coreaudio_rbuf_start (GstAudioRingBuffer * buf)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Start ringbuf");
  return gst_coreaudio_rbuf_do_start (self);
}

static gboolean
gst_coreaudio_rbuf_resume (GstAudioRingBuffer * buf)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Resume ringbuf");
  return gst_coreaudio_rbuf_do_start (self);
}

static gboolean
gst_coreaudio_rbuf_do_stop (GstCoreAudioRbuf * self)
{
  gboolean ret;

  GST_DEBUG_OBJECT (self, "Stop");
  CommandData *cmd = command_new (COMMAND_STOP);
  gst_coreaudio_rbuf_loop_push_command (self, cmd);
  ret = command_wait (cmd);
  command_free (cmd);

  return ret;
}

static gboolean
gst_coreaudio_rbuf_pause (GstAudioRingBuffer * buf)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Pause ringbuf");
  return gst_coreaudio_rbuf_do_stop (self);
}

static gboolean
gst_coreaudio_rbuf_stop (GstAudioRingBuffer * buf)
{
  GstCoreAudioRbuf *self = GST_COREAUDIO_RBUF (buf);

  GST_DEBUG_OBJECT (self, "Stop ringbuf");
  return gst_coreaudio_rbuf_do_stop (self);
}

static guint
gst_coreaudio_rbuf_delay (GstAudioRingBuffer * buf)
{
  // TODO: implement this similarly to osxaudio
  return 0;
}

static gboolean
_setup_kqueue (GstCoreAudioRbuf * self)
{
  struct kevent ev;

  self->rbuf_kqueue = kqueue ();
  if (self->rbuf_kqueue < 0) {
    GST_ERROR_OBJECT (self, "Failed to create event kqueue: %s",
        g_strerror (errno));
    return FALSE;
  }

  /* EVFILT_USER events need to be registered beforehand.
   * Other events (in our case: timers) can just be added on the fly. */
  EV_SET (&ev, RBUF_EVT_COMMAND, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
  kevent (self->rbuf_kqueue, &ev, 1, NULL, 0, NULL);

  return TRUE;
}

GstCoreAudioRbuf *
gst_coreaudio_rbuf_new (gpointer parent)
{
  GstCoreAudioRbuf *self =
      (GstCoreAudioRbuf *) g_object_new (GST_TYPE_COREAUDIO_RBUF, NULL);
  gst_object_ref_sink (self);

  self->opened = FALSE;
  self->running = FALSE;
  self->ctx = NULL;
  self->format = NULL;
  self->allowed_caps = NULL;
  self->is_first = TRUE;
  self->frames_per_packet = 0;
  self->segoffset = 0;
  self->device_uid = NULL;
  g_weak_ref_set (&self->parent_element, parent);

  if (!_setup_kqueue (self)) {
    g_object_unref (self);
    return NULL;
  }

  g_mutex_init (&self->rbuf_lock);
  self->rbuf_queue = gst_vec_deque_new (0);
  self->rbuf_loop = g_thread_new ("GstCoreAudioRbuf",
      (GThreadFunc) gst_coreaudio_rbuf_loop, self);

  g_mutex_init (&self->ctx_lock);
  g_cond_init (&self->ctx_cond);
  self->ctx_queue = gst_vec_deque_new (0);
  self->ctx_loop = g_thread_new ("GstCoreAudioRbufCtx",
      (GThreadFunc) gst_coreaudio_rbuf_ctx_loop, self);

  return self;
}

static void
gst_coreaudio_rbuf_finalize (GObject * object)
{
  GstCoreAudioRbuf *rbuf = GST_COREAUDIO_RBUF (object);
  CommandData *cmd;

  GST_DEBUG_OBJECT (rbuf, "Finalize rbuf");

  cmd = command_new (COMMAND_SHUTDOWN);
  gst_coreaudio_rbuf_loop_push_command (rbuf, cmd);

  g_thread_join (rbuf->rbuf_loop);
  g_thread_join (rbuf->ctx_loop);
  command_free (cmd);

  g_clear_pointer (&rbuf->device_uid, g_free);
  g_clear_pointer (&rbuf->allowed_caps, gst_caps_unref);
  asbd_clear (&rbuf->format);
  aclayout_clear (&rbuf->layout);

  if (rbuf->rbuf_kqueue >= 0) {
    close (rbuf->rbuf_kqueue);
    rbuf->rbuf_kqueue = -1;
  }

  g_mutex_clear (&rbuf->rbuf_lock);
  gst_vec_deque_free (rbuf->rbuf_queue);

  g_mutex_clear (&rbuf->ctx_lock);
  g_cond_clear (&rbuf->ctx_cond);
  gst_vec_deque_free (rbuf->ctx_queue);

  g_weak_ref_clear (&rbuf->parent_element);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_coreaudio_rbuf_init (GstCoreAudioRbuf * rbuf)
{
}

static void
gst_coreaudio_rbuf_class_init (GstCoreAudioRbufClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAudioRingBufferClass *rbuf_class = GST_AUDIO_RING_BUFFER_CLASS (klass);

  gobject_class->finalize = gst_coreaudio_rbuf_finalize;

  rbuf_class->open_device = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_open_device);
  rbuf_class->close_device =
      GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_close_device);
  rbuf_class->acquire = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_acquire);
  rbuf_class->release = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_release);
  rbuf_class->start = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_start);
  rbuf_class->resume = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_resume);
  rbuf_class->pause = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_pause);
  rbuf_class->stop = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_stop);
  rbuf_class->delay = GST_DEBUG_FUNCPTR (gst_coreaudio_rbuf_delay);
}
