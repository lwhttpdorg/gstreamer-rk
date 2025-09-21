/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *                    2005 Wim Taymans <wim@fluendo.com>
 *                    2007 Andy Wingo <wingo at pobox.com>
 *                    2008 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * deinterleave.c: deinterleave samples
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

#ifndef __DEINTERLEAVE_H__
#define __DEINTERLEAVE_H__

G_BEGIN_DECLS

#include <gst/gst.h>
#include <gst/audio/audio.h>

#define GST_TYPE_DEINTERLEAVE (gst_deinterleave_get_type())
G_DECLARE_FINAL_TYPE (GstDeinterleave, gst_deinterleave, GST, DEINTERLEAVE,
    GstElement)

typedef void (*GstDeinterleaveFunc) (gpointer out, gpointer in, guint stride, guint nframes);

struct _GstDeinterleave
{
  GstElement element;

  /*< private > */
  GList *srcpads;
  GstCaps *sinkcaps;
  GstAudioInfo audio_info;
  gboolean keep_positions;

  GstPad *sink;

  GstDeinterleaveFunc func;

  GList *pending_events;
};

G_END_DECLS

#endif /* __DEINTERLEAVE_H__ */
