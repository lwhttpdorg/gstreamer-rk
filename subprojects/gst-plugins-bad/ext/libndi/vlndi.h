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

#ifndef __VL_NDI_H__
#define __VL_NDI_H__

#include <glib.h>
#include <ndi/packet.h>

G_BEGIN_DECLS

ndi_packet_t* vl_ndi_packet_copy_deep (const ndi_packet_t *pkt);
void vl_ndi_packet_free(ndi_packet_t *pkt);

ndi_packet_video_t *vl_ndi_packet_video_copy_deep (const ndi_packet_video_t *pkt);
void vl_ndi_packet_video_free(ndi_packet_video_t *pkt);

ndi_packet_audio_t *vl_ndi_packet_audio_copy_deep (const ndi_packet_audio_t *pkt);
void vl_ndi_packet_audio_free(ndi_packet_audio_t *pkt);

ndi_packet_metadata_t *vl_ndi_packet_metadata_copy_deep (const ndi_packet_metadata_t *pkt);
void vl_ndi_packet_metadata_free(ndi_packet_metadata_t *pkt);

G_END_DECLS

#endif //__GST_NDI_SRC_H__
