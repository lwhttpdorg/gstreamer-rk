/*
 * gst-plugins-android — stub for the HIDL (V2_0) bufferpool ClientManager.
 *
 * C2Buffer.cpp:34 includes it. The audio path never uses a shared buffer pool
 * (it uses C2BasicLinearBlockPool over our malloc allocator), so every entry
 * point safely refuses. We declare just the surface C2Buffer.cpp names.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_HIDL_BUFFERPOOL_CLIENTMANAGER_STUB_H_
#define GST_C2_PORTING_HIDL_BUFFERPOOL_CLIENTMANAGER_STUB_H_

#include <cstdint>
#include <memory>
#include <vector>

#include <utils/RefBase.h>
#include <cutils/native_handle.h>

namespace android {
namespace hardware {
namespace media {
namespace bufferpool {

struct BufferPoolData {};

namespace V2_0 {

enum class ResultStatus : int32_t {
    OK = 0,
    NO_MEMORY,
    CRITICAL_ERROR,
    NOT_FOUND,
    ALREADY_EXISTS,
};

namespace implementation {

using ConnectionId = int64_t;
static constexpr ConnectionId INVALID_CONNECTIONID = -1;

struct BufferPoolAllocation {
    /* C2Buffer.cpp constructs this from C2{Linear,Graphic}Allocation::handle(),
     * which returns const C2Handle* (== const void* on Linux). Accept that. */
    explicit BufferPoolAllocation(const void* h) : mHandle(h) {}
    const void* handle() const { return mHandle; }
    const void* mHandle;
};

struct BufferPoolAllocator : public ::android::RefBase {
    virtual ~BufferPoolAllocator() = default;
    virtual ResultStatus allocate(const std::vector<uint8_t>&,
                                  std::shared_ptr<BufferPoolAllocation>*,
                                  size_t*) = 0;
    virtual bool compatible(const std::vector<uint8_t>&,
                            const std::vector<uint8_t>&) = 0;
};

struct ClientManager : public ::android::RefBase {
    static ::android::sp<ClientManager> getInstance() { return nullptr; }
    ResultStatus create(const std::shared_ptr<BufferPoolAllocator>&, ConnectionId*) {
        return ResultStatus::CRITICAL_ERROR;
    }
    ResultStatus close(ConnectionId) { return ResultStatus::OK; }
    ResultStatus allocate(ConnectionId, const std::vector<uint8_t>&,
                          native_handle_t**, std::shared_ptr<BufferPoolData>*) {
        return ResultStatus::CRITICAL_ERROR;
    }
};

}  // namespace implementation
}  // namespace V2_0
}  // namespace bufferpool
}  // namespace media
}  // namespace hardware
}  // namespace android

#endif
