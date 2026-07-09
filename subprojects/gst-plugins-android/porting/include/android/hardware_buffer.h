/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * gst-plugins-android — <android/hardware_buffer.h> opaque stub.
 * Audio path never calls AHardwareBuffer_*; the symbols are provided by
 * porting/hardware_buffer_stub.cpp so transitively-#included graphic paths
 * still link.
 */
#ifndef GST_C2_PORTING_ANDROID_HARDWARE_BUFFER_H_
#define GST_C2_PORTING_ANDROID_HARDWARE_BUFFER_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AHardwareBuffer AHardwareBuffer;

/* Subset of the NDK AHardwareBuffer_Format enum. Only referenced by graphic
 * paths we never hit; present so Codec2CommonUtils.h's signature compiles. */
typedef enum AHardwareBuffer_Format {
    AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM = 1,
    AHARDWAREBUFFER_FORMAT_Y8Cb8Cr8_420   = 0x23,
    AHARDWAREBUFFER_FORMAT_BLOB           = 0x21,
} AHardwareBuffer_Format;

typedef struct AHardwareBuffer_Desc {
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t format;
    uint64_t usage;
    uint32_t stride;
    uint32_t rfu0;
    uint64_t rfu1;
} AHardwareBuffer_Desc;

/* All implementations live in porting/hardware_buffer_stub.cpp and return
 * an error / NULL. */
int  AHardwareBuffer_allocate(const AHardwareBuffer_Desc* desc, AHardwareBuffer** out);
void AHardwareBuffer_acquire (AHardwareBuffer* buf);
void AHardwareBuffer_release (AHardwareBuffer* buf);
int  AHardwareBuffer_lock    (AHardwareBuffer* buf, uint64_t usage, int32_t fence,
                              const void* rect, void** outVAddr);
int  AHardwareBuffer_unlock  (AHardwareBuffer* buf, int32_t* fence);
void AHardwareBuffer_describe(const AHardwareBuffer* buf, AHardwareBuffer_Desc* desc);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif
