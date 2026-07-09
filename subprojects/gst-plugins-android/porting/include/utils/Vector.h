/*
 * gst-plugins-android — minimal <utils/Vector.h> shim.
 *
 * AOSP code uses android::Vector<T> in a handful of spots inside
 * ALooper/AMessage. We provide just the surface they need, on top of
 * std::vector. Same memory model (dynamic array), better debuggability.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_VECTOR_H_
#define GST_C2_PORTING_UTILS_VECTOR_H_

#include <algorithm>
#include <stddef.h>
#include <utility>
#include <vector>
#include <utils/String16.h>   /* AOSP code uses Vector<String16> in a few foundation headers */
#include <utils/String8.h>

namespace android {

template <typename T>
class Vector {
 public:
    Vector() = default;
    Vector(const Vector&) = default;
    Vector(Vector&&) noexcept = default;
    Vector& operator=(const Vector&) = default;
    Vector& operator=(Vector&&) noexcept = default;
    ~Vector() = default;

    size_t size()    const noexcept { return v_.size(); }
    bool   isEmpty() const noexcept { return v_.empty(); }
    void   clear()                   { v_.clear(); }

    const T& itemAt(size_t i) const  { return v_[i]; }
    T&       editItemAt(size_t i)    { return v_[i]; }
    const T& operator[](size_t i) const { return v_[i]; }
    T&       operator[](size_t i)       { return v_[i]; }
    const T& top()  const            { return v_.back(); }
    T&       editTop()               { return v_.back(); }

    ssize_t  add(const T& item)      { v_.push_back(item); return (ssize_t)v_.size() - 1; }
    ssize_t  add(T&& item)           { v_.push_back(std::move(item)); return (ssize_t)v_.size() - 1; }
    void     push(const T& item)     { v_.push_back(item); }
    void     push(T&& item)          { v_.push_back(std::move(item)); }

    ssize_t  insertAt(const T& item, size_t i) { v_.insert(v_.begin() + i, item); return (ssize_t)i; }
    ssize_t  removeAt(size_t i)      { v_.erase(v_.begin() + i); return (ssize_t)i; }
    void     pop()                   { v_.pop_back(); }
    ssize_t  removeItemsAt(size_t i, size_t n = 1) {
        v_.erase(v_.begin() + i, v_.begin() + i + n);
        return (ssize_t)i;
    }

    ssize_t  indexOf(const T& item) const {
        auto it = std::find(v_.begin(), v_.end(), item);
        return it == v_.end() ? -1 : (ssize_t)(it - v_.begin());
    }

    const T* array()       const { return v_.data(); }
    T*       editArray()         { return v_.data(); }

 private:
    std::vector<T> v_;
};

}  /* namespace android */

#endif
