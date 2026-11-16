/* Host test of the Panfrost request layer against a model of the Nord2GPU kernel.  SPDX-License-Identifier: MIT */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "nord2_pan.h"
#include "../include/nord2_gpu_transport.h"

struct kbo { int live; uint64_t va, size; void *mem; int released; };
static struct {
	struct kbo bo[2000];
	unsigned last_flags, not_ready, next, live, fail_alloc_with_gpu_only, busy_once, fault_next;
	uint64_t job, waited_to, done_to;
	struct nord2_gpu_submit_request last[16];
	unsigned nlast, submits, release_busy;
	unsigned max_inflight_seen, imports;
} K;

static int kcall(void *o, unsigned sel, const uint64_t *in, unsigned ni, const void *bytes, size_t size,
		 uint64_t *out, unsigned *no, void *ob, size_t *os)
{
	(void)o; (void)ni; (void)size;
	switch (sel) {
	case NORD2_GPU_SEL_STATUS: {
		struct nord2_gpu_status s = { .abi_version = 2, .enabled = 1, .powered = 1, .arch_ok = 1,
					      .gpu_id = 0x90910011, .tiler_features = 0x809, .shader_present_lo = 0x660067,
					      .tiler_present_lo = 1, .js_present = 7 };
		memcpy(ob, &s, sizeof(s));
		*os = sizeof(s);
		return 0;
	}
	case NORD2_GPU_SEL_HWINFO: {
		struct nord2_gpu_hwinfo w = { .version = 1, .thread_max_threads = 768, .thread_features = 0x0a040400, .texture_features = { 0xc1ffffff }, .js_features = { 0x20e, 0x20e, 0x7e } };
		memcpy(ob, &w, sizeof(w)); *os = sizeof(w);
		return 0;
	}
	case NORD2_GPU_SEL_ALLOC: {
		if (K.not_ready) { K.not_ready--; return (int)0xe00002d8u; }
		if (in[1] > 7 || (in[1] & 4 && K.fail_alloc_with_gpu_only) || (in[1] & 2 && K.fail_alloc_with_gpu_only))
			return (int)0xe00002c2u;
		K.last_flags = (unsigned)in[1];
		if (K.live >= 256)
			return (int)0xe00002beu;
		unsigned h = ++K.next;
		K.bo[h] = (struct kbo){ .live = 1, .va = 0x1000000000ull + (uint64_t)h * 0x2000000ull,
					.size = (in[0] + 16383) & ~16383ull };
		K.bo[h].mem = calloc(1, K.bo[h].size);
		K.live++;
		out[0] = h; out[1] = K.bo[h].va; out[2] = K.bo[h].size; *no = 3;
		return 0;
	}
	case NORD2_GPU_SEL_RELEASE:
		assert(K.bo[in[0]].live);     /* Busy: the handle is consumed, the free is deferred */
		free(K.bo[in[0]].mem);
		K.bo[in[0]].live = 0;
		K.live--;
		if (K.release_busy) { K.release_busy--; return (int)0xe00002d5u; }
		return 0;
	case NORD2_GPU_SEL_IMPORT_SURFACE: {
		assert(ni == 4 && in[0] == 77 && *no == 3);
		unsigned h = ++K.next;
		K.bo[h] = (struct kbo){ .live = 1, .va = 0x1000000000ull + (uint64_t)h * 0x2000000ull, .size = in[2] };
		K.bo[h].mem = calloc(1, in[2]);
		K.live++; K.imports++;
		out[0] = h; out[1] = K.bo[h].va; out[2] = in[2];
		return 0;
	}
	case NORD2_GPU_SEL_SUBMIT_JM: {
		const struct nord2_gpu_submit_request *r = bytes;
		assert(size == sizeof(*r) && r->version == 2);
		if (K.busy_once) { K.busy_once--; return (int)0xe00002d5u; }
		/* the head must lie in a listed buffer */
		uint64_t head = r->kind == 3 ? r->heads[1] : r->heads[0];
		int ok = 0;
		assert(r->kind == 1 || r->kind == 3);
		assert((r->kind == 1 && r->heads[1] == 0) || (r->kind == 3 && r->heads[0] == 0));
		assert(r->nr_handles >= 1 && r->nr_handles <= 32);
		for (unsigned i = 0; i < r->nr_handles; i++) {
			struct kbo *b = &K.bo[r->handles[i]];
			assert(b->live);
			ok |= head >= b->va && head + 8 <= b->va + b->size;
		}
		assert(ok);
		assert(r->nr_handles >= 32 || !(r->write_mask >> r->nr_handles));
		K.last[K.nlast++ & 15] = *r;
		K.submits++;
		out[0] = ++K.job; *no = 1;
		return 0;
	}
	case NORD2_GPU_SEL_WAIT_SEQ: {
		if (in[0] > K.job) return (int)0xe00002f0u;
		if (K.fault_next) {
			K.fault_next--;
			out[0] = 5; out[1] = 1; out[2] = in[0]; *no = 3;
			if (ob && os) { memset(ob, 0, *os); }
			return 0;
		}
		K.done_to = in[0] > K.done_to ? in[0] : K.done_to;
		out[0] = 4; out[1] = in[0]; out[2] = 0; *no = 3;
		return 0;
	}
	}
	return (int)0xe00002c7u;
}
static int kmap(void *o, unsigned h, uint64_t *a, uint64_t *s)
{
	(void)o;
	assert(K.bo[h].live);
	*a = (uint64_t)(uintptr_t)K.bo[h].mem; *s = K.bo[h].size;
	return 0;
}
static int ksurface(void *o, uint32_t id, struct np_surface_info *s)
{
	static char pixels[4 * 64 * 64];
	(void)o;
	if (id != 77) return -1;
	*s = (struct np_surface_info){ .size = sizeof(pixels), .cpu = (uint64_t)(uintptr_t)pixels, .rowBytes = 256, .width = 64,
				       .height = 64, .pixelFormat = 0x42475241, .planes = 1, .planeRowBytes = { 256, 0 } };
	return 0;
}
static int kunmap(void *o, unsigned h, uint64_t a) { (void)o; (void)h; (void)a; return 0; }

struct create_bo { uint32_t size, flags, handle, pad; uint64_t offset; };
struct submit { uint64_t jc, in_syncs; uint32_t in_sync_count, out_sync; uint64_t bo_handles;
		uint32_t bo_handle_count, requirements; uint64_t seqno; };
#define MESA_SUBMIT 40   /* Mesa 25.0.7 sends struct drm_panfrost_submit without jm_ctx_handle */
struct wait_bo { uint32_t handle, pad; int64_t timeout_ns; };
struct get_param { uint32_t param, pad; uint64_t value; };
struct so_create { uint32_t handle, flags; };
struct so_wait { uint64_t handles; int64_t timeout_nsec; uint32_t count_handles, flags, first_signaled, pad; uint64_t deadline_nsec; };
struct so_array { uint64_t handles; uint32_t count_handles, pad; };
struct gem_close { uint32_t handle, pad; };

static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
static uint32_t mk(unsigned size, unsigned flags, uint64_t *va)
{
	struct create_bo q = { .size = size, .flags = flags };
	assert(np_call(0x42, &q, sizeof(q), NULL) == 0 && q.handle && q.offset);
	if (va) *va = q.offset;
	return q.handle;
}

int main(void)
{
	struct np_ops ops = { .call = kcall, .map = kmap, .unmap = kunmap, .surface = ksurface };
	assert(np_call(0x44, &(struct get_param){0}, 16, NULL) == -1 && errno == ENODEV);
	assert(np_init(&ops) == 0 && np_ready());
	np_pool_enable(0);                  /* the legacy scenarios below close and release */

	struct get_param gp = { .param = 0 };
	assert(np_call(0x44, &gp, sizeof(gp), NULL) == 0 && gp.value == 0x9091);
	gp.param = 2; assert(np_call(0x44, &gp, sizeof(gp), NULL) == 0 && gp.value == 0x660067);
	gp.param = 14; assert(np_call(0x44, &gp, sizeof(gp), NULL) == 0 && gp.value == 768);
	gp.param = 18; assert(np_call(0x44, &gp, sizeof(gp), NULL) == 0 && gp.value == 0xc1ffffff);
	gp.param = 24; assert(np_call(0x44, &gp, sizeof(gp), NULL) == 0 && gp.value == 0x7e);
	gp.param = 999; assert(np_call(0x44, &gp, sizeof(gp), NULL) == -1 && errno == EINVAL);

	K.not_ready = 3;   /* the first allocations answer NotReady: the call waits */
	/* buffers: descriptor pool, shader (executable), heap */
	uint64_t va_pool, va_code, va_heap;
	uint32_t pool = mk(65536, 1, &va_pool), code = mk(4096, 0, &va_code), heap = mk(1 << 20, 3, &va_heap);
	assert(va_pool != va_code && va_code != va_heap);
	uint64_t addr = 0, size = 0;
	assert(np_map(pool, &addr, &size) == 0 && addr && size == 65536);
	uint64_t addr2 = 0;
	assert(np_map(pool, &addr2, &size) == 0 && addr2 == addr);
	assert(np_map(999, &addr2, &size) == -1);

	/* a heap on a kernel without GPU-only retries without it */
	K.fail_alloc_with_gpu_only = 1; uint32_t heap2 = mk(1 << 20, 3, NULL); K.fail_alloc_with_gpu_only = 0;

	/* syncobjs */
	struct so_create sc = { .flags = 0 };
	assert(np_call(0xbf, &sc, sizeof(sc), NULL) == 0 && sc.handle);
	uint32_t so = sc.handle;

	/* vertex/tiler job then fragment job */
	uint32_t list[4] = { code, pool, heap, heap2 };
	struct submit s = { .jc = va_pool + 64, .bo_handles = (uint64_t)(uintptr_t)list, .bo_handle_count = 4, .out_sync = so };
	assert(np_call(0x40, &s, sizeof(s), NULL) == 0 && s.seqno == 1);
	assert(K.last[0].kind == 1 && K.last[0].heads[0] == va_pool + 64 && K.last[0].nr_handles == 4);
	assert(K.last[0].handles[0] == 1);           /* the buffer holding the chain head goes first: the pool, kernel handle 1 */
	s.requirements = 1; s.jc = va_pool + 256; s.seqno = 0;
	assert(np_call(0x40, &s, sizeof(s), NULL) == 0 && s.seqno == 2);
	assert(K.last[1].kind == 3 && K.last[1].heads[1] == va_pool + 256 && K.last[1].heads[0] == 0);
	/* write mask: data buffers yes, executable and heap no */
	for (unsigned i = 0; i < K.last[1].nr_handles; i++) {
		unsigned kh = K.last[1].handles[i];
		unsigned bit = (K.last[1].write_mask >> i) & 1;
		if (kh == 1) assert(bit == 1);            /* pool */
		if (kh == 2) assert(bit == 0);            /* code */
		if (kh == 3 || kh == 4) assert(bit == 0); /* heaps */
	}

	/* a head in none of the listed buffers is refused */
	uint32_t only_code[1] = { code };
	struct submit bad = { .jc = va_pool, .bo_handles = (uint64_t)(uintptr_t)only_code, .bo_handle_count = 1 };
	assert(np_call(0x40, &bad, MESA_SUBMIT, NULL) == -1 && errno == EINVAL);

	/* the busy window answers once: the call waits and retries */
	K.busy_once = 1;
	s.requirements = 0; s.out_sync = 0;
	assert(np_call(0x40, &s, MESA_SUBMIT, NULL) == 0);

	/* fences: the syncobj of job 1 is signalled once job 1 is waited */
	uint32_t hs[1] = { so };
	struct so_wait w = { .handles = (uint64_t)(uintptr_t)hs, .timeout_nsec = (int64_t)(now() + 1000000000ull), .count_handles = 1, .flags = 1 };
	assert(np_call(0xc3, &w, sizeof(w), NULL) == 0);
	assert(*np_completed_ptr() >= 1);

	/* a reset syncobj never signals: only the deadline ends the wait */
	struct so_array ra = { .handles = (uint64_t)(uintptr_t)hs, .count_handles = 1 };
	assert(np_call(0xc4, &ra, sizeof(ra), NULL) == 0);
	w.timeout_nsec = (int64_t)(now() + 20000000ull);
	assert(np_call(0xc3, &w, sizeof(w), NULL) == -1 && errno == ETIMEDOUT);
	assert(np_call(0xc5, &ra, sizeof(ra), NULL) == 0);
	w.timeout_nsec = 0;
	assert(np_call(0xc3, &w, sizeof(w), NULL) == 0);

	/* idle query of a buffer listed by a finished job, and of one a newer job lists */
	struct wait_bo wb = { .handle = pool, .timeout_ns = 0 };
	s.requirements = 1; s.out_sync = 0;
	assert(np_call(0x40, &s, MESA_SUBMIT, NULL) == 0);
	K.done_to = 0;
	assert(np_call(0x41, &wb, sizeof(wb), NULL) == -1 && errno == ETIMEDOUT);    /* busy, no wait asked */
	wb.timeout_ns = (int64_t)(now() + 1000000000ull);
	assert(np_call(0x41, &wb, sizeof(wb), NULL) == 0);
	wb.handle = code; wb.timeout_ns = 0; assert(np_call(0x41, &wb, sizeof(wb), NULL) == 0);

	/* a faulted job is an I/O error, and the failure is counted */
	s.requirements = 1;
	assert(np_call(0x40, &s, MESA_SUBMIT, NULL) == 0);
	K.fault_next = 1; wb.handle = pool; wb.timeout_ns = (int64_t)(now() + 1000000000ull);
	assert(np_call(0x41, &wb, sizeof(wb), NULL) == -1 && errno == EIO);

	/* more than 32 buffers in the list: 32 go down, the head's buffer among them, full maintenance counted */
	uint32_t many[100];
	many[0] = code;
	for (unsigned i = 1; i < 100; i++) many[i] = (i == 99) ? pool : mk(4096, 1, NULL);
	struct submit big = { .jc = va_pool + 8, .bo_handles = (uint64_t)(uintptr_t)many, .bo_handle_count = 100, .requirements = 1 };
	assert(np_call(0x40, &big, MESA_SUBMIT, NULL) == 0);
	{
		const struct nord2_gpu_submit_request *r = &K.last[(K.nlast - 1) & 15];
		assert(r->nr_handles == 32 && r->handles[0] == 1);
	}
	struct np_stats st; np_stats(&st);
	assert(st.full_maint == 1 && st.submit_bos_dropped == 100 - 32 && st.no_head_bo == 1);

	struct gem_close gc = { 0 };
	/* an IOSurface: described, imported once, the same buffer again; unknown surfaces are refused */
	struct surface_req { uint32_t surface, handle; uint64_t size; uint32_t offset, rowBytes, width, height, pixelFormat, planes, po[2], pr[2]; } sr = { .surface = 77, .handle = 0xffffffffu };
	assert(np_call(0x7e, &sr, sizeof(sr), NULL) == 0 && sr.handle == 0 && sr.width == 64 && sr.rowBytes == 256 && K.imports == 0);
	sr.handle = 0; assert(np_call(0x7e, &sr, sizeof(sr), NULL) == 0 && sr.handle && K.imports == 1);
	uint32_t surf = sr.handle; sr.handle = 0; assert(np_call(0x7e, &sr, sizeof(sr), NULL) == 0 && sr.handle == surf && K.imports == 1);
	{ struct surface_req un = { .surface = 5 }; assert(np_call(0x7e, &un, sizeof(un), NULL) == -1 && errno == ENOENT); }
	{ uint32_t l[2] = { pool, surf }; uint64_t vs = 0; struct get_param dummy; (void)dummy; (void)vs;
	  struct submit fs = { .jc = va_pool + 512, .bo_handles = (uint64_t)(uintptr_t)l, .bo_handle_count = 2, .requirements = 1 };
	  assert(np_call(0x40, &fs, MESA_SUBMIT, NULL) == 0);
	  const struct nord2_gpu_submit_request *r = &K.last[(K.nlast - 1) & 15];
	  unsigned bit = 0; for (unsigned i = 0; i < r->nr_handles; i++) if (K.bo[r->handles[i]].size == 4 * 64 * 64) bit = (r->write_mask >> i) & 1;
	  assert(bit == 1); /* the surface is written by the job */
	  gc.handle = surf; }

	/* close: a held buffer is waited out, a busy release retried */
	K.release_busy = 1;
	gc.handle = many[5];
	assert(np_call(0x09, &gc, sizeof(gc), NULL) == 0);
	assert(np_call(0x09, &gc, sizeof(gc), NULL) == -1);              /* closed twice */
	unsigned live_before = K.live;
	for (unsigned i = 1; i < 100; i++) if (many[i] != pool && i != 5) { gc.handle = many[i]; assert(np_call(0x09, &gc, sizeof(gc), NULL) == 0); }
	assert(K.live < live_before);

	/* the pool: a closed buffer comes back for the next one of its size class, not before the GPU is done with it */
	np_pool_enable(1);
	unsigned allocs0 = K.next, rel_live0 = K.live;
	uint64_t pva1 = 0, pva2 = 0;
	uint32_t pb1 = mk(20000, 1, &pva1);                  /* class 32 KiB (size is rounded up to its class) */
	assert(K.next == allocs0 + 1 && K.live == rel_live0 + 1);
	uint64_t pmaddr = 0, pmsz = 0;
	assert(np_map(pb1, &pmaddr, &pmsz) == 0 && pmaddr);
	struct gem_close pgc = { .handle = pb1 };
	assert(np_call(0x09, &pgc, sizeof(pgc), NULL) == 0);
	assert(K.live == rel_live0 + 1);                      /* kept, not released */
	uint32_t pb2 = mk(30000, 1, &pva2);                  /* same class */
	assert(pb2 == pb1 && pva2 == pva1 && K.next == allocs0 + 1);
	uint64_t pmaddr2 = 0;
	assert(np_map(pb2, &pmaddr2, &pmsz) == 0 && pmaddr2 == pmaddr);   /* the mapping is still there */
	/* a buffer a job still lists is not handed out before that job is done */
	K.done_to = 0;
	uint32_t pl[2] = { pb2, pool };
	struct submit ps = { .jc = va_pool + 1024, .bo_handles = (uint64_t)(uintptr_t)pl, .bo_handle_count = 2, .requirements = 1 };
	assert(np_call(0x40, &ps, MESA_SUBMIT, NULL) == 0);
	pgc.handle = pb2; assert(np_call(0x09, &pgc, sizeof(pgc), NULL) == 0);
	uint64_t pva3 = 0; uint32_t pb3 = mk(25000, 1, &pva3);
	assert(pb3 == pb2 && *np_completed_ptr() >= ps.seqno);        /* handed out only after its job was waited out */
	/* executable and oversized buffers are never pooled */
	uint64_t xva = 0; uint32_t xb = mk(4096, 0, &xva); unsigned live_x = K.live;
	struct gem_close xgc = { .handle = xb }; assert(np_call(0x09, &xgc, sizeof(xgc), NULL) == 0 && K.live == live_x - 1);
	uint32_t hugebo = mk(32u << 20, 1, NULL); unsigned live_b = K.live;
	struct gem_close bgc = { .handle = hugebo }; assert(np_call(0x09, &bgc, sizeof(bgc), NULL) == 0 && K.live == live_b - 1);
	/* a refusal while the pool holds idle buffers: they go back to the kernel and the request is asked again */
	{
		uint32_t keep[300]; unsigned nk = 0;

		while (K.live < 256) { keep[nk++] = mk(20000, 1, NULL); assert(nk < 300); }
		for (unsigned i = 0; i < nk; i++) { struct gem_close kc = { .handle = keep[i] }; assert(np_call(0x09, &kc, sizeof(kc), NULL) == 0); }
		unsigned live_full = K.live;
		assert(live_full >= 256);                                    /* all of them pooled, none released */
		uint64_t bigva = 0; uint32_t bigbo = mk(1u << 20, 1, &bigva);       /* another class: the kernel refuses until the pool is returned */
		assert(bigbo && K.live < live_full);
		struct gem_close bc = { .handle = bigbo }; assert(np_call(0x09, &bc, sizeof(bc), NULL) == 0);
	}
	np_pool_enable(0);
	/* the handle table ends gracefully when the kernel's does */
	unsigned made = 0;
	for (;;) { struct create_bo q = { .size = 4096, .flags = 1 }; if (np_call(0x42, &q, sizeof(q), NULL)) { assert(errno == ENOMEM); break; } made++; assert(made < 400); }
	assert(made > 100);

	/* the refill thread tops the pool up in the background while the caller does nothing */
	K.live = 0; for (unsigned i = 0; i < 2000; i++) if (K.bo[i].live) { free(K.bo[i].mem); K.bo[i].live = 0; }
	np_pool_enable(1);
	unsigned live_before_refill = K.live;
	np_start_prime();
	{ struct timespec ts = { 6, 0 }; nanosleep(&ts, NULL); }
	assert(K.live > live_before_refill + 40);          /* many buffers were made ahead */
	np_pool_enable(0);                                   /* the thread stops making more */
	{ struct timespec ts = { 1, 0 }; nanosleep(&ts, NULL); }
	/* pre-warm: reads the wanted files (executables, dylibs, .car) of a tree, within the budget, and no others */
	{
		char dir[] = "/tmp/np_prewarm_XXXXXX";
		assert(mkdtemp(dir));
		char path[256]; FILE *f; static char blob[300 << 10];
		memset(blob, 7, sizeof(blob));
		snprintf(path, sizeof(path), "%s/Some.app", dir); assert(mkdir(path, 0755) == 0);
		snprintf(path, sizeof(path), "%s/Some.app/Some", dir); f = fopen(path, "wb"); fwrite(blob, 1, sizeof(blob), f); fclose(f); chmod(path, 0755);
		snprintf(path, sizeof(path), "%s/Some.app/Assets.car", dir); f = fopen(path, "wb"); fwrite(blob, 1, sizeof(blob), f); fclose(f);
		snprintf(path, sizeof(path), "%s/Some.app/Info.plist", dir); f = fopen(path, "wb"); fwrite(blob, 1, sizeof(blob), f); fclose(f);     /* not wanted */
		snprintf(path, sizeof(path), "%s/Some.app/tiny", dir); f = fopen(path, "wb"); fwrite(blob, 1, 100, f); fclose(f); chmod(path, 0755); /* too small */
		const char *dirs[1] = { dir }; uint64_t used = 0;
		assert(np_prewarm_dirs(dirs, 1, 100u << 20, &used) == 2 && used == 2 * sizeof(blob));
		assert(np_prewarm_dirs(dirs, 1, sizeof(blob), &used) == 1 && used == sizeof(blob));      /* the budget stops it */
		char cmd[300]; snprintf(cmd, sizeof(cmd), "rm -rf %s", dir); assert(system(cmd) == 0);
	}
	np_shutdown();
	for (unsigned i = 0; i < 2000; i++) if (K.bo[i].live) free(K.bo[i].mem);
	puts("nord2_pan_test: ok");
	return 0;
}
