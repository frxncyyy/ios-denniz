/* SPDX-License-Identifier: MIT */
/*
 * n2vcu.c -- the /dev/vcu device model: the Linux mtk_vcu ABI in user space, over n2vc_hw.
 *
 * Owner: V40.  Interface: n2vcu.h + mtk_vcu_abi.h.  Back end: include/n2vc_hw.h (T02 host fake,
 * P03 phone).  Reference: nord2-current-kernel/drivers/media/platform/mtk-vcu/mtk_vcu.c and
 * mtk_vcodec_mem.c; every handler below cites the lines it reproduces.
 *
 * What is faithful
 * ----------------
 *   - request numbers, 32-bit struct layouts, validation order and the -1/EPERM default case;
 *   - VCU_SET_OBJECT hands the payload to the AP handler and copies the (possibly modified)
 *     share_obj back; an in-range id with no handler still returns 0 (the kernel overwrites
 *     vcu_ipi_handler's -1 with copy_to_user's result, mtk_vcu.c:1814-1821);
 *   - VCU_GET_OBJECT blocks for the instance ipi_id belongs to, sets ipi_done on entry and clears
 *     ipi_got on fetch (mtk_vcu.c:478-518);
 *   - allocation/free field-by-field semantics of mtk_vcodec_mem.c;
 *   - GCE translation: CMD_READ/WRITE/POLL_REG/WAIT_EVENT/MEM_MV/POLL_ADDR and the skip-on-bad-
 *     address rule of vcu_set_gce_cmd() (mtk_vcu.c:695-800), synchronous, one rwvc_reg_op batch
 *     per run of adjacent register commands; a VENC list powers its core (RWVC_BLK_VENC or
 *     RWVC_BLK_VENC_C1) through hw->power for the duration of the run, as venc_encode_prepare/
 *     unprepare do in the kernel (mtk_vcu.c:994-997, :822); the flush callback is signalled even
 *     when the list failed, like the kernel's timeout-then-callback path;
 *   - DEC04: vcu_windows[] keeps the ten old VDEC entries (first match wins, so every address
 *     they cover keeps its old window index and K02's sequences are unchanged) and appends the
 *     ABI rev-3 RWVC_WIN_VDEC_ALL over 0x16000000+0x30000, which is the single mapping the vendor
 *     decoder takes.  Only the old gaps reach it: 0x1000/0x2000/0x3800, larb5 0xd000, 0xe000,
 *     0x11800, LAT 0x12000..0x18800, AVC_VLD 0x22000, PP 0x24000, 0x29000, larb4 0x2e000, and
 *     the tail of the LAT_VLD page.  The per-page power check and the larb read-only rule are
 *     the kernel's (redwood_vcodec.h rev 3 / DEC07), not this model's;
 *   - DEC04: guest register stores can be combined (n2vcu_set_write_combining(), default off):
 *     an aligned 32-bit store is queued and sent as one RWVC_REGS batch, flushed before any
 *     poll/wait_irq/GCE list/power change/sub-word access/ioctl entry and at 256 ops; a read
 *     that arrives with stores queued carries them ahead of itself in the same batch.  A combined
 *     full-word store does not read the register first, which also removes a spurious read of
 *     side-effecting registers (the VLD barrel shifter advances on a read);
 *   - mmap: vcu_check_reg_base() for the three VCU windows (mtk_vcu.c:679-693), the MAP_SHMEM_*
 *     ranges (:1704-1718) and the PA/iova mapping of allocated buffers (:1668-1690).
 *
 * What is different, deliberately (also in STATUS.md)
 * ---------------------------------------------------
 *   - the waits that the kernel gives a timeout keep one here too (GCE wait-for-event,
 *     poll-address, wait-for-callback: -ETIMEDOUT instead of hanging); the two that the kernel
 *     makes indefinite block until their event, abort or the calling cpu's stop_request, which
 *     returns -EINTR like a signal (GET_OBJECT, GET_LOG_OBJECT);
 *   - the GCE list is executed synchronously and its first hardware error is returned by the
 *     ioctl; the kernel queues it and reports failures through the timeout callback;
 *   - a register address inside a VCU map window but outside every RWVC_WIN_* window is skipped
 *     and counted (the model has no back end for the gaps; the kernel's CMDQ would write it);
 *   - mem_obj gets iova = pa = the hardware address for both allocation calls (the kernel leaves
 *     the unused one 0 and invents a 16-bit `va`); `va` is the guest mapping of the buffer so the
 *     daemon can use it, which is what vcu_mapping_dm_addr() does for the DMEM;
 *   - the VCU_SET_LOG_OBJECT request number cannot be derived from the kernel tree (struct
 *     log_test does not exist there), so nr 12 is accepted with any size; VCU_GET_LOG_OBJECT
 *     blocks like vcu_log_get() until the AP side calls n2vcu_ap_set_log(), a close, destroy or
 *     the calling cpu's stop_request (-EINTR; the kernel's wait_event_freezable), and clears
 *     vdec_log_got after the copy;
 *   - VCU_SET_MMAP_TYPE returns 0 (the kernel has no case for it and returns -1);
 *   - mmap accepts a bare `pa`/`iova` of an allocated buffer as the offset in addition to the
 *     MAP_SHMEM_* bases, because that is what the vendor's eVideoMemMAP passes;
 *   - the DMEM mmap honours the offset inside the MAP_SHMEM_ALLOC/COMMIT range (the kernel always
 *     maps from the start of the DMEM, mtk_vcu.c:1704-1712).
 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "n2vcu.h"
#include "mtk_vcu_abi.h"
#include "n2elf.h"

#define VCU_DMEM_BYTES 0x80000u             /* VCU_SHMEM_SIZE, mtk_vcu.c:94, :2542 */
#define VCU_ALLOC_MAX_BYTES 0x8000000u      /* CODEC_ALLOCATE_MAX_BUFFER_SIZE */
#define VCU_MAX_ALLOCS 512u                 /* CODEC_MAX_BUFFER */
#define VCU_MAX_GCE 64u                     /* VCODEC_INST_MAX */
#define VCU_PAGE 0x1000u
/* DEC04: one combined guest-store batch.  Small enough that a batch plus the GCE list that may
 * follow stays well below RWVC_REG_MAX_OPS (2048), large enough to hold a burst of register
 * programming between two reads. */
#define VCU_WC_MAX_OPS 256u

/* The three windows mtk_vcu_probe() takes from the device tree and vcu_check_reg_base() accepts
 * (mt6893.dts:6212-6218, FACTS 1.24). */
static const struct { uint32_t base, size; } vcu_map_base[] = {
	{ 0x16000000u, 0x40000u },
	{ 0x17020000u, 0x10000u },
	{ 0x17820000u, 0x10000u },
};

/* Physical address -> RWVC window (include/redwood_vcodec.h:13-15, mt6893.dts:6281-6292).  Used for
 * both the GCE command addresses and the guest MMIO hooks.  First match wins: the ten old VDEC
 * entries stay first so every address they already covered keeps its old index (K02 sequences,
 * existing V40 checks), and the rev-3 whole-VDEC window comes last and catches only the gaps.
 * The vendor decoder maps 0x16000000+0x40000 once and addresses AVC_VLD 0x22000, PP 0x24000, the
 * LAT blocks at 0x10000.. and larb4/larb5 through the same mapping (facts/VDEC_PROTOCOL.md
 * section 4, DEC02's trace); without the last entry those accesses would be dropped by
 * vcu_reg_lookup() and counted in mmio_bad/gce_skipped. */
static const struct { uint32_t base, size; uint8_t win; } vcu_windows[] = {
	{ 0x16000000u, 0x1000u, RWVC_WIN_VDEC_BASE },
	{ 0x16004000u, 0x1000u, RWVC_WIN_VDEC_RACING },
	{ 0x1600f000u, 0x1000u, RWVC_WIN_VDEC_SOC_GCON },
	{ 0x16010000u, 0x1000u, RWVC_WIN_VDEC_LAT_MISC },
	{ 0x16011000u, 0x0400u, RWVC_WIN_VDEC_LAT_VLD },
	{ 0x16020000u, 0x1000u, RWVC_WIN_VDEC_VLD },
	{ 0x16021000u, 0x1000u, RWVC_WIN_VDEC_MC },
	{ 0x16023000u, 0x1000u, RWVC_WIN_VDEC_MV },
	{ 0x16025000u, 0x4000u, RWVC_WIN_VDEC_MISC },
	{ 0x1602f000u, 0x1000u, RWVC_WIN_VDEC_SYS },
	{ 0x16000000u, 0x30000u, RWVC_WIN_VDEC_ALL },   /* DEC04; rev-3 ABI */
	{ 0x17020000u, 0x2000u, RWVC_WIN_VENC },
	{ 0x17820000u, 0x2000u, RWVC_WIN_VENC_C1 },
};

/* MAP_SHMEM_* from mtk_vcu.c:100-113. */
#define VCU_SHMEM_ALLOC_BASE 0x80000000ull
#define VCU_SHMEM_COMMIT_BASE 0x88000000ull
#define VCU_SHMEM_RANGE 0x80000ull
#define VCU_SHMEM_MM_BASE 0x200000000ull
#define VCU_SHMEM_MM_CACHEABLE_BASE 0x400000000ull
#define VCU_SHMEM_MM_RANGE 0x1ffffffffull
#define VCU_SHMEM_PA_BASE 0x800000000ull
#define VCU_SHMEM_PA_RANGE 0x100000000ull

/* Private guest address range for register-window MMIO mappings.  Below n2a32_mem_alloc()'s
 * ALLOC_START 0x40000000, so it never collides with anything the loader or the shims allocate.
 * 64 MB is far more than the 15 MMIO hooks n2a32_mem_mmio() supports. */
#define VCU_MMIO_FIRST 0x30000000u
#define VCU_MMIO_LAST 0x34000000u

struct n2vcu_inst {
	struct share_obj out;   /* the message the daemon will fetch */
	int got;                /* ipi_got: a message is queued */
	int dispatched;         /* ipi_done: the daemon entered GET_OBJECT since the last send */
};

struct n2vcu_alloc {
	int used;
	int is_mva;             /* allocated by VCU_MVA_ALLOCATION (a vb2 buffer in the kernel) */
	uint32_t handle;
	uint32_t pa;
	uint32_t bytes;
	uint32_t guest;
};

struct n2vcu_gce {
	int used;
	uint64_t handle;
	unsigned flush_done;    /* callbacks not yet consumed by VCU_GCE_WAIT_CALLBACK */
	unsigned pending;       /* flushes in progress (informational) */
};

struct n2vcu_map {
	struct n2vcu_map *next;
	struct n2vcu *vcu;
	struct n2a32_mmio hook;
	uint32_t guest, phys, bytes;
};

struct n2vcu {
	struct n2a32_mem *mem;
	struct n2vc_hw hw;
	int venc_hold;              /* opus-enc4k: keep VENC cores powered between lists (n2vcu_set_venc_hold) */
	uint8_t venc_held[2];       /* core 0 / core 1 power reference held by this model */
	pthread_mutex_t lock;       /* recursive: the MMIO hook may run under an ioctl */

	struct n2vfs_dev dev;
	struct n2vfs_dev dev_vpu;
	unsigned open_count;
	unsigned abort;

	unsigned th_num[VCU_CODEC_MAX];
	unsigned get_timeout_ms, irq_timeout_ms, poll_timeout_ms, cb_timeout_ms;

	n2vcu_ipi_handler fn[MTK_IPI_MAX];
	void *fn_ctx[MTK_IPI_MAX];
	struct n2vcu_inst inst[VCU_CODEC_MAX];

	struct n2vcu_alloc alloc[VCU_MAX_ALLOCS];
	struct n2vcu_gce gce[VCU_MAX_GCE];
	struct n2vcu_map *maps;

	uint32_t dmem_guest, dmem_bytes;
	/* IV carry patch (board: "IV -> V40: DMEM0_VMA is the COMMIT mmap address"): the kernel's
	 * VCU_DMEM0_VMA is the COMMIT mapping's vma->vm_start (mtk_vcu.c:1707-1711), and
	 * vcu_mapping_dm_addr() translates against it (:1199-1225).  vpud remap_shmem() maps the DMEM
	 * over its own RW data segment and returns pointers into that range, so the model must record
	 * the address the guest asked for, not only dmem_guest. */
	uint32_t commit_vma;
	int commit_seen;
	uint32_t map_buf_pa_valid;      /* the previous allocation was VCU_PA_ALLOCATION */

	struct rwvc_reg_op *ops;        /* RWVC_REG_MAX_OPS scratch, under `lock` */

	/* DEC04 write-combining.  wc_ops/wc_n are protected by `lock`; wc_on is read under it too.
	 * A queued store is already "executed" from the guest's point of view (the store returns),
	 * so the flush points in vcu_mmio_read(), vcu_mmio_write(), vcu_gce_flush_ioctl(),
	 * vcu_dev_ioctl() and vcu_dev_close() define the ordering the guest can observe. */
	int wc_on;
	unsigned wc_n;
	struct rwvc_reg_op wc_ops[VCU_WC_MAX_OPS + 1u];   /* + the read of vcu_wc_flush_read() */

	/* The log buffer and its wakeup, mirroring vcu->vdec_log_info / vdec_log_get_wq /
	 * vdec_log_got (mtk_vcu.c:307-309, :521-545, :2113-2145).  Own mutex: the log wait must not
	 * sit under the recursive `lock` (pthread_cond_timedwait cannot be combined with it), and no
	 * path takes `lock` while holding log_mu, so the two never invert. */
	pthread_mutex_t log_mu;
	pthread_cond_t log_cv;
	char log_info[MTK_VCU_LOG_INFO_SIZE];
	unsigned log_got;               /* vdec_log_got: a log is waiting to be fetched */
	unsigned log_abort;             /* n2vcu_abort(): every wait fails from now on */
	unsigned log_wake;              /* bumped by close(): wakes a blocked GET_LOG with -EINTR */
	unsigned log_waiters;           /* threads inside vcu_get_log_object() */
	unsigned log_blocked;           /* threads inside it that are waiting (gauge) */
	unsigned destroying;            /* n2vcu_destroy() has started */

	struct n2vcu_stats stats;
};

/* ------------------------------------------------------------------ small helpers */

static int64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static uint64_t now_us_mono(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void sleep_us(unsigned us)
{
	struct timespec ts;

	ts.tv_sec = us / 1000000u;
	ts.tv_nsec = (long)(us % 1000000u) * 1000L;
	nanosleep(&ts, NULL);
}

/* Absolute CLOCK_REALTIME deadline `ms` from now, for pthread_cond_timedwait().  CLOCK_REALTIME is
 * the only clock pthread_cond_timedwait() uses on every host the model is built for (Darwin has no
 * pthread_condattr_setclock), and the wait is only a 100 ms poll tick: a wall-clock step changes
 * the next check, never correctness. */
static void cond_deadline_ms(struct timespec *ts, unsigned ms)
{
	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_sec += (time_t)(ms / 1000u);
	ts->tv_nsec += (long)(ms % 1000u) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_nsec -= 1000000000L;
		ts->tv_sec++;
	}
}

/* 1 when [addr, addr+len) lies inside the window [base, base+window).  Only subtraction is used,
 * never addr+len, so a guest-controlled 64-bit mmap offset near UINT64_MAX cannot wrap a range
 * test (R1 finding F14).  `len` is still checked against the window, so a caller cannot pass a
 * length that would not fit. */
static int vcu_span_contains(uint64_t base, uint64_t window, uint64_t addr, uint32_t len)
{
	uint64_t d;

	if (addr < base)
		return 0;
	d = addr - base;
	return d <= window && (uint64_t)len <= window - d;
}

static uint32_t ld32(struct n2a32_cpu *c, uint32_t a) { return n2a32_ld32(c, a); }
static void st32(struct n2a32_cpu *c, uint32_t a, uint32_t v) { n2a32_st32(c, a, v); }
static uint64_t ld64(struct n2a32_cpu *c, uint32_t a) { return n2a32_ld64(c, a); }
static void st64(struct n2a32_cpu *c, uint32_t a, uint64_t v) { n2a32_st64(c, a, v); }

static void ld_bytes(struct n2a32_cpu *c, uint32_t a, void *dst, uint32_t n)
{
	uint8_t *d = (uint8_t *)dst;
	uint32_t i;

	for (i = 0; i < n; i++)
		d[i] = n2a32_ld8(c, a + i);
}

static void st_bytes(struct n2a32_cpu *c, uint32_t a, const void *src, uint32_t n)
{
	const uint8_t *s = (const uint8_t *)src;
	uint32_t i;

	for (i = 0; i < n; i++)
		n2a32_st8(c, a + i, s[i]);
}

/* The kernel's copy_from_user/copy_to_user of each uapi struct, field by field (the guest may pass
 * an unaligned pointer; the n2a32_ld32()/n2a32_st32() helpers go through memcpy). */
static void get_share(struct n2a32_cpu *c, uint32_t a, struct share_obj *o)
{
	o->id = (int32_t)ld32(c, a);
	o->len = ld32(c, a + 4);
	ld_bytes(c, a + 8, o->share_buf, MTK_VCU_SHARE_BUF_SIZE);
}

static void put_share(struct n2a32_cpu *c, uint32_t a, const struct share_obj *o)
{
	st32(c, a, (uint32_t)o->id);
	st32(c, a + 4, o->len);
	st_bytes(c, a + 8, o->share_buf, MTK_VCU_SHARE_BUF_SIZE);
}

static void get_mem(struct n2a32_cpu *c, uint32_t a, struct mem_obj *o)
{
	o->iova = ld64(c, a);
	o->len = ld32(c, a + 8);
	o->pad0 = ld32(c, a + 12);
	o->pa = ld64(c, a + 16);
	o->va = ld64(c, a + 24);
}

static void put_mem(struct n2a32_cpu *c, uint32_t a, const struct mem_obj *o)
{
	st64(c, a, o->iova);
	st32(c, a + 8, o->len);
	st32(c, a + 12, o->pad0);
	st64(c, a + 16, o->pa);
	st64(c, a + 24, o->va);
}

static void get_map_obj(struct n2a32_cpu *c, uint32_t a, struct map_obj *o)
{
	o->map_buf = ld32(c, a);
	o->map_type = ld32(c, a + 4);
	o->reserved = ld64(c, a + 8);
}

static void get_cmdq_obj(struct n2a32_cpu *c, uint32_t a, struct gce_cmdq_obj *o)
{
	o->cmds_user_ptr = ld64(c, a);
	o->gce_handle = ld64(c, a + 8);
	o->flush_order = ld32(c, a + 0x10);
	o->codec_type = ld32(c, a + 0x14);
	o->core_id = ld32(c, a + 0x18);
	o->secure = ld32(c, a + 0x1c);
}

static void get_gce_obj(struct n2a32_cpu *c, uint32_t a, struct gce_obj *o)
{
	o->gce_handle = ld64(c, a);
	o->flush_order = ld32(c, a + 8);
	o->codec_type = ld32(c, a + 12);
}

/* ------------------------------------------------------------------ register windows */

static int vcu_reg_lookup(uint32_t addr, unsigned *win, uint32_t *off)
{
	size_t i;

	for (i = 0; i < sizeof(vcu_windows) / sizeof(vcu_windows[0]); i++) {
		uint32_t base = vcu_windows[i].base;

		if (addr >= base && (uint64_t)addr + 4u <= (uint64_t)base + vcu_windows[i].size) {
			*win = vcu_windows[i].win;
			*off = addr - base;
			return 1;
		}
	}
	return 0;
}

static int vcu_in_map_base(uint64_t addr, uint32_t len)
{
	size_t i;

	for (i = 0; i < sizeof(vcu_map_base) / sizeof(vcu_map_base[0]); i++)
		if (vcu_span_contains(vcu_map_base[i].base, vcu_map_base[i].size, addr, len))
			return 1;
	return 0;
}

/* Every hw->regs() invocation goes through here, so stats.hw_regs_calls is exactly the number of
 * RWVC_REGS ioctls the phone back end would see (one per call).  Caller holds `lock`. */
static int vcu_hw_regs(struct n2vcu *v, struct rwvc_reg_op *ops, unsigned count, unsigned *done)
{
	v->stats.hw_regs_calls++;
	return v->hw.regs(v->hw.ctx, ops, count, done);
}

/* DEC04: send the queued guest stores as one batch, in their original order.  Caller holds
 * `lock`.  On a back-end error the queue is still emptied: the kernel's RWVC_REGS stops at the
 * first failed op (out_done), so resending the rest would duplicate the ones that landed.  The
 * stores that did not land are lost, exactly like posted writes to a block that lost power; the
 * guest store cannot report it and wc_errors is the visible counter. */
static int vcu_wc_flush(struct n2vcu *v)
{
	unsigned n = v->wc_n, done = 0;
	int rc;

	if (n == 0)
		return 0;
	v->wc_n = 0;
	rc = vcu_hw_regs(v, v->wc_ops, n, &done);
	if (rc || done != n) {
		v->stats.wc_errors++;
		return rc ? rc : -EIO;
	}
	v->stats.wc_flushes++;
	v->stats.wc_batch_ops += n;
	if (n > v->stats.wc_max_batch)
		v->stats.wc_max_batch = n;
	return 0;
}

/* DEC04: a guest read with stores queued.  The stores are the first ops of the batch and the read
 * is the last one, so the kernel executes them in order and returns the value the guest must see:
 * the flush and the read cost one RWVC_REGS ioctl instead of two, and the ordering rule ("no read
 * may pass a queued store") is unchanged.  Caller holds `lock`; v->wc_n > 0.  Stats count only the
 * queued stores, exactly like vcu_wc_flush(). */
static int vcu_wc_flush_read(struct n2vcu *v, unsigned win, uint32_t off, uint32_t *out)
{
	unsigned n = v->wc_n, done = 0;
	struct rwvc_reg_op *o = &v->wc_ops[n];
	int rc;

	memset(o, 0, sizeof(*o));
	o->op = RWVC_OP_READ;
	o->window = (uint8_t)win;
	o->offset = off;
	v->wc_n = 0;
	rc = vcu_hw_regs(v, v->wc_ops, n + 1u, &done);
	if (rc || done != n + 1u) {
		v->stats.wc_errors++;
		return rc ? rc : -EIO;
	}
	v->stats.wc_flushes++;
	v->stats.wc_batch_ops += n;
	if (n > v->stats.wc_max_batch)
		v->stats.wc_max_batch = n;
	*out = o->value;
	return 0;
}

/* One register read through the back end. */
static int vcu_hw_read(struct n2vcu *v, unsigned win, uint32_t off, uint32_t *out)
{
	struct rwvc_reg_op o;
	unsigned done = 0;
	int rc;

	memset(&o, 0, sizeof(o));
	o.op = RWVC_OP_READ;
	o.window = (uint8_t)win;
	o.offset = off;
	rc = vcu_hw_regs(v, &o, 1, &done);
	if (rc)
		return rc;
	if (done != 1)
		return -EIO;
	*out = o.value;
	return 0;
}

static int vcu_hw_write(struct n2vcu *v, unsigned win, uint32_t off, uint32_t value, uint32_t mask)
{
	struct rwvc_reg_op o;
	unsigned done = 0;
	int rc;

	memset(&o, 0, sizeof(o));
	o.op = (mask == 0xffffffffu) ? RWVC_OP_WRITE : RWVC_OP_WRITE_MASK;
	o.window = (uint8_t)win;
	o.offset = off;
	o.value = value;
	o.mask = mask;
	rc = vcu_hw_regs(v, &o, 1, &done);
	if (rc)
		return rc;
	return done == 1 ? 0 : -EIO;
}

/* ------------------------------------------------------------------ guest MMIO hooks */

/* Byte lane mask for `bytes` (1..4); the ternary keeps 8*bytes from ever reaching 32 in a shift. */
static uint32_t vcu_lane_mask(unsigned bytes)
{
	return bytes >= 4 ? 0xffffffffu : ((1u << (8u * bytes)) - 1u);
}

/*
 * A guest load/store to a mapped register window.  Aligned 32-bit accesses are the only kind the
 * kernel's own RWVC_REGS accepts (offsets must be 4-aligned, VCODEC_HW.md section 7) and the only
 * kind the vendor code issues; sub-word and (UNPREDICTABLE on real Device memory) unaligned
 * 32-bit accesses are nevertheless defined here as a little-endian byte access to the register
 * words, split across two words when they straddle one, so a stray LDRB cannot read garbage.
 */
static uint32_t vcu_mmio_read(void *ctx, uint32_t addr, unsigned bytes)
{
	struct n2vcu_map *m = (struct n2vcu_map *)ctx;
	struct n2vcu *v = m->vcu;
	uint32_t phys = m->phys + (addr - m->guest);
	uint32_t base = phys & ~3u;
	uint32_t sh = phys & 3u;
	uint32_t off = 0, w0 = 0, w1 = 0, value;
	unsigned win = 0, win1 = 0;
	uint32_t off1 = 0;
	int rc;

	if (bytes == 0 || bytes > 4)
		bytes = 4;
	if (!vcu_reg_lookup(base, &win, &off)) {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		return 0;
	}
	pthread_mutex_lock(&v->lock);
	/* DEC04: a read must observe every store the guest issued before it.  When stores are queued
	 * they travel in the same RWVC_REGS batch, ahead of the read: one ioctl, same order. */
	if (v->wc_n) {
		rc = vcu_wc_flush_read(v, win, off, &w0);
	} else {
		rc = vcu_hw_read(v, win, off, &w0);
	}
	if (rc != 0) {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		pthread_mutex_unlock(&v->lock);
		return 0;
	}
	if (sh + bytes > 4 && vcu_reg_lookup(base + 4u, &win1, &off1) && win1 == win)
		rc = vcu_hw_read(v, win1, off1, &w1);
	if (rc == 0) {
		v->stats.mmio_reads++;
		if (sh + bytes <= 4) {
			value = (w0 >> (8u * sh)) & vcu_lane_mask(bytes);
		} else {
			uint64_t both = (uint64_t)w0 | ((uint64_t)w1 << 32);

			value = (uint32_t)(both >> (8u * sh)) & vcu_lane_mask(bytes);
		}
	} else {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		value = 0;
	}
	pthread_mutex_unlock(&v->lock);
	return value;
}

static void vcu_mmio_write(void *ctx, uint32_t addr, unsigned bytes, uint32_t value)
{
	struct n2vcu_map *m = (struct n2vcu_map *)ctx;
	struct n2vcu *v = m->vcu;
	uint32_t phys = m->phys + (addr - m->guest);
	uint32_t base = phys & ~3u;
	uint32_t sh = phys & 3u;
	uint32_t off = 0, off1 = 0, w0 = 0, w1 = 0;
	unsigned win = 0, win1 = 0;
	int rc, have1 = 0;

	if (bytes == 0 || bytes > 4)
		bytes = 4;
	if (!vcu_reg_lookup(base, &win, &off)) {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
		return;
	}
	pthread_mutex_lock(&v->lock);
	/* opus-decint: the larb SMI_LARB_SLP_CON words (larb5 0x1600d00c, larb4 0x1602e00c).  The kernel
	 * (nord2_vcodec.inc n2vc_vdec_allow[]) accepts a change of bit 0 only, but the vendor's release
	 * (vdec_hal release_larb_sleep_protect) stores the whole word it read back: 0x00010000, bit 16
	 * being the read-only SLP_PROT_RDY ack (DEC02 trace) -> EACCES on the phone, larb left in sleep
	 * protect, every later VDEC DMA stalls.  Send every store to these two words as a masked write of
	 * bit 0: the same register effect on the hardware, and accepted by the kernel. */
	if (win == RWVC_WIN_VDEC_ALL && (off == 0x0d00cu || off == 0x2e00cu)) {
		uint32_t cur = 0, val;

		if (v->wc_n && vcu_wc_flush(v) != 0) {
			v->stats.mmio_bad++;
			v->stats.last_bad_mmio_addr = phys;
			pthread_mutex_unlock(&v->lock);
			return;
		}
		val = value;
		if (bytes != 4 || sh != 0) {
			/* sub-word store: only the lane holding bit 0 can change bit 0 */
			rc = sh == 0 ? 0 : vcu_hw_read(v, win, off, &cur);
			val = sh == 0 ? value : cur;
		} else {
			rc = 0;
		}
		if (rc == 0)
			rc = vcu_hw_write(v, win, off, val & 1u, 1u);
		if (rc == 0) {
			v->stats.mmio_writes++;
			v->stats.larb_slp_writes++;
		} else {
			v->stats.mmio_bad++;
			v->stats.last_bad_mmio_addr = phys;
		}
		pthread_mutex_unlock(&v->lock);
		return;
	}
	/* opus-decint: an aligned 32-bit store to a VDEC window is written as is, never read first.
	 * The read-modify-write below was harmless for VENC (kept unchanged for the working encode
	 * path), but decoder registers include read-sensitive ones (the VLD barrel-shifter read ports,
	 * status/clear words): a read the guest never issued may change hardware state. */
	if (!v->wc_on && bytes == 4 && sh == 0 && win != RWVC_WIN_VENC && win != RWVC_WIN_VENC_C1) {
		if (v->wc_n && vcu_wc_flush(v) != 0)
			rc = -EIO;
		else
			rc = vcu_hw_write(v, win, off, value, 0xffffffffu);
		if (rc == 0)
			v->stats.mmio_writes++;
		else {
			v->stats.mmio_bad++;
			v->stats.last_bad_mmio_addr = phys;
		}
		pthread_mutex_unlock(&v->lock);
		return;
	}
	/* DEC04: an aligned 32-bit store is exactly one register word, so it needs neither the
	 * read-modify-write below nor a round trip of its own: queue it.  A read, GCE list, power
	 * change, close or the 256-op cap flushes the queue first, so guest order is preserved. */
	if (v->wc_on && bytes == 4 && sh == 0) {
		struct rwvc_reg_op *o;

		if (v->wc_n == VCU_WC_MAX_OPS) {
			v->stats.wc_forced++;
			(void)vcu_wc_flush(v);   /* error -> wc_errors; the new store still queues */
		}
		o = &v->wc_ops[v->wc_n++];
		memset(o, 0, sizeof(*o));
		o->op = RWVC_OP_WRITE;
		o->window = (uint8_t)win;
		o->offset = off;
		o->value = value;
		o->mask = 0xffffffffu;
		v->stats.wc_writes++;
		v->stats.mmio_writes++;
		pthread_mutex_unlock(&v->lock);
		return;
	}
	/* DEC04: a sub-word or unaligned store needs the current word(s); flush first so its
	 * read-modify-write cannot overtake an earlier queued store. */
	if (v->wc_n) {
		if (vcu_wc_flush(v) != 0) {
			v->stats.mmio_bad++;
			v->stats.last_bad_mmio_addr = phys;
			pthread_mutex_unlock(&v->lock);
			return;
		}
	}
	rc = vcu_hw_read(v, win, off, &w0);
	if (rc == 0 && sh + bytes > 4) {
		if (vcu_reg_lookup(base + 4u, &win1, &off1) && win1 == win) {
			rc = vcu_hw_read(v, win1, off1, &w1);
			have1 = (rc == 0);
		} else {
			have1 = 0;   /* the next word belongs to no window: drop those bytes */
		}
	}
	if (rc == 0) {
		if (sh + bytes <= 4) {
			uint32_t msk = vcu_lane_mask(bytes) << (8u * sh);

			w0 = (w0 & ~msk) | ((value << (8u * sh)) & msk);
			rc = vcu_hw_write(v, win, off, w0, 0xffffffffu);
		} else {
			uint64_t cur = (uint64_t)w0 | ((uint64_t)w1 << 32);
			uint64_t msk = (uint64_t)vcu_lane_mask(bytes) << (8u * sh);
			uint64_t val = (uint64_t)value << (8u * sh);

			cur = (cur & ~msk) | (val & msk);
			rc = vcu_hw_write(v, win, off, (uint32_t)cur, 0xffffffffu);
			if (rc == 0 && have1)
				rc = vcu_hw_write(v, win1, off1, (uint32_t)(cur >> 32), 0xffffffffu);
		}
	}
	if (rc == 0)
		v->stats.mmio_writes++;
	else {
		v->stats.mmio_bad++;
		v->stats.last_bad_mmio_addr = phys;
	}
	pthread_mutex_unlock(&v->lock);
}

/* ------------------------------------------------------------------ DMA bookkeeping */

/* Guest address of the byte at hardware address `pa`, or 0.  Our own table first (it knows the
 * base and the size), then V41's registry for buffers this model did not allocate. */
static uint32_t vcu_dma_guest(struct n2vcu *v, uint32_t pa, uint32_t bytes)
{
	unsigned i;

	for (i = 0; i < VCU_MAX_ALLOCS; i++) {
		struct n2vcu_alloc *a = &v->alloc[i];

		if (a->used && pa >= a->pa && (uint64_t)pa + bytes <= (uint64_t)a->pa + a->bytes)
			return a->guest + (pa - a->pa);
	}
	{
		uint32_t handle = 0, guest = 0, len = 0;

		if (n2vc_dma_lookup_pa(pa, &handle, &guest, &len) == 0 && guest)
			return guest;
	}
	return 0;
}

static void vcu_alloc_release(struct n2vcu *v, struct n2vcu_alloc *a)
{
	if (!a->used)
		return;
	n2vc_dma_guest_unmap(&v->hw, v->mem, a->guest);
	v->hw.dma_free(v->hw.ctx, a->handle);
	memset(a, 0, sizeof(*a));
}

static void vcu_free_all(struct n2vcu *v)
{
	unsigned i;

	for (i = 0; i < VCU_MAX_ALLOCS; i++)
		if (v->alloc[i].used)
			vcu_alloc_release(v, &v->alloc[i]);
}

static struct n2vcu_gce *vcu_gce_find(struct n2vcu *v, uint64_t handle, int create)
{
	unsigned i, free_slot = VCU_MAX_GCE;

	for (i = 0; i < VCU_MAX_GCE; i++) {
		if (v->gce[i].used && v->gce[i].handle == handle)
			return &v->gce[i];
		if (!v->gce[i].used && free_slot == VCU_MAX_GCE)
			free_slot = i;
	}
	if (!create || free_slot == VCU_MAX_GCE)
		return NULL;
	memset(&v->gce[free_slot], 0, sizeof(v->gce[free_slot]));
	v->gce[free_slot].used = 1;
	v->gce[free_slot].handle = handle;
	return &v->gce[free_slot];
}

/* ------------------------------------------------------------------ GCE execution
 *
 * vcu_set_gce_cmd() (mtk_vcu.c:695-800) turns every command into CMDQ instructions; here the
 * register writes/polls become rwvc_reg_op entries executed in order by hw->regs().  A command
 * whose address the kernel would reject is logged and skipped there, so it is skipped here too
 * (and counted), not turned into an error.
 */
struct vcu_gce_run {
	struct n2vcu *v;
	struct n2a32_cpu *c;
	uint32_t codec_type;
	unsigned batch;
	uint32_t spr;
};

static int gce_flush(struct vcu_gce_run *g)
{
	struct n2vcu *v = g->v;
	unsigned n = g->batch, done = 0;
	int rc;

	if (n == 0)
		return 0;
	g->batch = 0;
	rc = vcu_hw_regs(v, v->ops, n, &done);
	if (rc) {
		v->stats.gce_errors++;
		return rc;
	}
	if (done != n) {
		v->stats.gce_errors++;
		return -EIO;
	}
	return 0;
}

static int gce_append(struct vcu_gce_run *g, uint8_t op, unsigned win, uint32_t off,
		      uint32_t value, uint32_t mask)
{
	struct rwvc_reg_op *o;
	int rc;

	if (g->batch == RWVC_REG_MAX_OPS) {
		rc = gce_flush(g);
		if (rc)
			return rc;
	}
	/* opus-decint: the larb SLP_CON words take bit 0 only (see vcu_mmio_write) */
	if (win == RWVC_WIN_VDEC_ALL && (off == 0x0d00cu || off == 0x2e00cu) &&
	    (op == RWVC_OP_WRITE || op == RWVC_OP_WRITE_MASK)) {
		value = (op == RWVC_OP_WRITE_MASK ? (value & mask) : value) & 1u;
		mask = 1u;
		op = RWVC_OP_WRITE_MASK;
	}
	o = &g->v->ops[g->batch++];
	memset(o, 0, sizeof(*o));
	o->op = op;
	o->window = (uint8_t)win;
	o->offset = off;
	o->value = value;
	o->mask = mask;
	return 0;
}

/* A read whose result the command stream can observe: flush first, then read alone. */
static int gce_read_one(struct vcu_gce_run *g, unsigned win, uint32_t off, uint32_t *out)
{
	int rc = gce_flush(g);

	if (rc)
		return rc;
	rc = vcu_hw_read(g->v, win, off, out);
	if (rc)
		g->v->stats.gce_errors++;
	else
		g->spr = *out;
	return rc;
}

/* Wait-for-event -> hw->wait_irq.  The kernel maps the event id to a CMDQ event number through
 * the device tree (mtk_vcu.c:2453-2520) and programs cmdq_pkt_wfe(); the model maps the event
 * class to the interrupt line the block raises (redwood_vcodec.h:30, VCODEC_HW.md section 0). */
static unsigned gce_event_irq(uint64_t event, uint32_t codec_type)
{
	if (event < (uint64_t)VDEC_EVENT_COUNT)
		return (event >= (uint64_t)VDEC_LAT_EVENT_0) ? RWVC_IRQ_VDEC_LAT : RWVC_IRQ_VDEC;
	if (event == (uint64_t)VENC_EOF_C1)
		return RWVC_IRQ_VENC_C1;
	if (event >= (uint64_t)VENC_EOF)
		return RWVC_IRQ_VENC;
	return codec_type == VCU_VDEC ? RWVC_IRQ_VDEC : RWVC_IRQ_VENC;
}

static int gce_cmd_read(struct vcu_gce_run *g, uint64_t addr)
{
	unsigned win = 0;
	uint32_t off = 0, value = 0;

	if (addr > 0xffffffffull || !vcu_reg_lookup((uint32_t)addr, &win, &off)) {
		g->v->stats.gce_skipped++;
		g->v->stats.last_skipped_cmd_addr = addr;
		return 0;
	}
	return gce_read_one(g, win, off, &value);
}

static int gce_cmd_write(struct vcu_gce_run *g, uint64_t addr, uint64_t data, uint32_t mask)
{
	unsigned win = 0;
	uint32_t off = 0;

	/* vcu_check_reg_base(): < 1 GiB and inside a map window, else pr_info + skip. */
	if (addr >= 0x40000000ull || !vcu_in_map_base(addr, 4) ||
	    !vcu_reg_lookup((uint32_t)addr, &win, &off)) {
		g->v->stats.gce_skipped++;
		g->v->stats.last_skipped_cmd_addr = addr;
		return 0;
	}
	return gce_append(g, mask == 0xffffffffu ? RWVC_OP_WRITE : RWVC_OP_WRITE_MASK, win, off,
			  (uint32_t)data, mask);
}

static int gce_cmd_poll_reg(struct vcu_gce_run *g, uint64_t addr, uint64_t data, uint32_t mask)
{
	unsigned win = 0;
	uint32_t off = 0;

	if (addr >= 0x40000000ull || !vcu_in_map_base(addr, 4) ||
	    !vcu_reg_lookup((uint32_t)addr, &win, &off)) {
		g->v->stats.gce_skipped++;
		g->v->stats.last_skipped_cmd_addr = addr;
		return 0;
	}
	return gce_append(g, RWVC_OP_POLL, win, off, (uint32_t)data, mask);
}

static int gce_cmd_wait_event(struct vcu_gce_run *g, uint64_t event)
{
	struct n2vcu *v = g->v;
	uint32_t status = 0;
	unsigned irq;
	int rc;

	if (event >= (uint64_t)MTK_VCU_GCE_EVENT_MAX) {
		/* mtk_vcu.c:746-751: "got wrong eid", command dropped. */
		v->stats.gce_skipped++;
		return 0;
	}
	rc = gce_flush(g);
	if (rc)
		return rc;
	/* DEC04: a wait is an ordering point.  The queue is normally empty here (the GCE flush
	 * ioctl flushed at entry), but the rule belongs to the wait as well. */
	rc = vcu_wc_flush(v);
	if (rc)
		return rc;
	irq = gce_event_irq(event, g->codec_type);
	uint64_t tw = now_us_mono();
	rc = v->hw.wait_irq(v->hw.ctx, irq, v->irq_timeout_ms, &status);
	v->stats.gce_wait_us += now_us_mono() - tw;
	v->stats.gce_waits++;
	v->stats.last_irq_status = status;
	if (rc) {
		if (rc == -ETIMEDOUT)
			v->stats.gce_wait_timeouts++;
		v->stats.gce_errors++;
		return rc;
	}
	return 0;
}

/* CMD_MEM_MV: mtk_vcu.c:752-772.  Destination must be a PA page; the source may be a register. */
static int gce_cmd_mem_mv(struct vcu_gce_run *g, uint64_t src, uint64_t dst)
{
	struct n2vcu *v = g->v;
	struct n2a32_cpu *c = g->c;
	unsigned win = 0;
	uint32_t off = 0, value = 0, sguest = 0, dguest;
	int have = 0, rc;

	if (src <= 0xffffffffull && vcu_reg_lookup((uint32_t)src, &win, &off)) {
		rc = gce_read_one(g, win, off, &value);
		if (rc)
			return rc;
		have = 1;
	} else if (src <= 0xffffffffull && (sguest = vcu_dma_guest(v, (uint32_t)src, 4)) != 0) {
		value = ld32(c, sguest);
		have = 1;
	}
	dguest = dst <= 0xffffffffull ? vcu_dma_guest(v, (uint32_t)dst, 4) : 0;
	if (!have || !dguest) {
		v->stats.gce_skipped++;
		v->stats.last_skipped_cmd_addr = dst;
		return 0;
	}
	rc = gce_flush(g);
	if (rc)
		return rc;
	st32(c, dguest, value);
	return 0;
}

/* CMD_POLL_ADDR: a register poll needs no special handling, a PA poll is done on the guest
 * mirror with the model's bounded timeout (the kernel uses cmdq_pkt_poll_timeout with count ~0). */
static int gce_cmd_poll_addr(struct vcu_gce_run *g, uint64_t addr, uint64_t data, uint32_t mask)
{
	struct n2vcu *v = g->v;
	unsigned win = 0;
	uint32_t off = 0, guest;
	int64_t deadline;
	int rc;

	if (addr <= 0xffffffffull && vcu_reg_lookup((uint32_t)addr, &win, &off))
		return gce_append(g, RWVC_OP_POLL, win, off, (uint32_t)data, mask);
	guest = addr <= 0xffffffffull ? vcu_dma_guest(v, (uint32_t)addr, 4) : 0;
	if (!guest) {
		v->stats.gce_skipped++;
		v->stats.last_skipped_cmd_addr = addr;
		return 0;
	}
	rc = gce_flush(g);
	if (rc)
		return rc;
	deadline = now_ms() + (int64_t)v->poll_timeout_ms;
	for (;;) {
		uint32_t value = ld32(g->c, guest);

		if ((value & mask) == ((uint32_t)data & mask))
			return 0;
		if (__atomic_load_n(&g->c->stop_request, __ATOMIC_ACQUIRE)) {
			v->stats.gce_errors++;
			return -EINTR;
		}
		if (now_ms() >= deadline) {
			v->stats.gce_wait_timeouts++;
			v->stats.gce_errors++;
			return -ETIMEDOUT;
		}
		sleep_us(100);
	}
}

static int gce_run_list(struct n2vcu *v, struct n2a32_cpu *c, uint32_t cmds_addr, uint32_t cmd_cnt,
			uint32_t codec_type)
{
	struct vcu_gce_run g;
	uint32_t i;
	int rc = 0;

	memset(&g, 0, sizeof(g));
	g.v = v;
	g.c = c;
	g.codec_type = codec_type;

	for (i = 0; i < cmd_cnt; i++) {
		unsigned cmd = n2a32_ld8(c, cmds_addr + i);
		uint64_t addr = ld64(c, cmds_addr + 0x800u + 8u * i);
		uint64_t data = ld64(c, cmds_addr + 0x4800u + 8u * i);
		uint32_t mask = ld32(c, cmds_addr + 0x8800u + 4u * i);

		v->stats.gce_cmds++;
		switch (cmd) {
		case CMD_READ:
			rc = gce_cmd_read(&g, addr);
			break;
		case CMD_WRITE:
			rc = gce_cmd_write(&g, addr, data, mask);
			break;
		case CMD_POLL_REG:
			rc = gce_cmd_poll_reg(&g, addr, data, mask);
			break;
		case CMD_WAIT_EVENT:
			rc = gce_cmd_wait_event(&g, data);
			break;
		case CMD_MEM_MV:
			rc = gce_cmd_mem_mv(&g, addr, data);
			break;
		case CMD_POLL_ADDR:
			rc = gce_cmd_poll_addr(&g, addr, data, mask);
			break;
		case CMD_SEC_WRITE:
			/* Secure-world path (CONFIG_MTK_SEC_VIDEO_PATH_SUPPORT); out of scope. */
			v->stats.gce_skipped++;
			v->stats.last_skipped_cmd_addr = addr;
			break;
		default:
			/* "unknown GCE cmd", skipped (mtk_vcu.c:798-800). */
			v->stats.gce_skipped++;
			break;
		}
		if (rc)
			return rc;
	}
	return gce_flush(&g);
}

/*
 * VENC bitstream mirror refresh (opus-hevc's HIGH for the first phone encode).
 *
 * With joined headers (VENC_SET_PARAM_PREPEND_HEADER, the daemon's seq_hdr_mode=1) the vendor
 * library lets the hardware encode at the start of the bitstream buffer and then shifts the frame
 * up on the CPU: VENC_PrependHeader (libvcodec_utility.so 0x228d0) memmoves frame_bytes at the
 * buffer start (call 0x2295c), copies VPS/SPS/PPS in front and flushes the range - with no
 * invalidate before the memmove.  On Android the buffer is coherent DMA memory, so the CPU reads
 * what the encoder wrote.  In this model the guest sees a copy (n2vc_dma.c); without the refresh
 * below the memmove reads the stale copy and the daemon returns header + zeros/garbage for H.264
 * and HEVC alike.  mtk_vcu.c has no equivalent because its user process maps real DMA memory.
 *
 * The list names the bitstream base in the 0x064 write (VENC_PROTOCOL.md: every buffer register
 * carries pa >> 4; HEVC uses the same offset, HEVC_ENC_PROTOCOL.md section 2).  After the list has
 * run, copy from that hardware address to the end of its buffer back into the guest mirror,
 * device -> guest: exactly what a CPU reads from DRAM after a DMA write.  Only 0x064 is refreshed;
 * it is the buffer the library reads back on the CPU inside the same job.  Source, reference and
 * MV buffers are written by the CPU before the trigger, so their mirrors are already correct.
 */
static void vcu_venc_refresh_bitstream(struct n2vcu *v, struct n2a32_cpu *c, uint32_t cmds_addr,
				       uint32_t cmd_cnt)
{
	uint32_t i;

	for (i = 0; i < cmd_cnt; i++) {
		uint64_t addr, data;
		uint32_t pa, guest = 0, bytes = 0, off;
		unsigned j, win = 0;
		uint32_t reg = 0;

		if (n2a32_ld8(c, cmds_addr + i) != CMD_WRITE)
			continue;
		addr = ld64(c, cmds_addr + 0x800u + 8u * i);
		if (addr > 0xffffffffull || !vcu_reg_lookup((uint32_t)addr, &win, &reg))
			continue;
		if ((win != RWVC_WIN_VENC && win != RWVC_WIN_VENC_C1) || reg != 0x064u)
			continue;
		data = ld64(c, cmds_addr + 0x4800u + 8u * i);
		if (data > 0xffffffffull)
			continue;
		pa = (uint32_t)data << 4;   /* pa >> 4 in the register, VENC_PROTOCOL.md */
		/* This model's own allocation table knows base, span and guest mapping. */
		for (j = 0; j < VCU_MAX_ALLOCS; j++) {
			struct n2vcu_alloc *a = &v->alloc[j];

			if (!a->used || pa < a->pa)
				continue;
			off = pa - a->pa;
			if (off >= a->bytes)
				continue;
			guest = a->guest + off;
			bytes = a->bytes - off;
			break;
		}
		if (bytes == 0) {
			/* A buffer the AP registered outside this model (V41's n2vc_dma.c):
			 * lookup_pa reports the guest address of `pa` and the bytes left there. */
			uint32_t handle = 0;

			if (n2vc_dma_lookup_pa(pa, &handle, &guest, &bytes) != 0 || !guest || bytes == 0)
				continue;
		}
		if (n2vc_dma_sync(&v->hw, v->mem, guest, 0, bytes, 0) == 0) {
			v->stats.gce_bitstream_syncs++;
			v->stats.gce_bitstream_bytes += bytes;
		}
	}
}

/* ------------------------------------------------------------------ ioctl handlers */

static int vcu_set_object(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg)
{
	struct share_obj o;
	n2vcu_ipi_handler fn;
	void *fn_ctx;
	int non_ack = -1;

	get_share(c, arg, &o);
	if (o.id < MTK_IPI_VCU_INIT || o.id >= MTK_IPI_MAX) {
		/* mtk_vcu.c:1806-1812 */
		return -EINVAL;
	}
	if (o.len > MTK_VCU_SHARE_BUF_SIZE)
		o.len = MTK_VCU_SHARE_BUF_SIZE;   /* a bogus len must not make the handler overrun */

	pthread_mutex_lock(&v->lock);
	fn = v->fn[o.id];
	fn_ctx = v->fn_ctx[o.id];
	pthread_mutex_unlock(&v->lock);

	if (fn)
		non_ack = fn(fn_ctx, o.id, o.share_buf, o.len);
	if (fn && o.id > MTK_IPI_VCU_INIT && non_ack == 0) {
		/* mtk_vcu.c:1403-1410: the ACK the sender's vcu_ipi_send() waits for. */
		pthread_mutex_lock(&v->lock);
		v->stats.ipi_acks++;
		pthread_mutex_unlock(&v->lock);
	}
	put_share(c, arg, &o);   /* mtk_vcu.c:1817-1821 */
	pthread_mutex_lock(&v->lock);
	v->stats.set_object++;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

static int vcu_get_object(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg)
{
	struct share_obj req, out;
	unsigned inst;
	int64_t deadline;
	int rc = -ETIMEDOUT;

	get_share(c, arg, &req);
	inst = mtk_vcu_ipi_instance(req.id);

	deadline = now_ms() + (int64_t)v->get_timeout_ms;
	pthread_mutex_lock(&v->lock);
	v->inst[inst].dispatched = 1;   /* ipi_done, mtk_vcu.c:500 */
	for (;;) {
		if (v->inst[inst].got) {
			out = v->inst[inst].out;
			v->inst[inst].got = 0;   /* ipi_got = 0, mtk_vcu.c:516 */
			v->stats.get_object++;
			rc = 0;
			break;
		}
		if (v->abort || __atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE)) {
			v->stats.get_object_interrupted++;
			rc = -EINTR;
			break;
		}
		if (now_ms() >= deadline) {
			v->stats.get_object_timeouts++;
			rc = -ETIMEDOUT;
			break;
		}
		pthread_mutex_unlock(&v->lock);
		sleep_us(200);
		pthread_mutex_lock(&v->lock);
	}
	pthread_mutex_unlock(&v->lock);
	if (rc == 0)
		put_share(c, arg, &out);   /* copy_to_user, mtk_vcu.c:506-511 */
	return rc;
}

/* Store a log for the daemon's next GET_LOG_OBJECT (the body of mtk_vcu_write(), mtk_vcu.c:
 * 2120-2142).  Caller holds log_mu.  The kernel memcpy's strnlen(val, 1023)+1 bytes, forcing byte
 * 1023 to NUL (:2123), and leaves the rest of the buffer as it was; `len` is that strnlen(). */
static void vcu_log_store_locked(struct n2vcu *v, const char *text, size_t len)
{
	if (len > MTK_VCU_LOG_INFO_SIZE - 1)
		len = MTK_VCU_LOG_INFO_SIZE - 1;
	memcpy(v->log_info, text, len);
	v->log_info[len] = '\0';
	v->log_info[MTK_VCU_LOG_INFO_SIZE - 1] = '\0';
	v->log_got = 1;
	pthread_cond_broadcast(&v->log_cv);
}

/*
 * VCU_GET_LOG_OBJECT (mtk_vcu.c:521-545): wait_event_freezable(vdec_log_get_wq,
 * atomic_read(&vdec_log_got)) -> copy sizeof(struct log_test_nofuse) -> vdec_log_got = 0.
 * The kernel's wait has no timeout and neither has this one, but it wakes every 100 ms to notice
 * stop_request (wait_event_freezable returns -ERESTARTSYS to a signal) and immediately on
 * n2vcu_ap_set_log(), a device close (the file the blocked ioctl belongs to is gone) and
 * n2vcu_destroy()/n2vcu_abort(); all four return -EINTR.  `size` is the _IOC size field: the
 * kernel always copies 1024 bytes, the model clamps so a smaller guest object is not overrun.
 *
 * The flag is cleared after the guest copy, like :542.  Two racing readers (or a set_log that
 * lands between the copy and the clear) can therefore lose a log, exactly as the kernel's
 * unlocked copy_to_user/atomic_set pair can; vpud has one log thread.
 */
static int vcu_get_log_object(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg, uint32_t size)
{
	char buf[MTK_VCU_LOG_INFO_SIZE];
	uint32_t n = size < MTK_VCU_LOG_INFO_SIZE ? size : MTK_VCU_LOG_INFO_SIZE;
	unsigned gen;
	int waited = 0;
	int rc = 0;

	pthread_mutex_lock(&v->log_mu);
	v->log_waiters++;   /* n2vcu_destroy() waits for this to reach 0 */
	gen = v->log_wake;
	for (;;) {
		struct timespec ts;

		/* A log that is already pending is returned even if a stop is in progress: the
		 * kernel's wait_event_freezable tests the condition before the signal too. */
		if (v->log_got)
			break;
		if (v->destroying || v->log_abort || v->log_wake != gen ||
		    __atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE)) {
			rc = -EINTR;
			break;
		}
		if (!waited) {
			waited = 1;
			v->log_blocked++;   /* visible in n2vcu_stats() while still blocked */
		}
		cond_deadline_ms(&ts, 100);
		pthread_cond_timedwait(&v->log_cv, &v->log_mu, &ts);
	}
	if (rc == 0)
		memcpy(buf, v->log_info, sizeof(buf));
	pthread_mutex_unlock(&v->log_mu);

	if (rc == 0)
		st_bytes(c, arg, buf, n);   /* the kernel's copy_to_user(:535-540) */

	pthread_mutex_lock(&v->lock);
	if (rc == 0)
		v->stats.log_gets++;
	if (waited)
		v->stats.log_waits++;
	pthread_mutex_unlock(&v->lock);

	pthread_mutex_lock(&v->log_mu);
	if (rc == 0)
		v->log_got = 0;             /* mtk_vcu.c:542, after the copy */
	if (waited)
		v->log_blocked--;
	v->log_waiters--;
	pthread_mutex_unlock(&v->log_mu);
	return rc;
}

static int vcu_set_log_object(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg, uint32_t size)
{
	char buf[MTK_VCU_LOG_INFO_SIZE];
	uint32_t n = size < MTK_VCU_LOG_INFO_SIZE ? size : MTK_VCU_LOG_INFO_SIZE;

	memset(buf, 0, sizeof(buf));
	ld_bytes(c, arg, buf, n);
	pthread_mutex_lock(&v->log_mu);
	vcu_log_store_locked(v, buf, n);
	pthread_mutex_unlock(&v->log_mu);
	pthread_mutex_lock(&v->lock);
	v->stats.log_sets++;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

static int vcu_do_alloc(struct n2vcu *v, struct mem_obj *m, int is_pa)
{
	struct n2vcu_alloc *a = NULL;
	uint32_t handle = 0, pa = 0, guest = 0;
	unsigned i;
	int rc;

	if (m->len == 0 || m->len > VCU_ALLOC_MAX_BYTES) {   /* mtk_vcodec_mem.c:167-172 */
		v->stats.allocation_failures++;
		return -EINVAL;
	}
	for (i = 0; i < VCU_MAX_ALLOCS; i++) {
		if (!v->alloc[i].used) {
			a = &v->alloc[i];
			break;
		}
	}
	if (!a) {
		v->stats.allocation_failures++;
		return -EINVAL;
	}
	rc = v->hw.dma_alloc(v->hw.ctx, m->len, 0, &handle, &pa);
	if (rc) {
		v->stats.allocation_failures++;
		return -ENOMEM;
	}
	rc = n2vc_dma_guest_map(&v->hw, v->mem, handle, m->len, &guest);
	if (rc || guest == 0) {
		v->hw.dma_free(v->hw.ctx, handle);
		v->stats.allocation_failures++;
		return -ENOMEM;
	}
	memset(a, 0, sizeof(*a));
	a->used = 1;
	a->is_mva = !is_pa;
	a->handle = handle;
	a->pa = pa;
	a->bytes = m->len;
	a->guest = guest;

	/* iova = pa (the model has no IOMMU for these buffers) and va = the guest mapping; the
	 * kernel puts CODEC_MSK(vaddr) in va and 0 in the field it does not use. */
	m->iova = pa;
	m->pa = pa;
	m->va = guest;
	v->stats.allocations++;
	return 0;
}

static int vcu_alloc_ioctl(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg, int is_pa)
{
	struct mem_obj m;
	int rc;

	get_mem(c, arg, &m);
	pthread_mutex_lock(&v->lock);
	rc = vcu_do_alloc(v, &m, is_pa);
	pthread_mutex_unlock(&v->lock);
	if (rc) {
		/* mtk_vcu.c:1841-1853: -1 in every address field, then copy back. */
		m.iova = ~(uint64_t)0;
		m.pa = ~(uint64_t)0;
		m.va = ~(uint64_t)0;
	}
	put_mem(c, arg, &m);
	return rc;
}

static int vcu_free_ioctl(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg, int is_pa)
{
	struct mem_obj m;
	struct n2vcu_alloc *a = NULL;
	unsigned i;
	int rc = -EINVAL;

	get_mem(c, arg, &m);
	if (m.iova == 0 && m.va == 0)   /* mtk_vcu.c:1883-1887 */
		return -EINVAL;

	pthread_mutex_lock(&v->lock);
	for (i = 0; i < VCU_MAX_ALLOCS; i++) {
		struct n2vcu_alloc *t = &v->alloc[i];

		if (!t->used || t->is_mva == is_pa)
			continue;
		if (is_pa) {
			/* mtk_vcodec_mem.c:292-296: pa and va only (len is not compared). */
			if (t->pa == (uint32_t)m.pa && t->guest == (uint32_t)m.va) {
				a = t;
				break;
			}
		} else {
			/* mtk_vcodec_mem.c:252-256: va, iova and len. */
			if (t->pa == (uint32_t)m.iova && t->guest == (uint32_t)m.va &&
			    t->bytes == m.len) {
				a = t;
				break;
			}
		}
	}
	if (a) {
		vcu_alloc_release(v, a);
		m.iova = 0;
		m.pa = 0;
		m.va = 0;
		rc = 0;
		v->stats.frees++;
	} else {
		v->stats.free_failures++;
	}
	pthread_mutex_unlock(&v->lock);

	if (rc == 0)
		put_mem(c, arg, &m);   /* mtk_vcu.c:1901-1908 */
	return rc;
}

static int vcu_cache_ioctl(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg, int op)
{
	struct mem_obj m;
	int rc = -EINVAL;
	unsigned i;

	get_mem(c, arg, &m);
	pthread_mutex_lock(&v->lock);
	for (i = 0; i < VCU_MAX_ALLOCS; i++) {
		struct n2vcu_alloc *a = &v->alloc[i];
		uint64_t off64;
		uint32_t off;

		/* mtk_vcodec_mem.c:398-410: only the vb2 (MVA) buffers, and only a range inside
		 * one of them.  Written without additions of guest-controlled values so it cannot
		 * wrap. */
		if (!a->used || !a->is_mva)
			continue;
		if (m.iova < a->pa)
			continue;
		off64 = m.iova - a->pa;
		if (off64 > a->bytes || (uint64_t)m.len > (uint64_t)a->bytes - off64)
			continue;
		off = (uint32_t)off64;
		rc = n2vc_dma_sync(&v->hw, v->mem, a->guest, off, m.len,
				   (uint32_t)op == MTK_VCU_CACHE_FLUSH_BUFF);
		if (rc) {
			rc = -EINVAL;
			break;
		}
		v->stats.cache_syncs++;
		rc = 0;
		break;
	}
	pthread_mutex_unlock(&v->lock);
	if (rc == 0)
		put_mem(c, arg, &m);
	return rc;
}

static int vcu_cache_flush_all(struct n2vcu *v)
{
	unsigned i;

	pthread_mutex_lock(&v->lock);
	for (i = 0; i < VCU_MAX_ALLOCS; i++) {
		struct n2vcu_alloc *a = &v->alloc[i];

		if (!a->used || !a->is_mva)
			continue;
		if (n2vc_dma_sync(&v->hw, v->mem, a->guest, 0, a->bytes, 1) == 0)
			v->stats.cache_syncs++;
	}
	pthread_mutex_unlock(&v->lock);
	return 0;   /* mtk_vcu.c:1922-1925 */
}

static int vcu_gce_flush_ioctl(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg)
{
	struct gce_cmdq_obj q;
	struct n2vcu_gce *h;
	uint32_t cmds_addr, cmd_cnt;
	int rc, ret = 0;

	get_cmdq_obj(c, arg, &q);
	if (q.cmds_user_ptr == 0 || q.cmds_user_ptr > 0xffffffffull)
		return -EINVAL;   /* mtk_vcu.c:917-919 */
	cmds_addr = (uint32_t)q.cmds_user_ptr;
	if (q.codec_type >= VCU_CODEC_MAX)   /* mtk_vcu.c:925-931 */
		return -EINVAL;

	pthread_mutex_lock(&v->lock);
	if (q.core_id >= v->th_num[q.codec_type]) {
		pthread_mutex_unlock(&v->lock);
		return -EINVAL;
	}
	h = vcu_gce_find(v, q.gce_handle, 1);
	if (!h) {
		pthread_mutex_unlock(&v->lock);
		return -EINVAL;
	}
	cmd_cnt = ld32(c, cmds_addr + 0xe800u);
	if (cmd_cnt >= MTK_VCU_CMDQ_CMD_MAX) {   /* mtk_vcu.c:1036-1041 */
		cmd_cnt = MTK_VCU_CMDQ_CMD_MAX;
		ret = -EINVAL;
	}
	/* DEC04: every store the guest queued before this ioctl must land before the list's first
	 * command, and the list's own power-on must not overtake them. */
	rc = vcu_wc_flush(v);
	if (rc) {
		pthread_mutex_unlock(&v->lock);
		return rc;
	}
	v->stats.gce_flushes++;
	h->pending++;
	/* venc_encode_prepare: clocks, larb, VENC domain -- mtk_vcu.c:994-997 / :822: the kernel powers
	 * the encoder core when the first GCE job of that core is flushed and
	 * unprepares it in the flush callback when the job count drops to 0.  The daemon sends
	 * no VCU_IPIMSG_ENC_POWER_ON in GCE mode.  The model runs the list synchronously, so it
	 * is power on -> run -> power off around this one job.  A NULL hook is a back end without
	 * power control (tests). */
	/* opus-enc4k: phase times for vcodecd, and the optional hold (no per-list power cycle) */
	unsigned core = q.core_id ? 1u : 0u;
	uint64_t t_ph = now_us_mono();
	if (q.codec_type == VCU_VENC) {
		if (core) v->stats.venc_lists_c1++; else v->stats.venc_lists_c0++;
	}
	if (q.codec_type == VCU_VENC && v->hw.power && !(v->venc_hold && v->venc_held[core])) {
		(void)v->hw.power(v->hw.ctx, core ? RWVC_BLK_VENC_C1 : RWVC_BLK_VENC, 1);
		v->stats.venc_power_ops++;
		if (v->venc_hold)
			v->venc_held[core] = 1;
	}
	uint64_t t_run = now_us_mono();
	if (q.codec_type == VCU_VENC)
		v->stats.venc_power_on_us += t_run - t_ph;
	rc = gce_run_list(v, c, cmds_addr, cmd_cnt, q.codec_type);
	if (q.codec_type == VCU_VENC)
		vcu_venc_refresh_bitstream(v, c, cmds_addr, cmd_cnt);
	uint64_t t_off = now_us_mono();
	if (q.codec_type == VCU_VENC)
		v->stats.gce_run_us += t_off - t_run;
	if (q.codec_type == VCU_VENC && v->hw.power && !v->venc_hold) {
		(void)v->hw.power(v->hw.ctx, core ? RWVC_BLK_VENC_C1 : RWVC_BLK_VENC, 0);
		v->stats.venc_power_ops++;
		v->stats.venc_power_off_us += now_us_mono() - t_off;
	}
	h->pending--;
	/* The kernel's CMDQ callback runs for a failed/timed-out packet too
	 * (vcu_gce_timeout_callback, then vcu_gce_flush_callback), so the daemon's
	 * VCU_GCE_WAIT_CALLBACK always returns even when the list itself failed.  The first
	 * hardware error is still returned by this ioctl.  Errors are counted where they are
	 * detected (gce_flush/gce_read_one/gce_cmd_wait_event/gce_cmd_poll_addr). */
	h->flush_done++;
	pthread_mutex_unlock(&v->lock);
	return rc ? rc : ret;
}

static int vcu_gce_wait_ioctl(struct n2vcu *v, struct n2a32_cpu *c, uint32_t arg)
{
	struct gce_obj o;
	struct n2vcu_gce *h;
	int64_t deadline;
	int rc = -ETIMEDOUT;

	get_gce_obj(c, arg, &o);
	pthread_mutex_lock(&v->lock);
	h = vcu_gce_find(v, o.gce_handle, 0);
	if (!h) {   /* mtk_vcu.c:1126-1128 */
		pthread_mutex_unlock(&v->lock);
		return -EINVAL;
	}
	deadline = now_ms() + (int64_t)v->cb_timeout_ms;
	for (;;) {
		if (h->flush_done) {
			h->flush_done--;
			v->stats.gce_callbacks++;
			rc = 0;
			break;
		}
		if (v->abort || __atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE)) {
			rc = -EINTR;
			break;
		}
		if (now_ms() >= deadline) {
			v->stats.gce_wait_timeouts++;
			rc = -ETIMEDOUT;
			break;
		}
		pthread_mutex_unlock(&v->lock);
		sleep_us(200);
		pthread_mutex_lock(&v->lock);
	}
	pthread_mutex_unlock(&v->lock);
	return rc;
}

/* ------------------------------------------------------------------ mmap */

static uint32_t vcu_map_regs(struct n2vcu *v, uint32_t phys, uint32_t len)
{
	uint32_t span = (len + 0xfffffu) & ~0xfffffu;
	uint32_t cand;

	for (cand = VCU_MMIO_FIRST; cand < VCU_MMIO_LAST; cand += 0x100000u) {
		struct n2vcu_map *m = (struct n2vcu_map *)calloc(1, sizeof(*m));

		if (!m)
			return 0;
		m->vcu = v;
		m->phys = phys;
		m->bytes = len;
		m->hook.rd = vcu_mmio_read;
		m->hook.wr = vcu_mmio_write;
		m->hook.ctx = m;
		if (n2a32_mem_mmio(v->mem, cand, span, &m->hook) == 0) {
			m->guest = cand;
			m->next = v->maps;
			v->maps = m;
			return cand;
		}
		free(m);
	}
	return 0;
}

static uint32_t vcu_mmap_clean_iova(struct n2vcu *v, uint64_t iova, uint32_t len)
{
	/* The model's pa/iova are 32 bits (the hardware address registers are); truncating a 64-bit
	 * iova would alias a different buffer. */
	if (iova > 0xffffffffull)
		return 0;
	return vcu_dma_guest(v, (uint32_t)iova, len);
}

static uint32_t vcu_mmap_ioctl(struct n2vcu *v, uint32_t len, uint64_t offset)
{
	uint32_t guest = 0;
	uint32_t pa;

	if (len == 0 || len > VCU_ALLOC_MAX_BYTES)
		return 0;

	/* The kernel's first branch: after a VCU_PA_ALLOCATION the next mmap is a PA mapping
	 * (mtk_vcu.c:1668-1684 with map_buf_pa set at :1873). */
	if (v->map_buf_pa_valid) {
		v->map_buf_pa_valid = 0;
		if (offset <= 0xffffffffull && (pa = (uint32_t)offset) != 0)
			guest = vcu_dma_guest(v, pa, len);
		if (guest)
			return guest;
		/* fall through: the offset may still name a window or a shmem range */
	}

	if (offset < 0x40000000ull) {
		if (!vcu_in_map_base(offset, len))
			return 0;
		return vcu_map_regs(v, (uint32_t)offset, len);
	}
	/* Every range test is vcu_span_contains(): offset - BASE, never offset + len, so an offset
	 * near UINT64_MAX cannot wrap into a range (R1 finding F14).  The guest address the DMEM
	 * offsets produce stays inside the DMEM because len <= RANGE - d. */
	if (vcu_span_contains(VCU_SHMEM_ALLOC_BASE, VCU_SHMEM_RANGE, offset, len))
		return v->dmem_guest + (uint32_t)(offset - VCU_SHMEM_ALLOC_BASE);
	if (vcu_span_contains(VCU_SHMEM_COMMIT_BASE, VCU_SHMEM_RANGE, offset, len))
		return v->dmem_guest + (uint32_t)(offset - VCU_SHMEM_COMMIT_BASE);
	/* MAP_SHMEM_MM_BASE / MAP_SHMEM_MM_CACHEABLE_BASE carry an iova, which is the pa here. */
	if (vcu_span_contains(VCU_SHMEM_MM_BASE, VCU_SHMEM_MM_RANGE, offset, len))
		return vcu_mmap_clean_iova(v, offset - VCU_SHMEM_MM_BASE, len);
	if (vcu_span_contains(VCU_SHMEM_MM_CACHEABLE_BASE, VCU_SHMEM_MM_RANGE, offset, len))
		return vcu_mmap_clean_iova(v, offset - VCU_SHMEM_MM_CACHEABLE_BASE, len);
	if (vcu_span_contains(VCU_SHMEM_PA_BASE, VCU_SHMEM_PA_RANGE, offset, len))
		return vcu_mmap_clean_iova(v, offset - VCU_SHMEM_PA_BASE, len);
	/* eVideoMemMAP() passes the bare physical address (vcu_usage.md section 3). */
	if (offset <= 0xffffffffull)
		return vcu_dma_guest(v, (uint32_t)offset, len);
	return 0;
}

/* ------------------------------------------------------------------ device callbacks */

static int vcu_dev_open(void *ctx, int flags)
{
	struct n2vcu *v = (struct n2vcu *)ctx;

	(void)flags;
	pthread_mutex_lock(&v->lock);
	v->open_count++;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

static int vcu_dev_close(void *ctx)
{
	struct n2vcu *v = (struct n2vcu *)ctx;

	pthread_mutex_lock(&v->lock);
	(void)vcu_wc_flush(v);   /* DEC04: a close must not lose stores the guest already issued */
	if (v->open_count)
		v->open_count--;
	if (v->open_count == 0)
		vcu_free_all(v);   /* mtk_vcu_mem_release(), mtk_vcu.c:1554-1594 */
	pthread_mutex_unlock(&v->lock);

	/* A close on any descriptor wakes a GET_LOG_OBJECT blocked on the same device: the file the
	 * blocked ioctl was issued on is gone, which the guest sees as -EINTR. */
	pthread_mutex_lock(&v->log_mu);
	v->log_wake++;
	pthread_cond_broadcast(&v->log_cv);
	pthread_mutex_unlock(&v->log_mu);
	return 0;
}

static int vcu_dev_ioctl(void *ctx, struct n2a32_cpu *c, uint32_t request, uint32_t guest_arg)
{
	struct n2vcu *v = (struct n2vcu *)ctx;
	int rc;

	/* DEC04: an ioctl is an ordering point between queued guest stores and anything the handler
	 * (or the AP handler that SET_OBJECT runs) may read or program.  The flush error is counted
	 * but does not fail an ioctl that has nothing to do with registers. */
	pthread_mutex_lock(&v->lock);
	(void)vcu_wc_flush(v);
	pthread_mutex_unlock(&v->lock);

	switch (request) {
	case MTK_VCU_SET_OBJECT:
		return vcu_set_object(v, c, guest_arg);
	case MTK_VCU_GET_OBJECT:
		return vcu_get_object(v, c, guest_arg);
	case MTK_VCU_MVA_ALLOCATION:
		return vcu_alloc_ioctl(v, c, guest_arg, 0);
	case MTK_VCU_MVA_FREE:
		return vcu_free_ioctl(v, c, guest_arg, 0);
	case MTK_VCU_PA_ALLOCATION:
		rc = vcu_alloc_ioctl(v, c, guest_arg, 1);
		if (rc == 0) {
			pthread_mutex_lock(&v->lock);
			v->map_buf_pa_valid = 1;   /* mtk_vcu.c:1871-1873 */
			pthread_mutex_unlock(&v->lock);
		}
		return rc;
	case MTK_VCU_PA_FREE:
		return vcu_free_ioctl(v, c, guest_arg, 1);
	case MTK_VCU_CACHE_FLUSH_ALL:
		return vcu_cache_flush_all(v);
	case MTK_VCU_CACHE_FLUSH_BUFF:
		return vcu_cache_ioctl(v, c, guest_arg, MTK_VCU_CACHE_FLUSH_BUFF);
	case MTK_VCU_CACHE_INVALIDATE_BUFF:
		return vcu_cache_ioctl(v, c, guest_arg, MTK_VCU_CACHE_INVALIDATE_BUFF);
	case MTK_VCU_GCE_SET_CMD_FLUSH:
		return vcu_gce_flush_ioctl(v, c, guest_arg);
	case MTK_VCU_GCE_WAIT_CALLBACK:
		return vcu_gce_wait_ioctl(v, c, guest_arg);
	case MTK_VCU_GET_LOG_OBJECT:
		return vcu_get_log_object(v, c, guest_arg, (request >> 16) & 0x3fffu);
	case MTK_VCU_SET_MMAP_TYPE:
		{
			struct map_obj o;

			get_map_obj(c, guest_arg, &o);
			pthread_mutex_lock(&v->lock);
			v->stats.map_types++;
			pthread_mutex_unlock(&v->lock);
			return 0;   /* the kernel has no case and returns -1; "accept" per TASK */
		}
	default:
		break;
	}
	/* nr 11/12 (the log ioctls): VCU_SET_LOG_OBJECT's size field cannot be derived from the
	 * kernel tree (struct log_test does not exist), so only type/dir/nr are checked. */
	if (((request >> 30) & 3u) == 1u && ((request >> 8) & 0xffu) == 0x76u) {
		switch (request & 0xffu) {
		case 11:
			return vcu_get_log_object(v, c, guest_arg, (request >> 16) & 0x3fffu);
		case 12:
			return vcu_set_log_object(v, c, guest_arg, (request >> 16) & 0x3fffu);
		default:
			break;
		}
	}
	pthread_mutex_lock(&v->lock);
	v->stats.unknown_requests++;
	pthread_mutex_unlock(&v->lock);
	return -EPERM;   /* "Unknown cmd", ret = -1, mtk_vcu.c:1966-1968 */
}

static uint32_t vcu_dev_mmap(void *ctx, struct n2a32_cpu *c, uint32_t len, int prot,
			     uint64_t offset, int *err)
{
	struct n2vcu *v = (struct n2vcu *)ctx;
	uint32_t guest;
	uint32_t req = n2shim_arg(c, 0) & ~(VCU_PAGE - 1u);

	(void)prot;
	pthread_mutex_lock(&v->lock);
	guest = vcu_mmap_ioctl(v, len, offset);
	if (guest) {
		/* mtk_vcu.c:1707-1711: the COMMIT mapping sets VCU_DMEM0_VMA = vma->vm_start.  The
		 * kernel sees the same pages at that user address; the model (one address space for
		 * daemon and AP) therefore translates against the address the guest asked for, which is
		 * where vpud placed its own codec tables (remap_shmem). */
		if (offset >= VCU_SHMEM_COMMIT_BASE &&
		    offset + len <= VCU_SHMEM_COMMIT_BASE + VCU_SHMEM_RANGE && req) {
			v->commit_vma = req;
			v->commit_seen = 1;
		}
		v->stats.mmaps++;
		if (err)
			*err = 0;
	} else {
		v->stats.mmap_failures++;
		if (err)
			*err = EINVAL;
	}
	pthread_mutex_unlock(&v->lock);
	return guest;
}

/* ------------------------------------------------------------------ public API */

struct n2vcu *n2vcu_create(struct n2a32_mem *mem, const struct n2vc_hw *hw)
{
	struct n2vcu *v;
	pthread_mutexattr_t attr;

	if (!mem || !hw || !hw->regs || !hw->dma_alloc || !hw->dma_free || !hw->wait_irq)
		return NULL;
	v = (struct n2vcu *)calloc(1, sizeof(*v));
	if (!v)
		return NULL;
	v->mem = mem;
	v->hw = *hw;
	v->ops = (struct rwvc_reg_op *)calloc(RWVC_REG_MAX_OPS, sizeof(struct rwvc_reg_op));
	if (!v->ops) {
		free(v);
		return NULL;
	}
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&v->lock, &attr);
	pthread_mutexattr_destroy(&attr);
	pthread_mutex_init(&v->log_mu, NULL);
	pthread_cond_init(&v->log_cv, NULL);

	v->th_num[VCU_VDEC] = 1;   /* mt6893.dts:6219-6220 */
	v->th_num[VCU_VENC] = 2;
	v->get_timeout_ms = 10000;
	v->irq_timeout_ms = 1000;
	v->poll_timeout_ms = 1000;
	v->cb_timeout_ms = 5000;

	v->dmem_bytes = VCU_DMEM_BYTES;
	v->dmem_guest = n2a32_mem_alloc(mem, VCU_DMEM_BYTES, VCU_PAGE);
	if (!v->dmem_guest) {
		free(v->ops);
		pthread_mutex_destroy(&v->lock);
		free(v);
		return NULL;
	}

	v->dev.path = "/dev/vcu";
	v->dev.open = vcu_dev_open;
	v->dev.ioctl = vcu_dev_ioctl;
	v->dev.mmap = vcu_dev_mmap;
	v->dev.close = vcu_dev_close;
	v->dev.ctx = v;
	v->dev_vpu = v->dev;
	v->dev_vpu.path = "/dev/vpu";
	return v;
}

void n2vcu_destroy(struct n2vcu *v)
{
	struct n2vcu_map *m;
	unsigned spins;

	if (!v)
		return;

	/* Wake a GET_LOG_OBJECT blocked on another thread and wait (bounded: 500 ms) for it to leave
	 * before the context is freed.  A caller must not destroy while a guest thread is stuck in
	 * the model; the wait is a safety net, not a licence. */
	pthread_mutex_lock(&v->log_mu);
	v->destroying = 1;
	v->log_wake++;
	pthread_cond_broadcast(&v->log_cv);
	for (spins = 0; v->log_waiters && spins < 500u; spins++) {
		pthread_mutex_unlock(&v->log_mu);
		sleep_us(1000);
		pthread_mutex_lock(&v->log_mu);
	}
	pthread_mutex_unlock(&v->log_mu);

	pthread_mutex_lock(&v->lock);
	(void)vcu_wc_flush(v);   /* DEC04: drain the guest-store queue before the context goes */
	vcu_free_all(v);
	m = v->maps;
	v->maps = NULL;
	pthread_mutex_unlock(&v->lock);
	while (m) {
		struct n2vcu_map *next = m->next;

		/* n2a32_mem has no unmap API: the MMIO pages stay registered in the address space,
		 * the hook context would dangle, so the caller must destroy the memory last.  The
		 * record is freed here; nothing may touch that guest range afterwards. */
		free(m);
		m = next;
	}
	free(v->ops);
	pthread_cond_destroy(&v->log_cv);
	pthread_mutex_destroy(&v->log_mu);
	pthread_mutex_destroy(&v->lock);
	free(v);
}

const struct n2vfs_dev *n2vcu_dev(struct n2vcu *v) { return &v->dev; }
const struct n2vfs_dev *n2vcu_dev_vpu(struct n2vcu *v) { return &v->dev_vpu; }

int n2vcu_ap_register(struct n2vcu *v, int ipi_id, n2vcu_ipi_handler handler, void *ctx)
{
	if (!v || !handler || ipi_id < MTK_IPI_VCU_INIT || ipi_id >= MTK_IPI_MAX)
		return -EINVAL;   /* vcu_ipi_register(), mtk_vcu.c:355-368 */
	pthread_mutex_lock(&v->lock);
	v->fn[ipi_id] = handler;
	v->fn_ctx[ipi_id] = ctx;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int n2vcu_ap_send(struct n2vcu *v, int ipi_id, const void *buf, uint32_t len)
{
	struct n2vcu_inst *in;
	unsigned inst;

	if (!v || !buf || ipi_id <= MTK_IPI_VCU_INIT || ipi_id >= MTK_IPI_MAX ||
	    len > MTK_VCU_SHARE_BUF_SIZE)
		return -EINVAL;   /* vcu_ipi_send(), mtk_vcu.c:388-400 */
	inst = mtk_vcu_ipi_instance(ipi_id);
	in = &v->inst[inst];
	pthread_mutex_lock(&v->lock);
	if (in->got)
		v->stats.ipi_overwritten++;   /* the kernel overwrites user_obj */
	in->out.id = ipi_id;
	in->out.len = len;
	memcpy(in->out.share_buf, buf, len);
	in->got = 1;
	in->dispatched = 0;
	v->stats.ipi_queued++;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int n2vcu_ap_pending(struct n2vcu *v, int ipi_id)
{
	int got;

	if (!v || ipi_id < MTK_IPI_VCU_INIT || ipi_id >= MTK_IPI_MAX)
		return 0;
	pthread_mutex_lock(&v->lock);
	got = v->inst[mtk_vcu_ipi_instance(ipi_id)].got;
	pthread_mutex_unlock(&v->lock);
	return got;
}

int n2vcu_ap_set_log(struct n2vcu *v, const char *text)
{
	size_t n;

	if (!v || !text)
		return -EINVAL;
	n = strnlen(text, MTK_VCU_LOG_INFO_SIZE - 1);   /* mtk_vcu.c:2121-2123 */
	pthread_mutex_lock(&v->log_mu);
	vcu_log_store_locked(v, text, n);
	pthread_mutex_unlock(&v->log_mu);
	pthread_mutex_lock(&v->lock);
	v->stats.log_sets++;
	pthread_mutex_unlock(&v->lock);
	return 0;
}

int n2vcu_ap_wait_dispatched(struct n2vcu *v, int ipi_id, unsigned timeout_ms)
{
	struct n2vcu_inst *in;
	int64_t deadline;
	int rc = -ETIMEDOUT;

	if (!v || ipi_id < MTK_IPI_VCU_INIT || ipi_id >= MTK_IPI_MAX)
		return -EINVAL;
	in = &v->inst[mtk_vcu_ipi_instance(ipi_id)];
	deadline = now_ms() + (int64_t)timeout_ms;
	pthread_mutex_lock(&v->lock);
	for (;;) {
		if (in->dispatched) {
			rc = 0;
			break;
		}
		if (v->abort) {
			rc = -EINTR;
			break;
		}
		if (now_ms() >= deadline)
			break;
		pthread_mutex_unlock(&v->lock);
		sleep_us(200);
		pthread_mutex_lock(&v->lock);
	}
	pthread_mutex_unlock(&v->lock);
	return rc;
}

void *n2vcu_map_dm_addr(struct n2vcu *v, uint32_t guest_addr)
{
	uint64_t off;
	void *p = NULL;

	if (!v || (!v->dmem_guest && !v->commit_seen) || guest_addr == 0)
		return NULL;
	pthread_mutex_lock(&v->lock);
	if (v->commit_seen) {
		/* DMEM0_VMA + d_off, mtk_vcu.c:1215-1225: the daemon's own pages at the commit VMA. */
		if (guest_addr >= v->commit_vma) {
			off = (uint64_t)guest_addr - v->commit_vma;
			if (off <= v->dmem_bytes)
				p = n2a32_ptr(v->mem, guest_addr);
		}
	} else if (guest_addr >= v->dmem_guest) {
		off = (uint64_t)guest_addr - v->dmem_guest;
		if (off <= v->dmem_bytes)   /* the kernel's d_off > len test, mtk_vcu.c:1216 */
			p = n2a32_ptr(v->mem, guest_addr);
	}
	pthread_mutex_unlock(&v->lock);
	return p;
}

int n2vcu_dmem(struct n2vcu *v, uint32_t *guest_addr, uint32_t *bytes)
{
	if (!v || !v->dmem_guest)
		return -ENODEV;
	if (guest_addr)
		*guest_addr = v->commit_seen ? v->commit_vma : v->dmem_guest;
	if (bytes)
		*bytes = v->dmem_bytes;
	return 0;
}

void n2vcu_set_get_timeout_ms(struct n2vcu *v, unsigned ms)
{
	if (v) v->get_timeout_ms = ms;
}

void n2vcu_set_irq_timeout_ms(struct n2vcu *v, unsigned ms)
{
	if (v) v->irq_timeout_ms = ms;
}

void n2vcu_set_poll_timeout_ms(struct n2vcu *v, unsigned ms)
{
	if (v) v->poll_timeout_ms = ms;
}

void n2vcu_set_callback_timeout_ms(struct n2vcu *v, unsigned ms)
{
	if (v) v->cb_timeout_ms = ms;
}

void n2vcu_set_gce_threads(struct n2vcu *v, unsigned vdec_threads, unsigned venc_threads)
{
	if (!v)
		return;
	v->th_num[VCU_VDEC] = vdec_threads;
	v->th_num[VCU_VENC] = venc_threads;
}

void n2vcu_set_write_combining(struct n2vcu *v, int on)
{
	if (!v)
		return;
	pthread_mutex_lock(&v->lock);
	if (!on)
		(void)vcu_wc_flush(v);   /* turning it off never drops what is already queued */
	v->wc_on = on != 0;
	pthread_mutex_unlock(&v->lock);
}

void n2vcu_abort(struct n2vcu *v)
{
	if (!v)
		return;
	pthread_mutex_lock(&v->lock);
	v->abort = 1;
	pthread_mutex_unlock(&v->lock);
	pthread_mutex_lock(&v->log_mu);
	v->log_abort = 1;
	pthread_cond_broadcast(&v->log_cv);
	pthread_mutex_unlock(&v->log_mu);
}

void n2vcu_set_venc_hold(struct n2vcu *v, int hold)
{
	if (!v)
		return;
	pthread_mutex_lock(&v->lock);
	v->venc_hold = hold ? 1 : 0;
	pthread_mutex_unlock(&v->lock);
	if (!hold)
		n2vcu_venc_release(v);
}

void n2vcu_venc_release(struct n2vcu *v)
{
	if (!v)
		return;
	pthread_mutex_lock(&v->lock);
	for (unsigned core = 0; core < 2u; core++) {
		if (v->venc_held[core] && v->hw.power) {
			uint64_t t = now_us_mono();
			(void)v->hw.power(v->hw.ctx, core ? RWVC_BLK_VENC_C1 : RWVC_BLK_VENC, 0);
			v->stats.venc_power_ops++;
			v->stats.venc_power_off_us += now_us_mono() - t;
		}
		v->venc_held[core] = 0;
	}
	pthread_mutex_unlock(&v->lock);
}

void n2vcu_venc_phases(struct n2vcu *v, uint64_t out[7])
{
	if (!v || !out)
		return;
	pthread_mutex_lock(&v->lock);
	out[0] = v->stats.venc_power_on_us;
	out[1] = v->stats.venc_power_off_us;
	out[2] = v->stats.venc_power_ops;
	out[3] = v->stats.gce_run_us;
	out[4] = v->stats.gce_wait_us;
	out[5] = v->stats.venc_lists_c0;
	out[6] = v->stats.venc_lists_c1;
	pthread_mutex_unlock(&v->lock);
}

void n2vcu_stats(const struct n2vcu *v, struct n2vcu_stats *out)
{
	struct n2vcu *m = (struct n2vcu *)v;

	if (!v || !out)
		return;
	pthread_mutex_lock(&m->lock);
	*out = v->stats;
	pthread_mutex_unlock(&m->lock);
	/* The gauge lives under the log mutex (taken after `lock`, never before it). */
	pthread_mutex_lock(&m->log_mu);
	out->log_blocked = m->log_blocked;
	pthread_mutex_unlock(&m->log_mu);
}
