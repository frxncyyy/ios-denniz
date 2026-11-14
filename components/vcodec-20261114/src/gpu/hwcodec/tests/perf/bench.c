/* SPDX-License-Identifier: MIT */
/*
 * bench.c -- host runner for the n2a32 micro benchmarks (opus-perf).
 *
 *   bench --lib OBJ [--lib OBJ] [--ms N] [--native]
 *
 * Loads the ARM objects through V03, calls every b_* kernel with an iteration count calibrated to run
 * for about --ms milliseconds (default 300), and prints guest instructions, wall time and guest MIPS
 * (c->icount / time).  --native also times the same kernels compiled for the host (bench_native.o must
 * be linked in with -DBENCH_NATIVE), to give the interpretation overhead per guest instruction.
 */
#define _POSIX_C_SOURCE 200809L
#include "n2a32.h"
#include "n2elf.h"
#ifdef BENCH_MIX
#include "n2a32_trace.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__linux__)        /* host instructions and cycles per guest instruction: stable on a shared host */
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>
static int pf_fd[2] = { -1, -1 };
static void pf_open(void)
{
	static const uint64_t cfg[2] = { PERF_COUNT_HW_INSTRUCTIONS, PERF_COUNT_HW_CPU_CYCLES };
	for (int k = 0; k < 2; k++) {
		struct perf_event_attr pa;
		memset(&pa, 0, sizeof(pa));
		pa.type = PERF_TYPE_HARDWARE;
		pa.size = sizeof(pa);
		pa.config = cfg[k];
		pa.exclude_kernel = 1;
		pa.exclude_hv = 1;
		pf_fd[k] = (int)syscall(SYS_perf_event_open, &pa, 0, -1, -1, 0);
	}
}
static uint64_t pf_read(int k)
{
	uint64_t v = 0;
	if (pf_fd[k] < 0 || read(pf_fd[k], &v, sizeof(v)) != (ssize_t)sizeof(v))
		return 0;
	return v;
}
#else
static void pf_open(void) {}
static uint64_t pf_read(int k) { (void)k; return 0; }
#endif
#ifdef BENCH_PROF            /* x86-64 Linux only: SIGPROF sampler of the host PC, build with -no-pie */
#include <signal.h>
#include <sys/time.h>
#include <ucontext.h>
static uint64_t prof_pc[1u << 20];
static volatile unsigned prof_n;
static void prof_handler(int sig, siginfo_t *si, void *uc)
{
	(void)sig; (void)si;
	if (prof_n < (1u << 20))
		prof_pc[prof_n++] = (uint64_t)((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP];
}
static void prof_start(void)
{
	struct sigaction sa;
	struct itimerval it = { { 0, 200 }, { 0, 200 } };
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = prof_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction(SIGPROF, &sa, NULL);
	setitimer(ITIMER_PROF, &it, NULL);
}
static void prof_stop(const char *path)
{
	struct itimerval it = { { 0, 0 }, { 0, 0 } };
	FILE *f;
	setitimer(ITIMER_PROF, &it, NULL);
	f = fopen(path, "w");
	if (!f)
		return;
	for (unsigned i = 0; i < prof_n; i++)
		fprintf(f, "0x%llx\n", (unsigned long long)prof_pc[i]);
	fclose(f);
}
#endif

#define STACK_SIZE (256u * 1024u)

/* Non-ABI switch of the optimised core (absent in older cores): --nofast runs every instruction through its
 * class function, to compare the fast paths against the class path in one process. */
extern volatile int n2a32_core_fastpath __attribute__((weak));

static const char *const kernels[] = { "b_alu", "b_mem", "b_branch", "b_bitw", "b_vfp", "b_neon" };
#define NK (sizeof(kernels) / sizeof(kernels[0]))

#ifdef BENCH_NATIVE
uint32_t b_alu(uint32_t); uint32_t b_mem(uint32_t); uint32_t b_branch(uint32_t);
uint32_t b_bitw(uint32_t); uint32_t b_vfp(uint32_t); uint32_t b_neon(uint32_t);
static uint32_t (*const native[])(uint32_t) = { b_alu, b_mem, b_branch, b_bitw, b_vfp, b_neon };
#endif

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);   /* CPU time: the build host is shared */
	return (double)ts.tv_sec + ts.tv_nsec * 1e-9;
}

static uint64_t last_hins, last_cyc;
static int run_one(struct n2a32_cpu *cpu, struct n2a32_mem *mem, uint32_t stack, uint32_t fn, uint32_t n,
		   uint32_t *res, uint64_t *insns, double *secs)
{
	double t0;
	uint64_t i0, c0;
	n2a32_cpu_init(cpu, mem);
	cpu->r[13] = stack;
#ifdef BENCH_MIX
	n2a32_trace_attach(cpu);     /* n2perf_mix.c linked instead of T01's trace: counts the mix */
#endif
	i0 = pf_read(0); c0 = pf_read(1);
	t0 = now();
	*res = n2a32_call(cpu, fn, &n, 1, NULL);
	*secs = now() - t0;
	last_hins = pf_read(0) - i0; last_cyc = pf_read(1) - c0;
	*insns = cpu->icount;
	return cpu->stop == N2A32_STOP_RETURN ? 0 : -1;
}

int main(int argc, char **argv)
{
	const char *libs[4];
	int nlibs = 0, i, do_native = 0;
	double target_ms = 300.0;
	struct n2a32_mem *mem;
	struct n2elf_set *set;
	struct n2a32_cpu cpu;
	char err[160];
	unsigned unresolved = 0;
	uint32_t stack;
	double total_i = 0, total_s = 0, total_h = 0, total_c = 0;
#ifdef BENCH_PROF
	const char *prof_path = NULL;
#endif

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--lib") && i + 1 < argc && nlibs < 4)
			libs[nlibs++] = argv[++i];
		else if (!strcmp(argv[i], "--ms") && i + 1 < argc)
			target_ms = atof(argv[++i]);
#ifdef BENCH_PROF
		else if (!strcmp(argv[i], "--prof") && i + 1 < argc)
			prof_path = argv[++i];
#endif
		else if (!strcmp(argv[i], "--nofast")) {
			if (&n2a32_core_fastpath != NULL)
				n2a32_core_fastpath = 0;
		} else if (!strcmp(argv[i], "--native"))
			do_native = 1;
		else {
			fprintf(stderr, "usage: %s --lib OBJ [--ms N] [--native]\n", argv[0]);
			return 2;
		}
	}
	if (!nlibs)
		return 2;
	mem = n2a32_mem_create();
	set = mem ? n2elf_set_create(mem) : NULL;
	if (!set)
		return 3;
	for (i = 0; i < nlibs; i++) {
		char so[16];
		snprintf(so, sizeof(so), "lib%d.so", i);
		if (!n2elf_load(set, libs[i], so, err)) {
			fprintf(stderr, "bench: load %s: %s\n", libs[i], err);
			return 3;
		}
	}
	if (n2elf_link(set, &unresolved, err) != 0 || unresolved) {
		fprintf(stderr, "bench: link failed (%s), unresolved=%u\n", err, unresolved);
		return 3;
	}
	stack = n2a32_mem_alloc(mem, STACK_SIZE, 8);
	if (!stack)
		return 3;
	stack += STACK_SIZE - 8;

#ifdef BENCH_PROF
	if (prof_path)
		prof_start();
#endif
	pf_open();
	printf("%-9s %12s %9s %9s %8s %8s %8s %s\n", "kernel", "guest_insn", "ms", "MIPS", "ns/insn", "hins/i", "cyc/i",
	       "native_x");
	for (size_t k = 0; k < NK; k++) {
		uint32_t fn = n2elf_sym(set, kernels[k]), n = 16, res = 0;
		uint64_t insns = 0;
		double secs = 0;

		if (!fn) {
			printf("%-9s missing\n", kernels[k]);
			continue;
		}
		/* calibrate: grow n until one run takes >= target/4, then scale */
		for (;;) {
			if (run_one(&cpu, mem, stack, fn, n, &res, &insns, &secs) != 0) {
				printf("%-9s STOP %d at %08x insn %08x\n", kernels[k], (int)cpu.stop, cpu.stop_addr, cpu.stop_insn);
				goto next;
			}
			if (secs * 1000.0 >= target_ms / 4 || n >= (1u << 28))
				break;
			n *= 4;
		}
		n = (uint32_t)((double)n * (target_ms / 1000.0) / (secs > 1e-6 ? secs : 1e-6));
		if (n < 1)
			n = 1;
		/* best of 5 */
		{
			double best = 1e9;
			uint64_t bh = 0, bc = ~0ull;
			for (int r = 0; r < 5; r++) {
				run_one(&cpu, mem, stack, fn, n, &res, &insns, &secs);
				if (secs < best)
					best = secs;
				if (last_cyc < bc) {
					bc = last_cyc;
					bh = last_hins;
				}
			}
			secs = best;
			total_h += (double)bh;
			total_c += (double)bc;
			last_hins = bh;
			last_cyc = bc;
		}
		total_i += (double)insns;
		total_s += secs;
		printf("%-9s %12llu %9.1f %9.1f %8.2f %8.1f %8.1f", kernels[k], (unsigned long long)insns, secs * 1e3,
		       insns / secs / 1e6, secs * 1e9 / insns, (double)last_hins / insns, (double)last_cyc / insns);
#ifdef BENCH_NATIVE
		if (do_native) {
			double t0 = now(), ns;
			volatile uint32_t r2 = native[k](n);
			(void)r2;
			ns = now() - t0;
			printf(" %8.0f", secs / (ns > 1e-9 ? ns : 1e-9));
		}
#else
		(void)do_native;
#endif
		printf("   (n=%u res=%08x)\n", n, res);
	next:;
	}
#ifdef BENCH_PROF
	if (prof_path)
		prof_stop(prof_path);
#endif
	printf("%-9s %12.0f %9.1f %9.1f %8.2f %8.1f %8.1f\n", "TOTAL", total_i, total_s * 1e3, total_i / total_s / 1e6,
	       total_s * 1e9 / total_i, total_h / total_i, total_c / total_i);
	return 0;
}
