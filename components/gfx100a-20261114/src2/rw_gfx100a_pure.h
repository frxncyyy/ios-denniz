/*
 * gfx100a (2026-11-14): the pure arithmetic of the gfx100a block of redwoodmetal.m, in a header the host test includes too.
 *
 * 1. XR ALIAS. Apple's extended-range formats 550-555 (BGRA10_XR, BGR10_XR and their _sRGB forms, plus the two private 'b3a8'
 *    ones) and 94 (BGR10A2Unorm, 'l10r') have no texture in the compiler library, and Nord2Metal refused every one by name
 *    ("no drawable: pixel format 554 is a ten-bit XR format ..."). The kernel already makes their surfaces as ordinary BGRA of the
 *    same geometry (MiniIOSurface.cpp: 'w30r', 'l10r', 'w30R', 'b3a8' -> 'BGRA', 4 bytes a pixel), and everyone reads the format
 *    back from the surface. So a texture of such a format is served as BGRA8Unorm (80) -- or BGRA8Unorm_sRGB (81) for the
 *    _sRGB ones -- which is exactly what lies in the surface's memory. What is lost is the range above 1.0 and the two extra bits
 *    a channel: this panel shows neither. Seen refusing: com.apple.WebKit.GPU (perf1a: Safari's page stays black), backboardd
 *    (night7, recov4: 554 over 1125x2436 and 281x609 surfaces = screen snapshots for the app switcher and the blurred home).
 *    552/553 over a surface the kernel did NOT substitute ('w40a', 8 bytes a pixel) still fail the FourCC check, by name.
 *
 * 2. ASTC EDGE. A copy into the layout of a block-compressed texture whose region ends inside the last, partial block row or
 *    column (ImageIO writes whole blocks: "region 0,1704 787x4 of 787x1705") was refused as "a region that leaves the level", and
 *    the last block row of every switcher snapshot was left empty. The region is clamped to the level's edge, which names the same
 *    blocks (rwtl_region rounds the edge up again).
 */
#ifndef RW_GFX100A_PURE_H
#define RW_GFX100A_PURE_H

#include <stdint.h>

/* the format a texture of `pf` is made as; `pf` itself when it is not one of the aliased ones */
static inline unsigned
rw_gfx_xr_alias_of(unsigned pf)
{
	switch (pf) {
	case 94:        /* MTLPixelFormatBGR10A2Unorm ('l10r' -> 'BGRA') */
	case 550:       /* private, 'b3a8' -> 'BGRA' */
	case 552:       /* MTLPixelFormatBGRA10_XR */
	case 554:       /* MTLPixelFormatBGR10_XR ('w30r' -> 'BGRA') */
		return 80;  /* MTLPixelFormatBGRA8Unorm */
	case 551:       /* private, 'b3a8' -> 'BGRA' */
	case 553:       /* MTLPixelFormatBGRA10_XR_sRGB */
	case 555:       /* MTLPixelFormatBGR10_XR_sRGB */
		return 81;  /* MTLPixelFormatBGRA8Unorm_sRGB */
	default:
		return pf;
	}
}

/*
 * A region (x, w) along one axis of a level `size` texels long, in blocks of `block`: if it ends past the level's edge but not past
 * the edge rounded up to a whole block, its length is cut to end at the edge. 1 when it was cut, 0 when it is left as it was
 * (inside the level already, or out of it by more than the partial block -- which stays refused).
 */
static inline int
rw_gfx_edge_clamp(uint64_t x, uint64_t *w, uint64_t size, uint32_t block)
{
	uint64_t rounded;

	if (block == 0 || *w == 0 || x >= size || x + *w <= size) {
		return 0;
	}
	rounded = (size + block - 1) / block * block;
	if (x + *w > rounded) {
		return 0;
	}
	*w = size - x;
	return 1;
}

#endif
