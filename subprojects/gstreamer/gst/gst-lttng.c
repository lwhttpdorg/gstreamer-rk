/*
 * Copyright (C) 2025 ekwange <ekwange@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define TRACEPOINT_CREATE_PROBES
#define TRACEPOINT_DEFINE

#include "gst-lttng.h"
#include "gst_private.h"
#include "gstinfo.h"

GST_DEBUG_CATEGORY_STATIC (gst_lttng_debug);
#define GST_CAT_DEFAULT gst_lttng_debug

static void
gst_lttng_log_function (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line,
    GObject * object, GstDebugMessage * message, gpointer user_data)
{
  gchar *obj_str;
  const gchar *message_str;

  if (category == gst_lttng_debug) {
    return;
  }

  if (object) {
    if (GST_IS_OBJECT (object)) {
      obj_str =
          g_strdup_printf ("<%s>", GST_OBJECT_NAME (GST_OBJECT (object)));
    } else {
      obj_str = g_strdup_printf ("<%p>", object);
    }
  } else {
    obj_str = g_strdup ("");
  }

  message_str = gst_debug_message_get (message);

  GST_TRACE ("LTTNG: %s:%d %s", function, line, message_str);

  tracepoint (gst_lttng, gst_log, _gst_getpid (), object,
      (gchar *) gst_debug_category_get_name (category), level, (gchar *) file,
      (gchar *) function, line, obj_str, (gchar *) message_str);

  g_free (obj_str);
}

void
_gst_lttng_init (void)
{
  if (g_getenv ("GST_DEBUG_LTTNG")) {
    GST_DEBUG_CATEGORY_INIT (gst_lttng_debug, "lttng", 0, "LTTng integration");
    GST_INFO ("enabled");
    gst_debug_add_log_function (gst_lttng_log_function, NULL, NULL);
  }
}
