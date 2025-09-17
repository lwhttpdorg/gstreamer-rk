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
#include "glib-object.h"
#include "gst/gstcaps.h"
#include "gst/gstvalue.h"
#include "gst/video/video-format.h"

void
vl_ndi_caps_video_clear (VlNdiCapsVideo * caps)
{
  g_return_if_fail (caps);

  caps->width = -1;
  caps->height = -1;
  caps->framerate_num = -1;
  caps->framerate_num = -1;
  caps->fourcc = 0;
}

gboolean
vl_ndi_caps_video_are_empty (VlNdiCapsVideo * caps)
{
  g_return_val_if_fail (caps, TRUE);

  return -1 == caps->width;
}

void
vl_ndi_caps_video_fill (VlNdiCapsVideo * caps, ndi_packet_video_t * pkt)
{
  g_return_if_fail (caps);
  g_return_if_fail (pkt);

  caps->width = pkt->width;
  caps->height = pkt->height;
  caps->framerate_num = pkt->fps_num;
  caps->framerate_den = pkt->fps_den;
  caps->fourcc = pkt->fourcc;
}

GstCaps *
vl_ndi_caps_video_to_gst (VlNdiCapsVideo * caps)
{
  GstCaps *out = NULL;
  const gchar *mimetype = NULL;
  const gchar *variant = NULL;
  const gchar *format = NULL;

  g_return_val_if_fail (caps, NULL);

  switch (caps->fourcc) {
    case GST_MAKE_FOURCC ('S', 'H', 'Q', '0'):
      mimetype = "video/x-speedhq";
      variant = "SHQ0";
      break;
    case GST_MAKE_FOURCC ('S', 'H', 'Q', '1'):
      mimetype = "video/x-speedhq";
      variant = "SHQ1";
      break;
    case GST_MAKE_FOURCC ('S', 'H', 'Q', '2'):
      mimetype = "video/x-speedhq";
      variant = "SHQ2";
      break;
    case GST_MAKE_FOURCC ('S', 'H', 'Q', '3'):
      mimetype = "video/x-speedhq";
      variant = "SHQ3";
      break;
    case GST_MAKE_FOURCC ('S', 'H', 'Q', '7'):
      mimetype = "video/x-speedhq";
      variant = "SHQ7";
      break;
    case GST_MAKE_FOURCC ('S', 'H', 'Q', '8'):
      mimetype = "video/x-speedhq";
      variant = "SHQ8";
      break;
    case GST_MAKE_FOURCC ('S', 'H', 'Q', '9'):
      mimetype = "video/x-speedhq";
      variant = "SHQ9";
      break;
    case GST_MAKE_FOURCC ('H', '2', '6', '4'):
      mimetype = "video/x-h264";
      break;
    case GST_MAKE_FOURCC ('H', 'E', 'V', 'C'):
      mimetype = "video/x-h265";
      break;
    default:
      mimetype = "video/x-raw";
      format =
          gst_video_format_to_string (gst_video_format_from_fourcc
          (caps->fourcc));
      break;
  }

  out = gst_caps_new_simple (mimetype,
      "width", G_TYPE_INT, caps->width,
      "height", G_TYPE_INT, caps->height,
      "framerate", GST_TYPE_FRACTION, caps->framerate_num, caps->framerate_den,
      NULL);

  if (variant) {
    gst_caps_set_simple (out, "variant", G_TYPE_STRING, variant, NULL);
  }

  if (format) {
    gst_caps_set_simple (out, "format", G_TYPE_STRING, format, NULL);
  }

  return out;
}

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
