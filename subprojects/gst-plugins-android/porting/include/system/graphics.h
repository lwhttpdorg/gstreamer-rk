/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * gst-plugins-android — <system/graphics.h> minimal stub.
 * Provides the HAL_PIXEL_FORMAT_* enum values referenced by SimpleC2Component
 * graphic branches (we #ifdef them out at the call site, but the enum tokens
 * must still be defined to satisfy the lexer/parser).
 */
#ifndef GST_C2_PORTING_SYSTEM_GRAPHICS_H_
#define GST_C2_PORTING_SYSTEM_GRAPHICS_H_

enum {
    HAL_PIXEL_FORMAT_YCBCR_420_888 = 0x23,
    HAL_PIXEL_FORMAT_YV12          = 0x32315659,
    HAL_PIXEL_FORMAT_RGBA_8888     = 0x1,
    HAL_PIXEL_FORMAT_RGB_888       = 0x3,
    HAL_PIXEL_FORMAT_BLOB          = 0x21,
    HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED = 0x22,
    HAL_PIXEL_FORMAT_YCBCR_P010    = 0x36,
    HAL_PIXEL_FORMAT_RGBA_1010102  = 0x2b,
    HAL_PIXEL_FORMAT_RGBA_FP16     = 0x16,
};

enum android_dataspace {
    HAL_DATASPACE_UNKNOWN = 0,
};

#endif
