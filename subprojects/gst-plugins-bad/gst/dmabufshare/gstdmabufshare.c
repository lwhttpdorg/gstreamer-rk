/* GStreamer
 *
 * Copyright (C) 2025 Julian Armistead <julian.armistead@linaro.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <gst/gst.h>
#include <glib.h>
#include <glib-object.h>
#include <gst/allocators/allocators.h>
#include <gst/allocators/allocators.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/base/gstpushsrc.h>
#include <gst/video/video.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <gst/allocators/allocators.h>

// Add package definitions
#define PACKAGE "dmabufshare"
#define PACKAGE_NAME "dmabufshare"
#define PACKAGE_VERSION "1.0"
#define PACKAGE_ORIGIN "https://example.com"

#define MAX_BUFFERS (4)

GST_DEBUG_CATEGORY_STATIC(gst_dmabuf_provider_debug);
#define GST_CAT_DEFAULT gst_dmabuf_provider_debug
#define GST_TYPE_DMABUF_PROVIDER (gst_dmabuf_provider_get_type())

// Note : Declare new final type, with fn naming prefix, gst_dmabuf_provider.
// Namespace GST_DMABUF_PROVIDER. Derives from GstBaseTransform base class
G_DECLARE_FINAL_TYPE(GstDmaBufProvider, gst_dmabuf_provider, GST, DMABUF_PROVIDER, GstBaseTransform)


typedef struct
{
    int dma_filedesc;
    void *virtual_addr;
    size_t buffer_size;
} DMABuffer;

typedef struct
{
    int dma_heap_filedesc;
    DMABuffer buffers[MAX_BUFFERS];
    int buffer_count;
} DMAContext;

struct _GstDmaBufProvider
{
    GstBaseTransform parent;
    DMAContext *dma_context;
    GstAllocator *allocator;
    guint current_buffer;
    gboolean *buffer_valid;
    GMutex buffer_lock;
};

enum
{
    PROPERTY_0,
    PROPERTY_DMA_CONTEXT
};

// Now define type. sets up class and instance init.
G_DEFINE_TYPE(GstDmaBufProvider, gst_dmabuf_provider, GST_TYPE_BASE_TRANSFORM);

struct _GstDmaBufProviderPool
{
    GstBufferPool parent;
    GstDmaBufProvider *provider;
    GstAllocator *allocator;
    GstAllocationParams params;
    GstCaps *caps;
};

struct _GstDmaBufProviderPoolClass
{
    GstBufferPoolClass parent_class;
};

// Create custom GstBufferPool class to handle the DMA buffer allocation:
// First define our custom pool type
typedef struct _GstDmaBufProviderPool GstDmaBufProviderPool;
typedef struct _GstDmaBufProviderPoolClass GstDmaBufProviderPoolClass;

GType gst_dmabuf_provider_pool_get_type(void);
G_DEFINE_TYPE(GstDmaBufProviderPool, gst_dmabuf_provider_pool, GST_TYPE_BUFFER_POOL);


static GstFlowReturn gst_dmabuf_provider_pool_alloc(GstBufferPool *bpool, GstBuffer **buffer,
                                                   GstBufferPoolAcquireParams *params)
{
    GstDmaBufProviderPool *pool = (GstDmaBufProviderPool *)bpool;
    GstBuffer *buf;
    GstMemory *mem;
    guint idx;

    idx = pool->provider->current_buffer % pool->provider->dma_context->buffer_count;
    
    buf = gst_buffer_new();
    if (!buf)
    {
        return GST_FLOW_ERROR;
    }

    mem = gst_dmabuf_allocator_alloc(pool->allocator,
                                    pool->provider->dma_context->buffers[idx].dma_filedesc,
                                    pool->provider->dma_context->buffers[idx].buffer_size);
    if (!mem)
    {
        gst_buffer_unref(buf);
        return GST_FLOW_ERROR;
    }

    gst_buffer_append_memory(buf, mem);
    pool->provider->current_buffer++;

    *buffer = buf;
    return GST_FLOW_OK;
}


static void gst_dmabuf_provider_pool_free(GstBufferPool *bpool, GstBuffer *buffer)
{
    gst_buffer_unref(buffer);
}


static gboolean gst_dmabuf_provider_pool_set_config(GstBufferPool *bpool, GstStructure *config)
{
    GstDmaBufProviderPool *pool = (GstDmaBufProviderPool *)bpool;
    GstCaps *caps;
    guint buffer_size, min_buffers, max_buffers;

    if (!gst_buffer_pool_config_get_params(config, &caps, &buffer_size, &min_buffers, &max_buffers))
    {
        return FALSE;
    }

    if (pool->caps)
    {
        gst_caps_unref(pool->caps);
    }

    pool->caps = caps ? gst_caps_ref(caps) : NULL;

    return GST_BUFFER_POOL_CLASS(gst_dmabuf_provider_pool_parent_class)->set_config(bpool, config);
}


static void gst_dmabuf_provider_pool_class_init(GstDmaBufProviderPoolClass *klass)
{
    GstBufferPoolClass *pool_class = GST_BUFFER_POOL_CLASS(klass);

    pool_class->alloc_buffer = gst_dmabuf_provider_pool_alloc;
    pool_class->free_buffer = gst_dmabuf_provider_pool_free;
    pool_class->set_config = gst_dmabuf_provider_pool_set_config;
}


static void gst_dmabuf_provider_pool_init(GstDmaBufProviderPool *pool)
{
    pool->provider = NULL;
    pool->allocator = NULL;
    pool->caps = NULL;
}


static void gst_dmabuf_provider_set_property(GObject *object, guint prop_id,
                                           const GValue *value, GParamSpec *pspec)
{
    GstDmaBufProvider *provider = GST_DMABUF_PROVIDER(object);

    switch (prop_id)
    {
        case PROPERTY_DMA_CONTEXT:
            provider->dma_context = g_value_get_pointer(value);
            if (provider->dma_context)
            {
                // Initialize buffer tracking
                provider->buffer_valid = g_new0(gboolean, provider->dma_context->buffer_count);
                for (int i = 0; i < provider->dma_context->buffer_count; i++)
                {
                    provider->buffer_valid[i] = TRUE;  // Initially all buffers are valid
                }
            }
            GST_DEBUG_OBJECT(provider, "DMA context set: %p", provider->dma_context);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}


static void gst_dmabuf_provider_get_property(GObject *object, guint prop_id,
                                           GValue *value, GParamSpec *pspec)
{
    GstDmaBufProvider *provider = GST_DMABUF_PROVIDER(object);

    switch (prop_id)
    {
        case PROPERTY_DMA_CONTEXT:
            g_value_set_pointer(value, provider->dma_context);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
            break;
    }
}


// Initialize element
static void gst_dmabuf_provider_init(GstDmaBufProvider *provider)
{
    provider->current_buffer = 0;
    provider->dma_context = NULL;

    // Create DMABuf allocator
    provider->allocator = gst_dmabuf_allocator_new();
    provider->buffer_valid = NULL;
    g_mutex_init(&provider->buffer_lock);

}


static void gst_dmabuf_provider_finalize(GObject *object)
{
    GstDmaBufProvider *provider = GST_DMABUF_PROVIDER(object);

    if (provider->allocator)
    {
        gst_object_unref(provider->allocator);
        provider->allocator = NULL;
    }

    G_OBJECT_CLASS(gst_dmabuf_provider_parent_class)->finalize(object);
}


// Add sink and source pad templates
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-raw, "
        "format = (string) { RGB16, BGR, RGB, ABGR, xBGR, RGBA, RGBx, "
        "GRAY8, GRAY16_LE, GRAY16_BE, YVU9, YV12, YUY2, YVYU, UYVY, "
        "Y42B, Y41B, YUV9, NV12_64Z32, NV24, NV61, NV16, NV21, NV12, "
        "I420, ARGB, xRGB, BGRA, BGRx, BGR15, RGB15 }, "
        "width = (int) [ 1, 32768 ], "
        "height = (int) [ 1, 32768 ], "
        "framerate = (fraction) [ 0/1, 2147483647/1 ]"
    )    
);


// Add after includes, before type definition
static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
    "src",                 // name of the pad
    GST_PAD_SRC,           // pad direction
    GST_PAD_ALWAYS,        // presence
    GST_STATIC_CAPS(       // capabilities
        "video/x-raw, "
        "format = (string) { RGB16, BGR, RGB, ABGR, xBGR, RGBA, RGBx, "
        "GRAY8, GRAY16_LE, GRAY16_BE, YVU9, YV12, YUY2, YVYU, UYVY, "
        "Y42B, Y41B, YUV9, NV12_64Z32, NV24, NV61, NV16, NV21, NV12, "
        "I420, ARGB, xRGB, BGRA, BGRx, BGR15, RGB15 }, "
        "width = (int) [ 1, 32768 ], "
        "height = (int) [ 1, 32768 ], "
        "framerate = (fraction) [ 0/1, 2147483647/1 ]"
    )
);


static GstCaps *gst_dmabuf_provider_transform_caps(GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, GstCaps *filter)
{
    GstDmaBufProvider *provider = GST_DMABUF_PROVIDER(trans);
    GstCaps *ret;

    // Just pass through the caps from v4l2src
    ret = gst_caps_copy(caps);

    GST_DEBUG_OBJECT(provider, "Transformed caps from %" GST_PTR_FORMAT 
                     " to %" GST_PTR_FORMAT, caps, ret);

    if (filter)
    {
        GstCaps *intersection;
        intersection = gst_caps_intersect_full(filter, ret, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref(ret);
        ret = intersection;
    }

    return ret;
}


// Now modify create_pool to use our custom pool
static GstBufferPool *gst_dmabuf_provider_create_pool(GstDmaBufProvider *provider, GstCaps *caps)
{
    GstDmaBufProviderPool *pool;
    GstStructure *config;

    GST_DEBUG_OBJECT(provider, "Creating custom buffer pool");

    pool = g_object_new(gst_dmabuf_provider_pool_get_type(), NULL);
    pool->provider = provider;
    pool->allocator = provider->allocator;

    config = gst_buffer_pool_get_config(GST_BUFFER_POOL_CAST(pool));
    gst_buffer_pool_config_set_params(config, caps,
                                     provider->dma_context->buffers[0].buffer_size,
                                     provider->dma_context->buffer_count,
                                     provider->dma_context->buffer_count);
    gst_buffer_pool_config_set_allocator(config, provider->allocator, NULL);
    gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (!gst_buffer_pool_set_config(GST_BUFFER_POOL_CAST(pool), config))
    {
        GST_ERROR_OBJECT(provider, "Failed to set buffer pool config");
        gst_object_unref(pool);
        return NULL;
    }

    return GST_BUFFER_POOL_CAST(pool);
}


static gboolean gst_dmabuf_provider_propose_allocation(GstBaseTransform *trans,
    GstQuery *decide_query, GstQuery *query)
{
    GstDmaBufProvider *provider = GST_DMABUF_PROVIDER(trans);
    GstCaps *caps;
    gboolean need_pool;
    
    gst_query_parse_allocation(query, &caps, &need_pool);
    
    GST_DEBUG_OBJECT(provider, "Allocation query caps: %" GST_PTR_FORMAT, caps);
    GST_DEBUG_OBJECT(provider, "Need pool: %d", need_pool);

    if (!caps)
    {
        GST_ERROR_OBJECT(provider, "No caps in allocation query");
        return FALSE;
    }

    if (need_pool)
    {
        GstBufferPool *pool = gst_dmabuf_provider_create_pool(provider, caps);
        if (!pool)
        {
            GST_ERROR_OBJECT(provider, "Failed to create buffer pool");
            return FALSE;
        }

        guint buffer_size = provider->dma_context->buffers[0].buffer_size;
        guint min_buffers = provider->dma_context->buffer_count;
        guint max_buffers = provider->dma_context->buffer_count;

        GST_DEBUG_OBJECT(provider, "Proposing pool with buffer size=%u, min=%u, max=%u",
                        buffer_size, min_buffers, max_buffers);

        gst_query_add_allocation_pool(query, pool, buffer_size, min_buffers, max_buffers);
        gst_object_unref(pool);

        // Add support for video meta
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, NULL);
    }

    return TRUE;
}


static void gst_dmabuf_provider_class_init(GstDmaBufProviderClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    GstElementClass *element_class = GST_ELEMENT_CLASS(klass);
    GstBaseTransformClass *transform_class = GST_BASE_TRANSFORM_CLASS(klass);

    // Add property handlers
    gobject_class->set_property = gst_dmabuf_provider_set_property;
    gobject_class->get_property = gst_dmabuf_provider_get_property;
    gobject_class->finalize = gst_dmabuf_provider_finalize;

    // Install properties
    g_object_class_install_property(gobject_class, PROPERTY_DMA_CONTEXT,
        g_param_spec_pointer("dma-ctx", "DMA Context",
            "DMA buffer context", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    // Add pad templates
    gst_element_class_add_static_pad_template(element_class, &sink_template);
    gst_element_class_add_static_pad_template(element_class, &src_template);


    // Set virtual functions
    transform_class->transform_caps = gst_dmabuf_provider_transform_caps;
    transform_class->set_caps = NULL;  // Accept any caps
     // Add propose_allocation virtual function
    transform_class->propose_allocation = gst_dmabuf_provider_propose_allocation;


    // Set metadata
    gst_element_class_set_static_metadata(element_class,
        "DMA Buffer Provider",
        "Transform/Video",
        "Provides DMA buffers for video frames",
        "julian@linaro.org");


}


static gboolean plugin_init(GstPlugin *plugin)
{

    GST_DEBUG_CATEGORY_INIT(gst_dmabuf_provider_debug,
                           "dmabufshare",
                           0,
                           "Transform element with DMA buffer support");


    return gst_element_register(plugin, "dmabufshare",
                              GST_RANK_NONE,
                              GST_TYPE_DMABUF_PROVIDER);
}


GST_PLUGIN_DEFINE(
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    dmabufshare,
    "DMA Buffer Provider",
    plugin_init,
    PACKAGE_VERSION,            
    "LGPL",                     
    PACKAGE_NAME,               
    PACKAGE_ORIGIN             
)
