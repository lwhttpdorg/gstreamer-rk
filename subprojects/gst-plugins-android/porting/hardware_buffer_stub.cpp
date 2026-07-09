/*
 * gst-plugins-android — AHardwareBuffer_* link-symbol stubs.
 *
 * The audio decoders never call into these — they exist purely so transitively
 * #included graphic paths in SimpleC2Component link cleanly. Any actual call
 * here is a programming error and returns an error code.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <android/hardware_buffer.h>

#include <cstring>

extern "C" {

int AHardwareBuffer_allocate(const AHardwareBuffer_Desc* /*d*/, AHardwareBuffer** out) {
    if (out) *out = nullptr;
    return -1;  /* errno-style: not supported */
}

void AHardwareBuffer_acquire(AHardwareBuffer* /*b*/) {}
void AHardwareBuffer_release(AHardwareBuffer* /*b*/) {}

int AHardwareBuffer_lock(AHardwareBuffer* /*b*/, uint64_t /*usage*/, int32_t /*fence*/,
                          const void* /*rect*/, void** outVAddr) {
    if (outVAddr) *outVAddr = nullptr;
    return -1;
}
int AHardwareBuffer_unlock(AHardwareBuffer* /*b*/, int32_t* fence) {
    if (fence) *fence = -1;
    return -1;
}
void AHardwareBuffer_describe(const AHardwareBuffer* /*b*/, AHardwareBuffer_Desc* desc) {
    if (desc) std::memset(desc, 0, sizeof(*desc));
}

}  /* extern "C" */
