/* SPDX-License-Identifier: MIT */
/*
 * t_fakevenc.c -- host test of fakevenc/n2venc_fake.c on top of T02's fake (tests/hw/n2vc_hw_fake.c).
 * Usage: t_fakevenc <src.nv12 320x240> <headers.h264 (SPS/PPS from an oracle encoder)> <out.h264>
 * The decoded-picture comparison against the source is done by run_tests.sh with ffmpeg (oracle).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2vc_hw.h"
#include "n2vc_hw_fake.h"
#include "n2venc_fake.h"

static unsigned pass, total;
#define CHECK(cond, ...) do { total++; if (cond) pass++; else { printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

/* The model finds buffers through V41's DMA registry; the test provides a minimal one. */
struct tbuf { uint32_t handle, pa, bytes; };
static struct tbuf g_buf[8];
static unsigned g_nbuf;

int n2vc_dma_lookup_pa(uint32_t pa, uint32_t *handle, uint32_t *guest_addr, uint32_t *bytes)
{
	for (unsigned i = 0; i < g_nbuf; i++)
		if (pa >= g_buf[i].pa && pa - g_buf[i].pa < g_buf[i].bytes) {
			if (handle)
				*handle = g_buf[i].handle;
			if (guest_addr)
				*guest_addr = 0;
			if (bytes)
				*bytes = g_buf[i].bytes - (pa - g_buf[i].pa);
			return 0;
		}
	return -ENOENT;
}

int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes);
int n2vc_dma_lookup_handle(uint32_t handle, uint32_t *pa, uint32_t *guest_addr, uint32_t *bytes)
{
	for (unsigned i = 0; i < g_nbuf; i++)
		if (g_buf[i].handle == handle) {
			if (pa)
				*pa = g_buf[i].pa;
			if (guest_addr)
				*guest_addr = 0;
			if (bytes)
				*bytes = g_buf[i].bytes;
			return 0;
		}
	return -ENOENT;
}

static struct tbuf alloc(struct n2vc_hw *hw, uint32_t bytes)
{
	struct tbuf b = {0, 0, bytes};

	if (hw->dma_alloc(hw->ctx, bytes, 0, &b.handle, &b.pa) == 0 && g_nbuf < 8)
		g_buf[g_nbuf++] = b;
	return b;
}

static int wr(struct n2vc_hw *hw, unsigned win, uint32_t off, uint32_t v)
{
	return n2vc_hw_wr(hw, win, off, v);
}

static uint32_t rd(struct n2vc_hw *hw, unsigned win, uint32_t off)
{
	return n2vc_hw_rd(hw, win, off);
}

static uint8_t *load(const char *path, uint32_t *n)
{
	FILE *f = fopen(path, "rb");
	uint8_t *p;
	long len;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	p = malloc(len > 0 ? (size_t)len : 1u);
	if (p && fread(p, 1, (size_t)len, f) != (size_t)len) {
		free(p);
		p = NULL;
	}
	fclose(f);
	*n = (uint32_t)len;
	return p;
}

/* Keep only the SPS (7) and PPS (8) NAL units of an Annex-B stream (4-byte start codes in the output). */
static uint32_t params_only(const uint8_t *p, uint32_t n, uint8_t *out)
{
	uint32_t i = 0, o = 0;

	while (i + 3u < n) {
		uint32_t s, e;

		if (!(p[i] == 0 && p[i + 1] == 0 && p[i + 2] == 1)) {
			i++;
			continue;
		}
		s = i + 3u;
		for (e = s; e + 2u < n && !(p[e] == 0 && p[e + 1] == 0 && (p[e + 2] == 1 || p[e + 2] == 0)); e++)
			;
		if (e + 2u >= n)
			e = n;
		if ((p[s] & 0x1fu) == 7 || (p[s] & 0x1fu) == 8) {
			memcpy(out + o, "\0\0\0\1", 4);
			memcpy(out + o + 4, p + s, e - s);
			o += 4u + (e - s);
		}
		i = e;
	}
	return o;
}

/* Program core `win` like H264_SeqLvlMMRSetting / _SetBufs do for a W x H NV12 frame (offsets and value
 * encodings from facts/VENC_PROTOCOL.md section 3). */
static void program(struct n2vc_hw *hw, unsigned win, unsigned w, unsigned h, const struct tbuf *src,
		    const struct tbuf *bs, uint32_t bs_size_reg)
{
	uint32_t p16 = w / 16u;

	(void)wr(hw, win, 0x024, ((h / 16u) << 16) | (w / 16u));
	(void)wr(hw, win, 0x028, (p16 << 20) | (p16 << 11));
	(void)wr(hw, win, 0x064, bs->pa >> 4);
	(void)wr(hw, win, 0x068, bs_size_reg);
	(void)wr(hw, win, 0x06c, src->pa >> 4);
	(void)wr(hw, win, 0x070, (src->pa + w * h) >> 4);
	(void)wr(hw, win, 0x094, 0);
}

int main(int argc, char **argv)
{
	struct n2vc_hw inner, hw;
	struct n2venc_fake *f = NULL;
	struct n2venc_fake_stats st;
	uint32_t nsrc = 0, nhdr = 0, status = 0, bytes;
	uint8_t *src, *hdr, *out;
	struct tbuf sbuf, bsbuf;
	struct rwvc_reg_op op;
	unsigned done = 0;
	FILE *o;

	if (argc < 4) {
		printf("usage: t_fakevenc src.nv12 headers.h264 out.h264\n");
		return 2;
	}
	src = load(argv[1], &nsrc);
	hdr = load(argv[2], &nhdr);
	CHECK(src && nsrc == 320u * 240u * 3u / 2u, "source picture %u bytes", nsrc);
	CHECK(hdr && nhdr > 8, "header file");
	if (!src || !hdr)
		goto end;
	{
		uint8_t *ps = malloc((size_t)nhdr + 64u);   /* 3-byte start codes grow by one byte each */

		if (!ps)
			goto end;
		nhdr = params_only(hdr, nhdr, ps);
		free(hdr);
		hdr = ps;
	}
	CHECK(nhdr > 8 && nhdr < 128, "SPS+PPS %u bytes", nhdr);

	CHECK(n2vc_hw_open_fake(&inner, NULL) == 0, "open T02 fake");
	CHECK(n2venc_fake_wrap(&inner, &hw, &f, NULL) == 0 && f, "wrap");
	CHECK(n2venc_fake_of(&hw) == f, "fake_of(outer)");
	CHECK(n2venc_fake_of(&inner) == NULL, "fake_of(inner) is NULL");
	CHECK(n2venc_fake_wrap(NULL, &hw, &f, NULL) == -EINVAL, "wrap NULL inner");

	/* Inner power rule is kept: VENC unpowered -> -EACCES, and a violation is counted. */
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x0a8, 0) == -EACCES, "write while unpowered");
	CHECK(n2vc_hw_fake_violations(&inner) == 1, "violation counted by the inner fake");
	CHECK(hw.power(hw.ctx, RWVC_BLK_VENC, 1) == 0, "power VENC");

	/* H264_ResetVENC: write 0, read 0, write 1, read 1. */
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x0a8, 0) == 0 && rd(&hw, RWVC_WIN_VENC, 0x0a8) == 0, "reset 0");
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x0a8, 1) == 0 && rd(&hw, RWVC_WIN_VENC, 0x0a8) == 1, "reset 1");

	sbuf = alloc(&hw, nsrc);
	bsbuf = alloc(&hw, 1u << 20);
	CHECK(sbuf.handle && bsbuf.handle, "dma alloc");
	CHECK(hw.dma_rw(hw.ctx, sbuf.handle, 0, src, nsrc, 1) == 0, "upload source");
	CHECK(n2venc_fake_set_headers(f, hdr, nhdr) == 0, "headers parsed (SPS+PPS)");
	CHECK(n2venc_fake_set_headers(f, (const uint8_t *)"\0\0\1\x09\x10", 5) == -EINVAL, "no SPS -> -EINVAL");

	program(&hw, RWVC_WIN_VENC, 320, 240, &sbuf, &bsbuf, 0x2000);
	CHECK(rd(&hw, RWVC_WIN_VENC, 0x05c) == 0, "irq status clear before trigger");
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x05c, 0xff) == 0 && rd(&hw, RWVC_WIN_VENC, 0x05c) == 0, "irq status read only");
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x0ec, 1) == 0, "0xec");
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x058, 4) == 0, "trigger FRM");
	CHECK(rd(&hw, RWVC_WIN_VENC, 0x05c) == 4, "irq status FRM");
	bytes = rd(&hw, RWVC_WIN_VENC, 0x098);
	/* 300 I_PCM MBs: at least 300 * 384 sample bytes + headers. */
	CHECK(bytes > 300u * 384u && bytes < (1u << 20), "BYTE_CNT %u", bytes);

	op = (struct rwvc_reg_op){RWVC_OP_POLL, RWVC_WIN_VENC, 0, 0x05c, 4, 4};
	CHECK(hw.regs(hw.ctx, &op, 1, &done) == 0 && done == 1, "POLL on the model status");
	op = (struct rwvc_reg_op){RWVC_OP_POLL, RWVC_WIN_VENC, 0, 0x05c, 8, 8};
	CHECK(hw.regs(hw.ctx, &op, 1, &done) == -ETIMEDOUT && done == 0, "POLL miss -> -ETIMEDOUT");

	CHECK(hw.wait_irq(hw.ctx, RWVC_IRQ_VENC, 100, &status) == 0 && status == 4, "wait_irq VENC status %#x", status);
	CHECK(hw.wait_irq(hw.ctx, RWVC_IRQ_VENC, 5, &status) == -ETIMEDOUT, "only one interrupt");
	CHECK(hw.wait_irq(hw.ctx, RWVC_IRQ_VENC_C1, 5, &status) == -ETIMEDOUT, "no interrupt on core 1");
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x060, 4) == 0 && rd(&hw, RWVC_WIN_VENC, 0x05c) == 0, "ack clears FRM");

	out = malloc(bytes + nhdr);
	if (out && hw.dma_rw(hw.ctx, bsbuf.handle, 0, out + nhdr, bytes, 0) == 0) {
		CHECK(!memcmp(out + nhdr, "\0\0\0\1\x65", 5), "IDR NAL start");
		memcpy(out, hdr, nhdr);
		o = fopen(argv[3], "wb");
		CHECK(o && fwrite(out, 1, bytes + nhdr, o) == bytes + nhdr, "write %s", argv[3]);
		if (o)
			fclose(o);
	} else {
		CHECK(0, "read bitstream");
	}
	free(out);

	/* Bitstream buffer too small: DRAM (0x8) instead of FRM, BYTE_CNT = capacity. */
	program(&hw, RWVC_WIN_VENC, 320, 240, &sbuf, &bsbuf, 8);   /* 8 << 7 = 1 KiB */
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x058, 4) == 0, "trigger small");
	CHECK(rd(&hw, RWVC_WIN_VENC, 0x05c) == 8, "DRAM status");
	CHECK(rd(&hw, RWVC_WIN_VENC, 0x098) == 1024, "BYTE_CNT = capacity");
	CHECK(hw.wait_irq(hw.ctx, RWVC_IRQ_VENC, 100, &status) == 0 && status == 8, "DRAM interrupt");
	CHECK(wr(&hw, RWVC_WIN_VENC, 0x060, 8) == 0, "ack DRAM");

	/* Core 1 has its own registers and interrupt line, and its own power block. */
	CHECK(wr(&hw, RWVC_WIN_VENC_C1, 0x058, 4) == -EACCES, "core 1 unpowered");
	CHECK(hw.power(hw.ctx, RWVC_BLK_VENC_C1, 1) == 0, "power core 1");
	program(&hw, RWVC_WIN_VENC_C1, 320, 240, &sbuf, &bsbuf, 0x2000);
	CHECK(rd(&hw, RWVC_WIN_VENC_C1, 0x064) == bsbuf.pa >> 4, "core 1 register file");
	CHECK(wr(&hw, RWVC_WIN_VENC_C1, 0x058, 4) == 0, "trigger core 1");
	CHECK(hw.wait_irq(hw.ctx, RWVC_IRQ_VENC_C1, 100, &status) == 0 && status == 4, "core 1 interrupt");
	CHECK(rd(&hw, RWVC_WIN_VENC_C1, 0x098) == bytes, "core 1 same frame size");

	/* Reset clears status and pending interrupts. */
	CHECK(wr(&hw, RWVC_WIN_VENC_C1, 0x058, 4) == 0 && wr(&hw, RWVC_WIN_VENC_C1, 0x0a8, 0) == 0, "trigger+reset");
	CHECK(rd(&hw, RWVC_WIN_VENC_C1, 0x05c) == 0, "reset clears status");
	CHECK(hw.wait_irq(hw.ctx, RWVC_IRQ_VENC_C1, 5, &status) == -ETIMEDOUT, "reset drops the interrupt");

	/* Without headers the model learns SPS/PPS from the start of the bitstream buffer. */
	{
		struct n2vc_hw in2, hw2;
		struct n2venc_fake *f2 = NULL;
		struct tbuf b2;

		CHECK(n2vc_hw_open_fake(&in2, NULL) == 0 && n2venc_fake_wrap(&in2, &hw2, &f2, NULL) == 0, "second fake");
		g_nbuf = 0;
		sbuf = alloc(&hw2, nsrc);
		b2 = alloc(&hw2, 1u << 20);
		(void)hw2.dma_rw(hw2.ctx, sbuf.handle, 0, src, nsrc, 1);
		(void)hw2.dma_rw(hw2.ctx, b2.handle, 0, hdr, nhdr, 1);
		(void)hw2.power(hw2.ctx, RWVC_BLK_VENC, 1);
		program(&hw2, RWVC_WIN_VENC, 320, 240, &sbuf, &b2, 0x2000);
		CHECK(wr(&hw2, RWVC_WIN_VENC, 0x058, 4) == 0, "trigger (headers in the buffer)");
		n2venc_fake_get_stats(f2, &st);
		CHECK(st.headers_learned == 1 && st.last_bytes == bytes && st.unsupported == 0,
		      "learned headers %u bytes %u unsupported %u", st.headers_learned, st.last_bytes, st.unsupported);
		hw2.close(hw2.ctx);
	}

	n2venc_fake_get_stats(f, &st);
	CHECK(st.frames == 4 && st.overflow == 1 && st.irqs_raised == 4 && st.irqs_taken == 3 && st.resets == 2 &&
	      st.dma_errors == 0 && st.unsupported == 0 && st.polls == 2 && st.poll_fail == 1,
	      "stats frames %u overflow %u irqs %u/%u resets %u dma %u unsup %u polls %u/%u", st.frames, st.overflow,
	      st.irqs_taken, st.irqs_raised, st.resets, st.dma_errors, st.unsupported, st.polls, st.poll_fail);
	hw.close(hw.ctx);
end:
	free(src);
	free(hdr);
	printf("N2TEST fakevenc-unit %s %u/%u\n", pass == total ? "PASS" : "FAIL", pass, total);
	return pass == total ? 0 : 1;
}
