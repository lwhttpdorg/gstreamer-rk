/* gst-plugins-android — <utils/Trace.h> no-op shim. ATRACE_* go nowhere. */
 * SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef GST_C2_PORTING_UTILS_TRACE_H_
#define GST_C2_PORTING_UTILS_TRACE_H_

#include <stdint.h>

#ifndef ATRACE_TAG
#define ATRACE_TAG 0
#endif

/* AOSP code (#define ATRACE_TAG ATRACE_TAG_VIDEO) selects a subsystem tag.
 * On Linux tracing is a no-op, so every tag is just 0. */
#define ATRACE_TAG_ALWAYS        0
#define ATRACE_TAG_VIDEO         0
#define ATRACE_TAG_AUDIO         0
#define ATRACE_TAG_CAMERA        0
#define ATRACE_TAG_GRAPHICS      0
#define ATRACE_TAG_HAL           0
#define ATRACE_TAG_NEVER         0

#define ATRACE_CALL()                 do {} while (0)
#define ATRACE_NAME(name)             do { (void)(name); } while (0)
#define ATRACE_BEGIN(name)            do { (void)(name); } while (0)
#define ATRACE_END()                  do {} while (0)
#define ATRACE_INT(name, value)       do { (void)(name); (void)(value); } while (0)
#define ATRACE_INT64(name, value)     do { (void)(name); (void)(value); } while (0)
#define ATRACE_ASYNC_BEGIN(name, c)   do { (void)(name); (void)(c); } while (0)
#define ATRACE_ASYNC_END(name, c)     do { (void)(name); (void)(c); } while (0)
#define ATRACE_ENABLED()              0

#ifdef __cplusplus
namespace android {
/* C2Buffer.cpp constructs an android::ScopedTrace on the stack. No-op here. */
class ScopedTrace {
 public:
    ScopedTrace(uint64_t /*tag*/, const char* /*name*/) {}
    ~ScopedTrace() {}
};
}  /* namespace android */
#endif

#endif
