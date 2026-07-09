/*
 * gst-plugins-android — Linux replacement for AOSP's frameworks/av/media/codec2/vndk/C2Store.cpp.
 *
 * On Android, C2Store walks /vendor/etc/media_codecs.xml and dlopen()s
 * vendor .so libraries. On a Linux host we instead statically wire the two
 * codecs we care about (c2.android.opus.decoder + c2.android.aac.decoder)
 * and return the malloc-backed allocator.
 *
 * The two `GetCodec2Platform*Store()` symbols are declared (in namespace
 * android) by the public AOSP header <C2PlatformSupport.h>; our .cpp defines
 * them there. This header only exposes the small convenience helper used by
 * the GStreamer element code.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_C2_STORE_LINUX_H_
#define GST_C2_PORTING_C2_STORE_LINUX_H_

#include <C2Component.h>
#include <memory>

namespace gst_c2 {

/* Convenience: create a component by name. The two recognised names are
 * "c2.android.opus.decoder" and "c2.android.aac.decoder". Returns C2_OK on
 * success and an error otherwise. */
c2_status_t CreateLinuxC2Component(const char* name, std::shared_ptr<C2Component>* out);

}  /* namespace gst_c2 */

#endif
