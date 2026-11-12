/* SPDX-License-Identifier: MIT */
/*
 * t_fakevdec_hevc.c -- unit test of fakevdec_hevc/n2vdec_hevc_fake.c without the vendor libraries.
 * An inner stub back end holds one DMA buffer (the bitstream ring); the test drives the register sequences the vendor
 * HEVC path issues (facts/HEVC_DEC_PROTOCOL.md section 2) and checks the values the vendor code relies on.
 * Expected values are hand-encoded H.265 bytes / Exp-Golomb codes.  Last line: N2TEST FAKEVDEC-HEVC PASS n/n.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "n2vdec_hevc_fake.h"

/* V41 symbol the model calls when it has guest memory; the test has none (mem NULL -> inner dma_rw). */
int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes)
{
	(void)handle; (void)pa; (void)guest_addr; (void)bytes;
	return -ENOENT;
}

static uint8_t g_buf[8192];
static const uint32_t PA = 0xda900000u;
static int st_power(void *c, unsigned b, int on) { (void)c; (void)b; (void)on; return 0; }
static int st_regs(void *c, struct rwvc_reg_op *o, unsigned n, unsigned *d) { (void)c; (void)o; if (d) *d = n; return 0; }
static int st_wait(void *c, unsigned i, unsigned t, uint32_t *s) { (void)c; (void)i; (void)t; (void)s; return -ETIMEDOUT; }
static int st_clear(void *c, unsigned i) { (void)c; (void)i; return 0; }
static int st_alloc(void *c, uint32_t b, uint32_t f, uint32_t *h, uint32_t *pa) { (void)c; (void)b; (void)f; *h = 7; *pa = PA; return 0; }
static int st_free(void *c, uint32_t h) { (void)c; (void)h; return 0; }
static int st_rw(void *c, uint32_t h, uint32_t off, void *host, uint32_t n, int wr)
{
	(void)c; (void)h;
	if (off + n > sizeof g_buf) return -EINVAL;
	if (wr) memcpy(g_buf + off, host, n); else memcpy(host, g_buf + off, n);
	return 0;
}
static void st_close(void *c) { (void)c; }

static struct n2vc_hw hw;
static unsigned pass, total;
static void ck(const char *what, uint32_t got, uint32_t want)
{
	total++;
	if (got == want) pass++;
	else printf("FAIL %s: got 0x%08x want 0x%08x\n", what, got, want);
}
static uint32_t rd(uint32_t abs) { struct rwvc_reg_op o = { RWVC_OP_READ, 12, 0, abs, 0, 0 }; unsigned d; hw.regs(hw.ctx, &o, 1, &d); return o.value; }
static void wr(uint32_t abs, uint32_t v) { struct rwvc_reg_op o = { RWVC_OP_WRITE, 12, 0, abs, v, 0 }; unsigned d; hw.regs(hw.ctx, &o, 1, &d); }
static uint32_t rdw(unsigned win, uint32_t off) { struct rwvc_reg_op o = { RWVC_OP_READ, (uint8_t)win, 0, off, 0, 0 }; unsigned d; hw.regs(hw.ctx, &o, 1, &d); return o.value; }

/* The LAT unit as the vendor initialises it (vdec_hal_init_barrel_shift + vdec_hal_h265_init_barrel_shifter_impl). */
static void init_lat(uint32_t rptr, uint32_t end)
{
	wr(0x110b4, PA >> 6);                 /* vfifo start */
	wr(0x110b8, end >> 6);                /* vfifo end */
	wr(0x110b0, rptr);                    /* read pointer (latched 16-byte aligned) */
	wr(0x11108, 0x100); wr(0x11108, 0);
	wr(0x1108c, 0x100000);                /* init fetch */
	ck("fetch ok VLD(58)", rd(0x110e8) & 1, 1);
	wr(0x1108c, 0x800000);
	for (uint32_t i = 0; i < (rptr & 15); i++) (void)rd(0x15000 + 8 * 4);   /* vendor skips rptr & 15 bytes: read(8) */
}

int main(void)
{
	struct n2vc_hw inner = { NULL, st_power, st_regs, st_wait, st_clear, st_alloc, st_free, st_rw, st_close };
	struct n2vdec_hevc_fake *m; uint32_t h, pa, s;
	if (n2vdec_hevc_fake_open(&hw, &inner, NULL, &m)) { puts("open failed"); return 1; }
	hw.dma_alloc(hw.ctx, sizeof g_buf, 0, &h, &pa);

	/* 1. stream: junk, then at 0x23: 00 00 01 | 26 01 (IDR_W_RADL) | slice bits; then 00 00 03 01 (EPB);
	 *    then a second NAL 00 00 01 02 01 (TRAIL_R) with first_slice_segment_in_pic_flag = 1 */
	static const uint8_t s1[] = { 0x00, 0x00, 0x01, 0x26, 0x01, 0xaf, 0x08, 0x40,
		0x00, 0x00, 0x03, 0x01, 0x5f, 0x00, 0x00, 0x01, 0x02, 0x01, 0xd0 };
	memset(g_buf, 0x55, sizeof g_buf);
	memcpy(g_buf + 0x23, s1, sizeof s1);
	init_lat(PA + 0x23, PA + 0x200);
	ck("peek at start code HEVC_VLD(0)", rd(0x15000), 0x00000126);
	ck("bit count after 3-byte skip HEVC_VLD(33)[13:8]", (rd(0x15084) >> 8) & 0x3f, 0x18);   /* 0x23 & 15 = 3 bytes */
	ck("read(8) = shift 8, next window", rd(0x15000 + 8 * 4), 0x00012601);
	ck("read(32) -> window at slice header", rd(0x15000 + 32 * 4), 0xaf084000);
	/* af = 1010 1111: ue: '1' = 0; then '010' = 1; then '1' = 0; then '1' = 0; then '1' = 0 */
	ck("ue 1 -> 0", rd(0x15000 + 0x26 * 4), 0);
	ck("ue 010 -> 1", rd(0x15000 + 0x26 * 4), 1);
	ck("se 1 -> 0", rd(0x15000 + 0x27 * 4), 0);
	ck("bit count after 4 bits", (rd(0x15084) >> 8) & 0x3f, (0x18 + 40 + 5) & 31);
	(void)rd(0x15000 + 3 * 4);            /* consume the rest of 0xaf */
	ck("window over EPB 08 40 00 00 | 01 (03 removed)", rd(0x15000 + 8 * 4), 0x40000001);
	/* 2. start-code search HEVC_VLD(37) bit 8: window on the next 00 00 01; bit 8 reads back 0 */
	wr(0x15094, 0x100);
	ck("search done HEVC_VLD(37) bit8=0", rd(0x15094) & 0x100, 0);
	ck("window on next start code", rd(0x15000), 0x00000102);
	(void)rd(0x15000 + 8 * 4);            /* H265_HAL_GetStartCode_PicStart: get_bit_stream_shift(8), then (32) */
	s = rd(0x15000);
	ck("nal_unit_type = ubfx(peek, 9, 6)", (s >> 9) & 0x3f, 1);
	s = rd(0x15000 + 0x20 * 4);
	ck("first_slice_segment_in_pic_flag (bit 31 after 40 bits)", s >> 31, 1);
	/* 3. LAT trigger HEVC_VLD(36) bit 0 -> LAT done IRQ, LAT_MISC(41) bit 16, last LCU from VLD_TOP(26), pic complete */
	wr(0x11868, 0x00030004);
	wr(0x15090, 1);
	ck("LAT IRQ raised", (uint32_t)hw.wait_irq(hw.ctx, RWVC_IRQ_VDEC_LAT, 10, &s), 0);
	ck("LAT_MISC(41) bit16 (finish flag)", rd(0x100a4) & 0x10000, 0x10000);
	ck("LAT mbx/mby HEVC_VLD(75)", rd(0x1512c), 0x00030004);
	ck("LAT pic complete HEVC_VLD(158) bit31", rd(0x15278) >> 31, 1);
	wr(0x100a4, rd(0x100a4) | 0x10); wr(0x100a4, rd(0x100a4) & ~0x10u);   /* vdec_hal_clr_isr */
	ck("finish flag cleared", rd(0x100a4) & 0x10000, 0);
	ck("no second LAT IRQ", (uint32_t)hw.wait_irq(hw.ctx, RWVC_IRQ_VDEC_LAT, 5, &s), (uint32_t)-ETIMEDOUT);
	/* 4. core trigger (core unit never got a vfifo: reads the LAT output) */
	wr(0x20868, 0x00030004);
	wr(0x28090, 1);
	ck("core IRQ raised", (uint32_t)hw.wait_irq(hw.ctx, RWVC_IRQ_VDEC, 10, &s), 0);
	ck("MISC(41) bit16 via window 7", rdw(7, 0xa4) & 0x10000, 0x10000);
	ck("core mbx/mby", rd(0x2812c), 0x00030004);
	/* 5. handshakes */
	wr(0x0d00c, 1); ck("larb5 SLP_CON bit16 follows bit0", rd(0x0d00c) & 0x10000, 0x10000);
	wr(0x2e00c, 0); ck("larb4 SLP_CON release", rd(0x2e00c) & 0x10000, 0);
	ck("WRAP_TOP(1)[5:0] idle", rd(0x02004) & 0x3f, 0x20);
	wr(0x25100, 1); ck("HW break MISC(65) 0x11", rd(0x25104) & 0x11, 0x11);
	ck("process flag VLD(61) bit15=0", rd(0x200f4) & 0x8000, 0);
	ck("MISC(68) all-dram idle", rd(0x25110), 0);
	ck("window 9 (LAT_VLD) is the same unit", rdw(9, 0xe8) & 1, 1);
	hw.close(hw.ctx);
	printf("N2TEST FAKEVDEC-HEVC %s %u/%u\n", pass == total ? "PASS" : "FAIL", pass, total);
	return pass == total ? 0 : 1;
}
