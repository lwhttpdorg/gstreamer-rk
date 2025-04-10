/* This library is free software; you can redistribute it and/or
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

#include "gst_private.h"
#include "gst.h"

#include <glib-android.h>
#include <android/log.h>
#include <string.h>

/* XXX: Workaround for Android <21 making signal() an inline function
 * around bsd_signal(), and Android >= 21 not having any bsd_signal()
 * symbol but only signal().
 * See https://bugzilla.gnome.org/show_bug.cgi?id=766235
 */
static gpointer
load_real_signal (gpointer data)
{
  GModule *module;
  gpointer ret = NULL;

  module = g_module_open ("libc.so", G_MODULE_BIND_LOCAL);
  g_module_symbol (module, "signal", &ret);

  /* As fallback, let's try bsd_signal */
  if (ret == NULL) {
    g_warning ("Can't find signal(3) in libc.so!");
    g_module_symbol (module, "bsd_signal", &ret);
  }

  g_module_close (module);

  return ret;
}

__sighandler_t bsd_signal (int signum, __sighandler_t handler)
    __attribute__((weak));
__sighandler_t
bsd_signal (int signum, __sighandler_t handler)
{
  static GOnce gonce = G_ONCE_INIT;
  __sighandler_t (*real_signal) (int signum, __sighandler_t handler);

  g_once (&gonce, load_real_signal, NULL);

  real_signal = gonce.retval;
  g_assert (real_signal != NULL);

  return real_signal (signum, handler);
}

static GstClockTime _priv_gst_info_start_time;

static void
gst_debug_logcat (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line,
    GObject * object, GstDebugMessage * message, gpointer unused)
{
  GstClockTime elapsed;
  gint android_log_level;
  gchar *tag;

  if (level > gst_debug_category_get_threshold (category))
    return;

  elapsed = GST_CLOCK_DIFF (_priv_gst_info_start_time,
      gst_util_get_timestamp ());

  switch (level) {
    case GST_LEVEL_ERROR:
      android_log_level = ANDROID_LOG_ERROR;
      break;
    case GST_LEVEL_WARNING:
      android_log_level = ANDROID_LOG_WARN;
      break;
    case GST_LEVEL_FIXME:
    case GST_LEVEL_INFO:
      android_log_level = ANDROID_LOG_INFO;
      break;
    case GST_LEVEL_DEBUG:
      android_log_level = ANDROID_LOG_DEBUG;
      break;
    default:
      android_log_level = ANDROID_LOG_VERBOSE;
      break;
  }

  tag = g_strdup_printf ("GStreamer+%s",
      gst_debug_category_get_name (category));

  if (object) {
    gchar *obj;

    if (GST_IS_PAD (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s:%s>", GST_DEBUG_PAD_NAME (object));
    } else if (GST_IS_OBJECT (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s>", GST_OBJECT_NAME (object));
    } else if (G_IS_OBJECT (object)) {
      obj = g_strdup_printf ("<%s@%p>", G_OBJECT_TYPE_NAME (object), object);
    } else {
      obj = g_strdup_printf ("<%p>", object);
    }

    __android_log_print (android_log_level, tag,
        "%" GST_TIME_FORMAT " %p %s:%d:%s:%s %s\n",
        GST_TIME_ARGS (elapsed), g_thread_self (),
        file, line, function, obj, gst_debug_message_get (message));

    g_free (obj);
  } else {
    __android_log_print (android_log_level, tag,
        "%" GST_TIME_FORMAT " %p %s:%d:%s %s\n",
        GST_TIME_ARGS (elapsed), g_thread_self (),
        file, line, function, gst_debug_message_get (message));
  }
  g_free (tag);
}

void
_priv_gst_android_init_logcat_logger (void)
{
  /* Set GStreamer log handlers */
  gst_debug_remove_log_function (NULL);
  gst_debug_set_default_threshold (GST_LEVEL_WARNING);
  gst_debug_add_log_function ((GstLogFunction) gst_debug_logcat, NULL, NULL);

  /* get time we started for debugging messages */
  _priv_gst_info_start_time = gst_util_get_timestamp ();
}

static gboolean
init (JNIEnv * env, jobject context)
{
  JavaVM *vm = NULL;
  jclass context_cls = NULL;
  jmethodID get_class_loader_id = 0;
  jobject class_loader = NULL;

  if ((*env)->GetJavaVM (env, &vm) != JNI_OK)
    return FALSE;

  context_cls = (*env)->GetObjectClass (env, context);
  if (!context_cls)
    return FALSE;

  get_class_loader_id = (*env)->GetMethodID (env, context_cls,
      "getClassLoader", "()Ljava/lang/ClassLoader;");
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  class_loader = (*env)->CallObjectMethod (env, context, get_class_loader_id);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  if (!glib_java_initialize (vm, class_loader)) {
    return FALSE;
  }

  if (!g_android_set_context (context)) {
    return FALSE;
  }

  return TRUE;
}

static void
gst_android_init (JNIEnv * env, jobject context)
{
  GError *error = NULL;

  if (!init (env, context)) {
    __android_log_print (ANDROID_LOG_INFO, "GStreamer",
        "GStreamer failed to initialize");
  }

  if (gst_is_initialized ()) {
    __android_log_print (ANDROID_LOG_INFO, "GStreamer",
        "GStreamer already initialized");
    return;
  }

  if (!gst_init_check (NULL, NULL, &error)) {
    gchar *message = g_strdup_printf ("GStreamer initialization failed: %s",
        error && error->message ? error->message : "(no message)");
    jclass exception_class = (*env)->FindClass (env, "java/lang/Exception");
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer", "%s", message);
    (*env)->ThrowNew (env, exception_class, message);
    g_free (message);
    return;
  }
  __android_log_print (ANDROID_LOG_INFO, "GStreamer",
      "GStreamer initialization complete");
}

static void
gst_android_init_jni (JNIEnv * env, jobject gstreamer, jobject context)
{
  gst_android_init (env, context);
}

static JNINativeMethod native_methods[] = {
  {"nativeInit", "(Landroid/content/Context;)V", (void *) gst_android_init_jni}
};

jint
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer",
        "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "org/freedesktop/gstreamer/GStreamer");
  if (klass)
    if ((*env)->RegisterNatives (env, klass, native_methods,
                                 G_N_ELEMENTS (native_methods)))
    {
      __android_log_print (ANDROID_LOG_ERROR, "GStreamer",
                           "Could not register native methods for org.freedesktop.gstreamer.GStreamer");
      return 0;
    }

  return JNI_VERSION_1_4;
}
