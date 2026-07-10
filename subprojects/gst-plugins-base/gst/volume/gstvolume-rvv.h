/* GStreamer
 * Copyright (C) 2026 Felix-Gong <gongxiaofei24@iscas.ac.cn>
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

#ifndef GST_VOLUME_RVV_H
#define GST_VOLUME_RVV_H

#include <glib.h>
#include <gst/gstcpuid.h>

G_BEGIN_DECLS

void volume_orc_process_int16_rvv (gint16 *data, gint32 vol, gint n);
void volume_orc_process_int16_clamp_rvv (gint16 *data, gint32 vol, gint n);
void volume_orc_process_int32_rvv (gint32 *data, gint32 vol, gint n);
void volume_orc_process_int32_clamp_rvv (gint32 *data, gint32 vol, gint n);
void volume_orc_scalarmultiply_f32_ns_rvv (gfloat *data, gfloat vol, gint n);
void volume_orc_scalarmultiply_f64_ns_rvv (gdouble *data, gdouble vol, gint n);

G_END_DECLS

#endif /* GST_VOLUME_RVV_H */
