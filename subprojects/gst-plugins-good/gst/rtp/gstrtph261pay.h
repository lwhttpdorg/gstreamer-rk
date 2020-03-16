/* GStreamer
 * Copyright (C) <2014> Stian Selnes <stian@pexip.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Dejan Sakelsak sahel@kiberpipa.org
 */

#ifndef __GST_RTP_H261_PAY_H__
#define __GST_RTP_H261_PAY_H__

#include <gst/gst.h>
#include <gst/rtp/gstrtpbasepayload.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_RTP_H261_PAY (gst_rtp_h261_pay_get_type())
G_DECLARE_FINAL_TYPE (GstRtpH261Pay, gst_rtp_h261_pay, GST, RTP_H261_PAY,
    GstRTPBasePayload)

struct _GstRtpH261Pay
{
  GstRTPBasePayload payload;

  GstAdapter *adapter;
  gint offset;
  GstClockTime timestamp;
};

typedef struct _GstRtpH261PayHeader
{
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
  unsigned int v:1;             /* Motion vector flag */
  unsigned int i:1;             /* Intra encoded data */
  unsigned int ebit:3;          /* End position */
  unsigned int sbit:3;          /* Start position */

  unsigned int mbap1:4;         /* MB address predictor - part1 */
  unsigned int gobn:4;          /* GOB number */

  unsigned int hmvd1:2;         /* Horizontal motion vector data - part1 */
  unsigned int quant:5;         /* Quantizer */
  unsigned int mbap2:1;         /* MB address predictor - part2 */

  unsigned int vmvd:5;          /* Horizontal motion vector data - part1 */
  unsigned int hmvd2:3;         /* Vertical motion vector data */
#elif G_BYTE_ORDER == G_BIG_ENDIAN
  unsigned int sbit:3;          /* Start position */
  unsigned int ebit:3;          /* End position */
  unsigned int i:1;             /* Intra encoded data */
  unsigned int v:1;             /* Motion vector flag */

  unsigned int gobn:4;          /* GOB number */
  unsigned int mbap1:4;         /* MB address predictor - part1 */

  unsigned int mbap2:1;         /* MB address predictor - part2 */
  unsigned int quant:5;         /* Quantizer */
  unsigned int hmvd1:2;         /* Horizontal motion vector data - part1 */

  unsigned int hmvd2:3;         /* Vertical motion vector data */
  unsigned int vmvd:5;          /* Horizontal motion vector data - part1 */
#else
#error "G_BYTE_ORDER should be big or little endian."
#endif
} GstRtpH261PayHeader;
#define GST_RTP_H261_PAYLOAD_HEADER_LEN 4

G_END_DECLS
#endif /* __GST_RTP_H261_PAY_H__ */
