/* GStreamer
 * Copyright (C) 2026 Aaron Boxer <aaron.boxer@collabora.com>
 *
 * gstwebvttoverlay.c: WebVTT overlay support for basetextoverlay
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
#include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/video-overlay-composition.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstbasetextoverlay.h"

/* Debug category defined in gstbasetextoverlay.c */
GST_DEBUG_CATEGORY_EXTERN (base_text_overlay_debug);
#define GST_CAT_DEFAULT base_text_overlay_debug

/* Lock/unlock macros matching those in gstbasetextoverlay.c */
#define GST_BASE_TEXT_OVERLAY_GET_LOCK(ov) (&GST_BASE_TEXT_OVERLAY(ov)->lock)
#define GST_BASE_TEXT_OVERLAY_GET_COND(ov) (&GST_BASE_TEXT_OVERLAY(ov)->cond)
#define GST_BASE_TEXT_OVERLAY_LOCK(ov)                                         \
  (g_mutex_lock(GST_BASE_TEXT_OVERLAY_GET_LOCK(ov)))
#define GST_BASE_TEXT_OVERLAY_UNLOCK(ov)                                       \
  (g_mutex_unlock(GST_BASE_TEXT_OVERLAY_GET_LOCK(ov)))
#define GST_BASE_TEXT_OVERLAY_BROADCAST(ov)                                    \
  (g_cond_broadcast(GST_BASE_TEXT_OVERLAY_GET_COND(ov)))

void gst_base_text_overlay_web_vtt_attributes_init
    (GstBaseTextOverlayWebVTTAttributes * attrs)
{
  attrs->cue_align = NULL;
  attrs->align = NULL;
  attrs->vertical = NULL;
  attrs->cue_id = NULL;
  attrs->region_width = 100.0;
  attrs->region_lines = 3;
  attrs->regionanchor_x = 0.0;
  attrs->regionanchor_y = 0.0;
  attrs->viewportanchor_x = 0.0;
  attrs->viewportanchor_y = 0.0;
  attrs->scroll = g_strdup ("none");
  attrs->counter = 0;
  attrs->end_time = GST_CLOCK_TIME_NONE;
  attrs->valid = FALSE;
}

void gst_base_text_overlay_web_vtt_attributes_clear
    (GstBaseTextOverlayWebVTTAttributes * attrs)
{
  g_free (attrs->cue_align);
  g_free (attrs->align);
  g_free (attrs->vertical);
  g_free (attrs->scroll);
  g_free (attrs->cue_id);
  attrs->cue_align = NULL;
  attrs->align = NULL;
  attrs->vertical = NULL;
  attrs->scroll = NULL;
  attrs->cue_id = NULL;
  attrs->end_time = GST_CLOCK_TIME_NONE;
  attrs->valid = FALSE;
}

void
gst_base_text_overlay_text_span_clear (GstBaseTextOverlayTextSpan * span)
{
  gst_base_text_overlay_web_vtt_attributes_clear (&span->attrs);
  g_free (span->pango_markup);
  if (span->text_image) {
    gst_buffer_unref (span->text_image);
    span->text_image = NULL;
  }
  span->start_time = GST_CLOCK_TIME_NONE;       /* Clear start_time */
  span->need_render = FALSE;    /* Reset need_render */
  g_free (span);
}

gboolean
gst_base_text_overlay_parse_vtt_comment (GstBaseTextOverlay * overlay,
    const gchar * comment, GstBaseTextOverlayWebVTTAttributes * attrs)
{
  gchar **tokens, **token;
  gboolean valid = FALSE;

  GST_LOG_OBJECT (overlay, "Parsing VTT comment: '%s'", comment);

  gst_base_text_overlay_web_vtt_attributes_clear (attrs);

  /* Restore defaults after clear */
  attrs->scroll = g_strdup ("none");
  attrs->region_width = 100.0;
  attrs->region_lines = 3;

  if (!g_str_has_prefix (comment, "<!--vtt") ||
      !g_str_has_suffix (comment, "-->")) {
    GST_WARNING_OBJECT (overlay, "Invalid VTT comment format: '%s'", comment);
    return FALSE;
  }

  gchar *content = g_strndup (comment + 7, strlen (comment) - 10);
  tokens = g_strsplit (content, " ", -1);
  g_free (content);

  for (token = tokens; *token; token++) {
    gchar **kv = g_strsplit (*token, "=", 2);
    if (g_strv_length (kv) != 2) {
      GST_LOG_OBJECT (overlay, "Skipping invalid attribute: '%s'", *token);
      g_strfreev (kv);
      continue;
    }

    gchar *value = g_strstrip (g_strdup (kv[1]));
    if (value[0] == '"' && value[strlen (value) - 1] == '"') {
      value[strlen (value) - 1] = '\0';
      memmove (value, value + 1, strlen (value));
    }

    if (g_strcmp0 (kv[0], "align") == 0) {
      attrs->align = g_strdup (value);
      valid = TRUE;
    } else if (g_strcmp0 (kv[0], "cue_align") == 0) {
      attrs->cue_align = g_strdup (value);
      valid = TRUE;
    } else if (g_strcmp0 (kv[0], "width") == 0) {
      if (g_str_has_suffix (value, "%")) {
        attrs->region_width = g_ascii_strtod (value, NULL);
        valid = TRUE;
      }
    } else if (g_strcmp0 (kv[0], "lines") == 0) {
      attrs->region_lines = (gint) g_ascii_strtoll (value, NULL, 10);
      valid = TRUE;
    } else if (g_strcmp0 (kv[0], "regionanchor") == 0) {
      gchar **coords = g_strsplit (value, ",", 2);
      if (g_strv_length (coords) == 2) {
        attrs->regionanchor_x = g_ascii_strtod (coords[0], NULL);
        attrs->regionanchor_y = g_ascii_strtod (coords[1], NULL);
        valid = TRUE;
      }
      g_strfreev (coords);
    } else if (g_strcmp0 (kv[0], "viewportanchor") == 0) {
      gchar **coords = g_strsplit (value, ",", 2);
      if (g_strv_length (coords) == 2) {
        attrs->viewportanchor_x = g_ascii_strtod (coords[0], NULL);
        attrs->viewportanchor_y = g_ascii_strtod (coords[1], NULL);
        valid = TRUE;
      }
      g_strfreev (coords);
    } else if (g_strcmp0 (kv[0], "scroll") == 0) {
      g_free (attrs->scroll);
      attrs->scroll = g_strdup (value);
      valid = TRUE;
    } else if (g_strcmp0 (kv[0], "cue_id") == 0) {
      attrs->cue_id = g_strdup (value);
      valid = TRUE;
    } else if (g_strcmp0 (kv[0], "counter") == 0) {
      attrs->counter = (guint) g_ascii_strtoull (value, NULL, 10);
      valid = TRUE;
    } else if (g_strcmp0 (kv[0], "end_time") == 0) {
      attrs->end_time = g_ascii_strtoull (value, NULL, 10);
      valid = TRUE;
    } else if (g_strcmp0 (kv[0], "vertical") == 0) {
      attrs->vertical = g_strdup (value);
      valid = TRUE;
      /* Set vertical-render property based on vertical setting */
      if (g_strcmp0 (value, "rl") == 0 || g_strcmp0 (value, "lr") == 0) {
        overlay->use_vertical_render = TRUE;
      } else {
        overlay->use_vertical_render = FALSE;
      }
    } else {
      GST_LOG_OBJECT (overlay, "Ignoring unknown attribute: %s=%s", kv[0],
          value);
    }

    g_free (value);
    g_strfreev (kv);
  }

  g_strfreev (tokens);
  attrs->valid = valid;

  if (valid) {
    GST_DEBUG_OBJECT (overlay,
        "VTT comment: align=%s viewportanchor=%.1f%%,%.1f%% scroll=%s",
        GST_STR_NULL (attrs->align), attrs->viewportanchor_x,
        attrs->viewportanchor_y, GST_STR_NULL (attrs->scroll));
  }

  return valid;
}

/* Create a new text span with the given markup and counter.
 * Takes ownership of the markup string. */
GstBaseTextOverlayTextSpan *
gst_base_text_overlay_new_text_span (gchar * markup, guint counter,
    GstClockTime start_time)
{
  GstBaseTextOverlayTextSpan *span = g_new0 (GstBaseTextOverlayTextSpan, 1);
  gst_base_text_overlay_web_vtt_attributes_init (&span->attrs);
  span->pango_markup = markup;
  span->attrs.cue_id = g_strdup_printf ("span_%u", counter);
  span->attrs.counter = counter;
  span->start_time = start_time;
  span->need_render = TRUE;
  span->attrs.valid = TRUE;
  return span;
}

/* Merge old spans into new spans, preserving cached render data for unchanged
 * spans */
void
gst_base_text_overlay_merge_old_spans (GList * old_spans, GList * new_spans)
{
  for (GList * l = old_spans; l; l = l->next) {
    GstBaseTextOverlayTextSpan *old_span = l->data;
    gboolean found = FALSE;
    for (GList * m = new_spans; m; m = m->next) {
      GstBaseTextOverlayTextSpan *new_span = m->data;
      if (g_strcmp0 (old_span->pango_markup, new_span->pango_markup) == 0 &&
          attrs_equal_for_scrolling (&old_span->attrs, &new_span->attrs)) {
        found = TRUE;
        new_span->start_time = old_span->start_time;
        new_span->need_render = FALSE;
        if (old_span->text_image) {
          new_span->text_image = gst_buffer_ref (old_span->text_image);
        }
        new_span->ink_rect = old_span->ink_rect;
        new_span->logical_rect = old_span->logical_rect;
        new_span->text_width = old_span->text_width;
        new_span->text_height = old_span->text_height;
        new_span->text_x = old_span->text_x;
        new_span->text_y = old_span->text_y;
        break;
      }
    }
    if (!found) {
      gst_base_text_overlay_text_span_clear (old_span);
    }
  }
}

GstBaseTextOverlayTextSpan *
gst_base_text_overlay_text_span_copy (GstBaseTextOverlayTextSpan * span,
    gpointer user_data)
{
  if (!span) {
    return NULL;
  }

  GstBaseTextOverlayTextSpan *copy = g_new0 (GstBaseTextOverlayTextSpan, 1);
  copy->pango_markup = g_strdup (span->pango_markup);
  copy->text_image =
      span->text_image ? gst_buffer_ref (span->text_image) : NULL;
  copy->ink_rect = span->ink_rect;
  copy->logical_rect = span->logical_rect;
  copy->text_width = span->text_width;
  copy->text_height = span->text_height;
  copy->text_x = span->text_x;
  copy->text_y = span->text_y;
  copy->start_time = span->start_time;
  copy->need_render = span->need_render;

  /* Copy WebVTT attributes */
  copy->attrs.cue_align = g_strdup (span->attrs.cue_align);
  copy->attrs.align = g_strdup (span->attrs.align);
  copy->attrs.vertical = g_strdup (span->attrs.vertical);
  copy->attrs.region_width = span->attrs.region_width;
  copy->attrs.region_lines = span->attrs.region_lines;
  copy->attrs.regionanchor_x = span->attrs.regionanchor_x;
  copy->attrs.regionanchor_y = span->attrs.regionanchor_y;
  copy->attrs.viewportanchor_x = span->attrs.viewportanchor_x;
  copy->attrs.viewportanchor_y = span->attrs.viewportanchor_y;
  copy->attrs.scroll = g_strdup (span->attrs.scroll);
  copy->attrs.cue_id = g_strdup (span->attrs.cue_id);
  copy->attrs.counter = span->attrs.counter;
  copy->attrs.valid = span->attrs.valid;

  GST_LOG ("Copied span: ID=%s, counter=%u", GST_STR_NULL (copy->attrs.cue_id),
      copy->attrs.counter);
  return copy;
}

void
gst_base_text_overlay_get_pos (GstBaseTextOverlay * overlay,
    GstBaseTextOverlayTextSpan * span, gint * xpos,
    gint * ypos, GstClockTime current_time)
{
  gint width, height;
  GstBaseTextOverlayWebVTTAttributes *attrs = &span->attrs;
  GST_DEBUG_OBJECT (overlay,
      "Calculating position for span with cue_id=%s, scroll=%s, valid=%d, "
      "align=%s, cue_align=%s, start_time=%" GST_TIME_FORMAT,
      GST_STR_NULL (attrs->cue_id), GST_STR_NULL (attrs->scroll), attrs->valid,
      GST_STR_NULL (attrs->align), GST_STR_NULL (attrs->cue_align),
      GST_TIME_ARGS (span->start_time));

  /* Skip invalid spans with empty rectangles */
  if (span->logical_rect.width <= 0 || span->logical_rect.height <= 0 ||
      span->ink_rect.width <= 0 || span->ink_rect.height <= 0) {
    GST_LOG_OBJECT (overlay, "Skipping invalid span with empty rectangle");
    *xpos = 0;
    *ypos = 0;
    return;
  }

  width = span->logical_rect.width;
  height = span->logical_rect.height;
  *xpos = 0;
  *ypos = 0;

  /* Apply WebVTT positioning for cues with explicit positioning attributes or
   * alignment */
  if (attrs->valid &&
      ((attrs->scroll && *attrs->scroll != '\0' &&
              g_strcmp0 (attrs->scroll, "none") != 0) ||
          (attrs->cue_align && *attrs->cue_align != '\0') ||
          (attrs->align && *attrs->align != '\0') ||
          attrs->region_width != 100.0 || attrs->region_lines != 3 ||
          attrs->regionanchor_x != 0.0 || attrs->regionanchor_y != 0.0 ||
          (attrs->viewportanchor_x != 0.0 && attrs->viewportanchor_x != 50.0) ||
          (attrs->viewportanchor_y != 0.0
              && attrs->viewportanchor_y != 50.0))) {
    gfloat region_width_px = overlay->width * (attrs->region_width / 100.0);
    gfloat region_height_px = overlay->height * (attrs->region_lines * 0.1);
    gfloat viewport_x = overlay->width * (attrs->viewportanchor_x / 100.0);
    gfloat viewport_y = overlay->height * (attrs->viewportanchor_y / 100.0);
    gfloat region_x = region_width_px * (attrs->regionanchor_x / 100.0);
    gfloat region_y = region_height_px * (attrs->regionanchor_y / 100.0);

    /* Position region relative to viewport, align text to bottom of region */
    *xpos = viewport_x - region_x;
    *ypos = viewport_y - region_y + (region_height_px - height);
    GST_LOG_OBJECT (overlay,
        "Initial WebVTT position: xpos=%d, ypos=%d, "
        "region_width=%.1fpx, region_height=%.1fpx, text_height=%d",
        *xpos, *ypos, region_width_px, region_height_px, height);

    /* Apply cue alignment within region */
    if (attrs->cue_align && *attrs->cue_align != '\0') {
      if (g_strcmp0 (attrs->cue_align, "start") == 0) {
        *xpos += overlay->xpad;
        *xpos = MAX (0, *xpos);
      } else if (g_strcmp0 (attrs->cue_align, "middle") == 0) {
        *xpos += (region_width_px - width) / 2;
      } else if (g_strcmp0 (attrs->cue_align, "end") == 0) {
        *xpos += region_width_px - width - overlay->xpad;
        *xpos = MIN (overlay->width - width, *xpos);
      }
      GST_LOG_OBJECT (overlay, "Applied cue_align=%s, adjusted xpos=%d",
          attrs->cue_align, *xpos);
    } else if (attrs->align && *attrs->align != '\0') {
      /* Apply CSS text-align if present */
      if (g_strcmp0 (attrs->align, "left") == 0) {
        *xpos += overlay->xpad;
        *xpos = MAX (0, *xpos);
      } else if (g_strcmp0 (attrs->align, "center") == 0) {
        *xpos += (region_width_px - width) / 2;
      } else if (g_strcmp0 (attrs->align, "right") == 0 ||
          g_strcmp0 (attrs->align, "end") == 0) {
        *xpos += region_width_px - width - overlay->xpad;
        *xpos = MIN (overlay->width - width, *xpos);
      }
      GST_LOG_OBJECT (overlay, "Applied CSS align=%s, adjusted xpos=%d",
          attrs->align, *xpos);
    } else {
      /* Default to center alignment within region */
      *xpos += (region_width_px - width) / 2;
      GST_DEBUG_OBJECT (overlay,
          "No align specified, defaulting to center, xpos=%d", *xpos);
    }

    /* Apply scrolling if enabled */
    if (g_strcmp0 (attrs->scroll, "up") == 0 ||
        g_strcmp0 (attrs->scroll, "down") == 0) {
      GST_DEBUG_OBJECT (overlay,
          "Scroll attribute detected: %s, current_time=%" GST_TIME_FORMAT
          ", span->start_time=%" GST_TIME_FORMAT,
          attrs->scroll, GST_TIME_ARGS (current_time),
          GST_TIME_ARGS (span->start_time));
      if (GST_CLOCK_TIME_IS_VALID (current_time) &&
          GST_CLOCK_TIME_IS_VALID (span->start_time)) {
        gdouble elapsed = 0.0;
        if (current_time >= span->start_time) {
          elapsed = (gdouble) (current_time - span->start_time) / GST_SECOND;
        } else {
          GST_WARNING_OBJECT (overlay,
              "Current time %" GST_TIME_FORMAT
              " is less than span start time %" GST_TIME_FORMAT,
              GST_TIME_ARGS (current_time), GST_TIME_ARGS (span->start_time));
          elapsed = 0.0;
        }
        gdouble scroll_distance = elapsed * overlay->scroll_speed;
        gdouble scroll_offset_y;
        if (g_strcmp0 (attrs->scroll, "up") == 0) {
          scroll_offset_y = -scroll_distance;   /* Scroll up: decrease y */
        } else {                /* down */
          scroll_offset_y = scroll_distance;    /* Scroll down: increase y */
        }
        /* Clamp scroll offset to region boundaries */
        scroll_offset_y =
            CLAMP (scroll_offset_y, -(region_height_px - height), 0.0);
        *ypos += (gint) scroll_offset_y;
        GST_DEBUG_OBJECT (overlay,
            "Applied scroll %s: offset_y=%.2f, new ypos=%d",
            attrs->scroll, scroll_offset_y, *ypos);
      } else {
        GST_WARNING_OBJECT (overlay,
            "Skipping scroll due to invalid timestamps: "
            "current_time=%" GST_TIME_FORMAT ", span_start=%" GST_TIME_FORMAT,
            GST_TIME_ARGS (current_time), GST_TIME_ARGS (span->start_time));
      }
    }

    /* Clamp to video bounds */
    *xpos = CLAMP (*xpos, 0, overlay->width - span->ink_rect.width);
    *ypos = CLAMP (*ypos, 0, overlay->height - span->ink_rect.height);
    GST_DEBUG_OBJECT (overlay,
        "Using WebVTT positioning: cue_align='%s', align='%s', "
        "region_width=%.1fpx, "
        "regionanchor=(%.1f%%,%.1f%%), viewportanchor=(%.1f%%,%.1f%%), "
        "scroll='%s', pos=(%d,%d)",
        GST_STR_NULL (attrs->cue_align), GST_STR_NULL (attrs->align),
        region_width_px, attrs->regionanchor_x, attrs->regionanchor_y,
        attrs->viewportanchor_x, attrs->viewportanchor_y, attrs->scroll, *xpos,
        *ypos);
  } else {
    GST_DEBUG_OBJECT (overlay,
        "Using fallback positioning: no valid WebVTT positioning "
        "attributes (valid=%d)", attrs->valid);
    /* Fallback to default alignment logic, using bottom-center for
     * non-positioned cues */
    switch (overlay->halign) {
      case GST_BASE_TEXT_OVERLAY_HALIGN_LEFT:
        *xpos = overlay->xpad;
        break;
      case GST_BASE_TEXT_OVERLAY_HALIGN_CENTER:
        *xpos = (overlay->width - width) / 2;
        break;
      case GST_BASE_TEXT_OVERLAY_HALIGN_RIGHT:
        *xpos = overlay->width - width - overlay->xpad;
        break;
      case GST_BASE_TEXT_OVERLAY_HALIGN_POS:
        *xpos = (gint) (overlay->width * overlay->xpos) - width / 2;
        *xpos = CLAMP (*xpos, 0, overlay->width - width);
        break;
      case GST_BASE_TEXT_OVERLAY_HALIGN_ABSOLUTE:
        *xpos = (overlay->width - width) * overlay->xpos;
        break;
      default:
        *xpos = 0;
        break;
    }
    *xpos += overlay->deltax;

    /* Force bottom alignment for non-positioned cues */
    *ypos = overlay->height - height - overlay->ypad;
    *ypos += overlay->deltay;

    /* Apply CSS text-align for WebVTT cues */
    if (attrs->valid && attrs->align && *attrs->align != '\0') {
      if (g_strcmp0 (attrs->align, "left") == 0) {
        *xpos = overlay->xpad;
      } else if (g_strcmp0 (attrs->align, "center") == 0) {
        *xpos = (overlay->width - width) / 2;
      } else if (g_strcmp0 (attrs->align, "right") == 0 ||
          g_strcmp0 (attrs->align, "end") == 0) {
        *xpos = overlay->width - width - overlay->xpad;
      }
      GST_DEBUG_OBJECT (overlay,
          "Applied CSS align=%s in fallback, adjusted xpos=%d",
          attrs->align, *xpos);
    }

    GST_DEBUG_OBJECT (overlay,
        "Using fallback positioning: halign=%d, valign=bottom, pos=(%d,%d)",
        overlay->halign, *xpos, *ypos);
  }

  span->text_x = *xpos;
  span->text_y = *ypos;
  GST_LOG_OBJECT (overlay, "Final overlay position: (%d, %d)", *xpos, *ypos);
}

/* Helper compare function */
gint
compare_counters (gconstpointer a, gconstpointer b)
{
  guint aa = GPOINTER_TO_UINT (a);
  guint bb = GPOINTER_TO_UINT (b);
  return (aa > bb) - (aa < bb);
}

void
gst_base_text_overlay_set_composition (GstBaseTextOverlay * overlay)
{
  GstVideoOverlayComposition *final_composition = NULL;
  gboolean composition_changed = FALSE;
  GList *current_counters = NULL;
  GList *prev_counters = NULL;
  GList *valid_spans = NULL;
  GList *tmp;
  gboolean counters_match = TRUE;
  GstClockTime current_time = overlay->segment.position;

  GST_DEBUG_OBJECT (overlay,
      "Starting composition update at time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (current_time));

  /* Validate overlay object */
  if (!GST_IS_BASE_TEXT_OVERLAY (overlay)) {
    GST_ERROR_OBJECT (overlay, "Invalid overlay object");
    return;
  }

  GST_BASE_TEXT_OVERLAY_LOCK (overlay);
  /* Check if text_spans is valid */
  if (!overlay->text_spans) {
    GST_DEBUG_OBJECT (overlay,
        "text_spans is empty, creating empty composition");
    final_composition = gst_video_overlay_composition_new (NULL);
    goto update_composition;
  }

  /* Validate text_spans list integrity */
  for (GList * l = overlay->text_spans; l; l = l->next) {
    if (!l || !l->data) {
      GST_WARNING_OBJECT (overlay,
          "Found invalid node in text_spans at %p, clearing list", l);
      g_list_free_full (overlay->text_spans,
          (GDestroyNotify) gst_base_text_overlay_text_span_clear);
      overlay->text_spans = NULL;
      final_composition = gst_video_overlay_composition_new (NULL);
      goto update_composition;
    }
  }

  /* Collect current counters */
  for (GList * l = overlay->text_spans; l; l = l->next) {
    GstBaseTextOverlayTextSpan *span = (GstBaseTextOverlayTextSpan *) l->data;
    if (!span || !span->attrs.cue_id || !span->pango_markup ||
        !span->attrs.valid) {
      GST_WARNING_OBJECT (overlay,
          "Invalid span at %p: cue_id=%p, pango_markup=%p, valid=%d", span,
          span ? span->attrs.cue_id : NULL, span ? span->pango_markup : NULL,
          span ? span->attrs.valid : 0);
      continue;
    }
    current_counters =
        g_list_append (current_counters,
        GUINT_TO_POINTER (span->attrs.counter));
    GST_DEBUG_OBJECT (overlay,
        "Current span: counter=%u, ID=%s, need_render=%d, text_image=%p",
        span->attrs.counter, GST_STR_NULL (span->attrs.cue_id),
        span->need_render, span->text_image);
  }
  current_counters =
      g_list_sort (current_counters, (GCompareFunc) compare_counters);

  /* Collect previous counters */
  for (GList * l = overlay->prev_text_spans; l; l = l->next) {
    GstBaseTextOverlayTextSpan *span = (GstBaseTextOverlayTextSpan *) l->data;
    if (!l || !l->data || !span || !span->attrs.cue_id || !span->pango_markup ||
        !span->attrs.valid) {
      GST_WARNING_OBJECT (overlay,
          "Invalid prev span at %p: cue_id=%p, pango_markup=%p, valid=%d", span,
          span ? span->attrs.cue_id : NULL, span ? span->pango_markup : NULL,
          span ? span->attrs.valid : 0);
      continue;
    }
    prev_counters =
        g_list_append (prev_counters, GUINT_TO_POINTER (span->attrs.counter));
    GST_LOG_OBJECT (overlay, "Prev span: counter=%u, ID=%s",
        span->attrs.counter, GST_STR_NULL (span->attrs.cue_id));
  }
  prev_counters = g_list_sort (prev_counters, (GCompareFunc) compare_counters);

  /* Compare counters */
  tmp = current_counters;
  for (GList * l = prev_counters; l && tmp; l = l->next, tmp = tmp->next) {
    if (GPOINTER_TO_UINT (l->data) != GPOINTER_TO_UINT (tmp->data)) {
      counters_match = FALSE;
      GST_DEBUG_OBJECT (overlay, "Counter mismatch: prev %u != current %u",
          GPOINTER_TO_UINT (l->data), GPOINTER_TO_UINT (tmp->data));
      break;
    }
  }
  if (g_list_length (current_counters) != g_list_length (prev_counters)) {
    counters_match = FALSE;
    GST_DEBUG_OBJECT (overlay,
        "Counter list length mismatch: current %d, prev %d",
        g_list_length (current_counters), g_list_length (prev_counters));
  }
  g_list_free (current_counters);
  g_list_free (prev_counters);

  /* Check for scrolling spans */
  gboolean has_scrolling = FALSE;
  for (GList * l = overlay->text_spans; l; l = l->next) {
    GstBaseTextOverlayTextSpan *span = (GstBaseTextOverlayTextSpan *) l->data;
    if (!l || !l->data || !span || !span->attrs.cue_id || !span->pango_markup ||
        !span->attrs.valid) {
      GST_WARNING_OBJECT (overlay,
          "Invalid span (scroll check) at %p: cue_id=%p, "
          "pango_markup=%p, valid=%d",
          span, span ? span->attrs.cue_id : NULL,
          span ? span->pango_markup : NULL, span ? span->attrs.valid : 0);
      continue;
    }
    if (g_strcmp0 (span->attrs.scroll, "up") == 0 ||
        g_strcmp0 (span->attrs.scroll, "down") == 0) {
      has_scrolling = TRUE;
      GST_DEBUG_OBJECT (overlay,
          "Scrolling detected for span ID %s (scroll: %s)",
          GST_STR_NULL (span->attrs.cue_id), span->attrs.scroll);
      break;
    }
  }

  if (counters_match && overlay->prev_composition && !has_scrolling) {
    GST_LOG_OBJECT (overlay,
        "Counters match and no scrolling, keeping cached composition");
    GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
    return;
  }

  GST_DEBUG_OBJECT (overlay,
      "Rebuilding composition (counters_match: %d, has_scrolling: %d)",
      counters_match, has_scrolling);

  /* Process current text spans */
  for (GList * l = overlay->text_spans; l; l = l->next) {
    GstBaseTextOverlayTextSpan *span;
    GstVideoOverlayComposition *span_composition = NULL;
    GstVideoOverlayRectangle *rectangle = NULL, *alt_rect = NULL;

    if (!l || !l->data) {
      GST_WARNING_OBJECT (overlay,
          "NULL node or data in text_spans at %p, skipping", l);
      continue;
    }
    span = (GstBaseTextOverlayTextSpan *) l->data;
    if (!span || !span->attrs.cue_id || !span->pango_markup ||
        !span->attrs.valid) {
      GST_WARNING_OBJECT (overlay,
          "Invalid span at %p: cue_id=%p, pango_markup=%p, valid=%d", span,
          span ? span->attrs.cue_id : NULL, span ? span->pango_markup : NULL,
          span ? span->attrs.valid : 0);
      continue;
    }

    /* Check if text_image is valid */
    if (!span->text_image) {
      GST_LOG_OBJECT (overlay, "No text image for span ID %s, skipping",
          GST_STR_NULL (span->attrs.cue_id));
      continue;
    }

    /* Validate rectangles */
    gboolean span_valid = span->logical_rect.width > 0 &&
        span->logical_rect.height > 0 &&
        span->ink_rect.width > 0 && span->ink_rect.height > 0;
    if (!span_valid) {
      GST_WARNING_OBJECT (overlay,
          "Invalid rectangles for span ID %s: "
          "logical_rect=(%d,%d,%d,%d), ink_rect=(%d,%d,%d,%d)",
          GST_STR_NULL (span->attrs.cue_id), span->logical_rect.x,
          span->logical_rect.y, span->logical_rect.width,
          span->logical_rect.height, span->ink_rect.x,
          span->ink_rect.y, span->ink_rect.width, span->ink_rect.height);
      continue;
    }

    valid_spans = g_list_prepend (valid_spans, span);

    gint xpos, ypos, alt_xpos, alt_ypos;
    gchar *span_id;

    if (!span->text_image) {
      GST_LOG_OBJECT (overlay, "No text image for span with ID %s, skipping",
          GST_STR_NULL (span->attrs.cue_id));
      continue;
    }

    /* Use temporary span ID */
    span_id = span->attrs.cue_id ? g_strdup (span->attrs.cue_id)
        : g_strdup_printf ("span_%p", span);
    GST_DEBUG_OBJECT (overlay,
        "Processing span ID %s (need_render: %d, scroll: %s)", span_id,
        span->need_render, GST_STR_NULL (span->attrs.scroll));

    /* Calculate position with current time */
    gst_base_text_overlay_get_pos (overlay, span, &xpos, &ypos, current_time);
    GST_LOG_OBJECT (overlay, "Calculated position for span ID %s: (%d, %d)",
        span_id, xpos, ypos);

    gint render_width = span->ink_rect.width;
    gint render_height = span->ink_rect.height;

    /* Create new rectangle for the span */
    span->text_image = gst_buffer_make_writable (span->text_image);
    if (!span->text_image) {
      GST_WARNING_OBJECT (overlay,
          "Failed to make text_image writable for span ID %s", span_id);
      g_free (span_id);
      continue;
    }
    gst_buffer_add_video_meta (span->text_image, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_OVERLAY_COMPOSITION_FORMAT_RGB,
        span->text_width, span->text_height);
    rectangle =
        gst_video_overlay_rectangle_new_raw (span->text_image, xpos, ypos,
        render_width, render_height,
        GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
    GST_LOG_OBJECT (overlay, "Created rectangle for span ID %s at (%d, %d)",
        span_id, xpos, ypos);

    /* Create or update span composition */
    span_composition = gst_video_overlay_composition_new (rectangle);
    g_hash_table_insert (overlay->span_compositions, g_strdup (span_id),
        span_composition);
    composition_changed = TRUE;
    GST_LOG_OBJECT (overlay, "Inserted/updated span composition for ID %s",
        span_id);

    /* Handle alternate rendering if enabled */
    if (overlay->alt_render && rectangle) {
      gint num_x = (overlay->window_width - xpos) / render_width;
      gint num_y = (overlay->window_height - ypos) / render_height;
      if (num_x < 2 && num_y < 2) {
        GST_WARNING_OBJECT (overlay,
            "Not enough space for alternate position for span ID %s", span_id);
      } else {
        alt_xpos = xpos + (overlay->alt_position_idx / num_y) * render_width;
        alt_ypos = ypos + (overlay->alt_position_idx % num_y) * render_height;
        GstBuffer *alt_text_image =
            gst_buffer_make_writable (gst_buffer_ref (span->text_image));
        if (alt_text_image) {
          alt_rect =
              gst_video_overlay_rectangle_new_raw (alt_text_image, alt_xpos,
              alt_ypos, render_width, render_height,
              GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA);
          gst_buffer_unref (alt_text_image);
          GST_DEBUG_OBJECT (overlay,
              "Created alternate rectangle for span ID %s at (%d, %d)", span_id,
              alt_xpos, alt_ypos);
          gst_video_overlay_composition_add_rectangle (span_composition,
              alt_rect);
          gst_video_overlay_rectangle_unref (alt_rect);
        } else {
          GST_WARNING_OBJECT (overlay,
              "Failed to make writable copy for alternate "
              "rectangle for span ID %s", span_id);
        }
        overlay->alt_position_idx++;
        overlay->alt_position_idx %= (num_x * num_y);
      }
    }

    if (rectangle) {
      if (!final_composition) {
        final_composition = gst_video_overlay_composition_new (rectangle);
      } else {
        gst_video_overlay_composition_add_rectangle (final_composition,
            rectangle);
      }
      gst_video_overlay_rectangle_unref (rectangle);
    }

    g_free (span_id);
  }

update_composition:
  /* Create an empty composition if no spans */
  if (!final_composition) {
    final_composition = gst_video_overlay_composition_new (NULL);
    GST_DEBUG_OBJECT (overlay, "Created empty final composition");
  }

  /* Update previous spans for comparison */
  valid_spans = g_list_reverse (valid_spans);
  g_list_free_full (overlay->prev_text_spans,
      (GDestroyNotify) gst_base_text_overlay_text_span_clear);
  overlay->prev_text_spans =
      g_list_copy_deep (valid_spans,
      (GCopyFunc) gst_base_text_overlay_text_span_copy, NULL);
  g_list_free (valid_spans);
  GST_DEBUG_OBJECT (overlay, "Updated prev_text_spans (length: %d)",
      g_list_length (overlay->prev_text_spans));

  /* Store the final composition */
  if (composition_changed) {
    GST_DEBUG_OBJECT (overlay,
        "Composition changed, updating final composition");
    if (overlay->prev_composition) {
      gst_video_overlay_composition_unref (overlay->prev_composition);
    }
    overlay->prev_composition = final_composition;
  } else {
    gst_video_overlay_composition_unref (final_composition);
  }

  GST_BASE_TEXT_OVERLAY_UNLOCK (overlay);
}

gboolean
attrs_equal_for_scrolling (const GstBaseTextOverlayWebVTTAttributes * a,
    const GstBaseTextOverlayWebVTTAttributes * b)
{
  return g_strcmp0 (a->cue_align, b->cue_align) == 0 &&
      g_strcmp0 (a->align, b->align) == 0 &&
      g_strcmp0 (a->vertical, b->vertical) == 0 &&
      a->region_width == b->region_width &&
      a->region_lines == b->region_lines &&
      a->regionanchor_x == b->regionanchor_x &&
      a->regionanchor_y == b->regionanchor_y &&
      a->viewportanchor_x == b->viewportanchor_x &&
      a->viewportanchor_y == b->viewportanchor_y &&
      g_strcmp0 (a->scroll, b->scroll) == 0 && a->valid == b->valid;
}

/* Ensure the GstCustomMeta type is registered on the overlay side.
 * Must match the registration in subparse. */
static void
gst_webvtt_cue_meta_ensure_registered (void)
{
  static gsize registered = 0;
  if (g_once_init_enter (&registered)) {
    const gchar *tags[] = { NULL };
    gst_meta_register_custom (GST_WEBVTT_CUE_META_NAME, tags, NULL, NULL, NULL);
    g_once_init_leave (&registered, 1);
  }
}

/* Read WebVTT cue attributes from GstCustomMeta on a buffer and
 * create text spans. The buffer text is split by cue index — each cue's
 * pango markup is the portion of text between consecutive <!--vtt--> markers
 * (or, with meta, there are no markers, so we split by cue count).
 *
 * Returns a GList of GstBaseTextOverlayTextSpan (in reverse order;
 * caller should reverse). Returns NULL if no meta found. */
GList *
gst_base_text_overlay_spans_from_meta (GstBaseTextOverlay * overlay,
    GstBuffer * buffer,
    const gchar * text, gsize text_len,
    guint * span_counter, GstClockTime clip_start)
{
  GstCustomMeta *meta;
  const GstStructure *s;
  guint n_cues = 0;
  GList *new_spans = NULL;
  guint counter = *span_counter;

  gst_webvtt_cue_meta_ensure_registered ();

  meta = gst_buffer_get_custom_meta (buffer, GST_WEBVTT_CUE_META_NAME);
  if (!meta)
    return NULL;

  s = gst_custom_meta_get_structure (meta);
  if (!gst_structure_get_uint (s, "n-cues", &n_cues) || n_cues == 0) {
    GST_DEBUG_OBJECT (overlay, "WebVTT meta found but n-cues is 0");
    return NULL;
  }

  GST_DEBUG_OBJECT (overlay, "Found WebVTT meta with %u cues on buffer",
      n_cues);

  /* Each cue's pango markup is stored in the meta as cue-N-markup. */
  for (guint i = 0; i < n_cues; i++) {
    GstBaseTextOverlayTextSpan *span;
    gchar *field;
    gchar *str_val;
    guint uint_val;
    guint64 uint64_val;
    gdouble dbl_val;
    gint int_val;
    gchar *markup = NULL;

    /* Read per-cue markup from meta */
    field = g_strdup_printf ("cue-%u-markup", i);
    gst_structure_get (s, field, G_TYPE_STRING, &markup, NULL);
    g_free (field);

    if (!markup || markup[0] == '\0') {
      g_free (markup);
      /* Fallback: use entire buffer text */
      markup = g_strndup (text, text_len);
    }

    span = gst_base_text_overlay_new_text_span (markup, counter, clip_start);

    /* Populate WebVTT attributes from meta fields */
    gst_base_text_overlay_web_vtt_attributes_clear (&span->attrs);
    span->attrs.valid = TRUE;

    field = g_strdup_printf ("cue-%u-counter", i);
    if (gst_structure_get_uint (s, field, &uint_val))
      span->attrs.counter = uint_val;
    g_free (field);

    field = g_strdup_printf ("cue-%u-end-time", i);
    if (gst_structure_get_uint64 (s, field, &uint64_val))
      span->attrs.end_time = uint64_val;
    g_free (field);

    field = g_strdup_printf ("cue-%u-align", i);
    str_val = NULL;
    gst_structure_get (s, field, G_TYPE_STRING, &str_val, NULL);
    if (str_val)
      span->attrs.align = str_val;      /* take ownership */
    g_free (field);

    field = g_strdup_printf ("cue-%u-cue-align", i);
    str_val = NULL;
    gst_structure_get (s, field, G_TYPE_STRING, &str_val, NULL);
    if (str_val)
      span->attrs.cue_align = str_val;  /* take ownership */
    g_free (field);

    field = g_strdup_printf ("cue-%u-vertical", i);
    str_val = NULL;
    gst_structure_get (s, field, G_TYPE_STRING, &str_val, NULL);
    if (str_val) {
      span->attrs.vertical = str_val;   /* take ownership */
      if (g_strcmp0 (str_val, "rl") == 0 || g_strcmp0 (str_val, "lr") == 0) {
        overlay->use_vertical_render = TRUE;
      } else {
        overlay->use_vertical_render = FALSE;
      }
    }
    g_free (field);

    field = g_strdup_printf ("cue-%u-scroll", i);
    str_val = NULL;
    gst_structure_get (s, field, G_TYPE_STRING, &str_val, NULL);
    if (str_val) {
      g_free (span->attrs.scroll);
      span->attrs.scroll = str_val;     /* take ownership */
    }
    g_free (field);

    field = g_strdup_printf ("cue-%u-region-width", i);
    if (gst_structure_get_double (s, field, &dbl_val))
      span->attrs.region_width = (gfloat) dbl_val;
    g_free (field);

    field = g_strdup_printf ("cue-%u-region-lines", i);
    if (gst_structure_get_int (s, field, &int_val))
      span->attrs.region_lines = int_val;
    g_free (field);

    field = g_strdup_printf ("cue-%u-regionanchor-x", i);
    if (gst_structure_get_double (s, field, &dbl_val))
      span->attrs.regionanchor_x = (gfloat) dbl_val;
    g_free (field);

    field = g_strdup_printf ("cue-%u-regionanchor-y", i);
    if (gst_structure_get_double (s, field, &dbl_val))
      span->attrs.regionanchor_y = (gfloat) dbl_val;
    g_free (field);

    field = g_strdup_printf ("cue-%u-viewportanchor-x", i);
    if (gst_structure_get_double (s, field, &dbl_val))
      span->attrs.viewportanchor_x = (gfloat) dbl_val;
    g_free (field);

    field = g_strdup_printf ("cue-%u-viewportanchor-y", i);
    if (gst_structure_get_double (s, field, &dbl_val))
      span->attrs.viewportanchor_y = (gfloat) dbl_val;
    g_free (field);

    span->attrs.cue_id = g_strdup_printf ("span_%u", counter);

    GST_DEBUG_OBJECT (overlay,
        "Created span from meta: ID=%s, counter=%u, align=%s, scroll=%s",
        span->attrs.cue_id, span->attrs.counter,
        GST_STR_NULL (span->attrs.align), GST_STR_NULL (span->attrs.scroll));

    new_spans = g_list_prepend (new_spans, span);
    counter++;
  }

  *span_counter = counter;
  return new_spans;
}
