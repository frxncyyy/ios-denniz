/*
 * Built-in IOSurface for SWEET.
 *
 * Apple's IOSurface kext is closed. IOSurface.framework (iOS 16.7) finds
 * IOServiceNameMatching("IOCoreSurfaceRoot"), opens it with type 0 and
 * talks to the user client below:
 *
 *   sel 13  limits: struct out 0x18, u16 at +0x16 must be 1 (ABI version),
 *           +0 offset alignment mask, +4 bytes-per-row alignment mask
 *   sel 0   create: scalar [parent id], struct in = serialized property
 *           dictionary (binary or XML), struct out = lock result (0xa68)
 *   sel 4   lookup by id: scalar [id], struct out = lock result
 *   sel 34  lookup by mach port: scalar [port name], struct out = lock result
 *   sel 35  create mach port: scalars [id, options], scalar out [port name]
 *   traps   0/1 use count +/- (id, category), 2 lock / 3 unlock
 *           (id, options, &seed), 4/5 release (id)
 *
 * The lock result tells the library where the surface lives in the
 * caller's address space: +0 pixels, +8 shared state page (registry id,
 * seed at +0xc), +0x10 timestamps, then id, sizes and geometry (layout in
 * sweet_surface_fill_result). Every surface is wired kernel memory mapped
 * into each task that creates or looks it up; mappings are counted per
 * client and dropped on release or close.
 *
 * Values, notifications, accelerators and shared events are accepted and
 * ignored (logged once per selector).
 */
#include "SweetSurface.h"
#include "sweet_surface_abi.h"

#if defined(SWEET)
#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/system.h>
#include <libkern/c++/OSArray.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSUnserialize.h>
#include <sys/proc.h>

enum {
	kSweetSurfaceCreate             = 0,
	kSweetSurfaceLookup             = 4,
	kSweetSurfaceLimits             = 13,
	kSweetSurfaceLookupFromPort     = 34,
	kSweetSurfaceCreatePort         = 35,
};

enum {
	kSweetSurfaceTrapIncrementUse   = 0,
	kSweetSurfaceTrapDecrementUse   = 1,
	kSweetSurfaceTrapLock           = 2,
	kSweetSurfaceTrapUnlock         = 3,
	kSweetSurfaceTrapRelease        = 4,
	kSweetSurfaceTrapReleaseFailed  = 5,
};

#define SWEET_SURFACE_RESULT_SIZE       0xa68
#define SWEET_SURFACE_MAX_ID            4096
#define SWEET_SURFACE_MAX_DIM           16384
#define SWEET_SURFACE_MAX_ALLOC         (256u << 20)
#define SWEET_SURFACE_MAX_PROPS         (64u << 10)
#define SWEET_SURFACE_ROW_ALIGN         64
#define SWEET_SURFACE_TIMESTAMPS        0x800   /* offset in the state page */
#define SWEET_SURFACE_STATE_SEED        0xc     /* u32 in the state page */
#define kIOSurfaceLockReadOnly          0x1

static IOLock       *sweet_surface_lock;
static SweetSurface *sweet_surface_table[SWEET_SURFACE_MAX_ID];
static uint32_t      sweet_surface_users[SWEET_SURFACE_MAX_ID];
static uint32_t      sweet_surface_next_id = 1;
static uint32_t      sweet_surface_creates_logged;
static uint64_t      sweet_surface_wired;       /* bytes of buffer surfaces */
/*
 * 0299: every surface here is wired kernel memory (nothing is purgeable or paged as on Apple's IOSurface), under one budget.
 * 1536 MiB was reached by the Photos application and the camera together (run user2: 1438 creations refused with
 * kIOReturnNoMemory, of every size). 2048 MiB of the 3776 MiB the kernel is given; each refusal now says what is held, by whom.
 */
#define SWEET_SURFACE_WIRED_MAX         (2048ull << 20)
static int32_t       sweet_surface_owner[SWEET_SURFACE_MAX_ID];     /* pid that put the surface in the table */
static uint32_t      sweet_surface_buffers;     /* buffer surfaces alive (in the table or only held by a port / the kernel) */
static uint64_t      sweet_surface_refused, sweet_surface_report_ns;

static uint32_t      sweet_surface_seq[SWEET_SURFACE_MAX_ID];       /* 0300: creation number of the surface in the slot */
static uint32_t      sweet_surface_born[SWEET_SURFACE_MAX_ID];      /* 0300: second of uptime it was put there */
static uint32_t      sweet_surface_created;     /* buffer surfaces ever made (released = created - alive) */
static void sweet_surface_report(const char *why, uint32_t asked);      /* after the client class */

static uint32_t
sweet_surface_uptime(void)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return (uint32_t)(ns / 1000000000ull);
}

OSDefineMetaClassAndStructors(SweetSurface, OSObject);

void
SweetSurface::free()
{
	if (OSDynamicCast(IOBufferMemoryDescriptor, memory) != NULL) {
		OSAddAtomic64(-(SInt64)allocSize, (volatile SInt64 *)&sweet_surface_wired);
		OSDecrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
	}
	OSSafeReleaseNULL(kernelMap);
	OSSafeReleaseNULL(memory);
	OSSafeReleaseNULL(state);
	OSObject::free();
}

uintptr_t
SweetSurface::kernelAddress()
{
	IOBufferMemoryDescriptor *buffer = OSDynamicCast(IOBufferMemoryDescriptor, memory);
	if (buffer != NULL) {
		return (uintptr_t)buffer->getBytesNoCopy();
	}
	IOLockLock(sweet_surface_lock);
	if (kernelMap == NULL && memory != NULL) {
		kernelMap = memory->map(kIOMapWriteCombineCache);
	}
	IOLockUnlock(sweet_surface_lock);
	return kernelMap != NULL ? (uintptr_t)kernelMap->getVirtualAddress() : 0;
}

static uint32_t *
sweet_surface_seed(SweetSurface *s)
{
	return (uint32_t *)((uint8_t *)s->state->getBytesNoCopy() + SWEET_SURFACE_STATE_SEED);
}

/*
 * Give the surface an id and a table slot; the table holds one retain and a
 * user count (creators, client mappings, kernel holders). Lock held.
 */
static bool
sweet_surface_insert_locked(SweetSurface *s)
{
	for (uint32_t n = 0; n < SWEET_SURFACE_MAX_ID - 1; n++) {
		uint32_t id = sweet_surface_next_id;
		sweet_surface_next_id = id + 1 < SWEET_SURFACE_MAX_ID ? id + 1 : 1;
		if (sweet_surface_table[id] == NULL) {
			s->id = id;
			s->retain();
			sweet_surface_table[id] = s;
			sweet_surface_users[id] = 1;
			sweet_surface_owner[id] = proc_selfpid();
			sweet_surface_seq[id] = sweet_surface_created;
			sweet_surface_born[id] = sweet_surface_uptime();
			*(uint64_t *)s->state->getBytesNoCopy() = id;   /* registry id */
			return true;
		}
	}
	return false;
}

/* Add a user to a surface that may have left the table (port lookups). */
static bool
sweet_surface_get(SweetSurface *s)
{
	bool ok = true;
	IOLockLock(sweet_surface_lock);
	if (sweet_surface_table[s->id] == s) {
		sweet_surface_users[s->id]++;
	} else if (sweet_surface_table[s->id] == NULL) {
		s->retain();
		sweet_surface_table[s->id] = s;
		sweet_surface_users[s->id] = 1;
	} else {
		ok = false;
	}
	IOLockUnlock(sweet_surface_lock);
	return ok;
}

/* Drop a user; the last one removes the surface from the table. */
static void
sweet_surface_put(SweetSurface *s)
{
	bool drop = false;
	IOLockLock(sweet_surface_lock);
	if (sweet_surface_table[s->id] == s && --sweet_surface_users[s->id] == 0) {
		sweet_surface_table[s->id] = NULL;
		drop = true;
	}
	IOLockUnlock(sweet_surface_lock);
	if (drop) {
		s->release();
	}
}

SweetSurface *
sweet_surface_copy(uint32_t id)
{
	SweetSurface *s = NULL;
	if (id == 0 || id >= SWEET_SURFACE_MAX_ID || sweet_surface_lock == NULL) {
		return NULL;
	}
	IOLockLock(sweet_surface_lock);
	s = sweet_surface_table[id];
	if (s != NULL) {
		s->retain();
	}
	IOLockUnlock(sweet_surface_lock);
	return s;
}

/* The table lock; the framebuffer may publish a surface before the root starts. */
static void
sweet_surface_init_lock(void)
{
	if (sweet_surface_lock == NULL) {
		IOLock *lock = IOLockAlloc();
		if (!OSCompareAndSwapPtr(NULL, lock, &sweet_surface_lock)) {
			IOLockFree(lock);
		}
	}
}

static SweetSurface *
sweet_surface_alloc(void)
{
	sweet_surface_init_lock();
	SweetSurface *s = OSTypeAlloc(SweetSurface);
	if (s == NULL || !s->init()) {
		OSSafeReleaseNULL(s);
		return NULL;
	}
	s->state = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
	    kIODirectionInOut | kIOMemoryKernelUserShared, page_size, page_size);
	if (s->state == NULL) {
		s->release();
		return NULL;
	}
	bzero(s->state->getBytesNoCopy(), page_size);
	return s;
}

static bool
sweet_surface_publish(SweetSurface *s)
{
	bool ok;
	IOLockLock(sweet_surface_lock);
	ok = sweet_surface_insert_locked(s);
	IOLockUnlock(sweet_surface_lock);
	return ok;
}

SweetSurface *
sweet_surface_create_physical(uint64_t phys, uint32_t width, uint32_t height,
    uint32_t bytesPerRow, uint32_t pixelFormat)
{
	SweetSurface *s = sweet_surface_alloc();
	if (s == NULL) {
		return NULL;
	}
	s->width = width;
	s->height = height;
	s->bytesPerRow = bytesPerRow;
	s->bytesPerElement = 4;
	s->elementWidth = s->elementHeight = 1;
	s->pixelFormat = pixelFormat;
	s->allocSize = (uint32_t)round_page(bytesPerRow * height);
	s->memory = IOMemoryDescriptor::withPhysicalAddress(phys, s->allocSize, kIODirectionInOut);
	s->mapCache = kIOMapWriteCombineCache;
	if (s->memory == NULL || !sweet_surface_publish(s)) {
		s->release();
		return NULL;
	}
	return s;
}

mach_port_name_t
sweet_surface_port_in_task(SweetSurface *s, task_t task)
{
	mach_port_name_t name = 0;
	IOUserClient::copyPortNameForObjectInTask(task, s, &name);
	return name;
}

static uint32_t
sweet_dict_u32(OSDictionary *dict, const char *key, uint32_t def)
{
	OSNumber *n = OSDynamicCast(OSNumber, dict->getObject(key));
	return n != NULL ? n->unsigned32BitValue() : def;
}

static uint32_t
sweet_div_up(uint32_t a, uint32_t b)
{
	return b > 1 ? (a + b - 1) / b : a;
}

static uint32_t
sweet_row_bytes(uint32_t width, uint32_t ew, uint32_t bpe)
{
	uint64_t row = (uint64_t)sweet_div_up(width, ew) * bpe;
	return (uint32_t)((row + SWEET_SURFACE_ROW_ALIGN - 1) & ~(uint64_t)(SWEET_SURFACE_ROW_ALIGN - 1));
}

/* Geometry from the creation properties (IOSurface.h keys). */
static IOReturn
sweet_surface_layout(SweetSurface *s, OSDictionary *props)
{
	uint64_t end;

	s->width = sweet_dict_u32(props, "IOSurfaceWidth", 0);
	s->height = sweet_dict_u32(props, "IOSurfaceHeight", 0);
	s->bytesPerElement = (uint16_t)sweet_dict_u32(props, "IOSurfaceBytesPerElement", 4);
	s->elementWidth = (uint8_t)sweet_dict_u32(props, "IOSurfaceElementWidth", 1);
	s->elementHeight = (uint8_t)sweet_dict_u32(props, "IOSurfaceElementHeight", 1);
	s->pixelFormat = sweet_dict_u32(props, "IOSurfacePixelFormat", 0);
	s->cacheMode = sweet_dict_u32(props, "IOSurfaceCacheMode", 0);
	s->offset = sweet_dict_u32(props, "IOSurfaceOffset", 0);
	if (s->width > SWEET_SURFACE_MAX_DIM || s->height > SWEET_SURFACE_MAX_DIM ||
	    s->bytesPerElement > 64 || s->elementWidth == 0 || s->elementHeight == 0) {
		return kIOReturnBadArgument;
	}
	s->bytesPerRow = sweet_dict_u32(props, "IOSurfaceBytesPerRow",
	    sweet_row_bytes(s->width, s->elementWidth, s->bytesPerElement));
	if (s->bytesPerRow < (uint64_t)sweet_div_up(s->width, s->elementWidth) * s->bytesPerElement) {
		return kIOReturnBadArgument;
	}
	end = (uint64_t)s->offset + (uint64_t)s->bytesPerRow * sweet_div_up(s->height, s->elementHeight);

	OSArray *planes = OSDynamicCast(OSArray, props->getObject("IOSurfacePlaneInfo"));
	s->planeCount = 0;
	if (planes != NULL) {
		uint64_t next = s->offset;
		if (planes->getCount() > SWEET_SURFACE_MAX_PLANES) {
			return kIOReturnBadArgument;
		}
		for (unsigned int i = 0; i < planes->getCount(); i++) {
			OSDictionary *pd = OSDynamicCast(OSDictionary, planes->getObject(i));
			struct sweet_surface_plane *p = &s->planes[i];
			if (pd == NULL) {
				return kIOReturnBadArgument;
			}
			p->width = sweet_dict_u32(pd, "IOSurfacePlaneWidth", s->width);
			p->height = sweet_dict_u32(pd, "IOSurfacePlaneHeight", s->height);
			p->bytesPerElement = (uint16_t)sweet_dict_u32(pd, "IOSurfacePlaneBytesPerElement", 1);
			p->elementWidth = (uint8_t)sweet_dict_u32(pd, "IOSurfacePlaneElementWidth", 1);
			p->elementHeight = (uint8_t)sweet_dict_u32(pd, "IOSurfacePlaneElementHeight", 1);
			if (p->width > SWEET_SURFACE_MAX_DIM || p->height > SWEET_SURFACE_MAX_DIM ||
			    p->bytesPerElement > 64 || p->elementWidth == 0 || p->elementHeight == 0) {
				return kIOReturnBadArgument;
			}
			p->bytesPerRow = sweet_dict_u32(pd, "IOSurfacePlaneBytesPerRow",
			    sweet_row_bytes(p->width, p->elementWidth, p->bytesPerElement));
			p->offset = sweet_dict_u32(pd, "IOSurfacePlaneOffset", (uint32_t)next);
			p->size = sweet_dict_u32(pd, "IOSurfacePlaneSize",
			    p->bytesPerRow * sweet_div_up(p->height, p->elementHeight));
			next = round_page((uint64_t)p->offset + p->size);
			if ((uint64_t)p->offset + p->size > end) {
				end = (uint64_t)p->offset + p->size;
			}
		}
		s->planeCount = planes->getCount();
	}

	uint64_t alloc = sweet_dict_u32(props, "IOSurfaceAllocSize", 0);
	if (alloc < end) {
		alloc = end;
	}
	alloc = round_page(alloc);
	if (alloc == 0 || alloc > SWEET_SURFACE_MAX_ALLOC) {
		return kIOReturnNoMemory;
	}
	s->allocSize = (uint32_t)alloc;
	return kIOReturnSuccess;
}

/* Per-client mapping of one surface. */
class SweetSurfaceRef : public OSObject {
	OSDeclareDefaultStructors(SweetSurfaceRef);

public:
	SweetSurface *surface;
	IOMemoryMap  *data;
	IOMemoryMap  *state;
	uint32_t      count;

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(SweetSurfaceRef, OSObject);

void
SweetSurfaceRef::free()
{
	OSSafeReleaseNULL(data);
	OSSafeReleaseNULL(state);
	if (surface != NULL) {
		sweet_surface_put(surface);
		surface->release();
		surface = NULL;
	}
	OSObject::free();
}

class IOCoreSurfaceRoot : public IOService {
	OSDeclareDefaultStructors(IOCoreSurfaceRoot);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(IOCoreSurfaceRoot, IOService);

bool
IOCoreSurfaceRoot::start(IOService *provider)
{
	if (!IOService::start(provider)) {
		return false;
	}
	sweet_surface_init_lock();
	setProperty(kIOUserClientClassKey, "IOSurfaceRootUserClient");
	registerService();
	IOLog("SWEET SURF: IOCoreSurfaceRoot registered\n");
	return true;
}

class IOSurfaceRootUserClient : public IOUserClient {
	OSDeclareDefaultStructors(IOSurfaceRootUserClient);

	task_t   fTask;
	IOLock  *fLock;
	OSArray *fRefs;
	int32_t  fPid;          /* 0300 */
	friend void sweet_surface_report(const char *why, uint32_t asked);

	SweetSurfaceRef *findRef(uint32_t id, unsigned int *index);
	IOReturn addRef(SweetSurface *s, SweetSurfaceRef **refOut, bool *existing);
	IOReturn returnSurface(SweetSurface *s, IOExternalMethodArguments *args);
	IOReturn create(IOExternalMethodArguments *args);

public:
	bool initWithTask(task_t owningTask, void *securityID, UInt32 type) APPLE_KEXT_OVERRIDE;
	void free() APPLE_KEXT_OVERRIDE;
	IOReturn clientClose(void) APPLE_KEXT_OVERRIDE;
	IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *args,
	    IOExternalMethodDispatch *dispatch, OSObject *target,
	    void *reference) APPLE_KEXT_OVERRIDE;
	IOExternalTrap *getTargetAndTrapForIndex(IOService **targetP, UInt32 index) APPLE_KEXT_OVERRIDE;

	IOReturn trapUseCount(void *id, void *category, void *, void *, void *, void *);
	IOReturn trapLock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapUnlock(void *id, void *options, void *seedOut, void *, void *, void *);
	IOReturn trapRelease(void *id, void *, void *, void *, void *, void *);
};

OSDefineMetaClassAndStructors(IOSurfaceRootUserClient, IOUserClient);

/*
 * 0300: who HOLDS the surfaces. A surface stays in the table (and wired) as long as one client has it in its fRefs: its creator or
 * anybody that looked it up; the table itself keeps nothing alive (sweet_surface_put). Run user3 showed all 719 buffer surfaces in
 * the table and a dead creator still "owning" 16 of them, i.e. somebody else holds those. So every open client is listed here
 * and a refusal says, besides the creators (0299): the holders, what of it is volatile (IOSurfaceSetPurgeable: the holder said the
 * contents may be thrown away; nothing is thrown away here, the pages are wired and mapped), which sizes, how old.
 * Lock order: sweet_clients_lock, then a client's fLock, then sweet_surface_lock (as fRefs' removal already takes the last two).
 */
#define SWEET_CLIENTS_MAX 512
static IOSurfaceRootUserClient *sweet_clients[SWEET_CLIENTS_MAX];
static IOLock *sweet_clients_lock;

static void
sweet_clients_init_lock(void)
{
	if (sweet_clients_lock == NULL) {
		IOLock *lock = IOLockAlloc();
		if (!OSCompareAndSwapPtr(NULL, lock, &sweet_clients_lock)) {
			IOLockFree(lock);
		}
	}
}

struct sweet_report_who { int32_t pid; uint32_t count, vol_count, dead_count; uint64_t bytes, vol_bytes, dead_bytes; };
struct sweet_report_size { uint32_t width, height, format, count, oldest; uint64_t bytes; };
#define SWEET_REPORT_WHO 32
#define SWEET_REPORT_SIZES 24

static void
sweet_report_sort(struct sweet_report_who *who, uint32_t used, uint32_t first)
{
	for (uint32_t n = 0; n < first && n < used; n++) {
		uint32_t best = n;
		for (uint32_t k = n + 1; k < used; k++) {
			if (who[k].bytes > who[best].bytes) {
				best = k;
			}
		}
		struct sweet_report_who tmp = who[n]; who[n] = who[best]; who[best] = tmp;
	}
}

static bool
sweet_report_dead(int32_t pid)
{
	proc_t p = pid > 0 ? proc_find(pid) : NULL;
	if (p != NULL) {
		proc_rele(p);
		return false;
	}
	return pid > 0;
}

/* At most every five seconds. Only reads; a client that is closing is kept out by sweet_clients_lock (free()) and its fLock. */
static void
sweet_surface_report(const char *why, uint32_t asked)
{
	static struct sweet_report_who made[SWEET_REPORT_WHO], held[SWEET_REPORT_WHO];     /* under sweet_clients_lock */
	static struct sweet_report_size sizes[3][SWEET_REPORT_SIZES];
	static bool dead[SWEET_REPORT_WHO];
	uint32_t made_n = 0, held_n = 0, in_table = 0, vol_count = 0, now = sweet_surface_uptime();
	uint64_t table_bytes = 0, vol_bytes = 0;
	uint64_t refused = OSAddAtomic64(1, (volatile SInt64 *)&sweet_surface_refused) + 1;
	sweet_clients_init_lock();
	IOLockLock(sweet_clients_lock);
	if (sweet_surface_report_ns != 0 && now - (uint32_t)sweet_surface_report_ns < 5) {
		IOLockUnlock(sweet_clients_lock);
		return;
	}
	sweet_surface_report_ns = now ? now : 1;
	/* creators */
	IOLockLock(sweet_surface_lock);
	for (uint32_t id = 1; id < SWEET_SURFACE_MAX_ID; id++) {
		SweetSurface *t = sweet_surface_table[id];
		if (t == NULL || OSDynamicCast(IOBufferMemoryDescriptor, t->memory) == NULL) {
			continue;
		}
		in_table++;
		table_bytes += t->allocSize;
		if (t->purgeable == 1) {
			vol_count++; vol_bytes += t->allocSize;
		}
		uint32_t k = 0;
		while (k < made_n && made[k].pid != sweet_surface_owner[id]) {
			k++;
		}
		if (k == made_n && made_n < SWEET_REPORT_WHO) {
			bzero(&made[made_n], sizeof(made[0]));
			made[made_n++].pid = sweet_surface_owner[id];
		}
		if (k < made_n) {
			made[k].count++; made[k].bytes += t->allocSize;
			if (t->purgeable == 1) {
				made[k].vol_count++; made[k].vol_bytes += t->allocSize;
			}
		}
	}
	sweet_report_sort(made, made_n, 5);
	/* the sizes of the three biggest creators */
	bzero(sizes, sizeof(sizes));
	for (uint32_t id = 1; id < SWEET_SURFACE_MAX_ID; id++) {
		SweetSurface *t = sweet_surface_table[id];
		if (t == NULL || OSDynamicCast(IOBufferMemoryDescriptor, t->memory) == NULL) {
			continue;
		}
		for (uint32_t n = 0; n < 3 && n < made_n; n++) {
			if (made[n].pid != sweet_surface_owner[id]) {
				continue;
			}
			uint32_t age = now - sweet_surface_born[id];
			for (uint32_t k = 0; k < SWEET_REPORT_SIZES; k++) {
				struct sweet_report_size *z = &sizes[n][k];
				if (z->count == 0 || (z->width == t->width && z->height == t->height && z->format == t->pixelFormat)) {
					z->width = t->width; z->height = t->height; z->format = t->pixelFormat;
					z->count++; z->bytes += t->allocSize;
					if (age > z->oldest) {
						z->oldest = age;
					}
					break;
				}
			}
		}
	}
	IOLockUnlock(sweet_surface_lock);
	for (uint32_t k = 0; k < made_n; k++) {
		dead[k] = k < 5 ? sweet_report_dead(made[k].pid) : false;
	}
	/* holders */
	for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
		IOSurfaceRootUserClient *c = sweet_clients[i];
		if (c == NULL || c->fLock == NULL || c->fRefs == NULL) {
			continue;
		}
		uint32_t k = 0;
		while (k < held_n && held[k].pid != c->fPid) {
			k++;
		}
		if (k == held_n) {
			if (held_n == SWEET_REPORT_WHO) {
				continue;
			}
			bzero(&held[held_n], sizeof(held[0]));
			held[held_n++].pid = c->fPid;
		}
		IOLockLock(c->fLock);
		for (unsigned int r = 0; r < c->fRefs->getCount(); r++) {
			SweetSurface *t = ((SweetSurfaceRef *)c->fRefs->getObject(r))->surface;
			if (t == NULL || t->id >= SWEET_SURFACE_MAX_ID || OSDynamicCast(IOBufferMemoryDescriptor, t->memory) == NULL) {
				continue;
			}
			held[k].count++; held[k].bytes += t->allocSize;
			if (t->purgeable == 1) {
				held[k].vol_count++; held[k].vol_bytes += t->allocSize;
			}
			int32_t owner = sweet_surface_owner[t->id];
			for (uint32_t m = 0; m < 5 && m < made_n; m++) {
				if (made[m].pid == owner && dead[m] && owner != c->fPid) {
					held[k].dead_count++; held[k].dead_bytes += t->allocSize;
				}
			}
		}
		IOLockUnlock(c->fLock);
	}
	sweet_report_sort(held, held_n, 6);
	IOLog("SWEET SURF: refused (%s, %u bytes asked, refusal %llu): %llu of %llu MiB wired in %u buffer surfaces (%u made, %u released); table has %u, %llu MiB; volatile %u, %llu MiB\n",
	    why, asked, refused, sweet_surface_wired >> 20, SWEET_SURFACE_WIRED_MAX >> 20, sweet_surface_buffers,
	    sweet_surface_created, sweet_surface_created - sweet_surface_buffers, in_table, table_bytes >> 20, vol_count, vol_bytes >> 20);
	for (uint32_t n = 0; n < 5 && n < made_n; n++) {
		char name[32] = "?";
		proc_name(made[n].pid, name, sizeof(name));
		IOLog("SWEET SURF:   made by %s[%d]%s: %u, %llu MiB (volatile %u, %llu MiB)\n", name, made[n].pid, dead[n] ? " (gone)" : "",
		    made[n].count, made[n].bytes >> 20, made[n].vol_count, made[n].vol_bytes >> 20);
		if (n >= 3) {
			continue;
		}
		for (uint32_t pass = 0; pass < 5; pass++) {      /* its five sizes with the most bytes */
			struct sweet_report_size *best = NULL;
			for (uint32_t k = 0; k < SWEET_REPORT_SIZES; k++) {
				if (sizes[n][k].count != 0 && (best == NULL || sizes[n][k].bytes > best->bytes)) {
					best = &sizes[n][k];
				}
			}
			if (best == NULL) {
				break;
			}
			IOLog("SWEET SURF:     %ux%u fmt 0x%x: %u, %llu MiB, oldest %u s\n", best->width, best->height, best->format,
			    best->count, best->bytes >> 20, best->oldest);
			best->count = 0;
		}
	}
	for (uint32_t n = 0; n < 6 && n < held_n; n++) {
		char name[32] = "?";
		proc_name(held[n].pid, name, sizeof(name));
		IOLog("SWEET SURF:   held by %s[%d]: %u, %llu MiB (volatile %u, %llu MiB; made by a process that is gone %u, %llu MiB)\n", name, held[n].pid,
		    held[n].count, held[n].bytes >> 20, held[n].vol_count, held[n].vol_bytes >> 20, held[n].dead_count, held[n].dead_bytes >> 20);
	}
	IOLockUnlock(sweet_clients_lock);
}

static uint64_t sweet_surf_logged;      /* selectors / traps already logged (< 64) */

static void
sweet_surf_log_once(const char *what, uint32_t n)
{
	uint32_t bit = (what[0] == 't' ? 48 : 0) + n;
	if (bit >= 64 || (sweet_surf_logged & (1ULL << bit))) {
		return;
	}
	sweet_surf_logged |= 1ULL << bit;
	char name[32];
	proc_selfname(name, sizeof(name));
	IOLog("SWEET SURF: %s %s %u\n", name, what, n);
}

bool
IOSurfaceRootUserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
	if (!IOUserClient::initWithTask(owningTask, securityID, type)) {
		return false;
	}
	fTask = owningTask;
	fLock = IOLockAlloc();
	fRefs = OSArray::withCapacity(16);
	if (fLock == NULL || fRefs == NULL) {
		return false;
	}
	fPid = proc_selfpid();
	sweet_clients_init_lock();
	IOLockLock(sweet_clients_lock);
	for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
		if (sweet_clients[i] == NULL) {
			sweet_clients[i] = this;
			break;
		}
	}
	IOLockUnlock(sweet_clients_lock);
	return true;
}

void
IOSurfaceRootUserClient::free()
{
	if (sweet_clients_lock != NULL) {
		/* out of the list before anything of it goes away; the report holds this lock while it reads fRefs */
		IOLockLock(sweet_clients_lock);
		for (uint32_t i = 0; i < SWEET_CLIENTS_MAX; i++) {
			if (sweet_clients[i] == this) {
				sweet_clients[i] = NULL;
			}
		}
		IOLockUnlock(sweet_clients_lock);
	}
	OSSafeReleaseNULL(fRefs);
	if (fLock != NULL) {
		IOLockFree(fLock);
		fLock = NULL;
	}
	IOUserClient::free();
}

IOReturn
IOSurfaceRootUserClient::clientClose(void)
{
	IOLockLock(fLock);
	fRefs->flushCollection();
	IOLockUnlock(fLock);
	terminate();
	return kIOReturnSuccess;
}

/* Lock held. */
SweetSurfaceRef *
IOSurfaceRootUserClient::findRef(uint32_t id, unsigned int *index)
{
	for (unsigned int i = 0; i < fRefs->getCount(); i++) {
		SweetSurfaceRef *ref = (SweetSurfaceRef *)fRefs->getObject(i);
		if (ref->surface->id == id) {
			if (index != NULL) {
				*index = i;
			}
			return ref;
		}
	}
	return NULL;
}

/*
 * Count one more create/lookup of s by this client, mapping it into the
 * task the first time. Takes over the caller's user count on s.
 *
 * A lookup of a surface this client already has maps nothing and counts
 * nothing (*existing): IOSurface.framework then gets base address -1 in the
 * lock result and reuses its cached IOSurfaceClient. Counting it instead
 * made the framework build a second client for the same id; releasing that
 * one (traps 4 + 5) unmapped the memory under the first (backboardd
 * snapshot crash in CGDataProviderCreateWithData).
 */
IOReturn
IOSurfaceRootUserClient::addRef(SweetSurface *s, SweetSurfaceRef **refOut, bool *existing)
{
	*existing = false;
	IOLockLock(fLock);
	SweetSurfaceRef *ref = findRef(s->id, NULL);
	if (ref != NULL && ref->surface == s) {
		IOLockUnlock(fLock);
		sweet_surface_put(s);
		*refOut = ref;
		*existing = true;
		return kIOReturnSuccess;
	}
	IOLockUnlock(fLock);

	ref = OSTypeAlloc(SweetSurfaceRef);
	if (ref == NULL || !ref->init()) {
		OSSafeReleaseNULL(ref);
		sweet_surface_put(s);
		return kIOReturnNoMemory;
	}
	s->retain();
	ref->surface = s;
	ref->count = 1;
	ref->data = s->memory->createMappingInTask(fTask, 0, kIOMapAnywhere | s->mapCache);
	ref->state = s->state->createMappingInTask(fTask, 0, kIOMapAnywhere | kIOMapReadOnly);
	if (ref->data == NULL || ref->state == NULL) {
		ref->release();
		return kIOReturnVMError;
	}
	IOLockLock(fLock);
	SweetSurfaceRef *raced = findRef(s->id, NULL);
	if (raced != NULL && raced->surface == s) {
		/* Another thread mapped it meanwhile: count there, drop ours. */
		raced->count++;
		IOLockUnlock(fLock);
		ref->release();
		*refOut = raced;
		return kIOReturnSuccess;
	}
	fRefs->setObject(ref);
	IOLockUnlock(fLock);
	ref->release();         /* fRefs holds it */
	*refOut = ref;
	return kIOReturnSuccess;
}

static void
put32(uint8_t *p, uint32_t off, uint32_t v)
{
	memcpy(p + off, &v, 4);
}

static void
put64(uint8_t *p, uint32_t off, uint64_t v)
{
	memcpy(p + off, &v, 8);
}

/* IOSurface.framework's lock result (copied to IOSurfaceClient + 0x68). */
static void
sweet_surface_fill_result(SweetSurface *s, SweetSurfaceRef *ref, uint8_t *out)
{
	bzero(out, SWEET_SURFACE_RESULT_SIZE);
	put64(out, 0x00, ref->data->getAddress());
	put64(out, 0x08, ref->state->getAddress());
	put64(out, 0x10, ref->state->getAddress() + SWEET_SURFACE_TIMESTAMPS);
	put32(out, 0x18, s->id);
	put32(out, 0x1c, s->allocSize);
	put32(out, 0x20, s->width);
	put32(out, 0x24, s->height);
	put32(out, 0x28, s->bytesPerRow);
	put32(out, 0x2c, s->offset);
	put32(out, 0x30, s->pixelFormat);
	put32(out, 0x34, s->allocSize);
	put32(out, 0x38, s->planeCount);
	memcpy(out + 0x40, &s->bytesPerElement, 2);
	out[0x42] = s->elementWidth;
	out[0x43] = s->elementHeight;
	put32(out, 0x44, s->cacheMode);
	put32(out, 0x50, s->parentID);
	put32(out, 0x54, 1);
	for (uint32_t i = 0; i < s->planeCount; i++) {
		const struct sweet_surface_plane *pl = &s->planes[i];
		struct sweet_plane_abi abi = { pl->width, pl->height, pl->bytesPerRow, pl->offset, pl->size,
			                       pl->bytesPerElement, pl->elementWidth, pl->elementHeight };
		sweet_plane_record(out + 0x68 + i * SWEET_PLANE_RECORD_BYTES, &abi);   /* bytesPerRow at +0x10 */
	}
}

static IOReturn
sweet_struct_out(IOExternalMethodArguments *args, const void *buf, uint32_t len)
{
	if (args->structureOutputDescriptor != NULL) {
		if (len > args->structureOutputDescriptor->getLength()) {
			return kIOReturnNoSpace;
		}
		args->structureOutputDescriptor->writeBytes(0, buf, len);
		args->structureOutputDescriptorSize = len;
		return kIOReturnSuccess;
	}
	if (args->structureOutput == NULL || len > args->structureOutputSize) {
		return kIOReturnNoSpace;
	}
	memcpy(args->structureOutput, buf, len);
	args->structureOutputSize = len;
	return kIOReturnSuccess;
}

/* Map s for this client and answer with its lock result. Takes s's user count. */
IOReturn
IOSurfaceRootUserClient::returnSurface(SweetSurface *s, IOExternalMethodArguments *args)
{
	SweetSurfaceRef *ref = NULL;
	bool existing = false;
	IOReturn ret = addRef(s, &ref, &existing);
	if (ret != kIOReturnSuccess) {
		return ret;
	}
	uint8_t *out = (uint8_t *)IOMallocZeroData(SWEET_SURFACE_RESULT_SIZE);
	if (out == NULL) {
		ret = kIOReturnNoMemory;
	} else {
		sweet_surface_fill_result(s, ref, out);
		if (existing) {
			put64(out, 0x00, ~0ULL);        /* "already yours": use the cached client */
		}
		ret = sweet_struct_out(args, out, SWEET_SURFACE_RESULT_SIZE);
		IOFreeData(out, SWEET_SURFACE_RESULT_SIZE);
	}
	if (ret != kIOReturnSuccess && !existing) {
		/* The caller never learns the id: undo this create/lookup. */
		trapRelease((void *)(uintptr_t)s->id, NULL, NULL, NULL, NULL, NULL);
	}
	return ret;
}

IOReturn
IOSurfaceRootUserClient::create(IOExternalMethodArguments *args)
{
	const char *buf = (const char *)args->structureInput;
	uint32_t len = args->structureInputSize;
	char *copy = NULL;
	IOReturn ret;

	if (args->structureInputDescriptor != NULL) {
		len = (uint32_t)args->structureInputDescriptor->getLength();
		if (len == 0 || len > SWEET_SURFACE_MAX_PROPS) {
			return kIOReturnBadArgument;
		}
		copy = (char *)IOMallocData(len);
		if (copy == NULL) {
			return kIOReturnNoMemory;
		}
		args->structureInputDescriptor->readBytes(0, copy, len);
		buf = copy;
	}
	OSObject *obj = (buf != NULL && len > 0) ? OSUnserializeXML(buf, len) : NULL;
	if (copy != NULL) {
		IOFreeData(copy, len);
	}
	OSDictionary *props = OSDynamicCast(OSDictionary, obj);
	if (props == NULL) {
		OSSafeReleaseNULL(obj);
		IOLog("SWEET SURF: create: bad property dictionary (%u bytes)\n", len);
		return kIOReturnBadArgument;
	}

	SweetSurface *s = sweet_surface_alloc();
	if (s == NULL) {
		props->release();
		return kIOReturnNoMemory;
	}
	s->parentID = args->scalarInputCount > 0 ? (uint32_t)args->scalarInput[0] : 0;
	ret = sweet_surface_layout(s, props);
	if (ret == kIOReturnSuccess) {
		IOBufferMemoryDescriptor *mem = NULL;
		bool in_budget = (uint64_t)OSAddAtomic64(s->allocSize, (volatile SInt64 *)&sweet_surface_wired) +
		    s->allocSize <= SWEET_SURFACE_WIRED_MAX;
		if (in_budget) {
			mem = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,
			    kIODirectionInOut | kIOMemoryKernelUserShared, s->allocSize, page_size);
		}
		if (mem == NULL) {
			OSAddAtomic64(-(SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
			ret = kIOReturnNoMemory;
			sweet_surface_report(in_budget ? "the kernel gave no memory" : "over the budget", s->allocSize);   /* 0299 */
		} else {
			OSIncrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
			OSIncrementAtomic((volatile SInt32 *)&sweet_surface_created);
			bzero(mem->getBytesNoCopy(), s->allocSize);
			/*
			 * 0300: a two-plane 4:2:0 surface that nobody has written yet must not read as green (Y 0, Cb Cr 0): the chroma
			 * plane starts at mid level, the luma of the video-range formats at their black. '420f' '420v' 8 bit, 'xf20' 'x420'
			 * 10 bit in 16 (level << 6); the packed/compressed kinds have other layouts and stay zero.
			 */
			if (s->planeCount == 2 && (s->pixelFormat == 0x34323066 || s->pixelFormat == 0x34323076 ||
			    s->pixelFormat == 0x78663230 || s->pixelFormat == 0x78343230)) {
				bool wide = (s->pixelFormat >> 24) == 0x78, video = (s->pixelFormat & 0xff) == 0x76 || s->pixelFormat == 0x78343230;
				uint8_t *base = (uint8_t *)mem->getBytesNoCopy();
				for (uint32_t i = 0; i < 2; i++) {
					uint64_t off = s->planes[i].offset, len = s->planes[i].size;
					if (off > s->allocSize || len > s->allocSize - off || (i == 0 && !video)) {
						continue;
					}
					if (!wide) {
						memset(base + off, i == 0 ? 16 : 0x80, (size_t)len);
					} else {
						uint16_t v = i == 0 ? 0x1000 : 0x8000;
						for (uint64_t k = 0; k + 2 <= len; k += 2) {
							memcpy(base + off + k, &v, 2);
						}
					}
				}
			}
			/* IOSurfaceAddress / IOSurfaceClientAddress: memory the creator already filled.  Keep a snapshot (see
			 * sweet_surface_abi.h) instead of zeros; an unusable range fails the creation so the caller falls back. */
			OSNumber *client = OSDynamicCast(OSNumber, props->getObject("IOSurfaceAddress"));
			if (client == NULL) { client = OSDynamicCast(OSNumber, props->getObject("IOSurfaceClientAddress")); }
			if (client != NULL) {
				uint64_t rows = (uint64_t)s->bytesPerRow * sweet_div_up(s->height, s->elementHeight), plane_end = 0;
				for (uint32_t i = 0; i < s->planeCount; i++) {
					if ((uint64_t)s->planes[i].offset + s->planes[i].size > plane_end) {
						plane_end = (uint64_t)s->planes[i].offset + s->planes[i].size;
					}
				}
				uint64_t address = client->unsigned64BitValue();
				uint64_t used = sweet_client_range_bytes(address, s->offset, rows, plane_end, s->allocSize);
				int err = used == 0 ? EINVAL : copyin((user_addr_t)address, mem->getBytesNoCopy(), (size_t)used);
				static uint32_t told;
				if (told++ < 6) {
					char name[32];
					proc_selfname(name, sizeof(name));
					IOLog("SWEET SURF: %s create from client memory %#llx (%llu of %u bytes) %ux%u fmt 0x%x -> %s\n",
					    name, address, used, s->allocSize, s->width, s->height, s->pixelFormat, err ? "refused" : "snapshot copied");
				}
				if (err) {
					OSAddAtomic64(-(SInt64)s->allocSize, (volatile SInt64 *)&sweet_surface_wired);
					OSDecrementAtomic((volatile SInt32 *)&sweet_surface_buffers);
					mem->release();
					mem = NULL;
					ret = kIOReturnBadArgument;
				}
			}
			if (mem != NULL) {
			s->memory = mem;
			s->mapCache = kIOMapDefaultCache;
			if (!sweet_surface_publish(s)) {
				ret = kIOReturnNoResources;
			}
			}
		}
	}
	if (ret != kIOReturnSuccess || sweet_surface_creates_logged < 64) {
		char name[32];
		proc_selfname(name, sizeof(name));
		sweet_surface_creates_logged++;
		IOLog("SWEET SURF: %s create id %u %ux%u fmt 0x%x bpe %u bpr %u planes %u size %u keys %u -> 0x%x\n",
		    name, s->id, s->width, s->height, s->pixelFormat, s->bytesPerElement,
		    s->bytesPerRow, s->planeCount, s->allocSize, props->getCount(), ret);
	}
	props->release();
	if (ret != kIOReturnSuccess) {
		s->release();
		return ret;
	}
	ret = returnSurface(s, args);   /* takes the creator's user count */
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::externalMethod(uint32_t selector, IOExternalMethodArguments *args,
    __unused IOExternalMethodDispatch *dispatch, __unused OSObject *target,
    __unused void *reference)
{
	uint64_t arg0 = args->scalarInputCount > 0 ? args->scalarInput[0] : 0;

	switch (selector) {
	case kSweetSurfaceLimits: {
		uint8_t limits[0x18] = { 0 };
		put32(limits, 0x0, SWEET_SURFACE_ROW_ALIGN - 1);
		put32(limits, 0x4, SWEET_SURFACE_ROW_ALIGN - 1);
		limits[0x16] = 1;
		sweet_surf_log_once("sel", selector);
		return sweet_struct_out(args, limits, sizeof(limits));
	}
	case kSweetSurfaceCreate:
		return create(args);
	case kSweetSurfaceLookup: {
		SweetSurface *s = sweet_surface_copy((uint32_t)arg0);
		IOReturn ret = kIOReturnNotFound;
		if (s != NULL) {
			ret = sweet_surface_get(s) ? returnSurface(s, args) : kIOReturnNotFound;
			s->release();
		}
		sweet_surf_log_once("sel", selector);
		return ret;
	}
	case kSweetSurfaceLookupFromPort: {
		OSObject *obj = NULL;
		IOReturn ret = IOUserClient::copyObjectForPortNameInTask(fTask, (mach_port_name_t)arg0, &obj);
		SweetSurface *s = OSDynamicCast(SweetSurface, obj);
		if (ret == kIOReturnSuccess) {
			ret = (s != NULL && sweet_surface_get(s)) ? returnSurface(s, args) : kIOReturnBadArgument;
		}
		OSSafeReleaseNULL(obj);
		sweet_surf_log_once("sel", selector);
		return ret;
	}
	case kSweetSurfaceCreatePort: {
		SweetSurface *s = sweet_surface_copy((uint32_t)arg0);
		if (s == NULL || args->scalarOutputCount < 1) {
			OSSafeReleaseNULL(s);
			return kIOReturnNotFound;
		}
		args->scalarOutput[0] = sweet_surface_port_in_task(s, fTask);
		args->scalarOutputCount = 1;
		s->release();
		sweet_surf_log_once("sel", selector);
		return kIOReturnSuccess;
	}
	default:
		break;
	}
	/* Everything that is not implemented above has a deliberate answer (sweet_surface_abi.h). */
	sweet_surf_log_once("sel", selector);
	switch (sweet_selector_policy(selector)) {
	case SWEET_POLICY_UNSUPPORTED:
		/* IOSurfaceWrapClientImage/Memory: failing makes CoreAnimation upload the pixels itself. */
		return kIOReturnUnsupported;
	case SWEET_POLICY_NOT_FOUND:
		return kIOReturnNotFound;
	case SWEET_POLICY_PURGEABLE: {
		SweetSurface *s = args->scalarInputCount >= 2 ? sweet_surface_copy((uint32_t)arg0) : NULL;
		if (s == NULL) {
			return args->scalarInputCount >= 2 ? kIOReturnNotFound : kIOReturnBadArgument;
		}
		IOLockLock(sweet_surface_lock);
		uint32_t old = sweet_purgeable_update(&s->purgeable, (uint32_t)args->scalarInput[1]);
		IOLockUnlock(sweet_surface_lock);
		if (args->scalarOutputCount >= 1) {
			args->scalarOutput[0] = old;
			args->scalarOutputCount = 1;
		}
		s->release();
		return kIOReturnSuccess;
	}
	case SWEET_POLICY_UNKNOWN: {
		/* Unidentified selector (seen: 40, 41, 43): tolerated as before; log its shape once so it can be named. */
		static uint64_t shape_logged;
		if (selector < 64 && !(shape_logged & (1ULL << selector))) {
			shape_logged |= 1ULL << selector;
			char name[32];
			proc_selfname(name, sizeof(name));
			IOLog("SWEET SURF: %s unidentified selector %u: scalars in %u out %u, struct in %u out %u, arg0 %llx -> accepted, zeroed output\n",
			    name, selector, args->scalarInputCount, args->scalarOutputCount, (uint32_t)args->structureInputSize,
			    (uint32_t)args->structureOutputSize, (unsigned long long)arg0);
		}
		break;
	}
	default:                /* SWEET_POLICY_ACCEPT: known, no effect */
		break;
	}
	for (uint32_t i = 0; i < args->scalarOutputCount; i++) {
		args->scalarOutput[i] = 0;
	}
	if (args->structureOutput != NULL) {
		bzero(args->structureOutput, args->structureOutputSize);
	}
	return kIOReturnSuccess;
}

IOReturn
IOSurfaceRootUserClient::trapUseCount(__unused void *id, __unused void *category,
    void *, void *, void *, void *)
{
	return kIOReturnSuccess;
}

static IOReturn
sweet_surface_seed_out(SweetSurface *s, void *seedOut, bool bump)
{
	uint32_t seed = bump ? OSIncrementAtomic((volatile SInt32 *)sweet_surface_seed(s)) + 1 :
	    *sweet_surface_seed(s);
	if (seedOut != NULL && copyout(&seed, (user_addr_t)seedOut, sizeof(seed)) != 0) {
		return kIOReturnBadArgument;
	}
	return kIOReturnSuccess;
}

/* Retained surface this client created or looked up, or NULL. */
static SweetSurface *
sweet_surface_held(IOLock *lock, OSArray *refs, uint32_t id)
{
	SweetSurface *s = NULL;
	IOLockLock(lock);
	for (unsigned int i = 0; i < refs->getCount(); i++) {
		SweetSurfaceRef *ref = (SweetSurfaceRef *)refs->getObject(i);
		if (ref->surface->id == id) {
			s = ref->surface;
			s->retain();
			break;
		}
	}
	IOLockUnlock(lock);
	return s;
}

extern "C" __attribute__((weak)) void nord2_gpu_surface_cpu_acquire(void *) {}
extern "C" __attribute__((weak)) void nord2_gpu_surface_cpu_release(void *) {}
extern "C" __attribute__((weak)) bool nord2_gpu_surface_busy(void *) { return false; }
extern "C" __attribute__((weak)) uint64_t nord2_gpu_resv_expired(void) { return 0; }

IOReturn
IOSurfaceRootUserClient::trapLock(void *id, __unused void *options, void *seedOut,
    void *, void *, void *)
{
	SweetSurface *s = sweet_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = sweet_surface_seed_out(s, seedOut, false);
	nord2_gpu_surface_cpu_acquire(s);       /* GPU-written lines of this surface must not satisfy the CPU's reads */
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapUnlock(void *id, void *options, void *seedOut,
    void *, void *, void *)
{
	SweetSurface *s = sweet_surface_held(fLock, fRefs, (uint32_t)(uintptr_t)id);
	if (s == NULL) {
		return kIOReturnNotFound;
	}
	IOReturn ret = sweet_surface_seed_out(s, seedOut,
	    !((uintptr_t)options & kIOSurfaceLockReadOnly));
	if (!((uintptr_t)options & kIOSurfaceLockReadOnly)) {
		nord2_gpu_surface_cpu_release(s);       /* the CPU wrote it: clean before the GPU's next read */
	}
	s->release();
	return ret;
}

IOReturn
IOSurfaceRootUserClient::trapRelease(void *id, void *, void *, void *, void *, void *)
{
	unsigned int index;
	IOLockLock(fLock);
	SweetSurfaceRef *ref = findRef((uint32_t)(uintptr_t)id, &index);
	if (ref != NULL && --ref->count == 0) {
		fRefs->removeObject(index);     /* unmaps and drops the user count */
	}
	IOLockUnlock(fLock);
	return ref != NULL ? kIOReturnSuccess : kIOReturnNotFound;
}

static IOExternalTrap sweet_surface_traps[] = {
	[kSweetSurfaceTrapIncrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUseCount },
	[kSweetSurfaceTrapDecrementUse]  = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUseCount },
	[kSweetSurfaceTrapLock]          = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapLock },
	[kSweetSurfaceTrapUnlock]        = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapUnlock },
	[kSweetSurfaceTrapRelease]       = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
	[kSweetSurfaceTrapReleaseFailed] = { NULL, (IOTrap) & IOSurfaceRootUserClient::trapRelease },
};

IOExternalTrap *
IOSurfaceRootUserClient::getTargetAndTrapForIndex(IOService **targetP, UInt32 index)
{
	if (index >= sizeof(sweet_surface_traps) / sizeof(sweet_surface_traps[0])) {
		sweet_surf_log_once("trap", index);
		return NULL;
	}
	sweet_surf_log_once("trap", index);
	*targetP = this;
	return &sweet_surface_traps[index];
}
#endif /* SWEET */
