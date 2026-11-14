/* SPDX-License-Identifier: MIT */
/*
 * n2shim_format.c -- guest printf/scanf (package V30).
 *
 * The public entry point is n2fmt() in n2fmt.h: it reads a guest format string and a guest AAPCS argument list
 * and produces bytes in a host buffer.  The snprintf/sprintf/vsnprintf/sscanf shims in n2shim_string[] are thin
 * wrappers that copy the result into guest memory; V33/V34 call n2fmt() directly for printf/fprintf/log output.
 *
 * Floating point: the host's printf is never used.  A binary64 value is decomposed into m * 2^e and converted with
 * exact big-integer arithmetic (m * 2^e * 10^k, rounded half-to-even), which is what a C library on an IEEE-754
 * host produces.  Decimal precision is capped at 400 digits (far beyond what the vendor code uses) and the format
 * string at 2047 bytes; both limits are documented in n2fmt.h and reported by the tests.
 *
 * The scanner is shared by sscanf() and by V33's fscanf() forward: n2fmt_vscan() takes a byte source, so this
 * file never needs to know what a guest FILE* is.
 */
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"

#define FMT_MAX     2048
#define PREC_MAX    400
#define BN_LIMBS    96
#define DEC_MAX     1024

/* ============================================================================================================= */
/* exact decimal bignum                                                                                          */
/* ============================================================================================================= */

typedef struct { int n; uint32_t d[BN_LIMBS]; } bn;

static void bn_zero(bn *a) { a->n = 0; }

static void bn_trim(bn *a)
{
	while (a->n > 0 && a->d[a->n - 1] == 0)
		a->n--;
}

static void bn_set_u64(bn *a, uint64_t v)
{
	a->n = 0;
	while (v) {
		a->d[a->n++] = (uint32_t)v;
		v >>= 32;
	}
}

static int bn_is_zero(const bn *a) { return a->n == 0; }

static int bn_cmp(const bn *a, const bn *b)
{
	int i;
	if (a->n != b->n)
		return a->n < b->n ? -1 : 1;
	for (i = a->n - 1; i >= 0; i--)
		if (a->d[i] != b->d[i])
			return a->d[i] < b->d[i] ? -1 : 1;
	return 0;
}

static int bn_mul_small(bn *a, uint32_t m)
{
	uint64_t carry = 0;
	int i;

	for (i = 0; i < a->n; i++) {
		uint64_t v = (uint64_t)a->d[i] * m + carry;
		a->d[i] = (uint32_t)v;
		carry = v >> 32;
	}
	while (carry) {
		if (a->n >= BN_LIMBS)
			return -1;
		a->d[a->n++] = (uint32_t)carry;
		carry >>= 32;
	}
	return 0;
}

static int bn_shl(bn *a, unsigned bits)
{
	unsigned words = bits / 32, b = bits % 32;
	int i;

	if (!a->n)
		return 0;
	if (words + (unsigned)a->n + (b ? 1u : 0u) > BN_LIMBS)
		return -1;
	if (words) {
		for (i = a->n - 1; i >= 0; i--)
			a->d[i + (int)words] = a->d[i];
		for (i = 0; i < (int)words; i++)
			a->d[i] = 0;
		a->n += (int)words;
	}
	if (b) {
		uint32_t carry = 0;
		for (i = 0; i < a->n; i++) {
			uint32_t v = a->d[i];
			a->d[i] = (v << b) | carry;
			carry = v >> (32 - b);
		}
		if (carry) {
			if (a->n >= BN_LIMBS)
				return -1;
			a->d[a->n++] = carry;
		}
	}
	bn_trim(a);
	return 0;
}

static void bn_shl1(bn *a)
{
	int i;
	uint32_t carry = 0;

	if (!a->n)
		return;
	for (i = 0; i < a->n; i++) {
		uint32_t v = a->d[i];
		a->d[i] = (v << 1) | carry;
		carry = v >> 31;
	}
	if (carry && a->n < BN_LIMBS)
		a->d[a->n++] = carry;
}

static void bn_add_small(bn *a, uint32_t v)
{
	uint64_t c = v;
	int i;

	for (i = 0; i < a->n && c; i++) {
		uint64_t t = (uint64_t)a->d[i] + c;
		a->d[i] = (uint32_t)t;
		c = t >> 32;
	}
	if (c && a->n < BN_LIMBS)
		a->d[a->n++] = (uint32_t)c;
}

static void bn_sub(bn *a, const bn *b)
{
	int64_t borrow = 0;
	int i;

	for (i = 0; i < a->n; i++) {
		int64_t t = (int64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
		if (t < 0) {
			t += (int64_t)1 << 32;
			borrow = 1;
		} else {
			borrow = 0;
		}
		a->d[i] = (uint32_t)t;
	}
	bn_trim(a);
}

static int bn_bits(const bn *a)
{
	int b = 0;
	uint32_t top;

	if (!a->n)
		return 0;
	top = a->d[a->n - 1];
	while (top) {
		b++;
		top >>= 1;
	}
	return (a->n - 1) * 32 + b;
}

static int bn_getbit(const bn *a, int i)
{
	int w = i / 32;
	if (w >= a->n)
		return 0;
	return (int)((a->d[w] >> (i % 32)) & 1u);
}

static void bn_setbit(bn *a, int i)
{
	int w = i / 32;
	while (a->n <= w && a->n < BN_LIMBS) {
		a->d[a->n++] = 0;
	}
	if (w < a->n)
		a->d[w] |= 1u << (i % 32);
}

/* q = floor(num / den), r = num - q * den.  Binary long division: exact, and fast enough for printf. */
static void bn_divmod(const bn *num, const bn *den, bn *q, bn *r)
{
	int i;

	bn_zero(q);
	bn_zero(r);
	for (i = bn_bits(num) - 1; i >= 0; i--) {
		bn_shl1(r);
		if (bn_getbit(num, i))
			bn_setbit(r, 0);
		if (bn_cmp(r, den) >= 0) {
			bn_sub(r, den);
			bn_setbit(q, i);
		}
	}
}

static void bn_divmod_small(bn *a, uint32_t d, uint32_t *rem)
{
	uint64_t r = 0;
	int i;

	for (i = a->n - 1; i >= 0; i--) {
		uint64_t v = (r << 32) | a->d[i];
		a->d[i] = (uint32_t)(v / d);
		r = v % d;
	}
	bn_trim(a);
	*rem = (uint32_t)r;
}

static int bn_mul_pow5(bn *a, unsigned k)
{
	static const uint32_t p5[13] = { 1u, 5u, 25u, 125u, 625u, 3125u, 15625u, 78125u, 390625u, 1953125u,
					 9765625u, 48828125u, 244140625u };
	while (k >= 13) {
		if (bn_mul_small(a, 1220703125u))
			return -1;
		k -= 13;
	}
	if (k && bn_mul_small(a, p5[k]))
		return -1;
	return 0;
}

/* Decimal digits of a, most significant first; returns the count. */
static int bn_to_dec(const bn *a, char *out)
{
	bn t = *a;
	char tmp[DEC_MAX];
	int len = 0, i;

	if (!a->n) {
		out[0] = '0';
		out[1] = 0;
		return 1;
	}
	while (t.n) {
		uint32_t r = 0;
		int j;
		bn_divmod_small(&t, 1000000000u, &r);
		for (j = 0; j < 9; j++) {
			if (len >= DEC_MAX - 1)
				return -1;
			tmp[len++] = (char)('0' + r % 10);
			r /= 10;
		}
	}
	while (len > 1 && tmp[len - 1] == '0')
		len--;
	for (i = 0; i < len; i++)
		out[i] = tmp[len - 1 - i];
	out[len] = 0;
	return len;
}

/* ============================================================================================================= */
/* binary64 -> decimal                                                                                          */
/* ============================================================================================================= */

struct n2fp {
	uint64_t m;    /* significand: v = m * 2^e, or 0 for zero */
	int e;
	int neg;
	int special;   /* 0 finite, 1 infinity, 2 NaN */
};

static void fp_decode(double v, struct n2fp *x)
{
	uint64_t u;
	unsigned exp;
	uint64_t frac;

	memcpy(&u, &v, sizeof u);
	x->neg = (int)(u >> 63);
	exp = (unsigned)((u >> 52) & 0x7ffu);
	frac = u & (((uint64_t)1 << 52) - 1);
	if (exp == 0x7ffu) {
		x->special = frac ? 2 : 1;
		x->m = 0;
		x->e = 0;
		return;
	}
	x->special = 0;
	if (exp == 0) {
		x->m = frac;
		x->e = -1074;
	} else {
		x->m = frac | ((uint64_t)1 << 52);
		x->e = (int)exp - 1075;
	}
}

/* floor(log10(v)) estimate for v = m * 2^e; the caller corrects it by digit counting. */
static int estimate_exp10(int e)
{
	int64_t t = (int64_t)e * 78913 + 4142932;   /* 78913/2^18 ~ log10(2); 52.5 * that */

	if (t >= 0)
		return (int)(t >> 18);
	return -(int)((-t + 262143) >> 18);
}

/* N = round_half_even(v * 10^k), v = m * 2^e > 0.  Returns 0 or -1 when out of range. */
static int scaled_round(const struct n2fp *x, int k, bn *out)
{
	bn num, den, q, r;
	int s;

	bn_set_u64(&num, x->m);
	bn_set_u64(&den, 1);
	if (k >= 0) {
		if (bn_mul_pow5(&num, (unsigned)k))
			return -1;
	} else {
		if (bn_mul_pow5(&den, (unsigned)-k))
			return -1;
	}
	s = x->e + k;
	if (s >= 0) {
		if (bn_shl(&num, (unsigned)s))
			return -1;
	} else {
		if (bn_shl(&den, (unsigned)-s))
			return -1;
	}
	if (den.n == 1 && den.d[0] == 1) {
		*out = num;
		return 0;
	}
	bn_divmod(&num, &den, &q, &r);
	{
		bn r2 = r;
		int c;

		bn_shl1(&r2);
		c = bn_cmp(&r2, &den);
		if (c > 0) {
			bn_add_small(&q, 1);
		} else if (c == 0) {
			if (q.n && (q.d[0] & 1u))
				bn_add_small(&q, 1);
		}
	}
	*out = q;
	return 0;
}

/* Compare v = m * 2^e (positive, finite) with 10^X exactly.  Returns -1, 0, 1. */
static int cmp_pow10(const struct n2fp *x, int X)
{
	bn a, b;
	int j = X >= 0 ? X : -X;
	int sh;
	int r;

	bn_set_u64(&a, x->m);
	bn_set_u64(&b, 1);
	if (X >= 0) {
		if (bn_mul_pow5(&b, (unsigned)j))
			return 0;
		sh = x->e - X;
		if (sh >= 0) {
			if (bn_shl(&a, (unsigned)sh))
				return 1;
		} else {
			if (bn_shl(&b, (unsigned)-sh))
				return -1;
		}
	} else {
		if (bn_mul_pow5(&a, (unsigned)j))
			return 1;
		sh = x->e + j;
		if (sh >= 0) {
			if (bn_shl(&a, (unsigned)sh))
				return 1;
		} else {
			if (bn_shl(&b, (unsigned)-sh))
				return -1;
		}
	}
	r = bn_cmp(&a, &b);
	return r < 0 ? -1 : r > 0 ? 1 : 0;
}

/* floor(log10(v)), exact: estimates from the binary exponent, then corrects with exact comparisons. */
static int true_exp10(const struct n2fp *x)
{
	uint64_t m = x->m;
	int b = 0;
	int X, guard = 0;

	while (m > 1) {
		m >>= 1;
		b++;
	}
	X = estimate_exp10(x->e + b - 52);
	while (cmp_pow10(x, X) < 0 && guard++ < 8)
		X--;
	while (cmp_pow10(x, X + 1) >= 0 && guard++ < 16)
		X++;
	return X;
}

/* Round |v| (nonzero, finite) to exactly `sig` significant digits.  dig gets sig digits and a NUL,
 * *exp10 is the decimal exponent of dig[0]. */
static int sig_round(const struct n2fp *x, int sig, char *dig, int *exp10)
{
	int X = true_exp10(x);
	int tries;

	for (tries = 0; tries < 8; tries++) {
		bn n;
		char all[DEC_MAX];
		int L;

		if (scaled_round(x, sig - 1 - X, &n))
			return -1;
		if (bn_is_zero(&n)) {
			X--;
			continue;
		}
		L = bn_to_dec(&n, all);
		if (L < 0)
			return -1;
		if (L == sig) {
			memcpy(dig, all, (size_t)sig);
			dig[sig] = 0;
			*exp10 = X;
			return 0;
		}
		X += L - sig;
	}
	return -1;
}

/* ============================================================================================================= */
/* output buffer                                                                                                */
/* ============================================================================================================= */

struct n2out {
	char *p;
	size_t cap;
	size_t n;
};

static void oput(struct n2out *o, char ch)
{
	if (o->p && o->n + 1 < o->cap)
		o->p[o->n] = ch;
	o->n++;
}

static void oputn(struct n2out *o, const char *s, size_t k)
{
	size_t i;
	for (i = 0; i < k; i++)
		oput(o, s[i]);
}

static void opad(struct n2out *o, char ch, size_t k)
{
	/* O(1) when the output is already truncated: a guest may pass a width of INT_MAX */
	if (o->p && o->n + 1 < o->cap) {
		size_t room = o->cap - 1 - o->n;
		size_t w = k < room ? k : room;
		if (w)
			memset(o->p + o->n, ch, w);
	}
	o->n += k;
}

static void oterm(struct n2out *o)
{
	if (o->p && o->cap)
		o->p[o->n < o->cap ? o->n : o->cap - 1] = 0;
}

/* ============================================================================================================= */
/* argument cursor                                                                                              */
/* ============================================================================================================= */

void n2fmt_args_regs(struct n2fmt_args *a, struct n2a32_cpu *c, unsigned first_slot)
{
	memset(a, 0, sizeof *a);
	a->c = c;
	a->mode = N2FMT_ARGS_REGS;
	a->next = first_slot;
}

int n2fmt_args_va(struct n2fmt_args *a, struct n2a32_cpu *c, uint32_t guest_va)
{
	memset(a, 0, sizeof *a);
	a->c = c;
	a->mode = N2FMT_ARGS_AREA;
	if (!guest_va) {
		a->fault = 1;
		return -1;
	}
	/* ARM EABI passes va_list as struct { void *__ap; } BY VALUE: the register holds the
	 * argument area itself, not a pointer to it (opus-dec1, HIGH finding; a call site in
	 * libvcodec_utility.so @0x1e59c builds the area on its stack and passes addw sp,#0x424).
	 * Loading through guest_va would take the first vararg as the area. */
	a->area = guest_va;
	return 0;
}

uint32_t n2fmt_arg32(struct n2fmt_args *a)
{
	struct n2a32_cpu *c = a->c;
	uint32_t v;

	if (a->mode == N2FMT_ARGS_REGS) {
		uint32_t i = a->next++;
		if (i < 4)
			v = c->r[i];
		else
			v = n2a32_ld32(c, c->r[13] + 4 * (i - 4));
	} else {
		v = n2a32_ld32(c, a->area + 4 * a->next);
		a->next++;
	}
	return v;
}

uint64_t n2fmt_arg64(struct n2fmt_args *a)
{
	uint32_t lo, hi;

	if (a->mode == N2FMT_ARGS_REGS) {
		if (a->next & 1)
			a->next++;              /* r1/r3 cannot hold the low half of a 64-bit argument */
		if (a->next == 3)
			a->next = 4;            /* ... and a 64-bit argument never starts in r3 */
	} else {
		uint32_t addr = (a->area + 4 * a->next + 7u) & ~7u;
		a->next = (addr - a->area) / 4;
	}
	lo = n2fmt_arg32(a);
	hi = n2fmt_arg32(a);
	return (uint64_t)lo | ((uint64_t)hi << 32);
}

double n2fmt_arg_double(struct n2fmt_args *a)
{
	uint64_t u = n2fmt_arg64(a);
	double d;
	memcpy(&d, &u, sizeof d);
	return d;
}

/* ============================================================================================================= */
/* conversions                                                                                                  */
/* ============================================================================================================= */

struct spec {
	int left, plus, space, alt, zero;
	int width;      /* -1 = absent */
	int prec;       /* -1 = absent */
	int len;        /* 0 none, 1 hh, 2 h, 3 l, 4 ll, 5 z, 6 j, 7 t, 8 L */
	char conv;
};

/* Emit a numeric body with optional zero padding after the first prefix_len characters. */
static void emit_field(struct n2out *o, const struct spec *s, const char *body, int blen, int prefix_len,
		       int allow_zero)
{
	int width = s->width < 0 ? 0 : s->width;

	if (width <= blen) {
		oputn(o, body, (size_t)blen);
		return;
	}
	if (s->left) {
		oputn(o, body, (size_t)blen);
		opad(o, ' ', (size_t)(width - blen));
		return;
	}
	if (s->zero && allow_zero) {
		oputn(o, body, (size_t)prefix_len);
		opad(o, '0', (size_t)(width - blen));
		oputn(o, body + prefix_len, (size_t)(blen - prefix_len));
		return;
	}
	opad(o, ' ', (size_t)(width - blen));
	oputn(o, body, (size_t)blen);
}

static void emit_int(struct n2out *o, const struct spec *s, struct n2a32_cpu *c, uint64_t mag, int neg,
		     unsigned base, int upper, int is_signed, int is_ptr)
{
	char body[PREC_MAX + 80];
	char dg[PREC_MAX + 80];
	char pre[4];
	int np = 0, nd = 0, zeros = 0, i;
	uint64_t v = mag;

	(void)c;
	if (is_ptr && v == 0) {
		static const char nil[] = "(nil)";
		emit_field(o, s, nil, 5, 0, 0);
		return;
	}
	if (v == 0) {
		if (s->prec != 0)
			dg[nd++] = '0';
	} else {
		char rev[70];
		int nr = 0;
		static const char ld[] = "0123456789abcdef";
		static const char ud[] = "0123456789ABCDEF";
		const char *digits = upper ? ud : ld;

		while (v && nr < (int)sizeof rev) {
			rev[nr++] = digits[v % base];
			v /= base;
		}
		while (nr && nd < (int)sizeof dg)
			dg[nd++] = rev[--nr];
	}
	if (s->prec > nd)
		zeros = s->prec - nd;
	if (neg && is_signed)
		pre[np++] = '-';
	else if (s->plus && is_signed)
		pre[np++] = '+';
	else if (s->space && is_signed)
		pre[np++] = ' ';      /* '+', ' ' and '#' do not apply to unsigned conversions */
	if ((s->alt || is_ptr) && base == 16 && mag != 0) {
		pre[np++] = '0';
		pre[np++] = upper ? 'X' : 'x';
	}
	if (s->alt && base == 8) {
		if (nd == 0)
			dg[nd++] = '0';                 /* %#.0o of 0 is "0" */
		else if (dg[0] != '0' && zeros == 0)
			zeros++;                        /* %#o of 8 is "010"; a precision zero already satisfies it */
	}
	if (s->zero && !s->left && s->prec < 0) {
		int total = np + zeros + nd;
		int width = s->width < 0 ? 0 : s->width;
		if (total < width) {
			int extra = width - total;
			int room = (int)sizeof(body) - np - nd;   /* emit_field pads the rest with opad */

			if (extra > room)
				extra = room;
			zeros += extra;
		}
	}
	/* body = prefix + zeros + digits */
	for (i = 0; i < np; i++)
		body[i] = pre[i];
	for (i = 0; i < zeros; i++)
		body[np + i] = '0';
	for (i = 0; i < nd; i++)
		body[np + zeros + i] = dg[i];
	emit_field(o, s, body, np + zeros + nd, np, s->prec < 0);   /* zero flag is ignored with a precision */
}

/* Integer argument fetch with the length rules of a 32-bit guest. */
static uint64_t arg_unsigned(struct n2fmt_args *a, int len)
{
	switch (len) {
	case 1: return (uint8_t)n2fmt_arg32(a);
	case 2: return (uint16_t)n2fmt_arg32(a);
	case 4: return n2fmt_arg64(a);
	case 5: return n2fmt_arg32(a);          /* size_t: 32-bit */
	case 6: return n2fmt_arg64(a);          /* intmax_t */
	case 7: return n2fmt_arg32(a);          /* ptrdiff_t */
	default: return n2fmt_arg32(a);         /* none, l (long is 32-bit), L, ... */
	}
}

static int64_t arg_signed(struct n2fmt_args *a, int len)
{
	switch (len) {
	case 1: return (int8_t)n2fmt_arg32(a);
	case 2: return (int16_t)n2fmt_arg32(a);
	case 4: return (int64_t)n2fmt_arg64(a);
	case 5: return (int32_t)n2fmt_arg32(a);
	case 6: return (int64_t)n2fmt_arg64(a);
	case 7: return (int32_t)n2fmt_arg32(a);
	default: return (int32_t)n2fmt_arg32(a);
	}
}

static uint64_t mask_len(uint64_t v, int len)
{
	switch (len) {
	case 1: return v & 0xffu;
	case 2: return v & 0xffffu;
	case 4: case 6: return v;
	default: return v & 0xffffffffu;
	}
}

static uint32_t guest_strnlen(struct n2a32_cpu *c, uint32_t a, uint32_t cap)
{
	uint32_t i;

	if (!a)
		return 0;
	for (i = 0; i < cap; i++)
		if (n2a32_ld8(c, a + i) == 0)
			break;
	return i;
}

static void emit_str(struct n2out *o, const struct spec *s, struct n2a32_cpu *c, uint32_t a)
{
	uint32_t len;
	int width = s->width < 0 ? 0 : s->width;

	if (!a) {
		/* bionic prints "(null)" and still applies the precision; glibc differs when precision < 6, and
		 * the difference is noted in STATUS.md. */
		static const char nil[] = "(null)";
		len = 6;
		if (s->prec >= 0 && (uint32_t)s->prec < len)
			len = (uint32_t)s->prec;
		if (width > (int)len && !s->left)
			opad(o, ' ', (size_t)(width - (int)len));
		oputn(o, nil, len);
		if (width > (int)len && s->left)
			opad(o, ' ', (size_t)(width - (int)len));
		return;
	}
	len = guest_strnlen(c, a, s->prec >= 0 ? (uint32_t)s->prec : 0xffffffffu);
	if (width > (int)len && !s->left)
		opad(o, ' ', (size_t)(width - (int)len));
	{
		uint32_t i;
		for (i = 0; i < len; i++)
			oput(o, (char)n2a32_ld8(c, a + i));
	}
	if (width > (int)len && s->left)
		opad(o, ' ', (size_t)(width - (int)len));
}

static void emit_char(struct n2out *o, const struct spec *s, char ch)
{
	int width = s->width < 0 ? 0 : s->width;

	if (width > 1 && !s->left)
		opad(o, ' ', (size_t)(width - 1));
	oput(o, ch);
	if (width > 1 && s->left)
		opad(o, ' ', (size_t)(width - 1));
}

/* %f: exactly `prec` digits after the point. */
static void fmt_fixed(struct n2out *o, const struct spec *s, double v)
{
	struct n2fp x;
	int p = s->prec < 0 ? 6 : s->prec;
	char body[PREC_MAX + 360];
	int bl = 0;

	if (p > PREC_MAX)
		p = PREC_MAX;
	fp_decode(v, &x);
	if (x.neg)
		body[bl++] = '-';
	else if (s->plus)
		body[bl++] = '+';
	else if (s->space)
		body[bl++] = ' ';
	if (x.special) {
		const char *w = x.special == 1 ? (s->conv == 'F' ? "INF" : "inf")
					       : (s->conv == 'F' ? "NAN" : "nan");
		memcpy(body + bl, w, 3);
		emit_field(o, s, body, bl + 3, bl, 0);
		return;
	}
	if (x.m == 0) {
		body[bl++] = '0';
		if (p > 0 || s->alt) {
			int i;
			body[bl++] = '.';
			for (i = 0; i < p; i++)
				body[bl++] = '0';
		}
		emit_field(o, s, body, bl, bl > 0 && (body[0] == '-' || body[0] == '+' || body[0] == ' ') ? 1 : 0, 1);
		return;
	}
	{
		bn n;
		char all[DEC_MAX];
		int L;

		if (scaled_round(&x, p, &n)) {
			static const char of[] = "overflow";
			oputn(o, of, 8);
			return;
		}
		L = bn_to_dec(&n, all);
		if (L < 0) {
			static const char of[] = "overflow";
			oputn(o, of, 8);
			return;
		}
		if (p == 0) {
			if (L > (int)sizeof body - bl - 1)
				L = (int)sizeof body - bl - 1;
			memcpy(body + bl, all, (size_t)L);
			bl += L;
			if (s->alt)
				body[bl++] = '.';
		} else if (L > p) {
			int ip = L - p;
			int i;
			if (ip > (int)sizeof body - bl - 2)
				ip = (int)sizeof body - bl - 2;
			memcpy(body + bl, all, (size_t)ip);
			bl += ip;
			body[bl++] = '.';
			for (i = 0; i < p && bl < (int)sizeof body - 1; i++)
				body[bl++] = all[ip + i];
		} else {
			int i;
			body[bl++] = '0';
			body[bl++] = '.';
			for (i = 0; i < p - L && bl < (int)sizeof body - 1; i++)
				body[bl++] = '0';
			if (L > (int)sizeof body - bl - 1)
				L = (int)sizeof body - bl - 1;
			memcpy(body + bl, all, (size_t)L);
			bl += L;
		}
	}
	emit_field(o, s, body, bl, (body[0] == '-' || body[0] == '+' || body[0] == ' ') ? 1 : 0, 1);
}

/* %e: one digit, point, p digits, exponent. */
static void fmt_exp(struct n2out *o, const struct spec *s, double v)
{
	struct n2fp x;
	int p = s->prec < 0 ? 6 : s->prec;
	char body[PREC_MAX + 32];
	char dig[PREC_MAX + 8];
	int bl = 0, X = 0, i;

	if (p > PREC_MAX)
		p = PREC_MAX;
	if (p + 1 > (int)sizeof dig - 1)
		p = (int)sizeof dig - 2;
	fp_decode(v, &x);
	if (x.neg)
		body[bl++] = '-';
	else if (s->plus)
		body[bl++] = '+';
	else if (s->space)
		body[bl++] = ' ';
	if (x.special) {
		const char *w = x.special == 1 ? (s->conv == 'E' ? "INF" : "inf")
					       : (s->conv == 'E' ? "NAN" : "nan");
		memcpy(body + bl, w, 3);
		emit_field(o, s, body, bl + 3, bl, 0);
		return;
	}
	if (x.m == 0) {
		dig[0] = '0';
		for (i = 1; i <= p; i++)
			dig[i] = '0';
		dig[p + 1] = 0;
		X = 0;
	} else if (sig_round(&x, p + 1, dig, &X)) {
		static const char of[] = "overflow";
		oputn(o, of, 8);
		return;
	}
	body[bl++] = dig[0];
	if (p > 0 || s->alt)
		body[bl++] = '.';
	for (i = 1; i <= p; i++)
		body[bl++] = dig[i];
	{
		char eb[16];
		int n = 0, expv = X < 0 ? -X : X;
		do {
			eb[n++] = (char)('0' + expv % 10);
			expv /= 10;
		} while (expv);
		body[bl++] = s->conv == 'E' ? 'E' : 'e';
		body[bl++] = X < 0 ? '-' : '+';
		while (n < 2)
			eb[n++] = '0';
		while (n)
			body[bl++] = eb[--n];
	}
	emit_field(o, s, body, bl, (body[0] == '-' || body[0] == '+' || body[0] == ' ') ? 1 : 0, 1);
}

/* %g: P significant digits, style chosen by the decimal exponent, trailing zeros removed unless '#'. */
static void fmt_general(struct n2out *o, const struct spec *s, double v)
{
	struct n2fp x;
	int P = s->prec < 0 ? 6 : s->prec;
	char body[PREC_MAX + 360];
	char dig[PREC_MAX + 8];
	int bl = 0, X = 0, i;

	if (P == 0)
		P = 1;
	if (P > PREC_MAX)
		P = PREC_MAX;
	fp_decode(v, &x);
	if (x.neg)
		body[bl++] = '-';
	else if (s->plus)
		body[bl++] = '+';
	else if (s->space)
		body[bl++] = ' ';
	if (x.special) {
		const char *w = x.special == 1 ? (s->conv == 'G' ? "INF" : "inf")
					       : (s->conv == 'G' ? "NAN" : "nan");
		memcpy(body + bl, w, 3);
		emit_field(o, s, body, bl + 3, bl, 0);
		return;
	}
	if (x.m == 0) {
		if (s->alt) {
			body[bl++] = '0';
			body[bl++] = '.';
			for (i = 1; i < P; i++)
				body[bl++] = '0';
		} else {
			body[bl++] = '0';
		}
		emit_field(o, s, body, bl, bl > 1 && (body[0] == '-' || body[0] == '+' || body[0] == ' ') ? 1 : 0,
			   1);
		return;
	}
	if (sig_round(&x, P, dig, &X)) {
		static const char of[] = "overflow";
		oputn(o, of, 8);
		return;
	}
	if (X < -4 || X >= P) {
		/* style e, with P-1 digits after the point */
		int last = P - 1;
		if (!s->alt)
			while (last > 0 && dig[last] == '0')
				last--;
		body[bl++] = dig[0];
		if (last > 0 || s->alt)
			body[bl++] = '.';
		for (i = 1; i <= last; i++)
			body[bl++] = dig[i];
		{
			char eb[16];
			int n = 0, expv = X < 0 ? -X : X;
			do {
				eb[n++] = (char)('0' + expv % 10);
				expv /= 10;
			} while (expv);
			body[bl++] = s->conv == 'G' ? 'E' : 'e';
			body[bl++] = X < 0 ? '-' : '+';
			while (n < 2)
				eb[n++] = '0';
			while (n)
				body[bl++] = eb[--n];
		}
	} else if (X >= 0) {
		int ip = X + 1;
		int last = P - 1;
		for (i = 0; i < ip; i++)
			body[bl++] = i < P ? dig[i] : '0';
		if (!s->alt)
			while (last >= ip && dig[last] == '0')
				last--;
		if (last >= ip) {
			body[bl++] = '.';
			for (i = ip; i <= last; i++)
				body[bl++] = dig[i];
		} else if (s->alt) {
			body[bl++] = '.';
		}
	} else {
		int zeros = -X - 1;
		int last = P - 1;
		if (!s->alt)
			while (last > 0 && dig[last] == '0')
				last--;
		body[bl++] = '0';
		body[bl++] = '.';
		for (i = 0; i < zeros; i++)
			body[bl++] = '0';
		for (i = 0; i <= last; i++)
			body[bl++] = dig[i];
	}
	emit_field(o, s, body, bl, (body[0] == '-' || body[0] == '+' || body[0] == ' ') ? 1 : 0, 1);
}

/* ============================================================================================================= */
/* n2fmt                                                                                                        */
/* ============================================================================================================= */

static int fmt_one(struct n2out *o, struct n2a32_cpu *c, const struct spec *s, struct n2fmt_args *a)
{
	switch (s->conv) {
	case 'd':
	case 'i': {
		int64_t v = arg_signed(a, s->len);
		int neg = v < 0;
		uint64_t mag = neg ? (uint64_t)(-(v + 1)) + 1 : (uint64_t)v;
		emit_int(o, s, c, mag, neg, 10, 0, 1, 0);
		return 0;
	}
	case 'u':
		emit_int(o, s, c, mask_len(arg_unsigned(a, s->len), s->len), 0, 10, 0, 0, 0);
		return 0;
	case 'o':
		emit_int(o, s, c, mask_len(arg_unsigned(a, s->len), s->len), 0, 8, 0, 0, 0);
		return 0;
	case 'x':
		emit_int(o, s, c, mask_len(arg_unsigned(a, s->len), s->len), 0, 16, 0, 0, 0);
		return 0;
	case 'X':
		emit_int(o, s, c, mask_len(arg_unsigned(a, s->len), s->len), 0, 16, 1, 0, 0);
		return 0;
	case 'p':
		emit_int(o, s, c, n2fmt_arg32(a), 0, 16, 0, 0, 1);
		return 0;
	case 'c':
		emit_char(o, s, (char)(n2fmt_arg32(a) & 0xffu));
		return 0;
	case 's':
		emit_str(o, s, c, n2fmt_arg32(a));
		return 0;
	case 'n': {
		uint32_t p = n2fmt_arg32(a);
		if (p) {
			uint64_t v = (uint64_t)o->n;
			switch (s->len) {
			case 1: n2a32_st8(c, p, (uint8_t)v); break;
			case 2: n2a32_st16(c, p, (uint16_t)v); break;
			case 4: case 6: n2a32_st64(c, p, v); break;
			default: n2a32_st32(c, p, (uint32_t)v); break;
			}
		}
		return 0;
	}
	case 'f':
	case 'F':
		fmt_fixed(o, s, n2fmt_arg_double(a));
		return 0;
	case 'e':
	case 'E':
		fmt_exp(o, s, n2fmt_arg_double(a));
		return 0;
	case 'g':
	case 'G':
		fmt_general(o, s, n2fmt_arg_double(a));
		return 0;
	default:
		return -1;
	}
}

int n2fmt(struct n2a32_cpu *c, char *out, size_t cap, uint32_t guest_fmt, struct n2fmt_args *a)
{
	struct n2out o;
	char fbuf[FMT_MAX];
	size_t fl = 0;
	int i = 0;

	o.p = out;
	o.cap = cap;
	o.n = 0;
	if (!guest_fmt)
		return -1;
	while (fl + 1 < sizeof fbuf) {
		char ch = (char)n2a32_ld8(c, guest_fmt + (uint32_t)fl);
		fbuf[fl++] = ch;
		if (!ch)
			break;
	}
	fbuf[sizeof fbuf - 1] = 0;
	while (fbuf[i]) {
		struct spec s;
		if (fbuf[i] != '%') {
			oput(&o, fbuf[i++]);
			continue;
		}
		i++;
		if (fbuf[i] == '%') {
			oput(&o, '%');
			i++;
			continue;
		}
		memset(&s, 0, sizeof s);
		s.width = -1;
		s.prec = -1;
		/* flags */
		for (;; i++) {
			if (fbuf[i] == '-') s.left = 1;
			else if (fbuf[i] == '+') s.plus = 1;
			else if (fbuf[i] == ' ') s.space = 1;
			else if (fbuf[i] == '#') s.alt = 1;
			else if (fbuf[i] == '0') s.zero = 1;
			else if (fbuf[i] == '\'') ;            /* grouping: no-op in the C locale */
			else break;
		}
		/* width */
		if (fbuf[i] == '*') {
			int w = (int32_t)n2fmt_arg32(a);
			i++;
			if (w < 0) {
				s.left = 1;
				s.width = -w;
			} else {
				s.width = w;
			}
		} else {
			int w = 0, any = 0;
			while (fbuf[i] >= '0' && fbuf[i] <= '9') {
				if (w < 100000000)          /* keep the accumulation inside int for absurd formats */
					w = w * 10 + (fbuf[i] - '0');
				i++;
				any = 1;
			}
			if (any)
				s.width = w;
		}
		/* precision */
		if (fbuf[i] == '.') {
			i++;
			if (fbuf[i] == '*') {
				int p = (int32_t)n2fmt_arg32(a);
				i++;
				s.prec = p < 0 ? -1 : p;
			} else {
				int p = 0, any = 0;
				while (fbuf[i] >= '0' && fbuf[i] <= '9') {
					if (p < 100000000)
						p = p * 10 + (fbuf[i] - '0');
					i++;
					any = 1;
				}
				s.prec = any ? p : 0;
			}
		}
		if (s.prec > PREC_MAX)
			s.prec = PREC_MAX;       /* documented cap: keeps every conversion's stack buffer bounded */
		/* length */
		if (fbuf[i] == 'h' && fbuf[i + 1] == 'h') {
			s.len = 1;
			i += 2;
		} else if (fbuf[i] == 'h') {
			s.len = 2;
			i++;
		} else if (fbuf[i] == 'l' && fbuf[i + 1] == 'l') {
			s.len = 4;
			i += 2;
		} else if (fbuf[i] == 'l') {
			s.len = 3;
			i++;
		} else if (fbuf[i] == 'z') {
			s.len = 5;
			i++;
		} else if (fbuf[i] == 'j') {
			s.len = 6;
			i++;
		} else if (fbuf[i] == 't') {
			s.len = 7;
			i++;
		} else if (fbuf[i] == 'L' || fbuf[i] == 'q') {
			s.len = 8;
			i++;
		}
		if (!fbuf[i]) {
			/* an incomplete conversion at the end: print the '%' literally, like a C library */
			oput(&o, '%');
			break;
		}
		s.conv = fbuf[i++];
		if (fmt_one(&o, c, &s, a) < 0) {
			/* unknown conversion: print the specification literally */
			oput(&o, '%');
			oput(&o, s.conv);
		}
	}
	oterm(&o);
	return o.n > INT_MAX ? INT_MAX : (int)o.n;
}

/* ============================================================================================================= */
/* scanner (sscanf and V33's fscanf forward)                                                                    */
/* ============================================================================================================= */

struct scan {
	struct n2a32_cpu *c;
	const struct n2fmt_input *in;
	int pushed;
	int has_push;
	int eof;
	uint32_t consumed;
};

static int sc_get(struct scan *s)
{
	int ch;

	if (s->has_push) {
		s->has_push = 0;
		return s->pushed;
	}
	ch = s->in->get(s->in->ctx);
	if (ch < 0) {
		s->eof = 1;
		return -1;
	}
	s->eof = 0;
	s->consumed++;
	return ch;
}

static void sc_unget(struct scan *s, int ch)
{
	if (ch < 0)
		return;
	if (s->in->unget)
		s->in->unget(s->in->ctx, ch);
	else {
		s->pushed = ch;
		s->has_push = 1;
	}
}

static int sc_isspace(int ch) { return ch == ' ' || (ch >= '\t' && ch <= '\r'); }

static int sc_digit(int ch, int base)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0' < base ? ch - '0' : 99;
	if (ch >= 'a' && ch <= 'z')
		return ch - 'a' + 10 < base ? ch - 'a' + 10 : 99;
	if (ch >= 'A' && ch <= 'Z')
		return ch - 'A' + 10 < base ? ch - 'A' + 10 : 99;
	return 99;
}

static void sc_store(struct n2a32_cpu *c, int len, uint32_t addr, uint64_t v)
{
	if (!addr)
		return;
	switch (len) {
	case 1: n2a32_st8(c, addr, (uint8_t)v); break;
	case 2: n2a32_st16(c, addr, (uint16_t)v); break;
	case 4: case 6: n2a32_st64(c, addr, v); break;
	default: n2a32_st32(c, addr, (uint32_t)v); break;
	}
}

int n2fmt_vscan(struct n2a32_cpu *c, const struct n2fmt_input *in, uint32_t guest_fmt, struct n2fmt_args *a)
{
	struct scan sc;
	char fbuf[FMT_MAX];
	size_t fl = 0;
	int i = 0, assigned = 0, eof_before = 0;

	sc.c = c;
	sc.in = in;
	sc.pushed = 0;
	sc.has_push = 0;
	sc.eof = 0;
	sc.consumed = 0;
	if (!guest_fmt)
		return -1;
	while (fl + 1 < sizeof fbuf) {
		char ch = (char)n2a32_ld8(c, guest_fmt + (uint32_t)fl);
		fbuf[fl++] = ch;
		if (!ch)
			break;
	}
	fbuf[sizeof fbuf - 1] = 0;

	while (fbuf[i]) {
		if (sc_isspace((unsigned char)fbuf[i])) {
			int ch;
			while (sc_isspace((unsigned char)fbuf[i]))
				i++;
			do {
				ch = sc_get(&sc);
			} while (sc_isspace(ch));
			sc_unget(&sc, ch);
			continue;
		}
		if (fbuf[i] != '%') {
			int ch = sc_get(&sc);
			if (ch < 0) {
				if (!assigned)
					eof_before = 1;
				break;
			}
			if (ch != (unsigned char)fbuf[i])
				break;
			i++;
			continue;
		}
		i++;
		if (fbuf[i] == '%') {
			int ch = sc_get(&sc);
			if (ch < 0) {
				if (!assigned)
					eof_before = 1;
				break;
			}
			if (ch != '%')
				break;
			i++;
			continue;
		}
		{
			int suppress = 0, width = -1, len = 0;
			char conv;

			if (fbuf[i] == '*') {
				suppress = 1;
				i++;
			}
			if (fbuf[i] >= '0' && fbuf[i] <= '9') {
				width = 0;
				while (fbuf[i] >= '0' && fbuf[i] <= '9') {
					if (width < 100000000)
						width = width * 10 + (fbuf[i] - '0');
					i++;
				}
			}
			if (fbuf[i] == 'h' && fbuf[i + 1] == 'h') {
				len = 1;
				i += 2;
			} else if (fbuf[i] == 'h') {
				len = 2;
				i++;
			} else if (fbuf[i] == 'l' && fbuf[i + 1] == 'l') {
				len = 4;
				i += 2;
			} else if (fbuf[i] == 'l') {
				len = 3;
				i++;
			} else if (fbuf[i] == 'z') {
				len = 5;
				i++;
			} else if (fbuf[i] == 'j') {
				len = 6;
				i++;
			} else if (fbuf[i] == 't') {
				len = 7;
				i++;
			} else if (fbuf[i] == 'L') {
				len = 8;
				i++;
			}
			if (!fbuf[i]) {
				if (!assigned)
					eof_before = 1;
				break;
			}
			conv = fbuf[i++];
			if (conv == 'n') {
				uint32_t p = suppress ? 0 : n2fmt_arg32(a);
				if (p)
					sc_store(c, len, p, sc.consumed);
				continue;
			}
			if (conv == 'c') {
				int w = width < 0 ? 1 : width;
				uint32_t p = suppress ? 0 : n2fmt_arg32(a);
				int k, first = 1;
				for (k = 0; k < w; k++) {
					int ch = sc_get(&sc);
					if (ch < 0) {
						if (first) {
							if (!assigned)
								eof_before = 1;
							goto done;
						}
						break;
					}
					first = 0;
					if (p)
						n2a32_st8(c, p + (uint32_t)k, (uint8_t)ch);
				}
				if (first) {
					if (!assigned && sc.eof)
						eof_before = 1;
					goto done;
				}
				if (!suppress)
					assigned++;
				continue;
			}
			if (conv == 's') {
				int ch;
				uint32_t p = suppress ? 0 : n2fmt_arg32(a);
				int k = 0, first = 1;
				do {
					ch = sc_get(&sc);
				} while (sc_isspace(ch));
				while (ch >= 0 && !sc_isspace(ch) && (width < 0 || k < width)) {
					first = 0;
					if (p)
						n2a32_st8(c, p + (uint32_t)k, (uint8_t)ch);
					k++;
					ch = sc_get(&sc);
				}
				sc_unget(&sc, ch);
				if (first) {
					if (!assigned && sc.eof)
						eof_before = 1;
					goto done;
				}
				if (p)
					n2a32_st8(c, p + (uint32_t)k, 0);
				if (!suppress)
					assigned++;
				continue;
			}
			if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'o' || conv == 'x' ||
			    conv == 'X' || conv == 'p') {
				int ch, neg = 0, base = 10, any = 0, ovf = 0, zero_pref = 0;
				uint64_t v = 0;
				uint32_t p = suppress ? 0 : n2fmt_arg32(a);
				uint64_t limit = (conv == 'd' || conv == 'i') ? ((uint64_t)1 << 63) : ~(uint64_t)0;

				if (conv == 'o')
					base = 8;
				else if (conv == 'x' || conv == 'X' || conv == 'p')
					base = 16;
				do {
					ch = sc_get(&sc);
				} while (sc_isspace(ch));
				if (ch == '+' || ch == '-') {
					neg = ch == '-';
					ch = sc_get(&sc);
				}
				if (conv == 'i' && ch == '0') {
					int nx = sc_get(&sc);
					if (nx == 'x' || nx == 'X') {
						base = 16;
						ch = sc_get(&sc);
					} else {
						base = 8;
						sc_unget(&sc, nx);
					}
				} else if ((conv == 'x' || conv == 'X' || conv == 'p') && ch == '0') {
					int nx = sc_get(&sc);
					if (nx == 'x' || nx == 'X') {
						zero_pref = 1;     /* the C library accepts the 0x prefix for %x too */
						ch = sc_get(&sc);
					} else {
						sc_unget(&sc, nx);
					}
				}
				{
					int k = 0;
					while (ch >= 0 && (width < 0 || k < width)) {
						int d = sc_digit(ch, base);
						if (d >= base)
							break;
						any = 1;
						if (v > (limit - (uint64_t)d) / (unsigned)base) {
							ovf = 1;
							v = limit;
						} else if (!ovf) {
							v = v * (unsigned)base + (unsigned)d;
						}
						k++;
						ch = sc_get(&sc);
					}
					sc_unget(&sc, ch);
				}
				if (!any) {
					if (zero_pref) {
						any = 1;             /* "0x" with no digit after it reads as zero */
					} else {
						if (!assigned && sc.eof)
							eof_before = 1;
						goto done;
					}
				}
				if (neg)
					v = (uint64_t)0 - v;      /* two's complement; LLONG_MIN is not negatable */
				if (!suppress) {
					sc_store(c, len, p, v);
					assigned++;
				}
				continue;
			}
			/* unknown conversion: stop */
			if (!assigned)
				eof_before = 1;
			goto done;
		}
	}
done:
	return eof_before ? -1 : assigned;
}

/* ============================================================================================================= */
/* guest-memory formats and the shims                                                                           */
/* ============================================================================================================= */

/* Format into guest memory.  `cap` includes the NUL (snprintf); unlimited == 1 means sprintf (cap is ignored, the
 * exact length is allocated).  Returns the untruncated length, or -1. */
static int format_to_guest(struct n2a32_cpu *c, uint32_t dst, uint32_t cap, uint32_t guest_fmt,
			   struct n2fmt_args *a, int unlimited)
{
	struct n2fmt_args save = *a;
	int len;
	size_t n;

	len = n2fmt(c, NULL, 0, guest_fmt, a);
	if (len < 0)
		return -1;
	if (!dst)
		return len;
	n = unlimited ? (size_t)len + 1 : (size_t)cap;
	if (n == 0)
		return len;
	if ((size_t)len + 1 < n)
		n = (size_t)len + 1;
	if (n > (size_t)64 << 20)
		return -1;                    /* refuse to build an absurd host buffer; the guest asked for too much */
	{
		char *tmp = (char *)malloc(n);
		if (!tmp)
			return -1;
		*a = save;
		n2fmt(c, tmp, n, guest_fmt, a);
		memcpy(n2a32_ptr(c->mem, dst), tmp, n);
		free(tmp);
	}
	return len;
}

void n2fmt_sh_snprintf(struct n2a32_cpu *c)
{
	uint32_t dst = n2shim_arg(c, 0), cap = n2shim_arg(c, 1), fmt = n2shim_arg(c, 2);
	struct n2fmt_args a;

	n2fmt_args_regs(&a, c, 3);
	n2shim_ret(c, (uint32_t)format_to_guest(c, dst, cap, fmt, &a, 0));
}

void n2fmt_sh_sprintf(struct n2a32_cpu *c)
{
	uint32_t dst = n2shim_arg(c, 0), fmt = n2shim_arg(c, 1);
	struct n2fmt_args a;

	n2fmt_args_regs(&a, c, 2);
	n2shim_ret(c, (uint32_t)format_to_guest(c, dst, 0, fmt, &a, 1));
}

void n2fmt_sh_vsnprintf(struct n2a32_cpu *c)
{
	uint32_t dst = n2shim_arg(c, 0), cap = n2shim_arg(c, 1), fmt = n2shim_arg(c, 2);
	struct n2fmt_args a;

	if (n2fmt_args_va(&a, c, n2shim_arg(c, 3)) < 0) {
		n2shim_ret(c, (uint32_t)-1);
		return;
	}
	n2shim_ret(c, (uint32_t)format_to_guest(c, dst, cap, fmt, &a, 0));
}

void n2fmt_sh_vsprintf(struct n2a32_cpu *c)
{
	uint32_t dst = n2shim_arg(c, 0), fmt = n2shim_arg(c, 1);
	struct n2fmt_args a;

	if (n2fmt_args_va(&a, c, n2shim_arg(c, 2)) < 0) {
		n2shim_ret(c, (uint32_t)-1);
		return;
	}
	n2shim_ret(c, (uint32_t)format_to_guest(c, dst, 0, fmt, &a, 1));
}

void n2fmt_sh_snprintf_chk(struct n2a32_cpu *c)
{
	/* (char *dest, size_t len, int flags, size_t destlen, const char *fmt, ...) */
	uint32_t dst = n2shim_arg(c, 0), cap = n2shim_arg(c, 1);
	uint32_t destlen = n2shim_arg(c, 3), fmt = n2shim_arg(c, 4);
	struct n2fmt_args a;

	if (cap > destlen) {              /* bionic's __snprintf_chk refuses len > destlen */
		c->stop_request = 1;
		cap = destlen;
	}
	n2fmt_args_regs(&a, c, 5);
	n2shim_ret(c, (uint32_t)format_to_guest(c, dst, cap, fmt, &a, 0));
}

void n2fmt_sh_vsnprintf_chk(struct n2a32_cpu *c)
{
	/* (char *dest, size_t len, int flags, size_t destlen, const char *fmt, va_list ap) */
	uint32_t dst = n2shim_arg(c, 0), cap = n2shim_arg(c, 1);
	uint32_t destlen = n2shim_arg(c, 3), fmt = n2shim_arg(c, 4);
	struct n2fmt_args a;

	if (cap > destlen) {
		c->stop_request = 1;
		cap = destlen;
	}
	if (n2fmt_args_va(&a, c, n2shim_arg(c, 5)) < 0) {
		n2shim_ret(c, (uint32_t)-1);
		return;
	}
	n2shim_ret(c, (uint32_t)format_to_guest(c, dst, cap, fmt, &a, 0));
}

void n2fmt_sh_vsprintf_chk(struct n2a32_cpu *c)
{
	/* (char *dest, int flags, size_t destlen, const char *fmt, va_list ap) */
	uint32_t dst = n2shim_arg(c, 0), destlen = n2shim_arg(c, 2), fmt = n2shim_arg(c, 3);
	struct n2fmt_args a;
	int len;

	if (n2fmt_args_va(&a, c, n2shim_arg(c, 4)) < 0) {
		n2shim_ret(c, (uint32_t)-1);
		return;
	}
	/* Format with a hard cap so a guest overflow of the fortified buffer cannot happen; signal it. */
	len = format_to_guest(c, dst, destlen, fmt, &a, 0);
	if (len >= 0 && (uint32_t)len >= destlen)
		c->stop_request = 1;
	n2shim_ret(c, (uint32_t)len);
}

/* sscanf: the source is a guest NUL-terminated string. */
struct sscan_src {
	struct n2a32_cpu *c;
	uint32_t p;
};

static int sscan_get(void *ctx)
{
	struct sscan_src *s = (struct sscan_src *)ctx;
	uint8_t ch = n2a32_ld8(s->c, s->p);

	if (!ch)
		return -1;
	s->p++;
	return ch;
}

void n2fmt_sh_sscanf(struct n2a32_cpu *c)
{
	uint32_t str = n2shim_arg(c, 0), fmt = n2shim_arg(c, 1);
	struct n2fmt_args a;
	struct sscan_src src;
	struct n2fmt_input in;

	n2fmt_args_regs(&a, c, 2);
	src.c = c;
	src.p = str;
	in.get = sscan_get;
	in.unget = NULL;
	in.ctx = &src;
	n2shim_ret(c, (uint32_t)n2fmt_vscan(c, &in, fmt, &a));
}
