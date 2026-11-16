/*
 * 0481 (memwod-20261113): the kernel side of MADVISE for GPU buffers (Panfrost DRM_IOCTL_PANFROST_MADVISE).  SPDX-License-Identifier: MIT
 *
 * Mesa's BO cache keeps freed buffers (64 MiB per process, more in an idle one) and tells the kernel which ones it does not need:
 * MADV_DONTNEED when a buffer enters the cache, MADV_WILLNEED when it is taken out again; "retained = 0" in the answer means the
 * kernel dropped it meanwhile and Mesa frees the BO instead of reusing it.  The shim (y2s/nord2_pan.c R_MADVISE) answers retained = 1
 * itself today: the kernel had no call and no purge path.
 *
 * Selector NORD2_GPU_SEL_MADVISE (IOConnectCallMethod only): scalar inputs { handle, advice }, scalar output { retained }.
 *   DONTNEED  marks a private, pageable + purgeable, CPU-uncached buffer; anything else is accepted and simply never purged
 *   WILLNEED  clears the mark; retained = 0 if the handle no longer exists (purged) -- the caller then drops its BO: its CPU mapping
 *             now reads zeros, and RELEASE of that handle answers "no handle" (to be ignored)
 * Purge (under memory pressure only, from a 2 s thread call): a marked buffer that no job references is RELEASED by the kernel through
 * the core's ordinary release path (n2c_bo_release: GPU unmap, address-space flush, deferred until every older job of the context is
 * done -- the 0185/0187 rule for raw job chains), and when its owner goes its pages are unwired and DROPPED although the client still
 * maps them (setPurgeable(Empty), then NonVolatile again so that a stray CPU access only zero-fills a page).
 * kern.n2gpu_madv: 0 (default) = MADVISE answers retained = 1 and marks nothing, new buffers are not purgeable; 1 = on.
 * A buffer is purgeable only if it was allocated while the mode was 1.
 *
 * Pure decisions (no IOKit): shared by Nord2GPU.cpp (nord2_gpu_madv_kern.inc) and memwod-20261113/gpumadv-0481/test/madv_host_test.c.
 */
#ifndef NORD2_GPU_MADV_H
#define NORD2_GPU_MADV_H
/* (uint*_t / bool come from the includer) */

#define NORD2_GPU_SEL_MADVISE   16u
#define N2MADV_WILLNEED         0u
#define N2MADV_DONTNEED         1u

/* BufferOwner.madv */
#define N2MADV_F_DONTNEED       1u      /* the client does not need the contents */
#define N2MADV_F_PURGED         2u      /* released by the kernel: free_owner drops the pages */

/* What MADVISE may mark: a private (not imported) buffer of pageable, purgeable, prepared memory that is mapped in the GPU. */
static inline bool
n2madv_markable(bool has_md, bool prepared, bool md_purgeable, bool imported, bool mapped)
{
	return has_md && prepared && md_purgeable && !imported && mapped;
}

/* The call.  found: the handle names a live buffer of this context.  Returns false for a bad advice value.  *mark: +1 newly marked,
 * -1 mark cleared, 0 unchanged. */
static inline bool
n2madv_advise(int mode, bool found, bool markable, uint8_t *flags, uint32_t advice, uint32_t *retained, int *mark)
{
	*mark = 0;
	*retained = 1;
	if (advice > N2MADV_DONTNEED) { return false; }
	if (!found) { *retained = 0; return true; }             /* purged (or never there): the caller must drop its BO */
	if (advice == N2MADV_DONTNEED) {
		if (mode == 1 && markable && !(*flags & N2MADV_F_DONTNEED)) { *flags |= N2MADV_F_DONTNEED; *mark = 1; }
	} else if (*flags & N2MADV_F_DONTNEED) {
		*flags &= (uint8_t)~N2MADV_F_DONTNEED; *mark = -1;
	}
	return true;
}

/* May the purge pass release this buffer now?  refs: the handle's own reference + one per job that lists it. */
static inline bool
n2madv_purge_ok(uint8_t flags, bool handle_live, uint32_t refs, bool imported, bool mapped)
{
	return (flags & (N2MADV_F_DONTNEED | N2MADV_F_PURGED)) == N2MADV_F_DONTNEED && handle_live && refs == 1 && !imported && mapped;
}

/* Bytes the pass should free: 0 unless available pages are under the threshold (then the deficit + hysteresis). */
static inline uint64_t
n2madv_goal(uint64_t avail_pages, uint64_t thr_pages, uint32_t page_bytes, uint64_t hyst_bytes)
{
	return avail_pages >= thr_pages ? 0 : (thr_pages - avail_pages) * page_bytes + hyst_bytes;
}

/* Default threshold: free + file-backed pages under 20 % of RAM (the 0420 governor's "low" mark, where it starts to ask for purges;
 * its kill mark is 12 %).  Above it the cache stays: reusing a cached buffer is what keeps allocation out of the frame. */
static inline uint64_t
n2madv_default_thr(uint64_t ram_bytes, uint32_t page_bytes)
{
	return page_bytes ? ram_bytes / page_bytes / 5 : 0;
}
#endif /* NORD2_GPU_MADV_H */
