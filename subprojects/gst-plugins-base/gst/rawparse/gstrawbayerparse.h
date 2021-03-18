/* GStreamer
 * Copyright (C) <2016> Carlos Rafael Giani <dv at pseudoterminal dot org>
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

#ifndef __GST_RAW_BAYER_PARSE_H__
#define __GST_RAW_BAYER_PARSE_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstrawbaseparse.h"

G_BEGIN_DECLS

#define GST_TYPE_RAW_BAYER_PARSE \
  (gst_raw_bayer_parse_get_type())
#define GST_RAW_BAYER_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_RAW_BAYER_PARSE, GstRawBayerParse))
#define GST_RAW_BAYER_PARSE_CAST(obj) \
  ((GstRawBayerParse *)(obj))
#define GST_RAW_BAYER_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_RAW_BAYER_PARSE, GstRawBayerParseClass))
#define GST_IS_RAW_BAYER_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_RAW_BAYER_PARSE))
#define GST_IS_RAW_BAYER_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_RAW_BAYER_PARSE))

typedef struct _GstRawBayerParseConfig GstRawBayerParseConfig;
typedef struct _GstRawBayerParse GstRawBayerParse;
typedef struct _GstRawBayerParseClass GstRawBayerParseClass;

typedef enum _GstRawBayerParseFormat GstRawBayerParseFormat;
enum _GstRawBayerParseFormat {
  GST_RAW_BAYER_PARSE_FORMAT_BGGR = 0,
  GST_RAW_BAYER_PARSE_FORMAT_GBRG,
  GST_RAW_BAYER_PARSE_FORMAT_GRBG,
  GST_RAW_BAYER_PARSE_FORMAT_RGGB
};
GType gst_raw_bayer_parse_format_type (void);
#define GST_RAW_BAYER_PARSE_FORMAT_TYPE (gst_raw_bayer_parse_format_type ())

/* Contains information about the video frame format. */
struct _GstRawBayerParseConfig
{
  /* If TRUE, then this configuration is ready to use */
  gboolean ready;

  gint width, height;
  GstRawBayerParseFormat format;
  gint pixel_aspect_ratio_n, pixel_aspect_ratio_d;
  gint framerate_n, framerate_d;

  /* Distance between the start of each frame, in bytes. If this value
   * is larger than the actual size of a frame, then the extra bytes
   * are skipped. For example, with frames that have 115200 bytes, a
   * frame_size value of 120000 means that 4800 trailing bytes are
   * skipped after the 115200 frame bytes. This is useful to skip
   * metadata in between frames. */
  guint frame_size;
};

struct _GstRawBayerParse
{
  GstRawBaseParse parent;

  /*< private > */

  /* Configuration controlled by the object properties. Its ready value
   * is set to TRUE from the start, so it can be used right away.
   */
  GstRawBayerParseConfig properties_config;
  /* Configuration controlled by the sink caps. Its ready value is
   * initially set to FALSE until valid sink caps come in. It is set to
   * FALSE again when the stream-start event is observed.
   */
  GstRawBayerParseConfig sink_caps_config;
  /* Currently active configuration. Points either to properties_config
   * or to sink_caps_config. This is never NULL. */
  GstRawBayerParseConfig *current_config;
};

struct _GstRawBayerParseClass
{
  GstRawBaseParseClass parent_class;
};

GType gst_raw_bayer_parse_get_type (void);

G_END_DECLS

#endif
