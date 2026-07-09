/*
 * gst-plugins-android — <system/thread_defs.h> shim.
 *
 * Provides the ANDROID_PRIORITY_* constants (SimpleC2Component passes
 * ANDROID_PRIORITY_VIDEO to ALooper::start). The values mirror AOSP's nice
 * values; on a generic Linux host they are advisory only.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_SYSTEM_THREAD_DEFS_H_
#define GST_C2_PORTING_SYSTEM_THREAD_DEFS_H_

enum {
    ANDROID_PRIORITY_LOWEST         =  19,
    ANDROID_PRIORITY_BACKGROUND     =  10,
    ANDROID_PRIORITY_NORMAL         =   0,
    ANDROID_PRIORITY_FOREGROUND     =  -2,
    ANDROID_PRIORITY_DISPLAY        =  -4,
    ANDROID_PRIORITY_URGENT_DISPLAY =  -8,
    ANDROID_PRIORITY_VIDEO          = -10,
    ANDROID_PRIORITY_AUDIO          = -16,
    ANDROID_PRIORITY_URGENT_AUDIO   = -19,
    ANDROID_PRIORITY_HIGHEST        = -20,
    ANDROID_PRIORITY_DEFAULT        =   0,
    ANDROID_PRIORITY_MORE_FAVORABLE =  -1,
    ANDROID_PRIORITY_LESS_FAVORABLE =  +1,
};

#endif
