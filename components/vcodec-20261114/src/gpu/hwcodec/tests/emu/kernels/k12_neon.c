/* SPDX-License-Identifier: MIT */
/* Small vector-friendly kernels that isolate individual NEON operation shapes used by the
 * larger k05 loops: variable shifts, unary negate, 16-bit multiply-accumulate, min/max,
 * narrowing shifts, narrowing moves and structure load/store.  At -O3 the ARM compiler
 * emits vshl/vneg/vmla/vmin/vmax/vshrn/vmovn/vld1/vst1 for these; at -O0 they stay scalar. */
#include "kcommon.h"

uint32_t k_neon_vshl(uint32_t seed)
{
	int32_t a[32], sh[32];
	int32_t o[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (int32_t)((seed >> (i & 15)) + i * 3u);
		sh[i] = (int32_t)(i & 31u) - 16;
	}
	for (i = 0; i < 32; i++)
		o[i] = (sh[i] >= 0) ? (int32_t)((uint32_t)a[i] << sh[i])
				    : (int32_t)((uint32_t)a[i] >> -sh[i]);
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint32_t)o[i]);
	return h;
}

uint32_t k_neon_vneg(uint32_t seed)
{
	int32_t a[32], o[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++)
		a[i] = (int32_t)((seed * (i + 1u)) ^ (i * 0x101u));
	for (i = 0; i < 32; i++)
		o[i] = (int32_t)(0u - (uint32_t)a[i]);
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint32_t)o[i]);
	return h;
}

uint32_t k_neon_vmla16(uint32_t seed)
{
	int16_t a[32], b[32], acc[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (int16_t)((seed >> (i & 15)) ^ (i * 37u));
		b[i] = (int16_t)((seed + i * 91u) >> 2);
		acc[i] = (int16_t)(i * 5u);
	}
	for (i = 0; i < 32; i++)
		acc[i] = ksat16((int32_t)acc[i] + (int32_t)a[i] * b[i]);
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint16_t)acc[i]);
	return h;
}

uint32_t k_neon_vminmax(uint32_t seed)
{
	int32_t a[32], b[32], mn[32], mx[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (int32_t)((seed >> (i & 15)) ^ (i * 0x1111u));
		b[i] = (int32_t)((seed + i * 0x101u) << 1);
	}
	for (i = 0; i < 32; i++) {
		mn[i] = a[i] < b[i] ? a[i] : b[i];
		mx[i] = a[i] > b[i] ? a[i] : b[i];
	}
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint32_t)mn[i] ^ (uint32_t)mx[i]);
	return h;
}

uint32_t k_neon_vshrn(uint32_t seed)
{
	uint32_t a[32];
	uint8_t o[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++)
		a[i] = (seed << (i & 15)) ^ (i * 0x01010101u);
	for (i = 0; i < 32; i++)
		o[i] = (uint8_t)(a[i] >> 8);
	for (i = 0; i < 32; i++)
		h = kmix(h, o[i]);
	return h;
}

uint32_t k_neon_vmovn(uint32_t seed)
{
	int32_t a[32];
	int16_t o[16];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++)
		a[i] = (int32_t)((seed >> (i & 15)) ^ (i * 0x10001u));
	for (i = 0; i < 16; i++)
		o[i] = (int16_t)a[i * 2];
	for (i = 0; i < 16; i++)
		h = kmix(h, (uint16_t)o[i]);
	return h;
}

uint32_t k_neon_vldst(uint32_t seed)
{
	uint32_t a[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++)
		a[i] = seed ^ (i * 0x9e3779b9u);
	for (i = 0; i < 32; i++)
		a[i] = a[i] + 1u;
	for (i = 0; i < 32; i++)
		h = kmix(h, a[i]);
	return h;
}
