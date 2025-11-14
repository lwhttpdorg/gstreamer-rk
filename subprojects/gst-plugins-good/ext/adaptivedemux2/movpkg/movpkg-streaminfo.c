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

#include "movpkg-streaminfo.h"

#include <gst/gst.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#define XML_FILENAME "StreamInfoBoot.xml"

typedef struct
{
  GList *list;
  gboolean has_sequence_numbers;
} MovpkgMediaSegments;

typedef struct
{
  GList *list;
} MovpkgMediaInitSegments;

typedef struct
{
  gchar *network_url;
  gchar *path_to_local_copy;
} MovpkgMediaPlaylist;

typedef struct
{
  GList *list;
} MovpkgMediaTypeList;

struct _GstMovpkgPersistentStreamInfo
{
  gchar *version;
  gboolean complete;
  gdouble peak_bandwidth;
  gboolean compressable;
  MovpkgMediaPlaylist playlist;
  MovpkgMediaTypeList media_type_list;
  GstMovpkgPersistentStreamInfoType stream_info_type;
  gchar *eviction_policy;
  MovpkgMediaSegments segments;
  MovpkgMediaInitSegments init_segments;
  gchar *unique_identifier;
  gsize media_bytes_stored;
};

typedef gboolean (*StreamInfoNodeFunc) (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);

typedef struct
{
  const gchar *tag;
  StreamInfoNodeFunc func;
} StreamInfoNodeHandler;

static gboolean parse_version (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_complete (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_peak_bandwidth (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_compressable (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_media_playlist (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_media_type_list (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_stream_info_type (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_eviction_policy (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_media_segments (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_media_init_segments (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_media_bytes_stored (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);
static gboolean parse_unique_identifier (GstMovpkgPersistentStreamInfo * self,
    xmlNode * node);

static const StreamInfoNodeHandler stream_info_handlers[] = {
  {.tag = "Version",.func = parse_version},
  {.tag = "Complete",.func = parse_complete},
  {.tag = "PeakBandwidth",.func = parse_peak_bandwidth},
  {.tag = "Compressable",.func = parse_compressable},
  {.tag = "MediaPlaylist",.func = parse_media_playlist},
  {.tag = "MediaTypeList",.func = parse_media_type_list},
  {.tag = "Type",.func = parse_stream_info_type},
  {.tag = "EvictionPolicy",.func = parse_eviction_policy},
  {.tag = "MediaSegments",.func = parse_media_segments},
  {.tag = "MediaInitializationSegments",.func = parse_media_init_segments},
  {.tag = "MediaBytesStored",.func = parse_media_bytes_stored},
  {.tag = "UniqueIdentifier",.func = parse_unique_identifier},
};

static gboolean
parse_stream_info_node (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  if (node->type != XML_ELEMENT_NODE) {
    return FALSE;
  }
  for (guint i = 0; i < G_N_ELEMENTS (stream_info_handlers); i++) {
    const StreamInfoNodeHandler *handler = &stream_info_handlers[i];
    if (xmlStrcmp (node->name, (const xmlChar *) handler->tag)) {
      continue;
    }
    if (!handler->func) {
      GST_WARNING ("no handler registered for %s", handler->tag);
      break;
    }
    return handler->func (self, node);
  }
  GST_ERROR ("unexpected tag `%s'", node->name);
  return FALSE;
}

static gboolean
parse_root (GstMovpkgPersistentStreamInfo ** self, xmlNode * root)
{
  GstMovpkgPersistentStreamInfo *info =
      g_new0 (GstMovpkgPersistentStreamInfo, 1);

  for (xmlNode * child = xmlFirstElementChild (root); child != NULL;
      child = xmlNextElementSibling (child)) {
    parse_stream_info_node (info, child);
  }

  *self = g_steal_pointer (&info);
  return TRUE;
}

gboolean
gst_movpkg_parse_stream_info (GstMovpkgPersistentStreamInfo ** info,
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

  if (xmlStrcmp (root->name, (const xmlChar *) "StreamInfo") != 0) {
    GST_ERROR ("root node in " XML_FILENAME "must be StreamInfo");
    goto err;
  }

  gboolean result = parse_root (info, root);

  g_clear_pointer (&doc, xmlFreeDoc);

  return result;

err:
  g_clear_pointer (&doc, xmlFreeDoc);
  return FALSE;
}

static gboolean
parse_version (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  self->version = (gchar *) xmlNodeGetContent (node);
  return TRUE;
}

static gboolean
parse_yesno (xmlNode * node)
{
  xmlChar *content = xmlNodeGetContent (node);
  gboolean value = xmlStrcmp (content, (const xmlChar *) "YES") == 0;
  g_clear_pointer (&content, xmlFree);
  return value;
}

static gboolean
parse_complete (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  self->complete = parse_yesno (node);
  return TRUE;
}

static gboolean
parse_peak_bandwidth (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  xmlChar *content = xmlNodeGetContent (node);
  self->peak_bandwidth = g_ascii_strtod ((gchar *) content, NULL);
  g_clear_pointer (&content, xmlFree);
  return TRUE;
}

static gboolean
parse_compressable (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  self->compressable = parse_yesno (node);
  return TRUE;
}

static gboolean
parse_media_playlist (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  for (xmlNode * child = xmlFirstElementChild (node); child != NULL;
      child = xmlNextElementSibling (child)) {
    if (xmlStrcmp (child->name, (const xmlChar *) "NetworkURL") == 0) {
      g_clear_pointer (&self->playlist.network_url, g_free);
      self->playlist.network_url = (gchar *) xmlNodeGetContent (child);
      continue;
    }
    if (xmlStrcmp (child->name, (const xmlChar *) "PathToLocalCopy") == 0) {
      g_clear_pointer (&self->playlist.path_to_local_copy, g_free);
      self->playlist.path_to_local_copy = (gchar *) xmlNodeGetContent (child);
      continue;
    }
  }
  return TRUE;
}

static gboolean
parse_media_type (MovpkgMediaTypeList * media_types, xmlNode * node)
{
  GstMovpkgMediaType type = {
    .type = (gchar *) xmlGetProp (node, (const xmlChar *) "type"),
  };
  media_types->list = g_list_prepend (media_types->list,
      g_memdup2 (&type, sizeof (GstMovpkgMediaType)));
  return TRUE;
}

static gboolean
parse_media_type_list (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  for (xmlNode * child = xmlFirstElementChild (node); child != NULL;
      child = xmlNextElementSibling (child)) {
    if (xmlStrcmp (child->name, (const xmlChar *) "MediaType")) {
      continue;
    }
    parse_media_type (&self->media_type_list, child);
  }

  self->media_type_list.list = g_list_reverse (self->media_type_list.list);

  return TRUE;
}

static gboolean
parse_stream_info_type (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  xmlChar *content = xmlNodeGetContent (node);
  if (xmlStrcmp (content, (const xmlChar *) "Main") == 0) {
    self->stream_info_type = GST_MOVPKG_PERSISTENT_STREAM_INFO_TYPE_MAIN;
  } else if (xmlStrcmp (content, (const xmlChar *) "Supplemental") == 0) {
    self->stream_info_type =
        GST_MOVPKG_PERSISTENT_STREAM_INFO_TYPE_SUPPLEMENTAL;
  } else {
    self->stream_info_type = GST_MOVPKG_PERSISTENT_STREAM_INFO_TYPE_NONE;
  }
  g_clear_pointer (&content, xmlFree);
  return TRUE;
}

static gboolean
parse_eviction_policy (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  self->eviction_policy = (gchar *) xmlNodeGetContent (node);
  return TRUE;
}

static GstClockTime
parse_time_seconds (xmlNode * node, const gchar * name)
{
  xmlChar *text = xmlGetProp (node, (const xmlChar *) name);
  if (text == NULL) {
    return GST_CLOCK_TIME_NONE;
  }
  gdouble seconds = g_ascii_strtod ((const gchar *) text, NULL);
  g_clear_pointer (&text, xmlFree);
  return (GstClockTime) GST_SECOND *seconds;
}

static gsize
parse_size (xmlNode * node, const gchar * name)
{
  xmlChar *text = xmlGetProp (node, (const xmlChar *) name);
  if (text == NULL) {
    return 0;
  }
  gsize size = g_ascii_strtoull ((const gchar *) text, NULL, 10);
  g_clear_pointer (&text, xmlFree);
  return size;
}

static gssize
parse_ssize (xmlNode * node, const gchar * name, gssize default_value)
{
  xmlChar *text = xmlGetProp (node, (const xmlChar *) name);
  if (text == NULL) {
    return default_value;
  }
  gssize size = g_ascii_strtoll ((const gchar *) text, NULL, 10);
  g_clear_pointer (&text, xmlFree);
  return size;
}

static gboolean
parse_media_segment (MovpkgMediaSegments * segments, xmlNode * node)
{
  if (xmlStrcmp (node->name, (const xmlChar *) "SEG")) {
    return FALSE;
  }
  xmlChar *url = xmlGetProp (node, (const xmlChar *) "URL");
  if (url == NULL) {
    return FALSE;
  }
  xmlChar *path = xmlGetProp (node, (const xmlChar *) "PATH");
  if (path == NULL) {
    g_clear_pointer (&url, xmlFree);
    return FALSE;
  }

  GstMovpkgMediaSegment segment = {
    .url = (gchar *) url,
    .path = (gchar *) path,
    .time = parse_time_seconds (node, "Tim"),
    .duration = parse_time_seconds (node, "Dur"),
    .offset = parse_size (node, "Off"),
    .length = parse_size (node, "Len"),
    .sequence_number = parse_ssize (node, "SeqNum", -1),
  };

  segments->has_sequence_numbers &= segment.sequence_number >= 0;
  segments->list = g_list_prepend (segments->list, g_memdup2 (&segment,
          sizeof (GstMovpkgMediaSegment)));

  return TRUE;
}

static gint
compare_segment_seqnum (gconstpointer a, gconstpointer b)
{
  const GstMovpkgMediaSegment *a_segment = a;
  const GstMovpkgMediaSegment *b_segment = b;
  if (a_segment->sequence_number < b_segment->sequence_number) {
    return -1;
  }
  if (a_segment->sequence_number > b_segment->sequence_number) {
    return +1;
  }
  return 0;
}

static gboolean
parse_media_segments (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  self->segments.has_sequence_numbers = TRUE;
  for (xmlNode * child = xmlFirstElementChild (node); child != NULL;
      child = xmlNextElementSibling (child)) {
    parse_media_segment (&self->segments, child);
  }
  if (self->segments.has_sequence_numbers) {
    self->segments.list =
        g_list_sort (self->segments.list, compare_segment_seqnum);
  } else {
    self->segments.list = g_list_reverse (self->segments.list);
    guint i = 0;
    for (GList * it = self->segments.list; it != NULL; it = it->next) {
      GstMovpkgMediaSegment *segment = (GstMovpkgMediaSegment *) it->data;
      segment->sequence_number = i;
      i++;
    }
    self->segments.has_sequence_numbers = TRUE;
  }
  return TRUE;
}

static gboolean
parse_media_init_segment (MovpkgMediaInitSegments * segments, xmlNode * node)
{
  if (xmlStrcmp (node->name, (const xmlChar *) "ISEG")) {
    return FALSE;
  }
  xmlChar *url = xmlGetProp (node, (const xmlChar *) "URL");
  if (url == NULL) {
    return FALSE;
  }
  xmlChar *path = xmlGetProp (node, (const xmlChar *) "PATH");
  if (path == NULL) {
    g_clear_pointer (&url, xmlFree);
    return FALSE;
  }

  GstMovpkgMediaInitSegment segment = {
    .url = (gchar *) url,
    .path = (gchar *) path,
    .offset = parse_size (node, "Off"),
    .length = parse_size (node, "Len"),
    .sequence_number = parse_ssize (node, "SeqNum", -1),
  };

  segments->list = g_list_prepend (segments->list, g_memdup2 (&segment,
          sizeof (GstMovpkgMediaInitSegment)));

  return TRUE;
}

static gint
compare_init_segment_seqnum (gconstpointer a, gconstpointer b)
{
  const GstMovpkgMediaInitSegment *a_segment = a;
  const GstMovpkgMediaInitSegment *b_segment = b;
  if (a_segment->sequence_number < b_segment->sequence_number) {
    return -1;
  }
  if (a_segment->sequence_number > b_segment->sequence_number) {
    return +1;
  }
  return 0;
}

static gboolean
parse_media_init_segments (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  for (xmlNode * child = xmlFirstElementChild (node); child != NULL;
      child = xmlNextElementSibling (child)) {
    parse_media_init_segment (&self->init_segments, child);
  }
  self->init_segments.list =
      g_list_sort (self->init_segments.list, compare_init_segment_seqnum);
  return TRUE;
}

static gboolean
parse_media_bytes_stored (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  xmlChar *content = xmlNodeGetContent (node);
  self->media_bytes_stored = g_ascii_strtoull ((gchar *) content, NULL, 10);
  g_clear_pointer (&content, xmlFree);
  return TRUE;
}

static gboolean
parse_unique_identifier (GstMovpkgPersistentStreamInfo * self, xmlNode * node)
{
  self->unique_identifier = (gchar *) xmlNodeGetContent (node);
  return TRUE;
}

void
gst_movpkg_media_segment_free (GstMovpkgMediaSegment * self)
{
  g_clear_pointer (&self->path, xmlFree);
  g_clear_pointer (&self->url, xmlFree);
  g_free (self);
}

void
gst_movpkg_media_init_segment_free (GstMovpkgMediaInitSegment * self)
{
  g_clear_pointer (&self->path, xmlFree);
  g_clear_pointer (&self->url, xmlFree);
  g_free (self);
}

static void
clear_segments (MovpkgMediaSegments * segments)
{
  g_list_free_full (segments->list,
      (GDestroyNotify) gst_movpkg_media_segment_free);
}

static void
clear_init_segments (MovpkgMediaInitSegments * segments)
{
  g_list_free_full (segments->list,
      (GDestroyNotify) gst_movpkg_media_init_segment_free);
}

static void
clear_playlist (MovpkgMediaPlaylist * playlist)
{
  g_clear_pointer (&playlist->network_url, g_free);
  g_clear_pointer (&playlist->path_to_local_copy, g_free);
}

static void
clear_media_type_list (MovpkgMediaTypeList * media_type_list)
{
  g_list_free_full (media_type_list->list, xmlFree);
}

void
gst_movpkg_stream_info_free (GstMovpkgPersistentStreamInfo * self)
{
  g_clear_pointer (&self->eviction_policy, xmlFree);
  g_clear_pointer (&self->version, xmlFree);
  g_clear_pointer (&self->unique_identifier, xmlFree);
  clear_segments (&self->segments);
  clear_init_segments (&self->init_segments);
  clear_playlist (&self->playlist);
  clear_media_type_list (&self->media_type_list);
}

gboolean
gst_movpkg_stream_info_is_complete (GstMovpkgPersistentStreamInfo * self)
{
  return self->complete;
}

GstIterator *
gst_movpkg_stream_info_iter_segments (GstMovpkgPersistentStreamInfo * self,
    GMutex * mutex, guint32 * cookie, GObject * parent)
{
  return gst_iterator_new_list (G_TYPE_POINTER, mutex, cookie,
      &self->segments.list, parent, NULL);
}

GstIterator *
gst_movpkg_stream_info_iter_init_segments (GstMovpkgPersistentStreamInfo * self,
    GMutex * mutex, guint32 * cookie, GObject * parent)
{
  return gst_iterator_new_list (G_TYPE_POINTER, mutex, cookie,
      &self->init_segments.list, parent, NULL);
}

GstMovpkgMediaInitSegment *
gst_movpkg_stream_info_find_init_segment (GstMovpkgPersistentStreamInfo * self,
    gssize sequence_number)
{
  if (sequence_number < 0) {
    return NULL;
  }
  GstMovpkgMediaInitSegment *last_segment = NULL;
  for (GList * it = self->init_segments.list; it != NULL; it = it->next) {
    GstMovpkgMediaInitSegment *segment = it->data;
    if (segment->sequence_number == sequence_number) {
      return segment;
    }
    if (segment->sequence_number < sequence_number) {
      last_segment = segment;
      continue;
    }
    if (it->prev == NULL) {
      return NULL;
    }
    return it->prev->data;
  }
  return last_segment;
}

GstIterator *
gst_movpkg_stream_info_iter_media_types (GstMovpkgPersistentStreamInfo * self,
    GMutex * mutex, guint32 * cookie, GObject * parent)
{
  return gst_iterator_new_list (G_TYPE_POINTER, mutex, cookie,
      &self->media_type_list.list, parent, NULL);
}
