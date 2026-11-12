/* SPDX-License-Identifier: MIT */
/*
 * n2shim_string.c -- string, memory and conversion shims for the guest (package V30).
 *
 * Every pointer below is a guest address.  A guest address 0 is the null pointer: functions that must return a
 * pointer return 0, functions that return a length return 0, and comparison functions treat it as an empty
 * string.  No function here dereferences a null guest pointer (n2a32_ptr() maps 0 to NULL) and none prints,
 * aborts or exits the host.
 *
 * The guest heap (n2shim_heap.c) is not used by this file.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"

/* n2shim_format.c: format entry points that live in n2shim_string[] (see n2fmt.h). */
void n2fmt_sh_snprintf(struct n2a32_cpu *c);
void n2fmt_sh_sprintf(struct n2a32_cpu *c);
void n2fmt_sh_vsnprintf(struct n2a32_cpu *c);
void n2fmt_sh_vsprintf(struct n2a32_cpu *c);
void n2fmt_sh_sscanf(struct n2a32_cpu *c);
void n2fmt_sh_snprintf_chk(struct n2a32_cpu *c);
void n2fmt_sh_vsnprintf_chk(struct n2a32_cpu *c);
void n2fmt_sh_vsprintf_chk(struct n2a32_cpu *c);

/* ---- memory -------------------------------------------------------------------------------------------------- */

static void sh_memcpy(struct n2a32_cpu *c)
{
	uint8_t *d = n2a32_ptr(c->mem, n2shim_arg(c, 0));
	const uint8_t *s = n2a32_ptr(c->mem, n2shim_arg(c, 1));
	uint32_t n = n2shim_arg(c, 2);

	if (d && s && n)
		memcpy(d, s, n);
	n2shim_ret(c, n2shim_arg(c, 0));
}

static void sh_memmove(struct n2a32_cpu *c)
{
	uint8_t *d = n2a32_ptr(c->mem, n2shim_arg(c, 0));
	const uint8_t *s = n2a32_ptr(c->mem, n2shim_arg(c, 1));
	uint32_t n = n2shim_arg(c, 2);

	if (d && s && n)
		memmove(d, s, n);
	n2shim_ret(c, n2shim_arg(c, 0));
}

static void sh_memset(struct n2a32_cpu *c)
{
	uint8_t *d = n2a32_ptr(c->mem, n2shim_arg(c, 0));
	uint32_t n = n2shim_arg(c, 2);

	if (d && n)
		memset(d, (int)(n2shim_arg(c, 1) & 0xff), n);
	n2shim_ret(c, n2shim_arg(c, 0));
}

static void sh_memcmp(struct n2a32_cpu *c)
{
	const uint8_t *a = n2a32_ptr(c->mem, n2shim_arg(c, 0));
	const uint8_t *b = n2a32_ptr(c->mem, n2shim_arg(c, 1));
	uint32_t n = n2shim_arg(c, 2);
	uint32_t i;
	int r = 0;

	if (n) {
		if (!a || !b) {
			r = a ? 1 : b ? -1 : 0;
		} else {
			for (i = 0; i < n; i++)
				if (a[i] != b[i]) {
					r = a[i] < b[i] ? -1 : 1;
					break;
				}
		}
	}
	n2shim_ret(c, (uint32_t)r);
}

static void sh_memchr(struct n2a32_cpu *c)
{
	const uint8_t *a = n2a32_ptr(c->mem, n2shim_arg(c, 0));
	uint32_t n = n2shim_arg(c, 2);
	uint8_t want = (uint8_t)n2shim_arg(c, 1);
	uint32_t i;

	if (!a || !n) {
		n2shim_ret(c, 0);
		return;
	}
	for (i = 0; i < n; i++)
		if (a[i] == want) {
			n2shim_ret(c, n2shim_arg(c, 0) + i);
			return;
		}
	n2shim_ret(c, 0);
}

/* ---- strings ------------------------------------------------------------------------------------------------ */

/* Guest strlen with a cap; the cap keeps a missing terminator from reading the whole 4 GiB. */
static uint32_t g_strnlen(struct n2a32_cpu *c, uint32_t a, uint32_t cap)
{
	uint32_t i;

	if (!a)
		return 0;
	for (i = 0; i < cap; i++)
		if (n2a32_ld8(c, a + i) == 0)
			break;
	return i;
}

static void sh_strlen(struct n2a32_cpu *c)
{
	n2shim_ret(c, g_strnlen(c, n2shim_arg(c, 0), 0xffffffffu));
}

static void sh_strnlen(struct n2a32_cpu *c)
{
	n2shim_ret(c, g_strnlen(c, n2shim_arg(c, 0), n2shim_arg(c, 1)));
}

static int g_strcmp(struct n2a32_cpu *c, uint32_t a, uint32_t b)
{
	uint32_t i = 0;

	for (;;) {
		uint8_t x = a ? n2a32_ld8(c, a + i) : 0;
		uint8_t y = b ? n2a32_ld8(c, b + i) : 0;
		if (x != y)
			return x < y ? -1 : 1;
		if (!x)
			return 0;
		i++;
	}
}

static void sh_strcmp(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)g_strcmp(c, n2shim_arg(c, 0), n2shim_arg(c, 1)));
}

static void sh_strncmp(struct n2a32_cpu *c)
{
	uint32_t a = n2shim_arg(c, 0), b = n2shim_arg(c, 1), n = n2shim_arg(c, 2), i;
	int r = 0;

	for (i = 0; i < n; i++) {
		uint8_t x = a ? n2a32_ld8(c, a + i) : 0;
		uint8_t y = b ? n2a32_ld8(c, b + i) : 0;
		if (x != y) {
			r = x < y ? -1 : 1;
			break;
		}
		if (!x)
			break;
	}
	n2shim_ret(c, (uint32_t)r);
}

static void sh_strncpy(struct n2a32_cpu *c)
{
	uint32_t d = n2shim_arg(c, 0), s = n2shim_arg(c, 1), n = n2shim_arg(c, 2), i;

	if (d) {
		for (i = 0; i < n; i++) {
			uint8_t ch = s ? n2a32_ld8(c, s + i) : 0;
			n2a32_st8(c, d + i, ch);
			if (!ch) {
				i++;
				break;
			}
		}
		for (; i < n; i++)           /* strncpy pads the rest with NUL */
			n2a32_st8(c, d + i, 0);
	}
	n2shim_ret(c, d);
}

static void sh_strcpy(struct n2a32_cpu *c)
{
	uint32_t d = n2shim_arg(c, 0), s = n2shim_arg(c, 1), i = 0;
	uint8_t ch;

	if (!d) {
		n2shim_ret(c, 0);
		return;
	}
	do {
		ch = s ? n2a32_ld8(c, s + i) : 0;
		n2a32_st8(c, d + i, ch);
		i++;
	} while (ch && i);
	n2shim_ret(c, d);
}

static void sh_strcat(struct n2a32_cpu *c)
{
	uint32_t d = n2shim_arg(c, 0), s = n2shim_arg(c, 1);
	uint32_t i, j = 0;
	uint8_t ch;

	if (!d) {
		n2shim_ret(c, 0);
		return;
	}
	for (i = 0; i < 0xffffffffu && n2a32_ld8(c, d + i); i++)
		;
	do {
		ch = s ? n2a32_ld8(c, s + j) : 0;
		n2a32_st8(c, d + i + j, ch);
		j++;
	} while (ch && j);
	n2shim_ret(c, d);
}

static void sh_strchr(struct n2a32_cpu *c)
{
	uint32_t a = n2shim_arg(c, 0), i;
	uint8_t want = (uint8_t)n2shim_arg(c, 1);

	if (!a) {
		n2shim_ret(c, 0);
		return;
	}
	for (i = 0;; i++) {
		uint8_t ch = n2a32_ld8(c, a + i);
		if (ch == want) {
			n2shim_ret(c, a + i);
			return;
		}
		if (!ch)
			break;
	}
	n2shim_ret(c, 0);
}

static void sh_strrchr(struct n2a32_cpu *c)
{
	uint32_t a = n2shim_arg(c, 0), i;
	uint8_t want = (uint8_t)n2shim_arg(c, 1), ch;
	uint32_t last = 0;

	if (!a) {
		n2shim_ret(c, 0);
		return;
	}
	for (i = 0;; i++) {
		ch = n2a32_ld8(c, a + i);
		if (ch == want)
			last = a + i;
		if (!ch)
			break;
	}
	n2shim_ret(c, last);
}

static void sh_strstr(struct n2a32_cpu *c)
{
	uint32_t h = n2shim_arg(c, 0), nd = n2shim_arg(c, 1), i, j;

	if (!h || !nd) {
		n2shim_ret(c, 0);
		return;
	}
	if (n2a32_ld8(c, nd) == 0) {
		n2shim_ret(c, h);
		return;
	}
	for (i = 0; n2a32_ld8(c, h + i); i++) {
		for (j = 0;; j++) {
			uint8_t want = n2a32_ld8(c, nd + j);
			if (!want) {
				n2shim_ret(c, h + i);
				return;
			}
			if (n2a32_ld8(c, h + i + j) != want)
				break;
		}
	}
	n2shim_ret(c, 0);
}

static void sh_strtok_r(struct n2a32_cpu *c)
{
	uint32_t s = n2shim_arg(c, 0), del = n2shim_arg(c, 1), save = n2shim_arg(c, 2);
	uint32_t p, start, i;

	if (!save) {
		n2shim_ret(c, 0);
		return;
	}
	p = s ? s : n2a32_ld32(c, save);
	if (!p) {
		n2shim_ret(c, 0);
		return;
	}
	/* skip leading delimiters */
	for (;;) {
		uint8_t ch = n2a32_ld8(c, p);
		int isdel = 0;
		if (!ch)
			break;
		for (i = 0; del && n2a32_ld8(c, del + i); i++)
			if (n2a32_ld8(c, del + i) == ch) {
				isdel = 1;
				break;
			}
		if (!isdel)
			break;
		p++;
	}
	if (!n2a32_ld8(c, p)) {
		n2a32_st32(c, save, 0);
		n2shim_ret(c, 0);
		return;
	}
	start = p;
	for (;;) {
		uint8_t ch = n2a32_ld8(c, p);
		int isdel = 0;
		if (!ch)
			break;
		for (i = 0; del && n2a32_ld8(c, del + i); i++)
			if (n2a32_ld8(c, del + i) == ch) {
				isdel = 1;
				break;
			}
		if (isdel) {
			n2a32_st8(c, p, 0);
			n2a32_st32(c, save, p + 1);
			n2shim_ret(c, start);
			return;
		}
		p++;
	}
	n2a32_st32(c, save, 0);
	n2shim_ret(c, start);
}

/* The C library's strtok keeps its state in static storage and is documented as not thread-safe; so is this.
 * The guest is one process with several threads, so a guest that uses strtok from two threads is already wrong. */
static uint32_t g_strtok_state;

static void sh_strtok(struct n2a32_cpu *c)
{
	uint32_t s = n2shim_arg(c, 0), del = n2shim_arg(c, 1);
	uint32_t start, i;

	if (!s)
		s = g_strtok_state;
	if (!s) {
		n2shim_ret(c, 0);
		return;
	}
	for (;;) {                                   /* skip leading delimiters */
		uint8_t ch = n2a32_ld8(c, s);
		int isdel = 0;
		if (!ch)
			break;
		for (i = 0; del && n2a32_ld8(c, del + i); i++)
			if (n2a32_ld8(c, del + i) == ch) {
				isdel = 1;
				break;
			}
		if (!isdel)
			break;
		s++;
	}
	if (!n2a32_ld8(c, s)) {
		g_strtok_state = 0;
		n2shim_ret(c, 0);
		return;
	}
	start = s;
	for (;;) {
		uint8_t ch = n2a32_ld8(c, s);
		int isdel = 0;
		if (!ch)
			break;
		for (i = 0; del && n2a32_ld8(c, del + i); i++)
			if (n2a32_ld8(c, del + i) == ch) {
				isdel = 1;
				break;
			}
		if (isdel) {
			n2a32_st8(c, s, 0);
			g_strtok_state = s + 1;
			n2shim_ret(c, start);
			return;
		}
		s++;
	}
	g_strtok_state = 0;
	n2shim_ret(c, start);
}

/* ---- strerror / strsignal ------------------------------------------------------------------------------------ */

/* One guest page shared by the two, allocated on first use.  Both functions return a pointer to static storage
 * in bionic as well, so a caller keeping the pointer across another call loses the same way. */
static uint32_t g_msg_buf;

static uint32_t msg_buffer(struct n2a32_cpu *c)
{
	if (!g_msg_buf && c->mem)
		g_msg_buf = n2a32_mem_alloc(c->mem, 128, 16);
	return g_msg_buf;
}

static const char *const g_errno_text[] = {
	"Success", "Operation not permitted", "No such file or directory", "No such process",
	"Interrupted system call", "I/O error", "No such device or address", "Argument list too long",
	"Exec format error", "Bad file descriptor", "No child processes", "Resource temporarily unavailable",
	"Cannot allocate memory", "Permission denied", "Bad address", "Block device required",
	"Device or resource busy", "File exists", "Cross-device link", "No such device",
	"Not a directory", "Is a directory", "Invalid argument", "Too many open files in system",
	"Too many open files", "Inappropriate ioctl for device", "Text file busy", "File too large",
	"No space left on device", "Illegal seek", "Read-only file system", "Too many links",
	"Broken pipe", "Domain error", "Result too large", "No message of desired type",
	"Identifier removed", "Channel number out of range", "Level 2 not synchronized", "Level 3 halted",
};

static const char *const g_signal_text[] = {
	"Unknown signal", "Hangup", "Interrupt", "Quit", "Illegal instruction", "Trace/breakpoint trap",
	"Aborted", "Bus error", "Floating point exception", "Killed", "User defined signal 1",
	"Segmentation fault", "User defined signal 2", "Broken pipe", "Alarm clock", "Terminated",
	"Stack fault", "Child exited", "Continued", "Stopped (signal)", "Stopped", "Stopped (tty input)",
	"Stopped (tty output)", "Urgent I/O condition", "CPU time limit exceeded", "File size limit exceeded",
	"Virtual timer expired", "Profiling timer expired", "Window changed", "I/O possible",
	"Power failure", "Bad system call",
};

static void put_guest_str(struct n2a32_cpu *c, uint32_t dst, const char *s, uint32_t cap)
{
	uint32_t i;

	if (!dst)
		return;
	for (i = 0; i + 1 < cap && s[i]; i++)
		n2a32_st8(c, dst + i, (uint8_t)s[i]);
	n2a32_st8(c, dst + i, 0);
}

static void sh_strerror(struct n2a32_cpu *c)
{
	int e = (int32_t)n2shim_arg(c, 0);
	uint32_t b = msg_buffer(c);
	char tmp[48];

	if (!b) {
		n2shim_ret(c, 0);
		return;
	}
	if (e >= 0 && (unsigned)e < sizeof g_errno_text / sizeof g_errno_text[0])
		put_guest_str(c, b, g_errno_text[e], 128);
	else {
		/* "Unknown error N" as bionic prints it */
		unsigned n = 0, v = (unsigned)e;
		char rev[16];
		const char pre[] = "Unknown error ";
		unsigned k = 0;
		while (pre[k]) {
			tmp[k] = pre[k];
			k++;
		}
		do {
			rev[n++] = (char)('0' + v % 10);
			v /= 10;
		} while (v);
		while (n)
			tmp[k++] = rev[--n];
		tmp[k] = 0;
		put_guest_str(c, b, tmp, 128);
	}
	n2shim_ret(c, b);
}

static void sh_strsignal(struct n2a32_cpu *c)
{
	int s = (int32_t)n2shim_arg(c, 0);
	uint32_t b = msg_buffer(c);

	if (!b) {
		n2shim_ret(c, 0);
		return;
	}
	if (s > 0 && (unsigned)s < sizeof g_signal_text / sizeof g_signal_text[0])
		put_guest_str(c, b, g_signal_text[s], 128);
	else
		put_guest_str(c, b, "Unknown signal", 128);
	n2shim_ret(c, b);
}

/* ---- integer parsing ----------------------------------------------------------------------------------------- */

static int g_isspace(uint8_t c)
{
	return c == ' ' || (c >= '\t' && c <= '\r');
}

static int g_digit_val(uint8_t c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'z')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	return 99;
}

/* strto[u]l[l] over guest memory.  `bits` is the guest's width for the result (32 for long, 64 for long long);
 * is_signed selects the saturation limit; base 0 means "detect". */
static uint64_t g_strtox(struct n2a32_cpu *c, uint32_t s, uint32_t endp, int base, int is_signed, int bits,
			 int *neg_out, int *overflow)
{
	uint32_t p = s, i;
	uint64_t v = 0;
	int neg = 0, any = 0, ovf = 0;
	uint64_t limit;

	*neg_out = 0;
	*overflow = 0;
	if (base != 0 && (base < 2 || base > 36)) {
		if (endp)
			n2a32_st32(c, endp, s);
		return 0;
	}
	if (!s) {
		if (endp)
			n2a32_st32(c, endp, s);
		return 0;
	}
	while (g_isspace(n2a32_ld8(c, p)))
		p++;
	if (n2a32_ld8(c, p) == '+' || n2a32_ld8(c, p) == '-') {
		neg = n2a32_ld8(c, p) == '-';
		p++;
	}
	if ((base == 0 || base == 16) && n2a32_ld8(c, p) == '0' &&
	    (n2a32_ld8(c, p + 1) == 'x' || n2a32_ld8(c, p + 1) == 'X')) {
		base = 16;
		p += 2;
		/* "0x" without digits is not a number; the 0 is */
		if (g_digit_val(n2a32_ld8(c, p)) >= 16) {
			p -= 2;
			base = 0;
		}
	}
	if (base == 0) {
		if (n2a32_ld8(c, p) == '0')
			base = 8;
		else
			base = 10;
	}
	if (is_signed)
		limit = neg ? ((uint64_t)1 << (bits - 1)) : (((uint64_t)1 << (bits - 1)) - 1);
	else
		limit = bits == 64 ? ~(uint64_t)0 : ((uint64_t)1 << bits) - 1;
	for (i = 0; i < 0xffffffffu; i++) {
		int d = g_digit_val(n2a32_ld8(c, p + i));
		if (d >= base)
			break;
		any = 1;
		if (v > (limit - (uint64_t)d) / (unsigned)base) {
			ovf = 1;
			v = limit;
		} else if (!ovf) {
			v = v * (unsigned)base + (unsigned)d;
		}
	}
	if (!any)
		p = s;
	else
		p += i;
	if (neg) {
		*neg_out = 1;
		if (is_signed)
			v = (uint64_t)0 - v;      /* two's complement; avoids negating INT64_MIN */
	}
	if (endp)
		n2a32_st32(c, endp, p);
	*overflow = ovf;
	return v;
}

static void sh_strtol(struct n2a32_cpu *c)
{
	int neg, ovf;
	uint64_t v = g_strtox(c, n2shim_arg(c, 0), n2shim_arg(c, 1), (int)n2shim_arg(c, 2), 1, 32, &neg, &ovf);
	n2shim_ret(c, (uint32_t)(int32_t)(int64_t)v);
}

static void sh_strtoul(struct n2a32_cpu *c)
{
	int neg, ovf;
	uint64_t v = g_strtox(c, n2shim_arg(c, 0), n2shim_arg(c, 1), (int)n2shim_arg(c, 2), 0, 32, &neg, &ovf);
	if (neg)
		v = (uint64_t)0 - v;
	n2shim_ret(c, (uint32_t)v);
}

static void sh_strtoll(struct n2a32_cpu *c)
{
	int neg, ovf;
	uint64_t v = g_strtox(c, n2shim_arg(c, 0), n2shim_arg(c, 1), (int)n2shim_arg(c, 2), 1, 64, &neg, &ovf);
	n2shim_ret64(c, v);
}

static void sh_strtoull(struct n2a32_cpu *c)
{
	int neg, ovf;
	uint64_t v = g_strtox(c, n2shim_arg(c, 0), n2shim_arg(c, 1), (int)n2shim_arg(c, 2), 0, 64, &neg, &ovf);
	if (neg)
		v = (uint64_t)0 - v;
	n2shim_ret64(c, v);
}

static void sh_atoi(struct n2a32_cpu *c)
{
	int neg, ovf;
	uint64_t v = g_strtox(c, n2shim_arg(c, 0), 0, 10, 1, 32, &neg, &ovf);
	n2shim_ret(c, (uint32_t)(int32_t)(int64_t)v);
}

static void sh_atoll(struct n2a32_cpu *c)
{
	int neg, ovf;
	uint64_t v = g_strtox(c, n2shim_arg(c, 0), 0, 10, 1, 64, &neg, &ovf);
	n2shim_ret64(c, v);
}

/* ---- qsort with a guest comparator --------------------------------------------------------------------------- */

static void g_swap(struct n2a32_cpu *c, uint32_t a, uint32_t b, uint32_t size, uint8_t *tmp)
{
	uint32_t i;

	for (i = 0; i < size; i++)
		tmp[i] = n2a32_ld8(c, a + i);
	for (i = 0; i < size; i++)
		n2a32_st8(c, a + i, n2a32_ld8(c, b + i));
	for (i = 0; i < size; i++)
		n2a32_st8(c, b + i, tmp[i]);
}

/* Returns the comparison, and sets *failed when the guest call did not return normally (fault/undef/noimport),
 * so the sort stops instead of running on with meaningless comparisons. */
static int g_cmp(struct n2a32_cpu *c, uint32_t fn, uint32_t a, uint32_t b, int *failed)
{
	uint32_t args[2];
	uint32_t r1 = 0;
	int r;

	if (!fn) {
		*failed = 1;
		return 0;
	}
	args[0] = a;
	args[1] = b;
	r = (int32_t)n2a32_call(c, fn, args, 2, &r1);
	if (c->stop != N2A32_STOP_NONE && c->stop != N2A32_STOP_RETURN) {
		*failed = 1;            /* RETURN is what a successful outer host->guest call leaves behind */
		return 0;
	}
	return r < 0 ? -1 : r > 0 ? 1 : 0;
}

static void sh_qsort(struct n2a32_cpu *c)
{
	uint32_t base = n2shim_arg(c, 0), n = n2shim_arg(c, 1), size = n2shim_arg(c, 2), fn = n2shim_arg(c, 3);
	uint32_t start, end, root, child;
	uint8_t *tmp;
	int failed = 0;

	if (n < 2 || !size || !base || !fn)
		return;
	if ((uint64_t)n * size > 0x40000000ULL)   /* refuse a range that cannot be guest memory */
		return;
	tmp = (uint8_t *)malloc(size);
	if (!tmp)
		return;
	/* heapsort: in place, O(n log n), no second guest buffer; equal elements keep no particular order */
	for (start = n / 2; start-- > 0 && !failed;) {
		root = start;
		for (;;) {
			child = 2 * root + 1;
			if (child >= n)
				break;
			if (child + 1 < n &&
			    g_cmp(c, fn, base + (child + 1) * size, base + child * size, &failed) > 0)
				child++;
			if (failed || g_cmp(c, fn, base + root * size, base + child * size, &failed) >= 0)
				break;
			g_swap(c, base + root * size, base + child * size, size, tmp);
			root = child;
		}
	}
	for (end = n; end-- > 1 && !failed;) {
		g_swap(c, base, base + end * size, size, tmp);
		root = 0;
		for (;;) {
			child = 2 * root + 1;
			if (child >= end)
				break;
			if (child + 1 < end &&
			    g_cmp(c, fn, base + (child + 1) * size, base + child * size, &failed) > 0)
				child++;
			if (failed || g_cmp(c, fn, base + root * size, base + child * size, &failed) >= 0)
				break;
			g_swap(c, base + root * size, base + child * size, size, tmp);
			root = child;
		}
	}
	free(tmp);
}

/* ---- AEABI memory routines and the fortified variants -------------------------------------------------------- */

static void sh_aeabi_memcpy(struct n2a32_cpu *c)   /* (dest, src, n) */
{
	sh_memcpy(c);
}

static void sh_aeabi_memmove(struct n2a32_cpu *c)
{
	sh_memmove(c);
}

static void sh_aeabi_memset(struct n2a32_cpu *c)   /* (dest, n, c) -- AEABI order, not memset's */
{
	uint8_t *d = n2a32_ptr(c->mem, n2shim_arg(c, 0));
	uint32_t n = n2shim_arg(c, 1);
	uint32_t ch = n2shim_arg(c, 2);

	if (d && n)
		memset(d, (int)(ch & 0xff), n);
}

static void sh_aeabi_memclr(struct n2a32_cpu *c)   /* (dest, n) */
{
	uint8_t *d = n2a32_ptr(c->mem, n2shim_arg(c, 0));
	uint32_t n = n2shim_arg(c, 1);

	if (d && n)
		memset(d, 0, n);
}

static void sh_memcpy_chk(struct n2a32_cpu *c)     /* (dest, src, len, destlen) */
{
	uint32_t dest = n2shim_arg(c, 0);
	uint32_t len = n2shim_arg(c, 2), destlen = n2shim_arg(c, 3);

	if (len > destlen) {
		c->stop_request = 1;                       /* fortify failure: stop the guest, do not corrupt */
		n2shim_ret(c, dest);
		return;
	}
	sh_memcpy(c);
}

static void sh_strlen_chk(struct n2a32_cpu *c)     /* (str, len) */
{
	uint32_t s = n2shim_arg(c, 0), max = n2shim_arg(c, 1);
	uint32_t n = g_strnlen(c, s, 0xffffffffu);   /* bionic: strlen() first, then the check */

	if (n >= max)
		c->stop_request = 1;
	n2shim_ret(c, n);
}

static void sh_strncpy_chk2(struct n2a32_cpu *c)   /* (dest, src, n, destlen, srclen) */
{
	uint32_t d = n2shim_arg(c, 0), s = n2shim_arg(c, 1), n = n2shim_arg(c, 2);
	uint32_t destlen = n2shim_arg(c, 3), srclen = n2shim_arg(c, 4);

	if (n > destlen || n > srclen) {
		uint32_t lim = destlen < srclen ? destlen : srclen;
		c->stop_request = 1;
		n = lim;
	}
	/* re-dispatch through the normal path with the clamped length */
	{
		uint32_t i;
		for (i = 0; i < n; i++) {
			uint8_t ch = d && s ? n2a32_ld8(c, s + i) : 0;
			if (d)
				n2a32_st8(c, d + i, ch);
			if (!ch) {
				i++;
				break;
			}
		}
		for (; d && i < n; i++)
			n2a32_st8(c, d + i, 0);
		n2shim_ret(c, d);
	}
}

/* ---- table --------------------------------------------------------------------------------------------------- */

#define S(x) { #x, sh_##x }
#define E(x, fn) { x, fn }

const struct n2shim_entry n2shim_string[] = {
	/* imports D01 assigns to this table (facts/imports.py --table string) */
	S(memcpy), S(memmove), S(memset), S(memcmp),
	S(strlen), S(strnlen), S(strcmp), S(strncpy), S(strstr), S(strtok),
	S(strerror), S(strsignal), S(atoi), S(atoll), S(qsort),
	E("__aeabi_memcpy", sh_aeabi_memcpy),
	E("__aeabi_memset", sh_aeabi_memset),
	E("__memcpy_chk", sh_memcpy_chk),
	E("__strlen_chk", sh_strlen_chk),
	E("__strncpy_chk2", sh_strncpy_chk2),
	E("snprintf", n2fmt_sh_snprintf),
	E("sprintf", n2fmt_sh_sprintf),
	E("vsnprintf", n2fmt_sh_vsnprintf),
	E("vsprintf", n2fmt_sh_vsprintf),
	E("sscanf", n2fmt_sh_sscanf),
	E("__snprintf_chk", n2fmt_sh_snprintf_chk),
	E("__vsnprintf_chk", n2fmt_sh_vsnprintf_chk),
	E("__vsprintf_chk", n2fmt_sh_vsprintf_chk),
	/* not imported by the five vendor objects today, but the same C library surface: cheap and useful to the
	 * other shim tables (and to libjpeg-alpha_vendor.so if it is ever hosted). */
	S(memchr), S(strncmp), S(strcpy), S(strcat), S(strchr), S(strrchr), S(strtok_r),
	S(strtol), S(strtoul), S(strtoll), S(strtoull),
	E("__aeabi_memcpy4", sh_aeabi_memcpy),
	E("__aeabi_memcpy8", sh_aeabi_memcpy),
	E("__aeabi_memmove", sh_aeabi_memmove),
	E("__aeabi_memmove4", sh_aeabi_memmove),
	E("__aeabi_memmove8", sh_aeabi_memmove),
	E("__aeabi_memset4", sh_aeabi_memset),
	E("__aeabi_memset8", sh_aeabi_memset),
	E("__aeabi_memclr", sh_aeabi_memclr),
	E("__aeabi_memclr4", sh_aeabi_memclr),
	E("__aeabi_memclr8", sh_aeabi_memclr),
	{ NULL, NULL },
};
