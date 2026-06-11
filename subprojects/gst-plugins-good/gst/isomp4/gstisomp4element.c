/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <glib/gi18n-lib.h>
#include <gst/pbutils/pbutils.h>

#include "gstisomp4elements.h"
#include "qtdemux.h"

void
isomp4_element_init (GstPlugin * plugin)
{
  static gsize res = FALSE;

  if (g_once_init_enter (&res)) {
#ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif /* ENABLE_NLS */

    gst_pb_utils_init ();

    /* ensure private tag is registered */
    gst_tag_register (GST_QT_DEMUX_PRIVATE_TAG, GST_TAG_FLAG_META,
        GST_TYPE_SAMPLE, "QT atom", "unparsed QT tag atom",
        gst_tag_merge_use_first);

    gst_tag_register (GST_QT_DEMUX_CLASSIFICATION_TAG, GST_TAG_FLAG_META,
        G_TYPE_STRING, GST_QT_DEMUX_CLASSIFICATION_TAG,
        "content classification", gst_tag_merge_use_first);

    if (!gst_tag_exists (GST_QT_DEMUX_GIMI_TRACK_CONTENT_ID))
      gst_tag_register_static (GST_QT_DEMUX_GIMI_TRACK_CONTENT_ID,
          GST_TAG_FLAG_META, G_TYPE_STRING, GST_QT_DEMUX_GIMI_TRACK_CONTENT_ID,
          "NGA.STND.0076 GEOINT Imagery Media for Intelligence, Surveillance,"
          " and Reconnaissance (ISR) (GIMI) Track ContentID", NULL);

    if (!gst_tag_exists (GST_QT_DEMUX_GIMI_COMPONENT_CONTENT_ID))
      gst_tag_register_static (GST_QT_DEMUX_GIMI_COMPONENT_CONTENT_ID,
          GST_TAG_FLAG_META, GST_TYPE_STRUCTURE,
          GST_QT_DEMUX_GIMI_COMPONENT_CONTENT_ID,
          "NGA.STND.0076 GEOINT Imagery Media for Intelligence, Surveillance,"
          " and Reconnaissance (ISR) (GIMI) Component ContentID", NULL);

    if (!gst_tag_exists (GST_QT_DEMUX_PRECISION_CLOCK_TYPE))
      gst_tag_register_static (GST_QT_DEMUX_PRECISION_CLOCK_TYPE,
          GST_TAG_FLAG_META, G_TYPE_STRING, GST_QT_DEMUX_PRECISION_CLOCK_TYPE,
          "ISO/IEC 23001-17 TAI Clock type information", NULL);

    if (!gst_tag_exists (GST_QT_DEMUX_PRECISION_TIME_UNCERTAINTY))
      gst_tag_register_static (GST_QT_DEMUX_PRECISION_TIME_UNCERTAINTY,
          GST_TAG_FLAG_META, G_TYPE_UINT64,
          GST_QT_DEMUX_PRECISION_TIME_UNCERTAINTY,
          "ISO/IEC 23001-17 TAI Clock time uncertainty (in nanoseconds) information",
          NULL);

    if (!gst_tag_exists (GST_QT_DEMUX_GIMI_SECURITY_MARKINGS_XML))
      gst_tag_register_static (GST_QT_DEMUX_GIMI_SECURITY_MARKINGS_XML,
          GST_TAG_FLAG_META, G_TYPE_STRING,
          GST_QT_DEMUX_GIMI_SECURITY_MARKINGS_XML,
          "NGA.STND.0076 GEOINT Imagery Media for Intelligence, Surveillance,"
          " and Reconnaissance (ISR) (GIMI) Security Markings XML", NULL);

    if (!gst_tag_exists (GST_QT_DEMUX_GIMI_SECURITY_MARKINGS_CONTENT_ID))
      gst_tag_register_static (GST_QT_DEMUX_GIMI_SECURITY_MARKINGS_CONTENT_ID,
          GST_TAG_FLAG_META, G_TYPE_STRING,
          GST_QT_DEMUX_GIMI_SECURITY_MARKINGS_CONTENT_ID,
          "NGA.STND.0076 GEOINT Imagery Media for Intelligence, Surveillance,"
          " and Reconnaissance (ISR) (GIMI) Security Markings XML Content ID",
          NULL);

    gst_meta_register_custom_simple ("GimiContentID");

    g_once_init_leave (&res, TRUE);
  }
}
