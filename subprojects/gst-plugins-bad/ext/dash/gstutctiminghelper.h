/* GStreamer
 *
 * Copyright (C) 2012 Orange
 * Authors:
 *   David Corvoysier <david.corvoysier@orange.com>
 *   Hamid Zakari <hamid.zakari@gmail.com>
 *
 * Copyright (C) 2013 Smart TV Alliance
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>, Collabora Ltd.
 * 
 * Copyright (C) 2025 Fundación Vicomtech
 *  Author: Roberto Viola <rviola@vicomtech.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_UTCTIMINGHELPER_H__
#define __GST_UTCTIMINGHELPER_H__

#include <gst/gst.h>
#include <gst/uridownloader/gsturidownloader.h>

G_BEGIN_DECLS

#define NTP_TO_UNIX_EPOCH G_GUINT64_CONSTANT(2208988800)        /* difference (in seconds) between NTP epoch and Unix epoch */

GstDateTime *gst_utctiming_helper_poll_ntp_server (GstClock * ntp_clock, gchar * url);
GstDateTime *gst_utctiming_helper_poll_ntp_server_simple (gchar * url);
GstFragment *gst_utctiming_helper_poll_http_server (gchar * url);
GstDateTime *gst_utctiming_helper_parse_http_head (GstFragment * download);
GstDateTime *gst_utctiming_helper_parse_http_ntp (GstBuffer * buffer);
GstDateTime *gst_utctiming_helper_parse_http_xsdate (GstBuffer * buffer);

G_END_DECLS
#endif /* __GST_UTCTIMINGHELPER_H__ */
