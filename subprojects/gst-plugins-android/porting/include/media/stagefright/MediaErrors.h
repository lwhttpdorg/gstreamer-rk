/*
 * gst-plugins-android — <media/stagefright/MediaErrors.h> shim.
 * Minimal subset of AOSP's status_t enums actually referenced by codec2
 * components and the libstagefright/foundation message-pump.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_MEDIA_STAGEFRIGHT_MEDIA_ERRORS_H_
#define GST_C2_PORTING_MEDIA_STAGEFRIGHT_MEDIA_ERRORS_H_

#include <utils/Errors.h>

namespace android {

/* The AOSP enum is sparse — these are just the values reachable from the
 * SW Opus/AAC paths. */
enum {
    MEDIA_ERROR_BASE         = -1000,

    ERROR_ALREADY_CONNECTED  = MEDIA_ERROR_BASE,
    ERROR_NOT_CONNECTED      = MEDIA_ERROR_BASE - 1,
    ERROR_UNKNOWN_HOST       = MEDIA_ERROR_BASE - 2,
    ERROR_CANNOT_CONNECT     = MEDIA_ERROR_BASE - 3,
    ERROR_IO                 = MEDIA_ERROR_BASE - 4,
    ERROR_CONNECTION_LOST    = MEDIA_ERROR_BASE - 5,
    ERROR_MALFORMED          = MEDIA_ERROR_BASE - 7,
    ERROR_OUT_OF_RANGE       = MEDIA_ERROR_BASE - 8,
    ERROR_BUFFER_TOO_SMALL   = MEDIA_ERROR_BASE - 9,
    ERROR_UNSUPPORTED        = MEDIA_ERROR_BASE - 10,
    ERROR_END_OF_STREAM      = MEDIA_ERROR_BASE - 11,

    ERROR_FORMAT_CHANGED     = MEDIA_ERROR_BASE - 12,
    INFO_FORMAT_CHANGED      = ERROR_FORMAT_CHANGED,
    INFO_DISCONTINUITY       = MEDIA_ERROR_BASE - 14,
    INFO_OUTPUT_BUFFERS_CHANGED = MEDIA_ERROR_BASE - 15,
};

}  /* namespace android */

#endif
