/* SPDX-License-Identifier: MIT */
/*
 * k10_dp.c -- differential kernel for package V10 (A32 data processing, moves, multiplies).
 *
 * These functions are the V10 part of the T01 differential corpus: the same C is compiled
 * natively (host, x86-64) and for ARMv7 (armv7a-linux-gnueabi) and executed by the
 * interpreter; the results must match.  The C is written so the ARM compiler emits the
 * instruction forms this package owns:
 *
 *   k_v10_alu_imm    AND/EOR/ORR/BIC/MVN/ADD/SUB/RSB/ADC/SBC/CMP/TST with ARM immediates
 *                  (rotated 8-bit encodings) and the constants that need MOVW/MOVT
 *   k_v10_alu_reg    the same opcodes with register and register-shifted-register operands
 *   k_v10_shift_imm  LSL/LSR/ASR/ROR by immediate amounts
 *   k_v10_shift_reg  LSL/LSR/ASR/ROR by a register amount (register-specified shift)
 *   k_v10_cmp_branch SUBS/CMP/CMN/TST/TEQ feeding if/else chains
 *   k_v10_mul32      MUL and MLA
 *   k_v10_mul64      UMULL/SMULL, UMLAL/SMLAL and UMAAL accumulations
 *   k_v10_movw       MOVW/MOVT constants
 *   k_v10_clz        CLZ (via __builtin_clz)
 *   k_v10_adr_switch ADR (ADD/SUB with r15) and a dense jump table / ALU write of r15
 *
 * Pure functions of the seed, exact-width types only, no globals, no libc: the rules at the
 * top of tests/emu/kernels/kcommon.h.  T01 owns tests/emu/kernels; this file lives in the
 * V10 package directory and T01 can compile it from here (posted on the board).
 */
#include "kcommon.h"

uint32_t k_v10_alu_imm(uint32_t seed)
{
	uint32_t x = seed | 1u;
	uint32_t a = x & 0xff00ff00u;
	uint32_t b = x | 0x80000000u;
	uint32_t c = x ^ 0x000000ffu;
	uint32_t d = x & ~0x00ffff00u;
	uint32_t e = ~x;
	uint32_t f = x + 0x7f000000u;
	uint32_t g = x - 0x00000001u;
	uint32_t h = 0x0000012cu - x;
	uint32_t i = x + ~0x000000f0u + 1u;
	uint32_t j = x + ~0x0000ff00u + (x >> 31);

	return kmix(kmix(kmix(a, b), kmix(c, d)), kmix(kmix(e, f), kmix(g, h))) ^
		kmix(kmix(i, j), x);
}

uint32_t k_v10_alu_reg(uint32_t seed)
{
	uint32_t a = seed ^ 0xa5a5a5a5u;
	uint32_t b = krotl(seed, 9) | 1u;
	uint32_t c = seed + 0x12345678u;	/* forces MOVW/MOVT for the constant */
	uint32_t d = (a & b) | (a ^ c);
	uint32_t e = (a | ~b) ^ (c & ~a);
	uint32_t f = a + b + c;			/* ADC in the 64-bit sense is k_v10_mul64 */
	uint32_t g = a - b - c;
	uint32_t h = c - a;
	uint32_t i = (b << 3) ^ (b >> 5) ^ (uint32_t)((int32_t)b >> 7);

	return kmix(kmix(kmix(d, e), kmix(f, g)), kmix(h, i)) ^ kmix(a, kmix(b, c));
}

uint32_t k_v10_shift_imm(uint32_t seed)
{
	uint32_t x = seed ^ 0x80000001u;

	return kmix(x << 1, kmix(x >> 2, (uint32_t)((int32_t)x >> 3))) ^
		kmix(x << 17, kmix(x >> 19, krotl(x, 7))) ^ (x << 31) ^ (x >> 31);
}

uint32_t k_v10_shift_reg(uint32_t seed)
{
	uint32_t x = seed ^ 0x40000001u;
	uint32_t n = seed >> 27;		/* 0..31, defined for C */
	uint32_t m = (seed >> 24) & 7u;
	uint32_t r = (x << n) ^ (x >> n) ^ (uint32_t)((int32_t)x >> n);

	return kmix(kmix(r, krotl(x, n)), (x << m) | (x >> (31u - m))) ^ (x >> m);
}

uint32_t k_v10_cmp_branch(uint32_t seed)
{
	uint32_t x = seed ^ 0x5a5a5a5au;
	uint32_t acc = 0;
	unsigned i;

	for (i = 0; i < 8; i++) {
		uint32_t y = krotl(x, i) + i;
		if (y < x)
			acc += y;
		else if (y == x)
			acc ^= y;
		else if (y > 0x80000000u)
			acc -= y;
		else
			acc |= y;
		if ((y & 0x00010000u) != 0)
			acc += 3;
		if (y >= 0x7fffffffu)
			acc ^= 0xffffu;
		x = krotl(x, 3) ^ i;
	}
	if (acc == 0)
		acc = 1;
	if ((int32_t)acc < 0)
		acc ^= 0x1234u;
	return kmix(acc, seed);
}

uint32_t k_v10_mul32(uint32_t seed)
{
	uint32_t a = seed | 1u;
	uint32_t b = krotl(seed, 11) ^ 0x01010101u;
	uint32_t p = a * b;
	uint32_t q = a * 7u + b;
	uint32_t r = b - a * 3u;

	return kmix(kmix(p, q), kmix(r, a * a + b * b));
}

uint32_t k_v10_mul64(uint32_t seed)
{
	uint32_t a = seed ^ 0x12345678u;
	uint32_t b = krotl(seed, 11) | 3u;
	uint64_t p = (uint64_t)a * b;
	uint64_t q = (uint64_t)(int64_t)(int32_t)a * (int32_t)b;
	uint64_t acc = seed;
	uint64_t sacc = (uint64_t)(int64_t)(int32_t)seed;
	uint32_t i;

	for (i = 1; i <= 6; i++) {
		uint32_t u = seed + i * 0x01010101u;
		uint32_t v = seed ^ (i * 0x11111111u);

		acc += (uint64_t)u * v;			/* UMLAL/UMAAL material */
		sacc += (uint64_t)(int64_t)(int32_t)u * (int32_t)v;	/* SMLAL material */
	}
	return kmix64(kmix64(kmix64(seed, p), q), kmix64(acc, sacc));
}

uint32_t k_v10_movw(uint32_t seed)
{
	uint32_t a = seed ^ 0x12345678u;	/* not an ARM immediate: MOVW/MOVT */
	uint32_t b = 0xfedcba98u - seed;	/* ditto */
	uint32_t c = (seed & 0xffff0000u) | 0x0000beefu;
	uint32_t d = 0xdeadbeefu ^ seed;

	return kmix(kmix(a, b), kmix(c, d));
}

uint32_t k_v10_clz(uint32_t seed)
{
	uint32_t x = seed | 1u;
	uint32_t y = seed;

	/* __builtin_clz(0) is undefined in C; the CLZ instruction returns 32, which is
	 * tested directly in test_v10.  Guard the C so the kernel is well defined. */
	return kmix((uint32_t)__builtin_clz(x), y ? (uint32_t)__builtin_clz(y) : 32u) ^
		kmix((uint32_t)__builtin_ctz(x), seed);
}

static uint32_t k_v10_case_body(uint32_t v, unsigned k)
{
	switch (k & 15u) {
	case 0: return v + 0x11u;
	case 1: return v ^ 0x2222u;
	case 2: return v * 3u;
	case 3: return v - 0x4444u;
	case 4: return ~v;
	case 5: return krotl(v, 5) + 0x6666u;
	case 6: return v | 0x7777u;
	case 7: return v & 0x8888u;
	default: return v + k;
	}
}

uint32_t k_v10_adr_switch(uint32_t seed)
{
	uint32_t acc = seed;
	uint32_t tab[4];
	unsigned i;

	tab[0] = seed + 1u;
	tab[1] = seed ^ 0x55u;
	tab[2] = krotl(seed, 3);
	tab[3] = seed * 5u;
	for (i = 0; i < 4; i++)
		acc += tab[i] + k_v10_case_body(seed, i * 3u + (seed >> 29));

	acc ^= tab[(seed >> 6) & 3u];
	return kmix(acc, k_v10_case_body(acc, acc));
}
