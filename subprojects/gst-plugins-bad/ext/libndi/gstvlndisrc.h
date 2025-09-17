/*
 * GStreamer VideoLAN NDI video source.
 *
 * Copyright (c) 2025 Michael Gruner <michael.gruner@ridgerun.com>
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

#ifndef __GST_VL_NDI_SRC_H__
#define __GST_VL_NDI_SRC_H__

#include <gst/gst.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

#define GST_VL_TYPE_NDI_SRC gst_vl_ndi_src_get_type ()
G_DECLARE_FINAL_TYPE (GstVlNdiSrc, gst_vl_ndi_src, GST_VL, NDI_SRC, GstPushSrc)
GST_ELEMENT_REGISTER_DECLARE (vl_ndi_src)

G_END_DECLS

#endif //__GST_VL_NDI_SRC_H__
