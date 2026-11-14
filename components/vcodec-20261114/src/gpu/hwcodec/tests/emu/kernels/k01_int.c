/* SPDX-License-Identifier: MIT */
/* Integer data processing: add/sub/logic/multiply, shifts, rotates, bit fields, switches,
 * recursion, saturation.  All arithmetic that may overflow is done in unsigned types. */
#include "kcommon.h"

uint32_t k_addsub(uint32_t seed)
{
	uint32_t a = seed, b = krotl(seed, 7) ^ 0xa5a5a5a5u;
	uint32_t s = a + b, d = a - b, r = b - a, c = a + ~b + 1u;

	return kmix(kmix(s, d), kmix(r, c));
}

uint32_t k_logic(uint32_t seed)
{
	uint32_t a = seed ^ 0x5a5a5a5au, b = krotl(seed, 13) | 1u;

	return kmix(kmix(a & b, a | b), kmix(a ^ b, a & ~b)) ^ (b | ~a);
}

uint32_t k_mul(uint32_t seed)
{
	uint32_t a = seed | 1u, b = (seed >> 3) ^ 0x01010101u;
	uint32_t p = a * b, q = (a + 7u) * (b - 3u);

	return kmix(p, q);
}

uint32_t k_mul64(uint32_t seed)
{
	uint32_t a = seed ^ 0x12345678u, b = krotl(seed, 11) | 3u;
	uint64_t p = (uint64_t)a * b;
	uint64_t q = (uint64_t)(int64_t)(int32_t)a * (int32_t)b;

	return kmix64(kmix64(seed, p), q);
}

uint32_t k_madd64(uint32_t seed)
{
	uint64_t acc = seed;
	int64_t sacc = (int64_t)(int32_t)seed;
	uint32_t i;

	for (i = 1; i <= 8; i++) {
		acc += (uint64_t)(seed + i) * (uint64_t)(seed ^ i);
		sacc += (int64_t)(int32_t)(seed - i) * (int32_t)(seed ^ (i * 7u));
	}
	return kmix64(kmix64(seed, acc), (uint64_t)sacc);
}

uint32_t k_shift_imm(uint32_t seed)
{
	uint32_t x = seed ^ 0x80000001u;

	return kmix(x << 1, kmix(x >> 2, (uint32_t)((int32_t)x >> 3))) ^
		kmix(x << 17, x >> 19);
}

uint32_t k_shift_var(uint32_t seed)
{
	uint32_t x = seed ^ 0x40000001u;
	unsigned n = seed & 31u, m = (seed >> 8) & 31u;
	uint32_t a = n ? x << n : x;
	uint32_t b = n ? x >> n : x;
	uint32_t c = (int32_t)x >> m;      /* arithmetic shift, same on ARM and x86-64 */

	return kmix(kmix(a, b), c ^ (x >> 1));
}

uint32_t k_rotate(uint32_t seed)
{
	uint32_t x = seed ^ 0x0f0f0f0fu;
	unsigned n = (seed >> 4) & 31u;

	return kmix(krotl(x, n), krotl(x, 32u - n) ^ (x >> 3));
}

uint32_t k_rev(uint32_t seed)
{
	uint32_t a = __builtin_bswap32(seed);
	uint16_t b = __builtin_bswap16((uint16_t)(seed >> 8));
	uint32_t c = ((uint32_t)b << 4) ^ a;

	return kmix(a, kmix(c, seed));
}

uint32_t k_bitfield(uint32_t seed)
{
	uint32_t x = seed ^ 0xdeadbeefu;
	uint32_t e = (x >> 5) & 0x3fu;
	uint32_t ins = (x & ~(0x1ffu << 7)) | ((seed & 0x1ffu) << 7);
	int32_t s = ksext(x >> 11, 21);
	int32_t t = ksext(seed, 13);

	return kmix(kmix(e, ins), (uint32_t)s + (uint32_t)t);
}

uint32_t k_select(uint32_t seed)
{
	uint32_t a = seed ^ 0x33333333u, b = krotl(seed, 9) + 11u;
	uint32_t lo = a < b ? a : b, hi = a > b ? a : b;
	uint32_t ad = kabsdiff(a, b);

	return kmix(kmix(lo, hi), ad) ^ (a == b ? 0x55555555u : 0xaaaaaaaau);
}

uint32_t k_clz(uint32_t seed)
{
	uint32_t a = kclz(seed | 1u);
	uint32_t b = (uint32_t)__builtin_ctz(seed | 0x80000000u);
	uint32_t c = kpop(seed ^ 0xffff0000u);

	return kmix(kmix(a, b), c);
}

uint32_t k_switch_dense(uint32_t seed)
{
	uint32_t x = seed & 15u, r;

	switch (x) {
	case 0: r = seed + 1u; break;
	case 1: r = seed ^ 0x11111111u; break;
	case 2: r = seed << 3; break;
	case 3: r = seed >> 2; break;
	case 4: r = seed * 5u; break;
	case 5: r = seed - 7u; break;
	case 6: r = ~seed; break;
	case 7: r = krotl(seed, 7); break;
	case 8: r = seed + 0x80000000u; break;
	case 9: r = seed & 0x00ff00ffu; break;
	case 10: r = seed | 0xff00ff00u; break;
	case 11: r = seed ^ 0x5a5a5a5au; break;
	case 12: r = seed / 3u; break;
	case 13: r = seed % 7u; break;
	case 14: r = seed * seed; break;
	default: r = 0xdeadbeefu ^ seed; break;
	}
	return kmix(r, x);
}

uint32_t k_switch_sparse(uint32_t seed)
{
	uint32_t r;

	switch (seed & 0x7ffu) {
	case 0: r = seed + 0x1000u; break;
	case 17: r = seed ^ 0x2000u; break;
	case 33: r = seed * 33u; break;
	case 129: r = ~seed; break;
	case 511: r = krotl(seed, 11); break;
	case 777: r = seed >> 5; break;
	case 1023: r = seed << 9; break;
	case 2047: r = seed - 0x4000u; break;
	default: r = seed + 0x55u; break;
	}
	return kmix(r, seed >> 16);
}

static uint32_t rfib(uint32_t n)
{
	if (n < 2)
		return n;
	return rfib(n - 1) + rfib(n - 2);
}

uint32_t k_recursion(uint32_t seed)
{
	uint32_t n = seed & 15u;
	uint32_t f = rfib(n);
	uint32_t g = 1;

	while (n)
		g *= n--;
	return kmix(f, g);
}

uint32_t k_saturate(uint32_t seed)
{
	int32_t a = (int32_t)(seed & 0xffffu) - 32768;
	int32_t b = (int32_t)((seed >> 16) & 0xffffu) - 32768;
	int32_t s = (int32_t)((int64_t)a + b);
	int32_t d = (int32_t)((int64_t)a - b);

	return kmix((uint32_t)(int32_t)ksat16(s), (uint32_t)(int32_t)ksat16(d)) ^ seed;
}

uint32_t k_absdiff(uint32_t seed)
{
	int32_t a = (int32_t)seed, b = (int32_t)krotl(seed, 12);
	uint32_t ad = kabsdiff((uint32_t)a, (uint32_t)b);
	uint32_t neg = ((uint32_t)a ^ ((uint32_t)a >> 31)) - ((uint32_t)a >> 31);

	return kmix(ad, neg) ^ (uint32_t)(a < b);
}

uint32_t k_signext(uint32_t seed)
{
	uint8_t bytes[8];
	int32_t s8 = 0, s16 = 0;
	unsigned i;

	for (i = 0; i < 8; i++)
		bytes[i] = (uint8_t)(seed >> (i * 4));
	for (i = 0; i < 8; i++) {
		s8 += (int8_t)bytes[i];
		s16 += (int16_t)((uint16_t)bytes[i] | ((uint16_t)bytes[(i + 1) & 7] << 8));
	}
	return kmix((uint32_t)s8, (uint32_t)s16) ^ seed;
}
