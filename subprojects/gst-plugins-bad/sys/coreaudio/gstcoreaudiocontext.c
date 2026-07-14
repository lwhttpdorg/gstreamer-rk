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

#include "gstcoreaudiocontext.h"
#include "gstcoreaudioutils.h"

GST_DEBUG_CATEGORY_EXTERN (gst_coreaudio_debug);
#define GST_CAT_DEFAULT gst_coreaudio_debug

static AudioDeviceID
figure_out_device_id_from_uid (const gchar * device_uid,
    GstCoreAudioDeviceMode mode)
{
  AudioDeviceID device_id = kAudioDeviceUnknown;
  gboolean is_sink = (mode == GST_COREAUDIO_DEVICE_MODE_SINK);
  GPtrArray *devices;

  if (device_uid == NULL) {
    GST_DEBUG ("No device uid specified, using default device");
    return get_default_coreaudio_device_id (is_sink);
  }

  devices = get_all_coreaudio_devices ();
  if (devices == NULL || devices->len == 0) {
    GST_ERROR ("No devices found");
    if (devices)
      g_ptr_array_free (devices, TRUE);
    return device_id;
  }

  for (guint i = 0; i < devices->len; i++) {
    GstCoreAudioDeviceId *device = g_ptr_array_index (devices, i);
    GST_LOG ("Checking device: %s id: %u", device->uid, device->id);
    if (g_strcmp0 (device->uid, device_uid) == 0) {
      device_id = device->id;
      GST_DEBUG ("Found device with matching uid: %s id: %u", device_uid,
          device_id);
      break;
    }
  }

  g_ptr_array_free (devices, TRUE);

  if (device_id == kAudioDeviceUnknown) {
    GST_WARNING ("No device found with uid: %s", device_uid);
  }

  return device_id;
}

static AudioUnit
make_audiounit_for_device (AudioDeviceID device_id, GstCoreAudioDeviceMode mode)
{
  AudioComponentDescription desc;
  AudioComponent component;
  OSStatus status;
  AudioUnit unit;

  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_HALOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;
  desc.componentFlags = 0;
  desc.componentFlagsMask = 0;

  component = AudioComponentFindNext (NULL, &desc);
  if (component == NULL) {
    GST_WARNING ("Couldn't find a HALOutput component");
    return FALSE;
  }

  status = AudioComponentInstanceNew (component, &unit);
  if (status != noErr) {
    GST_WARNING ("Couldn't open HALOutput component: %d", status);
    return FALSE;
  }

  if (mode == GST_COREAUDIO_DEVICE_MODE_SRC) {
    /* enable I/O on input element (1), disable on output (0)
     * nothing needed when we're a sink - default settings are fine */
    UInt32 enable = 1;
    status =
        AudioUnitSetProperty (unit, kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Input, 1, &enable, sizeof (enable));

    if (status != noErr) {
      GST_WARNING ("Failed to enable input on AU: %d", status);
      goto failed;
    }

    enable = 0;
    status =
        AudioUnitSetProperty (unit, kAudioOutputUnitProperty_EnableIO,
        kAudioUnitScope_Output, 0, &enable, sizeof (enable));
    if (status != noErr) {
      GST_WARNING ("Failed to disable output on AU: %d", status);
      goto failed;
    }
  }

  status =
      AudioUnitSetProperty (unit, kAudioOutputUnitProperty_CurrentDevice,
      kAudioUnitScope_Global, 0, &device_id, sizeof (AudioDeviceID));
  if (status != noErr) {
    GST_WARNING ("Failed binding to device %d: %d", device_id, status);
    goto failed;
  }

  GST_DEBUG ("Created HALOutput AudioUnit %p for device %u", unit, device_id);

  return unit;

failed:
  AudioComponentInstanceDispose (unit);
  return FALSE;
}

static gboolean
gst_coreaudio_ctx_set_au_property (GstCoreAudioCtx * ctx,
    AudioUnitPropertyID inID, void *inData, UInt32 inDataSize,
    gboolean outer_scope)
{
  OSStatus status;

  /* From AUComponent.h:
   * Bus 0 is used to send audio output to the device; bus 1 is used
   * to receive audio input from the device. */
  gboolean is_sink = gst_coreaudio_ctx_is_sink (ctx);
  AudioUnitScope scope = outer_scope ?
      (is_sink ? kAudioUnitScope_Output : kAudioUnitScope_Input) :
      (is_sink ? kAudioUnitScope_Input : kAudioUnitScope_Output);
  AudioUnitElement element = is_sink ? 0 : 1;

  status =
      AudioUnitSetProperty (ctx->unit, inID, scope, element, inData,
      inDataSize);
  if (status != noErr) {
    GST_WARNING ("Failed to set AudioUnit property: %d", (int) status);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_coreaudio_ctx_get_au_property (GstCoreAudioCtx * ctx,
    AudioUnitPropertyID inID, void *outData, UInt32 * outDataSize,
    gboolean outer_scope)
{
  OSStatus status;

  /* From AUComponent.h:
   * Bus 0 is used to send audio output to the device; bus 1 is used
   * to receive audio input from the device. */
  gboolean is_sink = gst_coreaudio_ctx_is_sink (ctx);
  AudioUnitScope scope = outer_scope ?
      (is_sink ? kAudioUnitScope_Output : kAudioUnitScope_Input) :
      (is_sink ? kAudioUnitScope_Input : kAudioUnitScope_Output);
  AudioUnitElement element = is_sink ? 0 : 1;

  status =
      AudioUnitGetProperty (ctx->unit, inID, scope, element, outData,
      outDataSize);
  if (status != noErr) {
    GST_WARNING ("Failed to get AudioUnit property: %d", (int) status);
    return FALSE;
  }

  return TRUE;
}

static OSStatus
audiodevice_prop_change_callback (AudioObjectID inObjectID,
    UInt32 inNumberAddresses,
    const AudioObjectPropertyAddress inAddresses[], void *inClientData)
{
  GstCoreAudioCtx *ctx = (GstCoreAudioCtx *) inClientData;

  for (guint i = 0; i < inNumberAddresses; i++) {
    AudioObjectPropertyAddress addr = inAddresses[i];
    GST_DEBUG
        ("AudioDevice property changed: id=%u Selector=%u Scope=%u Element=%u",
        inObjectID, addr.mSelector, addr.mScope, addr.mElement);

    switch (addr.mSelector) {
      case kAudioDevicePropertyDeviceIsAlive:
        GST_DEBUG ("Device most likely disconnected, stopping context");
        ctx->should_stop = TRUE;
        break;
      default:
        GST_LOG ("Unhandled device property change selector=%u",
            addr.mSelector);
        break;
    }
  }

  return noErr;
}

static void
audiounit_prop_change_callback (void *inRefCon, AudioUnit inUnit,
    AudioUnitPropertyID inID, AudioUnitScope inScope,
    AudioUnitElement inElement)
{
  GstCoreAudioCtx *ctx = (GstCoreAudioCtx *) inRefCon;
  g_assert (inUnit == ctx->unit);

  GST_DEBUG ("AudioUnit property changed: id=%u scope=%u element=%u", inID,
      inScope, inElement);

  /* TODO: Should react to those in the future.
   * e.g. adjust conversion on the fly. */
  switch (inID) {
    case kAudioUnitProperty_StreamFormat:
      GST_DEBUG ("Stream format property changed");
      break;
    case kAudioUnitProperty_AudioChannelLayout:
      GST_DEBUG ("Audio channel layout property changed");
      break;
    default:
      GST_LOG ("Unhandled property change id=%u", inID);
      break;
  }
}

static gboolean
gst_coreaudio_ctx_set_io_callback (GstCoreAudioCtx * ctx,
    AURenderCallbackStruct callback)
{
  AudioUnitPropertyID callback_type;

  callback_type = gst_coreaudio_ctx_is_sink (ctx) ?
      kAudioUnitProperty_SetRenderCallback :
      kAudioOutputUnitProperty_SetInputCallback;

  return gst_coreaudio_ctx_set_au_property (ctx, callback_type, &callback,
      sizeof (callback), FALSE);
}

static gboolean
gst_coreaudio_ctx_attach_listeners (GstCoreAudioCtx * ctx)
{
  if (ctx->listeners_attached)
    return TRUE;

  AudioUnitPropertyID au_props[] = {
    kAudioUnitProperty_StreamFormat,
    kAudioUnitProperty_AudioChannelLayout,
  };
  AudioDevicePropertyID device_props[] = {
    kAudioDevicePropertyDeviceIsAlive,
  };
  guint n_properties = G_N_ELEMENTS (au_props);
  OSStatus status;

  /* AudioUnits listener - stream format, SR, layout, etc. */
  for (guint i = 0; i < n_properties; i++) {
    AudioUnitPropertyID prop_id = au_props[i];
    status = AudioUnitAddPropertyListener (ctx->unit, prop_id,
        audiounit_prop_change_callback, ctx);
    if (status != noErr) {
      GST_WARNING ("Failed to add property listener for property %u: %d",
          prop_id, (int) status);
      return FALSE;
    }
  }

  /* AudioDevice listener - device disconnection etc. */
  n_properties = G_N_ELEMENTS (device_props);
  for (guint i = 0; i < n_properties; i++) {
    AudioDevicePropertyID prop_id = device_props[i];
    AudioObjectPropertyAddress addr = {
      prop_id,
      kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain,
    };
    status = AudioObjectAddPropertyListener (ctx->device_id, &addr,
        audiodevice_prop_change_callback, ctx);
    if (status != noErr) {
      GST_WARNING ("Failed to add device property listener for property %u: %d",
          prop_id, (int) status);
      return FALSE;
    }
  }

  ctx->listeners_attached = TRUE;

  return TRUE;
}

static AudioChannelLayout *
gst_coreaudio_ctx_get_hw_channel_layout (GstCoreAudioCtx * ctx)
{
  UInt32 size = 0;
  AudioChannelLayout *layout;

  if (!gst_coreaudio_ctx_get_au_property (ctx,
          kAudioUnitProperty_AudioChannelLayout, NULL, &size, TRUE)) {
    GST_WARNING ("Failed to get size of channel layout");
    return NULL;
  }

  layout = g_malloc0 (size);
  if (!gst_coreaudio_ctx_get_au_property (ctx,
          kAudioUnitProperty_AudioChannelLayout, layout, &size, TRUE)) {
    GST_WARNING ("Failed to get channel layout");
    g_free (layout);
    return NULL;
  }

  return layout;
}

static gboolean
gst_coreaudio_ctx_set_channel_layout (GstCoreAudioCtx * ctx,
    AudioChannelLayout * layout)
{
  gsize layoutSize = G_STRUCT_OFFSET (AudioChannelLayout,
      mChannelDescriptions[layout->mNumberChannelDescriptions]);

  GST_DEBUG_AUDIO_CHANNEL_LAYOUT ("Setting channel layout: ", *layout);

  return gst_coreaudio_ctx_set_au_property (ctx,
      kAudioUnitProperty_AudioChannelLayout, layout, layoutSize, FALSE);
}

static AudioStreamBasicDescription *
gst_coreaudio_ctx_get_hw_stream_format (GstCoreAudioCtx * ctx)
{
  UInt32 size = sizeof (AudioStreamBasicDescription);
  AudioStreamBasicDescription *asbd = g_new0 (AudioStreamBasicDescription, 1);

  if (!gst_coreaudio_ctx_get_au_property (ctx,
          kAudioUnitProperty_StreamFormat, asbd, &size, TRUE)) {
    GST_WARNING ("Failed to get hardware stream format");
    g_free (asbd);
    return NULL;
  }

  return asbd;
}

gboolean
gst_coreaudio_ctx_is_sink (GstCoreAudioCtx * ctx)
{
  return ctx->mode == GST_COREAUDIO_DEVICE_MODE_SINK;
}

gboolean
gst_coreaudio_ctx_prepare (GstCoreAudioCtx * ctx,
    AudioStreamBasicDescription * req_format,
    AudioChannelLayout * req_layout, guint32 req_fpp)
{
  AudioValueRange fpp_range;
  UInt32 prop_size, current_fpp;
  AudioStreamBasicDescription *hw_format, *out_format;
  AudioChannelLayout *hw_layout;
  gboolean is_sink;

  if (ctx->prepared) {
    GST_WARNING ("Context already initialized");
    return FALSE;
  }

  hw_format = ctx->hw_format;
  hw_layout = ctx->hw_layout;
  is_sink = gst_coreaudio_ctx_is_sink (ctx);

  /* CoreAudio will happily convert everything in sink mode.
   * For src mode we have to do the converting ourselves. See Apple TN2091. */
  if (!asbd_is_equal (req_format, hw_format) && !is_sink) {
    GstCaps *conv_in_caps, *conv_out_caps;
    GstStructure *converter_config;

    GST_INFO ("Setting up conversion from requested format to hardware format");
    GST_LOG ("HW format: " CORE_AUDIO_FORMAT,
        CORE_AUDIO_FORMAT_ARGS (*hw_format));
    GST_LOG ("Requested format: " CORE_AUDIO_FORMAT,
        CORE_AUDIO_FORMAT_ARGS (*req_format));

    if (!gst_coreaudio_ctx_set_au_property (ctx,
            kAudioUnitProperty_StreamFormat, hw_format,
            sizeof (*hw_format), FALSE)) {
      GST_ERROR ("Failed to set stream format to hardware format");
      return FALSE;
    }

    /* Ugly roundabout way for now, FIXME */
    conv_in_caps = asbd_to_caps (hw_format, hw_layout, FALSE, FALSE);
    conv_out_caps = asbd_to_caps (req_format, req_layout, FALSE, FALSE);
    if (!gst_audio_info_from_caps (&ctx->conv_in_info, conv_in_caps) ||
        !gst_audio_info_from_caps (&ctx->conv_out_info, conv_out_caps)) {
      GST_ERROR ("Failed to parse caps for conversion");
      gst_clear_caps (&conv_in_caps);
      gst_clear_caps (&conv_out_caps);
      return FALSE;
    }

    gst_clear_caps (&conv_in_caps);
    gst_clear_caps (&conv_out_caps);

    converter_config = gst_structure_new_static_str ("converter-config",
        GST_AUDIO_CONVERTER_OPT_DITHER_METHOD, GST_TYPE_AUDIO_DITHER_METHOD,
        GST_AUDIO_DITHER_TPDF, GST_AUDIO_CONVERTER_OPT_RESAMPLER_METHOD,
        GST_TYPE_AUDIO_RESAMPLER_METHOD, GST_AUDIO_RESAMPLER_METHOD_KAISER,
        NULL);
    gst_audio_resampler_options_set_quality (GST_AUDIO_RESAMPLER_METHOD_KAISER,
        GST_AUDIO_RESAMPLER_QUALITY_DEFAULT,
        GST_AUDIO_INFO_RATE (&ctx->conv_in_info),
        GST_AUDIO_INFO_RATE (&ctx->conv_out_info), converter_config);

    ctx->conv = gst_audio_converter_new (GST_AUDIO_CONVERTER_FLAG_NONE,
        &ctx->conv_in_info, &ctx->conv_out_info, converter_config);

    if (!ctx->conv) {
      GST_ERROR ("Couldn't create converter for requested format");
      return FALSE;
    }

    ctx->input_fifo = g_byte_array_new ();
    ctx->input_fifo_bytes = 0;
    ctx->output_fifo = g_byte_array_new ();
    ctx->output_fifo_bytes = 0;
  } else {
    /* TODO: We might need manual conversion / up/downmixing for sinks as well.
     * e.g. we stream to a 4ch DAC and switch to 2ch headphones,
     * CoreAudio currently errors out in that case */
    GST_DEBUG ("Setting stream format: " CORE_AUDIO_FORMAT,
        CORE_AUDIO_FORMAT_ARGS (*req_format));

    if (!gst_coreaudio_ctx_set_au_property (ctx,
            kAudioUnitProperty_StreamFormat, req_format, sizeof (*req_format),
            FALSE)) {
      GST_ERROR ("Failed to set stream format");
      return FALSE;
    }

    if (is_sink && !gst_coreaudio_ctx_set_channel_layout (ctx, req_layout)) {
      GST_ERROR ("Failed to set channel layout");
      return FALSE;
    }
  }

  prop_size = sizeof (current_fpp);
  if (!gst_coreaudio_ctx_get_au_property (ctx,
          kAudioDevicePropertyBufferFrameSize,
          &current_fpp, &prop_size, TRUE)) {
    GST_ERROR ("Failed to get current frame size from device");
    return FALSE;
  }

  prop_size = sizeof (fpp_range);
  gst_coreaudio_ctx_get_au_property (ctx,
      kAudioDevicePropertyBufferFrameSizeRange, &fpp_range, &prop_size, FALSE);

  GST_DEBUG ("Device current fpp %u, range [%f - %f], requested %u",
      current_fpp, fpp_range.mMinimum, fpp_range.mMaximum, req_fpp);

  /* FIXME: we might want to rescale fpp if we're converting to maintain 
   * similar latency/buffer-time to the one initially configured? */
  if (req_fpp != 0 && req_fpp != current_fpp) {
    if (req_fpp < fpp_range.mMinimum) {
      GST_WARNING
          ("Wanted frames per packet %u < device min %f, using device min",
          req_fpp, fpp_range.mMinimum);
      req_fpp = fpp_range.mMinimum;
    } else if (req_fpp > fpp_range.mMaximum) {
      GST_INFO
          ("Wanted frames per packet %u > device max %f, using device max",
          req_fpp, fpp_range.mMaximum);
      req_fpp = fpp_range.mMaximum;
    }

    if (!gst_coreaudio_ctx_set_au_property (ctx,
            kAudioDevicePropertyBufferFrameSize, &req_fpp,
            sizeof (req_fpp), FALSE)) {
      GST_ERROR ("Failed to set frame size");
      return FALSE;
    }

    ctx->frames_per_packet = req_fpp;
  } else {
    ctx->frames_per_packet = current_fpp;
  }

  if (AudioUnitInitialize (ctx->unit) != noErr) {
    GST_ERROR ("Failed to initialize AudioUnit");
    return FALSE;
  }

  if (AudioOutputUnitStart (ctx->unit) != noErr) {
    GST_ERROR ("Failed to start AudioUnit for warmup");
    return FALSE;
  }

  if (AudioOutputUnitStop (ctx->unit) != noErr) {
    GST_ERROR ("Failed to stop AudioUnit after warmup");
    return FALSE;
  }

  out_format = ctx->conv ? req_format : hw_format;
  ctx->selected_format = asbd_copy (out_format);
  ctx->selected_layout = aclayout_copy (req_layout);
  if (!is_sink) {
    ctx->abl_buf_size = ctx->frames_per_packet * out_format->mBytesPerFrame;
    ctx->abl =
        audio_buffer_list_prepare (out_format->mChannelsPerFrame,
        ctx->abl_buf_size);
  }

  GST_DEBUG ("Context for device %d is ready", ctx->device_id);
  ctx->prepared = TRUE;

  return TRUE;
}

gboolean
gst_coreaudio_ctx_render (GstCoreAudioCtx * ctx,
    const AudioTimeStamp * in_timestamp, UInt32 in_frames,
    gpointer * out_data, gsize * out_bytes)
{
  OSStatus status;
  gsize rendered_bytes;
  gpointer rendered_data;

  /* If we're converting, clear output from previous call */
  if (ctx->conv && ctx->output_fifo_bytes > 0) {
    g_byte_array_set_size (ctx->output_fifo, 0);
    ctx->output_fifo_bytes = 0;
  }

  /* AudioUnitRender always changes mDataByteSize into
   * the number of bytes actually read. */
  ctx->abl->mBuffers[0].mDataByteSize = ctx->abl_buf_size;

  GST_LOG ("Rendering %u frames at sample time %f", in_frames,
      in_timestamp->mSampleTime);
  status = AudioUnitRender (ctx->unit, 0, in_timestamp, 1, in_frames, ctx->abl);

  if (status != noErr) {
    GST_ERROR ("AudioUnitRender failed: %d", status);
    *out_data = NULL;
    *out_bytes = 0;
    return FALSE;
  }

  rendered_bytes = ctx->abl->mBuffers[0].mDataByteSize;
  rendered_data = ctx->abl->mBuffers[0].mData;

  /* Simply return the result of render() if no conversion is needed */
  if (ctx->conv == NULL) {
    *out_data = rendered_data;
    *out_bytes = rendered_bytes;
    return TRUE;
  }

  /* Otherwise do the conversion... */
  if (rendered_bytes > 0) {
    gsize old_size = ctx->input_fifo_bytes;
    g_byte_array_set_size (ctx->input_fifo, old_size + rendered_bytes);
    memcpy (ctx->input_fifo->data + old_size, rendered_data, rendered_bytes);
    ctx->input_fifo_bytes += rendered_bytes;
  }

  /* Convert as much as the converter can give us from input_fifo to output_fifo */
  while (ctx->input_fifo_bytes >= ctx->conv_in_info.bpf) {
    gsize in_frames_avail = ctx->input_fifo_bytes / ctx->conv_in_info.bpf;
    gsize out_frames =
        gst_audio_converter_get_out_frames (ctx->conv, in_frames_avail);
    gsize conv_out_bytes, old_output_bytes, consumed_bytes;
    gpointer in_planes[1], out_planes[1];

    if (out_frames == 0)
      break;

    conv_out_bytes = out_frames * ctx->conv_out_info.bpf;
    old_output_bytes = ctx->output_fifo_bytes;
    g_byte_array_set_size (ctx->output_fifo, old_output_bytes + conv_out_bytes);

    in_planes[0] = ctx->input_fifo->data;
    out_planes[0] = ctx->output_fifo->data + old_output_bytes;

    if (!gst_audio_converter_samples (ctx->conv,
            GST_AUDIO_CONVERTER_FLAG_NONE,
            in_planes, in_frames_avail, out_planes, out_frames)) {
      GST_ERROR ("Audio conversion failed");
      g_byte_array_set_size (ctx->output_fifo, old_output_bytes);
      *out_data = NULL;
      *out_bytes = 0;
      return FALSE;
    }

    ctx->output_fifo_bytes += conv_out_bytes;

    consumed_bytes = in_frames_avail * ctx->conv_in_info.bpf;
    if (consumed_bytes < ctx->input_fifo_bytes) {
      memmove (ctx->input_fifo->data,
          ctx->input_fifo->data + consumed_bytes,
          ctx->input_fifo_bytes - consumed_bytes);
    }
    ctx->input_fifo_bytes -= consumed_bytes;
    g_byte_array_set_size (ctx->input_fifo, ctx->input_fifo_bytes);

    if (consumed_bytes == 0)
      break;
  }

  /* Return whatever we have in output FIFO and assume downstream will consume it all */
  *out_data = ctx->output_fifo->data;
  *out_bytes = ctx->output_fifo_bytes;

  return TRUE;
}

gboolean
gst_coreaudio_ctx_start (GstCoreAudioCtx * ctx, AURenderCallbackStruct callback)
{
  if (ctx->conv) {
    gst_audio_converter_reset (ctx->conv);
    g_byte_array_set_size (ctx->input_fifo, 0);
    ctx->input_fifo_bytes = 0;
    g_byte_array_set_size (ctx->output_fifo, 0);
    ctx->output_fifo_bytes = 0;
  }

  if (!gst_coreaudio_ctx_attach_listeners (ctx)) {
    GST_ERROR ("Failed to attach property listeners");
    return FALSE;
  }

  if (!gst_coreaudio_ctx_set_io_callback (ctx, callback)) {
    GST_ERROR ("Failed to set AudioUnit callback");
    return FALSE;
  }

  if (AudioOutputUnitStart (ctx->unit) != noErr) {
    GST_ERROR ("Failed to start AudioUnit");
    return FALSE;
  }

  ctx->running = TRUE;
  ctx->should_stop = FALSE;
  GST_DEBUG ("Context started");

  return TRUE;
}

gboolean
gst_coreaudio_ctx_stop (GstCoreAudioCtx * ctx)
{
  AURenderCallbackStruct callback;

  if (AudioOutputUnitStop (ctx->unit) != noErr) {
    GST_ERROR ("Failed to stop AudioUnit");
    return FALSE;
  }

  callback.inputProc = NULL;
  callback.inputProcRefCon = NULL;
  gst_coreaudio_ctx_set_io_callback (ctx, callback);

  ctx->running = FALSE;
  ctx->should_stop = FALSE;
  GST_DEBUG ("Context stopped");

  return TRUE;
}

GstCoreAudioCtx *
gst_coreaudio_ctx_new (GstCoreAudioDeviceMode mode, const gchar * device_uid)
{
  GstCoreAudioCtx *ctx;
  AudioDeviceID device_id = figure_out_device_id_from_uid (device_uid, mode);
  AudioUnit unit;

  if (device_id == kAudioDeviceUnknown) {
    return NULL;
  }

  unit = make_audiounit_for_device (device_id, mode);
  if (!unit) {
    return NULL;
  }

  ctx = g_new0 (GstCoreAudioCtx, 1);
  ctx->mode = mode;
  ctx->device_id = device_id;
  ctx->unit = unit;
  ctx->frames_per_packet = 0;
  ctx->prepared = FALSE;
  ctx->selected_format = NULL;
  ctx->hw_layout = NULL;
  ctx->abl = NULL;
  ctx->abl_buf_size = 0;
  ctx->conv = NULL;
  ctx->input_fifo = NULL;
  ctx->input_fifo_bytes = 0;
  ctx->output_fifo = NULL;
  ctx->output_fifo_bytes = 0;
  ctx->running = FALSE;
  ctx->should_stop = FALSE;
  ctx->listeners_attached = FALSE;

  ctx->hw_format = gst_coreaudio_ctx_get_hw_stream_format (ctx);
  if (!ctx->hw_format) {
    GST_ERROR ("Failed to retrieve HW format for device %u", device_id);
    AudioComponentInstanceDispose (unit);
    g_free (ctx);
    return NULL;
  }

  GST_DEBUG ("Got hardware stream format: " CORE_AUDIO_FORMAT,
      CORE_AUDIO_FORMAT_ARGS (*ctx->hw_format));

  if (gst_coreaudio_ctx_is_sink (ctx)) {
    /* TODO: check if kAudioUnitProperty_SupportedChannelLayoutTags might be useful */
    ctx->hw_layout = gst_coreaudio_ctx_get_hw_channel_layout (ctx);

    if (!ctx->hw_layout) {
      GST_WARNING ("Failed to get hw channel layout for device %u", device_id);
    } else {
      GST_DEBUG_AUDIO_CHANNEL_LAYOUT ("Got hardware channel layout: ",
          *ctx->hw_layout);
    }
  }

  GST_DEBUG ("Created CoreAudio context for device %u (uid %s) mode %d",
      device_id, DEVICE_UID_STR (device_uid), mode);

  return ctx;
}

void
gst_coreaudio_ctx_free (GstCoreAudioCtx * ctx)
{
  if (ctx->running)
    gst_coreaudio_ctx_stop (ctx);

  if (ctx->unit) {
    AudioUnitUninitialize (ctx->unit);
    AudioComponentInstanceDispose (ctx->unit);
    ctx->unit = NULL;
  }

  asbd_clear (&ctx->selected_format);
  aclayout_clear (&ctx->selected_layout);
  asbd_clear (&ctx->hw_format);
  aclayout_clear (&ctx->hw_layout);

  if (ctx->abl) {
    audio_buffer_list_free (ctx->abl);
    ctx->abl = NULL;
  }

  if (ctx->conv) {
    gst_audio_converter_free (ctx->conv);
    g_byte_array_free (ctx->input_fifo, TRUE);
    g_byte_array_free (ctx->output_fifo, TRUE);
    ctx->conv = NULL;
    ctx->input_fifo = NULL;
    ctx->output_fifo = NULL;
  }

  g_free (ctx);
}
