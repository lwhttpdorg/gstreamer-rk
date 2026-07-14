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

#include "gstcoreaudioutils.h"
#include <mach/mach_time.h>

GST_DEBUG_CATEGORY_EXTERN (gst_coreaudio_debug);
#define GST_CAT_DEFAULT gst_coreaudio_debug

static mach_timebase_info_data_t timebase_info;
static dispatch_once_t timebase_once;

/* So far only the ones supported by osxaudio. Can easily be extended if needed. */
/* *INDENT-OFF* */
static const GstCoreAudioTagLayout coreaudio_layouts[] = {
  {
    1, kAudioChannelLayoutTag_Mono, { GST_AUDIO_CHANNEL_POSITION_MONO }}, {
    2, kAudioChannelLayoutTag_Stereo, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT }}, {
    4, kAudioChannelLayoutTag_Quadraphonic, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT }}, {
    4, kAudioChannelLayoutTag_Cube, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT,
      GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT }}, {
    5, kAudioChannelLayoutTag_Pentagonal, {
      GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT,
      GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER }}, {
    /* Only used when iterating through all positions */
    0, kAudioChannelLayoutTag_Unknown, { 0 } }
};
/* *INDENT-ON* */

static GstAudioChannelPosition
_channel_label_to_gst_pos (AudioChannelLabel label)
{
  switch (label) {
    case kAudioChannelLabel_Left:
      return GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
    case kAudioChannelLabel_Right:
      return GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
    case kAudioChannelLabel_Center:
      return GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
    case kAudioChannelLabel_LFEScreen:
      return GST_AUDIO_CHANNEL_POSITION_LFE1;
    case kAudioChannelLabel_LeftSurround:
      return GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT;
    case kAudioChannelLabel_RightSurround:
      return GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT;
    case kAudioChannelLabel_LeftSurroundDirect:
      return GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
    case kAudioChannelLabel_RightSurroundDirect:
      return GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
    case kAudioChannelLabel_CenterSurround:
      return GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
    case kAudioChannelLabel_LeftCenter:
      return GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER;
    case kAudioChannelLabel_RightCenter:
      return GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER;
    case kAudioChannelLabel_TopBackLeft:
      return GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT;
    case kAudioChannelLabel_TopBackCenter:
      return GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER;
    case kAudioChannelLabel_TopBackRight:
      return GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT;
    case kAudioChannelLabel_LeftWide:
      return GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT;
    case kAudioChannelLabel_RightWide:
      return GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT;
    case kAudioChannelLabel_LFE2:
      return GST_AUDIO_CHANNEL_POSITION_LFE2;
    case kAudioChannelLabel_VerticalHeightLeft:
      return GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT;
    case kAudioChannelLabel_VerticalHeightRight:
      return GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT;
    case kAudioChannelLabel_VerticalHeightCenter:
      return GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER;
    case kAudioChannelLabel_RearSurroundLeft:
      return GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
    case kAudioChannelLabel_RearSurroundRight:
      return GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
    case kAudioChannelLabel_TopCenterSurround:
      return GST_AUDIO_CHANNEL_POSITION_TOP_CENTER;

    case kAudioChannelLabel_Discrete:
    case kAudioChannelLabel_Unknown:   // osxaudio had a bug with unknown - matched it to Discrete_N by mistake
      return GST_AUDIO_CHANNEL_POSITION_NONE;

    default:
      return GST_AUDIO_CHANNEL_POSITION_INVALID;
  }
}

static AudioChannelLabel
_gst_pos_to_channel_label (GstAudioChannelPosition pos, int channel)
{
  /* Borrowed from osxaudio */
  switch (pos) {
    case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT:
      return kAudioChannelLabel_Left;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT:
      return kAudioChannelLabel_Right;
    case GST_AUDIO_CHANNEL_POSITION_REAR_CENTER:
      return kAudioChannelLabel_CenterSurround;
    case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
      return kAudioChannelLabel_RearSurroundLeft;
    case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
      return kAudioChannelLabel_RearSurroundRight;
    case GST_AUDIO_CHANNEL_POSITION_LFE1:
      return kAudioChannelLabel_LFEScreen;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER:
      return kAudioChannelLabel_Center;
    case GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT:
      return kAudioChannelLabel_LeftSurroundDirect;
    case GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT:
      return kAudioChannelLabel_RightSurroundDirect;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER:
      return kAudioChannelLabel_LeftCenter;
    case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER:
      return kAudioChannelLabel_RightCenter;
    case GST_AUDIO_CHANNEL_POSITION_TOP_REAR_LEFT:
      return kAudioChannelLabel_TopBackLeft;
    case GST_AUDIO_CHANNEL_POSITION_TOP_REAR_CENTER:
      return kAudioChannelLabel_TopBackCenter;
    case GST_AUDIO_CHANNEL_POSITION_TOP_REAR_RIGHT:
      return kAudioChannelLabel_TopBackRight;
    case GST_AUDIO_CHANNEL_POSITION_WIDE_LEFT:
      return kAudioChannelLabel_LeftWide;
    case GST_AUDIO_CHANNEL_POSITION_WIDE_RIGHT:
      return kAudioChannelLabel_RightWide;
    case GST_AUDIO_CHANNEL_POSITION_LFE2:
      return kAudioChannelLabel_LFE2;
    case GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_LEFT:
      return kAudioChannelLabel_VerticalHeightLeft;
    case GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_RIGHT:
      return kAudioChannelLabel_VerticalHeightRight;
    case GST_AUDIO_CHANNEL_POSITION_TOP_FRONT_CENTER:
      return kAudioChannelLabel_VerticalHeightCenter;
    case GST_AUDIO_CHANNEL_POSITION_TOP_CENTER:
      return kAudioChannelLabel_TopCenterSurround;
    case GST_AUDIO_CHANNEL_POSITION_SURROUND_LEFT:
      return kAudioChannelLabel_LeftSurround;
    case GST_AUDIO_CHANNEL_POSITION_SURROUND_RIGHT:
      return kAudioChannelLabel_RightSurround;

      /* Special position values */
    case GST_AUDIO_CHANNEL_POSITION_NONE:
      return kAudioChannelLabel_Discrete_0 | channel;
    case GST_AUDIO_CHANNEL_POSITION_MONO:
      return kAudioChannelLabel_Mono;

      /* Following positions are unmapped --
       * i.e. mapped to kAudioChannelLabel_Unknown: */
    case GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_LEFT:
    case GST_AUDIO_CHANNEL_POSITION_TOP_SIDE_RIGHT:
    case GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_CENTER:
    case GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_LEFT:
    case GST_AUDIO_CHANNEL_POSITION_BOTTOM_FRONT_RIGHT:
    default:
      return kAudioChannelLabel_Unknown;
  }
}

static gboolean
_parse_acl_channel_descriptions (const AudioChannelLayout * acl,
    guint * out_channels, guint64 * out_mask,
    GstAudioChannelPosition ** out_positions)
{
  g_assert (acl->mChannelLayoutTag ==
      kAudioChannelLayoutTag_UseChannelDescriptions);
  GstAudioChannelPosition *all_positions, pos;
  guint64 channel_mask = 0;
  guint channels = 0;
  gboolean positioned = FALSE;

  all_positions =
      g_new (GstAudioChannelPosition, acl->mNumberChannelDescriptions);

  /* Set all positions to none */
  for (guint i = 0; i < acl->mNumberChannelDescriptions; ++i)
    all_positions[i] = GST_AUDIO_CHANNEL_POSITION_NONE;

  /* First check if all positions are valid */
  for (guint i = 0; i < acl->mNumberChannelDescriptions; ++i) {
    if (_channel_label_to_gst_pos (acl->mChannelDescriptions[i].mChannelLabel)
        == GST_AUDIO_CHANNEL_POSITION_INVALID) {
      GST_WARNING
          ("Invalid positions given by CoreAudio, setting all to unpositioned");
      channels = acl->mNumberChannelDescriptions;
      channel_mask = 0;
      goto done;
    }
  }

  /* Then check if we're positioned */
  for (guint i = 0; i < acl->mNumberChannelDescriptions; ++i) {
    if (_channel_label_to_gst_pos (acl->mChannelDescriptions[i].mChannelLabel)
        > GST_AUDIO_CHANNEL_POSITION_INVALID) {
      positioned = TRUE;
      break;
    }
  }

  for (guint i = 0; i < acl->mNumberChannelDescriptions; ++i) {
    pos =
        _channel_label_to_gst_pos (acl->mChannelDescriptions[i].mChannelLabel);

    if ((positioned && pos >= 0) ||
        (!positioned && pos == GST_AUDIO_CHANNEL_POSITION_NONE)) {
      all_positions[channels] = pos;
      channel_mask |= G_GUINT64_CONSTANT (1) << pos;
      channels += 1;
    }
  }

done:
  if (out_channels)
    *out_channels = channels;
  if (out_mask)
    *out_mask = channel_mask;
  if (out_positions)
    *out_positions = all_positions;
  else
    g_free (all_positions);

  return TRUE;
}

static gboolean
_parse_acl_channel_tags (const AudioChannelLayout * acl, guint * out_channels,
    guint64 * out_mask, GstAudioChannelPosition ** out_positions)
{
  const GstCoreAudioTagLayout *layout;
  guint64 channel_mask = 0;
  AudioChannelLayoutTag tag = acl->mChannelLayoutTag;

  if (out_positions)
    *out_positions = NULL;

  /* Some layouts are identical for our needs */
  if (tag == kAudioChannelLayoutTag_StereoHeadphones ||
      tag == kAudioChannelLayoutTag_Binaural) {
    tag = kAudioChannelLayoutTag_Stereo;
  }

  for (layout = coreaudio_layouts; layout->channels; layout++) {
    const GstAudioChannelPosition *output_positions = layout->positions;

    if (layout->tag != acl->mChannelLayoutTag)
      continue;

    gst_audio_channel_positions_to_mask (output_positions, layout->channels,
        FALSE, &channel_mask);

    if (out_channels)
      *out_channels = layout->channels;
    if (out_mask)
      *out_mask = channel_mask;
    if (out_positions) {
      *out_positions = g_new (GstAudioChannelPosition, layout->channels);
      memcpy (*out_positions, output_positions,
          sizeof (GstAudioChannelPosition) * layout->channels);
    }

    return TRUE;
  }

  GST_WARNING ("Unsupported AudioChannelLayoutTag: %u", acl->mChannelLayoutTag);
  return FALSE;
}

gboolean
aclayout_to_mask_and_pos (const AudioChannelLayout * acl, guint * out_channels,
    guint64 * out_mask, GstAudioChannelPosition ** out_positions)
{
  g_return_val_if_fail (acl != NULL, FALSE);
  g_return_val_if_fail (out_channels != NULL, FALSE);
  g_return_val_if_fail (out_mask != NULL, FALSE);

  if (acl->mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelDescriptions) {
    return _parse_acl_channel_descriptions (acl, out_channels, out_mask,
        out_positions);
  } else {
    return _parse_acl_channel_tags (acl, out_channels, out_mask, out_positions);
  }
}

AudioChannelLayout *
aclayout_from_channels_and_mask (guint channels, guint64 channel_mask)
{
  guint acl_size;
  AudioChannelLayout *acl;
  GstAudioChannelPosition positions[64];
  gboolean have_mask = channel_mask != 0;

  acl_size =
      G_STRUCT_OFFSET (AudioChannelLayout, mChannelDescriptions[channels]);
  acl = g_malloc0 (acl_size);

  if (have_mask) {
    gst_audio_channel_positions_from_mask (channels, channel_mask, positions);
  }

  acl->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
  acl->mNumberChannelDescriptions = channels;
  acl->mChannelBitmap = 0;

  for (guint i = 0; i < channels; i++) {
    acl->mChannelDescriptions[i].mChannelLabel = have_mask ?
        _gst_pos_to_channel_label (positions[i], i) :
        (kAudioChannelLabel_Discrete_0 | i);

    acl->mChannelDescriptions[i].mChannelFlags = kAudioChannelFlags_AllOff;
    acl->mChannelDescriptions[i].mCoordinates[0] = 0.f;
    acl->mChannelDescriptions[i].mCoordinates[1] = 0.f;
    acl->mChannelDescriptions[i].mCoordinates[2] = 0.f;
  }

  return acl;
}

char *
aclayout_to_str (AudioChannelLayout * layout)
{
  GString *s = g_string_new (NULL);
  AudioChannelDescription *desc;

  if (!layout) {
    g_string_append (s, "NULL layout");
    return g_string_free (s, FALSE);
  }

  g_string_append_printf (s,
      "Tag: %u, Bitmap: 0x%08x, Channels: %u\n", layout->mChannelLayoutTag,
      layout->mChannelBitmap, layout->mNumberChannelDescriptions);

  for (UInt32 i = 0; i < layout->mNumberChannelDescriptions; i++) {
    desc = &layout->mChannelDescriptions[i];
    g_string_append_printf (s,
        "  [%u] Label: %u, Coords: (%f, %f, %f)\n", i, desc->mChannelLabel,
        desc->mCoordinates[0], desc->mCoordinates[1], desc->mCoordinates[2]);
  }

  return g_string_free (s, FALSE);
}

AudioDeviceID
get_default_coreaudio_device_id (gboolean is_sink)
{
  OSStatus status = noErr;
  UInt32 propertySize = sizeof (AudioDeviceID);
  AudioDeviceID device_id = kAudioDeviceUnknown;
  AudioObjectPropertySelector prop_selector;

  prop_selector = is_sink ? kAudioHardwarePropertyDefaultOutputDevice :
      kAudioHardwarePropertyDefaultInputDevice;

  AudioObjectPropertyAddress defaultDeviceAddress = {
    prop_selector,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };

  status = AudioObjectGetPropertyData (kAudioObjectSystemObject,
      &defaultDeviceAddress, 0, NULL, &propertySize, &device_id);
  if (status != noErr) {
    GST_ERROR ("Failed to get default CoreAudio device: %d", (int) status);
  }

  return device_id;
}

static inline char *
audio_device_get_uid (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  UInt32 prop_size;
  CFStringRef prop_val;
  CFIndex prop_len, max_size;
  gchar *device_name;
  AudioObjectPropertyAddress deviceNameAddress = {
    kAudioDevicePropertyDeviceUID,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };

  status = AudioObjectGetPropertyDataSize (device_id,
      &deviceNameAddress, 0, NULL, &prop_size);
  if (status != noErr) {
    GST_ERROR ("Failed to get device uid size: %d", (int) status);
    return NULL;
  }

  status = AudioObjectGetPropertyData (device_id,
      &deviceNameAddress, 0, NULL, &prop_size, &prop_val);
  if (status != noErr) {
    GST_ERROR ("Failed to get device uid: %d", (int) status);
    return NULL;
  }

  prop_len = CFStringGetLength (prop_val);
  max_size =
      CFStringGetMaximumSizeForEncoding (prop_len, kCFStringEncodingUTF8) + 1;
  device_name = g_malloc (max_size);

  if (!CFStringGetCString (prop_val, device_name, max_size,
          kCFStringEncodingUTF8)) {
    g_free (device_name);
    device_name = NULL;
  }

  CFRelease (prop_val);
  return device_name;
}

GPtrArray *
get_all_coreaudio_devices ()
{
  OSStatus status = noErr;
  UInt32 prop_size = 0;
  int n_devices;
  AudioDeviceID *device_ids;
  GPtrArray *devices;
  AudioObjectPropertyAddress address = {
    kAudioHardwarePropertyDevices,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };

  status = AudioObjectGetPropertyDataSize (kAudioObjectSystemObject,
      &address, 0, NULL, &prop_size);
  if (status != noErr) {
    GST_ERROR ("Failed to get size of devices list: %d", (int) status);
    return NULL;
  }

  n_devices = prop_size / sizeof (AudioDeviceID);
  device_ids = g_malloc (prop_size);
  if (!device_ids) {
    GST_ERROR ("Failed to alloc memory for devices list");
    return NULL;
  }

  status = AudioObjectGetPropertyData (kAudioObjectSystemObject,
      &address, 0, NULL, &prop_size, device_ids);
  if (status != noErr) {
    GST_ERROR ("Failed to retrieve CoreAudio devices: %d", (int) status);
    g_free (device_ids);
    return NULL;
  }

  devices =
      g_ptr_array_new_with_free_func ((GDestroyNotify) core_audio_device_free);

  for (int i = 0; i < n_devices; i++) {
    AudioDeviceID id = device_ids[i];
    gchar *uid = audio_device_get_uid (id);
    GstCoreAudioDeviceId *device;

    if (!uid) {
      GST_WARNING ("Failed to get UID for device %u", id);
      continue;
    }

    device = g_new0 (GstCoreAudioDeviceId, 1);
    device->id = id;
    device->uid = uid;

    g_ptr_array_add (devices, device);
  }

  g_free (device_ids);
  return devices;
}

GstAudioFormat
asbd_get_gst_audio_format (const AudioStreamBasicDescription * asbd)
{
  AudioFormatID format_id = asbd->mFormatID;
  AudioFormatFlags format_flags = asbd->mFormatFlags;
  GstAudioFormat format = GST_AUDIO_FORMAT_UNKNOWN;
  guint bps, endianness;
  gboolean sign;

  if (format_id != kAudioFormatLinearPCM) {
    GST_WARNING ("Only linear PCM is supported, got format ID %"
        GST_FOURCC_FORMAT, CORE_AUDIO_FOURCC_ARGS (format_id));
    return format;
  }

  if (!(format_flags & kAudioFormatFlagIsPacked)) {
    GST_WARNING ("Only packed formats supported");
    return format;
  }

  if (format_flags & kLinearPCMFormatFlagsSampleFractionMask) {
    GST_WARNING ("Fixed point audio is unsupported");
    return format;
  }

  bps = asbd->mBitsPerChannel;
  endianness = format_flags & kAudioFormatFlagIsBigEndian ?
      G_BIG_ENDIAN : G_LITTLE_ENDIAN;
  sign = format_flags & kAudioFormatFlagIsSignedInteger ? TRUE : FALSE;

  if (format_flags & kAudioFormatFlagIsFloat) {
    if (bps == 32) {
      if (endianness == G_LITTLE_ENDIAN)
        format = GST_AUDIO_FORMAT_F32LE;
      else
        format = GST_AUDIO_FORMAT_F32BE;
    } else if (bps == 64) {
      if (endianness == G_LITTLE_ENDIAN)
        format = GST_AUDIO_FORMAT_F64LE;
      else
        format = GST_AUDIO_FORMAT_F64BE;
    }
  } else {
    format = gst_audio_format_build_integer (sign, endianness, bps, bps);
  }

  return format;
}

AudioStreamBasicDescription *
asbd_copy (const AudioStreamBasicDescription * asbd)
{
  AudioStreamBasicDescription *dst = g_new0 (AudioStreamBasicDescription, 1);
  memcpy (dst, asbd, sizeof (AudioStreamBasicDescription));
  return dst;
}

void
asbd_free (AudioStreamBasicDescription * asbd)
{
  if (asbd)
    g_free (asbd);
}

void
asbd_clear (AudioStreamBasicDescription ** asbd)
{
  if (*asbd) {
    g_free (*asbd);
    *asbd = NULL;
  }
}

AudioChannelLayout *
aclayout_copy (const AudioChannelLayout * layout)
{
  gsize size;
  AudioChannelLayout *new_layout;

  if (!layout)
    return NULL;

  size =
      G_STRUCT_OFFSET (AudioChannelLayout,
      mChannelDescriptions[layout->mNumberChannelDescriptions]);
  new_layout = g_malloc0 (size);
  memcpy (new_layout, layout, size);
  return new_layout;
}

void
aclayout_free (AudioChannelLayout * layout)
{
  if (layout)
    g_free (layout);
}

void
aclayout_clear (AudioChannelLayout ** layout)
{
  if (*layout) {
    g_free (*layout);
    *layout = NULL;
  }
}

/* TODO: This shouldn't care about any conversion of formats/rates.
 * FIXME: Make this just give us the exact caps and then expand them elsewhere if needed. */
GstCaps *
asbd_to_caps (const AudioStreamBasicDescription * asbd,
    const AudioChannelLayout * layout, gboolean allow_any_rate,
    gboolean allow_conversion)
{
  GstCaps *caps = NULL, *out_caps = NULL, *templ_caps = NULL;
  GstAudioFormat format;
  guint64 channel_mask;
  guint rate, layout_channels, max_channels;

  rate = (guint) asbd->mSampleRate;
  if (rate == kAudioStreamAnyRate && !allow_any_rate) {
    GST_WARNING ("ASBD has no sample rate");
    goto error;
  }

  format = asbd_get_gst_audio_format (asbd);
  if (format == GST_AUDIO_FORMAT_UNKNOWN) {
    goto error;
  }

  if (layout == NULL) {
    layout_channels = max_channels = asbd->mChannelsPerFrame;
    channel_mask = 0;
  } else {
    if (!aclayout_to_mask_and_pos (layout,
            &layout_channels, &channel_mask, NULL)) {
      GST_WARNING ("Failed to parse AudioChannelLayout");
      goto error;
    }
    max_channels = asbd->mChannelsPerFrame;
  }

  g_assert (layout_channels <= max_channels);

  out_caps = gst_caps_new_empty ();
  templ_caps = gst_static_caps_get (&template_caps);
  templ_caps = gst_caps_make_writable (templ_caps);

  /* The preferred variant always goes first */
  /* TODO: this should all be done better, e.g. with variants combined together,
   * and make all of these have preferred format/rate first, anyformat/rate second */
  caps = gst_caps_copy (templ_caps);
  gst_caps_set_simple (caps,
      "format", G_TYPE_STRING, gst_audio_format_to_string (format),
      "channels", G_TYPE_INT, layout_channels, "rate", G_TYPE_INT, rate, NULL);
  /* TODO: channel mask 0 needs to be put here as well, e.g. for 4ch DAC output */
  if (channel_mask != 0 || layout_channels > 2) {
    gst_caps_set_simple (caps,
        "channel-mask", GST_TYPE_BITMASK, channel_mask, NULL);
  }
  gst_caps_append (out_caps, caps);

  if (!allow_conversion) {
    GST_DEBUG ("Converted ASBD to caps: %s", gst_caps_to_string (out_caps));
    gst_caps_unref (templ_caps);
    return out_caps;
  }

  /* Then the all-formats and maybe-any-rate variant */
  caps = gst_caps_copy (templ_caps);
  gst_caps_set_simple (caps, "channels", G_TYPE_INT, layout_channels, NULL);
  if (channel_mask != 0) {
    gst_caps_set_simple (caps,
        "channel-mask", GST_TYPE_BITMASK, channel_mask, NULL);
  }
  if (!allow_any_rate) {
    gst_caps_set_simple (caps, "rate", G_TYPE_INT, rate, NULL);
  }
  gst_caps_append (out_caps, caps);

  /* Special channel count cases:
   * - we have mono -> can expose stereo (CA will upmix)
   * - we have stereo -> can expose mono (CA will downmix)
   * - we have a positioned layout -> expose unpositioned layout with max channels
   */
  if (layout_channels == 1) {
    caps = gst_caps_copy (templ_caps);
    gst_caps_set_simple (caps,
        "channels", G_TYPE_INT, 2, "channel-mask", GST_TYPE_BITMASK,
        GST_AUDIO_CHANNEL_POSITION_MASK (FRONT_LEFT) |
        GST_AUDIO_CHANNEL_POSITION_MASK (FRONT_RIGHT), NULL);
    if (!allow_any_rate) {
      gst_caps_set_simple (caps, "rate", G_TYPE_INT, rate, NULL);
    }
    gst_caps_append (out_caps, caps);
  } else if (layout_channels == 2) {
    caps = gst_caps_copy (templ_caps);
    gst_caps_set_simple (caps,
        "format", G_TYPE_STRING, gst_audio_format_to_string (format),
        "channels", G_TYPE_INT, 1, NULL);
    if (!allow_any_rate) {
      gst_caps_set_simple (caps, "rate", G_TYPE_INT, rate, NULL);
    }
    gst_caps_append (out_caps, caps);
  }

  if (channel_mask != 0) {
    caps = gst_caps_copy (templ_caps);
    gst_caps_set_simple (caps, "channels", G_TYPE_INT, max_channels,
        "channel-mask", GST_TYPE_BITMASK, 0, NULL);
    if (!allow_any_rate) {
      gst_caps_set_simple (caps, "rate", G_TYPE_INT, rate, NULL);
    }
    gst_caps_append (out_caps, caps);
  }

  GST_DEBUG ("Converted ASBD to caps: %s", gst_caps_to_string (out_caps));
  gst_caps_unref (templ_caps);
  return out_caps;

error:
  return NULL;
}

AudioStreamBasicDescription *
asbd_from_audio_info (GstAudioInfo * info)
{
  /* Borrowed from osxaudio */
  AudioStreamBasicDescription *asbd = g_new0 (AudioStreamBasicDescription, 1);

  int width, depth;
  asbd->mFormatID = kAudioFormatLinearPCM;
  asbd->mSampleRate = (double) GST_AUDIO_INFO_RATE (info);
  asbd->mChannelsPerFrame = GST_AUDIO_INFO_CHANNELS (info);
  if (GST_AUDIO_INFO_IS_FLOAT (info)) {
    asbd->mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
    width = depth = GST_AUDIO_INFO_WIDTH (info);
  } else {
    asbd->mFormatFlags = kAudioFormatFlagIsSignedInteger;
    width = GST_AUDIO_INFO_WIDTH (info);
    depth = GST_AUDIO_INFO_DEPTH (info);
    if (width == depth) {
      asbd->mFormatFlags |= kAudioFormatFlagIsPacked;
    } else {
      asbd->mFormatFlags |= kAudioFormatFlagIsAlignedHigh;
    }
  }

  if (GST_AUDIO_INFO_IS_BIG_ENDIAN (info)) {
    asbd->mFormatFlags |= kAudioFormatFlagIsBigEndian;
  }

  asbd->mBytesPerFrame = GST_AUDIO_INFO_BPF (info);
  asbd->mBitsPerChannel = depth;
  asbd->mBytesPerPacket = GST_AUDIO_INFO_BPF (info);
  asbd->mFramesPerPacket = 1;
  asbd->mReserved = 0;

  return asbd;
}

gboolean
asbd_is_equal (const AudioStreamBasicDescription * a,
    const AudioStreamBasicDescription * b)
{
  if (a == b)
    return TRUE;

  if (a == NULL || b == NULL)
    return FALSE;

  return (a->mSampleRate == b->mSampleRate &&
      a->mFormatID == b->mFormatID &&
      a->mFormatFlags == b->mFormatFlags &&
      a->mBytesPerPacket == b->mBytesPerPacket &&
      a->mFramesPerPacket == b->mFramesPerPacket &&
      a->mBytesPerFrame == b->mBytesPerFrame &&
      a->mChannelsPerFrame == b->mChannelsPerFrame &&
      a->mBitsPerChannel == b->mBitsPerChannel && a->mReserved == b->mReserved);
}

AudioBufferList *
audio_buffer_list_prepare (UInt32 channels, UInt32 size)
{
  AudioBufferList *list;
  gsize list_size;
  UInt32 n = 0;

  /* We only support interleaved audio for now. */
  list_size = G_STRUCT_OFFSET (AudioBufferList, mBuffers[1]);
  list = (AudioBufferList *) g_malloc (list_size);

  list->mNumberBuffers = 1;
  /* See https://web.archive.org/web/20230918063924/https://lists.apple.com/archives/coreaudio-api/2015/Feb/msg00027.html */
  list->mBuffers[n].mNumberChannels = channels;
  /* AudioUnitRender will usually overwrite mDataByteSize */
  list->mBuffers[n].mDataByteSize = size;
  list->mBuffers[n].mData = g_malloc (size);

  return list;
}

void
audio_buffer_list_free (AudioBufferList * list)
{
  UInt32 n;

  if (list == NULL)
    return;

  for (n = 0; n < list->mNumberBuffers; ++n) {
    g_free (list->mBuffers[n].mData);
  }
  g_free (list);
}

void
core_audio_device_free (GstCoreAudioDeviceId * device)
{
  if (!device)
    return;

  g_free (device->uid);
  g_free (device);
}

uint64_t
mach_absolute_time_to_nanoseconds (uint64_t mach_time)
{
  dispatch_once (&timebase_once, ^ {
        mach_timebase_info (&timebase_info);
      }
  );

  return (mach_time * timebase_info.numer) / timebase_info.denom;
}

uint64_t
nanoseconds_to_mach_absolute_time (uint64_t nanoseconds)
{
  dispatch_once (&timebase_once, ^ {
        mach_timebase_info (&timebase_info);
      }
  );

  return (nanoseconds * timebase_info.denom) / timebase_info.numer;
}

char *
audiodevice_get_prop_str (AudioDeviceID device_id,
    AudioObjectPropertyElement prop_id)
{
  OSStatus status = noErr;
  UInt32 propertySize = 0;
  CFStringRef prop_val;
  gchar *result = NULL;

  AudioObjectPropertyAddress propAddress = {
    prop_id,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMain
  };

  propAddress.mScope = kAudioObjectPropertyScopeGlobal;

  /* Get the length of the device name */
  status = AudioObjectGetPropertyDataSize (device_id,
      &propAddress, 0, NULL, &propertySize);
  if (status != noErr) {
    goto beach;
  }

  /* Get the requested property */
  status = AudioObjectGetPropertyData (device_id,
      &propAddress, 0, NULL, &propertySize, &prop_val);
  if (status != noErr) {
    goto beach;
  }

  /* Convert to UTF-8 C String */
  CFIndex prop_len = CFStringGetLength (prop_val);
  CFIndex max_size =
      CFStringGetMaximumSizeForEncoding (prop_len, kCFStringEncodingUTF8) + 1;
  result = g_malloc (max_size);

  if (!CFStringGetCString (prop_val, result, max_size, kCFStringEncodingUTF8)) {
    g_free (result);
    result = NULL;
  }

  CFRelease (prop_val);

beach:
  return result;
}

UInt32
audiodevice_get_prop_uint32 (AudioDeviceID device_id,
    AudioObjectPropertyElement prop_id)
{
  OSStatus status = noErr;
  UInt32 propertySize = sizeof (UInt32);
  UInt32 result = UINT32_MAX;

  AudioObjectPropertyAddress propAddress = {
    .mSelector = prop_id,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain,
  };

  /* Get the requested property */
  status = AudioObjectGetPropertyData (device_id,
      &propAddress, 0, NULL, &propertySize, &result);
  if (status != noErr) {
    GST_WARNING ("Device %i, error code %i while fetching property %"
        GST_FOURCC_FORMAT, device_id, status,
        GST_FOURCC_ARGS (GUINT32_FROM_BE (prop_id)));
  }

  return result;
}
