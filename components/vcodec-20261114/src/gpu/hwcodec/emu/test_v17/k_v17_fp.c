/* SPDX-License-Identifier: MIT */
/*
 * k_v17_fp.c -- differential kernels for the V17 VFP scalar class.
 *
 * Self-contained (no kcommon.h) so this file can be dropped next to T01's kernels; every helper is
 * static and every external name is k17_*.  Pure functions of the seed, no globals, no libc,
 * exact-width types.  Float/double results are reduced as bit patterns; all float->integer
 * conversions are range checked first (the ARM result for out-of-range is a saturation value, and
 * the C result would be undefined, so the kernels never form one).
 *
 * Compile with T01's flags (-ffp-contract=off): the arithmetic kernels then produce VADD/VSUB/
 * VMUL/VDIV/VCVT/VCMP/VMOV sequences, not fused forms.  Fused VFMA, VSQRT, VSEL, VRINT, VMAXNM,
 * VCVT fixed-point and the half-precision forms cannot be requested portably from C; they are
 * covered by emu/test_v17/t_vfp.c and by the llvm-mc asm snippet test_v17/vfp_demo.s.
 */
#include <stdint.h>

static uint32_t k17_mix(uint32_t h, uint32_t v)
{
	h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
	return h;
}

static uint32_t k17_mix64(uint32_t h, uint64_t v)
{
	return k17_mix(k17_mix(h, (uint32_t)v), (uint32_t)(v >> 32));
}

static uint32_t k17_fbits(float f)
{
	union { float f; uint32_t u; } x;
	x.f = f;
	return x.u;
}

static uint64_t k17_dbits(double d)
{
	union { double d; uint64_t u; } x;
	x.d = d;
	return x.u;
}

uint32_t k_v17_f32_arith(uint32_t seed)
{
	float a = (float)(int32_t)(seed & 0xffffu) * 0.5f + 1.0f;
	float b = (float)(int32_t)((seed >> 16) & 0xffffu) * 0.25f + 2.0f;
	float s = a + b, d = a - b, p = a * b, q = a / b;

	return k17_mix(k17_mix(k17_fbits(s), k17_fbits(d)), k17_mix(k17_fbits(p), k17_fbits(q)));
}

uint32_t k_v17_f32_chain(uint32_t seed)
{
	float x = (float)(seed & 0xffu) * 0.03125f;
	float y = 1.0f;
	unsigned i;

	for (i = 0; i < 8; i++) {
		y = y * 0.5f + x;
		x = x * 0.75f - y * 0.125f;
	}
	return k17_mix(k17_fbits(x), k17_fbits(y));
}

uint32_t k_v17_f64_arith(uint32_t seed)
{
	double a = (double)(int32_t)(seed & 0xffffu) * 0.5 + 1.0;
	double b = (double)(int32_t)((seed >> 16) & 0xffffu) * 0.25 + 2.0;
	double s = a + b, d = a - b, p = a * b, q = a / b;

	return k17_mix64(k17_mix64(k17_mix64(k17_dbits(s), k17_dbits(d)),
				   k17_dbits(p)), k17_dbits(q));
}

uint32_t k_v17_f64_chain(uint32_t seed)
{
	double x = (double)(seed & 0xffu) * 0.03125;
	double y = 1.0;
	unsigned i;

	for (i = 0; i < 8; i++) {
		y = y * 0.5 + x;
		x = x * 0.75 - y * 0.125;
	}
	return k17_mix64(k17_dbits(x), k17_dbits(y));
}

uint32_t k_v17_cvt(uint32_t seed)
{
	float f = (float)(int32_t)(seed & 0xffffffu);
	double d = (double)f * 1.00000011920928955078125;
	float g = (float)(d * 0.5);
	int32_t i;

	if (f >= 0.0f && f < 8388608.0f)
		i = (int32_t)f;
	else
		i = -1;
	return k17_mix(k17_mix(k17_fbits(f), k17_fbits(g)), (uint32_t)i);
}

uint32_t k_v17_cmp(uint32_t seed)
{
	float a = (float)(int32_t)(seed & 0xffffu) - 32768.0f;
	float b = (float)(int32_t)((seed >> 16) & 0xffffu) - 32768.0f;
	uint32_t r = 0;

	if (a < b) r |= 1u;
	if (a == b) r |= 2u;
	if (a > b) r |= 4u;
	if (!(a < b)) r |= 8u;
	return k17_mix(r, k17_fbits(a < b ? a : b));
}

uint32_t k_v17_dcmp(uint32_t seed)
{
	double a = (double)(int32_t)(seed & 0xffffu) - 32768.0;
	double b = (double)(int32_t)((seed >> 16) & 0xffffu) - 32768.0;
	uint32_t r = 0;

	if (a <= b) r |= 1u;
	if (a >= b) r |= 2u;
	if (a != b) r |= 4u;
	if (a < b) r |= 8u;
	return k17_mix(r, k17_mix64(k17_dbits(a + b), k17_dbits(a - b)));
}

uint32_t k_v17_negabs(uint32_t seed)
{
	float f = (float)(int32_t)(seed & 0xffffu) * 0.25f - 8192.0f;
	float n = -f, s = f < 0.0f ? -f : f;

	return k17_mix(k17_fbits(n), k17_fbits(s));
}

uint32_t k_v17_int_to_fp(uint32_t seed)
{
	int32_t i = (int32_t)seed;
	uint32_t u = seed >> 1;
	float f = (float)i;
	double d = (double)u;
	float g = (float)d;

	return k17_mix(k17_mix(k17_fbits(f), k17_fbits(g)), (uint32_t)(int32_t)d);
}
