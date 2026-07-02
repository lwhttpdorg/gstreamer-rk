/*
 * Copyright 2026 Edward Hervey <edward@centricular.com>
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

#ifndef __GST_BASE_TS_MUX_AV1_H__
#define __GST_BASE_TS_MUX_AV1_H__

#include <gst/gst.h>
#include "gstbasetsmux.h"

G_BEGIN_DECLS

GstBuffer * gst_base_ts_mux_av1_prepare (GstBuffer * buf,
    GstBaseTsMuxPad * pad, GstBaseTsMux * mux);

GstMpegtsDescriptor * gst_av1_create_video_descriptor (GstCaps * caps);

G_END_DECLS

#endif /* __GST_BASE_TS_MUX_AV1_H__ */
