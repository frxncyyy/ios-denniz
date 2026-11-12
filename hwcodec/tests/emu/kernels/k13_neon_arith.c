/* SPDX-License-Identifier: MIT */
/* Isolated NEON multiply / widen / compare-select kernels, matching the operations that the
 * larger k_i8/k_i16/k_neon kernels use, so a failure names one instruction family. */
#include "kcommon.h"

uint32_t k_neon_vmul(uint32_t seed)
{
	int32_t a[32], b[32], o[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (int32_t)((seed >> (i & 15)) ^ (i * 0x101u));
		b[i] = (int32_t)((seed + i * 0x1111u) >> 2);
	}
	for (i = 0; i < 32; i++)
		o[i] = (int32_t)((uint32_t)a[i] * (uint32_t)b[i]);
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint32_t)o[i]);
	return h;
}

uint32_t k_neon_vmull(uint32_t seed)
{
	int16_t a[32], b[32];
	int32_t o[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (int16_t)((seed >> (i & 15)) + i * 7u);
		b[i] = (int16_t)((seed + i * 0x31u) >> 2);
	}
	for (i = 0; i < 32; i++)
		o[i] = (int32_t)a[i] * (int32_t)b[i];
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint32_t)o[i]);
	return h;
}

uint32_t k_neon_vmla(uint32_t seed)
{
	int32_t a[32], b[32], acc[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (int32_t)((seed >> (i & 15)) ^ (i * 3u));
		b[i] = (int32_t)((seed + i * 0x101u) >> 3);
		acc[i] = (int32_t)(i * 0x1001u);
	}
	for (i = 0; i < 32; i++)
		acc[i] = (int32_t)((uint32_t)acc[i] +
				   (uint32_t)a[i] * (uint32_t)b[i]);
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint32_t)acc[i]);
	return h;
}

uint32_t k_neon_vsubl(uint32_t seed)
{
	uint16_t a[32], b[32];
	int32_t o[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (uint16_t)(seed + i * 0x111u);
		b[i] = (uint16_t)((seed >> 1) + i * 0x37u);
	}
	for (i = 0; i < 32; i++)
		o[i] = (int32_t)a[i] - (int32_t)b[i];
	for (i = 0; i < 32; i++)
		h = kmix(h, (uint32_t)o[i]);
	return h;
}

uint32_t k_neon_vcgtbit(uint32_t seed)
{
	uint16_t a[32], b[32];
	uint16_t o[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (uint16_t)(seed + i * 0x101u);
		b[i] = (uint16_t)((seed >> 2) + i * 0x77u);
	}
	for (i = 0; i < 32; i++)
		o[i] = (a[i] > b[i]) ? a[i] : b[i];
	for (i = 0; i < 32; i++)
		h = kmix(h, o[i]);
	return h;
}
