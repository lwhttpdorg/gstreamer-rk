/* gstgoom.c: implementation of goom drawing element
 * Copyright (C) <2001> Richard Boulton <richard@tartarus.org>
 *           (C) <2006> Wim Taymans <wim at fluendo dot com>
 * Copyright (C) <2015> Luis de Bethencourt <luis@debethencourt.com>
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

#ifndef __GST_GOOM_H__
#define __GST_GOOM_H__

#include <gst/pbutils/gstaudiovisualizer.h>

#include "goom_core.h"

G_BEGIN_DECLS

#define GOOM2K1_SAMPLES 512

#define GST_TYPE_GOOM2K1 (gst_goom2k1_get_type())
G_DECLARE_FINAL_TYPE (GstGoom2k1, gst_goom2k1, GST, GOOM2K1, GstAudioVisualizer)

struct _GstGoom2k1
{
  GstAudioVisualizer parent;

  /* input tracking */
  gint channels;

  /* video state */
  gint width;
  gint height;

  /* goom stuff */
  GoomData goomdata;
};

GST_ELEMENT_REGISTER_DECLARE (goom2k1);

G_END_DECLS

#endif /* __GST_GOOM_H__ */

