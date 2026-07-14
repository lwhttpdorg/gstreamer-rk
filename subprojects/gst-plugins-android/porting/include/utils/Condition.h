/*
 * gst-plugins-android — <utils/Condition.h> shim built on std::condition_variable_any.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_CONDITION_H_
#define GST_C2_PORTING_UTILS_CONDITION_H_

#include <chrono>
#include <condition_variable>
#include <utils/Errors.h>
#include <utils/Mutex.h>

namespace android {

class Condition {
 public:
    enum WakeUpType {
        WAKE_UP_ONE = 0,
        WAKE_UP_ALL = 1,
    };

    Condition() = default;
    explicit Condition(int /*type*/) {}

    /* Required by AOSP API. The mutex is locked when wait() is called and stays
     * locked when wait() returns; we delegate to std::condition_variable_any
     * which can accept the user-supplied lock-like object. */
    status_t wait(Mutex& mutex) {
        std::unique_lock<std::mutex> lk(mutex.std_mutex(), std::adopt_lock);
        mCv.wait(lk);
        lk.release();
        return OK;
    }

    status_t waitRelative(Mutex& mutex, int64_t reltime_ns) {
        std::unique_lock<std::mutex> lk(mutex.std_mutex(), std::adopt_lock);
        const auto status = mCv.wait_for(lk, std::chrono::nanoseconds(reltime_ns));
        lk.release();
        return status == std::cv_status::timeout ? TIMED_OUT : OK;
    }

    void signal()    { mCv.notify_one(); }
    void signal(WakeUpType t) { if (t == WAKE_UP_ALL) mCv.notify_all(); else mCv.notify_one(); }
    void broadcast() { mCv.notify_all(); }

 private:
    std::condition_variable mCv;

    Condition(const Condition&) = delete;
    Condition& operator=(const Condition&) = delete;
};

}  /* namespace android */

#endif
