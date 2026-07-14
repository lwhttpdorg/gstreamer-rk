/*
 * gst-plugins-android — c2flacdec element header.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_FLAC_DEC_H_
#define GST_C2_FLAC_DEC_H_

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_C2_FLAC_DEC (gst_c2_flac_dec_get_type ())
G_DECLARE_FINAL_TYPE (GstC2FlacDec, gst_c2_flac_dec, GST, C2_FLAC_DEC, GstAudioDecoder)

gboolean gst_c2_flac_dec_register (GstPlugin * plugin);

G_END_DECLS

#endif
