/* GStreamer
 * Copyright (C) 2026 Aaron Boxer <aaron.boxer@collabora.com>
 *
 * gstwebvttoverlay.h: WebVTT overlay support for basetextoverlay
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

#ifndef __GST_WEBVTT_OVERLAY_H__
#define __GST_WEBVTT_OVERLAY_H__

#include <gst/gst.h>
#include <pango/pangocairo.h>

G_BEGIN_DECLS
#define GST_WEBVTT_CUE_META_NAME "GstWebVTTCueMeta"
typedef struct
{
  gchar *cue_align;             /* e.g., "middle", "start", "end" */
  gchar *align;
  gchar *vertical;              /* vertical text setting */
  gfloat region_width;          /* e.g., 50.0 (percentage) */
  gint region_lines;            /* e.g., 3 */
  gfloat regionanchor_x;        /* e.g., 50.0 (percentage) */
  gfloat regionanchor_y;        /* e.g., 100.0 (percentage) */
  gfloat viewportanchor_x;      /* e.g., 50.0 (percentage) */
  gfloat viewportanchor_y;      /* e.g., 90.0 (percentage) */
  gchar *scroll;                /* e.g., "none", "up" */
  gboolean valid;               /* Whether the attributes are valid */
  gchar *cue_id;                /* Unique ID for the cue */
  guint counter;                /* Monotonically increasing counter from WebVTT comment */
  GstClockTime end_time;        /* End time from WebVTT comment in nanoseconds */
} GstBaseTextOverlayWebVTTAttributes;

typedef struct
{
  GstBaseTextOverlayWebVTTAttributes attrs;
  gchar *pango_markup;
  GstBuffer *text_image;
  PangoRectangle ink_rect;
  PangoRectangle logical_rect;
  gint text_width;
  gint text_height;
  gint text_x;
  gint text_y;
  GstClockTime start_time;      /* Start time for scrolling calculations */
  gboolean need_render;         /* Flag to indicate if span needs re-rendering */
} GstBaseTextOverlayTextSpan;

/* WebVTT attribute lifecycle */
void gst_base_text_overlay_web_vtt_attributes_init
    (GstBaseTextOverlayWebVTTAttributes * attrs);
void gst_base_text_overlay_web_vtt_attributes_clear
    (GstBaseTextOverlayWebVTTAttributes * attrs);

/* Text span lifecycle */
void gst_base_text_overlay_text_span_clear (GstBaseTextOverlayTextSpan * span);
GstBaseTextOverlayTextSpan *gst_base_text_overlay_new_text_span (gchar * markup,
    guint counter, GstClockTime start_time);
GstBaseTextOverlayTextSpan
    * gst_base_text_overlay_text_span_copy (GstBaseTextOverlayTextSpan * span,
    gpointer user_data);

/* WebVTT comment parsing */
gboolean gst_base_text_overlay_parse_vtt_comment (GstBaseTextOverlay * overlay,
    const gchar * comment, GstBaseTextOverlayWebVTTAttributes * attrs);

/* Span comparison and merging */
gboolean attrs_equal_for_scrolling (const GstBaseTextOverlayWebVTTAttributes *
    a, const GstBaseTextOverlayWebVTTAttributes * b);
void gst_base_text_overlay_merge_old_spans (GList * old_spans,
    GList * new_spans);
gint compare_counters (gconstpointer a, gconstpointer b);

/* Position calculation and composition */
void gst_base_text_overlay_get_pos (GstBaseTextOverlay * overlay,
    GstBaseTextOverlayTextSpan * span, gint * xpos, gint * ypos,
    GstClockTime current_time);
void gst_base_text_overlay_set_composition (GstBaseTextOverlay * overlay);

/* Read WebVTT cue attributes from GstCustomMeta on a buffer and
 * create text spans. Returns a GList of GstBaseTextOverlayTextSpan. */
GList *gst_base_text_overlay_spans_from_meta (GstBaseTextOverlay * overlay,
    GstBuffer * buffer, const gchar * text, gsize text_len,
    guint * span_counter, GstClockTime clip_start);

G_END_DECLS
#endif /* __GST_WEBVTT_OVERLAY_H__ */
