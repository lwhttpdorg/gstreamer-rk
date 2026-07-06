/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
 *
 * gstzipparser.h:
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

#pragma once

#include <glib.h>

G_BEGIN_DECLS

/* ZIP constants from PKWARE APPNOTE.TXT that are exposed for testability, so
 * tests can write self-documenting archive fixtures.
 */
#define GST_ZIP_EOCD_SIGNATURE                          0x06054b50
#define GST_ZIP_CENTRAL_DIRECTORY_FILE_HEADER_SIGNATURE 0x02014b50
#define GST_ZIP_LOCAL_FILE_HEADER_SIGNATURE             0x04034b50
#define GST_ZIP_COMPRESSION_METHOD_STORE                0
#define GST_ZIP64_EXTENDED_INFORMATION_EXTRA_FIELD_ID   0x0001
#define GST_ZIP_UINT32_SENTINEL                         0xffffffff

typedef struct
{
  guint64 offset;
  guint64 size;
} GstZipEntryRange;

gboolean gst_zip_parser_find_stored_entry (gint fd, guint64 archive_size,
    const gchar * location, GstZipEntryRange * range, GError ** error);

G_END_DECLS
