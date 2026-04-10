/* GStreamer
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
 * Copyright (C) 2025 Igalia, S.L.
 *     Author: Victor Jaquez <vjaquez@igalia.com>
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

#include <gst/va/gstva.h>

G_BEGIN_DECLS

#define GST_VA_OBJECT_POOL_LARGE_OUTSTANDING 1024

GST_VA_API
GType gst_va_object_pool_get_type(void);

#define GST_TYPE_VA_OBJECT_POOL (gst_va_object_pool_get_type())
#define GST_VA_OBJECT_POOL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VA_OBJECT_POOL,GstVaObjectPool))
#define GST_VA_OBJECT_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VA_OBJECT_POOL, GstVaObjectPoolClass))
#define GST_IS_VA_OBJECT_POOL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VA_OBJECT_POOL))
#define GST_IS_VA_OBJECT_POOL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VA_OBJECT_POOL))
#define GST_VA_OBJECT_POOL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_VA_OBJECT_POOL, GstVaObjectPoolClass))

#define GST_VA_OBJECT_POOL_CAST(obj) ((GstVaObjectPool *) obj)

typedef struct _GstVaObjectPool GstVaObjectPool;
typedef struct _GstVaObjectPoolClass GstVaObjectPoolClass;

struct _GstVaObjectPool
{
  GstObject parent;

  GstVaDisplay *display;

  GArray *outstanding;
  GArray *available;
};

struct _GstVaObjectPoolClass
{
  GstObjectClass parent;

  VAGenericID (*alloc)   (GstVaObjectPool * pool);
  VAGenericID (*acquire) (GstVaObjectPool * pool);
  void        (*release) (GstVaObjectPool * pool, VAGenericID object);
  void        (*free)    (GstVaObjectPool * pool, VAGenericID object);
};

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GstVaObjectPool, gst_object_unref)

GST_VA_API
VAGenericID           gst_va_object_pool_alloc            (GstVaObjectPool * pool);

GST_VA_API
VAGenericID           gst_va_object_pool_acquire          (GstVaObjectPool * pool);

GST_VA_API
void                  gst_va_object_pool_release          (GstVaObjectPool * pool,
                                                           VAGenericID object);

GST_VA_API
guint                 gst_va_object_pool_length           (GstVaObjectPool * pool);

G_END_DECLS
