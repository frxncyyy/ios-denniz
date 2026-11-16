/* SPDX-License-Identifier: MIT */
/*
 * nord2_mdp_job.h -- 0671: MT6893 MDP job compiler, PURE part (stdint/stdbool only; host-tested by
 * gpu/build/mdp-20261114/test/t_mdp_job.c).  Turns a blit request (source/destination image, crop, rotation, flip, colour
 * conversion, scaling) into a list of register writes on the MDP window (mdpsys 0x1f000000 + offset) split in tiles, plus a
 * CPU reference model used by the kernel self-tests to check what the hardware wrote.
 *
 * Evidence (gpu/build/mdp-20261114/MDP_OFFLOAD.md section 7):
 *  [V-src]  register offsets: nord2-current-kernel mdp/mt6893 (bases, MOUT/SEL names, RDMA reset 0x008 + MON_STA_1 0x408 bit 8),
 *           upstream Linux mtk-mdp3 mdp_reg_rdma.h / mdp_reg_wrot.h / mdp_reg_rsz.h (offsets match this chip's dumps: 0xf00 bases).
 *  [V-dis]  libdpframework.so (mt6893, the phone's vendor file) disassembly: mutex id 4+n for RDMAn, MOD0 bits (RDMA0-3 0-3, FG 4-5,
 *           AAL0-3 6-9, HDR 10-11, RSZ0-3 12-15, WROT0-3 16-19, TDSHP0-3 20-23, TCC0-3 24-27), MOD1 bits COLOR0/1 0-1; mutex
 *           registers EN 0x20+0x20*id, SOF 0x2c+0x20*id (0 = single), MOD0 0x30+0x20*id, MOD1 0x34+0x20*id; topology tables
 *           s_moutMap / s_selInMap / s_selOutMap (MOUT = bitmask of outputs, SEL = index); RDMA GMCIF 0x10371 mask 0x30071;
 *           RDMA SRC_CON = id | swap<<14 | 0x20000 | 0x600; TRANSFORM_0 = en<<16 | mtx<<23 (mask 0x0f810000);
 *           WROT VIDO_CTRL = id | swap<<8 | 0x5000 | rot<<20 | flip<<24 (mask 0xf131510f).
 *  [I]      everything else (RSZ control/step encoding, WROT MAT_CTRL/MAIN_BUF fields, rotated-tile offsets, matrix ids
 *           4 = JPEG(full 601)->RGB, 6 = BT601->RGB, 7 = BT709->RGB, 1 = RGB->BT601 taken from the display IP's shared CSC enum).
 *           Every [I] field is exercised by a stage-2 self-test with a CPU reference; a capability is offered only when its test
 *           passed on this boot (nord2_mdp.inc).
 */
#ifndef NORD2_MDP_JOB_H
#define NORD2_MDP_JOB_H

/* ---- formats (named by memory byte order, iOS style) ----------------------------------------------------------------- */
enum { N2MF_NONE = 0, N2MF_BGRA = 1, N2MF_RGBA = 2, N2MF_NV12 = 3, N2MF_NV21 = 4 };
/* MTK hardware format ids (upstream mtk-mdp3-regs.h MDP_COLOR unique id / swap) */
#define N2MDP_HW_RGBA8888_ID  2u
#define N2MDP_HW_NV12_ID      12u

static inline bool n2mf_is_yuv(uint32_t f) { return f == N2MF_NV12 || f == N2MF_NV21; }
static inline bool n2mf_valid(uint32_t f) { return f >= N2MF_BGRA && f <= N2MF_NV21; }
static inline uint32_t n2mf_bpp(uint32_t f) { return n2mf_is_yuv(f) ? 1u : 4u; }   /* bytes per pixel of plane 0 */

/* ---- request ---------------------------------------------------------------------------------------------------------- */
struct n2mdp_img {
	uint32_t fmt, w, h;
	uint32_t addr[2];        /* device address of plane 0 / 1 (PA with the larb port in bypass, or IOVA when M4U1 translates) */
	uint32_t pitch[2];       /* bytes per row of plane 0 / 1 */
	uint8_t  full_range;     /* YUV only: 1 = 420f, 0 = 420v */
	uint8_t  bt709;          /* YUV only: matrix */
};
struct n2mdp_job {
	struct n2mdp_img src, dst;
	uint32_t cx, cy, cw, ch;  /* source crop (cw = 0: whole source) */
	uint8_t  rot;             /* clockwise quarter turns 0..3 */
	uint8_t  flip;            /* bit0 = mirror horizontally (applied before the rotation, iOS convention) */
	uint8_t  rgb_swap;        /* self-calibration: XOR applied to the RGB swap bits (stage 2 finds the right value) */
	uint8_t  tile_w;          /* tile width in units of 16 output pixels (0 = default 32 = 512 px) */
	uint8_t  wrot_lines;      /* 0676: WROT main-buffer line count (VIDO_MAIN_BUF_SIZE 14:8); 0 = n2mdp_wrot_lines() */
	uint8_t  allow_unverified; /* 0682: S2 diagnostic jobs only: compile multi-tile / RGB-source scaling despite the 0681 limit */
	uint8_t  pipe2;           /* 0690: unscaled job on the second pipe RDMA2 -> WROT2 (direct, no PQ/RSZ), so two jobs can run at once */
};

/* error codes of n2mdp_check() */
enum { N2MDP_OK = 0, N2MDP_E_FMT = 1, N2MDP_E_SIZE = 2, N2MDP_E_ALIGN = 3, N2MDP_E_CROP = 4, N2MDP_E_SCALE = 5, N2MDP_E_DST = 6,
	N2MDP_E_ADDR = 7, N2MDP_E_TILES = 8 };
#define N2MDP_MAX_DIM     8191u    /* 13-bit size fields */
#define N2MDP_MAX_SCALE   8u       /* we offer 1/8 .. 8x only */
#define N2MDP_MAX_TILES   64u

static inline uint32_t n2mdp_out_w(const struct n2mdp_job *j) { return (j->rot & 1u) ? j->dst.h : j->dst.w; }   /* pre-rotation */
static inline uint32_t n2mdp_out_h(const struct n2mdp_job *j) { return (j->rot & 1u) ? j->dst.w : j->dst.h; }
static inline uint32_t n2mdp_crop_w(const struct n2mdp_job *j) { return j->cw ? j->cw : j->src.w; }
static inline uint32_t n2mdp_crop_h(const struct n2mdp_job *j) { return j->ch ? j->ch : j->src.h; }
static inline bool n2mdp_scaled(const struct n2mdp_job *j) { return n2mdp_crop_w(j) != n2mdp_out_w(j) || n2mdp_crop_h(j) != n2mdp_out_h(j); }
/* plane bytes needed by an image (for range checks of the caller's buffers) */
static inline uint64_t n2mdp_plane_bytes(const struct n2mdp_img *im, unsigned p)
{
	if (p == 0) { return (uint64_t)im->pitch[0] * im->h; }
	return n2mf_is_yuv(im->fmt) ? (uint64_t)im->pitch[1] * ((im->h + 1u) / 2u) : 0;
}

static inline int
n2mdp_check(const struct n2mdp_job *j)
{
	if (!n2mf_valid(j->src.fmt) || !n2mf_valid(j->dst.fmt)) { return N2MDP_E_FMT; }
	const struct n2mdp_img *im[2] = { &j->src, &j->dst };
	for (unsigned i = 0; i < 2; i++) {
		if (im[i]->w < 2 || im[i]->h < 2 || im[i]->w > N2MDP_MAX_DIM || im[i]->h > N2MDP_MAX_DIM) { return N2MDP_E_SIZE; }
		if (im[i]->pitch[0] < im[i]->w * n2mf_bpp(im[i]->fmt) || (im[i]->pitch[0] & 15u)) { return N2MDP_E_ALIGN; }
		if (im[i]->addr[0] == 0 || (im[i]->addr[0] & 15u)) { return N2MDP_E_ADDR; }
		if (n2mf_is_yuv(im[i]->fmt)) {
			if ((im[i]->w | im[i]->h) & 1u) { return N2MDP_E_ALIGN; }
			if (im[i]->pitch[1] < im[i]->w || (im[i]->pitch[1] & 15u) || im[i]->addr[1] == 0 || (im[i]->addr[1] & 15u)) { return N2MDP_E_ALIGN; }
		}
	}
	if (n2mf_is_yuv(j->dst.fmt) && !n2mf_is_yuv(j->src.fmt)) { return N2MDP_E_DST; }        /* RGB -> YUV not offered */
	uint32_t cw = n2mdp_crop_w(j), ch = n2mdp_crop_h(j);
	if ((uint64_t)j->cx + cw > j->src.w || (uint64_t)j->cy + ch > j->src.h || cw < 2 || ch < 2) { return N2MDP_E_CROP; }
	if (n2mf_is_yuv(j->src.fmt) && ((j->cx | j->cy | cw | ch) & 1u)) { return N2MDP_E_CROP; }
	uint32_t ow = n2mdp_out_w(j), oh = n2mdp_out_h(j);
	if (ow * N2MDP_MAX_SCALE < cw || cw * N2MDP_MAX_SCALE < ow || oh * N2MDP_MAX_SCALE < ch || ch * N2MDP_MAX_SCALE < oh) { return N2MDP_E_SCALE; }
	if (j->rot > 3) { return N2MDP_E_SIZE; }
	/* 0677: the vendor's WROT mirror is a horizontal mirror of the DESTINATION after the rotation, our reference mirrors the
	 * source first; they agree only for rot 0 / 180-free cases, so mirror + rotation is refused until a phone run settles it */
	if ((j->flip & 1u) && (j->rot & 3u)) { return N2MDP_E_DST; }
	return N2MDP_OK;
}

/* ---- the register program ---------------------------------------------------------------------------------------------- */
/* MDP window offsets (mdpsys 0x1f000000 + ...) [V-src mdp_base.h] */
#define N2MDP_W_SYS     0x00000u
#define N2MDP_W_MUTEX   0x01000u
#define N2MDP_W_RDMA(n) (0x06000u + 0x1000u * (n))
#define N2MDP_W_AAL(n)  (0x0c000u + 0x1000u * (n))
#define N2MDP_W_RSZ(n)  (0x12000u + 0x1000u * (n))
#define N2MDP_W_WROT(n) (0x16000u + 0x1000u * (n))
#define N2MDP_W_TDSHP(n) (0x1a000u + 0x1000u * (n))
#define N2MDP_W_TCC(n)  (0x1e000u + 0x1000u * (n))
#define N2MDP_W_LARB2   0x03000u

/* pipes: 0 = RDMA0 -> WROT0 direct (no scaler; copy, rotate, flip, WROT colour matrix);
 *        2 = RDMA2 -> AAL2 -> RSZ2 -> TDSHP2 -> TCC2 -> WROT2 (scaling) [V-dis adjacency table] */
#define N2MDP_PIPE_DIRECT 0u
#define N2MDP_PIPE_SCALE  2u

/* op kinds of the program */
enum { N2MDP_OP_WRITE = 0, N2MDP_OP_POLL = 1, N2MDP_OP_GO = 2, N2MDP_OP_WAIT = 3, N2MDP_OP_END = 4 };
struct n2mdp_op { uint8_t op; uint32_t off, val, mask; };
/* WRITE: *(win+off) = (*(win+off) & ~mask) | (val & mask)    (mask 0xffffffff = plain write)
 * POLL : wait until (*(win+off) & mask) == val (timeout -> abort)
 * GO   : mutex EN for the tile (val = mutex id): the frame starts
 * WAIT : wait until the tile is done: (*(win+off) & mask) == val, the WROT's done condition */

#define N2MDP_MUTEX_ID(pipe)       (4u + (pipe))                    /* RDMA0 -> mutex 4, RDMA2 -> mutex 6 [V-dis] */
#define N2MDP_MUTEX_EN(id)         (N2MDP_W_MUTEX + 0x20u + 0x20u * (id))
#define N2MDP_MUTEX_SOF(id)        (N2MDP_W_MUTEX + 0x2cu + 0x20u * (id))
#define N2MDP_MUTEX_MOD0(id)       (N2MDP_W_MUTEX + 0x30u + 0x20u * (id))
#define N2MDP_MUTEX_MOD1(id)       (N2MDP_W_MUTEX + 0x34u + 0x20u * (id))
#define N2MDP_EV_WROT_DONE(pipe)   (0x123u - (pipe))                /* GCE-M events: wrot3 0x120 .. wrot0 0x123 [V-dt] */
#define N2MDP_EV_RDMA_DONE(pipe)   (0x133u - (pipe))                /* rdma3 0x130 .. rdma0 0x133 [V-dt] */

/* RDMA [V-src upstream] */
#define R_EN 0x000u
#define R_RESET 0x008u
#define R_CON 0x020u
#define R_GMCIF 0x028u
#define R_SRC_CON 0x030u
#define R_BKGD_B 0x060u
#define R_SRC_SIZE 0x070u
#define R_CLIP_SIZE 0x078u
#define R_OFFSET_1 0x080u
#define R_SF_BKGD_B 0x090u
#define R_SRC_END0 0x100u
#define R_SRC_END1 0x108u
#define R_OFFSET0 0x118u
#define R_OFFSET1 0x120u
#define R_TRANSFORM 0x200u
#define R_MON1 0x408u
#define R_BASE0 0xf00u
#define R_BASE1 0xf08u
/* RSZ */
#define Z_ENABLE 0x000u
#define Z_CTRL1 0x004u
#define Z_CTRL2 0x008u
#define Z_IN 0x010u
#define Z_OUT 0x014u
#define Z_HSTEP 0x018u
#define Z_VSTEP 0x01cu
#define Z_LH_INT 0x020u
#define Z_LH_SUB 0x024u
#define Z_LV_INT 0x028u
#define Z_LV_SUB 0x02cu
#define Z_CH_INT 0x030u
#define Z_CH_SUB 0x034u
/* WROT */
#define W_CTRL 0x000u
#define W_MAIN_BUF 0x008u
#define W_SOFT_RST 0x010u
#define W_SOFT_RST_STAT 0x014u
#define W_CROP_OFST 0x020u
#define W_TAR_SIZE 0x024u
#define W_OFST 0x02cu
#define W_STRIDE 0x030u
#define W_OFST_C 0x038u
#define W_STRIDE_C 0x03cu
#define W_DITHER 0x054u
#define W_RSV1 0x070u
#define W_IN_SIZE 0x078u
#define W_ROT_EN 0x07cu
#define W_MAT_CTRL 0x084u
#define W_BASE 0xf00u
#define W_BASE_C 0xf04u

/* topology registers (mdpsys) [V-dis tables] */
#define N2MDP_T_RDMA0_MOUT 0xf20u
#define N2MDP_T_RDMA2_MOUT 0xf28u
#define N2MDP_T_AAL2_SEL   0xf38u
#define N2MDP_T_RSZ2_SEL   0xf68u
#define N2MDP_T_WROT0_SEL  0xf70u
#define N2MDP_T_WROT2_SEL  0xf78u

struct n2mdp_tile { uint32_t ox0, ox1; uint32_t ix0, ix1; uint32_t lh_int, lh_sub; };
struct n2mdp_prog {
	struct n2mdp_op ops[2400];   /* 0676: + vendor per-tile RDMA buffer words, WROT INT clear */
	uint32_t n, tiles, pipe, mutex;
	uint32_t scaled;             /* 0690: 1 = scale lane (RDMA2 -> AAL2 -> RSZ2 -> TDSHP2 -> TCC2 -> WROT2); pipe 2 can also run direct */
	int err;
};

static inline void
n2mdp_emit(struct n2mdp_prog *p, uint8_t op, uint32_t off, uint32_t val, uint32_t mask)
{
	if (p->err) { return; }
	if (p->n >= sizeof(p->ops) / sizeof(p->ops[0]) - 1u) { p->err = N2MDP_E_TILES; return; }
	p->ops[p->n].op = op; p->ops[p->n].off = off; p->ops[p->n].val = val; p->ops[p->n].mask = mask; p->n++;
}
#define N2W(p, off, val) n2mdp_emit((p), N2MDP_OP_WRITE, (off), (val), 0xffffffffu)
#define N2WM(p, off, val, m) n2mdp_emit((p), N2MDP_OP_WRITE, (off), (val), (m))

/* scaler step, 1.15 fixed point of input pixels per output pixel (in-1)/(out-1) [I] */
static inline uint32_t n2mdp_step(uint32_t in, uint32_t out)
{
	if (out <= 1u || in <= 1u) { return 1u << 15; }
	return (uint32_t)((((uint64_t)(in - 1u)) << 15) / (out - 1u));
}

/* Split the pre-rotation output width into tiles and compute each tile's input span. */
static inline uint32_t
n2mdp_tiles(const struct n2mdp_job *j, struct n2mdp_tile *t, uint32_t max)
{
	uint32_t ow = n2mdp_out_w(j), cw = n2mdp_crop_w(j), tw = (j->tile_w ? j->tile_w : 32u) * 16u, n = 0;
	uint32_t step = n2mdp_step(cw, ow);
	bool scaled = n2mdp_scaled(j);
	for (uint32_t x = 0; x < ow && n < max; x += tw, n++) {
		uint32_t x1 = x + tw < ow ? x + tw : ow;
		t[n].ox0 = x; t[n].ox1 = x1;
		if (!scaled) {
			t[n].ix0 = x; t[n].ix1 = x1; t[n].lh_int = 0; t[n].lh_sub = 0;
		} else {
			uint64_t s0 = (uint64_t)x * step, s1 = (uint64_t)(x1 - 1u) * step;   /* first/last source position, 1.15 */
			uint32_t i0 = (uint32_t)(s0 >> 15), i1 = (uint32_t)(s1 >> 15) + 2u;   /* +1 right neighbour, +1 exclusive */
			uint32_t lo = i0 >= 4u ? i0 - 4u : 0u;                                   /* 4 px filter margin each side */
			uint32_t hi = i1 + 3u < cw ? i1 + 3u : cw;
			if (n2mf_is_yuv(j->src.fmt)) { lo &= ~1u; hi = (hi + 1u) & ~1u; if (hi > cw) { hi = cw; } }
			t[n].ix0 = lo; t[n].ix1 = hi;
			t[n].lh_int = i0 - lo; t[n].lh_sub = (uint32_t)(s0 & 0x7fffu) << 5;   /* subpixel field is 20 bit [I] */
		}
	}
	return n;
}

/* Destination byte offset of the top-left of a tile's footprint after flip + rotation (plane 0 / plane 1). */
static inline void
n2mdp_dst_offset(const struct n2mdp_job *j, const struct n2mdp_tile *t, uint32_t *o0, uint32_t *o1)
{
	/* 0677: the vendor's WROT start address (DpEngine_WROT::onConfigTile 0xbf30c, linear formats) [V-dis]: VIDO_OFST_ADDR points to
	 * the FIRST pixel the WROT writes, which for the mirrored / rotated walks is the LAST byte of that pixel (hence the "- 1").
	 * (x0, y0) = tile start in the unrotated output, W x H = unrotated output size, P = pitch, B = bytes per pixel, F = flip:
	 *   rot 0:   F0 y0*P + x0*B                     F1 y0*P + (W - x0)*B - 1
	 *   rot 90:  F0 x0*P + (H - y0)*B - 1           F1 x0*P + y0*B
	 *   rot 180: F0 (H-1-y0)*P + (W - x0)*B - 1     F1 (H-1-y0)*P + x0*B
	 *   rot 270: F0 (W-1-x0)*P + y0*B               F1 (W-1-x0)*P + (H - y0)*B - 1
	 * 0671 used the tile's top-left corner in the destination: right for rot 0 (copy256 passed on the phone) but for rot 90 the
	 * WROT walked backwards from offset 0, i.e. BELOW the destination buffer (q10jmdp6 rot90: every pixel wrong).  Our tiles span
	 * the whole height (y0 = 0).  YUV destinations keep the 0671 rot-0 formula (only RGB destinations are produced today). */
	uint32_t W = n2mdp_out_w(j), H = n2mdp_out_h(j), x0 = t->ox0, y0 = 0, P = j->dst.pitch[0], B = n2mf_bpp(j->dst.fmt);
	uint32_t F = j->flip & 1u;
	switch (j->rot & 3u) {
	case 0:  *o0 = F ? y0 * P + (W - x0) * B - 1u : y0 * P + x0 * B; break;
	case 1:  *o0 = F ? x0 * P + y0 * B : x0 * P + (H - y0) * B - 1u; break;
	case 2:  *o0 = F ? (H - 1u - y0) * P + x0 * B : (H - 1u - y0) * P + (W - x0) * B - 1u; break;
	default: *o0 = F ? (W - 1u - x0) * P + (H - y0) * B - 1u : (W - 1u - x0) * P + y0 * B; break;
	}
	*o1 = 0;
	if (n2mf_is_yuv(j->dst.fmt)) {   /* 0686: chroma plane = the same walk on the half-size grid, 2 bytes per UV pair [V-dis onConfigTile] */
		uint32_t cW = W / 2u, cH = H / 2u, cx = x0 / 2u, cy = y0 / 2u, cP = j->dst.pitch[1], cB = 2u;
		switch (j->rot & 3u) {
		case 0:  *o1 = F ? cy * cP + (cW - cx) * cB - 1u : cy * cP + cx * cB; break;
		case 1:  *o1 = F ? cx * cP + cy * cB : cx * cP + (cH - cy) * cB - 1u; break;
		case 2:  *o1 = F ? (cH - 1u - cy) * cP + cx * cB : (cH - 1u - cy) * cP + (cW - cx) * cB - 1u; break;
		default: *o1 = F ? (cW - 1u - cx) * cP + (cH - cy) * cB - 1u : (cW - 1u - cx) * cP + cy * cB; break;
		}
	}
}

/* Compile the job.  pipe chosen here: direct when not scaled, scale pipe otherwise. */
/* ======================================================================================================================
 * 0676: values the vendor userland (libdpframework.so lib64/mt6893, phone copy) programs and 0671 did not [V-dis]:
 *  RDMA (DpEngine_RDMA::onConfigTile 0xc1bd4 + calRDMASetting 0x215d34, 32 bpp = its default branch 0x21644c, executed under
 *  an emulator on the host for 64/256/512/1024 px to check this port): per tile 0x0c0 = a, 0x088 = (W*4)<<12 | 4a (mask 0x1ffff3ff),
 *  0x0d0 = (a*n)<<16 | n (mask 0x3fff1fff), 0x0d8 = b, 0x0b8 = 0 (mask 0x1ffff3ff), 0x0e8 = (b*m)<<16 | m (mask 0x3fff1fff) with
 *  W = tile width, a = W < 569 ? 8 : 5120/W, b = 2W < 569 ? 8 : 5120/(2W), n = roundup4(ceil(W/a)), m = roundup4(ceil(W/b)),
 *  a -= 1 if a*a*n > 5120, b -= 1 if 2*b*b*m > 5120.  Frame: RDMA_CON 0x020 = 0x20 (mask 0x1130), 0x2a8 = 0 (mask 7, 8-bit).
 *  WROT (DpEngine_WROT::onConfigFrame 0xbdc88 / onConfigTile 0xbf30c): frame VIDO_FIFO_TEST 0x080 = getWROTFIFOSetting() = 0x100,
 *  VIDO_ROT_EN 0x07c |= 0x900000; per tile VIDO_INT 0x01c = 1 before ROT_EN (the phone showed INT 0x7 left over before GO),
 *  VIDO_MAIN_BUF_SIZE = W<<16 | L<<8 with L from calWROTSetting (multiple of 4, ceil(W/L)*L*L <= 0x4000); 0671 wrote L = 1.
 * YUV sources keep the 0671 RDMA programming (their calRDMASetting branch is not ported yet). */
static inline uint32_t
n2mdp_wrot_lines(uint32_t tw)
{
	uint32_t L = 32u;
	while (L > 4u && ((tw + L - 1u) / L) * L * L > 0x4000u) { L -= 4u; }
	return L;
}
/* 0678: calRDMASetting NV12/NV21 branch (DpColorFormat 0x4c084c / 0x4c086c, 0x216174..0x2162d0) [V-dis + emulated: equal for every
 * width 2..5120]: a = W < 1025 ? 4 : 5120/W, n = roundup4(ceil(W/a)), m = roundup4((a + W/2 - 1)/a);
 * o = {a, W, a, a*n, n, a, W, 2a, a*m, m}, a -= 1 if a*a*n > 5120, o[5] -= 1 if 2*a*a*m > 5120. */
static inline void
n2mdp_rdma_buf_nv12(uint32_t W, uint32_t o[10])
{
	uint32_t a = W < 0x401u ? 4u : 5120u / W;
	if (a == 0u) { a = 1u; }
	uint32_t n = (a + W - 1u) / a, m = (a + (W >> 1) - 1u) / a;
	n = (n + 3u) & ~3u; m = (m + 3u) & ~3u;
	o[0] = a; o[1] = W; o[2] = a; o[3] = a * n; o[4] = n; o[5] = a; o[6] = W; o[7] = 2u * a; o[8] = a * m; o[9] = m;
	if (o[3] * a >= 0x1401u) { o[0] = a - 1u; }
	if (0x1401u <= 2u * (o[5] * o[8])) { o[5] = a - 1u; }
}

/* 0678: RSZ frame values as the vendor computes them [V-dis libpq_prot.so CPQRszFW::rsz_alg_det 0xba8c0 / rsz_config 0xbaec4 /
 * rsz_tbl_select 0xbb128 with the CPQRszFW::initialize defaults (thresholds 1 and 24, tables 9 / 7 / 17), port checked against an
 * emulation of those functions for 2315 size combinations] and packed like PQRSZAdaptor::calRegs (libdpframework 0x1100b0):
 *   algorithm per direction: in <= out -> 0 (6-tap); out < 3 or in > 24*out (vertical also (in-1)/(out-1) > 4096) -> 1; else 2
 *   alg 0: step = floor((in-1)/(out-1) * 2^15), table 7 if step == 2^15 else 9
 *   alg 1/2: step = in == out ? 2^20 : ceil((out-1)/(in-1) * 2^20), table 17; alg 2 truncation k: in <= out<<(6+k)
 *   PRZ_CONTROL_1 = 3 | (in <= out)<<4 | alg_h<<5 | alg_v<<7 | trunc_h<<10 | trunc_v<<13 | table_h<<16 | table_v<<21
 *   (calRegs forces the H/V enables to 1; the ultra-resolution / tap-adaptive words CONTROL_2, 0x050, 0x054..0x068, 0x22c.. are
 *   left 0 = their values when those features are off [I]).  Unscaled jobs never use the RSZ (vendor disables it). */
struct n2mdp_rszv { uint32_t alg_h, alg_v, step_h, step_v, ctrl1; };
static inline uint32_t
n2mdp_rsz_alg(uint32_t in, uint32_t out, bool vert)
{
	if (in <= out) { return 0; }
	if (out < 3u || in > 24u * out) { return 1; }
	if (vert && (in - 1u) / (out - 1u) > 0x1000u) { return 1; }
	return 2;
}
static inline uint32_t
n2mdp_rsz_step(uint32_t alg, uint32_t in, uint32_t out, uint32_t *trunc)
{
	*trunc = 0;
	if (alg == 0) { return out > 1u ? (uint32_t)((((uint64_t)(in - 1u)) << 15) / (out - 1u)) : (1u << 15); }
	if (in == out || in < 2u) { return 1u << 20; }
	if (alg == 2) { uint32_t k = 0; while (k < 6u && in > (out << (6u + k))) { k++; } *trunc = k; }
	return (uint32_t)(((((uint64_t)(out - 1u)) << 20) + (in - 2u)) / (in - 1u));
}
static inline void
n2mdp_rsz_values(uint32_t cw, uint32_t ch, uint32_t ow, uint32_t oh, struct n2mdp_rszv *r)
{
	uint32_t th, tv;
	r->alg_h = n2mdp_rsz_alg(cw, ow, false); r->alg_v = n2mdp_rsz_alg(ch, oh, true);
	r->step_h = n2mdp_rsz_step(r->alg_h, cw, ow, &th); r->step_v = n2mdp_rsz_step(r->alg_v, ch, oh, &tv);
	uint32_t tbh = r->alg_h ? 17u : (r->step_h == 0x8000u ? 7u : 9u), tbv = r->alg_v ? 17u : (r->step_v == 0x8000u ? 7u : 9u);
	r->ctrl1 = 3u | ((cw <= ow) ? 1u << 4 : 0u) | (r->alg_h << 5) | (r->alg_v << 7) | (th << 10) | (tv << 13) | (tbh << 16) | (tbv << 21);
}

static inline void
n2mdp_rdma_buf32(uint32_t W, uint32_t o[10])
{
	uint32_t a = W < 0x239u ? 8u : 5120u / W, b = 2u * W < 0x239u ? 8u : 5120u / (2u * W);
	if (a == 0u) { a = 1u; }                            /* W > 5120 / 2560: never for our <= 520 px tiles */
	if (b == 0u) { b = 1u; }
	uint32_t n = (a + W - 1u) / a, m = (W - 1u + b) / b;
	n = (n + 3u) & ~3u; m = (m + 3u) & ~3u;
	o[0] = a; o[1] = W * 4u; o[2] = a * 4u; o[3] = a * n; o[4] = n; o[5] = b; o[6] = 0; o[7] = 0; o[8] = b * m; o[9] = m;
	if (o[3] * a >= 0x1401u) { o[0] = a - 1u; }
	if (0x1401u <= 2u * (b * o[8])) { o[5] = b - 1u; }
}

static inline int
n2mdp_compile(const struct n2mdp_job *j, struct n2mdp_prog *p)
{
	p->n = 0; p->err = 0; p->tiles = 0;
	int e = n2mdp_check(j);
	if (e) { p->err = e; return e; }
	bool scaled = n2mdp_scaled(j);
	/* 0690: unscaled jobs may run on pipe 2 without its PQ / RSZ engines (RDMA2 -> WROT2), so pipe 0 and pipe 2 can work at once */
	uint32_t pipe = (scaled || j->pipe2) ? N2MDP_PIPE_SCALE : N2MDP_PIPE_DIRECT;
	uint32_t mid = N2MDP_MUTEX_ID(pipe);
	p->pipe = pipe; p->mutex = mid; p->scaled = scaled ? 1u : 0u;
	uint32_t R = N2MDP_W_RDMA(pipe), W = N2MDP_W_WROT(pipe), Z = N2MDP_W_RSZ(pipe);
	bool syuv = n2mf_is_yuv(j->src.fmt), dyuv = n2mf_is_yuv(j->dst.fmt);
	uint32_t cw = n2mdp_crop_w(j), ch = n2mdp_crop_h(j), ow = n2mdp_out_w(j), oh = n2mdp_out_h(j);

	/* topology + mutex modules */
	if (pipe == N2MDP_PIPE_DIRECT) {
		N2W(p, N2MDP_T_RDMA0_MOUT, 1u << 1);          /* RDMA0 out slot 1 = WROT0 */
		N2W(p, N2MDP_T_WROT0_SEL, 2u);                /* WROT0 in slot 2 = RDMA0 */
		N2W(p, N2MDP_MUTEX_MOD0(mid), (1u << 0) | (1u << 16));
	} else if (!scaled) {
		/* 0690 [V-dis libdpframework.so mt6893 tables, read from the binary]: DpPathConnection::s_moutMap RDMA2 (engine 14, reg 0xf28)
		 * outputs { 26 AAL2, 42 WROT2, 28, 20 } -> slot 1 = WROT2; s_selInMap WROT2 (engine 42, reg 0xf78) inputs { 38 TCC2, 14 RDMA2 }
		 * -> slot 1 = RDMA2 (same tables give 0671's RDMA0 MOUT bit 1 / WROT0 SEL 2 and the scale-lane values used below) */
		N2W(p, N2MDP_T_RDMA2_MOUT, 1u << 1);          /* RDMA2 out slot 1 = WROT2 */
		N2W(p, N2MDP_T_WROT2_SEL, 1u);                /* WROT2 in slot 1 = RDMA2 */
		N2W(p, N2MDP_MUTEX_MOD0(mid), (1u << 2) | (1u << 18));   /* RDMA2 | WROT2 */
	} else {
		N2W(p, N2MDP_T_RDMA2_MOUT, 1u << 0);          /* RDMA2 out slot 0 = AAL2 */
		N2W(p, N2MDP_T_AAL2_SEL, 0u);                 /* AAL2 in slot 0 = RDMA2 */
		N2W(p, N2MDP_T_RSZ2_SEL, 0u);                 /* RSZ2 in slot 0 = AAL2 */
		N2W(p, N2MDP_T_WROT2_SEL, 0u);                /* WROT2 in slot 0 = TCC2 */
		N2W(p, N2MDP_MUTEX_MOD0(mid), (1u << 2) | (1u << 8) | (1u << 14) | (1u << 22) | (1u << 26) | (1u << 18));
	}
	N2W(p, N2MDP_MUTEX_MOD1(mid), 0);
	N2WM(p, N2MDP_MUTEX_SOF(mid), 0, 7u);               /* single (memory to memory) */

	/* engine reset + frame config */
	N2WM(p, R + R_RESET, 1u, 1u);
	n2mdp_emit(p, N2MDP_OP_POLL, R + R_MON1, 1u << 8, 1u << 8);
	N2WM(p, R + R_RESET, 0u, 1u);
	N2WM(p, W + W_SOFT_RST, 1u, 1u);
	n2mdp_emit(p, N2MDP_OP_POLL, W + W_SOFT_RST_STAT, 1u, 1u);
	N2WM(p, W + W_SOFT_RST, 0u, 1u);
	n2mdp_emit(p, N2MDP_OP_POLL, W + W_SOFT_RST_STAT, 0u, 1u);

	uint32_t sbpp = n2mf_bpp(j->src.fmt);
	uint32_t s_id = syuv ? N2MDP_HW_NV12_ID : N2MDP_HW_RGBA8888_ID;
	uint32_t s_swap = (j->src.fmt == N2MF_RGBA || j->src.fmt == N2MF_NV21) ? 1u : 0u;
	if (!syuv) { s_swap ^= (j->rgb_swap & 1u); }
	N2WM(p, R + R_GMCIF, 0x00010371u, 0x00030071u);
	N2WM(p, R + R_SRC_CON, s_id | (s_swap << 14) | 0x20000u | 0x600u, 0x038cfe0fu);
	N2WM(p, R + R_CON, 0x20u, 0x1130u);                 /* 0676: vendor sets bit 5 */
	N2WM(p, R + 0x2a8u, 0u, 7u);                        /* 0676: 8-bit source (vendor 0x2a8 = 0 / 7 for 10-bit block) */
	N2W(p, R + R_BASE0, j->src.addr[0]);
	N2W(p, R + R_SRC_END0, (uint32_t)(j->src.addr[0] + n2mdp_plane_bytes(&j->src, 0) - 1u));
	N2W(p, R + R_BKGD_B, j->src.pitch[0]);
	if (syuv) {
		N2W(p, R + R_BASE1, j->src.addr[1]);
		N2W(p, R + R_SRC_END1, (uint32_t)(j->src.addr[1] + n2mdp_plane_bytes(&j->src, 1) - 1u));
		N2W(p, R + R_SF_BKGD_B, j->src.pitch[1]);
	}
	/* colour: the scale pipe works in YUV, so RGB sources are converted at the RDMA (RGB -> BT601 full) and back at the WROT;
	 * the direct pipe converts only YUV -> RGB at the WROT */
	uint32_t r_mtx = 0, r_en = 0;
	if (scaled && !syuv) { r_en = 1; r_mtx = 0; /* RGB_TO_JPEG */ }
	N2WM(p, R + R_TRANSFORM, (r_en << 16) | (r_mtx << 23), 0x0f810000u);

	if (scaled) {
		/* PQ engines of the scale pipe.  AAL2 / TDSHP2: [I] relay (their vendor programming goes through PQEngineInterface
		 * DpEngineAAL / DpEngineTDSHP algorithms that were not decoded); vendor-seen writes kept: AAL EN 0x000 = 1 (onInitEngine),
		 * AAL 0x200 bit 7, TDSHP 0x100 bit 2.  TCC2: [V-dis DpEngine_TCC] 0x000 = enable | width<<1 | height<<15 | 1<<29 (per tile). */
		N2WM(p, N2MDP_W_AAL(2) + 0x020u, 1u, 1u);
		N2W(p, N2MDP_W_AAL(2) + 0x000u, 1u);
		N2WM(p, N2MDP_W_AAL(2) + 0x200u, 0x80u, 0x80u);
		/* 0680 [V-dis DpEngine_TDSHP::onInitEngine 0x15f5e4, = upstream mtk-mdp3 init_tdshp]: TDSHP_CTRL 0x100 bit 0 = enable
		 * (q10jmdp9 had it 0 = TDSHP2 off, the pipe stalled after 4 lines), TDSHP_CFG 0x110 bit 1 = FIFO enable; TDSHP_CFG bit 0 =
		 * relay [I: the vendor value comes from the PQ algorithm; upstream writes tdshp.cfg there]; CTRL bit 2 = 0 (vendor frame). */
		N2WM(p, N2MDP_W_TDSHP(2) + 0x100u, 1u, 1u | 4u);
		N2WM(p, N2MDP_W_TDSHP(2) + 0x110u, 3u, 3u);
		N2WM(p, N2MDP_W_TCC(2) + 0x000u, 1u | (1u << 29), 1u | (1u << 29));
		/* RSZ [V-dis DpEngine_SCL::onInitEngine / onConfigFrame / calRegs]: reset pulse, enable, 0x22c = 0, CONTROL_1/2, steps */
		struct n2mdp_rszv rv;
		n2mdp_rsz_values(cw, ch, ow, oh, &rv);
		N2WM(p, Z + Z_ENABLE, 0x10000u, 1u << 16);
		N2WM(p, Z + Z_ENABLE, 0u, 1u << 16);
		N2WM(p, Z + Z_ENABLE, 1u, 1u);
		N2W(p, Z + 0x22cu, 0u);
		N2WM(p, Z + 0x050u, 0u, 0xffffcu);
		N2WM(p, Z + Z_CTRL1, rv.ctrl1, 0x03fffdf3u);
		N2WM(p, Z + Z_CTRL2, 0, 0x0fffc290u);
		N2W(p, Z + Z_HSTEP, rv.step_h);
		N2W(p, Z + Z_VSTEP, rv.step_v);
	}

	/* WROT frame */
	uint32_t d_id = dyuv ? N2MDP_HW_NV12_ID : N2MDP_HW_RGBA8888_ID;
	uint32_t d_swap = (j->dst.fmt == N2MF_RGBA || j->dst.fmt == N2MF_NV21) ? 1u : 0u;
	if (!dyuv) { d_swap ^= (j->rgb_swap & 1u); }
	uint32_t w_mat = 0;
	bool yuv_in_pipe = syuv || scaled;
	if (yuv_in_pipe && !dyuv) {
		uint32_t m = 4u;                                         /* JPEG (full 601) -> RGB */
		if (syuv) { m = j->src.bt709 ? 7u : (j->src.full_range ? 4u : 6u); }
		w_mat = 1u | (m << 4);
	}
	uint32_t flip = j->flip & 1u;
	N2W(p, W + W_BASE, j->dst.addr[0]);
	if (dyuv) { N2W(p, W + W_BASE_C, j->dst.addr[1]); N2W(p, W + W_STRIDE_C, j->dst.pitch[1]); }
	/* 0686: YUV 4:2:0 destinations: CTRL bits 28 / 30 (chroma sample selection) from DpEngine_WROT::s_uvTable (libdpframework
	 * 0x60cb8, indexed by vertical subsampling, rotation, flip) [V-dis]; 0671 set both bits for every rotation [I] */
	static const uint8_t uvtab[4][2][2] = { { { 0, 0 }, { 1, 0 } }, { { 0, 1 }, { 0, 0 } }, { { 1, 1 }, { 0, 1 } }, { { 1, 0 }, { 1, 1 } } };
	uint32_t uvb = dyuv ? (((uint32_t)uvtab[j->rot & 3u][flip][0] << 28) | ((uint32_t)uvtab[j->rot & 3u][flip][1] << 30)) : 0u;
	N2WM(p, W + W_CTRL, d_id | (d_swap << 8) | 0x5000u | ((uint32_t)(j->rot & 3u) << 20) | (flip << 24) | uvb, 0xf131510fu);
	N2W(p, W + W_STRIDE, j->dst.pitch[0]);
	N2WM(p, W + W_MAT_CTRL, w_mat, 0xf3u);
	N2WM(p, W + W_DITHER, 0xff000000u, 0xff000000u);
	N2WM(p, W + W_RSV1, 1u << 31, 1u << 31);
	N2W(p, W + 0x080u, 0x100u);                         /* 0676: VIDO_FIFO_TEST = getWROTFIFOSetting() */
	N2WM(p, W + W_ROT_EN, 0x900000u, 0x900000u);        /* 0676: vendor frame bits 20 / 23 */

	/* tiles */
	struct n2mdp_tile t[N2MDP_MAX_TILES];
	uint32_t nt = n2mdp_tiles(j, t, N2MDP_MAX_TILES);
	if (nt == 0 || t[nt - 1].ox1 != ow) { p->err = N2MDP_E_TILES; return p->err; }
	/* 0681: known limitation: scaling is supported for a single tile (output width <= the tile width, 512 px by default) and
	 * NV12 sources only.  q10jmdp10 [V-phone]: every single-tile NV12 scale job passes; the 3-tile BGRA-source scaletile job
	 * mismatched (RSZ tile offsets / filter overlap beyond the first tile and the RDMA RGB->YUV matrix are not decoded). */
	/* 0683: multi-tile NV12 scaling verified on hardware (q10jmdp12s3 nv12scale3t / nv12scale3o PASS): only RGB-source scaling
	 * stays behind a flag (set when the boot's smooth-pattern RGB scale jobs passed, N2MDP_CAP_RGB_SCALE) */
	if (scaled && !syuv && !j->allow_unverified) { p->err = N2MDP_E_SCALE; return p->err; }
	for (uint32_t k = 0; k < nt; k++) {
		uint32_t iw = t[k].ix1 - t[k].ix0, tw = t[k].ox1 - t[k].ox0;
		uint32_t sx = j->cx + t[k].ix0;
		/* RDMA tile */
		N2WM(p, R + R_EN, 1u, 1u);
		N2W(p, R + R_OFFSET0, j->cy * j->src.pitch[0] + sx * sbpp);
		if (syuv) { N2W(p, R + R_OFFSET1, (j->cy / 2u) * j->src.pitch[1] + sx); }
		N2W(p, R + R_SRC_SIZE, (ch << 16) | iw);
		N2W(p, R + R_CLIP_SIZE, (ch << 16) | iw);
		N2WM(p, R + R_OFFSET_1, 0, 0x003f001fu);
		if ((!syuv && sbpp == 4u) || j->src.fmt == N2MF_NV12) {   /* 0676/0678: vendor RDMA buffer words (calRDMASetting) */
			uint32_t o[10];
			if (syuv) { n2mdp_rdma_buf_nv12(iw, o); } else { n2mdp_rdma_buf32(iw, o); }
			N2W(p, R + 0x0c0u, o[0]);
			N2WM(p, R + 0x088u, (o[1] << 12) + o[2], 0x1ffff3ffu);
			N2WM(p, R + 0x0d0u, (o[3] << 16) + o[4], 0x3fff1fffu);
			N2W(p, R + 0x0d8u, o[5]);
			N2WM(p, R + 0x0b8u, (o[6] << 12) + o[7], 0x1ffff3ffu);
			N2WM(p, R + 0x0e8u, (o[8] << 16) + o[9], 0x3fff1fffu);
		}
		if (scaled) {
			/* 0678 [V-dis DpEngine_SCL::onConfigTile]: CONTROL_2 tile bits (mask 0x3800: bit 13 = not the last tile; bits 11/12 [I]
			 * left 0), INPUT/OUTPUT_IMAGE, luma/chroma offsets.  Offset units follow rsz_config: alg 0 = input pixels + 15-bit fraction,
			 * alg 1/2 = output domain, 20-bit fraction (offset * step).  Chroma = luma (the pipe is 4:4:4 after the RDMA). */
			struct n2mdp_rszv rv;
			n2mdp_rsz_values(cw, ch, ow, oh, &rv);
			uint32_t oi = t[k].lh_int, of = t[k].lh_sub >> 5;
			if (rv.alg_h) {
				uint64_t v = ((((uint64_t)oi) << 15) + of) * rv.step_h >> 15;   /* 2^-20 units of output */
				oi = (uint32_t)(v >> 20); of = (uint32_t)(v & 0xfffffu);
			}
			/* 0682: multi-tile jobs also get the other two vendor tile bits [V-dis DpEngine_SCL::onConfigTile: bit 11 = 0x800 when the
			 * tile input starts after column 0 (and an engine flag, assumed set), bit 12 = bit 0 of the inclusive input end column];
			 * single-tile jobs keep the q10jmdp10-verified value (bit 13 only) */
			uint32_t c2t = (t[k].ox1 < ow) ? (1u << 13) : 0u;
			if (nt > 1u) { c2t |= (t[k].ix0 > 0u ? (1u << 11) : 0u) | (((t[k].ix1 - 1u) & 1u) << 12); }
			N2WM(p, Z + Z_CTRL2, c2t, 0x3800u);
			N2W(p, Z + Z_IN, (ch << 16) | iw);
			N2W(p, Z + Z_OUT, (oh << 16) | tw);
			N2W(p, Z + Z_LH_INT, oi);
			N2W(p, Z + Z_LH_SUB, of);
			N2W(p, Z + Z_LV_INT, 0);
			N2W(p, Z + Z_LV_SUB, 0);
			N2W(p, Z + Z_CH_INT, oi);
			N2W(p, Z + Z_CH_SUB, of);
			N2WM(p, N2MDP_W_TCC(2) + 0x000u, ((oh << 15) & 0x1fff8000u) | ((tw << 1) & 0x7ffeu), 0x1ffffffeu);
			/* 0679: tile sizes of the two PQ engines [V-dis PQEngineInterface::DpEngineAALCongTile 0xdcb6c /
			 * DpEngineTDSHPConfigTile 0xe0784]; q10jfinal5a showed the pipe stalling after 4 RDMA2 lines with them unset.
			 * AAL2 (before the RSZ, no crop): 0x030 SIZE = in_w<<16 | in_h, 0x034 OUTPUT_SIZE = out_w<<16 | out_h, 0x038 OUTPUT_OFFSET.
			 * TDSHP2 (after the RSZ): 0x120 INPUT_SIZE, 0x124 OUTPUT_OFFSET, 0x128 OUTPUT_SIZE (w<<16 | h), 0x064 / 0x068 window
			 * x/y = (end<<16 | start) relative to the tile input (the vendor clamps start with two more parameters [I: 0]). */
			N2W(p, N2MDP_W_AAL(2) + 0x030u, (iw << 16) | ch);
			N2W(p, N2MDP_W_AAL(2) + 0x034u, (iw << 16) | ch);
			N2W(p, N2MDP_W_AAL(2) + 0x038u, 0u);
			N2W(p, N2MDP_W_TDSHP(2) + 0x120u, (tw << 16) | oh);
			N2W(p, N2MDP_W_TDSHP(2) + 0x124u, 0u);
			N2W(p, N2MDP_W_TDSHP(2) + 0x128u, (tw << 16) | oh);
			N2W(p, N2MDP_W_TDSHP(2) + 0x064u, (tw - 1u) << 16);
			N2W(p, N2MDP_W_TDSHP(2) + 0x068u, (oh - 1u) << 16);
		}
		/* WROT tile */
		uint32_t o0, o1;
		n2mdp_dst_offset(j, &t[k], &o0, &o1);
		N2W(p, W + W_OFST, o0);
		if (dyuv) { N2W(p, W + W_OFST_C, o1); }
		N2W(p, W + W_IN_SIZE, (oh << 16) | tw);
		N2W(p, W + W_TAR_SIZE, (oh << 16) | tw);
		N2W(p, W + W_CROP_OFST, 0);
		uint32_t wl = j->wrot_lines ? j->wrot_lines : n2mdp_wrot_lines(tw);
		N2WM(p, W + W_MAIN_BUF, (tw << 16) | ((wl & 0x7fu) << 8), 0x1fff7f00u);
		N2W(p, W + 0x01cu, 1u);                         /* 0676: VIDO_INT = 1 before the enable, as the vendor does */
		N2WM(p, W + W_ROT_EN, 1u, 1u);
		/* go + wait: GO clears the WROT frame-done GCE event and sets the mutex EN; WAIT polls that event through the GCE-M
		 * token registers (CPU-readable; live.dts mdp_wrot0_write_frame_done = 0x123, wrot2 = 0x121) and logs the other
		 * done indicators (mutex EN/INTSTA, RDMA MON_STA_1) the first time (nord2_mdp.inc n2mdp_run) */
		n2mdp_emit(p, N2MDP_OP_GO, N2MDP_MUTEX_EN(mid), N2MDP_EV_WROT_DONE(pipe), 1u);
		n2mdp_emit(p, N2MDP_OP_WAIT, N2MDP_MUTEX_EN(mid), N2MDP_EV_WROT_DONE(pipe), 1u);
		N2WM(p, N2MDP_MUTEX_EN(mid), 0u, 1u);
		N2WM(p, W + W_ROT_EN, 0u, 1u);
		N2WM(p, R + R_EN, 0u, 1u);
	}
	if (scaled) { N2WM(p, Z + Z_ENABLE, 0u, 1u); }
	n2mdp_emit(p, N2MDP_OP_END, 0, 0, 0);
	p->tiles = nt;
	return p->err;
}

/* ---- CPU reference --------------------------------------------------------------------------------------------------- */
/* Reads one source pixel as R,G,B (0..255) */
static inline void
n2mdp_ref_px(const struct n2mdp_img *im, const uint8_t *p0, const uint8_t *p1, uint32_t x, uint32_t y, int *r, int *g, int *b)
{
	if (!n2mf_is_yuv(im->fmt)) {
		const uint8_t *q = p0 + (uint64_t)y * im->pitch[0] + (uint64_t)x * 4u;
		if (im->fmt == N2MF_BGRA) { *b = q[0]; *g = q[1]; *r = q[2]; } else { *r = q[0]; *g = q[1]; *b = q[2]; }
		return;
	}
	int Y = p0[(uint64_t)y * im->pitch[0] + x];
	const uint8_t *c = p1 + (uint64_t)(y / 2u) * im->pitch[1] + (x & ~1u);
	int U = im->fmt == N2MF_NV12 ? c[0] : c[1], V = im->fmt == N2MF_NV12 ? c[1] : c[0];
	int yy, cr, cg1, cg2, cb;     /* fixed point 1.10 BT.601 / 709, full or video range */
	if (im->full_range) { yy = Y * 1024; } else { yy = (Y - 16) * 1192; }
	int u = U - 128, v = V - 128;
	if (im->bt709) { cr = im->full_range ? 1613 : 1836; cg1 = im->full_range ? 192 : 218; cg2 = im->full_range ? 479 : 546; cb = im->full_range ? 1900 : 2163; }
	else { cr = im->full_range ? 1436 : 1634; cg1 = im->full_range ? 352 : 401; cg2 = im->full_range ? 731 : 832; cb = im->full_range ? 1815 : 2066; }
	int R = (yy + cr * v + 512) >> 10, G = (yy - cg1 * u - cg2 * v + 512) >> 10, B = (yy + cb * u + 512) >> 10;
	*r = R < 0 ? 0 : R > 255 ? 255 : R; *g = G < 0 ? 0 : G > 255 ? 255 : G; *b = B < 0 ? 0 : B > 255 ? 255 : B;
}

/* Expected output pixel (x,y) of the destination (RGB destinations only); bilinear for scaled jobs. */
static inline void
n2mdp_ref_out(const struct n2mdp_job *j, const uint8_t *s0, const uint8_t *s1, uint32_t x, uint32_t y, int *r, int *g, int *b)
{
	uint32_t ow = n2mdp_out_w(j), oh = n2mdp_out_h(j), u, v;   /* (u,v) position in the unrotated, scaled picture */
	switch (j->rot & 3u) {
	case 0: u = x; v = y; break;
	case 1: u = y; v = oh - 1u - x; break;            /* 90 cw: dst(x,y) = src(y, H-1-x) */
	case 2: u = ow - 1u - x; v = oh - 1u - y; break;
	default: u = ow - 1u - y; v = x; break;           /* 270 */
	}
	if (j->flip & 1u) { u = ow - 1u - u; }
	uint32_t cw = n2mdp_crop_w(j), ch = n2mdp_crop_h(j);
	if (!n2mdp_scaled(j)) { n2mdp_ref_px(&j->src, s0, s1, j->cx + u, j->cy + v, r, g, b); return; }
	uint64_t fx = (uint64_t)u * n2mdp_step(cw, ow), fy = (uint64_t)v * n2mdp_step(ch, oh);
	uint32_t x0 = (uint32_t)(fx >> 15), y0 = (uint32_t)(fy >> 15);
	uint32_t ax = (uint32_t)(fx & 0x7fff), ay = (uint32_t)(fy & 0x7fff);
	uint32_t x1 = x0 + 1u < cw ? x0 + 1u : x0, y1 = y0 + 1u < ch ? y0 + 1u : y0;
	int c[4][3];
	n2mdp_ref_px(&j->src, s0, s1, j->cx + x0, j->cy + y0, &c[0][0], &c[0][1], &c[0][2]);
	n2mdp_ref_px(&j->src, s0, s1, j->cx + x1, j->cy + y0, &c[1][0], &c[1][1], &c[1][2]);
	n2mdp_ref_px(&j->src, s0, s1, j->cx + x0, j->cy + y1, &c[2][0], &c[2][1], &c[2][2]);
	n2mdp_ref_px(&j->src, s0, s1, j->cx + x1, j->cy + y1, &c[3][0], &c[3][1], &c[3][2]);
	int out[3];
	for (int k = 0; k < 3; k++) {
		int64_t top = (int64_t)c[0][k] * (32768 - ax) + (int64_t)c[1][k] * ax;
		int64_t bot = (int64_t)c[2][k] * (32768 - ax) + (int64_t)c[3][k] * ax;
		out[k] = (int)((top * (32768 - ay) + bot * ay + (1ll << 29)) >> 30);
	}
	*r = out[0]; *g = out[1]; *b = out[2];
}

/* Compare an RGB destination against the reference.  Returns 0 = match within tolerance; fills stats.
 * exact: max error must be 0 (copy/rotate/flip of RGB); else mean <= 6 and 99.x % of samples within 32 (scaler/CSC). */
struct n2mdp_cmp { uint32_t samples, max_err, sum_err, over, swapped_better; uint32_t first_x, first_y; };
static inline int
n2mdp_compare(const struct n2mdp_job *j, const uint8_t *s0, const uint8_t *s1, const uint8_t *d0, bool exact, struct n2mdp_cmp *c)
{
	c->samples = c->max_err = c->sum_err = c->over = c->swapped_better = 0; c->first_x = c->first_y = 0xffffffffu;
	uint32_t W = j->dst.w, H = j->dst.h, sw = 0, sn = 0;
	uint32_t stepx = W > 64 ? W / 64u : 1u, stepy = H > 64 ? H / 64u : 1u;
	for (uint32_t y = 0; y < H; y += (exact ? 1u : stepy)) {
		for (uint32_t x = 0; x < W; x += (exact ? 1u : stepx)) {
			int r, g, b;
			n2mdp_ref_out(j, s0, s1, x, y, &r, &g, &b);
			const uint8_t *q = d0 + (uint64_t)y * j->dst.pitch[0] + (uint64_t)x * 4u;
			int R, G, B;
			if (j->dst.fmt == N2MF_BGRA) { B = q[0]; G = q[1]; R = q[2]; } else { R = q[0]; G = q[1]; B = q[2]; }
			int e = (R > r ? R - r : r - R); int e2 = (G > g ? G - g : g - G); int e3 = (B > b ? B - b : b - B);
			int m = e > e2 ? e : e2; m = m > e3 ? m : e3;
			int es = (B > r ? B - r : r - B) + e2 + (R > b ? R - b : b - R);          /* the same pixel with R/B swapped */
			sw += (uint32_t)es; sn += (uint32_t)(e + e2 + e3);
			c->samples++; c->sum_err += (uint32_t)m;
			if ((uint32_t)m > c->max_err) { c->max_err = (uint32_t)m; }
			if (m > (exact ? 0 : 32)) { if (c->over == 0) { c->first_x = x; c->first_y = y; } c->over++; }
		}
	}
	c->swapped_better = sw * 2u < sn;
	if (exact) { return c->over ? 1 : 0; }
	return (c->sum_err > 6u * c->samples || c->over * 100u > c->samples) ? 1 : 0;
}

/* 0686: NV12 destination check (unscaled jobs): Y per pixel, UV per 2x2 block, mapped through the rotation like n2mdp_ref_out */
static inline int
n2mdp_compare_nv12(const struct n2mdp_job *j, const uint8_t *s0, const uint8_t *s1, const uint8_t *d0, const uint8_t *d1, struct n2mdp_cmp *c)
{
	c->samples = c->max_err = c->sum_err = c->over = c->swapped_better = 0; c->first_x = c->first_y = 0xffffffffu;
	uint32_t W = j->dst.w, H = j->dst.h, ow = n2mdp_out_w(j), oh = n2mdp_out_h(j);
	for (uint32_t y = 0; y < H; y++) {
		for (uint32_t x = 0; x < W; x++) {
			uint32_t u, v;
			switch (j->rot & 3u) {
			case 0: u = x; v = y; break;
			case 1: u = y; v = oh - 1u - x; break;
			case 2: u = ow - 1u - x; v = oh - 1u - y; break;
			default: u = ow - 1u - y; v = x; break;
			}
			if (j->flip & 1u) { u = ow - 1u - u; }
			int e = (int)d0[(uint64_t)y * j->dst.pitch[0] + x] - (int)s0[(uint64_t)(j->cy + v) * j->src.pitch[0] + j->cx + u];
			int m = e < 0 ? -e : e;
			if (!(x & 1u) && !(y & 1u)) {
				const uint8_t *dc = d1 + (uint64_t)(y / 2u) * j->dst.pitch[1] + x, *sc = s1 + (uint64_t)((j->cy + v) / 2u) * j->src.pitch[1] + ((j->cx + u) & ~1u);
				for (int k = 0; k < 2; k++) { int ek = (int)dc[k] - (int)sc[k]; if (ek < 0) { ek = -ek; } if (ek > m) { m = ek; } }
			}
			c->samples++; c->sum_err += (uint32_t)m;
			if ((uint32_t)m > c->max_err) { c->max_err = (uint32_t)m; }
			if (m > 32) { if (c->over == 0) { c->first_x = x; c->first_y = y; } c->over++; }
		}
	}
	return (c->sum_err > 6u * c->samples || c->over * 100u > c->samples) ? 1 : 0;
}

/* CRC32 (for the log) */
static inline uint32_t
n2mdp_crc32(const uint8_t *p, uint64_t n, uint32_t crc)
{
	crc = ~crc;
	for (uint64_t i = 0; i < n; i++) { crc ^= p[i]; for (int k = 0; k < 8; k++) { crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u))); } }
	return ~crc;
}

/* deterministic test pattern: RGB gradients + 8x8 checker + a corner marker (orientation is visible in every rotation) */
static inline void
n2mdp_pattern(const struct n2mdp_img *im, uint8_t *p0, uint8_t *p1, uint32_t seed)
{
	for (uint32_t y = 0; y < im->h; y++) {
		for (uint32_t x = 0; x < im->w; x++) {
			uint32_t r = (x * 255u) / (im->w - 1u), g = (y * 255u) / (im->h - 1u);
			uint32_t b = (((x >> 3) ^ (y >> 3)) & 1u) ? 200u : 40u;
			if (seed & 0x100u) { b = ((x + y) * 255u) / (im->w + im->h - 2u); }           /* 0683: smooth variant, no sharp chroma edges */
			else if (x < 16 && y < 16) { r = 255; g = 255; b = 255; }                    /* top-left marker */
			r ^= seed & 7u;
			if (!n2mf_is_yuv(im->fmt)) {
				uint8_t *q = p0 + (uint64_t)y * im->pitch[0] + (uint64_t)x * 4u;
				if (im->fmt == N2MF_BGRA) { q[0] = (uint8_t)b; q[1] = (uint8_t)g; q[2] = (uint8_t)r; }
				else { q[0] = (uint8_t)r; q[1] = (uint8_t)g; q[2] = (uint8_t)b; }
				q[3] = 0xff;
			} else {
				p0[(uint64_t)y * im->pitch[0] + x] = (uint8_t)(16u + (r * 3u + g * 5u + b) * 219u / 2295u);
				if (!(x & 1u) && !(y & 1u)) {
					uint8_t *c = p1 + (uint64_t)(y / 2u) * im->pitch[1] + x;
					c[0] = (uint8_t)(64u + (x * 128u) / im->w); c[1] = (uint8_t)(64u + (y * 128u) / im->h);
				}
			}
		}
	}
}
#endif /* NORD2_MDP_JOB_H */
