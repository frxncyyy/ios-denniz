/* SPDX-License-Identifier: MIT */
/* Small int8/int16/uint8 loops written so that -O3 -ftree-vectorize can use NEON on ARMv7
 * and SSE2 on x86-64.  Every value written back is saturated with explicit comparisons, so
 * the scalar and vector results are identical. */
#include "kcommon.h"

uint32_t k_i8_loop(uint32_t seed)
{
	int8_t a[64], b[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 64; i++) {
		a[i] = (int8_t)((seed >> (i & 15)) + i * 5u);
		b[i] = (int8_t)((seed ^ (i * 0x21u)) >> 3);
	}
	for (i = 0; i < 64; i++)
		a[i] = ksat8((int32_t)a[i] * 3 + b[i]);
	for (i = 0; i < 64; i++)
		h = kmix(h, (uint8_t)a[i]) ^ (uint32_t)(uint8_t)b[i];
	return h;
}

uint32_t k_i16_loop(uint32_t seed)
{
	int16_t a[48], b[48];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 48; i++) {
		a[i] = (int16_t)((seed >> (i & 15)) * 3u + i);
		b[i] = (int16_t)((seed + i * 0x111u) >> 2);
	}
	for (i = 0; i < 48; i++)
		a[i] = ksat16((int32_t)a[i] * 2 - b[i]);
	for (i = 0; i < 48; i++)
		h = kmix(h, (uint16_t)a[i]) + (uint16_t)b[i];
	return h;
}

uint32_t k_u8_sad(uint32_t seed)
{
	uint8_t a[64], b[64];
	uint32_t sad = 0;
	unsigned i;

	for (i = 0; i < 64; i++) {
		a[i] = (uint8_t)(seed >> (i & 7));
		b[i] = (uint8_t)((seed >> ((i + 3) & 7)) ^ i);
	}
	for (i = 0; i < 64; i++)
		sad += kabsdiff(a[i], b[i]);
	return kmix(seed, sad);
}

uint32_t k_i8_pack(uint32_t seed)
{
	int32_t t[32];
	int8_t p[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++)
		t[i] = (int32_t)(seed * (i + 1u)) >> 4;
	for (i = 0; i < 32; i++)
		p[i] = ksat8(t[i]);
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint8_t)p[i]);
	return h;
}

uint32_t k_i16_mul(uint32_t seed)
{
	int16_t a[40], b[40], c[40];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 40; i++) {
		a[i] = (int16_t)((seed >> (i & 15)) ^ (i * 31u));
		b[i] = (int16_t)((seed + i * 97u) >> 1);
	}
	for (i = 0; i < 40; i++)
		c[i] = ksat16(((int32_t)a[i] * b[i]) >> 8);
	for (i = 0; i < 40; i++)
		h = kmix(h, (uint16_t)c[i]);
	return h;
}

uint32_t k_u8_memcpy(uint32_t seed)
{
	uint8_t a[96], b[96];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 96; i++)
		a[i] = (uint8_t)(seed + i * 7u);
	for (i = 0; i < 96; i++)
		b[i] = a[i] ^ (uint8_t)(i * 3u);
	for (i = 0; i < 96; i++)
		a[i] = b[95 - i];
	for (i = 0; i < 96; i++)
		h = kmix(h, a[i]);
	return h;
}
