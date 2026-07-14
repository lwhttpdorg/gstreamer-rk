/*
 * gst-plugins-android — liblog replacement shim
 *
 * Provides the Android `ALOG*` macros + helpers used pervasively by AOSP
 * codec2/foundation sources. Routes to a single gst_c2_log() function
 * implemented in porting/gst_c2_log.cpp that honours the GST_C2_LOG_LEVEL
 * env var.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_LOG_LOG_H_
#define GST_C2_PORTING_LOG_LOG_H_

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Pull in libstdc++ headers that AOSP code routinely forgets to #include
 * (cstring/memory/string). Keeping them here means every translation unit
 * that hits <log/log.h> — which is everything via LOG_TAG/ALOG — gets them
 * transparently, the same way bionic+libcutils would in an Android build. */
#ifdef __cplusplus
#include <cstddef>   /* offsetof — C2Param.h uses it unqualified */
#include <cstring>
#include <limits>    /* std::numeric_limits — C2Buffer.h:464 */
#include <memory>
#include <string>
#include <vector>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Mirrors the android-log priority enum so any inline references in vendored
 * code keep their integer comparisons valid. */
typedef enum {
  ANDROID_LOG_UNKNOWN = 0,
  ANDROID_LOG_DEFAULT,
  ANDROID_LOG_VERBOSE,
  ANDROID_LOG_DEBUG,
  ANDROID_LOG_INFO,
  ANDROID_LOG_WARN,
  ANDROID_LOG_ERROR,
  ANDROID_LOG_FATAL,
  ANDROID_LOG_SILENT,
} android_LogPriority;

/* Implemented in porting/gst_c2_log.cpp. */
void gst_c2_log(int priority, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

int  gst_c2_log_enabled(int priority);  /* fast path used by ALOGV macros */

#ifdef __cplusplus
}  /* extern "C" */
#endif

#ifndef LOG_TAG
#define LOG_TAG "c2"
#endif

/* The classic Android macros that AOSP code uses unconditionally. */
#define ALOGV(...) do { if (gst_c2_log_enabled(ANDROID_LOG_VERBOSE)) gst_c2_log(ANDROID_LOG_VERBOSE, LOG_TAG, __VA_ARGS__); } while (0)
#define ALOGD(...) do { if (gst_c2_log_enabled(ANDROID_LOG_DEBUG))   gst_c2_log(ANDROID_LOG_DEBUG,   LOG_TAG, __VA_ARGS__); } while (0)
#define ALOGI(...) do { if (gst_c2_log_enabled(ANDROID_LOG_INFO))    gst_c2_log(ANDROID_LOG_INFO,    LOG_TAG, __VA_ARGS__); } while (0)
#define ALOGW(...) do { if (gst_c2_log_enabled(ANDROID_LOG_WARN))    gst_c2_log(ANDROID_LOG_WARN,    LOG_TAG, __VA_ARGS__); } while (0)
#define ALOGE(...) do { if (gst_c2_log_enabled(ANDROID_LOG_ERROR))   gst_c2_log(ANDROID_LOG_ERROR,   LOG_TAG, __VA_ARGS__); } while (0)
#define ALOGF(...) do { gst_c2_log(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__); abort(); } while (0)

#define ALOGV_IF(cond, ...) do { if (cond) ALOGV(__VA_ARGS__); } while (0)
#define ALOGD_IF(cond, ...) do { if (cond) ALOGD(__VA_ARGS__); } while (0)
#define ALOGI_IF(cond, ...) do { if (cond) ALOGI(__VA_ARGS__); } while (0)
#define ALOGW_IF(cond, ...) do { if (cond) ALOGW(__VA_ARGS__); } while (0)
#define ALOGE_IF(cond, ...) do { if (cond) ALOGE(__VA_ARGS__); } while (0)

#define ALOG_ASSERT(cond, ...) do { \
    if (__builtin_expect(!(cond), 0)) { \
      gst_c2_log(ANDROID_LOG_FATAL, LOG_TAG, "ALOG_ASSERT(%s) " #cond, "fail"); \
      gst_c2_log(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__); \
      abort(); \
    } \
  } while (0)

#define LOG_ALWAYS_FATAL(...)  do { gst_c2_log(ANDROID_LOG_FATAL, LOG_TAG, __VA_ARGS__); abort(); } while (0)
#define LOG_ALWAYS_FATAL_IF(cond, ...) do { if (cond) LOG_ALWAYS_FATAL(__VA_ARGS__); } while (0)
#define LOG_FATAL_IF(cond, ...)        LOG_ALWAYS_FATAL_IF(cond, __VA_ARGS__)

#define LOG_TAG_IS(t) 1                /* legacy macro from older AOSP headers */
#define LOG_NDEBUG    1                /* disable verbose by default — runtime env still works */
#define IF_ALOGV()    if (gst_c2_log_enabled(ANDROID_LOG_VERBOSE))

/* These are referenced by adebug.h-ish macros in the codec2 codebase. */
#define android_printLog(prio, tag, ...)  gst_c2_log(prio, tag, __VA_ARGS__)
#define __android_log_print(prio, tag, ...) gst_c2_log(prio, tag, __VA_ARGS__)
#define __android_log_vprint(prio, tag, fmt, ap) do { \
    char _gst_c2_buf[1024]; vsnprintf(_gst_c2_buf, sizeof(_gst_c2_buf), fmt, ap); \
    gst_c2_log(prio, tag, "%s", _gst_c2_buf); \
  } while (0)
#define __android_log_assert(cond, tag, ...) do { \
    gst_c2_log(ANDROID_LOG_FATAL, tag, __VA_ARGS__); abort(); \
  } while (0)

#endif  /* GST_C2_PORTING_LOG_LOG_H_ */
