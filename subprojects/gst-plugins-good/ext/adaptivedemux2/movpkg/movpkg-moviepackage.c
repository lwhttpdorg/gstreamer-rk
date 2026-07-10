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

#include "movpkg-moviepackage.h"

#include "gstmovpkglogging-private.h"

#include <libxml/parser.h>
#include <libxml/tree.h>

#define XML_FILENAME "root.xml"

struct _GstMovpkgMoviePackage
{
  gchar *version;
  GstMovpkgMoviePackageType package_type;
  gchar *boot_image;
};

typedef gboolean (*MoviePackageNodeFunc) (GstMovpkgMoviePackage * self,
    xmlNode * node);

typedef struct
{
  const gchar *tag;
  MoviePackageNodeFunc func;
} MoviePackageNodeHandler;

static gboolean parse_version (GstMovpkgMoviePackage * self, xmlNode * node);
static gboolean parse_movie_package_type (GstMovpkgMoviePackage * self,
    xmlNode * node);
static gboolean parse_boot_image (GstMovpkgMoviePackage * self, xmlNode * node);

static const MoviePackageNodeHandler handlers[] = {
  {.tag = "Version",.func = parse_version},
  {.tag = "MoviePackageType",.func = parse_movie_package_type},
  {.tag = "BootImage",.func = parse_boot_image},
};

static gboolean
parse_node (GstMovpkgMoviePackage * self, xmlNode * node)
{
  if (node->type != XML_ELEMENT_NODE) {
    return FALSE;
  }
  for (guint i = 0; i < G_N_ELEMENTS (handlers); i++) {
    const MoviePackageNodeHandler *handler = &handlers[i];
    if (xmlStrcmp (node->name, (xmlChar *) handler->tag)) {
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
parse_root (GstMovpkgMoviePackage ** self, xmlNode * root)
{
  GstMovpkgMoviePackage *pkg = g_new0 (GstMovpkgMoviePackage, 1);

  for (xmlNode * child = xmlFirstElementChild (root); child != NULL;
      child = xmlNextElementSibling (child)) {
    parse_node (pkg, child);
  }

  *self = g_steal_pointer (&pkg);
  return TRUE;
}

gboolean
gst_movpkg_parse_movie_package (GstMovpkgMoviePackage ** pkg,
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

  if (xmlStrcmp (root->name, (xmlChar *) "MoviePackage") != 0) {
    GST_ERROR ("root node in " XML_FILENAME " must be MoviePackage");
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
parse_version (GstMovpkgMoviePackage * self, xmlNode * node)
{
  self->version = (gchar *) xmlNodeGetContent (node);
  return TRUE;
}

static gboolean
parse_movie_package_type (GstMovpkgMoviePackage * self, xmlNode * node)
{
  xmlChar *content = xmlNodeGetContent (node);
  if (xmlStrcmp (content, (xmlChar *) "HLS") == 0) {
    self->package_type = GST_MOVPKG_MOVIE_PACKAGE_TYPE_HLS;
    goto ok;
  }

  self->package_type = GST_MOVPKG_MOVIE_PACKAGE_TYPE_NONE;

  g_clear_pointer (&content, xmlFree);
  return FALSE;

ok:
  g_clear_pointer (&content, xmlFree);
  return TRUE;
}

static gboolean
parse_boot_image (GstMovpkgMoviePackage * self, xmlNode * node)
{
  self->boot_image = (gchar *) xmlNodeGetContent (node);
  return TRUE;
}

void
gst_movpkg_movie_package_free (GstMovpkgMoviePackage * self)
{
  g_clear_pointer (&self->version, xmlFree);
  g_clear_pointer (&self->boot_image, xmlFree);
  g_free (self);
}

const gchar *
gst_movpkg_movie_package_get_boot_image (GstMovpkgMoviePackage * self)
{
  return self->boot_image;
}
