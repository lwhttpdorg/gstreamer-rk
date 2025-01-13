/* GStreamer Synchronous KLV output
 *
 * Copyright (C) <2024> Glyn Davies
 *  @author Glyn Davies <glyn@solet.io>
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

#include <stdio.h>
#include "gstbasetsmuxklv.h"
#include <string.h>
#include <gst/base/gstbytewriter.h>
#include <gst/gst.h>

#define GST_CAT_DEFAULT gst_base_ts_mux_debug

GstBuffer *
gst_base_ts_mux_prepare_klv (GstBuffer * buf, GstBaseTsMuxPad * pad,
    GstBaseTsMux * mux)
{
  GstByteWriter wr;

  gsize bufLength = gst_buffer_get_size (buf);

  // The metadata framing is 5 bytes
  gst_byte_writer_init_with_size (&wr, 5, FALSE);

  // Note: Sequence number isn't preserved in the metadata sent by mpegtsdemux

  GstMpegtsPESMetadataMeta *meta =
      gst_buffer_get_mpegts_pes_metadata_meta (buf);

  if (meta) {
    GST_LOG_OBJECT (mux,
        "Found metadata attached service_id=0x%02x flags=0x%02x",
        meta->metadata_service_id, meta->flags);
    gst_byte_writer_put_uint8 (&wr, meta->metadata_service_id); // Service id
    gst_byte_writer_put_uint8 (&wr, pad->stream->metadata_sequence_number);     // Sequence number
    gst_byte_writer_put_uint8 (&wr, meta->flags);       // Flags
    gst_byte_writer_put_uint16_be (&wr, bufLength);     // Data length

    g_free (meta);
    meta = NULL;
  } else {
    // From 13818-1 2007, Table 2-97 - Metadata AU cell
    // Default Flags
    // - No cell fragmentation (0xC0) - set to 11 - A single cell carries a complete AU
    // - Random access indicator (0x20) - set to 1 - This cell is an entrypoint to the metadata
    // - Decoder config - not set
    int flags = 0xC0 | 0x20;
    GST_LOG_OBJECT (mux,
        "Generating metdata service_id=0x00 flags=0x%02x", flags);

    gst_byte_writer_put_uint8 (&wr, 0x00);      // Service id
    gst_byte_writer_put_uint8 (&wr, pad->stream->metadata_sequence_number);     // Sequence number
    gst_byte_writer_put_uint8 (&wr, flags);     // Flags 
    gst_byte_writer_put_uint16_be (&wr, bufLength);     // Data length
  }

  // Increment the sequence number - this is a byte
  pad->stream->metadata_sequence_number++;

  GstBuffer *out_buf = gst_buffer_new_and_alloc (5);
  guint8 *metatdata_header = gst_byte_writer_reset_and_get_data (&wr);
  gst_buffer_fill (out_buf, 0, metatdata_header, 5);
  g_free (metatdata_header);

  // Copy original incoming... 
  gst_buffer_copy_into (out_buf, buf,
      GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_TIMESTAMPS |
      GST_BUFFER_COPY_MEMORY, 0, -1);

  return out_buf;
}

void
gst_base_ts_mux_free_klv (gpointer prepare_data)
{
  g_free (prepare_data);
}
