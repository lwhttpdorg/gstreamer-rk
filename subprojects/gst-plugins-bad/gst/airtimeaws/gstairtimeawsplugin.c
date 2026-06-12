/*
 * airtime aws plugin
 * Copyright (C) 2025 mmhmm, Inc.
 *   @author: Teus Groenewoud <teus@mmhmm.app>
 *   @author: Tomasz Mikolajczyk <tomasz.mikolajczyk@mmhmm.app>
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
#include "config.h"

#include "gstairtimes3src.h"

// announce the types that will be added to the plugin
GST_ELEMENT_REGISTER_DEFINE (airtime_s3_src, "airtimes3src", GST_RANK_PRIMARY,
    GST_TYPE_AIRTIMES3SRC)
/// @brief Plugin entry point
/// @param plugin An instance of the plugin
/// @return A boolean indicating success
     static gboolean airtime_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (airtime_s3_src, plugin);
}

// GStreamer looks for this structure to register airtime
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, airtimeaws,
    "airtime aws plugin", airtime_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
