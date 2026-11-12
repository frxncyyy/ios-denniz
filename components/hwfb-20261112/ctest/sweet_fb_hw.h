#ifndef SWEET_FB_HW_H
#define SWEET_FB_HW_H
/*
 * 0396 (hwfb-20261112): hardware composition of the SwapEnd layers by the display's overlay engines (Track C of the HW
 * composition work; 0394 = display pool in SweetSurface.cpp, 0395 = GPU acceptance of pool pages + write fences in Nord2GPU.cpp).
 * Pure and host-testable: no IOKit, no registers.  The driver (SweetFramebuffer.cpp) turns a swap into layer inputs, asks for a
 * plan here, turns the plan into register writes (sweet_fb_hw_regs), writes and reads them back on OVL0_2L (pipe 0, panel x
 * 0..539) and OVL1_2L (pipe 1, x 540..1079) and pushes the panel as before.  The CPU never reads or writes a pixel of a HW frame.
 * Starting point: the planner left by agent a647a03b in hwcomp-20261112 (reviewed: geometry kept, register list, test patterns,
 * reasons and the fault/race policy added).
 *
 * Hardware (gpu/hwcodec/DISPLAY_HW_COMPOSE.md section 1; MT6893 Android DRM driver mtk_disp_ovl.c):
 *   per layer n (stride 0x20): CON 0x30 ([7:0] plane alpha, 8 AEN, [15:12] CFMT, 23 CLRFMT_MAN, 24 BTSW, 25 RGB_SWAP), SRC_SIZE
 *   0x38 (h<<16|w), OFFSET 0x3c (y<<16|x inside the 540-wide ROI), PITCH_MSB 0x40, PITCH 0x44 ([15:0]), CLIP 0x4c, RDMA_CTRL 0xc0
 *   (bit 0), RDMA_GMC 0xc8, RDMA_FIFO 0xd0, ADDR 0xf40 (32 bit physical: the larb ports run with MMU_EN = 0 as LK left them);
 *   SRC_CON 0x2c bits 0/1 = layer 0/1 enable; ROI_BGCLR 0x28.  Layer 0 is the bottom one.  No shadow registers on this SoC:
 *   written while no transfer runs (between FRAME_DONE and the next DSI_START), which makes a frame's update atomic.
 *   Memory byte order B,G,R,A (CoreAnimation 'BGRA', = the panel buffers): CFMT 2.
 *
 * Semantics = the CPU path (sweet_fb_blend.h): the lowest visible layer is copied (its alpha ignored), every higher layer is
 * premultiplied source-over, panel pixels no layer covers are black.  So the bottom HW layer runs with AEN = 0 (opaque) over a
 * black background, the upper one with per-pixel alpha in the CON variant chosen at run time (kern.n2fbhwpm; H2 settles it):
 *   pm 0: CFMT 2 + AEN                               straight alpha (exact for opaque and fully clear pixels, darker edges otherwise)
 *   pm 1: CFMT 3 + MAN + AEN                         "legacy" 0x00803000 (not in either local tree; kept as a test variant)
 *   pm 2: CFMT 3 + MAN + BTSW + RGB_SWAP + AEN       DRM ARGB8888 premultiplied 0x03803000 (both local trees)
 *
 * Every address a plan lets the OVL read lies inside the layer's own surface memory AND inside a display-reachable window
 * (the display pool or the framebuffer carve-out, both below 4 GiB); otherwise the swap is not planned (CPU path).
 */
#include <stdbool.h>
#include <stdint.h>

#define SWEET_FB_HW_PIPES           2u
#define SWEET_FB_HW_SLOTS           2u          /* OVL*_2L layers 0 and 1 */
#define SWEET_FB_HW_MAX_IN          4u          /* SwapEnd layers */

/* display pool (0394): low DRAM 0xC0000000..0xD0000000, no FDT carve-out, no driver claim (DISPLAY_HW_COMPOSE.md 2.1 item 5) */
#ifndef SWEET_FB_HW_POOL_BASE      /* = sweet_fb_hw_pool.h (0394), same spelling */
#define SWEET_FB_HW_POOL_BASE           0xC0000000ull
#define SWEET_FB_HW_POOL_SIZE           0x10000000ull   /* 256 MiB */
#endif
/* framebuffer carve-out (mblock framebuffer; = SWEET_FB_SCAN_CARVE_END) */
#define SWEET_FB_HW_FB_BASE         0x76e10000ull
#define SWEET_FB_HW_FB_END          0x78c00000ull

/* OVL register offsets (bytes) */
#define SWEET_FB_HW_R_INTSTA        0x008u
#define SWEET_FB_HW_R_ROI_SIZE      0x020u
#define SWEET_FB_HW_R_ROI_BGCLR     0x028u
#define SWEET_FB_HW_R_SRC_CON       0x02cu
#define SWEET_FB_HW_R_CON(n)        (0x030u + 0x20u * (n))
#define SWEET_FB_HW_R_SRC_SIZE(n)   (0x038u + 0x20u * (n))
#define SWEET_FB_HW_R_OFFSET(n)     (0x03cu + 0x20u * (n))
#define SWEET_FB_HW_R_PITCH_MSB(n)  (0x040u + 0x20u * (n))
#define SWEET_FB_HW_R_PITCH(n)      (0x044u + 0x20u * (n))
#define SWEET_FB_HW_R_CLIP(n)       (0x04cu + 0x20u * (n))
#define SWEET_FB_HW_R_RDMA_CTRL(n)  (0x0c0u + 0x20u * (n))
#define SWEET_FB_HW_R_RDMA_GMC(n)   (0x0c8u + 0x20u * (n))
#define SWEET_FB_HW_R_RDMA_FIFO(n)  (0x0d0u + 0x20u * (n))
#define SWEET_FB_HW_R_ADDR(n)       (0xf40u + 0x20u * (n))
#define SWEET_FB_HW_INT_UNDERFLOW   ((1u << 2) | (0xfu << 5) | (0xfu << 9) | (1u << 13))  /* FME_UND, RDMAn EOF abnormal, RDMAn SMI underflow, abnormal SOF */
#define SWEET_FB_HW_DSI_UNDERRUN    (1u << 12)

#define SWEET_FB_HW_CON_OPAQUE      0x000020ffu     /* CFMT 2, AEN 0, plane alpha 255 */
#define SWEET_FB_HW_BGCLR_BLACK     0xff000000u
#define SWEET_FB_HW_PITCH_MAX       0xffffu

/* kern.n2fbhw */
enum sweet_fb_hw_mode {
	SWEET_FB_HW_OFF = 0,        /* CPU path for every swap (as 0393); the planner still counts what would be eligible */
	SWEET_FB_HW_DRY = 1,        /* plan every swap, count and log, never write a register (H0/H1 observation) */
	SWEET_FB_HW_SINGLE = 2,     /* H3: swaps whose only visible layer covers the whole panel go through the OVL */
	SWEET_FB_HW_DUAL = 3,       /* H4: up to two visible layers, any geometry inside the panel, alpha on the upper one */
	SWEET_FB_HW_MODE_MAX = 3
};

/* why a swap was not planned (counters in the driver, NORD2 FB HW lines) */
enum sweet_fb_hw_why {
	SWEET_FB_HW_OK = 0,
	SWEET_FB_HW_W_MODE,         /* mode off / dry */
	SWEET_FB_HW_W_NONE,         /* no visible layer */
	SWEET_FB_HW_W_COUNT,        /* more visible layers than the mode allows */
	SWEET_FB_HW_W_COVER,        /* SINGLE: the layer does not cover the panel */
	SWEET_FB_HW_W_FORMAT,       /* not 32 bpp BGRA single plane */
	SWEET_FB_HW_W_MEMORY,       /* not in display-reachable memory (an ordinary IOSurface) */
	SWEET_FB_HW_W_PITCH,        /* pitch not usable */
	SWEET_FB_HW_W_RANGE,        /* a read would leave the surface or the reachable window */
	SWEET_FB_HW_W_MISSING,      /* (driver) a layer's surface is gone / its id names another surface now */
	SWEET_FB_HW_W_FENCE,        /* (driver) a GPU write job on a layer is still in flight: never shown, the swap waits */
	SWEET_FB_HW_W_STATE,        /* (driver) power off, link not ready, overlay not as expected, fault latched */
	SWEET_FB_HW_W_COUNT_ENUM
};

static inline const char *
sweet_fb_hw_why_name(int w)
{
	static const char *const n[SWEET_FB_HW_W_COUNT_ENUM] = { "ok", "mode", "none", "count", "cover", "format", "memory", "pitch",
		                                                 "range", "missing", "fence", "state" };
	return (w >= 0 && w < (int)SWEET_FB_HW_W_COUNT_ENUM) ? n[w] : "?";
}

struct sweet_fb_hw_in {
	uint64_t pa;                /* physical start of the surface memory (0 = not display reachable) */
	uint64_t len;               /* bytes of surface memory from pa */
	uint32_t offset;            /* first pixel's byte offset in that memory (IOSurface offset) */
	uint32_t pitch;             /* bytes per row */
	uint32_t format_ok;         /* 32 bpp BGRA, one plane, 1x1 elements */
	int32_t sx, sy, dx, dy, w, h;   /* panel geometry after sweet_fb_layer_clip (dx,dy in panel pixels) */
};

struct sweet_fb_hw_slot {
	uint32_t en, addr, pitch, size, offset, con;
};

struct sweet_fb_hw_plan {
	uint32_t nlayers;
	struct sweet_fb_hw_slot slot[SWEET_FB_HW_PIPES][SWEET_FB_HW_SLOTS];
	uint32_t src_con[SWEET_FB_HW_PIPES];       /* layer enable bits 0/1 (other SRC_CON bits are kept by the driver) */
};

static inline uint32_t
sweet_fb_hw_con_upper(uint32_t pm)
{
	switch (pm) {
	case 1: return 0x00803000u | 0x100u | 0xffu;
	case 2: return 0x03803000u | 0x100u | 0xffu;
	default: return 0x00002000u | 0x100u | 0xffu;
	}
}

static inline bool
sweet_fb_hw_reachable(uint64_t a, uint64_t end)
{
	if (end <= a) { return false; }
	if (a >= SWEET_FB_HW_POOL_BASE && end <= SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE) { return true; }
	if (a >= SWEET_FB_HW_FB_BASE && end <= SWEET_FB_HW_FB_END) { return true; }
	return false;
}

/*
 * Plan for the visible layers in[0..n) (bottom first) on a fw x fh panel split into SWEET_FB_HW_PIPES equal column ranges.
 * mode: SWEET_FB_HW_SINGLE or SWEET_FB_HW_DUAL (anything else = W_MODE).  Returns SWEET_FB_HW_OK and fills *p, or the reason.
 */
static inline int
sweet_fb_hw_plan_make(struct sweet_fb_hw_plan *p, const struct sweet_fb_hw_in *in, unsigned n, uint32_t mode, uint32_t pm,
    uint32_t fw, uint32_t fh)
{
	unsigned i, k;
	uint32_t pw = fw / SWEET_FB_HW_PIPES;
	for (k = 0; k < SWEET_FB_HW_PIPES; k++) {
		p->src_con[k] = 0;
		for (i = 0; i < SWEET_FB_HW_SLOTS; i++) {
			struct sweet_fb_hw_slot z = { 0, 0, 0, 0, 0, 0 };
			p->slot[k][i] = z;
		}
	}
	p->nlayers = n;
	if (mode != SWEET_FB_HW_SINGLE && mode != SWEET_FB_HW_DUAL) { return SWEET_FB_HW_W_MODE; }
	if (n == 0) { return SWEET_FB_HW_W_NONE; }
	if (n > (mode == SWEET_FB_HW_SINGLE ? 1u : SWEET_FB_HW_SLOTS) || n > SWEET_FB_HW_MAX_IN) { return SWEET_FB_HW_W_COUNT; }
	if (fw == 0 || fh == 0 || (fw % SWEET_FB_HW_PIPES) != 0 || fh > 0xffffu || pw > 0xffffu) { return SWEET_FB_HW_W_COVER; }
	for (i = 0; i < n; i++) {
		const struct sweet_fb_hw_in *l = &in[i];
		if (!l->format_ok) { return SWEET_FB_HW_W_FORMAT; }
		if (l->pa == 0) { return SWEET_FB_HW_W_MEMORY; }
		if (l->w <= 0 || l->h <= 0 || l->sx < 0 || l->sy < 0 || l->dx < 0 || l->dy < 0 ||
		    (uint32_t)l->dx + (uint32_t)l->w > fw || (uint32_t)l->dy + (uint32_t)l->h > fh) { return SWEET_FB_HW_W_RANGE; }
		if (l->pitch == 0 || (l->pitch & 3u) || l->pitch > SWEET_FB_HW_PITCH_MAX || l->pitch < ((uint32_t)l->sx + (uint32_t)l->w) * 4u) {
			return SWEET_FB_HW_W_PITCH;
		}
		/* the whole rectangle read by both pipes together: [first, last) bytes from pa */
		uint64_t first = (uint64_t)l->offset + (uint64_t)l->sy * l->pitch + (uint64_t)l->sx * 4u;
		uint64_t last = (uint64_t)l->offset + (uint64_t)(l->sy + l->h - 1) * l->pitch + (uint64_t)(l->sx + l->w) * 4u;
		if (last > l->len || !sweet_fb_hw_reachable(l->pa + first, l->pa + last) || l->pa + last > 0x100000000ull) {
			return SWEET_FB_HW_W_RANGE;
		}
		if (mode == SWEET_FB_HW_SINGLE && (l->dx != 0 || l->dy != 0 || (uint32_t)l->w != fw || (uint32_t)l->h != fh)) {
			return SWEET_FB_HW_W_COVER;
		}
	}
	for (k = 0; k < SWEET_FB_HW_PIPES; k++) {
		int32_t c0 = (int32_t)(k * pw), c1 = c0 + (int32_t)pw;
		for (i = 0; i < n; i++) {
			const struct sweet_fb_hw_in *l = &in[i];
			int32_t x0 = l->dx > c0 ? l->dx : c0, x1 = l->dx + l->w < c1 ? l->dx + l->w : c1;
			if (x1 <= x0) { continue; }                              /* not in this pipe */
			struct sweet_fb_hw_slot *s = &p->slot[k][i];
			int32_t sx = l->sx + (x0 - l->dx);
			s->en = 1;
			s->addr = (uint32_t)(l->pa + l->offset + (uint64_t)l->sy * l->pitch + (uint64_t)sx * 4u);
			s->pitch = l->pitch;
			s->size = ((uint32_t)l->h << 16) | (uint32_t)(x1 - x0);
			s->offset = ((uint32_t)l->dy << 16) | (uint32_t)(x0 - c0);
			s->con = i == 0 ? SWEET_FB_HW_CON_OPAQUE : sweet_fb_hw_con_upper(pm);
			p->src_con[k] |= 1u << i;
		}
	}
	return SWEET_FB_HW_OK;
}

/*
 * The base (CPU path) state of one pipe's OVL*_2L, read once before the first HW frame; every HW frame is written relative to it
 * and sweet_fb_hw_regs_base() puts it back.  l0_addr is the panel buffer the CPU path shows (it changes with the 0156 flip).
 */
struct sweet_fb_hw_base {
	uint32_t src_con, bgclr;
	uint32_t con[SWEET_FB_HW_SLOTS], size[SWEET_FB_HW_SLOTS], offset[SWEET_FB_HW_SLOTS], pitch[SWEET_FB_HW_SLOTS];
	uint32_t pitch_msb[SWEET_FB_HW_SLOTS], clip[SWEET_FB_HW_SLOTS], rdma_ctrl[SWEET_FB_HW_SLOTS], gmc[SWEET_FB_HW_SLOTS], fifo[SWEET_FB_HW_SLOTS];
	uint32_t addr[SWEET_FB_HW_SLOTS];
};

/* one register write: offset, value, and the bits that must read back equal */
struct sweet_fb_hw_reg { uint32_t off, val, mask; };
#define SWEET_FB_HW_MAX_REGS 32u

static inline unsigned
sweet_fb_hw_put(struct sweet_fb_hw_reg *r, unsigned n, uint32_t off, uint32_t val, uint32_t mask)
{
	if (n < SWEET_FB_HW_MAX_REGS) { r[n].off = off; r[n].val = val; r[n].mask = mask; }
	return n + 1;
}

/*
 * Register writes for pipe k of a plan, in the order they are written (layer registers first, then the enables, then the
 * background).  A disabled slot keeps its base values except RDMA_CTRL bit 0 cleared (layer 1) / its SRC_CON bit.  Layer 1's
 * RDMA GMC / FIFO are the base values, or layer 0's when the base value is 0 (LK only configured layer 0).  Returns the count.
 */
static inline unsigned
sweet_fb_hw_regs(struct sweet_fb_hw_reg *r, const struct sweet_fb_hw_plan *p, unsigned k, const struct sweet_fb_hw_base *b)
{
	unsigned n = 0, i;
	for (i = 0; i < SWEET_FB_HW_SLOTS; i++) {
		const struct sweet_fb_hw_slot *s = &p->slot[k][i];
		if (!s->en) {
			if (i > 0) { n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_CTRL(i), b->rdma_ctrl[i] & ~1u, 1u); }
			continue;
		}
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_ADDR(i), s->addr, 0xffffffffu);
		/* pitch [15:0]; layer 0 keeps its base upper bits (LK's), layer 1 writes 0 there as the Linux driver does */
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_PITCH(i), (i == 0 ? (b->pitch[0] & 0xffff0000u) : 0u) | (s->pitch & 0xffffu), 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_PITCH_MSB(i), (b->pitch_msb[i] & ~0xfu) | ((s->pitch >> 16) & 0xfu), 0xfu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_SRC_SIZE(i), s->size, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_OFFSET(i), s->offset, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CLIP(i), 0u, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CON(i), s->con, 0x03fff1ffu);
		if (i > 0) {
			n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_GMC(i), b->gmc[i] ? b->gmc[i] : b->gmc[0], 0xffffffffu);
			n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_FIFO(i), b->fifo[i] ? b->fifo[i] : b->fifo[0], 0xffffffffu);
		}
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_CTRL(i), b->rdma_ctrl[i] | 1u, 1u);
	}
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_SRC_CON, (b->src_con & ~3u) | (p->src_con[k] & 3u), 3u);
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_ROI_BGCLR, SWEET_FB_HW_BGCLR_BLACK, 0xffffffffu);
	return n;
}

/* Register writes that put pipe k back to its base (CPU path) state, layer 0 reading l0_addr. */
static inline unsigned
sweet_fb_hw_regs_base(struct sweet_fb_hw_reg *r, unsigned k, const struct sweet_fb_hw_base *b, uint32_t l0_addr)
{
	unsigned n = 0, i;
	(void)k;
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_SRC_CON, b->src_con, 3u);          /* layer 1 off first */
	for (i = 0; i < SWEET_FB_HW_SLOTS; i++) {
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_ADDR(i), i == 0 ? l0_addr : b->addr[i], 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_PITCH(i), b->pitch[i], 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_PITCH_MSB(i), b->pitch_msb[i], 0xfu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_SRC_SIZE(i), b->size[i], 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_OFFSET(i), b->offset[i], 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CLIP(i), b->clip[i], 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CON(i), b->con[i], 0x03fff1ffu);
		if (i > 0) {
			n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_GMC(i), b->gmc[i], 0xffffffffu);
			n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_FIFO(i), b->fifo[i], 0xffffffffu);
		}
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_CTRL(i), b->rdma_ctrl[i], 1u);
	}
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_ROI_BGCLR, b->bgclr, 0xffffffffu);
	return n;
}

/*
 * The base state must be the one the early code and 0156 verified (layer 0 = the panel buffer, panel pitch, half panel, only
 * layer 0 enabled).  Anything else = refuse HW composition (somebody else programs the overlay).
 */
static inline bool
sweet_fb_hw_base_ok(const struct sweet_fb_hw_base *b, uint32_t l0_addr, uint32_t pitch, uint32_t half_w, uint32_t h)
{
	return b->addr[0] == l0_addr && (b->pitch[0] & 0xffffu) == pitch && b->size[0] == ((h << 16) | half_w) &&
	       (b->src_con & 3u) == 1u && b->offset[0] == 0u;
}

/* ---- H2 test patterns ----------------------------------------------------------------------------------------------------- */
/*
 * The pattern (fw x fh, B,G,R,A words) lives in the free third slot of the framebuffer carve-out (behind the two 0156 panel
 * buffers; the n2scan probe slot, unused unless n2scan is set).  Layer 1 of the gradient tests reads the gradient tile out of
 * the same buffer (rows TILE_Y.., x 0..TILE-1) and puts it at (GRAD_X, GRAD_Y), across the pipe seam at x = 540.  Below the HW
 * gradient the pattern carries two CPU-computed references over the same blue: REF_PM = what correct premultiplied
 * source-over gives, REF_ST = what the OVL gives if it treats the premultiplied tile as straight alpha.  A photo settles the
 * CON value: the HW gradient row must look like REF_PM (exact) for the variant to be usable for H4.
 * Lower half of the tile: 10x10 checkers of opaque white / fully clear = identical under every variant (geometry, seam).
 */
#define SWEET_FB_HW_TILE            300
#define SWEET_FB_HW_TILE_Y          2000        /* the tile's rows in the pattern buffer (shown raw by layer 0 too) */
#define SWEET_FB_HW_GRAD_X          390         /* destination of layer 1: x 390..689 crosses the seam at 540 */
#define SWEET_FB_HW_GRAD_Y          1000
#define SWEET_FB_HW_BAND_Y0         900         /* blue band behind the gradient and the references */
#define SWEET_FB_HW_BAND_Y1         1700
#define SWEET_FB_HW_REF_PM_Y        1320        /* 150 rows: premultiplied reference */
#define SWEET_FB_HW_REF_ST_Y        1480        /* 150 rows: straight-interpretation reference */
#define SWEET_FB_HW_BLUE            0xff2060c0u /* A=ff R=20 G=60 B=c0 as a 32-bit word = memory bytes B,G,R,A c0 60 20 ff */

static inline uint32_t
sweet_fb_hw_px(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
	return (a << 24) | (r << 16) | (g << 8) | b;      /* little endian word: bytes B,G,R,A */
}

/* the gradient tile at (tx, ty), premultiplied: orange (255,128,0) with alpha tx*255/299 (upper half), checkers (lower half) */
static inline uint32_t
sweet_fb_hw_tile_px(uint32_t tx, uint32_t ty)
{
	if (ty < SWEET_FB_HW_TILE / 2) {
		uint32_t a = tx * 255u / (SWEET_FB_HW_TILE - 1);
		return sweet_fb_hw_px(a, (128u * a + 127u) / 255u, 0u, a);
	}
	return (((tx / 10) + (ty / 10)) & 1u) ? sweet_fb_hw_px(255, 255, 255, 255) : 0u;
}

/* premultiplied source-over (what sweet_fb_blend.h does, rounded the same way per channel) */
static inline uint32_t
sweet_fb_hw_over_pm(uint32_t s, uint32_t d)
{
	uint32_t a = s >> 24, out = 0;
	for (unsigned c = 0; c < 32; c += 8) {
		uint32_t sc = (s >> c) & 0xffu, dc = (d >> c) & 0xffu;
		uint32_t v = sc + (dc * (255u - a) + 127u) / 255u;
		out |= (v > 255u ? 255u : v) << c;
	}
	return out;
}

/* the same tile pixel taken as straight alpha (colour multiplied by alpha a second time) */
static inline uint32_t
sweet_fb_hw_over_straight(uint32_t s, uint32_t d)
{
	uint32_t a = s >> 24, out = 0;
	for (unsigned c = 0; c < 24; c += 8) {
		uint32_t sc = (s >> c) & 0xffu, dc = (d >> c) & 0xffu;
		uint32_t v = (sc * a + dc * (255u - a) + 127u) / 255u;
		out |= (v > 255u ? 255u : v) << c;
	}
	return out | 0xff000000u;
}

/* the whole pattern at panel (x, y) */
static inline uint32_t
sweet_fb_hw_pattern_px(uint32_t x, uint32_t y, uint32_t fw, uint32_t fh)
{
	uint32_t half = fw / 2;
	if (y >= SWEET_FB_HW_TILE_Y && y < SWEET_FB_HW_TILE_Y + SWEET_FB_HW_TILE && x < SWEET_FB_HW_TILE) {
		return sweet_fb_hw_tile_px(x, y - SWEET_FB_HW_TILE_Y);
	}      /* the tile source (read by layer 1) comes first: exact */
	bool band = y >= SWEET_FB_HW_BAND_Y0 && y < SWEET_FB_HW_BAND_Y1;
	if (!band && x == half - 1) { return sweet_fb_hw_px(255, 0, 0, 255); }          /* seam markers: red at 539, green at 540 (not in the band) */
	if (!band && x == half) { return sweet_fb_hw_px(0, 255, 0, 255); }
	if (x < 4 || y < 4 || x >= fw - 4 || y >= fh - 4) { return sweet_fb_hw_px(255, 255, 255, 255); }   /* white frame */
	if (y < 200) { uint32_t v = x * 255u / (fw - 1); return sweet_fb_hw_px(v, v, v, 255); }           /* grey ramp */
	if (y < 600) {                                                                                     /* 8 colour bars */
		static const uint32_t bars[8] = { 0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u, 0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff000000u };
		uint32_t i = x * 8u / fw;
		return bars[i > 7 ? 7 : i];
	}
	if (y >= SWEET_FB_HW_BAND_Y0 && y < SWEET_FB_HW_BAND_Y1) {
		uint32_t gx0 = SWEET_FB_HW_GRAD_X, gx1 = SWEET_FB_HW_GRAD_X + SWEET_FB_HW_TILE;
		if (x >= gx0 && x < gx1 && y >= SWEET_FB_HW_REF_PM_Y && y < SWEET_FB_HW_REF_PM_Y + SWEET_FB_HW_TILE / 2) {
			return sweet_fb_hw_over_pm(sweet_fb_hw_tile_px(x - gx0, 0), SWEET_FB_HW_BLUE);
		}
		if (x >= gx0 && x < gx1 && y >= SWEET_FB_HW_REF_ST_Y && y < SWEET_FB_HW_REF_ST_Y + SWEET_FB_HW_TILE / 2) {
			return sweet_fb_hw_over_straight(sweet_fb_hw_tile_px(x - gx0, 0), SWEET_FB_HW_BLUE);
		}
		return SWEET_FB_HW_BLUE;
	}
	return ((x % 100) == 0 || (y % 100) == 0) ? sweet_fb_hw_px(64, 64, 64, 255) : sweet_fb_hw_px(24, 24, 24, 255);
}

/*
 * Layer inputs of test step t (1: pattern only, 2/3/4: pattern + gradient tile, CON pm 0/2/1) on the pattern buffer at slot_pa
 * with pitch.  Returns the layer count and the pm to plan with.
 */
static inline unsigned
sweet_fb_hw_test_layers(struct sweet_fb_hw_in in[2], uint32_t t, uint64_t slot_pa, uint32_t pitch, uint32_t fw, uint32_t fh, uint32_t *pm)
{
	uint64_t len = (uint64_t)pitch * fh;
	struct sweet_fb_hw_in l0 = { slot_pa, len, 0, pitch, 1, 0, 0, 0, 0, (int32_t)fw, (int32_t)fh };
	struct sweet_fb_hw_in l1 = { slot_pa, len, 0, pitch, 1, 0, SWEET_FB_HW_TILE_Y, SWEET_FB_HW_GRAD_X, SWEET_FB_HW_GRAD_Y,
		                     SWEET_FB_HW_TILE, SWEET_FB_HW_TILE };
	in[0] = l0; in[1] = l1;
	*pm = t == 2 ? 0u : (t == 3 ? 2u : (t == 4 ? 1u : 0u));
	return t >= 2 && t <= 4 ? 2u : 1u;
}

/* ---- fault / race policy ------------------------------------------------------------------------------------------------- */
/* Any OVL underflow / abnormal bit, DSI underrun, transfer timeout or register read-back mismatch on a HW frame turns HW
 * composition off (mode 0) at once.  hw_race (a GPU write to a shown layer surface started between the plan and FRAME_DONE)
 * turns it off after SWEET_FB_HW_RACE_LIMIT consecutive HW frames with a race. */
#define SWEET_FB_HW_RACE_LIMIT      16u
enum sweet_fb_hw_fault { SWEET_FB_HW_F_NONE = 0, SWEET_FB_HW_F_UNDERFLOW, SWEET_FB_HW_F_DSI, SWEET_FB_HW_F_TIMEOUT, SWEET_FB_HW_F_READBACK,
	                 SWEET_FB_HW_F_RACE, SWEET_FB_HW_F_BASE };
static inline const char *
sweet_fb_hw_fault_name(int f)
{
	static const char *const n[] = { "none", "ovl-underflow", "dsi-underrun", "frame-done-timeout", "readback-mismatch", "hw-race", "overlay-base" };
	return (f >= 0 && f <= (int)SWEET_FB_HW_F_BASE) ? n[f] : "?";
}
/* fault of a finished HW frame from the OVL INTSTA of both pipes, the DSI INTSTA and the transfer result; race_run is updated */
static inline int
sweet_fb_hw_frame_fault(uint32_t ovl_or, uint32_t dsi, bool timeout, bool raced, uint32_t *race_run)
{
	*race_run = raced ? *race_run + 1 : 0;
	if (timeout) { return SWEET_FB_HW_F_TIMEOUT; }
	if (ovl_or & SWEET_FB_HW_INT_UNDERFLOW) { return SWEET_FB_HW_F_UNDERFLOW; }
	if (dsi & SWEET_FB_HW_DSI_UNDERRUN) { return SWEET_FB_HW_F_DSI; }
	if (*race_run >= SWEET_FB_HW_RACE_LIMIT) { return SWEET_FB_HW_F_RACE; }
	return SWEET_FB_HW_F_NONE;
}

/* GPU fence rule for a HW frame: shown only when no write job on any layer is in flight (busy = queued/running write job or a
 * 0112 reservation; pending = 0395's per-surface write-pending).  Never forced: after the bounded wait the swap is requeued
 * (a newer swap replaces it) and the panel keeps its picture. */
static inline bool
sweet_fb_hw_fence_ok(const bool busy[], const bool pending[], unsigned n)
{
	for (unsigned i = 0; i < n; i++) { if (busy[i] || pending[i]) { return false; } }
	return true;
}

#endif /* SWEET_FB_HW_H */
