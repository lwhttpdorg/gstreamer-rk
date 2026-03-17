/* GStreamer
 * Copyright (C) 2008 Wim Taymans <wim.taymans at gmail.com>
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
/**
 * SECTION:rtsp-params
 * @short_description: Param get and set implementation
 * @see_also: #GstRTSPClient
 *
 * Last reviewed on 2026-03-19
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "rtsp-params.h"
#include "rtsp-media.h"
#include "rtsp-stream.h"

/*
 * Extracts the value of data="..."
 *
 * Returns:
 *   Newly allocated gchar* with the unquoted data value
 *   (caller must g_free()).
 *
 *   NULL if not found or malformed.
 */
static gchar *
gst_rtsp_params_extract_data_value (const gchar * src)
{
  const gchar *key = "data=\"";
  const gchar *start;
  const gchar *end;

  g_return_val_if_fail (src != NULL, NULL);

  start = g_strstr_len (src, -1, key);
  if (!start)
    return NULL;

  start += strlen (key);        /* move past data=" */

  end = strchr (start, '"');
  if (!end)
    return NULL;

  return g_strndup (start, end - start);
}

static GstMIKEYMessage *
gst_rtsp_params_create_mikey_message (const gchar * keymgmt)
{
  GstMIKEYMessage *result;
  gchar *data;
  guchar *decoded;
  gsize size;

  data = gst_rtsp_params_extract_data_value (keymgmt);
  if (!data)
    return NULL;

  GST_DEBUG ("found data '%s'", data);

  decoded = g_base64_decode_inplace (data, &size);
  result = gst_mikey_message_new_from_data (decoded, size, NULL, NULL);
  g_free (data);

  return result;
}

static GstRTSPStream *
gst_rtsp_params_get_stream_by_ssrc (GstRTSPMedia * media, guint32 ssrc)
{
  gint i;

  /* find the stream we want to update the crypto for */
  for (i = 0; i < gst_rtsp_media_n_streams (media); i++) {
    guint stream_ssrc;
    GstRTSPStream *stream;

    stream = gst_rtsp_media_get_stream (media, i);
    g_assert (stream);

    gst_rtsp_stream_get_ssrc (stream, &stream_ssrc);
    if (stream_ssrc == ssrc) {
      GST_DEBUG ("Found SSRC %u", ssrc);
      return stream;
    }
  }

  return NULL;
}

static GstRTSPStream *
gst_rtsp_params_find_stream_from_keymgmt (GstRTSPMedia * media,
    const gchar * keymgmt)
{
  const GstMIKEYMapSRTP *map;
  GstMIKEYMessage *msg;
  guint32 ssrc;

  msg = gst_rtsp_params_create_mikey_message (keymgmt);
  g_assert (msg);

  map = gst_mikey_message_get_cs_srtp (msg, 0);
  g_assert (map);
  ssrc = map->ssrc;

  GST_DEBUG ("SSRC: %u", ssrc);
  gst_mikey_message_unref (msg);

  return gst_rtsp_params_get_stream_by_ssrc (media, ssrc);
}

static gboolean
gst_rtsp_params_handle_set_parameter (GstRTSPClient * client,
    const GstRTSPMessage * request, GstRTSPContext * ctx)
{
  gchar *content_type = NULL;
  guint8 *body = NULL;
  guint body_size = 0;
  gchar **lines;
  gboolean invalid_parameter = FALSE;

  /* 1) Content-Type */
  gst_rtsp_message_get_header (request, GST_RTSP_HDR_CONTENT_TYPE,
      &content_type, 0);
  GST_DEBUG ("Content-Type: %s", content_type ? content_type : "(none)");

  /* 2) Body */
  gst_rtsp_message_get_body (request, &body, &body_size);

  if (!body || body_size == 0) {
    GST_WARNING ("No body");
    return FALSE;
  }

  gchar *body_str = g_strndup ((const gchar *) body, body_size);
  GST_DEBUG ("Raw body (%u bytes):\n%s", body_size, body_str);

  /* 3) Parse text/parameters */
  if (content_type && g_ascii_strcasecmp (content_type, "text/parameters") != 0) {
    GST_WARNING ("Unhandled Content-Type");
    g_free (body_str);
    return FALSE;
  }

  lines = g_strsplit (body_str, "\n", -1);

  for (gint i = 0; lines[i] != NULL; i++) {
    gchar *line = g_strstrip (lines[i]);

    if (*line == '\0')
      continue;

    gchar *colon = strchr (line, ':');
    if (!colon) {
      GST_WARNING ("Malformed parameter line: '%s'", line);
      continue;
    }

    *colon = '\0';

    gchar *name = g_strstrip (line);
    gchar *value = g_strstrip (colon + 1);

    GST_DEBUG ("Parameter '%s' = '%s'", name, value);

    if (g_ascii_strcasecmp (name, "KeyMgmt") != 0) {
      invalid_parameter = TRUE;
    } else {
      GstRTSPStream *stream;

      g_assert (ctx->media);
      stream = gst_rtsp_params_find_stream_from_keymgmt (ctx->media, value);
      if (stream)
        gst_rtsp_stream_handle_keymgmt (stream, value);
      else
        GST_WARNING ("Unable to find stream");
    }
  }

  g_strfreev (lines);

  g_free (body_str);
  return !invalid_parameter;
}

/**
 * gst_rtsp_params_set:
 * @client: a #GstRTSPClient
 * @ctx: (transfer none): a #GstRTSPContext
 *
 * Set parameters (not implemented yet)
 *
 * Returns: a #GstRTSPResult
 */
GstRTSPResult
gst_rtsp_params_set (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPStatusCode code = GST_RTSP_STS_OK;
  const GstRTSPMessage *request = ctx->request;
  gboolean client_managed_mikey = FALSE;

  if (ctx->media && gst_rtsp_media_is_client_managed_mikey (ctx->media))
    client_managed_mikey = TRUE;

  /* FIXME, actually parse the request based on the mime type and try to respond
   * with a list of the parameters */
  if (!client_managed_mikey || !gst_rtsp_params_handle_set_parameter (client,
          request, ctx))
    code = GST_RTSP_STS_PARAMETER_NOT_UNDERSTOOD;

  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  return GST_RTSP_OK;
}

/**
 * gst_rtsp_params_get:
 * @client: a #GstRTSPClient
 * @ctx: (transfer none): a #GstRTSPContext
 *
 * Get parameters (not implemented yet)
 *
 * Returns: a #GstRTSPResult
 */
GstRTSPResult
gst_rtsp_params_get (GstRTSPClient * client, GstRTSPContext * ctx)
{
  GstRTSPStatusCode code;

  /* FIXME, actually parse the request based on the mime type and try to respond
   * with a list of the parameters */
  code = GST_RTSP_STS_PARAMETER_NOT_UNDERSTOOD;

  gst_rtsp_message_init_response (ctx->response, code,
      gst_rtsp_status_as_text (code), ctx->request);

  return GST_RTSP_OK;
}
