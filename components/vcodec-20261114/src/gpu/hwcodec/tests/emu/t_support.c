/* SPDX-License-Identifier: MIT */
/* Host test for support_arm.c: the same C compiled natively, compared with the C operators.
 * This catches mistakes in the emulated-ARM runtime before the interpreter is involved. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern uint32_t __aeabi_uidiv(uint32_t n, uint32_t d);
extern uint64_t __aeabi_uidivmod(uint32_t n, uint32_t d);
extern int32_t __aeabi_idiv(int32_t n, int32_t d);
extern uint64_t __aeabi_idivmod(int32_t n, int32_t d);
extern uint64_t __aeabi_llsl(uint64_t v, int n);
extern uint64_t __aeabi_llsr(uint64_t v, int n);
extern uint64_t __aeabi_lasr(uint64_t v, int n);
extern uint64_t __aeabi_lmul(uint64_t a, uint64_t b);

static unsigned checks, failures;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { failures++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); putchar('\n'); } \
} while (0)

static uint32_t rng(void)
{
	static uint32_t s = 0x9e3779b9u;

	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

int main(void)
{
	uint32_t i;

	for (i = 0; i < 20000; i++) {
		uint32_t n = rng(), d = rng() | 1u;
		uint64_t qr = __aeabi_uidivmod(n, d);
		int32_t sn = (int32_t)n, sd = (int32_t)d;
		uint64_t sqr;

		CHECK(__aeabi_uidiv(n, d) == n / d, "uidiv %08x/%08x", n, d);
		CHECK((uint32_t)qr == n / d, "uidiv q");
		CHECK((uint32_t)(qr >> 32) == n % d, "uidiv r");
		if (sn == (-2147483647 - 1) && sd == -1)
			sd = 1;
		sqr = __aeabi_idivmod(sn, sd);
		CHECK((uint32_t)__aeabi_idiv(sn, sd) == (uint32_t)(sn / sd), "idiv %d/%d", sn, sd);
		CHECK((uint32_t)sqr == (uint32_t)(sn / sd), "idiv q");
		CHECK((uint32_t)(sqr >> 32) == (uint32_t)(sn % sd), "idiv r");
	}

	for (i = 0; i < 64; i++) {
		uint64_t x = ((uint64_t)rng() << 32) | rng();
		int64_t sx = (int64_t)x;

		CHECK(__aeabi_llsl(x, (int)i) == (i == 64 ? 0 : x << i), "llsl %u", i);
		CHECK(__aeabi_llsr(x, (int)i) == (i == 64 ? 0 : x >> i), "llsr %u", i);
		CHECK(__aeabi_lasr(x, (int)i) == (uint64_t)(sx >> i), "lasr %u", i);
	}

	for (i = 0; i < 10000; i++) {
		uint64_t a = ((uint64_t)rng() << 32) | rng();
		uint64_t b = ((uint64_t)rng() << 32) | rng();

		CHECK(__aeabi_lmul(a, b) == a * b, "lmul");
	}

	{
		uint8_t a[64], b[64];
		unsigned k;

		for (k = 0; k < sizeof(a); k++)
			a[k] = (uint8_t)(k * 7u + 3u);
		memset(b, 0, sizeof(b));
		CHECK(memcpy(b, a, sizeof(a)) == b, "memcpy ret");
		CHECK(memcmp(a, b, sizeof(a)) == 0, "memcpy data");
		memmove(a + 4, a, 20);
		CHECK(a[4] == 3 && a[23] == (uint8_t)(19u * 7u + 3u), "memmove overlap");
		memset(b, 0x5a, 16);
		CHECK(b[0] == 0x5a && b[15] == 0x5a &&
		      b[16] == (uint8_t)(16u * 7u + 3u), "memset");
		{
			char s[8] = "abcdefg";

			CHECK(strlen(s) == 7, "strlen");
		}
	}

	printf("T01 support: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
