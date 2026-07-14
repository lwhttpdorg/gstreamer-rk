/* gst-plugins-android — <android-base/macros.h> shim. */
 * SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef GST_C2_PORTING_ANDROID_BASE_MACROS_H_
#define GST_C2_PORTING_ANDROID_BASE_MACROS_H_

#ifndef DISALLOW_COPY_AND_ASSIGN
#define DISALLOW_COPY_AND_ASSIGN(T) \
    T(const T&) = delete;           \
    T& operator=(const T&) = delete
#endif

#ifndef DISALLOW_IMPLICIT_CONSTRUCTORS
#define DISALLOW_IMPLICIT_CONSTRUCTORS(T) \
    T() = delete;                          \
    DISALLOW_COPY_AND_ASSIGN(T)
#endif

#ifndef arraysize
#define arraysize(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef FRIEND_TEST
#define FRIEND_TEST(test_case_name, test_name) /* no-op without gtest */
#endif

#ifndef ABI_LIKELY
#define ABI_LIKELY(x)   __builtin_expect(!!(x), 1)
#define ABI_UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

#ifndef ATTRIBUTE_UNUSED
#define ATTRIBUTE_UNUSED __attribute__((__unused__))
#endif

#ifndef WARN_UNUSED
#define WARN_UNUSED __attribute__((__warn_unused_result__))
#endif

#endif
