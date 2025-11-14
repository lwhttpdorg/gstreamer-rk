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

#include "movpkg-chapters-json.h"

#include <gst/gst.h>
#include <json-glib/json-glib.h>

typedef struct
{
  GstToc *toc;
  GError *error;
} ForEachChapterContext;

static void
add_title (JsonArray * titles, guint index, JsonNode * node, gpointer user_data)
{
  GstTocEntry *chapter = (GstTocEntry *) user_data;
  JsonObject *title = json_node_get_object (node);
  if (title == NULL) {
    return;
  }

  const gchar *parent_id = gst_toc_entry_get_uid (chapter);

  const gchar *title_name = json_object_get_string_member (title, "title");
  const gchar *title_language =
      json_object_get_string_member_with_default (title, "language", "und");

  GstTagList *tags = gst_tag_list_new (GST_TAG_LANGUAGE_CODE, title_language,
      GST_TAG_TITLE, title_name, NULL);

  gchar *id = g_strdup_printf ("%s-%08x", parent_id, index);
  GstTocEntry *chapter_title = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_TITLE, id);
  gst_toc_entry_set_tags (chapter_title, g_steal_pointer (&tags));

  gst_toc_entry_append_sub_entry (chapter, chapter_title);

  g_clear_pointer (&id, g_free);
}

static void
add_chapter (JsonArray * chapters, guint index, JsonNode * node, gpointer
    user_data)
{
  GstToc *toc = (GstToc *) user_data;
  JsonObject *chapter = json_node_get_object (node);
  if (chapter == NULL) {
    return;
  }
  gint chapter_number =
      json_object_get_int_member_with_default (chapter, "chapter", -1);
  gdouble start_time =
      json_object_get_double_member_with_default (chapter, "start-time", -1);
  gdouble stop_time =
      json_object_get_double_member_with_default (chapter, "stop-time", -1);

  if (chapter_number < 1) {
    return;
  }

  if (start_time < 0) {
    return;
  }

  gchar *id = g_strdup_printf ("%08x", index);
  GstTocEntry *toc_chapter = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, id);
  gst_toc_entry_set_start_stop_times (toc_chapter, GST_SECOND * start_time,
      stop_time <= 0 ? GST_CLOCK_STIME_NONE : GST_SECOND * stop_time);

  JsonArray *titles = json_object_get_array_member (chapter, "titles");
  if (titles) {
    json_array_foreach_element (titles, add_title, toc_chapter);
  }

  gst_toc_append_entry (toc, toc_chapter);
  g_clear_pointer (&id, g_free);
}

GstToc *
gst_movpkg_parse_chapters_json (const gchar * data, gsize size, GError ** error)
{
  JsonParser *parser = json_parser_new ();
  GstToc *toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);

  if (!json_parser_load_from_data (parser, data, size, error)) {
    goto err;
  }

  JsonNode *root = json_parser_get_root (parser);
  JsonArray *chapters = json_node_get_array (root);
  if (chapters == NULL) {
    goto err;
  }

  json_array_foreach_element (chapters, add_chapter, toc);

  g_clear_object (&parser);

  return toc;

err:
  gst_clear_mini_object ((GstMiniObject **) & toc);
  g_clear_object (&parser);
  return NULL;
}
