/* gst-plugins-android — <utils/Timers.h> shim. */
 * SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef GST_C2_PORTING_UTILS_TIMERS_H_
#define GST_C2_PORTING_UTILS_TIMERS_H_

#include <stdint.h>
#include <time.h>

typedef int64_t nsecs_t;

enum {
    SYSTEM_TIME_REALTIME   = 0,
    SYSTEM_TIME_MONOTONIC  = 1,
    SYSTEM_TIME_PROCESS    = 2,
    SYSTEM_TIME_THREAD     = 3,
    SYSTEM_TIME_BOOTTIME   = 4,
};

static inline nsecs_t systemTime(int clk = SYSTEM_TIME_MONOTONIC) {
    struct timespec ts;
    clockid_t id = CLOCK_MONOTONIC;
    switch (clk) {
        case SYSTEM_TIME_REALTIME: id = CLOCK_REALTIME; break;
        case SYSTEM_TIME_PROCESS:  id = CLOCK_PROCESS_CPUTIME_ID; break;
        case SYSTEM_TIME_THREAD:   id = CLOCK_THREAD_CPUTIME_ID; break;
        case SYSTEM_TIME_BOOTTIME: id = CLOCK_BOOTTIME; break;
        default:                   id = CLOCK_MONOTONIC; break;
    }
    clock_gettime(id, &ts);
    return (nsecs_t)ts.tv_sec * 1000000000LL + (nsecs_t)ts.tv_nsec;
}

static inline nsecs_t s2ns(nsecs_t v)   { return v * 1000000000LL; }
static inline nsecs_t ms2ns(nsecs_t v)  { return v * 1000000LL; }
static inline nsecs_t us2ns(nsecs_t v)  { return v * 1000LL; }
static inline nsecs_t ns2s(nsecs_t v)   { return v / 1000000000LL; }
static inline nsecs_t ns2ms(nsecs_t v)  { return v / 1000000LL; }
static inline nsecs_t ns2us(nsecs_t v)  { return v / 1000LL; }

static inline nsecs_t elapsedRealtimeNano() { return systemTime(SYSTEM_TIME_BOOTTIME); }

#endif
