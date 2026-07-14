/*
 * gst-plugins-android — override for AOSP's util/C2Debug-log.h.
 *
 * AOSP ships this file with the non-Android branch left as a "TODO: implement
 * base debug utils" comment, which means C2_LOG / C2_DCHECK are simply
 * undefined when building outside __ANDROID__. We must define them here,
 * matching the ostream-style API used pervasively in C2InterfaceUtils.cpp:
 *
 *     C2_LOG(VERBOSE) << "msg" << x << y;
 *     C2_DCHECK(cond) << "extra info";
 *
 * Implementation strategy:
 *   - A LogStream class derives from std::ostringstream. On destruction it
 *     forwards the buffer to gst_c2_log(prio, tag, "%s", buf).
 *   - When the requested level is below the active threshold the macro
 *     evaluates to a no-op sink that discards everything via a compiler-
 *     optimisable `if (false)` branch.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_UTIL_C2DEBUG_LOG_H_
#define GST_C2_PORTING_UTIL_C2DEBUG_LOG_H_

#include <log/log.h>

#include <cstdlib>
#include <sstream>
#include <string>

namespace gst_c2 {

class LogStream {
 public:
  LogStream(int prio, const char* tag, bool fatal)
      : prio_(prio), tag_(tag ? tag : "c2"), fatal_(fatal) {}
  ~LogStream() {
    /* Always emit if fatal, otherwise honour the runtime level filter. */
    if (fatal_ || gst_c2_log_enabled(prio_)) {
      gst_c2_log(prio_, tag_, "%s", buf_.str().c_str());
    }
    if (fatal_) std::abort();
  }
  template <typename T>
  LogStream& operator<<(const T& v) { buf_ << v; return *this; }

 private:
  int                 prio_;
  const char*         tag_;
  bool                fatal_;
  std::ostringstream  buf_;
};

/* No-op sink for branches we want the compiler to fully fold away. */
class NoopStream {
 public:
  template <typename T>
  NoopStream& operator<<(const T&) { return *this; }
};

/* Voidify trick (glog/abseil): operator& binds tighter than the ternary
 * but looser than <<, letting us write the C2_DCHECK macro as
 *     (cond) ? (void)0 : Voidify{} & (LogStream(...) << ... << ...)
 * Both ternary arms now have type `void`, so no type-mismatch diagnostic. */
class Voidify {
 public:
  void operator&(const LogStream&) const {}
  void operator&(const NoopStream&) const {}
};

}  /* namespace gst_c2 */

/* Map C2_LOG's `level` token to ANDROID_LOG_*. C2_LOG(VERBOSE) etc. */
#define _GST_C2_PRIO_VERBOSE ANDROID_LOG_VERBOSE
#define _GST_C2_PRIO_DEBUG   ANDROID_LOG_DEBUG
#define _GST_C2_PRIO_INFO    ANDROID_LOG_INFO
#define _GST_C2_PRIO_WARNING ANDROID_LOG_WARN
#define _GST_C2_PRIO_WARN    ANDROID_LOG_WARN
#define _GST_C2_PRIO_ERROR   ANDROID_LOG_ERROR
#define _GST_C2_PRIO_FATAL   ANDROID_LOG_FATAL

#define _GST_C2_FATAL_VERBOSE 0
#define _GST_C2_FATAL_DEBUG   0
#define _GST_C2_FATAL_INFO    0
#define _GST_C2_FATAL_WARNING 0
#define _GST_C2_FATAL_WARN    0
#define _GST_C2_FATAL_ERROR   0
#define _GST_C2_FATAL_FATAL   1

#ifndef C2_LOG
#define C2_LOG(level) \
    ::gst_c2::LogStream(_GST_C2_PRIO_##level, "c2", _GST_C2_FATAL_##level)
#endif

/* Debug assert: like assert() but with an ostream tail. Active when
 * NDEBUG is not set; otherwise the entire `<< ...` chain is dead-code so
 * the compiler folds it away. */
#ifndef NDEBUG
#define C2_DCHECK(cond)                                                       \
    ((cond) ? (void)0                                                          \
            : ::gst_c2::Voidify() &                                            \
                (::gst_c2::LogStream(ANDROID_LOG_FATAL, "c2-dcheck", true)     \
                 << "DCHECK(" #cond ") failed: "))
#else
#define C2_DCHECK(cond) ((void)sizeof(cond))
#endif

#define C2_CHECK(cond)                                                        \
    ((cond) ? (void)0                                                          \
            : ::gst_c2::Voidify() &                                            \
                (::gst_c2::LogStream(ANDROID_LOG_FATAL, "c2-check", true)      \
                 << "CHECK(" #cond ") failed: "))

#define C2_DCHECK_EQ(a, b) C2_DCHECK((a) == (b))
#define C2_DCHECK_NE(a, b) C2_DCHECK((a) != (b))
#define C2_DCHECK_LT(a, b) C2_DCHECK((a) <  (b))
#define C2_DCHECK_LE(a, b) C2_DCHECK((a) <= (b))
#define C2_DCHECK_GT(a, b) C2_DCHECK((a) >  (b))
#define C2_DCHECK_GE(a, b) C2_DCHECK((a) >= (b))
#define C2_CHECK_EQ(a, b)  C2_CHECK((a) == (b))
#define C2_CHECK_NE(a, b)  C2_CHECK((a) != (b))
#define C2_CHECK_LT(a, b)  C2_CHECK((a) <  (b))
#define C2_CHECK_LE(a, b)  C2_CHECK((a) <= (b))
#define C2_CHECK_GT(a, b)  C2_CHECK((a) >  (b))
#define C2_CHECK_GE(a, b)  C2_CHECK((a) >= (b))

#endif  /* GST_C2_PORTING_UTIL_C2DEBUG_LOG_H_ */
