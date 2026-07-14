/*
 * gst-plugins-android — minimal <utils/String8.h> shim.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_STRING8_H_
#define GST_C2_PORTING_UTILS_STRING8_H_

#include <stddef.h>
#include <string>
#include <utils/Errors.h>

namespace android {

class String8 {
 public:
    String8() = default;
    String8(const char* s) : s_(s ? s : "") {}
    String8(const char* s, size_t n) : s_(s, n) {}
    String8(const std::string& s) : s_(s) {}
    String8(const String8&) = default;
    String8(String8&&) noexcept = default;
    String8& operator=(const String8&) = default;
    String8& operator=(String8&&) noexcept = default;

    const char* string() const noexcept { return s_.c_str(); }
    const char* c_str()  const noexcept { return s_.c_str(); }
    size_t      length() const noexcept { return s_.size(); }
    size_t      size()   const noexcept { return s_.size(); }
    bool        isEmpty() const noexcept { return s_.empty(); }
    void        clear()                  { s_.clear(); }

    String8&    append(const char* s)        { s_.append(s); return *this; }
    String8&    append(const String8& o)     { s_.append(o.s_); return *this; }
    status_t    appendFormat(const char* fmt, ...);

    bool operator==(const String8& o) const { return s_ == o.s_; }
    bool operator!=(const String8& o) const { return s_ != o.s_; }
    bool operator<(const String8& o)  const { return s_ <  o.s_; }

 private:
    std::string s_;
};

}  /* namespace android */

#endif
