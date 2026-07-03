#include "placebovulkan.h"

static void gst_placebo_vulkan_init (GstPlaceboVulkan * vk);
static void gst_placebo_vulkan_clear (GstPlaceboAPI * api);
static gboolean gst_placebo_vulkan_configure (GstPlaceboAPI * api);
static gboolean gst_placebo_vulkan_start (GstPlaceboAPI * api);
static gboolean gst_placebo_vulkan_stop (GstPlaceboAPI * api);
static gboolean gst_placebo_vulkan_ensure_element_data (GstPlaceboAPI * api);
static gboolean gst_placebo_vulkan_find_local_context (GstPlaceboAPI * api);
static gboolean gst_placebo_vulkan_handle_context_query (GstPlaceboAPI * api,
    GstPadDirection direction, GstQuery * query);
static void gst_placebo_vulkan_handle_set_context (GstPlaceboAPI * api,
    GstContext * context);
static gboolean gst_placebo_vulkan_decide_allocation (GstPlaceboAPI * api,
    GstQuery * query);
static gboolean gst_placebo_vulkan_set_caps (GstPlaceboAPI * api,
    GstCaps * in_caps, GstCaps * out_caps);
static gboolean gst_placebo_vulkan_map_frame (GstPlaceboAPI * api,
    GstBuffer * buf, struct pl_frame *frame, const GstVideoInfo * info,
    GstMapFlags flags);
static void gst_placebo_vulkan_unmap_frame (GstPlaceboAPI * api,
    struct pl_frame *frame);
static GstFlowReturn gst_placebo_vulkan_render (GstPlaceboAPI * api,
    struct pl_frame *frame, struct pl_frame *target);

#define GST_CAT_DEFAULT gst_placebo_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY_STATIC (gst_gl_debug);

#define gst_placebo_vulkan_parent_class parent_class
G_DEFINE_TYPE (GstPlaceboVulkan, gst_placebo_vulkan, GST_TYPE_PLACEBO_API);

static void
gst_placebo_vulkan_class_init (GstPlaceboVulkanClass * klass)
{
  GObjectClass *gobject_class;
  GstPlaceboAPIClass *api_class;

  gobject_class = (GObjectClass *) klass;
  api_class = GST_PLACEBO_API_CLASS (klass);

  api_class->clear = gst_placebo_vulkan_clear;
  api_class->configure = gst_placebo_vulkan_configure;
  api_class->start = gst_placebo_vulkan_start;
  api_class->stop = gst_placebo_vulkan_stop;
  api_class->ensure_element_data = gst_placebo_vulkan_ensure_element_data;
  api_class->find_local_context = gst_placebo_vulkan_find_local_context;
  api_class->handle_context_query = gst_placebo_vulkan_handle_context_query;
  api_class->handle_set_context = gst_placebo_vulkan_handle_set_context;
  api_class->decide_allocation = gst_placebo_vulkan_decide_allocation;
  api_class->set_caps = gst_placebo_vulkan_set_caps;
  api_class->map_frame = gst_placebo_vulkan_map_frame;
  api_class->unmap_frame = gst_placebo_vulkan_unmap_frame;
  api_class->render = gst_placebo_vulkan_render;
}

static void
gst_placebo_vulkan_init (GstPlaceboVulkan * vk)
{
  static gsize _init = 0;
  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_GET (gst_placebo_debug, "placebo");
    g_once_init_leave (&_init, 1);
  }
}

static void
gst_placebo_lock_queue (void *priv, uint32_t qf, uint32_t qidx)
{
  GstPlaceboVulkan *vk = priv;
  vk->queue = gst_vulkan_device_get_queue (vk->device, qf, qidx);
  gst_vulkan_queue_submit_lock (vk->queue);
}

static void
gst_placebo_unlock_queue (void *priv, uint32_t qf, uint32_t qidx)
{
  GstPlaceboVulkan *vk = priv;
  gst_vulkan_queue_submit_unlock (vk->queue);
}

static gboolean
_queue_family_index (GstVulkanDevice * device, GstVulkanQueue * queue,
    gpointer user_data)
{
  guint flags =
      device->physical_device->queue_family_props[queue->family].queueFlags;
  gsize count =
      device->physical_device->queue_family_props[queue->family].queueCount;
  struct pl_vulkan_import_params *params = user_data;
  if (flags & VK_QUEUE_GRAPHICS_BIT) {
    params->queue_graphics.index = queue->family;
    params->queue_graphics.count = count;
  }
  if (flags & VK_QUEUE_COMPUTE_BIT) {
    params->queue_compute.index = queue->family;
    params->queue_compute.count = count;
  }
  if (flags & VK_QUEUE_TRANSFER_BIT) {
    params->queue_transfer.index = queue->family;
    params->queue_transfer.count = count;
  }
  return TRUE;
}

static void
gst_placebo_vulkan_clear (GstPlaceboAPI * api)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  g_rec_mutex_lock (&api->context_lock);
  gst_clear_object (&vk->device);
  gst_clear_object (&vk->queue);
  gst_clear_object (&vk->instance);
  g_rec_mutex_unlock (&api->context_lock);
}

static gboolean
gst_placebo_vulkan_configure (GstPlaceboAPI * api)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  struct pl_vulkan_import_params params = {
    .instance = vk->instance->instance,
    .phys_device = vk->device->physical_device->device,
    .device = vk->device->device,
    .features = gst_vulkan_physical_device_get_features (vk->device->physical_device),
    .queue_ctx = vk,
    .lock_queue = gst_placebo_lock_queue,
    .unlock_queue = gst_placebo_unlock_queue,
    .queue_graphics = {
          .index = VK_QUEUE_FAMILY_IGNORED,
          .count = 0,
        },
    .queue_compute = {
          .index = VK_QUEUE_FAMILY_IGNORED,
          .count = 0,
        },
    .queue_transfer = {
          .index = VK_QUEUE_FAMILY_IGNORED,
          .count = 0,
        },
  };
  gst_vulkan_device_foreach_queue (vk->device,
      (GstVulkanDeviceForEachQueueFunc) _queue_family_index, &params);
  vk->impl = pl_vulkan_import (api->log, &params);
  if (!vk->impl) {
    GST_ELEMENT_ERROR (api->placebo, RESOURCE, NOT_FOUND,
        ("Failed creating Vulkan context"), (NULL));
    return FALSE;
  }
  api->gpu = vk->impl->gpu;
  return TRUE;
}

static gboolean
gst_placebo_vulkan_start (GstPlaceboAPI * api)
{
  return gst_placebo_api_configure (api);
}

static gboolean
gst_placebo_vulkan_stop (GstPlaceboAPI * api)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  pl_vulkan_destroy (&vk->impl);
  return TRUE;
}

static gboolean
gst_placebo_vulkan_ensure_element_data (GstPlaceboAPI * api)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  if (!gst_vulkan_ensure_element_data (GST_ELEMENT (api->placebo), NULL,
          &vk->instance)) {
    GST_ELEMENT_ERROR (api->placebo, RESOURCE, NOT_FOUND,
        ("Failed to retrieve vulkan instance"), (NULL));
    return FALSE;
  }
  if (!gst_vulkan_ensure_element_device (GST_ELEMENT (api->placebo),
          vk->instance, &vk->device, 0)) {
    return FALSE;
  }
  return TRUE;
}

static gboolean
_find_local_vk_context_unlocked (GstPlaceboVulkan * vk)
{
  GstPlaceboAPI *api = GST_PLACEBO_API (vk);
  GstVulkanDevice *device, *tmp;
  gboolean ret;
  if (vk->device && vk->queue)
    return TRUE;
  device = tmp = vk->device;
  g_rec_mutex_unlock (&api->context_lock);
  /* we need to drop the lock to query as another element may also be
   * performing a context query on us which would also attempt to take the
   * context_lock. Our query could block on the same lock in the other element.
   */
  ret =
      gst_vulkan_device_run_context_query (GST_ELEMENT (api->placebo), &device);

  g_rec_mutex_lock (&api->context_lock);
  if (ret) {
    if (vk->device != tmp) {
      /* we need to recheck everything since we dropped the lock and the
       * context has changed */
      if (vk->device) {
        if (device != vk->device)
          gst_clear_object (&device);
        return TRUE;
      }
    }
  }
  return FALSE;
}

static gboolean
gst_placebo_vulkan_find_local_context (GstPlaceboAPI * api)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  gboolean ret;
  g_rec_mutex_lock (&api->context_lock);
  ret = _find_local_vk_context_unlocked (vk);
  g_rec_mutex_unlock (&api->context_lock);
  return ret;
}

static gboolean
gst_placebo_vulkan_handle_context_query (GstPlaceboAPI * api,
    GstPadDirection direction, GstQuery * query)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  if (gst_vulkan_handle_context_query (GST_ELEMENT (api->placebo), query,
          NULL, vk->instance, vk->device))
    return TRUE;
  if (gst_vulkan_queue_handle_context_query (GST_ELEMENT (api->placebo),
          query, vk->queue))
    return TRUE;
  return FALSE;
}

static void
gst_placebo_vulkan_handle_set_context (GstPlaceboAPI * api,
    GstContext * context)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  g_rec_mutex_lock (&api->context_lock);
  gst_vulkan_handle_set_context (GST_ELEMENT (api->placebo), context, NULL,
      &vk->instance);
  g_rec_mutex_unlock (&api->context_lock);
}

static gboolean
gst_placebo_vulkan_decide_allocation_unlocked (GstPlaceboAPI * api,
    GstQuery * query)
{
  GstPlaceboVulkan *vk = GST_PLACEBO_VULKAN (api);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  gst_query_parse_allocation (query, &caps, NULL);
  if (!caps)
    return FALSE;
  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);
    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;
    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }
  if (!pool || !GST_IS_VULKAN_IMAGE_BUFFER_POOL (pool)) {
    if (pool)
      gst_object_unref (pool);
    pool = gst_vulkan_image_buffer_pool_new (vk->device);
  }
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_object_unref (pool);
    return FALSE;
  }
  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);
  gst_object_unref (pool);
  return TRUE;
}

static gboolean
gst_placebo_vulkan_decide_allocation (GstPlaceboAPI * api, GstQuery * query)
{
  gboolean res;
  g_rec_mutex_lock (&api->context_lock);
  res = gst_placebo_vulkan_decide_allocation_unlocked (api, query);
  g_rec_mutex_unlock (&api->context_lock);
  return res;
}

static gboolean
gst_placebo_vulkan_set_caps (GstPlaceboAPI * api, GstCaps * in_caps,
    GstCaps * out_caps)
{
  return TRUE;
}

static bool
gst_placebo_acquire_frame (pl_gpu gpu, struct pl_frame *frame)
{
  GstVideoFrame *f = frame->user_data;
  for (int n = 0; n < frame->num_planes; n++) {
    GstVulkanImageMemory *image = (GstVulkanImageMemory *) f->map[n].memory;
    pl_vulkan_release_ex (gpu,
// *INDENT-OFF*
        pl_vulkan_release_params (
          .tex =
            frame->planes[n].texture,
          .layout = image->barrier.image_layout,
          .qf = VK_QUEUE_FAMILY_IGNORED,
          .semaphore = {
                .sem = image->barrier.parent.semaphore,
                .value = image->barrier.parent.semaphore_value,
              },
        )
// *INDENT-ON*
//
        );
  }
  return true;
}

static void
gst_placebo_release_frame (pl_gpu gpu, struct pl_frame *frame)
{
  GstVideoFrame *f = frame->user_data;
  for (int n = 0; n < frame->num_planes; n++) {
    GstVulkanImageMemory *image = (GstVulkanImageMemory *) f->map[n].memory;
    int ok = pl_vulkan_hold_ex (gpu,
// *INDENT-OFF*
        pl_vulkan_hold_params (
          .tex =
            frame->planes[n].texture,
          .out_layout = &image->barrier.image_layout,
          .qf = VK_QUEUE_FAMILY_IGNORED,
          .semaphore = {
                .sem = image->barrier.parent.semaphore,
                .value = image->barrier.parent.semaphore_value + 1,
              },
        )
// *INDENT-ON*
//
    );
  }
}

static gboolean
gst_placebo_vulkan_map_frame (GstPlaceboAPI * api, GstBuffer * buf,
    struct pl_frame *frame, const GstVideoInfo * info, GstMapFlags flags)
{
  GstVideoFrame *f = frame->user_data;
  GstMemory *mem = NULL;
  guint planes = gst_buffer_n_memory (buf);

  const VkImageAspectFlags aspects[] = { VK_IMAGE_ASPECT_PLANE_0_BIT,
    VK_IMAGE_ASPECT_PLANE_1_BIT, VK_IMAGE_ASPECT_PLANE_2_BIT,
  };

  for (guint p = 0; p < planes; ++p) {
    mem = gst_buffer_peek_memory (buf, p);
    if (G_UNLIKELY (!gst_is_vulkan_image_memory (mem)))
      return FALSE;
    f->map[p].memory = mem;
    GstVulkanImageMemory *image = (GstVulkanImageMemory *) mem;
    VkImageAspectFlags plane_aspect;
    if (GST_VIDEO_INFO_N_PLANES (info) == planes)
      plane_aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    else
      plane_aspect = aspects[p];
    frame->planes[p].texture = pl_vulkan_wrap (api->gpu,
// *INDENT-OFF*
        pl_vulkan_wrap_params (
          .image = image->image,
          .aspect = plane_aspect,
          .width = GST_VIDEO_INFO_COMP_WIDTH (info, p),
          .height = GST_VIDEO_INFO_COMP_HEIGHT (info, p),
          .depth = 0,
          .format = image->create_info.format,
          .usage = image->create_info.usage,
        )
// *INDENT-ON*
//
        );
    if (!frame->planes[p].texture)
      return FALSE;
  }
  frame->acquire = gst_placebo_acquire_frame;
  frame->release = gst_placebo_release_frame;
  return TRUE;
}

static void
gst_placebo_vulkan_unmap_frame (GstPlaceboAPI * api, struct pl_frame *frame)
{
  for (guint p = 0; p < frame->num_planes; ++p) {
    if (!frame->planes[p].texture)
      continue;
    pl_vulkan_unwrap (api->gpu, frame->planes[p].texture, NULL, NULL);
  }
}

static GstFlowReturn
gst_placebo_vulkan_render (GstPlaceboAPI * api, struct pl_frame *frame,
    struct pl_frame *target)
{
  if (!pl_render_image (api->renderer, frame, target, &api->opts->params))
    return GST_FLOW_CUSTOM_ERROR;
  return GST_FLOW_OK;
}

GstPlaceboVulkan *
gst_placebo_vulkan_new (GstPlacebo * placebo)
{
  GstPlaceboVulkan *vk;

  g_return_val_if_fail (placebo != NULL, NULL);

  vk = g_object_new (GST_TYPE_PLACEBO_VULKAN, NULL);
  GST_PLACEBO_API (vk)->placebo = placebo;

  gst_object_ref_sink (vk);

  return vk;
}
