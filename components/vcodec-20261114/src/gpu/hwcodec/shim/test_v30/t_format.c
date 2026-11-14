/* SPDX-License-Identifier: MIT */
/*
 * t_format.c -- differential test of n2fmt() against the host C library.
 *
 * 300 generated format/argument combinations.  The guest argument list is laid out here from a description table
 * (type + value), independently of the formatter: 32-bit words, 64-bit values and doubles on an even slot, r0-r3
 * first and the guest stack above.  Every case is run through the register+stack mode (first variadic slot 0, 2 and
 * 3) and through the guest-va_list mode, at several output capacities, and compared byte for byte with snprintf()
 * on the host, whose return value must match too.
 *
 * The one deliberate difference: a NULL %s.  bionic prints "(null)" and truncates it with the precision, glibc
 * prints "" when a precision is given; the test checks the bionic behaviour directly instead of against the host.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stddef.h>
#include <sys/types.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"
#include "v30test.h"

enum atype { A_INT, A_UINT, A_LONG, A_ULONG, A_LL, A_ULL, A_SIZE, A_SSIZE, A_PTRDIFF, A_DBL, A_STR,
	     A_PTR, A_CHAR, A_WIDTH };

struct aval {
	enum atype t;
	uint64_t u;
	double d;
	const char *s;
};

enum shape {
	SH_INT1, SH_UINT1, SH_LONG1, SH_ULONG1, SH_LL1, SH_ULL1, SH_SIZE1, SH_SSIZE1, SH_PTRDIFF1,
	SH_STR1, SH_PTR1, SH_CHAR1, SH_DBL1, SH_INT2, SH_INT_INT_INT, SH_INT_STR, SH_INT_INT_DBL,
	SH_INT_STR_UINT, SH_UINT_STR
};

struct tmpl {
	const char *fmt;
	int nargs;
	enum atype t[3];
	enum shape sh;
};

static const struct tmpl TEMPLATES[] = {
	{ "%d", 1, { A_INT }, SH_INT1 },
	{ "%5d", 1, { A_INT }, SH_INT1 },
	{ "%-5d|", 1, { A_INT }, SH_INT1 },
	{ "%05d", 1, { A_INT }, SH_INT1 },
	{ "%+d", 1, { A_INT }, SH_INT1 },
	{ "% d", 1, { A_INT }, SH_INT1 },
	{ "%8.4d", 1, { A_INT }, SH_INT1 },
	{ "%.0d", 1, { A_INT }, SH_INT1 },
	{ "%u", 1, { A_UINT }, SH_UINT1 },
	{ "%x", 1, { A_UINT }, SH_UINT1 },
	{ "%#x", 1, { A_UINT }, SH_UINT1 },
	{ "%#08x", 1, { A_UINT }, SH_UINT1 },
	{ "%X", 1, { A_UINT }, SH_UINT1 },
	{ "%o", 1, { A_UINT }, SH_UINT1 },
	{ "%#o", 1, { A_UINT }, SH_UINT1 },
	{ "%hhd", 1, { A_INT }, SH_INT1 },
	{ "%hd", 1, { A_INT }, SH_INT1 },
	{ "%hhx", 1, { A_INT }, SH_INT1 },
	{ "%hu", 1, { A_INT }, SH_INT1 },
	{ "%ld", 1, { A_LONG }, SH_LONG1 },
	{ "%lu", 1, { A_ULONG }, SH_ULONG1 },
	{ "%lx", 1, { A_ULONG }, SH_ULONG1 },
	{ "%lld", 1, { A_LL }, SH_LL1 },
	{ "%llu", 1, { A_ULL }, SH_ULL1 },
	{ "%llx", 1, { A_ULL }, SH_ULL1 },
	{ "%zu", 1, { A_SIZE }, SH_SIZE1 },
	{ "%zd", 1, { A_SSIZE }, SH_SSIZE1 },
	{ "%td", 1, { A_PTRDIFF }, SH_PTRDIFF1 },
	{ "%c", 1, { A_CHAR }, SH_CHAR1 },
	{ "%5c|", 1, { A_CHAR }, SH_CHAR1 },
	{ "%s", 1, { A_STR }, SH_STR1 },
	{ "%10s|", 1, { A_STR }, SH_STR1 },
	{ "%-10.3s|", 1, { A_STR }, SH_STR1 },
	{ "%.3s", 1, { A_STR }, SH_STR1 },
	{ "%p", 1, { A_PTR }, SH_PTR1 },
	{ "%20p", 1, { A_PTR }, SH_PTR1 },
	{ "%f", 1, { A_DBL }, SH_DBL1 },
	{ "%.0f", 1, { A_DBL }, SH_DBL1 },
	{ "%.3f", 1, { A_DBL }, SH_DBL1 },
	{ "%12.4f|", 1, { A_DBL }, SH_DBL1 },
	{ "%e", 1, { A_DBL }, SH_DBL1 },
	{ "%.3e", 1, { A_DBL }, SH_DBL1 },
	{ "%E", 1, { A_DBL }, SH_DBL1 },
	{ "%g", 1, { A_DBL }, SH_DBL1 },
	{ "%.3g", 1, { A_DBL }, SH_DBL1 },
	{ "%#g", 1, { A_DBL }, SH_DBL1 },
	{ "%.10g", 1, { A_DBL }, SH_DBL1 },
	{ "%+g", 1, { A_DBL }, SH_DBL1 },
	{ "%.15g", 1, { A_DBL }, SH_DBL1 },
	{ "%.17g", 1, { A_DBL }, SH_DBL1 },
	{ "%.20f", 1, { A_DBL }, SH_DBL1 },
	{ "%010.3f", 1, { A_DBL }, SH_DBL1 },
	{ "%-14.6e|", 1, { A_DBL }, SH_DBL1 },
	{ "%#.0f", 1, { A_DBL }, SH_DBL1 },
	{ "%#.3o", 1, { A_UINT }, SH_UINT1 },
	{ "%#.0o", 1, { A_UINT }, SH_UINT1 },
	{ "%08.3d", 1, { A_INT }, SH_INT1 },
	{ "%+08d", 1, { A_INT }, SH_INT1 },
	{ "%*d", 2, { A_WIDTH, A_INT }, SH_INT2 },
	{ "%*.*d", 3, { A_WIDTH, A_WIDTH, A_INT }, SH_INT_INT_INT },
	{ "%.*s", 2, { A_WIDTH, A_STR }, SH_INT_STR },
	{ "%*.*f", 3, { A_WIDTH, A_WIDTH, A_DBL }, SH_INT_INT_DBL },
	{ "%d/%s/%x", 3, { A_INT, A_STR, A_UINT }, SH_INT_STR_UINT },
	{ "%08x-%s", 2, { A_UINT, A_STR }, SH_UINT_STR },
	{ "%+x", 1, { A_UINT }, SH_UINT1 },
	{ "% u", 1, { A_UINT }, SH_UINT1 },
	{ "%+o", 1, { A_UINT }, SH_UINT1 },
	{ "% 08lx", 1, { A_ULONG }, SH_ULONG1 },
};

#define NTEMPLATES ((int)(sizeof TEMPLATES / sizeof TEMPLATES[0]))
#define NCASES 300

/* ---- deterministic pseudo-random values ---------------------------------------------------------------------- */

static uint32_t g_rng = 0x12345678u;

static uint32_t rnd(void)
{
	uint32_t x = g_rng;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	g_rng = x;
	return x;
}

static const char *const STRINGS[] = {
	"", "a", "hello", "0123456789abcdef", "tab\there", "\xc3\xa9\xc3\xa8",
	"a rather long string that will need no padding at all but is longer than sixteen bytes"
};

static struct aval make_val(enum atype t)
{
	struct aval v;
	uint32_t r = rnd();

	memset(&v, 0, sizeof v);
	v.t = t;
	switch (t) {
	case A_INT:
	case A_LONG:
	case A_SSIZE:
	case A_PTRDIFF: {
		static const int32_t pool[] = { 0, 1, -1, 42, -42, 123456789, -123456789,
						INT32_MIN, INT32_MAX, 255, 65535, -32768, 7 };
		if (r & 1)
			v.u = (uint32_t)pool[r % (sizeof pool / sizeof pool[0])];
		else
			v.u = rnd();
		break;
	}
	case A_UINT:
	case A_ULONG:
	case A_SIZE: {
		static const uint32_t pool[] = { 0, 1, 0xffffffffu, 0x80000000u, 0xdeadbeefu, 0x1234u, 255u, 65536u };
		if (r & 1)
			v.u = pool[r % (sizeof pool / sizeof pool[0])];
		else
			v.u = rnd();
		break;
	}
	case A_LL: {
		static const int64_t pool[] = { 0, 1, -1, INT64_MIN, INT64_MAX, 0x123456789abcdefLL, -1234567890123LL };
		if (r & 1)
			v.u = (uint64_t)pool[r % (sizeof pool / sizeof pool[0])];
		else
			v.u = ((uint64_t)rnd() << 32) | rnd();
		break;
	}
	case A_ULL: {
		static const uint64_t pool[] = { 0, 1, UINT64_MAX, 0x8000000000000000ULL, 0xdeadbeefcafebabeULL };
		if (r & 1)
			v.u = pool[r % (sizeof pool / sizeof pool[0])];
		else
			v.u = ((uint64_t)rnd() << 32) | rnd();
		break;
	}
	case A_DBL: {
		static const double pool[] = {
			0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, 3.141592653589793, 0.1, 1e-5, 1e-4, 1e-310,
			5e-324, 1e308, 1.7976931348623157e308, 123456789.0, 999.9, 99999.0, 1e20, 0.0009999,
			-2.5, INFINITY, -INFINITY, NAN, 0.25, 0.125, 1e-3
		};
		if (r % 3 != 0) {
			v.d = pool[r % (sizeof pool / sizeof pool[0])];
		} else {
			uint64_t bits = ((uint64_t)rnd() << 32) | rnd();
			bits &= ~((uint64_t)0x7ff << 52);      /* finite */
			bits |= ((uint64_t)(1 + rnd() % 0x7fe)) << 52;
			memcpy(&v.d, &bits, 8);
		}
		break;
	}
	case A_STR:
		v.s = STRINGS[r % (sizeof STRINGS / sizeof STRINGS[0])];
		break;
	case A_PTR: {
		static const uint32_t pool[] = { 0, 0x1234, 0xffffffffu, 0xdeadbeefu, 0x80000000u, 1 };
		v.u = pool[r % (sizeof pool / sizeof pool[0])];
		break;
	}
	case A_CHAR: {
		static const uint8_t pool[] = { 'A', 'z', '0', 0x7f, 0xff, ' ' };
		v.u = pool[r % (sizeof pool / sizeof pool[0])];
		break;
	}
	case A_WIDTH:
		v.u = (uint32_t)((int)(r % 41) - 20);       /* -20..20 */
		break;
	}
	return v;
}

/* ---- guest argument layout (independent of the formatter) ----------------------------------------------------- */

struct layout {
	uint32_t r[4];
	uint32_t st[16];
	int nst;
	int nslots;
};

static int type_is_64(enum atype t)
{
	return t == A_LL || t == A_ULL || t == A_DBL;
}

static void word_pair(const struct aval *v, uint32_t guest_str, uint32_t *lo, uint32_t *hi)
{
	*hi = 0;
	if (v->t == A_DBL) {
		uint64_t bits;
		memcpy(&bits, &v->d, 8);
		*lo = (uint32_t)bits;
		*hi = (uint32_t)(bits >> 32);
	} else if (type_is_64(v->t)) {
		*lo = (uint32_t)v->u;
		*hi = (uint32_t)(v->u >> 32);
	} else if (v->t == A_STR) {
		*lo = guest_str;
	} else {
		*lo = (uint32_t)v->u;
	}
}

static void lay_out(const struct aval *v, int n, int first, const uint32_t *guest_strs, struct layout *L)
{
	int abs = first, i;

	memset(L, 0, sizeof *L);
	for (i = 0; i < n; i++) {
		uint32_t lo, hi;
		int two = type_is_64(v[i].t);

		if (two)
			abs = (abs + 1) & ~1;        /* 64-bit values sit on an even slot: skip r1 / r3 */
		word_pair(&v[i], guest_strs[i], &lo, &hi);
		if (abs < 4) {
			L->r[abs++] = lo;
			if (two)
				L->r[abs++] = hi;
		} else {
			L->st[abs - 4] = lo;
			abs++;
			if (two) {
				L->st[abs - 4] = hi;
				abs++;
			}
		}
	}
	L->nslots = abs;
	L->nst = abs > 4 ? abs - 4 : 0;
}

/* ---- host oracle --------------------------------------------------------------------------------------------- */

static int host_call(enum shape sh, char *b, size_t n, const char *f, const struct aval *v)
{
	switch (sh) {
	case SH_INT1: return snprintf(b, n, f, (int)v[0].u);
	case SH_UINT1: return snprintf(b, n, f, (unsigned)v[0].u);
	case SH_LONG1: return snprintf(b, n, f, (long)(int32_t)v[0].u);
	case SH_ULONG1: return snprintf(b, n, f, (unsigned long)(uint32_t)v[0].u);
	case SH_LL1: return snprintf(b, n, f, (long long)v[0].u);
	case SH_ULL1: return snprintf(b, n, f, (unsigned long long)v[0].u);
	case SH_SIZE1: return snprintf(b, n, f, (size_t)(uint32_t)v[0].u);
	case SH_SSIZE1: return snprintf(b, n, f, (ssize_t)(int32_t)v[0].u);
	case SH_PTRDIFF1: return snprintf(b, n, f, (ptrdiff_t)(int32_t)v[0].u);
	case SH_STR1: return snprintf(b, n, f, v[0].s);
	case SH_PTR1: return snprintf(b, n, f, (void *)(uintptr_t)(uint32_t)v[0].u);
	case SH_CHAR1: return snprintf(b, n, f, (int)v[0].u);
	case SH_DBL1: return snprintf(b, n, f, v[0].d);
	case SH_INT2: return snprintf(b, n, f, (int)v[0].u, (int)v[1].u);
	case SH_INT_INT_INT: return snprintf(b, n, f, (int)v[0].u, (int)v[1].u, (int)v[2].u);
	case SH_INT_STR: return snprintf(b, n, f, (int)v[0].u, v[1].s);
	case SH_INT_INT_DBL: return snprintf(b, n, f, (int)v[0].u, (int)v[1].u, v[2].d);
	case SH_INT_STR_UINT: return snprintf(b, n, f, (int)v[0].u, v[1].s, (unsigned)v[2].u);
	case SH_UINT_STR: return snprintf(b, n, f, (unsigned)v[0].u, v[1].s);
	}
	return 0;
}

/* ---- the differential test ------------------------------------------------------------------------------------ */

static void one_case(int idx)
{
	const struct tmpl *t = &TEMPLATES[idx % NTEMPLATES];
	struct aval v[3];
	uint32_t guest_str[3] = { 0, 0, 0 };
	uint32_t fmt;
	char host_full[2048], host_cap[2048], mine[2048];
	int hlen, i, mode, fi;
	static const int caps[] = { 2048, 0, 1, 5, 13 };

	memset(v, 0, sizeof v);
	for (i = 0; i < t->nargs; i++)
		v[i] = make_val(t->t[i]);
	for (i = 0; i < t->nargs; i++)
		if (v[i].t == A_STR)
			guest_str[i] = t_put(v[i].s);
	fmt = t_put(t->fmt);

	hlen = host_call(t->sh, host_full, sizeof host_full, t->fmt, v);
	if (hlen < 0) {
		t_checkf(0, "host snprintf failed for %s", t->fmt);
		return;
	}

	for (mode = 0; mode < 4; mode++) {
		int first = mode == 0 ? 0 : mode == 1 ? 2 : mode == 2 ? 3 : 0;
		struct layout L;
		uint32_t area = 0, va = 0;
		struct n2a32_cpu *c = t_cpu();

		lay_out(v, t->nargs, mode == 3 ? 0 : first, guest_str, &L);
		if (mode == 3) {
			int j;
			area = t_alloc(4 * (uint32_t)(L.nslots > 0 ? L.nslots : 1));
			for (j = 0; j < 4; j++)
				n2a32_st32(c, area + 4 * (uint32_t)j, L.r[j]);
			for (j = 0; j < L.nst; j++)
				n2a32_st32(c, area + 16 + 4 * (uint32_t)j, L.st[j]);
			/* ARM EABI: va_list is a struct passed by value; the register/host call carries
			 * the argument area itself (opus-dec1 HIGH finding). */
			va = area;
		} else {
			for (i = 0; i < 4; i++)
				c->r[i] = L.r[i];
			for (i = 0; i < L.nst; i++)
				n2a32_st32(c, c->r[13] + 4 * (uint32_t)i, L.st[i]);
		}
		for (fi = 0; fi < (int)(sizeof caps / sizeof caps[0]); fi++) {
			struct n2fmt_args a;
			int cap = caps[fi], r;

			if (mode == 3) {
				if (n2fmt_args_va(&a, c, va) < 0) {
					t_checkf(0, "va_list init failed for %s", t->fmt);
					continue;
				}
			} else {
				n2fmt_args_regs(&a, c, (unsigned)first);
			}
			memset(mine, 0xaa, sizeof mine);
			mine[0] = 0;
			r = n2fmt(c, mine, (size_t)cap, fmt, &a);
			if (cap == 2048) {
				t_checkf(r == hlen && strcmp(mine, host_full) == 0,
					 "fmt %s mode %d: n2fmt=[%s] (%d) host=[%s] (%d)", t->fmt, mode, mine, r,
					 host_full, hlen);
			} else {
				int hcap = host_call(t->sh, host_cap, (size_t)cap, t->fmt, v);
				size_t cmplen = (size_t)((hlen + 1) < cap ? (hlen + 1) : cap);
				int ok = r == hlen && hcap == hlen && memcmp(mine, host_cap, cmplen) == 0;
				if (ok && cap > 0 && hlen >= cap)
					ok = mine[cap - 1] == 0;
				if (cap > 0)
					t_checkf(ok, "fmt %s mode %d cap %d: n2fmt=[%s] (%d) host=[%s] (%d)",
						 t->fmt, mode, cap, mine, r, host_cap, hcap);
				else
					t_checkf(ok, "fmt %s mode %d cap 0: lengths %d/%d/%d", t->fmt, mode, r,
						 hlen, hcap);
			}
		}
	}
}

static void test_format_generated(void)
{
	int i;

	for (i = 0; i < NCASES; i++) {
		g_rng = 0x9e3779b9u ^ (uint32_t)i * 2654435761u;
		one_case(i);
	}
}

/* ---- hand-written corner cases the generator cannot express --------------------------------------------------- */

static void fmt_expect(const char *fmt, int kind, uint64_t u, double d, const char *expect)
{
	char out[256];
	struct n2fmt_args a;
	struct n2a32_cpu *c = t_cpu();
	uint32_t f = t_put(fmt);
	int r;

	n2fmt_args_regs(&a, c, 0);
	if (kind == 0) {
		c->r[0] = (uint32_t)u;
	} else if (kind == 1) {
		c->r[0] = (uint32_t)u;
		c->r[1] = (uint32_t)(u >> 32);
	} else {
		memcpy(&u, &d, 8);
		c->r[0] = (uint32_t)u;
		c->r[1] = (uint32_t)(u >> 32);
	}
	r = n2fmt(c, out, sizeof out, f, &a);
	t_checkf(strcmp(out, expect) == 0 && r == (int)strlen(expect), "fmt %s: expected [%s] got [%s] (%d)",
		 fmt, expect, out, r);
}

static void test_format_corners(void)
{
	/* NULL %s: bionic truncates "(null)" with the precision (glibc does not); documented in STATUS.md */
	{
		char out[64];
		struct n2fmt_args a;
		struct n2a32_cpu *c = t_cpu();
		uint32_t f = t_put("%.3s");
		n2fmt_args_regs(&a, c, 0);
		c->r[0] = 0;
		n2fmt(c, out, sizeof out, f, &a);
		T_CHECK(strcmp(out, "(nu") == 0);
	}
	{
		char out[64];
		struct n2fmt_args a;
		struct n2a32_cpu *c = t_cpu();
		uint32_t f = t_put("%s");
		n2fmt_args_regs(&a, c, 0);
		c->r[0] = 0;
		n2fmt(c, out, sizeof out, f, &a);
		T_CHECK(strcmp(out, "(null)") == 0);
	}
	/* a NULL format is an error, not a crash */
	{
		char out[8];
		struct n2fmt_args a;
		struct n2a32_cpu *c = t_cpu();
		n2fmt_args_regs(&a, c, 0);
		T_CHECK(n2fmt(c, out, sizeof out, 0, &a) == -1);
	}
	/* a NULL va_list is refused */
	{
		struct n2fmt_args a;
		struct n2a32_cpu *c = t_cpu();
		T_CHECK(n2fmt_args_va(&a, c, 0) == -1);
	}
	/* a huge width or precision must not overflow a stack buffer (precision is capped at 400) */
	{
		char out[16];
		struct n2fmt_args a;
		struct n2a32_cpu *c = t_cpu();
		uint32_t f = t_put("%*d");
		n2fmt_args_regs(&a, c, 0);
		c->r[0] = 100000;
		c->r[1] = 7;
		T_CHECK(n2fmt(c, out, sizeof out, f, &a) == 100000);
		T_CHECK(out[15] == 0);
	}
	{
		char out[64];
		struct n2fmt_args a;
		struct n2a32_cpu *c = t_cpu();
		uint32_t f = t_put("%.*d");
		n2fmt_args_regs(&a, c, 0);
		c->r[0] = 100000;
		c->r[1] = 7;
		T_CHECK(n2fmt(c, out, sizeof out, f, &a) == 400);
		T_CHECK(strlen(out) == 63);
	}
	/* an absurd literal width must not overflow the parser's int, and must not loop */
	{
		char out[8];
		struct n2fmt_args a;
		struct n2a32_cpu *c = t_cpu();
		uint32_t f = t_put("%999999999999999999d");
		n2fmt_args_regs(&a, c, 0);
		c->r[0] = 5;
		T_CHECK(n2fmt(c, out, sizeof out, f, &a) >= 999999999);
		T_CHECK(out[7] == 0);
	}
	/* glibc-compatible special values */
	fmt_expect("%f", 2, 0, INFINITY, "inf");
	fmt_expect("%+f", 2, 0, INFINITY, "+inf");
	fmt_expect("%010f", 2, 0, INFINITY, "       inf");
	fmt_expect("%-10f|", 2, 0, NAN, "nan       |");
	fmt_expect("%F", 2, 0, NAN, "NAN");
	fmt_expect("%e", 2, 0, 0.0, "0.000000e+00");
	fmt_expect("%g", 2, 0, -0.0, "-0");
	fmt_expect("%.0e", 2, 0, 9.9999999, "1e+01");
	fmt_expect("%.0f", 2, 0, 2.5, "2");
	fmt_expect("%.0f", 2, 0, 3.5, "4");
	fmt_expect("%.0f", 2, 0, -2.5, "-2");
	fmt_expect("%.1f", 2, 0, 0.25, "0.2");
	fmt_expect("%.2f", 2, 0, 0.125, "0.12");
	fmt_expect("%.0f", 2, 0, 9.9, "10");

}

/* ---- the snprintf family shims ------------------------------------------------------------------------------- */

static uint32_t call5_shim(const uint32_t *args)
{
	return t_call(n2shim_string, "snprintf", args, 4, NULL);
}


static void test_format_shims(void)
{
	struct n2a32_cpu *c;
	char buf[64];

	/* snprintf(dst, n, "%d-%s", 7, "x") */
	{
		uint32_t dst = t_alloc(32);
		uint32_t args[5] = { dst, 32, t_put("%d-%s"), 7, t_put("x") };
		uint32_t r = t_call(n2shim_string, "snprintf", args, 5, NULL);
		T_CHECK(r == 3);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "7-x") == 0);
	}
	/* snprintf truncation: return value counts the whole string */
	{
		uint32_t dst = t_alloc(8);
		uint32_t args[5] = { dst, 4, t_put("%d"), 12345, 0 };
		uint32_t r = t_call(n2shim_string, "snprintf", args, 4, NULL);
		T_CHECK(r == 5);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "123") == 0);
	}
	/* snprintf with n == 0 writes nothing */
	{
		uint32_t dst = t_alloc(8);
		uint32_t args[3] = { dst, 0, t_put("abc") };
		uint32_t r = t_call(n2shim_string, "snprintf", args, 3, NULL);
		T_CHECK(r == 3);
		T_CHECK(n2a32_ld8(t_cpu(), dst) == 0);
	}
	/* sprintf */
	{
		uint32_t dst = t_alloc(32);
		uint32_t args[4] = { dst, t_put("%s=%d"), t_put("k"), 9 };
		uint32_t r = t_call(n2shim_string, "sprintf", args, 4, NULL);
		T_CHECK(r == 3);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "k=9") == 0);
	}
	/* vsnprintf: build a guest va_list over the variadic area */
	{
		uint32_t dst = t_alloc(32);
		uint32_t area = t_alloc(8);
		uint32_t args[4];
		c = t_cpu();
		n2a32_st32(c, area, 1234);
		args[0] = dst;
		args[1] = 32;
		args[2] = t_put("v=%d");
		args[3] = area;
		T_CHECK(t_call(n2shim_string, "vsnprintf", args, 4, NULL) == 6);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "v=1234") == 0);
	}
	/* __snprintf_chk(dst, n, flag, destlen, fmt, ...) */
	{
		uint32_t dst = t_alloc(32);
		uint32_t args[6] = { dst, 32, 1, 32, t_put("<%d>"), 77 };
		T_CHECK(t_call(n2shim_string, "__snprintf_chk", args, 6, NULL) == 4);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "<77>") == 0);
	}
	/* __vsnprintf_chk(dst, n, flag, destlen, fmt, va) */
	{
		uint32_t dst = t_alloc(32);
		uint32_t area = t_alloc(8);
		uint32_t args[6];
		c = t_cpu();
		n2a32_st32(c, area, 55);
		args[0] = dst;
		args[1] = 32;
		args[2] = 1;
		args[3] = 32;
		args[4] = t_put("%x");
		args[5] = area;
		T_CHECK(t_call(n2shim_string, "__vsnprintf_chk", args, 6, NULL) == 2);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "37") == 0);
	}
	/* __vsprintf_chk(dst, flag, destlen, fmt, va) */
	{
		uint32_t dst = t_alloc(32);
		uint32_t area = t_alloc(8);
		uint32_t args[5];
		c = t_cpu();
		n2a32_st32(c, area, 0xabc);
		args[0] = dst;
		args[1] = 1;
		args[2] = 32;
		args[3] = t_put("%X");
		args[4] = area;
		T_CHECK(t_call(n2shim_string, "__vsprintf_chk", args, 5, NULL) == 3);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "ABC") == 0);
	}
	/* %n writes the count so far */
	{
		uint32_t dst = t_alloc(16), np = t_alloc(4);
		uint32_t args[6] = { dst, 16, t_put("abc%n"), np, 0, 0 };
		(void)call5_shim(args);
		T_CHECK(n2a32_ld32(t_cpu(), np) == 3);
	}
	/* a NaN double through a guest va_list (8-byte alignment from the area start) */
	{
		uint32_t dst = t_alloc(32);
		uint32_t area = t_alloc(16);
		uint32_t args[4];
		uint64_t bits = 0;
		double d = 1.25;
		c = t_cpu();
		memcpy(&bits, &d, 8);
		n2a32_st32(c, area, (uint32_t)bits);
		n2a32_st32(c, area + 4, (uint32_t)(bits >> 32));
		args[0] = dst;
		args[1] = 32;
		args[2] = t_put("%.2f");
		args[3] = area;
		T_CHECK(t_call(n2shim_string, "vsnprintf", args, 4, NULL) == 4);
		T_CHECK(strcmp(t_gstr(dst, buf, sizeof buf), "1.25") == 0);
	}
}

/* ---- sscanf and the shared scanner --------------------------------------------------------------------------- */

static int do_sscanf(const char *input, const char *fmt, uint32_t *out, int nout)
{
	uint32_t args[12];
	int i, n = 0;

	args[n++] = t_put(input);
	args[n++] = t_put(fmt);
	for (i = 0; i < nout; i++)
		args[n++] = out ? out[i] : 0;
	return (int32_t)t_call(n2shim_string, "sscanf", args, n, NULL);
}

static void test_scanf_cases(void)
{
	uint32_t d[6];
	char buf[64];

	/* two decimals */
	d[0] = t_alloc(4);
	d[1] = t_alloc(4);
	T_CHECK(do_sscanf(" 12 -34", "%d %d", d, 2) == 2);
	T_CHECK((int32_t)n2a32_ld32(t_cpu(), d[0]) == 12);
	T_CHECK((int32_t)n2a32_ld32(t_cpu(), d[1]) == -34);
	/* unsigned and hex */
	d[0] = t_alloc(4);
	d[1] = t_alloc(4);
	T_CHECK(do_sscanf("42 ff", "%u %x", d, 2) == 2);
	T_CHECK(n2a32_ld32(t_cpu(), d[0]) == 42 && n2a32_ld32(t_cpu(), d[1]) == 255);
	/* string and character */
	d[0] = t_alloc(16);
	d[1] = t_alloc(4);
	T_CHECK(do_sscanf("hello X", "%s %c", d, 2) == 2);
	T_CHECK(strcmp(t_gstr(d[0], buf, sizeof buf), "hello") == 0);
	T_CHECK(n2a32_ld8(t_cpu(), d[1]) == 'X');
	/* 64-bit */
	d[0] = t_alloc(8);
	T_CHECK(do_sscanf("-9223372036854775808", "%lld", d, 1) == 1);
	T_CHECK(n2a32_ld64(t_cpu(), d[0]) == 0x8000000000000000ULL);
	/* literal text */
	d[0] = t_alloc(4);
	d[1] = t_alloc(4);
	T_CHECK(do_sscanf("x=1;y=2", "x=%d;y=%d", d, 2) == 2);
	T_CHECK((int32_t)n2a32_ld32(t_cpu(), d[0]) == 1 && (int32_t)n2a32_ld32(t_cpu(), d[1]) == 2);
	/* %n */
	d[0] = t_alloc(4);
	d[1] = t_alloc(4);
	T_CHECK(do_sscanf("12345", "%d%n", d, 2) == 1);
	T_CHECK((int32_t)n2a32_ld32(t_cpu(), d[0]) == 12345);
	T_CHECK(n2a32_ld32(t_cpu(), d[1]) == 5);
	/* width */
	d[0] = t_alloc(4);
	T_CHECK(do_sscanf("12345", "%3d", d, 1) == 1);
	T_CHECK((int32_t)n2a32_ld32(t_cpu(), d[0]) == 123);
	/* suppression */
	d[0] = t_alloc(4);
	T_CHECK(do_sscanf("12 34", "%*d %d", d, 1) == 1);
	T_CHECK((int32_t)n2a32_ld32(t_cpu(), d[0]) == 34);
	/* %x accepts the 0x prefix, like strtoul(base 16) */
	d[0] = t_alloc(4);
	T_CHECK(do_sscanf("0x1f", "%x", d, 1) == 1 && n2a32_ld32(t_cpu(), d[0]) == 31);
	d[0] = t_alloc(4);
	T_CHECK(do_sscanf("0Xff", "%x", d, 1) == 1 && n2a32_ld32(t_cpu(), d[0]) == 255);
	d[0] = t_alloc(4);
	T_CHECK(do_sscanf("1234", "%x", d, 1) == 1 && n2a32_ld32(t_cpu(), d[0]) == 0x1234);
	/* %i base detection */
	d[0] = t_alloc(4);
	T_CHECK(do_sscanf("0x1f", "%i", d, 1) == 1 && n2a32_ld32(t_cpu(), d[0]) == 31);
	d[0] = t_alloc(4);
	T_CHECK(do_sscanf("077", "%i", d, 1) == 1 && n2a32_ld32(t_cpu(), d[0]) == 63);
	/* %% and no conversions */
	T_CHECK(do_sscanf("%", "%%", NULL, 0) == 0);
	/* matching failure and EOF */
	T_CHECK(do_sscanf("abc", "%d", NULL, 0) == 0);
	T_CHECK(do_sscanf("", "%d", NULL, 0) == -1);
	T_CHECK(do_sscanf("   ", "%d", NULL, 0) == -1);
	/* two characters through %c */
	d[0] = t_alloc(4);
	d[1] = t_alloc(4);
	T_CHECK(do_sscanf("a b", "%c %c", d, 2) == 2);
	T_CHECK(n2a32_ld8(t_cpu(), d[0]) == 'a' && n2a32_ld8(t_cpu(), d[1]) == 'b');
	/* %d then %s without a separator */
	d[0] = t_alloc(4);
	d[1] = t_alloc(16);
	T_CHECK(do_sscanf("12abc", "%d%s", d, 2) == 2);
	T_CHECK((int32_t)n2a32_ld32(t_cpu(), d[0]) == 12);
	T_CHECK(strcmp(t_gstr(d[1], buf, sizeof buf), "abc") == 0);
}

/* V33 will call n2fmt_vscan() with a FILE-backed source; here the same API runs over a guest string. */
struct vscan_src {
	struct n2a32_cpu *c;
	uint32_t p;
};

static int vscan_get(void *ctx)
{
	struct vscan_src *s = (struct vscan_src *)ctx;
	uint8_t ch = n2a32_ld8(s->c, s->p);

	if (!ch)
		return -1;
	s->p++;
	return ch;
}

static void test_scanf_api(void)
{
	struct n2a32_cpu *c = t_cpu();
	struct vscan_src src;
	struct n2fmt_input in;
	struct n2fmt_args a;
	uint32_t dst = t_alloc(4), sbuf = t_alloc(16);
	char buf[16];
	int r;

	src.c = c;
	src.p = t_put("17 rest");
	in.get = vscan_get;
	in.unget = NULL;
	in.ctx = &src;
	n2a32_st32(c, c->r[13], dst);            /* slot 4 = first variadic (r0..r3 are fixed args) */
	n2a32_st32(c, c->r[13] + 4, sbuf);       /* slot 5 */
	n2fmt_args_regs(&a, c, 4);
	r = n2fmt_vscan(c, &in, t_put("%d %s"), &a);
	T_CHECK(r == 2);
	T_CHECK((int32_t)n2a32_ld32(c, dst) == 17);
	T_CHECK(strcmp(t_gstr(sbuf, buf, sizeof buf), "rest") == 0);
}

void test_format(void)
{
	test_format_generated();
	test_format_corners();
	test_format_shims();
}

void test_scanf(void)
{
	test_scanf_cases();
	test_scanf_api();
}
