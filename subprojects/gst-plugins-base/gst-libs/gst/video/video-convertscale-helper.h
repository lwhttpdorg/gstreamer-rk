/* GStreamer
 * Copyright (C) 2026 Netflix Inc.
 *
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

#include <gst/gst.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/**
 * GstVideoConvertScaleFormatLoss:
 * @object: The object passed to gst_video_convertscale_fixate_caps()
 * @in_info: The input video format info
 * @out_info: The output video format info
 * @out_features: The output caps features
 * @ins: #GstStructure of the input caps
 * @outs: #GstStructure of the output caps
 *
 * Callback to guestimate conversion loss between the input and output formats.
 * A value of G_MAXINT means the conversion is not possible, a value of 0 means
 * it is passthrough.
 *
 * This function only needs to consider extra loss caused by custom fields,
 * "format" and "drm-format" are already taken into account.
 *
 * @outs is writable to allow custom fields to be fixated.
 * For example, this is used by glcolorconvert to fixate "target-texture" field.
 *
 * Returns: An integer representing the estimated loss. Lower values indicate less loss.
 * Since: 1.30
 */
typedef gint (*GstVideoConvertScaleFormatLoss)(GstObject *object,
                                               const GstVideoFormatInfo *in_info,
                                               const GstVideoFormatInfo *out_info,
                                               const GstCapsFeatures *out_features,
                                               const GstStructure *ins,
                                               GstStructure *outs);

GST_VIDEO_API
GstCaps *gst_video_convertscale_fixate_caps(GstObject *object,
                                            GstPadDirection direction,
                                            GstVideoOrientationMethod orientation,
                                            GstVideoConvertScaleFormatLoss format_loss,
                                            gboolean fixate_format,
                                            gboolean fixate_size,
                                            GstCaps *caps,
                                            GstCaps *othercaps);

G_END_DECLS
