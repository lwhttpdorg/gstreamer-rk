/*
 * gst-plugins-android — minimal <utils/KeyedVector.h> shim.
 *
 * AMessage holds key-value pairs in a KeyedVector<AString, item>. We back
 * it with std::map (sorted lookup) + a parallel vector for insertion-order
 * iteration so AMessage's countEntries()/keyAt(i) ordering is stable.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_KEYEDVECTOR_H_
#define GST_C2_PORTING_UTILS_KEYEDVECTOR_H_

#include <algorithm>
#include <map>
#include <stddef.h>
#include <utility>
#include <vector>

/* AOSP's <utils/KeyedVector.h> transitively pulls in <utils/Vector.h> (and
 * SortedVector.h). Foundation headers such as ALooperRoster.h rely on that
 * to name `Vector<...>` after only including KeyedVector.h, so mirror it. */
#include <utils/Vector.h>

namespace android {

template <typename K, typename V>
class KeyedVector {
 public:
    KeyedVector() = default;
    ~KeyedVector() = default;

    size_t  size() const { return keys_.size(); }
    bool    isEmpty() const { return keys_.empty(); }
    void    clear() { keys_.clear(); values_.clear(); index_.clear(); }

    /* AOSP semantics: add() returns the inserted-at index (or replaces). */
    ssize_t add(const K& key, const V& value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            values_[it->second] = value;
            return (ssize_t)it->second;
        }
        ssize_t idx = (ssize_t)keys_.size();
        keys_.push_back(key);
        values_.push_back(value);
        index_.emplace(key, (size_t)idx);
        return idx;
    }
    ssize_t add(const K& key, V&& value) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            values_[it->second] = std::move(value);
            return (ssize_t)it->second;
        }
        ssize_t idx = (ssize_t)keys_.size();
        keys_.push_back(key);
        values_.push_back(std::move(value));
        index_.emplace(key, (size_t)idx);
        return idx;
    }

    /* Replace value at a known key. */
    ssize_t replaceValueFor(const K& key, const V& value) {
        return add(key, value);
    }

    ssize_t indexOfKey(const K& key) const {
        auto it = index_.find(key);
        return it == index_.end() ? (ssize_t)-1 : (ssize_t)it->second;
    }

    const K& keyAt(size_t i)   const { return keys_[i]; }
    const V& valueAt(size_t i) const { return values_[i]; }
    V&       editValueAt(size_t i)   { return values_[i]; }

    ssize_t removeItemsAt(size_t i, size_t n = 1) {
        for (size_t j = 0; j < n && (i + j) < keys_.size(); ++j) {
            index_.erase(keys_[i + j]);
        }
        keys_.erase(keys_.begin() + i, keys_.begin() + i + n);
        values_.erase(values_.begin() + i, values_.begin() + i + n);
        /* Rebuild index for everything past i. */
        for (size_t k = i; k < keys_.size(); ++k) index_[keys_[k]] = k;
        return (ssize_t)i;
    }

    ssize_t removeItem(const K& key) {
        ssize_t i = indexOfKey(key);
        if (i < 0) return -1;
        return removeItemsAt((size_t)i, 1);
    }

 private:
    std::vector<K>           keys_;
    std::vector<V>           values_;
    std::map<K, size_t>      index_;
};

}  /* namespace android */

#endif
