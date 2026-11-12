/*
 * 0402 host test (hwmulti-20261113): up to 6 overlay slots per pipe (OVL*_2L layers 0/1 + the cascaded 4L OVL's layers 0..3),
 * arbitrary layer rects (source crop, destination offset, clipping to each 540-column half), plane alpha, the H6 fence.
 *
 *  (1) Random scenes of 1..6 layers: the plan's REGISTER LISTS (sweet_fb_hw_regs + sweet_fb_hw_regs4) are written into a simulated
 *      register file of both blocks of both pipes, and a pixel model of the cascade reads the panel back FROM THOSE REGISTERS
 *      (2L: BGCLR, layers 0/1; 4L: background = 2L output when DATAPATH_CON bit 2, layers 0..3; CON decoded by
 *      sweet_fb_hw_con_decode; AEN 0 opaque, premultiplied source-over, plane alpha s*pa).  Compared with
 *        - the CPU path itself (sweet_fb_layer_clip + sweet_fb_compose_row, sweet_fb_blend.h) when no plane alpha is used, and
 *        - an independent reference (lowest covering layer copied, the others premultiplied over it, plane alpha s*pa).
 *      Every read must lie inside a surface; the plan must exist iff each pipe needs <= cap slots.
 *  (2) apply then restore = the 2L base registers exactly, the 4L enables/DATAPATH back to base; restore of 4L = pass-through.
 *  (3) base4 acceptance, rejections, old interface == cap 2, plane-alpha CON, test 7 (four layers) on the real pattern.
 *  (4) H6: a simulated GPU (jobs with sequence numbers, reservations, completions in random order): READY never while a job
 *      submitted by SwapEnd or the job of a reservation outstanding at SwapEnd is unfinished; FORCED only after the limit;
 *      a mutant that forgets the SwapEnd reservations is caught.
 * Build: cc -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I../b/iokit/Kernel/arm -I<stage>/iokit/Kernel/arm hwmulti_host_test.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sweet_fb_blend.h"
#include "sweet_fb_hw.h"

static int fails;
static unsigned long checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { if (fails++ < 40) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static uint64_t rng = 0x243f6a8885a308d3ull;
static uint32_t rnd(uint32_t n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return n ? (uint32_t)(rng % n) : 0; }
static int32_t rndr(int32_t lo, int32_t hi) { return lo + (int32_t)rnd((uint32_t)(hi - lo + 1)); }

/* ---- simulated physical memory ---- */
#define MAXS 8
struct surf { uint64_t pa, len; uint32_t w, h, pitch, offset; uint32_t *mem; };
static struct surf S[MAXS];
static unsigned NS;

static uint32_t
mem_read(uint64_t pa, int *bad)
{
	for (unsigned i = 0; i < NS; i++) {
		if (pa >= S[i].pa && pa + 4 <= S[i].pa + S[i].len) { return S[i].mem[(pa - S[i].pa) / 4]; }
	}
	(*bad)++;
	return 0xdeadbeefu;
}

static uint32_t
pm_px(void)
{
	uint32_t k = rnd(10), a = k < 3 ? 255u : (k < 5 ? 0u : rnd(256));
	if (a == 0) { return 0; }
	return (a << 24) | (rnd(a + 1) << 16) | (rnd(a + 1) << 8) | rnd(a + 1);
}

static void
surf_free_all(void)
{
	for (unsigned i = 0; i < NS; i++) { free(S[i].mem); S[i].mem = NULL; }
	NS = 0;
}

static struct surf *
surf_new(uint64_t pa, uint32_t w, uint32_t h, uint32_t pitch, uint32_t offset, int fill)
{
	struct surf *s = &S[NS++];
	s->pa = pa; s->w = w; s->h = h; s->pitch = pitch; s->offset = offset;
	s->len = ((uint64_t)offset + (uint64_t)pitch * h + 4095u) & ~4095ull;
	s->mem = calloc(s->len / 4, 4);
	if (fill) { for (uint64_t i = 0; i < s->len / 4; i++) { s->mem[i] = pm_px(); } }
	return s;
}

/* ---- simulated register file: [pipe][block 0 = 2L, 1 = 4L] ---- */
static uint32_t R[2][2][0x400], R0[2][2][0x400];
static struct sweet_fb_hw_base B2[2], B4[2];

static void
read_base(const uint32_t *o, struct sweet_fb_hw_base *b, unsigned nl)
{
	memset(b, 0, sizeof(*b));
	b->src_con = o[SWEET_FB_HW_R_SRC_CON / 4]; b->bgclr = o[SWEET_FB_HW_R_ROI_BGCLR / 4];
	b->datapath = o[SWEET_FB_HW_R_DATAPATH / 4]; b->roi = o[SWEET_FB_HW_R_ROI_SIZE / 4];
	for (unsigned i = 0; i < nl; i++) {
		b->con[i] = o[SWEET_FB_HW_R_CON(i) / 4]; b->size[i] = o[SWEET_FB_HW_R_SRC_SIZE(i) / 4]; b->offset[i] = o[SWEET_FB_HW_R_OFFSET(i) / 4];
		b->pitch[i] = o[SWEET_FB_HW_R_PITCH(i) / 4]; b->pitch_msb[i] = o[SWEET_FB_HW_R_PITCH_MSB(i) / 4]; b->clip[i] = o[SWEET_FB_HW_R_CLIP(i) / 4];
		b->rdma_ctrl[i] = o[SWEET_FB_HW_R_RDMA_CTRL(i) / 4]; b->gmc[i] = o[SWEET_FB_HW_R_RDMA_GMC(i) / 4]; b->fifo[i] = o[SWEET_FB_HW_R_RDMA_FIFO(i) / 4];
		b->addr[i] = o[SWEET_FB_HW_R_ADDR(i) / 4];
	}
}

/* the state the early code + 0156 leave: 2L layer 0 = panel buffer, 4L = pass-through cascade stage (LK's layer 1 removed) */
static void
regs_init(uint32_t fw, uint32_t fh, uint32_t panel_pa, uint32_t panel_pitch)
{
	memset(R, 0, sizeof(R));
	for (unsigned k = 0; k < 2; k++) {
		uint32_t *o = R[k][0], *q = R[k][1], roi = (fh << 16) | (fw / 2);
		o[SWEET_FB_HW_R_SRC_CON / 4] = 0x00000001u; o[SWEET_FB_HW_R_ROI_BGCLR / 4] = 0xff000000u; o[SWEET_FB_HW_R_ROI_SIZE / 4] = roi;
		o[SWEET_FB_HW_R_DATAPATH / 4] = 0x01000001u;
		o[SWEET_FB_HW_R_CON(0) / 4] = 0x21ffu; o[SWEET_FB_HW_R_ADDR(0) / 4] = panel_pa + k * (fw / 2) * 4;
		o[SWEET_FB_HW_R_PITCH(0) / 4] = 0x00c00000u | panel_pitch; o[SWEET_FB_HW_R_SRC_SIZE(0) / 4] = roi;
		o[SWEET_FB_HW_R_RDMA_CTRL(0) / 4] = 0x00200001u; o[SWEET_FB_HW_R_RDMA_GMC(0) / 4] = 0x40402020u; o[SWEET_FB_HW_R_RDMA_FIFO(0) / 4] = 0x80000140u;
		o[SWEET_FB_HW_R_PITCH_MSB(0) / 4] = 0x00010000u; o[SWEET_FB_HW_R_CLIP(1) / 4] = 0x01020304u; o[SWEET_FB_HW_R_CON(1) / 4] = 0x1234u;
		q[SWEET_FB_HW_R_SRC_CON / 4] = 0x00000000u; q[SWEET_FB_HW_R_ROI_SIZE / 4] = roi; q[SWEET_FB_HW_R_DATAPATH / 4] = 0x01000005u | (k ? 0x40u : 0u);
		q[SWEET_FB_HW_R_ROI_BGCLR / 4] = 0xff00ff00u;      /* green: must never show (the cascade input is used) */
		q[SWEET_FB_HW_R_RDMA_CTRL(1) / 4] = 0x00200000u; q[SWEET_FB_HW_R_RDMA_GMC(1) / 4] = 0x40402020u; q[SWEET_FB_HW_R_RDMA_FIFO(1) / 4] = 0x800000a0u;
		q[SWEET_FB_HW_R_CLIP(2) / 4] = 0x05050505u;
	}
	memcpy(R0, R, sizeof(R));
	for (unsigned k = 0; k < 2; k++) {
		read_base(R[k][0], &B2[k], SWEET_FB_HW_SLOTS);
		read_base(R[k][1], &B4[k], SWEET_FB_HW_L4);
		B4[k].gmc_fb = B4[k].gmc[1] ? B4[k].gmc[1] : B2[k].gmc[0];
		B4[k].fifo_fb = B4[k].fifo[1] ? B4[k].fifo[1] : B2[k].fifo[0];
	}
}

static int
regs_write(unsigned k, unsigned blk, const struct sweet_fb_hw_reg *r, unsigned n)
{
	if (n > SWEET_FB_HW_MAX_REGS) { return -1; }
	for (unsigned i = 0; i < n; i++) {
		if (r[i].off >= 0x1000u || (r[i].off & 3u)) { return -2; }
		R[k][blk][r[i].off / 4] = r[i].val;
	}
	for (unsigned i = 0; i < n; i++) { if ((R[k][blk][r[i].off / 4] ^ r[i].val) & r[i].mask) { return -3; } }
	return 0;
}

static int
apply_plan(const struct sweet_fb_hw_plan *p)
{
	struct sweet_fb_hw_reg r[SWEET_FB_HW_MAX_REGS + 8];
	for (unsigned k = 0; k < 2; k++) {
		unsigned n = sweet_fb_hw_regs(r, p, k, &B2[k]);
		if (regs_write(k, 0, r, n)) { return -1; }
		n = sweet_fb_hw_regs4(r, p, k, &B4[k]);
		if (regs_write(k, 1, r, n)) { return -2; }
	}
	return 0;
}

static uint32_t
scale_px(uint32_t s, uint32_t a)
{
	uint32_t o = 0;
	for (unsigned c = 0; c < 32; c += 8) { o |= ((((s >> c) & 0xffu) * a + 127u) / 255u) << c; }
	return o;
}

/* one OVL layer j of a block over `out`, from the registers */
static uint32_t
model_layer(const uint32_t *o, unsigned j, uint32_t x, uint32_t y, uint32_t out, int *bad)
{
	uint32_t off = o[SWEET_FB_HW_R_OFFSET(j) / 4], sz = o[SWEET_FB_HW_R_SRC_SIZE(j) / 4];
	uint32_t ox = off & 0xffffu, oy = off >> 16, w = sz & 0xffffu, h = sz >> 16;
	if (x < ox || x >= ox + w || y < oy || y >= oy + h) { return out; }
	if (o[SWEET_FB_HW_R_CLIP(j) / 4] != 0) { (*bad)++; }
	uint32_t pitch = (o[SWEET_FB_HW_R_PITCH(j) / 4] & 0xffffu) | ((o[SWEET_FB_HW_R_PITCH_MSB(j) / 4] & 0xfu) << 16);
	uint64_t pa = (uint64_t)o[SWEET_FB_HW_R_ADDR(j) / 4] + (uint64_t)(y - oy) * pitch + (uint64_t)(x - ox) * 4u;
	uint32_t con = o[SWEET_FB_HW_R_CON(j) / 4];
	uint32_t px = sweet_fb_hw_con_decode(con, mem_read(pa, bad));
	if (!(con & SWEET_FB_HW_CON_AEN)) { return px; }
	if (SWEET_FB_HW_CON_CFMT(con) == 3u && (con & SWEET_FB_HW_CON_MAN)) {
		uint32_t a = con & 0xffu;
		if (a < 255u) { px = scale_px(px, a); }
		return sweet_fb_hw_over_pm(px, out);
	}
	return sweet_fb_hw_over_straight(px, out);
}

/* pipe k's output at ROI (x, y): 2L, then the 4L with the 2L output as background */
static uint32_t
model_px(unsigned k, uint32_t x, uint32_t y, int *bad)
{
	const uint32_t *o = R[k][0], *q = R[k][1];
	uint32_t out = o[SWEET_FB_HW_R_ROI_BGCLR / 4];
	for (unsigned j = 0; j < 2; j++) {
		if (((o[SWEET_FB_HW_R_SRC_CON / 4] >> j) & 1u) && (o[SWEET_FB_HW_R_RDMA_CTRL(j) / 4] & 1u)) { out = model_layer(o, j, x, y, out, bad); }
	}
	if (!(q[SWEET_FB_HW_R_DATAPATH / 4] & 4u)) { out = q[SWEET_FB_HW_R_ROI_BGCLR / 4]; (*bad)++; }
	for (unsigned j = 0; j < 4; j++) {
		if (!((q[SWEET_FB_HW_R_SRC_CON / 4] >> j) & 1u)) { continue; }
		if (!(q[SWEET_FB_HW_R_RDMA_CTRL(j) / 4] & 1u) || (q[SWEET_FB_HW_R_DATAPATH / 4] & (0x10u << j))) { (*bad)++; }
		out = model_layer(q, j, x, y, out, bad);
	}
	return out;
}

/* independent reference: per pixel, lowest covering layer copied (plane alpha: over black), others premultiplied over it */
static uint32_t
ref_px(const struct sweet_fb_hw_in *in, const unsigned *si, unsigned n, uint32_t x, uint32_t y)
{
	uint32_t out = 0xff000000u;
	int first = 1;
	for (unsigned i = 0; i < n; i++) {
		const struct sweet_fb_hw_in *l = &in[i];
		if ((int32_t)x < l->dx || (int32_t)x >= l->dx + l->w || (int32_t)y < l->dy || (int32_t)y >= l->dy + l->h) { continue; }
		const struct surf *s = &S[si[i]];
		uint32_t px = s->mem[(s->offset + (uint64_t)(l->sy + ((int32_t)y - l->dy)) * s->pitch) / 4 + (uint32_t)(l->sx + ((int32_t)x - l->dx))];
		uint32_t a = (l->alpha == 0 || l->alpha >= 255u) ? 255u : l->alpha;
		if (a < 255u) { out = sweet_fb_hw_over_pm(scale_px(px, a), first ? 0xff000000u : out); }
		else { out = first ? px : sweet_fb_hw_over_pm(px, out); }
		first = 0;
	}
	return out;
}

/* compare the model with the reference (and the CPU path when cpu != 0) on rows y % stride == 0; returns differing pixels */
static long
compare(const struct sweet_fb_hw_in *in, const unsigned *si, const struct sweet_fb_layer_geom *g, unsigned n, uint32_t fw, uint32_t fh,
    uint32_t stride, int cpu, int *bad)
{
	long diff = 0;
	uint32_t *row = malloc(fw * 4u);
	for (uint32_t y = 0; y < fh; y += stride) {
		const uint32_t *rows[SWEET_FB_HW_MAX_IN];
		uint32_t x0 = 0, x1 = 0;
		bool any = false;
		if (cpu) {
			for (unsigned i = 0; i < n; i++) {
				const struct surf *s = &S[si[i]];
				rows[i] = (y >= (uint32_t)g[i].dy && y < (uint32_t)(g[i].dy + g[i].h)) ?
				    s->mem + (s->offset + (uint64_t)(g[i].sy + (int32_t)(y - g[i].dy)) * s->pitch) / 4 + g[i].sx : NULL;
			}
			memset(row, 0, fw * 4u);
			any = sweet_fb_compose_row(row, rows, g, n, &x0, &x1, NULL);
		}
		for (uint32_t x = 0; x < fw; x++) {
			unsigned k = x / (fw / 2);
			uint32_t hw = model_px(k, x - k * (fw / 2), y, bad) & 0xffffffu;
			uint32_t rf = ref_px(in, si, n, x, y) & 0xffffffu;
			if (hw != rf) { if (diff++ < 2) { printf("  ref diff at %u,%u model %06x ref %06x\n", x, y, hw, rf); } }
			if (cpu) {
				uint32_t c = ((any && x >= x0 && x < x1) ? row[x] : 0u) & 0xffffffu;
				if (hw != c) { if (diff++ < 2) { printf("  cpu diff at %u,%u model %06x cpu %06x\n", x, y, hw, c); } }
			}
		}
	}
	free(row);
	return diff;
}

/* slots pipe k needs for the layers: highest index of a layer reaching it + 1 (slot = layer index) */
static unsigned
need(const struct sweet_fb_hw_in *in, unsigned n, unsigned k, uint32_t fw)
{
	int32_t c0 = (int32_t)(k * fw / 2), c1 = c0 + (int32_t)(fw / 2);
	unsigned c = 0;
	for (unsigned i = 0; i < n; i++) { if (in[i].dx < c1 && in[i].dx + in[i].w > c0) { c = i + 1; } }
	return c;
}

static long scenes, planned, rejected_count, l4_used, alpha_scenes, cpu_compared;

static void
scene(uint32_t fw, uint32_t fh, uint32_t stride, int full)
{
	unsigned n = 1 + rnd(6);
	struct sweet_fb_hw_in in[SWEET_FB_HW_MAX_IN];
	struct sweet_fb_layer_geom g[SWEET_FB_HW_MAX_IN];
	unsigned si[SWEET_FB_HW_MAX_IN], m = 0;
	int32_t offx = full ? 22 : rndr(0, 12), offy = full ? 18 : rndr(0, 12);
	int use_alpha = rnd(5) == 0;
	uint64_t pa = SWEET_FB_HW_POOL_BASE + (uint64_t)rnd(64) * 0x10000u;
	surf_free_all();
	for (unsigned i = 0; i < n; i++) {
		uint32_t w = 1 + rnd(full ? 1200 : fw + 40), h = 1 + rnd(full ? 2500 : fh + 40);
		if (rnd(4) == 0) { w = fw + (uint32_t)offx * 2 + rnd(3); h = fh + (uint32_t)offy * 2 + rnd(3); }      /* full-screen drawable */
		uint32_t pitch = ((w * 4u + 63u) & ~63u) + 64u * rnd(3), offset = rnd(4) == 0 ? 4u * rnd(64) : 0u;
		struct surf *s = surf_new(pa, w, h, pitch, offset, 1);
		pa = (s->pa + s->len + 0xffffu) & ~0xffffull;
		struct sweet_fb_layer_geom gg;
		int32_t ax = rnd(3) ? 0 : rndr(0, (int32_t)w / 2), ay = rnd(3) ? 0 : rndr(0, (int32_t)h / 2);
		int32_t bx = rndr(-10, (int32_t)fw + offx), by = rndr(-10, (int32_t)fh + offy), bw = rnd(4) ? rndr(0, (int32_t)w) : 0, bh = rnd(4) ? rndr(0, (int32_t)h) : 0;
		if (rnd(3) == 0) { bx = offx + rndr(-30, 30); by = offy + rndr(-30, 30); }
		if (!sweet_fb_layer_clip(&gg, ax, ay, bx, by, bw, bh, w, h, offx, offy, fw, fh)) { continue; }
		struct sweet_fb_hw_in l = { s->pa, s->len, s->offset, s->pitch, 1, gg.sx, gg.sy, gg.dx, gg.dy, gg.w, gg.h, 0 };
		if (use_alpha && rnd(2)) { l.alpha = 1 + rnd(254); }
		in[m] = l; g[m] = gg; si[m] = NS - 1; m++;
	}
	if (m == 0) { return; }
	scenes++;
	uint32_t cap = 2 + rnd(5), mx = 0;
	for (unsigned k = 0; k < 2; k++) { unsigned c = need(in, m, k, fw); if (c > mx) { mx = c; } }
	struct sweet_fb_hw_plan p;
	int r = sweet_fb_hw_plan_makec(&p, in, m, SWEET_FB_HW_DUAL, 2, fw, fh, cap);
	CHECK((r == SWEET_FB_HW_OK) == (mx <= cap), "plan %d with %u layers, max per pipe %u, cap %u", r, m, mx, cap);
	CHECK(r == SWEET_FB_HW_OK || r == SWEET_FB_HW_W_COUNT, "unexpected reason %s", sweet_fb_hw_why_name(r));
	if (r != SWEET_FB_HW_OK) { rejected_count++; return; }
	planned++;
	for (unsigned k = 0; k < 2; k++) { CHECK(p.nslot[k] == need(in, m, k, fw), "nslot pipe %u", k); }
	if (sweet_fb_hw_plan_uses4(&p)) { l4_used++; }
	if (use_alpha) { alpha_scenes++; }
	regs_init(fw, fh, 0x76e10000u, fw * 4u);
	CHECK(apply_plan(&p) == 0, "register lists");
	int bad = 0;
	int cpu = 1;
	for (unsigned i = 0; i < m; i++) { if (in[i].alpha) { cpu = 0; } }
	cpu_compared += cpu;
	long d = compare(in, si, g, m, fw, fh, stride, cpu, &bad);
	CHECK(d == 0, "scene %ld: %ld pixels differ (%u layers, cap %u, alpha %d)", scenes, d, m, cap, use_alpha);
	CHECK(bad == 0, "scene %ld: %d reads outside every surface / bad register state", scenes, bad);
	/* restore: 2L exactly the base, 4L enables + DATAPATH back; the cascade passes the CPU path's picture again */
	struct sweet_fb_hw_reg rr[SWEET_FB_HW_MAX_REGS];
	for (unsigned k = 0; k < 2; k++) {
		unsigned nn = sweet_fb_hw_regs_base(rr, k, &B2[k], B2[k].addr[0]);
		CHECK(regs_write(k, 0, rr, nn) == 0, "restore 2L");
		nn = sweet_fb_hw_regs4_base(rr, &B4[k]);
		CHECK(regs_write(k, 1, rr, nn) == 0, "restore 4L");
		CHECK(memcmp(R[k][0], R0[k][0], sizeof(R[k][0])) == 0, "2L registers not back to base (pipe %u)", k);
		CHECK(R[k][1][SWEET_FB_HW_R_SRC_CON / 4] == R0[k][1][SWEET_FB_HW_R_SRC_CON / 4] && R[k][1][SWEET_FB_HW_R_DATAPATH / 4] == R0[k][1][SWEET_FB_HW_R_DATAPATH / 4],
		    "4L enables / DATAPATH not back to base");
		for (unsigned j = 0; j < 4; j++) { CHECK((R[k][1][SWEET_FB_HW_R_RDMA_CTRL(j) / 4] & 1u) == (R0[k][1][SWEET_FB_HW_R_RDMA_CTRL(j) / 4] & 1u), "4L RDMA %u", j); }
	}
}

/* ---- H6 simulation ---- */
#define HJ 96
struct hlayer { uint64_t seq[HJ]; int done[HJ], must[HJ]; int nj, resv, resv_at_swap; };
struct hsim { struct hlayer L[4]; uint64_t next; int mutant; };
static bool h_busy(void *c, unsigned l) { struct hlayer *x = &((struct hsim *)c)->L[l]; if (x->resv) { return true; } for (int i = 0; i < x->nj; i++) { if (!x->done[i]) { return true; } } return false; }
static bool h_resv(void *c, unsigned l) { return ((struct hsim *)c)->L[l].resv != 0; }
static uint64_t h_seq(void *c, unsigned l) { struct hlayer *x = &((struct hsim *)c)->L[l]; return x->nj ? x->seq[x->nj - 1] : 0; }
static bool h_done(void *c, unsigned l, uint64_t s) { struct hlayer *x = &((struct hsim *)c)->L[l]; for (int i = 0; i < x->nj; i++) { if (!x->done[i] && x->seq[i] <= s) { return false; } } return true; }

static long h_ready, h_forced;

static void
h6_run(int mutant, long *viol_out)
{
	struct hsim g;
	memset(&g, 0, sizeof(g));
	g.next = 1; g.mutant = mutant;
	uint32_t layers = 1u + rnd(15);
	/* history before SwapEnd: some jobs, some still running, some reservations outstanding */
	for (unsigned l = 0; l < 4; l++) {
		int nj = (int)rnd(4);
		for (int i = 0; i < nj; i++) { struct hlayer *x = &g.L[l]; x->seq[x->nj] = g.next++; x->done[x->nj] = rnd(2); x->nj++; }
		g.L[l].resv = rnd(3) == 0;
	}
	uint64_t snap[4] = { 0 }; uint32_t seen = 0; uint64_t resnaps = 0;
	for (unsigned l = 0; l < 4; l++) {
		struct hlayer *x = &g.L[l];
		snap[l] = h_seq(&g, l);
		for (int i = 0; i < x->nj; i++) { x->must[i] = !x->done[i]; }
		x->resv_at_swap = x->resv;
		if (x->resv && !mutant) { seen |= 1u << l; }
	}
	struct sweet_fb_hw_h6_gpu gp = { &g, h_busy, h_resv, h_seq, h_done };
	uint64_t age = rnd(3000), limit = 22000;
	long viol = 0;
	for (int step = 0; step < 400; step++) {
		/* events between two polls */
		unsigned ne = rnd(3);
		for (unsigned e = 0; e < ne; e++) {
			unsigned l = rnd(4), kind = rnd(10);
			struct hlayer *x = &g.L[l];
			if (kind < 4) {      /* a job completes */
				int pend[HJ], np = 0;
				for (int i = 0; i < x->nj; i++) { if (!x->done[i]) { pend[np++] = i; } }
				if (np) { x->done[pend[rnd((uint32_t)np)]] = 1; }
			} else if (kind < 7 && x->nj < HJ) {      /* a job is submitted (it retires the reservation, if any) */
				x->seq[x->nj] = g.next++; x->done[x->nj] = 0; x->must[x->nj] = 0;
				if (x->resv && x->resv_at_swap) { x->must[x->nj] = 1; x->resv_at_swap = 0; }
				x->resv = 0; x->nj++;
			} else if (kind < 8) {      /* a reservation for a later frame */
				x->resv = 1;
			} else if (kind < 9) {      /* a reservation cancelled */
				if (x->resv) { x->resv = 0; x->resv_at_swap = 0; }
			}
		}
		int r = sweet_fb_hw_h6(&gp, layers, snap, &seen, &resnaps, age, limit);
		if (r == SWEET_FB_HW_H6_READY) {
			for (unsigned l = 0; l < 4; l++) {
				if (!(layers & (1u << l))) { continue; }
				struct hlayer *x = &g.L[l];
				for (int i = 0; i < x->nj; i++) { if (x->must[i] && !x->done[i]) { viol++; } }
				if (x->resv_at_swap && x->resv) { viol++; }      /* the promised frame is not even submitted */
			}
			h_ready++;
			break;
		}
		if (r == SWEET_FB_HW_H6_FORCED) { CHECK(age >= limit, "forced before the limit"); h_forced++; break; }
		age += 250;
	}
	*viol_out += viol;
}

int
main(void)
{
	/* (3) acceptance / rejection / interface */
	{
		regs_init(1080, 2400, 0x76e10000u, 4352);
		CHECK(sweet_fb_hw_base4_ok(&B4[0], B2[0].roi) && sweet_fb_hw_base4_ok(&B4[1], B2[1].roi), "base4 ok");
		struct sweet_fb_hw_base b = B4[0];
		b.src_con |= 2u; CHECK(!sweet_fb_hw_base4_ok(&b, B2[0].roi), "4L layer on accepted");
		b = B4[0]; b.src_con |= 0x100u; CHECK(!sweet_fb_hw_base4_ok(&b, B2[0].roi), "4L relay accepted");
		b = B4[0]; b.datapath &= ~4u; CHECK(!sweet_fb_hw_base4_ok(&b, B2[0].roi), "4L without BGCLR_IN_SEL accepted");
		b = B4[0]; b.roi ^= 1u; CHECK(!sweet_fb_hw_base4_ok(&b, B2[0].roi), "4L other ROI accepted");
		CHECK(!sweet_fb_hw_base4_ok(&B4[0], 0), "zero ROI accepted");

		surf_free_all();
		struct surf *s = surf_new(SWEET_FB_HW_POOL_BASE, 1125, 2436, 4544, 0, 0);
		struct sweet_fb_hw_in l = { s->pa, s->len, 0, 4544, 1, 22, 18, 0, 0, 1080, 2400, 0 }, in[8];
		struct sweet_fb_hw_plan p, q;
		for (unsigned i = 0; i < 8; i++) { in[i] = l; }
		CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_SINGLE, 2, 1080, 2400, 6) == SWEET_FB_HW_OK, "single");
		CHECK(p.slot[0][0].addr == 0xC0000000u + 18 * 4544 + 22 * 4 && p.slot[1][0].addr == 0xC0000000u + 18 * 4544 + (22 + 540) * 4, "single addr");
		CHECK(sweet_fb_hw_plan_makec(&p, in, 2, SWEET_FB_HW_SINGLE, 2, 1080, 2400, 6) == SWEET_FB_HW_W_COUNT, "single 2");
		CHECK(sweet_fb_hw_plan_makec(&p, in, 7, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_COUNT, "7 inputs");
		CHECK(sweet_fb_hw_plan_makec(&p, in, 6, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_OK, "6 full layers");
		CHECK(p.nslot[0] == 6 && p.nslot[1] == 6 && p.src_con[0] == 0x3fu, "6 slots");
		CHECK(p.slot[0][0].con == SWEET_FB_HW_CON_OPAQUE && p.slot[0][5].con == 0x028031ffu, "cons %08x %08x", p.slot[0][0].con, p.slot[0][5].con);
		CHECK(sweet_fb_hw_plan_makec(&p, in, 5, SWEET_FB_HW_DUAL, 2, 1080, 2400, 4) == SWEET_FB_HW_W_COUNT, "5 > cap 4");
		CHECK(sweet_fb_hw_plan_makec(&p, in, 3, SWEET_FB_HW_DUAL, 2, 1080, 2400, 2) == SWEET_FB_HW_W_COUNT, "3 > cap 2");
		CHECK(sweet_fb_hw_plan_make(&p, in, 2, SWEET_FB_HW_DUAL, 2, 1080, 2400) == SWEET_FB_HW_OK &&
		      sweet_fb_hw_plan_makec(&q, in, 2, SWEET_FB_HW_DUAL, 2, 1080, 2400, 2) == SWEET_FB_HW_OK && memcmp(&p, &q, sizeof(p)) == 0, "old interface = cap 2");
		CHECK(sweet_fb_hw_plan_make(&p, in, 3, SWEET_FB_HW_DUAL, 2, 1080, 2400) == SWEET_FB_HW_W_COUNT, "old interface 3 layers");
		/* two small layers in different halves + 4 full: per-pipe packing */
		struct sweet_fb_hw_in sm = l; sm.w = 100; sm.h = 100; sm.dx = 10; sm.dy = 10;
		struct sweet_fb_hw_in sm2 = sm; sm2.dx = 700;
		struct sweet_fb_hw_in v[6] = { l, l, sm, sm2, l, l };
		CHECK(sweet_fb_hw_plan_makec(&p, v, 6, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_OK && p.nslot[0] == 6 && p.nslot[1] == 6 &&
		      p.src_con[0] == 0x37u && p.src_con[1] == 0x3bu, "slot = layer index, per pipe enables %x %x", p.src_con[0], p.src_con[1]);
		CHECK(sweet_fb_hw_plan_makec(&p, v, 6, SWEET_FB_HW_DUAL, 2, 1080, 2400, 5) == SWEET_FB_HW_W_COUNT, "layer 5 with cap 5");
		CHECK(sweet_fb_hw_plan_makec(&p, v, 4, SWEET_FB_HW_DUAL, 2, 1080, 2400, 3) == SWEET_FB_HW_W_COUNT &&
		      sweet_fb_hw_plan_makec(&p, v, 3, SWEET_FB_HW_DUAL, 2, 1080, 2400, 3) == SWEET_FB_HW_OK && p.src_con[1] == 3u, "cap 3");
		/* 0401 window (when this header knows it): a translated layer may use the 2L slots only (the 4L ports read physically) */
		if (sweet_fb_hw_reachable(0x90000000ull, 0x90001000ull)) {
			struct sweet_fb_hw_in iv = l; iv.pa = 0x90000000ull;
			struct sweet_fb_hw_in w3[3] = { iv, l, l }, w4[3] = { l, l, iv };
			CHECK(sweet_fb_hw_plan_makec(&p, w3, 3, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_OK, "IOVA in slot 0");
			CHECK(sweet_fb_hw_plan_makec(&p, w4, 3, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_MEMORY, "IOVA in a 4L slot");
			CHECK(sweet_fb_hw_plan_makec(&p, w4, 3, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6 | SWEET_FB_HW_CAP_L4_IOVA) == SWEET_FB_HW_OK, "IOVA in a 4L slot, 4L translated");
			printf("0401 window known: IOVA layers kept out of the 4L slots\n");
		}
		/* plane alpha */
		struct sweet_fb_hw_in a0 = l; a0.alpha = 128;
		CHECK(sweet_fb_hw_plan_makec(&p, &a0, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_OK && p.slot[0][0].con == 0x02803180u, "plane alpha slot 0 %08x", p.slot[0][0].con);
		a0.alpha = 255; CHECK(sweet_fb_hw_plan_makec(&p, &a0, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_OK && p.slot[0][0].con == SWEET_FB_HW_CON_OPAQUE, "alpha 255 opaque");
		/* rejections */
		struct sweet_fb_hw_in x = l; x.format_ok = 0; CHECK(sweet_fb_hw_plan_makec(&p, &x, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_FORMAT, "format");
		x = l; x.pa = 0; CHECK(sweet_fb_hw_plan_makec(&p, &x, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_MEMORY, "memory");
		x = l; x.pitch = 4000; CHECK(sweet_fb_hw_plan_makec(&p, &x, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_PITCH, "pitch");
		x = l; x.pa = SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE - 0x100000u; CHECK(sweet_fb_hw_plan_makec(&p, &x, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_RANGE, "pool end");
		x = l; x.len = 4544u * 100u; CHECK(sweet_fb_hw_plan_makec(&p, &x, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_RANGE, "len");
		x = l; x.pa = 0x1C0000000ull; CHECK(sweet_fb_hw_plan_makec(&p, &x, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_RANGE, "above 4G");
		x = l; x.dx = 10; CHECK(sweet_fb_hw_plan_makec(&p, &x, 1, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_RANGE, "beyond panel");
		CHECK(sweet_fb_hw_plan_makec(&p, in, 0, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_W_NONE, "none");
		CHECK(sweet_fb_hw_plan_makec(&p, in, 1, SWEET_FB_HW_DRY, 2, 1080, 2400, 6) == SWEET_FB_HW_W_MODE, "mode");
		/* register count bound: 6 full layers */
		CHECK(sweet_fb_hw_plan_makec(&p, in, 6, SWEET_FB_HW_DUAL, 2, 1080, 2400, 6) == SWEET_FB_HW_OK, "6");
		struct sweet_fb_hw_reg rr[64];
		CHECK(sweet_fb_hw_regs(rr, &p, 0, &B2[0]) <= SWEET_FB_HW_MAX_REGS && sweet_fb_hw_regs4(rr, &p, 0, &B4[0]) <= SWEET_FB_HW_MAX_REGS, "register count");
		/* the 4L lists: FBDC cleared, BGCLR_IN_SEL kept, csw flips only upper slots */
		sweet_fb_hw_plan_csw(&p, 1u);
		CHECK(sweet_fb_hw_con_final(&p, 0, 0) == SWEET_FB_HW_CON_OPAQUE && sweet_fb_hw_con_final(&p, 0, 4) == (0x028031ffu ^ SWEET_FB_HW_CON_BYTE_SWAP), "csw");
	}
	/* (1)+(2) random scenes: small panel, every row; full panel, sampled rows */
	for (int i = 0; i < 6000; i++) { scene(216, 480, 1, 0); }
	for (int i = 0; i < 160; i++) { scene(1080, 2400, 23, 1); }
	printf("scenes %ld: planned %ld (4L used %ld, plane alpha %ld, also vs CPU path %ld), rejected for count %ld\n", scenes, planned, l4_used,
	    alpha_scenes, cpu_compared, rejected_count);
	CHECK(l4_used > 1000 && planned > 3000 && alpha_scenes > 300 && rejected_count > 100, "coverage");
	/* the model catches a wrong plan (sanity): drop the top slot of a 3-layer full-screen scene */
	{
		surf_free_all();
		struct surf *s0 = surf_new(SWEET_FB_HW_POOL_BASE, 216, 480, 896, 0, 1), *s1 = surf_new(SWEET_FB_HW_POOL_BASE + 0x100000u, 216, 480, 896, 0, 1);
		struct surf *s2 = surf_new(SWEET_FB_HW_POOL_BASE + 0x200000u, 216, 480, 896, 0, 1);
		struct sweet_fb_hw_in in[3] = { { s0->pa, s0->len, 0, 896, 1, 0, 0, 0, 0, 216, 480, 0 }, { s1->pa, s1->len, 0, 896, 1, 0, 0, 0, 0, 216, 480, 0 },
		                               { s2->pa, s2->len, 0, 896, 1, 0, 0, 0, 0, 216, 480, 0 } };
		struct sweet_fb_layer_geom g[3] = { { 0, 0, 0, 0, 216, 480 }, { 0, 0, 0, 0, 216, 480 }, { 0, 0, 0, 0, 216, 480 } };
		unsigned si[3] = { 0, 1, 2 };
		struct sweet_fb_hw_plan p;
		CHECK(sweet_fb_hw_plan_makec(&p, in, 3, SWEET_FB_HW_DUAL, 2, 216, 480, 6) == SWEET_FB_HW_OK, "3");
		p.src_con[1] &= ~4u;
		regs_init(216, 480, 0x76e10000u, 864); (void)apply_plan(&p);
		int bad = 0;
		CHECK(compare(in, si, g, 3, 216, 480, 1, 1, &bad) > 0, "model did not catch a missing 4L layer");
		p.src_con[1] |= 4u; p.slot[0][2].con = SWEET_FB_HW_CON_OPAQUE;
		regs_init(216, 480, 0x76e10000u, 864); (void)apply_plan(&p); bad = 0;
		CHECK(compare(in, si, g, 3, 216, 480, 1, 1, &bad) > 0, "model did not catch an opaque upper slot");
	}
	/* test 7 on the real pattern (carve-out slot 3 at 0x781FC000, pitch 4352) */
	{
		surf_free_all();
		uint32_t fw = 1080, fh = 2400, pitch = 4352;
		uint64_t slot = 0x76e10000ull + 2ull * pitch * fh;
		struct surf *s = surf_new(slot, fw, fh, pitch, 0, 0);
		for (uint32_t y = 0; y < fh; y++) { for (uint32_t x = 0; x < fw; x++) { s->mem[(uint64_t)y * pitch / 4 + x] = sweet_fb_hw_pattern_px(x, y, fw, fh); } }
		struct sweet_fb_hw_in in[4];
		uint32_t pm = 9;
		unsigned n = sweet_fb_hw_test_layers(in, 7, slot, pitch, fw, fh, 2, &pm);
		CHECK(n == 4 && pm == 2, "test 7 layers %u pm %u", n, pm);
		struct sweet_fb_hw_plan p;
		CHECK(sweet_fb_hw_plan_makec(&p, in, n, SWEET_FB_HW_DUAL, pm, fw, fh, 2) == SWEET_FB_HW_W_COUNT, "test 7 needs the 4L");
		CHECK(sweet_fb_hw_plan_makec(&p, in, n, SWEET_FB_HW_DUAL, pm, fw, fh, 6) == SWEET_FB_HW_OK && p.nslot[0] == 4 && p.nslot[1] == 4, "test 7 plan");
		regs_init(fw, fh, 0x76e10000u, pitch);
		CHECK(apply_plan(&p) == 0, "test 7 regs");
		unsigned si[4] = { 0, 0, 0, 0 };
		struct sweet_fb_layer_geom g[4];
		for (unsigned i = 0; i < 4; i++) { struct sweet_fb_layer_geom t = { in[i].sx, in[i].sy, in[i].dx, in[i].dy, in[i].w, in[i].h }; g[i] = t; }
		int bad = 0;
		CHECK(compare(in, si, g, 4, fw, fh, 3, 1, &bad) == 0 && bad == 0, "test 7 picture");
		/* the layer-1 strip must read R, G, B left to right (opaque pixels decode to themselves under pm 2) */
		bad = 0;
		CHECK((model_px(0, 400, 750, &bad) & 0xffffffu) == 0xff0000u && (model_px(0, 530, 750, &bad) & 0xffffffu) == 0x00ff00u &&
		      (model_px(1, 600 - 540, 750, &bad) & 0xffffffu) == 0x0000ffu && bad == 0, "test 7 colour strip");
	}
	/* (4) H6 */
	{
		long viol = 0, mviol = 0;
		for (int i = 0; i < 40000; i++) { h6_run(0, &viol); }
		long r0 = h_ready, f0 = h_forced;
		for (int i = 0; i < 40000; i++) { h6_run(1, &mviol); }
		printf("H6: %ld ready, %ld forced, %ld violations; mutant (SwapEnd reservations forgotten): %ld violations\n", r0, f0, viol, mviol);
		CHECK(viol == 0, "H6 showed a frame with its writes unfinished");
		CHECK(mviol > 0, "H6 mutant not caught");
		CHECK(r0 > 20000 && f0 > 100, "H6 coverage");
	}
	/* (4c) 0402c lock-free H6: snapshot = newest job id recorded for the layer at SwapEnd, low water = every id <= it completed.
	 * Random job streams, out-of-order completion across 2 job slots, jobs of other clients: READY only when every job of the
	 * frame (id <= snapshot on that layer) is complete; FORCED only at the limit; a mutant using the NEWEST seq (not the low water)
	 * as "completed" is caught. */
	{
		long viol = 0, mviol = 0, ready = 0, forced = 0;
		for (int run = 0; run < 40000; run++) {
			uint64_t id[64]; int lay[64], done[64], nj = 0; uint64_t next = 1;
			int pre = (int)rnd(8);
			for (int k = 0; k < pre; k++) { id[nj] = next++; lay[nj] = (int)rnd(6) - 1; done[nj] = (int)rnd(2); nj++; }      /* -1 = other client */
			uint32_t layers = 1u + rnd(15);
			uint64_t snap[4] = { 0, 0, 0, 0 };
			for (int k = 0; k < nj; k++) { if (lay[k] >= 0 && lay[k] < 4 && id[k] > snap[lay[k]]) { snap[lay[k]] = id[k]; } }
			uint64_t age = rnd(3000);
			for (int mut = 0; mut < 2; mut++) {
				int dn[64]; memcpy(dn, done, sizeof(dn)); int n2 = nj; uint64_t nx = next, ag = age; uint64_t id2[64]; int lay2[64];
				memcpy(id2, id, sizeof(id2)); memcpy(lay2, lay, sizeof(lay2));
				for (int step = 0; step < 200; step++) {
					unsigned e = rnd(4);
					if (e < 2) { int pend[64], np = 0; for (int k = 0; k < n2; k++) { if (!dn[k]) { pend[np++] = k; } } if (np) { dn[pend[rnd((uint32_t)np)]] = 1; } }
					else if (e == 2 && n2 < 64) { id2[n2] = nx++; lay2[n2] = (int)rnd(6) - 1; dn[n2] = 0; n2++; }      /* later frames / other clients */
					uint64_t lw = nx - 1, newest = 0;
					for (int k = 0; k < n2; k++) { if (!dn[k] && id2[k] - 1 < lw) { lw = id2[k] - 1; } if (id2[k] > newest) { newest = id2[k]; } }
					int r = sweet_fb_hw_h6c(layers, snap, mut ? newest : lw, ag, 22000);
					if (r == SWEET_FB_HW_H6_READY) {
						for (int k = 0; k < n2; k++) {
							if (lay2[k] >= 0 && lay2[k] < 4 && ((layers >> lay2[k]) & 1u) && id2[k] <= snap[lay2[k]] && !dn[k]) { if (mut) { mviol++; } else { viol++; } }
						}
						if (!mut) { ready++; }
						break;
					}
					if (r == SWEET_FB_HW_H6_FORCED) { CHECK(ag >= 22000, "h6c forced early"); if (!mut) { forced++; } break; }
					ag += 250;
				}
			}
		}
		printf("H6c (lock-free): %ld ready, %ld forced, %ld violations; mutant (newest seq as completed): %ld violations\n", ready, forced, viol, mviol);
		CHECK(viol == 0 && mviol > 0 && ready > 20000, "H6c");
		uint64_t sn[4] = { 5, 0, 9, 0 };
		CHECK(sweet_fb_hw_h6c(5u, sn, 9, 0, 10) == 0 && sweet_fb_hw_h6c(5u, sn, 8, 0, 10) == 1 && sweet_fb_hw_h6c(5u, sn, 8, 10, 10) == 2 &&
		      sweet_fb_hw_h6c(2u, sn, 0, 0, 10) == 0 && sweet_fb_hw_h6c(5u, sn, ~0ull, 0, 10) == 0, "h6c truth table (no GPU = ~0 low water = ready)");
	}
	/* fault policy unchanged */
	{
		uint32_t run = 0;
		CHECK(sweet_fb_hw_frame_fault(1u << 2, 0, false, false, &run) == SWEET_FB_HW_F_UNDERFLOW, "underflow");
		CHECK(sweet_fb_hw_frame_fault(0x3u | (1u << 14), 0, false, false, &run) == SWEET_FB_HW_F_NONE, "normal bits");
		CHECK(sweet_fb_hw_frame_fault(0, SWEET_FB_HW_DSI_UNDERRUN, false, false, &run) == SWEET_FB_HW_F_DSI, "dsi");
	}
	surf_free_all();
	printf("%s (%lu checks, %d failures)\n", fails ? "FAILED" : "ALL OK", checks, fails);
	return fails ? 1 : 0;
}
