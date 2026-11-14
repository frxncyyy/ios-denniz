/* SPDX-License-Identifier: MIT */
/*
 * t_vdec_vld.c -- host test of the VLD reference model.  The "driver" side below repeats the register sequences the
 * vendor library performs (addresses in gpu/hwcodec/facts/VDEC_REGISTERS.md); expected values are hand-encoded
 * Exp-Golomb / H.264 byte strings (worked out from the H.264 specification, clause 9.1 and 7.4.1), not model output.
 */
#include "n2_vdec_vld.h"
#include <stdio.h>
#include <string.h>

#define PA0 0x40000000ull
#define SZ  0x1000u
static uint8_t mem[SZ];
static int checks, fails;

#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
	printf(__VA_ARGS__); printf("\n"); } } while (0)

static int rd(void *ctx, uint64_t pa, uint8_t *dst, uint32_t n)
{
	(void)ctx;
	if (pa < PA0 || pa + n > PA0 + SZ)
		return -1;
	memcpy(dst, mem + (pa - PA0), n);
	return 0;
}

static struct n2vld m;
static uint32_t VLD, AVC, MISC, LARB;

static uint32_t R(uint32_t off)
{
	uint32_t v = 0xdeadbeef;
	CHECK(n2vld_access(&m, off, 0, &v) == 1, "read %#x not claimed", off);
	return v;
}
static void W(uint32_t off, uint32_t v) { n2vld_access(&m, off, 1, &v); }

/* vdec_hal_set_vld_vfifo + vdec_hal_vld_init_barrel_shift + vdec_hal_init_fetch_done (libvcodec_utility.so) */
static void vendor_init(uint64_t rptr, uint32_t end_off)
{
	W(VLD + 0xb4, (uint32_t)(PA0 >> 6));
	W(VLD + 0xb8, (uint32_t)((PA0 + end_off) >> 6));
	CHECK((R(VLD + 0xf4) & 0x8000) == 0, "process flag");
	for (uint32_t r = 0x110; r <= 0x13c; r += 4)
		CHECK(R(MISC + r) == 0, "misc %#x", r);
	W(VLD + 0xb0, (uint32_t)rptr);
	W(VLD + 0x108, 0x100);
	W(VLD + 0x108, 0);
	W(VLD + 0x8c, 0x100000);
	CHECK(R(VLD + 0xe8) & 1, "init fetch ready");
	W(VLD + 0x8c, 0x800000);
}

/* vdec_hal_read_vld_rptr + vdec_hal_h264_vldrptr wrap handling, transcribed arithmetic */
static uint64_t vendor_rptr(void)
{
	uint32_t s = R(VLD + 0xb4), e = R(VLD + 0xb8), bits = (R(AVC + 0x84) >> 16) & 0x3f;
	uint32_t r8 = R(VLD + 0xec), drp = R(VLD + 0xfc), r34 = R(VLD + 0xd0);
	uint32_t wi = (r8 >> 8) & 0xff, ri = r8 & 0xff, d;
	d = ri < wi ? ri - wi : ri - wi - ((r34 >> 16) & 0xff);
	uint32_t rp = drp + 4 * ((r8 >> 24) & 3) + 16 * d - 0x1c + bits / 8;
	if (rp < (s << 6))
		rp += (e - s) << 6;
	uint32_t size = (e << 6) - (s << 6);
	if (rp > (e << 6))
		rp -= size;
	return rp;
}

static uint32_t get_bits(unsigned n) { uint32_t v = R(AVC + 0); R(AVC + 4 * n); return n ? v >> (32 - n) : 0; }

int main(void)
{
	n2vld_init(&m, rd, NULL);
	VLD = N2VLD_CORE_VLD; AVC = N2VLD_CORE_AVC_VLD; MISC = N2VLD_CORE_MISC; LARB = N2VLD_CORE_LARB;

	/* 1. Exp-Golomb: ue 0,1,2,3,7 then se -1,+2 = 1 010 011 00100 0001000 011 00100 + rbsp stop bit 1 + 0000 */
	memset(mem, 0, sizeof mem);
	static const uint8_t eg[] = { 0xa6, 0x41, 0x0c, 0x98 };
	memcpy(mem + 0x100, eg, 4);
	vendor_init(PA0 + 0x100, SZ);
	CHECK(R(AVC + 0) == 0xa6410c98u, "peek %#x", R(AVC + 0));
	CHECK(R(AVC + 0x23c) == 0, "ue0");
	CHECK(R(AVC + 0x23c) == 1, "ue1");
	CHECK(R(AVC + 0x23c) == 2, "ue2");
	CHECK(R(AVC + 0x23c) == 3, "ue3");
	CHECK(R(AVC + 0x23c) == 7, "ue7");
	CHECK((int32_t)R(AVC + 0x240) == -1, "se-1");
	CHECK((int32_t)R(AVC + 0x240) == 2, "se+2");
	CHECK(get_bits(1) == 1, "stop bit");
	CHECK(((R(AVC + 0x84) >> 16) & 7) == 4, "bit-in-byte after 28 bits");
	CHECK(vendor_rptr() == PA0 + 0x103, "rptr %#llx", (unsigned long long)vendor_rptr());

	/* 2. shift-then-return semantics and u(n) */
	vendor_init(PA0 + 0x100, SZ);
	CHECK(R(AVC + 4 * 8) == 0x410c9800u, "shift 8 returns following window");
	CHECK(get_bits(4) == 0x4, "u(4)");
	CHECK(get_bits(12) == 0x10c, "u(12)");

	/* 3. emulation prevention: raw 00 00 03 00 01 ff = payload 00 00 00 01 ff */
	static const uint8_t epb[] = { 0x00, 0x00, 0x03, 0x00, 0x01, 0xff };
	memset(mem, 0, sizeof mem);
	memcpy(mem + 0x200, epb, sizeof epb);
	vendor_init(PA0 + 0x200, SZ);
	CHECK(R(AVC + 0) == 0x00000001u, "epb peek %#x", R(AVC + 0));
	CHECK(R(AVC + 0x80) == 0xff000000u, "after 32 payload bits");
	CHECK(vendor_rptr() == PA0 + 0x205, "rptr after epb %#llx", (unsigned long long)vendor_rptr());

	/* 4. start-code search as H264_HAL_GetStartCode_8530 does it; NAL types 7 (SPS) then 8 (PPS) */
	static const uint8_t sc[] = { 0x00, 0x00, 0x01, 0x67, 0xaa, 0xbb, 0x00, 0x00, 0x01, 0x68, 0xcc, 0x80 };
	memset(mem, 0, sizeof mem);
	memcpy(mem + 0x300, sc, sizeof sc);
	vendor_init(PA0 + 0x300, SZ);
	uint32_t w = R(AVC + 0), c = R(AVC + 0x84);
	CHECK((w & ~0xffu) == 0x100 && !(c >> 31), "aligned start code seen without search");
	R(AVC + 0x80);                       /* shift 32: past 00 00 01 67 */
	W(AVC + 0x2d8, 1);
	CHECK((R(AVC + 0x2d8) & 1) == 0, "search not busy");
	CHECK((R(AVC + 0x2c8) & 0x800) == 0, "stat");
	w = R(AVC + 0);
	R(AVC + 0x80);
	CHECK(w == 0x00000168u && (w & 0x1f) == 8, "next NAL %#x", w);
	CHECK(get_bits(8) == 0xcc, "payload after NAL header");

	/* 5. unaligned start + ring wrap: data straddles the vfifo end */
	memset(mem, 0, sizeof mem);
	mem[0x3fe] = 0x12; mem[0x3ff] = 0x34; mem[0x000] = 0x56; mem[0x001] = 0x78;
	vendor_init(PA0 + 0x3fe, 0x400);
	CHECK(R(AVC + 0) == 0x12345678u, "wrap peek %#x", R(AVC + 0));
	R(AVC + 4 * 24);
	CHECK(vendor_rptr() == PA0 + 0x001, "wrap rptr %#llx", (unsigned long long)vendor_rptr());

	/* 6. larb sleep protect handshake (vdec_hal_wait/release_larb_sleep_protect) */
	W(LARB + 0xc, 1);
	CHECK(R(LARB + 0xc) & 0x10000, "sleep ack");
	W(LARB + 0xc, 0);
	CHECK(!(R(LARB + 0xc) & 0x10000), "sleep released");

	/* 7. LAT unit is independent (core index 3 -> 0x11000/0x12000) */
	VLD = N2VLD_LAT_VLD; AVC = N2VLD_LAT_AVC_VLD; MISC = N2VLD_LAT_MISC; LARB = N2VLD_LAT_LARB;
	memset(mem, 0, sizeof mem);
	memcpy(mem + 0x100, eg, 4);
	vendor_init(PA0 + 0x100, SZ);
	CHECK(R(AVC + 0x23c) == 0 && R(AVC + 0x23c) == 1, "LAT ue");
	CHECK(m.u[N2VLD_CORE].abit != m.u[N2VLD_LAT].abit, "units independent");

	/* 8. decode trigger consumes the slice: reader moves to the next start code */
	VLD = N2VLD_CORE_VLD; AVC = N2VLD_CORE_AVC_VLD; MISC = N2VLD_CORE_MISC;
	memset(mem, 0, sizeof mem);
	memcpy(mem + 0x300, sc, sizeof sc);
	vendor_init(PA0 + 0x300, SZ);
	uint32_t one = 1;
	CHECK(n2vld_access(&m, AVC + 0x238, 1, &one) == 0, "trigger left to the fake");
	CHECK(R(AVC + 0) == 0x00000168u, "after trigger %#x", R(AVC + 0));

	/* 9. not claimed: plain registers stay with the fake */
	uint32_t v;
	CHECK(n2vld_access(&m, N2VLD_CORE_AVC_VLD + 0x280, 0, &v) == 0, "0x280 unclaimed");
	CHECK(n2vld_access(&m, 0x21000, 0, &v) == 0, "MC unclaimed");

	printf("t_vdec_vld: %d/%d\n", checks - fails, checks);
	return fails ? 1 : 0;
}
