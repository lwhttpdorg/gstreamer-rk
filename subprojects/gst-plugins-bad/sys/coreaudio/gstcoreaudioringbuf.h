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

#ifndef __GST_COREAUDIO_RINGBUF_H__
#define __GST_COREAUDIO_RINGBUF_H__

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/base/base.h>
#include "gstcoreaudioutils.h"
#include "gstcoreaudiocontext.h"

G_BEGIN_DECLS

#define GST_TYPE_COREAUDIO_RBUF (gst_coreaudio_rbuf_get_type())
G_DECLARE_FINAL_TYPE (GstCoreAudioRbuf, gst_coreaudio_rbuf,
    GST, COREAUDIO_RBUF, GstAudioRingBuffer);

struct _GstCoreAudioRbuf
{
  GstAudioRingBuffer parent;

  GWeakRef parent_element;

  /* Handles commands like start/aqcuire/set-device */
  GThread *rbuf_loop;
  GMutex rbuf_lock;
  GstVecDeque *rbuf_queue;
  int rbuf_kqueue;

  /* Handles requests from rbuf_loop to set up a new context/device (a)synchronously */
  GThread *ctx_loop;
  GMutex ctx_lock;
  GstVecDeque *ctx_queue;
  GCond ctx_cond;

  gboolean opened;
  gboolean running;
  GstCoreAudioCtx *ctx;
  GstCoreAudioDeviceMode mode;
  gchar *device_uid;
  AudioStreamBasicDescription *format;
  AudioChannelLayout *layout;
  GstCaps *allowed_caps;

  bool is_first;
  gint segoffset;
  guint32 frames_per_packet;

  /* Fallback / monitoring timers */
  gboolean fallback_timer_active;
  guint64 fallback_start_time;
  guint64 fallback_frames_processed;
  gboolean monitor_timer_active;
};

typedef enum {
  RBUF_EVT_COMMAND = 0,
  RBUF_EVT_FALLBACK_TIMER = 1,
  RBUF_EVT_MONITOR_TIMER = 2,
} RbufKqueueEvent;

typedef enum {
  COMMAND_SET_DEVICE,
  COMMAND_UPDATE_DEVICE,
  COMMAND_OPEN,
  COMMAND_CLOSE,
  COMMAND_ACQUIRE,
  COMMAND_RELEASE,
  COMMAND_START,
  COMMAND_STOP,
  COMMAND_SHUTDOWN,
  COMMAND_GET_CAPS,
  COMMAND_CREATE_CTX,
} CommandType;

typedef struct {
  CommandType type;
  gboolean success;
  gboolean completed;
  GMutex mutex;
  GCond cond;
} CommandData;

typedef struct {
  /* If rbuf is not null then it's an async request
   * and we'll send an update-device command back to rbuf loop once completed */
  GstCoreAudioRbuf *rbuf;
  GstCoreAudioDeviceMode mode;
  gchar *device_uid;
  guint32 frames_per_packet;
  AudioStreamBasicDescription *format;
  AudioChannelLayout *layout;

  GstCoreAudioCtx *ctx;
} CtxRequest;

GstCoreAudioRbuf * gst_coreaudio_rbuf_new (gpointer parent);
void gst_coreaudio_rbuf_set_device (GstCoreAudioRbuf * rbuf,
    GstCoreAudioDeviceMode mode, const gchar * device_id);
GstCaps * gst_coreaudio_rbuf_get_caps (GstCoreAudioRbuf * rbuf);
OSStatus gst_coreaudio_rbuf_handle_ioproc (GstCoreAudioRbuf * self,
    AudioUnitRenderActionFlags * ioActionFlags,
    const AudioTimeStamp * inTimeStamp, UInt32 inBusNumber,
    UInt32 inNumberFrames, AudioBufferList * __nullable ioData);

G_END_DECLS

#endif /* __GST_COREAUDIO_RINGBUF_H__ */
