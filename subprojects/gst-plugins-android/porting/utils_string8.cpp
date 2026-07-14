/*
 * gst-plugins-android — String8::appendFormat() implementation.
 * Kept out of the header so we don't need <cstdarg> everywhere.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <utils/String8.h>

#include <cstdarg>
#include <cstdio>

namespace android {

status_t String8::appendFormat(const char* fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return UNKNOWN_ERROR; }
    size_t old = s_.size();
    s_.resize(old + (size_t)n);
    std::vsnprintf(s_.data() + old, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return OK;
}

}  /* namespace android */
