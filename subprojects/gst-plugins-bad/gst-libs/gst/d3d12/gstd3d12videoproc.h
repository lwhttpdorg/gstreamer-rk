/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
#include <gst/d3d12/gstd3d12_fwd.h>

G_BEGIN_DECLS

#define GST_TYPE_D3D12_VIDEO_PROC            (gst_d3d12_video_proc_get_type ())
#define GST_D3D12_VIDEO_PROC(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_D3D12_VIDEO_PROC, GstD3D12VideoProc))
#define GST_D3D12_VIDEO_PROC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_D3D12_VIDEO_PROC, GstD3D12VideoProcClass))
#define GST_IS_D3D12_VIDEO_PROC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_D3D12_VIDEO_PROC))
#define GST_IS_D3D12_VIDEO_PROC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_D3D12_VIDEO_PROC))
#define GST_D3D12_VIDEO_PROC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_D3D12_VIDEO_PROC, GstD3D12VideoProcClass))
#define GST_D3D12_VIDEO_PROC_CAST(obj)       ((GstD3D12VideoProc*)(obj))

/**
 * GstD3D12VideoProc:
 *
 * Opaque GstD3D12VideoProc struct
 *
 * Since: 1.28
 */
struct _GstD3D12VideoProc
{
  GstObject parent;

  GstD3D12Device *device;

  /*< private >*/
  GstD3D12VideoProcPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

/**
 * GstD3D12VideoProcClass:
 *
 * Opaque GstD3D12VideoProcClass struct
 *
 * Since: 1.28
 */
struct _GstD3D12VideoProcClass
{
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GST_D3D12_API
GType               gst_d3d12_video_proc_get_type (void);

GST_D3D12_API
GstD3D12VideoProc * gst_d3d12_video_proc_new (GstD3D12Device * device,
                                              guint num_in_descs,
                                              const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC * in_desc,
                                              const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC * out_desc,
                                              GstStructure * config);

GST_D3D12_API
gboolean            gst_d3d12_video_proc_execute (GstD3D12VideoProc * vp,
                                                  guint num_in_fences,
                                                  ID3D12Fence ** in_fence,
                                                  guint64 * in_fence_val,
                                                  guint num_input_streams,
                                                  const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS * in_args,
                                                  const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS * out_args,
                                                  GstD3D12FenceData * fence_data,
                                                  ID3D12Fence ** out_fence,
                                                  guint64 * out_fence_val);

G_END_DECLS
