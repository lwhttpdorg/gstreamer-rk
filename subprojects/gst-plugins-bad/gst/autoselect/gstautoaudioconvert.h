/* GStreamer
 *
 *  Copyright 2024 NXP
 *   @author: Elliot Chen <elliot.chen@nxp.com>
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

#ifndef __GST_AUTO_AUDIO_CONVERT_H__
#define __GST_AUTO_AUDIO_CONVERT_H__

#include <gst/gst.h>
#include <gst/gstbin.h>
#include "gstautoselect.h"

G_BEGIN_DECLS
#define GST_TYPE_AUTO_AUDIO_CONVERT 	        	(gst_auto_audio_convert_get_type())
#define GST_AUTO_AUDIO_CONVERT(obj)	            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_AUTO_AUDIO_CONVERT,GstAutoAudioConvert))
#define GST_AUTO_AUDIO_CONVERT_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_AUTO_AUDIO_CONVERT,GstAutoAudioConvertClass))
#define GST_IS_AUTO_AUDIO_CONVERT(obj)            (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_AUTO_AUDIO_CONVERT))
#define GST_IS_AUTO_AUDIO_CONVERT_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_AUTO_AUDIO_CONVERT))
typedef struct _GstAutoAudioConvert GstAutoAudioConvert;
typedef struct _GstAutoAudioConvertClass GstAutoAudioConvertClass;

struct _GstAutoAudioConvert
{
  GstAutoSelect element;
  gchar *perferred_factory_order;
};

struct _GstAutoAudioConvertClass
{
  GstAutoSelectClass parent_class;
};

GType gst_auto_audio_convert_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (autoaudioconvert);

G_END_DECLS
#endif /* __GST_AUTO_AUDIO_CONVERT_H__ */
