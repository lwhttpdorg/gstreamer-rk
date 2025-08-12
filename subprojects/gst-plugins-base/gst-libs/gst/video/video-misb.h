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

G_BEGIN_DECLS static const guint8 H264_MISP_MICROSECTIME[] = {
  0x4D, 0x49, 0x53, 0x50, 0x6D, 0x69, 0x63, 0x72,
  0x6F, 0x73, 0x65, 0x63, 0x74, 0x69, 0x6D, 0x65
};

static const guint8 H265_MISP_MICROSECONDS[] = {
  0xA8, 0x68, 0x7D, 0xD4, 0xD7, 0x59, 0x37, 0x58,
  0xA5, 0xCE, 0xF0, 0x33, 0x8B, 0x65, 0x45, 0xF1
};

static const guint8 H265_MISP_NANOSECONDS[] = {
  0xCF, 0x84, 0x82, 0x78, 0xEE, 0x23, 0x30, 0x6C,
  0x92, 0x65, 0xE8, 0xFE, 0xF2, 0x2F, 0xB8, 0xB8
};

/**
 * GstVideoMisbPrecisionTimestampUnit:
 * @GST_VIDEO_MISB_PTS_UNIT_MICROSECONDS: Microseconds
 * @GST_VIDEO_MISB_PTS_UNIT_NANOSECONDS: Nanoseconds
 *
 * Since: 1.28
 */
    typedef enum
{
  GST_VIDEO_MISB_PTS_UNIT_MICROSECONDS = 0,
  GST_VIDEO_MISB_PTS_UNIT_NANOSECONDS = 1,
} GstVideoMisbPrecisionTimestampUnit;

/**
 * GstVideoMisbPrecisionTimestampMeta:
 * @meta: parent #GstMeta
 * @status: ST 0603 Time Status
 * @value: absolute timestamp in @unit
 * @unit: GstVideoMisbPrecisionTimestampUnit
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
  GstVideoMisbPrecisionTimestampUnit unit;
} GstVideoMisbPrecisionTimestampMeta;

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
 * Gets the GstVideoMisbPrecisionTimestampMeta that might be present on @b.
 *
 * Returns: (nullable): The first #GstVideoMisbPrecisionTimestampMeta present on @b, or %NULL if
 * no #GstVideoMisbPrecisionTimestampMeta are present
 *
 * Since: 1.28
 */
#define gst_buffer_get_video_misb_precision_timestamp_meta(b) \
        ((GstVideoMisbPrecisionTimestampMeta*)gst_buffer_get_meta((b),GST_VIDEO_MISB_PRECISION_TIMESTAMP_META_API_TYPE))

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
    GstVideoMisbPrecisionTimestampMeta *
gst_buffer_add_video_misb_precision_timestamp_meta (GstBuffer * buffer,
    guint8 status, guint64 value, GstVideoMisbPrecisionTimestampUnit unit);


/**
 * gst_video_misb_precision_timestamp_get_value:
 * @meta: A #GstVideoMisbPrecisionTimestampMeta
 * @value: (out): The value of the timestamp in the unit specified by @unit
 * @unit: (out): The #GstVideoMisbPrecisionTimestampUnit of the timestamp
 *
 * Since: 1.28
 */
GST_VIDEO_API
    gboolean
gst_video_misb_precision_timestamp_get_value (GstVideoMisbPrecisionTimestampMeta
    * meta, guint64 * value, GstVideoMisbPrecisionTimestampUnit * unit);

/**
 * gst_video_misb_identifier_from_caps:
 * @caps: A #GstCaps
 * @unit: The #GstVideoMisbPrecisionTimestampUnit of the timestamp
 * @id16: (out): The 16-byte identifier
 *
 * Generates a 16-byte identifier for the MISB Precision Timestamp message
 * based on the codec and unit.
 *
 * Returns: %TRUE if the identifier was successfully generated, %FALSE otherwise
 * Since: 1.28
 */
GST_VIDEO_API
    gboolean gst_video_misb_identifier_from_caps (const GstCaps * caps,
    GstVideoMisbPrecisionTimestampUnit unit, guint8 id16[16]);

G_END_DECLS
#endif /* __GST_VIDEO_MISB_H__ */
