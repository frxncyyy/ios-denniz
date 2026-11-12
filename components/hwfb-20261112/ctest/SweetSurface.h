/*
 * Built-in IOSurface for SWEET (see SweetSurface.cpp).
 *
 * Shared with SweetFramebuffer.cpp: the framebuffer looks surfaces up by
 * id when a swap names them and publishes the panel memory itself as the
 * default framebuffer surface.
 */
#ifndef _SWEET_SURFACE_H
#define _SWEET_SURFACE_H

#include <pexpert/arm64/board_config.h>

#if defined(SWEET)
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/c++/OSObject.h>
#include <mach/mach_types.h>

#define SWEET_SURFACE_MAX_PLANES        20

struct sweet_surface_plane {
	uint32_t width, height, bytesPerRow, offset, size;
	uint16_t bytesPerElement;
	uint8_t  elementWidth, elementHeight;
};

class SweetSurface : public OSObject {
	OSDeclareDefaultStructors(SweetSurface);

public:
	uint32_t id;
	uint32_t width, height, bytesPerRow, offset, pixelFormat, allocSize;
	uint16_t bytesPerElement;
	uint8_t  elementWidth, elementHeight;
	uint32_t cacheMode, planeCount, parentID;
	struct sweet_surface_plane planes[SWEET_SURFACE_MAX_PLANES];

	IOMemoryDescriptor       *memory;       /* pixels (buffer or physical) */
	IOOptionBits              mapCache;     /* cache mode for user mappings */
	IOBufferMemoryDescriptor *state;        /* shared state page, read-only for users */
	IOMemoryMap              *kernelMap;    /* lazily created for kernel copies */
	uint32_t                  purgeable;    /* IOSurfaceSetPurgeable state (0 = non-volatile) */

	bool init() APPLE_KEXT_OVERRIDE { return OSObject::init(); }
	void free() APPLE_KEXT_OVERRIDE;

	/* Kernel address of the pixels (mapped on first use), or 0. */
	uintptr_t kernelAddress();
};

/* Retained surface for an id, or NULL. */
SweetSurface *sweet_surface_copy(uint32_t id);

/*
 * CPU access protocol for the GPU driver's cache tracking (Nord2GPU, tracking level 2).  acquire: the CPU is
 * about to read the pixels (IOSurfaceLock, framebuffer copy); release: the CPU has written them (IOSurfaceUnlock
 * without kIOSurfaceLockReadOnly).  No-ops unless the GPU driver provides the strong definitions.
 */
extern "C" void nord2_gpu_surface_cpu_acquire(void *surface);
extern "C" void nord2_gpu_surface_cpu_release(void *surface);
/* True while a GPU job that declared it writes this surface has not finished (the framebuffer then keeps the previous frame). */
extern "C" bool nord2_gpu_surface_busy(void *surface);
/* 0148 (diagnostic): why a surface is busy: jobs in flight that list it (count, oldest age in us, its id and state), all jobs in flight / queued. */
extern "C" void nord2_gpu_surface_busy_why(void *surface, uint32_t *match, uint64_t *oldest_age_us, uint64_t *oldest_id, uint32_t *oldest_state,
    uint32_t *inflight, uint32_t *queued, uint32_t *resv);
/* 0112: reservations that expired unretired since boot (a frame that never got submitted, or a client that forgot to retire). */
extern "C" uint64_t nord2_gpu_resv_expired(void);

/*
 * Publish physical memory (the panel framebuffer) as a surface. Returns a
 * retained surface that stays in the id table until released by the caller
 * and every user.
 */
SweetSurface *sweet_surface_create_physical(uint64_t phys, uint32_t width,
    uint32_t height, uint32_t bytesPerRow, uint32_t pixelFormat);

/* Send right for the surface in task (IOSurfaceLookupFromMachPort). */
mach_port_name_t sweet_surface_port_in_task(SweetSurface *surface, task_t task);

#endif /* SWEET */
#endif /* _SWEET_SURFACE_H */
