/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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


#ifndef __GST_CUTTER_H__
#define __GST_CUTTER_H__


#include <gst/gst.h>
/* #include <gst/meta/audioraw.h> */


G_BEGIN_DECLS

#define GST_TYPE_CUTTER (gst_cutter_get_type())
G_DECLARE_FINAL_TYPE (GstCutter, gst_cutter, GST, CUTTER, GstElement)

struct _GstCutter
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  double threshold_level;       /* level below which to cut */
  double threshold_length;      /* how long signal has to remain
                                 * below this level before cutting */
  double silent_run_length;     /* how long has it been below threshold ? */
  gboolean silent;
  gboolean silent_prev;

  double pre_length;            /* how long can the pre-record buffer be ? */
  double pre_run_length;        /* how long is it currently ? */
  GList *pre_buffer;            /* list of GstBuffers in pre-record buffer */
  gboolean leaky;               /* do we leak an overflowing prebuffer ? */
  gboolean audio_level_meta;    /* whether or not generate GstAudioLevelMeta */

  GstAudioInfo info;
  GstSegment segment;
};

GST_ELEMENT_REGISTER_DECLARE (cutter);

G_END_DECLS

#endif /* __GST_CUTTER_H__ */
