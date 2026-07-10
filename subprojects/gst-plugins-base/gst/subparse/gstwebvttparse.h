/* GStreamer WebVTT parser
 * Copyright (C) 2025 Collabora Ltd.
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

#ifndef __GST_WEBVTT_PARSE_H__
#define __GST_WEBVTT_PARSE_H__

#include <gst/gst.h>

G_BEGIN_DECLS
/* Name used to register/lookup GstCustomMeta carrying WebVTT cue attributes.
 * Must match the name used by the consuming overlay element. */
#define GST_WEBVTT_CUE_META_NAME "GstWebVTTCueMeta"
/* WebVTT region: positioning metadata parsed from REGION blocks */
    typedef struct
{
  gchar *id;                    /* Region ID */
  gfloat width;                 /* Region width in percent */
  gint lines;                   /* Number of lines */
  gfloat regionanchor_x;        /* Region anchor X in percent */
  gfloat regionanchor_y;        /* Region anchor Y in percent */
  gfloat viewportanchor_x;      /* Viewport anchor X in percent */
  gfloat viewportanchor_y;      /* Viewport anchor Y in percent */
  gchar *scroll;                /* Scroll setting: "none" or "up" */
} GstSubParseVTTRegion;

/* WebVTT cue: a single timed subtitle with positioning metadata */
typedef struct _GstSubParseVTTCue
{
  gchar *cue_id;                /* Cue identifier */
  GstClockTime start_time;      /* Start timestamp */
  GstClockTime end_time;        /* End timestamp */
  gchar *text;                  /* Cue text */
  gchar *region_id;             /* Associated region ID */
  gchar *vertical;              /* Vertical setting */
  gchar *alignment;             /* Alignment setting */
  gchar *position;              /* Position setting */
  gchar *line;                  /* Line setting */
  guint8 text_position;         /* Text position percentage */
  guint8 text_size;             /* Text size percentage */
  gint16 line_position;         /* Line position percentage */
  gint16 line_number;           /* Line number */
  gboolean processed;           /* Track processed cues */
  guint counter;                /* Monotonically increasing counter */
} GstSubParseVTTCue;

/* Forward declarations — full definitions are in gstsubparse.h */
typedef struct _ParserState ParserState;
typedef struct _GstSubParse GstSubParse;

/* Parse WebVTT cue settings from the timestamp line (e.g. "align:start position:10%") */
void parse_webvtt_cue_settings (ParserState * state, const gchar * settings);

/* Parse a REGION block's key-value settings into a GstSubParseVTTRegion */
void parse_webvtt_region_settings (ParserState * state,
    const gchar * settings, GstSubParse * subparse);

/* Main WebVTT line parser — state machine matching the Parser callback signature */
gchar *parse_webvtt (ParserState * state, const gchar * line);

/* Process accumulated WebVTT cues, splitting overlapping time intervals
 * into discrete buffers so downstream sees non-overlapping segments */
void process_webvtt_cues (GstSubParse * subparse, ParserState * state);

/* Free a single GstSubParseVTTCue and all its owned strings */
void free_webvtt_cue (GstSubParseVTTCue * cue);

/* Ensure the GstWebVTTCueMeta custom meta type is registered */
void gst_webvtt_cue_meta_ensure_registered (void);

G_END_DECLS
#endif /* __GST_WEBVTT_PARSE_H__ */
