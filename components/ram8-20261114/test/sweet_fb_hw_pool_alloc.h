/*
 * 0394: display pool allocator logic, pure (no kernel calls) so the host test (hwcomp-20261112/test/pool_host_test.c) runs
 * exactly this code.  Locking, mapping, zeroing and logging live in SweetSurface.cpp.
 *
 * Bitmap of 64 KiB granules (256 MiB / 64 KiB = 4096 bits = 512 bytes), first fit over contiguous runs.  A 1125x2436 BGRA
 * surface (pitch 4544, 11,069,184 bytes) takes 169 granules (6 KiB waste): 24 fit in an empty pool, as with 10.56 MiB slots,
 * but any other size fits too.
 */
#ifndef SWEET_FB_HW_POOL_ALLOC_H
#define SWEET_FB_HW_POOL_ALLOC_H

#include "sweet_fb_hw_pool.h"

#define SWEET_FB_POOL_NGRAN     ((uint32_t)(SWEET_FB_HW_POOL_SIZE / SWEET_FB_HW_POOL_GRANULE))     /* 4096 */
#define SWEET_FB_POOL_WORDS     (SWEET_FB_POOL_NGRAN / 64)
#define SWEET_FB_POOL_FAIL      (~0ull)
#define SWEET_FB_POOL_BGRA      0x42475241u     /* 'BGRA' = memory B,G,R,A = DRM ARGB8888 = OVL CFMT 2/3 */
#define SWEET_FB_POOL_MIN_PIXELS (1000ull * 2000ull)   /* "full screen or larger" (the panel is 1080x2400, CA's canvas 1125x2436) */
#define SWEET_FB_POOL_MAX_PITCH (1u << 20)      /* OVL Ln_PITCH 16 bits + Ln_PITCH_MSB 4 bits */

/* Why a surface does not go to the pool (index into the per-reason counters). */
enum {
	SWEET_FB_POOL_OK = 0,
	SWEET_FB_POOL_NO_OFF,           /* kern.n2fbhw_pool == 0 (SweetSurface.cpp: only for surfaces that are otherwise eligible) */
	SWEET_FB_POOL_NO_FORMAT,        /* not 'BGRA' with 4-byte 1x1 elements */
	SWEET_FB_POOL_NO_PLANES,        /* planar */
	SWEET_FB_POOL_NO_SMALL,         /* fewer than 1000*2000 pixels */
	SWEET_FB_POOL_NO_STRIDE,        /* bytesPerRow not a multiple of 64, < width*4 or >= 1 MiB */
	SWEET_FB_POOL_NO_OFFSET,        /* IOSurfaceOffset != 0 */
	SWEET_FB_POOL_NO_SIZE,          /* allocation larger than the pool or smaller than the pixels */
	SWEET_FB_POOL_NREASONS
};

static inline uint32_t
sweet_fb_pool_eligible(uint32_t enabled, uint32_t width, uint32_t height, uint32_t bytesPerRow, uint32_t bytesPerElement,
    uint32_t elementWidth, uint32_t elementHeight, uint32_t pixelFormat, uint32_t planeCount, uint32_t offset, uint64_t allocSize)
{
	if (!enabled) {
		return SWEET_FB_POOL_NO_OFF;
	}
	if (pixelFormat != SWEET_FB_POOL_BGRA || bytesPerElement != 4 || elementWidth != 1 || elementHeight != 1) {
		return SWEET_FB_POOL_NO_FORMAT;
	}
	if (planeCount != 0) {
		return SWEET_FB_POOL_NO_PLANES;
	}
	if ((uint64_t)width * height < SWEET_FB_POOL_MIN_PIXELS) {
		return SWEET_FB_POOL_NO_SMALL;
	}
	if ((bytesPerRow & 63u) != 0 || (uint64_t)bytesPerRow < (uint64_t)width * 4 || bytesPerRow >= SWEET_FB_POOL_MAX_PITCH) {
		return SWEET_FB_POOL_NO_STRIDE;
	}
	if (offset != 0) {
		return SWEET_FB_POOL_NO_OFFSET;
	}
	if (allocSize > SWEET_FB_HW_POOL_SIZE || allocSize < (uint64_t)bytesPerRow * height) {
		return SWEET_FB_POOL_NO_SIZE;
	}
	return SWEET_FB_POOL_OK;
}

struct sweet_fb_pool {
	uint64_t bits[SWEET_FB_POOL_WORDS];     /* 1 = granule in use */
	uint32_t used, used_peak;               /* granules */
	uint32_t live, live_peak;               /* allocations */
	uint64_t allocs, frees, full, bad_free;
};

static inline uint32_t
sweet_fb_pool_granules(uint64_t bytes)
{
	return (uint32_t)((bytes + SWEET_FB_HW_POOL_GRANULE - 1) / SWEET_FB_HW_POOL_GRANULE);
}

static inline bool
sweet_fb_pool_test(const struct sweet_fb_pool *p, uint32_t g)
{
	return (p->bits[g >> 6] >> (g & 63)) & 1;
}

/* Longest run of free granules (for logs). */
static inline uint32_t
sweet_fb_pool_largest_free(const struct sweet_fb_pool *p)
{
	uint32_t best = 0, run = 0;
	for (uint32_t g = 0; g < SWEET_FB_POOL_NGRAN; g++) {
		if (sweet_fb_pool_test(p, g)) {
			run = 0;
		} else if (++run > best) {
			best = run;
		}
	}
	return best;
}

/* First fit: byte offset of the allocation inside the pool, or SWEET_FB_POOL_FAIL (counted as full). */
static inline uint64_t
sweet_fb_pool_alloc(struct sweet_fb_pool *p, uint64_t bytes)
{
	uint32_t n = sweet_fb_pool_granules(bytes);
	if (bytes == 0 || bytes > SWEET_FB_HW_POOL_SIZE || n > SWEET_FB_POOL_NGRAN - p->used) {
		p->full++;
		return SWEET_FB_POOL_FAIL;
	}
	uint32_t start = 0, run = 0;
	for (uint32_t g = 0; g < SWEET_FB_POOL_NGRAN; g++) {
		if ((g & 63) == 0 && p->bits[g >> 6] == ~0ull) {        /* whole word in use */
			run = 0;
			g += 63;
			continue;
		}
		if (sweet_fb_pool_test(p, g)) {
			run = 0;
			continue;
		}
		if (run++ == 0) {
			start = g;
		}
		if (run == n) {
			for (uint32_t k = start; k < start + n; k++) {
				p->bits[k >> 6] |= 1ull << (k & 63);
			}
			p->used += n;
			if (p->used > p->used_peak) {
				p->used_peak = p->used;
			}
			if (++p->live > p->live_peak) {
				p->live_peak = p->live;
			}
			p->allocs++;
			return (uint64_t)start * SWEET_FB_HW_POOL_GRANULE;
		}
	}
	p->full++;
	return SWEET_FB_POOL_FAIL;
}

/* Give back what sweet_fb_pool_alloc(bytes) returned at off.  A misaligned / out-of-range / not-allocated range changes nothing. */
static inline bool
sweet_fb_pool_free(struct sweet_fb_pool *p, uint64_t off, uint64_t bytes)
{
	uint32_t n = sweet_fb_pool_granules(bytes), g0 = (uint32_t)(off / SWEET_FB_HW_POOL_GRANULE);
	if (bytes == 0 || (off % SWEET_FB_HW_POOL_GRANULE) != 0 || off >= SWEET_FB_HW_POOL_SIZE ||
	    n > SWEET_FB_POOL_NGRAN - g0 || p->live == 0) {
		p->bad_free++;
		return false;
	}
	for (uint32_t k = g0; k < g0 + n; k++) {
		if (!sweet_fb_pool_test(p, k)) {
			p->bad_free++;
			return false;
		}
	}
	for (uint32_t k = g0; k < g0 + n; k++) {
		p->bits[k >> 6] &= ~(1ull << (k & 63));
	}
	p->used -= n;
	p->live--;
	p->frees++;
	return true;
}

/*
 * Canary in the first 16 bytes of every FREE granule (written at pool init and on free, checked on alloc): a mismatch means
 * something outside XNU (firmware, a stray DMA master) wrote into the pool (risk R9).  Detects writes that hit a granule head only.
 */
static inline uint64_t
sweet_fb_pool_canary(uint32_t granule)
{
	return 0x4e32504f4f4c0000ull ^ ((uint64_t)granule * 0x9e3779b97f4a7c15ull);
}

static inline void
sweet_fb_pool_canary_put(volatile uint64_t *head, uint32_t granule)
{
	head[0] = sweet_fb_pool_canary(granule);
	head[1] = ~sweet_fb_pool_canary(granule);
}

static inline bool
sweet_fb_pool_canary_ok(const volatile uint64_t *head, uint32_t granule)
{
	return head[0] == sweet_fb_pool_canary(granule) && head[1] == ~sweet_fb_pool_canary(granule);
}

#endif /* SWEET_FB_HW_POOL_ALLOC_H */
