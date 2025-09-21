/* GStreamer PCM to A-Law conversion
 * Copyright (C) 2000 by Abramo Bagnara <abramo@alsa-project.org>
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


#ifndef __GST_ALAW_ENCODE_H__
#define __GST_ALAW_ENCODE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_TYPE_ALAW_ENC (gst_alaw_enc_get_type())
G_DECLARE_FINAL_TYPE (GstALawEnc, gst_alaw_enc, GST, ALAW_ENC, GstAudioEncoder)

struct _GstALawEnc {
  GstAudioEncoder encoder;

  gint channels;
  gint rate;
};

GST_ELEMENT_REGISTER_DECLARE (alawenc);

G_END_DECLS

#endif /* __GST_ALAW_ENCODE_H__ */
