/*
 * gst-plugins-android — <libyuv.h> stub.
 *
 * libyuv is the YUV/RGB colour-conversion library AOSP's SimpleC2Component
 * uses for VIDEO output. The audio decoders never reach these code paths, but
 * the calls are compiled unconditionally, so we provide declarations (and
 * definitions in libyuv_stub.cpp) that satisfy the compiler+linker while
 * returning an error if ever actually invoked.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_LIBYUV_STUB_H_
#define GST_C2_PORTING_LIBYUV_STUB_H_

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus

namespace libyuv {

/* Conversion helpers referenced by SimpleC2Component.cpp. Declared variadic so
 * every call site (which passes many stride/plane args) type-checks; the
 * definitions in libyuv_stub.cpp ignore the args and return -1. */
int I420ToI420(...);
int I422ToI420(...);
int I444ToI420(...);
int I010ToI420(...);
int I010ToP010(...);
int I210ToI420(...);
int I210ToI010(...);
int I210ToP010(...);
int I210ToAB30Matrix(...);
int I410ToI420(...);
int I410ToI010(...);
int I410ToP010(...);
int I410ToAB30Matrix(...);

/* Matrix constant passed by address to the *Matrix() helpers. */
struct YuvConstants;
extern const struct YuvConstants kYuvV2020Constants;

}  /* namespace libyuv */

#endif  /* __cplusplus */

#endif
