/*
 * Copyright (C) 2013, Fluendo S.A.
 *   Author: Andoni Morales <amorales@fluendo.com>
 *
 * Copyright (C) 2014,2018 Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@collabora.com>
 *
 * Copyright (C) 2024, Ratchanan Srirattanamet <peathot@hotmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_AMC_SURFACE_TEXTURE_NDK_H__
#define __GST_AMC_SURFACE_TEXTURE_NDK_H__

#include "../gstamc-codec.h"
#include "../gstamcsurfacetexture.h"
#include "../jni/gstamcsurfacetexture-jni.h"

G_BEGIN_DECLS

#define GST_TYPE_AMC_SURFACE_TEXTURE_NDK gst_amc_surface_texture_ndk_get_type ()
G_DECLARE_FINAL_TYPE (GstAmcSurfaceTextureNDK, gst_amc_surface_texture_ndk, GST, AMC_SURFACE_TEXTURE_NDK, GstAmcSurfaceTextureJNI)

gboolean gst_amc_surface_texture_ndk_is_available (void);
GstAmcSurfaceTextureNDK * gst_amc_surface_texture_ndk_new (GError ** err);

G_END_DECLS

#endif
