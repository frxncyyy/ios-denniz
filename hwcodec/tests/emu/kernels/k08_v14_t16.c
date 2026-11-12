/* SPDX-License-Identifier: MIT */
/*
 * k08_v14_t16.c -- copy of emu/test_v14/k14_t16.c (V14) for the T01 corpus.
 * Original author: hw-v14.  Kept in sync by hand; the original remains the source of truth.
 *
 * V14 differential kernel for T01s harness: exercises the T32 16-bit
 * instruction classes (ALU/shift/compare/branch/load-store/stack/rev/extend)
 * in portable C so the same source can be compiled for ARM (Thumb) and for
 * x86-64 and run on both sides of the interpreter.
 *
 * Compiled with -mthumb -Os this produces mostly 16-bit encodings; the
 * surrounding harness compares the ARM-native result with the interpreted
 * result, so any mismatch is an encoding/semantics bug in a class package.
 * Pure function of the seed, no globals, no libc.
 */
#include "kcommon.h"

static inline uint16_t k14_bswap16(uint16_t x)
{
	return (uint16_t)((uint16_t)(x >> 8) | (uint16_t)(x << 8));
}

static inline uint32_t k14_bswap32(uint32_t x)
{
	return (x >> 24) | ((x >> 8) & 0x0000FF00u) |
	       ((x << 8) & 0x00FF0000u) | (x << 24);
}

static inline uint32_t k14_asr(uint32_t x, unsigned n)
{
	n &= 31u;
	if (n == 0)
		return x;
	{
		uint32_t r = x >> n;

		if (x & 0x80000000u)
			r |= ~(0xFFFFFFFFu >> n);
		return r;
	}
}

uint32_t k14_alu(uint32_t seed)
{
	uint32_t a = seed & 0xFFFFu, b = (seed >> 16) & 0xFFu;
	uint32_t s = a + b, d = a - b, r = b - a;
	uint32_t m = a & b, o = a | b, x = a ^ b, n = ~a, c = a & ~b;
	uint32_t p = a * b;
	unsigned sh = (seed >> 8) & 31u;

	s ^= (a << (sh ? sh : 1)) ^ (a >> (sh & 15u));
	d ^= k14_asr(a, sh & 15u);
	m ^= krotl(a, sh) ^ kclz(a);
	o += (uint32_t)ksext(b, 8) + (uint32_t)ksext(a, 16);
	x ^= (a & 0xFFu) + (a & 0xFFFFu);
	n ^= (uint32_t)k14_bswap16((uint16_t)a) ^ k14_bswap32(a);
	c += (uint32_t)ksext(a >> 8, 8);
	r ^= (uint32_t)ksext(a >> 3, 16);

	return kmix(kmix(s, d), kmix(m, o)) ^
	       kmix(kmix(x, n), kmix(c, kmix(r, p)));
}

uint32_t k14_mem(uint32_t seed)
{
	uint8_t bytes[16];
	uint16_t half[8];
	uint32_t word[8];
	uint32_t i, acc = seed;

	for (i = 0; i < 16; i++)
		bytes[i] = (uint8_t)(seed >> (i & 7)) + (uint8_t)i;
	for (i = 0; i < 8; i++) {
		half[i] = (uint16_t)(seed + i * 0x101u);
		word[i] = seed ^ (0x01010101u * i);
	}
	for (i = 0; i < 16; i++) {
		unsigned j = (unsigned)(seed >> (i & 3)) & 7u;

		acc = kmix(acc, bytes[i]);
		acc = kmix(acc, half[j]);
		acc = kmix(acc, word[j]);
		bytes[i] = (uint8_t)(acc + i);
		half[j] = (uint16_t)acc;
		word[j] = acc + i;
	}
	acc ^= (uint32_t)bytes[0] | ((uint32_t)half[0] << 8) | (word[0] << 16);

	return kmix(acc, word[7] ^ half[7] ^ bytes[15]);
}

uint32_t k14_branch(uint32_t seed)
{
	uint32_t acc = seed;
	uint32_t i;

	for (i = 0; i < 12; i++) {
		uint32_t v = (seed >> i) & 0xFFu;

		if (v == 0)
			acc += 3;
		else if (v < 8)
			acc ^= v + 1u;
		else if (v < 64)
			acc += v * 5u;
		else
			acc -= v;

		switch (v & 7u) {
		case 0: acc += 0x11u; break;
		case 1: acc ^= 0x22u; break;
		case 2: acc -= 0x33u; break;
		case 3: acc += 0x44u; break;
		default: acc ^= v; break;
		}
	}

	while (acc & 0x80000000u)
		acc = (acc << 1) ^ 0x9E3779B9u;

	return kmix(acc, k14_alu(seed) ^ k14_mem(seed + 1u));
}
