/* SPDX-License-Identifier: MIT */
/*
 * t_e2e.c -- V32 end-to-end host test: a real ARM shared object (arm/t_v32_guest.c) importing the pthread/sem/
 * sched/signal functions of n2shim_thread[], loaded by V03's real loader, executed by V02's real interpreter on
 * host threads started by V32's pthread_create shim.
 *
 * Each guest entry point runs its own checks and keeps g_total/g_fail plus the __LINE__ of each failure; this
 * driver adds the guest's counters to its own, so a guest check failure is reported like a host one.  The
 * failure-propagation entries are checked from here (first-failure slot, stop_request on the other cpus, and the
 * victims actually stopping when they are released).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

struct guest {
	struct n2a32_mem *m;
	struct n2elf_set *s;
	struct n2a32_cpu c;
	uint32_t total, fail, fail_n, fail_id;
	uint32_t dbg[8];
};

static const struct n2shim_data data_imports[] = {
	{ "__stack_chk_guard", 4, NULL },
	{ "stderr", 4, NULL },
	{ NULL, 0, NULL }
};

static uint32_t grd(struct guest *g, uint32_t a)
{
	return a != 0 ? n2a32_ld32(&g->c, a) : 0;
}

static void print_guest_failures(struct guest *g, unsigned from, unsigned to)
{
	unsigned i;

	for (i = from; i < to && i < 64; i++)
		printf("  guest check failed at t_v32_guest.c:%u\n", n2a32_ld32(&g->c, g->fail_id + 4 * i));
}

/* Run one guest entry point; `expect` is the stop reason n2a32_call must report. */
static void run_entry(struct guest *g, const char *sym, int expect)
{
	uint32_t fn = n2elf_sym(g->s, sym);
	uint32_t t0, f0, n0, t1, f1, n1;

	if (fn == 0) {
		ck(0, sym);
		return;
	}
	t0 = grd(g, g->total);
	f0 = grd(g, g->fail);
	n0 = grd(g, g->fail_n);
	(void)n2a32_call(&g->c, fn, NULL, 0, NULL);
	t1 = grd(g, g->total);
	f1 = grd(g, g->fail);
	n1 = grd(g, g->fail_n);

	if ((int)g->c.stop != expect) {
		printf("FAIL: %s stopped with %d, expected %d\n", sym, (int)g->c.stop, expect);
		fails++;
	}
	checks++;
	if (t1 < t0) {
		printf("FAIL: %s guest check counter went backwards\n", sym);
		fails++;
	}
	checks += (int)(t1 - t0);
	if (f1 != f0) {
		fails += (int)(f1 - f0);
		print_guest_failures(g, n0, n1);
		unsigned k;

		printf("  guest g_dbg:");
		for (k = 0; k < 8; k++)
			printf(" %d", (int)grd(g, g->dbg[k]));
		printf("\n");
	}
	printf("  %-22s stop=%-2d guest checks %u, failures %u, live %u\n", sym, (int)g->c.stop,
	       t1 - t0, f1 - f0, n2shim_thread_live());
}

static const struct n2shim_entry *find_shim(const char *name)
{
	const struct n2shim_entry *e;

	for (e = n2shim_thread; e->name != NULL; e++)
		if (strcmp(e->name, name) == 0)
			return e;
	return NULL;
}

/* Call a shim directly from host code (used to release guest threads blocked in sem_wait after a teardown). */
static uint32_t call_shim1(struct n2a32_cpu *c, const char *name, uint32_t a0)
{
	const struct n2shim_entry *e = find_shim(name);

	c->r[0] = a0;
	c->r[1] = 0;
	c->r[2] = 0;
	c->r[3] = 0;
	if (e == NULL)
		return 0xffffffffu;
	e->fn(c);
	return c->r[0];
}

static void clear_cpu_stop(struct guest *g)
{
	__atomic_store_n(&g->c.stop_request, 0u, __ATOMIC_RELEASE);
}

static int wait_live(unsigned want, long ms)
{
	struct timespec ts;
	long waited = 0;

	while (n2shim_thread_live() != want) {
		if (waited >= ms)
			return 0;
		ts.tv_sec = 0;
		ts.tv_nsec = 2000000L;          /* 2 ms */
		(void)nanosleep(&ts, NULL);
		waited += 2;
	}
	return 1;
}

int main(int argc, char **argv)
{
	struct guest g;
	char err[160];
	unsigned unresolved = 0;
	uint32_t stack;
	unsigned i;
	int fault_tests = 1;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <libv32guest.so> [--no-fault]\n", argv[0]);
		return 2;
	}
	if (argc > 2 && strcmp(argv[2], "--no-fault") == 0)
		fault_tests = 0;                /* TSan cannot follow V02's siglongjmp out of a guest fault */
	memset(&g, 0, sizeof(g));
	g.m = n2a32_mem_create();
	g.s = g.m != NULL ? n2elf_set_create(g.m) : NULL;
	if (g.m == NULL || g.s == NULL) {
		printf("N2TEST V32-E2E FAIL cannot create the guest address space\n");
		return 1;
	}
	n2elf_set_add_shims(g.s, n2shim_thread);
	n2elf_set_add_data(g.s, data_imports);

	if (n2elf_load(g.s, argv[1], "libv32guest.so", err) == NULL) {
		printf("N2TEST V32-E2E FAIL load: %s\n", err);
		return 1;
	}
	if (n2elf_link(g.s, &unresolved, err) != 0) {
		printf("N2TEST V32-E2E FAIL link: %s\n", err);
		return 1;
	}
	if (unresolved != 0) {
		printf("FAIL: %u imports of the guest are not covered by n2shim_thread[]\n", unresolved);
		fails++;
	}
	checks++;
	ck(n2elf_sym(g.s, "v32_increments") != 0, "guest entry points are present");

	for (i = 0; i < 8; i++)
		g.dbg[i] = n2elf_sym(g.s, "g_dbg") + 4 * i;
	g.total = n2elf_sym(g.s, "g_total");
	g.fail = n2elf_sym(g.s, "g_fail");
	g.fail_n = n2elf_sym(g.s, "g_fail_n");
	g.fail_id = n2elf_sym(g.s, "g_fail_id");
	ck(g.total && g.fail && g.fail_n && g.fail_id, "guest check counters are present");

	n2a32_cpu_init(&g.c, g.m);
	stack = n2a32_mem_alloc(g.m, 1u << 20, 16);
	ck(stack != 0, "main guest stack allocated");
	if (stack == 0) {
		printf("N2TEST V32-E2E FAIL no stack\n");
		return 1;
	}
	g.c.r[13] = stack + (1u << 20) - 16;
	g.c.r[14] = N2A32_RETURN_ADDR;

	ck(n2shim_thread_failure() == 0, "no failure before the first test");
	ck(n2shim_thread_live() == 0, "no guest thread registered yet");

	/* ---- the plain entries: they must all run to their return address ---- */
	run_entry(&g, "v32_increments", N2A32_STOP_RETURN);
	run_entry(&g, "v32_prodcons", N2A32_STOP_RETURN);
	run_entry(&g, "v32_sem_pingpong", N2A32_STOP_RETURN);
	run_entry(&g, "v32_once", N2A32_STOP_RETURN);
	run_entry(&g, "v32_rwlock", N2A32_STOP_RETURN);
	run_entry(&g, "v32_lifecycle", N2A32_STOP_RETURN);
	run_entry(&g, "v32_misc", N2A32_STOP_RETURN);
	run_entry(&g, "v32_zeroinit", N2A32_STOP_RETURN);
	run_entry(&g, "v32_churn", N2A32_STOP_RETURN);
	run_entry(&g, "v32_once_recurse", N2A32_STOP_RETURN);

	ck(n2shim_thread_created() >= 8 + 1 + 4 + 8 + 6 + 3 + 2, "pthread_create was used by the guest");
	ck(n2shim_thread_kind_clashes() >= 1, "one guest address reused for a second object kind was handled");
	ck(n2shim_thread_nested_once() == 1, "pthread_once re-entered from its own init routine returned EDEADLK");
	ck(n2shim_thread_failure() == 0, "no failure from the plain entries");
	ck(wait_live(1, 2000), "only the initial thread is live after the plain entries");

	/* ---- a pthread_once init routine that stops abnormally ---- */
	if (fault_tests) {
		n2shim_thread_failure_clear();
		run_entry(&g, "v32_once_fail", N2A32_STOP_REQUEST);
		ck(n2shim_thread_failure() == N2A32_STOP_FAULT, "the faulting once init recorded STOP_FAULT");
		ck(n2shim_thread_failure_count() == 1, "exactly one failure from the once init");
		clear_cpu_stop(&g);
	}

	/* ---- the initial thread calling pthread_exit() ---- */
	{
		uint32_t marker = n2elf_sym(g.s, "g_main_exit_marker");

		n2shim_thread_failure_clear();
		run_entry(&g, "v32_main_exit", N2A32_STOP_REQUEST);
		ck(grd(&g, marker) == 0, "the guest never resumed after pthread_exit on the initial thread");
		ck(n2shim_thread_failure() == 0, "pthread_exit is not recorded as a failure");
		ck(wait_live(0, 2000), "the initial thread is published as exited");
		clear_cpu_stop(&g);
		/* The daemon may hand more guest code to the same cpu: the next shim call registers it again. */
		ck(call_shim1(&g.c, "pthread_self", 0) != 0, "pthread_self re-registers the initial cpu");
		ck(n2shim_thread_live() == 1, "the initial thread is running again");
	}

	/* ---- one thread that stops abnormally: slot + stop_request on the initial thread ---- */
	if (fault_tests) {
		unsigned before = n2shim_thread_stop_requests();

		n2shim_thread_failure_clear();
		run_entry(&g, "v32_fail_single", N2A32_STOP_REQUEST);
		ck(n2shim_thread_failure() == N2A32_STOP_FAULT, "the faulting thread recorded STOP_FAULT first");
		ck(n2shim_thread_failure_thread() != 0, "the failing thread id was recorded");
		ck(n2shim_thread_failure_count() == 1, "exactly one failure was recorded");
		ck(n2shim_thread_stop_requests() > before, "stop_request was set on the initial thread's cpu");
		ck(wait_live(1, 2000), "the faulting thread exited");
		clear_cpu_stop(&g);
	}

	/* ---- three threads blocked in sem_wait while a fourth faults; all must be stopped ---- */
	if (fault_tests) {
		uint32_t sem = n2elf_sym(g.s, "g_fail_sem");
		unsigned before = n2shim_thread_stop_requests();
		struct n2a32_cpu scratch;

		ck(sem != 0, "guest semaphore symbol");
		n2shim_thread_failure_clear();
		run_entry(&g, "v32_fail_multi", N2A32_STOP_REQUEST);
		ck(n2shim_thread_failure() == N2A32_STOP_FAULT, "STOP_FAULT is still the first failure");
		ck(n2shim_thread_failure_count() == 1, "the stopped victims did not add a failure");
		ck(n2shim_thread_stop_requests() >= before + 4, "the failing thread asked every other cpu to stop");

		/* The three victims are blocked in sem_wait; the teardown request must wake them by itself (the wait
		 * is sliced at 100 ms), so they stop with N2A32_STOP_REQUEST without anyone posting the semaphore. */
		ck(wait_live(1, 2000), "the victims stopped on their own after the teardown request");
		/* The semaphore object is still usable afterwards (the guests never took it). */
		n2a32_cpu_init(&scratch, g.m);
		for (i = 0; i < 3; i++)
			ck(call_shim1(&scratch, "sem_post", sem) == 0, "sem_post after the teardown");
		clear_cpu_stop(&g);
	}

	/* ---- side table bookkeeping ---- */
	printf("  side table: live %u, peak %u, kind clashes %u, nested once %u, created %u\n",
	       n2shim_thread_objects(), n2shim_thread_object_peak(), n2shim_thread_kind_clashes(),
	       n2shim_thread_nested_once(), n2shim_thread_created());
	ck(n2shim_thread_objects() <= 10, "the side table does not leak objects across the whole run");
	ck(n2shim_thread_object_peak() <= 64, "the side table stayed small");

	/* The initial thread's record is still registered for this cpu; it is not a leak, but the accounting must
	 * agree with reality (one running guest thread on this host thread). */
	ck(n2shim_thread_live() <= 1, "no guest thread is left running");

	/* Tear the guest address space down while every guest thread has stopped. */
	n2elf_set_destroy(g.s);
	n2a32_mem_destroy(g.m);

	printf("V32RESULT %d/%d\n", checks - fails, checks);
	return fails == 0 ? 0 : 1;
}
