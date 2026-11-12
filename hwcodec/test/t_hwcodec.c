/* Host test of bsd/dev/nord2_hwcodec.inc against a model of the MT6893 registers it touches (gpu/hwcodec M1).
 * The model fails the test on: any access to the VEN domain's registers while the domain is off, isolated, in reset or bus
 * protected; any access to the encoder with its clock gated; a start with a DMA address outside the claim or with a larb port
 * still translating; any access outside the mapped windows. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define N2J_HOST 1
typedef uintptr_t vm_offset_t;
static int fails, checks, violations, quiet;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static void viol(const char *m, uint32_t pa) { violations++; printf("VIOLATION %s at %#x\n", m, pa); }

/* ---- model ---- */
enum { W_SPM, W_INFRA, W_TOP, W_CG, W_LARB, W_ENC, W_SMI, W_N };
static const uint32_t wbase[W_N] = {0x10006000, 0x10001000, 0x10000000, 0x17000000, 0x17010000, 0x17030000, 0x1411f000};
static uint32_t reg[W_N][0x400];
static uint8_t *ram;                         /* the 16 MiB claim */
static struct { bool no_power_ack, sram_stuck, never_done, stall, dis_off, prot_stuck, mux_dead; unsigned done_after; } F;
static unsigned polls_left;
static uint64_t now_us;
static uint32_t writes_venc;
#define R(w, o) reg[w][(o) / 4]
static bool dom_on(void)
{
	uint32_t c = R(W_SPM, 0x344);
	return (R(W_SPM, 0x16c) & (1u << 17)) && (c & 0x1f) == 0x0d && !(c & 0x1100) && !(R(W_INFRA, 0x2d0) & (3u << 26)) && !(R(W_INFRA, 0xdc8) & 3);
}
static void model_reset(void)
{
	memset(reg, 0, sizeof(reg)); memset(&F, 0, sizeof(F)); now_us = 0; writes_venc = 0;
	R(W_SPM, 0x16c) = R(W_SPM, 0x170) = 1u << 20;                    /* display on */
	R(W_SPM, 0x344) = 0x1112;                                        /* off: ISO, CLK_DIS, SRAM_PDN + ack */
	R(W_INFRA, 0x2d0) = R(W_INFRA, 0x2ec) = 3u << 26; R(W_INFRA, 0xdc8) = R(W_INFRA, 0xdd8) = 3;
	R(W_TOP, 0xe0) = 0x80000000;                                     /* venc_sel gated, parent 0 */
	R(W_SMI, 0x100) = 0xb; R(W_SMI, 0x220) = 0x4514; R(W_SMI, 0x300) = 0x4f1;
	for (unsigned p = 0; p < 27; p++) { R(W_LARB, 0x380 + 4 * p) = 0x5501; }   /* as Android leaves a port: translating, bank bits */
}
static void spm_update(void)
{
	uint32_t c = R(W_SPM, 0x344);
	bool on = (c & 0xc) == 0xc && !F.no_power_ack;
	if (!(c & 0xc)) { on = false; } else if ((c & 0xc) != 0xc) { on = (R(W_SPM, 0x16c) >> 17) & 1; }
	if (F.dis_off) { R(W_SPM, 0x16c) &= ~(1u << 20); R(W_SPM, 0x170) &= ~(1u << 20); }
	R(W_SPM, 0x16c) = (R(W_SPM, 0x16c) & ~(1u << 17)) | (on ? 1u << 17 : 0);
	R(W_SPM, 0x170) = (R(W_SPM, 0x170) & ~(1u << 17)) | (on ? 1u << 17 : 0);
	if (!F.sram_stuck) { c = (c & ~(1u << 12)) | ((c & (1u << 8)) ? 1u << 12 : 0); }
	R(W_SPM, 0x344) = c;
}
static void hw_encode(void)
{
	uint32_t ctrl = R(W_ENC, 0x104), dst = R(W_ENC, 0x120), luma = R(W_ENC, 0x170), chroma = R(W_ENC, 0x174), stall = R(W_ENC, 0x128);
	uint32_t w = R(W_ENC, 0x154) >> 16, h = R(W_ENC, 0x154) & 0xffff, stride = R(W_ENC, 0x178);
	for (unsigned p = 7; p <= 10; p++) { if (R(W_LARB, 0x380 + 4 * p) & 0xff01) { viol("start with a translating / banked larb port", p); } }
	if (R(W_LARB, 0x24) != 0x300256 || R(W_LARB, 0x40) != 1) { viol("larb7 not configured", 0); }
	uint32_t rows = (h + 15) & ~15u;
	if (luma < 0xd8000000u || chroma != luma + stride * rows || chroma + stride * rows / 2 > 0xd8800000u) { viol("source outside the claim", luma); }
	if (dst < 0xd8800000u || stall > 0xd9000000u || stall <= dst || (dst & 15)) { viol("destination outside the claim", dst); }
	if ((ctrl & 0x18) != 0x10 || !(ctrl & (1u << 20)) || !(ctrl & (1u << 29)) || (ctrl & (1u << 30)) || (ctrl & 0x420)) { viol("CTRL", ctrl); }
	if (R(W_ENC, 0x10c) != (stride / 16) * (rows / 16) * 6 - 1 || R(W_ENC, 0x17c) != stride || stride < w || R(W_ENC, 0x100) != 1) { viol("geometry", R(W_ENC, 0x10c)); }
	if (F.never_done) { return; }
	if (F.stall) { R(W_ENC, 0x11c) = 2; R(W_ENC, 0x124) = stall; return; }
	/* a stand-in stream: SOI, the luma checksum, the quality code, EOI */
	uint8_t *o = ram + (dst - 0xd8000000u);
	uint32_t sum = 0;
	for (uint32_t i = 0; i < stride * rows * 3 / 2; i++) { sum = sum * 31 + ram[luma - 0xd8000000u + i]; }
	uint32_t n = 64 + (w * h) / 64;
	memset(o, 0x11, n);
	o[0] = 0xff; o[1] = 0xd8; memcpy(o + 2, &sum, 4); o[6] = (uint8_t)R(W_ENC, 0x108); o[n - 2] = 0xff; o[n - 1] = 0xd9;
	R(W_ENC, 0x124) = dst + n; R(W_ENC, 0x110) = R(W_ENC, 0x10c);
	polls_left = F.done_after;
}
static bool lookup(vm_offset_t va, int *w, uint32_t *off)
{
	for (int i = 0; i < W_N; i++) { if (va >= (vm_offset_t)reg[i] && va < (vm_offset_t)reg[i] + 0x1000) { *w = i; *off = (uint32_t)(va - (vm_offset_t)reg[i]); return true; } }
	return false;
}
static void guard(int w, uint32_t off)
{
	if (w == W_CG || w == W_LARB || w == W_ENC) {
		if (!dom_on()) { viol("VEN register touched with the domain off / protected", wbase[w] + off); }
		else if (w == W_ENC && (R(W_CG, 0) & 0x101) != 0x101) { viol("encoder touched with its clock gated", wbase[w] + off); }
		else if (w == W_LARB && (R(W_CG, 0) & 0x11) != 0x11) { viol("larb touched with its clock gated", wbase[w] + off); }
	}
	if (w == W_SMI && !(R(W_SPM, 0x16c) & (1u << 20))) { viol("display SMI touched with the display off", off); }
}
static uint32_t ni_rd(vm_offset_t b, unsigned off)
{
	int w; uint32_t o;
	if (!lookup(b + off, &w, &o) || (o & 3)) { viol("read outside a window", (uint32_t)off); return 0; }
	guard(w, o);
	if (w == W_ENC && o == 0x11c && (R(W_ENC, 0x104) & 1) && !R(W_ENC, 0x11c) && !F.never_done && !F.stall && R(W_ENC, 0x124)) {
		if (polls_left) { polls_left--; } else { R(W_ENC, 0x11c) = 1; }
	}
	return R(w, o);
}
static void ni_wr(vm_offset_t b, unsigned off, uint32_t v)
{
	int w; uint32_t o;
	if (!lookup(b + off, &w, &o) || (o & 3)) { viol("write outside a window", (uint32_t)off); return; }
	guard(w, o);
	if (w == W_SPM) { if (o != 0 && o != 0x344) { viol("unexpected SPM write", o); } R(w, o) = v; spm_update(); return; }
	if (w == W_INFRA) {
		uint32_t *en = NULL, *ack = NULL; bool set = false;
		if (o == 0x2d4 || o == 0x2d8) { en = &R(w, 0x2d0); ack = &R(w, 0x2ec); set = o == 0x2d4; if (v & ~(3u << 26)) { viol("foreign MM protection bits", v); } }
		else if (o == 0xdcc || o == 0xdd0) { en = &R(w, 0xdc8); ack = &R(w, 0xdd8); set = o == 0xdcc; if (v & ~3u) { viol("foreign MM_2 protection bits", v); } }
		else { viol("unexpected infracfg write", o); return; }
		if (set) { *en |= v; if (!F.prot_stuck) { *ack |= v; } } else { *en &= ~v; *ack &= ~v; }
		return;
	}
	if (w == W_TOP) {
		if (F.mux_dead) { return; }
		if (o == 0xe4) { R(w, 0xe0) |= v; } else if (o == 0xe8) { R(w, 0xe0) &= ~v; } else if (o != 0x08) { viol("unexpected topckgen write", o); }
		if ((o == 0xe4 || o == 0xe8) && (v & 0x70ffffff)) { viol("foreign CLK_CFG_13 bits", v); }
		if (o == 0x08 && v != 1u << 24) { viol("foreign mux update bits", v); }
		return;
	}
	if (w == W_SMI) { viol("write to the display's SMI common", o); return; }
	writes_venc++;
	if (w == W_CG) { if (o == 4) { R(w, 0) |= v; } else if (o == 8) { R(w, 0) &= ~v; } else { viol("unexpected vencsys write", o); } return; }
	if (w == W_LARB) {
		bool ok = o == 0x24 || o == 0x40 || o == 0x524 || (o >= 0x21c && o <= 0x228) || (o >= 0x39c && o <= 0x3a8);
		if (!ok) { viol("larb7 register of another user written", o); }
		R(w, o) = v; return;
	}
	if (o == 0x11c) { R(w, o) = 0; return; }
	if (o == 0x100 && v == 0) { memset(reg[W_ENC], 0, sizeof(reg[W_ENC])); return; }
	R(w, o) = v;
	if (o == 0x104 && (v & 1)) { R(W_ENC, 0x11c) = 0; R(W_ENC, 0x124) = 0; hw_encode(); }
}
static void ni_mask(vm_offset_t b, unsigned off, uint32_t mask, uint32_t v) { ni_wr(b, off, (ni_rd(b, off) & ~mask) | (v & mask)); }
static vm_offset_t n2j_map(uint64_t pa, uint32_t bytes, int io)
{
	if (!io) { CHECK(pa == 0xd8000000ULL && bytes == 0x1000000); return (vm_offset_t)ram; }
	for (int i = 0; i < W_N; i++) { if (wbase[i] == pa) { CHECK(bytes == 0x1000); return (vm_offset_t)reg[i]; } }
	viol("unknown mapping", (uint32_t)pa); return 0;
}
static void n2j_us(unsigned us) { now_us += us; }
static void n2j_cache(vm_offset_t va, uint32_t len) { CHECK(va >= (vm_offset_t)ram && va + len <= (vm_offset_t)ram + 0x1000000); }
static int n2j_log(const char *f, ...) { if (quiet) { return 0; } va_list a; va_start(a, f); vprintf(f, a); va_end(a); return 0; }
static uint64_t mach_absolute_time(void) { return now_us * 1000; }
static void absolutetime_to_nanoseconds(uint64_t t, uint64_t *ns) { *ns = t; }

#include "../kernel/b/bsd/dev/nord2_hwcodec.inc"

static uint32_t cmd(uint32_t step, uint32_t w, uint32_t h, uint32_t q, const uint8_t **out, uint32_t *n)
{
	const uint32_t r[16] = {w, h, q};
	return n2j_command(step, r, out, n);
}
static void fresh(void) { model_reset(); memset(&n2j, 0, sizeof(n2j)); memset(ram, 0, 0x1000000); }

int main(int argc, char **argv)
{
	const uint8_t *out; uint32_t n;
	quiet = argc != 2;
	ram = malloc(0x1000000);
	/* 1. the whole sequence */
	fresh();
	CHECK(cmd(0, 0, 0, 0, &out, &n) == N2J_OK); CHECK(writes_venc == 0);
	CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_E_NOT_ON); CHECK(cmd(2, 0, 0, 0, &out, &n) == N2J_E_NOT_ON); CHECK(writes_venc == 0);
	CHECK(cmd(1, 0, 0, 0, &out, &n) == N2J_OK);
	CHECK(dom_on()); CHECK((R(W_TOP, 0xe0) >> 24) == 8); CHECK((R(W_CG, 0) & 0x10000111) == 0x10000111);
	CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_E_NOT_ON);                       /* step 2 missing */
	CHECK(cmd(2, 0, 0, 0, &out, &n) == N2J_OK);
	for (unsigned p = 0; p < 27; p++) { CHECK(R(W_LARB, 0x380 + 4 * p) == ((p >= 7 && p <= 10) ? 0x5400u & 0 : 0x5501u)); }
	CHECK(R(W_LARB, 0x21c) == 1 && R(W_LARB, 0x220) == 1 && R(W_LARB, 0x224) == 4 && R(W_LARB, 0x228) == 4);
	CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_OK); CHECK(out == ram + 0x800000 && n == 64 + 640 * 480 / 64);
	CHECK(out[0] == 0xff && out[1] == 0xd8 && out[6] == 7 && out[n - 1] == 0xd9);
	uint32_t sum640; memcpy(&sum640, out + 2, 4);
	CHECK(R(W_ENC, 0x154) == (640u << 16 | 480)); CHECK(R(W_ENC, 0x178) == 640); CHECK(R(W_ENC, 0x174) == 0xd8000000u + 640 * 480);
	/* odd sizes: padding to 16, sizes up to the claim */
	CHECK(cmd(3, 1918, 1082, 2, &out, &n) == N2J_OK); CHECK(R(W_ENC, 0x178) == 1920 && R(W_ENC, 0x174) == 0xd8000000u + 1920 * 1088 && out[6] == 2);
	CHECK(cmd(3, 4032, 3024, 0x10, &out, &n) == N2J_E_ARGS);                 /* 18.3 MB of NV12 does not fit 8 MiB: refused, M2 enlarges */
	CHECK(cmd(3, 2560, 1920, 0x10, &out, &n) == N2J_OK);
	CHECK(cmd(3, 641, 480, 7, &out, &n) == N2J_E_ARGS); CHECK(cmd(3, 640, 8, 7, &out, &n) == N2J_E_ARGS); CHECK(cmd(3, 640, 480, 0x11, &out, &n) == N2J_E_ARGS);
	CHECK(cmd(3, 4098, 16, 7, &out, &n) == N2J_E_ARGS); CHECK(cmd(5, 0, 0, 0, &out, &n) == N2J_E_STEP);
	/* slow hardware within the limit; the same picture gives the same bytes */
	F.done_after = 15000; CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_OK); F.done_after = 0;
	uint32_t again; memcpy(&again, out + 2, 4); CHECK(again == sum640);
	CHECK(cmd(4, 0, 0, 0, &out, &n) == N2J_OK);
	CHECK(!dom_on()); CHECK(!(R(W_SPM, 0x16c) & (1u << 17))); CHECK((R(W_SPM, 0x344) & 0x111f) == 0x1112); CHECK((R(W_INFRA, 0x2d0) >> 26) == 3 && R(W_INFRA, 0xdc8) == 3);
	CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_E_NOT_ON); CHECK(cmd(4, 0, 0, 0, &out, &n) == N2J_OK);
	/* and on again */
	CHECK(cmd(1, 0, 0, 0, &out, &n) == N2J_OK); CHECK(cmd(2, 0, 0, 0, &out, &n) == N2J_OK); CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_OK);
	/* 2. faults */
	fresh(); F.dis_off = true; spm_update(); CHECK(cmd(1, 0, 0, 0, &out, &n) == N2J_E_DIS_OFF); CHECK(R(W_SPM, 0x344) == 0x1112 && R(W_TOP, 0xe0) == 0x80000000);
	fresh(); F.mux_dead = true; CHECK(cmd(1, 0, 0, 0, &out, &n) == N2J_E_MUX); CHECK(R(W_SPM, 0x344) == 0x1112);
	fresh(); F.no_power_ack = true; CHECK(cmd(1, 0, 0, 0, &out, &n) == N2J_E_PWR_TIMEOUT); CHECK(R(W_SPM, 0x344) == 0x1112); CHECK((R(W_INFRA, 0x2d0) >> 26) == 3); CHECK(writes_venc == 0);
	fresh(); F.sram_stuck = true; CHECK(cmd(1, 0, 0, 0, &out, &n) == N2J_E_SRAM_TIMEOUT); CHECK((R(W_INFRA, 0x2d0) >> 26) == 3 && R(W_INFRA, 0xdc8) == 3); CHECK(writes_venc == 0);
	fresh(); R(W_TOP, 0xe0) = 3u << 24; CHECK(cmd(1, 0, 0, 0, &out, &n) == N2J_OK); CHECK(R(W_TOP, 0xe0) == 3u << 24);   /* a running mux is left alone */
	CHECK(cmd(2, 0, 0, 0, &out, &n) == N2J_OK);
	F.never_done = true; CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_E_ENC_TIMEOUT); CHECK(out == NULL && n == 0); CHECK(!(R(W_ENC, 0x104) & 1)); F.never_done = false;
	F.stall = true; CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_E_ENC_STATUS); CHECK(out == NULL); F.stall = false;
	CHECK(cmd(3, 0, 0, 0, &out, &n) == N2J_OK);
	F.prot_stuck = true; CHECK(cmd(4, 0, 0, 0, &out, &n) == N2J_E_PROT_TIMEOUT); CHECK(R(W_SPM, 0x16c) & (1u << 17));    /* left powered */
	/* 3. the picture */
	static uint8_t pic[656 * 496 * 3 / 2 + 16];
	memset(pic, 0xee, sizeof(pic)); n2j_pattern(pic, 642, 482, 656, 496);
	CHECK(pic[656 * 496 * 3 / 2] == 0xee);
	unsigned lo = 255, hi = 0; for (unsigned i = 0; i < 656 * 496; i++) { if (pic[i] < lo) { lo = pic[i]; } if (pic[i] > hi) { hi = pic[i]; } }
	CHECK(lo >= 16 && hi <= 243); CHECK(pic[641] == pic[655]); CHECK(pic[656 * 481 + 5] == pic[656 * 495 + 5]);
	if (argc == 3) { FILE *f = fopen(argv[2], "wb"); if (f) { fwrite(pic, 1, 656 * 496 * 3 / 2, f); fclose(f); } }   /* for tools/hwc.py's copy of the formula */
	CHECK(violations == 0);
	printf("N2TEST hwcodec-M1 %s %d/%d (violations %d)\n", fails || violations ? "FAIL" : "PASS", checks - fails, checks, violations);
	return fails || violations ? 1 : 0;
}
