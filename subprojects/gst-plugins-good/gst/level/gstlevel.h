/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2000,2001,2002,2003,2005
 *           Thomas Vander Stichele <thomas at apestaart dot org>
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


#ifndef __GST_LEVEL_H__
#define __GST_LEVEL_H__


#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/audio/audio.h>

G_BEGIN_DECLS

#define GST_TYPE_LEVEL (gst_level_get_type())
G_DECLARE_FINAL_TYPE (GstLevel, gst_level, GST, LEVEL, GstBaseTransform)

/**
 * GstLevel:
 *
 * Opaque data structure.
 */
struct _GstLevel {
  GstBaseTransform element;

  /* properties, protected by object lock */
  gboolean post_messages;       /* whether or not to post messages */
  guint64 interval;             /* how many nanoseconds between emits */
  gdouble decay_peak_ttl;       /* time to live for peak in nanoseconds */
  gdouble decay_peak_falloff;   /* falloff in dB/sec */
  gboolean audio_level_meta; /* whether or not generate GstAudioLevelMeta */

  GstAudioInfo info;
  gint num_frames;              /* frame count (1 sample per channel)
                                 * since last emit */
  gint interval_frames;         /* after how many frame to sent a message */
  GstClockTime message_ts;      /* starttime for next message */

  /* per-channel arrays for intermediate values */
  gdouble *CS;                  /* normalized Cumulative Square */
  gdouble *peak;                /* normalized Peak value over buffer */
  gdouble *last_peak;           /* last normalized Peak value over interval */
  gdouble *decay_peak;          /* running decaying normalized Peak */
  gdouble *decay_peak_base;     /* value of last peak we are decaying from */
  GstClockTime *decay_peak_age; /* age of last peak */

  void (*process)(gpointer, guint, guint, gdouble*, gdouble*);
};

GST_ELEMENT_REGISTER_DECLARE (level);

G_END_DECLS


#endif /* __GST_LEVEL_H__ */
