/* GStreamer ReplayGain limiter
 *
 * Copyright (C) 2007 Rene Stadler <mail@renestadler.de>
 *
 * gstrglimiter.h: Element to apply signal compression to raw audio data
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __GST_RG_LIMITER_H__
#define __GST_RG_LIMITER_H__

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#define GST_TYPE_RG_LIMITER (gst_rg_limiter_get_type())
G_DECLARE_FINAL_TYPE (GstRgLimiter, gst_rg_limiter, GST, RG_LIMITER,
    GstBaseTransform)

/**
 * GstRgLimiter:
 *
 * Opaque data structure.
 */
struct _GstRgLimiter
{
  GstBaseTransform element;

  /*< private >*/

  gboolean enabled;
};

GST_ELEMENT_REGISTER_DECLARE (rglimiter);

#endif /* __GST_RG_LIMITER_H__ */
