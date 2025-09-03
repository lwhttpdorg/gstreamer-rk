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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvaobjectpool-private.h"

#define GST_CAT_DEFAULT gst_debug_va_object_pool
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define parent_class gst_va_object_pool_parent_class

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstVaObjectPool, gst_va_object_pool,
    GST_TYPE_OBJECT, GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vaobjectpool",
        0, "VA object pool"));

static VAGenericID
gst_va_object_pool_default_alloc (GstVaObjectPool * pool)
{
  return VA_INVALID_ID;
}

static VAGenericID
gst_va_object_pool_default_acquire (GstVaObjectPool * pool)
{
  VAGenericID ret;

  GST_OBJECT_LOCK (pool);
  if (pool->available->len > 0) {
    ret = g_array_index (pool->available, VAGenericID, 0);
    pool->available = g_array_remove_index_fast (pool->available, 0);
  } else {
    ret = gst_va_object_pool_alloc (pool);
  }

  if (ret != VA_INVALID_ID) {
    g_array_append_val (pool->outstanding, ret);

#if defined(GST_ENABLE_EXTRA_CHECKS)
    if (pool->outstanding->len > GST_VA_OBJECT_POOL_LARGE_OUTSTANDING) {
      g_critical ("%s: There are a large number of objects outsanding:"
          "This usually means there is a reference counting issue somewhere",
          GST_OBJECT_NAME (pool));
    }
#endif
  }
  GST_OBJECT_UNLOCK (pool);

  return ret;
}

static void
gst_va_object_pool_default_release (GstVaObjectPool * pool, VAGenericID object)
{
  GST_OBJECT_LOCK (pool);
  for (int i = 0; i < pool->outstanding->len; i++) {
    if (g_array_index (pool->outstanding, VAGenericID, i) == object) {
      pool->outstanding = g_array_remove_index_fast (pool->outstanding, i);
      g_array_append_val (pool->available, object);
      GST_OBJECT_UNLOCK (pool);
      return;
    }
  }
  GST_OBJECT_UNLOCK (pool);

  g_warning ("%s: Attempt was made to release an object (0x%x) that does not "
      "belong to us", GST_OBJECT_NAME (pool), object);
}

static void
gst_va_object_pool_default_free (GstVaObjectPool * pool, VAGenericID object)
{
}

static void
gst_va_object_pool_dispose (GObject * object)
{
  GstVaObjectPool *pool = GST_VA_OBJECT_POOL (object);
  GstVaObjectPoolClass *klass = GST_VA_OBJECT_POOL_GET_CLASS (pool);

  if (pool->outstanding) {
    g_warn_if_fail (pool->outstanding->len <= 0);
    g_array_unref (pool->outstanding);
  }
  pool->outstanding = NULL;

  if (pool->available) {
    for (int i = 0; i < pool->available->len; i++)
      klass->free (pool, g_array_index (pool->available, VAGenericID, i));

    g_array_unref (pool->available);
  }
  pool->available = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_va_object_pool_finalize (GObject * object)
{
  GstVaObjectPool *pool = GST_VA_OBJECT_POOL (object);

  gst_clear_object (&pool->display);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_va_object_pool_init (GstVaObjectPool * pool)
{
  pool->outstanding = g_array_sized_new (FALSE, FALSE, sizeof (VAGenericID), 8);
  pool->available = g_array_sized_new (FALSE, FALSE, sizeof (VAGenericID), 8);
}

static void
gst_va_object_pool_class_init (GstVaObjectPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->dispose = gst_va_object_pool_dispose;
  gobject_class->finalize = gst_va_object_pool_finalize;

  klass->alloc = gst_va_object_pool_default_alloc;
  klass->acquire = gst_va_object_pool_default_acquire;
  klass->release = gst_va_object_pool_default_release;
  klass->free = gst_va_object_pool_default_free;
}

VAGenericID
gst_va_object_pool_alloc (GstVaObjectPool * pool)
{
  GstVaObjectPoolClass *klass;

  g_return_val_if_fail (GST_IS_VA_OBJECT_POOL (pool), VA_INVALID_ID);
  klass = GST_VA_OBJECT_POOL_GET_CLASS (pool);
  g_return_val_if_fail (klass->alloc != NULL, VA_INVALID_ID);

  return klass->alloc (pool);
}

VAGenericID
gst_va_object_pool_acquire (GstVaObjectPool * pool)
{
  GstVaObjectPoolClass *klass;

  g_return_val_if_fail (GST_IS_VA_OBJECT_POOL (pool), VA_INVALID_ID);
  klass = GST_VA_OBJECT_POOL_GET_CLASS (pool);
  g_return_val_if_fail (klass->acquire != NULL, VA_INVALID_ID);

  return klass->acquire (pool);
}

void
gst_va_object_pool_release (GstVaObjectPool * pool, VAGenericID object)
{
  GstVaObjectPoolClass *klass;

  g_return_if_fail (GST_IS_VA_OBJECT_POOL (pool));
  klass = GST_VA_OBJECT_POOL_GET_CLASS (pool);
  g_return_if_fail (klass->release != NULL);

  klass->release (pool, object);
}

guint
gst_va_object_pool_length (GstVaObjectPool * pool)
{
  guint size;

  g_return_val_if_fail (GST_IS_VA_OBJECT_POOL (pool), -1);

  GST_OBJECT_LOCK (pool);
  size = pool->available->len + pool->outstanding->len;
  GST_OBJECT_UNLOCK (pool);
  return size;
}
