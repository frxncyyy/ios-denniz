/* Panfrost/RedwoodMali requests (y2sios, MIT) answered over the Nord2GPU transport.
 *
 * What the kernel of y2sios (iokit/Kernel/RedwoodMali.cpp) does in a global GPU address space, this does for
 * one connection's address space with the Nord2 selectors: ALLOC/RELEASE for buffers, SUBMIT_JM (kind 1 for a
 * vertex/tiler chain, kind 3 for a fragment chain) and WAIT_SEQ.  Jobs of one context run in submission
 * order, so a syncobj is only a job number.
 *
 * A job names at most 32 buffers (N2C_JOB_BOS) and the head of its chain must lie in one of them.  A batch with
 * more is submitted with 32: the core then does full cache maintenance for the context, which is correct (the
 * GPU mappings are all there; only the CPU cache sync is coarser).
 * SPDX-License-Identifier: MIT */
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/resource.h>
#include <mach/mach_time.h>
#endif
#include <time.h>

#include "nord2_pan.h"
#include "nord2_gpu_transport.h"

#ifdef __APPLE__
#define NP_PROGNAME() getprogname()
#else
extern char *program_invocation_short_name;
#define NP_PROGNAME() program_invocation_short_name
#endif

#define KIO_BUSY        ((int)0xe00002d5u)

/* Linux's request numbers (DRM_COMMAND_BASE 0x40), as RedwoodMali.cpp has them */
enum {
	R_GEM_CLOSE = 0x09, R_SYNCOBJ_CREATE = 0xbf, R_SYNCOBJ_DESTROY = 0xc0, R_SYNCOBJ_WAIT = 0xc3,
	R_SYNCOBJ_RESET = 0xc4, R_SYNCOBJ_SIGNAL = 0xc5, R_SUBMIT = 0x40, R_WAIT_BO = 0x41, R_CREATE_BO = 0x42,
	R_MMAP_BO = 0x43, R_GET_PARAM = 0x44, R_GET_BO_OFFSET = 0x45, R_MADVISE = 0x48, R_SET_LABEL_BO = 0x49,
	R_SYNCOBJ_SET = 0x7b, R_SYNCOBJ_GET = 0x7c, R_COUNTS = 0x7d, R_SURFACE = 0x7e, R_SHOW = 0x7f,
};

struct submit { uint64_t jc, in_syncs; uint32_t in_sync_count, out_sync; uint64_t bo_handles;
		uint32_t bo_handle_count, requirements; };
struct wait_bo { uint32_t handle, pad; int64_t timeout_ns; };
struct create_bo { uint32_t size, flags, handle, pad; uint64_t offset; };
struct mmap_bo { uint32_t handle, flags; uint64_t offset; };
struct get_param { uint32_t param, pad; uint64_t value; };
struct get_bo_offset { uint32_t handle, pad; uint64_t offset; };
struct madvise { uint32_t handle, madv, retained; };
struct gem_close { uint32_t handle, pad; };
struct so_create { uint32_t handle, flags; };
struct so_destroy { uint32_t handle, pad; };
struct so_wait { uint64_t handles; int64_t timeout_nsec; uint32_t count_handles, flags, first_signaled, pad;
		 uint64_t deadline_nsec; };
struct so_array { uint64_t handles; uint32_t count_handles, pad; };
struct so_fence { uint32_t handle, pad; uint64_t fence; };
struct counts { uint64_t jobs, faults, resets, last; };
struct surface_req { uint32_t surface, handle; uint64_t size; uint32_t offset, rowBytes, width, height, pixelFormat, planes;
		     uint32_t planeOffset[2], planeRowBytes[2]; };
#define DESCRIBE            0xffffffffu

#define SO_CREATE_SIGNALED  1u
#define SO_WAIT_ALL         1u
#define REQ_FS              1u          /* PANFROST_JD_REQ_FS */
#define BO_MAX              (512ull << 20)
#define PATIENCE_MS         5000u
#define UNSIGNALED          (1ull << 62)    /* a fence no job will ever carry: reset, or created unsignalled */

#define NP_POOL_CLASSES 20u
#define NP_POOL_MAX_BOS 320u
#define NP_POOL_CAP     (200ull << 20)
static const uint32_t pool_class_kib[NP_POOL_CLASSES] = { 16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384 };
/* how many buffers of each class are kept ready; the refill thread tops a class up when it falls under half of this */
static const unsigned pool_target[NP_POOL_CLASSES] = { 48, 32, 16, 32, 16, 24, 12, 16, 8, 12, 6, 10, 4, 8, 4, 4, 2, 2, 2, 1 };

/* time spent in the requests of each kind (ns), seen from the caller: create, map, submit, wait, close, syncobj, other */
enum { TM_CREATE, TM_MAP, TM_SUBMIT, TM_WAIT, TM_CLOSE, TM_SYNC, TM_OTHER, TM_KINDS };
static uint64_t tm_n[TM_KINDS], tm_sum[TM_KINDS], tm_max[TM_KINDS];

struct bo {
	uint32_t kernel;                /* Nord2 handle, 0: free */
	uint64_t va, size, cpu;
	uint64_t last_seq;              /* newest job that listed it */
	uint8_t noexec, heap, imported, mapped;
	uint32_t surface;               /* IOSurface ID of an imported buffer */
	int import_fd;                  /* thumbrc: the DRM descriptor (one per Mesa screen) the import was made for */
	uint8_t pooled, cls;            /* closed by the client but kept (kernel buffer and CPU mapping alive) for the next CREATE_BO of its size class */
	uint32_t pool_next;
};

static struct {
	struct np_ops ops;
	int up;
	pthread_mutex_t lock;
	struct bo *bos;                 /* NP_BOS + 1 */
	uint64_t *syncobj;              /* fence per syncobj, ~0: free */
	volatile uint64_t completed;    /* newest job known terminal */
	uint64_t submitted;
	uint64_t last_fault_seq;
	struct np_stats stats;
	uint64_t pool_hits, pool_miss, pool_primed;
	struct nord2_gpu_status status;
	int have_status;
	uint64_t hwinfo[64];
	uint32_t js_features[16];
	int have_hwinfo;
	unsigned next_bo, next_syncobj;
	uint32_t pool_head[NP_POOL_CLASSES];
	unsigned pool_cnt[NP_POOL_CLASSES];
	uint64_t pool_bytes;
	unsigned pool_count;
	int pool_enabled;
	int no_gpuonly, no_wc;          /* the kernel refused GPU-only / write-combine buffers */
} D = { .lock = PTHREAD_MUTEX_INITIALIZER };

static uint64_t now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static void tm_note(unsigned k, uint64_t ns)
{
	uint64_t m = __atomic_load_n(&tm_max[k], __ATOMIC_RELAXED);

	__atomic_fetch_add(&tm_n[k], 1, __ATOMIC_RELAXED);
	__atomic_fetch_add(&tm_sum[k], ns, __ATOMIC_RELAXED);
	while (ns > m && !__atomic_compare_exchange_n(&tm_max[k], &m, ns, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
		;
}

static int np_fail(int e)
{
	errno = e;
	return -1;
}

static int kcall(unsigned sel, const uint64_t *in, unsigned ni, const void *bytes, size_t size,
		 uint64_t *out, unsigned *no, void *ob, size_t *os)
{
	return D.ops.call(D.ops.opaque, sel, in, ni, bytes, size, out, no, ob, os);
}

static int last_kr;

static int kerr(int kr)
{
	last_kr = kr;
	switch ((unsigned)kr) {
	case 0xe00002d5u: return EBUSY;
	case 0xe00002bdu: case 0xe00002beu: return ENOMEM;
	case 0xe00002c2u: return EINVAL;
	default: return EIO;
	}
}

int np_init(const struct np_ops *ops)
{
	if (!ops || !ops->call || !ops->map)
		return np_fail(EINVAL);
	pthread_mutex_lock(&D.lock);
	if (D.up) {
		pthread_mutex_unlock(&D.lock);
		return 0;
	}
	D.ops = *ops;
	D.bos = calloc(NP_BOS + 1, sizeof(struct bo));
	D.syncobj = malloc(NP_SYNCOBJS * sizeof(uint64_t));
	if (!D.bos || !D.syncobj) {
		free(D.bos);
		free(D.syncobj);
		D.bos = NULL;
		D.syncobj = NULL;
		pthread_mutex_unlock(&D.lock);
		return np_fail(ENOMEM);
	}
	for (unsigned i = 0; i < NP_SYNCOBJS; i++)
		D.syncobj[i] = ~0ull;
	/* The GPU powers up some seconds after the first client may open it: ask until it says it is ready */
	for (unsigned tries = 0; tries < 300; tries++) {
		size_t sz = sizeof(D.status);

		memset(&D.status, 0, sizeof(D.status));
		D.have_status = kcall(NORD2_GPU_SEL_STATUS, NULL, 0, NULL, 0, NULL, &(unsigned){0}, &D.status, &sz) == 0 &&
				sz >= 16;
		if (D.have_status && D.status.enabled && D.status.powered && D.status.arch_ok)
			break;
		struct timespec d = { 0, 100000000L };

		nanosleep(&d, NULL);
	}
	{
		struct nord2_gpu_hwinfo hw;
		size_t hs = sizeof(hw);

		memset(&hw, 0, sizeof(hw));
		if (kcall(NORD2_GPU_SEL_HWINFO, NULL, 0, NULL, 0, NULL, &(unsigned){0}, &hw, &hs) == 0 &&
		    hs >= sizeof(hw) && hw.version == 1) {          /* a kernel before 0134 refuses it: the zeros stand */
			D.hwinfo[13] = hw.thread_features;
			D.hwinfo[14] = hw.thread_max_threads;
			D.hwinfo[15] = hw.thread_max_workgroup_size;
			D.hwinfo[16] = hw.thread_max_barrier;
			D.hwinfo[17] = hw.coherency_features;
			for (unsigned i = 0; i < 4; i++)
				D.hwinfo[18 + i] = hw.texture_features[i];
			D.hwinfo[39] = hw.tls_alloc;
			D.hwinfo[40] = hw.afbc_features;
			D.have_hwinfo = 1;
			for (unsigned i = 0; i < 16; i++)
				D.js_features[i] = hw.js_features[i];
		}
	}
	D.pool_enabled = 1;
	D.up = 1;
	{
		const char *name = NP_PROGNAME();

		if (name && strcmp(name, "backboardd") == 0) {
			np_start_prime();
			np_start_prewarm();
			np_start_sampler();
		}
	}
	pthread_mutex_unlock(&D.lock);
	return 0;
}

int np_ready(void)
{
	return D.up;
}

void np_shutdown(void)
{
	pthread_mutex_lock(&D.lock);
	if (D.up) {
		free(D.bos);
		free(D.syncobj);
		D.bos = NULL;
		D.syncobj = NULL;
		D.up = 0;
	}
	pthread_mutex_unlock(&D.lock);
}

const volatile uint64_t *np_completed_ptr(void)
{
	return &D.completed;
}

void np_stats(struct np_stats *out)
{
	pthread_mutex_lock(&D.lock);
	*out = D.stats;
	pthread_mutex_unlock(&D.lock);
}

/* A diagnostic line into the kernel log (SEL_LOG: bounded text, no GPU work). */
void np_log(const char *text, size_t length)
{
	/* SEL_LOG takes up to 240 bytes a call: a longer record (a crash with its callers) goes in pieces */
	while (D.up && text && length) {
		char line[200];
		size_t n = length < 170 ? length : 170;
		unsigned no = 0;

		memcpy(line, "NORD2_RW ", 9);
		memcpy(line + 9, text, n);
		line[9 + n] = '\n';
		(void)kcall(NORD2_GPU_SEL_LOG, NULL, 0, line, 9 + n + 1, NULL, &no, NULL, NULL);
		text += n;
		length -= n;
	}
}

static void fr_report(void);

static void trace_call(unsigned number, size_t size, int r, int err)
{
	static unsigned traced, failed;
	static uint64_t last_stats;
	char b[420];
	uint64_t t = now_ns();

	if (t - last_stats >= 5000000000ull) {         /* every 5 s a process that calls: what it submitted */
		int m = snprintf(b, sizeof(b), "np stats pid=%d submits=%llu waits=%llu wait_calls=%llu creates=%llu closes=%llu full=%llu faults=%llu ext=%d",
				 getpid(), (unsigned long long)D.stats.submits, (unsigned long long)D.stats.waits,
				 (unsigned long long)D.stats.wait_calls, (unsigned long long)D.stats.creates,
				 (unsigned long long)D.stats.closes, (unsigned long long)D.stats.full_maint,
				 (unsigned long long)D.stats.faults, !(D.no_gpuonly || D.no_wc));
		last_stats = t;
		if (m > 0)
			np_log(b, (size_t)m);
		fr_report();
		m = snprintf(b, sizeof(b), "np time(n/avg_us/max_us) create=%llu/%llu/%llu map=%llu/%llu/%llu submit=%llu/%llu/%llu wait=%llu/%llu/%llu close=%llu/%llu/%llu sync=%llu/%llu/%llu pool hit=%llu miss=%llu primed=%llu held=%u/%lluKiB",
			     (unsigned long long)tm_n[TM_CREATE], (unsigned long long)(tm_n[TM_CREATE] ? tm_sum[TM_CREATE] / tm_n[TM_CREATE] / 1000 : 0), (unsigned long long)(tm_max[TM_CREATE] / 1000),
			     (unsigned long long)tm_n[TM_MAP], (unsigned long long)(tm_n[TM_MAP] ? tm_sum[TM_MAP] / tm_n[TM_MAP] / 1000 : 0), (unsigned long long)(tm_max[TM_MAP] / 1000),
			     (unsigned long long)tm_n[TM_SUBMIT], (unsigned long long)(tm_n[TM_SUBMIT] ? tm_sum[TM_SUBMIT] / tm_n[TM_SUBMIT] / 1000 : 0), (unsigned long long)(tm_max[TM_SUBMIT] / 1000),
			     (unsigned long long)tm_n[TM_WAIT], (unsigned long long)(tm_n[TM_WAIT] ? tm_sum[TM_WAIT] / tm_n[TM_WAIT] / 1000 : 0), (unsigned long long)(tm_max[TM_WAIT] / 1000),
			     (unsigned long long)tm_n[TM_CLOSE], (unsigned long long)(tm_n[TM_CLOSE] ? tm_sum[TM_CLOSE] / tm_n[TM_CLOSE] / 1000 : 0), (unsigned long long)(tm_max[TM_CLOSE] / 1000),
			     (unsigned long long)tm_n[TM_SYNC], (unsigned long long)(tm_n[TM_SYNC] ? tm_sum[TM_SYNC] / tm_n[TM_SYNC] / 1000 : 0), (unsigned long long)(tm_max[TM_SYNC] / 1000),
			     (unsigned long long)D.pool_hits, (unsigned long long)D.pool_miss, (unsigned long long)D.pool_primed, D.pool_count, (unsigned long long)(D.pool_bytes >> 10));
		if (m > 0)
			np_log(b, (size_t)(m < (int)sizeof(b) ? m : (int)sizeof(b) - 1));
		{                                                     /* where the buffers are: live private, imported surfaces, pooled, heaps */
			uint64_t nb[4] = { 0 }, by[4] = { 0 };

			for (uint32_t h = 1; h <= NP_BOS; h++) {
				const struct bo *x = &D.bos[h];
				unsigned k;

				if (!x->kernel)
					continue;
				k = x->pooled ? 2 : x->imported ? 1 : x->heap ? 3 : 0;
				nb[k]++;
				by[k] += x->size;
			}
			m = snprintf(b, sizeof(b), "np bo: private %llu/%llu MiB, imported %llu/%llu MiB, pooled %llu/%llu MiB, heaps %llu/%llu MiB; import failures %llu (lookup %llu)",
				     (unsigned long long)nb[0], (unsigned long long)(by[0] >> 20), (unsigned long long)nb[1], (unsigned long long)(by[1] >> 20),
				     (unsigned long long)nb[2], (unsigned long long)(by[2] >> 20), (unsigned long long)nb[3], (unsigned long long)(by[3] >> 20),
				     (unsigned long long)D.stats.surf_fail, (unsigned long long)D.stats.surf_fail_enoent);
			if (m > 0)
				np_log(b, (size_t)(m < (int)sizeof(b) ? m : (int)sizeof(b) - 1));
		}
	}

	if (traced < 40 || (r && failed < 80)) {
		int n = snprintf(b, sizeof(b), "np call %02x size=%zu -> %d errno=%d kr=%08x submitted=%llu done=%llu", number, size, r,
				 r ? err : 0, r ? (unsigned)last_kr : 0u, (unsigned long long)D.submitted, (unsigned long long)D.completed);
		traced++;
		failed += r != 0;
		if (n > 0)
			np_log(b, (size_t)n);
	}
}

/* ------------------------------------------------------------------ jobs */

static void note_done(uint64_t seq)
{
	if (seq > D.completed)
		D.completed = seq;
}

/* GNR1 (y2s63-gnr1, diagnostic): the kernel has always returned what a failed job died of (JS status, the
 * context's MMU FAULTSTATUS and FAULTADDRESS) in WAIT_SEQ's structure output, and nothing here read it: every
 * failure became "EIO" and, one level up, "did not finish within two seconds".  Now: the job's kind and chain
 * head (from a ring of the last submits), the JS status and the MMU fault decoded, and which buffer object the
 * address is in or nearest to (va, size, imported surface).  First 24 faults of a process. */
#define GNR1_RING 128u
static struct { uint64_t seq, jc; uint32_t kind, nbos, dropped; } gnr1_ring[GNR1_RING];
static unsigned gnr1_faults_said;

static void gnr1_note_submit(uint64_t seq, uint64_t jc, uint32_t kind, uint32_t nbos, uint32_t dropped)
{
	unsigned i = (unsigned)(seq % GNR1_RING);

	gnr1_ring[i].seq = seq; gnr1_ring[i].jc = jc; gnr1_ring[i].kind = kind;
	gnr1_ring[i].nbos = nbos; gnr1_ring[i].dropped = dropped;
}

static const char *gnr1_js_name(uint32_t s)
{
	switch (s & 0xff) {
	case 0x00: return "no exception"; case 0x02: return "interrupted"; case 0x03: return "stopped";
	case 0x04: return "terminated"; case 0x08: return "active"; case 0x40: return "job configuration fault";
	case 0x41: return "job power fault"; case 0x42: return "job read fault"; case 0x43: return "job write fault";
	case 0x44: return "job affinity fault"; case 0x48: return "job bus fault";
	case 0x50: return "instruction invalid PC"; case 0x51: return "instruction invalid encoding";
	case 0x52: return "instruction type mismatch"; case 0x53: return "instruction operand fault";
	case 0x54: return "instruction TLS fault"; case 0x55: return "instruction barrier fault";
	case 0x56: return "instruction alignment fault"; case 0x58: return "data invalid fault";
	case 0x59: return "tile range fault"; case 0x5a: return "address range fault"; case 0x60: return "out of memory";
	default:
		if ((s & 0xf8) == 0xc0) return "MMU translation fault";
		if ((s & 0xf8) == 0xc8) return "MMU permission fault";
		if ((s & 0xf8) == 0xd8) return "MMU access flag fault";
		return "other";
	}
}

static const char *gnr1_mmu_kind(uint32_t fs)
{
	static const char *k[8] = { "translation", "permission", "translation table bus", "access flag",
				    "address size", "memory attributes", "?6", "?7" };
	return fs ? k[(fs >> 3) & 7] : "none";
}

static const char *gnr1_mmu_access(uint32_t fs)
{
	static const char *a[4] = { "atomic", "execute", "read", "write" };
	return a[(fs >> 8) & 3];
}

/* the buffer object an address is in, else the nearest one below it (an overrun) and above it */
static void gnr1_where(uint64_t addr, char *out, size_t size)
{
	uint32_t in = 0, below = 0, above = 0;

	for (uint32_t h = 1; h <= NP_BOS; h++) {
		const struct bo *x = &D.bos[h];

		if (!x->kernel || !x->size)
			continue;
		if (addr >= x->va && addr < x->va + x->size) { in = h; break; }
		if (x->va + x->size <= addr && (!below || x->va + x->size > D.bos[below].va + D.bos[below].size))
			below = h;
		if (x->va > addr && (!above || x->va < D.bos[above].va))
			above = h;
	}
	if (in) {
		const struct bo *x = &D.bos[in];
		snprintf(out, size, "INSIDE bo %u (va 0x%llx size %llu, +%llu%s%s, surface %u)", in,
			 (unsigned long long)x->va, (unsigned long long)x->size, (unsigned long long)(addr - x->va),
			 x->imported ? ", imported" : "", x->noexec ? "" : ", executable", x->surface);
		return;
	}
	int n = snprintf(out, size, "in NO buffer object");
	if (below && n > 0 && (size_t)n < size) {
		const struct bo *x = &D.bos[below];
		n += snprintf(out + n, size - n, "; %llu bytes past the end of bo %u (va 0x%llx size %llu%s)",
			      (unsigned long long)(addr - (x->va + x->size)), below, (unsigned long long)x->va,
			      (unsigned long long)x->size, x->imported ? ", imported" : "");
	}
	if (above && n > 0 && (size_t)n < size) {
		const struct bo *x = &D.bos[above];
		snprintf(out + n, size - n, "; %llu bytes before bo %u (va 0x%llx size %llu)",
			 (unsigned long long)(x->va - addr), above, (unsigned long long)x->va, (unsigned long long)x->size);
	}
}

static void gnr1_say_fault(uint64_t waited, const uint64_t *out, unsigned n, const struct nord2_gpu_fault_info *fi)
{
	char b[512], where[224] = "";
	uint64_t id = ((uint64_t)fi->job_id_hi << 32) | fi->job_id_lo;
	uint64_t failed = id ? id : (n >= 3 ? out[2] : waited);
	unsigned r = (unsigned)(failed % GNR1_RING);
	int known = gnr1_ring[r].seq == failed;

	if (gnr1_faults_said >= 24)
		return;
	gnr1_faults_said++;
	if (fi->fault_address)
		gnr1_where(fi->fault_address, where, sizeof(where));
	int m = snprintf(b, sizeof(b),
			 "np GNR1 FAULT pid=%d waited seq %llu: job %llu (%s, chain 0x%llx, %u bos%s) state %u out0 %llu; "
			 "JS status 0x%x (%s); MMU faultstatus 0x%x (%s, %s access, source id 0x%x) address 0x%llx %s; "
			 "first incomplete task %u",
			 getpid(), (unsigned long long)waited, (unsigned long long)failed,
			 known ? (gnr1_ring[r].kind == 3 ? "fragment" : "vertex/tiler/compute") : "not in the ring",
			 known ? (unsigned long long)gnr1_ring[r].jc : 0ull, known ? gnr1_ring[r].nbos : 0,
			 known && gnr1_ring[r].dropped ? ", list FULL" : "", fi->state,
			 n >= 1 ? (unsigned long long)out[0] : 0ull, fi->status, gnr1_js_name(fi->status),
			 fi->fault_status, gnr1_mmu_kind(fi->fault_status), gnr1_mmu_access(fi->fault_status),
			 fi->fault_status >> 16, (unsigned long long)fi->fault_address, where, fi->first_incomplete_task);
	if (m > 0)
		np_log(b, (size_t)(m < (int)sizeof(b) ? m : (int)sizeof(b) - 1));
}

/* for air_metal.c's dispatch report: a Panfrost (KMS) handle's GPU range */
int nord2_gnr1_bo_info(uint32_t h, uint64_t *va, uint64_t *size);
__attribute__((visibility("default"))) int nord2_gnr1_bo_info(uint32_t h, uint64_t *va, uint64_t *size)
{
	if (!h || h > NP_BOS || !D.bos || !D.bos[h].kernel)
		return -1;
	*va = D.bos[h].va;
	*size = D.bos[h].size;
	return 0;
}

/* Wait until every job <= seq is terminal.  lock held.  0 done, ETIMEDOUT, EIO (failed job, reaped). */
static int wait_seq(uint64_t seq, uint64_t ms)
{
	uint64_t until = now_ns() + ms * 1000000ull;

	if (seq <= D.completed)
		return 0;
	if (seq > D.submitted) {                /* nothing will make it true: only the deadline ends the wait */
		struct timespec d = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };

		nanosleep(&d, NULL);
		return np_fail(ETIMEDOUT);
	}
	for (;;) {
		uint64_t left = until > now_ns() ? (until - now_ns() + 999999ull) / 1000000ull : 0;
		uint64_t in[2] = { seq, left > 1000 ? 1000 : left }, out[3] = { 0 };
		unsigned n = 3;
		struct nord2_gpu_fault_info fi;
		size_t fs = sizeof(fi);

		memset(&fi, 0, sizeof(fi));
		D.stats.wait_calls++;
		int e = kcall(NORD2_GPU_SEL_WAIT_SEQ, in, 2, NULL, 0, out, &n, &fi, &fs);

		if (e)
			return np_fail(kerr(e));
		if (n >= 1 && out[0] == 4) {
			note_done(seq);
			return 0;
		}
		if (n >= 1 && out[0] != 2) {
			gnr1_say_fault(seq, out, n, &fi);       /* GNR1 */
			note_done(seq);                 /* terminal: faulted, cancelled or reset, and reaped */
			D.stats.faults++;
			D.last_fault_seq = n >= 3 ? out[2] : seq;
			return np_fail(EIO);
		}
		if (left == 0)
			return np_fail(ETIMEDOUT);
	}
}

static uint32_t kernel_handle_of(uint32_t h)
{
	return h && h <= NP_BOS ? D.bos[h].kernel : 0;
}

static int find_head(const uint32_t *list, unsigned n, uint64_t jc)
{
	for (unsigned i = 0; i < n; i++) {
		struct bo *b = list[i] && list[i] <= NP_BOS ? &D.bos[list[i]] : NULL;

		if (b && b->kernel && jc >= b->va && jc + 8 <= b->va + b->size)
			return (int)i;
	}
	return -1;
}

/* Submit cadence.  A pause of 100 ms or more in the middle of an animation (a burst of at least six submits less than 40 ms apart) is what the
 * user feels as a stall; the sampler names what was running during it. */
static uint64_t gap_last_ns, gap_end_ns, gap_len_ns;
static unsigned gap_burst, gap_burst_before;

/* Frame bursts: the submits of one CoreAnimation frame come in a short burst; the interval between burst starts is the frame cadence (11.2 ms at
 * 90 Hz, twice that when a tick is skipped).  For skipped ticks: how long the burst before and after took (wall and thread CPU), and the idle time
 * in between -- slow CPU work, waiting for the GPU, or no work to do (the clients had nothing new). */
#define FR_GAP_NS 3000000ull
static volatile int fr_lock;
static uint64_t fr_first, fr_last, fr_cpu_first, fr_cpu_last, fr_prev_dur, fr_prev_cpu, fr_prev_idle;
static void *fr_tid;
static unsigned fr_n;
static uint32_t fr_int[14], fr_dur[6], fr_bursts, fr_skip, fr_skip_slow, fr_skip_idle, fr_skip_cpuwait, fr_nsub[5];
static uint64_t fr_skip_prev_dur, fr_skip_prev_cpu, fr_skip_next_dur, fr_skip_idle_sum;
static int fr_after_skip;

static uint64_t thread_cpu_ns(void)
{
	struct timespec ts;

	return clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) ? 0 : (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void fr_note(uint64_t t)
{
	while (__atomic_exchange_n(&fr_lock, 1, __ATOMIC_ACQUIRE))
		;
	if (fr_n && t >= fr_last && t - fr_last >= FR_GAP_NS) {              /* the previous burst is complete */
		uint64_t dur = fr_last - fr_first, idle = t - fr_last, interval = t - fr_first;
		uint64_t cpu = (fr_tid == (void *)pthread_self() && fr_cpu_last >= fr_cpu_first) ? fr_cpu_last - fr_cpu_first : 0;
		unsigned ib = interval < 6000000 ? 0 : interval < 8000000 ? 1 : interval < 9000000 ? 2 : interval < 10000000 ? 3 : interval < 11000000 ? 4 :
			      interval < 12000000 ? 5 : interval < 13000000 ? 6 : interval < 14000000 ? 7 : interval < 16000000 ? 8 : interval < 19000000 ? 9 :
			      interval < 24000000 ? 10 : interval < 34000000 ? 11 : interval < 67000000 ? 12 : 13;

		if (fr_after_skip) {
			fr_skip_next_dur += dur;
			fr_after_skip = 0;
		}
		fr_bursts++;
		fr_dur[dur < 2000000 ? 0 : dur < 4000000 ? 1 : dur < 6000000 ? 2 : dur < 8000000 ? 3 : dur < 12000000 ? 4 : 5]++;
		fr_nsub[fr_n < 4 ? 0 : fr_n < 12 ? 1 : fr_n < 24 ? 2 : fr_n < 40 ? 3 : 4]++;
		if (fr_prev_dur || fr_bursts > 1) {
			fr_int[ib]++;
			if (interval >= 18000000 && interval < 40000000) {         /* a skipped tick, not a pause between interactions */
				fr_skip++;
				fr_skip_prev_dur += fr_prev_dur;
				fr_skip_prev_cpu += fr_prev_cpu;
				fr_skip_idle_sum += idle;
				if (fr_prev_dur > 8000000)
					fr_skip_slow++;                              /* the burst before it was long */
				if (idle > 12000000)
					fr_skip_idle++;                              /* nothing was submitted for most of the gap */
				if (fr_prev_dur > 4000000 && fr_prev_cpu && fr_prev_cpu < fr_prev_dur / 2)
					fr_skip_cpuwait++;                           /* a long burst that was mostly not running on a CPU */
				fr_after_skip = 1;
			}
		}
		/* the burst just closed becomes "previous" for the next interval */
		fr_prev_dur = dur;
		fr_prev_cpu = cpu;
		fr_prev_idle = idle;
		fr_first = t;
		fr_tid = (void *)pthread_self();
		fr_cpu_first = thread_cpu_ns();
		fr_n = 0;
	} else if (!fr_n) {
		fr_first = t;
		fr_tid = (void *)pthread_self();
		fr_cpu_first = thread_cpu_ns();
	}
	fr_n++;
	fr_last = t;
	if (fr_tid == (void *)pthread_self())
		fr_cpu_last = thread_cpu_ns();
	__atomic_store_n(&fr_lock, 0, __ATOMIC_RELEASE);
}

static void fr_report(void)
{
	char b[420];
	int m;

	if (fr_bursts < 40)
		return;
	m = snprintf(b, sizeof(b), "np frames pid=%d bursts=%u interval_ms <6/8/9/10/11/12/13/14/16/19/24/34/67/more=%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u dur_ms <2/4/6/8/12/more=%u/%u/%u/%u/%u/%u submits<4/12/24/40/more=%u/%u/%u/%u/%u",
		     getpid(), fr_bursts, fr_int[0], fr_int[1], fr_int[2], fr_int[3], fr_int[4], fr_int[5], fr_int[6], fr_int[7], fr_int[8], fr_int[9], fr_int[10], fr_int[11], fr_int[12], fr_int[13],
		     fr_dur[0], fr_dur[1], fr_dur[2], fr_dur[3], fr_dur[4], fr_dur[5], fr_nsub[0], fr_nsub[1], fr_nsub[2], fr_nsub[3], fr_nsub[4]);
	if (m > 0)
		np_log(b, (size_t)(m < (int)sizeof(b) ? m : (int)sizeof(b) - 1));
	if (fr_skip) {
		m = snprintf(b, sizeof(b), "np frames skipped=%u (>=18 ms between burst starts): burst before avg %llu us (thread cpu %llu us), idle between avg %llu us, burst after avg %llu us; before>8ms %u, idle>12ms %u, long-but-off-cpu %u",
			     fr_skip, (unsigned long long)(fr_skip_prev_dur / fr_skip / 1000), (unsigned long long)(fr_skip_prev_cpu / fr_skip / 1000),
			     (unsigned long long)(fr_skip_idle_sum / fr_skip / 1000), (unsigned long long)(fr_skip_next_dur / fr_skip / 1000), fr_skip_slow, fr_skip_idle, fr_skip_cpuwait);
		if (m > 0)
			np_log(b, (size_t)(m < (int)sizeof(b) ? m : (int)sizeof(b) - 1));
	}
	memset(fr_int, 0, sizeof(fr_int));
	memset(fr_dur, 0, sizeof(fr_dur));
	memset(fr_nsub, 0, sizeof(fr_nsub));
	fr_bursts = fr_skip = fr_skip_slow = fr_skip_idle = fr_skip_cpuwait = 0;
	fr_skip_prev_dur = fr_skip_prev_cpu = fr_skip_next_dur = fr_skip_idle_sum = 0;
}

static void note_submit(void)
{
	uint64_t t = now_ns(), prev = __atomic_exchange_n(&gap_last_ns, t, __ATOMIC_RELAXED), d;

	fr_note(t);

	if (!prev || t < prev)
		return;
	d = t - prev;
	if (d < 40000000ull) {
		gap_burst++;
		return;
	}
	if (d >= 100000000ull && gap_burst >= 6) {
		gap_len_ns = d;
		gap_burst_before = gap_burst;
		__atomic_store_n(&gap_end_ns, t, __ATOMIC_RELEASE);
	}
	gap_burst = 0;
}

static int do_submit(struct submit *r, size_t size, uint64_t *seqno)
{
	const uint32_t *list = (const uint32_t *)(uintptr_t)r->bo_handles;
	unsigned count = r->bo_handle_count;
	struct nord2_gpu_submit_request q;
	int head;

	if (size < sizeof(struct submit) || !r->jc || count > 16384 || (count && !list))
		return np_fail(EINVAL);
	note_submit();
	memset(&q, 0, sizeof(q));
	q.version = NORD2_GPU_TRANSPORT_VERSION;
	q.kind = (r->requirements & REQ_FS) ? 3 : 1;
	q.heads[q.kind == 3 ? 1 : 0] = r->jc;
	head = find_head(list, count, r->jc);
	if (head < 0) {
		D.stats.no_head_bo++;
		return np_fail(EINVAL);
	}
	/* The BO that holds the chain's head first, then the rest in order until 32 are taken. */
	uint32_t pick[NP_KERNEL_BOS];
	unsigned np = 0;

	pick[np++] = list[head];
	for (unsigned i = 0; i < count && np < NP_KERNEL_BOS; i++) {
		uint32_t h = list[i];
		int dup = 0;

		if (!h || h > NP_BOS || !D.bos[h].kernel)
			continue;
		for (unsigned k = 0; k < np; k++)
			dup |= pick[k] == h;
		if (!dup)
			pick[np++] = h;
	}
	if (np == NP_KERNEL_BOS) {
		unsigned valid = 0;

		for (unsigned i = 0; i < count; i++)
			valid += list[i] && list[i] <= NP_BOS && D.bos[list[i]].kernel != 0;
		if (valid > np)
			D.stats.submit_bos_dropped += valid - np;
		D.stats.full_maint++;
	}
	for (unsigned i = 0; i < np; i++) {
		struct bo *b = &D.bos[pick[i]];

		q.handles[i] = b->kernel;
		/* The GPU may write any data buffer it was given; a job that writes none of it just costs the sync */
		if (b->noexec && !b->heap)
			q.write_mask |= 1u << i;
	}
	q.nr_handles = np;
	for (int tries = 0;; tries++) {
		uint64_t job = 0;
		unsigned n = 1;
		int e = kcall(NORD2_GPU_SEL_SUBMIT_JM, NULL, 0, &q, sizeof(q), &job, &n, NULL, NULL);

		if (e == KIO_BUSY && tries < 3) {
			if (wait_seq(D.submitted, PATIENCE_MS) && errno != EIO)
				return -1;
			continue;
		}
		if (e)
			return np_fail(kerr(e));
		if (n != 1 || !job)
			return np_fail(EIO);
		*seqno = job;
		D.submitted = job > D.submitted ? job : D.submitted;
		gnr1_note_submit(job, r->jc, q.kind, np, np == NP_KERNEL_BOS);   /* GNR1 */
		break;
	}
	for (unsigned i = 0; i < count; i++) {
		uint32_t h = list[i];

		if (h && h <= NP_BOS && D.bos[h].kernel)
			D.bos[h].last_seq = *seqno;
	}
	if (r->out_sync && r->out_sync <= NP_SYNCOBJS && D.syncobj[r->out_sync - 1] != ~0ull)
		D.syncobj[r->out_sync - 1] = *seqno;
	D.stats.submits++;
	return 0;
}

/* ------------------------------------------------------------------ buffers */

static int pool_class_of(uint64_t size)
{
	for (unsigned c = 0; c < NP_POOL_CLASSES; c++)
		if (size <= (uint64_t)pool_class_kib[c] << 10)
			return (int)c;
	return -1;
}

/* A buffer of the class that the GPU has finished with; the first one that is, else the oldest after a short wait. */
static uint32_t pool_take(unsigned cls)
{
	uint32_t *link = &D.pool_head[cls];

	for (; *link; link = &D.bos[*link].pool_next) {
		struct bo *b = &D.bos[*link];

		if (b->last_seq <= D.completed)
			goto take;
	}
	link = &D.pool_head[cls];
	if (*link && D.bos[*link].last_seq <= D.submitted && wait_seq(D.bos[*link].last_seq, 100) == 0)
		goto take;
	return 0;
take:;
	uint32_t h = *link;
	struct bo *b = &D.bos[h];

	*link = b->pool_next;
	b->pooled = 0;
	b->pool_next = 0;
	b->last_seq = 0;
	D.pool_count--;
	D.pool_cnt[b->cls]--;
	D.pool_bytes -= b->size;
	return h;
}

static void pool_push(uint32_t h)
{
	struct bo *b = &D.bos[h];

	b->pooled = 1;
	b->pool_next = D.pool_head[b->cls];
	D.pool_head[b->cls] = h;
	D.pool_count++;
	D.pool_cnt[b->cls]++;
	D.pool_bytes += b->size;
}

static pthread_cond_t refill_cv = PTHREAD_COND_INITIALIZER;
static int refill_running;

/* The kernel buffer of handle h goes back to the kernel (a context with unreaped jobs takes the handle and frees the buffer when idle: Busy). */
static int bo_release_kernel(uint32_t h)
{
	struct bo *b = &D.bos[h];
	uint64_t in[1] = { b->kernel };
	unsigned n = 0;
	int e;

	if (b->mapped && !b->imported && D.ops.unmap)
		D.ops.unmap(D.ops.opaque, b->kernel, b->cpu);
	e = kcall(NORD2_GPU_SEL_RELEASE, in, 1, NULL, 0, NULL, &n, NULL, NULL);
	if (e && e != KIO_BUSY)
		return e;
	memset(b, 0, sizeof(*b));
	return 0;
}

/* Every pooled buffer back to the kernel: the pool is only a convenience and must never be what makes an allocation fail against the budget. */
static unsigned pool_drain(void)
{
	unsigned n = 0;

	for (unsigned c = 0; c < NP_POOL_CLASSES; c++) {
		while (D.pool_head[c]) {
			uint32_t h = D.pool_head[c];
			struct bo *b = &D.bos[h];

			D.pool_head[c] = b->pool_next;
			b->pooled = 0;
			D.pool_count--;
			D.pool_cnt[c]--;
			D.pool_bytes -= b->size;
			if (bo_release_kernel(h) == 0)
				n++;
		}
	}
	return n;
}

static int new_bo_ex(uint64_t size, unsigned flags, int nopool, uint32_t *handle, uint64_t *va)
{
	uint32_t h = 0;
	int cls = -1;

	if (D.pool_enabled && (flags & NP_BO_NOEXEC) && !(flags & NP_BO_HEAP))
		cls = pool_class_of(size);
	if (cls >= 0 && !nopool) {
		h = pool_take((unsigned)cls);
		if (refill_running && D.pool_cnt[cls] * 2 < pool_target[cls])
			pthread_cond_signal(&refill_cv);
		if (h) {
			D.pool_hits++;
			*handle = h;
			*va = D.bos[h].va;
			D.stats.creates++;
			return 0;
		}
		D.pool_miss++;
	}
	if (cls >= 0)
		size = (uint64_t)pool_class_kib[cls] << 10;         /* a pool-sized buffer, so that it fits its class when it comes back */

	for (unsigned n = 0; n < NP_BOS; n++) {
		unsigned i = (D.next_bo + n) % NP_BOS + 1;

		if (!D.bos[i].kernel) {
			h = i;
			break;
		}
	}
	if (!h)
		return np_fail(ENOSPC);
	/* ALLOC flags: bit0 executable, bit1 GPU-only (a heap: the CPU never reads it), bit2 CPU write-combine (kernel 0135:
	 * no CPU cache maintenance for the buffer, as Linux's Panfrost maps its buffers).  A kernel without them refuses with
	 * BadArgument: the allocation is asked again with the flags it knows. */
	unsigned want = ((flags & NP_BO_NOEXEC) ? 0u : 1u);
	uint64_t out[3] = { 0 };
	unsigned n = 3;
	int e;

	if ((flags & NP_BO_HEAP) && !D.no_gpuonly)
		want |= 2u;
	else if (!(flags & NP_BO_HEAP) && !D.no_wc)
		want |= 4u;
	{
		uint64_t in[2] = { size, want };

		e = kcall(NORD2_GPU_SEL_ALLOC, in, 2, NULL, 0, out, &n, NULL, NULL);
		/* BadArgument is also a budget refusal: an extension is given up only when the plain allocation succeeds */
		if (e == (int)0xe00002c2u && (want & 6u)) {
			in[1] = want & 1u;
			n = 3;
			e = kcall(NORD2_GPU_SEL_ALLOC, in, 2, NULL, 0, out, &n, NULL, NULL);
			if (!e) {
				if (want & 2u)
					D.no_gpuonly = 1;
				if (want & 4u)
					D.no_wc = 1;
			}
		}
	}
	uint64_t in[2] = { size, want & 1u };
	if (e && kerr(e) != EIO && D.pool_count) {              /* refused with a pool full of idle buffers: give them back and ask again */
		unsigned drained = pool_drain();

		if (drained) {
			char b[96];
			int m = snprintf(b, sizeof(b), "np pool: allocation of %llu KiB refused, %u pooled buffers returned", (unsigned long long)(size >> 10), drained);

			if (m > 0)
				np_log(b, (size_t)m);
			if (D.submitted > D.completed)
				(void)wait_seq(D.submitted, PATIENCE_MS);
			n = 3;
			e = kcall(NORD2_GPU_SEL_ALLOC, in, 2, NULL, 0, out, &n, NULL, NULL);
		}
	}
	if (e && D.submitted > D.completed) {                   /* released buffers still count until reaped */
		(void)wait_seq(D.submitted, PATIENCE_MS);
		n = 3;
		e = kcall(NORD2_GPU_SEL_ALLOC, in, 2, NULL, 0, out, &n, NULL, NULL);
	}
	/* Anything but a refusal of the request (budget, handles, arguments) is "not ready yet": wait and ask again */
	for (unsigned tries = 0; e && kerr(e) == EIO && tries < 100; tries++) {
		struct timespec d = { 0, 100000000L };

		nanosleep(&d, NULL);
		n = 3;
		e = kcall(NORD2_GPU_SEL_ALLOC, in, 2, NULL, 0, out, &n, NULL, NULL);
	}
	if (e)
		return np_fail(kerr(e));
	if (n < 2 || !out[0] || !out[1])
		return np_fail(EIO);
	D.bos[h] = (struct bo){ .kernel = (uint32_t)out[0], .va = out[1], .size = n >= 3 && out[2] ? out[2] : size,
				.noexec = !!(flags & NP_BO_NOEXEC), .heap = !!(flags & NP_BO_HEAP), .cls = (uint8_t)(cls >= 0 ? cls : 0) };
	if (cls >= 0 && D.bos[h].size != ((uint64_t)pool_class_kib[cls] << 10))
		cls = -1;                                                 /* the kernel rounded it to something else: never pooled */
	D.bos[h].pooled = 0;
	if (cls < 0)
		D.bos[h].cls = 0xff;
	D.next_bo = h;
	*handle = h;
	*va = out[1];
	D.stats.creates++;
	return 0;
}

static int new_bo(uint64_t size, unsigned flags, uint32_t *handle, uint64_t *va)
{
	return new_bo_ex(size, flags, 0, handle, va);
}

/*
 * thumbrc (12 Nov 2026): ONE IMPORT PER SURFACE AND SCREEN. Mesa shares an imported buffer within one pipe_screen (pan_lookup_bo by
 * GEM handle, one GEM_CLOSE when its last user goes), and expects a second screen to get its own handle, as a DRM file
 * descriptor does. On iOS os_same_file_description() cannot tell two dup()s of the connection apart, so every rwa_device gets a
 * screen of its own, while this layer handed all of them the SAME handle for a surface: the first screen to let the surface go
 * closed the kernel buffer under the others, whose GPU addresses then fell on whatever buffer took that range next (run ph11: the
 * Photos grid's surfaces read back as vertex floats, BGRA texels, zeros). The descriptor of the import (drmPrimeFDToHandle, through
 * np_set_import_fd) is now part of the key: a screen gets its own handle and kernel buffer, and its GEM_CLOSE closes only that.
 * Logged: the first 48 imports and closes of imported buffers ('np import ...', 'np close ...').
 */
static __thread int np_import_fd = -1;
void np_set_import_fd(int fd);
void np_set_import_fd(int fd) { np_import_fd = fd; }

static void import_note(const char *what, uint32_t surface, int fd, uint32_t h, uint64_t va, uint64_t size)
{
	static unsigned said;
	char b[200];
	int m;

	if (said >= 48)
		return;
	said++;
	m = snprintf(b, sizeof(b), "np %s: IOSurface %u fd %d -> handle %u va 0x%llx size %llu", what, surface, fd, h,
		     (unsigned long long)va, (unsigned long long)size);
	if (m > 0)
		np_log(b, (size_t)m);
}

static int do_close(uint32_t h)
{
	struct bo *b;

	if (!h || h > NP_BOS || !D.bos[h].kernel)
		return np_fail(EINVAL);
	b = &D.bos[h];
	if (b->pooled)
		return np_fail(EINVAL);                                   /* closed twice */
	if (D.pool_enabled && b->cls != 0xff && !b->imported && !b->heap && b->noexec && D.pool_count < NP_POOL_MAX_BOS &&
	    D.pool_bytes + b->size <= NP_POOL_CAP) {
		pool_push(h);                                                 /* the kernel buffer and its CPU mapping stay for the next one of this size */
		D.stats.closes++;
		return 0;
	}
	if (b->mapped && !b->imported && D.ops.unmap)
		D.ops.unmap(D.ops.opaque, b->kernel, b->cpu);
	if (b->imported)
		import_note("close", b->surface, b->import_fd, h, b->va, b->size);
	uint64_t in[1] = { b->kernel };
	unsigned n = 0;
	int e = kcall(NORD2_GPU_SEL_RELEASE, in, 1, NULL, 0, NULL, &n, NULL, NULL);

	/* A context with unreaped jobs: the kernel takes the handle and frees the buffer when it is idle (Busy) */
	if (e && e != KIO_BUSY)
		return np_fail(kerr(e));
	memset(b, 0, sizeof(*b));
	D.stats.closes++;
	return 0;
}

static int np_map_impl(uint32_t handle, uint64_t *address, uint64_t *size)
{
	int r = -1;

	pthread_mutex_lock(&D.lock);
	if (D.up && handle && handle <= NP_BOS && D.bos[handle].kernel) {
		struct bo *b = &D.bos[handle];

		if (!b->mapped) {
			uint64_t a = 0, s = 0;

			if (D.ops.map(D.ops.opaque, b->kernel, &a, &s) == 0 && a) {
				b->cpu = a;
				b->mapped = 1;
			}
		}
		if (b->mapped) {
			*address = b->cpu;
			*size = b->size;
			r = 0;
		} else {
			errno = EIO;
		}
	} else {
		errno = EINVAL;
	}
	pthread_mutex_unlock(&D.lock);
	return r;
}

int np_map(uint32_t handle, uint64_t *address, uint64_t *size)
{
	uint64_t t0 = now_ns();
	int r = np_map_impl(handle, address, size);

	tm_note(TM_MAP, now_ns() - t0);
	return r;
}

int np_unmap(uint32_t handle, uint64_t address)
{
	(void)handle;
	(void)address;
	return 0;               /* a mapping lives until the buffer is closed */
}

/* An IOSurface's pixels as a buffer: the same surface is the same buffer (for one screen: thumbrc). */
/* the first 40 failed imports of a process, with the reason: a failed import is a missing texture */
static void surf_note(uint32_t surface, const char *why, int err)
{
	static unsigned said;
	char b[160];
	int m;

	if (said >= 40)
		return;
	said++;
	m = snprintf(b, sizeof(b), "np surface: import of IOSurface %u failed: %s (errno %d)", surface, why, err);
	if (m > 0)
		np_log(b, (size_t)m);
}

static int do_surface(struct surface_req *q, size_t size)
{
	struct np_surface_info in;

	if (size < offsetof(struct surface_req, planeOffset) || !D.ops.surface)
		return np_fail(size < offsetof(struct surface_req, planeOffset) ? EINVAL : ENOTSUP);
	memset(&in, 0, sizeof(in));
	if (D.ops.surface(D.ops.opaque, q->surface, &in) || !in.size || !in.cpu) {
		D.stats.surf_fail++;
		D.stats.surf_fail_enoent++;
		surf_note(q->surface, "the surface cannot be looked up", ENOENT);
		return np_fail(ENOENT);
	}
	q->size = in.size;
	q->offset = in.offset;
	q->rowBytes = in.rowBytes;
	q->width = in.width;
	q->height = in.height;
	q->pixelFormat = in.pixelFormat;
	q->planes = in.planes;
	if (size >= sizeof(*q))
		for (unsigned i = 0; i < 2; i++) {
			q->planeOffset[i] = in.planeOffset[i];
			q->planeRowBytes[i] = in.planeRowBytes[i];
		}
	if (q->handle == DESCRIBE) {
		q->handle = 0;
		return 0;
	}
	for (uint32_t h = 1; h <= NP_BOS; h++)
		if (D.bos[h].kernel && D.bos[h].imported && D.bos[h].surface == q->surface && D.bos[h].import_fd == np_import_fd) {
			q->handle = h;
			return 0;
		}
	uint32_t h = 0;

	for (unsigned n = 0; n < NP_BOS; n++) {
		unsigned i = (D.next_bo + n) % NP_BOS + 1;

		if (!D.bos[i].kernel) {
			h = i;
			break;
		}
	}
	if (!h)
		return np_fail(ENOSPC);
	uint64_t args[4] = { q->surface, 0, in.size, 0 }, reply[3] = { 0 };
	unsigned n = 3;
	int e = kcall(NORD2_GPU_SEL_IMPORT_SURFACE, args, 4, NULL, 0, reply, &n, NULL, NULL);

	if (e) {
		D.stats.surf_fail++;
		surf_note(q->surface, "the kernel refused the import", kerr(e));
		return np_fail(kerr(e));
	}
	if (n != 3 || !reply[0] || !reply[1] || (reply[1] & 4095) || reply[2] < in.size) {
		D.stats.surf_fail++;
		surf_note(q->surface, "the kernel's answer is not usable", EIO);
		return np_fail(EIO);
	}
	D.bos[h] = (struct bo){ .kernel = (uint32_t)reply[0], .va = reply[1], .size = in.size, .cpu = in.cpu,
				.noexec = 1, .imported = 1, .mapped = 1, .surface = q->surface, .import_fd = np_import_fd };
	D.next_bo = h;
	q->handle = h;
	import_note("import", q->surface, np_import_fd, h, reply[1], in.size);
	return 0;
}

/* ------------------------------------------------------------------ parameters */

/* The kernel's identity block (selector NP_SEL_HWINFO) when it has one, else what STATUS says and the
 * Mali-G77 constants the register reads give on every G77. */
static int param(uint32_t which, uint64_t *v)
{
	const struct nord2_gpu_status *s = &D.status;

	switch (which) {
	case 0: *v = (s->gpu_id >> 16) ? s->gpu_id >> 16 : 0x9091; break;                 /* GPU_PROD_ID */
	case 1: *v = s->gpu_id & 0xffff; break;                                             /* GPU_REVISION */
	case 2: *v = (uint64_t)s->shader_present_lo | ((uint64_t)s->shader_present_hi << 32); break;
	case 3: *v = s->tiler_present_lo; break;
	case 4: *v = 1; break;                                                              /* L2_PRESENT */
	case 5: *v = 0; break;                                                              /* STACK_PRESENT */
	case 6: *v = 0xff; break;                                                           /* AS_PRESENT */
	case 7: *v = s->js_present; break;
	case 8: *v = s->l2_features; break;
	case 9: *v = s->core_features; break;
	case 10: *v = s->tiler_features; break;
	case 11: *v = s->mem_features; break;
	case 12: *v = s->mmu_features; break;
	case 38: *v = 1; break;                                                             /* NR_CORE_GROUPS */
	case 42: *v = 13000000; break;                                                      /* timestamp frequency */
	case 43: *v = 1u << 1; break;                                                       /* priorities: medium */
	case 44: *v = 31; break;                                                            /* coherency: none */
	case 22 ... 37:                                                                     /* JS_FEATURES0..15 */
		*v = D.js_features[which - 22];
		break;
	default:
		if (D.have_hwinfo && which < 64) {
			*v = D.hwinfo[which];
			return 0;
		}
		return -1;
	}
	return 0;
}

/* ------------------------------------------------------------------ syncobj */

static int so_ok(uint32_t h)
{
	return h && h <= NP_SYNCOBJS && D.syncobj[h - 1] != ~0ull;
}

static int so_wait(struct so_wait *w)
{
	const uint32_t *hs = (const uint32_t *)(uintptr_t)w->handles;
	uint64_t need = 0;
	int all = !!(w->flags & SO_WAIT_ALL);
	uint64_t deadline_ms;

	if (!w->count_handles || !hs)
		return np_fail(EINVAL);
	for (unsigned i = 0; i < w->count_handles; i++)
		if (!so_ok(hs[i]))
			return np_fail(ENOENT);
	if (w->timeout_nsec <= (int64_t)now_ns()) {
		deadline_ms = 0;
	} else {
		uint64_t d = (uint64_t)w->timeout_nsec - now_ns();

		deadline_ms = d / 1000000ull > PATIENCE_MS ? PATIENCE_MS : (d + 999999ull) / 1000000ull;
	}
	for (unsigned i = 0; i < w->count_handles; i++) {
		uint64_t f = D.syncobj[hs[i] - 1];

		if (f == 0 || f <= D.completed) {
			if (!all) {
				w->first_signaled = i;
				return 0;
			}
			continue;
		}
		if (!all) {
			need = need && need < f ? need : f;
			continue;
		}
		if (wait_seq(f, deadline_ms))
			return -1;
	}
	if (all)
		return 0;
	if (!need)
		return 0;
	if (wait_seq(need, deadline_ms))
		return -1;
	for (unsigned i = 0; i < w->count_handles; i++) {
		uint64_t f = D.syncobj[hs[i] - 1];

		if (f == 0 || f <= D.completed) {
			w->first_signaled = i;
			break;
		}
	}
	return 0;
}

/* ------------------------------------------------------------------ dispatch */

static int np_call_impl(unsigned number, void *data, size_t size, size_t *returned)
{
	int r = 0;

	if (!D.up)
		return np_fail(ENODEV);
	if (returned)
		*returned = size;
	pthread_mutex_lock(&D.lock);
	{
		static unsigned entered;

		if (entered < 25) {             /* a call that never returns is only visible from its entry */
			char b[64];
			int n = snprintf(b, sizeof(b), "np enter %02x size=%zu", number, size);

			entered++;
			np_log(b, (size_t)n);
		}
	}
	switch (number) {
	case R_CREATE_BO: {
		struct create_bo *q = data;

		if (size < sizeof(*q) || !q->size || q->size > BO_MAX) {
			r = np_fail(EINVAL);
			break;
		}
		uint64_t va = 0;
		uint32_t h = 0;

		r = new_bo(q->size, q->flags, &h, &va);
		if (!r) {
			q->handle = h;
			q->offset = va;
		}
		break;
	}
	case R_MMAP_BO: {
		struct mmap_bo *q = data;

		if (size < sizeof(*q) || !kernel_handle_of(q->handle))
			r = np_fail(ENOENT);
		else
			q->offset = (uint64_t)q->handle << 12;     /* a cookie: the mapping is taken by handle (np_map) */
		break;
	}
	case R_GET_BO_OFFSET: {
		struct get_bo_offset *q = data;

		if (size < sizeof(*q) || !kernel_handle_of(q->handle))
			r = np_fail(ENOENT);
		else
			q->offset = D.bos[q->handle].va;
		break;
	}
	case R_WAIT_BO: {
		struct wait_bo *q = data;

		if (size < sizeof(*q) || !kernel_handle_of(q->handle)) {
			r = np_fail(ENOENT);
			break;
		}
		uint64_t seq = D.bos[q->handle].last_seq;

		D.stats.waits++;
		if (seq <= D.completed)
			break;
		uint64_t now = now_ns();

		if ((uint64_t)q->timeout_ns <= now) {
			r = np_fail(ETIMEDOUT);
			break;
		}
		uint64_t d = ((uint64_t)q->timeout_ns - now) / 1000000ull;

		r = wait_seq(seq, d > PATIENCE_MS ? PATIENCE_MS : d + 1);
		break;
	}
	case R_GET_PARAM: {
		struct get_param *q = data;

		if (size < sizeof(*q) || param(q->param, &q->value))
			r = np_fail(EINVAL);
		break;
	}
	case R_SUBMIT: {
		uint64_t seq = 0;

		r = do_submit(data, size, &seq);
		if (!r && size >= sizeof(struct submit) + 8)        /* the libdrm stand-in's extended request */
			*(uint64_t *)((char *)data + sizeof(struct submit)) = seq;
		break;
	}
	case R_MADVISE: {
		struct madvise *q = data;

		if (size >= sizeof(*q))
			q->retained = 1;
		break;
	}
	case R_SET_LABEL_BO:
		break;
	case R_GEM_CLOSE: {
		struct gem_close *q = data;

		r = size >= sizeof(*q) ? do_close(q->handle) : np_fail(EINVAL);
		break;
	}
	case R_SYNCOBJ_CREATE: {
		struct so_create *q = data;

		r = np_fail(ENOSPC);
		for (unsigned n = 0; size >= sizeof(*q) && n < NP_SYNCOBJS; n++) {
			unsigned i = (D.next_syncobj + n) % NP_SYNCOBJS;

			if (D.syncobj[i] == ~0ull) {
				D.syncobj[i] = (q->flags & SO_CREATE_SIGNALED) ? 0 : UNSIGNALED;
				D.next_syncobj = i + 1;
				q->handle = i + 1;
				r = 0;
				break;
			}
		}
		break;
	}
	case R_SYNCOBJ_DESTROY: {
		struct so_destroy *q = data;

		if (size < sizeof(*q) || !so_ok(q->handle))
			r = np_fail(ENOENT);
		else
			D.syncobj[q->handle - 1] = ~0ull;
		break;
	}
	case R_SYNCOBJ_WAIT:
		r = size >= sizeof(struct so_wait) ? so_wait(data) : np_fail(EINVAL);
		break;
	case R_SYNCOBJ_RESET:
	case R_SYNCOBJ_SIGNAL: {
		struct so_array *q = data;
		const uint32_t *hs;

		if (size < sizeof(*q) || !q->handles) {
			r = np_fail(EINVAL);
			break;
		}
		hs = (const uint32_t *)(uintptr_t)q->handles;
		for (unsigned i = 0; i < q->count_handles; i++)
			if (!so_ok(hs[i]))
				r = np_fail(ENOENT);
		for (unsigned i = 0; !r && i < q->count_handles; i++)
			D.syncobj[hs[i] - 1] = number == R_SYNCOBJ_SIGNAL ? 0 : UNSIGNALED;
		break;
	}
	case R_SYNCOBJ_SET: {
		struct so_fence *q = data;

		if (size < sizeof(*q) || !so_ok(q->handle))
			r = np_fail(ENOENT);
		else
			D.syncobj[q->handle - 1] = q->fence;
		break;
	}
	case R_SYNCOBJ_GET: {
		struct so_fence *q = data;

		if (size < sizeof(*q) || !so_ok(q->handle))
			r = np_fail(ENOENT);
		else
			q->fence = D.syncobj[q->handle - 1];
		break;
	}
	case R_SURFACE:
		r = do_surface(data, size);
		break;
	case R_SHOW:
		break;
	case R_COUNTS: {
		struct counts *q = data;

		if (size < sizeof(*q)) {
			r = np_fail(EINVAL);
			break;
		}
		q->jobs = D.completed;
		q->faults = D.stats.faults;
		q->resets = 0;
		q->last = D.submitted;
		break;
	}
	default:
		r = np_fail(ENOTSUP);
		break;
	}
	if (D.up)
		trace_call(number, size, r, errno);
	pthread_mutex_unlock(&D.lock);
	return r;
}

static unsigned tm_kind(unsigned number)
{
	switch (number) {
	case R_CREATE_BO: return TM_CREATE;
	case R_SUBMIT: return TM_SUBMIT;
	case R_WAIT_BO: return TM_WAIT;
	case R_GEM_CLOSE: return TM_CLOSE;
	case R_SYNCOBJ_WAIT: case R_SYNCOBJ_CREATE: case R_SYNCOBJ_DESTROY: case R_SYNCOBJ_RESET: case R_SYNCOBJ_SIGNAL: case R_SYNCOBJ_SET: case R_SYNCOBJ_GET:
		return TM_SYNC;
	default: return TM_OTHER;
	}
}

int np_call(unsigned number, void *data, size_t size, size_t *returned)
{
	uint64_t t0 = now_ns();
	int r = np_call_impl(number, data, size, returned);

	tm_note(tm_kind(number), now_ns() - t0);
	return r;
}

void np_pool_enable(int on)
{
	pthread_mutex_lock(&D.lock);
	D.pool_enabled = on;
	pthread_mutex_unlock(&D.lock);
}

/* ------------------------------------------------------------------ pool pre-fill */

/* One pool buffer of class c, made with the shim's lock released during the kernel call (a few ms; the renderer must not wait for it).
 * Returns 1 if one was added. */
static int refill_one(unsigned c)
{
	uint64_t size = (uint64_t)pool_class_kib[c] << 10, out[3] = { 0 };
	uint32_t h = 0;
	unsigned n = 3, want;
	int e;

	pthread_mutex_lock(&D.lock);
	if (!D.up || !D.pool_enabled || D.pool_count >= NP_POOL_MAX_BOS || D.pool_bytes + size > NP_POOL_CAP) {
		pthread_mutex_unlock(&D.lock);
		return 0;
	}
	for (unsigned k = 0; k < NP_BOS; k++) {
		unsigned i = (D.next_bo + k) % NP_BOS + 1;

		if (!D.bos[i].kernel) {
			h = i;
			break;
		}
	}
	if (!h) {
		pthread_mutex_unlock(&D.lock);
		return 0;
	}
	D.bos[h].kernel = 0xffffffffu;                            /* reserved while the kernel works */
	want = D.no_wc ? 0u : 4u;
	pthread_mutex_unlock(&D.lock);
	{
		uint64_t in[2] = { size, want };

		e = kcall(NORD2_GPU_SEL_ALLOC, in, 2, NULL, 0, out, &n, NULL, NULL);
	}
	pthread_mutex_lock(&D.lock);
	if (e || n < 2 || !out[0] || !out[1] || !D.up) {
		memset(&D.bos[h], 0, sizeof(D.bos[h]));
		pthread_mutex_unlock(&D.lock);
		return 0;
	}
	D.bos[h] = (struct bo){ .kernel = (uint32_t)out[0], .va = out[1], .size = n >= 3 && out[2] ? out[2] : size, .noexec = 1, .cls = (uint8_t)c };
	if (D.bos[h].size == size && D.pool_count < NP_POOL_MAX_BOS) {
		pool_push(h);
		D.pool_primed++;
		D.stats.creates++;
		pthread_mutex_unlock(&D.lock);
		return 1;
	}
	{                                                       /* an unexpected size: give it back */
		uint64_t in[1] = { D.bos[h].kernel };
		unsigned z = 0;

		(void)kcall(NORD2_GPU_SEL_RELEASE, in, 1, NULL, 0, NULL, &z, NULL, NULL);
		memset(&D.bos[h], 0, sizeof(D.bos[h]));
	}
	pthread_mutex_unlock(&D.lock);
	return 0;
}

/* The renderer (backboardd) idles for tens of seconds after the GPU is ready and before the first touch: fill the pool then, and keep
 * it topped up when an animation takes buffers faster than they come back. */
static void *refill_thread(void *arg)
{
	struct timespec d = { 3, 0 };

	(void)arg;
	nanosleep(&d, NULL);
	refill_running = 1;
	uint64_t last_submits = ~0ull, quiet_since = 0;

	for (;;) {
		int did = 0, quiet;
		uint64_t nowt = now_ns();

		/* Big buffers take tens of ms to make and map: only while the renderer has not submitted for a third of a second */
		pthread_mutex_lock(&D.lock);
		if (D.stats.submits != last_submits) {
			last_submits = D.stats.submits;
			quiet_since = nowt;
		}
		pthread_mutex_unlock(&D.lock);
		quiet = nowt - quiet_since >= 300000000ull;

		for (unsigned c = 0; c < NP_POOL_CLASSES; c++) {
			unsigned have;

			pthread_mutex_lock(&D.lock);
			have = D.pool_cnt[c];
			if (!D.up) {
				pthread_mutex_unlock(&D.lock);
				return NULL;
			}
			pthread_mutex_unlock(&D.lock);
			if (have < pool_target[c] && (pool_class_kib[c] < 2048 || quiet) && refill_one(c)) {
				struct timespec w = { 0, 2000000L };

				did = 1;
				nanosleep(&w, NULL);
			}
		}
		if (!did) {
			struct timespec until;

			clock_gettime(CLOCK_REALTIME, &until);
			until.tv_nsec += 250000000L;
			if (until.tv_nsec >= 1000000000L) {
				until.tv_sec++;
				until.tv_nsec -= 1000000000L;
			}
			pthread_mutex_lock(&D.lock);
			pthread_cond_timedwait(&refill_cv, &D.lock, &until);
			pthread_mutex_unlock(&D.lock);
		}
	}
	return NULL;
}

void np_start_prime(void)
{
	pthread_t t;
	pthread_attr_t a;

	if (pthread_attr_init(&a) != 0)
		return;
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	(void)pthread_create(&t, &a, refill_thread, NULL);
	pthread_attr_destroy(&a);
}

/* ------------------------------------------------------------------ file cache pre-warm */

/* The first launch of an app, the first Control Center, the first Settings: every page of code or data touched for the first time is a
 * synchronous read from storage (hundreds a second, measured with the paging counters), so those first interactions stall for a
 * second at a time.  The renderer reads the files they use once, in big sequential reads, while the machine is idle after boot; the
 * pages then sit in the file cache.  Bounded, and it backs off as soon as the CPUs or the GPU get busy. */
#define PREWARM_BUDGET  (400ull << 20)
#define PREWARM_FILE_MAX (96ull << 20)
#define PREWARM_FILE_MIN (32ull << 10)

static int system_quiet(void)
{
#ifdef __APPLE__
	host_cpu_load_info_data_t a, b;
	mach_msg_type_number_t n = HOST_CPU_LOAD_INFO_COUNT;
	struct timespec d = { 0, 250000000L };

	if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&a, &n) != KERN_SUCCESS)
		return 1;
	nanosleep(&d, NULL);
	n = HOST_CPU_LOAD_INFO_COUNT;
	if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, (host_info_t)&b, &n) != KERN_SUCCESS)
		return 1;
	uint64_t busy = (uint64_t)(b.cpu_ticks[CPU_STATE_USER] - a.cpu_ticks[CPU_STATE_USER]) + (b.cpu_ticks[CPU_STATE_SYSTEM] - a.cpu_ticks[CPU_STATE_SYSTEM]);
	uint64_t idle = (uint64_t)(b.cpu_ticks[CPU_STATE_IDLE] - a.cpu_ticks[CPU_STATE_IDLE]);

	return idle * 10 >= (busy + idle) * 6 || (busy + idle) == 0;           /* >= 60 % idle */
#else
	return 1;
#endif
}

static int prewarm_wanted(const char *name, mode_t mode, uint64_t size)
{
	size_t len = strlen(name);

	if (size < PREWARM_FILE_MIN || size > PREWARM_FILE_MAX)
		return 0;
	if (mode & 0111)
		return 1;
	if (len > 6 && (!strcmp(name + len - 6, ".dylib") || !strcmp(name + len - 4, ".car")))
		return 1;
	return strchr(name, '.') == NULL;                                      /* a bundle's binary has no extension */
}

static int name_has(const char *name, const char *const *keys)
{
	if (!keys)
		return 1;
	for (; *keys; keys++)
		if (strstr(name, *keys))
			return 1;
	return 0;
}

/* Reads the wanted files below `path` (to `depth` levels); *budget counts down the bytes still allowed.  Returns the files read. */
static unsigned prewarm_tree(const char *path, int depth, const char *const *only, uint64_t *budget, char *buf, size_t bufsz)
{
	DIR *dir = opendir(path);
	struct dirent *e;
	unsigned files = 0;

	if (!dir)
		return 0;
	while ((e = readdir(dir)) != NULL && *budget > 0) {
		char child[1024];
		struct stat st;

		if (e->d_name[0] == '.')
			continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) >= (int)sizeof(child) || lstat(child, &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth > 0 && (depth > 2 || name_has(e->d_name, only)))
				files += prewarm_tree(child, depth - 1, NULL, budget, buf, bufsz);
		} else if (S_ISREG(st.st_mode) && prewarm_wanted(e->d_name, st.st_mode, (uint64_t)st.st_size) && (uint64_t)st.st_size <= *budget) {
			int fd = open(child, O_RDONLY);
			uint64_t got = 0;

			if (fd < 0)
				continue;
			for (ssize_t r; (r = read(fd, buf, bufsz)) > 0; ) {
				got += (uint64_t)r;
				if ((got & ((8ull << 20) - 1)) < bufsz) {           /* a breath every 8 MiB so that a launch is not queued behind us */
					struct timespec d = { 0, 3000000L };

					nanosleep(&d, NULL);
				}
			}
			close(fd);
			*budget -= got < *budget ? got : *budget;
			files++;
		}
	}
	closedir(dir);
	return files;
}

/* For the tests: the same walk over a list of directories. */
unsigned np_prewarm_dirs(const char *const *dirs, unsigned n, uint64_t budget, uint64_t *used)
{
	char *buf = malloc(512 << 10);
	unsigned files = 0;
	uint64_t left = budget;

	if (!buf)
		return 0;
	for (unsigned i = 0; i < n && left > 0; i++)
		files += prewarm_tree(dirs[i], 3, NULL, &left, buf, 512 << 10);
	free(buf);
	if (used)
		*used = budget - left;
	return files;
}

static void *prewarm_thread(void *arg)
{
	static const char *const pref_frameworks[] = { "Preferences", "ControlCenter", "CCUI", "Settings", NULL };
	struct { const char *path; int depth; const char *const *only; } plan[] = {
		{ "/System/Library/ControlCenter/Bundles", 3, NULL },
		{ "/System/Library/PreferenceBundles", 3, NULL },
		{ "/Applications", 4, NULL },
		{ "/System/Library/PrivateFrameworks", 2, pref_frameworks },
	};
	char *buf = malloc(512 << 10);
	uint64_t left = PREWARM_BUDGET, t0;
	unsigned files = 0, waits = 0;
	struct timespec d;

	(void)arg;
	if (!buf)
		return NULL;
	/* after the boot storm (all CPUs at 100 % for ~15 s) and with the system quiet */
	d.tv_sec = 45;
	d.tv_nsec = 0;
	nanosleep(&d, NULL);
	while (!system_quiet() && waits++ < 120) {
		d.tv_sec = 1;
		d.tv_nsec = 0;
		nanosleep(&d, NULL);
	}
	t0 = now_ns();
	for (unsigned i = 0; i < sizeof(plan) / sizeof(plan[0]) && left > 0; i++) {
		uint64_t before = left;
		unsigned f = prewarm_tree(plan[i].path, plan[i].depth, plan[i].only, &left, buf, 512 << 10);
		char b[160];
		int m = snprintf(b, sizeof(b), "np prewarm %s: %u files, %llu MiB, %llu ms since start", plan[i].path, f,
				 (unsigned long long)((before - left) >> 20), (unsigned long long)((now_ns() - t0) / 1000000ull));

		files += f;
		if (m > 0)
			np_log(b, (size_t)m);
	}
	free(buf);
	{
		char b[120];
		int m = snprintf(b, sizeof(b), "np prewarm done: %u files, %llu MiB in %llu ms", files, (unsigned long long)((PREWARM_BUDGET - left) >> 20),
				 (unsigned long long)((now_ns() - t0) / 1000000ull));

		if (m > 0)
			np_log(b, (size_t)m);
	}
	return NULL;
}

#ifdef __APPLE__
/* ------------------------------------------------------------------ thread-state sampler (diagnostic) */

/* Where the processes wait during the first interactions: every 100 ms the state of every thread of every process (libproc), and every 25 ms
 * the main thread of SpringBoard.  UNINTERRUPTIBLE is where a thread blocked on storage sits; WAITING is a mach message or a lock.
 * A line every 2 s for the first 5 minutes of the renderer's life. */
extern int proc_listpids(uint32_t type, uint32_t typeinfo, void *buffer, int buffersize);
extern int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
extern int proc_name(int pid, void *buffer, uint32_t buffersize);
extern int proc_pid_rusage(int pid, int flavor, void *buffer);

struct n2_threadinfo {
	uint64_t user_time, system_time;
	int32_t cpu_usage, policy, run_state, flags, sleep_time, curpri, priority, maxpriority;
	char name[64];
};
#define N2_PID_LISTTHREADIDS 28
#define N2_PID_THREADID64INFO 15

struct tcount { char name[20]; unsigned u, w, r; };
struct trun { char label[40]; unsigned r; };
static struct trun trun_tab[96];
/* what the sampler saw, kept for ~10 s so a stall can be explained afterwards */
struct tick25 { uint64_t t; int8_t st; };
struct pass100 { uint64_t t; unsigned n; char lab[6][36]; };
static struct tick25 ring25[512];
static struct pass100 ring100[128];
static unsigned ring25_n, ring100_n;
static struct pass100 *cur_pass;
static uint64_t self_tid;
/* self-check of the sampler: what libproc really returned in the last window (pass over every process) */
static struct { unsigned pids, named, listed, info_ok, info_fail, st[7]; int err; } sm;

/* counts one running thread of process `proc`, named `thread` */
static void trun_note(const char *proc, const char *thread, int pri)
{
	char label[40];
	unsigned free_slot = 96, i;

	/* the current scheduler priority is part of the label: placement on the big cores depends on it (>= n2schpri) */
	snprintf(label, sizeof(label), "%.12s:%.16s/%d", proc, thread[0] ? thread : "-", pri);
	if (cur_pass && cur_pass->n < 6)
		strlcpy(cur_pass->lab[cur_pass->n++], label, sizeof(cur_pass->lab[0]));
	for (i = 0; i < 96; i++) {
		if (!trun_tab[i].label[0]) {
			if (free_slot == 96)
				free_slot = i;
			break;
		}
		if (!strcmp(trun_tab[i].label, label)) {
			trun_tab[i].r++;
			return;
		}
	}
	if (free_slot < 96) {
		strlcpy(trun_tab[free_slot].label, label, sizeof(trun_tab[free_slot].label));
		trun_tab[free_slot].r = 1;
	}
}

static unsigned sample_process(int pid, int *main_state, struct tcount *tc, unsigned *total_u, int note_running)
{
	uint64_t tids[64];
	int bytes = proc_pidinfo(pid, N2_PID_LISTTHREADIDS, 0, tids, sizeof(tids));
	unsigned n = bytes > 0 ? (unsigned)bytes / sizeof(uint64_t) : 0;

	if (note_running) {
		if (bytes > 0)
			sm.listed++;
		else if (!sm.err)
			sm.err = errno ? errno : -1;
	}

	for (unsigned i = 0; i < n; i++) {
		struct n2_threadinfo ti;

		if (note_running && tids[i] == self_tid)
			continue;                                        /* the sampler's own thread is not what the system is doing */
		if (proc_pidinfo(pid, N2_PID_THREADID64INFO, tids[i], &ti, sizeof(ti)) < (int)sizeof(ti)) {
			if (note_running) {
				sm.info_fail++;
				if (!sm.err)
					sm.err = errno ? 1000 + errno : -2;
			}
			continue;
		}
		if (note_running) {
			sm.info_ok++;
			sm.st[ti.run_state >= 1 && ti.run_state <= 5 ? ti.run_state : 6]++;
		}
		if (i == 0 && main_state)
			*main_state = ti.run_state;
		if (ti.run_state == 4) {
			tc->u++;
			(*total_u)++;
		} else if (ti.run_state == 3) {
			tc->w++;
		} else if (ti.run_state == 1) {
			tc->r++;
			if (note_running)
				trun_note(tc->name, ti.name, ti.curpri);
		}
	}
	return n;
}

/* CPU time per process: which process burns the CPU while SpringBoard waits (delta of the task's user+system time over the window), the IPC
 * traffic it sees, and one known pure waste on this device: vmd (Visual Voicemail) busy-loops without CommCenter. */
struct n2_taskinfo {
	uint64_t virtual_size, resident_size, total_user, total_system, threads_user, threads_system;
	int32_t policy, faults, pageins, cow_faults, messages_sent, messages_received, syscalls_mach, syscalls_unix, csw, threadnum, numrunning, priority;
};
#define N2_PID_TASKINFO 4
struct cpuprev { int pid; uint64_t ticks; int32_t msgs; };
static struct cpuprev cpu_prev[768];
static int vmd_stopped;

static void cpu_window_report(const int *pids, int cnt)
{
	static struct cpuprev now_tab[768];
	struct { char name[20]; uint64_t ms; int32_t msgs; } top[5];
	mach_timebase_info_data_t tb = { 1, 1 };
	unsigned nn = 0;

	memset(top, 0, sizeof(top));
	mach_timebase_info(&tb);
	for (int i = 0; i < cnt && nn < 768; i++) {
		struct n2_taskinfo ti;
		uint64_t ticks, prev_ticks = 0, ms;
		int32_t prev_msgs = 0;
		char nm[48] = "";

		if (pids[i] <= 0 || proc_pidinfo(pids[i], N2_PID_TASKINFO, 0, &ti, sizeof(ti)) < (int)sizeof(ti))
			continue;
		ticks = ti.total_user + ti.total_system;
		now_tab[nn].pid = pids[i];
		now_tab[nn].ticks = ticks;
		now_tab[nn].msgs = ti.messages_received;
		nn++;
		for (unsigned k = 0; k < 768 && cpu_prev[k].pid; k++)
			if (cpu_prev[k].pid == pids[i]) {
				prev_ticks = cpu_prev[k].ticks;
				prev_msgs = cpu_prev[k].msgs;
				break;
			}
		if (!prev_ticks || ticks < prev_ticks)
			continue;
		ms = (ticks - prev_ticks) * tb.numer / tb.denom / 1000000ull;
		if (!ms)
			continue;
		if (proc_name(pids[i], nm, sizeof(nm)) <= 0)
			snprintf(nm, sizeof(nm), "pid%d", pids[i]);
		for (unsigned t = 0; t < 5; t++)
			if (ms > top[t].ms) {
				for (unsigned u = 4; u > t; u--)
					top[u] = top[u - 1];
				strlcpy(top[t].name, nm, sizeof(top[t].name));
				top[t].ms = ms;
				top[t].msgs = ti.messages_received - prev_msgs;
				break;
			}
	}
	{
		char b[240];
		int o = snprintf(b, sizeof(b), "np cpu ms/2s (msgs received):");

		for (unsigned t = 0; t < 5 && top[t].ms && o > 0 && o < (int)sizeof(b) - 48; t++)
			o += snprintf(b + o, sizeof(b) - (size_t)o, " %s=%llu(%d)", top[t].name, (unsigned long long)top[t].ms, top[t].msgs);
		if (o > 0)
			np_log(b, (size_t)(o < (int)sizeof(b) ? o : (int)sizeof(b) - 1));
	}
	{                                                         /* every 10 s: footprint and wired memory of the two UI processes (what grows when the lag starts) */
		static unsigned tick;

		if (++tick % 5 == 0) {
			char b[240];
			int o = snprintf(b, sizeof(b), "np mem:");

			for (int i = 0; i < cnt && o > 0 && o < (int)sizeof(b) - 80; i++) {
				char nm[48] = "";
				struct rusage_info_v4 ri;

				if (pids[i] <= 0 || proc_name(pids[i], nm, sizeof(nm)) <= 0 || (strcmp(nm, "backboardd") && strcmp(nm, "SpringBoard")))
					continue;
				if (proc_pid_rusage(pids[i], RUSAGE_INFO_V4, &ri) == 0)
					o += snprintf(b + o, sizeof(b) - (size_t)o, " %s footprint %llu MiB, wired %llu MiB, resident %llu MiB;", nm,
						      (unsigned long long)(ri.ri_phys_footprint >> 20), (unsigned long long)(ri.ri_wired_size >> 20),
						      (unsigned long long)(ri.ri_resident_size >> 20));
			}
			if (o > 8)
				np_log(b, (size_t)(o < (int)sizeof(b) ? o : (int)sizeof(b) - 1));
		}
	}
	memset(cpu_prev, 0, sizeof(cpu_prev));
	memcpy(cpu_prev, now_tab, nn * sizeof(now_tab[0]));
}

/* Metal's own caches: what is in them and whether a file can be created there at all (the front-end cache dirs stay empty on this device). */
static void cache_probe(const char *dir)
{
	char b[200], path[256];
	DIR *d = opendir(dir);
	unsigned files = 0;
	unsigned long long bytes = 0;
	int o, fd, werr = 0;

	if (d) {
		struct dirent *e;

		while ((e = readdir(d)) != NULL) {
			struct stat st;

			if (e->d_name[0] == '.')
				continue;
			snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
			if (stat(path, &st) == 0) {
				files++;
				bytes += (unsigned long long)st.st_size;
			}
		}
		closedir(d);
	}
	snprintf(path, sizeof(path), "%s/.np_probe", dir);
	fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
	if (fd < 0) {
		werr = errno;
	} else {
		char blk[4096];

		memset(blk, 0x5a, sizeof(blk));
		if (write(fd, blk, sizeof(blk)) != (ssize_t)sizeof(blk))
			werr = errno ? errno : -1;
		if (fsync(fd) != 0 && !werr)
			werr = 1000 + errno;
		close(fd);
		unlink(path);
	}
	o = snprintf(b, sizeof(b), "np cache %s: %s, %u files %llu bytes, write test errno=%d", dir, d ? "opened" : "no dir", files, bytes, werr);
	if (o > 0)
		np_log(b, (size_t)(o < (int)sizeof(b) ? o : (int)sizeof(b) - 1));
}

/* A submit pause was seen (note_submit): what SpringBoard's main thread did and which threads were running during it. */
static void gap_report(uint64_t end_ns, uint64_t len_ns, unsigned burst)
{
	uint64_t start = end_ns > len_ns ? end_ns - len_ns : 0;
	unsigned r = 0, w = 0, u = 0, n = 0, passes = 0, nl = 0;
	struct { char lab[36]; unsigned c; } tab[24];
	char b[240];
	int o;

	memset(tab, 0, sizeof(tab));
	for (unsigned i = 0; i < 512; i++) {
		const struct tick25 *k = &ring25[i];

		if (k->t >= start && k->t <= end_ns) {
			n++;
			r += k->st == 1;
			w += k->st == 3;
			u += k->st == 4;
		}
	}
	for (unsigned i = 0; i < 128; i++) {
		const struct pass100 *q = &ring100[i];

		if (q->t < start || q->t > end_ns)
			continue;
		passes++;
		for (unsigned j = 0; j < q->n; j++) {
			unsigned t;

			for (t = 0; t < nl && strcmp(tab[t].lab, q->lab[j]); t++)
				;
			if (t == nl && nl < 24)
				strlcpy(tab[nl++].lab, q->lab[j], sizeof(tab[0].lab));
			if (t < 24)
				tab[t].c++;
		}
	}
	o = snprintf(b, sizeof(b), "np gap: %llu ms between submits after a burst of %u | SpringBoard main run/wait/unint=%u/%u/%u of %u | running (%u passes):",
		     (unsigned long long)(len_ns / 1000000ull), burst, r, w, u, n, passes);
	for (unsigned k = 0; k < 4; k++) {
		unsigned best = 24, top = 0;

		for (unsigned t = 0; t < nl; t++)
			if (tab[t].c > top) {
				top = tab[t].c;
				best = t;
			}
		if (best == 24 || o <= 0 || o >= (int)sizeof(b) - 50)
			break;
		o += snprintf(b + o, sizeof(b) - (size_t)o, " %s=%u", tab[best].lab, tab[best].c);
		tab[best].c = 0;
	}
	if (o > 0)
		np_log(b, (size_t)(o < (int)sizeof(b) ? o : (int)sizeof(b) - 1));
}

/* vmd (Visual Voicemail, com.apple.voicemail.vmd) busy-loops at ~95 % of a core without CommCenter and is not needed on this device: stopped
 * for good within half a second of its start (SIGSTOP: launchd does not restart a job that is still running; a respawn under a new pid is found
 * again).  Polls twice a second for the first 3 minutes of the renderer's life, then every 5 s for 20 minutes. */
static void *vmd_thread(void *arg)
{
	int pids[512];

	(void)arg;
	for (unsigned n = 0; D.up && n < 360 + 240; n++) {
		int cnt;

		nanosleep(n < 360 ? &(struct timespec){ 0, 500000000L } : &(struct timespec){ 5, 0 }, NULL);
		if (vmd_stopped && kill(vmd_stopped, 0) == 0)
			continue;
		vmd_stopped = 0;
		cnt = proc_listpids(1 /* PROC_ALL_PIDS */, 0, pids, sizeof(pids)) / (int)sizeof(int);
		for (int i = 0; i < cnt; i++) {
			char nm[48] = "";
			char b[112];
			int m;

			if (pids[i] <= 0 || proc_name(pids[i], nm, sizeof(nm)) <= 0 || strcmp(nm, "vmd"))
				continue;
			m = snprintf(b, sizeof(b), "np vmd: pid %d found after %u polls, SIGSTOP %s", pids[i], n, kill(pids[i], SIGSTOP) == 0 ? "sent" : "failed");
			vmd_stopped = pids[i];
			if (m > 0)
				np_log(b, (size_t)m);
			break;
		}
	}
	return NULL;
}

static void *sampler_thread(void *arg)
{
	int pids[512];
	struct tcount per[64];
	unsigned sb_r = 0, sb_w = 0, sb_u = 0, sb_n = 0, samples = 0, any_u = 0, windows = 0;
	uint64_t window_start = now_ns();
	int sb_pid = 0, tick = 0;
	struct timespec d = { 0, 25000000L };

	uint64_t gap_seen = 0;
	unsigned gap_lines = 0;

	(void)arg;
	(void)pthread_threadid_np(NULL, &self_tid);
	memset(per, 0, sizeof(per));
	nanosleep(&(struct timespec){ 20, 0 }, NULL);          /* after the renderer is up */
	while (windows < 250 && D.up) {
		nanosleep(&d, NULL);
		if (!sb_pid || tick % 40 == 0) {                         /* find SpringBoard again now and then */
			int cnt = proc_listpids(1 /* PROC_ALL_PIDS */, 0, pids, sizeof(pids)) / (int)sizeof(int);

			sb_pid = 0;
			for (int i = 0; i < cnt; i++) {
				char nm[32] = "";

				if (pids[i] > 0 && proc_name(pids[i], nm, sizeof(nm)) > 0 && !strcmp(nm, "SpringBoard")) {
					sb_pid = pids[i];
					break;
				}
			}
		}
		if (sb_pid) {
			int st = 0;
			struct tcount scratch = { "", 0, 0, 0 };
			unsigned zero = 0;

			if (sample_process(sb_pid, &st, &scratch, &zero, 0)) {
				sb_n++;
				sb_r += st == 1;
				sb_w += st == 3;
				sb_u += st == 4;
				ring25[ring25_n % 512].t = now_ns();
				ring25[ring25_n % 512].st = (int8_t)st;
				ring25_n++;
			}
		}
		{
			uint64_t ge = __atomic_load_n(&gap_end_ns, __ATOMIC_ACQUIRE);

			if (ge && ge != gap_seen) {
				gap_seen = ge;
				if (gap_lines++ < 80)
					gap_report(ge, gap_len_ns, gap_burst_before);
			}
		}
		if (++tick % 4 == 0) {                                   /* every 100 ms: everything */
			int cnt = proc_listpids(1, 0, pids, sizeof(pids)) / (int)sizeof(int);
			unsigned total_u = 0;

			cur_pass = &ring100[ring100_n++ % 128];
			cur_pass->t = now_ns();
			cur_pass->n = 0;

			for (int i = 0; i < cnt && cnt > 0; i++) {
				char nm[48] = "";                                 /* proc_name() fails for buffers this process-name table used to pass (20 bytes) */
				struct tcount tc = { "", 0, 0, 0 };
				unsigned slot = 0;

				sm.pids++;
				if (pids[i] <= 0)
					continue;
				if (proc_name(pids[i], nm, sizeof(nm)) > 0)
					sm.named++;
				else
					snprintf(nm, sizeof(nm), "pid%d", pids[i]);
				strlcpy(tc.name, nm, sizeof(tc.name));
				(void)sample_process(pids[i], NULL, &tc, &total_u, 1);
				if (!tc.u)
					continue;
				for (slot = 0; slot < 64; slot++)
					if (!per[slot].name[0] || !strcmp(per[slot].name, tc.name))
						break;
				if (slot < 64) {
					if (!per[slot].name[0])
						strlcpy(per[slot].name, tc.name, sizeof(per[slot].name));
					per[slot].u += tc.u;
				}
			}
			samples++;
			any_u += total_u;
		}
		if (now_ns() - window_start >= 2000000000ull) {
			char b[240];
			int m, o;
			unsigned best[3] = { 64, 64, 64 };

			for (unsigned k = 0; k < 3; k++) {
				unsigned top = 0, ti = 64;

				for (unsigned i = 0; i < 64; i++) {
					int taken = (best[0] == i) || (best[1] == i) || (best[2] == i);

					if (!taken && per[i].u > top) {
						top = per[i].u;
						ti = i;
					}
				}
				best[k] = ti;
			}
			m = snprintf(b, sizeof(b), "np threads: SpringBoard main thread run/wait/uninterruptible = %u/%u/%u of %u samples | all processes: uninterruptible threads per sample %u.%u |",
				     sb_r, sb_w, sb_u, sb_n, samples ? any_u / samples : 0, samples ? (any_u * 10 / samples) % 10 : 0);
			o = m;
			for (unsigned k = 0; k < 3 && best[k] < 64 && o > 0 && o < (int)sizeof(b) - 40; k++)
				o += snprintf(b + o, sizeof(b) - (size_t)o, " %s:%u", per[best[k]].name, per[best[k]].u);
			if (o > 0)
				np_log(b, (size_t)(o < (int)sizeof(b) ? o : (int)sizeof(b) - 1));
			{                                                        /* who is running: the five busiest threads of the window */
				char b2[240];
				int o2 = snprintf(b2, sizeof(b2), "np running threads (samples):");

				for (unsigned k = 0; k < 5; k++) {
					unsigned top = 0, ti = 96;

					for (unsigned i = 0; i < 96; i++)
						if (trun_tab[i].label[0] && trun_tab[i].r > top) {
							top = trun_tab[i].r;
							ti = i;
						}
					if (ti == 96 || o2 <= 0 || o2 >= (int)sizeof(b2) - 50)
						break;
					o2 += snprintf(b2 + o2, sizeof(b2) - (size_t)o2, " %s=%u", trun_tab[ti].label, trun_tab[ti].r);
					trun_tab[ti].r = 0;                              /* taken */
				}
				if (o2 > 0)
					np_log(b2, (size_t)(o2 < (int)sizeof(b2) ? o2 : (int)sizeof(b2) - 1));
				{
					char b3[200];
					int o3 = snprintf(b3, sizeof(b3), "np sampler check: pids=%u named=%u listed=%u thread_info ok=%u fail=%u states run/stop/wait/unint/halt/other=%u/%u/%u/%u/%u/%u err=%d",
							  sm.pids, sm.named, sm.listed, sm.info_ok, sm.info_fail, sm.st[1], sm.st[2], sm.st[3], sm.st[4], sm.st[5], sm.st[6], sm.err);

					if (o3 > 0)
						np_log(b3, (size_t)(o3 < (int)sizeof(b3) ? o3 : (int)sizeof(b3) - 1));
				}
				memset(&sm, 0, sizeof(sm));
				memset(trun_tab, 0, sizeof(trun_tab));
			}
			{
				int allp[512];
				int ncnt = proc_listpids(1, 0, allp, sizeof(allp)) / (int)sizeof(int);

				if (ncnt > 0)
					cpu_window_report(allp, ncnt);
			}
			if (windows == 30 || windows == 120) {
				cache_probe("/var/root/Library/Caches/com.apple.metalfe");
				cache_probe("/var/root/Library/Caches/com.apple.metal");
				cache_probe("/var/mobile/Library/Caches/com.apple.springboard/com.apple.metalfe");
				cache_probe("/var/mobile/Library/Caches/com.apple.springboard/com.apple.metal");
			}
			memset(per, 0, sizeof(per));
			sb_r = sb_w = sb_u = sb_n = samples = any_u = 0;
			windows++;
			window_start = now_ns();
		}
	}
	return NULL;
}

void np_start_sampler(void)
{
	pthread_t t;
	pthread_attr_t a;

	if (access("/System/Library/Extensions/Nord2Metal.bundle/diag", F_OK) != 0)
		return;                                   /* diagnostics only: the marker file `diag` in the bundle turns the sampler on */

	if (pthread_attr_init(&a) != 0)
		return;
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	(void)pthread_create(&t, &a, sampler_thread, NULL);
	(void)pthread_create(&t, &a, vmd_thread, NULL);
	pthread_attr_destroy(&a);
}
#else
void np_start_sampler(void) {}
#endif

void np_start_prewarm(void)
{
	pthread_t t;
	pthread_attr_t a;

	if (pthread_attr_init(&a) != 0)
		return;
	pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
	(void)pthread_create(&t, &a, prewarm_thread, NULL);
	pthread_attr_destroy(&a);
}
