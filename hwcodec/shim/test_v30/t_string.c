/* SPDX-License-Identifier: MIT */
/* t_string.c -- direct checks of n2shim_string[] and n2shim_heap[] string/allocator entry points. */
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"
#include "v30test.h"

static const struct n2shim_entry *S;

static uint32_t call1(const char *n, uint32_t a)
{
	uint32_t args[1] = { a };
	return t_call(S, n, args, 1, NULL);
}

static uint32_t call2(const char *n, uint32_t a, uint32_t b)
{
	uint32_t args[2] = { a, b };
	return t_call(S, n, args, 2, NULL);
}

static uint32_t call3(const char *n, uint32_t a, uint32_t b, uint32_t c)
{
	uint32_t args[3] = { a, b, c };
	return t_call(S, n, args, 3, NULL);
}

static uint32_t call4(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
	uint32_t args[4] = { a, b, c, d };
	return t_call(S, n, args, 4, NULL);
}

static uint32_t call5(const char *n, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e)
{
	uint32_t args[5] = { a, b, c, d, e };
	return t_call(S, n, args, 5, NULL);
}

static void fill_guest(uint32_t a, uint8_t v, uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; i++)
		n2a32_st8(t_cpu(), a + i, v);
}

static int guest_eq(uint32_t a, const char *s, uint32_t n)
{
	uint32_t i;
	for (i = 0; i < n; i++)
		if (n2a32_ld8(t_cpu(), a + i) != (uint8_t)s[i])
			return 0;
	return 1;
}

static void test_memory(void)
{
	uint32_t src = t_put("hello"), dst = t_alloc(16);

	fill_guest(dst, 'X', 16);
	T_CHECK(call3("memcpy", dst, src, 6) == dst);
	T_CHECK(guest_eq(dst, "hello", 6));
	T_CHECK(n2a32_ld8(t_cpu(), dst + 6) == 'X');
	/* null pointers are refused, not dereferenced */
	T_CHECK(call3("memcpy", 0, src, 6) == 0);
	T_CHECK(call3("memcpy", dst, 0, 6) == dst);
	T_CHECK(call3("memcpy", dst, src, 0) == dst);

	/* memmove forwards and backwards */
	{
		uint32_t b = t_put("abcdef");
		call3("memmove", b + 2, b, 4);
		T_CHECK(guest_eq(b, "ababcd", 6));
	}
	{
		uint32_t b = t_put("abcdef");
		call3("memmove", b, b + 2, 4);
		T_CHECK(guest_eq(b, "cdefef", 6));
	}

	fill_guest(dst, 0, 8);
	T_CHECK(call3("memset", dst, 'A', 5) == dst);
	T_CHECK(guest_eq(dst, "AAAAA", 5));
	T_CHECK(n2a32_ld8(t_cpu(), dst + 5) == 0);
	T_CHECK(call3("memset", 0, 'A', 5) == 0);

	{
		uint32_t a = t_put("abc"), b = t_put("abd"), c = t_put("abc");
		T_CHECK((int32_t)call3("memcmp", a, c, 3) == 0);
		T_CHECK((int32_t)call3("memcmp", a, b, 3) < 0);
		T_CHECK((int32_t)call3("memcmp", b, a, 3) > 0);
		T_CHECK((int32_t)call3("memcmp", 0, 0, 4) == 0);
		T_CHECK((int32_t)call3("memcmp", 0, a, 4) < 0);
		T_CHECK((int32_t)call3("memcmp", a, 0, 0) == 0);
	}

	{
		uint32_t a = t_put("abcabc");
		T_CHECK(call3("memchr", a, 'b', 6) == a + 1);
		T_CHECK(call3("memchr", a, 'z', 6) == 0);
		T_CHECK(call3("memchr", a, 'b', 1) == 0);
	}

	/* AEABI: __aeabi_memset(dest, n, c) -- the arguments are NOT in memset order */
	{
		uint32_t b = t_alloc(8);
		fill_guest(b, 0, 8);
		call3("__aeabi_memset", b, 3, 0x42);
		T_CHECK(guest_eq(b, "BBB", 3));
		T_CHECK(n2a32_ld8(t_cpu(), b + 3) == 0);
	}
	{
		uint32_t b = t_alloc(8);
		fill_guest(b, 0x11, 8);
		call2("__aeabi_memclr", b, 4);
		T_CHECK(n2a32_ld8(t_cpu(), b) == 0 && n2a32_ld8(t_cpu(), b + 3) == 0);
		T_CHECK(n2a32_ld8(t_cpu(), b + 4) == 0x11);
	}

	/* fortified variants: within bounds copies, over bounds stops the guest and does not copy */
	{
		uint32_t b = t_alloc(8);
		fill_guest(b, 'Z', 8);
		T_CHECK(call4("__memcpy_chk", b, src, 6, 8) == b);
		T_CHECK(guest_eq(b, "hello", 6));
		fill_guest(b, 'Z', 8);
		T_CHECK(call4("__memcpy_chk", b, src, 9, 8) == b);
		T_CHECK(t_last_cpu()->stop_request == 1);
		T_CHECK(n2a32_ld8(t_cpu(), b) == 'Z');
	}
	{
		uint32_t s = t_put("hello");
		T_CHECK(call2("__strlen_chk", s, 10) == 5);
		T_CHECK(t_last_cpu()->stop_request == 0);
		T_CHECK(call2("__strlen_chk", s, 3) == 5);
		T_CHECK(t_last_cpu()->stop_request == 1);
	}
	{
		uint32_t d = t_alloc(8);
		fill_guest(d, 'Q', 8);
		T_CHECK(call5("__strncpy_chk2", d, src, 3, 8, 6) == d);
		T_CHECK(guest_eq(d, "hel", 3));
		fill_guest(d, 'Q', 8);
		(void)call5("__strncpy_chk2", d, src, 3, 8, 2);
		T_CHECK(t_last_cpu()->stop_request == 1);
	}
}

static void test_strings(void)
{
	uint32_t s = t_put("hello"), e = t_put("");
	uint32_t a = t_put("abc"), b = t_put("abd");
	char buf[64];

	T_CHECK(call1("strlen", s) == 5);
	T_CHECK(call1("strlen", e) == 0);
	T_CHECK(call1("strlen", 0) == 0);
	T_CHECK(call2("strnlen", s, 3) == 3);
	T_CHECK(call2("strnlen", s, 30) == 5);
	T_CHECK(call2("strnlen", 0, 30) == 0);

	T_CHECK((int32_t)call2("strcmp", a, a) == 0);
	T_CHECK((int32_t)call2("strcmp", a, b) < 0);
	T_CHECK((int32_t)call2("strcmp", b, a) > 0);
	T_CHECK((int32_t)call2("strcmp", 0, 0) == 0);
	T_CHECK((int32_t)call2("strcmp", 0, e) == 0);
	T_CHECK((int32_t)call2("strcmp", 0, a) < 0);
	T_CHECK((int32_t)call3("strncmp", a, b, 2) == 0);
	T_CHECK((int32_t)call3("strncmp", a, b, 3) < 0);

	/* strncpy pads with NUL */
	{
		uint32_t d = t_alloc(8);
		fill_guest(d, 'X', 8);
		T_CHECK(call3("strncpy", d, a, 5) == d);
		T_CHECK(guest_eq(d, "abc\0\0", 5));
		T_CHECK(n2a32_ld8(t_cpu(), d + 5) == 'X');
		T_CHECK(call3("strncpy", 0, a, 5) == 0);
	}
	{
		uint32_t d = t_alloc(8);
		fill_guest(d, 0, 8);
		T_CHECK(call2("strcpy", d, a) == d);
		T_CHECK(guest_eq(d, "abc", 4));
		T_CHECK(call2("strcat", d, t_put("de")) == d);
		T_CHECK(guest_eq(d, "abcde", 6));
	}
	{
		uint32_t h = t_put("hello world");
		T_CHECK(call2("strstr", h, t_put("world")) == h + 6);
		T_CHECK(call2("strstr", h, e) == h);
		T_CHECK(call2("strstr", h, t_put("zzz")) == 0);
		T_CHECK(call2("strchr", h, 'o') == h + 4);
		T_CHECK(call2("strchr", h, 'z') == 0);
		T_CHECK(call2("strrchr", h, 'o') == h + 7);
	}

	/* strtok: consecutive delimiters collapse, the last token ends at the NUL */
	{
		uint32_t t = t_put("a,b,,c");
		uint32_t d = t_put(",");
		uint32_t p;

		p = call2("strtok", t, d);
		T_CHECK(t_gstr(p, buf, sizeof buf) && strcmp(buf, "a") == 0);
		p = call2("strtok", 0, d);
		T_CHECK(t_gstr(p, buf, sizeof buf) && strcmp(buf, "b") == 0);
		p = call2("strtok", 0, d);
		T_CHECK(t_gstr(p, buf, sizeof buf) && strcmp(buf, "c") == 0);
		T_CHECK(call2("strtok", 0, d) == 0);
	}

	T_CHECK(call1("strerror", 2) != 0);
	T_CHECK(strcmp(t_gstr(call1("strerror", 2), buf, sizeof buf), "No such file or directory") == 0);
	T_CHECK(strcmp(t_gstr(call1("strerror", 9999), buf, sizeof buf), "Unknown error 9999") == 0);
	T_CHECK(strcmp(t_gstr(call1("strsignal", 11), buf, sizeof buf), "Segmentation fault") == 0);
	T_CHECK(strcmp(t_gstr(call1("strsignal", 9999), buf, sizeof buf), "Unknown signal") == 0);
}

static void test_parse(void)
{
	char buf[64];

	T_CHECK((int32_t)call1("atoi", t_put("  -42xyz")) == -42);
	T_CHECK((int32_t)call1("atoi", t_put("+7")) == 7);
	T_CHECK((int32_t)call1("atoi", t_put("abc")) == 0);
	T_CHECK((int32_t)call1("atoi", 0) == 0);
	{
		uint32_t r1 = 0;
		uint32_t args[1] = { t_put("123456789012") };
		uint32_t lo = t_call(S, "atoll", args, 1, &r1);
		T_CHECK(((uint64_t)r1 << 32 | lo) == 123456789012ULL);
	}
	{
		uint32_t r1 = 0;
		uint32_t args[1] = { t_put("-9223372036854775808") };
		uint32_t lo = t_call(S, "atoll", args, 1, &r1);
		T_CHECK(((uint64_t)r1 << 32 | lo) == 0x8000000000000000ULL);
	}
	/* saturating strtol/strtoul on a 32-bit guest */
	{
		uint32_t end = t_alloc(4);
		T_CHECK((int32_t)call3("strtol", t_put("0x1f"), end, 16) == 31);
		T_CHECK((int32_t)call3("strtol", t_put("0x1f"), end, 0) == 31);
		T_CHECK((int32_t)call3("strtol", t_put("-0x10"), 0, 0) == -16);
		T_CHECK((int32_t)call3("strtol", t_put("999999999999"), 0, 10) == INT32_MAX);
		T_CHECK((int32_t)call3("strtol", t_put("-999999999999"), 0, 10) == INT32_MIN);
		T_CHECK(call3("strtoul", t_put("999999999999"), 0, 10) == 0xffffffffu);
		T_CHECK(call3("strtoul", t_put("0xdeadbeef"), 0, 16) == 0xdeadbeefu);
		T_CHECK((int32_t)call3("strtol", t_put("12z"), end, 10) == 12);
		T_CHECK(t_gstr(n2a32_ld32(t_cpu(), end), buf, sizeof buf) && strcmp(buf, "z") == 0);
	}
}

/* The exact assignment from facts/imports.py (D01): every one of these must resolve in my tables. */
static const char *const D01_STRING[] = {
	"__aeabi_memcpy", "__aeabi_memset", "__memcpy_chk", "__snprintf_chk", "__strlen_chk",
	"__strncpy_chk2", "__vsnprintf_chk", "__vsprintf_chk", "atoi", "atoll", "memcmp", "memcpy",
	"memmove", "memset", "qsort", "snprintf", "sprintf", "sscanf", "strcmp", "strerror", "strlen",
	"strncpy", "strnlen", "strsignal", "strstr", "strtok", "vsnprintf",
};

static const char *const D01_HEAP[] = {
	"calloc", "free", "getpagesize", "malloc", "memalign", "mmap", "mmap64", "munmap",
};

static void test_table_coverage(void)
{
	unsigned i;

	for (i = 0; i < sizeof D01_STRING / sizeof D01_STRING[0]; i++)
		T_CHECK(t_find(n2shim_string, D01_STRING[i]) != NULL);
	for (i = 0; i < sizeof D01_HEAP / sizeof D01_HEAP[0]; i++)
		T_CHECK(t_find(n2shim_heap, D01_HEAP[i]) != NULL);
	/* every entry is callable and no name is empty */
	for (i = 0; n2shim_string[i].name; i++)
		T_CHECK(n2shim_string[i].fn != NULL && n2shim_string[i].name[0] != 0);
	for (i = 0; n2shim_heap[i].name; i++)
		T_CHECK(n2shim_heap[i].fn != NULL && n2shim_heap[i].name[0] != 0);
}

void test_string(void)
{
	S = n2shim_string;
	test_table_coverage();
	test_memory();
	test_strings();
	test_parse();
}
