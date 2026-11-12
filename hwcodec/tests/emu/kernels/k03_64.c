/* SPDX-License-Identifier: MIT */
/* 64-bit arithmetic, comparisons, shifts and division.  The native / and % on uint64_t
 * deliberately exercise the compiler runtime import (__aeabi_uldivmod); the *_manual
 * kernels implement the same long division in interpreted code, without that helper. */
#include "kcommon.h"

uint32_t k_add64(uint32_t seed)
{
	uint64_t a = ((uint64_t)seed << 32) | krotl(seed, 5);
	uint64_t b = ((uint64_t)(seed ^ 0xa5a5a5a5u) << 29) | (seed + 17u);
	uint64_t s = a + b, d = a - b;
	uint64_t c = (a < b) ? b : a;

	return kmix64(kmix64(s, d), c);
}

uint32_t k_mul64p(uint32_t seed)
{
	uint64_t a = ((uint64_t)seed << 17) | (seed ^ 0x1234u);
	uint64_t b = ((uint64_t)krotl(seed, 3) << 32) | (seed | 1u);
	uint64_t p = a * b;
	int64_t sp = (int64_t)a * (int64_t)b;

	return kmix64(kmix64(p, (uint64_t)sp), a ^ b);
}

uint32_t k_udiv32(uint32_t seed)
{
	uint32_t d = (seed & 0xffffu) | 1u;
	uint32_t q = seed / d, r = seed % d;

	return kmix(q, r) ^ d;
}

uint32_t k_sdiv32(uint32_t seed)
{
	int32_t a = (int32_t)seed;
	int32_t d = (int32_t)((seed >> 16) | 1u);
	int32_t q, r;

	if (a == (-2147483647 - 1) && d == -1)
		d = 1;
	q = a / d;
	r = a % d;
	return kmix((uint32_t)q, (uint32_t)r) ^ (uint32_t)d;
}

uint32_t k_udiv64(uint32_t seed)
{
	uint64_t n = ((uint64_t)seed << 32) | krotl(seed, 9);
	uint64_t d = ((uint64_t)(seed | 1u) << 1) | 1u;
	uint64_t q = n / d, r = n % d;

	return kmix64(kmix64(q, r), n ^ d);
}

uint32_t k_sdiv64(uint32_t seed)
{
	int64_t n = (int64_t)(((uint64_t)seed << 32) | krotl(seed, 9));
	int64_t d = (int64_t)(((uint64_t)((seed >> 1) | 1u) << 3) | 1u);
	int64_t q, r;

	if (n == (-9223372036854775807LL - 1) && d == -1)
		d = 1;
	q = n / d;
	r = n % d;
	return kmix64((uint64_t)q, (uint64_t)r) ^ (uint32_t)d;
}

uint32_t k_mod64(uint32_t seed)
{
	uint64_t n = ((uint64_t)krotl(seed, 7) << 32) | (seed ^ 0x55aa55aau);
	uint64_t d = (seed & 0xffffffu) + 3u;

	return kmix64(n % d, n / d);
}

uint32_t k_udiv64_manual(uint32_t seed)
{
	uint64_t n = ((uint64_t)seed << 32) | krotl(seed, 9);
	uint64_t d = ((uint64_t)(seed | 1u) << 1) | 1u;
	uint64_t q = 0, r = 0;
	int i;

	for (i = 63; i >= 0; i--) {
		r = (r << 1) | ((n >> i) & 1u);
		if (r >= d) {
			r -= d;
			q |= (uint64_t)1 << i;
		}
	}
	return kmix64(kmix64(q, r), n ^ d);
}

uint32_t k_shift64(uint32_t seed)
{
	uint64_t x = ((uint64_t)seed << 32) | 0x80000001u;
	unsigned n = seed & 63u, m = (seed >> 8) & 63u;
	uint64_t a = n ? x << n : x;
	uint64_t b = n ? x >> n : x;
	int64_t c = (int64_t)x >> m;

	return kmix64(kmix64(a, b), (uint64_t)c);
}

uint32_t k_cmp64(uint32_t seed)
{
	uint64_t a = ((uint64_t)seed << 32) | krotl(seed, 3);
	uint64_t b = ((uint64_t)(seed ^ 0x80000000u) << 32) | (seed + 5u);
	uint64_t mn = a < b ? a : b;
	uint64_t mx = a > b ? a : b;

	return kmix64(kmix64(mn, mx), (uint64_t)(a == b)) ^ (uint32_t)(a < b);
}

uint32_t k_bswap64(uint32_t seed)
{
	uint64_t x = ((uint64_t)seed << 32) | ~seed;
	uint64_t y = ((uint64_t)__builtin_bswap32((uint32_t)x) << 32) |
		__builtin_bswap32((uint32_t)(x >> 32));

	return kmix64(x, y);
}
