/*
 * gst-plugins-android — stub for Codec2CommonUtils.h.
 *
 * SimpleC2Component.cpp:33 includes it and calls isAtLeastT() (:348/:1468) and
 * isHalPixelFormatSupported() (:1458). On Linux we are "newer than any Android
 * release", so the version probes return true and the pixel-format probe
 * returns false (no gralloc formats are supported — audio never asks).
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_CODEC2_COMMON_UTILS_STUB_H_
#define GST_C2_PORTING_CODEC2_COMMON_UTILS_STUB_H_

#include <android/hardware_buffer.h>

namespace android {

inline bool isAtLeastT() { return true; }
inline bool isAtLeastU() { return true; }
inline bool isAtLeastV() { return true; }
inline bool isVendorApiOrFirstApiAtLeastT() { return true; }
inline bool isHalPixelFormatSupported(AHardwareBuffer_Format /*format*/) { return false; }

}  /* namespace android */

#endif
