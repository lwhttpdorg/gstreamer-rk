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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstd3d12.h"
#include "gstd3d12-private.h"
#include "gstd3d12videoproc.h"
#include <directx/d3dx12.h>
#include <wrl.h>

/* *INDENT-OFF* */
using namespace Microsoft::WRL;
/* *INDENT-ON* */

GST_DEBUG_CATEGORY (gst_d3d12_video_proc_debug);
#define GST_CAT_DEFAULT gst_d3d12_video_proc_debug

struct _GstD3D12VideoProcPrivate
{
  _GstD3D12VideoProcPrivate ()
  {
    fence_data_pool = gst_d3d12_fence_data_pool_new ();
  }

   ~_GstD3D12VideoProcPrivate ()
  {
    if (fence_val > 0)
      gst_d3d12_cmd_queue_fence_wait (cq, fence_val);

    gst_clear_object (&ca_pool);
    gst_clear_object (&fence_data_pool);
  }

  ComPtr < ID3D12VideoDevice > video_device;
  ComPtr < ID3D12VideoProcessor > vp;
  ComPtr < ID3D12VideoProcessCommandList > cl;
  GstD3D12FenceDataPool *fence_data_pool;
  GstD3D12CmdAllocPool *ca_pool = nullptr;
  GstD3D12CmdQueue *cq = nullptr;
  ID3D12Fence *cq_fence = nullptr;
  guint64 fence_val = 0;
};

static void gst_d3d12_video_proc_finalize (GObject * object);

#define gst_d3d12_video_proc_parent_class parent_class
G_DEFINE_TYPE (GstD3D12VideoProc, gst_d3d12_video_proc, GST_TYPE_OBJECT);

static void
gst_d3d12_video_proc_class_init (GstD3D12VideoProcClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = gst_d3d12_video_proc_finalize;

  GST_DEBUG_CATEGORY_INIT (gst_d3d12_video_proc_debug,
      "d3d12videoproc", 0, "d3d12videoproc");
}

static void
gst_d3d12_video_proc_init (GstD3D12VideoProc * self)
{
  self->priv = new GstD3D12VideoProcPrivate ();
}

static void
gst_d3d12_video_proc_finalize (GObject * object)
{
  auto self = GST_D3D12_VIDEO_PROC (object);

  gst_clear_object (&self->device);

  delete self->priv;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/**
 * gst_d3d12_video_proc_new:
 * @device: a #GstD3D12Device
 * @num_in_descs: input stream desc array size
 * @in_desc: array of D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC
 * @out_desc: D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC
 * @config: (transfer full) (nullable): config
 *
 * Creates a new video processor instance
 *
 * Returns: (transfer full) (nullable): a new #GstD3D12VideoProc instance
 * or %NULL if conversion is not supported
 *
 * Since: 1.28
 */
GstD3D12VideoProc *
gst_d3d12_video_proc_new (GstD3D12Device * device,
    guint num_in_descs,
    const D3D12_VIDEO_PROCESS_INPUT_STREAM_DESC * in_desc,
    const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_DESC * out_desc,
    GstStructure * config)
{
  auto queue = gst_d3d12_device_get_cmd_queue (device,
      D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS);
  if (!queue) {
    GST_WARNING_OBJECT (device, "VP queue unavailable");
    return nullptr;
  }

  auto self = (GstD3D12VideoProc *)
      g_object_new (GST_TYPE_D3D12_VIDEO_PROC, nullptr);
  gst_object_ref_sink (self);
  self->device = (GstD3D12Device *) gst_object_ref (device);

  auto priv = self->priv;
  priv->cq = queue;
  priv->cq_fence = gst_d3d12_cmd_queue_get_fence_handle (queue);

  auto device_handle = gst_d3d12_device_get_device_handle (device);
  auto hr = device_handle->QueryInterface (IID_PPV_ARGS (&priv->video_device));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't get video device");
    gst_object_unref (self);
    return nullptr;
  }

  hr = priv->video_device->CreateVideoProcessor (0, out_desc, num_in_descs,
      in_desc, IID_PPV_ARGS (&priv->vp));
  if (!gst_d3d12_result (hr, device)) {
    GST_ERROR_OBJECT (self, "Couldn't create video processor");
    gst_object_unref (self);
    return nullptr;
  }

  priv->ca_pool = gst_d3d12_cmd_alloc_pool_new (device_handle,
      D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS);

  return self;
}

/**
 * gst_d3d12_video_proc_execute:
 * @vp: a #GstD3D12VideoProc
 * @num_in_fences: input fence size
 * @in_fence: array of input ID3D12Fence
 * @in_fence_val: array of input fence value
 * @num_input_streams: input stream arguments array size
 * @in_args: array of D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS
 * @out_args: D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS
 * @fence_data: (transfer full) (nullable): a #GstD3D12FenceData
 * @fence: (out) (transfer full) (optional): pointer to output ID3D12Fence
 * @fence_val: (out) (optional): output fence value
 *
 * Constructs commandlist and executes
 *
 * %TRUE if successful
 *
 * Since: 1.28
 */
gboolean
gst_d3d12_video_proc_execute (GstD3D12VideoProc * vp, guint num_in_fences,
    ID3D12Fence ** in_fence, guint64 * in_fence_val, guint num_input_streams,
    const D3D12_VIDEO_PROCESS_INPUT_STREAM_ARGUMENTS * in_args,
    const D3D12_VIDEO_PROCESS_OUTPUT_STREAM_ARGUMENTS * out_args,
    GstD3D12FenceData * fence_data, ID3D12Fence ** out_fence,
    guint64 * out_fence_val)
{
  auto priv = vp->priv;

  GstD3D12CmdAlloc *gst_ca = nullptr;
  gst_d3d12_cmd_alloc_pool_acquire (priv->ca_pool, &gst_ca);
  if (!gst_ca) {
    GST_ERROR_OBJECT (vp, "Couldn't acquire command allocator");
    if (fence_data)
      gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  if (!fence_data)
    gst_d3d12_fence_data_pool_acquire (priv->fence_data_pool, &fence_data);
  auto ca = gst_d3d12_cmd_alloc_get_handle (gst_ca);
  gst_d3d12_fence_data_push (fence_data, FENCE_NOTIFY_MINI_OBJECT (gst_ca));

  auto hr = ca->Reset ();
  if (!gst_d3d12_result (hr, vp->device)) {
    GST_ERROR_OBJECT (vp, "Couldn't reset command allocator");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  if (!priv->cl) {
    auto device = gst_d3d12_device_get_device_handle (vp->device);
    hr = device->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_VIDEO_PROCESS,
        ca, nullptr, IID_PPV_ARGS (&priv->cl));
  } else {
    hr = priv->cl->Reset (ca);
  }

  if (!gst_d3d12_result (hr, vp->device)) {
    GST_ERROR_OBJECT (vp, "Couldn't setup command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  priv->cl->ProcessFrames (priv->vp.Get (), out_args, num_input_streams,
      in_args);
  hr = priv->cl->Close ();

  if (!gst_d3d12_result (hr, vp->device)) {
    GST_ERROR_OBJECT (vp, "Couldn't close command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  ID3D12CommandList *cl[] = { priv->cl.Get () };

  guint64 fence_val_ret = 0;
  hr = gst_d3d12_cmd_queue_execute_command_lists_full (priv->cq,
      num_in_fences, in_fence, in_fence_val, 1, cl, &fence_val_ret);

  if (!gst_d3d12_result (hr, vp->device)) {
    GST_ERROR_OBJECT (vp, "Couldn't execute command list");
    gst_d3d12_fence_data_unref (fence_data);
    return FALSE;
  }

  gst_d3d12_cmd_queue_set_notify (priv->cq, fence_val_ret,
      FENCE_NOTIFY_MINI_OBJECT (fence_data));

  priv->fence_val = fence_val_ret;
  if (out_fence) {
    *out_fence = priv->cq_fence;
    priv->cq_fence->AddRef ();
  }

  if (out_fence_val)
    *out_fence_val = fence_val_ret;

  return TRUE;
}
