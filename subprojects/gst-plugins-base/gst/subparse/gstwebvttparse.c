/* GStreamer WebVTT parser
 * Copyright (C) 2025 Collabora Ltd.
 *
 * WebVTT cue parsing, region handling, and temporal overlap resolution.
 * Produces Pango-markup buffers annotated with positioning metadata via
 * HTML-comment headers (<!--vtt ...-->) for downstream renderers.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>

#include "gstsubparse.h"
#include "gstwebvttparse.h"

/* sub_parse_debug is declared in gstsubparseelements.h (included via gstsubparse.h) */
#define GST_CAT_DEFAULT sub_parse_debug

/* Monotonically increasing counter for unique cue identification across
 * the lifetime of the element; used for stable ordering when cues overlap. */
static guint webvtt_cue_counter = 0;

/* Register the GstCustomMeta type for WebVTT cue attributes.
 * Idempotent — safe to call multiple times. */
void
gst_webvtt_cue_meta_ensure_registered (void)
{
  static gsize registered = 0;
  if (g_once_init_enter (&registered)) {
    const gchar *tags[] = { NULL };
    gst_meta_register_custom (GST_WEBVTT_CUE_META_NAME, tags, NULL, NULL, NULL);
    g_once_init_leave (&registered, 1);
  }
}

/* ---- Cue settings parsing ---- */

/* Parse cue settings from the WebVTT timestamp line.
 * Format: "00:00:01.000 --> 00:00:02.000 vertical:rl align:start position:10%"
 * Only the part after the arrow and timestamp is passed to this function.
 * See https://www.w3.org/TR/webvtt1/#cue-settings */
void
parse_webvtt_cue_settings (ParserState * state, const gchar * settings)
{
  GstSubParse *subparse =
      state->user_data ? GST_SUBPARSE (state->user_data) : NULL;
  gchar **splitted_settings = g_strsplit_set (settings, " \t", -1);
  gint i = 0;
  gint16 text_position, text_size;
  gint16 line_position;
  gboolean vertical_found = FALSE;
  gboolean alignment_found = FALSE;

  g_free (state->region_id);
  state->region_id = NULL;
  g_free (state->position);
  state->position = NULL;
  g_free (state->line);
  state->line = NULL;
  g_free (state->vertical);
  state->vertical = NULL;
  GST_DEBUG_OBJECT (subparse, "Parsing cue settings string: '%s'", settings);
  for (i = 0; i < g_strv_length (splitted_settings); i++) {
    if (splitted_settings[i][0] == '\0') {
      GST_LOG_OBJECT (subparse, "Skipping empty setting at index %d", i);
      continue;
    }
    GST_LOG_OBJECT (subparse, "Processing setting %d: '%s'", i,
        splitted_settings[i]);
    gboolean valid_tag = FALSE;
    if (g_str_has_prefix (splitted_settings[i], "T:")) {
      if (sscanf (splitted_settings[i], "T:%" G_GINT16_FORMAT "%%",
              &text_position) > 0) {
        state->text_position = (guint8) text_position;
        valid_tag = TRUE;
        GST_DEBUG_OBJECT (subparse, "Parsed text position: %d%%",
            text_position);
      }
    } else if (g_str_has_prefix (splitted_settings[i], "vertical:") &&
        strlen (splitted_settings[i]) > 9) {
      vertical_found = TRUE;
      g_free (state->vertical);
      state->vertical = g_strdup (splitted_settings[i] + 9);
      valid_tag = TRUE;
      GST_DEBUG_OBJECT (subparse, "Parsed vertical: '%s'", state->vertical);
    } else if (g_str_has_prefix (splitted_settings[i], "line:")) {
      const gchar *line_value = splitted_settings[i] + 5;
      if (g_str_has_suffix (line_value, "%")) {
        if (sscanf (line_value, "%" G_GINT16_FORMAT "%%", &line_position) > 0) {
          state->line_position = line_position;
          g_free (state->line);
          state->line = g_strdup (line_value);
          valid_tag = TRUE;
          GST_DEBUG_OBJECT (subparse,
              "Parsed line position: %d%%, stored line: '%s'", line_position,
              state->line);
        }
      } else {
        if (sscanf (line_value, "%" G_GINT16_FORMAT, &line_position) > 0) {
          state->line_number = line_position;
          g_free (state->line);
          state->line = g_strdup (line_value);
          valid_tag = TRUE;
          GST_DEBUG_OBJECT (subparse,
              "Parsed line number: %d, stored line: '%s'", line_position,
              state->line);
        }
      }
    } else if (g_str_has_prefix (splitted_settings[i], "S:")) {
      if (sscanf (splitted_settings[i], "S:%" G_GINT16_FORMAT "%%",
              &text_size) > 0) {
        state->text_size = (guint8) text_size;
        valid_tag = TRUE;
        GST_DEBUG_OBJECT (subparse, "Parsed text size: %d%%", text_size);
      }
    } else if (g_str_has_prefix (splitted_settings[i], "align:")
        && strlen (splitted_settings[i]) > 6) {
      g_free (state->alignment);
      state->alignment = g_strdup (splitted_settings[i] + 6);
      alignment_found = TRUE;
      valid_tag = TRUE;
      GST_DEBUG_OBJECT (subparse, "Parsed alignment: '%s'", state->alignment);
    } else if (g_str_has_prefix (splitted_settings[i], "region:")
        && strlen (splitted_settings[i]) > 7) {
      g_free (state->region_id);
      state->region_id = g_strdup (splitted_settings[i] + 7);
      valid_tag = TRUE;
      GST_DEBUG_OBJECT (subparse, "Parsed region: '%s'", state->region_id);
    } else if (g_str_has_prefix (splitted_settings[i], "position:")
        && strlen (splitted_settings[i]) > 9) {
      g_free (state->position);
      state->position = g_strdup (splitted_settings[i] + 9);
      valid_tag = TRUE;
      GST_DEBUG_OBJECT (subparse, "Parsed position: '%s'", state->position);
    }
    if (!valid_tag) {
      GST_LOG_OBJECT (subparse, "Invalid or unrecognized setting: '%s'",
          splitted_settings[i]);
    }
  }
  g_strfreev (splitted_settings);
  if (!vertical_found) {
    g_free (state->vertical);
    state->vertical = g_strdup ("");
    GST_DEBUG_OBJECT (subparse, "No vertical setting found, set to empty");
  }
  if (!alignment_found) {
    g_free (state->alignment);
    state->alignment = g_strdup ("");
    GST_DEBUG_OBJECT (subparse, "No alignment found, set to empty");
  }
}


/* ---- Region parsing ---- */

/* Parse a WebVTT REGION block. Regions define viewport areas where cues
 * can be rendered; each region has an id, width, line count, anchor points,
 * and scroll mode. The parsed region is appended to state->regions. */
void
parse_webvtt_region_settings (ParserState * state, const gchar * settings,
    GstSubParse * subparse)
{
  gchar **splitted_settings = g_strsplit_set (settings, "\n", -1);
  GstSubParseVTTRegion *region = g_new0 (GstSubParseVTTRegion, 1);
  gint i = 0;

  region->width = 100.0;
  region->lines = 3;
  region->scroll = g_strdup ("none");

  while (i < g_strv_length (splitted_settings)) {
    gchar *setting = g_strstrip (splitted_settings[i]);
    if (strlen (setting) == 0) {
      i++;
      continue;
    }

    if (g_str_has_prefix (setting, "id:")) {
      region->id = g_strdup (setting + 3);
    } else if (g_str_has_prefix (setting, "width:")) {
      gfloat width;
      if (sscanf (setting, "width:%f%%", &width) == 1) {
        region->width = width;
      }
    } else if (g_str_has_prefix (setting, "lines:")) {
      gint lines;
      if (sscanf (setting, "lines:%d", &lines) == 1) {
        region->lines = lines;
      }
    } else if (g_str_has_prefix (setting, "regionanchor:")) {
      gfloat x, y;
      if (sscanf (setting, "regionanchor:%f%%,%f%%", &x, &y) == 2) {
        region->regionanchor_x = x;
        region->regionanchor_y = y;
      }
    } else if (g_str_has_prefix (setting, "viewportanchor:")) {
      gfloat x, y;
      if (sscanf (setting, "viewportanchor:%f%%,%f%%", &x, &y) == 2) {
        region->viewportanchor_x = x;
        region->viewportanchor_y = y;
      }
    } else if (g_str_has_prefix (setting, "scroll:")) {
      g_free (region->scroll);
      region->scroll = g_strdup (setting + 7);
    } else {
      GST_LOG_OBJECT (subparse, "Invalid or unrecognized region setting: %s",
          setting);
    }
    i++;
  }

  state->regions = g_list_append (state->regions, region);
  g_strfreev (splitted_settings);

  GST_DEBUG_OBJECT (subparse, "Parsed region: id=%s, width=%f, lines=%d, "
      "regionanchor=(%f,%f), viewportanchor=(%f,%f), scroll=%s",
      region->id, region->width, region->lines,
      region->regionanchor_x, region->regionanchor_y,
      region->viewportanchor_x, region->viewportanchor_y, region->scroll);
}


/* ---- Cue lifecycle ---- */

void
free_webvtt_cue (GstSubParseVTTCue * cue)
{
  g_free (cue->cue_id);
  g_free (cue->text);
  g_free (cue->region_id);
  g_free (cue->vertical);
  g_free (cue->alignment);
  g_free (cue->position);
  g_free (cue->line);
  g_free (cue);
}


/* ---- Overlap resolution ---- */

/* A time event marks the start or end of a cue; used to split overlapping
 * cue intervals into non-overlapping segments for buffer generation. */
typedef struct
{
  GstClockTime time;
  GstSubParseVTTCue *cue;
  gboolean is_start;
} GstSubParseCueEvent;

/* Sort events chronologically; ties are broken so start events precede
 * end events, ensuring the active set is correct at each boundary. */
static gint
compare_cue_events (gconstpointer a, gconstpointer b)
{
  const GstSubParseCueEvent *event_a = (GstSubParseCueEvent *) a;
  const GstSubParseCueEvent *event_b = (GstSubParseCueEvent *) b;
  if (event_a->time < event_b->time)
    return -1;
  if (event_a->time > event_b->time)
    return 1;
  return event_a->is_start ? -1 : 1;
}

/* Generate combined Pango markup from all currently active cues.
 * WebVTT cue positioning metadata is now carried via GstCustomMeta
 * (attached in push_subtitle_buffer) rather than inline HTML comments.
 * If @per_cue_markups is non-NULL, a list of per-cue markup strings
 * (owned by the caller) is returned through it; order matches active_cues. */
static gchar *
generate_subtitle_text (GList * active_cues, GstSubParse * subparse,
    GList ** per_cue_markups)
{
  GString *result = g_string_new (NULL);
  GList *iter;
  gboolean has_valid_text = FALSE;

  if (per_cue_markups)
    *per_cue_markups = NULL;

  GST_DEBUG_OBJECT (subparse, "Generating subtitle text for %d active cues",
      g_list_length (active_cues));

  for (iter = active_cues; iter; iter = g_list_next (iter)) {
    GstSubParseVTTCue *cue = (GstSubParseVTTCue *) iter->data;
    gchar *pango_markup = NULL;

    if (!cue->text || cue->text[0] == '\0'
        || g_strcmp0 (cue->text, "(null)") == 0) {
      GST_DEBUG_OBJECT (subparse,
          "Skipping cue %s (counter=%u) with invalid text: %s",
          GST_STR_NULL (cue->cue_id), cue->counter, GST_STR_NULL (cue->text));
      continue;
    }

    GST_DEBUG_OBJECT (subparse,
        "Processing cue %s (counter=%u): text='%s', position=%s, align=%s, line=%s, region_id=%s, vertical=%s",
        GST_STR_NULL (cue->cue_id), cue->counter, cue->text,
        GST_STR_NULL (cue->position), GST_STR_NULL (cue->alignment),
        GST_STR_NULL (cue->line), GST_STR_NULL (cue->region_id),
        GST_STR_NULL (cue->vertical));
#ifdef HAVE_WEBVTT_CSS
    if (subparse->css_parse) {
      gchar *css_cue_id;
      if (cue->cue_id && *cue->cue_id != '\0') {
        css_cue_id = g_strdup (cue->cue_id);
      } else {
        css_cue_id = g_strdup_printf ("webvtt_%u", cue->counter);
      }
      gst_cssparse_set_cue_id (subparse->css_parse, css_cue_id);
      g_free (css_cue_id);

      int code = gst_cssparse_parse (subparse->css_parse, "");
      if (code == GST_CSS_PARSE_OK) {
        pango_markup =
            gst_cssparse_to_pango_markup (subparse->css_parse, cue->text);
        GST_DEBUG_OBJECT (subparse,
            "CSS-generated Pango markup for cue %s (counter=%u): %s",
            GST_STR_NULL (cue->cue_id), cue->counter,
            pango_markup ? pango_markup : "(null)");
      } else {
        GST_WARNING_OBJECT (subparse,
            "CSS parsing failed for cue %s (counter=%u): code %d",
            GST_STR_NULL (cue->cue_id), cue->counter, code);
      }
    }
#endif

    if (!pango_markup) {
      gchar *escaped_text = g_markup_escape_text (cue->text, -1);
      pango_markup = g_strdup_printf ("<span>%s</span>", escaped_text);
      g_free (escaped_text);
      GST_DEBUG_OBJECT (subparse,
          "Fallback Pango markup for cue %s (counter=%u): %s",
          GST_STR_NULL (cue->cue_id), cue->counter, pango_markup);
    }

    /* Cue positioning metadata is now carried via GstCustomMeta
     * (attached in push_subtitle_buffer), not inline comments. */

    g_string_append (result, pango_markup);
    GST_DEBUG_OBJECT (subparse, "Added markup to result: %s", pango_markup);
    if (per_cue_markups)
      *per_cue_markups = g_list_append (*per_cue_markups,
          g_strdup (pango_markup));
    g_free (pango_markup);
    has_valid_text = TRUE;
  }

  if (!has_valid_text) {
    GST_DEBUG_OBJECT (subparse, "No valid text in active cues, returning NULL");
    g_string_free (result, TRUE);
    if (per_cue_markups) {
      g_list_free_full (*per_cue_markups, g_free);
      *per_cue_markups = NULL;
    }
    return NULL;
  }

  gchar *final_text = g_string_free (result, FALSE);
  GST_DEBUG_OBJECT (subparse, "Final subtitle text (%zu bytes): %s",
      strlen (final_text), final_text);

  if (!g_utf8_validate (final_text, -1, NULL)) {
    GST_WARNING_OBJECT (subparse, "Generated text is not valid UTF-8");
    g_free (final_text);
    if (per_cue_markups) {
      g_list_free_full (*per_cue_markups, g_free);
      *per_cue_markups = NULL;
    }
    return NULL;
  }

  return final_text;
}


/* Push a single subtitle buffer covering [start, start+duration) with the
 * combined text of all currently active cues. */
static GstFlowReturn
push_subtitle_buffer (GstSubParse * self, GstClockTime start,
    GstClockTime duration, GList * active_cues)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gchar *subtitle_text = NULL;
  GList *per_cue_markups = NULL;

  GST_DEBUG_OBJECT (self,
      "Preparing buffer at %" GST_TIME_FORMAT " for %" GST_TIME_FORMAT
      " with %d active cues", GST_TIME_ARGS (start), GST_TIME_ARGS (duration),
      g_list_length (active_cues));

  subtitle_text = generate_subtitle_text (active_cues, self, &per_cue_markups);
  if (!subtitle_text) {
    GST_WARNING_OBJECT (self,
        "Failed to generate subtitle text for buffer at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start));
    return GST_FLOW_OK;
  }

  GstBuffer *buf =
      gst_buffer_new_allocate (NULL, strlen (subtitle_text) + 1, NULL);
  if (!buf) {
    GST_ERROR_OBJECT (self, "Failed to allocate buffer for subtitle");
    g_free (subtitle_text);
    return GST_FLOW_ERROR;
  }

  gst_buffer_fill (buf, 0, subtitle_text, strlen (subtitle_text) + 1);
  gst_buffer_set_size (buf, strlen (subtitle_text));

  GST_BUFFER_TIMESTAMP (buf) = start;
  GST_BUFFER_DURATION (buf) = duration;

  /* Attach GstCustomMeta with per-cue WebVTT positioning attributes.
   * One meta per buffer; each cue's data is stored in fields keyed by index. */
  gst_webvtt_cue_meta_ensure_registered ();
  {
    GstCustomMeta *meta;
    GstStructure *s;
    guint cue_idx = 0;
    GList *markup_iter = per_cue_markups;

    meta = gst_buffer_add_custom_meta (buf, GST_WEBVTT_CUE_META_NAME);
    if (meta) {
      s = gst_custom_meta_get_structure (meta);

      for (GList * cue_iter = active_cues; cue_iter;
          cue_iter = g_list_next (cue_iter)) {
        GstSubParseVTTCue *cue = (GstSubParseVTTCue *) cue_iter->data;
        GstSubParseVTTRegion *selected_region = NULL;
        gchar *field;

        if (!cue->text || cue->text[0] == '\0'
            || g_strcmp0 (cue->text, "(null)") == 0)
          continue;

        /* Resolve the cue's region for positioning defaults */
        if (cue->region_id) {
          GList *region_iter = self->state.regions;
          while (region_iter) {
            GstSubParseVTTRegion *region =
                (GstSubParseVTTRegion *) region_iter->data;
            if (g_strcmp0 (region->id, cue->region_id) == 0) {
              selected_region = region;
              break;
            }
            region_iter = g_list_next (region_iter);
          }
        }

        /* Store per-cue pango markup */
        field = g_strdup_printf ("cue-%u-markup", cue_idx);
        gst_structure_set (s, field, G_TYPE_STRING,
            markup_iter ? (const gchar *) markup_iter->data : "", NULL);
        g_free (field);
        if (markup_iter)
          markup_iter = g_list_next (markup_iter);

        field = g_strdup_printf ("cue-%u-counter", cue_idx);
        gst_structure_set (s, field, G_TYPE_UINT, cue->counter, NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-end-time", cue_idx);
        gst_structure_set (s, field, G_TYPE_UINT64, cue->end_time, NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-align", cue_idx);
        gst_structure_set (s, field, G_TYPE_STRING,
            cue->alignment ? cue->alignment : "center", NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-cue-align", cue_idx);
        gst_structure_set (s, field, G_TYPE_STRING,
            cue->alignment ? cue->alignment : "center", NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-vertical", cue_idx);
        gst_structure_set (s, field, G_TYPE_STRING,
            cue->vertical ? cue->vertical : "", NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-scroll", cue_idx);
        gst_structure_set (s, field, G_TYPE_STRING,
            selected_region ? selected_region->scroll : "none", NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-region-width", cue_idx);
        gst_structure_set (s, field, G_TYPE_DOUBLE,
            (gdouble) (selected_region ? selected_region->width : 100.0), NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-region-lines", cue_idx);
        gst_structure_set (s, field, G_TYPE_INT,
            selected_region ? selected_region->lines : 3, NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-regionanchor-x", cue_idx);
        gst_structure_set (s, field, G_TYPE_DOUBLE,
            (gdouble) (selected_region ? selected_region->regionanchor_x : 0.0),
            NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-regionanchor-y", cue_idx);
        gst_structure_set (s, field, G_TYPE_DOUBLE,
            (gdouble) (selected_region ? selected_region->regionanchor_y : 0.0),
            NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-viewportanchor-x", cue_idx);
        gst_structure_set (s, field, G_TYPE_DOUBLE,
            (gdouble) (selected_region ?
                selected_region->viewportanchor_x : 50.0), NULL);
        g_free (field);

        field = g_strdup_printf ("cue-%u-viewportanchor-y", cue_idx);
        gst_structure_set (s, field, G_TYPE_DOUBLE,
            (gdouble) (selected_region ?
                selected_region->viewportanchor_y : 50.0), NULL);
        g_free (field);

        cue_idx++;
      }

      gst_structure_set (s, "n-cues", G_TYPE_UINT, cue_idx, NULL);
      GST_DEBUG_OBJECT (self,
          "Attached WebVTT meta with %u cues to buffer", cue_idx);
    }
  }

  GST_DEBUG_OBJECT (self,
      "Prepared buffer at %" GST_TIME_FORMAT " for %" GST_TIME_FORMAT
      " with text: %s, size: %" G_GSIZE_FORMAT ", refcount: %d",
      GST_TIME_ARGS (start), GST_TIME_ARGS (duration), subtitle_text,
      gst_buffer_get_size (buf), GST_MINI_OBJECT_REFCOUNT_VALUE (buf));

  GST_DEBUG_OBJECT (self,
      "Pushing buffer at %" GST_TIME_FORMAT " with refcount: %d to srcpad",
      GST_TIME_ARGS (start), GST_MINI_OBJECT_REFCOUNT_VALUE (buf));

  ret = gst_pad_push (self->srcpad, buf);
  if (ret != GST_FLOW_OK) {
    GST_WARNING_OBJECT (self,
        "Failed to push buffer at %" GST_TIME_FORMAT ": %s",
        GST_TIME_ARGS (start), gst_flow_get_name (ret));
  } else {
    GST_DEBUG_OBJECT (self, "Successfully pushed buffer at %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start));
    gst_buffer_replace (&self->last_buffer, buf);
    GST_DEBUG_OBJECT (self, "Stored last buffer for EOS duplication");
  }

  g_free (subtitle_text);
  g_list_free_full (per_cue_markups, g_free);
  return ret;
}


/* ---- Cue processing / overlap splitting ---- */

/* Process all accumulated cues by building a sorted event list of
 * start/end times, then walking through those events to produce
 * non-overlapping subtitle buffers. Each buffer contains the combined
 * text of all cues active during that interval. Previously processed
 * cues that are still active (their end_time extends past the current
 * batch's start) contribute end events so their text remains visible. */
void
process_webvtt_cues (GstSubParse * subparse, ParserState * state)
{
  GList *events = NULL, *iter;
  GList *active_cues = NULL;
  GstClockTime last_processed_time = GST_CLOCK_TIME_NONE;
  GstClockTime min_start = GST_CLOCK_TIME_NONE;

  if (!state->active_cues) {
    GST_DEBUG_OBJECT (subparse, "No active cues to process");
    return;
  }

  for (iter = state->active_cues; iter; iter = g_list_next (iter)) {
    GstSubParseVTTCue *cue = (GstSubParseVTTCue *) iter->data;
    GST_DEBUG_OBJECT (subparse,
        "Processing cue %s: text='%s', start=%" GST_TIME_FORMAT ", end=%"
        GST_TIME_FORMAT ", processed=%d",
        GST_STR_NULL (cue->cue_id), GST_STR_NULL (cue->text),
        GST_TIME_ARGS (cue->start_time), GST_TIME_ARGS (cue->end_time),
        cue->processed);
  }

  /* Find the earliest unprocessed cue start — this is the first boundary */
  for (iter = state->active_cues; iter; iter = g_list_next (iter)) {
    GstSubParseVTTCue *cue = (GstSubParseVTTCue *) iter->data;
    if (!cue->processed && GST_CLOCK_TIME_IS_VALID (cue->start_time) &&
        (!GST_CLOCK_TIME_IS_VALID (min_start) || cue->start_time < min_start)) {
      min_start = cue->start_time;
    }
  }

  if (!GST_CLOCK_TIME_IS_VALID (min_start)) {
    GST_DEBUG_OBJECT (subparse, "No valid min_start found");
    return;
  }

  /* Handle previously processed but still-active cues (their end extends
   * past the current batch start, so they contribute an end event) */
  iter = state->active_cues;
  while (iter) {
    GList *next = g_list_next (iter);
    GstSubParseVTTCue *cue = (GstSubParseVTTCue *) iter->data;
    if (cue->processed) {
      if (GST_CLOCK_TIME_IS_VALID (cue->end_time) && cue->end_time > min_start) {
        GstSubParseCueEvent *event = g_new0 (GstSubParseCueEvent, 1);
        event->cue = cue;
        event->time = cue->end_time;
        event->is_start = FALSE;
        events = g_list_append (events, event);
        if (cue->start_time <= min_start) {
          active_cues = g_list_append (active_cues, cue);
        }
      } else {
        GST_DEBUG_OBJECT (subparse,
            "Removing ended processed cue %s ending at %" GST_TIME_FORMAT,
            GST_STR_NULL (cue->cue_id), GST_TIME_ARGS (cue->end_time));
        state->active_cues = g_list_delete_link (state->active_cues, iter);
        free_webvtt_cue (cue);
      }
    }
    iter = next;
  }

  /* Create start and end events for every unprocessed cue */
  for (iter = state->active_cues; iter; iter = g_list_next (iter)) {
    GstSubParseVTTCue *cue = (GstSubParseVTTCue *) iter->data;
    if (!cue->processed && GST_CLOCK_TIME_IS_VALID (cue->start_time) &&
        GST_CLOCK_TIME_IS_VALID (cue->end_time) &&
        cue->text && cue->text[0] != '\0'
        && g_strcmp0 (cue->text, "(null)") != 0) {
      GstSubParseCueEvent *event;

      event = g_new0 (GstSubParseCueEvent, 1);
      event->cue = cue;
      event->time = cue->start_time;
      event->is_start = TRUE;
      events = g_list_append (events, event);

      event = g_new0 (GstSubParseCueEvent, 1);
      event->cue = cue;
      event->time = cue->end_time;
      event->is_start = FALSE;
      events = g_list_append (events, event);
    } else if (!cue->processed) {
      GST_DEBUG_OBJECT (subparse,
          "Skipping invalid cue %s: start=%" GST_TIME_FORMAT ", end=%"
          GST_TIME_FORMAT ", text=%s",
          GST_STR_NULL (cue->cue_id),
          GST_TIME_ARGS (cue->start_time), GST_TIME_ARGS (cue->end_time),
          GST_STR_NULL (cue->text));
    }
  }

  events = g_list_sort (events, (GCompareFunc) compare_cue_events);

  for (iter = events; iter; iter = g_list_next (iter)) {
    GstSubParseCueEvent *event = (GstSubParseCueEvent *) iter->data;
    GST_DEBUG_OBJECT (subparse,
        "Sorted event: %s for cue %s at %" GST_TIME_FORMAT,
        event->is_start ? "start" : "end", GST_STR_NULL (event->cue->cue_id),
        GST_TIME_ARGS (event->time));
  }

  last_processed_time = min_start;

  /* Walk sorted events: at each boundary, emit a buffer for the interval
   * [last_processed_time, current_time) with whatever cues are active. */
  for (iter = events; iter; iter = g_list_next (iter)) {
    GstSubParseCueEvent *event = (GstSubParseCueEvent *) iter->data;
    GstClockTime current_time = event->time;

    if (active_cues && GST_CLOCK_TIME_IS_VALID (last_processed_time) &&
        current_time > last_processed_time) {
      if (state->segment &&
          (last_processed_time >= state->segment->start ||
              state->segment->start == GST_CLOCK_TIME_NONE) &&
          (current_time <= state->segment->stop ||
              state->segment->stop == GST_CLOCK_TIME_NONE)) {
        GList *cue_iter;
        GList *interval_cues = NULL;
        gboolean has_valid_text = FALSE;
        for (cue_iter = active_cues; cue_iter;
            cue_iter = g_list_next (cue_iter)) {
          GstSubParseVTTCue *cue = (GstSubParseVTTCue *) cue_iter->data;
          if (cue->text && cue->text[0] != '\0'
              && g_strcmp0 (cue->text, "(null)") != 0
              && cue->start_time <= last_processed_time
              && cue->end_time > last_processed_time) {
            has_valid_text = TRUE;
            interval_cues = g_list_append (interval_cues, cue);
            GST_DEBUG_OBJECT (subparse,
                "Active cue in buffer: id=%s, text=%s, position=%s, align=%s, line=%s",
                GST_STR_NULL (cue->cue_id), cue->text,
                GST_STR_NULL (cue->position), GST_STR_NULL (cue->alignment),
                GST_STR_NULL (cue->line));
          } else {
            GST_DEBUG_OBJECT (subparse,
                "Skipping inactive or invalid cue %s: start=%" GST_TIME_FORMAT
                ", end=%" GST_TIME_FORMAT ", text=%s",
                GST_STR_NULL (cue->cue_id), GST_TIME_ARGS (cue->start_time),
                GST_TIME_ARGS (cue->end_time), GST_STR_NULL (cue->text));
          }
        }

        if (has_valid_text) {
          GST_DEBUG_OBJECT (subparse,
              "Creating buffer for interval %" GST_TIME_FORMAT " to %"
              GST_TIME_FORMAT " with %d active cues",
              GST_TIME_ARGS (last_processed_time), GST_TIME_ARGS (current_time),
              g_list_length (interval_cues));

          GstFlowReturn ret =
              push_subtitle_buffer (subparse, last_processed_time,
              current_time - last_processed_time, interval_cues);
          if (ret != GST_FLOW_OK) {
            GST_WARNING_OBJECT (subparse, "Failed to push buffer at %"
                GST_TIME_FORMAT ": %s",
                GST_TIME_ARGS (last_processed_time), gst_flow_get_name (ret));
          }
        } else {
          GST_DEBUG_OBJECT (subparse,
              "Skipping buffer for interval %" GST_TIME_FORMAT " to %"
              GST_TIME_FORMAT " due to no valid text",
              GST_TIME_ARGS (last_processed_time),
              GST_TIME_ARGS (current_time));
        }

        g_list_free (interval_cues);
      } else {
        GST_DEBUG_OBJECT (subparse,
            "Interval %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
            " outside segment start=%" GST_TIME_FORMAT ", stop=%"
            GST_TIME_FORMAT, GST_TIME_ARGS (last_processed_time),
            GST_TIME_ARGS (current_time), GST_TIME_ARGS (state->segment->start),
            GST_TIME_ARGS (state->segment->stop));
      }
    }

    if (event->is_start) {
      active_cues = g_list_append (active_cues, event->cue);
      event->cue->processed = TRUE;
    } else {
      active_cues = g_list_remove (active_cues, event->cue);
    }

    last_processed_time = current_time;
  }

  /* Drain remaining active cues whose end times extend past the last event */
  while (active_cues) {
    GstClockTime next_end_time = GST_CLOCK_TIME_NONE;
    GList *cue_iter;

    for (cue_iter = active_cues; cue_iter; cue_iter = g_list_next (cue_iter)) {
      GstSubParseVTTCue *cue = (GstSubParseVTTCue *) cue_iter->data;
      if (GST_CLOCK_TIME_IS_VALID (cue->end_time) &&
          (!GST_CLOCK_TIME_IS_VALID (next_end_time) ||
              cue->end_time < next_end_time)) {
        next_end_time = cue->end_time;
      }
    }

    if (!GST_CLOCK_TIME_IS_VALID (next_end_time) ||
        next_end_time <= last_processed_time) {
      break;
    }

    if (GST_CLOCK_TIME_IS_VALID (last_processed_time) &&
        next_end_time > last_processed_time) {
      if (state->segment &&
          (last_processed_time >= state->segment->start ||
              state->segment->start == GST_CLOCK_TIME_NONE) &&
          (next_end_time <= state->segment->stop ||
              state->segment->stop == GST_CLOCK_TIME_NONE)) {
        GList *interval_cues = NULL;
        gboolean has_valid_text = FALSE;
        for (cue_iter = active_cues; cue_iter;
            cue_iter = g_list_next (cue_iter)) {
          GstSubParseVTTCue *cue = (GstSubParseVTTCue *) cue_iter->data;
          if (cue->text && cue->text[0] != '\0'
              && g_strcmp0 (cue->text, "(null)") != 0
              && cue->start_time <= last_processed_time
              && cue->end_time > last_processed_time) {
            has_valid_text = TRUE;
            interval_cues = g_list_append (interval_cues, cue);
            GST_DEBUG_OBJECT (subparse,
                "Active cue in final buffer: id=%s, text=%s, position=%s, align=%s, line=%s",
                GST_STR_NULL (cue->cue_id), cue->text,
                GST_STR_NULL (cue->position), GST_STR_NULL (cue->alignment),
                GST_STR_NULL (cue->line));
          } else {
            GST_DEBUG_OBJECT (subparse,
                "Skipping inactive or invalid cue %s: start=%" GST_TIME_FORMAT
                ", end=%" GST_TIME_FORMAT ", text=%s",
                GST_STR_NULL (cue->cue_id), GST_TIME_ARGS (cue->start_time),
                GST_TIME_ARGS (cue->end_time), GST_STR_NULL (cue->text));
          }
        }

        if (has_valid_text) {
          GST_DEBUG_OBJECT (subparse,
              "Creating final buffer for interval %" GST_TIME_FORMAT " to %"
              GST_TIME_FORMAT " with %d active cues",
              GST_TIME_ARGS (last_processed_time),
              GST_TIME_ARGS (next_end_time), g_list_length (interval_cues));

          GstFlowReturn ret =
              push_subtitle_buffer (subparse, last_processed_time,
              next_end_time - last_processed_time, interval_cues);
          if (ret != GST_FLOW_OK) {
            GST_WARNING_OBJECT (subparse, "Failed to push final buffer at %"
                GST_TIME_FORMAT ": %s",
                GST_TIME_ARGS (last_processed_time), gst_flow_get_name (ret));
          }
        } else {
          GST_DEBUG_OBJECT (subparse,
              "Skipping final buffer for interval %" GST_TIME_FORMAT " to %"
              GST_TIME_FORMAT " due to no valid text",
              GST_TIME_ARGS (last_processed_time),
              GST_TIME_ARGS (next_end_time));
        }

        g_list_free (interval_cues);
      }
    }

    cue_iter = active_cues;
    while (cue_iter) {
      GList *next = g_list_next (cue_iter);
      GstSubParseVTTCue *cue = (GstSubParseVTTCue *) cue_iter->data;
      if (GST_CLOCK_TIME_IS_VALID (cue->end_time) &&
          cue->end_time <= next_end_time) {
        active_cues = g_list_delete_link (active_cues, cue_iter);
        free_webvtt_cue (cue);
      }
      cue_iter = next;
    }

    last_processed_time = next_end_time;
  }

  g_list_free_full (events, g_free);

  /* Prune fully-processed cues from state->active_cues */
  iter = state->active_cues;
  while (iter) {
    GList *next = g_list_next (iter);
    GstSubParseVTTCue *cue = (GstSubParseVTTCue *) iter->data;
    if (cue->processed && GST_CLOCK_TIME_IS_VALID (cue->end_time) &&
        cue->end_time < last_processed_time) {
      GST_DEBUG_OBJECT (subparse, "Removing processed cue %s ending at %"
          GST_TIME_FORMAT, GST_STR_NULL (cue->cue_id),
          GST_TIME_ARGS (cue->end_time));
      state->active_cues = g_list_delete_link (state->active_cues, iter);
      free_webvtt_cue (cue);
    }
    iter = next;
  }

  if (!state->active_cues) {
    GST_DEBUG_OBJECT (subparse, "No valid cues to process");
  }
}


/* ---- Main WebVTT line parser ---- */

/* WebVTT line parser state machine. Processes input line-by-line:
 *   state 0: expecting WEBVTT header, STYLE/REGION block, cue ID, or timestamp
 *   state 1: expecting timestamp line (cue ID was read in state 0)
 *   state 2: accumulating cue text
 *   state 3: accumulating STYLE block (CSS)
 *   state 4: accumulating REGION block
 *
 * Unlike other parsers, WebVTT cues are not emitted inline — they are
 * collected in state->active_cues and processed as a batch by
 * process_webvtt_cues() after all lines in the current buffer are parsed.
 * This is necessary because WebVTT allows temporally overlapping cues.
 *
 * See https://www.w3.org/TR/webvtt1/ */
gchar *
parse_webvtt (ParserState * state, const gchar * line)
{
  if (!state || !state->user_data) {
    GST_WARNING ("ParserState or user_data NULL, cannot parse");
    return NULL;
  }

  GstSubParse *subparse = GST_SUBPARSE (state->user_data);
  gchar *trimmed_line = g_strstrip (g_strdup (line));
  const gchar *orig_line = line;

  GST_DEBUG_OBJECT (subparse,
      "Processing line: '%s' (trimmed: '%s', state: %d, buf: %p, '%s')",
      orig_line, trimmed_line, state->state, state->buf,
      GST_STR_NULL (state->buf ? state->buf->str : "(empty)"));

  if (state->state == 0 || state->state == 1) {
#ifdef HAVE_WEBVTT_CSS
    if (subparse && state->state == 0 &&
        g_ascii_strcasecmp (trimmed_line, "STYLE") == 0) {
      GST_DEBUG_OBJECT (subparse, "Found WebVTT STYLE block");
      state->state = 3;
      if (!state->buf) {
        state->buf = g_string_new (NULL);
      }
      g_free (trimmed_line);
      return NULL;
    }
#endif
    if (g_ascii_strcasecmp (trimmed_line, "REGION") == 0) {
      GST_DEBUG_OBJECT (subparse, "Found WebVTT REGION block");
      state->state = 4;
      if (!state->buf) {
        state->buf = g_string_new (NULL);
      }
      g_free (trimmed_line);
      return NULL;
    }
    if (state->state == 0 &&
        g_ascii_strncasecmp (trimmed_line, "WEBVTT", 6) == 0) {
      GST_DEBUG_OBJECT (subparse, "Found WebVTT header");
      g_free (trimmed_line);
      return NULL;
    }
    if (trimmed_line[0] == '\0') {
      GST_DEBUG_OBJECT (subparse, "Empty line, staying in state %d",
          state->state);
      g_free (trimmed_line);
      return NULL;
    }

    GstClockTime ts_start, ts_end;
    gchar *end_time;
    gchar *cue_settings = NULL;

    if ((end_time = strstr (trimmed_line, " --> ")) &&
        parse_subrip_time (trimmed_line, &ts_start) &&
        parse_subrip_time (end_time + strlen (" --> "), &ts_end) &&
        ts_start <= ts_end) {
      GST_DEBUG_OBJECT (subparse, "Transitioning to state 2 for timestamp");
      state->state = 2;
      state->start_time = ts_start;
      state->duration = ts_end - ts_start;
      cue_settings = strstr (end_time + strlen (" --> "), " ");
      if (!state->buf) {
        state->buf = g_string_new (NULL);
      }
#ifdef HAVE_WEBVTT_CSS
      if (subparse->css_parse) {
        gst_cssparse_reinit (subparse->css_parse);
      }
#endif
      if (cue_settings) {
        parse_webvtt_cue_settings (state, cue_settings + 1);
      } else {
        g_free (state->vertical);
        state->vertical = g_strdup ("");
        g_free (state->alignment);
        state->alignment = g_strdup ("");
        g_free (state->region_id);
        state->region_id = NULL;
        g_free (state->position);
        state->position = NULL;
        g_free (state->line);
        state->line = NULL;
      }

      GstSubParseVTTCue *cue = g_new0 (GstSubParseVTTCue, 1);
      cue->cue_id = g_strdup (state->cue_id);
      cue->start_time = ts_start;
      cue->end_time = ts_end;
      cue->region_id = g_strdup (state->region_id);
      cue->vertical = g_strdup (state->vertical);
      cue->alignment = g_strdup (state->alignment);
      cue->position = g_strdup (state->position);
      cue->line = g_strdup (state->line);
      cue->text_position = state->text_position;
      cue->text_size = state->text_size;
      cue->line_position = state->line_position;
      cue->line_number = state->line_number;
      cue->processed = FALSE;
      cue->counter = webvtt_cue_counter++;

      state->active_cues = g_list_append (state->active_cues, cue);
      GST_DEBUG_OBJECT (subparse,
          "Added cue to active_cues: id=%s, counter=%u, start=%" GST_TIME_FORMAT
          ", end=%" GST_TIME_FORMAT, GST_STR_NULL (cue->cue_id), cue->counter,
          GST_TIME_ARGS (cue->start_time), GST_TIME_ARGS (cue->end_time));

      g_free (trimmed_line);
      return NULL;
    }

    state->state = 1;
    g_free (state->cue_id);
    state->cue_id = g_strdup (trimmed_line);
    if (!state->buf) {
      state->buf = g_string_new (NULL);
    }
    g_string_assign (state->buf, "");
    g_free (trimmed_line);
    return NULL;
  }
#ifdef HAVE_WEBVTT_CSS
  else if (state->state == 3 && subparse) {
    if (trimmed_line[0] == '\0') {
      if (state->buf && state->buf->len > 0) {
        gchar *css_data = g_string_free (state->buf, FALSE);
        int code = gst_cssparse_parse (subparse->css_parse, css_data);
        if (code != GST_CSS_PARSE_OK) {
          GST_WARNING_OBJECT (subparse, "CSS parsing failed with code %d",
              code);
        }
        g_free (css_data);
      }
      state->buf = NULL;
      state->state = 0;
      g_free (trimmed_line);
      return NULL;
    } else {
      if (!state->buf) {
        state->buf = g_string_new (NULL);
      }
      g_string_append (state->buf, trimmed_line);
      g_string_append_c (state->buf, '\n');
      g_free (trimmed_line);
      return NULL;
    }
  } else
#endif
  if (state->state == 4 && subparse) {
    if (trimmed_line[0] == '\0') {
      if (state->buf && state->buf->len > 0) {
        gchar *region_data = g_string_free (state->buf, FALSE);
        parse_webvtt_region_settings (state, region_data, subparse);
        g_free (region_data);
      }
      state->buf = NULL;
      state->state = 0;
      g_free (trimmed_line);
      return NULL;
    } else {
      if (!state->buf) {
        state->buf = g_string_new (NULL);
      }
      g_string_append (state->buf, trimmed_line);
      g_string_append_c (state->buf, '\n');
      g_free (trimmed_line);
      return NULL;
    }
  } else if (state->state == 2) {
    if (trimmed_line[0] == '\0') {
      GST_DEBUG_OBJECT (subparse,
          "Empty line in CUE_TEXT state, finalizing cue with cue_id: '%s'",
          GST_STR_NULL (state->cue_id));

      GstSubParseVTTCue *cue = NULL;
      GList *iter = state->active_cues;
      while (iter) {
        GstSubParseVTTCue *existing_cue = (GstSubParseVTTCue *) iter->data;
        if (g_strcmp0 (existing_cue->cue_id, state->cue_id) == 0 &&
            existing_cue->start_time == state->start_time &&
            existing_cue->end_time == state->start_time + state->duration) {
          cue = existing_cue;
          break;
        }
        iter = g_list_next (iter);
      }

      if (!cue) {
        GST_WARNING_OBJECT (subparse,
            "No matching cue found for cue_id '%s', start=%" GST_TIME_FORMAT,
            GST_STR_NULL (state->cue_id), GST_TIME_ARGS (state->start_time));
        g_free (trimmed_line);
        return NULL;
      }

      if (state->buf && state->buf->len > 0) {
        g_free (cue->text);
        cue->text = g_strdup (g_strchomp (state->buf->str));
        GST_DEBUG_OBJECT (subparse, "Set text for cue '%s' (counter=%u): '%s'",
            GST_STR_NULL (cue->cue_id), cue->counter, cue->text);
      } else {
        GST_WARNING_OBJECT (subparse,
            "No text for cue '%s' (counter=%u), setting empty text",
            GST_STR_NULL (cue->cue_id), cue->counter);
        g_free (cue->text);
        cue->text = g_strdup ("");
      }

      if (cue->text && cue->text[0] != '\0' &&
          g_strcmp0 (cue->text, "(null)") != 0 &&
          GST_CLOCK_TIME_IS_VALID (cue->start_time) &&
          GST_CLOCK_TIME_IS_VALID (cue->end_time)) {
        GST_DEBUG_OBJECT (subparse,
            "Validated cue '%s' (counter=%u): start=%" GST_TIME_FORMAT ", end=%"
            GST_TIME_FORMAT ", text='%s'", GST_STR_NULL (cue->cue_id),
            cue->counter, GST_TIME_ARGS (cue->start_time),
            GST_TIME_ARGS (cue->end_time), cue->text);
      } else {
        GST_DEBUG_OBJECT (subparse,
            "Removing invalid cue '%s' (counter=%u): start=%" GST_TIME_FORMAT
            ", end=%" GST_TIME_FORMAT ", text='%s'", GST_STR_NULL (cue->cue_id),
            cue->counter, GST_TIME_ARGS (cue->start_time),
            GST_TIME_ARGS (cue->end_time), GST_STR_NULL (cue->text));
        state->active_cues = g_list_remove (state->active_cues, cue);
        free_webvtt_cue (cue);
      }

      g_string_truncate (state->buf, 0);
      g_free (state->cue_id);
      state->cue_id = NULL;
      state->start_time = GST_CLOCK_TIME_NONE;
      state->duration = GST_CLOCK_TIME_NONE;
      g_free (state->position);
      g_free (state->line);
      g_free (state->alignment);
      g_free (state->region_id);
      state->position = state->line = state->alignment = state->region_id =
          NULL;
#ifdef HAVE_WEBVTT_CSS
      if (subparse->css_parse)
        gst_cssparse_reinit (subparse->css_parse);
#endif
      state->state = 0;

      GST_DEBUG_OBJECT (subparse, "Current active cues count: %d",
          g_list_length (state->active_cues));
      g_free (trimmed_line);
      return NULL;
    }

    if (!state->buf) {
      state->buf = g_string_new (NULL);
    }
    g_string_append (state->buf, trimmed_line);
    g_string_append_c (state->buf, '\n');
    g_free (trimmed_line);
    return NULL;
  }

  GST_WARNING_OBJECT (subparse, "Unexpected state %d for line '%s'",
      state->state, trimmed_line);
  g_free (trimmed_line);
  return NULL;
}
