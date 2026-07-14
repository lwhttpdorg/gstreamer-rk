/* gst-plugins-android — <utils/threads.h> shim aggregating Thread / Mutex /
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * Condition / RWLock. AOSP foundation code mostly only needs Mutex/Condition
 * pulled in via this single header.
 */
#ifndef GST_C2_PORTING_UTILS_THREADS_H_
#define GST_C2_PORTING_UTILS_THREADS_H_

#include <utils/Condition.h>
#include <utils/Mutex.h>
#include <utils/Thread.h>
/* AOSP's <utils/threads.h> also surfaces systemTime()/SYSTEM_TIME_MONOTONIC
 * (via <utils/Timers.h>); ALooper::GetNowUs() relies on that include path. */
#include <utils/Timers.h>

#endif
