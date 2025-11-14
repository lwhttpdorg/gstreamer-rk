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

#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstMovpkgHLSMoviePackage GstMovpkgHLSMoviePackage;

typedef enum {
  GST_MOVPKG_HLS_MOVIE_PACKAGE_TYPE_NONE,
  GST_MOVPKG_HLS_MOVIE_PACKAGE_TYPE_PERSISTED_STORE,
} GstMovpkgHLSMoviePackageType;

typedef struct
{
  gchar *id;
  gchar *path;
  gchar *network_url;
  gboolean complete;
} GstMovpkgHLSMoviePackageStream;

gboolean
gst_movpkg_parse_hls_movie_package (GstMovpkgHLSMoviePackage ** pkg,
                                    const gchar               * data,
                                    gsize                       size,
                                    GError                   ** error);

void
gst_movpkg_hls_movie_package_free (GstMovpkgHLSMoviePackage * self);

GstIterator *
gst_movpkg_hls_movie_package_iter_streams (GstMovpkgHLSMoviePackage * self,
                                           GMutex                   * mutex,
                                           guint32                  * cookie,
                                           GObject                  * parent);

const gchar *
gst_movpkg_hls_movie_package_get_network_url (GstMovpkgHLSMoviePackage * self);

gchar *
gst_movpkg_hls_movie_package_get_master_playlist_path (GstMovpkgHLSMoviePackage * self,
                                                       const gchar              * base_path);

gchar *
gst_movpkg_hls_movie_package_get_chapter_data_path (GstMovpkgHLSMoviePackage * self,
                                                    const gchar              * base_path);

GstIterator *
gst_movpkg_hls_movie_package_iter_data_items (GstMovpkgHLSMoviePackage * self,
                                              GMutex                   * mutex,
                                              guint32                  * cookie,
                                              GObject                  * parent);

G_END_DECLS
