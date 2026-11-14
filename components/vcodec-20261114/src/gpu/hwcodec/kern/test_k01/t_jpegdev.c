/* SPDX-License-Identifier: MIT */
/*
 * Host test of gpu/hwcodec/kern/bsd/dev/nord2_jpegdev.inc: /dev/redwood-jpeg against a model of the MT6893 VEN domain,
 * vencsys gates, larb7, and the JPEG encoder. The model is written from the Linux sources and from the phone read-back
 * values of gpu/hwcodec/runs/q10hw1 (see the literals in model_reset()), not from the driver.
 *
 * It fails on: any VEN/vencsys/larb7/encoder access while the domain is off, isolated, in reset or bus protected; an
 * encoder access with its clock gated; a start with a DMA address outside the 40 MiB claim, with a larb port still
 * translating, or with wrong geometry/control fields; any write to the display's SMI common; any register outside the
 * mapped windows; a copyin/copyout that the test marked as faulting; a copyin/copyout on the ioctl's kernel buffer.
 *
 * The ioctl entry is h_sys_ioctl(), a model of XNU's ioctl() (bsd/kern/sys_generic.c): it copies an IOC_IN struct into a
 * KERNEL buffer, zeroes it for IOC_OUT only, calls d_ioctl with that buffer and copies it out only when d_ioctl returned
 * 0 (patch 0316; the harness before 0316 passed the caller's pointer straight through and so accepted a driver that
 * copyin/copyout'ed `data`, which is EFAULT on the phone). h_kret holds the kernel buffer as d_ioctl left it, so the
 * out_* words of a failed job (never visible to user space) can still be checked.
 *
 * The test also checks the packed source bytes against an independent repacker, the exact control/geometry registers,
 * every validation branch, power and bus faults, timeout/stall/size faults, the interruptible one-job lock, two
 * concurrent callers, the idle power-off thread call (including while a job is in progress) and the hw-k02 VEN hook.
 */
#define _POSIX_C_SOURCE 200809L
#define N2J_HOST 1
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>

typedef uintptr_t vm_offset_t;
typedef uint64_t user_addr_t;
struct proc;
typedef char *caddr_t;
typedef int boolean_t;
typedef void *thread_call_param_t;

#define __unused __attribute__((unused))
#define TRUE 1
#define FALSE 0
#define NSEC_PER_MSEC 1000000ULL
#define PZERO 0
#define PCATCH 0x100
#define UID_ROOT 0
#define GID_WHEEL 0
#define DEVFS_CHAR 1

static int fails, checks, violations, quiet;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static void viol(const char *m, uint32_t pa) { violations++; printf("VIOLATION %s at %#x\n", m, pa); }

static uint64_t h_now_ns = 1u;
static int
n2j_log(const char *f, ...)
{
	if (quiet) { return 0; }
	va_list a; va_start(a, f); int r = vprintf(f, a); va_end(a); return r;
}
static void n2j_us(unsigned us) { (void)__atomic_add_fetch(&h_now_ns, (uint64_t)us * 1000u, __ATOMIC_SEQ_CST); }
static uint64_t mach_absolute_time(void) { return __atomic_load_n(&h_now_ns, __ATOMIC_SEQ_CST); }
static void absolutetime_to_nanoseconds(uint64_t t, uint64_t *ns) { *ns = t; }

/* ---- the user-kernel shims ---- */
static pthread_cond_t h_cond = PTHREAD_COND_INITIALIZER;
static int h_intr, h_waiters;
static uint64_t h_copyin_bytes, h_copyout_bytes;
static uint64_t h_fault_in_addr, h_fault_out_addr;
static int h_fault_in_on, h_fault_out_on;

typedef pthread_mutex_t lck_mtx_t;
struct h_grp { int x; };
#define LCK_GRP_DECLARE(name, str) static struct h_grp name __attribute__((unused)) = { 0 }
#define LCK_MTX_DECLARE(name, grp) static pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define lck_mtx_lock pthread_mutex_lock
#define lck_mtx_unlock pthread_mutex_unlock

static int n2jd_host_msleep(void *chan, lck_mtx_t *mtx, int pri, const char *msg, struct timespec *ts);
#define msleep n2jd_host_msleep
static void wakeup(void *chan) { (void)chan; pthread_cond_broadcast(&h_cond); }

static int
n2jd_host_msleep(void *chan, lck_mtx_t *mtx, int pri, const char *msg, struct timespec *ts)
{
	(void)chan; (void)pri; (void)msg; (void)ts;
	int r = 0;
	(void)__atomic_add_fetch(&h_waiters, 1, __ATOMIC_SEQ_CST);
	for (;;) {
		if (__atomic_load_n(&h_intr, __ATOMIC_SEQ_CST)) {
			__atomic_store_n(&h_intr, 0, __ATOMIC_SEQ_CST);
			r = EINTR;
			break;
		}
		struct timespec d;
		clock_gettime(CLOCK_REALTIME, &d);
		d.tv_nsec += 1000000;
		if (d.tv_nsec >= 1000000000L) { d.tv_sec++; d.tv_nsec -= 1000000000L; }
		int e = pthread_cond_timedwait(&h_cond, mtx, &d);
		if (e == ETIMEDOUT) { continue; }
		if (e != 0) { r = e; break; }
		break;                              /* the driver rechecks its condition after every wakeup */
	}
	(void)__atomic_sub_fetch(&h_waiters, 1, __ATOMIC_SEQ_CST);
	return r;
}

static int
h_range_has(uint64_t base, size_t len, uint64_t addr)
{
	return addr >= base && addr - base < len;
}

/* the kernel buffer of the ioctl in progress on this thread: user copies on it are a driver bug (EFAULT on the phone) */
static __thread uintptr_t h_kbuf_lo, h_kbuf_hi;
static int
h_on_kbuf(user_addr_t a, size_t n)
{
	if (h_kbuf_hi == 0) { return 0; }
	if (a < h_kbuf_hi && a + n > h_kbuf_lo) { viol("copyin/copyout on the ioctl kernel buffer", (uint32_t)a); return 1; }
	return 0;
}

static int
copyin(user_addr_t src, void *dst, size_t n)
{
	if (src == 0 || h_on_kbuf(src, n)) { return EFAULT; }
	if (__atomic_load_n(&h_fault_in_on, __ATOMIC_SEQ_CST) &&
	    h_range_has(src, n, __atomic_load_n(&h_fault_in_addr, __ATOMIC_SEQ_CST))) { return EFAULT; }
	memcpy(dst, (const void *)(uintptr_t)src, n);
	(void)__atomic_add_fetch(&h_copyin_bytes, n, __ATOMIC_SEQ_CST);
	return 0;
}

static int
copyout(const void *src, user_addr_t dst, size_t n)
{
	if (dst == 0 || h_on_kbuf(dst, n)) { return EFAULT; }
	if (__atomic_load_n(&h_fault_out_on, __ATOMIC_SEQ_CST) &&
	    h_range_has(dst, n, __atomic_load_n(&h_fault_out_addr, __ATOMIC_SEQ_CST))) { return EFAULT; }
	memcpy((void *)(uintptr_t)dst, src, n);
	(void)__atomic_add_fetch(&h_copyout_bytes, n, __ATOMIC_SEQ_CST);
	return 0;
}

/* devfs / cdevsw */
struct cdevsw {
	int (*d_open)(dev_t, int, int, struct proc *);
	int (*d_close)(dev_t, int, int, struct proc *);
	int (*d_ioctl)(dev_t, unsigned long, caddr_t, int, struct proc *);
};
#define minor(d) ((int)((d) & 0xff))
#define makedev(maj, min) ((((maj) & 0xff) << 8) | ((min) & 0xff))
static int h_devfs_calls, h_devfs_mode, h_devfs_type;
static char h_devfs_name[64];
static void *h_devfs_node = (void *)1;
static int h_cdevsw_fail, h_devfs_fail;
static int cdevsw_add(int index, const struct cdevsw *csw) { (void)index; (void)csw; return h_cdevsw_fail ? -1 : 42; }
static int cdevsw_remove(int major, const struct cdevsw *csw) { (void)major; (void)csw; return 0; }
static void *
devfs_make_node(dev_t dev, int type, uid_t uid, gid_t gid, int mode, const char *fmt, ...)
{
	va_list a; va_start(a, fmt);
	h_devfs_calls++;
	h_devfs_type = type;
	h_devfs_mode = mode;
	(void)uid; (void)gid; (void)dev;
	vsnprintf(h_devfs_name, sizeof(h_devfs_name), fmt, a);
	va_end(a);
	return h_devfs_fail ? NULL : h_devfs_node;
}

/* thread calls: one queue, run explicitly by test_timers() at the test's simulated time */
struct h_call { void (*fn)(thread_call_param_t, thread_call_param_t); thread_call_param_t p0, p1; uint64_t deadline; bool pending; };
typedef struct h_call *thread_call_t;
static struct h_call h_call_slot;
static pthread_mutex_t h_call_mtx = PTHREAD_MUTEX_INITIALIZER;
static thread_call_t
thread_call_allocate(void (*fn)(thread_call_param_t, thread_call_param_t), thread_call_param_t p0)
{
	pthread_mutex_lock(&h_call_mtx);
	h_call_slot.fn = fn; h_call_slot.p0 = p0; h_call_slot.p1 = NULL;
	h_call_slot.deadline = 0; h_call_slot.pending = false;
	pthread_mutex_unlock(&h_call_mtx);
	return &h_call_slot;
}
static boolean_t
thread_call_enter_delayed(thread_call_t call, uint64_t deadline)
{
	pthread_mutex_lock(&h_call_mtx);
	boolean_t was = call->pending;
	call->deadline = deadline;
	call->pending = true;
	pthread_mutex_unlock(&h_call_mtx);
	return was;
}
static void
clock_interval_to_deadline(uint32_t interval, uint32_t nsec, uint64_t *deadline)
{
	*deadline = __atomic_load_n(&h_now_ns, __ATOMIC_SEQ_CST) + (uint64_t)interval * nsec;
}
static void
test_timers(void)
{
	for (int guard = 0; guard < 1000; guard++) {
		uint64_t now = __atomic_load_n(&h_now_ns, __ATOMIC_SEQ_CST);
		void (*fn)(thread_call_param_t, thread_call_param_t) = NULL;
		thread_call_param_t p0 = NULL, p1 = NULL;
		pthread_mutex_lock(&h_call_mtx);
		if (h_call_slot.pending && h_call_slot.deadline <= now) {
			h_call_slot.pending = false;
			fn = h_call_slot.fn; p0 = h_call_slot.p0; p1 = h_call_slot.p1;
		}
		pthread_mutex_unlock(&h_call_mtx);
		if (fn == NULL) { break; }
		fn(p0, p1);
	}
}

/* ---- the register model ---- */
/* N2JD_DEC_MODEL (p031x/test/t_jpegdec.c, patch 0315): + the jpgdec core 0 window, its gate bit 12, larb7 ports 11/12 and
 * the 84 MiB decoder claim; without it the model is exactly K01's. */
enum { W_SPM, W_INFRA, W_TOP, W_CG, W_LARB, W_ENC, W_SMI, W_DEC, W_N };
static const uint32_t wbase[W_N] = { 0x10006000, 0x10001000, 0x10000000, 0x17000000, 0x17010000, 0x17030000, 0x1411f000, 0x17040000 };
#ifdef N2JD_DEC_MODEL
#define H_CG_MASK 0x10001111u
#define H_LARB_DEC(o) (((o) >= 0x22c && (o) <= 0x230) || ((o) >= 0x3ac && (o) <= 0x3b0))
static uint8_t *dram;                                    /* the decoder claim 0xDA800000 + 0x5400000 */
static void hd_write(uint32_t o, uint32_t v);            /* t_jpegdec.c: the decoder model */
static void hd_read(uint32_t o);
#else
#define H_CG_MASK 0x10000111u
#define H_LARB_DEC(o) 0
#endif
static uint32_t reg[W_N][0x400];
static uint8_t *ram;                                     /* the 40 MiB claim */
#define N2JD_CLAIM 0x2800000u
#define R(w, o) reg[w][(o) / 4]

static struct {
	bool no_power_ack, sram_stuck, never_done, stall, vcodec_irq, dis_off, prot_stuck, mux_dead, larb_stuck, claim_map_fail;
	bool bad_dma, bad_soi, no_eoi, stale_eoi, block_start;
	unsigned done_after;
	uint32_t force_n;
} F;

static struct { int w; uint32_t off, val; } tr[8192];
static unsigned trn;
static unsigned polls_left, model_starts, model_active, model_max_active;
static uint32_t hw_dma_size;

static pthread_mutex_t h_cond_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t h_cond_ev = PTHREAD_COND_INITIALIZER;
static int h_started, h_release;

static void trace_reset(void) { trn = 0; }
static int trace_count(int w) { int n = 0; for (unsigned i = 0; i < trn; i++) { if (tr[i].w == w) { n++; } } return n; }
static int trace_has(int w, uint32_t off, uint32_t val) { for (unsigned i = 0; i < trn; i++) { if (tr[i].w == w && tr[i].off == off && tr[i].val == val) { return 1; } } return 0; }
static int trace_idx(int w, uint32_t off, uint32_t val) { for (unsigned i = 0; i < trn; i++) { if (tr[i].w == w && tr[i].off == off && tr[i].val == val) { return (int)i; } } return -1; }
static int trace_last(int w, uint32_t off, uint32_t *val) { for (unsigned i = trn; i-- > 0; ) { if (tr[i].w == w && tr[i].off == off) { *val = tr[i].val; return 1; } } return 0; }
static int trace_count_val(int w, uint32_t off, uint32_t val) { int n = 0; for (unsigned i = 0; i < trn; i++) { if (tr[i].w == w && tr[i].off == off && tr[i].val == val) { n++; } } return n; }

static uint32_t h_align16(uint32_t v) { return (v + 15u) & ~15u; }
static uint32_t h_align8(uint32_t v) { return (v + 7u) & ~7u; }

static bool
dom_on(void)
{
	uint32_t c = R(W_SPM, 0x344);
	return (R(W_SPM, 0x16c) & (1u << 17)) && (c & 0x1f) == 0x0d && !(c & 0x1100) &&
	       !(R(W_INFRA, 0x2d0) & (3u << 26)) && !(R(W_INFRA, 0xdc8) & 3u);
}

static void
spm_update(void)
{
	uint32_t c = R(W_SPM, 0x344);
	bool on;
	if ((c & 0xc) == 0xc) { on = !F.no_power_ack; }
	else if (c & 0xc) { on = (R(W_SPM, 0x16c) >> 17) & 1; }
	else { on = false; }
	R(W_SPM, 0x16c) = (0x10580fffu & ~(1u << 17)) | (on ? 1u << 17 : 0);
	R(W_SPM, 0x170) = (0x10580fffu & ~(1u << 17)) | (on ? 1u << 17 : 0);
	if (F.dis_off) { R(W_SPM, 0x16c) &= ~(1u << 20); R(W_SPM, 0x170) &= ~(1u << 20); }
	if (!F.sram_stuck) { c = (c & ~(1u << 12)) | ((c & (1u << 8)) ? 1u << 12 : 0); }
	R(W_SPM, 0x344) = c;
}

static void
model_reset(void)
{
	memset(reg, 0, sizeof(reg));
	memset(&F, 0, sizeof(F));
	__atomic_store_n(&h_now_ns, 1u, __ATOMIC_SEQ_CST);
	trn = 0; polls_left = 0; model_starts = 0; model_active = 0; model_max_active = 0; hw_dma_size = 0;
	R(W_SPM, 0x16c) = R(W_SPM, 0x170) = 0x10580fffu;                 /* phone q10hw1: VEN off, DIS on */
	R(W_SPM, 0x344) = 0x1112;                                         /* off: ISO, CLK_DIS, SRAM_PDN + ack */
	R(W_SPM, 0x350) = 0x0d;                                           /* DIS_PWR_CON, as q10hw1 */
	R(W_INFRA, 0x2d0) = 3u << 26; R(W_INFRA, 0x2ec) = 0x5d554815u;    /* off + bus protected (q10hw1 step 4) */
	R(W_INFRA, 0xdc8) = 3u; R(W_INFRA, 0xdd8) = 0x143u;
	R(W_TOP, 0xe0) = 0x0c030601u;                                     /* parent 12, gate on, as found on the phone */
	R(W_SMI, 0x100) = 0x3; R(W_SMI, 0x220) = 0; R(W_SMI, 0x300) = 0x4ff;
	R(W_LARB, 0x24) = 0x370246u; R(W_LARB, 0x40) = 0; R(W_LARB, 0x524) = 0x370246u;
	for (unsigned p = 0; p < 32; p++) {
		R(W_LARB, 0x200 + 4 * p) = 0x3f3fu;                       /* OSTDL */
		R(W_LARB, 0x380 + 4 * p) = 0x5501u;                       /* translating, bank bits, as Android leaves it */
	}
	R(W_CG, 0) = 0;                                                   /* all vencsys gates off */
	R(W_ENC, 0x100) = 1; R(W_ENC, 0x104) = 0; R(W_ENC, 0x108) = 0; R(W_ENC, 0x10c) = 0; R(W_ENC, 0x110) = 0;
	R(W_ENC, 0x11c) = 0; R(W_ENC, 0x120) = 0; R(W_ENC, 0x124) = 0; R(W_ENC, 0x128) = 0xf0;
	R(W_ENC, 0x138) = 0; R(W_ENC, 0x150) = 4; R(W_ENC, 0x154) = 0x0a200798u;
	R(W_ENC, 0x160) = 0; R(W_ENC, 0x164) = 0; R(W_ENC, 0x168) = 0; R(W_ENC, 0x16c) = 0;
	R(W_ENC, 0x170) = 0; R(W_ENC, 0x174) = 0; R(W_ENC, 0x178) = 0; R(W_ENC, 0x17c) = 0;
	R(W_ENC, 0x300) = 0; R(W_ENC, 0x314) = 0; R(W_ENC, 0x318) = 0xffffffffu;
	memset(ram, 0, N2JD_CLAIM);
}

static void
guard(int w, uint32_t off)
{
	if (w == W_DEC && (!dom_on() || !(R(W_CG, 0) & 0x1000u))) { viol("jpgdec with the domain off or its clock gated", wbase[w] + off); }
	if (w == W_CG || w == W_LARB || w == W_ENC) {
		if (!dom_on()) { viol("VEN register with the domain off / protected", wbase[w] + off); }
		else if (w == W_ENC && (R(W_CG, 0) & 0x101u) != 0x101u) { viol("encoder with its clock gated", wbase[w] + off); }
		else if (w == W_LARB && (R(W_CG, 0) & 0x11u) != 0x11u) { viol("larb with its clock gated", wbase[w] + off); }
	}
	if (w == W_SMI && !(R(W_SPM, 0x16c) & (1u << 20))) { viol("display SMI with the display off", off); }
}

/* the block writes a stand-in "JPEG": SOI, the source checksum, the settings, EOI, then zero padding to a 64-byte multiple */
static void
hw_encode(uint32_t ctrl)
{
	uint32_t w = R(W_ENC, 0x154) >> 16, h = R(W_ENC, 0x154) & 0xffffu;
	uint32_t stride = R(W_ENC, 0x178), img_stride = R(W_ENC, 0x17c);
	uint32_t dst = R(W_ENC, 0x120), luma = R(W_ENC, 0x170), chroma = R(W_ENC, 0x174);
	uint32_t stall = R(W_ENC, 0x128), off = R(W_ENC, 0x138), mask = R(W_ENC, 0x16c);
	uint32_t fmt = (ctrl >> 3) & 3u, q = R(W_ENC, 0x108) & 0xffffu;
	bool yuyv = fmt == 0 || fmt == 1;
	uint32_t rows = yuyv ? h_align8(h) : h_align16(h);
	uint32_t want = yuyv ? 2u * h_align16(w) : h_align16(w);

	model_active++;
	if (model_active > model_max_active) { model_max_active = model_active; }
	for (unsigned p = 7; p <= 10; p++) { if (R(W_LARB, 0x380 + 4 * p) & 0xff01u) { viol("start with a translating/banked larb port", p); } }
	if (R(W_LARB, 0x24) != 0x300256u || R(W_LARB, 0x40) != 1 || R(W_LARB, 0x524) != 0x300256u) { viol("larb7 not configured", 0); }
	if (luma < 0xd8000000u || chroma != luma + stride * rows || chroma + stride * rows / (yuyv ? 2u : 1u) > 0xda800000u) { viol("source outside the claim", luma); }
	if (dst < 0xd9800000u || dst >= 0xda800000u || stall <= dst || stall > 0xda800000u || (dst & 15u)) { viol("destination outside the claim", dst); }
	if ((ctrl & 0x18u) != (fmt << 3) || !(ctrl & (1u << 2)) || !(ctrl & 1u) || !(ctrl & (1u << 20)) || !(ctrl & (1u << 29)) || (ctrl & (1u << 30))) { viol("CTRL", ctrl); }
	if (stride != want || img_stride != want) { viol("stride", stride); }
	uint32_t blk = yuyv ? (h_align16(w) >> 4) * (h_align8(h) >> 3) * 4u - 1u
	                    : (h_align16(w) >> 4) * (h_align16(h) >> 4) * 6u - 1u;
	if (R(W_ENC, 0x10c) != blk) { viol("BLK_NUM", R(W_ENC, 0x10c)); }
	if (off != 0 || mask != 0 || R(W_ENC, 0x100) != 1) { viol("offset/reset", off); }
	if (q > 0x10u) { viol("quality code", q); }
	if ((ctrl & (1u << 10)) != (R(W_ENC, 0x150) ? (1u << 10) : 0)) { viol("restart control", ctrl); }

	uint8_t *src = ram + (luma - 0xd8000000u);
	uint32_t total = stride * rows * (yuyv ? 1u : 3u) / (yuyv ? 2u : 1u);
	uint32_t sum = 2166136261u;
	for (uint32_t i = 0; i < total; i++) { sum = (sum ^ src[i]) * 16777619u; }

	uint8_t *o = ram + (dst - 0xd8000000u);
	uint32_t n = F.force_n ? F.force_n : 100u + (w * h) / 64u + (q & 7u);
	if (n < 16u) { n = 16u; }
	if (n > 0x1000000u - 0x2000u) { n = 0x1000000u - 0x2000u; }
	memset(o, 0x77, n);
	o[0] = 0xff; o[1] = 0xd8;
	memcpy(o + 2, &sum, 4);
	o[6] = (uint8_t)q; o[7] = (uint8_t)fmt;
	o[8] = (uint8_t)(R(W_ENC, 0x150) & 0xffu); o[9] = (uint8_t)(R(W_ENC, 0x150) >> 8);
	o[10] = (uint8_t)(w & 0xffu); o[11] = (uint8_t)(w >> 8);
	o[12] = (uint8_t)(h & 0xffu); o[13] = (uint8_t)(h >> 8);
	o[n - 2] = 0xff; o[n - 1] = 0xd9;
	uint32_t pad = (n + 127u) & ~127u;                    /* 0317: the block rounds to 128 (phone run jp5), not 64 */
	if (pad == n) { pad += 128u; }
	memset(o + n, 0, pad - n);
	if (F.stale_eoi && pad - n >= 8u) { o[pad - 4u] = 0xff; o[pad - 3u] = 0xd9; }   /* an earlier, longer job's EOI */
	if (F.bad_soi) { o[0] = 0; }
	if (F.no_eoi) { memset(o + (n > 256u ? n - 256u : 0u), 0x11, n > 256u ? 256u : n); }   /* the driver's whole window */
	hw_dma_size = pad;
	R(W_ENC, 0x124) = dst + pad;
	R(W_ENC, 0x110) = R(W_ENC, 0x10c);
	if (F.block_start) {
		F.block_start = false;
		pthread_mutex_lock(&h_cond_mtx);
		h_started++;
		pthread_cond_broadcast(&h_cond_ev);
		while (!h_release) { pthread_cond_wait(&h_cond_ev, &h_cond_mtx); }
		pthread_mutex_unlock(&h_cond_mtx);
	}
	polls_left = F.done_after ? F.done_after : 3u;
	if (F.stall) { R(W_ENC, 0x11c) = 2; }
	else if (F.vcodec_irq) { R(W_ENC, 0x11c) = 0x10; }
	else if (F.bad_dma) { R(W_ENC, 0x124) = dst - 16u; }
	model_starts++;
	model_active--;
}

static bool
lookup(vm_offset_t va, int *w, uint32_t *off)
{
	for (int i = 0; i < W_N; i++) {
		if (va >= (vm_offset_t)reg[i] && va < (vm_offset_t)reg[i] + 0x1000) { *w = i; *off = (uint32_t)(va - (vm_offset_t)reg[i]); return true; }
	}
	return false;
}

static uint32_t
ni_rd(vm_offset_t b, unsigned off)
{
	int w; uint32_t o;
	if (!lookup(b + off, &w, &o) || (o & 3)) { viol("read outside a window", (uint32_t)off); return 0; }
	guard(w, o);
#ifdef N2JD_DEC_MODEL
	if (w == W_DEC) { hd_read(o); }
#endif
	if (w == W_ENC && o == 0x11c && (R(W_ENC, 0x104) & 1u) && !R(W_ENC, 0x11c) && !F.never_done && !F.stall && !F.vcodec_irq && R(W_ENC, 0x124)) {
		if (polls_left == 0) { R(W_ENC, 0x11c) = 1; }
		else { polls_left--; if (polls_left == 0) { R(W_ENC, 0x11c) = 1; } }
	}
	return R(w, o);
}

static void
ni_wr(vm_offset_t b, unsigned off, uint32_t v)
{
	int w; uint32_t o;
	if (!lookup(b + off, &w, &o) || (o & 3)) { viol("write outside a window", (uint32_t)off); return; }
	guard(w, o);
	if (trn < sizeof(tr) / sizeof(tr[0])) { tr[trn].w = w; tr[trn].off = o; tr[trn].val = v; trn++; } else { viol("trace overflow", o); }
	if (w == W_SPM) {
		if (o != 0 && o != 0x344) { viol("unexpected SPM write", o); }
		R(w, o) = v; spm_update(); return;
	}
	if (w == W_INFRA) {
		uint32_t *en = NULL, *ack = NULL; bool set = false;
		if (o == 0x2d4 || o == 0x2d8) { en = &R(w, 0x2d0); ack = &R(w, 0x2ec); set = o == 0x2d4; if (v & ~(3u << 26)) { viol("foreign MM protection bits", v); } }
		else if (o == 0xdcc || o == 0xdd0) { en = &R(w, 0xdc8); ack = &R(w, 0xdd8); set = o == 0xdcc; if (v & ~3u) { viol("foreign MM_2 protection bits", v); } }
		else { viol("unexpected infracfg write", o); return; }
		if (set) { *en |= v; if (!F.prot_stuck) { *ack |= v; } }
		else { *en &= ~v; *ack &= ~v; }
		return;
	}
	if (w == W_TOP) {
		if (F.mux_dead) { return; }
		if (o == 0xe4) { R(w, 0xe0) |= v; } else if (o == 0xe8) { R(w, 0xe0) &= ~v; } else if (o != 0x08) { viol("unexpected topckgen write", o); }
		if ((o == 0xe4 || o == 0xe8) && (v & 0x70ffffffu)) { viol("foreign CLK_CFG_13 bits", v); }
		if (o == 0x08 && v != 1u << 24) { viol("foreign mux update bits", v); }
		return;
	}
	if (w == W_SMI) { viol("write to the display's SMI common", o); return; }
	if (w == W_CG) { if (o == 4) { R(w, 0) |= (v & H_CG_MASK); } else if (o == 8) { R(w, 0) &= ~(v & H_CG_MASK); } else { viol("unexpected vencsys write", o); } return; }
#ifdef N2JD_DEC_MODEL
	if (w == W_DEC) { hd_write(o, v); return; }
#else
	if (w == W_DEC) { viol("jpgdec written by the encoder build", o); return; }
#endif
	if (w == W_LARB) {
		bool ok = o == 0x24 || o == 0x40 || o == 0x524 || (o >= 0x21c && o <= 0x228) || (o >= 0x39c && o <= 0x3a8) || H_LARB_DEC(o);
		if (!ok) { viol("larb7 register of another user written", o); }
		if (F.larb_stuck && o >= 0x39c && o <= 0x3a8) { return; }   /* the port keeps translating */
		R(w, o) = v; return;
	}
	if (o == 0x11c) { R(w, o) = 0; return; }
	if (o == 0x100 && v == 0) {
		R(W_ENC, 0x100) = 0; R(W_ENC, 0x104) = 0; R(W_ENC, 0x108) = 0; R(W_ENC, 0x10c) = 0; R(W_ENC, 0x110) = 0;
		R(W_ENC, 0x11c) = 0; R(W_ENC, 0x120) = 0; R(W_ENC, 0x124) = 0; R(W_ENC, 0x138) = 0;
		R(W_ENC, 0x16c) = 0; R(W_ENC, 0x170) = 0; R(W_ENC, 0x174) = 0; R(W_ENC, 0x178) = 0; R(W_ENC, 0x17c) = 0;
		R(W_ENC, 0x300) = 0;
		/* STALL, RST_MCU_NUM, IMG_SIZE, DEBUG, CYCLE, ULTRA and CODEC_SEL keep the phone's values: the driver must write
		 * every one it depends on, and reset itself writes CODEC_SEL below */
		return;
	}
	R(w, o) = v;
	if (o == 0x104 && (v & 1u)) { R(W_ENC, 0x11c) = 0; R(W_ENC, 0x124) = 0; hw_encode(v); }
}

static void
ni_mask(vm_offset_t b, unsigned off, uint32_t mask, uint32_t v)
{
	ni_wr(b, off, (ni_rd(b, off) & ~mask) | (v & mask));
}

static vm_offset_t
n2j_map(uint64_t pa, uint32_t bytes, int io)
{
#ifdef N2JD_DEC_MODEL
	if (!io && pa == 0xda800000ULL) { CHECK(bytes == 0x5400000u); return (vm_offset_t)dram; }
#endif
	if (!io) { CHECK(pa == 0xd8000000ULL && bytes == N2JD_CLAIM); return F.claim_map_fail ? 0 : (vm_offset_t)ram; }
	for (int i = 0; i < W_N; i++) { if (wbase[i] == pa) { CHECK(bytes == 0x1000); return (vm_offset_t)reg[i]; } }
	viol("unknown mapping", (uint32_t)pa);
	return 0;
}

static void
n2j_cache(vm_offset_t va, uint32_t len)
{
#ifdef N2JD_DEC_MODEL
	if (va >= (vm_offset_t)dram && va + len <= (vm_offset_t)dram + 0x5400000u) { return; }
#endif
	if (va < (vm_offset_t)ram || va + len > (vm_offset_t)ram + N2JD_CLAIM) { viol("cache maintenance outside the claim", (uint32_t)va); }
}

#include "../kernel/b/bsd/dev/nord2_hwcodec.inc"
#include "../bsd/dev/nord2_jpegdev.inc"

/* ---- test helpers ---- */
static dev_t h_dev(void) { return makedev(42, 0); }
/* XNU ioctl() (sys_generic.c:875-1015) in front of d_ioctl; see the header comment */
#define H_IOC_VOID 0x20000000u
#define H_IOC_OUT  0x40000000u
#define H_IOC_IN   0x80000000u
static __thread union { uint8_t b[512]; uint64_t align; } h_kret;          /* the kernel buffer after the last d_ioctl */
#define K_ENC (*(const struct rwj_encode *)(const void *)h_kret.b)
static int
h_sys_ioctl(dev_t dev, unsigned long cmd, void *uaddr)
{
	uint32_t c = (uint32_t)cmd, size = (c >> 16) & 0x1fffu;            /* IOCPARM_LEN */
	union { uint8_t b[512]; uint64_t align; } k;
	if (size > sizeof(k.b)) { printf("FAIL h_sys_ioctl: %#x larger than the model's buffer\n", c); fails++; return ENOTTY; }
	memset(k.b, 0xa5, sizeof(k.b));
	if (c & H_IOC_IN) {
		if (size) { int e = copyin((user_addr_t)(uintptr_t)uaddr, k.b, size); if (e) { return e; } }
		else { memcpy(k.b, &uaddr, sizeof(uaddr)); }
	} else if ((c & H_IOC_OUT) && size) {
		memset(k.b, 0, size);
	} else if (c & H_IOC_VOID) {
		memcpy(k.b, &uaddr, sizeof(uaddr));
	}
	h_kbuf_lo = (uintptr_t)k.b; h_kbuf_hi = (uintptr_t)k.b + sizeof(k.b);
	int err = n2jd_ioctl(dev, cmd, (caddr_t)k.b, 0, NULL);
	h_kbuf_lo = h_kbuf_hi = 0;
	memcpy(h_kret.b, k.b, sizeof(k.b));
	if (err == 0 && (c & H_IOC_OUT) && size) { err = copyout(k.b, (user_addr_t)(uintptr_t)uaddr, size); }
	return err;
}
static int h_enc(struct rwj_encode *j) { return h_sys_ioctl(h_dev(), RWJ_ENCODE, j); }
static int h_inf(struct rwj_info *i) { return h_sys_ioctl(h_dev(), RWJ_INFO, i); }
static void h_fault_in(uint8_t *p) { __atomic_store_n(&h_fault_in_addr, (uint64_t)(uintptr_t)p, __ATOMIC_SEQ_CST); __atomic_store_n(&h_fault_in_on, 1, __ATOMIC_SEQ_CST); }
static void h_fault_out(uint8_t *p) { __atomic_store_n(&h_fault_out_addr, (uint64_t)(uintptr_t)p, __ATOMIC_SEQ_CST); __atomic_store_n(&h_fault_out_on, 1, __ATOMIC_SEQ_CST); }
static void h_fault_clear(void) { __atomic_store_n(&h_fault_in_on, 0, __ATOMIC_SEQ_CST); __atomic_store_n(&h_fault_out_on, 0, __ATOMIC_SEQ_CST); }

struct pic { struct rwj_encode j; uint8_t *y, *c, *d; uint32_t dcap, ybytes, cbytes; };

static struct pic
pic_make(uint32_t w, uint32_t h, uint32_t fmt, uint32_t stride, uint32_t cs, uint32_t q)
{
	struct pic p;
	memset(&p, 0, sizeof(p));
	bool yuyv = fmt == RWJ_FMT_YUYV || fmt == RWJ_FMT_YVYU;
	p.ybytes = stride * h;
	p.cbytes = yuyv ? 0 : (cs ? cs : stride) * (h / 2u);
	p.dcap = w * h / 2u + 8192u;
	p.y = malloc(p.ybytes);
	p.c = malloc(p.cbytes ? p.cbytes : 1);
	p.d = malloc(p.dcap);
	CHECK(p.y != NULL && p.c != NULL && p.d != NULL);
	for (uint32_t i = 0; i < p.ybytes; i++) { p.y[i] = (uint8_t)(13u + (i * 29u) % 233u); }
	for (uint32_t i = 0; i < p.cbytes; i++) { p.c[i] = (uint8_t)(77u + (i * 17u) % 151u); }
	memset(p.d, 0x5a, p.dcap);
	p.j.width = w; p.j.height = h; p.j.format = fmt; p.j.quality = q;
	p.j.stride = stride; p.j.chroma_stride = cs; p.j.restart_interval = 0; p.j.flags = 0;
	p.j.src_y = (uint64_t)(uintptr_t)p.y;
	p.j.src_c = yuyv ? 0 : (uint64_t)(uintptr_t)p.c;
	p.j.dst = (uint64_t)(uintptr_t)p.d;
	p.j.dst_capacity = p.dcap;
	return p;
}
static void pic_free(struct pic *p) { free(p->y); free(p->c); free(p->d); memset(p, 0, sizeof(*p)); }

/* independent repacker: the expected bytes in the 24 MiB source area */
static uint32_t
expect_pack(const struct pic *p, uint8_t *out)
{
	const struct rwj_encode *j = &p->j;
	bool yuyv = j->format == RWJ_FMT_YUYV || j->format == RWJ_FMT_YVYU;
	uint32_t w = j->width, h = j->height;
	uint32_t rs = yuyv ? 2u * h_align16(w) : h_align16(w);
	uint32_t rows = yuyv ? h_align8(h) : h_align16(h);
	uint32_t lb = yuyv ? 2u * w : w;
	memset(out, 0xee, yuyv ? rs * rows : rs * rows * 3u / 2u);
	for (uint32_t y = 0; y < h; y++) {
		uint8_t *d = out + (size_t)y * rs;
		memcpy(d, p->y + (size_t)y * j->stride, lb);
		if (yuyv) { for (uint32_t x = lb; x < rs; x += 4) { memcpy(d + x, d + lb - 4u, 4); } }
		else { for (uint32_t x = lb; x < rs; x++) { d[x] = d[lb - 1u]; } }
	}
	for (uint32_t y = h; y < rows; y++) { memcpy(out + (size_t)y * rs, out + (size_t)(h - 1u) * rs, rs); }
	if (!yuyv) {
		uint8_t *c = out + (size_t)rs * rows;
		uint32_t ch = h / 2u, cs = j->chroma_stride ? j->chroma_stride : j->stride;
		for (uint32_t y = 0; y < ch; y++) {
			uint8_t *d = c + (size_t)y * rs;
			memcpy(d, p->c + (size_t)y * cs, w);
			for (uint32_t x = w; x < rs; x += 2) { d[x] = d[w - 2u]; d[x + 1u] = d[w - 1u]; }
		}
		for (uint32_t y = ch; y < rows / 2u; y++) { memcpy(c + (size_t)y * rs, c + (size_t)(ch - 1u) * rs, rs); }
		return rs * rows * 3u / 2u;
	}
	return rs * rows;
}

static uint32_t
packed_size(uint32_t w, uint32_t h, uint32_t fmt)
{
	bool yuyv = fmt == RWJ_FMT_YUYV || fmt == RWJ_FMT_YVYU;
	return yuyv ? 2u * h_align16(w) * h_align8(h) : h_align16(w) * h_align16(h) * 3u / 2u;
}

static void
check_pack(const struct pic *p)
{
	uint32_t n = packed_size(p->j.width, p->j.height, p->j.format);
	uint8_t *want = malloc(n);
	CHECK(want != NULL);
	CHECK(expect_pack(p, want) == n);
	CHECK(memcmp(ram, want, n) == 0);
	if (memcmp(ram, want, n) != 0) {
		for (uint32_t i = 0; i < n; i++) { if (ram[i] != want[i]) { printf("  first pack difference at %u: model %02x expected %02x\n", i, ram[i], want[i]); break; } }
	}
	free(want);
}

/* the user destination got exactly out_bytes and nothing after it */
static void
check_out(const struct pic *p, uint32_t out_bytes)
{
	CHECK(out_bytes >= 4u && out_bytes <= p->dcap);
	uint8_t *want = ram + 0x1800000u;
	CHECK(memcmp(p->d, want, out_bytes) == 0);
	for (uint32_t i = out_bytes; i < p->dcap && i < out_bytes + 64u; i++) { CHECK(p->d[i] == 0x5a); }
}

struct h_job { struct pic *p; int err; };
static void *job_a(void *arg) { struct h_job *j = arg; j->err = h_enc(&j->p->j); return NULL; }
static void *job_b(void *arg) { struct h_job *j = arg; j->err = h_enc(&j->p->j); return NULL; }

/* a faithful stand-in for hw-k02: get powers VEN on (without the JPGENC gate), put powers it off at the last reference */
static int h_gets, h_puts, h_ven_refs;
static int
h_fake_get(void)
{
	h_gets++;
	if (h_ven_refs++ == 0) {
		uint32_t r = n2j_power();
		if (r != N2J_OK) { h_ven_refs = 0; return (int)r; }
		ni_wr(n2j.cg, 0x8, 1u << 8);           /* K02 does not touch bit 8 */
	}
	return 0;
}
static void
h_fake_put(void)
{
	h_puts++;
	/* K02's last put powers VEN down; here another user always keeps a reference, so only the count moves. The test's
	 * point is that the JPEG driver itself never runs the model's n2j_off() while the domain is shared. */
	if (h_ven_refs > 0) { h_ven_refs--; }
}

int
main(int argc, char **argv)
{
	quiet = argc > 1 && strcmp(argv[1], "-q") == 0;
	ram = malloc(N2JD_CLAIM);
	CHECK(ram != NULL);
	(void)n2j_command;                      /* the model's autotest entry stays in the file; it is not used here */

	/* ---- A. start, node, INFO, ioctl dispatch ---- */
	model_reset();
	h_cdevsw_fail = 1; CHECK(nord2_jpegdev_start() == EIO && !n2jd.made);
	h_cdevsw_fail = 0; h_devfs_fail = 1; CHECK(nord2_jpegdev_start() == EIO && !n2jd.made);
	h_devfs_fail = 0;
	CHECK(nord2_jpegdev_start() == 0);
	CHECK(h_devfs_calls == 2);
	CHECK(strcmp(h_devfs_name, "redwood-jpeg") == 0);
	CHECK(h_devfs_type == DEVFS_CHAR && h_devfs_mode == 0666);
	CHECK(nord2_jpegdev_start() == 0 && h_devfs_calls == 2);
	CHECK(model_starts == 0 && trace_count(W_SPM) == 0 && trace_count(W_ENC) == 0);
	struct rwj_info inf;
	memset(&inf, 0, sizeof(inf));
	inf.abi_version = RWJ_ABI_VERSION;
	CHECK(h_inf(&inf) == 0);
	CHECK(inf.abi_version == RWJ_ABI_VERSION && inf.max_pixels == RWJ_MAX_PIXELS && inf.caps == 1);
	CHECK(inf.powered == 0 && inf.jobs_done == 0 && inf.jobs_failed == 0 && inf.last_hw_us == 0);
#ifdef N2JD_DEC_MODEL
	for (unsigned i = 0; i < 9; i++) { CHECK(inf.reserved[i] == (i == 0 ? RWJD_REV : 0u)); }   /* 0315 feature word */
#else
	for (unsigned i = 0; i < 9; i++) { CHECK(inf.reserved[i] == 0); }
#endif
	h_fault_in((uint8_t *)(uintptr_t)&inf);
	CHECK(h_inf(&inf) == 0 && inf.abi_version == RWJ_ABI_VERSION);   /* _IOR: no copyin of the caller's struct */
	h_fault_clear();
	h_fault_out((uint8_t *)(uintptr_t)&inf);
	CHECK(h_inf(&inf) == EFAULT);
	h_fault_clear();
	memset(&inf, 0xff, sizeof(inf)); CHECK(h_inf(&inf) == 0);       /* _IOR: a garbage request is overwritten */
	CHECK(inf.abi_version == RWJ_ABI_VERSION && inf.max_pixels == RWJ_MAX_PIXELS);
	struct rwj_decode dec; memset(&dec, 0, sizeof(dec));
	CHECK(h_sys_ioctl(h_dev(), RWJ_DECODE, &dec) == ENOTSUP);
	CHECK(h_sys_ioctl(h_dev(), 0x1234u, &dec) == ENOTTY);
	CHECK(h_sys_ioctl(h_dev(), RWJ_ENCODE, NULL) == EFAULT);            /* sys_ioctl's copyin */
	CHECK(n2jd_ioctl(h_dev(), RWJ_ENCODE, NULL, 0, NULL) == EFAULT);    /* the driver's own guard */
	CHECK(h_sys_ioctl(makedev(42, 1), RWJ_INFO, &inf) == ENXIO);
	{                                                                  /* 0316: a failed ioctl copies nothing out */
		struct rwj_info keep; memset(&keep, 0x3c, sizeof(keep)); struct rwj_info i2 = keep;
		CHECK(h_sys_ioctl(makedev(42, 1), RWJ_INFO, &i2) == ENXIO && memcmp(&i2, &keep, sizeof(keep)) == 0);
	}
	CHECK(n2jd_open(h_dev(), 0, 0, NULL) == 0);
	CHECK(n2jd_close(h_dev(), 0, 0, NULL) == 0);
	CHECK(n2jd_close(h_dev(), 0, 0, NULL) == 0);                       /* a close without an open is harmless */
	CHECK(n2jd_open(makedev(42, 1), 0, 0, NULL) == ENXIO);
	CHECK(model_starts == 0 && trace_count(W_SPM) == 0);

	/* ---- B. every validation branch; nothing is copied or started ---- */
	struct pic base = pic_make(64, 64, RWJ_FMT_NV12, 64, 0, 90);
	struct { int err; const char *what; } bad[] = {
		{ EINVAL, "width 0" }, { EINVAL, "width 14" }, { EINVAL, "height 0" },
		{ EINVAL, "width odd" }, { EINVAL, "height odd" }, { EINVAL, "width > max" },
		{ EINVAL, "height > max" }, { EINVAL, "pixels > max" }, { EINVAL, "format" },
		{ EINVAL, "quality 0" }, { EINVAL, "quality 101" }, { EINVAL, "stride" },
		{ EINVAL, "chroma stride" }, { EINVAL, "restart > 16 bits" }, { EINVAL, "flags" },
		{ EINVAL, "src_y 0" }, { EINVAL, "src_c 0" }, { EINVAL, "dst 0" },
		{ EINVAL, "src wrap" }, { EINVAL, "dst wrap" }, { EINVAL, "yuyv > 24 MiB" },
		{ ENOSPC, "capacity too small" }, { ENOSPC, "capacity 0" }, { EINVAL, "src_c wrap" },
	};
	for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		struct rwj_encode j = base.j;
		uint64_t before_in = __atomic_load_n(&h_copyin_bytes, __ATOMIC_SEQ_CST);
		switch (i) {
		case 0: j.width = 0; break; case 1: j.width = 14; break; case 2: j.height = 0; break;
		case 3: j.width = 65; break; case 4: j.height = 65; break; case 5: j.width = 8194; break;
		case 6: j.height = 8194; break; case 7: j.width = 4098; j.height = 3072; break;
		case 8: j.format = 5; break; case 9: j.quality = 0; break; case 10: j.quality = 101; break;
		case 11: j.stride = j.width - 1; break; case 12: j.chroma_stride = j.width - 1; break;
		case 13: j.restart_interval = 0x10000; break; case 14: j.flags = 2; break;
		case 15: j.src_y = 0; break; case 16: j.src_c = 0; break; case 17: j.dst = 0; break;
		case 18: j.src_y = UINT64_MAX - 8; break; case 19: j.dst = UINT64_MAX - 8; break;
		case 20: j.width = 4082; j.height = 3082; j.format = RWJ_FMT_YUYV; j.stride = 8192; j.dst_capacity = 7000000; break;
		case 21: j.dst_capacity = j.width * j.height / 2 + 4095; break;
		case 22: j.dst_capacity = 0; break;
		case 23: j.src_c = UINT64_MAX - 8; break;
		}
		int e = h_enc(&j);
		if (e != bad[i].err) { printf("  validation %s: got %d want %d\n", bad[i].what, e, bad[i].err); }
		CHECK(e == bad[i].err);
		CHECK(__atomic_load_n(&h_copyin_bytes, __ATOMIC_SEQ_CST) - before_in == sizeof(struct rwj_encode));
		CHECK(model_starts == 0);
	}
	/* a failed mapping of the 40 MiB claim is a bus error and touches no hardware; the next call retries */
	{
		struct rwj_encode jf = base.j;
		F.claim_map_fail = true;
		jf.out_status = 77u;
		CHECK(h_enc(&jf) == EIO && K_ENC.out_status == RWJ_E_BUS && !n2jd.mapped && model_starts == 0);
		CHECK(jf.out_status == 77u);                                /* 0316: errno only, the struct is not copied out */
		F.claim_map_fail = false;
	}
	/* YUYV with 2*width is the minimum, and chroma_stride 0 means stride */
	{
		struct pic yy = pic_make(64, 64, RWJ_FMT_YUYV, 128, 0, 90);
		struct rwj_encode j = yy.j; j.stride = 127; CHECK(h_enc(&j) == EINVAL);
		struct rwj_encode j2 = base.j; j2.chroma_stride = 0; CHECK(h_enc(&j2) == 0);
		struct rwj_encode j3 = base.j; j3.dst_capacity = j3.width * j3.height / 2 + 4096;
		CHECK(h_enc(&j3) == 0);                                     /* exactly the documented minimum */
		pic_free(&yy);
	}
	CHECK(model_starts == 2);                                          /* only the two valid jobs */

	/* ---- C. the first real job: power-on, bus, encode, cut, copyout, stats ---- */
	model_reset();
	trace_reset();
	n2j.bus_ready = false;
	n2jd.powered = false;
	n2jd.jobs_done = 0; n2jd.jobs_failed = 0; n2jd.last_hw_us = 0; n2jd.last_job_ns = 0; n2jd.session_jobs = 0;
	pthread_mutex_lock(&h_call_mtx); h_call_slot.pending = false; pthread_mutex_unlock(&h_call_mtx);
	struct pic p640 = pic_make(640, 480, RWJ_FMT_NV12, 640, 0, 90);
	CHECK(h_enc(&p640.j) == 0);
	CHECK(p640.j.out_status == 0);
	CHECK(p640.j.out_bytes == 4907u);                                  /* 100 + 640*480/64 + quality code 7 */
	CHECK(p640.j.out_bytes < hw_dma_size);
	CHECK(p640.j.out_hw_us > 0);
	check_pack(&p640);
	check_out(&p640, p640.j.out_bytes);
	CHECK(R(W_ENC, 0x154) == ((640u << 16) | 480u));
	CHECK(R(W_ENC, 0x178) == 640u && R(W_ENC, 0x17c) == 640u);
	CHECK(R(W_ENC, 0x170) == 0xd8000000u && R(W_ENC, 0x174) == 0xd8000000u + 640u * 480u);
	CHECK(R(W_ENC, 0x10c) == 7199u);
	CHECK(R(W_ENC, 0x120) == 0xd9800000u && R(W_ENC, 0x128) == 0xda7ff000u);
	CHECK(R(W_ENC, 0x138) == 0 && R(W_ENC, 0x16c) == 0);
	CHECK((R(W_ENC, 0x108) & 0xffffu) == 7u);
	CHECK(R(W_ENC, 0x150) == 0);
	CHECK(R(W_ENC, 0x104) == 0x20100015u);                             /* enable, int, NV12, padding, no EXIF/restart */
	CHECK(R(W_ENC, 0x100) == 1 && R(W_ENC, 0x314) == 0);
	/* the power-on sequence, exactly the model's, in this order */
	CHECK(trace_has(W_SPM, 0x000, 0xb160001u));
	CHECK(trace_count_val(W_SPM, 0x000, 0xb160001u) == 1);
	CHECK(trace_has(W_SPM, 0x344, 0x100du));
	CHECK(trace_has(W_INFRA, 0x2d8, 1u << 27) && trace_has(W_INFRA, 0xdd0, 2u) && trace_has(W_INFRA, 0x2d8, 1u << 26) && trace_has(W_INFRA, 0xdd0, 1u));
	CHECK(trace_has(W_CG, 0x4, 0x4u) && trace_has(W_CG, 0x4, 0x10000111u));
	CHECK(trace_has(W_LARB, 0x24, 0x300256u) && trace_has(W_LARB, 0x40, 1u) && trace_has(W_LARB, 0x524, 0x300256u));
	CHECK(trace_has(W_LARB, 0x21c, 1u) && trace_has(W_LARB, 0x220, 1u) && trace_has(W_LARB, 0x224, 4u) && trace_has(W_LARB, 0x228, 4u));
	CHECK(R(W_LARB, 0x39c) == 0 && R(W_LARB, 0x3a0) == 0 && R(W_LARB, 0x3a4) == 0 && R(W_LARB, 0x3a8) == 0);
	CHECK(R(W_LARB, 0x380) == 0x5501u && R(W_LARB, 0x398) == 0x5501u && R(W_LARB, 0x3ac) == 0x5501u);
	CHECK(R(W_LARB, 0x24) == 0x300256u && R(W_LARB, 0x40) == 1u && R(W_LARB, 0x524) == 0x300256u);
	CHECK(R(W_CG, 0) == 0x10000111u);
	CHECK(R(W_SPM, 0x344) == 0x000du && dom_on());
	CHECK(R(W_INFRA, 0x2d0) == 0 && R(W_INFRA, 0x2ec) == 0x51554815u);
	CHECK(R(W_INFRA, 0xdc8) == 0 && R(W_INFRA, 0xdd8) == 0x140u);
	CHECK(trace_idx(W_SPM, 0x000, 0xb160001u) < trace_idx(W_CG, 0x4, 0x10000111u));
	CHECK(trace_idx(W_CG, 0x4, 0x10000111u) < trace_idx(W_LARB, 0x24, 0x300256u));
	CHECK(trace_idx(W_LARB, 0x24, 0x300256u) < trace_idx(W_ENC, 0x100, 1u));
	CHECK(trace_idx(W_ENC, 0x100, 1u) < trace_idx(W_ENC, 0x104, 0x20100015u));
	CHECK(trace_count(W_SMI) == 0);
	memset(&inf, 0, sizeof(inf)); inf.abi_version = 1;
	CHECK(h_inf(&inf) == 0 && inf.powered == 1 && inf.jobs_done == 1 && inf.jobs_failed == 0 && inf.last_hw_us == p640.j.out_hw_us);

	/* ---- D. a second job while powered repeats no power/bus write ---- */
	trace_reset();
	struct pic p2 = pic_make(640, 480, RWJ_FMT_NV12, 640, 0, 85);
	CHECK(h_enc(&p2.j) == 0);
	CHECK(p2.j.out_status == 0 && (R(W_ENC, 0x108) & 0xffffu) == 2u);
	CHECK(model_starts == 2);
	CHECK(trace_count(W_SPM) == 0 && trace_count(W_INFRA) == 0 && trace_count(W_TOP) == 0 && trace_count(W_CG) == 0 && trace_count(W_LARB) == 0);
	CHECK(trace_count(W_ENC) > 0);
	CHECK(dom_on());
	pic_free(&p2);

	/* ---- D2. the header's maximum picture: 4096x3072 NV12 (12.6 MP, 18 MiB packed) ---- */
	{
		struct pic max = pic_make(4096, 3072, RWJ_FMT_NV12, 4096, 0, 90);
		CHECK(h_enc(&max.j) == 0);
		CHECK(max.j.out_status == 0);
		CHECK(max.j.out_bytes == 196715u);                            /* 100 + 4096*3072/64 + 7 */
		CHECK(R(W_ENC, 0x10c) == 294911u);
		CHECK(R(W_ENC, 0x178) == 4096u && R(W_ENC, 0x174) == 0xd8c00000u);
		check_pack(&max);
		check_out(&max, max.j.out_bytes);
		pic_free(&max);
	}

	/* ---- E. formats, padding, geometry ---- */
	const uint32_t fmts[4] = { RWJ_FMT_NV12, RWJ_FMT_NV21, RWJ_FMT_YUYV, RWJ_FMT_YVYU };
	for (unsigned f = 0; f < 4; f++) {
		uint32_t w = 642, h = 482;
		bool yuyv = fmts[f] == RWJ_FMT_YUYV || fmts[f] == RWJ_FMT_YVYU;
		struct pic pp = pic_make(w, h, fmts[f], yuyv ? 2u * w + 6u : w + 6u, 0, 60);
		CHECK(h_enc(&pp.j) == 0);
		CHECK(pp.j.out_status == 0);
		CHECK((R(W_ENC, 0x104) & 0x18u) == (fmts[f] << 3));
		CHECK(R(W_ENC, 0x178) == (yuyv ? 2u * 656u : 656u));
		CHECK(R(W_ENC, 0x17c) == (yuyv ? 2u * 656u : 656u));
		uint32_t exp_blk = yuyv ? (656u >> 4) * (488u >> 3) * 4u - 1u : (656u >> 4) * (496u >> 4) * 6u - 1u;
		CHECK(R(W_ENC, 0x10c) == exp_blk);
		CHECK(R(W_ENC, 0x174) == (yuyv ? 0xd8000000u + 2u * 656u * 488u : 0xd8000000u + 656u * 496u));
		check_pack(&pp);
		check_out(&pp, pp.j.out_bytes);
		CHECK(((uint8_t *)ram)[0x1800006] == 1u);                  /* q60 */
		CHECK(((uint8_t *)ram)[0x1800007] == (uint8_t)fmts[f]);
		pic_free(&pp);
	}

	/* ---- F. the quality table, literal codes from mtk_jpeg_core.c:1148 ---- */
	static const struct { uint32_t q, code; } qt[] = {
		{ 1, 4 }, { 37, 4 }, { 38, 0 }, { 43, 0 }, { 44, 0x0e }, { 51, 0x0e }, { 52, 0x0a }, { 59, 0x0a },
		{ 60, 1 }, { 71, 1 }, { 72, 0x0f }, { 77, 0x0f }, { 78, 6 }, { 84, 6 }, { 85, 2 }, { 89, 2 },
		{ 90, 7 }, { 94, 7 }, { 95, 3 }, { 96, 3 }, { 97, 0x10 }, { 100, 0x10 },
	};
	for (unsigned i = 0; i < sizeof(qt) / sizeof(qt[0]); i++) {
		struct pic q = pic_make(16, 16, RWJ_FMT_NV12, 16, 0, qt[i].q);
		CHECK(h_enc(&q.j) == 0);
		CHECK((R(W_ENC, 0x108) & 0xffffu) == qt[i].code);
		CHECK(((uint8_t *)ram)[0x1800006] == (uint8_t)qt[i].code);
		pic_free(&q);
	}

	/* ---- G. restart interval and EXIF flag ---- */
	{
		struct pic g = pic_make(64, 64, RWJ_FMT_NV12, 64, 0, 90);
		g.j.restart_interval = 7;
		CHECK(h_enc(&g.j) == 0);
		CHECK((R(W_ENC, 0x104) & (1u << 10)) != 0 && R(W_ENC, 0x150) == 7u);
		CHECK(((uint8_t *)ram)[0x1800008] == 7u && ((uint8_t *)ram)[0x1800009] == 0u);
		g.j.restart_interval = 0; g.j.flags = RWJ_FLAG_EXIF_GAP;
		CHECK(h_enc(&g.j) == 0);
		CHECK((R(W_ENC, 0x104) & (1u << 10)) == 0 && R(W_ENC, 0x150) == 0u);
		CHECK((R(W_ENC, 0x104) & (1u << 5)) != 0);
		g.j.flags = 0; g.j.restart_interval = 1;
		CHECK(h_enc(&g.j) == 0);
		CHECK((R(W_ENC, 0x104) & (1u << 10)) != 0 && (R(W_ENC, 0x104) & (1u << 5)) == 0);
		pic_free(&g);
	}

	/* ---- H. the idle power-off thread call ---- */
	{
		uint64_t last = n2jd.last_job_ns;
		__atomic_store_n(&h_now_ns, last + 2900000000ULL, __ATOMIC_SEQ_CST);
		test_timers();
		CHECK(n2jd.powered && dom_on());
		trace_reset();
		__atomic_store_n(&h_now_ns, last + 3100000000ULL, __ATOMIC_SEQ_CST);
		test_timers();
		CHECK(!n2jd.powered && !dom_on());
		CHECK(R(W_SPM, 0x344) == 0x1112u);
		CHECK((R(W_INFRA, 0x2d0) >> 26) == 3u && R(W_INFRA, 0xdc8) == 3u);
		CHECK(R(W_INFRA, 0x2ec) == 0x5d554815u && R(W_INFRA, 0xdd8) == 0x143u);
		CHECK(R(W_CG, 0) == 0 && !(R(W_SPM, 0x16c) & (1u << 17)));
		CHECK(trace_has(W_CG, 0x8, 0x10000111u));
		uint32_t cv = 0;
		CHECK(trace_last(W_SPM, 0x344, &cv) && cv == 0x1112u);
		CHECK(trace_has(W_INFRA, 0x2d4, 1u << 26) && trace_has(W_INFRA, 0xdcc, 1u));
		pthread_mutex_lock(&h_call_mtx); CHECK(!h_call_slot.pending); pthread_mutex_unlock(&h_call_mtx);
		memset(&inf, 0, sizeof(inf)); inf.abi_version = 1;
		CHECK(h_inf(&inf) == 0 && inf.powered == 0);
		/* and on again on the next job */
		trace_reset();
		struct pic on = pic_make(64, 64, RWJ_FMT_NV12, 64, 0, 90);
		CHECK(h_enc(&on.j) == 0);
		CHECK(trace_has(W_SPM, 0x000, 0xb160001u) && trace_has(W_CG, 0x4, 0x10000111u) && dom_on());
		pic_free(&on);
	}

	/* ---- I. power and bus faults ---- */
	struct pic fault = pic_make(64, 64, RWJ_FMT_NV12, 64, 0, 90);
	uint32_t failed0;
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	memset(&inf, 0, sizeof(inf)); inf.abi_version = 1; CHECK(h_inf(&inf) == 0); failed0 = inf.jobs_failed;
	trace_reset();
	F.dis_off = true; spm_update();
	CHECK(h_enc(&fault.j) == EIO && K_ENC.out_status == RWJ_E_POWER && K_ENC.out_bytes == 0);
	CHECK(trace_count(W_SPM) == 0 && trace_count(W_CG) == 0 && !n2jd.powered);
	CHECK(R(W_SPM, 0x344) == 0x1112u);
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	trace_reset(); F.no_power_ack = true;
	CHECK(h_enc(&fault.j) == EIO && K_ENC.out_status == RWJ_E_POWER);
	CHECK(R(W_SPM, 0x344) == 0x1112u && (R(W_INFRA, 0x2d0) >> 26) == 3u && trace_count(W_CG) == 0);
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	R(W_TOP, 0xe0) = 0x80000000u; F.mux_dead = true;
	CHECK(h_enc(&fault.j) == EIO && K_ENC.out_status == RWJ_E_POWER); CHECK(R(W_SPM, 0x344) == 0x1112u);
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	R(W_TOP, 0xe0) = 0x80000000u;
	CHECK(h_enc(&fault.j) == 0 && (R(W_TOP, 0xe0) & 0x80000000u) == 0 && ((R(W_TOP, 0xe0) >> 24) & 0xf) == 8u);
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	F.sram_stuck = true; CHECK(h_enc(&fault.j) == EIO && K_ENC.out_status == RWJ_E_POWER);
	CHECK((R(W_INFRA, 0x2d0) >> 26) == 3u && (R(W_INFRA, 0xdc8) & 3u) == 3u);
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	F.larb_stuck = true;
	CHECK(h_enc(&fault.j) == EIO && K_ENC.out_status == RWJ_E_BUS && !n2jd.powered);
	CHECK(R(W_SPM, 0x344) == 0x1112u);                        /* the failed bus setup powered the domain off again */
	memset(&inf, 0, sizeof(inf)); inf.abi_version = 1; CHECK(h_inf(&inf) == 0); CHECK(inf.jobs_failed == failed0 + 5u);
	/* recovery */
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	CHECK(h_enc(&fault.j) == 0 && dom_on());

	/* ---- J. hardware faults ---- */
	struct pic hw = pic_make(640, 480, RWJ_FMT_NV12, 640, 0, 90);
	F.never_done = true;
	CHECK(h_enc(&hw.j) == EIO && K_ENC.out_status == RWJ_E_TIMEOUT && K_ENC.out_hw_us == 2000000u && K_ENC.out_bytes == 0);
	CHECK((R(W_ENC, 0x104) & 1u) == 0);                                /* reset cleared enable */
	F.never_done = false;
	F.stall = true; CHECK(h_enc(&hw.j) == EIO && K_ENC.out_status == RWJ_E_STALL); F.stall = false;
	F.vcodec_irq = true; CHECK(h_enc(&hw.j) == EIO && K_ENC.out_status == RWJ_E_STALL); F.vcodec_irq = false;
	F.bad_dma = true; CHECK(h_enc(&hw.j) == EIO && K_ENC.out_status == RWJ_E_SIZE); F.bad_dma = false;
	F.bad_soi = true; CHECK(h_enc(&hw.j) == EIO && K_ENC.out_status == RWJ_E_SIZE); F.bad_soi = false;
	F.no_eoi = true; CHECK(h_enc(&hw.j) == EIO && K_ENC.out_status == RWJ_E_SIZE); F.no_eoi = false;
	/* 0317: 124 bytes of padding after the EOI (jp5: 42240 failed with "no FF D9 in the last 64 bytes") */
	F.force_n = 42116u;
	CHECK(h_enc(&hw.j) == 0 && hw_dma_size == 42240u && K_ENC.out_bytes == 42116u && hw.j.out_bytes == 42116u);
	CHECK(hw.d[42114] == 0xffu && hw.d[42115] == 0xd9u);
	/* 0317: a stale FF D9 inside the padding (left by a longer job) is not the end: the first one in the window is */
	F.stale_eoi = true;
	CHECK(h_enc(&hw.j) == 0 && hw_dma_size == 42240u && K_ENC.out_bytes == 42116u);
	F.force_n = 42200u;                                           /* 40 bytes of padding, stale EOI at 42236 */
	CHECK(h_enc(&hw.j) == 0 && hw_dma_size == 42240u && K_ENC.out_bytes == 42200u);
	F.stale_eoi = false; F.force_n = 0;
	F.force_n = 200000u;                                          /* > width*height/2 + 4096 = 157696 */
	CHECK(h_enc(&hw.j) == EIO && K_ENC.out_status == RWJ_E_SIZE && K_ENC.out_bytes == 0);
	F.force_n = 0;
	CHECK(h_enc(&hw.j) == 0);

	/* ---- K. copyin / copyout faults ---- */
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	unsigned starts0 = model_starts;
	h_fault_in(fault.y + 5u * fault.j.stride + 3u);
	CHECK(h_enc(&fault.j) == EFAULT && fault.j.out_status == 0 && model_starts == starts0 && !n2jd.powered);
	h_fault_clear();
	struct pic fo = pic_make(64, 64, RWJ_FMT_NV12, 64, 0, 90);
	h_fault_out(fo.d + 10u);
	int eo = h_enc(&fo.j);
	CHECK(eo == EFAULT && K_ENC.out_status == 0 && K_ENC.out_bytes > 10u && fo.j.out_bytes == 0);
	CHECK(model_starts == starts0 + 1);
	for (uint32_t i = 0; i < 10u; i++) { CHECK(fo.d[i] == 0x5a); }
	h_fault_clear();
	h_fault_in((uint8_t *)(uintptr_t)&fo.j);                           /* the ioctl struct itself */
	CHECK(h_enc(&fo.j) == EFAULT);
	h_fault_clear();
	h_fault_out((uint8_t *)(uintptr_t)&fo.j);                          /* only sys_ioctl's final struct copyout */
	CHECK(h_enc(&fo.j) == EFAULT && model_starts == starts0 + 2 && K_ENC.out_status == 0 && K_ENC.out_bytes > 10u);
	h_fault_clear();
	pic_free(&fo);

	/* ---- L. one job at a time: two callers, one running, one sleeping; and EINTR ---- */
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	struct pic ca = pic_make(320, 240, RWJ_FMT_NV12, 320, 0, 90);
	struct pic cb = pic_make(320, 240, RWJ_FMT_NV21, 320, 0, 80);
	{
		struct h_job ja = { &ca, -1 }, jb = { &cb, -1 };
		pthread_t ta, tb;
		F.block_start = true;
		h_release = 0; h_started = 0;
		CHECK(pthread_create(&ta, NULL, job_a, &ja) == 0);
		pthread_mutex_lock(&h_cond_mtx);
		while (!h_started) { pthread_cond_wait(&h_cond_ev, &h_cond_mtx); }
		pthread_mutex_unlock(&h_cond_mtx);
		/* B is now busy and sleeps interruptibly */
		CHECK(pthread_create(&tb, NULL, job_b, &jb) == 0);
		for (int i = 0; i < 200 && __atomic_load_n(&h_waiters, __ATOMIC_SEQ_CST) == 0; i++) { struct timespec ts = { 0, 1000000 }; nanosleep(&ts, NULL); }
		CHECK(__atomic_load_n(&h_waiters, __ATOMIC_SEQ_CST) >= 1);
		CHECK(n2jd_share_ven(h_fake_get, h_fake_put) == EBUSY);
		__atomic_store_n(&h_intr, 1, __ATOMIC_SEQ_CST);
		pthread_join(tb, NULL);
		CHECK(jb.err == EINTR);
		pthread_mutex_lock(&h_cond_mtx); h_release = 1; pthread_cond_broadcast(&h_cond_ev); pthread_mutex_unlock(&h_cond_mtx);
		pthread_join(ta, NULL);
		CHECK(ja.err == 0 && ja.p->j.out_status == 0);
		/* now both really run, in order */
		ja.err = jb.err = -1;
		CHECK(pthread_create(&ta, NULL, job_a, &ja) == 0);
		CHECK(pthread_create(&tb, NULL, job_b, &jb) == 0);
		pthread_join(ta, NULL); pthread_join(tb, NULL);
		CHECK(ja.err == 0 && jb.err == 0);
		CHECK(model_max_active == 1);
	}
	pic_free(&ca); pic_free(&cb);

	/* ---- M. the idle timer while a job is in progress does not power off ---- */
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	struct pic busy = pic_make(320, 240, RWJ_FMT_NV12, 320, 0, 90);
	{
		struct h_job j = { &busy, -1 };
		pthread_t t;
		F.block_start = true; h_release = 0; h_started = 0;
		CHECK(pthread_create(&t, NULL, job_a, &j) == 0);
		pthread_mutex_lock(&h_cond_mtx); while (!h_started) { pthread_cond_wait(&h_cond_ev, &h_cond_mtx); } pthread_mutex_unlock(&h_cond_mtx);
		uint64_t last = n2jd.last_job_ns + 4000000000ULL;
		__atomic_store_n(&h_now_ns, last, __ATOMIC_SEQ_CST);
		test_timers();
		CHECK(dom_on());                                           /* not powered off under the job */
		pthread_mutex_lock(&h_call_mtx); int pending = h_call_slot.pending; pthread_mutex_unlock(&h_call_mtx);
		CHECK(pending);                                            /* it looked again */
		pthread_mutex_lock(&h_cond_mtx); h_release = 1; pthread_cond_broadcast(&h_cond_ev); pthread_mutex_unlock(&h_cond_mtx);
		pthread_join(t, NULL);
		CHECK(j.err == 0 && j.p->j.out_status == 0 && n2jd.powered);
		last = n2jd.last_job_ns;
		__atomic_store_n(&h_now_ns, last + 3100000000ULL, __ATOMIC_SEQ_CST);
		test_timers();
		CHECK(!n2jd.powered && !dom_on());
	}
	pic_free(&busy);

	/* ---- N. hw-k02's VEN reference count ---- */
	model_reset(); n2j.bus_ready = false; n2jd.powered = false;
	{
		CHECK(n2jd_share_ven(NULL, NULL) == EINVAL);
		CHECK(n2jd_share_ven(h_fake_get, h_fake_put) == 0);
		h_gets = h_puts = 0; h_ven_refs = 0;
		struct pic hook = pic_make(64, 64, RWJ_FMT_NV12, 64, 0, 90);
		trace_reset();
		CHECK(h_enc(&hook.j) == 0);
		CHECK(h_gets == 1 && h_puts == 0 && dom_on() && n2jd.powered);
		CHECK((R(W_CG, 0) & 0x100u) == 0x100u);          /* the driver gated the JPEG clock after get */
		uint64_t last = n2jd.last_job_ns;
		__atomic_store_n(&h_now_ns, last + 3100000000ULL, __ATOMIC_SEQ_CST);
		trace_reset();
		test_timers();
		CHECK(h_gets == 1 && h_puts == 1 && !n2jd.powered && dom_on());
		/* the driver gated bit 8 itself, called put once, and ran no power-off sequence of its own */
		CHECK(trace_has(W_CG, 0x8, 0x100u));
		CHECK(trace_count(W_SPM) == 0 && trace_count(W_INFRA) == 0);
		CHECK((R(W_CG, 0) & 0x100u) == 0 && (R(W_CG, 0) & 0x10000011u) == 0x10000011u);
		CHECK(h_enc(&hook.j) == 0);
		CHECK(h_gets == 2 && h_puts == 1 && dom_on() && n2jd.powered);
		CHECK((R(W_CG, 0) & 0x100u) == 0x100u);
		pic_free(&hook);
	}

	/* ---- O. statistics and close ---- */
	memset(&inf, 0, sizeof(inf)); inf.abi_version = 1;
	CHECK(h_inf(&inf) == 0);
	CHECK(inf.jobs_done > 0 && inf.jobs_failed > 0 && inf.last_hw_us > 0 && inf.powered == 1);
	CHECK(n2jd_open(h_dev(), 0, 0, NULL) == 0 && n2jd_close(h_dev(), 0, 0, NULL) == 0);
	CHECK(dom_on());                                                   /* close() does not power off immediately */

	pic_free(&base); pic_free(&p640); pic_free(&fault); pic_free(&hw);
	CHECK(violations == 0);
	printf("N2TEST K01 %s %d/%d (violations %d)\n", fails || violations ? "FAIL" : "PASS", checks - fails, checks, violations);
	return fails || violations ? 1 : 0;
}
