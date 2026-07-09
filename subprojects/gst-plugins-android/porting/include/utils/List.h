/*
 * gst-plugins-android — <utils/List.h> shim backed by std::list.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_LIST_H_
#define GST_C2_PORTING_UTILS_LIST_H_

#include <list>
#include <utility>

namespace android {

template <typename T>
class List : public std::list<T> {
 public:
    using std::list<T>::list;
    /* AOSP API quirks we map to std::list. */
    void push(const T& v) { this->push_back(v); }
    void push(T&& v)       { this->push_back(std::move(v)); }
    T&       editItemAt(typename std::list<T>::iterator it) { return *it; }
    const T& itemAt(typename std::list<T>::const_iterator it) const { return *it; }
    bool     empty_list() const { return this->empty(); }
};

}  /* namespace android */

#endif
