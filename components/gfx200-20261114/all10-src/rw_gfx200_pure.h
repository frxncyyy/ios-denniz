/*
 * gfx200 (perf, 14 Nov 2026): the decisions of Nord2Metal's gfx200 block that need no Objective-C, Metal or Mesa, so the host
 * test (rw_gfx200_host_test.c) runs exactly what the driver runs. SPDX-License-Identifier: MIT.
 */
#ifndef RW_GFX200_PURE_H
#define RW_GFX200_PURE_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Metal's numbers (MTLTextureType, MTLTextureUsage, MTLLoadAction, MTLStoreAction) */
#define RWG2_TYPE_2D          2u
#define RWG2_TYPE_2D_MS       4u
#define RWG2_USAGE_RT         0x4u
#define RWG2_USAGE_PFV        0x10u
#define RWG2_LOAD_LOAD        1u
#define RWG2_STORE_DONTCARE   0u
#define RWG2_STORE_RESOLVE    2u

struct rwg2_tex {            /* what the decisions read of a texture */
	unsigned pixel_format, type, width, height, depth, array_length, mip_levels, sample_count, usage;
	bool plain;              /* ours, made by -newTextureWithDescriptor: or over a surface, not a view / buffer texture */
	bool has_mesa;           /* has a library texture */
};

/*
 * MULTISAMPLE RENDER-TO-TEXTURE: may colour attachment `from` (the pass's multisample texture, drawn at level `level` slice
 * `slice`, load action `load`, FINAL store action `store`) be replaced in every draw of its pass by its resolve texture `to`
 * (resolve level/slice `rlevel`/`rslice`)? Only for MTLStoreActionMultisampleResolve (the multisample contents are then
 * undefined after the pass, so they never need to exist in memory) with a load action that does not read them (Clear or
 * DontCare), between a 4-sample 2D multisample texture and a 1-sample 2D render-target texture of the same format and size,
 * both at level 0 / slice 0. Returns 0 (yes) or a reason code > 0.
 */
enum {
	RWG2_RTT_OK = 0, RWG2_RTT_NOT_RESOLVE = 1, RWG2_RTT_LOADS = 2, RWG2_RTT_NOT_OURS = 3, RWG2_RTT_SHAPE = 4,
	RWG2_RTT_SIZE = 5, RWG2_RTT_FORMAT = 6, RWG2_RTT_LEVEL = 7, RWG2_RTT_NOT_RT = 8, RWG2_RTT_NO_DRAWS = 9,
};
static inline int
rwg2_rtt_ok(const struct rwg2_tex *from, const struct rwg2_tex *to, unsigned load, unsigned store, unsigned level,
	    unsigned slice, unsigned rlevel, unsigned rslice, unsigned ndraws)
{
	if (store != RWG2_STORE_RESOLVE)
		return RWG2_RTT_NOT_RESOLVE;
	if (load == RWG2_LOAD_LOAD)
		return RWG2_RTT_LOADS;
	if (ndraws == 0)
		return RWG2_RTT_NO_DRAWS;
	if (!from || !to || !from->plain || !to->plain || !from->has_mesa || !to->has_mesa)
		return RWG2_RTT_NOT_OURS;
	if (from->type != RWG2_TYPE_2D_MS || from->sample_count != 4 || to->type != RWG2_TYPE_2D || to->sample_count > 1 ||
	    from->mip_levels > 1 || from->array_length > 1 || to->array_length > 1 || (from->depth > 1) || (to->depth > 1))
		return RWG2_RTT_SHAPE;
	if (from->width != to->width || from->height != to->height)
		return RWG2_RTT_SIZE;
	if (from->pixel_format != to->pixel_format)
		return RWG2_RTT_FORMAT;
	if (level || slice || rlevel || rslice)
		return RWG2_RTT_LEVEL;
	if (!(to->usage & RWG2_USAGE_RT))
		return RWG2_RTT_NOT_RT;
	return RWG2_RTT_OK;
}

/* the word a draw carries in rwa_draw.bc_pad (air_metal.h): 0, or the tag and the flags */
static inline unsigned
rwg2_bcpad(unsigned rtt_mask, bool z_dontcare, bool s_dontcare)
{
	unsigned f = (rtt_mask & 0xffu) | (z_dontcare ? 0x1000u : 0u) | (s_dontcare ? 0x2000u : 0u);

	return f ? (0x5254u << 16) | f : 0u;
}

/*
 * THE RENDER-TARGET POOL: a texture made for nothing but being drawn into (usage RenderTarget, optionally PixelFormatView: no
 * shader read or write, so nothing can ever see what it held before) and big (multisample, or >= 4 MiB): kept when let go and
 * handed to the next request of the very same shape once the GPU is done with it. Maps makes and frees a 4-sample full-screen
 * colour and depth/stencil texture pair 15 times a second (perf1a: 75 + 76 per 5 s, 1874 ms of the render thread in texture
 * creation per 5 s).
 */
static inline size_t
rwg2_tex_bytes(const struct rwg2_tex *s)
{
	size_t px = (size_t)s->width * (s->height ? s->height : 1) * (s->depth ? s->depth : 1) *
		    (s->array_length ? s->array_length : 1) * (s->sample_count ? s->sample_count : 1);

	return px * 4 * (s->mip_levels > 1 ? 4 : 3) / 3;   /* ~4 bytes a sample, mips +1/3 (an estimate for the budget only) */
}

static inline bool
rwg2_rtpool_eligible(const struct rwg2_tex *s)
{
	if (!(s->usage & RWG2_USAGE_RT) || (s->usage & ~(RWG2_USAGE_RT | RWG2_USAGE_PFV)) != 0)
		return false;
	return s->sample_count > 1 || rwg2_tex_bytes(s) >= (4u << 20);
}

static inline bool
rwg2_same_shape(const struct rwg2_tex *a, const struct rwg2_tex *b)
{
	return a->pixel_format == b->pixel_format && a->type == b->type && a->width == b->width && a->height == b->height &&
	       a->depth == b->depth && a->array_length == b->array_length && a->mip_levels == b->mip_levels &&
	       a->sample_count == b->sample_count && a->usage == b->usage;
}
#endif
