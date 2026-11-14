/* SPDX-License-Identifier: MIT */
/*
 * test-only stand-in for the parts of V02 (include/n2a32.h) that the loader needs:
 *   - a 4 GiB PROT_NONE guest reservation, page mapping/protection, a bump allocator;
 *   - host-call slots with per-(fn,name) deduplication, slot 0 reserved;
 *   - a n2a32_call() that records the call instead of interpreting guest code.
 *
 * It is NOT the interpreter and must not be shipped: run_tests.sh can link the real emu sources once V02 has
 * landed (N2HW_V03_REAL_EMU=1) and otherwise compiles this file.
 */
#define _DEFAULT_SOURCE 1

#include "n2a32.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define STUB_HC_MAX 256

struct stub_priv {
	uint32_t next;
};

static struct {
	n2a32_hostcall fn;
	char *name;
} stub_hc[STUB_HC_MAX];
static int stub_nhc;
static int stub_next_slot = 1;

uint32_t n2a32_stub_calls[512];
unsigned n2a32_stub_ncalls;

/* Per-page permission bookkeeping, so a host test can verify that n2elf_link() applied protections. */
static uint8_t stub_perm[1u << 20];	/* 4 GiB / 4096 */

int n2a32_stub_perm(uint32_t addr)
{
	return stub_perm[addr >> 12] ? (int)stub_perm[addr >> 12] - 1 : -1;
}

void n2a32_stub_reset(void)
{
	n2a32_stub_ncalls = 0;
}

struct n2a32_mem *n2a32_mem_create(void)
{
	struct n2a32_mem *m;
	struct stub_priv *p;
	void *base;

	base = mmap(NULL, (size_t)1 << 32, PROT_NONE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (base == MAP_FAILED)
		return NULL;
	m = (struct n2a32_mem *)calloc(1, sizeof(*m));
	p = (struct stub_priv *)calloc(1, sizeof(*p));
	if (!m || !p) {
		free(m);
		free(p);
		munmap(base, (size_t)1 << 32);
		return NULL;
	}
	m->base = (uint8_t *)base;
	p->next = 0x40000000u;
	m->priv = p;
	return m;
}

void n2a32_mem_destroy(struct n2a32_mem *m)
{
	if (!m)
		return;
	if (m->base)
		munmap(m->base, (size_t)1 << 32);
	free(m->priv);
	free(m);
}

int n2a32_mem_map(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable)
{
	if (!m || (addr & 0xfffu) || (len & 0xfffu) || len == 0)
		return -1;
	if ((uint64_t)addr + len > ((uint64_t)1 << 32))
		return -1;
	if (mprotect(m->base + addr, len, PROT_READ | (writable ? PROT_WRITE : 0)) != 0)
		return -1;
	memset(m->base + addr, 0, len);
	for (uint32_t p = addr >> 12; p < (addr + len) >> 12; p++)
		stub_perm[p] = (uint8_t)(writable ? 2 : 1);
	return 0;
}

int n2a32_mem_protect(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable)
{
	if (!m || (addr & 0xfffu) || (len & 0xfffu) || len == 0)
		return -1;
	if ((uint64_t)addr + len > ((uint64_t)1 << 32))
		return -1;
	if (mprotect(m->base + addr, len, PROT_READ | (writable ? PROT_WRITE : 0)) != 0)
		return -1;
	for (uint32_t p = addr >> 12; p < (addr + len) >> 12; p++)
		stub_perm[p] = (uint8_t)(writable ? 2 : 1);
	return 0;
}

uint32_t n2a32_mem_alloc(struct n2a32_mem *m, uint32_t len, uint32_t align)
{
	struct stub_priv *p;
	uint32_t start;

	if (!m || len == 0)
		return 0;
	p = (struct stub_priv *)m->priv;
	if (align == 0)
		align = 4096;
	if ((align & (align - 1)) != 0)
		return 0;
	if (align < 4096)
		align = 4096;
	len = (len + 4095u) & ~4095u;
	if (len == 0)
		return 0;
	start = (p->next + align - 1) & ~(align - 1);
	if (start < p->next || start > 0xf0000000u - len)
		return 0;
	if (n2a32_mem_map(m, start, len, 1) != 0)
		return 0;
	p->next = start + len;
	return start;
}

uint32_t n2a32_hostcall_register(n2a32_hostcall fn, const char *name)
{
	int i;

	if (!name)
		return 0;
	for (i = 0; i < stub_nhc; i++) {
		if (stub_hc[i].fn == fn && strcmp(stub_hc[i].name, name) == 0)
			return N2A32_HOSTCALL_BASE + 4u * (uint32_t)(i + 1);
	}
	if (stub_nhc >= STUB_HC_MAX || stub_next_slot >= (int)N2A32_HOSTCALL_MAX)
		return 0;
	stub_hc[stub_nhc].fn = fn;
	stub_hc[stub_nhc].name = (char *)malloc(strlen(name) + 1);
	if (!stub_hc[stub_nhc].name)
		return 0;
	strcpy(stub_hc[stub_nhc].name, name);
	stub_nhc++;
	stub_next_slot++;
	return N2A32_HOSTCALL_BASE + 4u * (uint32_t)stub_nhc;
}

/* Test-only: 1 if the slot was registered with a NULL implementation (the unresolved convention). */
int n2a32_stub_hostcall_is_null(uint32_t guest_addr)
{
	uint32_t off;

	if (guest_addr < N2A32_HOSTCALL_BASE)
		return -1;
	off = guest_addr - N2A32_HOSTCALL_BASE;
	if ((off & 3u) != 0 || off / 4u == 0 || off / 4u > (uint32_t)stub_nhc)
		return -1;
	return stub_hc[off / 4u - 1].fn == NULL;
}

const char *n2a32_hostcall_name(uint32_t guest_addr)
{
	uint32_t off;

	if (guest_addr < N2A32_HOSTCALL_BASE)
		return NULL;
	off = guest_addr - N2A32_HOSTCALL_BASE;
	if ((off & 3u) != 0 || off / 4u == 0)
		return NULL;
	if (off / 4u > (uint32_t)stub_nhc)
		return NULL;
	return stub_hc[off / 4u - 1].name;
}

void n2a32_cpu_init(struct n2a32_cpu *c, struct n2a32_mem *m)
{
	memset(c, 0, sizeof(*c));
	c->mem = m;
}

uint32_t n2a32_call(struct n2a32_cpu *c, uint32_t fn, const uint32_t *args, unsigned nargs, uint32_t *r1)
{
	unsigned i;

	(void)args;
	(void)nargs;
	if (r1)
		*r1 = 0;
	if (n2a32_stub_ncalls < sizeof(n2a32_stub_calls) / sizeof(n2a32_stub_calls[0]))
		n2a32_stub_calls[n2a32_stub_ncalls++] = fn;
	/* A completed host->guest call: n2a32.h says a RETURN stop is what the caller sees. */
	for (i = 0; i < 16; i++)
		c->r[i] = 0;
	c->stop = N2A32_STOP_RETURN;
	return 0;
}
