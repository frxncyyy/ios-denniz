/* SPDX-License-Identifier: MIT */
/* Table-driven and branchy kernels: CRC, byte hash, matrix multiply, binary search,
 * reductions.  The read-only tables exercise PC-relative literal loads and .rodata mapping. */
#include "kcommon.h"

uint32_t k_crc32(uint32_t seed)
{
	uint8_t b[24];
	uint32_t crc = 0xffffffffu;
	unsigned i, k;

	for (i = 0; i < 24; i++)
		b[i] = (uint8_t)(seed >> ((i & 3) * 8)) ^ (uint8_t)(i * 13u);
	for (i = 0; i < 24; i++) {
		crc ^= b[i];
		for (k = 0; k < 8; k++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return crc ^ seed;
}

uint32_t k_hash_bytes(uint32_t seed)
{
	uint8_t b[40];
	uint32_t h = 2166136261u;
	unsigned i;

	for (i = 0; i < 40; i++)
		b[i] = (uint8_t)(seed + i * 0x9d);
	for (i = 0; i < 40; i++) {
		h ^= b[i];
		h *= 16777619u;
	}
	return h;
}

uint32_t k_matrix(uint32_t seed)
{
	int32_t a[9], b[9], c[9];
	uint32_t h = seed;
	unsigned i, j, k;

	for (i = 0; i < 9; i++) {
		a[i] = (int32_t)((seed >> (i & 15)) & 0xffu) - 128;
		b[i] = (int32_t)((seed + i * 41u) & 0x7fu) - 64;
	}
	for (i = 0; i < 3; i++) {
		for (j = 0; j < 3; j++) {
			int32_t s = 0;
			for (k = 0; k < 3; k++)
				s += a[i * 3 + k] * b[k * 3 + j];
			c[i * 3 + j] = s;
		}
	}
	for (i = 0; i < 9; i++)
		h = kmix(h, (uint32_t)c[i]);
	return h;
}

static const uint16_t sorted[32] = {
	2, 5, 9, 14, 20, 27, 35, 44, 54, 65, 77, 90, 104, 119, 135, 152,
	170, 189, 209, 230, 252, 275, 299, 324, 350, 377, 405, 434, 464, 495, 527, 560
};

uint32_t k_branch(uint32_t seed)
{
	uint32_t key = seed & 0x3ffu;
	int lo = 0, hi = 31, idx = -1;

	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (sorted[mid] == key) {
			idx = mid;
			break;
		}
		if (sorted[mid] < key)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return kmix((uint32_t)idx, key) ^ (uint32_t)(lo + hi);
}

uint32_t k_parity(uint32_t seed)
{
	uint32_t x = seed ^ 0xaaaaaaaa;
	uint32_t p = x ^ (x >> 16);

	p ^= p >> 8;
	p ^= p >> 4;
	p &= 0xfu;
	return kmix(p, seed) ^ (0x6996u >> p & 1u);
}

uint32_t k_bitreverse(uint32_t seed)
{
	uint32_t x = seed, r = 0;
	unsigned i;

	for (i = 0; i < 32; i++) {
		r = (r << 1) | (x & 1u);
		x >>= 1;
	}
	return kmix(r, seed);
}

uint32_t k_countbits(uint32_t seed)
{
	uint32_t w[4], n = 0;
	unsigned i;

	for (i = 0; i < 4; i++)
		w[i] = seed ^ (i * 0x11111111u);
	for (i = 0; i < 4; i++)
		n += kpop(w[i]);
	return kmix(n, seed ^ w[0]);
}

uint32_t k_bswap16(uint32_t seed)
{
	uint16_t a = (uint16_t)seed, b = (uint16_t)(seed >> 16);
	uint16_t c = __builtin_bswap16(a), d = __builtin_bswap16(b);

	return kmix((uint32_t)c << 16 | d, a + b);
}
