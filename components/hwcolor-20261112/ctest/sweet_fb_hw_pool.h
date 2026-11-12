/*
 * 0394 (hwcomp-20261112, TRACK A): the display surface pool -- INTERFACE CONTRACT shared by the HW-composition tracks.
 *
 * Master copy: gpu/build/hwcomp-20261112/sweet_fb_hw_pool.h.  Installed by patch 0394 as iokit/Kernel/arm/sweet_fb_hw_pool.h
 * (byte-identical; pool-0394/make_patch.sh checks it).  Pure C/C++, no kernel headers: usable from Nord2GPU.cpp,
 * SweetFramebuffer.cpp, bsd/dev C files and host tests.
 *
 * The pool is 256 MiB of low DRAM (PA 0xC0000000..0xD0000000) that XNU never owns as RAM (XNU's RAM is 0x1_42000000 + 3776 MiB,
 * all above 4 GiB).  The display OVL (32-bit Ln_ADDR, larb MMU off) and the Mali GPU can both address it.  Large BGRA surfaces
 * are carved from it as physically contiguous IOSurfaces when sysctl kern.n2fbhw_pool = 1 (boot-arg n2fbhw_pool or n2fp, default 0).
 *
 * Cache attribute of EVERY CPU mapping of the pool (kernel and user) is chosen once per boot (boot-arg n2fbpoolc):
 *   0 (default) = SWEET_FB_HW_POOL_CACHE_WC:       Normal non-cacheable (write-combined).  CPU writes reach DRAM after a DSB,
 *                                                  no CPU cache maintenance is ever needed for the display or the GPU; CPU
 *                                                  reads are uncached (slow).
 *   1           = SWEET_FB_HW_POOL_CACHE_COPYBACK: Normal write-back cacheable.  The usual IOSurface rules apply: the CPU writer
 *                                                  must clean (IOSurfaceUnlock -> nord2_gpu_surface_cpu_release) before a
 *                                                  non-coherent reader (GPU, OVL) reads; readers after a GPU write invalidate.
 * The two are never mixed for the same PA (no mismatched-attribute aliases); XNU's physmap has no alias of the pool.
 */
#ifndef SWEET_FB_HW_POOL_H
#define SWEET_FB_HW_POOL_H

#include <stdint.h>
#include <stdbool.h>

#define SWEET_FB_HW_POOL_BASE           0xC0000000ull
#define SWEET_FB_HW_POOL_SIZE           0x10000000ull   /* 256 MiB */
#define SWEET_FB_HW_POOL_GRANULE        0x10000ull      /* 64 KiB: allocation unit and alignment of every pool surface */

#define SWEET_FB_HW_POOL_CACHE_NONE     0u              /* not a pool surface */
#define SWEET_FB_HW_POOL_CACHE_WC       1u
#define SWEET_FB_HW_POOL_CACHE_COPYBACK 2u

/* True iff [pa, pa + len) lies inside the pool and len > 0.  Pure range check: says nothing about allocation state. */
static inline bool
sweet_fb_hw_pool_contains(uint64_t pa, uint64_t len)
{
	return len != 0 && pa >= SWEET_FB_HW_POOL_BASE && len <= SWEET_FB_HW_POOL_SIZE &&
	       pa - SWEET_FB_HW_POOL_BASE <= SWEET_FB_HW_POOL_SIZE - len;
}

#ifdef __cplusplus
extern "C" {
#endif
/*
 * Defined in SweetSurface.cpp (0394).  By surface id (looked up in the IOSurface table; 0 / NONE for a missing or non-pool id):
 *   pool_pa     PA of the surface base = pixel (0,0) (pool surfaces always have IOSurfaceOffset 0); below 4 GiB, 64 KiB aligned.
 *   pool_stride bytes per row (multiple of 64).
 *   pool_size   bytes of the contiguous allocation starting at pool_pa (>= stride * height, multiple of 64 KiB).
 *   pool_cache  SWEET_FB_HW_POOL_CACHE_* of all CPU mappings of it.
 * The *_obj variants take a SweetSurface * the caller already holds (use these when the caller keeps the object: an id can be
 * reused once the surface left the table while a kernel holder still retains the old object).
 * A pool surface's memory goes back to the pool in SweetSurface::free(), i.e. when the LAST retain is dropped: whoever lets the
 * OVL or the GPU read it (scanout until FRAME_DONE, a GPU job) must hold a retain on the SweetSurface for that whole time.
 */
uint64_t sweet_surface_pool_pa(uint32_t surface_id);
uint32_t sweet_surface_pool_stride(uint32_t surface_id);
uint64_t sweet_surface_pool_size(uint32_t surface_id);
uint32_t sweet_surface_pool_cache(uint32_t surface_id);
uint64_t sweet_surface_pool_pa_obj(const void *surface);
uint32_t sweet_surface_pool_stride_obj(const void *surface);
uint64_t sweet_surface_pool_size_obj(const void *surface);
uint32_t sweet_surface_pool_cache_obj(const void *surface);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
class SweetSurface;
/*
 * Kernel-side pool surface (e.g. H2 test patterns): width x height 'BGRA', bytesPerRow = round_up(width * 4, 64), zeroed,
 * published in the id table like sweet_surface_create_physical().  Ignores kern.n2fbhw_pool (the caller asked for the pool
 * explicitly).  NULL when the pool is full or the size is out of range.  Returned retained (caller releases).
 */
SweetSurface *sweet_surface_create_pool(uint32_t width, uint32_t height);
#endif

#endif /* SWEET_FB_HW_POOL_H */
