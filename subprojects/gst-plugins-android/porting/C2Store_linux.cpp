/*
 * gst-plugins-android — Linux C2 platform support.
 *
 * Implements:
 *   - LinuxAllocatorStore   -> singleton returning our malloc allocator
 *   - LinuxComponentStore   -> singleton creating Opus / AAC SoftDec components
 *   - android::GetCodec2PlatformAllocatorStore()
 *   - android::GetCodec2PlatformComponentStore()
 *     (the link-time entry points declared, in namespace android, by the public
 *     AOSP header <C2PlatformSupport.h>).
 *
 * NOTE on namespaces: <C2PlatformSupport.h> declares the two Get*Store()
 * functions and class C2PlatformAllocatorStore inside `namespace android`.
 * The store/allocator base classes (C2ComponentStore, C2AllocatorStore) and
 * C2ReflectorHelper live in the GLOBAL namespace (see C2Component.h and
 * util/C2InterfaceHelper.h respectively). To make our definitions match the
 * header's declarations we therefore define the Get*Store() functions inside
 * `namespace android`, and pull C2ReflectorHelper in with the explicit
 * <util/C2InterfaceHelper.h> include below — <C2PlatformSupport.h> does NOT
 * transitively provide it.
 *
 * The per-component factories follow AOSP convention:
 *   extern "C" ::C2ComponentFactory* CreateCodec2Factory();
 *   extern "C" void DestroyCodec2Factory(::C2ComponentFactory*);
 * but because we link both components into one binary we rename them per
 * component (see aosp/meson.build) and forward-declare the renamed symbols.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "C2Store_linux.h"
#include "C2LinuxMallocAllocator.h"

#include <C2Component.h>          // C2Component, C2ComponentStore, C2AllocatorStore, Traits
#include <C2ComponentFactory.h>   // C2ComponentFactory
#include <C2BufferPriv.h>         // C2BasicLinearBlockPool
#include <C2PlatformSupport.h>    // android::GetCodec2Platform*Store, android::C2PlatformAllocatorStore
#include <util/C2InterfaceHelper.h> // C2ReflectorHelper (global namespace)

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#define LOG_TAG "c2.linux.store"
#include <log/log.h>
#include <cstdio>
#define GST_C2_STRACE(...) do{ if(getenv("GST_C2_TRACE")){fprintf(stderr,"[STORE] " __VA_ARGS__);fprintf(stderr,"\n");fflush(stderr);} }while(0)

/* Forward-declare the per-component factories. They are defined in
 * aosp/components/{opus,aac}/C2Soft*Dec.cpp; the build renames the canonical
 * "CreateCodec2Factory"/"DestroyCodec2Factory" exports per component so both
 * can co-exist in one binary. */
extern "C" ::C2ComponentFactory* CreateOpusDecoderFactory();
extern "C" void                  DestroyOpusDecoderFactory(::C2ComponentFactory*);
extern "C" ::C2ComponentFactory* CreateAacDecoderFactory();
extern "C" void                  DestroyAacDecoderFactory(::C2ComponentFactory*);
extern "C" ::C2ComponentFactory* CreateFlacDecoderFactory();
extern "C" void                  DestroyFlacDecoderFactory(::C2ComponentFactory*);
extern "C" ::C2ComponentFactory* CreateVorbisDecoderFactory();
extern "C" void                  DestroyVorbisDecoderFactory(::C2ComponentFactory*);
extern "C" ::C2ComponentFactory* CreateMp3DecoderFactory();
extern "C" void                  DestroyMp3DecoderFactory(::C2ComponentFactory*);

namespace {

/* ---------------------------------------------------------------------------
 * Allocator store.
 *
 * C2AllocatorStore is declared in the GLOBAL namespace (C2Component.h); its
 * pure virtuals are: getName(), listAllocators_nb() const, fetchAllocator().
 * The platform allocator IDs (ION, GRALLOC, BLOB, DMABUFHEAP, ...) live in
 * android::C2PlatformAllocatorStore (C2PlatformSupport.h).
 * ------------------------------------------------------------------------- */
class LinuxAllocatorStore : public C2AllocatorStore {
 public:
    LinuxAllocatorStore() = default;

    C2String getName() const override { return "gst-c2-linux-allocator-store"; }

    std::vector<std::shared_ptr<const C2Allocator::Traits>>
    listAllocators_nb() const override {
        auto a = gst_c2::GetLinuxLinearAllocator();
        return { a->getTraits() };
    }

    c2_status_t fetchAllocator(id_t id,
                               std::shared_ptr<C2Allocator>* const allocator) override {
        if (!allocator) return C2_BAD_VALUE;
        if (id == ::android::C2PlatformAllocatorStore::ION ||
            id == C2AllocatorStore::DEFAULT_LINEAR) {
            *allocator = gst_c2::GetLinuxLinearAllocator();
            return C2_OK;
        }
        return C2_NOT_FOUND;
    }
};

/* ---------------------------------------------------------------------------
 * Component store.
 *
 * C2ComponentStore is declared in the GLOBAL namespace (C2Component.h). The
 * full pure-virtual vtable we MUST override is:
 *   C2String getName() const
 *   c2_status_t createComponent(C2String, std::shared_ptr<C2Component>* const)
 *   c2_status_t createInterface(C2String, std::shared_ptr<C2ComponentInterface>* const)
 *   std::vector<std::shared_ptr<const C2Component::Traits>> listComponents()
 *   c2_status_t copyBuffer(std::shared_ptr<C2GraphicBuffer>, std::shared_ptr<C2GraphicBuffer>)
 *   c2_status_t query_sm(const std::vector<C2Param*>&,
 *                        const std::vector<C2Param::Index>&,
 *                        std::vector<std::unique_ptr<C2Param>>* const) const
 *   c2_status_t config_sm(const std::vector<C2Param*>&,
 *                         std::vector<std::unique_ptr<C2SettingResult>>* const)
 *   std::shared_ptr<C2ParamReflector> getParamReflector() const
 *   c2_status_t querySupportedParams_nb(
 *                   std::vector<std::shared_ptr<C2ParamDescriptor>>* const) const
 *   c2_status_t querySupportedValues_sm(std::vector<C2FieldSupportedValuesQuery>&) const
 *
 * The component factory ABI (C2ComponentFactory.h) is:
 *   c2_status_t createComponent(c2_node_id_t,
 *                               std::shared_ptr<C2Component>* const,
 *                               ComponentDeleter = std::default_delete<C2Component>())
 *   c2_status_t createInterface(c2_node_id_t,
 *                               std::shared_ptr<C2ComponentInterface>* const,
 *                               InterfaceDeleter = std::default_delete<C2ComponentInterface>())
 * with ComponentDeleter = std::function<void(::C2Component*)> and
 *      InterfaceDeleter = std::function<void(::C2ComponentInterface*)>.
 * Like the real C2Store.cpp we just call factory->createComponent(id, out).
 * ------------------------------------------------------------------------- */
class LinuxComponentStore : public C2ComponentStore {
 public:
    LinuxComponentStore()
        : mReflector(std::make_shared<C2ReflectorHelper>()) {
        // NB: do NOT create the factories here. The AOSP C2SoftXxxFactory
        // constructors call GetCodec2PlatformComponentStore()->getParamReflector(),
        // which would re-enter this singleton's init mutex while we are still
        // inside it -> recursive self-deadlock. We record only the create/
        // destroy function pointers + traits, and instantiate each factory
        // lazily on first use (by which point the singleton is fully built and
        // cached, so the re-entrant Get...Store() returns immediately).
        registerComponent("c2.android.opus.decoder", CreateOpusDecoderFactory,
                           DestroyOpusDecoderFactory, C2Component::DOMAIN_AUDIO,
                           C2Component::KIND_DECODER, "audio/opus");
        registerComponent("c2.android.aac.decoder",  CreateAacDecoderFactory,
                           DestroyAacDecoderFactory,  C2Component::DOMAIN_AUDIO,
                           C2Component::KIND_DECODER, "audio/mp4a-latm");
        registerComponent("c2.android.flac.decoder", CreateFlacDecoderFactory,
                           DestroyFlacDecoderFactory, C2Component::DOMAIN_AUDIO,
                           C2Component::KIND_DECODER, "audio/flac");
        registerComponent("c2.android.vorbis.decoder", CreateVorbisDecoderFactory,
                           DestroyVorbisDecoderFactory, C2Component::DOMAIN_AUDIO,
                           C2Component::KIND_DECODER, "audio/vorbis");
        registerComponent("c2.android.mp3.decoder",    CreateMp3DecoderFactory,
                           DestroyMp3DecoderFactory,    C2Component::DOMAIN_AUDIO,
                           C2Component::KIND_DECODER, "audio/mpeg");
    }

    ~LinuxComponentStore() override {
        for (auto& e : mEntries) {
            if (e.destroyFactory && e.factory) e.destroyFactory(e.factory);
        }
    }

    C2String getName() const override { return "gst-c2-linux-component-store"; }

    c2_status_t createComponent(
            C2String name, std::shared_ptr<C2Component>* const component) override {
        if (!component) return C2_BAD_VALUE;
        component->reset();
        GST_C2_STRACE("store.createComponent(%s)", name.c_str());
        Entry* e = find(name);
        if (!e) return C2_NOT_FOUND;
        ::C2ComponentFactory* factory = ensureFactory(e);
        if (!factory) return C2_NOT_FOUND;
        GST_C2_STRACE("store.createComponent: calling factory->createComponent");
        return factory->createComponent(
            mNextId.fetch_add(1, std::memory_order_relaxed),
            component, [](C2Component* c){ delete c; });
    }

    c2_status_t createInterface(
            C2String name, std::shared_ptr<C2ComponentInterface>* const interface) override {
        if (!interface) return C2_BAD_VALUE;
        interface->reset();
        Entry* e = find(name);
        if (!e) return C2_NOT_FOUND;
        ::C2ComponentFactory* factory = ensureFactory(e);
        if (!factory) return C2_NOT_FOUND;
        return factory->createInterface(
            mNextId.fetch_add(1, std::memory_order_relaxed),
            interface, [](C2ComponentInterface* i){ delete i; });
    }

    std::vector<std::shared_ptr<const C2Component::Traits>>
    listComponents() override {
        std::vector<std::shared_ptr<const C2Component::Traits>> out;
        for (auto& e : mEntries) out.push_back(e.traits);
        return out;
    }

    c2_status_t copyBuffer(std::shared_ptr<C2GraphicBuffer> /*src*/,
                           std::shared_ptr<C2GraphicBuffer> /*dst*/) override {
        return C2_OMITTED;  /* graphic buffers unsupported in this port */
    }

    /* No system-wide settings in this port. Signatures MUST match the pure
     * virtuals exactly (including the heapParamIndices arg on query_sm) or the
     * class stays abstract. */
    c2_status_t query_sm(
            const std::vector<C2Param*>& /*stackParams*/,
            const std::vector<C2Param::Index>& /*heapParamIndices*/,
            std::vector<std::unique_ptr<C2Param>>* const /*heapParams*/) const override {
        return C2_OK;
    }
    c2_status_t config_sm(
            const std::vector<C2Param*>& /*params*/,
            std::vector<std::unique_ptr<C2SettingResult>>* const /*failures*/) override {
        return C2_OK;
    }
    std::shared_ptr<C2ParamReflector> getParamReflector() const override {
        return mReflector;
    }
    c2_status_t querySupportedParams_nb(
            std::vector<std::shared_ptr<C2ParamDescriptor>>* const /*params*/) const override {
        return C2_OK;
    }
    c2_status_t querySupportedValues_sm(
            std::vector<C2FieldSupportedValuesQuery>& /*fields*/) const override {
        return C2_OK;
    }

 private:
    struct Entry {
        std::shared_ptr<C2Component::Traits> traits;
        ::C2ComponentFactory* (*createFactory)();
        void (*destroyFactory)(::C2ComponentFactory*);
        ::C2ComponentFactory*                factory;  /* lazily created */
    };

    void registerComponent(const char* name,
                            ::C2ComponentFactory* (*create)(),
                            void (*destroy)(::C2ComponentFactory*),
                            C2Component::domain_t domain,
                            C2Component::kind_t kind,
                            const char* mediaType) {
        Entry e;
        e.createFactory  = create;
        e.destroyFactory = destroy;
        e.factory        = nullptr;   /* created on first use — see ensureFactory */
        e.traits = std::make_shared<C2Component::Traits>();
        e.traits->name      = name;
        e.traits->domain    = domain;
        e.traits->kind      = kind;
        e.traits->rank      = 8;   /* AOSP gives audio components rank 8 */
        e.traits->mediaType = mediaType;
        e.traits->owner     = getName();
        mEntries.push_back(e);
        ALOGI("registered C2 component '%s'", name);
    }

    /* Lazily instantiate (and cache) a component's factory. Safe to call only
     * after the store singleton is fully constructed; the factory ctor may
     * re-enter GetCodec2PlatformComponentStore(), which by then returns the
     * cached instance without re-locking the init mutex recursively. */
    ::C2ComponentFactory* ensureFactory(Entry* e) {
        std::lock_guard<std::mutex> lk(mFactoryLock);
        if (!e->factory) {
            GST_C2_STRACE("ensureFactory %s: creating", e->traits->name.c_str());
            e->factory = e->createFactory();
            GST_C2_STRACE("ensureFactory %s: factory=%p", e->traits->name.c_str(),
                          (void*)e->factory);
        }
        return e->factory;
    }

    Entry* find(const C2String& name) {
        for (auto& e : mEntries) {
            if (e.traits->name == name) return &e;
        }
        return nullptr;
    }

    std::shared_ptr<C2ReflectorHelper> mReflector;
    std::vector<Entry>                 mEntries;
    std::atomic<uint32_t>              mNextId{1};
    std::mutex                         mFactoryLock;
};

}  /* namespace */

/* ---------------------------------------------------------------------------
 * Public entry points expected by AOSP code via C2PlatformSupport.h.
 * These are declared inside `namespace android` by the header, so they MUST be
 * defined there too. Singletons follow the real C2Store.cpp pattern (weak_ptr
 * cache guarded by a mutex).
 * ------------------------------------------------------------------------- */
namespace android {

std::shared_ptr<C2AllocatorStore> GetCodec2PlatformAllocatorStore() {
    static std::mutex mutex;
    static std::weak_ptr<C2AllocatorStore> sStore;
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<C2AllocatorStore> store = sStore.lock();
    if (!store) {
        store = std::make_shared<LinuxAllocatorStore>();
        sStore = store;
    }
    return store;
}

std::shared_ptr<C2ComponentStore> GetCodec2PlatformComponentStore() {
    static std::mutex mutex;
    static std::weak_ptr<C2ComponentStore> sStore;
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<C2ComponentStore> store = sStore.lock();
    if (!store) {
        store = std::make_shared<LinuxComponentStore>();
        sStore = store;
    }
    return store;
}

/* ---------------------------------------------------------------------------
 * Block-pool platform support. SimpleC2Component fetches its OUTPUT block pool
 * through GetCodec2BlockPool(); for the audio decoders that pool is always a
 * linear (1D) malloc-backed C2BasicLinearBlockPool. Graphic pools are refused.
 * ------------------------------------------------------------------------- */
c2_status_t GetCodec2BlockPool(
        C2BlockPool::local_id_t id,
        std::shared_ptr<const C2Component> /*component*/,
        std::shared_ptr<C2BlockPool>* pool) {
    if (!pool) return C2_BAD_VALUE;
    pool->reset();
    auto alloc = gst_c2::GetLinuxLinearAllocator();
    switch (id) {
        case C2BlockPool::BASIC_LINEAR:
        // PLATFORM_START and any vendor linear id all map to our single
        // malloc-backed linear pool — the audio path only ever needs 1D.
        default:
            *pool = std::make_shared<C2BasicLinearBlockPool>(alloc);
            return *pool ? C2_OK : C2_NO_MEMORY;
        case C2BlockPool::BASIC_GRAPHIC:
            return C2_NOT_FOUND;   // no graphic pools on the audio-only port
    }
}

c2_status_t CreateCodec2BlockPool(
        C2PlatformAllocatorStore::id_t /*allocatorId*/,
        std::shared_ptr<const C2Component> /*component*/,
        std::shared_ptr<C2BlockPool>* pool) {
    if (!pool) return C2_BAD_VALUE;
    *pool = std::make_shared<C2BasicLinearBlockPool>(gst_c2::GetLinuxLinearAllocator());
    return *pool ? C2_OK : C2_NO_MEMORY;
}

int GetCodec2PoolMask() {
    /* Single malloc allocator available; expose it as the ION slot. */
    return 1 << C2PlatformAllocatorStore::ION;
}

C2PlatformAllocatorStore::id_t GetPreferredLinearAllocatorId(int /*poolMask*/) {
    return C2PlatformAllocatorStore::ION;   // our malloc allocator registers here
}

}  /* namespace android */

namespace gst_c2 {

c2_status_t CreateLinuxC2Component(const char* name, std::shared_ptr<C2Component>* out) {
    if (!name || !out) return C2_BAD_VALUE;
    return ::android::GetCodec2PlatformComponentStore()->createComponent(name, out);
}

}  /* namespace gst_c2 */
