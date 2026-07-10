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

#include "movpkg-hlsmoviepackage.h"

#include "gstmovpkglogging-private.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#define XML_FILENAME "boot.xml"

typedef struct
{
  GList *list;
} HLSMoviePackageStreams;

typedef struct
{
  gchar *network_url;
} HLSMasterPlaylist;

typedef struct
{
  gchar *id;
  gchar *category;
  gchar *name;
  gchar *descriptor_path;
  gchar *data_path;
  gchar *role;
} HLSDataItem;

typedef struct
{
  gchar *directory;
  GList *list;
} HLSDataItems;

struct _GstMovpkgHLSMoviePackage
{
  gchar *version;
  GstMovpkgHLSMoviePackageType package_type;
  HLSMoviePackageStreams streams;
  HLSMasterPlaylist master_playlist;
  HLSDataItems data_items;
};

typedef gboolean (*HLSMoviePackageNodeFunc) (GstMovpkgHLSMoviePackage * self,
    xmlNode * node);

typedef struct
{
  const gchar *tag;
  HLSMoviePackageNodeFunc func;
} HLSMoviePackageNodeHandler;

static gboolean parse_version (GstMovpkgHLSMoviePackage * self, xmlNode * node);
static gboolean parse_movie_package_type (GstMovpkgHLSMoviePackage * self,
    xmlNode * node);
static gboolean parse_streams (GstMovpkgHLSMoviePackage * self, xmlNode * node);
static gboolean parse_master_playlist (GstMovpkgHLSMoviePackage * self,
    xmlNode * node);
static gboolean parse_data_items (GstMovpkgHLSMoviePackage * self,
    xmlNode * node);
static void clear_master_playlist (HLSMasterPlaylist * self);
static void clear_data_item (HLSDataItem * self);

static const HLSMoviePackageNodeHandler handlers[] = {
  {.tag = "Version",.func = parse_version},
  {.tag = "HLSMoviePackageType",.func = parse_movie_package_type},
  {.tag = "Streams",.func = parse_streams},
  {.tag = "MasterPlaylist",.func = parse_master_playlist},
  {.tag = "DataItems",.func = parse_data_items},
};

static gboolean
parse_node (GstMovpkgHLSMoviePackage * self, xmlNode * node)
{
  if (node->type != XML_ELEMENT_NODE) {
    return FALSE;
  }
  for (guint i = 0; i < G_N_ELEMENTS (handlers); i++) {
    const HLSMoviePackageNodeHandler *handler = &handlers[i];
    if (xmlStrcmp (node->name, (const xmlChar *) handler->tag)) {
      continue;
    }
    if (!handler->func) {
      GST_WARNING ("no handler registered for %s", handler->tag);
      break;
    }
    return handler->func (self, node);
  }
  GST_DEBUG ("ignoring tag `%s'", node->name);
  return FALSE;
}

static gboolean
parse_root (GstMovpkgHLSMoviePackage ** self, xmlNode * root)
{
  GstMovpkgHLSMoviePackage *pkg = g_new0 (GstMovpkgHLSMoviePackage, 1);

  for (xmlNode * child = xmlFirstElementChild (root); child != NULL;
      child = xmlNextElementSibling (child)) {
    parse_node (pkg, child);
  }

  *self = g_steal_pointer (&pkg);
  return TRUE;
}

gboolean
gst_movpkg_parse_hls_movie_package (GstMovpkgHLSMoviePackage ** pkg,
    const gchar * data, gsize size, GError ** error)
{
  LIBXML_TEST_VERSION;

  xmlDocPtr doc =
      xmlReadMemory (data, size, XML_FILENAME, NULL, XML_PARSE_NONET);
  if (doc == NULL) {
    GST_ERROR ("failed to parse " XML_FILENAME);
    return FALSE;
  }

  xmlNode *root = xmlDocGetRootElement (doc);

  if (root->type != XML_ELEMENT_NODE) {
    GST_ERROR ("no root node in " XML_FILENAME);
    goto err;
  }

  if (xmlStrcmp (root->name, (const xmlChar *) "HLSMoviePackage") != 0) {
    GST_ERROR ("root node in " XML_FILENAME " must be HLSMoviePackage");
    goto err;
  }

  gboolean result = parse_root (pkg, root);

  g_clear_pointer (&doc, xmlFreeDoc);

  return result;

err:
  g_clear_pointer (&doc, xmlFreeDoc);
  return FALSE;
}

static gboolean
parse_version (GstMovpkgHLSMoviePackage * self, xmlNode * node)
{
  self->version = (gchar *) xmlNodeGetContent (node);
  return TRUE;
}

static gboolean
parse_movie_package_type (GstMovpkgHLSMoviePackage * self, xmlNode * node)
{
  xmlChar *content = xmlNodeGetContent (node);
  if (xmlStrcmp (content, (const xmlChar *) "PersistedStore") == 0) {
    self->package_type = GST_MOVPKG_HLS_MOVIE_PACKAGE_TYPE_PERSISTED_STORE;
    goto ok;
  }

  self->package_type = GST_MOVPKG_HLS_MOVIE_PACKAGE_TYPE_NONE;

  xmlFree (content);
  return FALSE;

ok:
  xmlFree (content);
  return TRUE;
}

static gboolean
parse_stream (GstMovpkgHLSMoviePackage * self, xmlNode * node)
{
  if (xmlStrcmp (node->name, (const xmlChar *) "Stream")) {
    GST_ERROR ("wrong element %s", node->name);
    return FALSE;
  }
  xmlChar *id = xmlGetProp (node, (const xmlChar *) "ID");
  xmlChar *path = xmlGetProp (node, (const xmlChar *) "Path");
  xmlChar *network_url = xmlGetProp (node, (const xmlChar *) "NetworkURL");
  if (id == NULL) {
    GST_ERROR ("missing id");
    goto err;
  }
  if (path == NULL) {
    GST_ERROR ("missing path");
    goto err;
  }
  if (network_url == NULL) {
    GST_ERROR ("missing network url");
    goto err;
  }

  GstMovpkgHLSMoviePackageStream stream = {
    .id = (gchar *) id,
    .path = (gchar *) path,
    .network_url = (gchar *) network_url,
    .complete = TRUE,
  };

  self->streams.list = g_list_prepend (self->streams.list,
      g_memdup2 (&stream, sizeof (GstMovpkgHLSMoviePackageStream)));
  return TRUE;

err:
  g_clear_pointer (&id, xmlFree);
  g_clear_pointer (&path, xmlFree);
  g_clear_pointer (&network_url, xmlFree);
  return FALSE;
}

static gboolean
parse_streams (GstMovpkgHLSMoviePackage * self, xmlNode * node)
{
  for (xmlNode * child = xmlFirstElementChild (node); child != NULL;
      child = xmlNextElementSibling (child)) {
    if (xmlStrcmp (child->name, (const xmlChar *) "Stream")) {
      continue;
    }
    parse_stream (self, child);
  }
  self->streams.list = g_list_reverse (self->streams.list);
  return TRUE;
}

static gboolean
parse_master_playlist (GstMovpkgHLSMoviePackage * self, xmlNode * node)
{
  for (xmlNode * child = xmlFirstElementChild (node); child != NULL;
      child = xmlNextElementSibling (child)) {
    if (xmlStrcmp (child->name, (const xmlChar *) "NetworkURL")) {
      continue;
    }
    clear_master_playlist (&self->master_playlist);
    self->master_playlist.network_url = (gchar *) xmlNodeGetContent (child);
  }
  return TRUE;
}

static xmlChar *
get_text_by_xpath (xmlNode * node, const gchar * child)
{
  xmlXPathContextPtr ctx = xmlXPathNewContext (node->doc);
  gchar *path = g_strdup_printf ("string(*[name() = '%s'][1])", child);
  xmlXPathObjectPtr obj = xmlXPathNodeEval (node, (const xmlChar *) path, ctx);
  xmlChar *text = xmlStrdup (obj->stringval);
  g_clear_pointer (&path, g_free);
  g_clear_pointer (&obj, xmlXPathFreeObject);
  g_clear_pointer (&ctx, xmlXPathFreeContext);
  return text;
}

static gboolean
parse_data_item_id (HLSDataItem * self, xmlNode * node)
{
  self->id = (gchar *) get_text_by_xpath (node, "ID");
  return self->id != NULL;
}

static gboolean
parse_data_item_category (HLSDataItem * self, xmlNode * node)
{
  self->category = (gchar *) get_text_by_xpath (node, "Category");
  return self->category != NULL;
}

static gboolean
parse_data_item_name (HLSDataItem * self, xmlNode * node)
{
  self->name = (gchar *) get_text_by_xpath (node, "Name");
  return self->name != NULL;
}

static gboolean
parse_data_item_descriptor_path (HLSDataItem * self, xmlNode * node)
{
  self->descriptor_path = (gchar *) get_text_by_xpath (node, "DescriptorPath");
  return self->descriptor_path != NULL;
}

static gboolean
parse_data_item_data_path (HLSDataItem * self, xmlNode * node)
{
  self->data_path = (gchar *) get_text_by_xpath (node, "DataPath");
  return self->data_path != NULL;
}

static gboolean
parse_data_item_role (HLSDataItem * self, xmlNode * node)
{
  self->role = (gchar *) get_text_by_xpath (node, "Role");
  return self->role != NULL;
}

static gboolean
parse_data_item (HLSDataItems * self, xmlNode * node)
{
  if (xmlStrcmp (node->name, (const xmlChar *) "DataItem")) {
    GST_ERROR ("wrong element %s", node->name);
    return FALSE;
  }

  HLSDataItem item = { 0 };
  if (!parse_data_item_id (&item, node)) {
    goto err;
  }
  if (!parse_data_item_category (&item, node)) {
    goto err;
  }
  if (!parse_data_item_name (&item, node)) {
    goto err;
  }
  if (!parse_data_item_descriptor_path (&item, node)) {
    goto err;
  }
  if (!parse_data_item_data_path (&item, node)) {
    goto err;
  }
  if (!parse_data_item_role (&item, node)) {
    goto err;
  }

  self->list = g_list_prepend (self->list,
      g_memdup2 (&item, sizeof (HLSDataItem)));

  return TRUE;

err:
  clear_data_item (&item);
  return FALSE;
}

static gboolean
parse_data_items (GstMovpkgHLSMoviePackage * self, xmlNode * node)
{
  for (xmlNode * child = xmlFirstElementChild (node); child != NULL;
      child = xmlNextElementSibling (child)) {
    parse_data_item (&self->data_items, child);
  }
  self->data_items.list = g_list_reverse (self->data_items.list);
  self->data_items.directory =
      (gchar *) xmlGetProp (node, (const xmlChar *) "Directory");
  return TRUE;
}

static void
clear_data_item (HLSDataItem * self)
{
  g_clear_pointer (&self->id, xmlFree);
  g_clear_pointer (&self->category, xmlFree);
  g_clear_pointer (&self->name, xmlFree);
  g_clear_pointer (&self->descriptor_path, xmlFree);
  g_clear_pointer (&self->data_path, xmlFree);
  g_clear_pointer (&self->role, xmlFree);
}

static void
free_data_item (HLSDataItem * self)
{
  clear_data_item (self);
  g_free (self);
}

static void
clear_data_items (HLSDataItems * items)
{
  g_list_free_full (items->list, (GDestroyNotify) free_data_item);
  items->list = NULL;
  g_clear_pointer (&items->directory, g_free);
}

static void
free_stream (GstMovpkgHLSMoviePackageStream * self)
{
  g_clear_pointer (&self->id, xmlFree);
  g_clear_pointer (&self->network_url, xmlFree);
  g_clear_pointer (&self->path, xmlFree);
  g_free (self);
}

static void
clear_streams (HLSMoviePackageStreams * streams)
{
  g_list_free_full (streams->list, (GDestroyNotify) free_stream);
  streams->list = NULL;
}

static void
clear_master_playlist (HLSMasterPlaylist * self)
{
  g_clear_pointer (&self->network_url, xmlFree);
}

void
gst_movpkg_hls_movie_package_free (GstMovpkgHLSMoviePackage * self)
{
  clear_streams (&self->streams);
  clear_master_playlist (&self->master_playlist);
  clear_data_items (&self->data_items);
  g_clear_pointer (&self->version, xmlFree);
}

GstIterator *
gst_movpkg_hls_movie_package_iter_streams (GstMovpkgHLSMoviePackage * self,
    GMutex * mutex, guint32 * cookie, GObject * parent)
{
  return gst_iterator_new_list (G_TYPE_POINTER, mutex, cookie,
      &self->streams.list, parent, NULL);
}

const gchar *
gst_movpkg_hls_movie_package_get_network_url (GstMovpkgHLSMoviePackage * self)
{
  return self->master_playlist.network_url;
}

static gboolean
is_master_playlist (const GValue * item, GValue * context, gpointer user_data)
{
  HLSDataItem *data_item = g_value_get_pointer (item);
  if (g_strcmp0 ("Playlist", data_item->category)
      || g_strcmp0 ("Master", data_item->role)) {
    return TRUE;
  }
  const gchar **playlist_path = (const gchar **) user_data;
  *playlist_path = data_item->data_path;
  return FALSE;
}

gchar *
gst_movpkg_hls_movie_package_get_master_playlist_path (GstMovpkgHLSMoviePackage
    * self, const gchar * base_path)
{
  GMutex mutex;
  guint32 cookie = 0;
  g_mutex_init (&mutex);
  const gchar *path = NULL;
  GstIterator *it =
      gst_movpkg_hls_movie_package_iter_data_items (self, &mutex, &cookie,
      NULL);
  while (gst_iterator_fold (it, is_master_playlist, NULL,
          &path) == GST_ITERATOR_RESYNC) {
    gst_iterator_resync (it);
  }
  g_clear_pointer (&it, gst_iterator_free);
  if (path == NULL) {
    return NULL;
  }
  return g_build_filename (base_path, self->data_items.directory, path, NULL);
}

static gboolean
is_chapter_data (const GValue * item, GValue * context, gpointer user_data)
{
  HLSDataItem *data_item = g_value_get_pointer (item);
  if (g_strcmp0 ("SessionData", data_item->category)
      || g_strcmp0 ("chaptersjson", data_item->name)) {
    return TRUE;
  }
  const gchar **path = (const gchar **) user_data;
  *path = data_item->data_path;
  return FALSE;
}

gchar *
gst_movpkg_hls_movie_package_get_chapter_data_path (GstMovpkgHLSMoviePackage
    * self, const gchar * base_path)
{
  GMutex mutex;
  guint32 cookie = 0;
  g_mutex_init (&mutex);
  const gchar *path = NULL;
  GstIterator *it =
      gst_movpkg_hls_movie_package_iter_data_items (self, &mutex, &cookie,
      NULL);
  while (gst_iterator_fold (it, is_chapter_data, NULL,
          &path) == GST_ITERATOR_RESYNC) {
    gst_iterator_resync (it);
  }
  g_clear_pointer (&it, gst_iterator_free);
  if (path == NULL) {
    return NULL;
  }
  return g_build_filename (base_path, self->data_items.directory, path, NULL);
}

GstIterator *
gst_movpkg_hls_movie_package_iter_data_items (GstMovpkgHLSMoviePackage * self,
    GMutex * mutex, guint32 * cookie, GObject * parent)
{
  return gst_iterator_new_list (G_TYPE_POINTER, mutex, cookie,
      &self->data_items.list, parent, NULL);
}
