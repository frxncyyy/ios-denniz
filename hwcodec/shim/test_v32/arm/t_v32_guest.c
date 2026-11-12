/* SPDX-License-Identifier: MIT */
/*
 * t_v32_guest.c -- the guest side of the V32 tests: a real ARM shared object with no C library that imports
 * exactly the pthread/sem/sched/signal functions n2shim_thread[] implements.
 *
 * Built by run_tests.sh with
 *   clang --target=armv7a-linux-androideabi -marm -march=armv8-a -mfloat-abi=soft -fPIC -shared -nostdlib
 *   -fno-stack-protector -fno-unwind-tables
 * loaded by V03's loader and executed by V02's core.  memcpy/memset are defined here so the object needs no
 * other shim table; every entry point returns 0 when all of its checks passed.
 *
 * The check counters are globals so the host driver can report which guest check failed (g_fail_id holds the
 * __LINE__ values).  All ck() calls are made from the main guest thread, so the counters need no lock.
 *
 * Deliberate choices for the interpreter: ARM mode only, no switch statements (no jump tables), no 64-bit
 * division, no varargs, and no guest-side atomics (V12's LDREX/STREX is not on the tested path here).
 */
typedef unsigned int u32;
typedef unsigned long uptr;
typedef u32 pthread_t;

extern int pthread_create(pthread_t *, const u32 *, void *(*)(void *), void *);
extern int pthread_join(pthread_t, void **);
extern void pthread_exit(void *);
extern pthread_t pthread_self(void);
extern int pthread_equal(pthread_t, pthread_t);
extern int pthread_detach(pthread_t);
extern int pthread_kill(pthread_t, int);
extern int pthread_setname_np(pthread_t, const char *);
extern int pthread_gettid_np(pthread_t);
extern int pthread_once(u32 *, void (*)(void));
extern int pthread_attr_init(u32 *);
extern int pthread_attr_destroy(u32 *);
extern int pthread_attr_setdetachstate(u32 *, int);
extern int pthread_attr_getdetachstate(const u32 *, int *);
extern int pthread_attr_setschedparam(u32 *, const void *);
extern int pthread_attr_getschedparam(const u32 *, void *);
extern int pthread_attr_setschedpolicy(u32 *, int);
extern int pthread_attr_getschedpolicy(const u32 *, int *);
extern int pthread_mutex_init(u32 *, const u32 *);
extern int pthread_mutex_destroy(u32 *);
extern int pthread_mutex_lock(u32 *);
extern int pthread_mutex_trylock(u32 *);
extern int pthread_mutex_timedlock(u32 *, const void *);
extern int pthread_mutex_unlock(u32 *);
extern int pthread_mutexattr_init(u32 *);
extern int pthread_mutexattr_destroy(u32 *);
extern int pthread_mutexattr_settype(u32 *, int);
extern int pthread_mutexattr_gettype(const u32 *, int *);
extern int pthread_cond_init(u32 *, const u32 *);
extern int pthread_cond_destroy(u32 *);
extern int pthread_cond_wait(u32 *, u32 *);
extern int pthread_cond_timedwait(u32 *, u32 *, const void *);
extern int pthread_cond_signal(u32 *);
extern int pthread_cond_broadcast(u32 *);
extern int pthread_condattr_init(u32 *);
extern int pthread_condattr_destroy(u32 *);
extern int pthread_condattr_setclock(u32 *, int);
extern int pthread_rwlock_init(u32 *, const void *);
extern int pthread_rwlock_destroy(u32 *);
extern int pthread_rwlock_rdlock(u32 *);
extern int pthread_rwlock_wrlock(u32 *);
extern int pthread_rwlock_tryrdlock(u32 *);
extern int pthread_rwlock_trywrlock(u32 *);
extern int pthread_rwlock_timedrdlock(u32 *, const void *);
extern int pthread_rwlock_timedwrlock(u32 *, const void *);
extern int pthread_rwlock_unlock(u32 *);
extern int sem_init(u32 *, int, unsigned);
extern int sem_destroy(u32 *);
extern int sem_wait(u32 *);
extern int sem_trywait(u32 *);
extern int sem_timedwait(u32 *, const void *);
extern int sem_post(u32 *);
extern int sem_getvalue(u32 *, int *);
extern int sched_yield(void);
extern int sched_getaffinity(int, unsigned, void *);
extern int sched_setaffinity(int, unsigned, const void *);
extern int usleep(unsigned);
extern int gettid(void);
extern int prctl(int, ...);
extern int sigaction(int, const void *, void *);
extern int sigemptyset(void *);
extern void *signal(int, void *);
extern int __register_atfork(void *, void *, void *, void *);

/* Linux error numbers the tests compare against. */
#define EPERM 1
#define ESRCH 3
#define EAGAIN 11
#define EBUSY 16
#define EINVAL 22
#define EDEADLK 35
#define EINTR 4
#define ETIMEDOUT 110

int g_total;
int g_fail;
int g_fail_n;
int g_fail_id[64];
int g_dbg[8];           /* values the host driver prints when a guest check fails */

void *memcpy(void *d, const void *s, unsigned n)
{
	unsigned char *dd = (unsigned char *)d;
	const unsigned char *ss = (const unsigned char *)s;
	unsigned i;

	for (i = 0; i < n; i++)
		dd[i] = ss[i];
	return d;
}

void *memset(void *d, int c, unsigned n)
{
	unsigned char *dd = (unsigned char *)d;
	unsigned i;

	for (i = 0; i < n; i++)
		dd[i] = (unsigned char)c;
	return d;
}

unsigned strlen(const char *s)
{
	unsigned n = 0;

	while (s[n] != 0)
		n++;
	return n;
}

int memcmp(const void *a, const void *b, unsigned n)
{
	const unsigned char *aa = (const unsigned char *)a;
	const unsigned char *bb = (const unsigned char *)b;
	unsigned i;

	for (i = 0; i < n; i++)
		if (aa[i] != bb[i])
			return (int)aa[i] - (int)bb[i];
	return 0;
}

int strcmp(const char *a, const char *b)
{
	unsigned i = 0;

	while (a[i] != 0 && a[i] == b[i])
		i++;
	return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
}

static void ck(int ok, int id)
{
	g_total++;
	if (!ok) {
		g_fail++;
		if (g_fail_n < 64)
			g_fail_id[g_fail_n++] = id;
	}
}

/* A guest struct timespec is two 32-bit words; this host is 32-bit ARM, so the layout is the bionic one. */
struct gts {
	int sec;
	int nsec;
};

/* ---------------------------------------------------------------------------------------------------------------
 * 1. 8 threads x 100000 increments under a statically initialised (all-zero) mutex
 * -------------------------------------------------------------------------------------------------------------- */
#define INC_THREADS 8
#define INC_ITERS   100000

static u32 g_inc_mutex;                 /* all zero == PTHREAD_MUTEX_INITIALIZER, never pthread_mutex_init'ed */
static int g_inc_count;
static int g_inc_wfail[INC_THREADS];    /* per worker: the __LINE__ of its failure, 0 = none */

static void *inc_worker(void *arg)
{
	int idx = (int)(uptr)arg;
	int i;

	for (i = 0; i < INC_ITERS; i++) {
		if (pthread_mutex_lock(&g_inc_mutex) != 0) {
			g_inc_wfail[idx] = __LINE__;
			return (void *)0xdead;
		}
		g_inc_count++;
		if (pthread_mutex_unlock(&g_inc_mutex) != 0)
			g_inc_wfail[idx] = __LINE__;
	}
	return (void *)0x51;
}

int v32_increments(void)
{
	pthread_t t[INC_THREADS];
	int i;
	int before = g_fail;

	for (i = 0; i < INC_THREADS; i++) {
		g_inc_wfail[i] = 0;
		ck(pthread_create(&t[i], 0, inc_worker, (void *)(uptr)i) == 0, __LINE__);
	}
	for (i = 0; i < INC_THREADS; i++) {
		void *rv = 0;

		ck(pthread_join(t[i], &rv) == 0, __LINE__);
		ck(rv == (void *)0x51, __LINE__);
		ck(g_inc_wfail[i] == 0, __LINE__);
	}
	ck(g_inc_count == INC_THREADS * INC_ITERS, __LINE__);
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 2. producer / consumer over a ring buffer with mutex + condition variable
 * -------------------------------------------------------------------------------------------------------------- */
#define QN 8
#define QITEMS 2000

static u32 g_q_mutex;
static u32 g_q_cond;
static int q_buf[QN];
static int q_head, q_tail, q_count, q_done, q_produced, q_consumed;
static int q_pfail;                     /* producer failure line */
static int q_cfail[4];                  /* per consumer */

static void *q_producer(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < QITEMS; i++) {
		if (pthread_mutex_lock(&g_q_mutex) != 0) {
			q_pfail = __LINE__;
			return 0;
		}
		while (q_count == QN)
			if (pthread_cond_wait(&g_q_cond, &g_q_mutex) != 0)
				q_pfail = __LINE__;
		q_buf[q_tail] = i;
		q_tail = (q_tail + 1) % QN;
		q_count++;
		q_produced += i;
		if (pthread_cond_signal(&g_q_cond) != 0)
			q_pfail = __LINE__;
		if (pthread_mutex_unlock(&g_q_mutex) != 0)
			q_pfail = __LINE__;
	}
	(void)pthread_mutex_lock(&g_q_mutex);
	q_done = 1;
	(void)pthread_cond_broadcast(&g_q_cond);
	(void)pthread_mutex_unlock(&g_q_mutex);
	return 0;
}

static int q_local_sum;                 /* per consumer, so the reads are not shared between readers */

static void *q_consumer(void *arg)
{
	int idx = (int)(uptr)arg;
	int mine = 0;

	if (pthread_mutex_lock(&g_q_mutex) != 0) {
		q_cfail[idx] = __LINE__;
		return 0;
	}
	for (;;) {
		while (q_count == 0 && !q_done)
			if (pthread_cond_wait(&g_q_cond, &g_q_mutex) != 0)
				q_cfail[idx] = __LINE__;
		if (q_count == 0 && q_done)
			break;
		mine += q_buf[q_head];
		q_head = (q_head + 1) % QN;
		q_count--;
		if (pthread_cond_signal(&g_q_cond) != 0)
			q_cfail[idx] = __LINE__;
	}
	(void)pthread_mutex_unlock(&g_q_mutex);
	/* publish under the mutex so the final sum is ordered */
	if (pthread_mutex_lock(&g_q_mutex) != 0)
		q_cfail[idx] = __LINE__;
	q_local_sum += mine;
	(void)pthread_mutex_unlock(&g_q_mutex);
	return 0;
}

int v32_prodcons(void)
{
	pthread_t prod, cons[4];
	int i;
	int before = g_fail;

	q_head = q_tail = q_count = q_done = q_produced = q_consumed = 0;
	q_local_sum = 0;
	q_pfail = 0;
	q_cfail[0] = q_cfail[1] = q_cfail[2] = q_cfail[3] = 0;
	ck(pthread_mutex_init(&g_q_mutex, 0) == 0, __LINE__);
	ck(pthread_cond_init(&g_q_cond, 0) == 0, __LINE__);
	ck(pthread_create(&prod, 0, q_producer, 0) == 0, __LINE__);
	for (i = 0; i < 4; i++)
		ck(pthread_create(&cons[i], 0, q_consumer, (void *)(uptr)i) == 0, __LINE__);
	ck(pthread_join(prod, 0) == 0, __LINE__);
	for (i = 0; i < 4; i++)
		ck(pthread_join(cons[i], 0) == 0, __LINE__);
	q_consumed = q_local_sum;
	ck(q_pfail == 0, __LINE__);
	ck(q_cfail[0] == 0 && q_cfail[1] == 0 && q_cfail[2] == 0 && q_cfail[3] == 0, __LINE__);
	ck(q_produced == QITEMS * (QITEMS - 1) / 2, __LINE__);
	ck(q_consumed == q_produced, __LINE__);
	ck(q_count == 0, __LINE__);
	ck(pthread_cond_destroy(&g_q_cond) == 0, __LINE__);
	ck(pthread_mutex_destroy(&g_q_mutex) == 0, __LINE__);
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 3. semaphore ping-pong, 2 threads x 5000 exchanges
 * -------------------------------------------------------------------------------------------------------------- */
#define PP_ROUNDS 5000

static u32 g_pp_main, g_pp_peer;
static int g_pp_value;
static int g_pp_pfail;

static void *pp_peer(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < PP_ROUNDS; i++) {
		if (sem_wait(&g_pp_peer) != 0) {
			g_pp_pfail = __LINE__;
			return 0;
		}
		if (g_pp_value != i * 2 + 1) {
			g_pp_pfail = __LINE__;
			return 0;
		}
		g_pp_value++;
		if (sem_post(&g_pp_main) != 0)
			g_pp_pfail = __LINE__;
	}
	return 0;
}

int v32_sem_pingpong(void)
{
	pthread_t peer;
	int i, v = -1;
	int before = g_fail;

	g_pp_value = 0;
	g_pp_pfail = 0;
	ck(sem_init(&g_pp_main, 0, 1) == 0, __LINE__);
	ck(sem_init(&g_pp_peer, 0, 0) == 0, __LINE__);
	ck(sem_init(&g_pp_peer, 0, 0) == EBUSY, __LINE__);      /* the second init must be refused */
	ck(pthread_create(&peer, 0, pp_peer, 0) == 0, __LINE__);
	for (i = 0; i < PP_ROUNDS; i++) {
		ck(sem_wait(&g_pp_main) == 0, __LINE__);
		if (g_pp_value != i * 2) {
			ck(0, __LINE__);
			break;
		}
		g_pp_value++;
		ck(sem_post(&g_pp_peer) == 0, __LINE__);
	}
	ck(pthread_join(peer, 0) == 0, __LINE__);
	ck(g_pp_pfail == 0, __LINE__);
	ck(g_pp_value == PP_ROUNDS * 2, __LINE__);
	ck(sem_getvalue(&g_pp_main, &v) == 0 && v == 1, __LINE__);
	ck(sem_getvalue(&g_pp_peer, &v) == 0 && v == 0, __LINE__);
	ck(sem_trywait(&g_pp_peer) == EAGAIN, __LINE__);
	ck(sem_destroy(&g_pp_peer) == 0, __LINE__);
	ck(sem_destroy(&g_pp_main) == 0, __LINE__);
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 4. pthread_once from 8 threads (zero-initialised control), plus the recursive-call detection
 * -------------------------------------------------------------------------------------------------------------- */
static u32 g_once_zero;                 /* all zero == PTHREAD_ONCE_INIT */
static int g_once_runs;
static u32 g_once_mutex;
static int g_once_threads;

static void once_init(void)
{
	g_once_runs++;
}

static void *once_worker(void *arg)
{
	(void)arg;
	(void)pthread_once(&g_once_zero, once_init);
	(void)pthread_mutex_lock(&g_once_mutex);
	g_once_threads++;
	(void)pthread_mutex_unlock(&g_once_mutex);
	return 0;
}

int v32_once(void)
{
	pthread_t t[8];
	int i;
	int before = g_fail;

	ck(pthread_create(&t[0], 0, once_worker, 0) == 0, __LINE__);   /* first one alone */
	ck(pthread_join(t[0], 0) == 0, __LINE__);
	for (i = 1; i < 8; i++)
		ck(pthread_create(&t[i], 0, once_worker, 0) == 0, __LINE__);
	for (i = 1; i < 8; i++)
		ck(pthread_join(t[i], 0) == 0, __LINE__);
	g_dbg[0] = g_once_threads;
	g_dbg[1] = g_once_runs;
	ck(g_once_runs == 1, __LINE__);
	ck(g_once_threads == 8, __LINE__);
	(void)pthread_once(&g_once_zero, once_init);                   /* after the fact: must not run again */
	ck(g_once_runs == 1, __LINE__);
	ck(pthread_mutex_destroy(&g_once_mutex) == 0, __LINE__);
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 5. rwlock: 4 readers check two words for equality while 2 writers update both under the write lock
 * -------------------------------------------------------------------------------------------------------------- */
static u32 g_rw;
static int rw_a, rw_b;
static int rw_bad[4];
static int rw_write_bad[2];

static void *rw_reader(void *arg)
{
	int idx = (int)(uptr)arg;
	int i;

	for (i = 0; i < 20000; i++) {
		if (pthread_rwlock_rdlock(&g_rw) != 0) {
			rw_bad[idx] += 1000000;
			return 0;
		}
		if (rw_a != rw_b)
			rw_bad[idx]++;
		(void)pthread_rwlock_unlock(&g_rw);
	}
	return 0;
}

static void *rw_writer(void *arg)
{
	int idx = (int)(uptr)arg;
	int i;

	for (i = 0; i < 20000; i++) {
		if (pthread_rwlock_wrlock(&g_rw) != 0) {
			rw_write_bad[idx] = 1000000;
			return 0;
		}
		rw_a++;
		rw_b++;
		(void)pthread_rwlock_unlock(&g_rw);
	}
	return 0;
}

int v32_rwlock(void)
{
	pthread_t rd[4], wr[2];
	int i;
	int before = g_fail;

	rw_a = rw_b = 0;
	rw_write_bad[0] = rw_write_bad[1] = 0;
	for (i = 0; i < 4; i++)
		rw_bad[i] = 0;
	ck(pthread_rwlock_init(&g_rw, 0) == 0, __LINE__);
	for (i = 0; i < 4; i++)
		ck(pthread_create(&rd[i], 0, rw_reader, (void *)(uptr)i) == 0, __LINE__);
	for (i = 0; i < 2; i++)
		ck(pthread_create(&wr[i], 0, rw_writer, (void *)(uptr)i) == 0, __LINE__);
	for (i = 0; i < 4; i++)
		ck(pthread_join(rd[i], 0) == 0, __LINE__);
	for (i = 0; i < 2; i++)
		ck(pthread_join(wr[i], 0) == 0, __LINE__);
	ck(rw_bad[0] == 0 && rw_bad[1] == 0 && rw_bad[2] == 0 && rw_bad[3] == 0, __LINE__);
	ck(rw_write_bad[0] == 0 && rw_write_bad[1] == 0, __LINE__);
	ck(rw_a == 40000 && rw_b == 40000, __LINE__);
	ck(pthread_rwlock_destroy(&g_rw) == 0, __LINE__);
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 6. thread lifecycle: self/equal, detach, exit value, join errors, gettid, setname/prctl, kill
 * -------------------------------------------------------------------------------------------------------------- */
static u32 g_life_sem;                  /* "the detached thread is running" */
static u32 g_life_rel1;                 /* the main thread lets the detached thread go */
static u32 g_life_rel2;
static int g_life_detached_ran;

static void *life_exit_worker(void *arg)
{
	(void)arg;
	pthread_exit((void *)0x1234);
	return (void *)0x99;            /* never reached */
}

static void *life_detached_worker(void *arg)
{
	u32 *release = (u32 *)arg;

	g_life_detached_ran = 1;
	(void)sem_post(&g_life_sem);
	(void)sem_wait(release);        /* stay alive until the main thread has joined it */
	return (void *)0x77;
}

int v32_lifecycle(void)
{
	pthread_t self = pthread_self();
	pthread_t t;
	void *rv = 0;
	u32 attr = 0;
	int ds = -1, v = -1;
	char name[16];
	int before = g_fail;

	ck(self != 0, __LINE__);
	ck(pthread_equal(self, pthread_self()) != 0, __LINE__);
	ck(pthread_equal(self, self + 1000) == 0, __LINE__);
	ck(pthread_gettid_np(self) == gettid(), __LINE__);
	ck(gettid() > 0, __LINE__);
	ck(pthread_kill(self, 0) == 0, __LINE__);
	ck(pthread_kill(self, 10) == 0, __LINE__);              /* recorded, never delivered */
	ck(pthread_kill(0, 0) == ESRCH, __LINE__);
	ck(pthread_kill(9999, 0) == ESRCH, __LINE__);

	/* name round trip through the shim and through prctl(PR_GET_NAME) */
	ck(pthread_setname_np(self, "v32-main") == 0, __LINE__);
	name[0] = 0;
	ck(prctl(16, name) == 0, __LINE__);                     /* PR_GET_NAME */
	ck(strcmp(name, "v32-main") == 0, __LINE__);

	/* attr: defaults, detach state and sched parameter/policy storage */
	ck(pthread_attr_init(&attr) == 0, __LINE__);
	ck(pthread_attr_getdetachstate(&attr, &ds) == 0 && ds == 0, __LINE__);
	ck(pthread_attr_setdetachstate(&attr, 1) == 0, __LINE__);
	ck(pthread_attr_getdetachstate(&attr, &ds) == 0 && ds == 1, __LINE__);
	{
		int prio[2];

		prio[0] = 7;
		prio[1] = 0;
		ck(pthread_attr_setschedparam(&attr, prio) == 0, __LINE__);
		ck(pthread_attr_getschedparam(&attr, prio) == 0 && prio[0] == 7, __LINE__);
		ck(pthread_attr_setschedpolicy(&attr, 2) == 0, __LINE__);
		ck(pthread_attr_getschedpolicy(&attr, &v) == 0 && v == 2, __LINE__);
		ck(pthread_attr_setschedpolicy(&attr, 9) == EINVAL, __LINE__);
	}
	ck(pthread_attr_destroy(&attr) == 0, __LINE__);

	/* detached through the attr; the worker waits on g_life_release so the join below always sees it alive */
	ck(sem_init(&g_life_sem, 0, 0) == 0, __LINE__);
	ck(sem_init(&g_life_rel1, 0, 0) == 0, __LINE__);
	ck(pthread_attr_init(&attr) == 0, __LINE__);
	ck(pthread_attr_setdetachstate(&attr, 1) == 0, __LINE__);
	ds = -1;
	ck(pthread_attr_getdetachstate(&attr, &ds) == 0 && ds == 1, __LINE__);
	g_dbg[2] = ds;
	ck(pthread_create(&t, &attr, life_detached_worker, (void *)&g_life_rel1) == 0, __LINE__);
	ck(pthread_attr_destroy(&attr) == 0, __LINE__);
	ck(sem_wait(&g_life_sem) == 0, __LINE__);
	ck(g_life_detached_ran == 1, __LINE__);
	g_dbg[3] = (int)pthread_join(t, 0);                     /* joining a detached thread is EINVAL */
	ck(g_dbg[3] == EINVAL, __LINE__);
	ck(sem_post(&g_life_rel1) == 0, __LINE__);
	ck(sem_destroy(&g_life_sem) == 0, __LINE__);
	/* g_life_rel1 stays alive: the worker may reach its sem_wait after this point, and a destroyed
	 * semaphore would be re-created empty and block it forever. */

	/* pthread_detach() on a live joinable thread */
	ck(sem_init(&g_life_sem, 0, 0) == 0, __LINE__);
	ck(sem_init(&g_life_rel2, 0, 0) == 0, __LINE__);
	ck(pthread_create(&t, 0, life_detached_worker, (void *)&g_life_rel2) == 0, __LINE__);
	ck(pthread_detach(t) == 0, __LINE__);
	ck(pthread_detach(t) == EINVAL, __LINE__);              /* already detached */
	ck(sem_wait(&g_life_sem) == 0, __LINE__);
	ck(pthread_join(t, 0) == EINVAL, __LINE__);
	ck(sem_post(&g_life_rel2) == 0, __LINE__);
	ck(sem_destroy(&g_life_sem) == 0, __LINE__);

	/* pthread_exit value visible through join */
	ck(pthread_create(&t, 0, life_exit_worker, 0) == 0, __LINE__);
	ck(pthread_join(t, &rv) == 0, __LINE__);
	ck(rv == (void *)0x1234, __LINE__);
	ck(pthread_join(t, 0) == ESRCH, __LINE__);              /* the slot is gone after the join */
	ck(pthread_self() == self, __LINE__);
	ck(pthread_equal(pthread_self(), self) != 0, __LINE__);
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 7. scheduling, time, signals, and the error paths of the synchronisation objects
 * -------------------------------------------------------------------------------------------------------------- */
static int g_sig_seen;
static u32 g_sig_mutex;

static void sig_handler(int sig)
{
	(void)sig;
	(void)pthread_mutex_lock(&g_sig_mutex);
	g_sig_seen++;
	(void)pthread_mutex_unlock(&g_sig_mutex);
}

static u32 g_misc_mutex;
static int g_misc_held;
static u32 g_misc_sem;

static void *hold_worker(void *arg)
{
	(void)arg;
	(void)pthread_mutex_lock(&g_misc_mutex);
	g_misc_held = 1;
	(void)sem_post(&g_misc_sem);
	(void)usleep(30000);
	g_misc_held = 0;
	(void)pthread_mutex_unlock(&g_misc_mutex);
	return 0;
}

int v32_misc(void)
{
	unsigned char mask[16];
	pthread_t t;
	u32 m = 0, c = 0, s = 0;
	struct gts ts;
	int i;
	int before = g_fail;

	/* scheduling */
	ck(sched_yield() == 0, __LINE__);
	for (i = 0; i < 16; i++)
		mask[i] = 0;
	ck(sched_getaffinity(0, 16, mask) == 0, __LINE__);
	ck(mask[0] == 0xff, __LINE__);                          /* 8 CPUs, bits 0-7 */
	for (i = 1; i < 16; i++)
		ck(mask[i] == 0, __LINE__);
	ck(sched_setaffinity(0, 16, mask) == 0, __LINE__);
	ck(sched_getaffinity(0, 0, mask) == EINVAL, __LINE__);
	ck(usleep(1000) == 0, __LINE__);

	/* signals: recorded, never delivered */
	ck((uptr)signal(10, (void *)sig_handler) == 0, __LINE__);        /* previous handler was SIG_DFL */
	ck((uptr)signal(10, (void *)sig_handler) == (uptr)sig_handler, __LINE__);
	ck(sigemptyset(mask) == 0, __LINE__);
	ck(mask[0] == 0 && mask[1] == 0 && mask[2] == 0 && mask[3] == 0, __LINE__);
	{
		u32 act[8];
		u32 old[8];

		for (i = 0; i < 8; i++)
			act[i] = old[i] = 0xdeadbeef;
		ck(sigaction(12, act, old) == 0, __LINE__);
		ck(old[0] == 0, __LINE__);                       /* sa_handler = SIG_DFL on return */
	}
	ck(__register_atfork(0, 0, 0, 0) == 0, __LINE__);
	ck(g_sig_seen == 0, __LINE__);                          /* nothing was delivered */

	/* mutex error paths, PTHREAD_MUTEX_NORMAL first: no owner tracking, trylock on our own lock is EBUSY */
	ck(pthread_mutex_init(&m, 0) == 0, __LINE__);
	ck(pthread_mutex_trylock(&m) == 0, __LINE__);
	ck(pthread_mutex_trylock(&m) == EBUSY, __LINE__);
	ck(pthread_mutex_unlock(&m) == 0, __LINE__);
	ts.sec = 1;                                             /* deadline in 1970: already passed */
	ts.nsec = 2000000000u;                                  /* out of range for tv_nsec */
	ck(pthread_mutex_timedlock(&m, &ts) == EINVAL, __LINE__);
	ts.nsec = 0;
	ck(pthread_mutex_timedlock(&m, &ts) == 0, __LINE__);     /* uncontended: locks despite the deadline */
	ck(pthread_mutex_unlock(&m) == 0, __LINE__);
	ck(pthread_mutex_destroy(&m) == 0, __LINE__);
	ck(pthread_mutex_destroy(&m) == 0, __LINE__);            /* destroying a never-used object is a no-op */

	/* PTHREAD_MUTEX_ERRORCHECK: unlock-by-non-owner EPERM, relock-by-owner EDEADLK */
	{
		u32 ma = 0;
		u32 em = 0;

		ck(pthread_mutexattr_init(&ma) == 0, __LINE__);
		ck(pthread_mutexattr_settype(&ma, 2) == 0, __LINE__);
		ck(pthread_mutex_init(&em, &ma) == 0, __LINE__);
		ck(pthread_mutexattr_destroy(&ma) == 0, __LINE__);
		ck(pthread_mutex_lock(&em) == 0, __LINE__);
		ck(pthread_mutex_lock(&em) == EDEADLK, __LINE__);
		ck(pthread_mutex_unlock(&em) == 0, __LINE__);
		ck(pthread_mutex_destroy(&em) == 0, __LINE__);
		/* After the destroy the address has no object; unlocking it is the documented EPERM path
		 * (t_direct checks the same path; TSan would warn about a real host unlock). */
		ck(pthread_mutex_unlock(&em) == EPERM, __LINE__);
	}

	/* a mutex held by another thread: trylock EBUSY and timedlock ETIMEDOUT */
	ck(pthread_mutex_init(&g_misc_mutex, 0) == 0, __LINE__);
	ck(sem_init(&g_misc_sem, 0, 0) == 0, __LINE__);
	ck(pthread_create(&t, 0, hold_worker, 0) == 0, __LINE__);
	ck(sem_wait(&g_misc_sem) == 0, __LINE__);
	ck(pthread_mutex_trylock(&g_misc_mutex) == EBUSY, __LINE__);
	ts.sec = 1;
	ts.nsec = 0;
	{
		int rc = pthread_mutex_timedlock(&g_misc_mutex, &ts);        /* past deadline */

		g_dbg[4] = rc;
		ck(rc == ETIMEDOUT, __LINE__);
	}
	ts.sec = 0x7fffffff;                                    /* 2038-01-19: still ahead */
	ts.nsec = 0;
	ck(pthread_mutex_timedlock(&g_misc_mutex, &ts) == 0, __LINE__);  /* waits out the holder */
	ck(g_misc_held == 0, __LINE__);                          /* the holder released it */
	ck(pthread_mutex_unlock(&g_misc_mutex) == 0, __LINE__);
	ck(pthread_join(t, 0) == 0, __LINE__);
	ck(pthread_mutex_destroy(&g_misc_mutex) == 0, __LINE__);
	ck(sem_destroy(&g_misc_sem) == 0, __LINE__);

	/* cond timedwait on a past deadline returns ETIMEDOUT and re-acquires the mutex */
	ck(pthread_cond_init(&c, 0) == 0, __LINE__);
	ck(pthread_mutex_init(&m, 0) == 0, __LINE__);
	ck(pthread_mutex_lock(&m) == 0, __LINE__);
	ts.sec = 1;
	ts.nsec = 0;
	ck(pthread_cond_timedwait(&c, &m, &ts) == ETIMEDOUT, __LINE__);
	ck(pthread_mutex_unlock(&m) == 0, __LINE__);
	ck(pthread_cond_signal(&c) == 0, __LINE__);              /* no waiter: success */
	ck(pthread_cond_broadcast(&c) == 0, __LINE__);
	ck(pthread_cond_destroy(&c) == 0, __LINE__);
	ck(pthread_mutex_destroy(&m) == 0, __LINE__);

	/* condattr CLOCK_MONOTONIC is converted to the host realtime clock */
	{
		u32 ca = 0, mc = 0, mm = 0;
		struct gts mts;

		ck(pthread_condattr_init(&ca) == 0, __LINE__);
		ck(pthread_condattr_setclock(&ca, 1) == 0, __LINE__);        /* CLOCK_MONOTONIC */
		ck(pthread_cond_init(&mc, &ca) == 0, __LINE__);
		ck(pthread_condattr_destroy(&ca) == 0, __LINE__);
		ck(pthread_mutex_init(&mm, 0) == 0, __LINE__);
		ck(pthread_mutex_lock(&mm) == 0, __LINE__);
		mts.sec = 1;                                            /* monotonic second 1: long past */
		mts.nsec = 0;
		ck(pthread_cond_timedwait(&mc, &mm, &mts) == ETIMEDOUT, __LINE__);
		ck(pthread_mutex_unlock(&mm) == 0, __LINE__);
		ck(pthread_cond_destroy(&mc) == 0, __LINE__);
		ck(pthread_mutex_destroy(&mm) == 0, __LINE__);
	}

	/* sem_timedwait with a past deadline on an empty semaphore */
	ck(sem_init(&s, 0, 0) == 0, __LINE__);
	ts.sec = 1;
	ts.nsec = 0;
	ck(sem_timedwait(&s, &ts) == ETIMEDOUT, __LINE__);
	ck(sem_destroy(&s) == 0, __LINE__);

	/* sem error paths and re-initialisation after destroy */
	ck(sem_init(&s, 0, 2) == 0, __LINE__);
	ck(sem_trywait(&s) == 0, __LINE__);
	ck(sem_trywait(&s) == 0, __LINE__);
	ck(sem_trywait(&s) == EAGAIN, __LINE__);
	ck(sem_post(&s) == 0, __LINE__);
	ck(sem_destroy(&s) == 0, __LINE__);
	ck(sem_init(&s, 0, 1) == 0, __LINE__);
	ck(sem_wait(&s) == 0, __LINE__);
	ck(sem_post(&s) == 0, __LINE__);
	ck(sem_destroy(&s) == 0, __LINE__);

	/* mutexattr type storage */
	{
		u32 ma = 0;
		int type = -1;

		ck(pthread_mutexattr_init(&ma) == 0, __LINE__);
		ck(pthread_mutexattr_gettype(&ma, &type) == 0 && type == 0, __LINE__);
		ck(pthread_mutexattr_settype(&ma, 1) == 0, __LINE__);
		ck(pthread_mutexattr_gettype(&ma, &type) == 0 && type == 1, __LINE__);
		ck(pthread_mutexattr_settype(&ma, 5) == EINVAL, __LINE__);
		ck(pthread_mutexattr_destroy(&ma) == 0, __LINE__);
	}
	/* condattr clock storage */
	{
		u32 ca = 0;
		int clock = -1;

		ck(pthread_condattr_init(&ca) == 0, __LINE__);
		ck(pthread_condattr_setclock(&ca, 1) == 0, __LINE__);
		ck(pthread_condattr_setclock(&ca, 7) == EINVAL, __LINE__);
		ck(pthread_condattr_destroy(&ca) == 0, __LINE__);
		(void)clock;
	}

	/* rwlock try and timed paths on an unheld lock */
	{
		u32 rw = 0;

		ck(pthread_rwlock_init(&rw, 0) == 0, __LINE__);
		ck(pthread_rwlock_tryrdlock(&rw) == 0, __LINE__);
		ck(pthread_rwlock_trywrlock(&rw) != 0, __LINE__);     /* a read lock is held */
		ck(pthread_rwlock_unlock(&rw) == 0, __LINE__);
		ck(pthread_rwlock_trywrlock(&rw) == 0, __LINE__);
		ck(pthread_rwlock_unlock(&rw) == 0, __LINE__);
		ts.sec = 0x7fffffff;                                    /* 2038 */
		ts.nsec = 0;
		ck(pthread_rwlock_timedrdlock(&rw, &ts) == 0, __LINE__);   /* free: locks at once */
		ts.sec = 1;                                             /* 1970: already passed */
		ts.nsec = 0;
		{
			int rc = pthread_rwlock_timedwrlock(&rw, &ts);      /* reader held, past deadline */

			/* glibc may also report EDEADLK (a write lock while this thread reads) instead of timing out. */
			g_dbg[5] = rc;
			ck(rc == ETIMEDOUT || rc == EDEADLK, __LINE__);
		}
		ck(pthread_rwlock_unlock(&rw) == 0, __LINE__);
		ck(pthread_rwlock_destroy(&rw) == 0, __LINE__);
	}
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 8. statically initialised objects, including a guest address reused for two object kinds
 * -------------------------------------------------------------------------------------------------------------- */
static u32 g_zero_rw;
static u32 g_zero_once;
static int g_zero_once_runs;

static void zero_once_fn(void)
{
	g_zero_once_runs++;
}

int v32_zeroinit(void)
{
	union {
		u32 m;
		u32 s;
	} o = { 0 };
	int before = g_fail;
	int v = -1;

	/* no pthread_mutex_init: all-zero object, used directly (PTHREAD_MUTEX_INITIALIZER) */
	ck(pthread_mutex_lock(&o.m) == 0, __LINE__);
	ck(pthread_mutex_unlock(&o.m) == 0, __LINE__);
	/* same guest address, different kind, without destroying the mutex: must not be refused */
	ck(sem_init(&o.s, 0, 1) == 0, __LINE__);
	ck(sem_wait(&o.s) == 0, __LINE__);
	ck(sem_getvalue(&o.s, &v) == 0 && v == 0, __LINE__);
	ck(sem_post(&o.s) == 0, __LINE__);
	ck(sem_wait(&o.s) == 0, __LINE__);
	ck(sem_destroy(&o.s) == 0, __LINE__);

	/* zero-initialised rwlock and once */
	ck(pthread_rwlock_rdlock(&g_zero_rw) == 0, __LINE__);
	ck(pthread_rwlock_unlock(&g_zero_rw) == 0, __LINE__);
	ck(pthread_rwlock_wrlock(&g_zero_rw) == 0, __LINE__);
	ck(pthread_rwlock_unlock(&g_zero_rw) == 0, __LINE__);
	ck(pthread_rwlock_destroy(&g_zero_rw) == 0, __LINE__);
	ck(pthread_once(&g_zero_once, zero_once_fn) == 0, __LINE__);
	ck(pthread_once(&g_zero_once, zero_once_fn) == 0, __LINE__);
	ck(g_zero_once_runs == 1, __LINE__);
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 9. a guest thread that stops abnormally: first-failure slot + stop_request on the other cpus
 * -------------------------------------------------------------------------------------------------------------- */
u32 g_fail_sem;
u32 g_fail_ready;
u32 g_victim[3];
static int g_victim_ran;

static void *fail_victim(void *arg)
{
	(void)arg;
	(void)sem_post(&g_fail_ready);
	(void)sem_wait(&g_fail_sem);
	g_victim_ran = 1;               /* must never be reached: the cpu is asked to stop */
	return 0;
}

int v32_fail_single(void)
{
	pthread_t t;
	void *rv = (void *)0x1;
	int before = g_fail;

	/* 0xfffffff0 is inside the 4 GiB reservation but unmapped: the interpreter stops with STOP_FAULT */
	ck(pthread_create(&t, 0, (void *(*)(void *))0xfffffff0u, 0) == 0, __LINE__);
	ck(pthread_join(t, &rv) == 0, __LINE__);
	ck(rv == 0, __LINE__);
	return g_fail - before;
}

int v32_fail_multi(void)
{
	int i;
	int before = g_fail;

	ck(sem_init(&g_fail_sem, 0, 0) == 0, __LINE__);
	ck(sem_init(&g_fail_ready, 0, 0) == 0, __LINE__);
	for (i = 0; i < 3; i++)
		ck(pthread_create(&g_victim[i], 0, fail_victim, 0) == 0, __LINE__);
	for (i = 0; i < 3; i++)
		ck(sem_wait(&g_fail_ready) == 0, __LINE__);   /* all three are inside sem_wait now */
	ck(g_victim_ran == 0, __LINE__);
	{
		pthread_t bad;

		ck(pthread_create(&bad, 0, (void *(*)(void *))0xfffffff0u, 0) == 0, __LINE__);
		/* The failing thread asks this cpu to stop, so the join returns EINTR and the very next guest
		 * instruction is never executed: the driver checks that this function's return value is 0 and
		 * that the guest never reached the marker below. */
		ck(pthread_join(bad, 0) == EINTR, __LINE__);
	}
	ck(g_victim_ran == 0, __LINE__);
	return g_fail - before;
}

/* A pthread_once init routine that stops abnormally: the failure is recorded, the control is marked done so
 * waiters cannot hang, and the caller is asked to stop at its next instruction boundary. */
static u32 g_bad_once;
int v32_once_fail(void)
{
	(void)pthread_once(&g_bad_once, (void (*)(void))0xfffffff0u);
	return 0;                       /* never reached: the cpu is asked to stop */
}

/* The initial thread calling pthread_exit(): V32 stops this cpu and asks every other cpu to stop; the host
 * process stays alive (the driver checks c->stop == N2A32_STOP_REQUEST). */
int g_main_exit_marker;
int v32_main_exit(void)
{
	(void)pthread_exit((void *)0xabc);
	g_main_exit_marker = 1;         /* must never be reached */
	return 1;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 10. thread churn: 100 sequential create/join pairs (record and stack-pool reuse) and 40 live at once
 * -------------------------------------------------------------------------------------------------------------- */
static void *churn_worker(void *arg)
{
	return (void *)(uptr)((uptr)arg + 1);
}

int v32_churn(void)
{
	pthread_t t;
	int i;
	int before = g_fail;

	for (i = 0; i < 100; i++) {
		void *rv = 0;

		ck(pthread_create(&t, 0, churn_worker, (void *)(uptr)i) == 0, __LINE__);
		ck(pthread_join(t, &rv) == 0, __LINE__);
		ck(rv == (void *)(uptr)(i + 1), __LINE__);
	}
	{
		pthread_t many[40];
		int j;

		for (j = 0; j < 40; j++)
			ck(pthread_create(&many[j], 0, churn_worker, (void *)(uptr)j) == 0, __LINE__);
		for (j = 0; j < 40; j++) {
			void *rv = 0;

			ck(pthread_join(many[j], &rv) == 0, __LINE__);
			ck(rv == (void *)(uptr)(j + 1), __LINE__);
		}
	}
	return g_fail - before;
}

/* ---------------------------------------------------------------------------------------------------------------
 * 11. pthread_once re-entered from its own init routine: EDEADLK instead of a deadlock
 * -------------------------------------------------------------------------------------------------------------- */
static u32 g_recurse_once;
static int g_recurse_rc = -1;
static int g_recurse_runs;

static void recurse_init(void)
{
	g_recurse_runs++;
	g_recurse_rc = pthread_once(&g_recurse_once, recurse_init);
}

int v32_once_recurse(void)
{
	int before = g_fail;

	ck(pthread_once(&g_recurse_once, recurse_init) == 0, __LINE__);
	ck(g_recurse_runs == 1, __LINE__);
	ck(g_recurse_rc == EDEADLK, __LINE__);
	return g_fail - before;
}
