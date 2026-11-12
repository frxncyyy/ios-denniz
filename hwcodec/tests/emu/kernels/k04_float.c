/* SPDX-License-Identifier: MIT */
/* VFP scalar: float/double arithmetic, conversions, comparisons and rounding edges.
 * Built with -ffp-contract=off for both targets, so every operation rounds separately and
 * x86-64 SSE2 and ARM VFPv4 produce the same IEEE-754 results.  Conversions to integers are
 * range checked; NaN is created from its bit pattern, never by 0/0. */
#include "kcommon.h"

uint32_t k_faddsub(uint32_t seed)
{
	float a = (float)(int32_t)(seed & 0xffffu) * 0.5f;
	float b = (float)(int32_t)((seed >> 16) & 0xffffu) * 0.25f + 1.0f;
	float s = a + b, d = a - b, p = a * b;

	return kmix(kmix(kfbits(s), kfbits(d)), kfbits(p));
}

uint32_t k_fmuldiv(uint32_t seed)
{
	float a = (float)(int32_t)(seed & 0x7fffu) + 1.0f;
	float b = (float)((seed >> 15) & 0xffu) + 3.0f;
	float q = a / b, r = b / a;

	return kmix(kfbits(q), kfbits(r)) ^ kfbits(a * 0.125f);
}

uint32_t k_fconv(uint32_t seed)
{
	float f = (float)(int32_t)(seed & 0xffffffu);
	uint32_t u = (uint32_t)(f * 0.5f);
	int32_t i = (int32_t)(f - 8388608.0f);
	double d = (double)f * 1.00000011920928955078125;
	float g = (float)d;

	return kmix(kmix(u, (uint32_t)i), kmix(kfbits(f), kfbits(g)));
}

uint32_t k_daddsub(uint32_t seed)
{
	double a = (double)(int32_t)(seed & 0xffffffu) * 0.5;
	double b = (double)(int32_t)((seed >> 8) & 0xffffffu) * 0.25 + 0.125;
	double s = a + b, d = a - b, p = a * b;

	return kmix(kmix(kdbits(s), kdbits(d)), kdbits(p));
}

uint32_t k_dmuldiv(uint32_t seed)
{
	double a = (double)(int32_t)(seed & 0xffffu) + 1.5;
	double b = (double)((seed >> 16) & 0xffu) + 2.25;
	double q = a / b, r = b / a;

	return kmix(kdbits(q), kdbits(r)) ^ kdbits(a * 0.0625);
}

uint32_t k_dconv(uint32_t seed)
{
	double d = (double)(int32_t)(seed & 0xffffffu) - 8388608.0;
	int32_t i = (int32_t)d;
	uint32_t u = (uint32_t)(d + 8388608.0);
	float f = (float)d;
	double back = (double)f;

	return kmix(kmix((uint32_t)i, u), kmix(kfbits(f), kdbits(back)));
}

uint32_t k_fcompare(uint32_t seed)
{
	float a = (float)(int32_t)(seed & 0xffffu);
	float b = (float)(int32_t)((seed >> 16) & 0xffffu);
	float nan = kf(0x7fc00000u);
	uint32_t h = seed;

	h = kmix(h, (uint32_t)(a < b));
	h = kmix(h, (uint32_t)(a <= b));
	h = kmix(h, (uint32_t)(a == b));
	h = kmix(h, (uint32_t)(a > b));
	h = kmix(h, (uint32_t)(a != nan));
	h = kmix(h, (uint32_t)(nan < a));
	h = kmix(h, (uint32_t)(nan == nan));
	return h;
}

uint32_t k_fround(uint32_t seed)
{
	float f = (float)(int32_t)(seed & 0xffffu) * 0.5f;
	float g = f + 0.5f;
	int32_t i = (int32_t)g;
	float r1 = (float)(0x01000001u + (seed & 3u));
	double d1 = (double)16777217u + (double)(seed & 1u);
	float r2 = (float)d1;
	float r3 = (float)(int32_t)((seed >> 4) & 0xffffu) * 0.1f;

	return kmix(kmix((uint32_t)i, kfbits(r1)), kmix(kfbits(r2), kfbits(r3)));
}

uint32_t k_fvec_loop(uint32_t seed)
{
	float a[32], b[32], c[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (float)(int32_t)((seed >> (i & 15)) & 0xffffu) * 0.03125f;
		b[i] = (float)(int32_t)(i * 7u + (seed & 0xffu)) * 0.5f;
		c[i] = (float)(int32_t)((seed ^ (i * 0x101u)) & 0xffu) + 1.0f;
	}
	for (i = 0; i < 32; i++)
		a[i] = a[i] * b[i] + c[i];
	for (i = 0; i < 32; i++)
		h = kmix(h, kfbits(a[i]));
	return h;
}

uint32_t k_dvec_loop(uint32_t seed)
{
	double a[24], b[24];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 24; i++) {
		a[i] = (double)(int32_t)((seed >> (i & 15)) & 0xffffu) * 0.125;
		b[i] = (double)(int32_t)(i * 3u + 1u) * 0.25;
	}
	for (i = 0; i < 24; i++)
		a[i] = a[i] * 0.5 + b[i];
	for (i = 0; i < 24; i++)
		h = kmix(h, kdbits(a[i]));
	return h;
}

uint32_t k_fminmax(uint32_t seed)
{
	float a = (float)(int32_t)(seed & 0xffffu) * 0.25f;
	float b = (float)(int32_t)((seed >> 16) & 0xffffu) * 0.5f;
	float mn = a < b ? a : b;
	float mx = a > b ? a : b;
	float absa = a < 0.0f ? -a : a;

	return kmix(kmix(kfbits(mn), kfbits(mx)), kfbits(absa));
}
