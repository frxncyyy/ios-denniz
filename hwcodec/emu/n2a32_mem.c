/* SPDX-License-Identifier: MIT */
/*
 * n2a32_mem.c -- the guest's 4 GiB address space, one host reservation.  V02.
 *
 * The whole space is created PROT_NONE / MAP_NORESERVE.  Pages become usable through n2a32_mem_map() (plain
 * memory, zero filled) or n2a32_mem_mmio() (every access goes through a hook; no host memory is committed).
 * n2a32_mem_alloc() is a bump allocator that starts at 0x40000000 and never hands out an address twice;
 * explicit maps that fall into its path are skipped, not overwritten.
 *
 * Addresses below 4 KiB are never mapped: n2a32_ptr(0) is NULL by contract, so guest address 0 is not usable.
 * The host-call page [0xfff00000, 0xfff04000) and N2A32_RETURN_ADDR (0xffff0000) need no backing memory;
 * they are intercepted by the core before any fetch or load.
 *
 * Host page size: guest pages are 4 KiB but iOS/arm64 host pages are 16 KiB, and XNU's mprotect() returns EINVAL
 * for an address that is not host-page aligned (first phone run: every guest map failed, "guest memory exhausted").
 * So the per-4 KiB state lives in p->page and the host protection is applied per host page as the most permissive
 * state of the guest pages it contains (any RW -> RW, else any RO -> R, else NONE).  On a 16 KiB host a free or
 * read-only guest page that shares a host page with a writable one therefore does not fault; plain memory and MMIO
 * (megabyte aligned) are otherwise unchanged.  N2A32_MEM_GRANULE=<pow2 >= 4096> overrides the granule for tests.
 */
#include "n2a32.h"

#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#define GUEST_SIZE   (1ull << 32)
#define GUARD_SIZE   (64ull * 1024ull)      /* host-only guard after 4 GiB: catches accesses that cross the end */
#define RESERVE_SIZE (GUEST_SIZE + GUARD_SIZE)
#define PAGE_SIZE_U  4096u
#define MMIO_SIZE    (1u << N2A32_MMIO_SHIFT)
#define MMIO_MASK    (MMIO_SIZE - 1u)
#define ALLOC_START  0x40000000u
#define ALLOC_LIMIT  N2A32_HOSTCALL_BASE     /* keep the host-call page out of the bump region */
#define NPAGES       (GUEST_SIZE / PAGE_SIZE_U)
#define MMIO_SLOTS   (sizeof(((struct n2a32_mem *)0)->mmio) / sizeof(((struct n2a32_mem *)0)->mmio[0]))

enum { PG_FREE = 0, PG_RW = 1, PG_RO = 2, PG_MMIO = 3 };
/* PG_DIRTY: the host page holding this guest page has been host-writable at some point, so it may hold non-zero
 * bytes.  Pages without it are still the kernel's zero-fill pages and map_locked() does not memset them: on iOS
 * every touched page counts toward the jetsam footprint (second phone run: per-process-limit kill inside this
 * memset while loading the first vendor library). */
#define PG_DIRTY 0x80u
#define PG_STATE(v) ((uint8_t)((v) & 0x7fu))

struct mem_priv {
	uint8_t *page;            /* one byte per 4 KiB page: PG_* */
	uint32_t brk;             /* next free bump address */
	pthread_mutex_t lock;     /* map/protect/alloc/mmio; the load/store paths stay lock-free */
};

static size_t page_of(uint32_t addr) { return (size_t)(addr >> 12); }
static size_t npages(uint32_t len) { return ((size_t)len + 4095u) / PAGE_SIZE_U; }

static n2a32_mem_logfn g_logfn;
static int g_loglevel = -1;     /* -1: not yet read from N2A32_MEM_LOG */

void n2a32_mem_set_log(n2a32_mem_logfn fn, int level)
{
	g_logfn = fn;
	g_loglevel = level;
}

/* level 1: failures (always; stderr when no hook), 2: maps >= 256 KiB or with zeroing, 3: every mprotect */
static void mem_log(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void mem_log(int level, const char *fmt, ...)
{
	char line[256];
	va_list ap;

	if (g_loglevel < 0) {
		const char *e = getenv("N2A32_MEM_LOG");
		g_loglevel = (e && *e) ? atoi(e) : 1;
	}
	if (level > g_loglevel)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	if (g_logfn)
		g_logfn(line);
	else
		fprintf(stderr, "%s\n", line);
}

static uint32_t granule;      /* host protection granule (>= 4 KiB, power of two); set by n2a32_mem_create */

static void granule_init(void)
{
	long hp = sysconf(_SC_PAGESIZE);
	const char *e = getenv("N2A32_MEM_GRANULE");
	uint32_t g = (hp > (long)PAGE_SIZE_U) ? (uint32_t)hp : PAGE_SIZE_U;

	if (e && *e) {
		unsigned long v = strtoul(e, NULL, 0);
		if (v >= g && v <= (1ul << 20) && (v & (v - 1)) == 0)
			g = (uint32_t)v;
	}
	if ((g & (g - 1)) != 0)
		g = 16384u;
	granule = g;
}

/* Re-apply the host protection of every host page overlapping [addr, addr+len) from the guest page states.
 * Runs of host pages with the same protection are merged into one mprotect.  Caller holds the lock. */
static int host_apply(struct n2a32_mem *m, uint32_t addr, uint64_t len)
{
	struct mem_priv *p = (struct mem_priv *)m->priv;
	uint64_t g = granule, start = (uint64_t)addr & ~(g - 1), end = ((uint64_t)addr + len + g - 1) & ~(g - 1);
	size_t per = (size_t)(g / PAGE_SIZE_U);
	uint64_t run_start = start;
	int run_prot = -1;
	uint64_t a;

	if (end > GUEST_SIZE)
		end = GUEST_SIZE;
	for (a = start; a <= end; a += g) {
		int prot = -2;
		if (a < end) {
			size_t first = page_of((uint32_t)a), i;
			int rw = 0, ro = 0;
			for (i = 0; i < per; i++) {
				uint8_t st = PG_STATE(p->page[first + i]);
				rw |= (st == PG_RW);
				ro |= (st == PG_RO);
			}
			prot = rw ? (PROT_READ | PROT_WRITE) : ro ? PROT_READ : PROT_NONE;
			if (rw) {
				for (i = 0; i < per; i++)
					p->page[first + i] |= PG_DIRTY;
			}
		}
		if (prot != run_prot) {
			if (run_prot >= 0 && a > run_start) {
				int r = mprotect(m->base + run_start, (size_t)(a - run_start), run_prot);
				int e = r ? errno : 0;
				mem_log(r ? 1 : 3, "n2a32_mem: mprotect(guest 0x%llx, 0x%llx, prot %d) = %d errno=%d (%s) granule=%u",
				        (unsigned long long)run_start, (unsigned long long)(a - run_start), run_prot, r, e,
				        e ? strerror(e) : "ok", granule);
				if (r) {
					errno = e;
					return -1;
				}
			}
			run_start = a;
			run_prot = prot;
		}
	}
	return 0;
}

struct n2a32_mem *n2a32_mem_create(void)
{
	struct n2a32_mem *m;
	struct mem_priv *p;
	void *base;

	if (granule == 0)
		granule_init();
	base = mmap(NULL, (size_t)RESERVE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (base == MAP_FAILED)
		return NULL;
	m = (struct n2a32_mem *)calloc(1, sizeof(*m));
	p = (struct mem_priv *)calloc(1, sizeof(*p));
	if (!m || !p) {
		munmap(base, (size_t)RESERVE_SIZE);
		free(m);
		free(p);
		return NULL;
	}
	p->page = (uint8_t *)calloc(NPAGES, 1);
	if (!p->page) {
		munmap(base, (size_t)RESERVE_SIZE);
		free(p);
		free(m);
		return NULL;
	}
	p->brk = ALLOC_START;
	pthread_mutex_init(&p->lock, NULL);
	m->base = (uint8_t *)base;
	m->priv = p;
	return m;
}

void n2a32_mem_destroy(struct n2a32_mem *m)
{
	struct mem_priv *p;

	if (!m)
		return;
	p = (struct mem_priv *)m->priv;
	if (p)
		pthread_mutex_destroy(&p->lock);
	if (m->base)
		munmap(m->base, (size_t)RESERVE_SIZE);
	free(p ? p->page : NULL);
	free(p);
	free(m);
}

/* Range checks shared by map/protect/mmio.  Returns 0 when addr/len are in range and page aligned. */
static int range_ok(uint32_t addr, uint32_t len)
{
	uint64_t plen = ((uint64_t)len + PAGE_SIZE_U - 1) & ~(uint64_t)(PAGE_SIZE_U - 1);
	uint64_t end = (uint64_t)addr + plen;

	if (len == 0)
		return -1;
	if ((addr & (PAGE_SIZE_U - 1)) != 0)
		return -1;
	if (addr == 0)
		return -1;                    /* n2a32_ptr(0) == NULL */
	if (end > GUEST_SIZE)
		return -1;
	return 0;
}

static int map_locked(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable)
{
	struct mem_priv *p;
	size_t first, n, i, ndirty = 0;

	if (!m || !m->base || !m->priv)
		return -1;
	if (range_ok(addr, len) != 0)
		return -1;
	p = (struct mem_priv *)m->priv;
	first = page_of(addr);
	n = npages(len);
	for (i = 0; i < n; i++) {
		if (PG_STATE(p->page[first + i]) == PG_MMIO)
			return -1;            /* MMIO needs no backing memory; do not turn it into memory */
		ndirty += (p->page[first + i] & PG_DIRTY) != 0;
	}
	/* Zero only guest pages that may hold old bytes (PG_DIRTY); the others are untouched zero-fill pages.  Their
	 * host pages are made writable for the memset (host-page granular: XNU mprotect needs host-page alignment), then
	 * the final protection is applied from the page states. */
	if (ndirty) {
		uint64_t g = granule, hs = (uint64_t)addr & ~(g - 1);
		uint64_t he = ((uint64_t)addr + (uint64_t)n * PAGE_SIZE_U + g - 1) & ~(g - 1);
		int r;
		if (he > GUEST_SIZE)
			he = GUEST_SIZE;
		r = mprotect(m->base + hs, (size_t)(he - hs), PROT_READ | PROT_WRITE);
		{
			int e = r ? errno : 0;
			mem_log(r ? 1 : 3, "n2a32_mem: mprotect(guest 0x%llx, 0x%llx, RW) for zeroing = %d errno=%d (%s) granule=%u",
			        (unsigned long long)hs, (unsigned long long)(he - hs), r, e, e ? strerror(e) : "ok", granule);
		}
		if (r != 0)
			return -1;
		for (i = 0; i < n; i++) {
			if (p->page[first + i] & PG_DIRTY)
				memset(m->base + addr + i * PAGE_SIZE_U, 0, PAGE_SIZE_U);   /* whole pages: no stale tail */
		}
	}
	if ((uint64_t)n * PAGE_SIZE_U >= (256u << 10) || ndirty)
		mem_log(2, "n2a32_mem: map guest 0x%08x len 0x%llx %s zeroed=%zu pages", addr, (unsigned long long)n * PAGE_SIZE_U,
		        writable ? "rw" : "ro", ndirty);
	for (i = 0; i < n; i++)
		p->page[first + i] = (uint8_t)((p->page[first + i] & PG_DIRTY) | (writable ? PG_RW : PG_RO));
	if (host_apply(m, addr, (uint64_t)n * PAGE_SIZE_U) != 0) {
		/* Leave them RW rather than half mapped; report the failure. */
		for (i = 0; i < n; i++)
			p->page[first + i] = (uint8_t)((p->page[first + i] & PG_DIRTY) | PG_RW);
		host_apply(m, addr, (uint64_t)n * PAGE_SIZE_U);
		return -1;
	}
	return 0;
}

int n2a32_mem_map(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable)
{
	int r;
	struct mem_priv *p = m ? (struct mem_priv *)m->priv : NULL;

	if (!p)
		return -1;
	pthread_mutex_lock(&p->lock);
	r = map_locked(m, addr, len, writable);
	pthread_mutex_unlock(&p->lock);
	return r;
}

int n2a32_mem_protect(struct n2a32_mem *m, uint32_t addr, uint32_t len, int writable)
{
	struct mem_priv *p;
	size_t first, n, i;

	if (!m || !m->base || !m->priv)
		return -1;
	if (range_ok(addr, len) != 0)
		return -1;
	p = (struct mem_priv *)m->priv;
	pthread_mutex_lock(&p->lock);
	first = page_of(addr);
	n = npages(len);
	for (i = 0; i < n; i++) {
		if (PG_STATE(p->page[first + i]) != PG_RW && PG_STATE(p->page[first + i]) != PG_RO) {
			pthread_mutex_unlock(&p->lock);
			return -1;            /* not mapped, or MMIO: nothing to protect */
		}
	}
	{
		uint8_t *old = (uint8_t *)malloc(n);
		if (!old) {
			pthread_mutex_unlock(&p->lock);
			return -1;
		}
		memcpy(old, p->page + first, n);
		for (i = 0; i < n; i++)
			p->page[first + i] = (uint8_t)((p->page[first + i] & PG_DIRTY) | (writable ? PG_RW : PG_RO));
		if (host_apply(m, addr, (uint64_t)n * PAGE_SIZE_U) != 0) {
			memcpy(p->page + first, old, n);
			host_apply(m, addr, (uint64_t)n * PAGE_SIZE_U);
			free(old);
			pthread_mutex_unlock(&p->lock);
			return -1;
		}
		free(old);
	}
	pthread_mutex_unlock(&p->lock);
	return 0;
}

uint32_t n2a32_mem_alloc(struct n2a32_mem *m, uint32_t len, uint32_t align)
{
	struct mem_priv *p;
	uint64_t a, start, plen, end;

	if (!m || !m->priv || len == 0)
		return 0;
	p = (struct mem_priv *)m->priv;
	pthread_mutex_lock(&p->lock);
	a = align ? (uint64_t)align : PAGE_SIZE_U;
	if ((a & (a - 1)) != 0) {
		pthread_mutex_unlock(&p->lock);
		return 0;                 /* alignment must be a power of two */
	}
	if (a < PAGE_SIZE_U)
		a = PAGE_SIZE_U;
	plen = ((uint64_t)len + PAGE_SIZE_U - 1) & ~(uint64_t)(PAGE_SIZE_U - 1);
	start = ((uint64_t)p->brk + a - 1) & ~(a - 1);
	if (start < ALLOC_START)
		start = ALLOC_START;

	for (;;) {
		size_t first, n, i;
		int conflict = 0;

		end = start + plen;
		if (end > ALLOC_LIMIT) {
			pthread_mutex_unlock(&p->lock);
			return 0;
		}
		first = page_of((uint32_t)start);
		n = (size_t)(plen / PAGE_SIZE_U);
		for (i = 0; i < n; i++) {
			if (PG_STATE(p->page[first + i]) != PG_FREE) {
				conflict = 1;
				break;
			}
		}
		if (!conflict)
			break;
		/* Skip past the conflicting page, keep the requested alignment, and look again. */
		start = ((uint64_t)(first + i + 1) * PAGE_SIZE_U + a - 1) & ~(a - 1);
	}
	if (map_locked(m, (uint32_t)start, (uint32_t)plen, 1) != 0) {
		pthread_mutex_unlock(&p->lock);
		return 0;
	}
	p->brk = (uint32_t)end;
	pthread_mutex_unlock(&p->lock);
	return (uint32_t)start;
}

int n2a32_mem_mmio(struct n2a32_mem *m, uint32_t addr, uint32_t len, const struct n2a32_mmio *hook)
{
	struct mem_priv *p;
	uint64_t end;
	size_t first, n, i;
	unsigned slot = 0;
	int found = 0;

	if (!m || !m->base || !m->priv || !hook)
		return -1;
	if (len == 0 || addr == 0 || (addr & MMIO_MASK) != 0 || (len & MMIO_MASK) != 0)
		return -1;                /* no MMIO at guest 0; both ends are megabyte aligned */
	end = (uint64_t)addr + len;
	if (end > GUEST_SIZE)
		return -1;
	p = (struct mem_priv *)m->priv;
	pthread_mutex_lock(&p->lock);
	first = page_of(addr);
	n = npages(len);
	for (i = 0; i < n; i++) {
		if (PG_STATE(p->page[first + i]) != PG_FREE) {
			pthread_mutex_unlock(&p->lock);
			return -1;            /* mapped memory or another hook: refuse rather than shadow it */
		}
	}
	/* Reuse an identical hook (same rd, wr, ctx), else take the first free slot.  Slot 0 is "plain memory". */
	for (i = 1; i < MMIO_SLOTS; i++) {
		if (m->mmio[i].rd == hook->rd && m->mmio[i].wr == hook->wr && m->mmio[i].ctx == hook->ctx) {
			slot = (unsigned)i;
			found = 1;
			break;
		}
	}
	if (!found) {
		for (i = 1; i < MMIO_SLOTS; i++) {
			if (m->mmio[i].rd == NULL && m->mmio[i].wr == NULL && m->mmio[i].ctx == NULL) {
				slot = (unsigned)i;
				found = 1;
				break;
			}
		}
	}
	if (!found) {
		pthread_mutex_unlock(&p->lock);
		return -1;                /* 15 distinct hooks is the documented limit */
	}
	if (granule > MMIO_SIZE || mprotect(m->base + addr, len, PROT_NONE) != 0) {
		mem_log(1, "n2a32_mem: mmio mprotect(guest 0x%08x, 0x%x, NONE) failed errno=%d granule=%u", addr, len, errno, granule);
		pthread_mutex_unlock(&p->lock);
		return -1;
	}
	m->mmio[slot] = *hook;        /* also copies a write-only hook whose rd is NULL */
	for (i = 0; i < (size_t)(len >> N2A32_MMIO_SHIFT); i++)
		m->mmio_idx[(addr >> N2A32_MMIO_SHIFT) + i] = (uint8_t)slot;
	for (i = 0; i < n; i++)
		p->page[first + i] = (uint8_t)((p->page[first + i] & PG_DIRTY) | PG_MMIO);
	pthread_mutex_unlock(&p->lock);
	return 0;
}

static uint32_t mmio_load_byte(struct n2a32_mem *m, uint32_t a)
{
	unsigned idx;
	struct n2a32_mmio hook;

	idx = m->mmio_idx[a >> N2A32_MMIO_SHIFT];
	if (idx == 0) {
		uint8_t b = 0;
		memcpy(&b, m->base + a, 1);
		return b;
	}
	if (idx >= MMIO_SLOTS)
		return 0;
	hook = m->mmio[idx];
	return hook.rd ? hook.rd(hook.ctx, a, 1) : 0;
}

static void mmio_store_byte(struct n2a32_mem *m, uint32_t a, uint32_t v)
{
	unsigned idx;
	struct n2a32_mmio hook;

	idx = m->mmio_idx[a >> N2A32_MMIO_SHIFT];
	if (idx == 0) {
		uint8_t b = (uint8_t)(v & 0xffu);
		memcpy(m->base + a, &b, 1);
		return;
	}
	if (idx >= MMIO_SLOTS)
		return;
	hook = m->mmio[idx];
	if (hook.wr)
		hook.wr(hook.ctx, a, 1, v & 0xffu);
}

uint32_t n2a32_mmio_load(struct n2a32_mem *m, uint32_t a, unsigned bytes)
{
	unsigned idx;
	struct n2a32_mmio hook;
	uint32_t v = 0;
	unsigned i;

	if (!m || !m->base)
		return 0;
	if (bytes == 0 || (uint64_t)a + bytes > GUEST_SIZE)
		return 0;
	idx = m->mmio_idx[a >> N2A32_MMIO_SHIFT];
	if (idx == 0) {
		memcpy(&v, m->base + a, bytes > 4 ? 4 : bytes);
		return v;
	}
	if (idx >= MMIO_SLOTS)
		return 0;
	/* A register access never straddles a megabyte; if a caller does it anyway, deliver bytes in order. */
	if ((a & MMIO_MASK) + bytes > MMIO_SIZE) {
		for (i = 0; i < bytes; i++)
			v |= mmio_load_byte(m, a + i) << (8u * i);
	} else {
		hook = m->mmio[idx];
		v = hook.rd ? hook.rd(hook.ctx, a, bytes) : 0;
	}
	n2a32_trace_mmio(m, a, bytes, v, 0);
	return v;
}

void n2a32_mmio_store(struct n2a32_mem *m, uint32_t a, unsigned bytes, uint32_t v)
{
	unsigned idx;
	struct n2a32_mmio hook;
	unsigned i;

	if (!m || !m->base || bytes == 0 || (uint64_t)a + bytes > GUEST_SIZE)
		return;
	idx = m->mmio_idx[a >> N2A32_MMIO_SHIFT];
	if (idx == 0) {
		memcpy(m->base + a, &v, bytes > 4 ? 4 : bytes);
		return;
	}
	if (idx >= MMIO_SLOTS)
		return;
	if ((a & MMIO_MASK) + bytes > MMIO_SIZE) {
		for (i = 0; i < bytes; i++)
			mmio_store_byte(m, a + i, v >> (8u * i));
	} else {
		hook = m->mmio[idx];
		if (hook.wr)
			hook.wr(hook.ctx, a, bytes, v);
	}
	n2a32_trace_mmio(m, a, bytes, v, 1);
}
