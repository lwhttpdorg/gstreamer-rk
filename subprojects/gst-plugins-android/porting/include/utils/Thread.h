/*
 * gst-plugins-android — <utils/Thread.h> shim.
 *
 * AOSP's android::Thread is a RefBase subclass with run()/requestExit()/join()
 * semantics, invoking a virtual threadLoop() that may return false to exit.
 * SimpleC2Component spawns a worker thread via ALooper's pump, but the underlying
 * primitive here is what AOSP libutils provides.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_THREAD_H_
#define GST_C2_PORTING_UTILS_THREAD_H_

#include <atomic>
#include <cstdint>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <utils/Errors.h>
#include <utils/Mutex.h>
#include <utils/RefBase.h>

namespace android {

/* Subset of <utils/ThreadDefs.h>. */
enum {
    PRIORITY_LOWEST          =  19,
    PRIORITY_BACKGROUND      =  10,
    PRIORITY_NORMAL          =   0,
    PRIORITY_FOREGROUND      =  -2,
    PRIORITY_DISPLAY         =  -4,
    PRIORITY_URGENT_DISPLAY  =  -8,
    PRIORITY_AUDIO           = -16,
    PRIORITY_URGENT_AUDIO    = -19,
    PRIORITY_HIGHEST         = -20,
    PRIORITY_DEFAULT         =   0,
};

class Thread : public RefBase {
 public:
    explicit Thread(bool canCallJava = false) : mCanCallJava(canCallJava) {}
    ~Thread() override { requestExitAndWait(); }

    /* Spawn the worker thread. `prio` is best-effort — we ignore it here
     * (no CAP_SYS_NICE assumption on a generic Linux host). */
    status_t run(const char* /*name*/ = nullptr, int32_t /*prio*/ = 0, size_t /*stack*/ = 0) {
        Mutex::Autolock _l(mLock);
        if (mRunning.load()) return INVALID_OPERATION;
        mExit.store(false);
        mRunning.store(true);
        mWorker = std::thread(&Thread::_threadEntry, this);
        return OK;
    }

    void requestExit() { mExit.store(true); }

    status_t requestExitAndWait() {
        requestExit();
        if (mWorker.joinable()) mWorker.join();
        return OK;
    }

    status_t join() {
        if (mWorker.joinable()) mWorker.join();
        return OK;
    }

    bool exitPending() const { return mExit.load(); }
    bool isRunning()  const { return mRunning.load(); }

 protected:
    /* Subclasses implement this; returning false (or setting exitPending()) ends
     * the loop. */
    virtual bool threadLoop() = 0;
    virtual status_t readyToRun() { return OK; }

 private:
    void _threadEntry() {
        if (readyToRun() == OK) {
            while (!mExit.load() && threadLoop()) {
                /* loop */
            }
        }
        mRunning.store(false);
    }

    Mutex                mLock;
    std::thread          mWorker;
    std::atomic<bool>    mRunning {false};
    std::atomic<bool>    mExit    {false};
    bool                 mCanCallJava;

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
};

/* Opaque thread-ID handle. AOSP declares this in <utils/ThreadDefs.h>
 * (system/core/libutils) as `typedef void* android_thread_id_t;`. We mirror
 * the pointer width so a full pthread_t survives the round-trip — truncating
 * to int32_t could alias two threads and break ALooper::LooperThread::
 * isCurrentThread(). The pointer is never dereferenced; it is only compared
 * for identity. The NULL sentinel ALooper uses (`mThreadId(NULL)`) then also
 * type-checks cleanly. */
typedef void* android_thread_id_t;

/* Free function used by AOSP libutils and a couple of AMessage paths. */
inline android_thread_id_t androidGetThreadId() {
    return reinterpret_cast<android_thread_id_t>(
        static_cast<uintptr_t>(::pthread_self()));
}
inline pid_t    androidGetTid()           {
    /* gettid() is in glibc >= 2.30; we use the syscall fallback to stay
     * portable across older sysroots. */
#if defined(SYS_gettid)
    return (pid_t)::syscall(SYS_gettid);
#else
    return (pid_t)::pthread_self();
#endif
}

}  /* namespace android */

#endif
