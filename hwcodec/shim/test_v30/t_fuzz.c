/* SPDX-License-Identifier: MIT */
/*
 * t_fuzz.c -- extra differential fuzz of n2fmt() against the host printf (one aggregate check).
 *
 * 30000 random single-argument conversions: random flag set, width, precision, length and conversion, over
 * random int/unsigned/double values.  This is broader than the 300 cases of the description-table test and it
 * found one real bug during development (the '+' and ' ' flags were applied to unsigned conversions).  The seed
 * is fixed, so a failure is reproducible.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"
#include "v30test.h"

static uint32_t fz_rng = 0x12345678u;

static uint32_t fz_rd(void)
{
	uint32_t x = fz_rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	fz_rng = x;
	return x;
}

void test_fuzz(void)
{
	static const char *const flagsets[] = { "", "-", "0", "+", " ", "#", "-0", "+0", "#0", "- ", "+#" };
	int it, failures = 0;

	fz_rng = 0x12345678u;
	{
		const char *seed = getenv("V30_FUZZ_SEED");
		if (seed)
			fz_rng = (uint32_t)strtoul(seed, NULL, 0);
	}
	for (it = 0; it < 30000; it++) {
		int kind = (int)(fz_rd() % 3);       /* 0 int32, 1 uint32, 2 double */
		char f[64];
		int n = 0, len;
		const char *fl;
		const char *conv;
		uint32_t fmt;
		struct n2a32_cpu *c = t_cpu();
		struct n2fmt_args a;
		char mine[4096], host[4096];
		int rm, rh;

		f[n++] = '%';
		fl = flagsets[fz_rd() % (sizeof flagsets / sizeof flagsets[0])];
		while (*fl)
			f[n++] = *fl++;
		if (fz_rd() % 3 == 0)
			n += sprintf(f + n, "%u", fz_rd() % 25);
		if (fz_rd() % 2 == 0)
			n += sprintf(f + n, ".%u", fz_rd() % 22);
		len = (int)(fz_rd() % 3);            /* 0 none, 1 l, 2 ll */
		if (kind == 2) {
			conv = fz_rd() % 4 == 0 ? "f" : fz_rd() % 3 == 0 ? "e" : fz_rd() % 2 == 0 ? "g" : "G";
			if (fz_rd() % 3 == 0)
				f[n++] = 'l';
		} else if (kind == 0) {
			conv = fz_rd() % 2 == 0 ? "d" : "i";
		} else {
			static const char *const u[] = { "u", "x", "X", "o" };
			conv = u[fz_rd() % 4];
		}
		if (kind != 2) {
			if (len == 1)
				f[n++] = 'l';
			else if (len == 2) {
				f[n++] = 'l';
				f[n++] = 'l';
			}
		}
		strcpy(f + n, conv);
		fmt = t_put(f);
		if (kind == 2) {
			double v;
			uint64_t u;
			if (fz_rd() % 4 == 0) {
				uint64_t bits = ((uint64_t)fz_rd() << 32) | fz_rd();
				bits &= ~((uint64_t)0x7ff << 52);
				bits |= ((uint64_t)(1 + fz_rd() % 0x7fe)) << 52;
				memcpy(&v, &bits, 8);
			} else {
				v = (double)(int32_t)fz_rd() / (double)(1 + fz_rd() % 100000);
			}
			memcpy(&u, &v, 8);
			c->r[0] = (uint32_t)u;
			c->r[1] = (uint32_t)(u >> 32);
			n2fmt_args_regs(&a, c, 0);
			rm = n2fmt(c, mine, sizeof mine, fmt, &a);
			rh = snprintf(host, sizeof host, f, v);
		} else if (kind == 0) {
			static const int32_t pool[] = { 0, 1, -1, 42, -42, INT32_MAX, INT32_MIN, 255, 65535, -32768 };
			int32_t v = fz_rd() % 4 == 0 ? pool[fz_rd() % 10] : (int32_t)fz_rd();
			uint64_t u = (uint64_t)(int64_t)v;
			if (len == 2) {
				c->r[0] = (uint32_t)u;
				c->r[1] = (uint32_t)(u >> 32);
			} else {
				c->r[0] = (uint32_t)v;
			}
			n2fmt_args_regs(&a, c, 0);
			rm = n2fmt(c, mine, sizeof mine, fmt, &a);
			if (len == 2)
				rh = snprintf(host, sizeof host, f, (long long)v);
			else if (len == 1)
				rh = snprintf(host, sizeof host, f, (long)v);
			else
				rh = snprintf(host, sizeof host, f, v);
		} else {
			static const uint32_t pool[] = { 0, 1, 0xffffffffu, 0x80000000u, 0xdeadbeefu, 255, 65536 };
			uint32_t v = fz_rd() % 4 == 0 ? pool[fz_rd() % 7] : fz_rd();
			c->r[0] = v;
			c->r[1] = 0;
			n2fmt_args_regs(&a, c, 0);
			rm = n2fmt(c, mine, sizeof mine, fmt, &a);
			if (len == 2)
				rh = snprintf(host, sizeof host, f, (unsigned long long)v);
			else if (len == 1)
				rh = snprintf(host, sizeof host, f, (unsigned long)v);
			else
				rh = snprintf(host, sizeof host, f, v);
		}
		if (rm != rh || strcmp(mine, host) != 0) {
			if (failures < 20)
				printf("FAIL fuzz [%s]: n2fmt=[%s](%d) host=[%s](%d)\n", f, mine, rm, host, rh);
			failures++;
		}
	}
	t_checkf(failures == 0, "format fuzz: %d of 30000 cases differ from the host", failures);
}
