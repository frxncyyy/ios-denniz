/* SPDX-License-Identifier: MIT
 * n2trace.c -- safety net for the libmnl bring-up (gnssd2):
 *  1. import-call trace: every libmnl GOT slot points to a 2-instruction thunk (n2shim_va.S _n2t_thunks) instead of the
 *     shim. While the trace window is open (first N seconds after mtk_gps_sys_function_register; --trace-seconds, default
 *     10) each call is logged to n2mnld.log BEFORE the shim runs ("> name(x0, x1, x2) @mnl+caller tid") and its return
 *     value after ("< name = x0"); each line is written with write(2) at once, so a crash leaves the full sequence.
 *     Limits: 120 logged calls per import, 30000 lines in total; a per-import count summary when the window closes.
 *     Outside the window the thunk costs one load + one branch (n2t_active == 0 -> tail jump to the shim).
 *     The thunks never move sp, so Linux variadic callers' stacked arguments still reach the V() trampolines.
 *  2. SIGSEGV/SIGBUS/SIGILL/SIGFPE handler (on an alternate stack for the main thread): logs pc/lr/sp/fp/far with
 *     libmnl offsets, x0..x28, a frame-pointer backtrace and the last traced import of the thread, then restores the
 *     default action and returns, so the fault repeats and the normal crash report is still written.
 */
#include "n2sys.h"

#define N2T_MAX 160
#define N2T_CAP 120
#define N2T_LINES 30000
extern char n2t_thunks[], n2t_ret[], n2mnl_base[];
extern char mh_header[] __asm__("__mh_execute_header");
extern pthread_t pthread_self(void);
void *n2t_real[N2T_MAX];                       /* read by the asm fast path: n2t_real[i] for thunk i */
volatile int n2t_active;                       /* idem */
static const char *n2t_name[N2T_MAX];
static unsigned n2t_cnt[N2T_MAX]; static int n2t_n;
static uint64_t n2t_until, n2t_t0; static long n2t_lines; static int n2t_summary_done;

static uint64_t ms(void) { struct timespec t; clock_gettime(D_CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u; }

void *n2_trace_wrap(const char *name, void *real)
{
	for (int i = 0; i < n2t_n; i++) if (n2t_real[i] == real && !strcmp(n2t_name[i], name)) return n2t_thunks + 8 * i;
	if (n2t_n >= N2T_MAX) return real;
	n2t_real[n2t_n] = real; n2t_name[n2t_n] = name;
	return n2t_thunks + 8 * n2t_n++;
}
void n2_trace_start(int seconds)
{
	if (seconds <= 0) return;
	n2t_t0 = ms(); n2t_until = n2t_t0 + (uint64_t)seconds * 1000u;
	__atomic_store_n(&n2t_active, 1, __ATOMIC_RELEASE);
	n2_log("import trace on for %d s (%d thunks; lines '> name(args) @mnl+caller' / '< name = ret' in n2mnld.log)", seconds, n2t_n);
}
void n2_trace_summary(int force)
{
	if (n2t_summary_done || !n2t_until || (!force && ms() < n2t_until)) return;
	n2t_summary_done = 1; __atomic_store_n(&n2t_active, 0, __ATOMIC_RELEASE);
	char b[1100]; int k = 0;
	for (int i = 0; i < n2t_n && k < (int)sizeof b - 40; i++) if (n2t_cnt[i]) k += snprintf(b + k, sizeof b - (size_t)k, " %s:%u", n2t_name[i], n2t_cnt[i]);
	n2_log("import trace off: %ld lines; calls per import:%s", n2t_lines, k ? b : " none");
}

/* per-thread return stack (the thunk replaces lr with n2t_ret; n2t_leave pops the real lr) */
struct tstk { int depth; int last; uint64_t lr[32]; short idx[32]; };
static unsigned long tkey; static int tkey_ok;
static struct tstk *tst(void)
{
	if (!tkey_ok) return 0;
	struct tstk *s = pthread_getspecific(tkey);
	if (!s) { s = calloc(1, sizeof *s); if (s) { s->last = -1; pthread_setspecific(tkey, s); } }
	return s;
}
static int is_str_import(const char *n)
{
	static const char *const l[] = { "fopen", "access", "stat", "__open_2", "opendir", "mkdir", "remove", "rename", "chmod",
		"property_get", "printf", "puts", "fputs", "atoi", "atof", "strtoul", "socket", 0 };
	for (int i = 0; l[i]; i++) if (!strcmp(n, l[i])) return 1;
	return 0;
}
static int noreturn_import(const char *n) { return !strcmp(n, "exit") || !strcmp(n, "abort") || !strcmp(n, "pthread_exit") || !strcmp(n, "__stack_chk_fail"); }
static const char *sp_(uint64_t p) { return p >= 0x100000000ull && p < 0x300000000000ull ? (const char *)(uintptr_t)p : 0; }
static void caller(uint64_t lr, char *b, size_t n)
{
	uint64_t base = (uint64_t)(uintptr_t)n2mnl_base;
	if (lr >= base && lr < base + 0x6b4db4u) snprintf(b, n, "mnl+%#llx", (unsigned long long)(lr - base));
	else snprintf(b, n, "%#llx", (unsigned long long)lr);
}

struct n2t_pair { void *fn; void *lr; };
struct n2t_pair n2t_enter(uint64_t *regs, char *thunk, uint64_t lr)
{
	int i = (int)((thunk - n2t_thunks) / 8);
	struct n2t_pair r = { n2t_real[i], (void *)(uintptr_t)lr };
	if (ms() >= n2t_until) { __atomic_store_n(&n2t_active, 0, __ATOMIC_RELEASE); return r; }
	unsigned c = __atomic_add_fetch(&n2t_cnt[i], 1, __ATOMIC_RELAXED);
	struct tstk *s = tst();
	if (s) s->last = i;
	if (c > N2T_CAP || n2t_lines >= N2T_LINES) return r;
	__atomic_add_fetch(&n2t_lines, 1, __ATOMIC_RELAXED);
	char cl[40]; caller(lr, cl, sizeof cl);
	const char *n = n2t_name[i];
	unsigned tid = (unsigned)((uintptr_t)pthread_self() >> 12) & 0xfffff;
	if (is_str_import(n)) {
		const char *a = sp_(regs[0]), *b = sp_(regs[1]);
		int s2 = !strcmp(n, "fopen") || !strcmp(n, "rename") || !strcmp(n, "printf") ? 1 : 0;
		if (!strcmp(n, "printf") || !strcmp(n, "puts")) b = 0, s2 = 0;
		n2_logq("> %s(\"%.80s\"%s%.40s%s, %#llx) @%s t%05x #%u", n, a ? a : "<bad ptr>", s2 ? ", \"" : "", s2 && b ? b : "", s2 ? "\"" : "",
		    (unsigned long long)regs[s2 ? 2 : 1], cl, tid, c);
	} else if (!strcmp(n, "sscanf") || !strcmp(n, "fscanf") || !strcmp(n, "fprintf")) {
		const char *f = sp_(regs[1]);
		n2_logq("> %s(%#llx, \"%.60s\") @%s t%05x #%u", n, (unsigned long long)regs[0], f ? f : "<bad ptr>", cl, tid, c);
	} else
		n2_logq("> %s(%#llx, %#llx, %#llx, %#llx) @%s t%05x #%u", n, (unsigned long long)regs[0], (unsigned long long)regs[1],
		    (unsigned long long)regs[2], (unsigned long long)regs[3], cl, tid, c);
	if (s && s->depth < 32 && !noreturn_import(n)) { s->lr[s->depth] = lr; s->idx[s->depth] = (short)i; s->depth++; r.lr = n2t_ret; }
	return r;
}
uint64_t n2t_leave(uint64_t ret)
{
	struct tstk *s = tst();
	if (!s || s->depth <= 0) { n2_log("FATAL import trace: return stack empty"); abort(); }
	s->depth--;
	int i = s->idx[s->depth];
	const char *n = n2t_name[i];
	if (!strcmp(n, "fgets") || !strcmp(n, "strerror") || !strcmp(n, "strstr") || !strcmp(n, "strchr") || !strcmp(n, "strrchr") || !strcmp(n, "strtok")) {
		const char *p = sp_(ret);
		n2_logq("< %s = %#llx%s%.60s%s", n, (unsigned long long)ret, p ? " \"" : "", p ? p : "", p ? "\"" : "");
	} else
		n2_logq("< %s = %#llx", n, (unsigned long long)ret);
	return s->lr[s->depth];
}

/* ------------------------------------------------------------------ fault handler */
struct d_sigaction { void *handler; uint32_t mask; int flags; };
struct d_stack { void *sp; size_t size; int flags; };
extern int sigaction(int, const struct d_sigaction *, struct d_sigaction *);
extern int sigaltstack(const struct d_stack *, struct d_stack *);
void n2_log_raw(const char *s, size_t n);      /* n2mnld.c: write to n2mnld.log + console, no lock */
static void off(uint64_t a, char *b, size_t n)
{
	uint64_t base = (uint64_t)(uintptr_t)n2mnl_base, mh = (uint64_t)(uintptr_t)mh_header;
	if (a >= base && a < base + 0x6b4db4u) snprintf(b, n, "%#llx(mnl+%#llx)", (unsigned long long)a, (unsigned long long)(a - base));
	else if (a >= mh && a < base) snprintf(b, n, "%#llx(n2mnld+%#llx)", (unsigned long long)a, (unsigned long long)(a - mh));
	else snprintf(b, n, "%#llx", (unsigned long long)a);
}
static void fault(int sig, void *si, void *uc)
{
	static volatile int busy;
	if (__atomic_exchange_n(&busy, 1, __ATOMIC_ACQ_REL)) return;
	const unsigned char *mc = *(unsigned char *const *)((const char *)uc + 48);   /* ucontext.uc_mcontext */
	uint64_t far, x[29], fp, lr, sp, pc; uint32_t esr; int code; void *addr;
	memcpy(&far, mc, 8); memcpy(&esr, mc + 8, 4); memcpy(x, mc + 16, sizeof x);
	memcpy(&fp, mc + 248, 8); memcpy(&lr, mc + 256, 8); memcpy(&sp, mc + 264, 8); memcpy(&pc, mc + 272, 8);
	memcpy(&code, (const char *)si + 8, 4); memcpy(&addr, (const char *)si + 24, 8);
	char b[1400], p1[48], p2[48]; int k;
	off(pc, p1, sizeof p1); off(lr, p2, sizeof p2);
	k = snprintf(b, sizeof b, "N2MNLD FAULT sig %d code %d addr %p far %#llx esr %#x pc %s lr %s sp %#llx fp %#llx", sig, code, addr,
	    (unsigned long long)far, esr, p1, p2, (unsigned long long)sp, (unsigned long long)fp);
	struct tstk *s = tkey_ok ? pthread_getspecific(tkey) : 0;
	if (s && s->last >= 0) k += snprintf(b + k, sizeof b - (size_t)k, "; last import of this thread: %s (pending %d)", n2t_name[s->last], s->depth);
	n2_log_raw(b, (size_t)k);
	k = snprintf(b, sizeof b, "N2MNLD FAULT regs");
	for (int i = 0; i < 29; i++) k += snprintf(b + k, sizeof b - (size_t)k, " x%d=%llx", i, (unsigned long long)x[i]);
	n2_log_raw(b, (size_t)k);
	k = snprintf(b, sizeof b, "N2MNLD FAULT fp-chain");
	uint64_t f = fp;
	for (int i = 0; i < 16 && f && !(f & 7) && f >= sp && f - sp < (8u << 20); i++) {
		uint64_t nf, ra; memcpy(&nf, (void *)(uintptr_t)f, 8); memcpy(&ra, (void *)(uintptr_t)(f + 8), 8);
		off(ra, p1, sizeof p1); k += snprintf(b + k, sizeof b - (size_t)k, " %s", p1);
		if (nf <= f) break;
		f = nf;
	}
	n2_log_raw(b, (size_t)k);
	struct d_sigaction d = { 0, 0, 0 }; sigaction(sig, &d, 0);   /* SIG_DFL; returning re-executes the fault -> crash report */
}
void n2_fault_install(void)
{
	if (!pthread_key_create(&tkey, 0)) tkey_ok = 1;
	static char alt[64 * 1024]; struct d_stack st = { alt, sizeof alt, 0 }; sigaltstack(&st, 0);
	struct d_sigaction a = { (void *)fault, 0, 0x40 | 0x1 };    /* SA_SIGINFO | SA_ONSTACK */
	sigaction(11, &a, 0); sigaction(10, &a, 0); sigaction(4, &a, 0); sigaction(8, &a, 0);  /* SEGV BUS ILL FPE */
}
