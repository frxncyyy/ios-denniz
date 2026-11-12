/* SPDX-License-Identifier: MIT */
/*
 * k20_neon.c -- differential kernels for package V20 (NEON two-register miscellaneous, VEXT, VTBL/VTBX,
 * VDUP, modified immediates, and the element/structure load/store class).
 *
 * The same C is compiled natively (host reference) and for ARMv7a with NEON; the ARM build should emit the
 * instructions this package owns (vld1/vst1 multiple structures of 8/16/32/64-bit elements, vmov/vdup
 * immediates, vtbl/vuzp/vzip/vrev).  The results must be identical.  Pure functions of the seed, exact-width
 * types, no libc, no globals: rules at the top of tests/emu/kernels/kcommon.h.
 *
 * T01 owns tests/emu/kernels; this file lives in the V20 package directory and can be copied there as is.
 */
#include "kcommon.h"

typedef uint8_t v16u8 __attribute__((vector_size(16)));

/* 8-byte block copy with an unaligned destination: vld1.8/vst1.8, one register */
uint32_t k_v20_copy8(uint32_t seed)
{
	uint8_t src[40], dst[48];
	uint32_t h = seed;
	unsigned i, k;

	for (i = 0; i < 40; i++)
		src[i] = (uint8_t)(kmix(h, i * 0x01010101u) >> 24);
	for (i = 0; i < 48; i++)
		dst[i] = 0;
	for (i = 0; i + 8 <= 40; i += 8)
		for (k = 0; k < 8; k++)
			dst[i + 1 + k] = src[i + k];
	for (i = 0; i < 48; i++)
		h = kmix(h, dst[i]);
	return h;
}

/* 16-byte block copy: vld1.8 {d0,d1} / vst1.8 {d0,d1} */
uint32_t k_v20_copy16(uint32_t seed)
{
	uint8_t src[64], dst[64];
	uint32_t h = seed;
	unsigned i, k;

	for (i = 0; i < 64; i++)
		src[i] = (uint8_t)(kmix(h, i ^ 0x5A5A5A5Au) >> 16);
	for (i = 0; i < 64; i += 16)
		for (k = 0; k < 16; k++)
			dst[i + k] = src[i + k];
	for (i = 0; i < 64; i += 4)
		h = kmix(h, (uint32_t)dst[i] | ((uint32_t)dst[i + 1] << 8) |
			    ((uint32_t)dst[i + 2] << 16) | ((uint32_t)dst[i + 3] << 24));
	return h;
}

/* 32-bit element blocks: vld1.32/vst1.32, two to four registers */
uint32_t k_v20_copy32(uint32_t seed)
{
	uint32_t src[16], dst[16];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 16; i++)
		src[i] = kmix(h, i + seed);
	for (i = 0; i < 16; i += 4) {
		dst[i] = src[i] ^ 0xA5A5A5A5u;
		dst[i + 1] = src[i + 1] + 0x01010101u;
		dst[i + 2] = src[i + 2] - 3u;
		dst[i + 3] = src[i + 3] ^ 0x0F0F0F0Fu;
	}
	for (i = 0; i < 16; i++)
		h = kmix(h, dst[i]);
	return h;
}

/* 64-bit element blocks: vld1.64/vst1.64 */
uint32_t k_v20_copy64(uint32_t seed)
{
	uint64_t src[8], dst[8];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 8; i++)
		src[i] = ((uint64_t)kmix(h, i) << 32) | kmix(h, i + 100u);
	for (i = 0; i < 8; i += 2) {
		dst[i] = src[i] ^ 0xFFFFFFFF00000000ull;
		dst[i + 1] = src[i + 1] + 0x0000000100000001ull;
	}
	for (i = 0; i < 8; i++)
		h = kmix64(h, dst[i]);
	return h;
}

/* constant fills: vmov.i32 / vdup */
uint32_t k_v20_fill(uint32_t seed)
{
	uint8_t bytes[32];
	uint32_t words[8];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++)
		bytes[i] = (uint8_t)(seed >> ((i & 3u) * 8u));
	for (i = 0; i < 8; i++)
		words[i] = (seed & 0xFFFF0000u) | i;
	for (i = 0; i < 32; i += 8)
		h = kmix(h, (uint32_t)bytes[i] | ((uint32_t)bytes[i + 3] << 8) |
			    ((uint32_t)bytes[i + 5] << 16) | ((uint32_t)bytes[i + 7] << 24));
	for (i = 0; i < 8; i++)
		h = kmix(h, words[i]);
	return h;
}

/* interleave two 16-byte vectors byte by byte: vzip.8 / vuzp.8 territory */
uint32_t k_v20_zip(uint32_t seed)
{
	v16u8 a, b, r;
	uint8_t out[16];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 16; i++) {
		a[i] = (uint8_t)(kmix(h, i) >> 24);
		b[i] = (uint8_t)(kmix(h, i + 200u) >> 24);
	}
	r = __builtin_shufflevector(a, b, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
	for (i = 0; i < 16; i++)
		out[i] = r[i];
	for (i = 0; i < 16; i++)
		h = kmix(h, out[i]);
	return h;
}

/* reverse the bytes inside each 32-bit group: vrev32.8 */
uint32_t k_v20_rev(uint32_t seed)
{
	v16u8 a, r;
	uint8_t out[16];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 16; i++)
		a[i] = (uint8_t)(kmix(h, i * 7u) >> 24);
	r = __builtin_shufflevector(a, a, 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12);
	for (i = 0; i < 16; i++)
		out[i] = r[i];
	for (i = 0; i < 16; i++)
		h = kmix(h, out[i]);
	return h;
}

/* broadcast a byte and a 32-bit word through vector types: vdup.8/vdup.32, and a constant shuffle of
 * two vectors for the table-lookup shape (VTBL/VTBX territory) */
uint32_t k_v20_dup_shuffle(uint32_t seed)
{
	v16u8 a, b, r;
	uint8_t out[16];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 16; i++)
		a[i] = (uint8_t)(seed >> ((i & 3u) * 8u));
	for (i = 0; i < 16; i++)
		b[i] = (uint8_t)((seed + i) & 0xFFu);
	r = __builtin_shufflevector(a, b, 0, 17, 2, 19, 4, 21, 6, 23, 8, 25, 10, 27, 12, 29, 14, 31);
	for (i = 0; i < 16; i++)
		out[i] = r[i];
	for (i = 0; i < 16; i++)
		h = kmix(h, out[i]);
	return h;
}
