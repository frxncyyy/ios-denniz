/* SPDX-License-Identifier: MIT */
/* Memory kernels: byte/word/halfword loops, overlapping copies, string scans, stack traffic.
 * The compiler may turn some loops into memcpy/memset calls; tests/emu/support_arm.c provides
 * them.  The k_call_* kernels import them explicitly, so the V03 cross-object lookup and the
 * interpreted support code are covered too. */
#include "kcommon.h"
#include <stddef.h>

extern void *memcpy(void *dst, const void *src, size_t n);
extern void *memmove(void *dst, const void *src, size_t n);
extern void *memset(void *dst, int c, size_t n);
extern size_t strlen(const char *s);

uint32_t k_call_memcpy(uint32_t seed)
{
	uint8_t a[40], b[40];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < sizeof(a); i++)
		a[i] = (uint8_t)(seed >> (i & 7)) + (uint8_t)i;
	memcpy(b, a, sizeof(a));
	memcpy(b + 8, a + 4, 16);
	for (i = 0; i < sizeof(b); i++)
		h = kmix(h, b[i] ^ a[i]);
	return h;
}

uint32_t k_call_memmove(uint32_t seed)
{
	uint8_t a[48];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < sizeof(a); i++)
		a[i] = (uint8_t)(seed + i * 5u);
	memmove(a + 4, a, 24);
	for (i = 0; i < sizeof(a); i++)
		h = kmix(h, a[i]);
	return h;
}

uint32_t k_call_memset(uint32_t seed)
{
	uint8_t a[48];
	uint32_t h = seed;
	unsigned i;

	memset(a, (int)(seed & 0xffu), sizeof(a));
	memset(a + 8, 0x5a, 16);
	for (i = 0; i < sizeof(a); i++)
		h = kmix(h, a[i]);
	return h;
}

uint32_t k_call_strlen(uint32_t seed)
{
	char s[40];
	unsigned i, n = seed % 32u;

	for (i = 0; i < n; i++)
		s[i] = (char)('A' + (i % 26u));
	s[n] = '\0';
	return kmix(seed, (uint32_t)strlen(s));
}

uint32_t k_memcpy_loop(uint32_t seed)
{
	uint8_t src[64], dst[64];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < sizeof(src); i++)
		src[i] = (uint8_t)(seed >> (i & 7)) ^ (uint8_t)i;
	for (i = 0; i < sizeof(dst); i++)
		dst[i] = 0;
	for (i = 0; i < sizeof(src); i++)
		dst[i] = src[i];
	for (i = 0; i < sizeof(dst); i++)
		h = kmix(h, dst[i]);
	return h;
}

uint32_t k_memmove_loop(uint32_t seed)
{
	uint8_t a[48];
	unsigned i;

	for (i = 0; i < sizeof(a); i++)
		a[i] = (uint8_t)(seed + i * 3u);
	for (i = 0; i + 8 < sizeof(a); i++)
		a[i + 8] = a[i];
	for (i = 0; i + 8 < sizeof(a); i++)
		a[i] = a[i + 8];
	{
		uint32_t h = seed;
		for (i = 0; i < sizeof(a); i++)
			h = kmix(h, a[i]);
		return h;
	}
}

uint32_t k_memset_loop(uint32_t seed)
{
	uint16_t a[40];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 40; i++)
		a[i] = (uint16_t)(seed ^ (i * 0x101u));
	for (i = 0; i < 40; i += 3)
		a[i] = (uint16_t)(seed + i);
	for (i = 0; i < 40; i++)
		h = kmix(h, a[i]);
	return h;
}

uint32_t k_strlen_loop(uint32_t seed)
{
	char s[56];
	unsigned n = seed % 48u, i, len = 0;
	uint32_t h = seed;

	for (i = 0; i < n; i++)
		s[i] = (char)('a' + (i % 26u));
	s[n] = '\0';
	while (s[len] != '\0')
		len++;
	for (i = 0; i < len; i++)
		h = kmix(h, (uint8_t)s[i]);
	return kmix(h, len);
}

uint32_t k_strcmp_loop(uint32_t seed)
{
	char a[24], b[24];
	unsigned i, n = seed % 20u;
	uint32_t h = seed;

	for (i = 0; i < n; i++) {
		a[i] = (char)('A' + (i % 13u));
		b[i] = a[i];
	}
	a[n] = b[n] = '\0';
	if (seed & 1u)
		b[n / 2] = (char)(b[n / 2] + 1);
	i = 0;
	while (a[i] && a[i] == b[i])
		i++;
	h = kmix(h, (uint32_t)(unsigned char)a[i] - (uint32_t)(unsigned char)b[i]);
	return kmix(h, i);
}

uint32_t k_array_sum(uint32_t seed)
{
	int32_t a[32];
	uint32_t sum = 0;
	int32_t mn = 0x7fffffff, mx = -0x7fffffff - 1;
	unsigned i;

	for (i = 0; i < 32; i++) {
		a[i] = (int32_t)(seed * (i + 1u)) >> 3;
		sum += (uint32_t)a[i];
		if (a[i] < mn)
			mn = a[i];
		if (a[i] > mx)
			mx = a[i];
	}
	return kmix((uint32_t)sum, (uint32_t)mn ^ (uint32_t)mx);
}

uint32_t k_array_reverse(uint32_t seed)
{
	int16_t a[24];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 24; i++)
		a[i] = (int16_t)((seed >> (i & 15)) ^ (i * 0x111u));
	for (i = 0; i < 12; i++) {
		int16_t t = a[i];
		a[i] = a[23 - i];
		a[23 - i] = t;
	}
	for (i = 0; i < 24; i++)
		h = kmix(h, (uint16_t)a[i]);
	return h;
}

struct pair8 {
	uint32_t a[8];
	uint16_t b[8];
};

uint32_t k_struct_copy(uint32_t seed)
{
	struct pair8 p, q;
	unsigned i;
	uint32_t h = seed;

	for (i = 0; i < 8; i++) {
		p.a[i] = seed + i * 0x01010101u;
		p.b[i] = (uint16_t)(seed >> i);
	}
	q = p;
	for (i = 0; i < 8; i++) {
		q.a[i] ^= q.b[7 - i];
		h = kmix(h, q.a[i]);
	}
	return h;
}

uint32_t k_byte_load(uint32_t seed)
{
	uint8_t b[16];
	uint32_t w[4] = { 0, 0, 0, 0 };
	unsigned i;

	for (i = 0; i < 16; i++)
		b[i] = (uint8_t)(seed >> ((i & 3) * 8)) + (uint8_t)i;
	for (i = 0; i < 16; i++)
		w[i >> 2] |= (uint32_t)b[i] << ((i & 3) * 8);
	return kmix(w[0] ^ w[1], w[2] + w[3]);
}

uint32_t k_stack_spill(uint32_t seed)
{
	volatile uint32_t v[32];
	uint32_t h = seed;
	unsigned i;

	for (i = 0; i < 32; i++)
		v[i] = seed ^ (i * 0x9e3779b9u);
	for (i = 1; i < 31; i++)
		v[i] = v[i - 1] + v[i + 1];
	for (i = 0; i < 32; i++)
		h = kmix(h, v[i]);
	return h;
}
