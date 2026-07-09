/*
 * gst-plugins-android — <cutils/properties.h> shim.
 *
 * The legacy C API. Implemented in porting/properties_stub.cpp so we don't have
 * to leak <cstdio> into every translation unit that includes this header.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_CUTILS_PROPERTIES_H_
#define GST_C2_PORTING_CUTILS_PROPERTIES_H_

#include <stdint.h>

#define PROPERTY_VALUE_MAX 92

/* bionic <android/api-level.h> compatibility. C2SoftAacDec.cpp compares
 * mDeviceApiLevel < __ANDROID_API_S__ and calls android_get_device_api_level();
 * neither exists on glibc. We are effectively "newer than any release", so
 * report a very high API level. */
#ifndef __ANDROID_API_S__
#define __ANDROID_API_S__ 31
#endif
#ifndef __ANDROID_API_T__
#define __ANDROID_API_T__ 33
#endif
#ifndef __ANDROID_API_U__
#define __ANDROID_API_U__ 34
#endif

#ifdef __cplusplus
extern "C" {
#endif

static inline int android_get_device_api_level(void) { return 10000; }

/* Returns the number of bytes written to `value` (excluding the terminator),
 * or the length of `default_value` if the property is unset. Mirrors the AOSP
 * libcutils ABI. */
int property_get(const char* key, char* value, const char* default_value);

/* AOSP exposes a `bool` variant. */
int property_get_bool(const char* key, int default_value);

int32_t  property_get_int32(const char* key, int32_t default_value);
int64_t  property_get_int64(const char* key, int64_t default_value);

int  property_set(const char* key, const char* value);

#ifdef __cplusplus
}
#endif

#endif
