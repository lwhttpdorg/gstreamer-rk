/*
 * gst-plugins-android — Linux malloc-backed C2Allocator (linear only).
 *
 * Replaces AOSP's C2AllocatorIon for the audio path: we do not use ION on
 * Linux, so allocations are plain `aligned_alloc()`. A synthesised
 * native_handle_t with 0 fds carries the pointer/size so the existing C2
 * machinery (which insists on having a handle) is satisfied.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_C2_LINUX_MALLOC_ALLOCATOR_H_
#define GST_C2_PORTING_C2_LINUX_MALLOC_ALLOCATOR_H_

#include <C2.h>
#include <C2Buffer.h>
#include <memory>

namespace gst_c2 {

/* Public so C2Store_linux.cpp can hand it out via fetchAllocator(). */
std::shared_ptr<C2Allocator> GetLinuxLinearAllocator();

}  /* namespace gst_c2 */

#endif
