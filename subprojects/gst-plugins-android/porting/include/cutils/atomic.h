/*
 * gst-plugins-android — <cutils/atomic.h> shim mapping the android_atomic_*
 * family to GCC __atomic_* builtins.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_CUTILS_ATOMIC_H_
#define GST_C2_PORTING_CUTILS_ATOMIC_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int32_t android_atomic_inc(volatile int32_t* p) {
    return __atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST);
}
static inline int32_t android_atomic_dec(volatile int32_t* p) {
    return __atomic_fetch_sub(p, 1, __ATOMIC_SEQ_CST);
}
static inline int32_t android_atomic_add(int32_t v, volatile int32_t* p) {
    return __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST);
}
static inline int32_t android_atomic_and(int32_t v, volatile int32_t* p) {
    return __atomic_fetch_and(p, v, __ATOMIC_SEQ_CST);
}
static inline int32_t android_atomic_or(int32_t v, volatile int32_t* p) {
    return __atomic_fetch_or(p, v, __ATOMIC_SEQ_CST);
}
static inline int32_t android_atomic_release_load(volatile const int32_t* p) {
    return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}
static inline void android_atomic_release_store(int32_t v, volatile int32_t* p) {
    __atomic_store_n(p, v, __ATOMIC_RELEASE);
}
static inline int android_atomic_release_cas(int32_t oldv, int32_t newv, volatile int32_t* p) {
    int32_t exp = oldv;
    return __atomic_compare_exchange_n(p, &exp, newv, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED) ? 0 : 1;
}

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif
