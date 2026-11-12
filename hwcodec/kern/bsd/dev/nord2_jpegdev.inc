/* SPDX-License-Identifier: MIT */
/*
 * nord2_jpegdev.inc -- /dev/redwood-jpeg, the MT6893 hardware JPEG encoder as a real character device.
 *
 * Included by bsd/dev/sweet_touch.c after nord2_hwcodec.inc (the phone-verified 0298 model). It reuses that model's
 * n2j_* power/clock/reset/bus sequences unchanged: n2j_power(), n2j_bus() and n2j_off() are called, never copied, so
 * every register sequence here is the one already run on the phone (gpu/hwcodec/runs/q10hw1).
 *
 * Host builds define N2J_HOST and provide the kernel shims (dev_t, cdevsw, devfs, locks, msleep, copyin/copyout,
 * thread_call, ml_io_map_unmappable, mach_absolute_time, n2j_map/n2j_us/n2j_log): see kern/test_k01/t_jpegdev.c.
 *
 * ABI: include/redwood_jpeg.h (frozen). One job at a time, callers sleep interruptibly. The source picture is copied into
 * the 24 MiB source area at RW_CLAIM_JPEG_PA, re-laid out to the hardware's stride with the right and bottom padding
 * replicated; the JPEG is written by the block into the 16 MiB after it and copied out. The VEN domain is powered on for
 * the first job and off again RWJ_IDLE_OFF_MS after the last one, from a thread call.
 *
 * Memory: RW_CLAIM_JPEG_PA .. +RW_CLAIM_JPEG_BYTES (40 MiB: source 24 MiB, output 16 MiB), mapped lazily on the first job.
 * The DMA address registers are 32 bits wide and the larb7 ports run with the IOMMU bypassed (as the cameras do).
 */
#ifndef NORD2_JPEGDEV_INC
#define NORD2_JPEGDEV_INC 1

/*
 * The frozen ABI headers are in gpu/hwcodec/include. Host builds pass -Igpu/hwcodec/include; in the kernel tree the .inc
 * is included from a source file, so fall back to paths relative to this file (and to the kernel/b copy of it).
 */
#if defined(__has_include)
# if __has_include("redwood_jpeg.h")
#  include "redwood_jpeg.h"
#  include "redwood_vcodec.h"
# elif __has_include("../../../include/redwood_jpeg.h")
#  include "../../../include/redwood_jpeg.h"
#  include "../../../include/redwood_vcodec.h"
# elif __has_include("../../../../include/redwood_jpeg.h")
#  include "../../../../include/redwood_jpeg.h"
#  include "../../../../include/redwood_vcodec.h"
# else
#  error "redwood_jpeg.h not found: add -Igpu/hwcodec/include or keep this file under gpu/hwcodec/kern/bsd/dev"
# endif
#else
# include "redwood_jpeg.h"
# include "redwood_vcodec.h"
#endif

#ifdef N2J_HOST
/* kernel types and functions are supplied by kern/test_k01/t_jpegdev.c */
#else
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
#include <kern/clock.h>
#include <kern/thread_call.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#endif

#define N2JD_NODE        "redwood-jpeg"
#define N2JD_SRC_PA      ((uint32_t)RW_CLAIM_JPEG_PA)
#define N2JD_SRC_BYTES   0x01800000u            /* 24 MiB */
#define N2JD_DST_OFF     N2JD_SRC_BYTES
#define N2JD_DST_PA      (N2JD_SRC_PA + N2JD_SRC_BYTES)
#define N2JD_DST_BYTES   0x01000000u            /* 16 MiB */
#define N2JD_STALL_BYTES (N2JD_DST_BYTES - 0x1000u)
#define N2JD_POLL_US     100u
#define N2JD_POLLS       20000u                 /* 2 s, as the 0298 model */
#define N2JD_LOG_JOBS    8u                     /* per-job log lines after a power-on (bring-up verbosity) */

#if defined(__cplusplus)
#error "nord2_jpegdev.inc is C"
#endif
_Static_assert(RW_CLAIM_JPEG_BYTES == N2JD_SRC_BYTES + N2JD_DST_BYTES,
    "the 40 MiB JPEG claim in redwood_vcodec.h no longer matches source+output");
_Static_assert(RWJ_MAX_WIDTH <= 8192u && RWJ_MAX_HEIGHT <= 8192u, "header limits changed: check the 32-bit DMA registers");

/* hw-k02's VEN reference count, registered when its file is in the build (see the board note of K01). */
typedef int (*n2jd_ven_get_fn)(void);
typedef void (*n2jd_ven_put_fn)(void);

static struct {
	int major;
	bool made;
	bool busy;                              /* one hardware job at a time */
	uint32_t powered;                        /* this driver holds a VEN reference and the block is set up (atomic: INFO reads it) */
	bool mapped;
	unsigned opens;
	uint32_t jobs_done, jobs_failed;
	uint32_t last_hw_us;
	unsigned session_jobs;
	uint64_t last_job_ns;
	thread_call_t idle;
	n2jd_ven_get_fn ven_get;
	n2jd_ven_put_fn ven_put;
	vm_offset_t src, dst;                   /* the 40 MiB claim, mapped lazily */
} n2jd;

LCK_GRP_DECLARE(n2jd_grp, "nord2-jpeg");
LCK_MTX_DECLARE(n2jd_lock, &n2jd_grp);

static uint64_t
n2jd_now_ns(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns;
}

static uint32_t
n2jd_align16(uint32_t v)
{
	return (v + 15u) & ~15u;
}

static uint32_t
n2jd_align8(uint32_t v)
{
	return (v + 7u) & ~7u;
}

/* include/redwood_jpeg.h: nearest hardware code not above the requested quality, mtk_jpeg_core.c:1148 */
static uint32_t
n2jd_quality_code(uint32_t q)
{
	if (q >= 97) { return 0x10; }           /* Q97 */
	if (q >= 95) { return 3; }              /* Q95 */
	if (q >= 90) { return 7; }              /* Q90 */
	if (q >= 85) { return 2; }              /* Q85 */
	if (q >= 78) { return 6; }              /* Q78 */
	if (q >= 72) { return 0x0f; }           /* Q72 */
	if (q >= 60) { return 1; }              /* Q60 */
	if (q >= 52) { return 0x0a; }           /* Q52 */
	if (q >= 44) { return 0x0e; }           /* Q44 */
	if (q >= 38) { return 0; }              /* Q38 */
	return 4;                               /* Q30 */
}

/* a user range must be non-empty, start above 0 and not wrap the 64-bit address space */
static bool
n2jd_urange(uint64_t base, uint64_t off, uint64_t len)
{
	if (base == 0 || len == 0) { return false; }
	if (off > UINT64_MAX - base) { return false; }
	uint64_t a = base + off;
	return len <= UINT64_MAX - a;
}

struct n2jd_geom {
	uint32_t w, h, rows, hw_stride, line_bytes, chroma_stride, chroma_rows;
	bool yuyv;
	uint32_t src_bytes;
	uint32_t blk_num;
};

static bool
n2jd_geometry(const struct rwj_encode *j, struct n2jd_geom *g)
{
	g->w = j->width;
	g->h = j->height;
	g->yuyv = (j->format == RWJ_FMT_YUYV || j->format == RWJ_FMT_YVYU);
	g->hw_stride = g->yuyv ? 2u * n2jd_align16(j->width) : n2jd_align16(j->width);
	g->rows = g->yuyv ? n2jd_align8(j->height) : n2jd_align16(j->height);
	g->line_bytes = g->yuyv ? 2u * j->width : j->width;
	g->chroma_stride = j->chroma_stride ? j->chroma_stride : j->stride;
	g->chroma_rows = g->yuyv ? 0 : g->rows / 2u;
	/* mtk_jpeg_core.c:1188: 4:2:0 sixteen-pixel MCUs, 4:2:2 eight-line MCUs of four blocks */
	g->blk_num = g->yuyv
	    ? (n2jd_align16(j->width) >> 4) * (n2jd_align8(j->height) >> 3) * 4u - 1u
	    : (n2jd_align16(j->width) >> 4) * (n2jd_align16(j->height) >> 4) * 6u - 1u;
	uint64_t bytes = (uint64_t)g->hw_stride * g->rows * (g->yuyv ? 1u : 3u) / (g->yuyv ? 1u : 2u);
	if (bytes > N2JD_SRC_BYTES) { return false; }
	g->src_bytes = (uint32_t)bytes;
	return true;
}

/* every limit in include/redwood_jpeg.h; EINVAL except the documented ENOSPC of dst_capacity */
static int
n2jd_validate(const struct rwj_encode *j, struct n2jd_geom *g)
{
	if (j->width < 16u || j->height < 16u) { return EINVAL; }
	if (j->width > RWJ_MAX_WIDTH || j->height > RWJ_MAX_HEIGHT) { return EINVAL; }
	if ((j->width & 1u) || (j->height & 1u)) { return EINVAL; }
	if ((uint64_t)j->width * j->height > (uint64_t)RWJ_MAX_PIXELS) { return EINVAL; }
	if (j->format != RWJ_FMT_NV12 && j->format != RWJ_FMT_NV21 &&
	    j->format != RWJ_FMT_YUYV && j->format != RWJ_FMT_YVYU) { return EINVAL; }
	if (j->quality < 1u || j->quality > 100u) { return EINVAL; }
	bool yuyv = (j->format == RWJ_FMT_YUYV || j->format == RWJ_FMT_YVYU);
	if (j->stride < (yuyv ? 2u * j->width : j->width)) { return EINVAL; }
	uint32_t cs = j->chroma_stride ? j->chroma_stride : j->stride;
	if (!yuyv && cs < j->width) { return EINVAL; }
	/* the JPEG DRI interval is a 16-bit field; > 0xffff cannot be represented in the stream */
	if (j->restart_interval > 0xffffu) { return EINVAL; }
	if (j->flags & ~((uint32_t)RWJ_FLAG_EXIF_GAP)) { return EINVAL; }
	if (j->dst == 0 || j->src_y == 0 || (!yuyv && j->src_c == 0)) { return EINVAL; }
	uint64_t need = ((uint64_t)j->width * j->height) / 2u + 4096u;
	if ((uint64_t)j->dst_capacity < need) { return ENOSPC; }
	if (!n2jd_urange(j->dst, 0, j->dst_capacity)) { return EINVAL; }
	uint32_t line_bytes = yuyv ? 2u * j->width : j->width;
	if (!n2jd_urange(j->src_y, (uint64_t)(j->height - 1u) * j->stride, line_bytes)) { return EINVAL; }
	if (!yuyv && !n2jd_urange(j->src_c, (uint64_t)(j->height / 2u - 1u) * cs, j->width)) { return EINVAL; }
	if (!n2jd_geometry(j, g)) { return EINVAL; }             /* packed picture does not fit the 24 MiB source claim */
	return 0;
}

/*
 * Bring the block up: K02's VEN reference first when it exists, then the model's own sequences (idempotent: an already
 * powered domain is left powered, the larb and encoder are always configured for the four JPEG ports).
 */
static uint32_t
n2jd_power_on(void)
{
	if (__atomic_load_n(&n2jd.powered, __ATOMIC_RELAXED) && n2j.bus_ready && n2j_clocked()) { return 0; }   /* the domain, larb7 and JPEG clock are still ours */
	bool shared = n2jd.ven_get != NULL;
	uint32_t r;
	if (shared) {
		/* K02 owns the domain and leaves the JPGENC gate (bit 8) off; it releases protection and the larb gate */
		if (n2jd.ven_get() != 0) {
			__atomic_store_n(&n2jd.powered, 0u, __ATOMIC_RELEASE);
			return RWJ_E_POWER;
		}
		if (!n2j_domain_on()) {                          /* a contract violation from the provider: do not touch registers */
			n2jd.ven_put();
			__atomic_store_n(&n2jd.powered, 0u, __ATOMIC_RELEASE);
			return RWJ_E_POWER;
		}
		ni_wr(n2j.cg, 0x4, 1u << 8);                     /* vencsys SET: JPGENC clock on (bit 8 only) */
	} else {
		r = n2j_power();
		if (r != N2J_OK) {
			__atomic_store_n(&n2jd.powered, 0u, __ATOMIC_RELEASE);
			return r == N2J_E_NO_BUS ? RWJ_E_BUS : RWJ_E_POWER;
		}
	}
	r = n2j_bus();
	if (r != N2J_OK) {
		if (shared) {
			ni_wr(n2j.cg, 0x8, 1u << 8);                 /* gate our block before dropping the reference */
			n2jd.ven_put();
		} else {
			(void)n2j_off();
		}
		__atomic_store_n(&n2jd.powered, 0u, __ATOMIC_RELEASE);
		return r == N2J_E_NO_BUS ? RWJ_E_BUS : RWJ_E_POWER;
	}
	__atomic_store_n(&n2jd.powered, 1u, __ATOMIC_RELEASE);
	n2jd.session_jobs = 0;
	return 0;
}

static uint32_t
n2jd_power_off(void)
{
	if (n2jd.ven_put != NULL) {
		if (n2j_clocked()) {                     /* gate the JPEG block before K02's reference is dropped */
			ni_wr(n2j.cg, 0x8, 1u << 8);
		}
		n2jd.ven_put();                          /* K02 drops its reference; it powers off at zero */
		__atomic_store_n(&n2jd.powered, 0u, __ATOMIC_RELEASE);
		n2j.bus_ready = false;
		return 0;
	}
	uint32_t r = n2j_off();
	if (r == N2J_OK || !(ni_rd(n2j.spm, 0x16c) & N2J_PWR_STA)) { __atomic_store_n(&n2jd.powered, 0u, __ATOMIC_RELEASE); return 0; }
	return r == N2J_E_PROT_TIMEOUT ? RWJ_E_BUS : RWJ_E_POWER;
}

/* map the register windows and the 40 MiB claim once, on the first job; mapping touches no register */
static bool
n2jd_maps(void)
{
	if (n2jd.mapped) { return true; }
	n2j.spm = n2j_map(0x10006000, 0x1000, 1);
	n2j.infra = n2j_map(0x10001000, 0x1000, 1);
	n2j.top = n2j_map(0x10000000, 0x1000, 1);
	n2j.cg = n2j_map(0x17000000, 0x1000, 1);
	n2j.larb = n2j_map(0x17010000, 0x1000, 1);
	n2j.enc = n2j_map(0x17030000, 0x1000, 1);
	n2j.dispsmi = n2j_map(0x1411f000, 0x1000, 1);
	if (!n2j.spm || !n2j.infra || !n2j.top || !n2j.cg || !n2j.larb || !n2j.enc || !n2j.dispsmi) { return false; }
	n2j.buf = n2j_map(RW_CLAIM_JPEG_PA, N2JD_SRC_BYTES + N2JD_DST_BYTES, 0);
	if (!n2j.buf) { return false; }
	n2jd.src = n2j.buf;
	n2jd.dst = n2j.buf + N2JD_DST_OFF;
	n2jd.mapped = true;
	return true;
}

/*
 * Copy the user's picture into the packed source area: luma lines of `width` bytes (YUYV: 2 * width), the last pixel
 * replicated to the hardware stride, the last line replicated to the aligned height, chroma the same with U,V pairs.
 */
static int
n2jd_copy_src(const struct rwj_encode *j, const struct n2jd_geom *g)
{
	uint8_t *p = (uint8_t *)n2jd.src;
	uint32_t rs = g->hw_stride;

	for (uint32_t y = 0; y < g->h; y++) {
		uint8_t *d = p + (size_t)y * rs;
		int e = copyin((user_addr_t)(j->src_y + (uint64_t)y * j->stride), d, g->line_bytes);
		if (e) { return e; }
		if (g->yuyv) {
			for (uint32_t x = g->line_bytes; x < rs; x += 4u) { memcpy(d + x, d + g->line_bytes - 4u, 4); }
		} else {
			memset(d + g->line_bytes, d[g->line_bytes - 1u], rs - g->line_bytes);
		}
	}
	uint8_t *last = p + (size_t)(g->h - 1u) * rs;
	for (uint32_t y = g->h; y < g->rows; y++) { memcpy(p + (size_t)y * rs, last, rs); }
	if (!g->yuyv) {
		uint8_t *c = p + (size_t)rs * g->rows;
		uint32_t ch = g->h / 2u;
		for (uint32_t y = 0; y < ch; y++) {
			uint8_t *d = c + (size_t)y * rs;
			int e = copyin((user_addr_t)(j->src_c + (uint64_t)y * g->chroma_stride), d, g->w);
			if (e) { return e; }
			for (uint32_t x = g->w; x < rs; x += 2u) { memcpy(d + x, d + g->w - 2u, 2); }
		}
		uint8_t *clast = c + (size_t)(ch - 1u) * rs;
		for (uint32_t y = ch; y < g->chroma_rows; y++) { memcpy(c + (size_t)y * rs, clast, rs); }
	}
	return 0;
}

/* the last FF D9 in the last 64 bytes (the block rounds its byte count up to 64); 0: no EOI found */
static uint32_t
n2jd_cut(const uint8_t *o, uint32_t size)
{
	if (size < 4u) { return 0; }
	for (uint32_t k = 0; k < 64u && k + 2u <= size; k++) {
		uint32_t i = size - 2u - k;
		if (o[i] == 0xffu && o[i + 1u] == 0xd9u) { return i + 2u; }
	}
	return 0;
}

/*
 * One job. Called with n2jd.busy set and no lock held (the idle thread call sees busy and reschedules). Returns an errno;
 * j->out_status says what the hardware did (RWJ_E_*). j->out_bytes is 0 unless a complete JPEG was cut.
 */
static int
n2jd_run(struct rwj_encode *j, const struct n2jd_geom *g)
{
	j->out_bytes = 0;
	j->out_hw_us = 0;
	j->out_status = 0;
	if (!n2jd_maps()) {
		n2j_log("NORD2 JPEG: the register windows or the 40 MiB claim cannot be mapped\n");
		j->out_status = RWJ_E_BUS;
		return EIO;
	}
	int err = n2jd_copy_src(j, g);
	if (err) { return err; }
	n2j_cache(n2jd.src, g->src_bytes);

	uint32_t pr = n2jd_power_on();
	if (pr != 0) {
		n2j_log("NORD2 JPEG: power-on failed (%u)\n", pr);
		j->out_status = pr;
		return EIO;
	}
	uint32_t q = n2jd_quality_code(j->quality);
	uint32_t src_pa = N2JD_SRC_PA;
	uint32_t dst_pa = N2JD_DST_PA;
	vm_offset_t e = n2j.enc;

	n2j_reset();
	ni_wr(e, 0x154, (j->width << 16) | j->height);                          /* IMG_SIZE */
	ni_mask(e, 0x104, 0x18u, (uint32_t)j->format << 3);                     /* YUV format 0..3 */
	ni_wr(e, 0x10c, g->blk_num);                                            /* BLK_NUM */
	ni_wr(e, 0x17c, g->hw_stride);                                          /* IMG_STRIDE */
	ni_wr(e, 0x178, g->hw_stride);                                          /* STRIDE */
	ni_wr(e, 0x170, src_pa);                                                /* SRC_LUMA_ADDR */
	ni_wr(e, 0x174, src_pa + g->hw_stride * g->rows);                       /* SRC_CHROMA_ADDR */
	ni_wr(e, 0x138, 0);                                                     /* OFFSET_ADDR */
	ni_wr(e, 0x16c, dst_pa & 0xfu);                                         /* BYTE_OFFSET_MASK */
	ni_wr(e, 0x120, dst_pa & ~0xfu);                                        /* DST_ADDR0 */
	ni_wr(e, 0x128, (dst_pa + N2JD_STALL_BYTES) & ~0xfu);                   /* STALL_ADDR0 */
	ni_mask(e, 0x108, 0xffffu, q);                                          /* QUALITY low 16 bits */
	ni_mask(e, 0x104, 1u << 10, j->restart_interval ? 1u << 10 : 0);        /* restart enable */
	ni_wr(e, 0x150, j->restart_interval);                                   /* RST_MCU_NUM */
	ni_mask(e, 0x104, 1u << 5, (j->flags & RWJ_FLAG_EXIF_GAP) ? 1u << 5 : 0);
	uint32_t ctrl = (ni_rd(e, 0x104) | (1u << 2) | (1u << 0) | (1u << 20) | (1u << 29)) & ~(1u << 30);

	uint64_t t0 = mach_absolute_time();
	ni_wr(e, 0x104, ctrl);                                                  /* start */
	uint32_t st = 0;
	unsigned polls = 0;
	for (; polls < N2JD_POLLS; polls++) {
		st = ni_rd(e, 0x11c) & 0x13u;
		if (st) { break; }
		n2j_us(N2JD_POLL_US);
	}
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
	uint32_t us = ns / 1000u > UINT32_MAX ? UINT32_MAX : (uint32_t)(ns / 1000u);
	j->out_hw_us = us;
	if (st) { ni_wr(e, 0x11c, 0); }
	if (!st) {
		n2j_log("NORD2 JPEG: encode timeout after %u polls (%u us)\n", polls, us);
		n2j_reset();
		j->out_status = RWJ_E_TIMEOUT;
		return EIO;
	}
	if (!(st & 1u)) {
		n2j_reset();
		j->out_status = RWJ_E_STALL;
		return EIO;
	}
	uint32_t dma = ni_rd(e, 0x124);                                         /* DMA_ADDR0 */
	uint32_t base = ni_rd(e, 0x120);                                        /* DST_ADDR0 */
	if (dma < base || dma > N2JD_DST_PA + N2JD_STALL_BYTES) {
		j->out_status = RWJ_E_SIZE;
		return EIO;
	}
	uint32_t size = dma - base;
	if (size < 4u) {
		j->out_status = RWJ_E_SIZE;
		return EIO;
	}
	n2j_cache(n2jd.dst, size);
	uint8_t *o = (uint8_t *)n2jd.dst;
	if (o[0] != 0xffu || o[1] != 0xd8u) {
		n2j_log("NORD2 JPEG: the block did not write a JPEG start (%02x %02x)\n", o[0], o[1]);
		j->out_status = RWJ_E_SIZE;
		return EIO;
	}
	uint32_t cut = n2jd_cut(o, size);
	if (cut == 0) {
		n2j_log("NORD2 JPEG: no FF D9 in the last 64 bytes of %u\n", size);
		j->out_status = RWJ_E_SIZE;
		return EIO;
	}
	if (cut > j->dst_capacity) {
		n2j_log("NORD2 JPEG: %u bytes of JPEG do not fit the user's %u\n", cut, j->dst_capacity);
		j->out_status = RWJ_E_SIZE;
		return EIO;
	}
	j->out_bytes = cut;
	err = copyout(o, (user_addr_t)j->dst, cut);
	if (err) { return err; }
	if (n2jd.session_jobs < N2JD_LOG_JOBS) {
		n2jd.session_jobs++;
		n2j_log("NORD2 JPEG: %ux%u fmt %u q %u -> %u bytes in %u us\n", j->width, j->height, j->format, j->quality, cut, us);
	}
	return 0;
}

static void
n2jd_schedule_off(void)
{
	if (n2jd.idle == NULL) { return; }
	uint64_t dl;
	clock_interval_to_deadline(RWJ_IDLE_OFF_MS, NSEC_PER_MSEC, &dl);
	(void)thread_call_enter_delayed(n2jd.idle, dl);
}

/* RWJ_IDLE_OFF_MS after the last job, from the thread call; a job in progress makes it look again */
static void
n2jd_idle(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	lck_mtx_lock(&n2jd_lock);
	if (!__atomic_load_n(&n2jd.powered, __ATOMIC_ACQUIRE)) { lck_mtx_unlock(&n2jd_lock); return; }
	if (n2jd.busy) { n2jd_schedule_off(); lck_mtx_unlock(&n2jd_lock); return; }
	uint64_t now = n2jd_now_ns(), idle = (uint64_t)RWJ_IDLE_OFF_MS * NSEC_PER_MSEC;
	if (now < n2jd.last_job_ns || now - n2jd.last_job_ns < idle) { n2jd_schedule_off(); lck_mtx_unlock(&n2jd_lock); return; }
	uint32_t r = n2jd_power_off();
	if (r != 0) {
		n2j_log("NORD2 JPEG: idle power-off failed (%u), the domain is left as it is\n", r);
		n2jd_schedule_off();                     /* try again in RWJ_IDLE_OFF_MS, do not lie about the state */
	}
	lck_mtx_unlock(&n2jd_lock);
}

static int
n2jd_open(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	if (n2jd.made == false || minor(dev) != 0) { return ENXIO; }
	lck_mtx_lock(&n2jd_lock);
	n2jd.opens++;
	lck_mtx_unlock(&n2jd_lock);
	return 0;
}

static int
n2jd_close(dev_t dev, __unused int flags, __unused int type, __unused struct proc *p)
{
	if (n2jd.made == false || minor(dev) != 0) { return ENXIO; }
	lck_mtx_lock(&n2jd_lock);
	if (n2jd.opens) { n2jd.opens--; }
	lck_mtx_unlock(&n2jd_lock);
	return 0;                                       /* close needs nothing: the idle call owns the power-off */
}

static int
n2jd_info(caddr_t data)
{
	struct rwj_info info;
	memset(&info, 0, sizeof(info));
	info.abi_version = RWJ_ABI_VERSION;
	info.max_pixels = RWJ_MAX_PIXELS;
	lck_mtx_lock(&n2jd_lock);
	info.jobs_done = n2jd.jobs_done;
	info.jobs_failed = n2jd.jobs_failed;
	info.powered = __atomic_load_n(&n2jd.powered, __ATOMIC_ACQUIRE);
	info.last_hw_us = n2jd.last_hw_us;
	lck_mtx_unlock(&n2jd_lock);
	info.caps = 1u;                                 /* bit 0 encoder; the decoder is track J */
	return copyout(&info, (user_addr_t)(uintptr_t)data, sizeof(info)) != 0 ? EFAULT : 0;
}

static int
n2jd_ioctl(dev_t dev, unsigned long cmd, caddr_t data, __unused int fflag, __unused struct proc *p)
{
	uint32_t c = (uint32_t)cmd;
	if (n2jd.made == false || minor(dev) != 0) { return ENXIO; }
	if (data == NULL) { return EFAULT; }
	if (c == (uint32_t)RWJ_INFO) { return n2jd_info(data); }
	if (c == (uint32_t)RWJ_DECODE) { return ENOTSUP; }
	if (c != (uint32_t)RWJ_ENCODE) { return ENOTTY; }

	struct rwj_encode j;
	if (copyin((user_addr_t)(uintptr_t)data, &j, sizeof(j)) != 0) { return EFAULT; }
	struct n2jd_geom g;
	int err = n2jd_validate(&j, &g);
	if (err) { return err; }
	j.out_bytes = 0;
	j.out_hw_us = 0;
	j.out_status = 0;

	lck_mtx_lock(&n2jd_lock);
	while (n2jd.busy) {
		err = msleep(&n2jd.busy, &n2jd_lock, PZERO | PCATCH, "redwood-jpeg", NULL);
		if (err) {
			lck_mtx_unlock(&n2jd_lock);
			return err == EWOULDBLOCK ? EAGAIN : err;
		}
	}
	n2jd.busy = true;
	lck_mtx_unlock(&n2jd_lock);

	err = n2jd_run(&j, &g);

	lck_mtx_lock(&n2jd_lock);
	n2jd.busy = false;
	if (j.out_hw_us) { n2jd.last_hw_us = j.out_hw_us; }
	if (j.out_status == 0 && err == 0) { n2jd.jobs_done++; }
	else if (j.out_status == 0 && err != 0) { n2jd.jobs_failed++; }       /* source fault, copyout fault: no hardware failure */
	else { n2jd.jobs_failed++; }
	if (__atomic_load_n(&n2jd.powered, __ATOMIC_ACQUIRE)) {
		n2jd.last_job_ns = n2jd_now_ns();
		n2jd_schedule_off();
	}
	wakeup(&n2jd.busy);
	lck_mtx_unlock(&n2jd_lock);
	/* deliver out_bytes/out_status/out_hw_us even after a hardware failure; a faulting struct copyout is EFAULT */
	if (copyout(&j, (user_addr_t)(uintptr_t)data, sizeof(j)) != 0) { return err ? err : EFAULT; }
	return err;
}

#ifdef N2J_HOST
static const struct cdevsw n2jd_cdevsw = {
	.d_open = n2jd_open, .d_close = n2jd_close, .d_ioctl = n2jd_ioctl,
};
#else
static const struct cdevsw n2jd_cdevsw = {
	.d_open = n2jd_open, .d_close = n2jd_close,
	.d_read = eno_rdwrt, .d_write = eno_rdwrt, .d_ioctl = n2jd_ioctl,
	.d_stop = eno_stop, .d_reset = eno_reset, .d_ttys = NULL,
	.d_select = eno_select, .d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0,
};
#endif

/*
 * Called from nord2_input_init() (the integrator adds the call). No register is touched here: the node exists and the
 * first RWJ_ENCODE maps the windows, copies the picture in and powers the domain on.
 */
static int nord2_jpegdev_start(void) __attribute__((unused));
static int
nord2_jpegdev_start(void)
{
	if (n2jd.made) { return 0; }
	int major = cdevsw_add(-1, &n2jd_cdevsw);
	if (major < 0) {
		n2j_log("NORD2 JPEG: cdevsw_add failed (%d)\n", major);
		return EIO;
	}
	if (devfs_make_node(makedev(major, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "%s", N2JD_NODE) == NULL) {
		n2j_log("NORD2 JPEG: devfs_make_node failed\n");
		(void)cdevsw_remove(major, &n2jd_cdevsw);
		return EIO;
	}
	n2jd.idle = thread_call_allocate(n2jd_idle, NULL);
	if (n2jd.idle == NULL) {
		n2j_log("NORD2 JPEG: no thread call: the VEN domain stays on after the first job\n");
	}
	n2jd.major = major;
	n2jd.made = true;
	n2j_log("NORD2 JPEG: /dev/%s made (major %d, 0666), %u MiB source + %u MiB output at %#x\n",
	    N2JD_NODE, major, N2JD_SRC_BYTES >> 20, N2JD_DST_BYTES >> 20, (uint32_t)RW_CLAIM_JPEG_PA);
	return 0;
}

/*
 * hw-k02 registers its VEN reference count here (it calls this once after n2vc_ven_get/put exist). Without the hook the
 * driver powers the domain with the model's n2j_power()/n2j_off() itself; with it, get() is called per job and put() on
 * the idle timer, and this driver never powers the shared domain off.
 */
int
n2jd_share_ven(n2jd_ven_get_fn get, n2jd_ven_put_fn put)
{
	if (get == NULL || put == NULL) { return EINVAL; }
	lck_mtx_lock(&n2jd_lock);
	if (n2jd.busy) { lck_mtx_unlock(&n2jd_lock); return EBUSY; }
	n2jd.ven_get = get;
	n2jd.ven_put = put;
	lck_mtx_unlock(&n2jd_lock);
	return 0;
}

#endif /* NORD2_JPEGDEV_INC */
