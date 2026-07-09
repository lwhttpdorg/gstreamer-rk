/*
 * gst-plugins-android — libcutils property_get* implementation backed by getenv().
 *
 * Same mapping convention as <android-base/properties.h>: "foo.bar.baz" -> env
 * var "GST_C2_FOO_BAR_BAZ".
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <cutils/properties.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

std::string envify(const char* key) {
    std::string out("GST_C2_");
    if (!key) return out;
    for (const char* p = key; *p; ++p) {
        char c = *p;
        if (c == '.' || c == '-') out.push_back('_');
        else                       out.push_back((char)std::toupper((unsigned char)c));
    }
    return out;
}

bool parse_bool(const char* s, bool dflt) {
    if (!s) return dflt;
    std::string v(s);
    for (auto& c : v) c = (char)std::tolower((unsigned char)c);
    if (v == "1" || v == "true" || v == "yes" || v == "on")  return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    return dflt;
}

}  /* namespace */

extern "C" {

int property_get(const char* key, char* value, const char* default_value) {
    const char* v = std::getenv(envify(key).c_str());
    if (!v) v = default_value ? default_value : "";
    int n = (int)std::strlen(v);
    if (n >= PROPERTY_VALUE_MAX) n = PROPERTY_VALUE_MAX - 1;
    if (value) {
        std::memcpy(value, v, (size_t)n);
        value[n] = '\0';
    }
    return n;
}

int property_get_bool(const char* key, int default_value) {
    return parse_bool(std::getenv(envify(key).c_str()), default_value != 0) ? 1 : 0;
}

int32_t property_get_int32(const char* key, int32_t default_value) {
    const char* v = std::getenv(envify(key).c_str());
    if (!v) return default_value;
    return (int32_t)std::strtol(v, nullptr, 10);
}

int64_t property_get_int64(const char* key, int64_t default_value) {
    const char* v = std::getenv(envify(key).c_str());
    if (!v) return default_value;
    return (int64_t)std::strtoll(v, nullptr, 10);
}

int property_set(const char* /*key*/, const char* /*value*/) {
    /* No-op on host Linux. */
    return 0;
}

}  /* extern "C" */
