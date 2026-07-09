/*
 * gst-plugins-android — minimal <utils/RefBase.h> shim.
 *
 * AOSP libutils' RefBase provides intrusive strong/weak refcounting
 * via incStrong()/decStrong(). The SimpleC2Component / ALooper /
 * AMessage code paths we vendor depend on the strong-pointer (`sp<T>`)
 * API in particular — they DO NOT use weak refs in any path we exercise,
 * so wp<T> is a no-op compile-time stub.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_REFBASE_H_
#define GST_C2_PORTING_UTILS_REFBASE_H_

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace android {

class RefBase {
 public:
    void incStrong(const void * /*id*/) const noexcept {
        if (mRefs.fetch_add(1, std::memory_order_relaxed) == 0) {
            const_cast<RefBase*>(this)->onFirstRef();
        }
    }
    void decStrong(const void * /*id*/) const noexcept {
        if (mRefs.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            const_cast<RefBase*>(this)->onLastStrongRef(nullptr);
            delete this;
        }
    }
    int32_t getStrongCount() const noexcept { return mRefs.load(std::memory_order_relaxed); }

 protected:
    RefBase() noexcept : mRefs(0) {}
    virtual ~RefBase() = default;
    virtual void onFirstRef() {}
    virtual void onLastStrongRef(const void * /*id*/) {}
    virtual void onIncStrongAttempted(uint32_t /*flags*/, const void * /*id*/) {}
    virtual void onLastWeakRef(const void * /*id*/) {}
    virtual bool onIncStrongAttempted_unchecked() { return true; }

 private:
    mutable std::atomic<int32_t> mRefs;
};

template <typename T>
class sp {
 public:
    sp() noexcept : mPtr(nullptr) {}
    sp(std::nullptr_t) noexcept : mPtr(nullptr) {}
    /* AOSP code frequently uses `return NULL;` from a function returning
     * sp<T>. With C++11+ NULL is `0L`, which is ambiguous between sp(T*) and
     * sp(nullptr_t). Provide an `int` overload that selects the null path. */
    sp(int v) noexcept : mPtr(nullptr) { (void)v; /* assert(v == 0); */ }
    sp(long v) noexcept : mPtr(nullptr) { (void)v; }
    sp(T* p) noexcept : mPtr(p) { if (mPtr) mPtr->incStrong(this); }
    sp(const sp<T>& o) noexcept : mPtr(o.mPtr) { if (mPtr) mPtr->incStrong(this); }
    sp(sp<T>&& o) noexcept : mPtr(o.mPtr) { o.mPtr = nullptr; }

    /* Cross-type construction: sp<Derived> -> sp<Base>. */
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    sp(const sp<U>& o) noexcept : mPtr(o.get()) { if (mPtr) mPtr->incStrong(this); }
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    sp(sp<U>&& o) noexcept : mPtr(o.get()) { o.reset_without_decstrong(); }

    ~sp() { if (mPtr) mPtr->decStrong(this); }

    sp& operator=(const sp<T>& o) noexcept {
        T* p = o.mPtr;
        if (p) p->incStrong(this);
        if (mPtr) mPtr->decStrong(this);
        mPtr = p;
        return *this;
    }
    sp& operator=(sp<T>&& o) noexcept {
        if (this != &o) {
            if (mPtr) mPtr->decStrong(this);
            mPtr = o.mPtr;
            o.mPtr = nullptr;
        }
        return *this;
    }
    sp& operator=(T* p) noexcept {
        if (p) p->incStrong(this);
        if (mPtr) mPtr->decStrong(this);
        mPtr = p;
        return *this;
    }
    sp& operator=(std::nullptr_t) noexcept {
        if (mPtr) mPtr->decStrong(this);
        mPtr = nullptr;
        return *this;
    }

    T* get()   const noexcept { return mPtr; }
    T* operator->() const noexcept { return mPtr; }
    T& operator*()  const noexcept { return *mPtr; }
    explicit operator bool() const noexcept { return mPtr != nullptr; }

    void clear() noexcept {
        if (mPtr) { mPtr->decStrong(this); mPtr = nullptr; }
    }

    /* Internal: take ownership of an already-incStrong'd pointer. */
    void reset_without_decstrong() noexcept { mPtr = nullptr; }

 private:
    T* mPtr;
};

template <typename T, typename U>
bool operator==(const sp<T>& a, const sp<U>& b) noexcept { return a.get() == b.get(); }
template <typename T, typename U>
bool operator!=(const sp<T>& a, const sp<U>& b) noexcept { return a.get() != b.get(); }
template <typename T>
bool operator==(const sp<T>& a, std::nullptr_t) noexcept { return a.get() == nullptr; }
template <typename T>
bool operator!=(const sp<T>& a, std::nullptr_t) noexcept { return a.get() != nullptr; }
template <typename T>
bool operator==(std::nullptr_t, const sp<T>& a) noexcept { return a.get() == nullptr; }
template <typename T>
bool operator!=(std::nullptr_t, const sp<T>& a) noexcept { return a.get() != nullptr; }

/* Weak pointer — codec2 audio path never promotes/follows it across an
 * object's death, so a non-owning raw-pointer stub suffices. We keep a raw
 * T* so promote() can round-trip the pointer (ALooper/AHandler/AMessage all
 * promote() and compare these). NOTE: this does NOT implement true weak
 * semantics — promote() returns a live sp<> even if the object was deleted.
 * That matches how AOSP's foundation code uses these in the paths we
 * exercise (the roster prunes handlers explicitly), but is not a general
 * wp<> replacement. */
template <typename T>
class wp {
 public:
    wp() noexcept : mPtr(nullptr) {}
    wp(std::nullptr_t) noexcept : mPtr(nullptr) {}
    /* AOSP assigns `setID(0, NULL)` / `mLooper = NULL`; absorb the 0/0L
     * literal the same way sp<> does. */
    wp(int v) noexcept : mPtr(nullptr) { (void)v; }
    wp(long v) noexcept : mPtr(nullptr) { (void)v; }
    wp(T* p) noexcept : mPtr(p) {}
    wp(const sp<T>& s) noexcept : mPtr(s.get()) {}
    wp(const wp<T>& o) noexcept = default;
    wp(wp<T>&& o) noexcept = default;

    /* Cross-type construction: wp<Derived> -> wp<Base>. */
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
    wp(const wp<U>& o) noexcept : mPtr(o.unsafe_get()) {}

    wp& operator=(const wp<T>& o) noexcept = default;
    wp& operator=(wp<T>&& o)      noexcept = default;
    wp& operator=(T* p)            noexcept { mPtr = p; return *this; }
    wp& operator=(const sp<T>& s)  noexcept { mPtr = s.get(); return *this; }
    wp& operator=(std::nullptr_t)  noexcept { mPtr = nullptr; return *this; }

    sp<T> promote() const noexcept { return sp<T>(mPtr); }
    T*    unsafe_get() const noexcept { return mPtr; }
    void  clear()        noexcept { mPtr = nullptr; }

    bool operator==(const wp<T>& o) const noexcept { return mPtr == o.mPtr; }
    bool operator!=(const wp<T>& o) const noexcept { return mPtr != o.mPtr; }
    bool operator==(const T* p)     const noexcept { return mPtr == p; }
    bool operator!=(const T* p)     const noexcept { return mPtr != p; }
    bool operator==(std::nullptr_t) const noexcept { return mPtr == nullptr; }
    bool operator!=(std::nullptr_t) const noexcept { return mPtr != nullptr; }

 private:
    T* mPtr;
};

/* Convenience factory mirroring std::make_shared semantics for RefBase types. */
template <typename T, typename... Args>
inline sp<T> make_sp(Args&&... args) {
    return sp<T>(new T(std::forward<Args>(args)...));
}

}  /* namespace android */

#endif  /* GST_C2_PORTING_UTILS_REFBASE_H_ */
