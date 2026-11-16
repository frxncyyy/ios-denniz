/*
 * sweet_m2_req.h -- 0685: decoder of the IOSurfaceAccelerator transform request (selector 1, 0x170 bytes), pure C (host-tested with
 * the 16 mediaserverd requests captured on q10jmdp14, gpu/build/mdp-20261114/test/t_m2_req.c).
 *
 * Layout [V-dsc 20H392 IOSurfaceAccelerator: convertToTransform 0x1d3c21a8c, prepareTransformRectangles 0x1d3c22be0,
 * prepareTransformBuffersAndOptions 0x1d3c21d28; values seen in the phone's requests]:
 *   +0x00 u32  source IOSurfaceID              +0x04 u32  destination IOSurfaceID
 *   +0x08 u64  completion callback (0 = synchronous)   +0x10 u64 refcon   +0x18 u64 (third completion word)
 *   +0x20 u64  flags: bits 0-4 "Transform" (0 none, bit 0 = horizontal flip [I], 0x4 rot 90, 0x8 rot 180, 0x10 rot 270),
 *              0x40 CustomFilter, 0x80 NearestFilter, 0x100 OptOutBlitClip, 0x200 LockInScaler, 0x400 SkipAsyncCallback,
 *              0x1000 FixUpscaling, 0x2000 set by prepareTransformBuffersAndOptions (DitherControl field, bits 13+),
 *              UnwireSurface << 35; bit 30 seen set by mediaserverd [I: not decoded, treated as a hint]
 *   +0x28 u64  source x (16.16)   +0x30 u64 source y (16.16)   +0x38 u64 source width (16.16)   +0x40 u64 source height (16.16)
 *   +0x48..+0x57  zero in every request seen (option words)
 *   +0x58 u32  destination x   +0x5c u32 destination y   +0x60 u32 destination width   +0x64 u32 destination height (pixels)
 *   +0x68..+0x16f options (alpha / border fill / histogram / custom filter ...): must be zero for us
 *   all-zero rectangles = whole surfaces (the old IOSurfaceAcceleratorTransferSurface path)
 * mediaserverd (Magnifier preview, q10jmdp14): flags 0x40003004 = rot 90 + FixUpscaling + 0x2000 + bit 30, src rect 0,0 2048x1536
 * (or 1504x1128), dst rect 0,0 1536x2048 (1128x1504) = whole-frame rotation.
 */
#ifndef SWEET_M2_REQ_H
#define SWEET_M2_REQ_H
#include <stdint.h>

enum {
	SWEET_M2_OK = 0, SWEET_M2_R_SHORT, SWEET_M2_R_ASYNC, SWEET_M2_R_FLAGS, SWEET_M2_R_TRANSFORM, SWEET_M2_R_FRACT, SWEET_M2_R_OPTIONS,
	SWEET_M2_R_RECT, SWEET_M2_R_N
};
static const char *const sweet_m2_reason[SWEET_M2_R_N] = { "accepted", "short struct", "asynchronous completion", "flags",
	"transform", "fractional source rectangle", "options", "rectangle sizes" };

/* flag bits we accept as quality hints (they change nothing we can do): FixUpscaling, the 0x2000 word, SkipAsyncCallback,
 * NearestFilter (we filter anyway), OptOutBlitClip, LockInScaler, bit 30 */
#define SWEET_M2_FLAGS_OK (0x1full | 0x80ull | 0x100ull | 0x200ull | 0x400ull | 0x1000ull | 0x2000ull | 0x40000000ull)

struct sweet_m2_req {
	uint32_t src, dst, rot, flip;
	uint64_t cb, flags;
	uint32_t sx, sy, sw, sh;        /* integer pixels; sw == 0: whole source */
	uint32_t dx, dy, dw, dh;        /* dw == 0: whole destination */
};

static inline uint64_t sweet_m2_u64(const uint8_t *p, uint32_t o) { uint64_t v = 0; for (int i = 7; i >= 0; i--) { v = (v << 8) | p[o + (uint32_t)i]; } return v; }
static inline uint32_t sweet_m2_u32(const uint8_t *p, uint32_t o) { return (uint32_t)p[o] | ((uint32_t)p[o + 1] << 8) | ((uint32_t)p[o + 2] << 16) | ((uint32_t)p[o + 3] << 24); }

static inline int
sweet_m2_decode(const uint8_t *p, uint32_t n, struct sweet_m2_req *r)
{
	if (n < 0x68) { return SWEET_M2_R_SHORT; }
	r->src = sweet_m2_u32(p, 0); r->dst = sweet_m2_u32(p, 4);
	r->cb = sweet_m2_u64(p, 8); r->flags = sweet_m2_u64(p, 0x20);
	if (r->cb != 0 && !(r->flags & 0x400ull)) { return SWEET_M2_R_ASYNC; }
	if (r->flags & ~SWEET_M2_FLAGS_OK) { return SWEET_M2_R_FLAGS; }
	uint32_t t = (uint32_t)(r->flags & 0x1fu);
	r->flip = t & 1u;
	switch (t & ~1u) {
	case 0: r->rot = 0; break;
	case 0x4: r->rot = 1; break;
	case 0x8: r->rot = 2; break;
	case 0x10: r->rot = 3; break;
	default: return SWEET_M2_R_TRANSFORM;
	}
	uint64_t s[4] = { sweet_m2_u64(p, 0x28), sweet_m2_u64(p, 0x30), sweet_m2_u64(p, 0x38), sweet_m2_u64(p, 0x40) };
	for (int i = 0; i < 4; i++) { if ((s[i] & 0xffffu) || (s[i] >> 32)) { return SWEET_M2_R_FRACT; } }
	r->sx = (uint32_t)(s[0] >> 16); r->sy = (uint32_t)(s[1] >> 16); r->sw = (uint32_t)(s[2] >> 16); r->sh = (uint32_t)(s[3] >> 16);
	for (uint32_t o = 0x48; o < 0x58; o += 4) { if (sweet_m2_u32(p, o)) { return SWEET_M2_R_OPTIONS; } }
	for (uint32_t o = 0x68; o + 4 <= n; o += 4) { if (sweet_m2_u32(p, o)) { return SWEET_M2_R_OPTIONS; } }
	r->dx = sweet_m2_u32(p, 0x58); r->dy = sweet_m2_u32(p, 0x5c); r->dw = sweet_m2_u32(p, 0x60); r->dh = sweet_m2_u32(p, 0x64);
	if ((r->sw == 0) != (r->sh == 0) || (r->dw == 0) != (r->dh == 0)) { return SWEET_M2_R_RECT; }
	if (r->sw == 0 && (r->sx | r->sy)) { return SWEET_M2_R_RECT; }
	if (r->dw == 0 && (r->dx | r->dy)) { return SWEET_M2_R_RECT; }
	return SWEET_M2_OK;
}
#endif
