/* SPDX-License-Identifier: MIT */
/*
 * support_arm.c -- compiler runtime for the differential kernels, compiled for ARM and loaded
 * as its own ET_DYN object through the V03 loader.  It implements the mem* family and the
 * 32-bit AEABI division helpers in plain C, without calling itself:
 *   - divisions use a bit-at-a-time shift/subtract loop, never the / operator on 64-bit;
 *   - 64-bit shifts are written on 32-bit halves;
 *   - no builtin calls, no stack protector, no unwind tables in the build.
 *
 * __aeabi_uldivmod / __aeabi_ldivmod are NOT here: their AEABI result is a 128-bit value in
 * r0-r3 and clang cannot express that C return convention on ARM32 (GCC's value_in_regs is
 * unsupported).  tests/emu/diffrun.c supplies those two as host shims; the interpreter still
 * executes the PLT call and the host-call page transition, so the loader path is exercised.
 */
#include <stddef.h>
#include <stdint.h>

static void copy_fwd(void *dst, const void *src, size_t n)
{
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	while (n--)
		*d++ = *s++;
}

static void copy_bwd(void *dst, const void *src, size_t n)
{
	uint8_t *d = (uint8_t *)dst;
	const uint8_t *s = (const uint8_t *)src;

	while (n--) {
		d[n] = s[n];
	}
}

static void fill(void *dst, int c, size_t n)
{
	uint8_t *d = (uint8_t *)dst;

	while (n--)
		*d++ = (uint8_t)c;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	copy_fwd(dst, src, n);
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	if ((uintptr_t)dst < (uintptr_t)src)
		copy_fwd(dst, src, n);
	else if ((uintptr_t)dst > (uintptr_t)src)
		copy_bwd(dst, src, n);
	return dst;
}

void *memset(void *dst, int c, size_t n)
{
	fill(dst, c, n);
	return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;

	while (n--) {
		if (*x != *y)
			return (int)*x - (int)*y;
		x++;
		y++;
	}
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0')
		n++;
	return n;
}

void __aeabi_memcpy(void *dst, const void *src, size_t n) { copy_fwd(dst, src, n); }
void __aeabi_memcpy4(void *dst, const void *src, size_t n) { copy_fwd(dst, src, n); }
void __aeabi_memcpy8(void *dst, const void *src, size_t n) { copy_fwd(dst, src, n); }
void __aeabi_memmove(void *dst, const void *src, size_t n) { (void)memmove(dst, src, n); }
void __aeabi_memmove4(void *dst, const void *src, size_t n) { (void)memmove(dst, src, n); }
void __aeabi_memmove8(void *dst, const void *src, size_t n) { (void)memmove(dst, src, n); }
void __aeabi_memset(void *dst, size_t n, int c) { fill(dst, c, n); }
void __aeabi_memset4(void *dst, size_t n, int c) { fill(dst, c, n); }
void __aeabi_memset8(void *dst, size_t n, int c) { fill(dst, c, n); }
void __aeabi_memclr(void *dst, size_t n) { fill(dst, 0, n); }
void __aeabi_memclr4(void *dst, size_t n) { fill(dst, 0, n); }
void __aeabi_memclr8(void *dst, size_t n) { fill(dst, 0, n); }

static uint32_t udiv32(uint32_t n, uint32_t d, uint32_t *rem)
{
	uint32_t q = 0, r = 0;
	int i;

	if (d == 0) {                 /* ARM UDIV by zero yields 0; AEABI leaves it to the runtime */
		if (rem)
			*rem = 0;
		return 0;
	}
	for (i = 31; i >= 0; i--) {
		uint32_t carry = r >> 31;

		r = (r << 1) | ((n >> i) & 1u);
		if (carry || r >= d) {
			r -= d;               /* when carry is set this is the low half of
						   (2^32 + r) - d, which fits for d > 2^31 */
			q |= (uint32_t)1 << i;
		}
	}
	if (rem)
		*rem = r;
	return q;
}

uint32_t __aeabi_uidiv(uint32_t n, uint32_t d)
{
	return udiv32(n, d, NULL);
}

/* AEABI: quotient in r0, remainder in r1 -- a little-endian uint64_t return does exactly that. */
uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d)
{
	uint32_t r, q = udiv32(n, d, &r);

	return (uint64_t)q | ((uint64_t)r << 32);
}

uint64_t __aeabi_idivmod(int32_t n, int32_t d)
{
	uint32_t un = n < 0 ? 0u - (uint32_t)n : (uint32_t)n;
	uint32_t ud = d < 0 ? 0u - (uint32_t)d : (uint32_t)d;
	uint32_t ur, uq = udiv32(un, ud, &ur);
	uint32_t sq, sr;

	if ((n < 0) != (d < 0))
		sq = 0u - uq;
	else
		sq = uq;
	sr = n < 0 ? 0u - ur : ur;
	return (uint64_t)sq | ((uint64_t)sr << 32);
}

int32_t __aeabi_idiv(int32_t n, int32_t d)
{
	return (int32_t)(uint32_t)__aeabi_idivmod(n, d);
}

uint64_t __aeabi_llsl(uint64_t v, int n)
{
	uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);

	if (n <= 0)
		return v;
	if (n >= 64)
		return 0;
	if (n >= 32) {
		hi = lo << (n - 32);
		lo = 0;
	} else {
		hi = (hi << n) | (lo >> (32 - n));
		lo <<= n;
	}
	return (uint64_t)lo | ((uint64_t)hi << 32);
}

uint64_t __aeabi_llsr(uint64_t v, int n)
{
	uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);

	if (n <= 0)
		return v;
	if (n >= 64)
		return 0;
	if (n >= 32) {
		lo = hi >> (n - 32);
		hi = 0;
	} else {
		lo = (lo >> n) | (hi << (32 - n));
		hi >>= n;
	}
	return (uint64_t)lo | ((uint64_t)hi << 32);
}

uint64_t __aeabi_lasr(uint64_t v, int n)
{
	uint32_t lo = (uint32_t)v, hi = (uint32_t)(v >> 32);
	uint32_t sign = hi >> 31;

	if (n <= 0)
		return v;
	if (n >= 64)
		return sign ? ~(uint64_t)0 : 0;
	if (n >= 32) {
		lo = (uint32_t)((int32_t)hi >> (n - 32));
		hi = sign ? 0xffffffffu : 0;
	} else {
		lo = (lo >> n) | (hi << (32 - n));
		hi = (hi >> n) | (sign ? (0xffffffffu << (32 - n)) : 0);
	}
	return (uint64_t)lo | ((uint64_t)hi << 32);
}

uint64_t __aeabi_lmul(uint64_t a, uint64_t b)
{
	uint32_t al = (uint32_t)a, ah = (uint32_t)(a >> 32);
	uint32_t bl = (uint32_t)b, bh = (uint32_t)(b >> 32);
	uint64_t lo = (uint64_t)al * bl;
	uint64_t mid = (uint64_t)al * bh + (uint64_t)ah * bl;

	return lo + (mid << 32);
}
