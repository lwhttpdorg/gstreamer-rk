/* GStreamer
 * Copyright © 2025 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * Author: Sreerenj Balachandran <sreerenj@amazon.com>
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

#ifndef __GST_CODEC_DECODER_H__
#define __GST_CODEC_DECODER_H__

#include <gst/gst.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/codecs/codecs-prelude.h>

G_BEGIN_DECLS

typedef struct _GstCodecDecoder GstCodecDecoder;
typedef struct _GstCodecDecoderClass GstCodecDecoderClass;

#define GST_TYPE_CODEC_DECODER            (gst_codec_decoder_get_type())
#define GST_CODEC_DECODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CODEC_DECODER,GstCodecDecoder))
#define GST_CODEC_DECODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CODEC_DECODER,GstCodecDecoderClass))
#define GST_CODEC_DECODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_CODEC_DECODER,GstCodecDecoderClass))
#define GST_IS_CODEC_DECODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CODEC_DECODER))
#define GST_IS_CODEC_DECODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CODEC_DECODER))
#define GST_CODEC_DECODER_CAST(obj)       ((GstCodecDecoder*)obj)

/* GstCodecDebugFlags:
 *
 * Flags to control debug dumping behavior in codec decoders:
 *
 * @GST_CODEC_DEBUG_NONE: No debug dumping
 * @GST_CODEC_DEBUG_SEQUENCE_HDRS: Dump headers sent once per sequence (e.g: SPS)
 * @GST_CODEC_DEBUG_PICTURE_HDRS: Dump headers sent per picture (e.g: PPS, Slice).
 * @GST_CODEC_DEBUG_RAW_FRAME: Dump raw decoded frame content for inspection.
 */
typedef enum {
  GST_CODEC_DEBUG_NONE              = 0,
  GST_CODEC_DEBUG_SEQUENCE_HDRS    = (1 << 0),
  GST_CODEC_DEBUG_PICTURE_HDRS     = (1 << 1),
  GST_CODEC_DEBUG_RAW_FRAME        = (1 << 2),
} GstCodecDebugFlags;
GType gst_codec_debug_flags_get_type(void);

struct _GstCodecDecoder {
  GstVideoDecoder parent;
  GstCodecDebugFlags debug_mode;
};

struct _GstCodecDecoderClass {
  GstVideoDecoderClass parent_class;
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstCodecDecoder, gst_object_unref)

#define GST_TYPE_CODEC_DECODER            (gst_codec_decoder_get_type())
GST_CODECS_API
GType gst_codec_decoder_get_type (void);

G_END_DECLS

#endif /* __GST_CODEC_DECODER_H__ */
