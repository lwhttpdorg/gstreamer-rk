/*
 * gst-plugins-android — <utils/Errors.h> shim.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTILS_ERRORS_H_
#define GST_C2_PORTING_UTILS_ERRORS_H_

#include <errno.h>
#include <stdint.h>

namespace android {

typedef int32_t status_t;

/* These constants match the AOSP libutils <utils/Errors.h> contract. The
 * AOSP enum uses INT32_MIN as a base for non-errno-derived values; we follow
 * the same scheme so ADebug.h's switch-case-asString table compiles without
 * duplicate case values (which it gets when several constants collapse onto
 * the same number, e.g. PERMISSION_DENIED == FDS_NOT_ALLOWED == -1 if we
 * naively use -EPERM).
 */
enum {
    OK                  = 0,
    NO_ERROR            = 0,
    UNKNOWN_ERROR       = (-2147483647 - 1),  /* INT32_MIN, AOSP convention */

    NO_MEMORY           = UNKNOWN_ERROR + 1,
    INVALID_OPERATION   = UNKNOWN_ERROR + 2,
    BAD_VALUE           = UNKNOWN_ERROR + 3,
    BAD_TYPE            = UNKNOWN_ERROR + 4,
    NAME_NOT_FOUND      = UNKNOWN_ERROR + 5,
    PERMISSION_DENIED   = UNKNOWN_ERROR + 6,
    NO_INIT             = UNKNOWN_ERROR + 7,
    ALREADY_EXISTS      = UNKNOWN_ERROR + 8,
    DEAD_OBJECT         = UNKNOWN_ERROR + 9,
    FAILED_TRANSACTION  = UNKNOWN_ERROR + 10,
    BAD_INDEX           = UNKNOWN_ERROR + 11,
    NOT_ENOUGH_DATA     = UNKNOWN_ERROR + 12,
    WOULD_BLOCK         = UNKNOWN_ERROR + 13,
    TIMED_OUT           = UNKNOWN_ERROR + 14,
    UNKNOWN_TRANSACTION = UNKNOWN_ERROR + 15,
    FDS_NOT_ALLOWED     = UNKNOWN_ERROR + 16,
    UNEXPECTED_NULL     = UNKNOWN_ERROR + 17,
};

}  /* namespace android */

#endif  /* GST_C2_PORTING_UTILS_ERRORS_H_ */
