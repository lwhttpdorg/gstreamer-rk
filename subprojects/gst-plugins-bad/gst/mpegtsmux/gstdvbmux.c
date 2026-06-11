/* DVB Transport Stream muxer
 * Copyright (C) 2026 LTN Global Communications, Inc.
 *   Author: Jan Alexander Steffens (heftig) <jan.steffens@ltnglobal.com>
 *
 * dvbmux.c:
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
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdvbmux.h"

GST_DEBUG_CATEGORY (gst_dvb_mux_debug);
#define GST_CAT_DEFAULT gst_dvb_mux_debug

G_DEFINE_TYPE (GstDVBMux, gst_dvb_mux, GST_TYPE_BASE_TS_MUX);
GST_ELEMENT_REGISTER_DEFINE (dvbmux, "dvbmux", GST_RANK_PRIMARY,
    gst_dvb_mux_get_type ());

#define parent_class gst_dvb_mux_parent_class

static GstStaticPadTemplate gst_dvb_mux_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpegts, "
        "systemstream = (boolean) true, " "packetsize = (int) 188 ")
    );

static GstStaticPadTemplate gst_dvb_mux_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("sink_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "parsed = (boolean) TRUE, "
        "mpegversion = (int) 2, "
        "systemstream = (boolean) false; "
        "video/x-h264,stream-format=(string)byte-stream,"
        "alignment=(string){au, nal}; "
        "video/x-h265,stream-format=(string)byte-stream,"
        "alignment=(string){au, nal}; "
        "audio/mpeg, "
        "parsed = (boolean) TRUE, "
        "mpegversion = (int) 1;"
        "audio/mpeg, "
        "framed = (boolean) TRUE, "
        "mpegversion = (int) {2, 4}, stream-format = (string) { adts, raw };"
        "audio/x-ac3, framed = (boolean) TRUE;"
        "audio/x-dts, framed = (boolean) TRUE;"
        "subpicture/x-dvb; application/x-teletext; meta/x-klv, parsed=true;"));

/* Internals */

static void
gst_dvb_mux_class_init (GstDVBMuxClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_dvb_mux_debug, "dvbmux", 0, "DVB muxer");

  gst_element_class_set_static_metadata (gstelement_class,
      "DVB Transport Stream Muxer", "Codec/Muxer",
      "Multiplexes media streams into a DVB-compliant Transport Stream",
      "Jan Alexander Steffens (heftig) <jan.steffens@ltnglobal.com>");

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &gst_dvb_mux_sink_factory, GST_TYPE_BASE_TS_MUX_PAD);

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &gst_dvb_mux_src_factory, GST_TYPE_AGGREGATOR_PAD);
}

static void
gst_dvb_mux_init (GstDVBMux * mux)
{
}
