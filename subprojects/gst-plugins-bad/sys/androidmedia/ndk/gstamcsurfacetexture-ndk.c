/*
 * Copyright (C) 2013, Fluendo S.A.
 *   Author: Andoni Morales <amorales@fluendo.com>
 *
 * Copyright (C) 2014,2018 Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@collabora.com>
 *
 * Copyright (C) 2024, Ratchanan Srirattanamet <peathot@hotmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <dlfcn.h>
#include <android/surface_texture.h>
#include <android/surface_texture_jni.h>
#include <media/NdkMediaCodec.h>

#include <gio/gio.h>

#include "gstjniutils.h"
#include "gstamc-ndk.h"
#include "gstamc-internal-ndk.h"
#include "gstamcsurfacetexture-ndk.h"
#include "../jni/gstamc-jni.h"

GST_DEBUG_CATEGORY_STATIC (gst_amc_surface_texture_ndk_debug);
#define GST_CAT_DEFAULT gst_amc_surface_texture_ndk_debug

struct _GstAmcSurfaceTextureNDK
{
  GstAmcSurfaceTextureJNI parent_instance;

  ASurfaceTexture *a_surface_texture;
};

static struct
{
  void *libandroid_handle;

  void (*release) (ASurfaceTexture * st);
  ANativeWindow *(*acquire_a_native_window) (ASurfaceTexture * st);
  int (*attach_to_gl_context) (ASurfaceTexture * st, uint32_t texName);
  int (*detach_from_gl_context) (ASurfaceTexture * st);
  int (*update_tex_image) (ASurfaceTexture * st);
  void (*get_transform_matrix) (ASurfaceTexture * st, float mtx[16]);
    int64_t (*get_timestamp) (ASurfaceTexture * st);

  ASurfaceTexture *(*from_surface_texture) (JNIEnv * env,
      jobject surfacetexture);
} a_surface_texture;

static void
gst_amc_surface_texture_ndk_initable_iface_init (GInitableIface * iface);

G_DEFINE_TYPE_WITH_CODE (GstAmcSurfaceTextureNDK, gst_amc_surface_texture_ndk,
    GST_TYPE_AMC_SURFACE_TEXTURE_JNI,
    G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
        gst_amc_surface_texture_ndk_initable_iface_init));

gboolean
gst_amc_surface_texture_ndk_static_init (void)
{
  if (a_surface_texture.libandroid_handle)
    return TRUE;

  GST_DEBUG_CATEGORY_INIT (gst_amc_surface_texture_ndk_debug,
      "amcsurfacetexture-ndk", 0, "NDK-based SurfaceTexture");

  a_surface_texture.libandroid_handle = dlopen ("libandroid.so", RTLD_NOW);
  if (!a_surface_texture.libandroid_handle)
    return FALSE;

  a_surface_texture.release =
      dlsym (a_surface_texture.libandroid_handle, "ASurfaceTexture_release");
  a_surface_texture.acquire_a_native_window =
      dlsym (a_surface_texture.libandroid_handle,
      "ASurfaceTexture_acquireANativeWindow");
  a_surface_texture.attach_to_gl_context =
      dlsym (a_surface_texture.libandroid_handle,
      "ASurfaceTexture_attachToGLContext");
  a_surface_texture.detach_from_gl_context =
      dlsym (a_surface_texture.libandroid_handle,
      "ASurfaceTexture_detachFromGLContext");
  a_surface_texture.update_tex_image =
      dlsym (a_surface_texture.libandroid_handle,
      "ASurfaceTexture_updateTexImage");
  a_surface_texture.get_transform_matrix =
      dlsym (a_surface_texture.libandroid_handle,
      "ASurfaceTexture_getTransformMatrix");
  a_surface_texture.get_timestamp =
      dlsym (a_surface_texture.libandroid_handle,
      "ASurfaceTexture_getTimestamp");
  a_surface_texture.from_surface_texture =
      dlsym (a_surface_texture.libandroid_handle,
      "ASurfaceTexture_fromSurfaceTexture");

  if (!a_surface_texture.release || !a_surface_texture.acquire_a_native_window
      || !a_surface_texture.attach_to_gl_context
      || !a_surface_texture.detach_from_gl_context
      || !a_surface_texture.update_tex_image
      || !a_surface_texture.get_transform_matrix
      || !a_surface_texture.get_timestamp
      || !a_surface_texture.from_surface_texture) {
    GST_WARNING ("Failed to get ASurfaceTexture functions");
    dlclose (a_surface_texture.libandroid_handle);
    a_surface_texture.libandroid_handle = NULL;
    return FALSE;
  }

  GST_INFO ("ASurfaceTexture is available and will be used to control "
      "Java SurfaceTexture.");
  return TRUE;
}

static gboolean
gst_amc_surface_texture_ndk_update_tex_image (GstAmcSurfaceTexture * base,
    GError ** err)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (base);
  int ret;

  ret = a_surface_texture.update_tex_image (self->a_surface_texture);
  if (ret != 0) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "ASurfaceTexture_updateTexImage() failed: %s", g_strerror (-ret));
  }
  return (ret == 0);
}

static gboolean
gst_amc_surface_texture_ndk_detach_from_gl_context (GstAmcSurfaceTexture * base,
    GError ** err)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (base);
  int ret;

  ret = a_surface_texture.detach_from_gl_context (self->a_surface_texture);
  if (ret != 0) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "ASurfaceTexture_detachFromGLContext() failed: %s", g_strerror (-ret));
  }
  return (ret == 0);
}

static gboolean
gst_amc_surface_texture_ndk_attach_to_gl_context (GstAmcSurfaceTexture * base,
    gint texture_id, GError ** err)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (base);
  int ret;

  ret =
      a_surface_texture.attach_to_gl_context (self->a_surface_texture,
      texture_id);

  if (ret != 0) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "ASurfaceTexture_attachToGLContext() failed: %s", g_strerror (-ret));
  }
  return (ret == 0);
}

static gboolean
gst_amc_surface_texture_ndk_get_transform_matrix (GstAmcSurfaceTexture * base,
    gfloat * matrix, GError ** err)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (base);

  a_surface_texture.get_transform_matrix (self->a_surface_texture, matrix);
  return TRUE;
}

static gboolean
gst_amc_surface_texture_ndk_get_timestamp (GstAmcSurfaceTexture * base,
    gint64 * result, GError ** err)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (base);

  *result = a_surface_texture.get_timestamp (self->a_surface_texture);
  return true;
}

static ANativeWindow *
gst_amc_surface_texture_ndk_acquire_native_window (GstAmcSurfaceTexture * base,
    GError ** err)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (base);

  return a_surface_texture.acquire_a_native_window (self->a_surface_texture);
}

static void
gst_amc_surface_texture_ndk_dispose (GObject * object)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (object);

  if (self->a_surface_texture)
    a_surface_texture.release (self->a_surface_texture);

  G_OBJECT_CLASS (gst_amc_surface_texture_ndk_parent_class)->dispose (object);
}

static void
gst_amc_surface_texture_ndk_class_init (GstAmcSurfaceTextureNDKClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstAmcSurfaceTextureClass *surface_texture_class =
      GST_AMC_SURFACE_TEXTURE_CLASS (klass);

  gobject_class->dispose = gst_amc_surface_texture_ndk_dispose;

  surface_texture_class->update_tex_image =
      gst_amc_surface_texture_ndk_update_tex_image;
  surface_texture_class->detach_from_gl_context =
      gst_amc_surface_texture_ndk_detach_from_gl_context;
  surface_texture_class->attach_to_gl_context =
      gst_amc_surface_texture_ndk_attach_to_gl_context;
  surface_texture_class->get_transform_matrix =
      gst_amc_surface_texture_ndk_get_transform_matrix;
  surface_texture_class->get_timestamp =
      gst_amc_surface_texture_ndk_get_timestamp;
  surface_texture_class->acquire_a_native_window =
      gst_amc_surface_texture_ndk_acquire_native_window;

  /*
   * Inherits release() and set_on_frame_available_callback() from JNI
   * parent class.
   */
}

static void
gst_amc_surface_texture_ndk_init (GstAmcSurfaceTextureNDK * self)
{
}

static gboolean
gst_amc_surface_texture_ndk_initable_init (GInitable * initable,
    GCancellable * cancellable, GError ** err)
{
  GstAmcSurfaceTextureNDK *self = GST_AMC_SURFACE_TEXTURE_NDK (initable);
  GInitableIface *initable_iface, *parent_initable_iface;
  JNIEnv *env;
  jobject java_surface_texture = NULL;

  if (!gst_amc_surface_texture_ndk_is_available ()) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "ASurfaceTexture is not supported on this device");
    return FALSE;
  }

  initable_iface =
      G_TYPE_INSTANCE_GET_INTERFACE (self, G_TYPE_INITABLE, GInitableIface);
  parent_initable_iface = g_type_interface_peek_parent (initable_iface);

  if (!parent_initable_iface->init (initable, cancellable, err))
    return FALSE;

  env = gst_amc_jni_get_env ();
  java_surface_texture =
      gst_amc_surface_texture_jni_get_jobject (GST_AMC_SURFACE_TEXTURE_JNI
      (self));

  self->a_surface_texture =
      a_surface_texture.from_surface_texture (env, java_surface_texture);
  if (!self->a_surface_texture) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to create ASurfaceTexture from Java SurfaceTexture");
    return FALSE;
  }

  return TRUE;
}

static void
gst_amc_surface_texture_ndk_initable_iface_init (GInitableIface * iface)
{
  iface->init = gst_amc_surface_texture_ndk_initable_init;
}

gboolean
gst_amc_surface_texture_ndk_is_available (void)
{
  return a_surface_texture.libandroid_handle != NULL;
}

GstAmcSurfaceTextureNDK *
gst_amc_surface_texture_ndk_new (GError ** err)
{
  if (!gst_amc_surface_texture_ndk_is_available ())
    return NULL;

  return g_initable_new (GST_TYPE_AMC_SURFACE_TEXTURE_NDK, NULL, err, NULL);
}
