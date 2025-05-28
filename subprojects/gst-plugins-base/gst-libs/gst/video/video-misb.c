/* GStreamer
 * Copyright (C) 2021 Fluendo S.A. <support@fluendo.com>
 *   Authors: Andoni Morales Alastruey <amorales@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include "video-misb.h"

/**
 * SECTION:gstvideomisbp
 * @title: GstVideo MISB Precision Timestamp
 * @short_description: Utilities for MISB Precision Timestamp
 *
 * A collection of objects and methods to assist with MISB Precision Timestamp
 * metadata in H.264 and H.265 streams.
 *
 * Since: 1.28
 */

GST_DEBUG_CATEGORY_STATIC (gst_video_misb_debug);
#define GST_CAT_DEFAULT gst_video_misb_debug

GType
gst_video_misb_precision_timestamp_meta_api_get_type (void)
{
  static GType type = 0;

  if (g_once_init_enter (&type)) {
    static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };
    GType _type =
        gst_meta_api_type_register ("GstVideoMISBPrecisionTimestampMetaAPI",
        tags);

    GST_DEBUG_CATEGORY_INIT (gst_video_misb_debug, "video-misb", 0,
        "MISB Precision Timestamp messages utilities");
    g_once_init_leave (&type, _type);
  }

  return type;
}

static gboolean
gst_video_misb_precision_timestamp_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstVideoMISBPrecisionTimestampMeta *m =
      (GstVideoMISBPrecisionTimestampMeta *) meta;

  m->status = 0;
  m->value = 0;
  m->unit = GST_VIDEO_MISB_PTS_UNIT_MICROSECONDS;

  return TRUE;
}

static void
gst_video_misb_precision_timestamp_meta_free (GstMeta * meta,
    GstBuffer * buffer)
{
  /* Nothing to free */
  (void) meta;
  (void) buffer;
}

static gboolean
gst_video_misb_precision_timestamp_meta_transform (GstBuffer * dest,
    GstMeta * meta, GstBuffer * buffer, GQuark type, gpointer data)
{
  if (GST_META_TRANSFORM_IS_COPY (type)) {
    const GstVideoMISBPrecisionTimestampMeta *s =
        (const GstVideoMISBPrecisionTimestampMeta *) meta;
    GstVideoMISBPrecisionTimestampMeta *d =
        gst_buffer_add_video_misb_precision_timestamp_meta (dest, s->status,
        s->value, s->unit);
    return d != NULL;
  }

  return FALSE;
}

const GstMetaInfo *
gst_video_misb_precision_timestamp_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & meta_info)) {
    const GstMetaInfo *mi =
        gst_meta_register (GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_API_TYPE,
        "GstVideoMISBPrecisionTimestampMeta",
        sizeof (GstVideoMISBPrecisionTimestampMeta),
        gst_video_misb_precision_timestamp_meta_init,
        gst_video_misb_precision_timestamp_meta_free,
        gst_video_misb_precision_timestamp_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & meta_info, (GstMetaInfo *) mi);
  }

  return meta_info;
}

/**
 * gst_buffer_add_video_misb_precision_timestamp_meta:
 * @buffer: A #GstBuffer
 * @status: ST 0603 Time Status
 * @value: 64-bit absolute time value in the specified @unit
 * @unit: a #GstVideoMISBPrecisionTimestampUnit
 *
 * Adds a #GstVideoMISBPrecisionTimestampMeta to @buffer.
 *
 * Returns: (transfer full): The #GstVideoMISBPrecisionTimestampMeta added to @buffer
 * Since: 1.28
 */
GstVideoMISBPrecisionTimestampMeta *
gst_buffer_add_video_misb_precision_timestamp_meta (GstBuffer * buffer,
    guint8 status, guint64 value, GstVideoMISBPrecisionTimestampUnit unit)
{
  GstVideoMISBPrecisionTimestampMeta *meta;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  meta = (GstVideoMISBPrecisionTimestampMeta *) gst_buffer_add_meta (buffer,
      GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_INFO, NULL);
  if (!meta)
    return NULL;

  meta->status = status;
  meta->value = value;
  meta->unit = unit;

  return meta;
}

  /**
  * gst_video_misb_precision_timestamp_get_value:
  * @meta: A #GstVideoMISBPrecisionTimestampMeta
  * @value: (out): The value of the timestamp in the unit specified by @unit
  * @unit: (out): The #GstVideoMISBPrecisionTimestampUnit of the timestamp
  *
  * Returns: %TRUE if the value was found, %FALSE otherwise
  * Since: 1.28
  */
gboolean
gst_video_misb_precision_timestamp_get_value (GstVideoMISBPrecisionTimestampMeta
    * meta, guint64 * value, GstVideoMISBPrecisionTimestampUnit * unit)
{
  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (unit != NULL, FALSE);

  *value = meta->value;
  *unit = (GstVideoMISBPrecisionTimestampUnit) meta->unit;
  return TRUE;
}

/**
 * gst_video_misb_identifier_from_caps:
 * @caps: A #GstCaps
 * @unit: The #GstVideoMISBPrecisionTimestampUnit of the timestamp
 * @uuid: (out): The 16-byte UUID
 *
 * Generates a 16-byte identifier for the MISB Precision Timestamp message
 * based on the codec and unit.
 *
 * Returns: %TRUE if the identifier was successfully generated, %FALSE otherwise
 * Since: 1.28
 */
gboolean
gst_video_misb_identifier_from_caps (const GstCaps * caps,
    GstVideoMISBPrecisionTimestampUnit unit, guint8 uuid[16])
{
  const GstStructure *s;

  g_return_val_if_fail (GST_IS_CAPS (caps), FALSE);
  g_return_val_if_fail (uuid != NULL, FALSE);

  if (gst_caps_is_empty (caps) || gst_caps_is_any (caps))
    return FALSE;

  s = gst_caps_get_structure (caps, 0);
  if (s == NULL)
    return FALSE;

  if (gst_structure_has_name (s, "video/x-h264")) {
    if (unit != GST_VIDEO_MISB_PTS_UNIT_MICROSECONDS)
      return FALSE;             /* H.264 only supports microseconds identifier */
    memcpy (uuid, H264_MISP_MICROSECTIME, 16);
    return TRUE;
  }

  if (gst_structure_has_name (s, "video/x-h265")) {
    if (unit == GST_VIDEO_MISB_PTS_UNIT_MICROSECONDS) {
      memcpy (uuid, H265_MISP_MICROSECONDS, 16);
      return TRUE;
    } else if (unit == GST_VIDEO_MISB_PTS_UNIT_NANOSECONDS) {
      memcpy (uuid, H265_MISP_NANOSECONDS, 16);
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * gst_video_misb_precision_timestamp_build_payload:
 * @status: ST 0603 Time Status byte
 * @value: 64-bit absolute time value in the specified @unit
 * @unit: a #GstVideoMISBPrecisionTimestampUnit
 * @payload: (out): 12-byte buffer to fill with the MISB payload
 *
 * Builds the 12-byte MISB Precision (or Nano Precision) Time Stamp payload
 * (status + interleaved 0xFF + big-endian timestamp bytes) as per ST 0604.
 *
 * Since: 1.28
 */
void
gst_video_misb_precision_timestamp_build_payload (guint8 status,
    guint64 value, guint8 payload[12])
{
  /* Status */
  payload[0] = status;

  /* 8-byte big-endian timestamp with 0xFF padding per ST 0604 Tables 2/3 */
  payload[1] = (value >> 56) & 0xFF;
  payload[2] = (value >> 48) & 0xFF;
  payload[3] = 0xFF;
  payload[4] = (value >> 40) & 0xFF;
  payload[5] = (value >> 32) & 0xFF;
  payload[6] = 0xFF;
  payload[7] = (value >> 24) & 0xFF;
  payload[8] = (value >> 16) & 0xFF;
  payload[9] = 0xFF;
  payload[10] = (value >> 8) & 0xFF;
  payload[11] = value & 0xFF;
}

/**
 * gst_video_misb_precision_timestamp_payload_from_meta:
 * @meta: a #GstVideoMISBPrecisionTimestampMeta
 * @payload: (out): 12-byte buffer to fill with the MISB payload
 *
 * Convenience wrapper around gst_video_misb_precision_timestamp_build_payload()
 * using the values from @meta.
 *
 * Since: 1.28
 */
gboolean
gst_video_misb_precision_timestamp_payload_from_meta (const
    GstVideoMISBPrecisionTimestampMeta * meta, guint8 payload[12])
{
  g_return_val_if_fail (meta != NULL, FALSE);
  g_return_val_if_fail (payload != NULL, FALSE);

  gst_video_misb_precision_timestamp_build_payload (meta->status, meta->value,
      payload);
  return TRUE;
}
