/* GStreamer
 * Copyright (C) 2016 Hyunjun Ko <zzoon@igalia.com>
 * Copyright (C) 2025 Nirbheek Chauhan <nirbheek@centricular.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* The following code is mostly identical to osxaudio's device provider.
 * It has the same functionality with the small exception of not exposing the
 * AudioDeviceID because we've moved to using unique IDs instead. */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include "gstcoreaudiodeviceprovider.h"
#include "gstcoreaudiocontext.h"

GST_DEBUG_CATEGORY_EXTERN (gst_coreaudio_debug);
#define GST_CAT_DEFAULT gst_coreaudio_debug

#if defined(MAC_OS_X_VERSION_MAX_ALLOWED) && MAC_OS_X_VERSION_MAX_ALLOWED < 120000
#define kAudioObjectPropertyElementMain kAudioObjectPropertyElementMaster
#endif

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_COREAUDIO_STATIC_CAPS)
    );

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_COREAUDIO_STATIC_CAPS)
    );

static GstCoreAudioDevice *gst_coreaudio_device_new (GstCoreAudioCtx * ctx,
    const gchar * device_uid, const gchar * device_name, UInt32 transport_type,
    GstCoreAudioDeviceMode mode, gboolean is_default);

G_DEFINE_TYPE (GstCoreAudioDeviceProvider, gst_coreaudio_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static gboolean gst_coreaudio_device_provider_start (GstDeviceProvider *
    provider);
static void gst_coreaudio_device_provider_stop (GstDeviceProvider * provider);
static GList *gst_coreaudio_device_provider_probe (GstDeviceProvider *
    provider);
static void
gst_coreaudio_device_provider_update_devices (GstCoreAudioDeviceProvider *
    self);

static void
gst_coreaudio_device_provider_class_init (GstCoreAudioDeviceProviderClass *
    klass)
{
  GstDeviceProviderClass *dm_class = GST_DEVICE_PROVIDER_CLASS (klass);

  dm_class->start = gst_coreaudio_device_provider_start;
  dm_class->stop = gst_coreaudio_device_provider_stop;
  dm_class->probe = gst_coreaudio_device_provider_probe;

  gst_device_provider_class_set_static_metadata (dm_class,
      "CoreAudio device provider", "Source/Sink/Audio",
      "List and monitor macOS audio source and sink devices",
      "Hyunjun Ko <zzoon@igalia.com>, Nirbheek Chauhan <nirbheek@centricular.com>, Piotr Brzeziński <piotr@centricular.com>");
}

static void
gst_coreaudio_device_provider_init (GstCoreAudioDeviceProvider * provider)
{
}

static inline gboolean
_audio_device_is_default (AudioDeviceID device_id, gboolean is_sink)
{
  AudioDeviceID default_id = kAudioObjectUnknown;
  OSStatus status = noErr;
  UInt32 propertySize = sizeof (AudioDeviceID);

  AudioObjectPropertyAddress defaultDeviceAddress = {
    is_sink ? kAudioHardwarePropertyDefaultOutputDevice :
        kAudioHardwarePropertyDefaultInputDevice,
    kAudioObjectPropertyScopeGlobal,
    kAudioObjectPropertyElementMain
  };

  status = AudioObjectGetPropertyData (kAudioObjectSystemObject,
      &defaultDeviceAddress, 0, NULL, &propertySize, &default_id);

  if (status != noErr) {
    GST_WARNING ("failed getting default device property: %d", (int) status);
    return FALSE;
  }

  return device_id == default_id;
}

static inline gboolean
_audio_device_has_output (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  UInt32 propertySize;

  AudioObjectPropertyAddress streamsAddress = {
    kAudioDevicePropertyStreams,
    kAudioDevicePropertyScopeOutput,
    kAudioObjectPropertyElementMain
  };

  status = AudioObjectGetPropertyDataSize (device_id,
      &streamsAddress, 0, NULL, &propertySize);

  if (status != noErr) {
    GST_WARNING ("failed getting device property: %d", (int) status);
    return FALSE;
  }
  if (propertySize == 0) {
    GST_DEBUG ("property size was 0; device has no output channels");
    return FALSE;
  }

  return TRUE;
}

static inline gboolean
_audio_device_has_input (AudioDeviceID device_id)
{
  OSStatus status = noErr;
  UInt32 propertySize;

  AudioObjectPropertyAddress streamsAddress = {
    kAudioDevicePropertyStreams,
    kAudioDevicePropertyScopeInput,
    kAudioObjectPropertyElementMain
  };

  status = AudioObjectGetPropertyDataSize (device_id,
      &streamsAddress, 0, NULL, &propertySize);

  if (status != noErr) {
    GST_WARNING ("failed getting device property: %d", (int) status);
    return FALSE;
  }
  if (propertySize == 0) {
    GST_DEBUG ("property size was 0; device has no input channels");
    return FALSE;
  }

  return TRUE;
}

static OSStatus
_audio_devices_changed_cb (AudioObjectID inObjectID, UInt32 inNumberAddresses,
    const AudioObjectPropertyAddress * inAddresses,
    void *__nullable inClientData)
{
  GstCoreAudioDeviceProvider *self =
      GST_COREAUDIO_DEVICE_PROVIDER (inClientData);

  GST_DEBUG ("Audio devices changed");
  gst_coreaudio_device_provider_update_devices (self);

  return noErr;
}

static AudioObjectPropertyAddress
_get_devices_list_address ()
{
  AudioObjectPropertyAddress address = {
    .mSelector = kAudioHardwarePropertyDevices,
    .mScope = kAudioObjectPropertyScopeGlobal,
    .mElement = kAudioObjectPropertyElementMain
  };

  return address;
}

static gboolean
_start_audio_device_watcher (GstCoreAudioDeviceProvider * self)
{
  AudioObjectPropertyAddress propertyAddress = _get_devices_list_address ();
  OSStatus result = AudioObjectAddPropertyListener (kAudioObjectSystemObject,
      &propertyAddress, _audio_devices_changed_cb, self);

  if (result != noErr) {
    GST_WARNING ("Failed to add device list change listener: %d", result);
    return FALSE;
  }

  GST_DEBUG ("Audio device watcher started");
  return TRUE;
}

static gboolean
_stop_audio_device_watcher (GstCoreAudioDeviceProvider * self)
{
  AudioObjectPropertyAddress propertyAddress = _get_devices_list_address ();
  OSStatus result = AudioObjectRemovePropertyListener (kAudioObjectSystemObject,
      &propertyAddress, _audio_devices_changed_cb, self);

  if (result != noErr) {
    GST_WARNING ("Failed to remove device list change listener: %d", result);
    return FALSE;
  }

  GST_DEBUG ("Audio device watcher stopped");
  return TRUE;
}

static GstCoreAudioDevice *
gst_coreaudio_device_provider_probe_device (GstCoreAudioDeviceProvider *
    provider, GstCoreAudioDeviceId * device_id, const gchar * device_name,
    UInt32 transport_type, GstCoreAudioDeviceMode mode)
{
  GstCoreAudioDevice *gst_device = NULL;
  GstCoreAudioCtx *ctx = NULL;
  gboolean is_default;

  is_default = _audio_device_is_default (device_id->id,
      mode == GST_COREAUDIO_DEVICE_MODE_SINK);
  ctx = gst_coreaudio_ctx_new (mode, device_id->uid);
  gst_device =
      gst_coreaudio_device_new (ctx, device_id->uid, device_name,
      transport_type, mode, is_default);
  gst_coreaudio_ctx_free (ctx);

  return gst_device;
}

static void
gst_coreaudio_device_provider_probe_internal (GstCoreAudioDeviceProvider * self,
    GPtrArray * our_devices, GList ** gst_devices)
{
  for (int i = 0; i < our_devices->len; i++) {
    UInt32 transport_type;
    char *device_name;
    GstCoreAudioDevice *gst_device;
    GstCoreAudioDeviceId *our_device = g_ptr_array_index (our_devices, i);

    device_name = audiodevice_get_prop_str (our_device->id,
        kAudioObjectPropertyName);
    if (!device_name)
      continue;

    transport_type = audiodevice_get_prop_uint32 (our_device->id,
        kAudioDevicePropertyTransportType);

    if (transport_type == UINT_MAX)
      transport_type = kAudioDeviceTransportTypeUnknown;

    if (_audio_device_has_input (our_device->id)) {
      gst_device =
          gst_coreaudio_device_provider_probe_device (self, our_device,
          device_name, transport_type, GST_COREAUDIO_DEVICE_MODE_SRC);
      if (gst_device) {
        GST_DEBUG ("Input Device ID: %u, Name: %s, Transport Type: %"
            GST_FOURCC_FORMAT, (unsigned) our_device->id, device_name,
            GST_FOURCC_ARGS (GUINT32_FROM_BE (transport_type)));
        *gst_devices = g_list_prepend (*gst_devices, gst_device);
      }
    }

    if (_audio_device_has_output (our_device->id)) {
      gst_device =
          gst_coreaudio_device_provider_probe_device (self, our_device,
          device_name, transport_type, GST_COREAUDIO_DEVICE_MODE_SINK);
      if (gst_device) {
        GST_DEBUG ("Output Device ID: %u, Name: %s, Transport Type: %"
            GST_FOURCC_FORMAT, (unsigned) our_device->id, device_name,
            GST_FOURCC_ARGS (GUINT32_FROM_BE (transport_type)));
        *gst_devices = g_list_prepend (*gst_devices, gst_device);
      }
    }

    g_free (device_name);
  }
}

static GList *
gst_coreaudio_device_provider_probe (GstDeviceProvider * provider)
{
  GstCoreAudioDeviceProvider *self = GST_COREAUDIO_DEVICE_PROVIDER (provider);
  GList *gst_devices = NULL;
  GPtrArray *our_devices = NULL;

  our_devices = get_all_coreaudio_devices ();

  if (our_devices == NULL || our_devices->len == 0) {
    GST_WARNING ("No audio devices found");
    goto done;
  }

  GST_INFO ("Found %d audio device(s)", our_devices->len);

  gst_coreaudio_device_provider_probe_internal (self, our_devices,
      &gst_devices);

done:
  if (gst_devices)
    g_ptr_array_free (our_devices, TRUE);

  return gst_devices;
}

static gboolean
gst_coreaudio_device_provider_start (GstDeviceProvider * provider)
{
  GstCoreAudioDeviceProvider *self = GST_COREAUDIO_DEVICE_PROVIDER (provider);
  GList *devices = NULL;
  GList *iter;

  devices = gst_coreaudio_device_provider_probe (provider);

  for (iter = devices; iter; iter = iter->next) {
    gst_device_provider_device_add (provider, GST_DEVICE (iter->data));
  }

  /* Device references were floating, so were transferred in
   * gst_device_provider_device_add() */
  g_list_free (devices);

  return _start_audio_device_watcher (self);
}

static void
gst_coreaudio_device_provider_stop (GstDeviceProvider * provider)
{
  GstCoreAudioDeviceProvider *self = GST_COREAUDIO_DEVICE_PROVIDER (provider);
  _stop_audio_device_watcher (self);
}

static gboolean
gst_coreaudio_device_is_in_list (GList * list, GstDevice * gst_device)
{
  GstCoreAudioDevice *osx_device = GST_COREAUDIO_DEVICE (gst_device);
  gchar *name = gst_device_get_display_name (gst_device);
  GList *iter;
  gboolean found = FALSE;

  for (iter = list; iter; iter = g_list_next (iter)) {
    GstCoreAudioDevice *other_osx = GST_COREAUDIO_DEVICE (iter->data);
    gchar *other_name = gst_device_get_display_name (GST_DEVICE (iter->data));

    /* Only checking ID + class for now.
     * Should be enough to pick up changes when an existing output device
     * adds an input or vice versa */
    if (g_ascii_strcasecmp (name, other_name) == 0
        && g_ascii_strcasecmp (osx_device->uid, other_osx->uid) == 0) {
      found = TRUE;
    }

    g_free (other_name);

    if (found)
      break;
  }

  g_free (name);
  return found;
}

static void
gst_coreaudio_device_provider_update_devices (GstCoreAudioDeviceProvider * self)
{
  GstDeviceProvider *provider = GST_DEVICE_PROVIDER_CAST (self);
  GList *prev_devices = NULL;
  GList *new_devices = NULL;
  GList *to_add = NULL;
  GList *to_remove = NULL;
  GList *iter;

  GST_OBJECT_LOCK (self);
  prev_devices = g_list_copy_deep (provider->devices,
      (GCopyFunc) gst_object_ref, NULL);
  GST_OBJECT_UNLOCK (self);

  new_devices = gst_coreaudio_device_provider_probe (provider);
  if (!new_devices)
    goto done;

  /* Ownership of GstDevice for gst_device_provider_device_add()
   * and gst_device_provider_device_remove() is a bit complicated.
   * Remove floating reference here for things to be clear */
  for (iter = new_devices; iter; iter = g_list_next (iter))
    gst_object_ref_sink (iter->data);

  /* Check added devices */
  for (iter = new_devices; iter; iter = g_list_next (iter)) {
    if (!gst_coreaudio_device_is_in_list
        (prev_devices, GST_DEVICE (iter->data))) {
      to_add = g_list_prepend (to_add, gst_object_ref (iter->data));
    }
  }

  /* Check removed devices */
  for (iter = prev_devices; iter; iter = g_list_next (iter)) {
    if (!gst_coreaudio_device_is_in_list (new_devices, GST_DEVICE (iter->data))) {
      to_remove = g_list_prepend (to_remove, gst_object_ref (iter->data));
    }
  }

  for (iter = to_remove; iter; iter = g_list_next (iter))
    gst_device_provider_device_remove (provider, GST_DEVICE (iter->data));

  for (iter = to_add; iter; iter = g_list_next (iter))
    gst_device_provider_device_add (provider, GST_DEVICE (iter->data));

done:
  if (prev_devices)
    g_list_free_full (prev_devices, (GDestroyNotify) gst_object_unref);

  if (to_add)
    g_list_free_full (to_add, (GDestroyNotify) gst_object_unref);

  if (to_remove)
    g_list_free_full (to_remove, (GDestroyNotify) gst_object_unref);

  if (new_devices)
    g_list_free (new_devices);
}

enum
{
  PROP_UID = 1,
};

#define gst_coreaudio_device_parent_class parent_class
G_DEFINE_TYPE (GstCoreAudioDevice, gst_coreaudio_device, GST_TYPE_DEVICE);

static void gst_coreaudio_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_coreaudio_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_coreaudio_device_finalize (GObject * object);
static GstElement *gst_coreaudio_device_create_element (GstDevice * device,
    const gchar * name);

static void
gst_coreaudio_device_class_init (GstCoreAudioDeviceClass * klass)
{
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  dev_class->create_element = gst_coreaudio_device_create_element;

  object_class->get_property = gst_coreaudio_device_get_property;
  object_class->set_property = gst_coreaudio_device_set_property;
  object_class->finalize = gst_coreaudio_device_finalize;

  g_object_class_install_property (object_class, PROP_UID,
      g_param_spec_string ("uid", "Unique ID",
          "Unique ID of audio device", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_coreaudio_device_init (GstCoreAudioDevice * device)
{
}

static void
gst_coreaudio_device_finalize (GObject * object)
{
  GstCoreAudioDevice *our_device = GST_COREAUDIO_DEVICE (object);
  g_clear_pointer (&our_device->uid, g_free);
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstElement *
gst_coreaudio_device_create_element (GstDevice * device, const gchar * name)
{
  GstCoreAudioDevice *our_device = GST_COREAUDIO_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make (our_device->element, name);
  g_object_set (elem, "device", our_device->uid, NULL);

  return elem;
}

static GstCoreAudioDevice *
gst_coreaudio_device_new (GstCoreAudioCtx * ctx, const gchar * device_uid,
    const gchar * device_name, UInt32 transport_type,
    GstCoreAudioDeviceMode mode, gboolean is_default)
{
  const gchar *element_name = NULL;
  const gchar *klass = NULL;
  GstCoreAudioDevice *gst_device;
  GstCaps *template_caps, *caps;
  GstStructure *props = gst_structure_new_empty ("properties");
  char *transport_name = g_strdup_printf ("%" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (GUINT32_FROM_BE (transport_type)));

  g_return_val_if_fail (device_name, NULL);

  gst_structure_set (props,
      "is-default", G_TYPE_BOOLEAN, is_default,
      "transport", G_TYPE_STRING, transport_name, NULL);

  g_free (transport_name);

  switch (mode) {
    case GST_COREAUDIO_DEVICE_MODE_SRC:
      element_name = "coreaudiosrc";
      klass = "Audio/Source";

      template_caps = gst_static_pad_template_get_caps (&src_factory);
      caps = asbd_to_caps (ctx->hw_format, ctx->hw_layout, FALSE, FALSE);
      gst_caps_unref (template_caps);

      break;
    case GST_COREAUDIO_DEVICE_MODE_SINK:
      element_name = "coreaudiosink";
      klass = "Audio/Sink";

      template_caps = gst_static_pad_template_get_caps (&sink_factory);
      caps = asbd_to_caps (ctx->hw_format, ctx->hw_layout, TRUE, TRUE);
      gst_caps_unref (template_caps);

      break;
    default:
      g_assert_not_reached ();
      break;
  }

  gst_device = g_object_new (GST_TYPE_COREAUDIO_DEVICE, "uid", device_uid,
      "display-name", device_name, "caps", caps, "properties", props,
      "device-class", klass, NULL);
  gst_structure_free (props);
  gst_caps_unref (caps);

  gst_device->element = element_name;

  return gst_device;
}

static void
gst_coreaudio_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCoreAudioDevice *device;

  device = GST_COREAUDIO_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_UID:
      g_value_set_string (value, device->uid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_coreaudio_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCoreAudioDevice *device;

  device = GST_COREAUDIO_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_UID:
      device->uid = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
