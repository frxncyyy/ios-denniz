/* SPDX-License-Identifier: MIT */
/*
 * V19 differential kernels for T01's harness (tests/emu/kernels/).  Pure functions of the seed,
 * integer only, well defined for every uint32_t input.  The loops are written so that clang
 * -O3 -ftree-vectorize emits the V19 families on ARMv7 NEON: shifts by immediate (VSHR/VSRA/
 * VRSHR/VQSHL), narrowing (VSHRN/VQSHRN), widening multiply-accumulate (VMLAL/VMLSL) and
 * saturating arithmetic.  Compile with the same flags as the other k*.c files.
 */
#include "kcommon.h"

/* VSHR/VSRA by immediate on int16 lanes. */
uint32_t k19_shift_imm(uint32_t seed)
{
	int16_t a[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64; i++)
		a[i] = (int16_t)(uint16_t)((seed >> (i & 15)) + i * 37u);
	for (i = 0; i < 64; i++)
		a[i] = (int16_t)(a[i] >> 3);
	for (i = 0; i < 64; i++)
		h = kmix(h, (uint16_t)a[i]);
	return h;
}

/* VRSHR pattern: (x + 2^(n-1)) >> n on unsigned 16-bit lanes. */
uint32_t k19_shift_round(uint32_t seed)
{
	uint16_t a[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64; i++)
		a[i] = (uint16_t)((seed >> (i & 15)) * 3u + i * 0x111u);
	for (i = 0; i < 64; i++)
		a[i] = (uint16_t)((a[i] + 4u) >> 3);
	for (i = 0; i < 64; i++)
		h = kmix(h, a[i]);
	return h;
}

/* Narrowing: 16 -> 8 with arithmetic shift and truncation (VSHRN/VMOVN pattern). */
uint32_t k19_narrow(uint32_t seed)
{
	int16_t a[64];
	int8_t b[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64; i++)
		a[i] = (int16_t)(uint16_t)((seed >> (i & 15)) * 5u + i);
	for (i = 0; i < 64; i++)
		b[i] = (int8_t)(uint8_t)(a[i] >> 4);
	for (i = 0; i < 64; i++)
		h = kmix(h, (uint8_t)b[i]);
	return h;
}

/* Saturating narrowing: 16 -> 8 with explicit saturation (VQSHRN/VQMOVN pattern). */
uint32_t k19_narrow_sat(uint32_t seed)
{
	int16_t a[64];
	int8_t b[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64; i++)
		a[i] = (int16_t)(uint16_t)((seed >> (i & 15)) * 7u + i * 3u);
	for (i = 0; i < 64; i++)
		b[i] = ksat8((int32_t)a[i] >> 4);
	for (i = 0; i < 64; i++)
		h = kmix(h, (uint8_t)b[i]);
	return h;
}

/* Widening multiply-accumulate: int16 x int16 -> int32 (VMLAL.S16). */
uint32_t k19_widen_mul(uint32_t seed)
{
	int16_t a[48], b[48];
	int32_t acc[48];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 48; i++) {
		a[i] = (int16_t)(uint16_t)((seed >> (i & 15)) + i * 11u);
		b[i] = (int16_t)(uint16_t)((seed * 3u + i) >> 2);
		acc[i] = (int32_t)(i * 17u);
	}
	for (i = 0; i < 48; i++)
		acc[i] += (int32_t)a[i] * b[i];
	for (i = 0; i < 48; i++)
		h = kmix(h, (uint32_t)acc[i]);
	return h;
}

/* Widening multiply-subtract: VMLSL.S16 pattern. */
uint32_t k19_widen_mls(uint32_t seed)
{
	int16_t a[48], b[48];
	int32_t acc[48];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 48; i++) {
		a[i] = (int16_t)(uint16_t)((seed >> (i & 15)) ^ (i * 29u));
		b[i] = (int16_t)(uint16_t)((seed + i * 7u) >> 1);
		acc[i] = (int32_t)(i * 0x101u);
	}
	for (i = 0; i < 48; i++)
		acc[i] -= (int32_t)a[i] * b[i];
	for (i = 0; i < 48; i++)
		h = kmix(h, (uint32_t)acc[i]);
	return h;
}

/* Saturating left shift: VQSHL pattern (the comparison form is what compilers recognise). */
uint32_t k19_sat_shift(uint32_t seed)
{
	int16_t a[64];
	int16_t b[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64; i++)
		a[i] = (int16_t)(uint16_t)((seed >> (i & 15)) * 13u + i);
	for (i = 0; i < 64; i++) {
		int32_t v = (int32_t)a[i] * 4;

		b[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
	for (i = 0; i < 64; i++)
		h = kmix(h, (uint16_t)b[i]);
	return h;
}

/* Multiply by a scalar held in a variable: VMUL/VMUL-by-scalar pattern. */
uint32_t k19_scalar_mul(uint32_t seed)
{
	int16_t a[64];
	int16_t b[64];
	int16_t k = (int16_t)ksext((seed | 1u) & 0xFFFFu, 16);
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64; i++)
		a[i] = (int16_t)(uint16_t)((seed >> (i & 15)) + i * 3u);
	for (i = 0; i < 64; i++)
		b[i] = (int16_t)(uint16_t)(a[i] * k);
	for (i = 0; i < 64; i++)
		h = kmix(h, (uint16_t)b[i]);
	return h;
}
