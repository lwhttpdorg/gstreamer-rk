/*
 * gst-plugins-android — gst_c2_log() implementation.
 *
 * Reads GST_C2_LOG_LEVEL once (on first call) and prints to stderr.
 * Format: "[c2:<tag>][<L>] <msg>\n", where <L> is V/D/I/W/E/F.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include <log/log.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>   /* strcasecmp() is POSIX, not in std:: */
#include <mutex>

namespace {

/* Default baked in at compile time via -DGST_C2_DEFAULT_LOG_LEVEL=... ; the
 * meson option lets the packager pick it. Runtime env var still wins. */
#ifndef GST_C2_DEFAULT_LOG_LEVEL
#define GST_C2_DEFAULT_LOG_LEVEL ANDROID_LOG_WARN
#endif

std::atomic<int> g_level{-1};

int _level_from_env() {
    const char* v = std::getenv("GST_C2_LOG_LEVEL");
    if (!v) return GST_C2_DEFAULT_LOG_LEVEL;
    if (strcasecmp(v, "off")     == 0) return ANDROID_LOG_SILENT;
    if (strcasecmp(v, "error")   == 0) return ANDROID_LOG_ERROR;
    if (strcasecmp(v, "warning") == 0) return ANDROID_LOG_WARN;
    if (strcasecmp(v, "info")    == 0) return ANDROID_LOG_INFO;
    if (strcasecmp(v, "debug")   == 0) return ANDROID_LOG_DEBUG;
    if (strcasecmp(v, "verbose") == 0) return ANDROID_LOG_VERBOSE;
    return GST_C2_DEFAULT_LOG_LEVEL;
}

int _current_level() {
    int lvl = g_level.load(std::memory_order_relaxed);
    if (lvl < 0) {
        lvl = _level_from_env();
        g_level.store(lvl, std::memory_order_relaxed);
    }
    return lvl;
}

char _level_char(int prio) {
    switch (prio) {
        case ANDROID_LOG_VERBOSE: return 'V';
        case ANDROID_LOG_DEBUG:   return 'D';
        case ANDROID_LOG_INFO:    return 'I';
        case ANDROID_LOG_WARN:    return 'W';
        case ANDROID_LOG_ERROR:   return 'E';
        case ANDROID_LOG_FATAL:   return 'F';
        default:                  return '?';
    }
}

std::mutex g_io_mutex;

}  // namespace

extern "C" int gst_c2_log_enabled(int priority) {
    return priority >= _current_level();
}

extern "C" void gst_c2_log(int priority, const char* tag, const char* fmt, ...) {
    if (priority < _current_level()) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> g(g_io_mutex);
    std::fprintf(stderr, "[c2:%s][%c] %s\n", tag ? tag : "?", _level_char(priority), buf);
}
