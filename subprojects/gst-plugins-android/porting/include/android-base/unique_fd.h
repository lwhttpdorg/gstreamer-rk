/*
 * gst-plugins-android — <android-base/unique_fd.h> shim.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_ANDROID_BASE_UNIQUE_FD_H_
#define GST_C2_PORTING_ANDROID_BASE_UNIQUE_FD_H_

#include <unistd.h>

namespace android {
namespace base {

class unique_fd {
 public:
    unique_fd() noexcept : fd_(-1) {}
    explicit unique_fd(int fd) noexcept : fd_(fd) {}
    unique_fd(unique_fd&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    ~unique_fd() { reset(); }

    unique_fd& operator=(unique_fd&& o) noexcept {
        if (this != &o) { reset(o.fd_); o.fd_ = -1; }
        return *this;
    }

    int get()  const noexcept { return fd_; }
    int operator*() const noexcept { return fd_; }
    bool ok()  const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return fd_ >= 0; }

    int release() noexcept { int t = fd_; fd_ = -1; return t; }
    void reset(int fd = -1) noexcept {
        if (fd_ >= 0 && fd_ != fd) ::close(fd_);
        fd_ = fd;
    }

 private:
    int fd_;
    unique_fd(const unique_fd&) = delete;
    unique_fd& operator=(const unique_fd&) = delete;
};

}  /* namespace base */
}  /* namespace android */

#endif
