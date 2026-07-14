/*
 * gst-plugins-android — stub for C2AllocatorIon.h.
 *
 * C2Buffer.cpp has `using android::C2AllocatorIon;` (around line 42) but the
 * audio path never instantiates it (ION is replaced by our malloc allocator).
 * An incomplete forward declaration satisfies the using-declaration.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_C2_ALLOCATOR_ION_STUB_H_
#define GST_C2_PORTING_C2_ALLOCATOR_ION_STUB_H_

#include <C2Buffer.h>

namespace android {
class C2AllocatorIon;   /* incomplete — never instantiated on Linux */
}

#endif
