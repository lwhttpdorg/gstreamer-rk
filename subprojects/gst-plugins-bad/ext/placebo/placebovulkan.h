/*
 * placebo gstreamer plugin
 * Copyright (C) 2026 Martin Rodriguez Reboredo <yakoyoku@gmail.com>
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

#ifndef _GST_PLACEBO_VULKAN_H_
#define _GST_PLACEBO_VULKAN_H_

#include <gst/vulkan/vulkan.h>

#include <libplacebo/vulkan.h>

#include "placeboapi.h"

typedef struct _GstPlaceboVulkan GstPlaceboVulkan;
typedef struct _GstPlaceboVulkanClass GstPlaceboVulkanClass;

#define GST_TYPE_PLACEBO_VULKAN             (gst_placebo_vulkan_get_type())
#define GST_PLACEBO_VULKAN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLACEBO_VULKAN,GstPlaceboVulkan))
#define GST_IS_PLACEBO_VULKAN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLACEBO_VULKAN))
#define GST_PLACEBO_VULKAN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_PLACEBO_VULKAN,GstPlaceboVulkanClass))
#define GST_IS_PLACEBO_VULKAN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_PLACEBO_VULKAN))
#define GST_PLACEBO_VULKAN_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_PLACEBO_VULKAN,GstPlaceboVulkanClass))

struct _GstPlaceboVulkan
{
  GstPlaceboAPI api;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;
  GstVulkanQueue *queue;
  GstVulkanFullScreenQuad *quad;
  pl_vulkan impl;
};

struct _GstPlaceboVulkanClass
{
  GstPlaceboAPIClass api_class;
};

GType gst_placebo_vulkan_get_type ();

GstPlaceboVulkan * gst_placebo_vulkan_new (GstPlacebo * placebo);

#endif /* _GST_PLACEBO_VULKAN_H_ */
