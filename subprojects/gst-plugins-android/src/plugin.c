/*
 * gst-plugins-android — plugin entry point.
 *
 * Registers c2opusdec and c2aacdec under the "androidcodec2" plugin name.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <gst/gst.h>

#include "gstc2opusdec.h"
#include "gstc2aacdec.h"
#include "gstc2flacdec.h"
#include "gstc2vorbisdec.h"
#include "gstc2mp3dec.h"
#ifdef HAVE_IAMF
#include "gstc2iamfdec.h"
#endif

#ifndef PACKAGE
#define PACKAGE "gst-plugins-android"
#endif
#ifndef VERSION
#define VERSION "0.1.0"
#endif
#ifndef GST_PACKAGE_NAME
#define GST_PACKAGE_NAME "GStreamer Android Codec2 plugin"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "https://github.com/changyongahn/PoC_GstAnd"
#endif

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean ok = TRUE;
  ok &= gst_c2_opus_dec_register (plugin);
  ok &= gst_c2_aac_dec_register  (plugin);
  ok &= gst_c2_flac_dec_register (plugin);
  ok &= gst_c2_vorbis_dec_register (plugin);
  ok &= gst_c2_mp3_dec_register (plugin);
#ifdef HAVE_IAMF
  ok &= gst_c2_iamf_dec_register (plugin);
#endif
  return ok;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    androidcodec2,
    "Android 16 Codec2 SW audio decoders (Opus, AAC, FLAC, Vorbis, MP3"
#ifdef HAVE_IAMF
    ", IAMF"
#endif
    ") on Linux",
    plugin_init,
    VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
