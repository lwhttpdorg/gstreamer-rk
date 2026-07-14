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

#pragma once

#include "gstv4l2decoder.h"
#include <gst/codecs/gstcodecdecoder.h>

G_BEGIN_DECLS

void
gst_v4l2_codec_h264_debug_dump_sequence (GstCodecDecoder *dec,
                                         const struct v4l2_ctrl_h264_sps *sps);

void
gst_v4l2_codec_h264_debug_dump_pps (GstCodecDecoder *dec,
                                    const struct v4l2_ctrl_h264_pps *pps);

void
gst_v4l2_codec_h264_debug_dump_scaling_matrix (GstCodecDecoder *dec,
                                               const struct v4l2_ctrl_h264_scaling_matrix *scaling);

void
gst_v4l2_codec_h264_debug_dump_decode_params(GstCodecDecoder *self,
                                             const struct v4l2_ctrl_h264_decode_params *p);

void
gst_v4l2_codec_h264_debug_dump_pred_weights(GstCodecDecoder *self,
                                            const struct v4l2_ctrl_h264_pred_weights *w);

void
gst_v4l2_codec_h264_debug_dump_slice_params(GstCodecDecoder *self,
                                            const struct v4l2_ctrl_h264_slice_params *p);

G_END_DECLS
