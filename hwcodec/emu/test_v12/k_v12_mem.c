/* SPDX-License-Identifier: MIT */
/*
 * k_v12_mem.c -- differential kernels for the V12 A32 load/store + branch class.
 *
 * Self-contained (no kcommon.h) so this file can be dropped next to T01's kernels; every
 * helper is static and every external name is k12_*.  Pure functions of the seed, no globals,
 * no libc, exact-width types; shifts are masked, no division, unsigned overflow only.
 *
 * Expected generator mix when compiled -marm -O2: LDR/STR (imm + register + pre/post index),
 * LDRB/STRB, LDRH/STRH, LDRSB/LDRSH, LDRD/STRD, LDM/STM (struct copies, PUSH/POP), B/BL/BX
 * (calls through a local function pointer), SP-relative stack traffic.
 */
#include <stdint.h>

static uint32_t kv12_mix(uint32_t h, uint32_t v)
{
	h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
	return h;
}

static uint32_t kv12_mix64(uint32_t h, uint64_t v)
{
	return kv12_mix(kv12_mix(h, (uint32_t)v), (uint32_t)(v >> 32));
}

uint32_t k12_ldst(uint32_t seed)
{
	uint32_t w[12];
	uint16_t h[16];
	uint8_t b[24];
	uint32_t acc = seed;
	unsigned i;

	for (i = 0; i < 12; i++)
		w[i] = seed * 2654435761u + i * 0x01010101u;
	for (i = 0; i < 16; i++)
		h[i] = (uint16_t)(seed >> (i & 15)) + (uint16_t)i;
	for (i = 0; i < 24; i++)
		b[i] = (uint8_t)(seed >> (i & 7)) ^ (uint8_t)(i * 7u);

	for (i = 0; i < 12; i++)
		acc = kv12_mix(acc, w[i] + w[(i + 5u) % 12u]);
	for (i = 0; i < 16; i++)
		acc = kv12_mix(acc, (uint32_t)h[i] ^ (uint32_t)h[(i + 3u) % 16u]);
	for (i = 0; i < 24; i++)
		acc = kv12_mix(acc, (uint32_t)b[i] + (uint32_t)b[(i + 11u) % 24u]);
	return acc;
}

uint32_t k12_ldrd(uint32_t seed)
{
	uint64_t a[16];
	uint64_t c[16];
	uint32_t acc = seed;
	unsigned i;

	for (i = 0; i < 16; i++)
		a[i] = (uint64_t)seed * 0x9e3779b97f4a7c15ull + i;
	for (i = 0; i < 16; i++)
		c[i] = a[i] + (a[(i + 1u) & 15u] >> 3);
	for (i = 0; i < 16; i++)
		acc = kv12_mix64(acc, c[i]);
	return acc;
}

uint32_t k12_signed(uint32_t seed)
{
	int8_t sb[16];
	int16_t sh[16];
	uint32_t acc = seed;
	unsigned i;

	for (i = 0; i < 16; i++)
		sb[i] = (int8_t)(seed >> (i & 7));
	for (i = 0; i < 16; i++)
		sh[i] = (int16_t)(seed >> (i & 15));
	for (i = 0; i < 16; i++)
		acc = kv12_mix(acc, (uint32_t)(int32_t)sb[i]);
	for (i = 0; i < 16; i++)
		acc = kv12_mix(acc, (uint32_t)((int32_t)sh[i] * 3));
	return acc;
}

struct kv12_s40 {
	uint32_t v[10];
};

static struct kv12_s40 kv12_mk(uint32_t seed)
{
	struct kv12_s40 s;
	unsigned i;

	for (i = 0; i < 10; i++)
		s.v[i] = seed + i * 0x11111111u;
	return s;
}

static struct kv12_s40 kv12_add(struct kv12_s40 a, struct kv12_s40 b)
{
	struct kv12_s40 r;
	unsigned i;

	for (i = 0; i < 10; i++)
		r.v[i] = a.v[i] + b.v[(i + 2u) % 10u];
	return r;
}

uint32_t k12_ldm_stm(uint32_t seed)
{
	struct kv12_s40 a = kv12_mk(seed);
	struct kv12_s40 b = a;                  /* 40-byte struct copy: LDM/STM or LDRD/STRD */
	struct kv12_s40 c = kv12_add(a, b);     /* by-value args and return: stack traffic */
	uint32_t acc = seed;
	unsigned i;

	b.v[3] ^= c.v[7];
	for (i = 0; i < 10; i++)
		acc = kv12_mix(acc, a.v[i] ^ b.v[i] ^ c.v[(i + 4u) % 10u]);
	return acc;
}

static uint32_t kv12_leaf1(uint32_t x)
{
	return x ^ 0x55aa55aau;
}

static uint32_t kv12_leaf2(uint32_t x)
{
	return x + 0x1234567u;
}

static uint32_t kv12_leaf3(uint32_t x)
{
	return (x << 7) | (x >> 25);
}

uint32_t k12_branch(uint32_t seed)
{
	uint32_t acc = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		uint32_t (*fn)(uint32_t) = (seed & 1u) ? kv12_leaf1 : kv12_leaf2;

		switch ((acc >> (i & 15)) & 7u) {
		case 0: acc += 3; break;
		case 1: acc ^= 0x9e3779b9u; break;
		case 2: acc = fn(acc); break;            /* BLX through a register */
		case 3: acc = kv12_leaf3(acc); break;     /* BL */
		case 4: acc -= 7; break;
		case 5: acc = acc >> 1; break;
		default: acc = kv12_mix(acc, i); break;
		}
		if (acc & 0x100u)
			acc = kv12_leaf2(acc);
		else
			acc -= 11;
	}
	return acc;
}

static uint32_t kv12_recurse(uint32_t n, uint32_t acc)
{
	uint32_t buf[8];
	unsigned i;

	for (i = 0; i < 8; i++)
		buf[i] = acc + i * n;
	if (n == 0)
		return acc + buf[3];
	return kv12_recurse(n - 1, acc ^ buf[n & 7]) + buf[1];
}

uint32_t k12_stack(uint32_t seed)
{
	uint32_t local[24];
	unsigned i;

	for (i = 0; i < 24; i++)
		local[i] = seed + i;
	return kv12_recurse(seed & 7u, local[seed & 23u]) + local[(seed >> 3) & 23u];
}
