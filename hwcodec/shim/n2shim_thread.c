/* SPDX-License-Identifier: MIT */
/*
 * n2shim_thread.c -- guest threads, mutexes, condition variables, semaphores, rwlocks, once, scheduling
 * (package V32).
 *
 * Everything the vendor set imports from the pthread/sem/sched/signal family is implemented here, on top of the
 * host's pthreads: the guest's 32-bit ARM threads are host threads, each with its own struct n2a32_cpu on the one
 * shared struct n2a32_mem, started with n2a32_call() on a 1 MiB guest stack from n2a32_mem_alloc().
 *
 * Two tables, no guest-visible state:
 *
 *   threads -- fixed array of N2S_MAX_THREADS records.  The guest pthread_t is (slot + 1), so 0 is never a valid
 *              id.  A record is RESERVED while pthread_create() prepares it, RUNNING while the guest code runs,
 *              EXITED until it is joined (or is reaped immediately when detached), then FREE.  Entry 0 is the
 *              guest's main thread, registered lazily by the first shim call on its cpu.  Stacks are recycled
 *              through a small pool: the interpreter has no unmap, so a thread stack can never be returned to
 *              the address space, but a create/join loop does not grow the guest's memory.
 *
 *   objects -- pthread_mutex_t / pthread_cond_t / sem_t are 4 guest bytes and pthread_rwlock_t is 40, far too
 *              small for the host objects (and all-zero must mean PTHREAD_MUTEX_INITIALIZER), so the host
 *              objects live in a fixed pool, hashed by guest address and created on first use.  *_destroy()
 *              unlinks and frees the entry; the guest bytes are never read or written, which is exactly what
 *              makes a statically initialised (all-zero) object work.  Reference counts keep an entry alive
 *              while an operation is in flight (a cond wait holds its cond and its mutex), so a destroy racing
 *              a user cannot free a host object under it.
 *
 * Locking rules, needed for TSan and for the 8-thread test:
 *   - g_obj_lock protects the object hash, free list and every attr field; g_thr_lock protects the thread table,
 *     the stack pool and the failure slot.  The two are never held at the same time.
 *   - No guest memory is read or written while either lock is held: a guest fault longjmps to the core's run
 *     context and would leave the lock held forever.
 *   - Operations on the host objects themselves (mutex lock, cond wait, rwlock) run with no global lock held.
 *   - Guest code is never called with a lock held (pthread_once releases everything first).
 *
 * A guest thread that stops for a reason other than a normal return or a requested stop records its stop reason
 * in the first-failure slot (n2shim_thread_failure()) and sets stop_request on every other live guest cpu, so a
 * fatal guest fault tears down the whole emulated process instead of hanging its siblings.  stop_request is a
 * volatile uint32_t in the frozen n2a32.h; V32 writes and reads it with __atomic_* so that its own side of the
 * handshake is defined (see the board post to hw-v02 about the core's plain read).
 *
 * Deliberate differences from bionic, all listed in test_v32/STATUS.md:
 *   - the guest object bytes are left untouched (no tid/state words) and such an object cannot be copied;
 *   - signal delivery, thread cancellation, joining a detached thread's resources and pthread_exit() of the
 *     initial thread killing the process are not emulated (pthread_exit on the main thread stops that cpu and
 *     asks every other cpu to stop; the host process stays alive);
 *   - pthread_join() returns EINTR if the calling cpu is asked to stop while it waits;
 *   - sem_init() on an initialised semaphore returns EBUSY (bionic does too), mutex/cond/rwlock init on an
 *     initialised object returns 0 and leaves it alone (safer than bionic's overwrite of a live object);
 *   - sched_getaffinity() reports 8 CPUs (bits 0-7) whatever the host has; sched_setaffinity() accepts.
 *
 * Host-only: no phone run, no arm64 iOS build in this package (plain C11 + pthreads, no platform assembly).
 */
#define _GNU_SOURCE 1
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#include "n2a32.h"
#include "n2elf.h"
#include "n2shim_thread.h"

#define N2S_MAX_THREADS  128u
#define N2S_MAX_OBJ      512u
#define N2S_HASH_BUCKETS 1024u
#define N2S_STACK_SIZE   (1u << 20)     /* the 1 MiB guest stack per created thread */
#define N2S_STACK_POOL   32u
#define N2S_NAME_MAX     16
#define N2S_JOIN_SLICE_NS 100000000L    /* 100 ms: bounded wait so a teardown request is noticed */

/* Linux/32-bit-bionic numbers as the guest sees them. */
enum {
	N2S_EPERM = 1, N2S_ESRCH = 3, N2S_EINTR = 4, N2S_EAGAIN = 11, N2S_ENOMEM = 12,
	N2S_EBUSY = 16, N2S_EINVAL = 22, N2S_EDEADLK = 35, N2S_ENOSYS = 38, N2S_ETIMEDOUT = 110
};
enum { N2S_CREATE_JOINABLE = 0, N2S_CREATE_DETACHED = 1 };
enum { N2S_SCHED_OTHER = 0, N2S_SCHED_FIFO = 1, N2S_SCHED_RR = 2 };
enum { N2S_MUTEX_NORMAL = 0, N2S_MUTEX_RECURSIVE = 1, N2S_MUTEX_ERRORCHECK = 2 };
enum { N2S_CLOCK_REALTIME = 0, N2S_CLOCK_MONOTONIC = 1 };

enum { N2S_K_NONE = 0, N2S_K_MUTEX, N2S_K_COND, N2S_K_RWLOCK, N2S_K_SEM, N2S_K_ONCE, N2S_K_ATTR };
enum { N2S_T_FREE = 0, N2S_T_RESERVED, N2S_T_RUNNING, N2S_T_EXITED };

/* ---------------------------------------------------------------------------------------------------------------
 * Host helpers
 * -------------------------------------------------------------------------------------------------------------- */
static unsigned long n2s_host_tid(void)
{
#if defined(__linux__)
	return (unsigned long)syscall(SYS_gettid);
#else
	/* No gettid() outside Linux: the thread pointer is host-stable and unique while the thread lives. */
	return (unsigned long)(uintptr_t)pthread_self();
#endif
}

static void n2s_host_setname(pthread_t th, const char *name)
{
#if defined(__linux__)
	(void)pthread_setname_np(th, name);
#else
	if (pthread_equal(th, pthread_self()))
		(void)pthread_setname_np(name);
	else
		(void)th;
#endif
}

static void n2s_now(struct timespec *ts)
{
	(void)clock_gettime(CLOCK_REALTIME, ts);
}

/* Add `ms` to a CLOCK_REALTIME timespec. */
static void n2s_deadline(struct timespec *ts, long ms)
{
	ts->tv_sec += ms / 1000;
	ts->tv_nsec += (ms % 1000) * 1000000L;
	if (ts->tv_nsec >= 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

static int n2s_stop_requested(struct n2a32_cpu *c)
{
	return __atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE) != 0;
}

/* A 100 ms slice starts at `now` unless the guest's absolute deadline `abs` is nearer; returns 1 when `abs` has
 * already passed. */
static int n2s_slice(struct timespec *slice, const struct timespec *abs)
{
	struct timespec now;

	n2s_now(&now);
	if (abs != NULL && (now.tv_sec > abs->tv_sec ||
			    (now.tv_sec == abs->tv_sec && now.tv_nsec >= abs->tv_nsec))) {
		*slice = *abs;                  /* already expired: hand the guest deadline on unchanged */
		return 1;
	}
	*slice = now;
	n2s_deadline(slice, 100);
	if (abs != NULL && (slice->tv_sec > abs->tv_sec ||
			    (slice->tv_sec == abs->tv_sec && slice->tv_nsec > abs->tv_nsec)))
		*slice = *abs;
	return 0;
}

/* Darwin has no pthread_mutex_timedlock()/pthread_rwlock_timed*lock(): poll with trylock.  The 2 ms nap keeps a
 * contended wait cheap enough for the codec's control path; Linux uses the native timed locks. */
static int n2s_mutex_timedlock(pthread_mutex_t *m, const struct timespec *abs)
{
#if defined(__linux__)
	return pthread_mutex_timedlock(m, abs);
#else
	for (;;) {
		struct timespec now, nap;
		int rc = pthread_mutex_trylock(m);

		if (rc == 0)
			return 0;
		if (rc != EBUSY)
			return rc;
		(void)clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec > abs->tv_sec || (now.tv_sec == abs->tv_sec && now.tv_nsec >= abs->tv_nsec))
			return ETIMEDOUT;
		nap.tv_sec = 0;
		nap.tv_nsec = 2000000L;
		(void)nanosleep(&nap, NULL);
	}
#endif
}

/* Acquire a host mutex in 100 ms slices so a teardown request (n2shim_thread_request_stop or a failing sibling)
 * cannot leave this thread blocked forever behind a holder that has itself stopped.  `abs` == NULL means "no
 * guest deadline".  Returns 0, an errno, or N2S_EINTR when the calling cpu was asked to stop. */
static int n2s_mutex_acquire(struct n2a32_cpu *c, pthread_mutex_t *m, const struct timespec *abs)
{
	for (;;) {
		struct timespec slice;
		int rc;

		(void)n2s_slice(&slice, abs);   /* a past deadline is kept: an uncontended lock still succeeds */
		rc = n2s_mutex_timedlock(m, &slice);
		if (rc == 0)
			return 0;               /* also EDEADLK/EPERM for errorcheck and recursive mutexes */
		if (rc != ETIMEDOUT)
			return rc;
		if (abs != NULL && (n2s_slice(&slice, abs) || (slice.tv_sec == abs->tv_sec &&
								slice.tv_nsec == abs->tv_nsec)))
			return N2S_ETIMEDOUT;   /* the slice was cut short by the guest deadline */
		if (n2s_stop_requested(c))
			return N2S_EINTR;
	}
}

/* pthread_cond_timedwait in 100 ms slices: 0 = a real wakeup, N2S_ETIMEDOUT = the guest deadline passed,
 * N2S_EINTR = the calling cpu was asked to stop.  Called with the cond's internal mutex held. */
static int n2s_cond_wait_sliced(struct n2a32_cpu *c, pthread_cond_t *cv, pthread_mutex_t *m,
				const struct timespec *abs)
{
	for (;;) {
		struct timespec slice;
		int rc;

		if (n2s_stop_requested(c))
			return N2S_EINTR;
		if (n2s_slice(&slice, abs))
			return N2S_ETIMEDOUT;
		rc = pthread_cond_timedwait(cv, m, &slice);
		if (rc == 0)
			return 0;
		if (rc != ETIMEDOUT)
			return rc;
	}
}

static int n2s_rwlock_timedlock(pthread_rwlock_t *rw, int write, const struct timespec *abs)
{
#if defined(__linux__)
	return write ? pthread_rwlock_timedwrlock(rw, abs) : pthread_rwlock_timedrdlock(rw, abs);
#else
	for (;;) {
		struct timespec now, nap;
		int rc = write ? pthread_rwlock_trywrlock(rw) : pthread_rwlock_tryrdlock(rw);

		if (rc == 0)
			return 0;
		if (rc != EBUSY && rc != EDEADLK)
			return rc;
		if (rc == EDEADLK)
			return EBUSY;
		(void)clock_gettime(CLOCK_REALTIME, &now);
		if (now.tv_sec > abs->tv_sec || (now.tv_sec == abs->tv_sec && now.tv_nsec >= abs->tv_nsec))
			return ETIMEDOUT;
		nap.tv_sec = 0;
		nap.tv_nsec = 2000000L;
		(void)nanosleep(&nap, NULL);
	}
#endif
}

/* Guest struct timespec (32-bit time_t, 32-bit long) -> host absolute timespec.  `guest_clock` is the clock the
 * guest's deadline is expressed in; the host condition variables use CLOCK_REALTIME, so a monotonic deadline is
 * converted through "now".  Returns 0 or N2S_EINVAL. */
static int n2s_guest_abs(struct n2a32_cpu *c, uint32_t p, int guest_clock, struct timespec *out)
{
	int32_t sec;
	uint32_t nsec;

	if (p == 0)
		return N2S_EINVAL;
	sec = (int32_t)n2a32_ld32(c, p);
	nsec = n2a32_ld32(c, p + 4);
	if (nsec >= 1000000000u)
		return N2S_EINVAL;
	if (guest_clock == N2S_CLOCK_MONOTONIC) {
		struct timespec mono, real;
		int64_t delta_ns;

		(void)clock_gettime(CLOCK_MONOTONIC, &mono);
		(void)clock_gettime(CLOCK_REALTIME, &real);
		delta_ns = ((int64_t)sec - (int64_t)mono.tv_sec) * 1000000000LL
			 + ((int64_t)nsec - (int64_t)mono.tv_nsec);
		if (delta_ns < 0)
			delta_ns = 0;
		real.tv_sec += (time_t)(delta_ns / 1000000000LL);
		real.tv_nsec += (long)(delta_ns % 1000000000LL);
		if (real.tv_nsec >= 1000000000L) {
			real.tv_sec++;
			real.tv_nsec -= 1000000000L;
		}
		*out = real;
	} else {
		out->tv_sec = (time_t)sec;
		out->tv_nsec = (long)nsec;
	}
	return 0;
}

/* ---------------------------------------------------------------------------------------------------------------
 * Thread table
 * -------------------------------------------------------------------------------------------------------------- */
struct n2s_thread {
	struct n2a32_cpu *cpu;          /* the cpu this guest thread runs on (own for created threads) */
	struct n2a32_cpu own;
	uint32_t stack;                 /* guest stack base, 0 after it went back to the pool */
	uint32_t start, arg, retval;
	pthread_t host;
	unsigned long tid;              /* host kernel tid, 0 until the host thread started */
	unsigned id;                    /* guest pthread_t == slot + 1 */
	uint8_t state;                  /* N2S_T_* */
	uint8_t detached, host_detached, is_main, exiting, named;
	char name[N2S_NAME_MAX];
};

static struct n2s_thread g_threads[N2S_MAX_THREADS];
static pthread_mutex_t g_thr_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_thr_cond = PTHREAD_COND_INITIALIZER;
static uint32_t g_stack_pool[N2S_STACK_POOL];
static unsigned g_stack_n;
static unsigned g_thr_created;

/* First failure slot, protected by g_thr_lock. */
static int g_fail_reason;
static unsigned g_fail_count, g_fail_thread;
static uint32_t g_fail_insn, g_fail_addr;
static unsigned g_stop_requests;

/* The calling host thread's cached record.  TLS is only valid for the cpu it names; n2s_self() re-validates. */
static _Thread_local unsigned tls_id;
static _Thread_local struct n2a32_cpu *tls_cpu;

/* The object side table and its attribute fields are defined further down; the thread shims need the getter. */
#define N2S_ATTR_DETACH  0
#define N2S_ATTR_POLICY  1
#define N2S_ATTR_PRIO    2
#define N2S_ATTR_TYPE    3
#define N2S_ATTR_PSHARED 4
#define N2S_ATTR_CLOCK   5
static int n2s_attr_field(uint32_t ap, int field);

static struct n2a32_cpu *n2s_atomic_stop(struct n2a32_cpu *c)
{
	__atomic_store_n(&c->stop_request, 1u, __ATOMIC_RELEASE);
	return c;
}

/* Set stop_request on every live cpu except `except` (may be NULL).  g_thr_lock held. */
static unsigned n2s_request_stop_locked(struct n2a32_cpu *except)
{
	unsigned i, n = 0;

	for (i = 0; i < N2S_MAX_THREADS; i++) {
		struct n2s_thread *t = &g_threads[i];

		if (t->state != N2S_T_RUNNING || t->cpu == NULL || t->cpu == except)
			continue;
		n2s_atomic_stop(t->cpu);
		n++;
	}
	g_stop_requests += n;
	return n;
}

/* Record an abnormal stop of `c` (a thread of record `t`, or t == NULL for the daemon's own call).  g_thr_lock
 * held.  Sets stop_request on every other live cpu, as the package contract requires. */
static void n2s_note_failure_locked(int reason, struct n2s_thread *t, struct n2a32_cpu *c)
{
	if (g_fail_count == 0) {
		g_fail_reason = reason;
		g_fail_thread = t != NULL ? t->id : 0;
		g_fail_insn = c != NULL ? c->stop_insn : 0;
		g_fail_addr = c != NULL ? c->stop_addr : 0;
	}
	g_fail_count++;
	n2s_request_stop_locked(c);
}

static struct n2s_thread *n2s_slot_locked(unsigned id)
{
	if (id == 0 || id > N2S_MAX_THREADS)
		return NULL;
	return &g_threads[id - 1];
}

/* The record of the calling thread, registered on first use.  The fast path needs no lock: a RUNNING record
 * cannot be freed or reused while its own thread is running. */
static struct n2s_thread *n2s_self(struct n2a32_cpu *c)
{
	struct n2s_thread *t;
	unsigned i;

	if (c == NULL)
		return NULL;
	if (tls_id != 0 && tls_cpu == c) {
		t = &g_threads[tls_id - 1];
		if (t->state == N2S_T_RUNNING && t->cpu == c)
			return t;
	}
	pthread_mutex_lock(&g_thr_lock);
	for (i = 0; i < N2S_MAX_THREADS; i++) {
		t = &g_threads[i];
		if (t->state != N2S_T_FREE && t->cpu == c) {
			/* The initial thread can call pthread_exit() and later be given more guest code by the
			 * daemon on the same cpu; revive the record so the shims keep one identity per cpu. */
			if (t->state == N2S_T_EXITED) {
				t->state = N2S_T_RUNNING;
				t->retval = 0;
				t->exiting = 0;
			}
			tls_id = i + 1;
			tls_cpu = c;
			pthread_mutex_unlock(&g_thr_lock);
			return t;
		}
	}
	for (i = 0; i < N2S_MAX_THREADS; i++) {
		t = &g_threads[i];
		if (t->state == N2S_T_FREE) {
			memset(t, 0, sizeof(*t));
			t->id = i + 1;
			t->cpu = c;
			t->state = N2S_T_RUNNING;
			t->is_main = 1;
			t->tid = n2s_host_tid();
			tls_id = t->id;
			tls_cpu = c;
			pthread_mutex_unlock(&g_thr_lock);
			return t;
		}
	}
	pthread_mutex_unlock(&g_thr_lock);
	return NULL;
}

/* Give a thread's guest stack back to the pool.  g_thr_lock held. */
static void n2s_stack_release_locked(uint32_t stack)
{
	if (stack == 0)
		return;
	if (g_stack_n < N2S_STACK_POOL)
		g_stack_pool[g_stack_n++] = stack;
	/* else: leaked on purpose; n2a32_mem has no unmap. */
}

static void *n2s_thr_main(void *opaque)
{
	unsigned id = (unsigned)(uintptr_t)opaque;
	struct n2s_thread *t = &g_threads[id - 1];
	struct n2a32_cpu *c = t->cpu;
	uint32_t arg = t->arg, fn = t->start, ret = 0;
	enum n2a32_stop st;

	tls_id = id;
	tls_cpu = c;

	pthread_mutex_lock(&g_thr_lock);
	t->tid = n2s_host_tid();
	if (t->named)
		n2s_host_setname(pthread_self(), t->name);
	pthread_mutex_unlock(&g_thr_lock);

	ret = n2a32_call(c, fn, &arg, 1, NULL);
	st = c->stop;

	pthread_mutex_lock(&g_thr_lock);
	if (st != N2A32_STOP_RETURN && st != N2A32_STOP_REQUEST)
		n2s_note_failure_locked((int)st, t, c);
	if (!t->exiting)
		t->retval = st == N2A32_STOP_RETURN ? ret : 0;
	n2s_stack_release_locked(t->stack);
	t->stack = 0;
	t->state = N2S_T_EXITED;
	if (t->detached) {
		t->state = N2S_T_FREE;
		t->cpu = NULL;
	}
	pthread_cond_broadcast(&g_thr_cond);
	pthread_mutex_unlock(&g_thr_lock);
	return NULL;
}

/* ---- pthread_* ---------------------------------------------------------------------------------------------- */
static void n2s_shim_pthread_create(struct n2a32_cpu *c)
{
	uint32_t tp = n2shim_arg(c, 0);
	uint32_t ap = n2shim_arg(c, 1);
	uint32_t fn = n2shim_arg(c, 2);
	uint32_t arg = n2shim_arg(c, 3);
	struct n2s_thread *t = NULL;
	struct n2a32_mem *mem = c != NULL ? c->mem : NULL;
	pthread_t host;
	uint32_t stack = 0, id = 0;
	int detached, rc;
	unsigned i;

	if (tp == 0 || fn == 0 || mem == NULL) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	/* Creating a thread means this cpu runs guest code: register it, so a failing worker can stop it too. */
	(void)n2s_self(c);
	detached = n2s_attr_field(ap, N2S_ATTR_DETACH);  /* 0 = joinable unless the attr says otherwise */

	pthread_mutex_lock(&g_thr_lock);
	for (i = 0; i < N2S_MAX_THREADS; i++) {
		if (g_threads[i].state == N2S_T_FREE) {
			t = &g_threads[i];
			memset(t, 0, sizeof(*t));
			t->id = i + 1;
			t->state = N2S_T_RESERVED;
			id = t->id;
			break;
		}
	}
	if (t != NULL && g_stack_n > 0)
		stack = g_stack_pool[--g_stack_n];
	pthread_mutex_unlock(&g_thr_lock);

	if (t == NULL) {
		n2shim_ret(c, N2S_EAGAIN);      /* thread table full */
		return;
	}
	if (stack == 0) {
		stack = n2a32_mem_alloc(mem, N2S_STACK_SIZE, 16);
		if (stack == 0) {
			pthread_mutex_lock(&g_thr_lock);
			t->state = N2S_T_FREE;
			t->cpu = NULL;
			pthread_mutex_unlock(&g_thr_lock);
			n2shim_ret(c, N2S_EAGAIN);
			return;
		}
	}

	n2a32_cpu_init(&t->own, mem);
	t->own.r[13] = stack + N2S_STACK_SIZE;  /* 16-aligned top; AAPCS wants SP 8-aligned at a call */
	t->own.r[14] = N2A32_RETURN_ADDR;
	t->cpu = &t->own;
	t->stack = stack;
	t->start = fn;
	t->arg = arg;
	t->detached = (uint8_t)(detached == N2S_CREATE_DETACHED);

	pthread_mutex_lock(&g_thr_lock);
	t->state = N2S_T_RUNNING;
	if (t->detached) {
		/* The guest never joins it: let the host reclaim the thread resources by itself. */
		pthread_attr_t ha;

		(void)pthread_attr_init(&ha);
		(void)pthread_attr_setdetachstate(&ha, PTHREAD_CREATE_DETACHED);
		rc = pthread_create(&host, &ha, n2s_thr_main, (void *)(uintptr_t)id);
		(void)pthread_attr_destroy(&ha);
		t->host_detached = 1;
	} else {
		rc = pthread_create(&host, NULL, n2s_thr_main, (void *)(uintptr_t)id);
	}
	if (rc == 0) {
		t->host = host;
		g_thr_created++;
	} else {
		t->state = N2S_T_FREE;
		t->cpu = NULL;
		n2s_stack_release_locked(stack);
		t->stack = 0;
	}
	pthread_mutex_unlock(&g_thr_lock);

	if (rc != 0) {
		n2shim_ret(c, (uint32_t)rc);
		return;
	}
	n2a32_st32(c, tp, id);                  /* guest write after every lock is released */
	n2shim_ret(c, 0);
}

static void n2s_shim_pthread_join(struct n2a32_cpu *c)
{
	uint32_t id = n2shim_arg(c, 0);
	uint32_t retp = n2shim_arg(c, 1);
	struct n2s_thread *t;
	uint32_t rv = 0;
	pthread_t host = (pthread_t)0;
	int rc = 0, join_host = 0;

	(void)n2s_self(c);                      /* register the joining cpu as a guest thread */
	pthread_mutex_lock(&g_thr_lock);
	t = n2s_slot_locked(id);
	if (t == NULL || t->state == N2S_T_FREE) {
		pthread_mutex_unlock(&g_thr_lock);
		n2shim_ret(c, N2S_ESRCH);
		return;
	}
	if (t->cpu == c) {
		pthread_mutex_unlock(&g_thr_lock);
		n2shim_ret(c, N2S_EDEADLK);
		return;
	}
	if (t->detached || t->state == N2S_T_RESERVED) {
		pthread_mutex_unlock(&g_thr_lock);
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	while (t->state == N2S_T_RUNNING) {
		struct timespec ts;

		if (__atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE) != 0) {
			rc = N2S_EINTR;         /* process teardown: do not block the join */
			break;
		}
		n2s_now(&ts);
		n2s_deadline(&ts, N2S_JOIN_SLICE_NS / 1000000L);
		(void)pthread_cond_timedwait(&g_thr_cond, &g_thr_lock, &ts);
	}
	if (rc == 0) {
		if (t->state != N2S_T_EXITED) {
			rc = N2S_EINVAL;
		} else {
			rv = t->retval;
			host = t->host;
			join_host = !t->host_detached;
			t->host_detached = 1;
			t->state = N2S_T_FREE;
			t->cpu = NULL;
		}
	}
	pthread_mutex_unlock(&g_thr_lock);
	if (join_host)
		(void)pthread_join(host, NULL);  /* the host thread has finished; reclaim its resources */
	if (rc == 0 && retp != 0)
		n2a32_st32(c, retp, rv);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_pthread_detach(struct n2a32_cpu *c)
{
	uint32_t id = n2shim_arg(c, 0);
	struct n2s_thread *t;
	pthread_t host = (pthread_t)0;
	int rc = 0, join_host = 0, detach_host = 0;

	pthread_mutex_lock(&g_thr_lock);
	t = n2s_slot_locked(id);
	if (t == NULL || t->state == N2S_T_FREE) {
		rc = N2S_ESRCH;
	} else if (t->detached) {
		rc = N2S_EINVAL;
	} else if (t->is_main) {
		rc = N2S_EINVAL;                /* the initial thread cannot be detached */
	} else {
		t->detached = 1;
		if (!t->host_detached) {
			t->host_detached = 1;
			host = t->host;
			if (t->state == N2S_T_EXITED) {
				t->state = N2S_T_FREE;
				t->cpu = NULL;
				join_host = 1;  /* finished already: reclaim it now */
			} else {
				detach_host = 1;
			}
		}
	}
	pthread_mutex_unlock(&g_thr_lock);
	if (join_host)
		(void)pthread_join(host, NULL);
	else if (detach_host)
		(void)pthread_detach(host);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_pthread_exit(struct n2a32_cpu *c)
{
	uint32_t v = n2shim_arg(c, 0);
	struct n2s_thread *t = n2s_self(c);

	pthread_mutex_lock(&g_thr_lock);
	if (t != NULL) {
		t->retval = v;
		t->exiting = 1;
		if (t->is_main) {
			/* The initial thread ends the emulated process: publish its exit and ask every other
			 * cpu to stop.  The host process itself stays alive. */
			(void)n2s_request_stop_locked(c);
			t->state = N2S_T_EXITED;
			pthread_cond_broadcast(&g_thr_cond);
		}
	}
	pthread_mutex_unlock(&g_thr_lock);
	/* Stop this cpu at the next instruction boundary: the guest never resumes after pthread_exit(). */
	n2s_atomic_stop(c);
	n2shim_ret(c, 0);
}

static void n2s_shim_pthread_self(struct n2a32_cpu *c)
{
	struct n2s_thread *t = n2s_self(c);

	n2shim_ret(c, t != NULL ? t->id : 0);
}

static void n2s_shim_pthread_equal(struct n2a32_cpu *c)
{
	uint32_t a = n2shim_arg(c, 0), b = n2shim_arg(c, 1);

	n2shim_ret(c, a == b ? 1u : 0u);
}

static void n2s_shim_pthread_kill(struct n2a32_cpu *c)
{
	uint32_t id = n2shim_arg(c, 0);
	uint32_t sig = n2shim_arg(c, 1);
	struct n2s_thread *t;
	int rc = 0;

	pthread_mutex_lock(&g_thr_lock);
	t = n2s_slot_locked(id);
	if (t == NULL || t->state == N2S_T_FREE || t->state == N2S_T_RESERVED)
		rc = N2S_ESRCH;
	pthread_mutex_unlock(&g_thr_lock);
	if (rc == 0 && sig != 0)
		rc = 0;                          /* signals are recorded elsewhere and never delivered */
	n2shim_ret(c, (uint32_t)rc);
}

static int n2s_read_guest_str(struct n2a32_cpu *c, uint32_t a, char *buf, unsigned cap)
{
	unsigned i;

	if (a == 0 || cap == 0)
		return N2S_EINVAL;
	for (i = 0; i + 1 < cap; i++) {
		char ch = (char)n2a32_ld8(c, a + i);

		buf[i] = ch;
		if (ch == 0)
			return 0;
	}
	buf[cap - 1] = 0;
	return 0;
}

static void n2s_shim_pthread_setname(struct n2a32_cpu *c)
{
	uint32_t id = n2shim_arg(c, 0), namep = n2shim_arg(c, 1);
	char name[N2S_NAME_MAX];
	struct n2s_thread *t;
	int rc;

	rc = n2s_read_guest_str(c, namep, name, sizeof(name));
	if (rc != 0) {
		n2shim_ret(c, (uint32_t)rc);
		return;
	}
	pthread_mutex_lock(&g_thr_lock);
	t = n2s_slot_locked(id);
	if (t == NULL || t->state == N2S_T_FREE) {
		rc = N2S_ESRCH;
	} else {
		memcpy(t->name, name, sizeof(name));
		t->named = 1;
		if (t->cpu == c && t->state == N2S_T_RUNNING)
			n2s_host_setname(pthread_self(), t->name);
	}
	pthread_mutex_unlock(&g_thr_lock);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_pthread_gettid(struct n2a32_cpu *c)
{
	uint32_t id = n2shim_arg(c, 0);
	struct n2s_thread *t;
	unsigned long tid = 0;

	pthread_mutex_lock(&g_thr_lock);
	t = n2s_slot_locked(id);
	if (t != NULL && t->state != N2S_T_FREE && t->state != N2S_T_RESERVED)
		tid = t->tid;
	pthread_mutex_unlock(&g_thr_lock);
	n2shim_ret(c, (uint32_t)tid);
}

/* ---- attributes --------------------------------------------------------------------------------------------- */
/* Object table first, because the attrs live in it. */

/* ---------------------------------------------------------------------------------------------------------------
 * Object side table
 * -------------------------------------------------------------------------------------------------------------- */
struct n2s_attr {
	int detach;                     /* N2S_CREATE_JOINABLE / _DETACHED */
	int policy;                     /* N2S_SCHED_* */
	int prio;                       /* sched_param.sched_priority */
	int type;                       /* N2S_MUTEX_* */
	int pshared;
	int clock;                      /* N2S_CLOCK_* for condattrs */
};

struct n2s_obj {
	struct n2s_obj *hnext;
	struct n2s_obj *fnext;
	uint32_t key;                   /* guest address */
	int refs;                       /* operations in flight; the entry is freed at 0 once dying */
	uint8_t kind;
	uint8_t used;
	uint8_t dying;
	union {
		struct { pthread_mutex_t m; int type; } mutex;
		struct { pthread_mutex_t m; pthread_cond_t cv; int clock; } cond;
		struct { pthread_rwlock_t rw; } rw;
		struct { pthread_mutex_t m; pthread_cond_t cv; unsigned count; } sem;
		struct { pthread_mutex_t m; pthread_cond_t cv; int state; unsigned long owner; } once;
		struct n2s_attr attr;
	} u;
};

static struct n2s_obj g_objs[N2S_MAX_OBJ];
static struct n2s_obj *g_hash[N2S_HASH_BUCKETS];
static struct n2s_obj *g_obj_free;
static pthread_mutex_t g_obj_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t g_obj_once = PTHREAD_ONCE_INIT;
static unsigned g_obj_live, g_obj_peak, g_kind_clashes, g_nested_once;

static void n2s_obj_pool_init(void)
{
	unsigned i;

	for (i = 0; i < N2S_MAX_OBJ; i++) {
		g_objs[i].fnext = g_obj_free;
		g_obj_free = &g_objs[i];
	}
}

static unsigned n2s_hash(uint32_t key)
{
	return (unsigned)(((key >> 2) * 2654435761u) >> 22) & (N2S_HASH_BUCKETS - 1u);
}

static void n2s_free_locked(struct n2s_obj *o)
{
	switch (o->kind) {
	case N2S_K_MUTEX:
		(void)pthread_mutex_destroy(&o->u.mutex.m);
		break;
	case N2S_K_COND:
		(void)pthread_cond_destroy(&o->u.cond.cv);
		(void)pthread_mutex_destroy(&o->u.cond.m);
		break;
	case N2S_K_RWLOCK:
		(void)pthread_rwlock_destroy(&o->u.rw.rw);
		break;
	case N2S_K_SEM:
		(void)pthread_cond_destroy(&o->u.sem.cv);
		(void)pthread_mutex_destroy(&o->u.sem.m);
		break;
	case N2S_K_ONCE:
		(void)pthread_cond_destroy(&o->u.once.cv);
		(void)pthread_mutex_destroy(&o->u.once.m);
		break;
	default:
		break;
	}
	o->used = 0;
	o->dying = 0;
	o->refs = 0;
	o->key = 0;
	o->kind = N2S_K_NONE;
	o->fnext = g_obj_free;
	g_obj_free = o;
	g_obj_live--;
}

static void n2s_unlink_locked(struct n2s_obj *o)
{
	struct n2s_obj **pp = &g_hash[n2s_hash(o->key)];

	while (*pp != NULL) {
		if (*pp == o) {
			*pp = o->hnext;
			o->hnext = NULL;
			return;
		}
		pp = &(*pp)->hnext;
	}
}

static struct n2s_obj *n2s_new_locked(int kind, uint32_t key, int arg)
{
	struct n2s_obj *o;

	(void)pthread_once(&g_obj_once, n2s_obj_pool_init);
	o = g_obj_free;

	if (o == NULL)
		return NULL;
	g_obj_free = o->fnext;
	memset(o, 0, sizeof(*o));
	o->key = key;
	o->kind = (uint8_t)kind;
	o->used = 1;
	o->refs = 1;
	switch (kind) {
	case N2S_K_MUTEX: {
		pthread_mutexattr_t a;
		int type = PTHREAD_MUTEX_DEFAULT;

		if (arg == N2S_MUTEX_RECURSIVE)
			type = PTHREAD_MUTEX_RECURSIVE;
		else if (arg == N2S_MUTEX_ERRORCHECK)
			type = PTHREAD_MUTEX_ERRORCHECK;
		(void)pthread_mutexattr_init(&a);
		(void)pthread_mutexattr_settype(&a, type);
		(void)pthread_mutex_init(&o->u.mutex.m, &a);
		(void)pthread_mutexattr_destroy(&a);
		o->u.mutex.type = arg;
		break;
	}
	case N2S_K_COND:
		(void)pthread_mutex_init(&o->u.cond.m, NULL);
		(void)pthread_cond_init(&o->u.cond.cv, NULL);
		o->u.cond.clock = arg;
		break;
	case N2S_K_RWLOCK:
		(void)pthread_rwlock_init(&o->u.rw.rw, NULL);
		break;
	case N2S_K_SEM:
		(void)pthread_mutex_init(&o->u.sem.m, NULL);
		(void)pthread_cond_init(&o->u.sem.cv, NULL);
		o->u.sem.count = (unsigned)arg;
		break;
	case N2S_K_ONCE:
		(void)pthread_mutex_init(&o->u.once.m, NULL);
		(void)pthread_cond_init(&o->u.once.cv, NULL);
		o->u.once.state = 0;
		break;
	case N2S_K_ATTR:
		o->u.attr.detach = N2S_CREATE_JOINABLE;
		o->u.attr.policy = N2S_SCHED_OTHER;
		o->u.attr.prio = 0;
		o->u.attr.type = N2S_MUTEX_NORMAL;
		o->u.attr.pshared = 0;
		o->u.attr.clock = N2S_CLOCK_REALTIME;
		break;
	default:
		o->kind = N2S_K_NONE;
		o->used = 0;
		o->fnext = g_obj_free;
		g_obj_free = o;
		return NULL;
	}
	o->hnext = g_hash[n2s_hash(key)];
	g_hash[n2s_hash(key)] = o;
	g_obj_live++;
	if (g_obj_live > g_obj_peak)
		g_obj_peak = g_obj_live;
	return o;
}

/* Find (or create) the entry for `key`.  A different kind at the same address retires the old entry (the guest
 * reused a stack slot for another object type), counted in n2shim_thread_kind_clashes().  g_obj_lock held. */
static struct n2s_obj *n2s_find_locked(uint32_t key, int kind, int arg, int create, int *existed)
{
	unsigned h = n2s_hash(key);
	struct n2s_obj *o;

	if (existed != NULL)
		*existed = 0;
	for (o = g_hash[h]; o != NULL; o = o->hnext) {
		if (o->key == key && !o->dying)
			break;
	}
	if (o != NULL && o->kind != kind) {
		n2s_unlink_locked(o);
		o->dying = 1;
		g_kind_clashes++;
		if (o->refs == 0)
			n2s_free_locked(o);
		o = NULL;
	}
	if (o != NULL) {
		o->refs++;
		if (existed != NULL)
			*existed = 1;
		return o;
	}
	if (!create)
		return NULL;
	return n2s_new_locked(kind, key, arg);
}

static struct n2s_obj *n2s_obj_ref(uint32_t key, int kind, int arg, int create)
{
	struct n2s_obj *o;

	pthread_mutex_lock(&g_obj_lock);
	o = n2s_find_locked(key, kind, arg, create, NULL);
	pthread_mutex_unlock(&g_obj_lock);
	return o;
}

/* Create-or-find for the *_init functions: returns 0 (or an errno) and reports whether the entry already
 * existed.  The creation reference is released again, so an initialised object is idle (refs == 0). */
static int n2s_obj_init(uint32_t key, int kind, int arg, int *existed)
{
	struct n2s_obj *o;
	int rc = 0;

	pthread_mutex_lock(&g_obj_lock);
	o = n2s_find_locked(key, kind, arg, 1, existed);
	if (o == NULL)
		rc = N2S_ENOMEM;
	else if (o->refs > 0)
		o->refs--;
	pthread_mutex_unlock(&g_obj_lock);
	return rc;
}

static void n2s_obj_unref(struct n2s_obj *o)
{
	int free_it;

	if (o == NULL)
		return;
	pthread_mutex_lock(&g_obj_lock);
	if (o->refs > 0)
		o->refs--;
	free_it = (o->dying && o->refs == 0);
	if (free_it)
		n2s_free_locked(o);
	pthread_mutex_unlock(&g_obj_lock);
}

/* Retire an entry: unlink it now, free it when the last operation finishes. */
static void n2s_obj_drop(struct n2s_obj *o)
{
	if (o == NULL)
		return;
	pthread_mutex_lock(&g_obj_lock);
	if (!o->dying) {
		n2s_unlink_locked(o);
		o->dying = 1;
	}
	pthread_mutex_unlock(&g_obj_lock);
}

static void n2s_attr_set(uint32_t ap, int field, int value)
{
	struct n2s_obj *o;

	if (ap == 0)
		return;
	o = n2s_obj_ref(ap, N2S_K_ATTR, 0, 1);
	if (o == NULL)
		return;
	pthread_mutex_lock(&g_obj_lock);
	switch (field) {
	case N2S_ATTR_DETACH: o->u.attr.detach = value; break;
	case N2S_ATTR_POLICY: o->u.attr.policy = value; break;
	case N2S_ATTR_PRIO: o->u.attr.prio = value; break;
	case N2S_ATTR_TYPE: o->u.attr.type = value; break;
	case N2S_ATTR_PSHARED: o->u.attr.pshared = value; break;
	case N2S_ATTR_CLOCK: o->u.attr.clock = value; break;
	default: break;
	}
	pthread_mutex_unlock(&g_obj_lock);
	n2s_obj_unref(o);
}

static int n2s_attr_field(uint32_t ap, int field)
{
	struct n2s_obj *o;
	int v = 0;

	if (ap == 0)
		return field == N2S_ATTR_DETACH ? N2S_CREATE_JOINABLE : 0;
	o = n2s_obj_ref(ap, N2S_K_ATTR, 0, 1);
	if (o == NULL)
		return field == N2S_ATTR_DETACH ? N2S_CREATE_JOINABLE : 0;
	pthread_mutex_lock(&g_obj_lock);
	switch (field) {
	case N2S_ATTR_DETACH: v = o->u.attr.detach; break;
	case N2S_ATTR_POLICY: v = o->u.attr.policy; break;
	case N2S_ATTR_PRIO: v = o->u.attr.prio; break;
	case N2S_ATTR_TYPE: v = o->u.attr.type; break;
	case N2S_ATTR_PSHARED: v = o->u.attr.pshared; break;
	case N2S_ATTR_CLOCK: v = o->u.attr.clock; break;
	default: v = 0; break;
	}
	pthread_mutex_unlock(&g_obj_lock);
	n2s_obj_unref(o);
	return v;
}

/* ---- pthread_attr_* ----------------------------------------------------------------------------------------- */
static void n2s_shim_attr_init(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	int existed = 0;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	rc = n2s_obj_init(ap, N2S_K_ATTR, 0, &existed);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_attr_destroy(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_ATTR, 0, 0);
	if (o != NULL)
		n2s_obj_drop(o);
	n2s_obj_unref(o);
	n2shim_ret(c, 0);
}

static void n2s_shim_attr_setdetachstate(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), v = n2shim_arg(c, 1);

	if (v != N2S_CREATE_JOINABLE && v != N2S_CREATE_DETACHED) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2s_attr_set(ap, 0, (int)v);
	n2shim_ret(c, 0);
}

static void n2s_shim_attr_getdetachstate(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), outp = n2shim_arg(c, 1);
	int v = n2s_attr_field(ap, 0);

	if (outp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2a32_st32(c, outp, (uint32_t)v);
	n2shim_ret(c, 0);
}

static void n2s_shim_attr_setschedparam(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), pp = n2shim_arg(c, 1);
	uint32_t prio;

	if (pp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	prio = n2a32_ld32(c, pp);               /* struct sched_param { int sched_priority; } */
	n2s_attr_set(ap, 2, (int)prio);
	n2shim_ret(c, 0);
}

static void n2s_shim_attr_getschedparam(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), pp = n2shim_arg(c, 1);
	int v = n2s_attr_field(ap, 2);

	if (pp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2a32_st32(c, pp, (uint32_t)v);
	n2shim_ret(c, 0);
}

static void n2s_shim_attr_setschedpolicy(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), v = n2shim_arg(c, 1);

	if (v > N2S_SCHED_RR) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2s_attr_set(ap, 1, (int)v);
	n2shim_ret(c, 0);
}

static void n2s_shim_attr_getschedpolicy(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), outp = n2shim_arg(c, 1);

	if (outp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2a32_st32(c, outp, (uint32_t)n2s_attr_field(ap, 1));
	n2shim_ret(c, 0);
}

/* ---- mutexattr / condattr ----------------------------------------------------------------------------------- */
static void n2s_shim_mutexattr_init(struct n2a32_cpu *c)
{
	n2s_shim_attr_init(c);                  /* the guest representation is the same 4-byte address */
}

static void n2s_shim_mutexattr_destroy(struct n2a32_cpu *c)
{
	n2s_shim_attr_destroy(c);
}

static void n2s_shim_mutexattr_settype(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), v = n2shim_arg(c, 1);

	if (v > N2S_MUTEX_ERRORCHECK) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2s_attr_set(ap, 3, (int)v);
	n2shim_ret(c, 0);
}

static void n2s_shim_mutexattr_gettype(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), outp = n2shim_arg(c, 1);

	if (outp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2a32_st32(c, outp, (uint32_t)n2s_attr_field(ap, 3));
	n2shim_ret(c, 0);
}

static void n2s_shim_mutexattr_setpshared(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), v = n2shim_arg(c, 1);

	if (v > 1) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2s_attr_set(ap, 4, (int)v);
	n2shim_ret(c, 0);
}

static void n2s_shim_mutexattr_getpshared(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), outp = n2shim_arg(c, 1);

	if (outp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2a32_st32(c, outp, (uint32_t)n2s_attr_field(ap, 4));
	n2shim_ret(c, 0);
}

static void n2s_shim_condattr_init(struct n2a32_cpu *c)
{
	n2s_shim_attr_init(c);
}

static void n2s_shim_condattr_destroy(struct n2a32_cpu *c)
{
	n2s_shim_attr_destroy(c);
}

static void n2s_shim_condattr_setclock(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), v = n2shim_arg(c, 1);

	if (v != N2S_CLOCK_REALTIME && v != N2S_CLOCK_MONOTONIC) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2s_attr_set(ap, 5, (int)v);
	n2shim_ret(c, 0);
}

static void n2s_shim_condattr_getclock(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), outp = n2shim_arg(c, 1);

	if (outp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2a32_st32(c, outp, (uint32_t)n2s_attr_field(ap, 5));
	n2shim_ret(c, 0);
}

/* ---- mutex -------------------------------------------------------------------------------------------------- */
static void n2s_shim_mutex_init(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), attr = n2shim_arg(c, 1);
	int type = attr != 0 ? n2s_attr_field(attr, N2S_ATTR_TYPE) : N2S_MUTEX_NORMAL;
	int existed = 0;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	if (type < N2S_MUTEX_NORMAL || type > N2S_MUTEX_ERRORCHECK)
		type = N2S_MUTEX_NORMAL;
	rc = n2s_obj_init(ap, N2S_K_MUTEX, type, &existed);
	n2shim_ret(c, (uint32_t)rc);            /* already initialised: leave it alone, return 0 */
}

static void n2s_shim_mutex_destroy(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;
	int rc = 0;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_MUTEX, N2S_MUTEX_NORMAL, 0);
	if (o == NULL) {
		n2shim_ret(c, 0);               /* never used: a static initialiser needs no destroy */
		return;
	}
	if (pthread_mutex_trylock(&o->u.mutex.m) == 0)
		(void)pthread_mutex_unlock(&o->u.mutex.m);
	else
		rc = N2S_EBUSY;
	if (rc == 0)
		n2s_obj_drop(o);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_mutex_lock(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_MUTEX, N2S_MUTEX_NORMAL, 1);
	if (o == NULL) {
		n2shim_ret(c, N2S_ENOMEM);
		return;
	}
	rc = n2s_mutex_acquire(c, &o->u.mutex.m, NULL);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_mutex_trylock(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_MUTEX, N2S_MUTEX_NORMAL, 1);
	if (o == NULL) {
		n2shim_ret(c, N2S_ENOMEM);
		return;
	}
	rc = pthread_mutex_trylock(&o->u.mutex.m);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_mutex_timedlock(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), abst = n2shim_arg(c, 1);
	struct n2s_obj *o;
	struct timespec ts;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	rc = n2s_guest_abs(c, abst, N2S_CLOCK_REALTIME, &ts);
	if (rc != 0) {
		n2shim_ret(c, (uint32_t)rc);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_MUTEX, N2S_MUTEX_NORMAL, 1);
	if (o == NULL) {
		n2shim_ret(c, N2S_ENOMEM);
		return;
	}
	rc = n2s_mutex_acquire(c, &o->u.mutex.m, &ts);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_mutex_unlock(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_MUTEX, N2S_MUTEX_NORMAL, 0);
	if (o == NULL) {
		n2shim_ret(c, N2S_EPERM);       /* unlock of an object no one ever locked */
		return;
	}
	rc = pthread_mutex_unlock(&o->u.mutex.m);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

/* ---- condition variables ------------------------------------------------------------------------------------ */
static void n2s_shim_cond_init(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), attr = n2shim_arg(c, 1);
	int clock = attr != 0 ? n2s_attr_field(attr, N2S_ATTR_CLOCK) : N2S_CLOCK_REALTIME;
	int existed = 0;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	if (clock != N2S_CLOCK_REALTIME && clock != N2S_CLOCK_MONOTONIC)
		clock = N2S_CLOCK_REALTIME;
	rc = n2s_obj_init(ap, N2S_K_COND, clock, &existed);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_cond_destroy(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;
	int rc = 0;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_COND, 0, 0);
	if (o == NULL) {
		n2shim_ret(c, 0);
		return;
	}
	pthread_mutex_lock(&g_obj_lock);
	if (o->refs > 1)
		rc = N2S_EBUSY;                 /* a waiter (or another operation) still owns it */
	pthread_mutex_unlock(&g_obj_lock);
	if (rc == 0)
		n2s_obj_drop(o);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

static int n2s_cond_wait(struct n2a32_cpu *c, uint32_t condp, uint32_t mutexp, uint32_t abst, int timed)
{
	struct n2s_obj *cv, *mu;
	struct timespec ts;
	int clock, rc, rc2;

	if (condp == 0 || mutexp == 0)
		return N2S_EINVAL;
	cv = n2s_obj_ref(condp, N2S_K_COND, 0, 1);
	mu = n2s_obj_ref(mutexp, N2S_K_MUTEX, N2S_MUTEX_NORMAL, 1);
	if (cv == NULL || mu == NULL) {
		n2s_obj_unref(cv);
		n2s_obj_unref(mu);
		return N2S_ENOMEM;
	}
	if (timed) {
		pthread_mutex_lock(&g_obj_lock);
		clock = cv->u.cond.clock;
		pthread_mutex_unlock(&g_obj_lock);
		rc = n2s_guest_abs(c, abst, clock, &ts);
		if (rc != 0) {
			n2s_obj_unref(cv);
			n2s_obj_unref(mu);
			return rc;
		}
	}
	/* Release the guest mutex while holding the condvar's internal mutex, so a signal that arrives between
	 * the unlock and the wait cannot be lost. */
	pthread_mutex_lock(&cv->u.cond.m);
	rc = pthread_mutex_unlock(&mu->u.mutex.m);
	if (rc != 0) {
		pthread_mutex_unlock(&cv->u.cond.m);
		n2s_obj_unref(cv);
		n2s_obj_unref(mu);
		return rc;
	}
	rc = n2s_cond_wait_sliced(c, &cv->u.cond.cv, &cv->u.cond.m, timed ? &ts : NULL);
	(void)pthread_mutex_unlock(&cv->u.cond.m);
	/* POSIX: the mutex is held again on return.  If the process is being torn down the holder may itself have
	 * stopped while holding it, so the re-acquire is bounded too; then the mutex is *not* held (documented). */
	rc2 = n2s_mutex_acquire(c, &mu->u.mutex.m, NULL);
	n2s_obj_unref(cv);
	n2s_obj_unref(mu);
	if (rc == N2S_EINTR || rc2 == N2S_EINTR)
		return N2S_EINTR;
	if (rc2 != 0)
		return rc2;
	return rc;
}

static void n2s_shim_cond_wait(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_cond_wait(c, n2shim_arg(c, 0), n2shim_arg(c, 1), 0, 0));
}

static void n2s_shim_cond_timedwait(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_cond_wait(c, n2shim_arg(c, 0), n2shim_arg(c, 1), n2shim_arg(c, 2), 1));
}

static void n2s_cond_signal(struct n2a32_cpu *c, int broadcast)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_COND, 0, 0);  /* no waiters and no entry: a signal on a static cond is a no-op */
	if (o == NULL) {
		n2shim_ret(c, 0);
		return;
	}
	pthread_mutex_lock(&o->u.cond.m);
	if (broadcast)
		(void)pthread_cond_broadcast(&o->u.cond.cv);
	else
		(void)pthread_cond_signal(&o->u.cond.cv);
	pthread_mutex_unlock(&o->u.cond.m);
	n2s_obj_unref(o);
	n2shim_ret(c, 0);
}

static void n2s_shim_cond_signal(struct n2a32_cpu *c)
{
	n2s_cond_signal(c, 0);
}

static void n2s_shim_cond_broadcast(struct n2a32_cpu *c)
{
	n2s_cond_signal(c, 1);
}

/* ---- rwlocks ------------------------------------------------------------------------------------------------ */
static void n2s_shim_rwlock_init(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), attr = n2shim_arg(c, 1);
	int existed = 0;
	int rc;

	(void)attr;
	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	rc = n2s_obj_init(ap, N2S_K_RWLOCK, 0, &existed);
	n2shim_ret(c, (uint32_t)rc);
}

static void n2s_shim_rwlock_destroy(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;
	int rc = 0;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_RWLOCK, 0, 0);
	if (o == NULL) {
		n2shim_ret(c, 0);
		return;
	}
	rc = pthread_rwlock_trywrlock(&o->u.rw.rw);
	if (rc == 0)
		(void)pthread_rwlock_unlock(&o->u.rw.rw);
	else
		rc = N2S_EBUSY;
	if (rc == 0)
		n2s_obj_drop(o);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

static int n2s_rwlock_op(struct n2a32_cpu *c, uint32_t ap, int op, uint32_t abst)
{
	struct n2s_obj *o;
	struct timespec ts;
	int rc;

	if (ap == 0)
		return N2S_EINVAL;
	o = n2s_obj_ref(ap, N2S_K_RWLOCK, 0, 1);
	if (o == NULL)
		return N2S_ENOMEM;
	if (op >= 4) {                          /* timed variants */
		rc = n2s_guest_abs(c, abst, N2S_CLOCK_REALTIME, &ts);
		if (rc != 0) {
			n2s_obj_unref(o);
			return rc;
		}
	}
	{
		int write = (op == 1 || op == 3 || op == 5);
		int timed = (op == 4 || op == 5);

		if (op == 2 || op == 3) {       /* try variants */
			rc = write ? pthread_rwlock_trywrlock(&o->u.rw.rw) : pthread_rwlock_tryrdlock(&o->u.rw.rw);
		} else {
			const struct timespec *abs = timed ? &ts : NULL;

			for (;;) {
				struct timespec slice;
				int expiring;

				expiring = n2s_slice(&slice, abs);
				rc = n2s_rwlock_timedlock(&o->u.rw.rw, write, &slice);
				if (rc == 0)
					break;
				if (rc != ETIMEDOUT)
					break;          /* EDEADLK etc. */
				if (expiring)
					break;          /* the guest deadline was already past */
				if (n2s_stop_requested(c)) {
					rc = N2S_EINTR;
					break;
				}
			}
		}
	}
	n2s_obj_unref(o);
	if (rc == EDEADLK)
		rc = N2S_EBUSY;                 /* write-locking an rwlock this thread already holds */
	return rc;
}

static void n2s_shim_rwlock_rdlock(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_rwlock_op(c, n2shim_arg(c, 0), 0, 0));
}

static void n2s_shim_rwlock_wrlock(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_rwlock_op(c, n2shim_arg(c, 0), 1, 0));
}

static void n2s_shim_rwlock_tryrdlock(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_rwlock_op(c, n2shim_arg(c, 0), 2, 0));
}

static void n2s_shim_rwlock_trywrlock(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_rwlock_op(c, n2shim_arg(c, 0), 3, 0));
}

static void n2s_shim_rwlock_timedrdlock(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_rwlock_op(c, n2shim_arg(c, 0), 4, n2shim_arg(c, 1)));
}

static void n2s_shim_rwlock_timedwrlock(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_rwlock_op(c, n2shim_arg(c, 0), 5, n2shim_arg(c, 1)));
}

static void n2s_shim_rwlock_unlock(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;
	int rc;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_RWLOCK, 0, 0);
	if (o == NULL) {
		n2shim_ret(c, N2S_EPERM);
		return;
	}
	rc = pthread_rwlock_unlock(&o->u.rw.rw);
	n2s_obj_unref(o);
	n2shim_ret(c, (uint32_t)rc);
}

/* ---- semaphores --------------------------------------------------------------------------------------------- */
static void n2s_shim_sem_init(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), pshared = n2shim_arg(c, 1), value = n2shim_arg(c, 2);
	int existed = 0;
	int rc;

	if (ap == 0 || value > 0x7fffffffu) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	(void)pshared;                           /* process-shared semaphores cannot span the emulated space */
	rc = n2s_obj_init(ap, N2S_K_SEM, (int)value, &existed);
	n2shim_ret(c, (uint32_t)(rc != 0 ? rc : (existed ? N2S_EBUSY : 0)));
}

static int n2s_sem_take(struct n2a32_cpu *c, uint32_t ap, uint32_t abst, int mode)
{
	struct n2s_obj *o;
	struct timespec ts;
	int rc = 0;

	if (ap == 0)
		return N2S_EINVAL;
	o = n2s_obj_ref(ap, N2S_K_SEM, 0, 1);
	if (o == NULL)
		return N2S_ENOMEM;
	if (mode == 2) {
		rc = n2s_guest_abs(c, abst, N2S_CLOCK_REALTIME, &ts);
		if (rc != 0) {
			n2s_obj_unref(o);
			return rc;
		}
	}
	pthread_mutex_lock(&o->u.sem.m);
	while (o->u.sem.count == 0) {
		if (mode == 1) {                /* trywait */
			rc = N2S_EAGAIN;
			break;
		}
		rc = n2s_cond_wait_sliced(c, &o->u.sem.cv, &o->u.sem.m, mode == 2 ? &ts : NULL);
		if (rc != 0)
			break;                  /* ETIMEDOUT, EINTR or a host error */
	}
	if (rc == 0)
		o->u.sem.count--;
	pthread_mutex_unlock(&o->u.sem.m);
	n2s_obj_unref(o);
	return rc;
}

static void n2s_shim_sem_wait(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_sem_take(c, n2shim_arg(c, 0), 0, 0));
}

static void n2s_shim_sem_trywait(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_sem_take(c, n2shim_arg(c, 0), 0, 1));
}

static void n2s_shim_sem_timedwait(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_sem_take(c, n2shim_arg(c, 0), n2shim_arg(c, 1), 2));
}

static void n2s_shim_sem_post(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_SEM, 0, 1);
	if (o == NULL) {
		n2shim_ret(c, N2S_ENOMEM);
		return;
	}
	pthread_mutex_lock(&o->u.sem.m);
	if (o->u.sem.count != 0xffffffffu)
		o->u.sem.count++;
	(void)pthread_cond_signal(&o->u.sem.cv);
	pthread_mutex_unlock(&o->u.sem.m);
	n2s_obj_unref(o);
	n2shim_ret(c, 0);
}

static void n2s_shim_sem_getvalue(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0), outp = n2shim_arg(c, 1);
	struct n2s_obj *o;
	unsigned v;

	if (ap == 0 || outp == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_SEM, 0, 1);
	if (o == NULL) {
		n2shim_ret(c, N2S_ENOMEM);
		return;
	}
	pthread_mutex_lock(&o->u.sem.m);
	v = o->u.sem.count;
	pthread_mutex_unlock(&o->u.sem.m);
	n2s_obj_unref(o);
	n2a32_st32(c, outp, v);
	n2shim_ret(c, 0);
}

static void n2s_shim_sem_destroy(struct n2a32_cpu *c)
{
	uint32_t ap = n2shim_arg(c, 0);
	struct n2s_obj *o;

	if (ap == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ap, N2S_K_SEM, 0, 0);
	if (o == NULL) {
		n2shim_ret(c, 0);
		return;
	}
	/* Destroy always succeeds.  A sem_post() that is still in flight (the waiter has already returned, the
	 * poster has not yet dropped its reference) finishes on the retired entry and frees it; a thread still
	 * blocked in sem_wait() at destroy time is the POSIX undefined case and stays blocked (never freed). */
	n2s_obj_drop(o);
	n2s_obj_unref(o);
	n2shim_ret(c, 0);
}

/* ---- pthread_once ------------------------------------------------------------------------------------------- */
static void n2s_shim_pthread_once(struct n2a32_cpu *c)
{
	uint32_t ctl = n2shim_arg(c, 0), fn = n2shim_arg(c, 1);
	struct n2s_obj *o;
	unsigned long me = n2s_host_tid();
	int run = 0;

	if (ctl == 0 || fn == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	o = n2s_obj_ref(ctl, N2S_K_ONCE, 0, 1);
	if (o == NULL) {
		n2shim_ret(c, N2S_ENOMEM);
		return;
	}
	pthread_mutex_lock(&o->u.once.m);
	for (;;) {
		if (o->u.once.state == 2)
			break;
		if (o->u.once.state == 0) {
			o->u.once.state = 1;
			o->u.once.owner = me;
			run = 1;
			break;
		}
		if (o->u.once.owner == me) {
			/* Re-entering the same control from its own init routine is undefined; refuse instead of
			 * deadlocking.  Counted for the tests. */
			pthread_mutex_unlock(&o->u.once.m);
			n2s_obj_unref(o);
			pthread_mutex_lock(&g_obj_lock);
			g_nested_once++;
			pthread_mutex_unlock(&g_obj_lock);
			n2shim_ret(c, N2S_EDEADLK);
			return;
		}
		{
			int wrc = n2s_cond_wait_sliced(c, &o->u.once.cv, &o->u.once.m, NULL);

			if (wrc != 0) {
				/* teardown (or a host error): leave the control to the runner and let the cpu stop */
				pthread_mutex_unlock(&o->u.once.m);
				n2s_obj_unref(o);
				n2shim_ret(c, 0);
				return;
			}
		}
	}
	pthread_mutex_unlock(&o->u.once.m);

	if (run) {
		uint32_t r1 = 0;
		enum n2a32_stop before = c->stop;
		enum n2a32_stop st;

		(void)n2a32_call(c, fn, NULL, 0, &r1);          /* guest code, nothing held */
		/* A nested n2a32_call() restores the caller's stop reason on success and leaves the failure in
		 * c->stop only when it stopped; the caller's value is NONE during a run. */
		st = c->stop == before ? N2A32_STOP_RETURN : c->stop;
		pthread_mutex_lock(&o->u.once.m);
		/* On a normal return the control is done; on a fatal stop mark it done as well so waiters cannot
		 * hang, and record the failure (the caller stops at its next instruction boundary too). */
		o->u.once.state = 2;
		(void)pthread_cond_broadcast(&o->u.once.cv);
		pthread_mutex_unlock(&o->u.once.m);
		if (st != N2A32_STOP_RETURN && st != N2A32_STOP_REQUEST) {
			struct n2s_thread *me = n2s_self(c);   /* takes g_thr_lock itself: do it before locking */

			pthread_mutex_lock(&g_thr_lock);
			n2s_note_failure_locked((int)st, me, c);
			pthread_mutex_unlock(&g_thr_lock);
			/* the caller stops at its next instruction boundary too */
			n2s_atomic_stop(c);
		}
	}
	n2s_obj_unref(o);
	n2shim_ret(c, 0);
}

/* ---- scheduling, time, process plumbing ---------------------------------------------------------------------- */
static void n2s_shim_sched_yield(struct n2a32_cpu *c)
{
	(void)sched_yield();
	n2shim_ret(c, 0);
}

static void n2s_shim_sched_getaffinity(struct n2a32_cpu *c)
{
	uint32_t pid = n2shim_arg(c, 0), size = n2shim_arg(c, 1), mask = n2shim_arg(c, 2);
	uint32_t n, i;

	(void)pid;
	if (mask == 0 || size == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n = size > 128u ? 128u : size;
	for (i = 0; i < n; i++)
		n2a32_st8(c, mask + i, i == 0 ? 0xffu : 0u);    /* 8 CPUs, bits 0-7 */
	n2shim_ret(c, 0);
}

static void n2s_shim_sched_setaffinity(struct n2a32_cpu *c)
{
	uint32_t pid = n2shim_arg(c, 0), size = n2shim_arg(c, 1), mask = n2shim_arg(c, 2);

	(void)pid;
	if (mask == 0 || size == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2shim_ret(c, 0);                        /* accepted, not applied: the guest does not own the host cpus */
}

static void n2s_shim_usleep(struct n2a32_cpu *c)
{
	uint32_t usec = n2shim_arg(c, 0);
	struct timespec ts;
	uint64_t left = (uint64_t)usec * 1000u;

	while (left > 0) {
		uint64_t slice = left > 50000000ull ? 50000000ull : left;   /* 50 ms slices */

		if (__atomic_load_n(&c->stop_request, __ATOMIC_ACQUIRE) != 0)
			break;
		ts.tv_sec = (time_t)(slice / 1000000000ull);
		ts.tv_nsec = (long)(slice % 1000000000ull);
		if (nanosleep(&ts, NULL) != 0 && errno == EINTR)
			continue;
		left -= slice;
	}
	n2shim_ret(c, 0);
}

static void n2s_shim_gettid(struct n2a32_cpu *c)
{
	n2shim_ret(c, (uint32_t)n2s_host_tid());
}

static void n2s_shim_prctl(struct n2a32_cpu *c)
{
	uint32_t option = n2shim_arg(c, 0), arg2 = n2shim_arg(c, 1);
	struct n2s_thread *t;

	if (option == 15 /* PR_SET_NAME */) {
		char name[N2S_NAME_MAX];
		int rc = n2s_read_guest_str(c, arg2, name, sizeof(name));

		if (rc != 0) {
			n2shim_ret(c, (uint32_t)rc);
			return;
		}
		t = n2s_self(c);
		if (t != NULL) {
			pthread_mutex_lock(&g_thr_lock);
			memcpy(t->name, name, sizeof(name));
			t->named = 1;
			pthread_mutex_unlock(&g_thr_lock);
			n2s_host_setname(pthread_self(), name);
		}
		n2shim_ret(c, 0);
		return;
	}
	if (option == 16 /* PR_GET_NAME */) {
		char name[N2S_NAME_MAX];

		if (arg2 == 0) {
			n2shim_ret(c, N2S_EINVAL);
			return;
		}
		name[0] = 0;
		t = n2s_self(c);
		if (t != NULL) {
			pthread_mutex_lock(&g_thr_lock);
			memcpy(name, t->name, sizeof(name));
			pthread_mutex_unlock(&g_thr_lock);
		}
		name[N2S_NAME_MAX - 1] = 0;
		{
			unsigned i;

			for (i = 0; i < N2S_NAME_MAX; i++)
				n2a32_st8(c, arg2 + i, (uint8_t)name[i]);
		}
		n2shim_ret(c, 0);
		return;
	}
	n2shim_ret(c, 0);                        /* every other option is accepted and ignored */
}

/* ---- signals (recorded, never delivered) --------------------------------------------------------------------- */
static uint32_t g_sig_action[65];
static uint32_t g_sig_handler[65];

static void n2s_shim_sigaction(struct n2a32_cpu *c)
{
	uint32_t sig = n2shim_arg(c, 0), act = n2shim_arg(c, 1), old = n2shim_arg(c, 2);

	if (sig == 0 || sig > 64) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	pthread_mutex_lock(&g_obj_lock);
	if (act != 0) {
		g_sig_action[sig] = act;
	}
	pthread_mutex_unlock(&g_obj_lock);
	if (old != 0)
		n2a32_st32(c, old, 0);           /* sa_handler = SIG_DFL */
	n2shim_ret(c, 0);
}

static void n2s_shim_sigemptyset(struct n2a32_cpu *c)
{
	uint32_t set = n2shim_arg(c, 0);

	if (set == 0) {
		n2shim_ret(c, N2S_EINVAL);
		return;
	}
	n2a32_st32(c, set, 0);                   /* bionic's 32-bit sigset_t is one word */
	n2shim_ret(c, 0);
}

static void n2s_shim_signal(struct n2a32_cpu *c)
{
	uint32_t sig = n2shim_arg(c, 0), handler = n2shim_arg(c, 1);
	uint32_t prev = 0;

	if (sig == 0 || sig > 64) {
		n2shim_ret(c, (uint32_t)-1);     /* SIG_ERR */
		return;
	}
	pthread_mutex_lock(&g_obj_lock);
	prev = g_sig_handler[sig];
	g_sig_handler[sig] = handler;
	pthread_mutex_unlock(&g_obj_lock);
	n2shim_ret(c, prev);
}

static void n2s_shim_register_atfork(struct n2a32_cpu *c)
{
	(void)c;
	n2shim_ret(c, 0);
}

/* ---- accessors ---------------------------------------------------------------------------------------------- */
int n2shim_thread_failure(void)
{
	int v;

	pthread_mutex_lock(&g_thr_lock);
	v = g_fail_reason;
	pthread_mutex_unlock(&g_thr_lock);
	return v;
}

unsigned n2shim_thread_failure_thread(void)
{
	unsigned v;

	pthread_mutex_lock(&g_thr_lock);
	v = g_fail_thread;
	pthread_mutex_unlock(&g_thr_lock);
	return v;
}

int n2shim_thread_failure_detail(uint32_t *insn, uint32_t *addr)
{
	int v;

	pthread_mutex_lock(&g_thr_lock);
	v = g_fail_reason;
	if (insn != NULL)
		*insn = g_fail_insn;
	if (addr != NULL)
		*addr = g_fail_addr;
	pthread_mutex_unlock(&g_thr_lock);
	return v;
}

unsigned n2shim_thread_failure_count(void)
{
	unsigned v;

	pthread_mutex_lock(&g_thr_lock);
	v = g_fail_count;
	pthread_mutex_unlock(&g_thr_lock);
	return v;
}

void n2shim_thread_failure_clear(void)
{
	pthread_mutex_lock(&g_thr_lock);
	g_fail_reason = 0;
	g_fail_count = 0;
	g_fail_thread = 0;
	g_fail_insn = 0;
	g_fail_addr = 0;
	pthread_mutex_unlock(&g_thr_lock);
}

int n2shim_thread_note_stop(struct n2a32_cpu *c)
{
	struct n2s_thread *t;
	int st;

	if (c == NULL)
		return 0;
	st = (int)c->stop;
	if (st == N2A32_STOP_NONE || st == N2A32_STOP_RETURN || st == N2A32_STOP_REQUEST)
		return 0;
	t = n2s_self(c);                        /* may take g_thr_lock: never call it under the lock */
	pthread_mutex_lock(&g_thr_lock);
	n2s_note_failure_locked(st, t, c);
	pthread_mutex_unlock(&g_thr_lock);
	return st;
}

unsigned n2shim_thread_request_stop(void)
{
	unsigned n;

	pthread_mutex_lock(&g_thr_lock);
	n = n2s_request_stop_locked(NULL);
	pthread_mutex_unlock(&g_thr_lock);
	return n;
}

unsigned n2shim_thread_live(void)
{
	unsigned i, n = 0;

	pthread_mutex_lock(&g_thr_lock);
	for (i = 0; i < N2S_MAX_THREADS; i++)
		if (g_threads[i].state == N2S_T_RUNNING)
			n++;
	pthread_mutex_unlock(&g_thr_lock);
	return n;
}

unsigned n2shim_thread_created(void)
{
	unsigned v;

	pthread_mutex_lock(&g_thr_lock);
	v = g_thr_created;
	pthread_mutex_unlock(&g_thr_lock);
	return v;
}

unsigned n2shim_thread_stop_requests(void)
{
	unsigned v;

	pthread_mutex_lock(&g_thr_lock);
	v = g_stop_requests;
	pthread_mutex_unlock(&g_thr_lock);
	return v;
}

unsigned n2shim_thread_objects(void)
{
	unsigned v;

	pthread_mutex_lock(&g_obj_lock);
	v = g_obj_live;
	pthread_mutex_unlock(&g_obj_lock);
	return v;
}

unsigned n2shim_thread_object_peak(void)
{
	unsigned v;

	pthread_mutex_lock(&g_obj_lock);
	v = g_obj_peak;
	pthread_mutex_unlock(&g_obj_lock);
	return v;
}

unsigned n2shim_thread_kind_clashes(void)
{
	unsigned v;

	pthread_mutex_lock(&g_obj_lock);
	v = g_kind_clashes;
	pthread_mutex_unlock(&g_obj_lock);
	return v;
}

unsigned n2shim_thread_nested_once(void)
{
	unsigned v;

	pthread_mutex_lock(&g_obj_lock);
	v = g_nested_once;
	pthread_mutex_unlock(&g_obj_lock);
	return v;
}

uint32_t n2shim_thread_self_id(void)
{
	unsigned v = tls_id;

	return v;
}

/* ---------------------------------------------------------------------------------------------------------------
 * The shim table
 * -------------------------------------------------------------------------------------------------------------- */
const struct n2shim_entry n2shim_thread[] = {
	{ "gettid", n2s_shim_gettid },
	{ "prctl", n2s_shim_prctl },
	{ "pthread_attr_destroy", n2s_shim_attr_destroy },
	{ "pthread_attr_getdetachstate", n2s_shim_attr_getdetachstate },
	{ "pthread_attr_getschedparam", n2s_shim_attr_getschedparam },
	{ "pthread_attr_getschedpolicy", n2s_shim_attr_getschedpolicy },
	{ "pthread_attr_init", n2s_shim_attr_init },
	{ "pthread_attr_setdetachstate", n2s_shim_attr_setdetachstate },
	{ "pthread_attr_setschedparam", n2s_shim_attr_setschedparam },
	{ "pthread_attr_setschedpolicy", n2s_shim_attr_setschedpolicy },
	{ "pthread_cond_broadcast", n2s_shim_cond_broadcast },
	{ "pthread_cond_destroy", n2s_shim_cond_destroy },
	{ "pthread_cond_init", n2s_shim_cond_init },
	{ "pthread_cond_signal", n2s_shim_cond_signal },
	{ "pthread_cond_timedwait", n2s_shim_cond_timedwait },
	{ "pthread_cond_wait", n2s_shim_cond_wait },
	{ "pthread_condattr_destroy", n2s_shim_condattr_destroy },
	{ "pthread_condattr_getclock", n2s_shim_condattr_getclock },
	{ "pthread_condattr_init", n2s_shim_condattr_init },
	{ "pthread_condattr_setclock", n2s_shim_condattr_setclock },
	{ "pthread_create", n2s_shim_pthread_create },
	{ "pthread_detach", n2s_shim_pthread_detach },
	{ "pthread_equal", n2s_shim_pthread_equal },
	{ "pthread_exit", n2s_shim_pthread_exit },
	{ "pthread_gettid_np", n2s_shim_pthread_gettid },
	{ "pthread_join", n2s_shim_pthread_join },
	{ "pthread_kill", n2s_shim_pthread_kill },
	{ "pthread_mutex_destroy", n2s_shim_mutex_destroy },
	{ "pthread_mutex_init", n2s_shim_mutex_init },
	{ "pthread_mutex_lock", n2s_shim_mutex_lock },
	{ "pthread_mutex_timedlock", n2s_shim_mutex_timedlock },
	{ "pthread_mutex_trylock", n2s_shim_mutex_trylock },
	{ "pthread_mutex_unlock", n2s_shim_mutex_unlock },
	{ "pthread_mutexattr_destroy", n2s_shim_mutexattr_destroy },
	{ "pthread_mutexattr_getpshared", n2s_shim_mutexattr_getpshared },
	{ "pthread_mutexattr_gettype", n2s_shim_mutexattr_gettype },
	{ "pthread_mutexattr_init", n2s_shim_mutexattr_init },
	{ "pthread_mutexattr_setpshared", n2s_shim_mutexattr_setpshared },
	{ "pthread_mutexattr_settype", n2s_shim_mutexattr_settype },
	{ "pthread_once", n2s_shim_pthread_once },
	{ "pthread_rwlock_destroy", n2s_shim_rwlock_destroy },
	{ "pthread_rwlock_init", n2s_shim_rwlock_init },
	{ "pthread_rwlock_rdlock", n2s_shim_rwlock_rdlock },
	{ "pthread_rwlock_timedrdlock", n2s_shim_rwlock_timedrdlock },
	{ "pthread_rwlock_timedwrlock", n2s_shim_rwlock_timedwrlock },
	{ "pthread_rwlock_tryrdlock", n2s_shim_rwlock_tryrdlock },
	{ "pthread_rwlock_trywrlock", n2s_shim_rwlock_trywrlock },
	{ "pthread_rwlock_unlock", n2s_shim_rwlock_unlock },
	{ "pthread_rwlock_wrlock", n2s_shim_rwlock_wrlock },
	{ "pthread_self", n2s_shim_pthread_self },
	{ "pthread_setname_np", n2s_shim_pthread_setname },
	{ "sched_getaffinity", n2s_shim_sched_getaffinity },
	{ "sched_setaffinity", n2s_shim_sched_setaffinity },
	{ "sched_yield", n2s_shim_sched_yield },
	{ "sem_destroy", n2s_shim_sem_destroy },
	{ "sem_getvalue", n2s_shim_sem_getvalue },
	{ "sem_init", n2s_shim_sem_init },
	{ "sem_post", n2s_shim_sem_post },
	{ "sem_timedwait", n2s_shim_sem_timedwait },
	{ "sem_trywait", n2s_shim_sem_trywait },
	{ "sem_wait", n2s_shim_sem_wait },
	{ "sigaction", n2s_shim_sigaction },
	{ "sigemptyset", n2s_shim_sigemptyset },
	{ "signal", n2s_shim_signal },
	{ "usleep", n2s_shim_usleep },
	{ "__register_atfork", n2s_shim_register_atfork },
	{ NULL, NULL }
};
