/* GStreamer
 *
 * SPDX-License-Identifier: LGPL-2.1
 *
 * Copyright (C) 2024-2025 Collabora Ltd.
 *  @author: Jordan Yelloz <jordan.yelloz@collabora.com>
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
 * SECTION:element-movpkgdemux
 * @title: movpkgdemux
 *
 * Movpkg demuxer element
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 playbin3 uri=file:///path/to/movie.movpkg/root.xml
 * ]|
 *
 * Since: 1.28
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstmovpkgelements.h"
#include "gstadaptivedemuxelements.h"
#include "gstadaptivedemux.h"
#include "gstmovpkgdemux.h"
#include "movpkg-moviepackage.h"
#include "movpkg-hlsmoviepackage.h"
#include "movpkg-streaminfo.h"
#include "movpkg-chapters-json.h"
#include "gstmovpkgdemux-stream.h"
#include "hls/m3u8.h"

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-movpkg"));

GST_DEBUG_CATEGORY (gst_movpkg_demux_debug);
#define GST_CAT_DEFAULT gst_movpkg_demux_debug

struct _GstMovpkgDemux
{
  GstAdaptiveDemux parent;
  gchar *base_path;
  GstMovpkgMoviePackage *pkg;
  GstMovpkgHLSMoviePackage *hls_pkg;
  GstHLSMasterPlaylist *master_playlist;
  GstToc *chapters;
  GstClockTime duration;
};

static gboolean gst_movpkg_demux_process_manifest (GstAdaptiveDemux * demux,
    GstBuffer * buf);
static gboolean gst_movpkg_demux_seek (GstAdaptiveDemux * demux,
    GstEvent * seek);
static GstClockTime gst_movpkg_demux_get_duration (GstAdaptiveDemux * demux);
static gboolean
gst_movpkg_demux_requires_periodical_playlist_update (GstAdaptiveDemux * demux);

static gboolean movpkgdemux_element_init (GstPlugin * plugin);

GST_ELEMENT_REGISTER_DEFINE_CUSTOM (movpkgdemux, movpkgdemux_element_init);

#define gst_movpkg_demux_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstMovpkgDemux, gst_movpkg_demux,
    GST_TYPE_ADAPTIVE_DEMUX, movpkg_element_init ());

static void
gst_movpkg_demux_finalize (GObject * obj)
{
  GstMovpkgDemux *self = GST_MOVPKG_DEMUX (obj);
  g_clear_pointer (&self->pkg, gst_movpkg_movie_package_free);
  g_clear_pointer (&self->hls_pkg, gst_movpkg_hls_movie_package_free);
  g_clear_pointer (&self->master_playlist, hls_master_playlist_unref);
  gst_clear_mini_object ((GstMiniObject **) & self->chapters);
  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_movpkg_demux_class_init (GstMovpkgDemuxClass * klass)
{
  GObjectClass *oclass = (GObjectClass *) klass;
  GstElementClass *eclass = (GstElementClass *) klass;
  GstAdaptiveDemuxClass *dclass = (GstAdaptiveDemuxClass *) klass;

  oclass->finalize = gst_movpkg_demux_finalize;

  gst_element_class_add_static_pad_template (eclass, &sink_template);

  gst_element_class_set_static_metadata (eclass,
      "Movpkg Demuxer",
      "Codec/Demuxer/Adaptive",
      "Movpkg Demuxer", "Jordan Yelloz <jordan.yelloz@collabora.com>");

  dclass->process_manifest = gst_movpkg_demux_process_manifest;
  dclass->seek = gst_movpkg_demux_seek;
  dclass->get_duration = gst_movpkg_demux_get_duration;
  dclass->requires_periodical_playlist_update =
      gst_movpkg_demux_requires_periodical_playlist_update;
}

static void
gst_movpkg_demux_init (GstMovpkgDemux * self)
{
  self->duration = GST_CLOCK_TIME_NONE;
  self->pkg = NULL;
  self->hls_pkg = NULL;
  self->master_playlist = NULL;
  self->chapters = NULL;
}

static gchar *
build_base_uri (const gchar * root_uri)
{
  return gst_uri_join_strings (root_uri, "./");
}

static gboolean
parse_boot_manifest (GstMovpkgDemux * self)
{
  const gchar *boot_filename =
      gst_movpkg_movie_package_get_boot_image (self->pkg);
  gchar *boot_path = g_build_filename (self->base_path, boot_filename, NULL);
  GST_LOG_OBJECT (self, "manifest is at `%s'", boot_path);
  gchar *data = NULL;
  gsize size = 0;
  if (!g_file_get_contents (boot_path, &data, &size, NULL)) {
    GST_ERROR_OBJECT (self, "no manifest at `%s'", boot_path);
    goto err;
  }
  if (!gst_movpkg_parse_hls_movie_package (&self->hls_pkg, data, size, NULL)) {
    GST_ERROR_OBJECT (self, "failed to parse manifest data from `%s'",
        boot_path);
    goto err;
  }

  g_clear_pointer (&boot_path, g_free);
  g_clear_pointer (&data, g_free);
  return TRUE;

err:
  g_clear_pointer (&boot_path, g_free);
  g_clear_pointer (&data, g_free);
  return FALSE;
}

static gboolean
parse_master_playlist (GstMovpkgDemux * self)
{
  gchar *path =
      gst_movpkg_hls_movie_package_get_master_playlist_path (self->hls_pkg,
      self->base_path);
  if (path == NULL) {
    goto ok;
  }

  gchar *data = NULL;
  gsize size = 0;
  if (!g_file_get_contents (path, &data, &size, NULL)) {
    GST_ERROR_OBJECT (self, "failed to load master playlist from `%s'", path);
    goto err;
  }

  g_clear_pointer (&self->master_playlist, hls_master_playlist_unref);
  self->master_playlist =
      hls_master_playlist_new_from_data (g_steal_pointer (&data),
      gst_movpkg_hls_movie_package_get_network_url (self->hls_pkg));

ok:
  g_clear_pointer (&path, g_free);
  return TRUE;

err:
  g_clear_pointer (&path, g_free);
  return FALSE;
}

static gboolean
parse_root_manifest (GstMovpkgDemux * self, GstBuffer * buf)
{
  GstMapInfo map;
  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    return FALSE;
  }

  if (!gst_movpkg_parse_movie_package (&self->pkg, (gchar *) map.data, map.size,
          NULL)) {
    gst_buffer_unmap (buf, &map);
    GST_ERROR_OBJECT (self, "failed to parse movpkg root manifest");
    return FALSE;
  }

  GST_LOG_OBJECT (self, "parsed root manifest, boot manifest at `%s'",
      gst_movpkg_movie_package_get_boot_image (self->pkg));

  gst_buffer_unmap (buf, &map);
  return TRUE;
}

static gboolean
parse_chapters (GstMovpkgDemux * self)
{
  gchar *path =
      gst_movpkg_hls_movie_package_get_chapter_data_path (self->hls_pkg,
      self->base_path);
  if (path == NULL) {
    GST_DEBUG_OBJECT (self, "movpkg manifest has no chapter data entry");
    goto ok;
  }

  gchar *data = NULL;
  gsize size = 0;
  if (!g_file_get_contents (path, &data, &size, NULL)) {
    GST_ERROR_OBJECT (self, "failed to load chapter data from `%s'", path);
    goto err;
  }

  gst_clear_mini_object ((GstMiniObject **) & self->chapters);
  self->chapters = gst_movpkg_parse_chapters_json (data, size, NULL);

ok:
  g_clear_pointer (&path, g_free);
  return TRUE;

err:
  g_clear_pointer (&path, g_free);
  return FALSE;
}

static GstHLSRenditionStream *
find_hls_rendition (GstMovpkgDemux * self, GstMovpkgHLSMoviePackageStream
    * pkg_stream)
{
  if (self->master_playlist == NULL) {
    return NULL;
  }
  GList *renditions = self->master_playlist->renditions;
  for (GList * it = renditions; it != NULL; it = it->next) {
    GstHLSRenditionStream *rendition = it->data;
    if (g_strcmp0 (pkg_stream->network_url, rendition->uri)) {
      continue;
    }
    return rendition;
  }
  return NULL;
}

static void
add_hls_stream (const GValue * item, GstMovpkgDemux * self)
{
  GstAdaptiveDemux *demux = GST_ADAPTIVE_DEMUX (self);
  GstMovpkgHLSMoviePackageStream *pkg_stream = g_value_get_pointer (item);
  gchar *streaminfo_path = g_build_filename (self->base_path, pkg_stream->path,
      "StreamInfoBoot.xml", NULL);
  gchar *data = NULL;
  gsize size = 0;
  if (!g_file_get_contents (streaminfo_path, &data, &size, NULL)) {
    GST_ERROR_OBJECT (self, "failed to read streaminfo for %s", pkg_stream->id);
    g_clear_pointer (&streaminfo_path, g_free);
    return;
  }
  GST_LOG_OBJECT (self, "adding stream %s `%s'", pkg_stream->id,
      streaminfo_path);
  g_clear_pointer (&streaminfo_path, g_free);

  GstMovpkgPersistentStreamInfo *info = NULL;
  if (!gst_movpkg_parse_stream_info (&info, data, size, NULL)) {
    g_clear_pointer (&data, g_free);
    return;
  }
  g_clear_pointer (&data, g_free);

  if (!gst_movpkg_stream_info_is_complete (info)) {
    GST_LOG_OBJECT (self, "skipping incomplete stream %s", pkg_stream->id);
    g_clear_pointer (&info, gst_movpkg_stream_info_free);
    return;
  }

  gchar *stream_base_path = g_build_filename (self->base_path,
      pkg_stream->path, G_DIR_SEPARATOR_S, NULL);

  GstHLSRenditionStream *rendition = find_hls_rendition (self, pkg_stream);
  GstAdaptiveDemux2Stream *stream =
      gst_movpkg_demux_stream_new (pkg_stream->id, stream_base_path, info,
      rendition);
  g_clear_pointer (&stream_base_path, g_free);

  GST_LOG_OBJECT (self, "created stream %" GST_PTR_FORMAT, stream);

  if (!gst_adaptive_demux2_add_stream (demux, stream)) {
    GST_ERROR_OBJECT (self, "failed to add stream %" GST_PTR_FORMAT, stream);
    gst_clear_object (&stream);
    return;
  }

  if (self->chapters) {
    GstEvent *event = gst_event_new_toc (self->chapters, FALSE);
    gst_adaptive_demux2_stream_queue_event (stream, event);
  }

  GstClockTime stream_duration =
      gst_movpkg_demux_stream_get_duration (GST_MOVPKG_DEMUX_STREAM_CAST
      (stream));
  if (GST_CLOCK_TIME_IS_VALID (stream_duration)) {
    if (GST_CLOCK_TIME_IS_VALID (self->duration)) {
      self->duration = MAX (self->duration, stream_duration);
    } else {
      self->duration = stream_duration;
    }
  }
}

static void
add_hls_streams (GstMovpkgDemux * self)
{
  GMutex mutex;
  g_mutex_init (&mutex);
  guint32 cookie = 0;
  GstIterator *it = gst_movpkg_hls_movie_package_iter_streams (self->hls_pkg,
      &mutex, &cookie, G_OBJECT (self));
  while (gst_iterator_foreach (it, (GstIteratorForeachFunction) add_hls_stream,
          self) == GST_ITERATOR_RESYNC) {
    gst_iterator_resync (it);
  }
  g_clear_pointer (&it, gst_iterator_free);
}

static gboolean
gst_movpkg_demux_process_manifest (GstAdaptiveDemux * demux, GstBuffer * buf)
{
  GstMovpkgDemux *self = GST_MOVPKG_DEMUX (demux);

  GST_LOG_OBJECT (self, "playlist location: %s (base uri: %s)",
      demux->manifest_uri, demux->manifest_base_uri);

  gchar *base_uri = build_base_uri (demux->manifest_uri);
  GError *error = NULL;
  g_clear_pointer (&self->base_path, g_free);
  self->base_path = g_filename_from_uri (base_uri, NULL, &error);
  g_clear_pointer (&base_uri, g_free);

  if (error) {
    GST_ERROR_OBJECT (self, "failed to get filesystem path from uri: %s",
        error->message);
    g_clear_error (&error);
    return FALSE;
  }

  GST_LOG_OBJECT (self, "manifest base path %s", self->base_path);

  if (!parse_root_manifest (self, buf)) {
    GST_ERROR_OBJECT (self, "failed to parse root manifest");
    return FALSE;
  }
  if (!parse_boot_manifest (self)) {
    GST_ERROR_OBJECT (self, "failed to parse boot manifest");
    return FALSE;
  }
  if (!parse_master_playlist (self)) {
    GST_ERROR_OBJECT (self, "failed to parse master playlist");
    return FALSE;
  }
  if (!parse_chapters (self)) {
    GST_ERROR_OBJECT (self, "failed to parse chapters");
    return FALSE;
  }

  if (!gst_adaptive_demux_start_new_period (demux)) {
    GST_ERROR_OBJECT (self, "failed to start new period");
    return FALSE;
  }

  add_hls_streams (self);

  return TRUE;
}

static GstClockTime
gst_movpkg_demux_get_duration (GstAdaptiveDemux * demux)
{
  GstMovpkgDemux *self = GST_MOVPKG_DEMUX (demux);
  return self->duration;
}

static gboolean
gst_movpkg_demux_requires_periodical_playlist_update (GstAdaptiveDemux * demux)
{
  return FALSE;
}

static gboolean
movpkgdemux_element_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_movpkg_demux_debug, "movpkgdemux", 0,
      "movpkg demuxer");

  if (!adaptivedemux2_base_element_init (plugin))
    return TRUE;

  return gst_element_register (plugin, "movpkgdemux", GST_RANK_PRIMARY,
      GST_TYPE_MOVPKG_DEMUX);
}

static gboolean
gst_movpkg_demux_seek (GstAdaptiveDemux * demux, GstEvent * seek)
{
  // TODO: Add some rules to decide when seek is possible
  return TRUE;
}
