/*
 * gst-plugins-android — stub for the AIDL (bufferpool2) ClientManager.
 *
 * C2Buffer.cpp:35 includes it. Mirrors the HIDL stub but in the AIDL namespace
 * aidl::android::hardware::media::bufferpool2, and getInstance() returns a
 * std::shared_ptr (the C2Buffer.cpp AIDL Impl holds it as shared_ptr, unlike
 * the HIDL one which uses sp<>).
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_AIDL_BUFFERPOOL2_CLIENTMANAGER_STUB_H_
#define GST_C2_PORTING_AIDL_BUFFERPOOL2_CLIENTMANAGER_STUB_H_

#include <cstdint>
#include <memory>
#include <vector>

#include <cutils/native_handle.h>

namespace aidl {
namespace android {
namespace hardware {
namespace media {
namespace bufferpool2 {

struct BufferPoolData {};

enum class ResultStatus : int32_t {
    OK = 0,
    NO_MEMORY,
    CRITICAL_ERROR,
    NOT_FOUND,
    ALREADY_EXISTS,
};

namespace implementation {

using ConnectionId = int64_t;
using BufferPoolStatus = ResultStatus;
static constexpr ConnectionId INVALID_CONNECTIONID = -1;

struct BufferPoolAllocation {
    /* See HIDL stub: handle() is const C2Handle* == const void* on Linux. */
    explicit BufferPoolAllocation(const void* h) : mHandle(h) {}
    const void* handle() const { return mHandle; }
    const void* mHandle;
};

struct BufferPoolAllocator {
    virtual ~BufferPoolAllocator() = default;
    virtual BufferPoolStatus allocate(const std::vector<uint8_t>&,
                                      std::shared_ptr<BufferPoolAllocation>*,
                                      size_t*) = 0;
    virtual bool compatible(const std::vector<uint8_t>&,
                            const std::vector<uint8_t>&) = 0;
};

struct ClientManager {
    static std::shared_ptr<ClientManager> getInstance() { return nullptr; }
    BufferPoolStatus create(const std::shared_ptr<BufferPoolAllocator>&, ConnectionId*) {
        return ResultStatus::CRITICAL_ERROR;
    }
    BufferPoolStatus close(ConnectionId) { return ResultStatus::OK; }
    BufferPoolStatus allocate(ConnectionId, const std::vector<uint8_t>&,
                              native_handle_t**, std::shared_ptr<BufferPoolData>*) {
        return ResultStatus::CRITICAL_ERROR;
    }
};

}  // namespace implementation
}  // namespace bufferpool2
}  // namespace media
}  // namespace hardware
}  // namespace android
}  // namespace aidl

#endif
