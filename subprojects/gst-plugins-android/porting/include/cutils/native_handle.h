/*
 * gst-plugins-android — <cutils/native_handle.h> shim.
 *
 * A native_handle_t is a flexible-array carrying a variable number of file
 * descriptors plus a tail of ints. The codec2 linear-allocator path on
 * Android uses it to hand an ion fd back; on our Linux port we synthesise
 * handles with 0 fds because allocator memory is plain malloc()ed.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_CUTILS_NATIVE_HANDLE_H_
#define GST_C2_PORTING_CUTILS_NATIVE_HANDLE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_HANDLE_MAX_FDS  1024
#define NATIVE_HANDLE_MAX_INTS 1024

typedef struct native_handle {
    int version;   /* sizeof(native_handle_t) */
    int numFds;
    int numInts;
    int data[0];   /* fds followed by ints */
} native_handle_t;

native_handle_t* native_handle_create(int numFds, int numInts);
native_handle_t* native_handle_clone(const native_handle_t* h);
int              native_handle_delete(native_handle_t* h);
int              native_handle_close(const native_handle_t* h);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif
