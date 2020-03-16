/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_ASTERISK_H263_H__
#define __GST_ASTERISK_H263_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_ASTERISK_H263 (gst_asteriskh263_get_type())
G_DECLARE_FINAL_TYPE (GstAsteriskh263, gst_asteriskh263, GST, ASTERISK_H263,
    GstElement)

struct _GstAsteriskh263
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  GstAdapter *adapter;

  guint32 lastts;
};

G_END_DECLS

#endif /* __GST_ASTERISK_H263_H__ */
