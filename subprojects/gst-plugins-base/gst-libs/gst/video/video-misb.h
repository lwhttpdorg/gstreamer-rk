/* GStreamer
 * Copyright (C) <2021> Fluendo S.A. <contact@fluendo.com>
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

#ifndef __GST_VIDEO_MISB_H__
#define __GST_VIDEO_MISB_H__

#include <gst/gst.h>
#include <gst/video/video.h>

#include "video-sei.h"

G_BEGIN_DECLS
/**
 * GstVideoMISBPrecisionTimestampUnit:
 * @GST_VIDEO_MISB_PTS_UNIT_MICROSECONDS: Microseconds
 * @GST_VIDEO_MISB_PTS_UNIT_NANOSECONDS: Nanoseconds
 *
 * Since: 1.28
 */
    typedef enum
{
  GST_VIDEO_MISB_PTS_UNIT_MICROSECONDS = 0,
  GST_VIDEO_MISB_PTS_UNIT_NANOSECONDS = 1,
} GstVideoMISBPrecisionTimestampUnit;

/**
 * GstVideoMISBPrecisionTimestampMeta:
 * @meta: parent #GstMeta
 * @status: ST 0603 Time Status
 * @value: absolute timestamp in @unit
 * @unit: GstVideoMISBPrecisionTimestampUnit
 *
 * Metadata for MISB Precision Timestamp messages
 *
 * Since: 1.28
 */
typedef struct
{
  GstMeta meta;

  guint8 status;
  guint64 value;
  GstVideoMISBPrecisionTimestampUnit unit;
} GstVideoMISBPrecisionTimestampMeta;

GST_VIDEO_API GType gst_video_misb_precision_timestamp_meta_api_get_type (void);
/**
 * GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_API_TYPE:
 *
 * Since: 1.28
 */
#define GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_API_TYPE (\
    gst_video_misb_precision_timestamp_meta_api_get_type())

GST_VIDEO_API
    const GstMetaInfo *gst_video_misb_precision_timestamp_meta_get_info (void);
/**
 * GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_INFO:
 *
 * Since: 1.28
 */
#define GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_INFO (\
    gst_video_misb_precision_timestamp_meta_get_info())

/**
 * gst_buffer_get_video_misb_precision_timestamp_meta:
 * @b: A #GstBuffer
 *
 * Gets the GstVideoMISBPrecisionTimestampMeta that might be present on @b.
 *
 * Returns: (nullable): The first #GstVideoMISBPrecisionTimestampMeta present on @b, or %NULL if
 * no #GstVideoMISBPrecisionTimestampMeta are present
 *
 * Since: 1.28
 */
#define gst_buffer_get_video_misb_precision_timestamp_meta(b) \
        ((GstVideoMISBPrecisionTimestampMeta*)gst_buffer_get_meta((b),GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_API_TYPE))

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
GST_VIDEO_API
    GstVideoMISBPrecisionTimestampMeta
    * gst_buffer_add_video_misb_precision_timestamp_meta (GstBuffer * buffer,
    guint8 status, guint64 value, GstVideoMISBPrecisionTimestampUnit unit);


  /**
  * gst_video_misb_precision_timestamp_get_value:
  * @meta: A #GstVideoMISBPrecisionTimestampMeta
  * @value: (out): The value of the timestamp in the unit specified by @unit
  * @unit: (out): The #GstVideoMISBPrecisionTimestampUnit of the timestamp
  *
  * Returns: %TRUE if the value was found, %FALSE otherwise
  * Since: 1.28
  */
GST_VIDEO_API
    gboolean
gst_video_misb_precision_timestamp_get_value (GstVideoMISBPrecisionTimestampMeta
    * meta, guint64 * value, GstVideoMISBPrecisionTimestampUnit * unit);

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
GST_VIDEO_API
    gboolean gst_video_misb_identifier_from_caps (const GstCaps * caps,
    GstVideoMISBPrecisionTimestampUnit unit, guint8 uuid[16]);

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
GST_VIDEO_API
    void gst_video_misb_precision_timestamp_build_payload (guint8 status,
    guint64 value, guint8 payload[12]);

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
GST_VIDEO_API
    gboolean gst_video_misb_precision_timestamp_payload_from_meta (const
    GstVideoMISBPrecisionTimestampMeta * meta, guint8 payload[12]);

G_END_DECLS
#endif /* __GST_VIDEO_MISB_H__ */
