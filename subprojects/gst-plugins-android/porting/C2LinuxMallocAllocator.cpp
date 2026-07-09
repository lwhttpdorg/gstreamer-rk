/*
 * gst-plugins-android — malloc-backed C2 linear allocator for Linux hosts.
 *
 * The AOSP C2Buffer.h public contract requires:
 *   - C2Allocator with id() / getName() / newLinearAllocation()
 *   - C2LinearAllocation deriving from C2LinearCapacity + C2LinearAllocation,
 *     exposing map() to obtain a raw pointer + capacity.
 *
 * Our implementation backs all storage with aligned_alloc() and synthesises
 * a 0-fd native_handle_t purely to keep the existing pool plumbing happy.
 * The audio decoders consume linear input/output exclusively, so this is
 * sufficient for c2.android.opus.decoder and c2.android.aac.decoder.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "C2LinuxMallocAllocator.h"

#include <C2Buffer.h>
#include <C2PlatformSupport.h>
#include <cutils/native_handle.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>

#define LOG_TAG "c2.linux.alloc"
#include <log/log.h>

namespace {

constexpr size_t kAlign = 64;
constexpr C2Allocator::id_t kAllocatorId =
    static_cast<C2Allocator::id_t>(android::C2PlatformAllocatorStore::ION);

class LinuxLinearAllocation : public C2LinearAllocation {
 public:
    LinuxLinearAllocation(size_t capacity, std::shared_ptr<void> backing)
        : C2LinearAllocation(capacity),
          mCapacity(capacity),
          mBacking(std::move(backing)) {
        mHandle = native_handle_create(0, 0);
    }

    ~LinuxLinearAllocation() override {
        if (mHandle) {
            native_handle_close(mHandle);
            native_handle_delete(mHandle);
        }
    }

    c2_status_t map(size_t offset, size_t size, C2MemoryUsage /*usage*/,
                    C2Fence* fence, void** addr) override {
        if (!addr) return C2_BAD_VALUE;
        if (offset + size > mCapacity) return C2_BAD_VALUE;
        if (fence) *fence = C2Fence();
        *addr = static_cast<uint8_t*>(mBacking.get()) + offset;
        return C2_OK;
    }

    c2_status_t unmap(void* /*addr*/, size_t /*size*/, C2Fence* /*fence*/) override {
        /* No-op: we never copy data out of the backing buffer. */
        return C2_OK;
    }

    C2Allocator::id_t getAllocatorId() const override { return kAllocatorId; }

    const C2Handle* handle() const override { return reinterpret_cast<C2Handle*>(mHandle); }

    bool equals(const std::shared_ptr<C2LinearAllocation>& other) const override {
        if (!other) return false;
        return other.get() == this;
    }

 private:
    size_t                   mCapacity;
    std::shared_ptr<void>    mBacking;     /* aligned_alloc()'d, freed by deleter */
    native_handle_t*         mHandle = nullptr;
};

class LinuxLinearAllocator : public C2Allocator {
 public:
    LinuxLinearAllocator() {
        mTraits = std::make_shared<C2Allocator::Traits>(C2Allocator::Traits{
            "android.allocator.gst-c2-linux-malloc",
            kAllocatorId,
            C2Allocator::LINEAR,
            /* minimumUsage */  {0, 0},
            /* maximumUsage */  {0, ~uint64_t(0)},
        });
    }

    id_t getId() const override { return mTraits->id; }
    C2String getName() const override { return mTraits->name; }

    std::shared_ptr<const Traits> getTraits() const override { return mTraits; }

    c2_status_t newLinearAllocation(uint32_t capacity, C2MemoryUsage /*usage*/,
                                     std::shared_ptr<C2LinearAllocation>* allocation) override {
        if (!allocation) return C2_BAD_VALUE;
        const size_t aligned = (capacity + kAlign - 1) & ~(kAlign - 1);
        void* p = std::aligned_alloc(kAlign, aligned ? aligned : kAlign);
        if (!p) return C2_NO_MEMORY;
        std::shared_ptr<void> backing(p, [](void* x){ std::free(x); });
        *allocation = std::make_shared<LinuxLinearAllocation>(capacity, std::move(backing));
        return C2_OK;
    }

    c2_status_t priorLinearAllocation(const C2Handle* /*handle*/,
                                       std::shared_ptr<C2LinearAllocation>* allocation) override {
        /* We don't share allocations across processes; refuse rebinds. */
        if (allocation) allocation->reset();
        return C2_OMITTED;
    }

    bool checkHandle(const C2Handle* /*handle*/) const override { return true; }

 private:
    std::shared_ptr<C2Allocator::Traits> mTraits;
};

std::shared_ptr<C2Allocator> g_allocator;
std::once_flag               g_once;

}  /* namespace */

namespace gst_c2 {

std::shared_ptr<C2Allocator> GetLinuxLinearAllocator() {
    std::call_once(g_once, []{ g_allocator = std::make_shared<LinuxLinearAllocator>(); });
    return g_allocator;
}

}  /* namespace gst_c2 */
