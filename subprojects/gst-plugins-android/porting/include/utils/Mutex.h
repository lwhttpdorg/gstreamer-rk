/*
 * gst-plugins-android — <utils/Mutex.h> shim.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_MUTEX_H_
#define GST_C2_PORTING_UTILS_MUTEX_H_

#include <mutex>
#include <utils/Errors.h>

namespace android {

class Mutex {
 public:
    enum {
        PRIVATE = 0,
        SHARED  = 1,
    };

    Mutex() = default;
    explicit Mutex(int /*type*/) {}
    explicit Mutex(const char* /*name*/) {}
    Mutex(int /*type*/, const char* /*name*/) {}
    ~Mutex() = default;

    status_t lock()    { mMutex.lock(); return OK; }
    void     unlock()  { mMutex.unlock(); }
    status_t tryLock() { return mMutex.try_lock() ? OK : WOULD_BLOCK; }
    /* AOSP code uses ALooper's timed lock variants too — fall back to plain lock since
     * the audio decoders don't depend on the timed-wait semantics. */
    status_t timedLock(int64_t /*nsec*/) { return lock(); }

    class Autolock {
     public:
        explicit Autolock(Mutex& m) : mRef(m) { mRef.lock(); }
        explicit Autolock(Mutex* m) : mRef(*m) { mRef.lock(); }
        ~Autolock() { mRef.unlock(); }
     private:
        Mutex& mRef;
        Autolock(const Autolock&) = delete;
        Autolock& operator=(const Autolock&) = delete;
    };

    /* Required by std::condition_variable_any-style wrappers (we use one ourselves
     * in Condition.h). */
    std::mutex& std_mutex() noexcept { return mMutex; }

 private:
    std::mutex mMutex;

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
};

/* AOSP code uses both Mutex::Autolock and the un-namespaced `AutoMutex`
 * spelling. Provide the alias at namespace scope so unqualified references
 * (which AOSP relies on after `using namespace android;`) resolve. */
typedef Mutex::Autolock AutoMutex;

}  /* namespace android */

#endif
