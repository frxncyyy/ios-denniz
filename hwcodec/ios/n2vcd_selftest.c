/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_selftest.c -- first thing to run on the phone (CRITICAL_PATH item 9): proves, under iOS 16.7 on the Nord 2, the process
 * facilities vcodecd depends on, before any hardware is involved.  No kernel driver is needed.  Owner: opus-ios.
 *
 *   T1 vm       a 4 GiB + 64 KiB PROT_NONE reservation (what n2a32_mem_create does); if it fails, the largest one that fits
 *               (binary search) and where the process's pieces live (text, heap, stack), so the cause is visible in one run
 *   T2 tls      _Thread_local on two threads (dyld's __tlv_bootstrap path on iOS)
 *   T3 interp   the real interpreter (IV's ios-lib archive) runs an A32 and a T32 loop on two host threads at once, results
 *               checked against the same loops computed natively; instructions per second (the MIPS number opus-perf needs)
 *   T4 fault    a guest load from an unmapped page -> host SIGSEGV/SIGBUS -> N2A32_STOP_FAULT at the right address, on both
 *               threads, and the same cpu then runs normally again (siglongjmp out of the handler under iOS)
 *   T5 vendor   n2vhost_start() on the phone's REAL five vendor objects with T02's fake hardware: load, link, init arrays, vpud's
 *               guest main up to its first /dev/vcu ioctl (M-a/M-b/M-c) -- the loader and shims under iOS, no hardware
 *
 * Every line goes to stdout and /dev/console (= the kernel log the main session reads with phone.py logs), prefix "N2VCDT".
 * Last line: "N2VCDT DONE pass=P fail=F skip=S ...".  A summary is also written to <outdir>/selftest-<pid>.txt.
 *
 *   n2vcd_selftest [--gate PATH] [--keep-gate] [--vendor DIR] [--out DIR] [--iters N] [--vendor-seconds S] [--no-console]
 *                  [--skip-vendor]
 *
 * --gate PATH: exit 0 at once (one line) unless PATH exists; after a complete run the gate is removed (one run per installation)
 * unless --keep-gate.  This is the launchd job's switch.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "n2a32.h"
#include "n2memlimit.h"
#include "n2vc_hw.h"
#include "n2vhost.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

static int g_console = -1;
static FILE *g_sum;
static unsigned g_pass, g_fail, g_skip;
static uint64_t g_t0;

static uint64_t now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	char buf[320];
	uint64_t t = now_us() - g_t0;
	int n = snprintf(buf, sizeof buf, "N2VCDT %llu.%03llu ", (unsigned long long)(t / 1000000u),
	                 (unsigned long long)(t / 1000u % 1000u));
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf + n, sizeof buf - (size_t)n - 1, fmt, ap);
	va_end(ap);
	size_t len = strnlen(buf, sizeof buf - 2);
	for (size_t i = 0; i < len; i++) {
		if ((unsigned char)buf[i] < 0x20) buf[i] = ' ';     /* one console line per call */
	}
	buf[len++] = '\n';
	fwrite(buf, 1, len, stdout);
	fflush(stdout);
	if (g_console >= 0) (void)!write(g_console, buf, len);
	if (g_sum) { fwrite(buf, 1, len, g_sum); fflush(g_sum); }
}

/* opus-guestmem: footprint / RSS at each step (jetsam enforces phys_footprint), and n2a32_mem's own log lines */
static void usage_say(const char *step)
{
	char u[96];
	say("mem %s %s", step, n2ml_usage_str(u, sizeof u));
}
static void mem_say(const char *line) { say("%s", line); }

/* Host faults outside the guest bridge (the core chains to this): signal, si_code, address, then the default action.
 * Async-signal-safe: hand-formatted hex, write(2) only. */
static char g_crash_stack[65536];
static void put_hex(char **o, uintptr_t v)
{
	char t[2 + 16];
	int n = 0;
	do { t[n++] = "0123456789abcdef"[v & 15u]; v >>= 4; } while (v && n < 16);
	*(*o)++ = '0'; *(*o)++ = 'x';
	while (n) *(*o)++ = t[--n];
}
static void crash_handler(int sig, siginfo_t *si, void *uc)
{
	(void)uc;
	char buf[160], *o = buf;
	const char *h = "N2VCDT CRASH host fault sig=";
	while (*h) *o++ = *h++;
	put_hex(&o, (uintptr_t)sig);
	h = " si_code=";
	while (*h) *o++ = *h++;
	put_hex(&o, (uintptr_t)(si ? (unsigned)si->si_code : 0u));
	h = " si_addr=";
	while (*h) *o++ = *h++;
	put_hex(&o, si ? (uintptr_t)si->si_addr : 0u);
	*o++ = '\n';
	(void)!write(1, buf, (size_t)(o - buf));
	if (g_console >= 0) (void)!write(g_console, buf, (size_t)(o - buf));
	signal(sig, SIG_DFL);
	raise(sig);
}
static void install_crash_handler(void)
{
	stack_t ss;
	struct sigaction sa;
	memset(&ss, 0, sizeof ss);
	ss.ss_sp = g_crash_stack;
	ss.ss_size = sizeof g_crash_stack;
	(void)sigaltstack(&ss, NULL);
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	(void)sigaction(SIGSEGV, &sa, NULL);
	(void)sigaction(SIGBUS, &sa, NULL);
	(void)sigaction(SIGILL, &sa, NULL);
	(void)sigaction(SIGFPE, &sa, NULL);
}

static void verdict(const char *test, int ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void verdict(const char *test, int ok, const char *fmt, ...)
{
	char detail[240];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(detail, sizeof detail, fmt, ap);
	va_end(ap);
	if (ok > 0) g_pass++; else if (ok == 0) g_fail++; else g_skip++;
	say("%s %s %s", test, ok > 0 ? "PASS" : ok == 0 ? "FAIL" : "SKIP", detail);
}

/* ------------------------------------------------------------------------------------------------- T1 vm */

#define GUEST_RESERVE (4ull * 1024 * 1024 * 1024 + 64 * 1024)

static int t1_vm(void)
{
	int ok = 1;
	struct rlimit rl;
	char lim[96] = "";
	if (getrlimit(RLIMIT_AS, &rl) == 0)
		snprintf(lim, sizeof lim, "rlimit_as=%llx data=", (unsigned long long)rl.rlim_cur);
	if (getrlimit(RLIMIT_DATA, &rl) == 0)
		snprintf(lim + strlen(lim), sizeof lim - strlen(lim), "%llx", (unsigned long long)rl.rlim_cur);
	int local = 0;
	void *heap = malloc(64);
	say("vm layout text=%p heap=%p stack=%p page=%ld %s", (void *)&t1_vm, heap, (void *)&local, sysconf(_SC_PAGESIZE), lim);
	free(heap);

	/* largest PROT_NONE reservation that fits, bisected 64 MiB..4 GiB+64 KiB to 16 MiB resolution (logged always) */
	uint64_t lo = 0, hi = GUEST_RESERVE + 1;
	{
		void *q = mmap(NULL, (size_t)GUEST_RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (q != MAP_FAILED) { munmap(q, (size_t)GUEST_RESERVE); lo = GUEST_RESERVE; }
		else {
			lo = 0; hi = GUEST_RESERVE;
			q = mmap(NULL, (size_t)(64ull << 20), PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
			if (q != MAP_FAILED) { munmap(q, (size_t)(64ull << 20)); lo = 64ull << 20; }
			while (lo && hi - lo > (16ull << 20)) {
				uint64_t mid = (lo + hi) / 2;
				q = mmap(NULL, (size_t)mid, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
				if (q != MAP_FAILED) { munmap(q, (size_t)mid); lo = mid; } else { hi = mid; }
			}
		}
	}
	say("vm largest PROT_NONE reservation %llu MiB%s", (unsigned long long)(lo >> 20), lo >= GUEST_RESERVE ? " (full 4GiB+64KiB)" : "");

	void *p = mmap(NULL, (size_t)GUEST_RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (p == MAP_FAILED) {
		int e = errno;
		verdict("T1 vm", 0, "4GiB+64KiB PROT_NONE mmap failed errno=%d (%s); largest that fits ~%llu MiB", e, strerror(e),
		        (unsigned long long)(lo >> 20));
		return 0;
	}
	/* Diagnostic: a 4 KiB-aligned (not host-page aligned) mprotect, which XNU rejects with EINVAL on 16 KiB pages;
	 * the interpreter (n2a32_mem.c) now protects whole host pages, so this is informational only. */
	uint8_t *b = (uint8_t *)p;
	long hp = sysconf(_SC_PAGESIZE);
	if (hp < 4096) hp = 4096;
	errno = 0;
	int r0 = mprotect(b + 4096, 4096, PROT_READ | PROT_WRITE);
	int e0 = r0 ? errno : 0;
	if (r0 == 0) mprotect(b + 4096, 4096, PROT_NONE);
	/* touch one host page at each end through mprotect, as n2a32_mem_map does */
	errno = 0;
	int r1 = mprotect(b, (size_t)hp, PROT_READ | PROT_WRITE);
	int e1 = r1 ? errno : 0;
	errno = 0;
	int r2 = mprotect(b + GUEST_RESERVE - 64 * 1024 - (uint64_t)hp, (size_t)hp, PROT_READ | PROT_WRITE);
	int e2 = r2 ? errno : 0;
	if (r1 == 0 && r2 == 0) {
		b[0] = 0x5a;
		b[GUEST_RESERVE - 64 * 1024 - 1] = 0xa5;
		ok = b[0] == 0x5a && b[GUEST_RESERVE - 64 * 1024 - 1] == 0xa5;
	} else {
		ok = 0;
	}
	verdict("T1 vm", ok, "reserve 4GiB+64KiB at %p..%p hostpage=%ld mprotect=%d/%d errno=%d/%d (%s/%s); 4KiB-unaligned mprotect=%d errno=%d (%s)",
	        p, (void *)(b + GUEST_RESERVE), hp, r1, r2, e1, e2, e1 ? strerror(e1) : "ok", e2 ? strerror(e2) : "ok",
	        r0, e0, e0 ? strerror(e0) : "ok");
	munmap(p, (size_t)GUEST_RESERVE);
	return ok;
}

/* ------------------------------------------------------------------------------------------------ T2 tls */

static _Thread_local uint64_t tls_word = 7;
static _Thread_local char tls_big[256];

static void *t2_thread(void *arg)
{
	uintptr_t id = (uintptr_t)arg;
	int bad = 0;
	if (tls_word != 7) bad = 1;                 /* initial image copied per thread */
	for (unsigned i = 0; i < 100000; i++) {
		tls_word = id * 1000003u + i;
		tls_big[i & 255] = (char)id;
		if ((i & 4095) == 0) sched_yield();
		if (tls_word != id * 1000003u + i) { bad = 1; break; }
	}
	for (unsigned i = 0; i < 256; i++) if (tls_big[i] != (char)id) bad = 1;
	return (void *)(uintptr_t)bad;
}

static int t2_tls(void)
{
	pthread_t a, b;
	void *ra = (void *)1, *rb = (void *)1;
	int ok = pthread_create(&a, NULL, t2_thread, (void *)1) == 0 && pthread_create(&b, NULL, t2_thread, (void *)2) == 0;
	if (ok) { pthread_join(a, &ra); pthread_join(b, &rb); }
	ok = ok && ra == NULL && rb == NULL && tls_word == 7;
	verdict("T2 tls", ok, "two threads x 100000 _Thread_local writes, main still %llu", (unsigned long long)tls_word);
	return ok;
}

/* ----------------------------------------------------------------------------------------- T3/T4 interp */

/* Assembled with llvm-mc -triple=armv7a-none-eabi -mattr=+thumb2 (source in the comment); loaded at 0x10000.
 *   0x00 loop_a: push {r4,lr}; mov r3,#0x1000; orr r3,r3,#0x234
 *        1: add r3,r3,r0; eor r3,r3,r3,lsl #3; str r3,[r1]; ldr r4,[r1]; add r3,r3,r4,ror #7; subs r0,r0,#1; bne 1b
 *        mov r0,r3; pop {r4,pc}
 *   0x30 fault_a: ldr r0,[r0]; bx lr
 *   0x38 loop_t (Thumb): push {r4,lr}; movw r3,#0x5678
 *        1: add r3,r0; eor.w r3,r3,r3,lsl #5; str r3,[r1,#4]; ldr r4,[r1,#4]; add.w r3,r3,r4,ror #11; subs r0,#1; bne 1b
 *        mov r0,r3; pop {r4,pc} */
static const uint8_t guest_code[84] = {
	0x10, 0x40, 0x2d, 0xe9, 0x01, 0x3a, 0xa0, 0xe3, 0x8d, 0x3f, 0x83, 0xe3, 0x00, 0x30, 0x83, 0xe0, 0x83, 0x31, 0x23, 0xe0,
	0x00, 0x30, 0x81, 0xe5, 0x00, 0x40, 0x91, 0xe5, 0xe4, 0x33, 0x83, 0xe0, 0x01, 0x00, 0x50, 0xe2, 0xf8, 0xff, 0xff, 0x1a,
	0x03, 0x00, 0xa0, 0xe1, 0x10, 0x80, 0xbd, 0xe8, 0x00, 0x00, 0x90, 0xe5, 0x1e, 0xff, 0x2f, 0xe1, 0x10, 0xb5, 0x45, 0xf2,
	0x78, 0x63, 0x03, 0x44, 0x83, 0xea, 0x43, 0x13, 0x4b, 0x60, 0x4c, 0x68, 0x03, 0xeb, 0xf4, 0x23, 0x01, 0x38, 0xf6, 0xd1,
	0x18, 0x46, 0x10, 0xbd,
};
#define CODE_VA   0x00010000u
#define LOOP_A    (CODE_VA + 0x00u)
#define FAULT_A   (CODE_VA + 0x30u)
#define LOOP_T    (CODE_VA + 0x38u + 1u)
#define FAULT_VA  0x30000000u          /* never mapped */

static uint32_t ror32(uint32_t v, unsigned s) { return (v >> s) | (v << (32 - s)); }
static uint32_t native_a(uint32_t n)
{
	uint32_t r3 = 0x1234;
	for (; n; n--) { r3 += n; r3 ^= r3 << 3; r3 += ror32(r3, 7); }
	return r3;
}
static uint32_t native_t(uint32_t n)
{
	uint32_t r3 = 0x5678;
	for (; n; n--) { r3 += n; r3 ^= r3 << 5; r3 += ror32(r3, 11); }
	return r3;
}

struct ithread {
	struct n2a32_mem *m;
	unsigned idx;
	uint32_t iters;
	uint32_t stack_top, buf;
	/* results */
	uint32_t ra, rt, fault_addr;
	int fault_sig, fault_code;
	uintptr_t fault_host;
	enum n2a32_stop fault_stop, after_stop;
	uint32_t after;
	uint64_t icount, us;
	int err;
};

static void *interp_thread(void *arg)
{
	struct ithread *t = (struct ithread *)arg;
	struct n2a32_cpu c;
	n2a32_cpu_init(&c, t->m);
	c.r[13] = t->stack_top;
	uint64_t t0 = now_us();
	uint32_t args[2] = { t->iters, t->buf };
	t->ra = n2a32_call(&c, LOOP_A, args, 2, NULL);
	if (c.stop != N2A32_STOP_RETURN && c.stop != N2A32_STOP_NONE) { t->err = 1; }
	t->rt = n2a32_call(&c, LOOP_T, args, 2, NULL);
	if (c.stop != N2A32_STOP_RETURN && c.stop != N2A32_STOP_NONE) { t->err |= 2; }
	t->us = now_us() - t0;
	t->icount = c.icount;
	/* T4: fault, then the same cpu must work again */
	uint32_t fargs[1] = { FAULT_VA + t->idx * 0x100u };
	(void)n2a32_call(&c, FAULT_A, fargs, 1, NULL);
	t->fault_stop = c.stop;
	t->fault_addr = c.stop_addr;
	n2a32_last_fault(&t->fault_sig, &t->fault_code, &t->fault_host);
	c.r[13] = t->stack_top;               /* a fault leaves the stack where the guest was */
	c.stop = N2A32_STOP_NONE;
	uint32_t a2[2] = { 1000u, t->buf };
	t->after = n2a32_call(&c, LOOP_A, a2, 2, NULL);
	t->after_stop = c.stop;
	return NULL;
}

static int t3_t4_interp(uint32_t iters)
{
	struct n2a32_mem *m = n2a32_mem_create();
	if (!m) {
		verdict("T3 interp", 0, "n2a32_mem_create failed (the 4 GiB reservation; see T1) errno=%d", errno);
		verdict("T4 fault", -1, "no guest memory");
		return 0;
	}
	int ok = n2a32_mem_map(m, CODE_VA, 0x1000, 1) == 0;
	if (ok) {
		memcpy(n2a32_ptr(m, CODE_VA), guest_code, sizeof guest_code);
		ok = n2a32_mem_protect(m, CODE_VA, 0x1000, 0) == 0;
	}
	struct ithread th[2];
	memset(th, 0, sizeof th);
	for (unsigned i = 0; i < 2 && ok; i++) {
		uint32_t stk = n2a32_mem_alloc(m, 0x10000, 0x1000);
		uint32_t buf = n2a32_mem_alloc(m, 0x1000, 0x1000);
		if (!stk || !buf) { ok = 0; break; }
		th[i].m = m; th[i].idx = i; th[i].iters = iters; th[i].stack_top = stk + 0x10000u; th[i].buf = buf;
	}
	if (!ok) {
		verdict("T3 interp", 0, "guest map/alloc failed");
		verdict("T4 fault", -1, "no guest memory");
		return 0;
	}
	pthread_t p[2];
	uint64_t t0 = now_us();
	int started = 0;
	for (unsigned i = 0; i < 2; i++) if (pthread_create(&p[i], NULL, interp_thread, &th[i]) == 0) started++;
	for (int i = 0; i < started; i++) pthread_join(p[i], NULL);
	uint64_t wall = now_us() - t0;
	uint32_t ea = native_a(iters), et = native_t(iters);
	int good = started == 2;
	uint64_t insns = 0;
	for (unsigned i = 0; i < 2; i++) {
		good = good && th[i].err == 0 && th[i].ra == ea && th[i].rt == et;
		insns += th[i].icount;
	}
	double mips = wall ? (double)insns / (double)wall : 0.0;
	verdict("T3 interp", good, "A32 %08x/%08x T32 %08x/%08x (want %08x %08x) insns=%llu wall=%llums 2 threads %.1f MIPS (1 thread %.1f)",
	        th[0].ra, th[1].ra, th[0].rt, th[1].rt, ea, et, (unsigned long long)insns, (unsigned long long)(wall / 1000u), mips,
	        th[0].us ? (double)th[0].icount / (double)th[0].us : 0.0);
	int fok = started == 2;
	for (unsigned i = 0; i < 2; i++) {
		fok = fok && th[i].fault_stop == N2A32_STOP_FAULT && th[i].fault_addr == FAULT_VA + i * 0x100u &&
		      th[i].after == native_a(1000u) && (th[i].after_stop == N2A32_STOP_RETURN || th[i].after_stop == N2A32_STOP_NONE);
	}
	verdict("T4 fault", fok, "stop=%d/%d addr=%08x/%08x sig=%d/%d si_code=%d/%d host=%p/%p then rerun=%s/%s",
	        (int)th[0].fault_stop, (int)th[1].fault_stop, th[0].fault_addr, th[1].fault_addr, th[0].fault_sig,
	        th[1].fault_sig, th[0].fault_code, th[1].fault_code, (void *)th[0].fault_host, (void *)th[1].fault_host,
	        th[0].after == native_a(1000u) ? "ok" : "BAD", th[1].after == native_a(1000u) ? "ok" : "BAD");
	n2a32_mem_destroy(m);
	return good && fok;
}

/* ------------------------------------------------------------------------------------------------ T5 vendor */

static int t5_vendor(const char *vendor, unsigned seconds)
{
	struct stat st;
	char probe[600];
	snprintf(probe, sizeof probe, "%s/lib/libvcodecdrv.so", vendor);
	if (stat(probe, &st) != 0) {
		verdict("T5 vendor", -1, "%s absent (vendor objects not installed)", probe);
		return -1;
	}
	struct n2vc_hw hw;
	memset(&hw, 0, sizeof hw);
	if (n2vc_hw_open_fake(&hw, NULL) != 0) {
		verdict("T5 vendor", 0, "n2vc_hw_open_fake failed");
		return 0;
	}
	struct n2vhost *h = NULL;
	uint64_t t0 = now_us();
	usage_say("T5 before n2vhost_start");
	int r = n2vhost_start(&h, &hw, vendor);
	uint64_t ms = (now_us() - t0) / 1000u;
	usage_say("T5 after n2vhost_start");
	say("T5 vendor n2vhost_start=%d in %llums unresolved=%u error=%s", r, (unsigned long long)ms, h ? n2vhost_unresolved(h) : 0u,
	    h ? n2vhost_error(h) : "-");
	/* let vpud's guest main run up to its first /dev/vcu ioctl (M-c).  n2vhost decides M-c from the /dev/vcu statistics when
	 * the report is made (host run: n2vhost_milestone() said M-b until then), so poll through the report. */
	unsigned ms_reached = h ? n2vhost_milestone(h) : 0;
	FILE *devnull = fopen("/dev/null", "w");
	for (unsigned waited = 0; h && r >= 0 && devnull && waited < seconds * 2u && ms_reached < N2VHOST_MS_C; waited++) {
		struct timespec ts = { 0, 500000000 };
		nanosleep(&ts, NULL);
		n2vhost_report(h, devnull);
		ms_reached = n2vhost_milestone(h);
	}
	if (devnull) fclose(devnull);
	if (h) {
		/* the runner's own report, line by line, through say() so it reaches the console */
		char *text = NULL;
		size_t len = 0;
		FILE *mf = open_memstream(&text, &len);
		if (mf) {
			n2vhost_report(h, mf);
			fclose(mf);
			unsigned lines = 0;
			for (char *s = text, *e; s && *s && lines < 60; s = e ? e + 1 : NULL, lines++) {
				e = strchr(s, '\n');
				if (e) *e = 0;
				if (*s) say("T5 %.280s", s);
			}
			free(text);
		}
		ms_reached = n2vhost_milestone(h);
		for (unsigned k = N2VHOST_MS_A; k <= N2VHOST_MS_C; k++)
			say("T5 milestone %s %s: %.200s", n2vhost_milestone_name(k), ms_reached >= k ? "reached" : "not reached",
			    n2vhost_milestone_evidence(h, k) ? n2vhost_milestone_evidence(h, k) : "-");
	}
	int ok = r >= 0 && ms_reached >= N2VHOST_MS_C;
	verdict("T5 vendor", ok, "n2vhost_start=%d milestone=%s stop=%s", r, n2vhost_milestone_name(ms_reached),
	        h ? n2vhost_stop_text(h) : "-");
	/* n2vhost_stop() may leave the guest thread detached (documented); the process exits right after anyway. */
	if (h) n2vhost_stop(h);
	return ok;
}

/* ------------------------------------------------------------------------------------------------------ main */

int main(int argc, char **argv)
{
	const char *gate = NULL, *vendor = "/usr/local/lib/n2vendor", *outdir = "/private/var/db/nord2-vcodec";
	int keep_gate = 0, no_console = 0, skip_vendor = 0;
	uint32_t iters = 2000000u;
	unsigned vendor_s = 20;
	g_t0 = now_us();
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "--gate") && i + 1 < argc) gate = argv[++i];
		else if (!strcmp(a, "--keep-gate")) keep_gate = 1;
		else if (!strcmp(a, "--vendor") && i + 1 < argc) vendor = argv[++i];
		else if (!strcmp(a, "--out") && i + 1 < argc) outdir = argv[++i];
		else if (!strcmp(a, "--iters") && i + 1 < argc) iters = (uint32_t)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(a, "--vendor-seconds") && i + 1 < argc) vendor_s = (unsigned)strtoul(argv[++i], NULL, 0);
		else if (!strcmp(a, "--no-console")) no_console = 1;
		else if (!strcmp(a, "--skip-vendor")) skip_vendor = 1;
		else { fprintf(stderr, "usage: n2vcd_selftest [--gate PATH] [--keep-gate] [--vendor DIR] [--out DIR] [--iters N]\n"
		                       "                      [--vendor-seconds S] [--no-console] [--skip-vendor]\n"); return 2; }
	}
	if (!no_console) g_console = open("/dev/console", O_WRONLY | O_NOCTTY | O_NONBLOCK);
	if (gate && access(gate, F_OK) != 0) {
		say("gate %s absent: not running", gate);
		return 0;
	}
	if (iters == 0) iters = 1;
	char path[600];
	(void)mkdir(outdir, 0755);
	snprintf(path, sizeof path, "%s/selftest-%d.txt", outdir, (int)getpid());
	g_sum = fopen(path, "w");
	say("start pid=%d uid=%d iters=%u vendor=%s summary=%s", (int)getpid(), (int)getuid(), iters, vendor, g_sum ? path : "-");

	install_crash_handler();
	n2a32_mem_set_log(mem_say, 2);
	usage_say("start");
	{
		char ml[400];
		int lr = n2ml_raise(3072, ml, sizeof ml);
		say("T0 jetsam %s %s", lr == 0 ? "raised" : "NOT raised", ml);
	}
	usage_say("after-memlimit");
	t1_vm();
	usage_say("after-T1");
	t2_tls();
	usage_say("after-T2");
	t3_t4_interp(iters);
	usage_say("after-T3T4");
	if (skip_vendor) verdict("T5 vendor", -1, "--skip-vendor");
	else t5_vendor(vendor, vendor_s);
	usage_say("after-T5");

	say("DONE pass=%u fail=%u skip=%u", g_pass, g_fail, g_skip);
	if (gate && !keep_gate) {
		if (unlink(gate) == 0) say("gate %s removed (one run per installation)", gate);
	}
	if (g_sum) fclose(g_sum);
	/* _exit: the hosted vpud thread of T5 may still be running inside the interpreter (n2vhost_stop detaches it). */
	_exit(g_fail ? 1 : 0);
}
