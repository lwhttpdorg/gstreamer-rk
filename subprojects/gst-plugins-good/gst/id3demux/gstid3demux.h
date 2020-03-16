/* GStreamer ID3 tag demuxer
 * Copyright (C) 2005 Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) 2003-2004 Benjamin Otte <otte@gnome.org>
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

#ifndef __GST_ID3DEMUX_H__
#define __GST_ID3DEMUX_H__

#include <gst/tag/gsttagdemux.h>

G_BEGIN_DECLS

#define GST_TYPE_ID3DEMUX (gst_id3demux_get_type())
G_DECLARE_FINAL_TYPE (GstID3Demux, gst_id3demux, GST, ID3DEMUX, GstTagDemux)

struct _GstID3Demux
{
  GstTagDemux tagdemux;

  gboolean prefer_v1;     /* prefer ID3v1 tags over ID3v2 tags? */
};

GST_ELEMENT_REGISTER_DECLARE (id3demux);

G_END_DECLS

#endif /* __GST_ID3DEMUX_H__ */

