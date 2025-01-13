/* GStreamer Synchronous KLV output
 *
 * Copyright (C) <2024> Glyn Davies
 *  @author Glyn Davies <glyn@solet.io>
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
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef __BASETSMUX_KLV_H__
#define __BASETSMUX_KLV_H__

#include "gstbasetsmux.h"


GstBuffer *gst_base_ts_mux_prepare_klv (GstBuffer * buf, GstBaseTsMuxPad * pad,
    GstBaseTsMux * mux);


void
gst_base_ts_mux_free_klv (gpointer prepare_data);

#endif /* __BASETSMUX_KLV_H__ */
