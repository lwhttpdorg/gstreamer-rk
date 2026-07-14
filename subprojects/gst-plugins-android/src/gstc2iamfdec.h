/*
 * gst-plugins-android — c2iamfdec element header.
 *
 * IAMF (Immersive Audio Model & Formats) decoder. Unlike the other elements in
 * this plugin, c2iamfdec does NOT wrap an AOSP Codec2 component — the AOSP
 * c2.android.iamf.decoder is a stub. It drives AOM's libiamf reference decoder
 * directly. The element name keeps the "c2" prefix for plugin consistency.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_IAMF_DEC_H_
#define GST_C2_IAMF_DEC_H_

#include <gst/gst.h>
#include <gst/audio/gstaudiodecoder.h>

G_BEGIN_DECLS

#define GST_TYPE_C2_IAMF_DEC (gst_c2_iamf_dec_get_type ())
G_DECLARE_FINAL_TYPE (GstC2IamfDec, gst_c2_iamf_dec, GST, C2_IAMF_DEC, GstAudioDecoder)

gboolean gst_c2_iamf_dec_register (GstPlugin * plugin);

G_END_DECLS

#endif
