/* SPDX-License-Identifier: MIT */
/* k_v16_atomic is omitted: at -march=armv8-a clang emits LDAEX/STLXR, an ARMv8
 * acquire/release instruction that the vendor set (ARMv7 Thumb) never uses and
 * V12 deliberately does not implement.  It remains in V16s own test suite. */
/*
 * k15_v16_mem.c -- copy of emu/test_v16/k_v16_mem.c (V16) for the T01 corpus.
 * Original author: hw-v16.  Kept in sync by hand; the original remains the source of truth.
 */
/* SPDX-License-Identifier: MIT */
/*
 * k_v16_mem.c -- V16 differential kernels for T01s harness (copy to tests/emu/kernels/
 * as k14_v16_mem.c, or keep this file in sync by hand; this is the source of truth).
 *
 * Same shape as the other packages' kernels: pure functions of one uint32_t seed, no globals,
 * no libc, exact-width types.  On ARM (Thumb) they produce the 32-bit load/store, table
 * branch, branch-with-link and exclusive encodings that package V16 implements, so compiling
 * the file for armv7a/thumb and running it under the interpreter versus natively exercises
 * exactly those encodings.
 *
 * What each kernel is meant to exercise:
 *   k_v16_ldst_sizes   LDRB/STRB, LDRH/STRH, LDR/STR with immediate offsets (word-sized
 *                      even/odd offsets, negative offsets through pointer decrements)
 *   k_v16_ldst_imm     LDR/STR with large constant offsets (imm12) and register offsets
 *                      (base + index << 2/3 from array indexing)
 *   k_v16_ldrd         LDRD/STRD: 64-bit loads/stores of a uint64_t array
 *   k_v16_ldm          LDM/STM and PUSH/POP wide: a 40-byte struct copied by value
 *   k_v16_switch_dense TBB/TBH: a dense switch on a small value (clang emits TBB/TBH)
 *   k_v16_switch_wide  a switch whose cases are spread out (CMP/B<c>.W chains)
 *   k_v16_branch       B/BL/B<c>.W: a loop with calls to a local helper and long forward
 *                      branches
 *   k_v16_stack        deep local arrays and recursion (SP-relative LDR/STR, PUSH/POP wide)
 *   k_v16_atomic       LDREX/STREX through the C11 __atomic builtins on a local variable
 */
#include "kcommon.h"

/* ------------------------------------------------------------------ byte/halfword/word */

uint32_t k_v16_ldst_sizes(uint32_t seed)
{
	uint8_t bytes[32];
	uint16_t halves[16];
	uint32_t words[16];
	uint32_t h = seed;
	unsigned i;
	uint32_t acc = 0;

	for (i = 0; i < 32u; i++)
		bytes[i] = (uint8_t)(h = kmix(h, i));
	for (i = 0; i < 16u; i++)
		halves[i] = (uint16_t)(h = kmix(h, 0x100u + i));
	for (i = 0; i < 16u; i++)
		words[i] = h = kmix(h, 0x200u + i);

	/* Constant indices give the assembler fixed immediate offsets; the odd offsets below
	 * force the immediate-8 pre/post-indexed forms for byte accesses. */
	acc = kmix(acc, bytes[0]);
	acc = kmix(acc, bytes[3]);
	acc = kmix(acc, bytes[15]);
	acc = kmix(acc, bytes[31]);
	acc = kmix(acc, halves[0]);
	acc = kmix(acc, halves[7]);
	acc = kmix(acc, words[0]);
	acc = kmix(acc, words[13]);

	bytes[5] = (uint8_t)h;
	bytes[11] = (uint8_t)(h >> 8);
	halves[9] = (uint16_t)h;
	words[3] = h;
	words[12] = h ^ 0xA5A5A5A5u;

	/* Variable indices force the register-offset forms (Rm << 0..3). */
	for (i = 0; i < 8u; i++) {
		unsigned k = (h >> (i * 3)) & 15u;

		acc = kmix(acc, bytes[k]);
		acc = kmix(acc, halves[k & 15u]);
		acc = kmix(acc, words[k]);
	}
	return kmix(kmix(acc, bytes[5]), words[3] ^ words[12]);
}

uint32_t k_v16_ldst_imm(uint32_t seed)
{
	uint32_t buf[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64u; i++)
		buf[i] = h = kmix(h, i);

	/* Fixed large offsets (imm12) and a pointer walk with negative steps. */
	{
		uint32_t acc = 0;
		uint32_t *p = &buf[60];

		acc = kmix(acc, buf[0]);
		acc = kmix(acc, buf[17]);
		acc = kmix(acc, buf[63]);
		acc = kmix(acc, *p);
		p -= 7;
		acc = kmix(acc, *p);
		*p = acc;
		p -= 3;
		*p = acc ^ 0x5A5A5A5Au;
		acc = kmix(acc, *p);
		/* Register offset: index scaled by 4 at -O2 and above. */
		for (i = 0; i < 9u; i++) {
			unsigned k = (acc >> (i * 2)) & 31u;

			acc = kmix(acc, buf[k]);
		}
		return acc;
	}
}

uint32_t k_v16_ldrd(uint32_t seed)
{
	uint64_t v[16];
	uint32_t h = seed;
	unsigned i;
	uint64_t acc = 0;

	for (i = 0; i < 16u; i++) {
		v[i] = ((uint64_t)kmix(h, i) << 32) | kmix(h, 0x40u + i);
		h = kmix(h, 0x80u + i);
	}
	for (i = 0; i < 16u; i++)
		acc += v[i] * (uint64_t)(i + 1u) + (acc >> 17);
	v[3] = acc;
	v[9] = acc ^ 0x0123456789ABCDEFull;
	return kmix64(kmix64(h, acc), v[3] + v[9]);
}

/* ------------------------------------------------------------------ LDM/STM, PUSH/POP */

struct k_v16_blob {
	uint32_t a, b, c, d, e, f, g, h, i, j;
};

static uint32_t k_v16_blob_sum(struct k_v16_blob s)
{
	return s.a ^ s.b ^ s.c ^ s.d ^ s.e ^ s.f ^ s.g ^ s.h ^ s.i ^ s.j;
}

static struct k_v16_blob k_v16_blob_mix(struct k_v16_blob s, uint32_t k)
{
	s.a += k;
	s.c ^= s.a;
	s.e += s.c;
	s.g ^= s.e;
	s.i += s.g;
	s.b += s.i;
	s.d ^= s.b;
	s.f += s.d;
	s.h ^= s.f;
	s.j += s.h;
	return s;
}

uint32_t k_v16_ldm(uint32_t seed)
{
	struct k_v16_blob s;
	uint32_t h = seed;

	s.a = h; s.b = h ^ 1u; s.c = h ^ 2u; s.d = h ^ 3u; s.e = h ^ 4u;
	s.f = h ^ 5u; s.g = h ^ 6u; s.h = h ^ 7u; s.i = h ^ 8u; s.j = h ^ 9u;
	s = k_v16_blob_mix(s, 0x9E3779B9u);
	s = k_v16_blob_mix(s, 0x85EBCA6Bu);
	return k_v16_blob_sum(k_v16_blob_mix(s, h));
}

/* ------------------------------------------------------------------ table branch */

static uint32_t k_v16_step(uint32_t x, unsigned which)
{
	switch (which) {
	case 0: return x + 0x11u;
	case 1: return x ^ 0x22u;
	case 2: return x - 0x33u;
	case 3: return (x << 3) | (x >> 29);
	case 4: return x * 5u;
	case 5: return ~x;
	case 6: return x + (x >> 7);
	case 7: return x ^ 0xA5A5A5A5u;
	case 8: return (x >> 1) + 0x1234u;
	case 9: return x * 3u + 7u;
	default: return x;
	}
}

uint32_t k_v16_switch_dense(uint32_t seed)
{
	uint32_t x = seed;
	unsigned i;

	/* Ten cases: clang emits a TBB/TBH jump table for this once the value is masked. */
	for (i = 0; i < 32u; i++) {
		unsigned which = (x >> 4) & 15u;

		x = k_v16_step(x, which);
		if (which >= 10u)
			x = kmix(x, i);
	}
	return x;
}

uint32_t k_v16_switch_wide(uint32_t seed)
{
	uint32_t x = seed;
	unsigned i;

	for (i = 0; i < 16u; i++) {
		switch ((x >> 24) & 0xFFu) {
		case 0x00: x += 1u; break;
		case 0x11: x ^= 0xFFFF0000u; break;
		case 0x22: x -= 0x12345678u; break;
		case 0x33: x = (x << 5) | (x >> 27); break;
		case 0x44: x *= 0x01010101u; break;
		case 0x55: x = ~x; break;
		case 0x66: x += x >> 11; break;
		case 0x77: x ^= 0x0F0F0F0Fu; break;
		case 0x88: x -= 0x80000000u; break;
		case 0x99: x |= 0x00FF00FFu; break;
		default: x = kmix(x, i); break;
		}
	}
	return x;
}

/* ------------------------------------------------------------------ branches and calls */

static uint32_t k_v16_leaf(uint32_t x)
{
	return (x ^ (x >> 13)) * 0x5BD1E995u;
}

static uint32_t k_v16_mid(uint32_t x, unsigned n)
{
	uint32_t acc = x;
	unsigned i;

	for (i = 0; i < n; i++) {
		if (acc & 1u)
			acc = k_v16_leaf(acc + i);
		else
			acc = k_v16_leaf(acc ^ i) + 1u;
	}
	return acc;
}

uint32_t k_v16_branch(uint32_t seed)
{
	uint32_t acc = seed;
	unsigned i;

	for (i = 0; i < 24u; i++) {
		if (i & 1u) {
			acc = k_v16_mid(acc, 3u);
		} else {
			/* Long forward branch over a block at -O0. */
			acc ^= 0x9E3779B9u;
			acc += i;
			acc = k_v16_leaf(acc);
			acc += k_v16_leaf(acc >> 3);
		}
		if ((acc & 0xFFu) == 0x5Au)
			acc = k_v16_mid(acc, 5u);
	}
	return acc;
}

uint32_t k_v16_stack(uint32_t seed)
{
	uint32_t a[24];
	uint32_t b[12];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 24u; i++)
		a[i] = h = kmix(h, i);
	for (i = 0; i < 12u; i++)
		b[i] = h = kmix(h, 0x80u + i);
	/* A nested call forces a full PUSH/POP frame at -O0. */
	h = k_v16_mid(a[3] ^ b[7], 4u);
	for (i = 0; i < 24u; i += 3)
		a[i] = h ^ b[i / 3u];
	return kmix(a[0] ^ a[21], b[11] ^ h);
}

/* ------------------------------------------------------------------ exclusives */


