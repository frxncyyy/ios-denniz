/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_selftest.c -- vcodecd --selftest.  This is the first thing the main session runs on the phone: it proves
 * that the parts of the user-mode emulator vcodecd depends on work with no hardware and no vendor files present:
 *
 *   - the 4 GiB guest address space can be reserved and pages mapped;
 *   - two guest threads, each with its own n2a32_cpu, run A32 and T32 code at the same time;
 *   - exactly 1,000,000 guest instructions are interpreted (250,000 A32 + 250,000 T32 per thread), counted by the
 *     interpreter's own icount, with the instruction budget as the stopping reason;
 *   - a host call made from guest code sees the calling thread's _Thread_local state, and the two threads do not
 *     share it;
 *   - an unmapped load raises N2A32_STOP_FAULT with the faulting address, and the same cpu runs normally
 *     afterwards (recovery);
 *   - a Thumb function pointer (bit 0 set) is entered in T32 state and returns its value.
 *
 * Nothing here prints from library code: the selftest is the program, so it owns its output.  The guest byte
 * arrays come from daemon/test/selftest_{a32,t32}.s via llvm-mc (n2vcd_selftest_prog.h), and daemon/test/
 * run_tests.sh re-assembles them and fails if they differ.
 */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "n2vcd_selftest.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2vcd_selftest_prog.h"

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1                  /* the gpu/video iOS stub unistd.h has write() but not the constants */
#endif

/* Guest layout.  Addresses are the selftest's own choice; nothing else is mapped. */
#define SV_CODE      0x00100000u              /* A32 blob at +0, T32 blob at +0x1000 */
#define SV_CODE_LEN  0x00002000u
#define SV_DATA      0x00102000u
#define SV_DATA_LEN  0x00001000u
#define SV_STACK0    0x00200000u
#define SV_STACK_LEN 0x00020000u
#define SV_STACK_TOP(id) (SV_STACK0 + (uint32_t)(id) * 0x00010000u + 0x0000f000u)
#define SV_FAR       0x30000000u              /* never mapped: the fault test loads here */
#define SV_PER_THREAD 250000u                 /* A32 and again T32, so two threads make 1,000,000 */
#define SV_COOKIE    0x600d0000u

struct sv_thread {
	pthread_t host;
	struct n2a32_cpu cpu;
	unsigned id;
	unsigned long tls_addr;                   /* &tls_probe as seen by this thread */
	unsigned long tls_calls;                  /* this thread's own probe counter */
	uint64_t a32_count, t32_count;
	enum n2a32_stop a32_stop, t32_stop;
	enum n2a32_stop fault_stop;
	uint32_t fault_addr;
	uint32_t ok_a32, ok_t32, recovered;
	int a32_mismatch, t32_mismatch;
};

static _Thread_local unsigned sv_tls_probe;
static _Thread_local unsigned sv_tls_calls;
static struct n2a32_mem *sv_mem;
static uint32_t sv_probe_addr;
static uint32_t sv_a32_loop, sv_a32_ok, sv_a32_fault, sv_t32_loop, sv_t32_ok;
static struct sv_thread sv_th[2];
static atomic_uint sv_probe_calls;
static atomic_uint sv_probe_bad;

/* ---- output -------------------------------------------------------------------------------------------------- */

static int sv_console = -1;
static unsigned sv_checks, sv_fails;
static char sv_fail_step[64];

static void
sv_out(const char *line)
{
	(void)write(STDOUT_FILENO, line, strlen(line));
	if (sv_console >= 0) { (void)write(sv_console, line, strlen(line)); }
}

static int
sv_check(int cond, const char *step, const char *fmt, ...)
{
	sv_checks++;
	if (cond) { return 0; }
	sv_fails++;
	if (sv_fail_step[0] == '\0') { (void)snprintf(sv_fail_step, sizeof(sv_fail_step), "%s", step); }
	char line[512];
	int n = snprintf(line, sizeof(line), "N2VCD selftest FAIL %s: ", step);
	if (n < 0) { return -1; }
	size_t used = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1u;
	va_list ap;
	va_start(ap, fmt);
	int m = vsnprintf(line + used, sizeof(line) - used, fmt, ap);
	va_end(ap);
	if (m > 0) { used += (size_t)m < sizeof(line) - used ? (size_t)m : sizeof(line) - used - 1u; }
	if (used + 1u < sizeof(line)) { line[used++] = '\n'; line[used] = '\0'; }
	sv_out(line);
	return -1;
}

/* ---- the probe host call ------------------------------------------------------------------------------------- */

/* Called from guest code on the guest thread's host thread.  Its _Thread_local state must belong to that thread
 * alone; the return value carries the thread id so guest code can also check the call/return path. */
static void
sv_probe(struct n2a32_cpu *c)
{
	unsigned id = (unsigned)(uintptr_t)c->user;
	sv_tls_calls++;
	(void)atomic_fetch_add(&sv_probe_calls, 1u);
	if (sv_tls_probe != id) { (void)atomic_fetch_add(&sv_probe_bad, 1u); }
	c->r[0] = SV_COOKIE | id;                 /* the guest compares this with its expected id */
}

static void
sv_probe_entry(struct n2a32_cpu *c)
{
	sv_probe(c);
}

/* ---- one guest thread ---------------------------------------------------------------------------------------- */

static void
sv_load(void)
{
	memcpy(sv_mem->base + SV_CODE, n2sv_a32, sizeof(n2sv_a32));
	memcpy(sv_mem->base + SV_CODE + 0x1000, n2sv_t32, sizeof(n2sv_t32));
	sv_a32_loop = SV_CODE + N2SV_A32_LOOP_OFF;
	sv_a32_ok = SV_CODE + N2SV_A32_OK_OFF;
	sv_a32_fault = SV_CODE + N2SV_A32_FAULT_OFF;
	sv_t32_loop = SV_CODE + 0x1000 + N2SV_T32_LOOP_OFF;
	sv_t32_ok = SV_CODE + 0x1000 + N2SV_T32_OK_OFF;
}

static void *
sv_thread_main(void *arg)
{
	struct sv_thread *t = (struct sv_thread *)arg;
	uint32_t expected = SV_COOKIE | t->id;

	sv_tls_probe = t->id;
	sv_tls_calls = 0;
	t->tls_addr = (unsigned long)(uintptr_t)&sv_tls_probe;

	n2a32_cpu_init(&t->cpu, sv_mem);
	t->cpu.user = (void *)(uintptr_t)t->id;
	t->cpu.r[13] = SV_STACK_TOP(t->id);
	t->cpu.r[7] = sv_probe_addr;              /* the guest calls this with BLX r7 */

	uint32_t args[3];
	args[0] = sv_probe_addr;
	args[1] = 0x7fffffffu;                    /* larger than the budget: STOP_LIMIT ends the run */
	args[2] = expected;

	uint64_t start = t->cpu.icount;
	t->cpu.ilimit = start + SV_PER_THREAD;
	(void)n2a32_call(&t->cpu, sv_a32_loop, args, 3u, NULL);
	t->a32_count = t->cpu.icount - start;
	t->a32_stop = t->cpu.stop;

	start = t->cpu.icount;
	t->cpu.ilimit = start + SV_PER_THREAD;
	(void)n2a32_call(&t->cpu, sv_t32_loop | 1u, args, 3u, NULL);   /* bit 0: enter in T32 state */
	t->t32_count = t->cpu.icount - start;
	t->t32_stop = t->cpu.stop;

	/* No budget now: the same cpu must run to a normal return (proves STOP_LIMIT leaves usable state). */
	t->cpu.ilimit = 0;
	t->ok_a32 = n2a32_call(&t->cpu, sv_a32_ok, NULL, 0u, NULL);

	/* A Thumb entry point, via the bit-0 convention of n2a32_call. */
	t->ok_t32 = n2a32_call(&t->cpu, sv_t32_ok | 1u, NULL, 0u, NULL);

	/* One unmapped load: N2A32_STOP_FAULT with the address, then a normal call on the same cpu. */
	uint32_t bad = SV_FAR;
	(void)n2a32_call(&t->cpu, sv_a32_fault, &bad, 1u, NULL);
	t->fault_stop = t->cpu.stop;
	t->fault_addr = t->cpu.stop_addr;
	t->recovered = n2a32_call(&t->cpu, sv_a32_ok, NULL, 0u, NULL);
	t->tls_calls = sv_tls_calls;

	return NULL;
}

/* ---- the selftest -------------------------------------------------------------------------------------------- */

int
n2vcd_selftest(void)
{
	memset(sv_th, 0, sizeof(sv_th));
	sv_checks = 0;
	sv_fails = 0;
	sv_fail_step[0] = '\0';
	atomic_store(&sv_probe_calls, 0);
	atomic_store(&sv_probe_bad, 0);
	sv_console = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK);

	sv_mem = n2a32_mem_create();
	sv_check(sv_mem != NULL && sv_mem->base != NULL, "reserve-4gib", "n2a32_mem_create returned %p", (void *)sv_mem);
	if (!sv_mem) {
		sv_out("N2VCD selftest FAIL reserve-4gib: no 4 GiB guest reservation\n");
		if (sv_console >= 0) { (void)close(sv_console); }
		return 1;
	}
	sv_out("N2VCD selftest PASS reserve-4gib\n");

	int map_ok = n2a32_mem_map(sv_mem, SV_CODE, SV_CODE_LEN, 1) == 0 &&
	             n2a32_mem_map(sv_mem, SV_DATA, SV_DATA_LEN, 1) == 0 &&
	             n2a32_mem_map(sv_mem, SV_STACK0, SV_STACK_LEN, 1) == 0;
	sv_check(map_ok, "map", "code=%u data=%u stack=%u", SV_CODE, SV_DATA, SV_STACK0);
	if (!map_ok) {
		sv_out("N2VCD selftest FAIL map: n2a32_mem_map refused a page\n");
		n2a32_mem_destroy(sv_mem);
		sv_mem = NULL;
		if (sv_console >= 0) { (void)close(sv_console); }
		return 1;
	}
	sv_check(sizeof(n2sv_a32) <= 0x1000u && sizeof(n2sv_t32) <= 0x1000u, "program", "blob sizes %zu/%zu",
	         sizeof(n2sv_a32), sizeof(n2sv_t32));
	sv_load();
	sv_out("N2VCD selftest PASS map\n");

	sv_probe_addr = n2a32_hostcall_register(sv_probe_entry, "selftest_probe");
	sv_check(sv_probe_addr >= N2A32_HOSTCALL_BASE && sv_probe_addr < N2A32_HOSTCALL_BASE + 4u * N2A32_HOSTCALL_MAX,
	         "hostcall", "probe address 0x%08x", sv_probe_addr);

	int started = 1;
	for (unsigned i = 0; i < 2u; i++) {
		sv_th[i].id = i + 1u;
		if (pthread_create(&sv_th[i].host, NULL, sv_thread_main, &sv_th[i]) != 0) { started = 0; }
	}
	sv_check(started, "threads-start", "pthread_create failed");
	if (!started) {
		sv_out("N2VCD selftest FAIL threads-start: pthread_create failed\n");
		n2a32_mem_destroy(sv_mem);
		sv_mem = NULL;
		if (sv_console >= 0) { (void)close(sv_console); }
		return 1;
	}
	for (unsigned i = 0; i < 2u; i++) { (void)pthread_join(sv_th[i].host, NULL); }

	int a32_ok = 1, t32_ok = 1, total_ok = 1, tls_ok = 1;
	uint64_t total = 0;
	for (unsigned i = 0; i < 2u; i++) {
		struct sv_thread *t = &sv_th[i];
		sv_check(t->a32_stop == N2A32_STOP_LIMIT && t->a32_count == SV_PER_THREAD, "a32-loop",
		         "thread %u stop=%d count=%llu", t->id, (int)t->a32_stop, (unsigned long long)t->a32_count);
		if (t->a32_stop != N2A32_STOP_LIMIT || t->a32_count != SV_PER_THREAD) { a32_ok = 0; }
		sv_check(t->t32_stop == N2A32_STOP_LIMIT && t->t32_count == SV_PER_THREAD, "t32-loop",
		         "thread %u stop=%d count=%llu addr=0x%08x insn=0x%08x", t->id, (int)t->t32_stop,
		         (unsigned long long)t->t32_count, t->cpu.stop_addr, t->cpu.stop_insn);
		if (t->t32_stop != N2A32_STOP_LIMIT || t->t32_count != SV_PER_THREAD) { t32_ok = 0; }
		sv_check(t->ok_a32 == 0x600du && t->ok_t32 == 0x11u, "return",
		         "thread %u a32=0x%x t32=0x%x", t->id, t->ok_a32, t->ok_t32);
		sv_check(t->fault_stop == N2A32_STOP_FAULT && t->fault_addr == SV_FAR, "fault",
		         "thread %u stop=%d addr=0x%08x", t->id, (int)t->fault_stop, t->fault_addr);
		sv_check(t->recovered == 0x600du, "fault-recover", "thread %u recovered=0x%x", t->id, t->recovered);
		total += t->a32_count + t->t32_count;
	}
	sv_out(a32_ok ? "N2VCD selftest PASS a32-loop\n" : "N2VCD selftest FAIL a32-loop\n");
	sv_out(t32_ok ? "N2VCD selftest PASS t32-loop\n" : "N2VCD selftest FAIL t32-loop\n");
	sv_check(total == 1000000u, "total-1e6", "interpreted %llu instructions", (unsigned long long)total);
	if (total != 1000000u) { total_ok = 0; }

	/* _Thread_local: both threads set their own value, the probe read it back on the thread that ran the guest
	 * code, no mismatch was seen, and the two addresses are different (a shared variable would be one address). */
	sv_check(atomic_load(&sv_probe_bad) == 0u, "tls-value", "%u of %u probe calls saw the wrong thread id",
	         atomic_load(&sv_probe_bad), atomic_load(&sv_probe_calls));
	sv_check(atomic_load(&sv_probe_calls) >= 2u * (SV_PER_THREAD / 8u), "tls-probe-count", "%u probe calls",
	         atomic_load(&sv_probe_calls));
	sv_check(sv_th[0].tls_calls > 0u && sv_th[1].tls_calls > 0u &&
	         sv_th[0].tls_calls + sv_th[1].tls_calls == atomic_load(&sv_probe_calls), "tls-count",
	         "thread 1 %lu thread 2 %lu total %u", sv_th[0].tls_calls, sv_th[1].tls_calls,
	         atomic_load(&sv_probe_calls));
	sv_check(sv_th[0].tls_addr != 0 && sv_th[1].tls_addr != 0 && sv_th[0].tls_addr != sv_th[1].tls_addr,
	         "tls-address", "thread 1 %lx thread 2 %lx", sv_th[0].tls_addr, sv_th[1].tls_addr);
	if (atomic_load(&sv_probe_bad) != 0u || sv_th[0].tls_addr == sv_th[1].tls_addr) { tls_ok = 0; }
	sv_out(tls_ok ? "N2VCD selftest PASS tls\n" : "N2VCD selftest FAIL tls\n");

	n2a32_mem_destroy(sv_mem);
	sv_mem = NULL;

	if (sv_fails == 0 && total_ok) {
		char line[128];
		(void)snprintf(line, sizeof(line), "N2VCD selftest PASS %u/%u (4 GiB, 2 threads, 1000000 insns, fault, TLS)\n",
		               sv_checks, sv_checks);
		sv_out(line);
		if (sv_console >= 0) { (void)close(sv_console); }
		return 0;
	}
	{
		char line[256];
		(void)snprintf(line, sizeof(line), "N2VCD selftest FAIL %s (%u/%u checks failed)\n",
		               sv_fail_step[0] ? sv_fail_step : "unknown", sv_fails, sv_checks);
		sv_out(line);
	}
	if (sv_console >= 0) { (void)close(sv_console); }
	return 1;
}
