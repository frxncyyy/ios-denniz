/* SPDX-License-Identifier: MIT */
/*
 * k14_v18_neon.c -- copy of emu/test_v18/k18_neon_3same.c (V18) for the T01 corpus.
 * Original author: hw-v18.  Kept in sync by hand; the original remains the source of truth.
 */
/* SPDX-License-Identifier: MIT */
/*
 * k18_neon_3same.c -- differential kernels for V18 (NEON three registers of the same length).
 *
 * Self-contained uint32_t k18_*(uint32_t seed) kernels in the style of tests/emu/kernels: no globals,
 * no libc, only kcommon.h helpers, seed-pure.  They are written so that clang -O2/-O3 -ftree-vectorize
 * for armv7a NEON emits V18-class instructions; the mnemonics listed per kernel were checked with
 * llvm-objdump on the built object (see test_v18/STATUS.md).  Scalar C is the reference, so the
 * harness compares my class against the compiler's own vectorised version of the same arithmetic.
 *
 * Provenance: written for package V18; T01 may copy this into tests/emu/kernels/.
 */
#include "kcommon.h"

/* Byte add/sub with wraparound: VADD.I8 / VSUB.I8 (also .I16/.I32 in the 16/32 variants). */
uint32_t k18_addsub8(uint32_t seed)
{
	uint8_t a[64], b[64], s[64], d[64];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 64; i++) {
			a[i] = (uint8_t)((seed >> ((i + k) & 15)) ^ (i * 37u + k));
			b[i] = (uint8_t)((seed * (i + 1u)) >> (k + 1));
			s[i] = (uint8_t)(a[i] + b[i]);
			d[i] = (uint8_t)(a[i] - b[i]);
		}
		for (i = 0; i < 64; i++)
			h = kmix(h, (uint32_t)s[i] | ((uint32_t)d[i] << 8));
	}
	return h;
}

uint32_t k18_addsub16(uint32_t seed)
{
	uint16_t a[32], b[32], s[32], d[32];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 32; i++) {
			a[i] = (uint16_t)((seed >> (i & 15)) + i * 101u + k);
			b[i] = (uint16_t)((seed * (i + 3u)) >> 3);
			s[i] = (uint16_t)(a[i] + b[i]);
			d[i] = (uint16_t)(a[i] - b[i]);
		}
		for (i = 0; i < 32; i++)
			h = kmix(h, ((uint32_t)s[i] << 16) | d[i]);
	}
	return h;
}

uint32_t k18_addsub32(uint32_t seed)
{
	uint32_t a[16], b[16], s[16], d[16];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 8; k++) {
		for (i = 0; i < 16; i++) {
			a[i] = seed * (i + 1u) + k * 0x01010101u;
			b[i] = (seed >> (i & 15)) ^ (i * 0x11111111u);
			s[i] = a[i] + b[i];
			d[i] = a[i] - b[i];
		}
		for (i = 0; i < 16; i++)
			h = kmix(h, s[i] ^ d[i]);
	}
	return h;
}

/* Signed and unsigned minimum/maximum: VMAX.S/U and VMIN.S/U on bytes and halfwords. */
uint32_t k18_minmax8(uint32_t seed)
{
	int8_t sa[64], sb[64];
	uint8_t ua[64], ub[64];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 64; i++) {
			sa[i] = (int8_t)((seed >> (i & 15)) ^ (i * 53u));
			sb[i] = (int8_t)((seed + i * 91u) >> 2);
			ua[i] = (uint8_t)sa[i];
			ub[i] = (uint8_t)sb[i];
		}
		for (i = 0; i < 64; i++) {
			int8_t mn = sa[i] < sb[i] ? sa[i] : sb[i];
			int8_t mx = sa[i] > sb[i] ? sa[i] : sb[i];
			uint8_t umn = ua[i] < ub[i] ? ua[i] : ub[i];
			uint8_t umx = ua[i] > ub[i] ? ua[i] : ub[i];

			h = kmix(h, (uint8_t)mn | ((uint32_t)(uint8_t)mx << 8) |
				   ((uint32_t)umn << 16) | ((uint32_t)umx << 24));
		}
	}
	return h;
}

uint32_t k18_minmax32(uint32_t seed)
{
	int32_t a[16], b[16];
	uint32_t ua[16], ub[16];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 8; k++) {
		for (i = 0; i < 16; i++) {
			a[i] = (int32_t)(seed * (i + 1u) + k * 7u);
			b[i] = (int32_t)((seed >> (i & 15)) ^ (i * 0x01010101u));
			ua[i] = (uint32_t)a[i];
			ub[i] = (uint32_t)b[i];
		}
		for (i = 0; i < 16; i++) {
			int32_t mn = a[i] < b[i] ? a[i] : b[i];
			int32_t mx = a[i] > b[i] ? a[i] : b[i];
			uint32_t umn = ua[i] < ub[i] ? ua[i] : ub[i];
			uint32_t umx = ua[i] > ub[i] ? ua[i] : ub[i];

			h = kmix(h, (uint32_t)mn ^ (uint32_t)mx ^ umn ^ umx);
		}
	}
	return h;
}

/* Absolute difference and accumulate: VABD.U8/I16 and VABA. */
uint32_t k18_absdiff(uint32_t seed)
{
	uint8_t a[64], b[64];
	uint16_t s[32];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 64; i++) {
			a[i] = (uint8_t)((seed >> (i & 15)) * (i + 1u));
			b[i] = (uint8_t)((seed ^ (i * 29u)) >> 1);
		}
		for (i = 0; i < 32; i++)
			s[i] = (uint16_t)((a[2 * i] > b[2 * i] ? a[2 * i] - b[2 * i]
							      : b[2 * i] - a[2 * i]) +
					  (a[2 * i + 1] > b[2 * i + 1] ? a[2 * i + 1] - b[2 * i + 1]
								      : b[2 * i + 1] - a[2 * i + 1]));
		for (i = 0; i < 32; i++)
			h = kmix(h, s[i]);
	}
	return h;
}

/* Multiply and multiply-accumulate on 16-bit lanes: VMUL.I16 / VMLA.I16 (mod 2^16). */
uint32_t k18_mul16(uint32_t seed)
{
	uint16_t a[32], b[32], acc[32];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 32; i++) {
			a[i] = (uint16_t)((seed >> (i & 15)) + i * 13u);
			b[i] = (uint16_t)((seed * (i + 5u)) >> (k + 1));
			acc[i] = (uint16_t)(i * 7u + k);
		}
		for (i = 0; i < 32; i++)
			acc[i] = (uint16_t)(acc[i] + (uint16_t)(a[i] * b[i]));
		for (i = 0; i < 32; i++)
			h = kmix(h, acc[i]);
	}
	return h;
}

/* Bitwise operations and a byte select: VAND/VORR/VEOR/VBIC/VBSL. */
uint32_t k18_logic(uint32_t seed)
{
	uint32_t a[16], b[16], c[16];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 8; k++) {
		for (i = 0; i < 16; i++) {
			a[i] = seed * (i + 1u) ^ (k * 0x01010101u);
			b[i] = (seed >> (i & 15)) + i * 0x11111111u;
			c[i] = (a[i] & b[i]) ^ (a[i] | (b[i] >> 3)) ^ (a[i] & ~b[i]);
		}
		for (i = 0; i < 16; i++)
			h = kmix(h, c[i]);
	}
	return h;
}

uint32_t k18_select(uint32_t seed)
{
	uint8_t a[64], b[64], m[64], o[64];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 64; i++) {
			a[i] = (uint8_t)(seed * (i + 1u));
			b[i] = (uint8_t)((seed >> (i & 15)) ^ (i * 17u));
			m[i] = (uint8_t)(i * 37u + k);
			o[i] = (m[i] & 0x80) ? a[i] : b[i];
		}
		for (i = 0; i < 64; i++)
			h = kmix(h, o[i]);
	}
	return h;
}

/* Variable shifts on 32-bit lanes: VSHL (register), .S for negative (arithmetic) counts. */
uint32_t k18_shift_var(uint32_t seed)
{
	int32_t a[16];
	int32_t sh[16];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 8; k++) {
		for (i = 0; i < 16; i++) {
			a[i] = (int32_t)(seed * (i + 1u) + k);
			sh[i] = (int32_t)((i + k) & 31u) - 16;
		}
		for (i = 0; i < 16; i++) {
			int32_t v;

			if (sh[i] >= 0)
				v = (int32_t)((uint32_t)a[i] << sh[i]);
			else if (sh[i] > -32)
				v = a[i] >> -sh[i];
			else
				v = a[i] < 0 ? -1 : 0;
			h = kmix(h, (uint32_t)v);
		}
	}
	return h;
}

/* Halving add: (a + b) >> 1 on bytes/halfwords -- VHADD.U8 / VHADD.U16 and VHADD.S16. */
uint32_t k18_halving(uint32_t seed)
{
	uint16_t a[32], b[32], o[32];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 32; i++) {
			a[i] = (uint16_t)(seed * (i + 1u) + k);
			b[i] = (uint16_t)((seed >> (i & 15)) ^ (i * 101u));
			o[i] = (uint16_t)(((uint32_t)a[i] + b[i]) >> 1);
		}
		for (i = 0; i < 32; i++)
			h = kmix(h, o[i]);
	}
	return h;
}

/* Compare-equal on bytes: VCEQ.I8 -> materialise 0x00/0xff. */
uint32_t k18_cmpeq(uint32_t seed)
{
	uint8_t a[64], b[64], o[64];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 64; i++) {
			a[i] = (uint8_t)((seed >> (i & 15)) ^ (i * 7u));
			b[i] = (uint8_t)((a[i] + (i >> 3) + k) & 0xfe);
		}
		for (i = 0; i < 64; i++)
			o[i] = (a[i] == b[i]) ? 0xff : 0x00;
		for (i = 0; i < 64; i++)
			h = kmix(h, o[i]);
	}
	return h;
}

/* float32 add/multiply on 8-lane arrays: VADD.F32 / VMUL.F32 / VSUB.F32. */
uint32_t k18_float(uint32_t seed)
{
	float a[32], b[32], o[32];
	uint32_t h = seed;
	unsigned i, k;

	for (k = 0; k < 4; k++) {
		for (i = 0; i < 32; i++) {
			a[i] = (float)(int32_t)((seed >> (i & 15)) & 0xffffu) * 0.25f + (float)i;
			b[i] = (float)(int32_t)((seed * (i + 1u)) & 0x3ffu) * 0.5f - (float)k;
			o[i] = a[i] * b[i] + (a[i] - b[i]);
		}
		for (i = 0; i < 32; i++)
			h = kmix(h, kfbits(o[i]));
	}
	return h;
}
