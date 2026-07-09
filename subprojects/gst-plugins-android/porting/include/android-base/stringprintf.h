/*
 * gst-plugins-android — <android-base/stringprintf.h> shim (header-only).
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_ANDROID_BASE_STRINGPRINTF_H_
#define GST_C2_PORTING_ANDROID_BASE_STRINGPRINTF_H_

#include <cstdarg>
#include <cstdio>
#include <cstring>   /* AOSP code pulls strncmp/strnlen through this header */
#include <string>

namespace android {
namespace base {

inline std::string StringPrintf(const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return {}; }
    std::string out;
    out.resize((size_t)n);
    std::vsnprintf(out.data(), (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

inline void StringAppendF(std::string* dst, const char* fmt, ...) {
    if (!dst) return;
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return; }
    size_t old = dst->size();
    dst->resize(old + (size_t)n);
    std::vsnprintf(dst->data() + old, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
}

}  /* namespace base */
}  /* namespace android */

#endif
