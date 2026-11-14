/* SPDX-License-Identifier: MIT */
/*
 * t_support.c -- the small part of the V02 core that the shims use, for the V30 host tests.
 *
 * The real interpreter (gpu/hwcodec/core) is not linked here: V30 only needs guest memory, the AAPCS argument
 * helpers and n2a32_call() for the qsort comparator.  The guest address space is one 1 GiB reservation; the
 * bump allocator is the same contract as n2a32_mem_alloc() (fresh zeroed pages, never reused).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"
#include "v30test.h"

#define MEM_SIZE   (1u << 30)
#define STACK_ZONE (64u << 10)
#define STACK_ZONES 8u

int t_pass, t_total;

struct n2a32_mem t_mem;

static uint8_t *g_base;
static uint32_t g_bump = 0x10000000u;
static uint32_t g_stack;
static pthread_mutex_t g_mem_lock = PTHREAD_MUTEX_INITIALIZER;

/* qsort comparator hook: the address the test passes is a tag, and this is the "guest" side. */
uint32_t (*t_cmp_hook)(struct n2a32_cpu *c, uint32_t a, uint32_t b);
static struct n2a32_cpu *g_last_cpu;

struct n2a32_cpu *t_last_cpu(void)
{
	return g_last_cpu;
}

const char *t_gstr(uint32_t a, char *buf, size_t n)
{
	size_t i;
	for (i = 0; i + 1 < n; i++) {
		uint8_t ch = a ? g_base[a + i] : 0;
		buf[i] = (char)ch;
		if (!ch)
			return buf;
	}
	buf[n - 1] = 0;
	return buf;
}

static pthread_mutex_t g_check_lock = PTHREAD_MUTEX_INITIALIZER;

void t_checkf(int ok, const char *fmt, ...)
{
	va_list ap;

	pthread_mutex_lock(&g_check_lock);
	t_total++;
	if (ok) {
		t_pass++;
		pthread_mutex_unlock(&g_check_lock);
		return;
	}
	printf("FAIL: ");
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	pthread_mutex_unlock(&g_check_lock);
}

/* ---- core stand-ins ------------------------------------------------------------------------------------------ */

uint32_t n2a32_mem_alloc(struct n2a32_mem *m, uint32_t len, uint32_t align)
{
	uint32_t a;

	(void)m;
	pthread_mutex_lock(&g_mem_lock);
	if (align < 16)
		align = 16;
	g_bump = (g_bump + align - 1) & ~(align - 1);
	a = g_bump;
	g_bump += (len + 4095u) & ~4095u;
	pthread_mutex_unlock(&g_mem_lock);
	if (g_bump > MEM_SIZE) {
		printf("FATAL: test guest memory exhausted\n");
		exit(1);
	}
	return a;
}

int n2a32_mem_map(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable)
{
	(void)m;
	(void)writable;
	if ((uint64_t)addr + len > MEM_SIZE)
		return -1;
	return 0;
}

uint32_t n2a32_mmio_load(struct n2a32_mem *m, uint32_t a, unsigned bytes)
{
	(void)m;
	(void)a;
	(void)bytes;
	printf("FATAL: unexpected MMIO load\n");
	exit(1);
}

void n2a32_mmio_store(struct n2a32_mem *m, uint32_t a, unsigned bytes, uint32_t v)
{
	(void)m;
	(void)a;
	(void)bytes;
	(void)v;
	printf("FATAL: unexpected MMIO store\n");
	exit(1);
}

uint32_t n2a32_call(struct n2a32_cpu *c, uint32_t fn, const uint32_t *args, unsigned nargs, uint32_t *r1)
{
	if (t_cmp_hook && fn == 0x51510001u && nargs == 2) {
		if (r1)
			*r1 = 0;
		return (uint32_t)t_cmp_hook(c, args[0], args[1]);
	}
	printf("FATAL: unexpected guest call 0x%x\n", fn);
	exit(1);
}

/* ---- helpers ------------------------------------------------------------------------------------------------- */

void t_init(void)
{
	g_base = (uint8_t *)mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE,
				 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (g_base == MAP_FAILED) {
		printf("FATAL: cannot reserve guest memory\n");
		exit(1);
	}
	memset(&t_mem, 0, sizeof t_mem);
	t_mem.base = g_base;
	g_stack = n2a32_mem_alloc(&t_mem, STACK_ZONE * STACK_ZONES, 4096);
}

uint32_t t_alloc(uint32_t n)
{
	return n2a32_mem_alloc(&t_mem, n, 16);
}

uint32_t t_putn(const void *p, uint32_t n)
{
	uint32_t a = t_alloc(n ? n : 1);

	memcpy(g_base + a, p, n);
	return a;
}

uint32_t t_put(const char *s)
{
	return t_putn(s, (uint32_t)strlen(s) + 1);
}

struct n2a32_cpu *t_cpu_zone(unsigned zone, uint32_t *sp_out)
{
	static struct n2a32_cpu cpus[STACK_ZONES];
	struct n2a32_cpu *c = &cpus[zone % STACK_ZONES];

	memset(c, 0, sizeof *c);
	c->mem = &t_mem;
	c->r[13] = g_stack + zone * STACK_ZONE + (STACK_ZONE / 2);   /* 8-aligned */
	if (sp_out)
		*sp_out = c->r[13];
	return c;
}

struct n2a32_cpu *t_cpu(void)
{
	return t_cpu_zone(0, NULL);
}

const struct n2shim_entry *t_find(const struct n2shim_entry *tab, const char *name)
{
	const struct n2shim_entry *e;

	for (e = tab; e->name; e++)
		if (strcmp(e->name, name) == 0)
			return e;
	return NULL;
}

uint32_t t_call(const struct n2shim_entry *tab, const char *name, const uint32_t *args, int nargs,
		uint32_t *r1)
{
	const struct n2shim_entry *e = t_find(tab, name);
	struct n2a32_cpu *c;
	int i;

	if (!e) {
		printf("FATAL: no shim named %s\n", name);
		exit(1);
	}
	c = t_cpu();
	for (i = 0; i < nargs; i++) {
		if (i < 4)
			c->r[i] = args[i];
		else
			n2a32_st32(c, c->r[13] + 4 * (uint32_t)(i - 4), args[i]);
	}
	e->fn(c);
	g_last_cpu = c;
	if (r1)
		*r1 = c->r[1];
	return c->r[0];
}

uint64_t t_vfs_last_off;
int t_vfs_calls;

#if !defined(V30_NO_VFS)
/* V33 stand-in for the descriptor-backed mmap hook (strong definition beats the weak one in n2shim_heap.c). */
uint32_t n2vfs_mmap(struct n2a32_cpu *c, uint32_t addr, uint32_t len, int prot, int flags, int fd, uint64_t off)
{
	(void)c;
	(void)addr;
	(void)prot;
	(void)flags;
	t_vfs_calls++;
	t_vfs_last_off = off;
	if (fd == 7 && len)
		return 0x20000000u + (uint32_t)(off & 0xffff);   /* an address the test recognises */
	return 0;
}

uint32_t n2vfs_errno_location(struct n2a32_cpu *c)
{
	(void)c;
	return 0;
}
#endif
