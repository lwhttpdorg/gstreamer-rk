/*
 * Copyright (C) 2025 ekwange <ekwange@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF

 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN

 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#undef TRACEPOINT_PROVIDER
#define TRACEPOINT_PROVIDER gst_lttng

#undef TRACEPOINT_INCLUDE
#define TRACEPOINT_INCLUDE "./gst-lttng.h"

#if !defined(GST_LTTNG_H) || defined(TRACEPOINT_HEADER_MULTI_READ)
#define GST_LTTNG_H

#include <glib.h>
#include <sys/types.h>
#include <lttng/tracepoint.h>

TRACEPOINT_EVENT(
    gst_lttng,
    gst_log,
    TP_ARGS(
        pid_t, pid,
        gconstpointer, object_ptr,
        char*, category,
        int, level,
        char*, file,
        char*, function,
        int, line,
        char*, object,
        char*, message
    ),
    TP_FIELDS(
        ctf_integer(pid_t, pid, pid)
        ctf_integer_hex(gconstpointer, object_ptr, object_ptr)
        ctf_string(category, category)
        ctf_integer(int, level, level)
        ctf_string(file, file)
        ctf_string(function, function)
        ctf_integer(int, line, line)
        ctf_string(object, object)
        ctf_string(message, message)
    )
)

#endif /* GST_LTTNG_H */

#include <lttng/tracepoint-event.h>
