/* GStreamer plugin for forward error correction
 * Copyright (C) 2017 Pexip
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Author: Mikhail Fludkov <misha@pexip.com>
 */

#ifndef __RTP_STORAGE_H__
#define __RTP_STORAGE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define RTP_TYPE_STORAGE (rtp_storage_get_type())
G_DECLARE_FINAL_TYPE (RtpStorage, rtp_storage, RTP, STORAGE, GObject)

struct _RtpStorage {
  GObject parent;
  GstClockTime size_time;
  GHashTable *streams;
  GMutex streams_lock;
};

GstBufferList * rtp_storage_get_packets_for_recovery (RtpStorage * self, gint fec_pt,
                                                      guint32 ssrc, guint16 lost_seq);
void            rtp_storage_put_recovered_packet     (RtpStorage * self, GstBuffer * buffer,
                                                      guint8 pt, guint32 ssrc, guint16 seq);
GstBuffer     * rtp_storage_get_redundant_packet     (RtpStorage * self, guint32 ssrc,
                                                      guint16 lost_seq);
gboolean        rtp_storage_append_buffer            (RtpStorage *self, GstBuffer *buffer);
void            rtp_storage_clear                    (RtpStorage *self);
RtpStorage    * rtp_storage_new                      (void);
void            rtp_storage_set_size                 (RtpStorage *self, GstClockTime size);
GstClockTime    rtp_storage_get_size                 (RtpStorage *self);

G_END_DECLS

#endif /* __RTP_STORAGE_H__ */
