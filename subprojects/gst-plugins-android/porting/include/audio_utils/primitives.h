/*
 * gst-plugins-android — minimal <audio_utils/primitives.h> shim.
 *
 * AOSP's FLACDecoder.cpp uses exactly ONE symbol from system/media's
 * audio_utils (float_from_i32). The real header is ~2400 lines with unrelated
 * deps, so we reproduce only that trivial inline (verbatim from AOSP
 * android-16.0.0_r1 audio_utils/include/audio_utils/primitives.h).
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_AUDIO_UTILS_PRIMITIVES_H_
#define GST_C2_PORTING_AUDIO_UTILS_PRIMITIVES_H_

#include <stdint.h>

static inline float float_from_i32(int32_t ival) {
    static const float scale = 1.f / (float)(1UL << 31);
    return ival * scale;
}

#endif
