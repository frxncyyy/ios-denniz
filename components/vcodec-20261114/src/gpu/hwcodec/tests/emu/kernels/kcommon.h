/* SPDX-License-Identifier: MIT */
/*
 * Helpers shared by the differential kernels.  Every kernel is a pure function of its seed:
 * no globals, no libc, only types with an exact width.  The reductions below are written so
 * that they are well defined for every uint32_t input on both ARM and x86-64:
 *   - shifts are masked to 0..31 (never by the width);
 *   - signed division is guarded against zero and INT32_MIN/-1;
 *   - sign extension uses ((v ^ m) - m), not an arithmetic shift of a negative value;
 *   - float -> integer conversions are range checked first.
 */
#ifndef KCOMMON_H
#define KCOMMON_H

#include <stdint.h>

static inline uint32_t kmix(uint32_t h, uint32_t v)
{
	h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
	return h;
}

static inline uint32_t kmix64(uint32_t h, uint64_t v)
{
	return kmix(kmix(h, (uint32_t)v), (uint32_t)(v >> 32));
}

static inline uint32_t krotl(uint32_t x, unsigned n)
{
	n &= 31u;
	return n ? (x << n) | (x >> (32u - n)) : x;
}

static inline uint32_t kclz(uint32_t x)
{
	uint32_t n = 0;

	if (!x)
		return 32;
	while (!(x & 0x80000000u)) {
		x <<= 1;
		n++;
	}
	return n;
}

static inline uint32_t kpop(uint32_t x)
{
	uint32_t n = 0;

	while (x) {
		x &= x - 1u;
		n++;
	}
	return n;
}

static inline int32_t ksext(uint32_t v, unsigned bits)
{
	uint32_t m = 1u << (bits - 1u);

	return (int32_t)((v ^ m) - m);
}

static inline uint32_t kabsdiff(uint32_t a, uint32_t b)
{
	return a >= b ? a - b : b - a;
}

static inline int32_t ksat16(int32_t v)
{
	if (v > 32767)
		return 32767;
	if (v < -32768)
		return -32768;
	return v;
}

static inline int8_t ksat8(int32_t v)
{
	if (v > 127)
		return 127;
	if (v < -128)
		return -128;
	return (int8_t)v;
}

static inline uint32_t kfbits(float f)
{
	union { float f; uint32_t u; } x;

	x.f = f;
	return x.u;
}

static inline uint32_t kdbits(double d)
{
	union { double d; uint64_t u; } x;

	x.d = d;
	return (uint32_t)x.u ^ (uint32_t)(x.u >> 32);
}

static inline float kf(uint32_t u)
{
	union { float f; uint32_t u; } x;

	x.u = u;
	return x.f;
}

static inline double kd(uint64_t u)
{
	union { double d; uint64_t u; } x;

	x.u = u;
	return x.d;
}

#endif
