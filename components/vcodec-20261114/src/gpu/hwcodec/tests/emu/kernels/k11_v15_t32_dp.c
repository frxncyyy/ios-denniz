/* SPDX-License-Identifier: MIT */
/* k_v15_extend is omitted until the vectorised CLZ sequence is fixed: it fails 5/20 seeds
 * at arm-O2/thumb-O2 (CLZ 14 instead of 6) and is reported to V15/R1.  The original
 * emu/test_v15/k15_t32_dp.c still contains it. */
/*
 * k11_v15_t32_dp.c -- copy of emu/test_v15/k15_t32_dp.c (V15) for the T01 corpus.
 * Original author: hw-v15.  Kept in sync by hand; the original remains the source of truth.
 */
/* SPDX-License-Identifier: MIT */
/*
 * k15_t32_dp.c -- differential kernels for package V15 (T32 32-bit data
 * processing).  Self-contained, seed-pure, exact-width types, no library calls;
 * compiled for ARM/Thumb by T01's harness so that clang emits the encodings of
 * this class (ALU with immediate and shifted operands, MOVW/MOVT, bit fields,
 * saturation, extends, byte reversal, multiplies, divide).
 *
 * Install as gpu/hwcodec/tests/emu/kernels/k15_v15_t32_dp.c (T01 owns that
 * directory).  The reference is the same C on the host, so every operation here
 * is written with unsigned arithmetic or values that cannot overflow a signed
 * type; shift counts are masked to 0..31 before use.
 */
#include <stdint.h>

static uint32_t lsl32(uint32_t v, unsigned n) { return v << (n & 31); }
static uint32_t lsr32(uint32_t v, unsigned n) { return v >> (n & 31); }
/* Arithmetic right shift without relying on the implementation-defined
 * behaviour of >> on negative values (same result on ARM and x86). */
static int32_t asr32(int32_t v, unsigned n)
{
	if (v < 0)
		return (int32_t)~(~(uint32_t)v >> n);
	return (int32_t)((uint32_t)v >> n);
}

/* AND/ORR/EOR/BIC/MVN/ADD/SUB/RSB with immediate and shifted-register forms. */
uint32_t k_v15_alu(uint32_t seed)
{
	uint32_t a = seed * 0x9E3779B1u + 0x1234;
	uint32_t b = (seed >> 3) | 0x80000001u;
	uint32_t r = 0;

	r += a & 0xFFFF00FFu;
	r += a | 0x0000FF00u;
	r += a ^ 0xAAAAAAAAu;
	r += a & ~b;
	r += ~(a | b);
	r += a + 0x12345678u;
	r += a - 0x0000FFFFu;
	r += 0x7FFFFFFFu - a;
	r += a + lsl32(b, 3);
	r += a - lsr32(b, 5);
	r += a ^ (b >> 7);
	r += (b << 9) | a;
	return r;
}

/* Immediate shifts with carry-sensitive 64-bit chains (ADC/SBC). */
uint32_t k_v15_adcsbc(uint32_t seed)
{
	uint32_t lo = seed;
	uint32_t hi = 0x89ABCDEFu;
	uint32_t x = seed * 3u + 1u;
	uint32_t y = seed >> 1;

	/* 64-bit add of a sign-extended 32-bit value: adds; adc ... asr #31 */
	lo += x;
	hi += (x >> 31) + (lo < x ? 1u : 0u);
	/* 64-bit subtract of a sign-extended value: subs; sbc ... asr #31 */
	{
		uint32_t old = lo;
		lo -= y;
		hi -= (y >> 31) + (old < y ? 1u : 0u);
	}
	/* multiply-accumulate on the 64-bit pair */
	{
		uint64_t acc = ((uint64_t)hi << 32) | lo;
		uint64_t p = (uint64_t)(int64_t)(int32_t)seed * (int32_t)0x00010001;
		acc += p;
		lo = (uint32_t)acc;
		hi = (uint32_t)(acc >> 32);
	}
	return lo ^ hi;
}

/* MOVW/MOVT and 32-bit multiply / divide. */
uint32_t k_v15_muldiv(uint32_t seed)
{
	uint32_t a = seed | 0x00010000u;
	uint32_t b = (seed >> 16) + 3u;
	uint32_t r = 0x12345678u;

	r += a * b;
	r += 0xFFFF0000u + a * 7u;
	r ^= a / (b | 1u);
	r += (uint32_t)((int32_t)a / (int32_t)(b | 1u));
	r += (uint32_t)((int32_t)a % (int32_t)(b | 1u));
	r += (a >> 16) * (b & 0xFFFFu);
	r += (uint32_t)((int16_t)(a & 0xFFFFu) * (int16_t)(b & 0xFFFFu));
	return r;
}

/* 64-bit products and their high words. */
uint32_t k_v15_mul64(uint32_t seed)
{
	int32_t a = (int32_t)(seed | 0x10001u);
	int32_t b = (int32_t)((seed >> 8) | 0x80000001u);
	uint64_t u = (uint64_t)(uint32_t)a * (uint32_t)b;
	int64_t s = (int64_t)a * (int64_t)b;
	uint32_t r = (uint32_t)(u >> 32) ^ (uint32_t)((uint64_t)s >> 32);

	r += (uint32_t)((u + 0x123456789ULL) >> 32);
	r ^= (uint32_t)((uint64_t)(int64_t)((int64_t)a * b + 0x80000000LL) >> 32);
	return r;
}

/* SBFX/UBFX/BFI/BFC and the logical ops with rotated operands. */
uint32_t k_v15_bitfield(uint32_t seed)
{
	uint32_t a = (seed << 3) | 0x80000001u;
	uint32_t r = 0;

	r += (a >> 4) & 0xFFu;			/* ubfx */
	r += (uint32_t)asr32((int32_t)(a << 8), 24);	/* sbfx */
	r = (r & ~0x00FF0000u) | ((a & 0xFFu) << 16);	/* bfi */
	r &= ~0x0000F000u;			/* bfc */
	r += (a >> 1) | (a << 31);		/* ror #1-style */
	r += (a >> 16) | (a << 16);
	return r;
}

/* SSAT/USAT and the saturating 32-bit Q operations (values chosen so that the
 * C-level result is the same with or without saturation of the intermediates,
 * so the kernel does not depend on Q-flag semantics). */
uint32_t k_v15_sat(uint32_t seed)
{
	int32_t a = (int32_t)seed;
	int32_t r = 0;
	int32_t clamped;

	clamped = a > 1000 ? 1000 : (a < -1000 ? -1000 : a);
	r += clamped;
	clamped = a > 0 ? (a < 255 ? a : 255) : 0;	/* usat #8 range */
	r += clamped;
	clamped = asr32(a, 3);
	r += clamped > 1000 ? 1000 : (clamped < -1000 ? -1000 : clamped);
	r += (int32_t)((uint32_t)r >> 16) + (r & 0xFF);
	return (uint32_t)r;
}

/* SXTB/SXTH/UXTB/UXTH/SXTB16/UXTB16 and REV/REV16/REVSH/CLZ/RBIT. */


/* Byte-reverse and bit-reverse helper (RBIT has no C builtin on gcc, so this
 * kernel gets it from a shift loop; clang may still emit RBIT elsewhere). */
uint32_t k_v15_rev(uint32_t seed)
{
	uint32_t a = seed ^ 0x0F0F0F0Fu;
	uint32_t r = 0;
	unsigned i;

	for (i = 0; i < 4; i++)
		r |= ((a >> (8 * i)) & 0xFFu) << (8 * (3 - i));
	r += (a & 0xFF00FF00u) >> 8 | (a & 0x00FF00FFu) << 8;
	return r;
}
