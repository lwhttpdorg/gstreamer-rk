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

#include "vlndi.h"
#include "glib.h"
#include "ndi/packet.h"

ndi_packet_t *
vl_ndi_packet_copy_deep (const ndi_packet_t * src)
{
  ndi_packet_t *dst = NULL;

  dst = g_memdup2 (src, sizeof (ndi_packet_t));

  switch (src->type) {
    case NDI_DATA_VIDEO:
      dst->packet = vl_ndi_packet_video_copy_deep (src->packet);
      break;
    case NDI_DATA_AUDIO:
      dst->packet = vl_ndi_packet_audio_copy_deep (src->packet);
      break;
    case NDI_DATA_METADATA:
      dst->packet = vl_ndi_packet_metadata_copy_deep (src->packet);
      break;
    default:
      g_warning ("Malformed NDI packet received. This should not happen");
      break;
  }

  return dst;
}

void
vl_ndi_packet_free (ndi_packet_t * pkt)
{
  if (!pkt) {
    return;
  }

  switch (pkt->type) {
    case NDI_DATA_VIDEO:
      vl_ndi_packet_video_free (pkt->packet);
      break;
    case NDI_DATA_AUDIO:
      vl_ndi_packet_audio_free (pkt->packet);
      break;
    case NDI_DATA_METADATA:
      vl_ndi_packet_metadata_free (pkt->packet);
      break;
    default:
      g_warning ("Malformed NDI packet received. This should not happen");
      break;
  }

  g_free (pkt);
}

ndi_packet_video_t *
vl_ndi_packet_video_copy_deep (const ndi_packet_video_t * pkt)
{
  ndi_packet_video_t *dst = NULL;

  dst = g_memdup2 (pkt, sizeof (ndi_packet_video_t));
  dst->data = g_memdup2 (pkt->data, pkt->size);

  return dst;
}

void
vl_ndi_packet_video_free (ndi_packet_video_t * pkt)
{
  g_free (pkt->data);
  g_free (pkt);
}

ndi_packet_audio_t *
vl_ndi_packet_audio_copy_deep (const ndi_packet_audio_t * pkt)
{
  ndi_packet_audio_t *dst = NULL;

  dst = g_memdup2 (pkt, sizeof (ndi_packet_audio_t));

  // TODO: deep copy
  return dst;
}

void
vl_ndi_packet_audio_free (ndi_packet_audio_t * pkt)
{
  g_free (pkt);
}

ndi_packet_metadata_t *
vl_ndi_packet_metadata_copy_deep (const ndi_packet_metadata_t * pkt)
{
  ndi_packet_metadata_t *dst = NULL;

  dst = g_memdup2 (pkt, sizeof (ndi_packet_metadata_t));

  // TODO: deep copy
  return dst;
}

void
vl_ndi_packet_metadata_free (ndi_packet_metadata_t * pkt)
{
  g_free (pkt);
}
