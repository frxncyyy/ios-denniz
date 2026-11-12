/* SPDX-License-Identifier: MIT */
/*
 * Host test of kern/bsd/dev/nord2_vcodec_relay.inc (package IK).  It supplies the kernel primitives the .inc uses
 * (locks, msleep/wakeup, copyin/copyout, devfs, a fake clock and a fake DMA pool) and then exercises the relay exactly
 * as the kernel would: one server process on minor 0, three client processes on minor 1, queues, overflow, timeouts,
 * close notices and the close races, plus the DMA handle access rule.
 *
 * The test never includes the .inc's implementation assumptions: expected values are the documented ABI constants
 * (include/redwood_vcodec.h) and the rules in the .inc's header comment.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define N2VR_HOST 1
typedef unsigned long u_long;
typedef char *caddr_t;
typedef uintptr_t user_addr_t;
#define __unused __attribute__((unused))

static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* ---- kernel primitives -------------------------------------------------- */

/* a fake process table: proc_find() takes a reference, proc_exiting() marks a process that is on its way out, and
 * proc_kill() makes it vanish (the reaper must notice both).  uniqueid is stable across exec and never reused. */
struct proc { int pid; const char *name; atomic_bool alive, exiting; uint64_t uid; atomic_int refs; };
static struct proc srv = { 1, "vcodecd", true, false, 1, 0 };
static struct proc pa = { 2, "plug-in A", true, false, 2, 0 };
static struct proc pb = { 3, "plug-in B", true, false, 3, 0 };
static struct proc pc = { 4, "plug-in C", true, false, 4, 0 };
static struct proc pr = { 5, "close race", true, false, 5, 0 };

#define FAKE_PROCS 24
static struct proc *fake_procs[FAKE_PROCS] = { &srv, &pa, &pb, &pc, &pr };
static unsigned fake_nprocs = 5;
static void proc_register(struct proc *p) { if (fake_nprocs < FAKE_PROCS) { fake_procs[fake_nprocs++] = p; } }

static struct proc *
proc_find(int pid)
{
	for (unsigned i = 0; i < fake_nprocs; i++) {
		struct proc *p = fake_procs[i];
		/* like XNU: proc_find() takes no new reference on a process that is exiting (its references drain) */
		if (p->pid == pid && atomic_load(&p->alive) && !atomic_load(&p->exiting)) {
			atomic_fetch_add(&p->refs, 1);
			return p;
		}
	}
	return NULL;
}
static void proc_rele(struct proc *p) { if (p != NULL) { atomic_fetch_sub(&p->refs, 1); } }
static __attribute__((unused)) int proc_exiting(struct proc *p) { return p == NULL || p->exiting || !p->alive; }
static int proc_pid(struct proc *p) { return p->pid; }
static uint64_t proc_uniqueid(struct proc *p) { return p->uid; }
static void proc_kill(struct proc *p) { p->alive = false; }
static void proc_reset(void)
{
	srv.alive = pa.alive = pb.alive = pc.alive = pr.alive = true;
	srv.exiting = pa.exiting = pb.exiting = pc.exiting = pr.exiting = false;
}

#ifdef minor
#undef minor
#endif
static unsigned minor(dev_t d) { return d & 0xffu; }
static dev_t makedev(unsigned maj, unsigned min) { return (dev_t)((maj << 8) | (min & 0xffu)); }

typedef pthread_mutex_t lck_mtx_t;
#define LCK_GRP_DECLARE(var, name) int var##_declared
#define LCK_MTX_DECLARE(var, grp) pthread_mutex_t var = PTHREAD_MUTEX_INITIALIZER
#define lck_mtx_lock(m) pthread_mutex_lock(m)
#define lck_mtx_unlock(m) pthread_mutex_unlock(m)

static pthread_cond_t n2vr_cond = PTHREAD_COND_INITIALIZER;
static int
msleep(void *chan, pthread_mutex_t *mtx, int pri, const char *wmesg, struct timespec *ts)
{
	(void)chan; (void)pri; (void)wmesg;
	if (ts == NULL) { return pthread_cond_wait(&n2vr_cond, mtx) == 0 ? 0 : EINTR; }
	struct timespec abs;
	(void)clock_gettime(CLOCK_REALTIME, &abs);
	abs.tv_sec += ts->tv_sec;
	abs.tv_nsec += ts->tv_nsec;
	if (abs.tv_nsec >= 1000000000L) { abs.tv_sec++; abs.tv_nsec -= 1000000000L; }
	int r = pthread_cond_timedwait(&n2vr_cond, mtx, &abs);
	return r == ETIMEDOUT ? EWOULDBLOCK : 0;
}
static void wakeup(void *chan) { (void)chan; (void)pthread_cond_broadcast(&n2vr_cond); }

#define N2VR_BAD_USER ((user_addr_t)1u)         /* never a real buffer: copyin/copyout fail */
static int copyin(user_addr_t u, void *k, size_t n) { if (u == N2VR_BAD_USER) { return EFAULT; } memcpy(k, (const void *)(uintptr_t)u, n); return 0; }
static int copyout(const void *k, user_addr_t u, size_t n) { if (u == N2VR_BAD_USER) { return EFAULT; } memcpy((void *)(uintptr_t)u, k, n); return 0; }

static uint64_t
mach_absolute_time(void)
{
	struct timespec t;
	(void)clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void absolutetime_to_nanoseconds(uint64_t t, uint64_t *ns) { *ns = t; }

#ifndef ERESTART
#define ERESTART (-1)
#endif
#ifndef PZERO
#define PZERO 0
#endif
#ifndef PCATCH
#define PCATCH 0x100
#endif

#define DEVFS_CHAR 0
#define UID_ROOT 0
#define GID_WHEEL 0
/* the kernel's eno_* stubs (bsd/sys/conf.h): the host only checks that the slot is filled, so the addresses are dummies */
#define eno_rdwrt ((void *)1)
#define eno_stop ((void *)2)
#define eno_reset ((void *)3)
#define eno_select ((void *)4)
#define eno_mmap ((void *)5)
#define eno_strat ((void *)6)
struct cdevsw {
	int (*d_open)(dev_t, int, int, struct proc *);
	int (*d_close)(dev_t, int, int, struct proc *);
	void *d_read, *d_write;
	int (*d_ioctl)(dev_t, u_long, caddr_t, int, struct proc *);
	void *d_stop, *d_reset, *d_select, *d_mmap, *d_strategy, *d_reserved_1, *d_reserved_2;
	int d_type;
};
static struct { int major_calls; dev_t major; unsigned node_minor, node_perms; char node_name[64]; int node_chrblk; bool node_made; } devfs;

static int
cdevsw_add(int major, const struct cdevsw *sw)
{
	devfs.major_calls++;
	CHECK(sw->d_open != NULL && sw->d_close != NULL && sw->d_ioctl != NULL);
	CHECK(sw->d_read != NULL && sw->d_write != NULL && sw->d_stop != NULL && sw->d_reset != NULL);
	CHECK(sw->d_select != NULL && sw->d_mmap != NULL && sw->d_strategy != NULL);
	return major < 0 ? 42 : major;
}
static void *
devfs_make_node(dev_t dev, int chrblk, uid_t uid, gid_t gid, int perms, const char *fmt, ...)
{
	(void)uid; (void)gid;
	devfs.node_minor = minor(dev);
	devfs.node_perms = (unsigned)perms;
	devfs.node_chrblk = chrblk;
	va_list ap; va_start(ap, fmt); (void)vsnprintf(devfs.node_name, sizeof(devfs.node_name), fmt, ap); va_end(ap);
	devfs.node_made = true;
	return &devfs;
}

/* nord2_vcodec.inc's per-client cleanup; the relay must call it exactly once, at the last close of a process */
void n2vc_relay_closed(struct proc *p);
static struct proc *closed_proc;
static int closed_calls;
void
n2vc_relay_closed(struct proc *p)
{
	closed_calls++;
	closed_proc = p;
}

#include "redwood_vcodec.h"
#include "../bsd/dev/nord2_vcodec_relay.inc"

/* ---- a fake hardware side: the two callbacks nord2_vcodec.inc supplies -- */

struct fake_dma { bool used; uint32_t handle; uint32_t bytes, flags; unsigned owner_minor; struct proc *owner; };
static struct fake_dma pool[16];
static uint32_t next_handle = 100;
static unsigned hw_info_calls;

static int
fake_hw_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	(void)flag;
	switch (cmd) {
	case RWVC_DMA_ALLOC: {
		struct rwvc_dma_alloc *a = (struct rwvc_dma_alloc *)data;
		if (a->bytes == 0 || a->bytes > 0x100000u || (a->flags & ~1u) != 0) { return EINVAL; }
		for (unsigned i = 0; i < 16; i++) {
			if (!pool[i].used) {
				pool[i].used = true; pool[i].handle = next_handle++; pool[i].bytes = a->bytes; pool[i].flags = a->flags;
				pool[i].owner_minor = minor(dev); pool[i].owner = p;
				a->out_handle = pool[i].handle;
				a->out_pa = RW_CLAIM_VCODEC_PA + i * 0x10000u;
				return 0;
			}
		}
		return ENOMEM;
	}
	case RWVC_DMA_FREE:
	case RWVC_DMA_RW: {
		uint32_t h = cmd == RWVC_DMA_FREE ? ((struct rwvc_dma_free *)data)->handle : ((struct rwvc_dma_rw *)data)->handle;
		for (unsigned i = 0; i < 16; i++) {
			if (pool[i].used && pool[i].handle == h) {
				if (!n2vr_dma_allowed(pool[i].owner_minor, pool[i].owner, pool[i].flags, minor(dev), p)) { return EACCES; }
				if (cmd == RWVC_DMA_FREE) { pool[i].used = false; }
				return 0;
			}
		}
		return EINVAL;
	}
	case RWVC_DMA_INFO: {
		/* header rev 2: EINVAL handle 0, ENOENT unknown, EPERM not usable by the caller; same rule as DMA_RW */
		struct rwvc_dma_info *inf = (struct rwvc_dma_info *)data;
		if (inf->handle == 0) { return EINVAL; }
		for (unsigned i = 0; i < 16; i++) {
			if (pool[i].used && pool[i].handle == inf->handle) {
				if (!n2vr_dma_allowed(pool[i].owner_minor, pool[i].owner, pool[i].flags, minor(dev), p)) { return EPERM; }
				inf->bytes = pool[i].bytes;
				inf->out_pa = RW_CLAIM_VCODEC_PA + i * 0x10000u;
				inf->reserved = 0;
				return 0;
			}
		}
		return ENOENT;
	}
	default: return ENOTTY;
	}
}
static void
fake_hw_info(struct rwvc_info *out)
{
	hw_info_calls++;
	out->powered_mask = 1u;
	out->pool_pa = RW_CLAIM_VCODEC_PA;
	out->pool_bytes = RW_CLAIM_VCODEC_BYTES;
	out->pool_free = 4321u;
	out->irq_mode = 0;
}
static const struct n2vr_hw hw = { .ioctl = fake_hw_ioctl, .info = fake_hw_info };

/* ---- message helpers ---------------------------------------------------- */

static dev_t server_dev(void) { return makedev(42, 0); }
static dev_t client_dev(void) { return makedev(42, 1); }

static int
send_(dev_t dev, struct proc *p, const void *buf, uint32_t bytes, uint32_t client)
{
	struct rwvc_msg m = { .user = (user_addr_t)(uintptr_t)buf, .bytes = bytes, .client = client, .timeout_ms = 0 };
	return n2vr_ioctl(dev, RWVC_MSG_SEND, (caddr_t)&m, 0, p);
}
static int
recv_(dev_t dev, struct proc *p, void *buf, uint32_t cap, uint32_t timeout_ms, uint32_t *bytes, uint32_t *client)
{
	struct rwvc_msg m = { .user = (user_addr_t)(uintptr_t)buf, .bytes = cap, .timeout_ms = timeout_ms };
	int e = n2vr_ioctl(dev, RWVC_MSG_RECV, (caddr_t)&m, 0, p);
	if (bytes != NULL) { *bytes = m.bytes; }
	if (client != NULL) { *client = m.client; }
	return e;
}
static void
client_reset_all(void)
{
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) { if (n2vr.c[i].state != N2VR_SLOT_FREE) { n2vr_slot_free(&n2vr.c[i]); } }
	n2vr.server_head = n2vr.server_count = 0;
	n2vr.close_head = n2vr.close_count = 0;
}

/* ---- the sequential tests ----------------------------------------------- */

static void
test_start_and_info(void)
{
	CHECK(!n2vr_start(NULL));
	struct n2vr_hw broken = hw; broken.ioctl = NULL;
	CHECK(!n2vr_start(&broken));
	CHECK(devfs.major_calls == 0);                  /* nothing created for a bad registration */
	CHECK(n2vr_start(&hw));
	CHECK(devfs.node_made);
	CHECK(strcmp(devfs.node_name, "redwood-vcodec-client") == 0);
	CHECK(devfs.node_minor == 1);
	CHECK(devfs.node_perms == 0666);
	CHECK(devfs.node_chrblk == DEVFS_CHAR);
	CHECK(n2vr_start(&hw));                         /* idempotent */
	CHECK(devfs.major_calls == 1);
	struct proc ghost = { 99, "gone process", false, false, 99, 0 };
	/* q10je: open takes no process reference (it would block the client's exit); a slot is made by identity and
	 * freed by its close */
	CHECK(n2vr_open(client_dev(), 0, 0, &ghost) == 0);
	CHECK(atomic_load(&ghost.refs) == 0);
	CHECK(n2vr_close(client_dev(), 0, 0, &ghost) == 0);
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) { CHECK(n2vr.c[i].state == N2VR_SLOT_FREE); }

	CHECK(n2vr_relay_cmd(RWVC_MSG_SEND) && n2vr_relay_cmd(RWVC_MSG_RECV));
	CHECK(!n2vr_relay_cmd(RWVC_INFO) && !n2vr_relay_cmd(RWVC_DMA_ALLOC) && !n2vr_relay_cmd(RWVC_POWER));
	CHECK(!n2vr_relay_cmd(RWVC_DMA_INFO));           /* the relay's cdevsw is minor 1; K02's minor 0 answers DMA_INFO */
	CHECK(n2vr_server_present() == 0);

	struct rwvc_info info;
	memset(&info, 0xee, sizeof(info));
	CHECK(n2vr_ioctl(client_dev(), RWVC_INFO, (caddr_t)&info, 0, &pa) == 0);
	CHECK(info.abi_version == RWVC_ABI_VERSION && info.server_present == 0);
	CHECK(info.powered_mask == 1 && info.pool_pa == RW_CLAIM_VCODEC_PA && info.pool_bytes == RW_CLAIM_VCODEC_BYTES);
	CHECK(info.pool_free == 4321 && info.irq_mode == 0);
	for (unsigned i = 0; i < 8; i++) { CHECK(info.reserved[i] == 0); }
	CHECK(hw_info_calls == 1);

	n2vr_server_open();
	CHECK(n2vr_server_present() == 1);
	CHECK(n2vr_ioctl(client_dev(), RWVC_INFO, (caddr_t)&info, 0, &pa) == 0 && info.server_present == 1);

	/* minor 1 is the client policy: hardware ioctls are refused, minor 0's INFO belongs to nord2_vcodec.inc */
	CHECK(n2vr_ioctl(client_dev(), RWVC_POWER, (caddr_t)&info, 0, &pa) == EACCES);
	CHECK(n2vr_ioctl(client_dev(), RWVC_REGS, (caddr_t)&info, 0, &pa) == EACCES);
	CHECK(n2vr_ioctl(client_dev(), RWVC_WAIT_IRQ, (caddr_t)&info, 0, &pa) == EACCES);
	CHECK(n2vr_ioctl(client_dev(), RWVC_IRQ_CLEAR, (caddr_t)&info, 0, &pa) == EACCES);
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_FILL, (caddr_t)&info, 0, &pa) == EACCES);
	CHECK(n2vr_ioctl(server_dev(), RWVC_INFO, (caddr_t)&info, 0, &srv) == ENOTTY);
	struct rwvc_dma_info di = { .handle = 1 };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&di, 0, &pa) == EBADF);  /* no client slot yet */
	CHECK(n2vr_ioctl(server_dev(), RWVC_DMA_INFO, (caddr_t)&di, 0, &srv) == ENOTTY); /* K02's minor 0 cdevsw owns it */
	CHECK(n2vr_ioctl(server_dev(), RWVC_MSG_SEND, NULL, 0, &srv) == EINVAL);
	CHECK(n2vr_ioctl(client_dev(), RWVC_MSG_RECV, NULL, 0, &pa) == EINVAL);
}

static void
test_single_client(void)
{
	client_reset_all();
	char out[64];
	uint32_t bytes = 0, from = 0;
	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	uint32_t id = n2vr_by_proc(&pa, N2VR_SLOT_OPEN)->id;
	CHECK(send_(client_dev(), &pa, "hello", 5, 0) == 0);
	CHECK(send_(client_dev(), &pa, "x", 0, 0) == EINVAL);                 /* no empty client datagram: 0 = close */
	CHECK(send_(client_dev(), &pa, "x", RWVC_MSG_MAX + 1, 0) == EINVAL);
	memset(out, 0, sizeof(out));
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0);
	CHECK(bytes == 5 && from == id && memcmp(out, "hello", 5) == 0);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == EAGAIN);

	CHECK(send_(server_dev(), &srv, "reply", 5, id) == 0);
	CHECK(recv_(client_dev(), &pa, out, sizeof(out), 0, &bytes, &from) == 0);
	CHECK(bytes == 5 && from == id && memcmp(out, "reply", 5) == 0);
	CHECK(recv_(client_dev(), &pa, out, sizeof(out), 0, &bytes, &from) == EAGAIN);
	CHECK(send_(server_dev(), &srv, "x", 1, 0) == EINVAL);                /* destination required */
	CHECK(send_(server_dev(), &srv, "x", 1, 9999) == ESRCH);
	CHECK(send_(server_dev(), &srv, "x", RWVC_MSG_MAX + 1, 1) == EINVAL);
}

static void
test_queues(void)
{
	client_reset_all();
	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	uint32_t id = n2vr_by_proc(&pa, N2VR_SLOT_OPEN)->id;
	uint8_t buf[RWVC_MSG_MAX], out[RWVC_MSG_MAX];
	uint32_t bytes = 0, from = 0;
	/* 16 toward the server, the 17th EAGAIN, per-client cap */
	for (unsigned i = 0; i < N2VR_CLIENT_QUEUE; i++) {
		memset(buf, (int)i, sizeof(buf));
		CHECK(send_(client_dev(), &pa, buf, 8, 0) == 0);
	}
	CHECK(send_(client_dev(), &pa, buf, 8, 0) == EAGAIN);
	for (unsigned i = 0; i < N2VR_CLIENT_QUEUE; i++) {
		CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0);
		CHECK(bytes == 8 && from == id && out[0] == (uint8_t)i);
	}
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == EAGAIN);
	/* 16 toward the client, the 17th EAGAIN */
	for (unsigned i = 0; i < N2VR_CLIENT_QUEUE; i++) { CHECK(send_(server_dev(), &srv, buf, 8, id) == 0); }
	CHECK(send_(server_dev(), &srv, buf, 8, id) == EAGAIN);
	for (unsigned i = 0; i < N2VR_CLIENT_QUEUE; i++) { CHECK(recv_(client_dev(), &pa, out, sizeof(out), 0, &bytes, &from) == 0); CHECK(bytes == 8 && from == id); }
	CHECK(recv_(client_dev(), &pa, out, sizeof(out), 0, &bytes, &from) == EAGAIN);
}

static void
test_short_buffer(void)
{
	client_reset_all();
	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	uint32_t id = n2vr_by_proc(&pa, N2VR_SLOT_OPEN)->id;
	uint8_t big[64], small[4];
	uint32_t bytes = 0, from = 0;
	CHECK(send_(client_dev(), &pa, "abcdefgh", 8, 0) == 0);
	CHECK(recv_(server_dev(), &srv, small, sizeof(small), 0, &bytes, &from) == EMSGSIZE);
	CHECK(recv_(server_dev(), &srv, big, sizeof(big), 0, &bytes, &from) == 0);   /* still queued, not lost */
	CHECK(bytes == 8 && memcmp(big, "abcdefgh", 8) == 0);
	CHECK(send_(server_dev(), &srv, "01234567", 8, id) == 0);
	CHECK(recv_(client_dev(), &pa, small, sizeof(small), 0, &bytes, &from) == EMSGSIZE);
	CHECK(recv_(client_dev(), &pa, big, sizeof(big), 0, &bytes, &from) == 0 && bytes == 8 && memcmp(big, "01234567", 8) == 0);
}

static void
test_timeouts(void)
{
	client_reset_all();
	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	uint8_t out[32];
	uint32_t bytes = 0, from = 0;
	uint64_t t0, t1;
	t0 = mach_absolute_time();
	CHECK(recv_(client_dev(), &pa, out, sizeof(out), 100, &bytes, &from) == ETIMEDOUT);
	t1 = mach_absolute_time();
	CHECK((t1 - t0) / 1000000ull >= 80);           /* it really waited (host scheduling may add) */
	CHECK(recv_(client_dev(), &pa, out, sizeof(out), 0, &bytes, &from) == EAGAIN);
	CHECK(recv_(client_dev(), &pa, out, sizeof(out), N2VR_RECV_MAX_MS + 1, &bytes, &from) == EINVAL);
	CHECK(recv_(client_dev(), &pa, out, 0, 0, &bytes, &from) == EINVAL);
	CHECK(recv_(client_dev(), &pa, NULL, sizeof(out), 0, &bytes, &from) == EINVAL);
}

struct blocker { struct proc *p; int error; char got[32]; uint32_t bytes, from; };

static void *
blocking_recv(void *arg)
{
	struct blocker *b = (struct blocker *)arg;
	b->error = recv_(client_dev(), b->p, b->got, sizeof(b->got), 2000, &b->bytes, &b->from);
	return NULL;
}

static void
test_blocking(void)
{
	client_reset_all();
	struct blocker b = { .p = &pb };
	pthread_t t;
	CHECK(n2vr_open(client_dev(), 0, 0, &pb) == 0);
	CHECK(pthread_create(&t, NULL, blocking_recv, &b) == 0);
	(void)usleep(50000);
	CHECK(send_(server_dev(), &srv, "wake", 4, n2vr_by_proc(&pb, N2VR_SLOT_OPEN)->id) == 0);
	CHECK(pthread_join(t, NULL) == 0);
	CHECK(b.error == 0 && b.bytes == 4 && b.from == n2vr_by_proc(&pb, N2VR_SLOT_OPEN)->id && memcmp(b.got, "wake", 4) == 0);
}

static void
test_close_notice(void)
{
	client_reset_all();
	uint8_t out[32];
	uint32_t bytes = 1, from = 0;
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == 0);
	uint32_t old = n2vr_by_proc(&pc, N2VR_SLOT_OPEN)->id;
	CHECK(n2vr_close(client_dev(), 0, 0, &pc) == 0);
	CHECK(n2vr_by_proc(&pc, N2VR_SLOT_OPEN) == NULL);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0);
	CHECK(bytes == 0 && from == old);               /* the synthetic close datagram */
	CHECK(n2vr_by_proc(&pc, N2VR_SLOT_OPEN) == NULL);
	/* id is not reused while its notice is queued; after the server reads it the slot is free */
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == 0);
	CHECK(n2vr_by_proc(&pc, N2VR_SLOT_OPEN)->id != old);
	CHECK(n2vr_close(client_dev(), 0, 0, &pc) == 0);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && bytes == 0);
	/* data before close keeps its order */
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == 0);
	uint32_t last = n2vr_by_proc(&pc, N2VR_SLOT_OPEN)->id;
	CHECK(send_(client_dev(), &pc, "one", 3, 0) == 0);
	CHECK(send_(client_dev(), &pc, "two", 3, 0) == 0);
	CHECK(send_(client_dev(), &pc, "tri", 3, 0) == 0);
	CHECK(n2vr_close(client_dev(), 0, 0, &pc) == 0);
	for (unsigned i = 0; i < 3; i++) {
		CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0);
		CHECK(bytes == 3 && from == last && memcmp(out, i == 0 ? "one" : i == 1 ? "two" : "tri", 3) == 0);
	}
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && bytes == 0 && from == last);
}

static void
test_server_close(void)
{
	client_reset_all();
	uint8_t out[32];
	uint32_t bytes = 0, from = 0;
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == 0);
	CHECK(send_(client_dev(), &pc, "lost", 4, 0) == 0);
	n2vr_server_close();
	CHECK(n2vr_server_present() == 0);
	CHECK(recv_(client_dev(), &pc, out, sizeof(out), 100, &bytes, &from) == ENXIO);
	CHECK(send_(client_dev(), &pc, "late", 4, 0) == ENXIO);
	n2vr_server_open();
	CHECK(n2vr_server_present() == 1);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == EAGAIN);   /* the queue was dropped at close */
	/* server open/close is reference counted like the fd */
	n2vr_server_open();
	n2vr_server_close();
	CHECK(n2vr_server_present() == 1);
	n2vr_server_close();
	CHECK(n2vr_server_present() == 0);
	n2vr_server_open();
}

static void
test_limits(void)
{
	client_reset_all();
	static struct proc extra[N2VR_MAX_CLIENTS];
	char name[16];
	/* free every slot first (close notices queued and read) */
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) {
		if (n2vr.c[i].state == N2VR_SLOT_OPEN) {
			struct proc *p = n2vr.c[i].proc;
			CHECK(n2vr_close(client_dev(), 0, 0, p) == 0);
		}
	}
	uint8_t out[8]; uint32_t bytes, from;
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) { (void)recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from); }
	CHECK(n2vr.server_count == 0 && n2vr.close_count == 0);

	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) {
		extra[i].pid = 100 + (int)i;
		extra[i].alive = true;
		extra[i].exiting = false;
		extra[i].uid = 1000 + i;
		extra[i].refs = 0;
		(void)snprintf(name, sizeof(name), "extra %u", i);
		extra[i].name = strdup(name);
		proc_register(&extra[i]);
		CHECK(n2vr_open(client_dev(), 0, 0, &extra[i]) == 0);
	}
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == EBUSY);
	/* closing one and reading its notice frees the slot for a new client */
	CHECK(n2vr_close(client_dev(), 0, 0, &extra[0]) == 0);
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == EBUSY);                /* the notice is not read yet */
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && bytes == 0);
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == 0);
	CHECK(n2vr_close(client_dev(), 0, 0, &pc) == 0);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && bytes == 0);
	for (unsigned i = 1; i < N2VR_MAX_CLIENTS; i++) { CHECK(n2vr_close(client_dev(), 0, 0, &extra[i]) == 0); }
	for (unsigned i = 1; i < N2VR_MAX_CLIENTS; i++) { CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && bytes == 0); }
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) { free((void *)extra[i].name); }
}

static void
test_two_opens_one_client(void)
{
	client_reset_all();
	uint8_t out[16]; uint32_t bytes = 0, from = 0;
	CHECK(n2vr_open(client_dev(), 0, 0, &pb) == 0);
	uint32_t id1 = n2vr_by_proc(&pb, N2VR_SLOT_OPEN)->id;
	CHECK(n2vr_open(client_dev(), 0, 0, &pb) == 0);          /* d_open runs per open ... */
	CHECK(n2vr_by_proc(&pb, N2VR_SLOT_OPEN)->id == id1);     /* ... one process is one client */
	int live = closed_calls;
	n2vr_reap();                                             /* alive: no notice, even after one descriptor closes */
	CHECK(closed_calls == live);
	CHECK(n2vr_by_proc(&pb, N2VR_SLOT_OPEN) != NULL && n2vr.close_count == 0);
	CHECK(send_(client_dev(), &pb, "still", 5, 0) == 0);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && from == id1 && bytes == 5);
	int before = closed_calls;
	/* d_close reaches the device only once, when the last descriptor closes (spec_vnops.c:2760): one notice */
	CHECK(n2vr_close(client_dev(), 0, 0, &pb) == 0);
	CHECK(closed_calls == before + 1 && closed_proc == &pb);
	CHECK(n2vr_by_proc(&pb, N2VR_SLOT_OPEN) == NULL);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && bytes == 0 && from == id1);
}

static void
test_dma_rule(void)
{
	client_reset_all();
	struct rwvc_dma_alloc a = { .bytes = 4096, .flags = 0 };
	struct rwvc_dma_rw rw = { 0 };
	struct rwvc_dma_free fr = { 0 };
	CHECK(n2vr_dma_allowed(1, &pa, 0, 1, &pa));
	CHECK(!n2vr_dma_allowed(1, &pa, 0, 1, &pb));
	CHECK(n2vr_dma_allowed(1, &pa, RWVC_DMA_SHARED, 0, &srv));
	CHECK(!n2vr_dma_allowed(1, &pa, 0, 0, &srv));
	CHECK(n2vr_dma_allowed(0, &srv, RWVC_DMA_SHARED, 1, &pa));
	CHECK(!n2vr_dma_allowed(0, &srv, 0, 1, &pa));
	CHECK(n2vr_dma_allowed(0, &srv, 0, 0, &srv));

	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_ALLOC, (caddr_t)&a, 0, &pa) == 0);
	CHECK(a.out_handle != 0 && a.out_pa == RW_CLAIM_VCODEC_PA);
	rw.handle = a.out_handle; rw.bytes = 16;
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_RW, (caddr_t)&rw, 0, &pa) == 0);        /* own */
	CHECK(fake_hw_ioctl(server_dev(), RWVC_DMA_RW, (caddr_t)&rw, 0, &srv) == EACCES);   /* not shared */
	CHECK(fake_hw_ioctl(client_dev(), RWVC_DMA_RW, (caddr_t)&rw, 0, &pb) == EACCES);    /* another client */
	fr.handle = a.out_handle;
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_FREE, (caddr_t)&fr, 0, &pa) == 0);

	struct rwvc_dma_alloc b = { .bytes = 8192, .flags = RWVC_DMA_SHARED };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_ALLOC, (caddr_t)&b, 0, &pa) == 0);
	rw.handle = b.out_handle;
	CHECK(fake_hw_ioctl(server_dev(), RWVC_DMA_RW, (caddr_t)&rw, 0, &srv) == 0);        /* shared to minor 0 */
	CHECK(fake_hw_ioctl(client_dev(), RWVC_DMA_RW, (caddr_t)&rw, 0, &pb) == EACCES);    /* never to another client */
	fr.handle = b.out_handle;
	CHECK(fake_hw_ioctl(server_dev(), RWVC_DMA_FREE, (caddr_t)&fr, 0, &srv) == 0);
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_ALLOC, (caddr_t)&(struct rwvc_dma_alloc){ .bytes = 0 }, 0, &pa) == EINVAL);
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_FREE, (caddr_t)&(struct rwvc_dma_free){ .handle = 999 }, 0, &pa) == EINVAL);
	/* after the client's last close a DMA ioctl on its old descriptor can no longer reach the pool */
	CHECK(n2vr_close(client_dev(), 0, 0, &pa) == 0);
	struct rwvc_dma_alloc late = { .bytes = 4096 };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_ALLOC, (caddr_t)&late, 0, &pa) == EBADF);
}

static void
test_dma_info(void)
{
	client_reset_all();
	struct rwvc_dma_alloc a = { .bytes = 12288, .flags = RWVC_DMA_SHARED };
	struct rwvc_dma_alloc b = { .bytes = 4096, .flags = 0 };
	struct rwvc_dma_info in;
	struct rwvc_dma_free fr;

	/* the pool answers only for a live client slot on minor 1 (K02's pool is behind the relay for clients) */
	in = (struct rwvc_dma_info){ .handle = 1 };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pa) == EBADF);

	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	CHECK(n2vr_open(client_dev(), 0, 0, &pb) == 0);
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_ALLOC, (caddr_t)&a, 0, &pa) == 0);
	CHECK(a.out_handle != 0 && a.out_pa == RW_CLAIM_VCODEC_PA);
	uint32_t ha = a.out_handle;

	/* the allocator always sees its own handle */
	in = (struct rwvc_dma_info){ .handle = ha, .bytes = 0xdead, .out_pa = 0xbeef, .reserved = 0xffffffffu };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pa) == 0);
	CHECK(in.bytes == 12288 && in.out_pa == a.out_pa && in.reserved == 0);

	/* the server (minor 0) is the normal caller: a shared client buffer returns pa and size */
	in = (struct rwvc_dma_info){ .handle = ha };
	CHECK(fake_hw_ioctl(server_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &srv) == 0);
	CHECK(in.bytes == 12288 && in.out_pa == a.out_pa);

	/* a second client never gets it, even though the buffer is shared to the other minor */
	in = (struct rwvc_dma_info){ .handle = ha };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pb) == EPERM);

	/* an unshared buffer belongs to its allocator only */
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_ALLOC, (caddr_t)&b, 0, &pa) == 0);
	uint32_t hb = b.out_handle;
	in = (struct rwvc_dma_info){ .handle = hb };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pa) == 0 && in.bytes == 4096 && in.out_pa == b.out_pa);
	in = (struct rwvc_dma_info){ .handle = hb };
	CHECK(fake_hw_ioctl(server_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &srv) == EPERM);

	/* documented errors: handle 0, unknown, freed */
	in = (struct rwvc_dma_info){ .handle = 0 };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pa) == EINVAL);
	in = (struct rwvc_dma_info){ .handle = 0x00ffffffu };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pa) == ENOENT);
	fr.handle = hb;
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_FREE, (caddr_t)&fr, 0, &pa) == 0);
	in = (struct rwvc_dma_info){ .handle = hb };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pa) == ENOENT);

	/* the last close stops the forwarding before the pool is asked */
	fr.handle = ha;
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_FREE, (caddr_t)&fr, 0, &pa) == 0);
	CHECK(n2vr_close(client_dev(), 0, 0, &pb) == 0);
	CHECK(n2vr_close(client_dev(), 0, 0, &pa) == 0);
	in = (struct rwvc_dma_info){ .handle = ha };
	CHECK(n2vr_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&in, 0, &pa) == EBADF);
}

/*
 * XNU calls d_close only for the node's last descriptor anywhere; a client that dies while another one keeps the device
 * open is found by n2vr_reap() at the next relay ioctl.  The dead client's already-sent data keeps its place, then the
 * normal {client, 0} notice arrives.
 */
static void
test_dead_client(void)
{
	client_reset_all();
	uint8_t out[16]; uint32_t bytes = 0, from = 0;
	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	CHECK(n2vr_open(client_dev(), 0, 0, &pb) == 0);
	uint32_t ida = n2vr_by_proc(&pa, N2VR_SLOT_OPEN)->id;
	uint32_t idb = n2vr_by_proc(&pb, N2VR_SLOT_OPEN)->id;
	CHECK(ida != idb);
	CHECK(send_(client_dev(), &pa, "last", 4, 0) == 0);
	int before = closed_calls;
	proc_kill(&pa);                                         /* the process vanishes; no d_close reaches the relay */
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && from == ida && bytes == 4);
	CHECK(memcmp(out, "last", 4) == 0);                     /* data sent before death is not lost */
	CHECK(closed_calls == before + 1);                      /* the reaper cleaned K02's per-client state once */
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && from == ida && bytes == 0);
	CHECK(n2vr_by_id(ida) == NULL);                         /* the notice freed the slot */
	CHECK(send_(server_dev(), &srv, "x", 1, ida) == ESRCH);
	/* the other client is unaffected */
	CHECK(send_(client_dev(), &pb, "live", 4, 0) == 0);
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && from == idb && bytes == 4);
	/* a process that is exiting but still findable is just as dead */
	pb.exiting = true;
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && from == idb && bytes == 0);
	pb.exiting = false;
	proc_reset();
}

static void
test_faults(void)
{
	client_reset_all();
	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	char out[16];
	uint32_t bytes = 0, from = 0;
	CHECK(send_(client_dev(), &pa, (const void *)N2VR_BAD_USER, 4, 0) == EFAULT);
	CHECK(send_(server_dev(), &srv, (const void *)N2VR_BAD_USER, 4, n2vr_by_proc(&pa, N2VR_SLOT_OPEN)->id) == EFAULT);
	CHECK(send_(client_dev(), &pa, "msg", 3, 0) == 0);
	struct rwvc_msg m = { .user = N2VR_BAD_USER, .bytes = sizeof(out) };
	CHECK(n2vr_ioctl(server_dev(), RWVC_MSG_RECV, (caddr_t)&m, 0, &srv) == EFAULT);   /* lost, documented */
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == EAGAIN);
}

/* ---- the concurrent test: 1 server thread, 3 client threads --------------- */

#define CONC_MSGS 50
struct conc_client { struct proc *p; unsigned tag; int error; int sent, received; pthread_t thread; };
struct conc_server { unsigned got; int error; atomic_bool stop; };

struct wire { uint32_t tag, seq; uint8_t pad[8]; };

static struct conc_client cc[3];
static struct conc_server cs;

static int
retry_send(struct proc *p, const struct wire *w, int attempts)
{
	int e;
	for (int i = 0; i < attempts; i++) {
		e = send_(client_dev(), p, w, sizeof(*w), 0);
		if (e != EAGAIN) { return e; }
		(void)usleep(200);
	}
	return EAGAIN;
}

static void *
conc_client_thread(void *arg)
{
	struct conc_client *c = (struct conc_client *)arg;
	for (unsigned i = 0; i < CONC_MSGS; i++) {
		struct wire w = { .tag = c->tag, .seq = i };
		int e = retry_send(c->p, &w, 2000);
		if (e != 0) { c->error = e; return NULL; }
		c->sent++;
		for (int tries = 0; tries < 2000; tries++) {
			uint32_t bytes = 0, from = 0;
			e = recv_(client_dev(), c->p, &w, sizeof(w), 20, &bytes, &from);
			if (e == ETIMEDOUT) { continue; }
			if (e != 0) { c->error = e; return NULL; }
			if (bytes != sizeof(w) || w.tag != c->tag || w.seq != i) { c->error = EPROTO; return NULL; }
			uint32_t id = n2vr_by_proc(c->p, N2VR_SLOT_OPEN)->id;
			if (from != id) { c->error = EPROTO; return NULL; }
			c->received++;
			break;
		}
	}
	return NULL;
}

static void *
conc_server_thread(void *arg)
{
	struct conc_server *s = (struct conc_server *)arg;
	while (!atomic_load(&s->stop) && s->got < 3 * CONC_MSGS) {
		struct wire w; uint32_t bytes = 0, from = 0;
		int e = recv_(server_dev(), &srv, &w, sizeof(w), 50, &bytes, &from);
		if (e == ETIMEDOUT) { continue; }
		if (e != 0) { s->error = e; return NULL; }
		if (bytes != sizeof(w)) { s->error = EPROTO; return NULL; }
		e = send_(server_dev(), &srv, &w, sizeof(w), from);
		for (int i = 0; e == EAGAIN && i < 2000; i++) { (void)usleep(200); e = send_(server_dev(), &srv, &w, sizeof(w), from); }
		if (e != 0) { s->error = e; return NULL; }
		s->got++;
	}
	return NULL;
}

static void
test_concurrent(void)
{
	client_reset_all();
	pthread_t st;
	atomic_store(&cs.stop, false); cs.got = 0; cs.error = 0;
	for (unsigned i = 0; i < 3; i++) {
		cc[i].p = i == 0 ? &pa : i == 1 ? &pb : &pc;
		cc[i].tag = 10 + i; cc[i].sent = cc[i].received = 0; cc[i].error = 0;
		CHECK(n2vr_open(client_dev(), 0, 0, cc[i].p) == 0);
	}
	CHECK(pthread_create(&st, NULL, conc_server_thread, &cs) == 0);
	for (unsigned i = 0; i < 3; i++) { CHECK(pthread_create(&cc[i].thread, NULL, conc_client_thread, &cc[i]) == 0); }
	for (unsigned i = 0; i < 3; i++) { CHECK(pthread_join(cc[i].thread, NULL) == 0); }
	atomic_store(&cs.stop, true);
	CHECK(pthread_join(st, NULL) == 0);
	CHECK(cs.error == 0);
	CHECK(cs.got == 3 * CONC_MSGS);
	for (unsigned i = 0; i < 3; i++) {
		CHECK(cc[i].error == 0);
		CHECK(cc[i].sent == CONC_MSGS && cc[i].received == CONC_MSGS);
	}
	/* three close notices, in any order */
	uint8_t out[4]; uint32_t bytes = 0, from = 0;
	unsigned closes = 0;
	for (unsigned i = 0; i < 3; i++) { CHECK(n2vr_close(client_dev(), 0, 0, cc[i].p) == 0); }
	for (unsigned i = 0; i < 3; i++) {
		int e = recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from);
		CHECK(e == 0 && bytes == 0);
		if (e == 0 && bytes == 0) { closes++; }
	}
	CHECK(closes == 3);

	/* close race: a client sends and vanishes while the server answers */
	CHECK(n2vr_open(client_dev(), 0, 0, &pc) == 0);
	uint32_t id = n2vr_by_proc(&pc, N2VR_SLOT_OPEN)->id;
	for (unsigned i = 0; i < 5; i++) { CHECK(send_(client_dev(), &pc, "race", 4, 0) == 0); }
	CHECK(n2vr_close(client_dev(), 0, 0, &pc) == 0);
	for (unsigned i = 0; i < 5; i++) {
		CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && from == id);
		int e = send_(server_dev(), &srv, "back", 4, id);
		CHECK(e == ESRCH);                       /* the client is gone, never a crash or a lost queue entry */
	}
	CHECK(recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from) == 0 && bytes == 0 && from == id);
}

/* ---- a close race: the last close runs while the same process is inside ioctls ---- */
static atomic_bool race_stop;

static void *
race_client_thread(void *arg)
{
	(void)arg;
	for (unsigned i = 0; i < 400 && !atomic_load(&race_stop); i++) {
		struct wire w = { .tag = 77, .seq = i };
		char buf[32];
		uint32_t bytes = 0, from = 0;
		int e = send_(client_dev(), &pr, &w, sizeof(w), 0);
		if (e == EBADF || e == ENXIO) { break; }
		if (e == EAGAIN) { (void)usleep(200); continue; }
		e = recv_(client_dev(), &pr, buf, sizeof(buf), 5, &bytes, &from);
		if (e == EBADF || e == ENXIO) { break; }
	}
	return NULL;
}

static void *
race_server_thread(void *arg)
{
	(void)arg;
	while (!atomic_load(&race_stop)) {
		struct wire w;
		uint32_t bytes = 0, from = 0;
		int e = recv_(server_dev(), &srv, &w, sizeof(w), 5, &bytes, &from);
		if (e == 0) { (void)send_(server_dev(), &srv, &w, sizeof(w), from); }
		else if (e != ETIMEDOUT && e != EAGAIN) { break; }
	}
	return NULL;
}

static void
test_close_race(void)
{
	uint8_t out[64]; uint32_t bytes = 0, from = 0;   /* >= the 16-byte wire: the drain must not stop on EMSGSIZE */
	client_reset_all();
	atomic_store(&race_stop, false);
	CHECK(n2vr_open(client_dev(), 0, 0, &pr) == 0);
	CHECK(n2vr_open(client_dev(), 0, 0, &pr) == 0);         /* two descriptors, one client */
	pthread_t ct, st;
	CHECK(pthread_create(&st, NULL, race_server_thread, NULL) == 0);
	CHECK(pthread_create(&ct, NULL, race_client_thread, NULL) == 0);
	(void)usleep(2000);
	int before = closed_calls;
	/* d_close runs while the same process is inside send/recv: the slot must be released without a use-after-free */
	CHECK(n2vr_close(client_dev(), 0, 0, &pr) == 0);
	CHECK(closed_calls == before + 1 && closed_proc == &pr);
	CHECK(pthread_join(ct, NULL) == 0);
	atomic_store(&race_stop, true);
	CHECK(pthread_join(st, NULL) == 0);
	CHECK(n2vr_by_proc(&pr, N2VR_SLOT_OPEN) == NULL);
	for (unsigned i = 0; i < 16; i++) {                      /* the notice may or may not have been read by the server thread */
		int e = recv_(server_dev(), &srv, out, sizeof(out), 0, &bytes, &from);
		if (e == EAGAIN) { break; }
		CHECK(e == 0 && (bytes == 0 || from != 0));
	}
}

/* deterministic pseudo-random ioctls: the argument validator is the only thing between user space and the queues.
 * The argument buffer is the union of every ioctl's type, because the kernel's ioctl layer sizes `data` from the
 * command's _IOC size field (RWVC_IOC embeds sizeof(type)), not from the caller's buffer. */
static void
test_fuzz(void)
{
	static const u_long cmds[] = { RWVC_INFO, RWVC_POWER, RWVC_REGS, RWVC_WAIT_IRQ, RWVC_IRQ_CLEAR, RWVC_DMA_ALLOC,
		RWVC_DMA_FREE, RWVC_DMA_RW, RWVC_DMA_FILL, RWVC_MSG_SEND, RWVC_MSG_RECV, RWVC_DMA_INFO, 13, 0x12345678 };
	union {
		struct rwvc_info info;
		struct rwvc_power power;
		struct rwvc_reg_batch batch;
		struct rwvc_wait_irq irq;
		struct rwvc_irq_clear clr;
		struct rwvc_dma_alloc alloc;
		struct rwvc_dma_free fre;
		struct rwvc_dma_rw rw;
		struct rwvc_dma_fill fill;
		struct rwvc_msg msg;
		uint8_t raw[256];
	} arg;
	uint8_t payload[1200];
	uint32_t seed = 0x13572468u;
	client_reset_all();
	CHECK(n2vr_open(client_dev(), 0, 0, &pa) == 0);
	CHECK(n2vr_server_present() == 1);
	for (unsigned i = 0; i < 1000; i++) {
		seed = seed * 1664525u + 1013904223u;
		u_long cmd = cmds[seed % (sizeof(cmds) / sizeof(cmds[0]))];
		int minor = (seed >> 8) & 1;
		struct proc *p = minor ? &pa : &srv;
		uint32_t bytes = (seed >> 12) % 1200u;
		memset(&arg, 0, sizeof(arg));
		arg.msg.user = ((seed >> 16) & 7u) == 0 ? N2VR_BAD_USER : (user_addr_t)(uintptr_t)payload;
		arg.msg.bytes = bytes;
		arg.msg.client = (seed >> 20) % 13u;
		arg.msg.timeout_ms = (seed >> 24) % 3u;
		int e = n2vr_ioctl(minor ? client_dev() : server_dev(), cmd, (caddr_t)&arg, 0, p);
		bool ok = e == 0 || e == EINVAL || e == EFAULT || e == EAGAIN || e == ETIMEDOUT || e == ENXIO ||
		    e == EBADF || e == EMSGSIZE || e == ESRCH || e == EACCES || e == ENOTTY || e == ENOMEM ||
		    e == EPERM || e == ENOENT;
		CHECK(ok);
		if (!ok) { printf("fuzz %u: cmd %#lx minor %d bytes %u -> %d\n", i, cmd, minor, bytes, e); break; }
	}
	CHECK(n2vr_by_proc(&pa, N2VR_SLOT_OPEN) != NULL);
	client_reset_all();
}

/*
 * q10jy phone reset replay (opus-relaycrash, 11 Nov): exactly what reached the kernel in the first minute of the run.
 * vcodecd: open minor 0 -> RWVC_INFO -> close (n2vhost_start failed); then n2vcd_probe after its 45 s delay: every
 * second open minor 1 -> RWVC_INFO (server_present 0) -> close, up to 90 times; then the next run's path with a server:
 * open, INFO, HELLO send, server recv/send, client recv, DMA_INFO, close, close notice, server close.  Through the cdevsw
 * entries (n2vr_open/_close/_ioctl_entry, i.e. with the breadcrumb lines) and with process references counted.
 */
static void
test_probe_replay(void)
{
	client_reset_all();
	while (n2vr_server_present()) { n2vr_server_close(); }
	int calls0 = closed_calls, refs0 = atomic_load(&pa.refs);
	n2vr_trace_left = 8;                                   /* breadcrumbs on for the first entries, then silent */
	/* vcodecd */
	n2vr_server_open();
	n2vr_server_close();
	CHECK(n2vr_server_present() == 0);
	/* n2vcd_probe, server absent */
	for (unsigned k = 0; k < 90; k++) {
		CHECK(n2vr_cdevsw.d_open(client_dev(), 3, 0x2000, &pa) == 0);
		struct rwvc_info info;
		memset(&info, 0, sizeof(info));
		CHECK(n2vr_cdevsw.d_ioctl(client_dev(), RWVC_INFO, (caddr_t)&info, 3, &pa) == 0);
		CHECK(info.abi_version == RWVC_ABI_VERSION && info.server_present == 0);
		CHECK(n2vr_cdevsw.d_close(client_dev(), 3, 0x2000, &pa) == 0);
		CHECK(atomic_load(&pa.refs) == refs0);              /* no process reference kept or over-released */
	}
	CHECK(closed_calls == calls0 + 90);
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) { CHECK(n2vr.c[i].state == N2VR_SLOT_FREE && n2vr.c[i].ref == NULL); }
	/* the next run: vcodecd serving */
	n2vr_server_open();
	CHECK(n2vr_cdevsw.d_open(client_dev(), 3, 0x2000, &pa) == 0);
	struct rwvc_info info;
	CHECK(n2vr_cdevsw.d_ioctl(client_dev(), RWVC_INFO, (caddr_t)&info, 3, &pa) == 0 && info.server_present == 1);
	uint8_t hello[64], buf[RWVC_MSG_MAX];
	memset(hello, 0x5a, sizeof(hello));
	struct rwvc_msg m = { .user = (user_addr_t)(uintptr_t)hello, .bytes = sizeof(hello) };
	CHECK(n2vr_cdevsw.d_ioctl(client_dev(), RWVC_MSG_SEND, (caddr_t)&m, 3, &pa) == 0);
	uint32_t bytes = 0, from = 0;
	CHECK(recv_(server_dev(), &srv, buf, sizeof(buf), 1000, &bytes, &from) == 0 && bytes == sizeof(hello) && from != 0);
	CHECK(send_(server_dev(), &srv, hello, 32, from) == 0);
	struct rwvc_msg r = { .user = (user_addr_t)(uintptr_t)buf, .bytes = sizeof(buf), .timeout_ms = 2000 };
	CHECK(n2vr_cdevsw.d_ioctl(client_dev(), RWVC_MSG_RECV, (caddr_t)&r, 3, &pa) == 0 && r.bytes == 32);
	struct rwvc_dma_info di = { .handle = 12345 };
	CHECK(n2vr_cdevsw.d_ioctl(client_dev(), RWVC_DMA_INFO, (caddr_t)&di, 3, &pa) == ENOENT);
	/* a receive with nothing queued times out (deadline arithmetic, short) */
	r.timeout_ms = 30;
	CHECK(n2vr_cdevsw.d_ioctl(client_dev(), RWVC_MSG_RECV, (caddr_t)&r, 3, &pa) == ETIMEDOUT);
	CHECK(n2vr_cdevsw.d_close(client_dev(), 3, 0x2000, &pa) == 0);
	CHECK(recv_(server_dev(), &srv, buf, sizeof(buf), 0, &bytes, &from) == 0 && bytes == 0);   /* close notice */
	n2vr_server_close();
	CHECK(atomic_load(&pa.refs) == refs0);
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) { CHECK(n2vr.c[i].state == N2VR_SLOT_FREE); }
	CHECK(n2vr_trace_left == 0);
	n2vr_trace_left = 64;
}

int
main(void)
{
	CHECK(N2VR_MAX_CLIENTS == 8 && N2VR_CLIENT_QUEUE == 16);       /* the header's "at most 16 datagrams" */
	test_start_and_info();
	test_single_client();
	test_queues();
	test_short_buffer();
	test_timeouts();
	test_queues();                                  /* leave the queues empty again for the blocking test */
	test_blocking();
	test_close_notice();
	test_server_close();
	test_limits();
	test_two_opens_one_client();
	test_dma_rule();
	test_dma_info();
	test_dead_client();
	test_faults();
	test_fuzz();
	test_concurrent();
	test_close_race();
	test_probe_replay();
	/* every job still ends: no slot leaks */
	for (unsigned i = 0; i < N2VR_MAX_CLIENTS; i++) { CHECK(n2vr.c[i].state == N2VR_SLOT_FREE || n2vr.c[i].state == N2VR_SLOT_OPEN); }
	client_reset_all();
	printf("N2TEST IK %s %d/%d\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	return fails ? 1 : 0;
}
