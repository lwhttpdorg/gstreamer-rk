/* GStreamer CSS parser
 * Copyright (c) 2025 Collabora Ltd.
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

#ifndef __GST_CSS_PARSE_H__
#define __GST_CSS_PARSE_H__
#ifdef HAVE_WEBVTT_CSS

#include <gst/gst.h>

G_BEGIN_DECLS
/* Opaque type — implemented in Rust (gstcssparse.rs) */
typedef struct GstCssParse_ GstCssParse;

#define GST_CSS_PARSE_OK 0

/**
 * gst_cssparse_new:
 *
 * Create a new CSS parser instance.
 *
 * Returns: (transfer full): A new #GstCssParse, free with gst_cssparse_free().
 */
GstCssParse *gst_cssparse_new (void);

/**
 * gst_cssparse_free:
 * @cssparse: The CSS parser instance.
 *
 * Free a CSS parser instance and all its resources.
 */
void gst_cssparse_free (GstCssParse * cssparse);

/**
 * gst_cssparse_reinit:
 * @cssparse: The CSS parser instance.
 *
 * Reset computed CSS properties to defaults. Accumulated CSS rules are kept.
 */
void gst_cssparse_reinit (GstCssParse * cssparse);

/**
 * gst_cssparse_set_cue_id:
 * @cssparse: The CSS parser instance.
 * @cue_id: (nullable): The cue ID for selector matching, or NULL.
 *
 * Set the current cue ID used for CSS selector matching (#id selectors).
 */
void gst_cssparse_set_cue_id (GstCssParse * cssparse, const gchar * cue_id);

/**
 * gst_cssparse_parse:
 * @cssparse: The CSS parser instance.
 * @css_data: (nullable): CSS text to parse, or empty string/"" to re-evaluate.
 *
 * Parse CSS data and apply matching rules for the current cue.
 * If @css_data is empty, just re-evaluates existing rules.
 *
 * Returns: 0 (GST_CSS_PARSE_OK) on success, non-zero on error.
 */
int gst_cssparse_parse (GstCssParse * cssparse, const gchar * css_data);

/**
 * gst_cssparse_to_pango_markup:
 * @cssparse: The CSS parser instance.
 * @text: The subtitle cue text (may contain HTML-like tags).
 *
 * Generate Pango markup from the parsed CSS properties and cue text.
 *
 * Returns: (transfer full): A newly allocated Pango markup string,
 *          free with g_free(). NULL on error.
 */
gchar *gst_cssparse_to_pango_markup (GstCssParse * cssparse,
    const gchar * text);

G_END_DECLS
#endif /* HAVE_WEBVTT_CSS */
#endif /* __GST_CSS_PARSE_H__ */
