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
 *   pm 1: CFMT 3 + MAN + AEN                         "legacy" 0x00803000 (alpha first: wrong layout for 'BGRA'; test variant only)
 *   pm 2: CFMT 3 + MAN + RGB_SWAP + AEN              0x028031ff: premultiplied, memory B,G,R,A  (0399's default; 0414: NOT the default)
 *   pm 3: CFMT 3 + MAN + BYTE_SWAP + RGB_SWAP + AEN  0x038031ff: the DRM table's "ARGB8888 premultiplied" (0396's pm 2)
 *
 * 0399 (hwcolor-20261112) COLOUR ORDER.  CON bits (mtk_disp_ovl.c 296-313, identical in oneplus-mt6893 and nord2-current-kernel):
 * [15:12] CFMT (2 = "RGBA8888", 3 = "ARGB8888"), 23 CLRFMT_MAN (premultiplied), 24 BYTE_SWAP, 25 RGB_SWAP, 8 AEN, [7:0] alpha.
 * ovl_fmt_convert() (869-925) maps DRM_FORMAT_ARGB8888 (memory B,G,R,A = CoreAnimation 'BGRA') to CFMT 2 (straight; = LK's
 * panel layer 0x21ff and our layer 0: colours right on the phone [V-dev]) and to CFMT 3 | MAN | BYTE_SWAP | RGB_SWAP when
 * premultiplied.  On the phone (q10jfi..k, mode 3, Photos) that value (0x038031ff, 0396's default) showed layer 1 with RED AND
 * BLUE SWAPPED (iOS blue 0,122,255 -> 255,122,0) while layer 0 (0x20ff) was right [V-dev, coordinator's photos].  So on this OVL
 * BYTE_SWAP must be CLEAR for memory B,G,R,A in the premultiplied format as well: 0x028031ff ([I] until the 0399 photo test;
 * kern.n2fbhwcsw flips bit 24 per layer at run time to settle it in one boot).  sweet_fb_hw_con_decode() below is this model,
 * written out so the host test checks every layer's CON against it instead of silently assuming the colour order.
 *
 * 0414 (colorfix-20261113): on q10jgk (0399, pm 2 = 0x028031ff, csw 0, CON written logged) the user still saw iOS blue as ORANGE
 * in Photos while frames were composed by the overlay (Library grid = two-layer frames, layer 1 = backboardd's UI) [V-dev, user
 * report, no photo yet].  So BOTH premultiplied values (0x038031ff and 0x028031ff) showed R/B swapped on layer 1: the CFMT 3 + MAN
 * rows of the model below are UNVERIFIED (BYTE_SWAP may be ignored with MAN set).  Only CFMT 2 is known right on this OVL (LK's
 * 0x21ff, our 0x20ff).  Default pm is therefore 0 (CFMT 2 + AEN = 0x000021ff, straight alpha: exact colours, opaque and clear pixels
 * exact, anti-aliased edges slightly darker).  kern.n2fbhwpm / kern.n2fbhwcsw unchanged; boot-args n2fbhwpm=0..3 / n2fbhwcsw=0..3
 * set them once at hwInit.  sweet_fb_hw_con_verified() = the CON values known right on the phone.
 *
 * Every address a plan lets the OVL read lies inside the layer's own surface memory AND inside a display-reachable window
 * (the display pool or the framebuffer carve-out, both below 4 GiB); otherwise the swap is not planned (CPU path).
 *
 * 0402 (hwmulti-20261113) MORE LAYERS.  Each pipe is OVL*_2L -> OVL* (4 layers) -> RDMA in cascade (mtk_drm_drv.c
 * mt6885_mtk_ddp_main / _dual_main [V-code]); the 4L block takes the 2L output as its background (DATAPATH_CON bit 2
 * BGCLR_IN_SEL, set by mtk_ovl_connect when the previous component is an OVL [V-code]), so its layers stack ABOVE both 2L
 * layers.  Visible layer i (bottom first) uses slot i = 2L L0, 2L L1, 4L L0..L3 in every pipe it reaches (a pipe it does not
 * reach leaves that slot off), for i < cap (kern.n2fbhwlayers 2..6; 2 = exactly the 0399 registers).  Slot 0 is opaque (AEN 0)
 * over the black 2L background unless the layer has a plane alpha; every other slot is premultiplied source-over.  That equals
 * the CPU path: where the bottom layer does not cover, the CPU copies the next layer, the OVL blends it over black
 * (premultiplied s + 0 = s).  Plane alpha (CON[7:0] with AEN, as the DRM driver does for PLANE_PROP_PLANE_ALPHA) is modelled
 * as out = s*pa + d*(1 - sa*pa) [I: Android's HWC2 premultiplied+plane-alpha rule]; no swap field carries a layer opacity
 * today, so the driver passes 255.  4L layers: DATAPATH_CON bit 4+n (layer n FBDC compression) is cleared, ROI_BGCLR untouched.
 * The OVL has no scaler and no semi-planar YUV input (only packed UYVY/YUYV, ovl_fmt_convert) [V-code]: 420v/420f video and
 * scaled layers are never planned here.
 */
#include <stdbool.h>
#include <stdint.h>
#include "sweet_fb_yuv.h"             /* 0490 */

#define SWEET_FB_HW_PIPES           2u
#define SWEET_FB_HW_SLOTS           2u          /* OVL*_2L layers 0 and 1 */
#define SWEET_FB_HW_L4              4u          /* 0402: OVL* (4L) layers 0..3 = slots 2..5 */
#define SWEET_FB_HW_NSLOT           6u
#define SWEET_FB_HW_MAX_IN          6u          /* planner inputs (SwapEnd has 4) */

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
#define SWEET_FB_HW_R_DATAPATH      0x024u      /* 0402: bit 2 BGCLR_IN_SEL, bit 4+n layer n FBDC */
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
#define SWEET_FB_HW_CON_AEN         (1u << 8)
#define SWEET_FB_HW_CON_CFMT(c)     (((c) >> 12) & 0xfu)
#define SWEET_FB_HW_CON_MAN         (1u << 23)      /* CLRFMT_MAN: premultiplied */
#define SWEET_FB_HW_CON_BYTE_SWAP   (1u << 24)      /* swaps R and B (see the colour order note above) */
#define SWEET_FB_HW_CON_RGB_SWAP    (1u << 25)      /* CFMT 3: alpha in the last byte instead of the first */
#define SWEET_FB_HW_BGCLR_BLACK     0xff000000u
#define SWEET_FB_HW_PITCH_MAX       0xffffu

/* kern.n2fbhw */
enum sweet_fb_hw_mode {
	SWEET_FB_HW_OFF = 0,        /* CPU path for every swap (as 0393); the planner still counts what would be eligible */
	SWEET_FB_HW_DRY = 1,        /* plan every swap, count and log, never write a register (H0/H1 observation) */
	SWEET_FB_HW_SINGLE = 2,     /* H3: swaps whose only visible layer covers the whole panel go through the OVL */
	SWEET_FB_HW_DUAL = 3,       /* H4/H5: up to `cap` slots per pipe (0402: kern.n2fbhwlayers), any geometry, alpha above slot 0 */
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
	uint32_t format_ok;         /* 32 bpp BGRA, one plane, 1x1 elements; 0490: > 1 = packed 4:2:2, the L_CON format bits (sweet_fb_yuv_con) */
	int32_t sx, sy, dx, dy, w, h;   /* panel geometry after sweet_fb_layer_clip (dx,dy in panel pixels) */
	uint32_t alpha;             /* 0402: plane alpha 1..254, 0 or >= 255 = opaque layer (no plane alpha) */
};

struct sweet_fb_hw_slot {
	uint32_t en, addr, pitch, size, offset, con;
	uint32_t clip;              /* 0490: Ln_CLIP (0 for BGRA) */
};

struct sweet_fb_hw_plan {
	uint32_t nlayers;
	struct sweet_fb_hw_slot slot[SWEET_FB_HW_PIPES][SWEET_FB_HW_NSLOT];
	uint32_t src_con[SWEET_FB_HW_PIPES];       /* slot enable bits 0..5 (2L SRC_CON bits 0/1, 4L SRC_CON bits 0..3 = bits 2..5) */
	uint32_t nslot[SWEET_FB_HW_PIPES];         /* 0402: highest slot used + 1, per pipe */
	uint32_t con_xor[SWEET_FB_HW_SLOTS];       /* 0399: [0] slot 0, [1] every upper slot (kern.n2fbhwcsw); 0 from plan_make */
};

/* 0399: kern.n2fbhwcsw bit 0 flips BYTE_SWAP on layer 1 (upper), bit 1 on layer 0 (bottom) */
static inline void
sweet_fb_hw_plan_csw(struct sweet_fb_hw_plan *p, uint32_t csw)
{
	p->con_xor[0] = (csw & 2u) ? SWEET_FB_HW_CON_BYTE_SWAP : 0u;
	p->con_xor[1] = (csw & 1u) ? SWEET_FB_HW_CON_BYTE_SWAP : 0u;
}

/* the CON value slot i of pipe k is written with */
static inline uint32_t
sweet_fb_hw_con_final(const struct sweet_fb_hw_plan *p, unsigned k, unsigned i)
{
	return p->slot[k][i].con ^ p->con_xor[i ? 1u : 0u];
}

/*
 * 0399: what the OVL makes of one memory word under a CON value (the model above): returns 0xAARRGGBB, the CPU path's own
 * reading of a CoreAnimation 'BGRA' word.  A correct CON for our surfaces decodes every word to itself.
 *   CFMT 2: alpha in byte 3, colour bytes 0,1,2 = B,G,R; BYTE_SWAP -> R,G,B.            [V-dev: 0x20ff/0x21ff right]
 *   CFMT 3 + RGB_SWAP: alpha in byte 3, colour bytes 0,1,2 = B,G,R; BYTE_SWAP -> R,G,B.  [V-dev: 0x038031ff = R/B swapped]
 *   CFMT 3 without RGB_SWAP: alpha in byte 0, colour bytes 1,2,3 = B,G,R; BYTE_SWAP -> R,G,B.  [from the DRM table only]
 */
static inline uint32_t
sweet_fb_hw_con_decode(uint32_t con, uint32_t w)
{
	uint32_t b0 = w & 0xffu, b1 = (w >> 8) & 0xffu, b2 = (w >> 16) & 0xffu, b3 = w >> 24;
	uint32_t a, c0, c1, c2;
	if (SWEET_FB_HW_CON_CFMT(con) == 3u && !(con & SWEET_FB_HW_CON_RGB_SWAP)) { a = b0; c0 = b1; c1 = b2; c2 = b3; }
	else { a = b3; c0 = b0; c1 = b1; c2 = b2; }
	uint32_t r = c2, b = c0;
	if (con & SWEET_FB_HW_CON_BYTE_SWAP) { r = c0; b = c2; }
	return (a << 24) | (r << 16) | (c1 << 8) | b;
}

/* 0414: the default upper-layer variant (kern.n2fbhwpm / boot-arg n2fbhwpm override it) */
#define SWEET_FB_HW_PM_DEFAULT      0u

/* 0414: CON values whose colour order is verified on the phone: CFMT 2 (LK's 0x21ff, layer 0's 0x20ff), no MAN, no swap bits */
static inline bool
sweet_fb_hw_con_verified(uint32_t con)
{
	return SWEET_FB_HW_CON_CFMT(con) == 2u && !(con & (SWEET_FB_HW_CON_MAN | SWEET_FB_HW_CON_BYTE_SWAP | SWEET_FB_HW_CON_RGB_SWAP));
}

static inline uint32_t
sweet_fb_hw_con_upper(uint32_t pm)
{
	switch (pm) {
	case 1: return 0x00803000u | 0x100u | 0xffu;
	case 2: return 0x02803000u | 0x100u | 0xffu;     /* 0399 */
	case 3: return 0x03803000u | 0x100u | 0xffu;
	default: return 0x00002000u | 0x100u | 0xffu;
	}
}

/* 0402: readable by an OVL port running with MMU_EN = 0 (physical): the display pool or the framebuffer carve-out */
static inline bool
sweet_fb_hw_phys(uint64_t a, uint64_t end)
{
	return end > a && ((a >= SWEET_FB_HW_POOL_BASE && end <= SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE) ||
	                   (a >= SWEET_FB_HW_FB_BASE && end <= SWEET_FB_HW_FB_END));
}

static inline bool
sweet_fb_hw_reachable(uint64_t a, uint64_t end)
{
	if (end <= a) { return false; }
	if (a >= SWEET_FB_HW_POOL_BASE && end <= SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE) { return true; }
	if (a >= SWEET_FB_HW_FB_BASE && end <= SWEET_FB_HW_FB_END) { return true; }
	if (a >= 0x90000000ull && end <= 0xB0000000ull) { return true; }      /* 0401 IOMMU: IOVA window (sweet_fb_iommu.h), handed out only while armed */
	return false;
}

/*
 * Plan for the visible layers in[0..n) (bottom first) on a fw x fh panel split into SWEET_FB_HW_PIPES equal column ranges.
 * mode: SWEET_FB_HW_SINGLE or SWEET_FB_HW_DUAL (anything else = W_MODE); cap: slots per pipe (2..6; 0402) | flags.  Returns
 * SWEET_FB_HW_OK and fills *p, or the reason (W_COUNT = layer >= cap reaches a pipe).  0415: no slot is physical-only any more: the
 * 4L ports (larb0/1 p5) translate together with the 2L ports (0401 gate, sweet_fb_iommu_gate4), so an IOVA layer may take any slot.
 * cap bits above 0xff are ignored (0402's SWEET_FB_HW_CAP_L4_IOVA is now the only behaviour).
 */
#define SWEET_FB_HW_CAP_L4_IOVA     0x100u      /* 0415: accepted and ignored (always on) */
static inline int
sweet_fb_hw_plan_makec(struct sweet_fb_hw_plan *p, const struct sweet_fb_hw_in *in, unsigned n, uint32_t mode, uint32_t pm,
    uint32_t fw, uint32_t fh, uint32_t cap)
{
	unsigned i, k;
	uint32_t pw = fw / SWEET_FB_HW_PIPES;
	__builtin_memset(p, 0, sizeof(*p));
	p->nlayers = n;
	cap &= 0xffu;
	if (cap > SWEET_FB_HW_NSLOT) { cap = SWEET_FB_HW_NSLOT; }
	if (mode != SWEET_FB_HW_SINGLE && mode != SWEET_FB_HW_DUAL) { return SWEET_FB_HW_W_MODE; }
	if (n == 0) { return SWEET_FB_HW_W_NONE; }
	if (n > SWEET_FB_HW_MAX_IN || (mode == SWEET_FB_HW_SINGLE && n > 1u)) { return SWEET_FB_HW_W_COUNT; }
	if (fw == 0 || fh == 0 || (fw % SWEET_FB_HW_PIPES) != 0 || fh > 0xffffu || pw > 0xffffu) { return SWEET_FB_HW_W_COVER; }
	for (i = 0; i < n; i++) {
		const struct sweet_fb_hw_in *l = &in[i];
		if (!l->format_ok) { return SWEET_FB_HW_W_FORMAT; }
		if (l->pa == 0) { return SWEET_FB_HW_W_MEMORY; }
		if (l->w <= 0 || l->h <= 0 || l->sx < 0 || l->sy < 0 || l->dx < 0 || l->dy < 0 ||
		    (uint32_t)l->dx + (uint32_t)l->w > fw || (uint32_t)l->dy + (uint32_t)l->h > fh) { return SWEET_FB_HW_W_RANGE; }
		uint32_t bp = l->format_ok > 1u ? 2u : 4u;      /* 0490: bytes per pixel; a 4:2:2 layer reads even column pairs (vx, vw) */
		int32_t vx = l->sx, vw = l->w;
		if (bp == 2u) { (void)sweet_fb_yuv_span(l->sx, l->w, &vx, &vw); }
		if (l->pitch == 0 || (l->pitch & 3u) || l->pitch > SWEET_FB_HW_PITCH_MAX || l->pitch < ((uint32_t)vx + (uint32_t)vw) * bp) {
			return SWEET_FB_HW_W_PITCH;
		}
		/* the whole rectangle read by both pipes together: [first, last) bytes from pa */
		uint64_t first = (uint64_t)l->offset + (uint64_t)l->sy * l->pitch + (uint64_t)vx * bp;
		uint64_t last = (uint64_t)l->offset + (uint64_t)(l->sy + l->h - 1) * l->pitch + (uint64_t)(vx + vw) * bp;
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
			unsigned ns = i;                                         /* slot = layer index (0399 for two layers) */
			const struct sweet_fb_hw_in *l = &in[i];
			int32_t x0 = l->dx > c0 ? l->dx : c0, x1 = l->dx + l->w < c1 ? l->dx + l->w : c1;
			if (x1 <= x0) { continue; }                              /* not in this pipe: no slot */
			if (ns >= cap) { return SWEET_FB_HW_W_COUNT; }
			struct sweet_fb_hw_slot *s = &p->slot[k][ns];
			int32_t sx = l->sx + (x0 - l->dx), sw = x1 - x0;
			if (l->format_ok > 1u) { s->clip = sweet_fb_yuv_span(sx, sw, &sx, &sw); }      /* 0490 */
			uint32_t a = (l->alpha == 0 || l->alpha >= 255u) ? 255u : l->alpha;
			s->en = 1;
			s->addr = (uint32_t)(l->pa + l->offset + (uint64_t)l->sy * l->pitch + (uint64_t)sx * (l->format_ok > 1u ? 2u : 4u));
			s->pitch = l->pitch;
			s->size = ((uint32_t)l->h << 16) | (uint32_t)sw;
			s->offset = ((uint32_t)l->dy << 16) | (uint32_t)(x0 - c0);
			s->con = (ns == 0 && a == 255u) ? SWEET_FB_HW_CON_OPAQUE : ((sweet_fb_hw_con_upper(pm) & ~0xffu) | a);
			if (l->format_ok > 1u) { s->con = sweet_fb_yuv_con_alpha(l->format_ok, l->alpha); }      /* 0490 */
			p->src_con[k] |= 1u << ns;
			p->nslot[k] = ns + 1u;
		}
	}
	return SWEET_FB_HW_OK;
}

/* 0396/0399 interface: at most two slots per pipe */
static inline int
sweet_fb_hw_plan_make(struct sweet_fb_hw_plan *p, const struct sweet_fb_hw_in *in, unsigned n, uint32_t mode, uint32_t pm,
    uint32_t fw, uint32_t fh)
{
	return sweet_fb_hw_plan_makec(p, in, n, mode, pm, fw, fh, SWEET_FB_HW_SLOTS);
}

/* 0402: the plan uses the 4L block (a slot >= 2) in some pipe */
static inline bool
sweet_fb_hw_plan_uses4(const struct sweet_fb_hw_plan *p)
{
	unsigned k;
	for (k = 0; k < SWEET_FB_HW_PIPES; k++) { if (p->src_con[k] & ~3u) { return true; } }
	return false;
}

/*
 * The base (CPU path) state of one pipe's OVL*_2L, read once before the first HW frame; every HW frame is written relative to it
 * and sweet_fb_hw_regs_base() puts it back.  l0_addr is the panel buffer the CPU path shows (it changes with the 0156 flip).
 */
struct sweet_fb_hw_base {
	uint32_t src_con, bgclr;
	uint32_t con[SWEET_FB_HW_L4], size[SWEET_FB_HW_L4], offset[SWEET_FB_HW_L4], pitch[SWEET_FB_HW_L4];
	uint32_t pitch_msb[SWEET_FB_HW_L4], clip[SWEET_FB_HW_L4], rdma_ctrl[SWEET_FB_HW_L4], gmc[SWEET_FB_HW_L4], fifo[SWEET_FB_HW_L4];
	uint32_t addr[SWEET_FB_HW_L4];
	uint32_t datapath, roi, gmc_fb, fifo_fb;   /* 0402: DATAPATH_CON, ROI_SIZE; 4L: GMC/FIFO for layers LK left at 0 */
};

/* one register write: offset, value, and the bits that must read back equal */
struct sweet_fb_hw_reg { uint32_t off, val, mask; };
#define SWEET_FB_HW_MAX_REGS 48u

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
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CLIP(i), s->clip, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CON(i), s->con ^ p->con_xor[i], 0x03fff1ffu);
		if (i > 0) {      /* RDMA GMC/FIFO = bandwidth only (no colour bits) */
			n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_GMC(i), b->gmc[i] ? b->gmc[i] : b->gmc[0], 0xffffffffu);
			n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_FIFO(i), b->fifo[i] ? b->fifo[i] : b->fifo[0], 0xffffffffu);
		}
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_CTRL(i), b->rdma_ctrl[i] | 1u, 1u);
	}
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_SRC_CON, (b->src_con & ~3u) | (p->src_con[k] & 3u), 3u);
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_ROI_BGCLR, SWEET_FB_HW_BGCLR_BLACK, 0xffffffffu);
	return n;
}

/*
 * 0402: register writes for the 4L block of pipe k (slots 2..5 = its layers 0..3), b = that block's base.  Every 4L layer is
 * premultiplied over the cascade input; its pitch upper bits are 0 (as the DRM driver writes them); GMC/FIFO = its own base or
 * gmc_fb/fifo_fb.  FBDC (DATAPATH_CON bit 4+n) off; BGCLR_IN_SEL (bit 2) must stay set.  A plan without 4L slots turns them off.
 */
static inline unsigned
sweet_fb_hw_regs4(struct sweet_fb_hw_reg *r, const struct sweet_fb_hw_plan *p, unsigned k, const struct sweet_fb_hw_base *b)
{
	unsigned n = 0, j;
	for (j = 0; j < SWEET_FB_HW_L4; j++) {
		const struct sweet_fb_hw_slot *s = &p->slot[k][SWEET_FB_HW_SLOTS + j];
		if (!s->en) { n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_CTRL(j), b->rdma_ctrl[j] & ~1u, 1u); continue; }
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_ADDR(j), s->addr, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_PITCH(j), s->pitch & 0xffffu, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_PITCH_MSB(j), (b->pitch_msb[j] & ~0xfu) | ((s->pitch >> 16) & 0xfu), 0xfu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_SRC_SIZE(j), s->size, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_OFFSET(j), s->offset, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CLIP(j), s->clip, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_CON(j), s->con ^ p->con_xor[1], 0x03fff1ffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_GMC(j), b->gmc[j] ? b->gmc[j] : b->gmc_fb, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_FIFO(j), b->fifo[j] ? b->fifo[j] : b->fifo_fb, 0xffffffffu);
		n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_CTRL(j), b->rdma_ctrl[j] | 1u, 1u);
	}
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_DATAPATH, b->datapath & ~0xf0u, 0xf4u);
	n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_SRC_CON, (b->src_con & ~0xfu) | ((p->src_con[k] >> SWEET_FB_HW_SLOTS) & 0xfu), 0xfu);
	return n;
}

/* 0402: the 4L block back to its base (layers off as the early code left them, DATAPATH_CON as read) */
static inline unsigned
sweet_fb_hw_regs4_base(struct sweet_fb_hw_reg *r, const struct sweet_fb_hw_base *b)
{
	unsigned n = sweet_fb_hw_put(r, 0, SWEET_FB_HW_R_SRC_CON, b->src_con, 0xfu), j;
	for (j = 0; j < SWEET_FB_HW_L4; j++) { n = sweet_fb_hw_put(r, n, SWEET_FB_HW_R_RDMA_CTRL(j), b->rdma_ctrl[j], 1u); }
	return sweet_fb_hw_put(r, n, SWEET_FB_HW_R_DATAPATH, b->datapath, 0xf4u);
}

/* 0402: the 4L block is usable as a cascade stage: no layer on, background = the 2L output, same ROI as the 2L */
static inline bool
sweet_fb_hw_base4_ok(const struct sweet_fb_hw_base *b4, uint32_t roi2l)
{
	return (b4->src_con & 0xfu) == 0u && (b4->src_con & 0x100u) == 0u && (b4->datapath & 4u) != 0u && b4->roi == roi2l && roi2l != 0u;
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
#define SWEET_FB_HW_RGB_Y0          620         /* 0399: layer-0 colour strip R | G | B (100 px each) at x 390..689, rows 620..699 */
#define SWEET_FB_HW_RGB_ROWS        80
#define SWEET_FB_HW_RGB_L1_Y        710         /* 0399: layer 1 puts the same strip (source rows 2310.., x 0..299) at rows 710..789 */
#define SWEET_FB_HW_RGB_SRC_Y       2310
#define SWEET_FB_HW_T7_Y            1750        /* 0402: test 7's second tile (4L layer 1), over the dark grid */
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

/* 0399: pure, opaque red | green | blue, 100 px each (tx = 0..299) */
static inline uint32_t
sweet_fb_hw_rgb_px(uint32_t tx)
{
	return tx < 100 ? sweet_fb_hw_px(255, 0, 0, 255) : (tx < 200 ? sweet_fb_hw_px(0, 255, 0, 255) : sweet_fb_hw_px(0, 0, 255, 255));
}

/* the whole pattern at panel (x, y) */
static inline uint32_t
sweet_fb_hw_pattern_px(uint32_t x, uint32_t y, uint32_t fw, uint32_t fh)
{
	if (y >= SWEET_FB_HW_RGB_SRC_Y && y < SWEET_FB_HW_RGB_SRC_Y + SWEET_FB_HW_RGB_ROWS && x < SWEET_FB_HW_TILE) {
		return sweet_fb_hw_rgb_px(x);      /* layer 1's colour strip source (first: exact) */
	}
	if (y >= SWEET_FB_HW_RGB_Y0 && y < SWEET_FB_HW_RGB_Y0 + SWEET_FB_HW_RGB_ROWS && x >= SWEET_FB_HW_GRAD_X && x < SWEET_FB_HW_GRAD_X + SWEET_FB_HW_TILE) {
		return sweet_fb_hw_rgb_px(x - SWEET_FB_HW_GRAD_X);      /* layer 0's colour strip */
	}
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
 * Layer inputs of test step t on the pattern buffer at slot_pa with pitch: 1 pattern only; 2/3/4 pattern + gradient tile with
 * CON pm 0/2/3; 6 (0399 colour test) pattern + layer 1 = the R|G|B strip at rows 710..789 under layer 0's own strip, with cur_pm.
 * Returns the layer count and the pm to plan with.
 */
static inline unsigned
sweet_fb_hw_test_layers(struct sweet_fb_hw_in in[4], uint32_t t, uint64_t slot_pa, uint32_t pitch, uint32_t fw, uint32_t fh, uint32_t cur_pm,
    uint32_t *pm)
{
	if (t == 7) {      /* 0402: 4 layers = test 3 (pattern, gradient tile) + the R|G|B strip at rows 710.. + the tile again at 390,1750 (4L L0/L1) */
		(void)sweet_fb_hw_test_layers(in, 3, slot_pa, pitch, fw, fh, cur_pm, pm);
		struct sweet_fb_hw_in c = in[1], d = in[1];
		c.sy = SWEET_FB_HW_RGB_SRC_Y; c.dy = SWEET_FB_HW_RGB_L1_Y; c.h = SWEET_FB_HW_RGB_ROWS;
		d.dy = SWEET_FB_HW_T7_Y;
		in[2] = c; in[3] = d;
		*pm = cur_pm;
		return 4u;
	}
	if (t == 6) {
		uint64_t len6 = (uint64_t)pitch * fh;
		struct sweet_fb_hw_in a = { slot_pa, len6, 0, pitch, 1, 0, 0, 0, 0, (int32_t)fw, (int32_t)fh, 0 };
		struct sweet_fb_hw_in b = { slot_pa, len6, 0, pitch, 1, 0, SWEET_FB_HW_RGB_SRC_Y, SWEET_FB_HW_GRAD_X, SWEET_FB_HW_RGB_L1_Y,
			                    SWEET_FB_HW_TILE, SWEET_FB_HW_RGB_ROWS, 0 };
		in[0] = a; in[1] = b;
		*pm = cur_pm;
		return 2u;
	}
	uint64_t len = (uint64_t)pitch * fh;
	struct sweet_fb_hw_in l0 = { slot_pa, len, 0, pitch, 1, 0, 0, 0, 0, (int32_t)fw, (int32_t)fh, 0 };
	struct sweet_fb_hw_in l1 = { slot_pa, len, 0, pitch, 1, 0, SWEET_FB_HW_TILE_Y, SWEET_FB_HW_GRAD_X, SWEET_FB_HW_GRAD_Y,
		                     SWEET_FB_HW_TILE, SWEET_FB_HW_TILE, 0 };
	in[0] = l0; in[1] = l1;
	*pm = t == 2 ? 0u : (t == 3 ? 2u : (t == 4 ? 3u : 0u));
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

/* 0416: panel blank (DCS 0x28 display off / 0x29 on); no CPU fade and no fallback: a failed DCS command is retried. */
enum { SWEET_FB_BLANK_NONE = 0, SWEET_FB_BLANK_OFF, SWEET_FB_BLANK_ON };
static inline int
sweet_fb_blank_action(bool power_off, bool blanked)
{
	if (power_off) { return blanked ? SWEET_FB_BLANK_NONE : SWEET_FB_BLANK_OFF; }
	return blanked ? SWEET_FB_BLANK_ON : SWEET_FB_BLANK_NONE;
}

/*
 * 0402 H6: the GPU fence of one swap, polled by the present thread.  layers = bit mask of the swap's visible layers with a surface;
 * snap[l] = nord2_gpu_surface_write_seq at SwapEnd (the newest write submitted for layer l by then).  A layer is ready when no
 * write job <= snap[l] is unfinished (a job submitted later -- the client drawing its next frame -- does not hold an older swap).
 * A 0112 reservation (a frame promised, not yet submitted; *resv_seen starts with the ones seen at SwapEnd) holds the layer; when
 * it retires the snapshot is taken again, so the promised job is waited for.  Returns READY, WAIT (age < limit) or FORCED (shown anyway: the bounded hold is over).
 */
/* 0402c: the lock-free form actually used.  snap[l] = the newest write job recorded for layer l's surface at SwapEnd (fence table peek),
 * lowwater = every job id <= it has completed (job table peek).  Ready = all snapshots <= lowwater (conservative: an older job of
 * another client also holds the frame).  No GPU-driver lock, no sleep; the caller re-polls by timer and forces after the limit. */
static inline int
sweet_fb_hw_h6c(uint32_t layers, const uint64_t snap[], uint64_t lowwater, uint64_t age_us, uint64_t limit_us)
{
	for (unsigned l = 0; l < 32u && (layers >> l) != 0; l++) {
		if (((layers >> l) & 1u) && snap[l] > lowwater) { return age_us < limit_us ? 1 : 2; }
	}
	return 0;
}

#define SWEET_FB_HW_H6_READY  0
#define SWEET_FB_HW_H6_WAIT   1
#define SWEET_FB_HW_H6_FORCED 2
struct sweet_fb_hw_h6_gpu {
	void *ctx;
	bool (*busy)(void *ctx, unsigned l);                /* any unfinished write job or a reservation (nord2_gpu_surface_busy) */
	bool (*resv)(void *ctx, unsigned l);                /* a reservation is outstanding */
	uint64_t (*seq)(void *ctx, unsigned l);             /* newest write job submitted (write_seq) */
	bool (*done)(void *ctx, unsigned l, uint64_t s);    /* no unfinished write job <= s (writes_done) */
};
static inline int
sweet_fb_hw_h6(const struct sweet_fb_hw_h6_gpu *g, uint32_t layers, uint64_t snap[], uint32_t *resv_seen, uint64_t *resnaps, uint64_t age_us,
    uint64_t limit_us)
{
	bool ready = true;
	for (unsigned l = 0; l < 32u && (layers >> l) != 0; l++) {
		uint32_t bit = 1u << l;
		if (!(layers & bit)) { continue; }
		if (!g->busy(g->ctx, l)) { *resv_seen &= ~bit; continue; }
		if (g->resv(g->ctx, l)) { *resv_seen |= bit; ready = false; continue; }
		if (*resv_seen & bit) { *resv_seen &= ~bit; snap[l] = g->seq(g->ctx, l); (*resnaps)++; }
		if (!g->done(g->ctx, l, snap[l])) { ready = false; }
	}
	return ready ? SWEET_FB_HW_H6_READY : (age_us < limit_us ? SWEET_FB_HW_H6_WAIT : SWEET_FB_HW_H6_FORCED);
}

#endif /* SWEET_FB_HW_H */
