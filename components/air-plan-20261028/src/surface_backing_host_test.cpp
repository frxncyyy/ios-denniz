// Minimal kernel service doubles; the allocator/free implementation itself is
// extracted from ordered XNU patch sections by test_gpu_surface_backing.py.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <thread>
#include <vector>
#include "TeguSurfaceGPUPage.h"
#include "TeguSurfaceGPUImportCandidate.h"

using SInt64 = int64_t;
using UInt64 = uint64_t;
using IOByteCount = uint64_t;
using IOOptionBits = uint32_t;
using task_t = void *;
constexpr int kIODirectionInOut = 1;
constexpr int kIOMemoryKernelUserShared = 2;
constexpr int kIOMemoryMapperNone = 4;
constexpr int kIOMapDefaultCache = 0x10, kIOMapWriteCombineCache = 0x20;
constexpr uint64_t page_size = 16384;
#define APPLE_KEXT_OVERRIDE override
#define IONew(T,n) (arrayFailure ? nullptr : new(std::nothrow) T[n])
#define IODelete(p,T,n) delete[] (p)
#define bzero(p,n) std::memset((p),0,(n))
#define OSDeclareDefaultStructors(T) public: T() = default; \
    static void *operator new(size_t n) noexcept { \
        if (allocationFailure.exchange(false)) return nullptr; \
        unsigned call = objectAllocationCalls.fetch_add(1) + 1; \
        if (failObjectAllocationCall.load() == (int)call) return nullptr; \
        return ::operator new(n, std::nothrow); \
    } \
    static void operator delete(void *p) noexcept { ::operator delete(p); }
#define OSDefineMetaClassAndStructors(T, B)
#define OSDynamicCast(T, p) dynamic_cast<T *>(p)
#define OSSafeReleaseNULL(p) do { if (p) { (p)->release(); (p) = nullptr; } } while (0)

#include "actual_surface_limits.h"
static uint64_t sweet_surface_wired;
static uint64_t sweet_surface_gpu_wired;
static void *kernel_task;
extern "C" unsigned long gPhysBase = 0x882000000UL;
extern "C" unsigned long gPhysSize = 0x7e000000UL;

static SInt64 OSAddAtomic64(SInt64 amount, volatile SInt64 *ptr)
{
    return __atomic_fetch_add(ptr, amount, __ATOMIC_SEQ_CST);
}

static bool OSCompareAndSwap64(UInt64 expected, UInt64 desired, volatile UInt64 *ptr)
{
    return __atomic_compare_exchange_n(ptr, &expected, desired, false,
                                      __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
static std::mutex surface_mutex;
static std::mutex *sweet_surface_lock = &surface_mutex;
static void sweet_surface_init_lock() {}
static void IOLockLock(std::mutex *lock) { lock->lock(); }
static void IOLockUnlock(std::mutex *lock) { lock->unlock(); }

static std::atomic<bool> allocationFailure{false};
static std::atomic<bool> arrayFailure{false};
static std::atomic<bool> multiInitFailure{false};
static std::atomic<int> failObjectAllocationCall{-1};
static std::atomic<unsigned> objectAllocationCalls{0};
static std::atomic<unsigned> initCalls{0};
static std::atomic<int> failInitCall{-1};
static std::atomic<int> badPhysicalCall{-1};
static std::atomic<bool> initFailure{false};
static std::atomic<int64_t> badPhysicalOffset{-1};
static std::atomic<uint64_t> forcedPhysical{0x882000000ULL};
static std::atomic<uint64_t> forcedSegment{TEGU_SURFACE_GPU_PAGE_BYTES};
static std::atomic<unsigned> physicalCalls{0};
static std::atomic<unsigned> descriptorFinalFrees{0};

class OSObject {
    std::atomic<unsigned> refs{1};
public:
    virtual bool init() { return true; }
    virtual ~OSObject() = default;
    virtual void free() { delete this; }
    void retain() { refs.fetch_add(1, std::memory_order_relaxed); }
    void release() {
        if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1) free();
    }
};

static std::atomic<bool> mapFailure{false};
class IOMemoryDescriptor;
class IOMemoryMap : public OSObject {
    IOMemoryDescriptor *backing = nullptr;
    uintptr_t address = 0x12345000;
public:
    explicit IOMemoryMap(IOMemoryDescriptor *d);
    uintptr_t getVirtualAddress() const { return address; }
    void free() override;
};
class IOMemoryDescriptor : public OSObject {
public:
    virtual uint64_t getLength() const = 0;
    virtual uint64_t getPhysicalSegment(uint64_t, IOByteCount *, IOOptionBits) = 0;
    virtual IOMemoryMap *map(IOOptionBits);
};
inline IOMemoryMap::IOMemoryMap(IOMemoryDescriptor *d):backing(d){ if(backing)backing->retain(); }
inline void IOMemoryMap::free(){if(backing)backing->release();OSObject::free();}
inline IOMemoryMap *IOMemoryDescriptor::map(IOOptionBits){if(mapFailure)return nullptr;return new(std::nothrow) IOMemoryMap(this);}

static std::atomic<bool> scatterBackingMode{false};
static std::atomic<uint64_t> nextScatterPA{0x882000000ULL};
class IOBufferMemoryDescriptor : public IOMemoryDescriptor {
public:
    uint64_t length = 0;
    uint64_t scatterPA = 0;
    uint64_t getLength() const override { return length; }
    void *getBytesNoCopy() { return reinterpret_cast<void *>(0x12345000); }
    bool initWithPhysicalMask(task_t, unsigned flags, uint64_t bytes, uint64_t alignment,
                              uint64_t mask)
    {
        assert(flags == (kIODirectionInOut | kIOMemoryKernelUserShared | kIOMemoryMapperNone));
        assert(alignment == 1 && mask == TEGU_SURFACE_GPU_PHYSICAL_MASK);
        length = bytes;
        scatterPA = nextScatterPA.fetch_add(0x8000);
        unsigned call = initCalls.fetch_add(1) + 1;
        return !initFailure.load() && failInitCall.load() != (int)call;
    }
    uint64_t getPhysicalSegment(uint64_t offset, IOByteCount *segment, IOOptionBits mapper) override
    {
        assert(mapper == kIOMemoryMapperNone);
        unsigned call = physicalCalls.fetch_add(1) + 1;
        *segment = forcedSegment.load();
        if (badPhysicalOffset.load() == static_cast<int64_t>(offset) || badPhysicalCall.load() == (int)call) return 0;
        return (scatterBackingMode.load() && this->length == page_size ? scatterPA : forcedPhysical.load()) + offset;
    }
    void free() override { descriptorFinalFrees.fetch_add(1); OSObject::free(); }
};

class IOMultiMemoryDescriptor : public IOMemoryDescriptor {
    std::vector<IOMemoryDescriptor *> children;
    uint64_t length = 0;
public:
    bool initWithDescriptors(IOMemoryDescriptor **descriptors, uint32_t count, unsigned, bool asReference) {
        assert(!asReference);
        if (multiInitFailure.exchange(false)) return false;
        for (uint32_t i=0; i<count; ++i) { children.push_back(descriptors[i]); descriptors[i]->retain(); length += descriptors[i]->getLength(); }
        return true;
    }
    uint64_t getLength() const override { return length; }
    uint64_t getPhysicalSegment(uint64_t offset, IOByteCount *segment, IOOptionBits mapper) override {
        for (auto *child : children) { if (offset < child->getLength()) return child->getPhysicalSegment(offset,segment,mapper); offset -= child->getLength(); }
        *segment=0; return 0;
    }
    void free() override { for (auto *child : children) child->release(); OSObject::free(); }
};

struct sweet_surface_plane { uint32_t width=0,height=0,bytesPerRow=0,offset=0,size=0; uint16_t bytesPerElement=0; uint8_t elementWidth=0,elementHeight=0; };
struct sweet_surface_gpu_layout { uint32_t width,height,bytesPerRow,offset,pixelFormat,allocSize,planeCount; uint16_t bytesPerElement; uint8_t elementWidth,elementHeight; sweet_surface_plane plane0; };
class SweetSurface : public OSObject {
public:
    uint32_t id=0,width=0,height=0,bytesPerRow=0,offset=0,pixelFormat=0,planeCount=0;
    uint16_t bytesPerElement=0; uint8_t elementWidth=0,elementHeight=0; bool gpuShared=false;
    sweet_surface_plane planes[20]{};
    uint32_t gpuImportLeaseRefs = 0;
    uint64_t gpuImportChargedBytes = 0;
    uint32_t allocSize = 0;
    IOMemoryDescriptor *memory = nullptr;
    IOMemoryMap *kernelMap = nullptr;
    IOBufferMemoryDescriptor *state = nullptr;
    uintptr_t kernelAddress();
    void free() override;
};
static SweetSurface *testSurface = nullptr;
static SweetSurface *sweet_surface_copy(uint32_t id) { if (!id || !testSurface || testSurface->id != id) return nullptr; testSurface->retain(); return testSurface; }

// Actual patched class and methods, not a host reimplementation.
#include "actual_surface_methods.inc"

static void resetCounters()
{
    assert(sweet_surface_wired == 0 && sweet_surface_gpu_wired == 0);
    physicalCalls = 0;
    badPhysicalOffset = -1;
    badPhysicalCall = -1;
    initCalls = 0; failInitCall = -1;
    objectAllocationCalls = 0; failObjectAllocationCall = -1;
    arrayFailure = false; multiInitFailure = false; mapFailure = false;
    forcedPhysical = 0x882000000ULL;
    nextScatterPA = 0x882000000ULL;
    scatterBackingMode = false;
    forcedSegment = TEGU_SURFACE_GPU_PAGE_BYTES;
    initFailure = false;
    allocationFailure = false;
}

static void expectZero()
{
    assert(sweet_surface_wired == 0 && sweet_surface_gpu_wired == 0);
}

static void test_refcounted_surface_and_map_lifetime()
{
    resetCounters();
    auto *d = SweetSurfaceGPUMemoryDescriptor::allocate(0x3000);
    assert(d && sweet_surface_wired == 0x3000 && sweet_surface_gpu_wired == 0x3000);
    assert(physicalCalls == 3);
    d->retain(); // duplicate owner/map retain
    d->release();
    assert(sweet_surface_wired == 0x3000 && sweet_surface_gpu_wired == 0x3000);

    auto *surface = new SweetSurface;
    surface->allocSize = 0x3000;
    surface->memory = d; // surface owns original reference
    d->retain();          // an independent map keeps the descriptor alive
    surface->release();
    assert(sweet_surface_wired == 0x3000 && sweet_surface_gpu_wired == 0x3000);
    d->release();
    expectZero();
}

static void test_failure_rollback()
{
    resetCounters();
    allocationFailure = true;
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0x1000) == nullptr);
    expectZero();

    initFailure = true;
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0x1000) == nullptr);
    expectZero();
    initFailure = false;

    badPhysicalOffset = 0x1000;
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0x3000) == nullptr);
    expectZero();
    assert(physicalCalls == 2);
    badPhysicalOffset = -1;

    forcedSegment = 0xfff;
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0x2000) == nullptr);
    expectZero();
    forcedSegment = TEGU_SURFACE_GPU_PAGE_BYTES;

    forcedPhysical = TEGU_SURFACE_GPU_PA_MIN - TEGU_SURFACE_GPU_PAGE_BYTES;
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0x1000) == nullptr);
    expectZero();
    forcedPhysical = 0x882000000ULL;

    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0) == nullptr);
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0x1001) == nullptr);
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(SWEET_SURFACE_MAX_ALLOC + 0x1000) == nullptr);
    expectZero();
}

static void test_limits_and_concurrent_accounting()
{
    resetCounters();
    std::vector<SweetSurfaceGPUMemoryDescriptor *> descriptors;
    for (unsigned i = 0; i < 32; ++i) {
        auto *d = SweetSurfaceGPUMemoryDescriptor::allocate(4u << 20);
        assert(d);
        descriptors.push_back(d);
    }
    assert(sweet_surface_gpu_wired == SWEET_SURFACE_GPU_WIRED_MAX);
    assert(sweet_surface_wired == SWEET_SURFACE_GPU_WIRED_MAX);
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(0x1000) == nullptr);
    for (auto *d : descriptors) d->release();
    expectZero();

    resetCounters();
    std::mutex lock;
    std::vector<SweetSurfaceGPUMemoryDescriptor *> concurrent;
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < 48; ++i) {
        threads.emplace_back([&] {
            auto *d = SweetSurfaceGPUMemoryDescriptor::allocate(8u << 20);
            if (d) { std::lock_guard<std::mutex> guard(lock); concurrent.push_back(d); }
        });
    }
    for (auto &thread : threads) thread.join();
    assert(!concurrent.empty());
    assert(sweet_surface_gpu_wired <= SWEET_SURFACE_GPU_WIRED_MAX);
    assert(sweet_surface_wired <= SWEET_SURFACE_WIRED_MAX);
    assert(sweet_surface_gpu_wired == concurrent.size() * (8u << 20));
    assert(sweet_surface_wired == sweet_surface_gpu_wired);
    for (auto *d : concurrent) d->release();
    expectZero();

    // Exercise the shared-total cap with ordinary backing already charged.
    sweet_surface_wired = SWEET_SURFACE_WIRED_MAX - TEGU_SURFACE_GPU_PAGE_BYTES;
    auto *last = SweetSurfaceGPUMemoryDescriptor::allocate(TEGU_SURFACE_GPU_PAGE_BYTES);
    assert(last && sweet_surface_wired == SWEET_SURFACE_WIRED_MAX);
    assert(SweetSurfaceGPUMemoryDescriptor::allocate(TEGU_SURFACE_GPU_PAGE_BYTES) == nullptr);
    last->release();
    assert(sweet_surface_wired == SWEET_SURFACE_WIRED_MAX - TEGU_SURFACE_GPU_PAGE_BYTES);
    sweet_surface_wired = 0; // simulated independent ordinary backing release
    expectZero();
}

static void test_actual_surface_free_charge_paths()
{
    resetCounters();
    sweet_surface_wired = 0x5000; // existing ordinary IOSurface allocation
    auto *gpu = SweetSurfaceGPUMemoryDescriptor::allocate(0x2000);
    assert(gpu && sweet_surface_wired == 0x7000 && sweet_surface_gpu_wired == 0x2000);
    auto *gpuSurface = new SweetSurface;
    gpuSurface->allocSize = 0x2000;
    gpuSurface->memory = gpu;
    gpuSurface->release();
    assert(sweet_surface_wired == 0x5000 && sweet_surface_gpu_wired == 0);

    auto *ordinary = new SweetSurface;
    ordinary->allocSize = 0x5000;
    ordinary->memory = new IOBufferMemoryDescriptor;
    ordinary->release();
    expectZero();
}

static void test_actual_import_lease_lifetime_and_quota()
{
    resetCounters();
    auto *ordinary = new SweetSurface;
    ordinary->allocSize = 0x4000;
    auto *memory = new IOBufferMemoryDescriptor;
    memory->length = ordinary->allocSize;
    ordinary->memory = memory;
    sweet_surface_wired = ordinary->allocSize;
    auto *first = sweet_surface_gpu_import_lease_create(ordinary);
    auto *second = sweet_surface_gpu_import_lease_create(ordinary);
    assert(first && second && ordinary->gpuImportLeaseRefs == 2);
    assert(sweet_surface_gpu_wired == 0x4000); // deduplicated physical backing
    first->retain(); // an in-flight/quarantined submission owns the lease
    first->release(); // imported handle closes
    second->release();
    assert(ordinary->gpuImportLeaseRefs == 1 && sweet_surface_gpu_wired == 0x4000);
    ordinary->release(); // caller drops the last non-GPU surface reference
    assert(sweet_surface_wired == 0x4000 && sweet_surface_gpu_wired == 0x4000);
    first->release(); // safe completion frees both quota and final surface owner
    expectZero();

    auto *custom = new SweetSurface;
    custom->allocSize = 0x2000;
    custom->memory = SweetSurfaceGPUMemoryDescriptor::allocate(custom->allocSize);
    assert(custom->memory && sweet_surface_gpu_wired == 0x2000);
    auto *customLease = sweet_surface_gpu_import_lease_create(custom);
    assert(customLease && custom->gpuImportChargedBytes == 0);
    assert(sweet_surface_gpu_wired == 0x2000); // no double charge
    custom->release();
    customLease->release();
    expectZero();

    ordinary = new SweetSurface;
    ordinary->allocSize = 0x1000;
    memory = new IOBufferMemoryDescriptor;
    memory->length = ordinary->allocSize;
    ordinary->memory = memory;
    sweet_surface_wired = ordinary->allocSize;
    sweet_surface_gpu_wired = SWEET_SURFACE_GPU_WIRED_MAX; // other live clients
    assert(!sweet_surface_gpu_import_lease_create(ordinary));
    assert(ordinary->gpuImportLeaseRefs == 0 && ordinary->gpuImportChargedBytes == 0);
    assert(sweet_surface_gpu_wired == SWEET_SURFACE_GPU_WIRED_MAX);
    ordinary->release();
    sweet_surface_gpu_wired = 0; // independent clients complete
    expectZero();
}

static void test_scatter_failure_rollback_and_map_lifetime()
{
    const uint32_t bytes=4u*static_cast<uint32_t>(page_size);
    resetCounters(); scatterBackingMode=true;
    failInitCall=2; // first child succeeds, second page allocation fails
    assert(SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes)==nullptr);
    expectZero(); assert(initCalls==2); resetCounters(); scatterBackingMode=true;

    badPhysicalCall=5; // after four validated pages of child 1, fail child 2 page 0
    assert(SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes)==nullptr);
    expectZero(); assert(physicalCalls==5); resetCounters(); scatterBackingMode=true;

    multiInitFailure=true; // all child pages allocated; aggregate init refuses
    assert(SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes)==nullptr);
    expectZero(); assert(initCalls==4); resetCounters(); scatterBackingMode=true;

    arrayFailure=true;
    assert(SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes)==nullptr);
    expectZero(); resetCounters(); scatterBackingMode=true;

    failObjectAllocationCall=5; // 4 child descriptors succeed; aggregate object allocation fails
    assert(SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes)==nullptr);
    expectZero(); assert(initCalls==4); resetCounters(); scatterBackingMode=true;

    auto *surface=new SweetSurface; surface->gpuShared=true; surface->allocSize=bytes;
    surface->memory=SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes);
    assert(surface->memory); uintptr_t address=surface->kernelAddress(); assert(address!=0);
    assert(surface->kernelMap && sweet_surface_gpu_wired==bytes);
    IOMemoryMap *independentMap=surface->kernelMap; independentMap->retain();
    surface->release(); // releases surface map and descriptor refs, external map still pins children
    assert(sweet_surface_gpu_wired==bytes);
    independentMap->release(); expectZero();

    resetCounters(); scatterBackingMode=true;
    surface=new SweetSurface; surface->gpuShared=true; surface->allocSize=bytes;
    surface->memory=SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes);
    assert(surface->memory); mapFailure=true;
    assert(surface->kernelAddress()==0); // map failure leaves backing owned by surface for normal free
    assert(sweet_surface_gpu_wired==bytes); surface->release(); expectZero();
}

static void test_selector13_17_classification_for_exact_scatter_owner()
{
    resetCounters();
    scatterBackingMode = true;
    const uint32_t bytes=2u*static_cast<uint32_t>(page_size);
    auto *gpu=new SweetSurface;
    gpu->id=91; gpu->gpuShared=true; gpu->width=2; gpu->height=2; gpu->bytesPerRow=8;
    gpu->pixelFormat=80; gpu->allocSize=bytes; gpu->planeCount=1; gpu->bytesPerElement=4;
    gpu->planes[0].width=2; gpu->planes[0].height=2; gpu->planes[0].bytesPerRow=8;
    gpu->memory=SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes);
    assert(gpu->memory); testSurface=gpu;
    sweet_surface_gpu_layout layout{};
    SweetSurface *legacy=sweet_surface_copy_gpu_shared(gpu->id,&layout);
    assert(legacy==gpu&&layout.allocSize==bytes&&layout.width==2);
    legacy->release();
    SweetSurface *candidate=sweet_surface_copy_gpu_import_candidate(gpu->id,&layout);
    assert(candidate==gpu&&layout.allocSize==bytes); candidate->release();
    assert(sweet_surface_gpu_wired==bytes); // neither legacy nor v1 candidate adds lease charges
    testSurface=nullptr; gpu->release(); expectZero();

    resetCounters();
    auto *unconstrained=new SweetSurface; unconstrained->id=92; unconstrained->gpuShared=true;
    auto *ordinary=new IOBufferMemoryDescriptor; ordinary->length=0x4000; unconstrained->memory=ordinary;
    testSurface=unconstrained;
    assert(sweet_surface_copy_gpu_shared(unconstrained->id,&layout)==nullptr);
    assert(sweet_surface_copy_gpu_import_candidate(unconstrained->id,&layout)==nullptr);
    testSurface=nullptr; sweet_surface_wired=0x4000; unconstrained->allocSize=0x4000; unconstrained->release(); expectZero();
}

static void test_fullsize_scattered_target_and_last_child_failure()
{
    // R34's exact allocation must be assembled from native pages even when
    // adjacent logical pages have no physically contiguous run.
    constexpr uint32_t bytes = 11239424;
    constexpr unsigned children = 686;
    static_assert(bytes == children * page_size);
    resetCounters(); scatterBackingMode = true;
    auto *backing = SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes);
    assert(backing && backing->getLength() == bytes && initCalls == children);
    assert(sweet_surface_wired == bytes && sweet_surface_gpu_wired == bytes);
    uint64_t previous = 0;
    for (uint64_t off = 0; off < bytes; off += TEGU_SURFACE_GPU_PAGE_BYTES) {
        IOByteCount segment = 0;
        uint64_t pa = backing->getPhysicalSegment(off, &segment, kIOMemoryMapperNone);
        assert(tegu_surface_gpu_page_valid(off, bytes, pa, segment, gPhysBase, gPhysSize));
        if (previous && !(off % page_size))
            assert(pa != previous + TEGU_SURFACE_GPU_PAGE_BYTES);
        previous = pa;
    }
    backing->release(); expectZero();

    resetCounters(); scatterBackingMode = true;
    failInitCall = children;
    assert(SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes) == nullptr);
    assert(initCalls == children); expectZero();
}

static void test_scatter_backing_lease_owner_and_disjoint_pages()
{
    resetCounters();
    scatterBackingMode = true;
    const uint32_t bytes = 3u * static_cast<uint32_t>(page_size);
    auto *multi = OSDynamicCast(SweetSurfaceGPUScatterMemoryDescriptor,
        SweetSurfaceGPUMemoryDescriptor::allocateScatter(bytes));
    assert(multi && multi->getLength() == bytes);
    assert(sweet_surface_wired == bytes && sweet_surface_gpu_wired == bytes);
    uint64_t previous = 0;
    for (uint64_t off=0; off<bytes; off += TEGU_SURFACE_GPU_PAGE_BYTES) {
        IOByteCount segment = 0;
        uint64_t pa = multi->getPhysicalSegment(off,&segment,kIOMemoryMapperNone);
        assert(tegu_surface_gpu_page_valid(off,bytes,pa,segment,gPhysBase,gPhysSize));
        if (previous && off % page_size == 0) assert(pa != previous + TEGU_SURFACE_GPU_PAGE_BYTES);
        previous=pa;
    }
    auto *surface = new SweetSurface;
    surface->allocSize=bytes;
    surface->memory=multi;
    auto *lease=sweet_surface_gpu_import_lease_create(surface);
    assert(lease && surface->gpuImportLeaseRefs==1 && surface->gpuImportChargedBytes==0);
    assert(sweet_surface_gpu_wired==bytes); // child pages own this charge; lease does not double charge
    surface->release();
    lease->release();
    expectZero();
}

int main()
{
    test_refcounted_surface_and_map_lifetime();
    test_failure_rollback();
    test_limits_and_concurrent_accounting();
    test_actual_surface_free_charge_paths();
    test_actual_import_lease_lifetime_and_quota();
    test_scatter_backing_lease_owner_and_disjoint_pages();
    test_scatter_failure_rollback_and_map_lifetime();
    test_fullsize_scattered_target_and_last_child_failure();
    test_selector13_17_classification_for_exact_scatter_owner();
    std::puts("actual patched IOSurface GPU backing ASan/UBSan tests passed");
}
