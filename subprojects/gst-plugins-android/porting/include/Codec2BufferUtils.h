/*
 * gst-plugins-android — minimal stub for Codec2BufferUtils.h.
 *
 * The real header exposes YUV/RGB color-conversion helpers used by video
 * components. Audio decoders never call into them; only SimpleC2Component
 * #includes the header. We provide an empty stub so the include resolves.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_CODEC2_BUFFER_UTILS_STUB_H_
#define GST_C2_PORTING_CODEC2_BUFFER_UTILS_STUB_H_

#include <C2Buffer.h>

#endif
