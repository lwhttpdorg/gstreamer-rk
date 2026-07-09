/*
 * gst-plugins-android — <android-base/properties.h> shim.
 *
 * AOSP property_get*/GetProperty calls are mapped to plain `getenv()` lookups.
 * The audio decoders only read a handful of tuning knobs (notably
 * `media.aac_51_output_enabled`). Convention: any "foo.bar.baz" key is
 * exposed via the env var GST_C2_FOO_BAR_BAZ (uppercase, dots -> underscores).
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_ANDROID_BASE_PROPERTIES_H_
#define GST_C2_PORTING_ANDROID_BASE_PROPERTIES_H_

#include <cctype>
#include <cstdlib>
#include <string>

namespace android {
namespace base {

inline std::string _gst_c2_envify(const std::string& key) {
    std::string out;
    out.reserve(key.size() + 8);
    out.append("GST_C2_");
    for (char c : key) {
        if (c == '.' || c == '-') out.push_back('_');
        else                       out.push_back((char)std::toupper((unsigned char)c));
    }
    return out;
}

inline std::string GetProperty(const std::string& key, const std::string& default_value) {
    const char* v = std::getenv(_gst_c2_envify(key).c_str());
    return v ? std::string(v) : default_value;
}

inline bool GetBoolProperty(const std::string& key, bool default_value) {
    const char* v = std::getenv(_gst_c2_envify(key).c_str());
    if (!v) return default_value;
    /* Accept the usual truthy/falsy strings. */
    std::string s(v);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    if (s == "1" || s == "true" || s == "yes" || s == "on")  return true;
    if (s == "0" || s == "false" || s == "no" || s == "off") return false;
    return default_value;
}

template <typename T>
inline T GetIntProperty(const std::string& key, T default_value,
                         T /*min*/ = T(0), T /*max*/ = T(0)) {
    const char* v = std::getenv(_gst_c2_envify(key).c_str());
    if (!v) return default_value;
    return (T)std::strtoll(v, nullptr, 10);
}

inline bool SetProperty(const std::string& /*key*/, const std::string& /*value*/) {
    /* No-op outside Android. */
    return true;
}

}  /* namespace base */
}  /* namespace android */

#endif
