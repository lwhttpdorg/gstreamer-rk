/* gst-plugins-android — <cutils/compiler.h> shim. */
 * SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef GST_C2_PORTING_CUTILS_COMPILER_H_
#define GST_C2_PORTING_CUTILS_COMPILER_H_
#ifndef CC_LIKELY
#define CC_LIKELY(x)   __builtin_expect(!!(x), 1)
#define CC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif
#endif
