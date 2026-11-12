/* SPDX-License-Identifier: MIT */
/*
 * t_direct.c -- V32 host tests that drive n2shim_thread[] directly with a fake cpu, without guest code.
 *
 * What only this binary can check: that the table covers every import D01 counted for the thread family (and
 * that no name appears twice), and that every shim answers documented errno values instead of crashing on NULL,
 * out-of-range ids and uninitialised objects.  The threaded behaviour (create/join/cond/sem/once/rwlock) is
 * covered by t_e2e with real ARM guest code, so this file does not start guest threads.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"
#include "n2shim_thread.h"

static int checks, fails;

static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL: %s\n", what);
	}
}

/* D01's census of the thread family: 50 imports of bin/vpud + the four vendor libraries. */
static const char *const d01_thread_imports[] = {
	"gettid", "prctl",
	"pthread_attr_destroy", "pthread_attr_getdetachstate", "pthread_attr_getschedparam",
	"pthread_attr_init", "pthread_attr_setdetachstate", "pthread_attr_setschedparam",
	"pthread_attr_setschedpolicy",
	"pthread_cond_broadcast", "pthread_cond_destroy", "pthread_cond_init", "pthread_cond_signal",
	"pthread_cond_wait", "pthread_condattr_destroy", "pthread_condattr_init",
	"pthread_create", "pthread_equal", "pthread_exit", "pthread_gettid_np", "pthread_join",
	"pthread_kill", "pthread_mutex_destroy", "pthread_mutex_init", "pthread_mutex_lock",
	"pthread_mutex_timedlock", "pthread_mutex_trylock", "pthread_mutex_unlock",
	"pthread_mutexattr_destroy", "pthread_mutexattr_init", "pthread_once",
	"pthread_rwlock_destroy", "pthread_rwlock_init", "pthread_rwlock_rdlock", "pthread_rwlock_unlock",
	"pthread_rwlock_wrlock", "pthread_self", "pthread_setname_np",
	"sched_getaffinity", "sched_setaffinity", "sched_yield",
	"sem_destroy", "sem_getvalue", "sem_init", "sem_post", "sem_wait",
	"sigaction", "sigemptyset", "signal", "usleep",
	NULL
};

static const struct n2shim_entry *find_shim(const char *name)
{
	const struct n2shim_entry *e;

	for (e = n2shim_thread; e->name != NULL; e++)
		if (strcmp(e->name, name) == 0)
			return e;
	return NULL;
}

static struct n2a32_mem *g_mem;
static struct n2a32_cpu g_cpu;

static uint32_t call0(const char *name)
{
	const struct n2shim_entry *e = find_shim(name);

	g_cpu.r[0] = g_cpu.r[1] = g_cpu.r[2] = g_cpu.r[3] = 0;
	if (e == NULL)
		return 0xdeadbeefu;
	e->fn(&g_cpu);
	return g_cpu.r[0];
}

static uint32_t call1(const char *name, uint32_t a0)
{
	const struct n2shim_entry *e = find_shim(name);

	g_cpu.r[0] = a0;
	g_cpu.r[1] = g_cpu.r[2] = g_cpu.r[3] = 0;
	if (e == NULL)
		return 0xdeadbeefu;
	e->fn(&g_cpu);
	return g_cpu.r[0];
}

static uint32_t call2(const char *name, uint32_t a0, uint32_t a1)
{
	const struct n2shim_entry *e = find_shim(name);

	g_cpu.r[0] = a0;
	g_cpu.r[1] = a1;
	g_cpu.r[2] = g_cpu.r[3] = 0;
	if (e == NULL)
		return 0xdeadbeefu;
	e->fn(&g_cpu);
	return g_cpu.r[0];
}

static uint32_t call3(const char *name, uint32_t a0, uint32_t a1, uint32_t a2)
{
	const struct n2shim_entry *e = find_shim(name);

	g_cpu.r[0] = a0;
	g_cpu.r[1] = a1;
	g_cpu.r[2] = a2;
	g_cpu.r[3] = 0;
	if (e == NULL)
		return 0xdeadbeefu;
	e->fn(&g_cpu);
	return g_cpu.r[0];
}

#define EPERM 1
#define ESRCH 3
#define EAGAIN 11
#define ENOMEM 12
#define EBUSY 16
#define EINVAL 22
#define EDEADLK 35
#define ETIMEDOUT 110

#define PAGE_A 0x00100000u
#define MUTEX_A (PAGE_A + 0x100)
#define COND_A  (PAGE_A + 0x200)
#define SEM_A   (PAGE_A + 0x300)
#define RW_A    (PAGE_A + 0x400)
#define ATTR_A  (PAGE_A + 0x500)
#define TS_A    (PAGE_A + 0x600)
#define PARAM_A (PAGE_A + 0x700)
#define MASK_A  (PAGE_A + 0x800)
#define NAME_A  (PAGE_A + 0x900)
#define ACT_A   (PAGE_A + 0xA00)
#define OLD_A   (PAGE_A + 0xB00)

int main(void)
{
	const struct n2shim_entry *e;
	unsigned i, n_d01 = 0, n_table = 0;
	uint32_t v, prev;

	g_mem = n2a32_mem_create();
	if (g_mem == NULL) {
		printf("N2TEST V32-DIRECT FAIL cannot create the guest address space\n");
		return 1;
	}
	if (n2a32_mem_map(g_mem, PAGE_A, 0x10000, 1) != 0) {
		printf("N2TEST V32-DIRECT FAIL cannot map the test page\n");
		return 1;
	}
	n2a32_cpu_init(&g_cpu, g_mem);

	/* ---- the table itself ---- */
	for (i = 0; d01_thread_imports[i] != NULL; i++) {
		n_d01++;
		ck(find_shim(d01_thread_imports[i]) != NULL, d01_thread_imports[i]);
	}
	ck(n_d01 == 50, "D01 counted 50 thread-family imports");
	for (e = n2shim_thread; e->name != NULL; e++) {
		const struct n2shim_entry *f;

		n_table++;
		ck(e->fn != NULL, "every table entry has a function");
		for (f = e + 1; f->name != NULL; f++)
			ck(strcmp(e->name, f->name) != 0, "no duplicate name in n2shim_thread[]");
	}
	ck(n_table >= 50, "the table has at least the 50 D01 imports");
	printf("  table: %u entries, D01 covers %u\n", n_table, n_d01);
	/* the extras that the vendor code and the tests rely on */
	ck(find_shim("pthread_detach") != NULL, "pthread_detach");
	ck(find_shim("pthread_cond_timedwait") != NULL, "pthread_cond_timedwait");
	ck(find_shim("pthread_rwlock_tryrdlock") != NULL, "pthread_rwlock_tryrdlock");
	ck(find_shim("sem_trywait") != NULL, "sem_trywait");
	ck(find_shim("__register_atfork") != NULL, "__register_atfork (V32 deliverable)");

	/* ---- the calling cpu is registered as the initial guest thread ---- */
	ck(n2shim_thread_self_id() == 0, "no shim call yet: no thread record for this host thread");
	v = call0("pthread_self");
	ck(v != 0, "pthread_self returns a non-zero id");
	ck(n2shim_thread_self_id() == v, "n2shim_thread_self_id agrees with pthread_self");
	ck(n2shim_thread_live() == 1, "one live guest thread record");
	ck(call2("pthread_equal", v, v) == 1, "pthread_equal(x, x)");
	ck(call2("pthread_equal", v, v + 1) == 0, "pthread_equal(x, y)");
	ck(call1("gettid", 0) > 0, "gettid > 0");
	ck(call1("pthread_gettid_np", v) > 0, "pthread_gettid_np(self) > 0");
	ck(call1("pthread_gettid_np", v) == call0("gettid"), "pthread_gettid_np(self) == gettid()");

	/* ---- NULL / out-of-range arguments: documented errno, never a crash ---- */
	ck(call1("pthread_join", 0) == ESRCH, "pthread_join(0) == ESRCH");
	ck(call1("pthread_join", 9999) == ESRCH, "pthread_join(9999) == ESRCH");
	ck(call2("pthread_join", v, 0) == EDEADLK, "a guest thread joining itself == EDEADLK");
	ck(call1("pthread_detach", 0) == ESRCH, "pthread_detach(0) == ESRCH");
	ck(call1("pthread_kill", 0) == ESRCH, "pthread_kill(0) == ESRCH");
	ck(call1("pthread_kill", 9999) == ESRCH, "pthread_kill(9999) == ESRCH");
	ck(call2("pthread_kill", v, 10) == 0, "pthread_kill(self, SIGUSR1) is accepted");
	ck(call1("pthread_gettid_np", 9999) == 0, "pthread_gettid_np(invalid) == 0");
	ck(call2("pthread_setname_np", v, 0) == EINVAL, "pthread_setname_np(self, NULL) == EINVAL");
	ck(call2("pthread_setname_np", 9999, NAME_A) == ESRCH, "pthread_setname_np(invalid) == ESRCH");
	ck(call2("pthread_setname_np", v, NAME_A) == 0, "pthread_setname_np(self, name) == 0");
	n2a32_st32(&g_cpu, NAME_A, 0x00633276u);        /* "v2c\0" */
	ck(call3("pthread_create", 0, 0, 1) == EINVAL, "pthread_create(NULL thread) == EINVAL");
	ck(call3("pthread_create", PAGE_A, 0, 0) == EINVAL, "pthread_create(fn=0) == EINVAL");
	ck(call1("pthread_mutex_lock", 0) == EINVAL, "pthread_mutex_lock(NULL) == EINVAL");
	ck(call1("pthread_mutex_unlock", 0) == EINVAL, "pthread_mutex_unlock(NULL) == EINVAL");
	ck(call1("pthread_mutex_unlock", MUTEX_A) == EPERM, "unlock of a never-locked object == EPERM");
	ck(call1("pthread_mutex_trylock", 0) == EINVAL, "pthread_mutex_trylock(NULL) == EINVAL");
	ck(call2("pthread_mutex_timedlock", MUTEX_A, 0) == EINVAL, "timedlock without a timespec == EINVAL");
	ck(call2("pthread_cond_wait", 0, 0) == EINVAL, "pthread_cond_wait(NULL, NULL) == EINVAL");
	ck(call1("sem_init", 0) == EINVAL, "sem_init(NULL) == EINVAL");
	ck(call2("sem_getvalue", 0, 0) == EINVAL, "sem_getvalue(NULL) == EINVAL");
	ck(call1("pthread_rwlock_rdlock", 0) == EINVAL, "pthread_rwlock_rdlock(NULL) == EINVAL");
	ck(call1("pthread_once", 0) == EINVAL, "pthread_once(NULL) == EINVAL");
	ck(call2("pthread_attr_setdetachstate", ATTR_A, 5) == EINVAL, "attr_setdetachstate(5) == EINVAL");
	ck(call3("sched_getaffinity", 0, 0, MASK_A) == EINVAL, "sched_getaffinity(size=0) == EINVAL");
	ck(call2("signal", 0, 0) == 0xffffffffu, "signal(0, ...) == SIG_ERR");
	ck(call1("sigemptyset", 0) == EINVAL, "sigemptyset(NULL) == EINVAL");
	ck(call3("__register_atfork", 0, 0, 0) == 0, "__register_atfork() == 0");

	/* ---- attributes stored in guest memory ---- */
	ck(call1("pthread_attr_init", ATTR_A) == 0, "pthread_attr_init");
	n2a32_st32(&g_cpu, NAME_A, 0);
	ck(call2("pthread_attr_getdetachstate", ATTR_A, NAME_A) == 0, "attr_getdetachstate");
	ck(n2a32_ld32(&g_cpu, NAME_A) == 0, "default detach state is joinable");
	ck(call2("pthread_attr_setdetachstate", ATTR_A, 1) == 0, "attr_setdetachstate(detached)");
	ck(call2("pthread_attr_getdetachstate", ATTR_A, NAME_A) == 0, "attr_getdetachstate again");
	ck(n2a32_ld32(&g_cpu, NAME_A) == 1, "detach state round trips through guest memory");
	n2a32_st32(&g_cpu, PARAM_A, 42);
	ck(call2("pthread_attr_setschedparam", ATTR_A, PARAM_A) == 0, "attr_setschedparam");
	n2a32_st32(&g_cpu, PARAM_A, 0);
	ck(call2("pthread_attr_getschedparam", ATTR_A, PARAM_A) == 0, "attr_getschedparam");
	ck(n2a32_ld32(&g_cpu, PARAM_A) == 42, "sched_priority round trips");
	ck(call2("pthread_attr_setschedpolicy", ATTR_A, 1) == 0, "attr_setschedpolicy(SCHED_FIFO)");
	ck(call2("pthread_attr_getschedpolicy", ATTR_A, NAME_A) == 0, "attr_getschedpolicy");
	ck(n2a32_ld32(&g_cpu, NAME_A) == 1, "sched policy round trips");
	ck(call1("pthread_attr_destroy", ATTR_A) == 0, "pthread_attr_destroy");

	/* ---- sched: 8 CPUs reported, setaffinity accepted ---- */
	{
		unsigned char *mask = (unsigned char *)(g_mem->base + MASK_A);

		for (i = 0; i < 16; i++)
			n2a32_st8(&g_cpu, MASK_A + i, 0xaa);
		ck(call3("sched_getaffinity", 0, 16, MASK_A) == 0, "sched_getaffinity");
		ck(mask[0] == 0xff, "bits 0-7 set: 8 CPUs");
		for (i = 1; i < 16; i++)
			if (mask[i] != 0) {
				ck(0, "cpuset beyond byte 0 is zero");
				break;
			}
		ck(i == 16, "cpuset beyond byte 0 is zero");
		ck(call3("sched_setaffinity", 0, 16, MASK_A) == 0, "sched_setaffinity accepted");
	}
	ck(call0("sched_yield") == 0, "sched_yield");
	ck(call1("usleep", 1000) == 0, "usleep(1000)");
	ck(call1("prctl", 0) == 0, "prctl(unknown option) accepted");

	/* ---- signals are recorded, never delivered ---- */
	n2a32_st32(&g_cpu, OLD_A, 0xdeadbeefu);
	ck(call3("sigaction", 12, ACT_A, OLD_A) == 0, "sigaction accepted");
	ck(n2a32_ld32(&g_cpu, OLD_A) == 0, "sigaction reports SIG_DFL as the old handler");
	n2a32_st32(&g_cpu, MASK_A, 0xffffffffu);
	ck(call1("sigemptyset", MASK_A) == 0, "sigemptyset");
	ck(n2a32_ld32(&g_cpu, MASK_A) == 0, "sigemptyset zeroes the set");
	ck(call2("signal", 10, 0x1234) == 0, "signal() returns the previous handler (SIG_DFL)");
	ck(call2("signal", 10, 0x5678) == 0x1234, "signal() returns the handler installed before");
	ck(call2("signal", 65, 0x5678) == 0xffffffffu, "signal(65) == SIG_ERR");

	/* ---- object side table: create, use, retire ---- */
	v = n2shim_thread_objects();
	ck(call1("pthread_mutex_lock", MUTEX_A) == 0, "mutex_lock creates the object on first use");
	ck(call1("pthread_mutex_trylock", MUTEX_A) == EBUSY, "normal mutex: trylock from the owner is EBUSY");
	ck(call1("pthread_mutex_unlock", MUTEX_A) == 0, "mutex_unlock");
	ck(n2shim_thread_objects() == v + 1, "one object created");
	ck(call1("pthread_mutex_destroy", MUTEX_A) == 0, "mutex_destroy");
	ck(n2shim_thread_objects() == v, "destroy removes the object");
	ck(call1("pthread_mutex_destroy", MUTEX_A) == 0, "destroying it twice is a no-op");

	/* a second kind at the same guest address retires the first entry */
	ck(call1("pthread_mutex_lock", MUTEX_A) == 0, "lock again (re-created)");
	ck(call1("pthread_mutex_unlock", MUTEX_A) == 0, "unlock");
	ck(call3("sem_init", MUTEX_A, 0, 1) == 0, "sem_init on the same address");
	ck(n2shim_thread_kind_clashes() >= 1, "the kind clash was counted");
	ck(call1("sem_wait", MUTEX_A) == 0, "sem_wait after the clash");
	v = n2shim_thread_objects();
	ck(call1("sem_destroy", MUTEX_A) == 0, "sem_destroy");
	ck(n2shim_thread_objects() < v, "sem_destroy removed its entry");

	/* semaphores: counts, trywait, re-init */
	ck(call3("sem_init", SEM_A, 0, 2) == 0, "sem_init(count 2)");
	ck(call3("sem_init", SEM_A, 0, 2) == EBUSY, "sem_init on an initialised semaphore == EBUSY");
	ck(call1("sem_trywait", SEM_A) == 0, "sem_trywait");
	ck(call2("sem_getvalue", SEM_A, PARAM_A) == 0, "sem_getvalue");
	ck(n2a32_ld32(&g_cpu, PARAM_A) == 1, "sem_getvalue reports the count");
	ck(call1("sem_trywait", SEM_A) == 0, "sem_trywait");
	ck(call1("sem_trywait", SEM_A) == EAGAIN, "sem_trywait on an empty semaphore == EAGAIN");
	ck(call1("sem_post", SEM_A) == 0, "sem_post");
	ck(call1("sem_post", SEM_A) == 0, "sem_post");
	ck(call1("sem_wait", SEM_A) == 0, "sem_wait");
	ck(call1("sem_destroy", SEM_A) == 0, "sem_destroy");

	/* a signal on a cond that was never used is a no-op, not an error */
	ck(call1("pthread_cond_signal", COND_A) == 0, "cond_signal on a static cond");
	ck(call1("pthread_cond_broadcast", COND_A) == 0, "cond_broadcast on a static cond");
	ck(call1("pthread_cond_destroy", COND_A) == 0, "cond_destroy of a never-used cond");

	/* rwlock try paths */
	ck(call1("pthread_rwlock_init", RW_A) == 0, "rwlock_init");
	ck(call1("pthread_rwlock_tryrdlock", RW_A) == 0, "rwlock_tryrdlock");
	ck(call1("pthread_rwlock_trywrlock", RW_A) != 0, "trywrlock while a reader holds it fails");
	ck(call1("pthread_rwlock_unlock", RW_A) == 0, "rwlock_unlock");
	ck(call1("pthread_rwlock_trywrlock", RW_A) == 0, "trywrlock when free");
	ck(call1("pthread_rwlock_unlock", RW_A) == 0, "rwlock_unlock");
	ck(call1("pthread_rwlock_destroy", RW_A) == 0, "rwlock_destroy");

	/* ---- the failure slot ---- */
	n2shim_thread_failure_clear();
	ck(n2shim_thread_failure() == 0, "cleared failure slot");
	g_cpu.stop = N2A32_STOP_RETURN;
	ck(n2shim_thread_note_stop(&g_cpu) == 0, "a normal return is not a failure");
	g_cpu.stop = N2A32_STOP_REQUEST;
	ck(n2shim_thread_note_stop(&g_cpu) == 0, "a requested stop is not a failure");
	g_cpu.stop = N2A32_STOP_UNDEF;
	g_cpu.stop_insn = 0xdeadbeefu;
	g_cpu.stop_addr = 0x1234u;
	ck(n2shim_thread_note_stop(&g_cpu) == N2A32_STOP_UNDEF, "an undefined instruction is recorded");
	ck(n2shim_thread_failure() == N2A32_STOP_UNDEF, "failure() reports the first reason");
	ck(n2shim_thread_failure_count() == 1, "one failure counted");
	{
		uint32_t insn = 0, addr = 0;

		ck(n2shim_thread_failure_detail(&insn, &addr) == N2A32_STOP_UNDEF, "failure_detail returns the reason");
		ck(insn == 0xdeadbeefu && addr == 0x1234u, "failure_detail reports insn and address");
	}
	g_cpu.stop = N2A32_STOP_FAULT;
	ck(n2shim_thread_note_stop(&g_cpu) == N2A32_STOP_FAULT, "a second failure is counted");
	ck(n2shim_thread_failure() == N2A32_STOP_UNDEF, "the first failure is not overwritten");
	ck(n2shim_thread_failure_count() == 2, "two failures counted");
	ck(n2shim_thread_stop_requests() == 0, "the failing cpu itself is not asked to stop");
	ck(__atomic_load_n(&g_cpu.stop_request, __ATOMIC_ACQUIRE) == 0, "no stop_request was set");
	prev = n2shim_thread_request_stop();
	ck(prev == 1, "n2shim_thread_request_stop asks the one live cpu");
	ck(__atomic_load_n(&g_cpu.stop_request, __ATOMIC_ACQUIRE) == 1, "our own cpu was asked to stop");
	n2shim_thread_failure_clear();
	ck(n2shim_thread_failure() == 0 && n2shim_thread_failure_count() == 0, "failure_clear resets the slot");
	g_cpu.stop = N2A32_STOP_NONE;
	g_cpu.stop_insn = 0;
	g_cpu.stop_addr = 0;
	__atomic_store_n(&g_cpu.stop_request, 0u, __ATOMIC_RELEASE);

	/* ---- accounting ---- */
	printf("  side table: live %u, peak %u, kind clashes %u, created %u, live threads %u\n",
	       n2shim_thread_objects(), n2shim_thread_object_peak(), n2shim_thread_kind_clashes(),
	       n2shim_thread_created(), n2shim_thread_live());
	ck(n2shim_thread_live() == 1, "exactly the initial thread is live");
	ck(n2shim_thread_created() == 0, "this binary never created a guest thread");
	ck(n2shim_thread_objects() == 0, "every object created here was destroyed");

	/* ---- the documented object-table limit: 512 entries, then ENOMEM ---- */
	{
		uint32_t base = PAGE_A + 0x2000;
		uint32_t a;
		unsigned n = 0, first_bad = 0;

		for (a = base; a < base + 4 * 600; a += 4) {
			uint32_t rc = call1("pthread_mutex_lock", a);

			if (rc != 0) {
				first_bad = rc;
				break;
			}
			(void)call1("pthread_mutex_unlock", a);
			n++;
		}
		ck(n == 512, "exactly 512 objects fit in the side table");
		ck(first_bad == ENOMEM, "the 513th object reports ENOMEM");
		ck(n2shim_thread_objects() == 512, "the table reports 512 live objects");
		for (a = base; a < base + 4 * n; a += 4)
			(void)call1("pthread_mutex_destroy", a);
		ck(n2shim_thread_objects() == 0, "the side table is empty after destroying them");
	}

	printf("V32DIRECTRESULT %d/%d\n", checks - fails, checks);
	return fails == 0 ? 0 : 1;
}
