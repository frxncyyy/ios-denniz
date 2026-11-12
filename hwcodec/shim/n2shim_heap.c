/* SPDX-License-Identifier: MIT */
/*
 * n2shim_heap.c -- the guest heap and anonymous mmap (package V30).
 *
 * The guest owns one 4 GiB address space (struct n2a32_mem).  This allocator carves blocks out of arenas handed
 * over by n2a32_mem_alloc(), which never reuses an address: freed blocks are kept on per-size-class free lists
 * inside the guest memory itself, so the heap does not grow when the guest frees and re-allocates.
 *
 * Layout of a block:
 *
 *      base            base+16 = payload (16-byte aligned)
 *      | header 16 B   | user bytes ...                       |
 *      | size   magic  | cls  parent                          |
 *
 * magic is N2H_MAGIC while the block is live, N2H_FREED once it is on a free list (a second free is therefore
 * detected instead of corrupting a list), and cls is the size-class index, or N2H_ALIGNED|log2(align) for a
 * memalign()ed block, whose `parent` field holds the payload address of the underlying block.  A payload is
 * always 16-byte aligned; block sizes are 32 << cls, so the class of a block is recovered from its header.
 *
 * Thread safety: one host mutex around every operation.  Guest code runs on several host threads (V32) and the
 * heap is process-wide, exactly like the C library's.
 *
 * Errors never abort the host: a bad magic, a double free, a foreign address or a NULL mem increments a
 * diagnostic counter and leaves the heap untouched (the block leaks).  n2shim_heap_errors() exposes the count
 * for the tests; nothing prints.
 *
 * mmap: fd == -1 (anonymous) gets fresh zero-filled guest pages.  MAP_FIXED maps at the requested address.
 * There is no unmap operation in the core interface, so munmap() drops the mapping bookkeeping and returns 0;
 * the pages stay allocated for the life of the process (the vendor maps a handful of large buffers once).
 * A descriptor-backed mmap is forwarded to V33's strong n2vfs_mmap(); without it the call fails with EINVAL.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2fmt.h"   /* n2vfs_mmap / n2vfs_errno_location (weak here, strong in V33) */

#define N2H_MAGIC    0x4e32484du   /* "N2HM" */
#define N2H_FREED    0x4e324846u   /* "N2HF" */
#define N2H_ALIGNED  0x80000000u
#define N2H_ALIGN    16u
#define N2H_HDR      16u
#define N2H_MIN      32u
#define N2H_CLASSES  27            /* 32 << 26 == 2 GiB, the largest class */
#define N2H_ARENA    (1u << 20)
#define N2H_LARGE    (256u << 10)  /* at or above this, the block gets its own arena */

/* Linux mmap/munmap constants as the guest sees them (32-bit ARM). */
#define N2H_PROT_WRITE 0x2
#define N2H_MAP_FIXED  0x10
#define N2H_MAP_ANON   0x20
#define N2H_MAP_FAILED 0xffffffffu
#define N2H_ENOMEM     12
#define N2H_EINVAL     22

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct n2a32_mem *g_mem;
static uint32_t g_free[N2H_CLASSES];
static uint32_t g_bump, g_bump_end;
static uint64_t g_used, g_peak;
static uint64_t g_allocs, g_frees;
static uint32_t g_errors;

uint64_t n2shim_heap_used(void);
uint64_t n2shim_heap_peak(void);
uint64_t n2shim_heap_allocs(void);
uint64_t n2shim_heap_frees(void);
uint32_t n2shim_heap_errors(void);

uint64_t n2shim_heap_used(void)
{
	uint64_t v;
	pthread_mutex_lock(&g_lock);
	v = g_used;
	pthread_mutex_unlock(&g_lock);
	return v;
}

uint64_t n2shim_heap_peak(void)
{
	uint64_t v;
	pthread_mutex_lock(&g_lock);
	v = g_peak;
	pthread_mutex_unlock(&g_lock);
	return v;
}

uint64_t n2shim_heap_allocs(void)
{
	uint64_t v;
	pthread_mutex_lock(&g_lock);
	v = g_allocs;
	pthread_mutex_unlock(&g_lock);
	return v;
}

uint64_t n2shim_heap_frees(void)
{
	uint64_t v;
	pthread_mutex_lock(&g_lock);
	v = g_frees;
	pthread_mutex_unlock(&g_lock);
	return v;
}

/* Diagnostic walk of the free lists; the test calls it to catch corruption early.  Returns the number of
 * inconsistent links (0 = clean). */
uint32_t n2shim_heap_check(struct n2a32_cpu *c)
{
	uint32_t bad = 0;
	int cls;

	pthread_mutex_lock(&g_lock);
	for (cls = 0; cls < N2H_CLASSES; cls++) {
		uint32_t p = g_free[cls];
		uint32_t guard = 0;

		while (p && guard++ < 1000000u) {
			if (n2a32_ld32(c, p + 4) != N2H_FREED || n2a32_ld32(c, p + 8) != (uint32_t)cls)
				bad++;
			p = n2a32_ld32(c, p + N2H_HDR);
		}
		if (guard >= 1000000u)
			bad++;
	}
	pthread_mutex_unlock(&g_lock);
	return bad;
}

uint32_t n2shim_heap_errors(void)
{
	uint32_t v;
	pthread_mutex_lock(&g_lock);
	v = g_errors;
	pthread_mutex_unlock(&g_lock);
	return v;
}

/* ---- block helpers ------------------------------------------------------------------------------------------- */

static void hdr_write(struct n2a32_cpu *c, uint32_t base, uint32_t size, uint32_t magic, uint32_t cls,
		      uint32_t parent)
{
	n2a32_st32(c, base + 0, size);
	n2a32_st32(c, base + 4, magic);
	n2a32_st32(c, base + 8, cls);
	n2a32_st32(c, base + 12, parent);
}

static int class_of(uint32_t total)
{
	int cls = 0;
	uint32_t s = N2H_MIN;

	while (s < total) {
		if (cls == N2H_CLASSES - 1)
			return -1;
		s <<= 1;
		cls++;
	}
	return cls;
}

static int log2_exact(uint32_t v)
{
	int n = 0;
	if (!v || (v & (v - 1)))
		return -1;
	while (v > 1) {
		v >>= 1;
		n++;
	}
	return n;
}

static uint32_t new_pages(struct n2a32_cpu *c, uint32_t len)
{
	return n2a32_mem_alloc(c->mem, len, 4096);
}

/* Carve one class block from the bump area, allocating a new arena when needed.  g_lock held. */
static uint32_t carve(struct n2a32_cpu *c, uint32_t size)
{
	uint32_t base;

	if ((uint64_t)g_bump + size <= g_bump_end) {
		base = g_bump;
		g_bump += size;
		return base;
	}
	if (size >= N2H_LARGE)               /* dedicated arena: do not strand a mostly unused bump area */
		return new_pages(c, size);
	{
		uint32_t len = N2H_ARENA;
		while (len < size + 4096) {
			if (len > 0x40000000u)
				return 0;
			len <<= 1;
		}
		base = new_pages(c, len);
		if (!base)
			return 0;
		g_bump = base;
		g_bump_end = base + len;
	}
	base = g_bump;
	g_bump += size;
	return base;
}

/* g_lock held.  Returns the guest payload address, 0 on failure. */
#define N2H_ZERO_REUSE 0x10000u   /* recycled blocks this large are handed out zeroed (bionic's large allocations) */

static uint32_t heap_alloc_locked(struct n2a32_cpu *c, uint32_t size, uint32_t align)
{
	uint32_t need = size ? size : 1;
	uint32_t total, bsize, base, payload, cls;
	int lc;

	if (size > 0x7ffff000u)
		return 0;
	lc = log2_exact(align);
	if (lc < 0)
		return 0;
	if (align < N2H_ALIGN)
		align = N2H_ALIGN;               /* over-aligned is always acceptable */
	if (align > N2H_ALIGN) {
		/* reserve room for the alignment header and the gap; the payload size includes them */
		if (need > 0x7ff00000u - align - N2H_HDR)
			return 0;
		total = N2H_HDR + ((need + align + N2H_HDR + N2H_ALIGN - 1) & ~(N2H_ALIGN - 1));
	} else {
		total = N2H_HDR + ((need + N2H_ALIGN - 1) & ~(N2H_ALIGN - 1));
	}
	if (total < N2H_MIN)
		total = N2H_MIN;
	cls = (uint32_t)class_of(total);
	if ((int)cls < 0)
		return 0;
	bsize = N2H_MIN << cls;

	if (g_free[cls]) {
		base = g_free[cls];
		if (n2a32_ld32(c, base + 4) != N2H_FREED || n2a32_ld32(c, base + 8) != cls) {
			g_errors++;                    /* free-list corruption: refuse the block */
			g_free[cls] = 0;
			return 0;
		}
		g_free[cls] = n2a32_ld32(c, base + N2H_HDR);   /* next pointer lives in the payload */
		hdr_write(c, base, size, N2H_MAGIC, cls, 0);
		/* opus-enc4k, 12 Nov: bionic serves large blocks from fresh anonymous mappings (unmapped again on free), so a
		 * large malloc() on the phone's Android always returned zero pages, and the vendor relies on it: hevc_enc_init
		 * mallocs its 0x5ab10-byte handle and clears only 0x424 bytes (h264_enc_init clears all of it), so a reused
		 * block kept the previous session's GCE buffer-list slots (handle+0x58cc0+0x18) -> "[Error] too many GCE
		 * buffers !" and the second HEVC session's frame never completed (vc18/vc24/vc25 hangs, reproduced on the host).
		 * Recycled blocks of 64 KiB and more are therefore zeroed like a fresh mapping. */
		if (bsize >= N2H_ZERO_REUSE)
			memset(n2a32_ptr(c->mem, base + N2H_HDR), 0, (size_t)(bsize - N2H_HDR));
	} else {
		base = carve(c, bsize);
		if (!base) {
			g_errors++;
			return 0;
		}
		hdr_write(c, base, size, N2H_MAGIC, cls, 0);
	}
	payload = base + N2H_HDR;
	if (align > N2H_ALIGN) {
		uint32_t a = (payload + N2H_HDR + align - 1) & ~(align - 1);
		if (a + need > base + bsize)
			return 0;                /* cannot happen with the size above; refuse rather than overrun */
		/* give the aligned address its own header; the parent is freed through it */
		hdr_write(c, a - N2H_HDR, size, N2H_MAGIC, N2H_ALIGNED | (uint32_t)lc, payload);
		payload = a;
	}
	g_used += size;
	if (g_used > g_peak)
		g_peak = g_used;
	g_allocs++;
	return payload;
}

/* Resolve an aligned payload to the underlying block payload; 0 if this is not one of ours. */
static uint32_t block_of(struct n2a32_cpu *c, uint32_t p, uint32_t *magic_out)
{
	uint32_t hdr, magic, cls;

	if (!p || p < N2H_HDR)
		return 0;                        /* a pointer below the first possible header cannot be a block */
	hdr = p - N2H_HDR;
	magic = n2a32_ld32(c, hdr + 4);
	cls = n2a32_ld32(c, hdr + 8);
	if (magic_out)
		*magic_out = magic;
	if (magic != N2H_MAGIC)
		return 0;
	if (cls & N2H_ALIGNED)
		return n2a32_ld32(c, hdr + 12);
	return p;
}

/* g_lock held. */
static void heap_free_locked(struct n2a32_cpu *c, uint32_t p)
{
	uint32_t magic = 0, cls, hdr, base;

	if (!p)
		return;
	base = block_of(c, p, &magic);
	if (!base) {
		if (magic == N2H_FREED)
			g_errors++;              /* double free (or free of a stale pointer) */
		else
			g_errors++;              /* not a block of this heap */
		return;
	}
	hdr = base - N2H_HDR;
	cls = n2a32_ld32(c, hdr + 8);
	if (cls >= N2H_CLASSES) {
		g_errors++;
		return;
	}
	g_used -= n2a32_ld32(c, hdr + 0);
	g_frees++;
	n2a32_st32(c, hdr + 4, N2H_FREED);
	n2a32_st32(c, hdr + N2H_HDR, g_free[cls]);   /* free-list link lives in the payload */
	g_free[cls] = hdr;                            /* the lists hold block starts, not payloads */
	if (p != base) {                              /* the aligned header is dead too */
		n2a32_st32(c, p - N2H_HDR + 4, N2H_FREED);
	}
}

/* ---- libc entry points --------------------------------------------------------------------------------------- */

static void sh_malloc(struct n2a32_cpu *c)
{
	uint32_t p;

	pthread_mutex_lock(&g_lock);
	if (!g_mem)
		g_mem = c->mem;
	else if (g_mem != c->mem) {
		g_errors++;
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	p = heap_alloc_locked(c, n2shim_arg(c, 0), N2H_ALIGN);
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, p);
}

static void sh_calloc(struct n2a32_cpu *c)
{
	uint32_t n = n2shim_arg(c, 0), sz = n2shim_arg(c, 1), p;
	uint64_t total = (uint64_t)n * sz;

	pthread_mutex_lock(&g_lock);
	if (!g_mem)
		g_mem = c->mem;
	if (g_mem != c->mem || total > 0x7ffff000u) {
		g_errors++;
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	p = heap_alloc_locked(c, (uint32_t)total, N2H_ALIGN);
	if (p && total)
		memset(n2a32_ptr(c->mem, p), 0, (size_t)total);
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, p);
}

static void sh_free(struct n2a32_cpu *c)
{
	pthread_mutex_lock(&g_lock);
	heap_free_locked(c, n2shim_arg(c, 0));
	pthread_mutex_unlock(&g_lock);
}

static void sh_memalign(struct n2a32_cpu *c)
{
	uint32_t align = n2shim_arg(c, 0), size = n2shim_arg(c, 1), p;

	pthread_mutex_lock(&g_lock);
	if (!g_mem)
		g_mem = c->mem;
	if (g_mem != c->mem || log2_exact(align) < 0) {
		g_errors++;
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	p = heap_alloc_locked(c, size, align);
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, p);
}

static void sh_realloc(struct n2a32_cpu *c)
{
	uint32_t p = n2shim_arg(c, 0), n = n2shim_arg(c, 1), base, magic = 0, old = 0, np;

	pthread_mutex_lock(&g_lock);
	if (!g_mem)
		g_mem = c->mem;
	if (g_mem != c->mem) {
		g_errors++;
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	if (!p) {
		np = heap_alloc_locked(c, n, N2H_ALIGN);
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, np);
		return;
	}
	if (!n) {
		heap_free_locked(c, p);
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	base = block_of(c, p, &magic);
	if (!base) {
		g_errors++;
		pthread_mutex_unlock(&g_lock);
		n2shim_ret(c, 0);
		return;
	}
	old = n2a32_ld32(c, base - N2H_HDR + 0);
	{
		uint32_t cls = n2a32_ld32(c, p - N2H_HDR + 8);
		uint32_t align = N2H_ALIGN;

		if (cls & N2H_ALIGNED)
			align = 1u << (cls & 0x1fu);
		if (n <= old && n >= old / 2) {      /* in place: the block is big enough */
			pthread_mutex_unlock(&g_lock);
			n2shim_ret(c, p);
			return;
		}
		np = heap_alloc_locked(c, n, align);
		if (np) {
			uint32_t copy = old < n ? old : n;
			if (copy)
				memcpy(n2a32_ptr(c->mem, np), n2a32_ptr(c->mem, p), copy);
			heap_free_locked(c, p);
		}
	}
	pthread_mutex_unlock(&g_lock);
	n2shim_ret(c, np);
}

/* posix_memalign(void **memptr, size_t alignment, size_t size): 0, EINVAL or ENOMEM; never a guest pointer
 * return value. */
static void sh_posix_memalign(struct n2a32_cpu *c)
{
	uint32_t pp = n2shim_arg(c, 0), align = n2shim_arg(c, 1), size = n2shim_arg(c, 2), p;

	if (!pp || log2_exact(align) < 0 || (align & 3)) {
		if (pp)
			n2a32_st32(c, pp, 0);
		n2shim_ret(c, N2H_EINVAL);
		return;
	}
	if (align < N2H_ALIGN)
		align = N2H_ALIGN;
	pthread_mutex_lock(&g_lock);
	if (!g_mem)
		g_mem = c->mem;
	if (g_mem != c->mem) {
		g_errors++;
		pthread_mutex_unlock(&g_lock);
		n2a32_st32(c, pp, 0);
		n2shim_ret(c, N2H_ENOMEM);
		return;
	}
	p = heap_alloc_locked(c, size, align);
	pthread_mutex_unlock(&g_lock);
	if (!p) {
		n2a32_st32(c, pp, 0);
		n2shim_ret(c, N2H_ENOMEM);
		return;
	}
	n2a32_st32(c, pp, p);
	n2shim_ret(c, 0);
}

static void sh_getpagesize(struct n2a32_cpu *c)
{
	n2shim_ret(c, 4096);
}

/* ---- mmap ---------------------------------------------------------------------------------------------------- */

static void set_errno(struct n2a32_cpu *c, int e)
{
	uint32_t loc = n2vfs_errno_location(c);

	if (loc)
		n2a32_st32(c, loc, (uint32_t)e);
}

static void sh_mmap_common(struct n2a32_cpu *c, uint32_t addr, uint32_t len, int prot, int flags, int fd,
			   uint64_t off)
{
	uint32_t rlen, p;

	if (!len) {
		set_errno(c, N2H_EINVAL);
		n2shim_ret(c, N2H_MAP_FAILED);
		return;
	}
	if (len > 0xfffff000u) {
		set_errno(c, N2H_ENOMEM);
		n2shim_ret(c, N2H_MAP_FAILED);
		return;
	}
	rlen = (len + 4095u) & ~4095u;
	if (fd == -1 || (flags & N2H_MAP_ANON)) {
		if ((flags & N2H_MAP_FIXED) && addr) {
			if (n2a32_mem_map(c->mem, addr & ~4095u, rlen, 1) != 0) {
				set_errno(c, N2H_ENOMEM);
				n2shim_ret(c, N2H_MAP_FAILED);
				return;
			}
			n2shim_ret(c, addr & ~4095u);
			return;
		}
		p = n2a32_mem_alloc(c->mem, rlen, 4096);
		if (!p) {
			set_errno(c, N2H_ENOMEM);
			n2shim_ret(c, N2H_MAP_FAILED);
			return;
		}
		n2shim_ret(c, p);
		return;
	}
	p = n2vfs_mmap(c, addr, len, prot, flags, fd, off);
	if (!p) {
		set_errno(c, N2H_EINVAL);
		n2shim_ret(c, N2H_MAP_FAILED);
		return;
	}
	n2shim_ret(c, p);
}

static void sh_mmap(struct n2a32_cpu *c)
{
	/* (addr, len, prot, flags, fd, off_t off32) */
	sh_mmap_common(c, n2shim_arg(c, 0), n2shim_arg(c, 1), (int)n2shim_arg(c, 2),
		       (int)n2shim_arg(c, 3), (int)n2shim_arg(c, 4), (uint64_t)n2shim_arg(c, 5));
}

static void sh_mmap64(struct n2a32_cpu *c)
{
	/* fd at stack slot 4, then the 8-byte offset aligned to slot 6: slots 6 and 7 */
	uint32_t lo = n2shim_arg(c, 6), hi = n2shim_arg(c, 7);

	sh_mmap_common(c, n2shim_arg(c, 0), n2shim_arg(c, 1), (int)n2shim_arg(c, 2),
		       (int)n2shim_arg(c, 3), (int)n2shim_arg(c, 4),
		       (uint64_t)lo | ((uint64_t)hi << 32));
}

static void sh_munmap(struct n2a32_cpu *c)
{
	n2shim_ret(c, 0);
}

/* ---- weak hooks (V33 overrides them) ------------------------------------------------------------------------- */

__attribute__((weak)) uint32_t n2vfs_mmap(struct n2a32_cpu *c, uint32_t addr, uint32_t len, int prot, int flags,
					  int fd, uint64_t off)
{
	(void)c;
	(void)addr;
	(void)len;
	(void)prot;
	(void)flags;
	(void)fd;
	(void)off;
	return 0;
}

__attribute__((weak)) uint32_t n2vfs_errno_location(struct n2a32_cpu *c)
{
	(void)c;
	return 0;
}

/* ---- table --------------------------------------------------------------------------------------------------- */

const struct n2shim_entry n2shim_heap[] = {
	{ "malloc", sh_malloc },
	{ "calloc", sh_calloc },
	{ "free", sh_free },
	{ "memalign", sh_memalign },
	{ "getpagesize", sh_getpagesize },
	{ "mmap", sh_mmap },
	{ "mmap64", sh_mmap64 },
	{ "munmap", sh_munmap },
	/* not imported by the five objects today, but part of the same allocator surface */
	{ "realloc", sh_realloc },
	{ "posix_memalign", sh_posix_memalign },
	{ NULL, NULL },
};
