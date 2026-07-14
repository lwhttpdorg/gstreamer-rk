/*
 * gst-plugins-android — minimal native_handle_t implementation backed by malloc.
 *
 * The audio decoders never need real fd handling: when our C2LinuxMallocAllocator
 * creates a C2LinearAllocation it synthesises a 0-fd handle whose payload is
 * just the malloc()ed buffer pointer (packed into the `data[]` ints area).
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <cutils/native_handle.h>

#include <cstdlib>
#include <cstring>
#include <unistd.h>

extern "C" {

native_handle_t* native_handle_create(int numFds, int numInts) {
    if (numFds < 0 || numInts < 0 ||
        numFds > NATIVE_HANDLE_MAX_FDS || numInts > NATIVE_HANDLE_MAX_INTS) {
        return nullptr;
    }
    const size_t bytes =
        sizeof(native_handle_t) + (size_t)(numFds + numInts) * sizeof(int);
    auto* h = (native_handle_t*)std::calloc(1, bytes);
    if (!h) return nullptr;
    h->version = (int)sizeof(native_handle_t);
    h->numFds  = numFds;
    h->numInts = numInts;
    for (int i = 0; i < numFds; ++i) h->data[i] = -1;
    return h;
}

native_handle_t* native_handle_clone(const native_handle_t* src) {
    if (!src) return nullptr;
    native_handle_t* dst = native_handle_create(src->numFds, src->numInts);
    if (!dst) return nullptr;
    /* dup the fds, copy the ints verbatim. */
    for (int i = 0; i < src->numFds; ++i) {
        dst->data[i] = src->data[i] >= 0 ? ::dup(src->data[i]) : -1;
    }
    std::memcpy(dst->data + dst->numFds, src->data + src->numFds,
                (size_t)src->numInts * sizeof(int));
    return dst;
}

int native_handle_close(const native_handle_t* h) {
    if (!h) return 0;
    int rc = 0;
    for (int i = 0; i < h->numFds; ++i) {
        if (h->data[i] >= 0) {
            if (::close(h->data[i]) < 0) rc = -1;
        }
    }
    return rc;
}

int native_handle_delete(native_handle_t* h) {
    if (!h) return 0;
    std::free(h);
    return 0;
}

}  /* extern "C" */
