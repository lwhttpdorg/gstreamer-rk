/*
 * gst-plugins-android — c2opusdec element header.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_OPUS_DEC_H_
#define GST_C2_OPUS_DEC_H_

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_C2_OPUS_DEC (gst_c2_opus_dec_get_type ())
G_DECLARE_FINAL_TYPE (GstC2OpusDec, gst_c2_opus_dec, GST, C2_OPUS_DEC, GstAudioDecoder)

gboolean gst_c2_opus_dec_register (GstPlugin * plugin);

G_END_DECLS

#endif
