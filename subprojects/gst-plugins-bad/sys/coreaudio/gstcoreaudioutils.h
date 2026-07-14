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

#ifndef __GST_COREAUDIO_UTILS_H__
#define __GST_COREAUDIO_UTILS_H__

#include <AudioToolbox/AudioToolbox.h>
#include <gst/gst.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_COREAUDIO_MAX_CHANNELS (64)

/* CA formats were being printed in reverse without this */
#define CORE_AUDIO_FOURCC_ARGS(f) GST_FOURCC_ARGS(CFSwapInt32HostToBig((f)))
#define CORE_AUDIO_FORMAT "FormatID: %" GST_FOURCC_FORMAT " rate: %f flags: 0x%x BytesPerPacket: %u FramesPerPacket: %u BytesPerFrame: %u ChannelsPerFrame: %u BitsPerChannel: %u"
#define CORE_AUDIO_FORMAT_ARGS(f) CORE_AUDIO_FOURCC_ARGS((f).mFormatID),(f).mSampleRate,(unsigned int)(f).mFormatFlags,(unsigned int)(f).mBytesPerPacket,(unsigned int)(f).mFramesPerPacket,(unsigned int)(f).mBytesPerFrame,(unsigned int)(f).mChannelsPerFrame,(unsigned int)(f).mBitsPerChannel

#define DEVICE_UID_STR(device_uid) ((device_uid) ? (device_uid) : "default")

#define GST_COREAUDIO_STATIC_CAPS "audio/x-raw, " \
        "format = (string) " GST_AUDIO_FORMATS_ALL ", " \
        "layout = (string) interleaved, " \
        "rate = " GST_AUDIO_RATE_RANGE ", " \
        "channels = " GST_AUDIO_CHANNELS_RANGE

#define GST_DEBUG_AUDIO_CHANNEL_LAYOUT(prefix, layout)            \
G_STMT_START {                                                    \
  gchar *acl_str__ = aclayout_to_str (&(layout));   \
  GST_DEBUG ("%s\n%s", prefix, acl_str__);     \
  g_free (acl_str__);                                              \
} G_STMT_END

static GstStaticCaps template_caps =
GST_STATIC_CAPS (GST_COREAUDIO_STATIC_CAPS);

typedef enum
{
  GST_COREAUDIO_DEVICE_MODE_SINK = 0,
  GST_COREAUDIO_DEVICE_MODE_SRC,
} GstCoreAudioDeviceMode;

typedef struct
{
  gchar *uid;
  AudioDeviceID id;
} GstCoreAudioDeviceId;

typedef struct
{
  gint channels;
  AudioChannelLayoutTag tag;
  GstAudioChannelPosition positions[8];
} GstCoreAudioTagLayout;

gboolean
aclayout_to_mask_and_pos (const AudioChannelLayout * acl, guint * out_channels, guint64 * out_mask, GstAudioChannelPosition ** out_positions);

AudioChannelLayout *
aclayout_from_channels_and_mask (guint channels, guint64 channel_mask);

char *
aclayout_to_str (AudioChannelLayout *layout);

AudioChannelLayout *
aclayout_copy (const AudioChannelLayout * layout);

void
aclayout_free (AudioChannelLayout * layout);

void
aclayout_clear (AudioChannelLayout ** layout);

AudioDeviceID
get_default_coreaudio_device_id (gboolean output);

GPtrArray *
get_all_coreaudio_devices (void);

GstAudioFormat
asbd_get_gst_audio_format (const AudioStreamBasicDescription * asbd);

AudioStreamBasicDescription *
asbd_copy (const AudioStreamBasicDescription * in);

void
asbd_free (AudioStreamBasicDescription * asbd);

void
asbd_clear (AudioStreamBasicDescription ** asbd);

GstCaps *
asbd_to_caps (const AudioStreamBasicDescription * asbd,
    const AudioChannelLayout * layout, gboolean any_rate_allowed, gboolean allow_conversion);

AudioStreamBasicDescription *
asbd_from_audio_info (GstAudioInfo * info);

gboolean
asbd_is_equal (const AudioStreamBasicDescription *a,
  const AudioStreamBasicDescription *b);

AudioBufferList *
audio_buffer_list_prepare (UInt32 channels, UInt32 size);

void
audio_buffer_list_free (AudioBufferList * list);

void
core_audio_device_free (GstCoreAudioDeviceId * device);

uint64_t
mach_absolute_time_to_nanoseconds (uint64_t mach_time);

uint64_t
nanoseconds_to_mach_absolute_time (uint64_t nanoseconds);

char *
audiodevice_get_prop_str (AudioDeviceID device_id, AudioObjectPropertyElement prop_id);

UInt32
audiodevice_get_prop_uint32 (AudioDeviceID device_id, AudioObjectPropertyElement prop_id);

G_END_DECLS

#endif /* __GST_COREAUDIO_UTILS_H__ */
