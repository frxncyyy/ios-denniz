/* SPDX-License-Identifier: MIT */
/*
 * t_vld.c -- DEC03 host test of the VLD/AVC_VLD/HEVC_VLD model (n2vld.c).
 *
 * Expected values are literals worked out from the H.264/H.265 specifications and from the
 * vendor's own register use (REGISTERS.md), never from the model's own code.  The random
 * differential test at the end compares the model against a deliberately separate, simple
 * reference reader written in this file; the vector tests before it use hand-derived literals.
 *
 * The final test drives the model through a wrapper of T02's fake n2vc_hw with the exact hook
 * signature DEC03 proposed to hw-t02, so the integration shape is exercised before the fake has
 * the setter.
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2vc_hw.h"
#include "n2vc_hw_fake.h"
#include "n2vld.h"

static int checks, fails;

#define CK(c) do { \
	checks++; \
	if (!(c)) { \
		fails++; \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
	} \
} while (0)

#define CKU(a, b) do { \
	uint32_t ck_a = (uint32_t)(a), ck_b = (uint32_t)(b); \
	checks++; \
	if (ck_a != ck_b) { \
		fails++; \
		fprintf(stderr, "FAIL %s:%d: %s = 0x%08x, want 0x%08x\n", __FILE__, __LINE__, #a, ck_a, ck_b); \
	} \
} while (0)

static uint32_t avc_rd(struct n2vld *v, uint32_t reg)
{
	uint32_t val = 0xdeadbeefu;
	int rc = n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + reg * 4u, 0, &val);

	return rc == 1 ? val : 0xdeadbeefu;
}

static int avc_wr(struct n2vld *v, uint32_t reg, uint32_t val)
{
	uint32_t x = val;

	return n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + reg * 4u, 1, &x);
}

/* The vendor's u(n): peek 32, consume n, return the top n bits. */
static uint32_t avc_get(struct n2vld *v, unsigned n)
{
	uint32_t peek = avc_rd(v, 0);

	if (n == 0)
		return 0;
	(void)avc_rd(v, n);
	return n >= 32u ? peek : (peek >> (32u - n));
}

static uint32_t hevc_rd(struct n2vld *v, uint32_t reg)
{
	uint32_t val = 0xdeadbeefu;
	int rc = n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_HEVC_VLD_OFF + reg * 4u, 0, &val);

	return rc == 1 ? val : 0xdeadbeefu;
}

static uint32_t hevc_get(struct n2vld *v, unsigned n)
{
	uint32_t peek = hevc_rd(v, 0);

	if (n == 0)
		return 0;
	(void)hevc_rd(v, n);
	return n >= 32u ? peek : (peek >> (32u - n));
}

static uint32_t vld_rd(struct n2vld *v, uint32_t reg)
{
	uint32_t val = 0xdeadbeefu;
	int rc = n2vld_hook(v, N2VLD_WIN_VDEC_VLD, reg * 4u, 0, &val);

	return rc == 1 ? val : 0xdeadbeefu;
}

static int vld_wr(struct n2vld *v, uint32_t reg, uint32_t val)
{
	uint32_t x = val;

	return n2vld_hook(v, N2VLD_WIN_VDEC_VLD, reg * 4u, 1, &x);
}

static uint32_t lat_rd(struct n2vld *v, uint32_t off)
{
	uint32_t val = 0xdeadbeefu;
	int rc = n2vld_hook(v, N2VLD_WIN_VDEC_LAT_VLD, off, 0, &val);

	return rc == 1 ? val : 0xdeadbeefu;
}

static void test_init(void)
{
	struct n2vld *v = n2vld_create(NULL);

	CK(v != NULL);
	CK(v && n2vld_rbsp_len(v) == 0);
	CK(v && n2vld_bitpos(v) == 0);
	CK(v && n2vld_last_error(v)[0] == '\0');
	CK(v && n2vld_map_window(v, 4, 0x16020000u, 0x1000u) == 0);
	CK(v && n2vld_map_window(v, 4, 0x16020000u, 0x800u) == 0);
	CK(v && n2vld_map_window(v, 5, 0x16020000u, 0) == -EINVAL);
	CK(v && n2vld_map_window(v, 5, 0x16020000u, 0x1000u) == 0);
	CK(v && n2vld_map_window(v, 6, 0x16020000u, 0x1000u) == -ENOSPC);
	n2vld_destroy(v);
	n2vld_destroy(NULL);
}

static void test_peek_advance(void)
{
	static const uint8_t d[] = { 0x12, 0x34, 0x56, 0x78, 0x9a };
	struct n2vld *v = n2vld_create(NULL);

	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CKU(n2vld_rbsp_len(v), 5);
	CKU(avc_rd(v, 0), 0x12345678u);       /* peek does not advance */
	CKU(n2vld_bitpos(v), 0);
	CKU(avc_rd(v, 0), 0x12345678u);
	CKU(avc_rd(v, 8), 0x3456789au);       /* shift 8, return the next 32 bits */
	CKU(n2vld_bitpos(v), 8);
	CKU(avc_rd(v, 0), 0x3456789au);
	CKU(avc_rd(v, 16), 0x789a0000u);      /* shift 16 more, next window */
	CKU(n2vld_bitpos(v), 24);
	CKU(avc_rd(v, 0), 0x789a0000u);
	CKU(avc_rd(v, 32), 0u);               /* shift 32 from bit 24: past the end */
	CKU(n2vld_bitpos(v), 56);

	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CKU(avc_rd(v, 1), 0x2468acf1u);       /* first bit is 0, then the next 32 bits */
	CKU(n2vld_bitpos(v), 1);
	CKU(avc_get(v, 4), 0x2u);             /* bits 1..4: 0010 */
	CKU(avc_get(v, 12), 0x468u);          /* bits 5..16: 010001101000 */
	CKU(n2vld_bitpos(v), 17);

	/* out-of-range / not-a-bit-command reads are left to the register array */
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + 0x8b * 4u, 0, &(uint32_t){0}) == 0);
	CK(n2vld_hook(v, 3, 0, 0, &(uint32_t){0}) == 0);
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_VLD, 2, 0, &(uint32_t){0}) == 0);
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_VLD, 0x4000, 0, &(uint32_t){0}) == 0);
	n2vld_destroy(v);
}

static void test_ue_literals(void)
{
	/* Concatenated ue(v) codes for 0,1,2,3,4,5,6,7,8,9,15,16,31,32,63,64,100,255,256,1000,
	 * written from the Exp-Golomb rule of H.264 9.1 and zero padded to a byte. */
	static const uint8_t u[] = {
		0xa6, 0x42, 0x98, 0xe2, 0x04, 0x8a, 0x08, 0x04, 0x41, 0x00, 0x21, 0x02,
		0x00, 0x10, 0x40, 0xca, 0x01, 0x00, 0x00, 0x80, 0x80, 0x3e, 0x90
	};
	static const uint32_t want[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 31, 32, 63, 64, 100, 255, 256, 1000
	};
	/* se(v) for 0,1,-1,2,-2,3,-3,127,-128,1000,-1000: the same rule on the mapped codeNum. */
	static const uint8_t s[] = {
		0xa6, 0x42, 0x98, 0xe0, 0x3f, 0x80, 0x20, 0x20, 0x07, 0xd0, 0x00, 0x3e, 0x88
	};
	static const int32_t wants[] = { 0, 1, -1, 2, -2, 3, -3, 127, -128, 1000, -1000 };
	struct n2vld *v = n2vld_create(NULL);
	unsigned i;

	CK(v && n2vld_set_bytes(v, u, sizeof u) == 0);
	for (i = 0; i < sizeof want / sizeof want[0]; i++)
		CKU(avc_rd(v, N2VLD_AVC_UE), want[i]);
	CKU(n2vld_bitpos(v), 180);

	CK(v && n2vld_set_bytes(v, s, sizeof s) == 0);
	for (i = 0; i < sizeof wants / sizeof wants[0]; i++)
		CKU(avc_rd(v, N2VLD_AVC_SE), (uint32_t)wants[i]);
	CKU(n2vld_bitpos(v), 101);
	n2vld_destroy(v);
}

static void test_epb(void)
{
	/* 00 00 03 00..03 are emulation-prevention bytes and must vanish; 00 00 03 04 must stay. */
	static const uint8_t raw[] = {
		0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x03, 0x02,
		0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x03, 0x04, 0x80
	};
	static const uint8_t want[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02,
		0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x04, 0x80
	};
	static const uint8_t bits[] = { 0x00, 0x00, 0x03, 0x00, 0x01, 0xff };
	struct n2vld *v = n2vld_create(NULL);
	const uint8_t *p;
	uint32_t n;

	CK(v && n2vld_set_bytes(v, raw, sizeof raw) == 0);
	p = n2vld_rbsp(v, &n);
	CKU(n, sizeof want);
	CK(p && memcmp(p, want, sizeof want) == 0);

	/* the bit reader itself skips the EPB: 00 00 03 00 01 ff -> payload 00 00 00 01 ff */
	CK(v && n2vld_set_bytes(v, bits, sizeof bits) == 0);
	CKU(avc_rd(v, 0), 0x00000001u);
	CKU(avc_rd(v, 32), 0xff000000u);      /* skip 00 00 03 00, then consume the next 32 */
	CKU(n2vld_bitpos(v), 40);            /* raw position: the EPB byte counts */

	/* 00 00 03 at the very end of a NAL, before the next start code, is a real EPB. */
	{
		static const uint8_t tail[] = { 0xaa, 0x00, 0x00, 0x03, 0x00, 0x00, 0x01, 0x68 };
		CK(v && n2vld_set_bytes(v, tail, sizeof tail) == 0);
		CKU(n2vld_rbsp_len(v), sizeof tail - 1u);
	}
	n2vld_destroy(v);
}

static void test_startcode_and_sps(void)
{
	/* The first SPS NAL of an ffmpeg/libx264 320x240 Main profile stream (ffprobe: 320x240,
	 * profile Main, level 3.1).  Kept as the raw Annex-B bytes, emulation prevention included. */
	static const uint8_t nal[] = {
		0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x0d, 0xda, 0x05, 0x07, 0xec, 0x04,
		0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0c, 0x83, 0xc5, 0x0a,
		0xa8, 0x00
	};
	struct n2vld *v = n2vld_create(NULL);

	CK(v && n2vld_set_bytes(v, nal, sizeof nal) == 0);
	/* vdec_hal_h264_shift_startcode: peek the 32-bit window, check 0x000001xx, consume 32. */
	CKU(avc_rd(v, 0), 0x00000167u);
	CKU(avc_rd(v, 32), 0x4d400ddau);      /* post-shift window */
	CKU(n2vld_bitpos(v), 32);
	/* Now the SPS payload: profile_idc, constraint flags, level_idc, then ue(v) fields.
	 * Values derived from H.264 7.3.2.1 and the ffprobe result; REGISTERS.md section 4. */
	CKU(avc_get(v, 8), 77);               /* profile_idc = Main */
	CKU(avc_get(v, 8), 64);               /* constraint flags */
	CKU(avc_get(v, 8), 13);               /* level_idc = 3.1 */
	CKU(avc_rd(v, N2VLD_AVC_UE), 0);      /* seq_parameter_set_id */
	CKU(avc_rd(v, N2VLD_AVC_UE), 0);      /* log2_max_frame_num_minus4 */
	CKU(avc_rd(v, N2VLD_AVC_UE), 2);      /* pic_order_cnt_type (2: no poc_lsb field) */
	CKU(avc_rd(v, N2VLD_AVC_UE), 1);      /* max_num_ref_frames */
	CKU(avc_get(v, 1), 0);                /* gaps_in_frame_num_value_allowed_flag */
	CKU(avc_rd(v, N2VLD_AVC_UE), 19);     /* pic_width_in_mbs_minus1  -> (19+1)*16 = 320 */
	CKU(avc_rd(v, N2VLD_AVC_UE), 14);     /* pic_height_in_map_units_minus1 -> (14+1)*16 = 240 */
	CKU(avc_get(v, 1), 1);                /* frame_mbs_only_flag */
	n2vld_destroy(v);
}

static void test_fetch_and_vfifo(void)
{
	static const uint8_t d[64] = {
		0x00, 0x00, 0x01, 0x67, 0xaa, 0xbb, 0xcc, 0xdd,
		0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
	};
	struct n2vld *v = n2vld_create(NULL);
	uint32_t x;

	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CKU(vld_rd(v, N2VLD_VLD_FETCH_OK), 0);         /* no fetch started yet */
	CK(vld_wr(v, N2VLD_VLD_FETCH_CTRL, 1u << 20) == 1);
	CKU(vld_rd(v, N2VLD_VLD_FETCH_OK), 1);
	CKU(n2vld_bitpos(v), 0);
	CK(vld_wr(v, N2VLD_VLD_FETCH_CTRL, 1u << 23) == 1);
	CKU(vld_rd(v, N2VLD_VLD_FETCH_OK), 1);         /* the load keeps the ready bit */
	CKU(vld_rd(v, N2VLD_VLD_FETCH_CTRL), 1u << 23);

	/* A vfifo programmed without a bus records the window. */
	CK(n2vld_set_vfifo(v, 0x1000u, 0x1040u) == 0);
	CKU(n2vld_vfifo_start(v), 0x1000u);
	CKU(n2vld_vfifo_end(v), 0x1040u);
	CKU(vld_rd(v, N2VLD_VLD_VFIFO_START), 0x40u);
	CKU(vld_rd(v, N2VLD_VLD_VFIFO_END), 0x41u);
	CK(n2vld_set_vfifo(v, 0x1001u, 0x1040u) == -EINVAL);

	/* VLD 0x2c is the read pointer byte PA; 0x42 bit 8 latches it. */
	CK(vld_wr(v, N2VLD_VLD_RPTR, 0x1004u) == 1);
	CK(vld_wr(v, N2VLD_VLD_RESET, 0x100u) == 1);
	CKU(vld_rd(v, N2VLD_VLD_FETCH_OK), 1);   /* a latched reader is fetch-ready */
	CKU(vld_rd(v, N2VLD_VLD_RPTR), 0x1004u);
	CKU(avc_rd(v, 0), 0xaabbccddu);

	/* ... and 0x23 bit 23 latches it too (vdec_hal_init_fetch_done). */
	CK(vld_wr(v, N2VLD_VLD_RPTR, 0x1008u) == 1);
	CK(vld_wr(v, N2VLD_VLD_FETCH_CTRL, 0x800000u) == 1);
	CKU(avc_rd(v, 0), 0x11223344u);

	/* Readback words the vendor's rptr arithmetic uses. */
	CK(vld_wr(v, N2VLD_VLD_SRAM, 0xffffffffu) == 1);
	CKU(vld_rd(v, N2VLD_VLD_SRAM), 0xfcff0000u);
	CK(vld_wr(v, N2VLD_VLD_SRAM2, 0xffffffffu) == 1);
	CKU(vld_rd(v, N2VLD_VLD_SRAM2), 0xff00ffffu);
	CKU(vld_rd(v, N2VLD_VLD_PROC), 0);
	x = vld_rd(v, N2VLD_VLD_DRAMPTR);
	CKU(x, 0x1000u + ((((uint32_t)(n2vld_bitpos(v) >> 5) * 4u) + 0x1cu) % 0x40u));
	n2vld_destroy(v);
}

struct dma_buf {
	uint32_t pa;
	uint8_t *host;
	uint32_t bytes;
};

static int dma_read_pa(void *ctx, uint32_t pa, void *dst, uint32_t bytes)
{
	struct dma_buf *b = ctx;
	uint32_t off;

	if (pa < b->pa)
		return -ENOENT;
	off = pa - b->pa;
	if (off > b->bytes || bytes > b->bytes - off)
		return -ENOENT;
	memcpy(dst, b->host + off, bytes);
	return 0;
}

static void test_bus_vfifo(void)
{
	static uint8_t mem[0x400];
	static const uint8_t stream[] = {
		0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x0d, 0xda, 0x05, 0x07, 0xec,
		0x00, 0x00, 0x03, 0x00, 0x40, 0x00
	};
	struct dma_buf b = { 0xda801000u, mem, sizeof mem };
	struct n2vld_bus bus = { dma_read_pa, &b };
	struct n2vld *v = n2vld_create(&bus);

	memset(mem, 0, sizeof mem);
	memcpy(mem, stream, sizeof stream);
	CK(v != NULL);
	CK(n2vld_set_vfifo(v, b.pa, b.pa + 64u) == 0);
	CKU(n2vld_vfifo_start(v), b.pa);
	CKU(n2vld_vfifo_end(v), b.pa + 64u);
	CKU(avc_rd(v, 0), 0x00000167u);
	CKU(avc_rd(v, 32), 0x4d400ddau);        /* vdec_hal_h264_shift_startcode consumes 32 */
	CKU(avc_get(v, 8), 0x4du);
	CKU(avc_get(v, 8), 0x40u);
	CKU(avc_get(v, 8), 0x0du);
	CKU(avc_rd(v, N2VLD_AVC_UE), 0);
	CKU(avc_rd(v, N2VLD_AVC_UE), 0);
	CKU(avc_rd(v, N2VLD_AVC_UE), 2);

	/* a window that is not inside the buffer fails the fetch, and every read is zero */
	CK(n2vld_set_vfifo(v, 0xda810000u, 0xda810040u) == 0);
	CKU(avc_rd(v, 0), 0);
	{
		struct n2vld_stats st;
		n2vld_stats(v, &st);
		CK(st.bus_errors >= 1);
	}
	n2vld_destroy(v);
}

static void test_ring_wrap_and_rptr(void)
{
	static uint8_t mem[0x400];
	struct dma_buf b = { 0xda802000u, mem, sizeof mem };
	struct n2vld_bus bus = { dma_read_pa, &b };
	struct n2vld *v = n2vld_create(&bus);
	uint32_t s, e, bits, r8, drp, r34, d, rp, size;

	memset(mem, 0, sizeof mem);
	mem[0x3fe] = 0x12;
	mem[0x3ff] = 0x34;
	mem[0x000] = 0x56;
	mem[0x001] = 0x78;
	CK(v && n2vld_set_vfifo(v, b.pa, b.pa + 0x400u) == 0);
	CK(vld_wr(v, N2VLD_VLD_RPTR, b.pa + 0x3feu) == 1);
	CK(vld_wr(v, N2VLD_VLD_RESET, 0x100u) == 1);
	CKU(avc_rd(v, 0), 0x12345678u);          /* wraps at the ring end */
	CKU(avc_rd(v, 24), 0x78000000u);         /* 24 bits: 12 34 56, next window starts at 0x78 */
	CKU(n2vld_rptr_unit(v, N2VLD_UNIT_CORE), b.pa + 0x001u);

	/* the vendor's vdec_hal_read_vld_rptr arithmetic, transcribed from util 0x2a7a8 */
	s = vld_rd(v, N2VLD_VLD_VFIFO_START);
	e = vld_rd(v, N2VLD_VLD_VFIFO_END);
	bits = (avc_rd(v, N2VLD_AVC_CTRL) >> 16) & 0x3fu;
	r8 = vld_rd(v, N2VLD_VLD_SRAM);
	drp = vld_rd(v, N2VLD_VLD_DRAMPTR);
	r34 = vld_rd(v, N2VLD_VLD_SRAM2);
	{
		uint32_t wi = (r8 >> 8) & 0xffu, ri = r8 & 0xffu;

		d = ri < wi ? ri - wi : ri - wi - ((r34 >> 16) & 0xffu);
	}
	size = (e << 6) - (s << 6);
	rp = drp + 4u * ((r8 >> 24) & 3u) + 16u * d - 0x1cu + bits / 8u;
	if (rp < (s << 6))
		rp += size;
	if (rp > (e << 6))
		rp -= size;
	CKU(rp, b.pa + 0x001u);
	n2vld_destroy(v);
}

static void test_avc_reset_keeps_position(void)
{
	static const uint8_t d[64] = {
		0x00, 0x00, 0x01, 0x67, 0xaa, 0xbb, 0xcc, 0xdd,
		0x11, 0x22, 0x33, 0x44
	};
	struct n2vld *v = n2vld_create(NULL);
	uint32_t val;

	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CK(n2vld_set_vfifo_unit(v, N2VLD_UNIT_LAT, 0x2000u, 0x2040u) == 0);
	val = 0x2004u;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_LAT_VLD, N2VLD_VLD_RPTR * 4u, 1, &val) == 1);
	val = 0x101u;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_LAT_VLD, N2VLD_VLD_RESET * 4u, 1, &val) == 1);
	val = 0xdeadbeefu;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LAT_AVC_VLD_OFF + 0, 0, &val) == 1);
	CKU(val, 0xaabbccddu);                   /* LAT starts at rptr + 4 */
	val = 1;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LAT_AVC_VLD_OFF + N2VLD_AVC_RESET * 4u, 1, &val) == 1);
	val = 0;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LAT_AVC_VLD_OFF + N2VLD_AVC_RESET * 4u, 1, &val) == 1);
	val = 0xdeadbeefu;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LAT_AVC_VLD_OFF + 0, 0, &val) == 1);
	CKU(val, 0xaabbccddu);                   /* 0xa3 = 0 does not rewind */
	CKU(n2vld_bitpos_unit(v, N2VLD_UNIT_LAT), 32);
	n2vld_destroy(v);
}

static void test_pointer_status(void)
{
	static uint8_t d[400];
	struct n2vld *v = n2vld_create(NULL);
	uint32_t val;

	memset(d, 0x55, sizeof d);
	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CK(v && n2vld_set_vfifo(v, 0x1000u, 0x1040u) == 0);
	CK(avc_wr(v, 0x280, 0) == 0);                 /* writes to unmodelled registers pass through */
	CK(vld_wr(v, N2VLD_VLD_RPTR, 0x10000000u) == 1);

	/* consume 200 bits */
	CKU(avc_get(v, 32), 0x55555555u);
	CKU(avc_get(v, 32), 0x55555555u);
	CKU(avc_get(v, 32), 0x55555555u);
	CKU(avc_get(v, 32), 0x55555555u);
	CKU(avc_get(v, 32), 0x55555555u);
	CKU(avc_get(v, 8), 0x55u);
	CKU(n2vld_bitpos(v), 168);
	CKU(avc_get(v, 32), 0x55555555u);
	CKU(n2vld_bitpos(v), 200);

	CKU((avc_rd(v, N2VLD_AVC_CTRL) >> 16) & 0x3fu, 200u & 31u);
	CKU(vld_rd(v, N2VLD_VLD_SRAM2), 0);
	CKU(vld_rd(v, N2VLD_VLD_DRAMPTR),
	    0x1000u + (((uint32_t)(n2vld_bitpos(v) >> 5) * 4u + 0x1cu) % 0x40u));
	CKU(vld_rd(v, N2VLD_VLD_PROC) & 0x8001u, 0);

	/* LAT_VLD +0xe8 is the LAT unit's fetch-ok word, not the core one. */
	CKU(lat_rd(v, N2VLD_VLD_FETCH_OK * 4u), 0);
	val = 0x2000u;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_LAT_VLD, N2VLD_VLD_RPTR * 4u, 1, &val) == 1);
	val = 0x101u;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_LAT_VLD, N2VLD_VLD_RESET * 4u, 1, &val) == 1);
	CKU(lat_rd(v, N2VLD_VLD_FETCH_OK * 4u), 1);   /* the LAT trace has no 0x8c write */
	val = 1u << 20;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_LAT_VLD, N2VLD_VLD_FETCH_CTRL * 4u, 1, &val) == 1);
	CKU(lat_rd(v, N2VLD_VLD_FETCH_OK * 4u), 1);
	{
		uint32_t w = 0;
		CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LAT_VLD_OFF + N2VLD_VLD_FETCH_OK * 4u, 0, &w) == 1);
		CKU(w, 1);
	}
	CKU(n2vld_bitpos_unit(v, N2VLD_UNIT_LAT), 0);
	CKU(n2vld_bitpos_unit(v, N2VLD_UNIT_CORE), 200);
	n2vld_destroy(v);
}

static void test_eof(void)
{
	struct n2vld *v = n2vld_create(NULL);

	CK(v && n2vld_set_bytes(v, NULL, 0) == 0);
	CKU(n2vld_rbsp_len(v), 0);
	CKU(avc_rd(v, 0), 0);
	CKU(avc_rd(v, 1), 0);
	CKU(n2vld_bitpos(v), 1);
	CKU(avc_rd(v, N2VLD_AVC_UE), N2VLD_ERR);
	CKU(avc_rd(v, N2VLD_AVC_SE), N2VLD_ERR);
	CK(v && n2vld_set_bytes(v, NULL, 1) == -EINVAL);
	n2vld_destroy(v);
}

static void test_startcode_search(void)
{
	static const uint8_t sc[] = {
		0x00, 0x00, 0x01, 0x67, 0xaa, 0xbb, 0x00, 0x00, 0x01, 0x68, 0xcc, 0x80
	};
	struct n2vld *v = n2vld_create(NULL);
	uint32_t w;

	CK(v && n2vld_set_bytes(v, sc, sizeof sc) == 0);
	CKU(avc_rd(v, 0), 0x00000167u);
	CKU(avc_rd(v, 32), 0xaabb0000u);      /* post-shift window after the NAL header */
	CKU(avc_rd(v, N2VLD_AVC_SC_SEARCH) & 1u, 0); /* not busy before a search */
	CK(avc_wr(v, N2VLD_AVC_SC_SEARCH, 1) == 1);
	CKU(avc_rd(v, N2VLD_AVC_SC_SEARCH) & 1u, 0);
	w = avc_rd(v, 0);
	CKU(w, 0x00000168u);
	CK((avc_rd(v, N2VLD_AVC_SC_STAT) & 0x800u) == 0);
	CKU(avc_get(v, 32), 0x00000168u);
	CKU(avc_get(v, 8), 0xccu);             /* GetStartCode shifts 32: start code + NAL header */
	CKU(avc_get(v, 8), 0x80u);

	/* the decode trigger also moves the reader to the next start code (fake still owns IRQ) */
	CK(v && n2vld_set_bytes(v, sc, sizeof sc) == 0);
	CK(avc_wr(v, N2VLD_AVC_TRIG, 1) == 0);
	CKU(avc_rd(v, 0), 0x00000168u);
	n2vld_destroy(v);
}

static void test_misc_and_larb(void)
{
	struct n2vld *v = n2vld_create(NULL);
	uint32_t val;
	unsigned off;

	for (off = 0x110u; off <= 0x13cu; off += 4u) {
		val = 0xdeadbeefu;
		CK(n2vld_hook(v, N2VLD_WIN_VDEC_MISC, off, 0, &val) == 1);
		CKU(val, 0);
		val = 0xdeadbeefu;
		CK(n2vld_hook(v, N2VLD_WIN_VDEC_LAT_MISC, off, 0, &val) == 1);
		CKU(val, 0);
	}
	val = 0;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_MISC, 0x200u, 0, &val) == 0);

	/* larb4 / larb5 sleep-protect: write bit 0 -> read bit 16 */
	val = 1;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LARB4_OFF + 0x00cu, 1, &val) == 1);
	val = 0;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LARB4_OFF + 0x00cu, 0, &val) == 1);
	CKU(val & 0x10000u, 0x10000u);
	val = 0;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LARB4_OFF + 0x00cu, 1, &val) == 1);
	val = 0xdeadbeefu;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LARB4_OFF + 0x00cu, 0, &val) == 1);
	CKU(val & 0x10000u, 0);
	val = 1;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LARB5_OFF + 0x00cu, 1, &val) == 1);
	val = 0;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LARB5_OFF + 0x00cu, 0, &val) == 1);
	CKU(val & 0x10000u, 0x10000u);
	n2vld_destroy(v);
}

static void test_lat_unit(void)
{
	static const uint8_t d[] = { 0x12, 0x34, 0x56, 0x78, 0x9a };
	struct n2vld *v = n2vld_create(NULL);
	uint32_t val;

	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CK(n2vld_set_vfifo_unit(v, N2VLD_UNIT_LAT, 0x2000u, 0x2040u) == 0);
	CKU(n2vld_vfifo_start_unit(v, N2VLD_UNIT_LAT), 0x2000u);
	CKU(n2vld_vfifo_end_unit(v, N2VLD_UNIT_LAT), 0x2040u);
	CKU(avc_get(v, 8), 0x12u);               /* core position 8 */
	val = 0xdeadbeefu;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LAT_AVC_VLD_OFF + 0, 0, &val) == 1);
	CKU(val, 0x12345678u);                   /* LAT still at 0 */
	val = 0xdeadbeefu;
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_ALL, N2VLD_LAT_AVC_VLD_OFF + 8u * 4u, 0, &val) == 1);
	CKU(val, 0x3456789au);                   /* LAT shifts its own position */
	CKU(n2vld_bitpos_unit(v, N2VLD_UNIT_LAT), 8);
	CKU(n2vld_bitpos_unit(v, N2VLD_UNIT_CORE), 8);
	CKU(avc_get(v, 8), 0x34u);               /* core from 8 */
	CKU(n2vld_bitpos_unit(v, N2VLD_UNIT_LAT), 8);
	CKU(n2vld_bitpos_unit(v, N2VLD_UNIT_CORE), 16);
	n2vld_destroy(v);
}

static void test_hevc_literals(void)
{
	static const uint8_t u[] = {
		0xa6, 0x42, 0x98, 0xe2, 0x04, 0x8a, 0x08, 0x04, 0x41, 0x00, 0x21, 0x02,
		0x00, 0x10, 0x40, 0xca, 0x01, 0x00, 0x00, 0x80, 0x80, 0x3e, 0x90
	};
	static const uint32_t want[] = {
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 31, 32, 63, 64, 100, 255, 256, 1000
	};
	static const uint8_t d[] = { 0x12, 0x34, 0x56, 0x78, 0x9a };
	struct n2vld *v = n2vld_create(NULL);
	unsigned i;

	CK(v && n2vld_set_bytes(v, u, sizeof u) == 0);
	for (i = 0; i < sizeof want / sizeof want[0]; i++)
		CKU(hevc_rd(v, N2VLD_HEVC_UE), want[i]);
	CKU(n2vld_bitpos(v), 180);

	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CKU(hevc_rd(v, 0), 0x12345678u);
	CKU(hevc_rd(v, 8), 0x3456789au);
	CKU(hevc_rd(v, 0), 0x3456789au);
	CKU(hevc_rd(v, 0x20), 0u);
	CKU(n2vld_bitpos(v), 40);
	CKU((hevc_rd(v, N2VLD_AVC_CTRL) >> 8) & 7u, 40u & 7u);
	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	CKU(hevc_get(v, 3), 0x0u);
	CKU((hevc_rd(v, N2VLD_AVC_CTRL) >> 8) & 7u, 3u);
	CK(n2vld_hook(v, N2VLD_WIN_VDEC_MISC, 0x3000u + N2VLD_HEVC_UE * 4u, 0, &(uint32_t){0}) == 1);
	n2vld_destroy(v);
}

/* ---- a separate, simple reference reader over the model's RBSP output ------------------------- */

struct ref {
	const uint8_t *b;
	uint32_t bytes;
	uint64_t pos;
};

static unsigned ref_bit(struct ref *r)
{
	unsigned bit = 0;

	if (r->pos / 8u < r->bytes)
		bit = (r->b[r->pos / 8u] >> (7u - (unsigned)(r->pos % 8u))) & 1u;
	r->pos++;
	return bit;
}

static uint32_t ref_peek32(const struct ref *r)
{
	uint32_t x = 0;
	uint64_t p = r->pos;
	unsigned i;

	for (i = 0; i < 32u; i++) {
		unsigned bit = 0;

		if (p / 8u < r->bytes)
			bit = (r->b[p / 8u] >> (7u - (unsigned)(p % 8u))) & 1u;
		p++;
		x = (x << 1) | bit;
	}
	return x;
}

static uint32_t ref_ue(struct ref *r)
{
	unsigned z = 0, i;
	uint32_t x = 0;

	while (z < 32u && ref_bit(r) == 0)
		z++;
	if (z == 32u)
		return N2VLD_ERR;
	for (i = 0; i < z; i++)
		x = (x << 1) | ref_bit(r);
	return ((1u << z) - 1u) + x;
}

static uint32_t ref_se(struct ref *r)
{
	uint32_t k = ref_ue(r);
	int32_t x;

	if (k == N2VLD_ERR)
		return N2VLD_ERR;
	x = (k & 1u) ? (int32_t)((k + 1u) >> 1) : -(int32_t)(k >> 1);
	return (uint32_t)x;
}

static uint32_t lcg(uint32_t *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

static void test_differential(void)
{
	enum { NBYTES = 512, NOPS = 600 };
	static uint8_t d[NBYTES];
	struct n2vld *v = n2vld_create(NULL);
	uint32_t seed = 0x20261111u;
	unsigned i;

	for (i = 0; i < NBYTES; i++)
		d[i] = (uint8_t)(lcg(&seed) >> 24);
	CK(v && n2vld_set_bytes(v, d, sizeof d) == 0);
	{
		struct ref r;
		const uint8_t *p = n2vld_rbsp(v, &r.bytes);

		r.b = p;
		r.pos = 0;
		seed = 0xdec03decu;
		for (i = 0; i < NOPS; i++) {
			uint32_t op = lcg(&seed) % 4u;
			uint32_t got = 0, want = 0;

			if (op == 0) {
				got = avc_rd(v, N2VLD_AVC_UE);
				want = ref_ue(&r);
			} else if (op == 1) {
				got = avc_rd(v, N2VLD_AVC_SE);
				want = ref_se(&r);
			} else if (op == 2) {
				unsigned n = 1u + (lcg(&seed) % 32u);

				r.pos += n;
				want = ref_peek32(&r);
				got = avc_rd(v, n);
			} else {
				got = avc_rd(v, 0);
				want = ref_peek32(&r);
			}
			if (got != want) {
				checks++;
				fails++;
				fprintf(stderr, "FAIL differential op %u: got 0x%08x want 0x%08x\n", i, got, want);
				break;
			}
			checks++;
			if (n2vld_bitpos(v) != r.pos) {
				checks++;
				fails++;
				fprintf(stderr, "FAIL differential pos %llu vs %llu\n",
					(unsigned long long)n2vld_bitpos(v), (unsigned long long)r.pos);
				break;
			}
		}
	}
	n2vld_destroy(v);
}

/* ---- T02 fake bridge: the hook signature proposed to hw-t02 -------------------------------- */

struct bridge {
	struct n2vc_hw inner;
	struct n2vc_hw outer;
	struct n2vld *vld;
};

static int br_power(void *ctx, unsigned block, int on)
{
	struct bridge *b = ctx;

	return b->inner.power(b->inner.ctx, block, on);
}

static int br_regs(void *ctx, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	struct bridge *b = ctx;
	unsigned i;

	for (i = 0; i < count; i++) {
		struct rwvc_reg_op *o = &ops[i];
		uint32_t val = o->value;
		int rc;

		if (o->op == RWVC_OP_READ || o->op == RWVC_OP_WRITE) {
			rc = n2vld_hook(b->vld, o->window, o->offset, o->op == RWVC_OP_WRITE, &val);
			if (rc < 0) {
				if (done)
					*done = i;
				return rc;
			}
			if (rc > 0) {
				if (o->op == RWVC_OP_READ)
					o->value = val;
				continue;
			}
		}
		{
			struct rwvc_reg_op one = *o;
			unsigned d1 = 0;
			rc = b->inner.regs(b->inner.ctx, &one, 1, &d1);
			if (rc) {
				if (done)
					*done = i;
				return rc;
			}
			o->value = one.value;
		}
	}
	if (done)
		*done = count;
	return 0;
}

static int br_wait_irq(void *ctx, unsigned irq, unsigned ms, uint32_t *st)
{
	struct bridge *b = ctx;

	return b->inner.wait_irq(b->inner.ctx, irq, ms, st);
}

static int br_irq_clear(void *ctx, unsigned irq)
{
	struct bridge *b = ctx;

	return b->inner.irq_clear(b->inner.ctx, irq);
}

static int br_dma_alloc(void *ctx, uint32_t bytes, uint32_t flags, uint32_t *h, uint32_t *pa)
{
	struct bridge *b = ctx;

	return b->inner.dma_alloc(b->inner.ctx, bytes, flags, h, pa);
}

static int br_dma_free(void *ctx, uint32_t h)
{
	struct bridge *b = ctx;

	return b->inner.dma_free(b->inner.ctx, h);
}

static int br_dma_rw(void *ctx, uint32_t h, uint32_t off, void *host, uint32_t bytes, int write)
{
	struct bridge *b = ctx;

	return b->inner.dma_rw(b->inner.ctx, h, off, host, bytes, write);
}

static void br_close(void *ctx)
{
	struct bridge *b = ctx;

	b->inner.close(b->inner.ctx);
	free(b);
}

static struct n2vc_hw *bridge_new(const struct n2vc_hw *inner, struct n2vld *vld)
{
	struct bridge *b = calloc(1, sizeof *b);

	if (!b)
		return NULL;
	b->inner = *inner;
	b->vld = vld;
	b->outer.ctx = b;
	b->outer.power = br_power;
	b->outer.regs = br_regs;
	b->outer.wait_irq = br_wait_irq;
	b->outer.irq_clear = br_irq_clear;
	b->outer.dma_alloc = br_dma_alloc;
	b->outer.dma_free = br_dma_free;
	b->outer.dma_rw = br_dma_rw;
	b->outer.close = br_close;
	return &b->outer;
}

static void test_through_fake(void)
{
	static const uint8_t nal[] = {
		0x00, 0x00, 0x01, 0x67, 0x4d, 0x40, 0x0d, 0xda, 0x05, 0x07, 0xec, 0x04,
		0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x0c, 0x83, 0xc5, 0x0a,
		0xa8, 0x00
	};
	struct n2vc_hw fake;
	struct dma_buf b;
	struct n2vld_bus bus;
	struct n2vld *vld = NULL;
	struct n2vc_hw *hw = NULL;
	uint8_t *host = NULL;
	uint32_t bytes = 0, handle = 0, pa = 0, p;

	CK(n2vc_hw_open_fake(&fake, NULL) == 0);
	CK(fake.power(fake.ctx, RWVC_BLK_VDEC_CORE, 1) == 0);
	CK(fake.dma_alloc(fake.ctx, 4096, 0, &handle, &pa) == 0);
	CK(n2vc_hw_fake_dma_host(&fake, handle, (void **)&host, &bytes) == 0);
	memset(host, 0, bytes);
	memcpy(host, nal, sizeof nal);
	b.pa = pa;
	b.host = host;
	b.bytes = bytes;
	bus.read_pa = dma_read_pa;
	bus.ctx = &b;
	vld = n2vld_create(&bus);
	CK(vld != NULL);
	hw = bridge_new(&fake, vld);
	CK(hw != NULL);
	if (hw) {
		/* The bridge forwards power/DMA to the same fake instance the pool belongs to. */
		CK(hw->power(hw->ctx, RWVC_BLK_VDEC_CORE, 1) == 0);
		/* program the vfifo exactly as vdec_hal_set_vld_vfifo does (start/end >> 6) */
		CK(n2vc_hw_wr(hw, RWVC_WIN_VDEC_VLD, N2VLD_VLD_VFIFO_START * 4u, pa >> 6) == 0);
		CK(n2vc_hw_wr(hw, RWVC_WIN_VDEC_VLD, N2VLD_VLD_VFIFO_END * 4u, (pa + 64u) >> 6) == 0);
		CK(n2vc_hw_wr(hw, RWVC_WIN_VDEC_VLD, N2VLD_VLD_FETCH_CTRL * 4u, 1u << 20) == 0);
		CKU(n2vc_hw_rd(hw, RWVC_WIN_VDEC_VLD, N2VLD_VLD_FETCH_OK * 4u), 1);
		CKU(n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + 0 * 4u), 0x00000167u);
		CKU(n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + 32 * 4u), 0x4d400ddau);
		p = n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + 8 * 4u);
		CKU((p >> 24) & 0xffu, 0x40u);       /* post-shift window at bit 40 */
		p = n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + 8 * 4u);
		CKU((p >> 24) & 0xffu, 0x0du);       /* level_idc */
		p = n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + 8 * 4u);
		CKU((p >> 24) & 0xffu, 0xdau);       /* first bits after level_idc */
		CKU(n2vld_bitpos(vld), 56);
		CKU(n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + N2VLD_AVC_UE * 4u), 0);
		CKU(n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + N2VLD_AVC_UE * 4u), 0);
		CKU(n2vc_hw_rd(hw, N2VLD_WIN_VDEC_ALL, N2VLD_AVC_VLD_OFF + N2VLD_AVC_UE * 4u), 2);
		hw->close(hw->ctx);
		hw = NULL;
	} else {
		fake.close(fake.ctx);
	}
	n2vld_destroy(vld);
}

int main(void)
{
	test_init();
	test_peek_advance();
	test_ue_literals();
	test_epb();
	test_startcode_and_sps();
	test_fetch_and_vfifo();
	test_bus_vfifo();
	test_ring_wrap_and_rptr();
	test_avc_reset_keeps_position();
	test_pointer_status();
	test_eof();
	test_startcode_search();
	test_misc_and_larb();
	test_lat_unit();
	test_hevc_literals();
	test_differential();
	test_through_fake();

	printf("N2VLD checks %d pass %d\n", checks, checks - fails);
	return fails ? 1 : 0;
}
