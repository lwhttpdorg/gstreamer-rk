/* GStreamer
 * Copyright (C) 2016 Hyunjun Ko <zzoon@igalia.com>
 *
 * gstcoreaudiodeviceeprovider.h: OSX audio probing and monitoring
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


#ifndef __GST_COREAUDIO_DEIVCE_PROVIDER_H__
#define __GST_COREAUDIO_DEIVCE_PROVIDER_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

G_BEGIN_DECLS
typedef struct _GstCoreAudioDeviceProvider GstCoreAudioDeviceProvider;
typedef struct _GstCoreAudioDeviceProviderClass GstCoreAudioDeviceProviderClass;

#define GST_TYPE_COREAUDIO_DEVICE_PROVIDER                 (gst_coreaudio_device_provider_get_type())
#define GST_IS_COREAUDIO_DEVICE_PROVIDER(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_COREAUDIO_DEVICE_PROVIDER))
#define GST_IS_COREAUDIO_DEVICE_PROVIDER_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_COREAUDIO_DEVICE_PROVIDER))
#define GST_COREAUDIO_DEVICE_PROVIDER_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_COREAUDIO_DEVICE_PROVIDER, GstCoreAudioDeviceProviderClass))
#define GST_COREAUDIO_DEVICE_PROVIDER(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_COREAUDIO_DEVICE_PROVIDER, GstCoreAudioDeviceProvider))
#define GST_COREAUDIO_DEVICE_PROVIDER_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE_PROVIDER, GstCoreAudioDeviceProviderClass))
#define GST_COREAUDIO_DEVICE_PROVIDER_CAST(obj)            ((GstCoreAudioDeviceProvider *)(obj))

struct _GstCoreAudioDeviceProvider
{
  GstDeviceProvider parent;
};

struct _GstCoreAudioDeviceProviderClass
{
  GstDeviceProviderClass parent_class;
};

GType gst_coreaudio_device_provider_get_type (void);

typedef struct _GstCoreAudioDevice GstCoreAudioDevice;
typedef struct _GstCoreAudioDeviceClass GstCoreAudioDeviceClass;

#define GST_TYPE_COREAUDIO_DEVICE                 (gst_coreaudio_device_get_type())
#define GST_IS_COREAUDIO_DEVICE(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_COREAUDIO_DEVICE))
#define GST_IS_COREAUDIO_DEVICE_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_COREAUDIO_DEVICE))
#define GST_COREAUDIO_DEVICE_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_COREAUDIO_DEVICE, GstCoreAudioClass))
#define GST_COREAUDIO_DEVICE(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_COREAUDIO_DEVICE, GstCoreAudioDevice))
#define GST_COREAUDIO_DEVICE_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEVICE, GstCoreAudioDeviceClass))
#define GST_COREAUDIO_DEVICE_CAST(obj)            ((GstCoreAudioDevice *)(obj))

struct _GstCoreAudioDevice
{
  GstDevice parent;

  const gchar *element;
  char *uid;
};

struct _GstCoreAudioDeviceClass
{
  GstDeviceClass parent_class;
};

GType gst_coreaudio_device_get_type (void);

GST_DEVICE_PROVIDER_REGISTER_DECLARE(coreaudiodeviceprovider);

G_END_DECLS
#endif /* __GST_COREAUDIO_DEIVCE_PROVIDER_H__ */
