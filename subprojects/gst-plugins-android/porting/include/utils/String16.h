/* gst-plugins-android — minimal <utils/String16.h> shim.
 *
 * The audio path never genuinely consumes UTF-16 text, but
 * ALooperRoster::dump() (compiled into libc2sf) constructs String16 literals
 * and compares them with operator==, and Vector<String16> is named in
 * ALooperRoster.h. So we back this with a std::string (UTF-8) and provide the
 * minimal value-semantics + equality the foundation code relies on. The
 * string() accessor still returns char16_t* for source compatibility, backed
 * by a lazily-built UTF-16 mirror.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_STRING16_H_
#define GST_C2_PORTING_UTILS_STRING16_H_
#include <cstddef>
#include <string>
namespace android {
class String16 {
 public:
    String16() = default;
    explicit String16(const char* s) : s_(s ? s : "") {}
    String16(const String16&) = default;
    String16(String16&&) noexcept = default;
    String16& operator=(const String16&) = default;
    String16& operator=(String16&&) noexcept = default;

    size_t size()    const { return u16_().size(); }
    bool   isEmpty() const { return s_.empty(); }

    const char16_t* string() const {
        u16_();             /* build/refresh the mirror */
        return u16_cache_.c_str();
    }

    bool operator==(const String16& o) const { return s_ == o.s_; }
    bool operator!=(const String16& o) const { return s_ != o.s_; }
    bool operator<(const String16& o)  const { return s_ <  o.s_; }

 private:
    const std::u16string& u16_() const {
        u16_cache_.assign(s_.begin(), s_.end());   /* ASCII widen; sufficient for the literals used */
        return u16_cache_;
    }
    std::string s_;
    mutable std::u16string u16_cache_;
};
}  /* namespace android */
#endif
