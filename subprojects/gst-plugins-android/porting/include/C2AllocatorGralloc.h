/*
 * gst-plugins-android — concrete stub for C2AllocatorGralloc.h.
 *
 * C2Buffer.cpp:1560/1581 actually instantiate `C2AllocatorGralloc(0)` and call
 * isValid()/priorGraphicAllocation() (a `using android::C2AllocatorGralloc;` is
 * at C2Buffer.cpp:41), so a 2-free-function stub is not enough — we need a
 * concrete class overriding the C2Allocator pure-virtuals. The audio path never
 * allocates a graphic buffer, so every operation safely refuses.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef GST_C2_PORTING_C2_ALLOCATOR_GRALLOC_STUB_H_
#define GST_C2_PORTING_C2_ALLOCATOR_GRALLOC_STUB_H_

#include <C2Buffer.h>
#include <cstdint>
#include <memory>

namespace android {

class C2AllocatorGralloc : public C2Allocator {
 public:
    explicit C2AllocatorGralloc(id_t id) : mId(id) {}
    ~C2AllocatorGralloc() override = default;

    C2String getName() const override { return "android.allocator.gralloc.stub"; }
    id_t     getId()   const override { return mId; }
    std::shared_ptr<const Traits> getTraits() const override { return nullptr; }
    bool checkHandle(const C2Handle* const o) const override { return CheckHandle(o); }

    c2_status_t priorGraphicAllocation(
            const C2Handle* /*handle*/,
            std::shared_ptr<C2GraphicAllocation>* allocation) override {
        if (allocation) *allocation = nullptr;
        return C2_OMITTED;
    }

    bool isValid(const C2Handle* const o) const { return CheckHandle(o); }
    static bool CheckHandle(const C2Handle* const /*o*/) { return false; }

 private:
    id_t mId;
};

}  /* namespace android */

/* The two free functions C2Buffer.cpp also references. Weak so any actual
 * graphic-path call links but is obviously a no-op. */
extern "C" {
__attribute__((weak)) uint32_t ExtractFormatFromUsage(uint64_t usage);
__attribute__((weak)) uint64_t ExtractGenerationFromUsage(uint64_t usage);
}

#endif
