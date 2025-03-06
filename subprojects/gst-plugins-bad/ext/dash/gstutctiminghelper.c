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

#include <stdio.h>
#include <gio/gio.h>
#include <gst/net/gstnet.h>
#include "gstutctiminghelper.h"

struct Rfc5322TimeZone
{
  const gchar *name;
  gfloat tzoffset;
};

/*
 * The DASH standard does not specify which version of NTP. This
 * function only works with NTPv4 servers.
 */
GstDateTime *
gst_utctiming_helper_poll_ntp_server (GstClock * ntp_clock, gchar * url)
{
  GstClockTime ntp_clock_time;
  GDateTime *dt, *dt2;

  if (!ntp_clock) {
    GResolver *resolver;
    GList *inet_addrs;
    GError *err = NULL;
    gchar *ip_addr;

    resolver = g_resolver_get_default ();
    GST_DEBUG ("Connecting to NTP time server %s", url);
    inet_addrs = g_resolver_lookup_by_name (resolver, url, NULL, &err);
    g_object_unref (resolver);
    if (!inet_addrs || g_list_length (inet_addrs) == 0) {
      GST_ERROR ("Failed to resolve hostname of NTP server: %s",
          err ? (err->message) : "unknown error");
      if (inet_addrs)
        g_resolver_free_addresses (inet_addrs);
      if (err)
        g_error_free (err);
      return NULL;
    }
    ip_addr =
        g_inet_address_to_string ((GInetAddress
            *) (g_list_first (inet_addrs)->data));
    ntp_clock = gst_ntp_clock_new ("dashntp", ip_addr, 123, 0);
    g_free (ip_addr);
    g_resolver_free_addresses (inet_addrs);
    if (!ntp_clock) {
      GST_ERROR ("Failed to create NTP clock");
      return NULL;
    }
    if (!gst_clock_wait_for_sync (ntp_clock, 5 * GST_SECOND)) {
      g_object_unref (ntp_clock);
      ntp_clock = NULL;
      GST_ERROR ("Failed to lock to NTP clock");
      return NULL;
    }
  }
  ntp_clock_time = gst_clock_get_time (ntp_clock);
  if (ntp_clock_time == GST_CLOCK_TIME_NONE) {
    GST_ERROR ("Failed to get time from NTP clock");
    return NULL;
  }
  ntp_clock_time -= NTP_TO_UNIX_EPOCH * GST_SECOND;
  dt = g_date_time_new_from_unix_utc (ntp_clock_time / GST_SECOND);
  if (!dt) {
    GST_ERROR ("Failed to create GstDateTime");
    return NULL;
  }
  ntp_clock_time =
      gst_util_uint64_scale (ntp_clock_time % GST_SECOND, 1000000, GST_SECOND);
  dt2 = g_date_time_add (dt, ntp_clock_time);
  g_date_time_unref (dt);
  return gst_date_time_new_from_g_date_time (dt2);
}

/*
 * This function downloads the UTC timing information
 * from the NTP server.
 */
GstDateTime *
gst_utctiming_helper_poll_ntp_server_simple (gchar * url)
{
  GstClock *ntp_clock = NULL;
  GstDateTime *dt = NULL;
  dt = gst_utctiming_helper_poll_ntp_server (ntp_clock, url);
  if (ntp_clock)
    g_object_unref (ntp_clock);
  return dt;
}

/*
 * This function downloads the UTC timing information
 * from the HTTP server.
 */
GstFragment *
gst_utctiming_helper_poll_http_server (gchar * url)
{
  GstFragment *download = NULL;
  GstUriDownloader *downloader = gst_uri_downloader_new ();
  GST_DEBUG ("Fetching from HTTP server: %s", url);
  download =
      gst_uri_downloader_fetch_uri (downloader,
      url, NULL, TRUE, TRUE, TRUE, NULL);
  gst_object_unref (downloader);
  if (!download)
    return NULL;
  return download;
}

/*
 Parse an RFC5322 (section 3.3) date-time from the Date: field in the
 HTTP response.
 See https://tools.ietf.org/html/rfc5322#section-3.3
*/
GstDateTime *
gst_utctiming_helper_parse_http_head (GstFragment * download)
{
  static const gchar *months[] = { NULL, "Jan", "Feb", "Mar", "Apr",
    "May", "Jun", "Jul", "Aug",
    "Sep", "Oct", "Nov", "Dec", NULL
  };
  static const struct Rfc5322TimeZone timezones[] = {
    {"Z", 0},
    {"UT", 0},
    {"GMT", 0},
    {"BST", 1},
    {"EST", -5},
    {"EDT", -4},
    {"CST", -6},
    {"CDT", -5},
    {"MST", -7},
    {"MDT", -6},
    {"PST", -8},
    {"PDT", -7},
    {NULL, 0}
  };
  GstDateTime *value = NULL;
  const GstStructure *response_headers;
  const gchar *http_date;
  const GValue *val;
  gint ret;
  const gchar *pos;
  gint year = -1, month = -1, day = -1, hour = -1, minute = -1, second = -1;
  gchar zone[6];
  gchar monthstr[4];
  gfloat tzoffset = 0;
  gboolean parsed_tz = FALSE;

  g_return_val_if_fail (download != NULL, NULL);
  g_return_val_if_fail (download->headers != NULL, NULL);

  val = gst_structure_get_value (download->headers, "response-headers");
  if (!val) {
    return NULL;
  }
  response_headers = gst_value_get_structure (val);
  http_date = gst_structure_get_string (response_headers, "Date");
  if (!http_date)
    return NULL;

  /* skip optional text version of day of the week */
  pos = strchr (http_date, ',');
  if (pos)
    pos++;
  else
    pos = http_date;
  ret =
      sscanf (pos, "%02d %3s %04d %02d:%02d:%02d %5s", &day, monthstr, &year,
      &hour, &minute, &second, zone);
  if (ret == 7) {
    gchar *z = zone;
    gint i;

    for (i = 1; months[i]; ++i) {
      if (g_ascii_strncasecmp (months[i], monthstr, strlen (months[i])) == 0) {
        month = i;
        break;
      }
    }
    for (i = 0; timezones[i].name && !parsed_tz; ++i) {
      if (g_ascii_strncasecmp (timezones[i].name, z,
              strlen (timezones[i].name)) == 0) {
        tzoffset = timezones[i].tzoffset;
        parsed_tz = TRUE;
      }
    }
    if (!parsed_tz) {
      gint hh, mm;
      gboolean neg = FALSE;
      /* check if it is in the form +-HHMM */
      if (*z == '+' || *z == '-') {
        if (*z == '+')
          ++z;
        else if (*z == '-') {
          ++z;
          neg = TRUE;
        }
        ret = sscanf (z, "%02d%02d", &hh, &mm);
        if (ret == 2) {
          tzoffset = hh;
          tzoffset += mm / 60.0;
          if (neg)
            tzoffset = -tzoffset;
          parsed_tz = TRUE;
        }
      }
    }
    /* Accept year in both 2 digit or 4 digit format */
    if (year < 100)
      year += 2000;
  }
  if (month > 0 && parsed_tz) {
    value = gst_date_time_new (tzoffset,
        year, month, day, hour, minute, second);
  }
  return value;
}

/*
   The timing information is contained in the message body of the HTTP
   response and contains a time value formatted according to NTP timestamp
   format in IETF RFC 5905.

       0                   1                   2                   3
       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                            Seconds                            |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
      |                            Fraction                           |
      +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

                             NTP Timestamp Format
*/
GstDateTime *
gst_utctiming_helper_parse_http_ntp (GstBuffer * buffer)
{
  gint64 seconds;
  guint64 fraction;
  GDateTime *dt, *dt2;
  GstMapInfo mapinfo;

  /* See https://tools.ietf.org/html/rfc5905#page-12 for details of
     the NTP Timestamp Format */
  gst_buffer_map (buffer, &mapinfo, GST_MAP_READ);
  if (mapinfo.size != 8) {
    gst_buffer_unmap (buffer, &mapinfo);
    return NULL;
  }
  seconds = GST_READ_UINT32_BE (mapinfo.data);
  fraction = GST_READ_UINT32_BE (mapinfo.data + 4);
  gst_buffer_unmap (buffer, &mapinfo);
  fraction = gst_util_uint64_scale (fraction, 1000000,
      G_GUINT64_CONSTANT (1) << 32);
  /* subtract constant to convert from 1900 based time to 1970 based time */
  seconds -= NTP_TO_UNIX_EPOCH;
  dt = g_date_time_new_from_unix_utc (seconds);
  dt2 = g_date_time_add (dt, fraction);
  g_date_time_unref (dt);
  return gst_date_time_new_from_g_date_time (dt2);
}

/*
  The timing information is contained in the message body of the
  HTTP response and contains a time value formatted according to
  xs:dateTime as defined in W3C XML Schema Part 2: Datatypes specification.
*/
GstDateTime *
gst_utctiming_helper_parse_http_xsdate (GstBuffer * buffer)
{
  GstDateTime *value = NULL;
  GstMapInfo mapinfo;

  /* the string from the server might not be zero terminated */
  if (gst_buffer_map (buffer, &mapinfo, GST_MAP_READ)) {
    gchar *str;
    str = g_strndup ((const gchar *) mapinfo.data, mapinfo.size);
    gst_buffer_unmap (buffer, &mapinfo);
    value = gst_date_time_new_from_iso8601_string (str);
  }
  return value;
}
