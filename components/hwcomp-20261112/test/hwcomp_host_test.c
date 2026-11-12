/*
 * 0394 host test: sweet_fb_hw.h plans against the CPU composition of sweet_fb_blend.h.
 *  - An OVL model reads simulated physical memory exactly as a plan programs it (per pipe: ROI = fw/2 x fh, black background,
 *    slots bottom first, ADDR/PITCH/SRC_SIZE/OFFSET, CON opaque or premultiplied/straight source-over) and builds the panel.
 *  - The reference is what presentCopy does: sweet_fb_layer_clip per layer, sweet_fb_compose_row per row, black elsewhere.
 *  - RGB of every panel pixel must match (the panel ignores alpha); premultiplied variants exactly, straight alpha only where the
 *    upper layer is opaque or clear.
 *  - Fixed cases: the real 1125x2436 canvas on the 1080x2400 panel (crop 22,18), refusals (ordinary memory, pitch, ranges, counts).
 * Build: cc -O2 -Wall -Wextra -I../b/iokit/Kernel/arm -I<sweet_fb_blend.h dir> hwcomp_host_test.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "sweet_fb_blend.h"
#include "sweet_fb_hw.h"

#define MEM_BYTES (24u << 20)                  /* simulated pool prefix */
static uint8_t *mem;                           /* mem[0] = physical SWEET_FB_HW_POOL_BASE */
static uint32_t rd(uint64_t pa) {
	if (pa < SWEET_FB_HW_POOL_BASE || pa + 4 > SWEET_FB_HW_POOL_BASE + MEM_BYTES) { fprintf(stderr, "OVL read outside memory %#llx\n", (unsigned long long)pa); exit(3); }
	uint32_t v; memcpy(&v, mem + (pa - SWEET_FB_HW_POOL_BASE), 4); return v;
}
static uint32_t rnd(void) { return (uint32_t)rand() ^ ((uint32_t)rand() << 15) ^ ((uint32_t)rand() << 30); }

/* one OVL pipe: out[x + y*pw] = BGRA, black background */
static void ovl_pipe(const struct sweet_fb_hw_plan *p, unsigned k, uint32_t pw, uint32_t fh, uint32_t *out) {
	for (uint32_t i = 0; i < pw * fh; i++) out[i] = SWEET_FB_HW_BGCLR_BLACK;
	for (unsigned s = 0; s < SWEET_FB_HW_SLOTS; s++) {
		const struct sweet_fb_hw_slot *l = &p->slot[k][s];
		if (!l->en) continue;
		if (!(p->src_con[k] & (1u << s))) { fprintf(stderr, "slot enabled but SRC_CON bit clear\n"); exit(3); }
		uint32_t w = l->size & 0xffff, h = l->size >> 16, ox = l->offset & 0xffff, oy = l->offset >> 16;
		if (ox + w > pw || oy + h > fh) { fprintf(stderr, "layer outside the ROI\n"); exit(3); }
		uint32_t cfmt = (l->con >> 12) & 0xf, aen = (l->con >> 8) & 1, man = (l->con >> 23) & 1;
		for (uint32_t y = 0; y < h; y++) for (uint32_t x = 0; x < w; x++) {
			uint32_t sp = rd((uint64_t)l->addr + (uint64_t)y * l->pitch + (uint64_t)x * 4), *d = &out[(oy + y) * pw + ox + x];
			if (!aen) { *d = sp | 0xff000000u; continue; }
			uint32_t a = sp >> 24, r = 0;
			for (int c = 0; c < 3; c++) {
				uint32_t sc = (sp >> (8 * c)) & 0xff, dc = (*d >> (8 * c)) & 0xff, v;
				if (cfmt == 3 && man) v = sc + (dc * (255 - a) + 127) / 255;            /* premultiplied source-over */
				else v = (sc * a + dc * (255 - a) + 127) / 255;                           /* straight */
				if (v > 255) v = 255;
				r |= v << (8 * c);
			}
			*d = r | 0xff000000u;
		}
	}
}

struct surf { uint64_t pa; uint32_t w, h, pitch, offset; };

static int run_case(const struct surf *sf, unsigned n, const int32_t ra[][4], const int32_t rb[][4], uint32_t fw, uint32_t fh,
    int32_t offx, int32_t offy, uint32_t mode, uint32_t pm, bool *planned, unsigned *mism_px) {
	struct sweet_fb_layer_geom g[4]; struct sweet_fb_hw_in in[4]; unsigned nl = 0; unsigned idx[4];
	for (unsigned i = 0; i < n; i++) {
		if (!sweet_fb_layer_clip(&g[nl], ra[i][0], ra[i][1], rb[i][0], rb[i][1], rb[i][2], rb[i][3], sf[i].w, sf[i].h, offx, offy, fw, fh)) continue;
		in[nl].pa = sf[i].pa; in[nl].len = (uint64_t)sf[i].offset + (uint64_t)sf[i].pitch * sf[i].h; in[nl].offset = sf[i].offset;
		in[nl].pitch = sf[i].pitch; in[nl].format_ok = 1;
		in[nl].sx = g[nl].sx; in[nl].sy = g[nl].sy; in[nl].dx = g[nl].dx; in[nl].dy = g[nl].dy; in[nl].w = g[nl].w; in[nl].h = g[nl].h;
		idx[nl] = i; nl++;
	}
	struct sweet_fb_hw_plan p;
	int why = sweet_fb_hw_plan_make(&p, in, nl, mode, pm, fw, fh);
	*planned = why == SWEET_FB_HW_OK; *mism_px = 0;
	if (!*planned) return why;
	/* reference */
	uint32_t *ref = calloc((size_t)fw * fh, 4), *hw = calloc((size_t)fw * fh, 4), *pipe = calloc((size_t)(fw / 2) * fh, 4);
	const uint32_t *rows[4];
	for (uint32_t y = 0; y < fh; y++) {
		for (unsigned i = 0; i < nl; i++) {
			const struct surf *s = &sf[idx[i]];
			rows[i] = ((int32_t)y >= g[i].dy && (int32_t)y < g[i].dy + g[i].h) ?
			    (const uint32_t *)(mem + (s->pa - SWEET_FB_HW_POOL_BASE) + s->offset + (uint64_t)(g[i].sy + ((int32_t)y - g[i].dy)) * s->pitch + (uint64_t)g[i].sx * 4) : NULL;
		}
		uint32_t x0, x1;
		if (sweet_fb_compose_row(ref + (size_t)y * fw, rows, g, nl, &x0, &x1, NULL)) {
			for (uint32_t x = 0; x < x0; x++) ref[y * fw + x] = 0;
			for (uint32_t x = x1; x < fw; x++) ref[y * fw + x] = 0;
		}
	}
	for (unsigned k = 0; k < 2; k++) {
		ovl_pipe(&p, k, fw / 2, fh, pipe);
		for (uint32_t y = 0; y < fh; y++) memcpy(hw + (size_t)y * fw + k * (fw / 2), pipe + (size_t)y * (fw / 2), (fw / 2) * 4);
	}
	for (uint32_t y = 0; y < fh; y++) for (uint32_t x = 0; x < fw; x++) {
		uint32_t a = ref[y * fw + x] & 0xffffff, b = hw[y * fw + x] & 0xffffff;
		if (a == b) continue;
		if (pm == 0) {      /* straight alpha: only pixels where an upper layer is partly transparent may differ */
			bool partial = false;
			for (unsigned i = 1; i < nl; i++) {
				if ((int32_t)x < g[i].dx || (int32_t)x >= g[i].dx + g[i].w || (int32_t)y < g[i].dy || (int32_t)y >= g[i].dy + g[i].h) continue;
				const struct surf *s = &sf[idx[i]];
				uint32_t sp; memcpy(&sp, mem + (s->pa - SWEET_FB_HW_POOL_BASE) + s->offset + (uint64_t)(g[i].sy + ((int32_t)y - g[i].dy)) * s->pitch + (uint64_t)(g[i].sx + ((int32_t)x - g[i].dx)) * 4, 4);
				if ((sp >> 24) != 0 && (sp >> 24) != 255) partial = true;
				if (sp != 0 && (sp >> 24) == 0) partial = true;      /* a clear pixel with colour: premultiplied adds it, straight does not */
			}
			if (partial) continue;
		}
		/* rounding of x/255: the CPU path rounds exactly, the model with +127; allow 1 per channel */
		int ok = 1;
		for (int c = 0; c < 3; c++) { int da = (a >> (8 * c)) & 0xff, db = (b >> (8 * c)) & 0xff; if (da - db > 1 || db - da > 1) ok = 0; }
		if (!ok) (*mism_px)++;
	}
	free(ref); free(hw); free(pipe);
	return why;
}

/* fill a surface with premultiplied BGRA; kind 0 opaque, 1 mixed (opaque/clear/partial) */
static void fill(const struct surf *s, int kind) {
	for (uint32_t y = 0; y < s->h; y++) for (uint32_t x = 0; x < s->w; x++) {
		uint32_t v = rnd(), a;
		if (kind == 0) a = 255; else { uint32_t r = rnd() % 4; a = r == 0 ? 0 : r == 1 ? 255 : rnd() % 256; }
		uint32_t px = a << 24;
		for (int c = 0; c < 3; c++) { uint32_t ch = (v >> (8 * c)) & 0xff; ch = ch * a / 255; px |= ch << (8 * c); }
		memcpy(mem + (s->pa - SWEET_FB_HW_POOL_BASE) + s->offset + (uint64_t)y * s->pitch + (uint64_t)x * 4, &px, 4);
	}
}

int main(void) {
	int fails = 0;
	mem = calloc(MEM_BYTES, 1);
	srand(394);
	/* 1. the real geometry: 1125x2436 BGRA, pitch 4544 (64-aligned rows) at pool base; panel 1080x2400 at (22,18) */
	{
		struct sweet_fb_hw_in in = { SWEET_FB_HW_POOL_BASE + 0x100000, 4544ull * 2436, 0, 4544, 1, 22, 18, 0, 0, 1080, 2400 };
		struct sweet_fb_layer_geom g;
		if (!sweet_fb_layer_clip(&g, 0, 0, 0, 0, 1125, 2436, 1125, 2436, 22, 18, 1080, 2400) || g.sx != 22 || g.sy != 18 || g.w != 1080 || g.h != 2400) { printf("FAIL clip of the real canvas\n"); fails++; }
		struct sweet_fb_hw_plan p;
		int why = sweet_fb_hw_plan_make(&p, &in, 1, SWEET_FB_HW_SINGLE, 0, 1080, 2400);
		uint32_t a0 = (uint32_t)(in.pa + 18 * 4544 + 22 * 4), a1 = a0 + 540 * 4;
		bool ok = why == 0 && p.slot[0][0].addr == a0 && p.slot[1][0].addr == a1 && p.slot[0][0].size == 0x0960021cu && p.slot[1][0].size == 0x0960021cu &&
		    p.slot[0][0].offset == 0 && p.slot[1][0].offset == 0 && p.slot[0][0].pitch == 4544 && p.slot[0][0].con == 0x20ffu && p.src_con[0] == 1 && p.src_con[1] == 1 &&
		    !p.slot[0][1].en && !p.slot[1][1].en;
		printf("real canvas: why=%d pipe0 addr %#x (want %#x) pipe1 %#x (want %#x) size %#x offset %#x con %#x -> %s\n", why, p.slot[0][0].addr, a0,
		    p.slot[1][0].addr, a1, p.slot[0][0].size, p.slot[0][0].offset, p.slot[0][0].con, ok ? "ok" : "FAIL");
		if (!ok) fails++;
		/* the panel buffer itself as a layer (carve-out window) */
		struct sweet_fb_hw_in fb = { SWEET_FB_HW_FB_BASE, 4352ull * 2400, 0, 4352, 1, 0, 0, 0, 0, 1080, 2400 };
		why = sweet_fb_hw_plan_make(&p, &fb, 1, SWEET_FB_HW_SINGLE, 0, 1080, 2400);
		if (why != 0 || p.slot[0][0].addr != 0x76e10000u || p.slot[1][0].addr != 0x76e10870u) { printf("FAIL panel buffer plan (LK layout 0x76e10000/0x76e10870)\n"); fails++; }
		/* refusals */
		struct sweet_fb_hw_in bad = in; bad.pa = 0x142000000ull;                     /* ordinary XNU memory above 4 GiB */
		if (sweet_fb_hw_plan_make(&p, &bad, 1, 1, 0, 1080, 2400) != SWEET_FB_HW_W_RANGE) { printf("FAIL high memory accepted\n"); fails++; }
		bad = in; bad.pa = 0;
		if (sweet_fb_hw_plan_make(&p, &bad, 1, 1, 0, 1080, 2400) != SWEET_FB_HW_W_MEMORY) { printf("FAIL pa 0 accepted\n"); fails++; }
		bad = in; bad.pa = SWEET_FB_HW_POOL_BASE + SWEET_FB_HW_POOL_SIZE - 0x100000;    /* runs off the pool end */
		if (sweet_fb_hw_plan_make(&p, &bad, 1, 1, 0, 1080, 2400) != SWEET_FB_HW_W_RANGE) { printf("FAIL pool overrun accepted\n"); fails++; }
		bad = in; bad.len = 4544ull * 2000;
		if (sweet_fb_hw_plan_make(&p, &bad, 1, 1, 0, 1080, 2400) != SWEET_FB_HW_W_RANGE) { printf("FAIL surface overrun accepted\n"); fails++; }
		bad = in; bad.pitch = 4000;
		if (sweet_fb_hw_plan_make(&p, &bad, 1, 1, 0, 1080, 2400) != SWEET_FB_HW_W_PITCH) { printf("FAIL short pitch accepted\n"); fails++; }
		bad = in; bad.pitch = 4546;
		if (sweet_fb_hw_plan_make(&p, &bad, 1, 1, 0, 1080, 2400) != SWEET_FB_HW_W_PITCH) { printf("FAIL odd pitch accepted\n"); fails++; }
		bad = in; bad.format_ok = 0;
		if (sweet_fb_hw_plan_make(&p, &bad, 1, 1, 0, 1080, 2400) != SWEET_FB_HW_W_FORMAT) { printf("FAIL format accepted\n"); fails++; }
		bad = in; bad.w = 1000;
		if (sweet_fb_hw_plan_make(&p, &bad, 1, SWEET_FB_HW_SINGLE, 0, 1080, 2400) != SWEET_FB_HW_W_COVER) { printf("FAIL partial layer in SINGLE\n"); fails++; }
		struct sweet_fb_hw_in two[2] = { in, in };
		if (sweet_fb_hw_plan_make(&p, two, 2, SWEET_FB_HW_SINGLE, 0, 1080, 2400) != SWEET_FB_HW_W_COUNT) { printf("FAIL 2 layers in SINGLE\n"); fails++; }
		struct sweet_fb_hw_in three[3] = { in, in, in };
		if (sweet_fb_hw_plan_make(&p, three, 3, SWEET_FB_HW_DUAL, 0, 1080, 2400) != SWEET_FB_HW_W_COUNT) { printf("FAIL 3 layers in DUAL\n"); fails++; }
		if (sweet_fb_hw_plan_make(&p, &in, 1, SWEET_FB_HW_OFF, 0, 1080, 2400) != SWEET_FB_HW_W_MODE) { printf("FAIL mode off planned\n"); fails++; }
		if (sweet_fb_hw_con_upper(1) != 0x008031ffu || sweet_fb_hw_con_upper(2) != 0x038031ffu || sweet_fb_hw_con_upper(0) != 0x000021ffu) { printf("FAIL CON values\n"); fails++; }
		struct sweet_fb_hw_ctl c;
		if (!sweet_fb_hw_ctl_parse(0x112, &c) || c.mode != 2 || c.pm != 1 || !c.dry || sweet_fb_hw_ctl_parse(3, &c) || sweet_fb_hw_ctl_parse(0x30, &c) ||
		    !sweet_fb_hw_ctl_parse(0x8000, &c) || !c.clear_fault || sweet_fb_hw_ctl_parse(0x10000, &c)) { printf("FAIL control word\n"); fails++; }
		uint32_t run = 0; bool l1 = sweet_fb_hw_fault_step(&run, true), l2 = sweet_fb_hw_fault_step(&run, true), l0 = sweet_fb_hw_fault_step(&run, false);
		bool l3 = false; for (int i = 0; i < 3; i++) l3 = sweet_fb_hw_fault_step(&run, true);
		if (l1 || l2 || l0 || !l3) { printf("FAIL fault latch\n"); fails++; }
	}
	/* 2. random compositions on a small panel (fw 96, fh 48, logical offset like the real one), all modes and CON variants */
	unsigned cases = 0, planned_n = 0, mism_cases = 0, refused[SWEET_FB_HW_W_COUNT_ENUM] = { 0 };
	for (int it = 0; it < 6000; it++) {
		uint32_t fw = 96, fh = 48; int32_t offx = 3, offy = 2;
		unsigned n = 1 + rnd() % 2;
		uint32_t mode = 1 + rnd() % 2, pm = rnd() % 3;
		struct surf sf[2]; int32_t ra[2][4], rb[2][4];
		uint64_t at = SWEET_FB_HW_POOL_BASE;
		for (unsigned i = 0; i < n; i++) {
			bool full = (rnd() % 3) == 0;
			sf[i].w = full ? fw + 2 * offx : 8 + rnd() % 110; sf[i].h = full ? fh + 2 * offy : 4 + rnd() % 56;
			sf[i].pitch = ((sf[i].w * 4 + 63) & ~63u) + (rnd() % 2) * 64; sf[i].offset = (rnd() % 2) * 256;
			sf[i].pa = at; at += (sf[i].offset + (uint64_t)sf[i].pitch * sf[i].h + 0xffff) & ~0xffffull;
			fill(&sf[i], i == 0 ? (int)(rnd() % 2) : 1);
			ra[i][0] = full ? 0 : (int32_t)(rnd() % 6); ra[i][1] = full ? 0 : (int32_t)(rnd() % 6);
			rb[i][0] = full ? 0 : (int32_t)(rnd() % (fw + 10)); rb[i][1] = full ? 0 : (int32_t)(rnd() % (fh + 6));
			rb[i][2] = full ? 0 : (int32_t)(rnd() % 120); rb[i][3] = full ? 0 : (int32_t)(rnd() % 60);
		}
		bool planned; unsigned mism;
		int why = run_case(sf, n, (const int32_t (*)[4])ra, (const int32_t (*)[4])rb, fw, fh, offx, offy, mode, pm, &planned, &mism);
		cases++;
		if (planned) { planned_n++; if (mism) { mism_cases++; if (mism_cases <= 5) printf("MISMATCH case %d: n=%u mode=%u pm=%u px=%u\n", it, n, mode, pm, mism); } }
		else refused[why]++;
	}
	printf("random: cases=%u planned=%u mismatching=%u refused: none=%u count=%u cover=%u range=%u pitch=%u\n", cases, planned_n, mism_cases,
	    refused[SWEET_FB_HW_W_NONE], refused[SWEET_FB_HW_W_COUNT], refused[SWEET_FB_HW_W_COVER], refused[SWEET_FB_HW_W_RANGE], refused[SWEET_FB_HW_W_PITCH]);
	if (mism_cases || planned_n < cases / 4) fails++;
	printf("%s\n", fails ? "FAIL" : "ALL OK");
	return fails ? 1 : 0;
}
