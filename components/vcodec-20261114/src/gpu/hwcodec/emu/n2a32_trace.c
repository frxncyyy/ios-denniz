/* SPDX-License-Identifier: MIT */
/*
 * n2a32_trace.c -- compact instruction/host-call/MMIO trace for the AArch32 interpreter.
 *
 * The three hooks declared in include/n2a32.h write one short line per event into a global
 * ring buffer that keeps the last N2A32_TRACE_RING events.  Nothing is printed by the hooks:
 * a front end calls n2a32_trace_dump() when n2a32_run() returns.  If N2HW_TRACE names a file,
 * every event is also appended to that file as it happens (the full trace); the file is
 * opened on the first n2a32_trace_attach() and closed by n2a32_trace_close()/atexit.
 *
 * Addresses are symbolised through n2elf_describe() when the loader is linked and a set has
 * been registered with n2a32_trace_set_elf().  The reference is weak, so a front end that
 * does not link the loader still works.
 *
 * Thread safety: several guest CPUs may share one trace state (c->trace points at it).  The
 * ring and the trace file are protected by a small C11 spin lock; the file is line buffered.
 * No dynamic allocation and no printing unless a file was explicitly requested.
 */
#include "n2a32.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct n2elf_set;

#if defined(__APPLE__)
#define N2_TRACE_WEAK __attribute__((weak_import))
#else
#define N2_TRACE_WEAK __attribute__((weak))
#endif
extern const char *n2elf_describe(struct n2elf_set *s, uint32_t addr, char *buf, size_t len) N2_TRACE_WEAK;

#define N2A32_TRACE_RING 4096
#define N2A32_TRACE_LINE 88

struct n2a32_trace_state {
	char line[N2A32_TRACE_RING][N2A32_TRACE_LINE];
	uint64_t seq;                    /* events ever written */
	uint64_t insn, hostcall, mmio;   /* per-kind counters (saturating not needed) */
	_Atomic(struct n2elf_set *) elf;
	_Atomic unsigned attached;       /* CPUs with c->trace pointing here */
	_Atomic int regs;                /* also record a register line before each insn */
	_Atomic int dregs;               /* also record d16..d23 before each insn */
	FILE *file;
	int env_checked;
};

static struct n2a32_trace_state g_trace;
static atomic_flag g_lock = ATOMIC_FLAG_INIT;

static void lock(void)
{
	while (atomic_flag_test_and_set_explicit(&g_lock, memory_order_acquire))
		;
}

static void unlock(void)
{
	atomic_flag_clear_explicit(&g_lock, memory_order_release);
}

/* The frozen header deliberately exposes no trace configuration, so the small extension API
 * lives in tests/emu/n2a32_trace.h.  Keep the symbols here so every front end links them. */
void n2a32_trace_set_elf(struct n2elf_set *s);
void n2a32_trace_regs(int on);
void n2a32_trace_dregs(int on);
void n2a32_trace_attach(struct n2a32_cpu *c);
void n2a32_trace_detach(struct n2a32_cpu *c);
void n2a32_trace_clear(void);
void n2a32_trace_dump(struct n2a32_cpu *c, FILE *out);
uint64_t n2a32_trace_events(void);
int n2a32_trace_open(const char *path);
void n2a32_trace_close(void);

static const char *stop_name(enum n2a32_stop s)
{
	switch (s) {
	case N2A32_STOP_NONE: return "NONE";
	case N2A32_STOP_RETURN: return "RETURN";
	case N2A32_STOP_UNDEF: return "UNDEF";
	case N2A32_STOP_FAULT: return "FAULT";
	case N2A32_STOP_SVC: return "SVC";
	case N2A32_STOP_BKPT: return "BKPT";
	case N2A32_STOP_LIMIT: return "LIMIT";
	case N2A32_STOP_REQUEST: return "REQUEST";
	case N2A32_STOP_NOIMPORT: return "NOIMPORT";
	}
	return "?";
}

static int open_env_locked(void)
{
	const char *p;

	if (g_trace.file || g_trace.env_checked)
		return g_trace.file != NULL;
	g_trace.env_checked = 1;
	p = getenv("N2HW_TRACE");
	if (!p || !*p)
		return 0;
	g_trace.file = fopen(p, "w");
	if (!g_trace.file)
		return 0;
	setvbuf(g_trace.file, NULL, _IOLBF, 16384);
	return 1;
}

int n2a32_trace_open(const char *path)
{
	FILE *f;

	if (!path || !*path)
		return -1;
	f = fopen(path, "w");
	if (!f)
		return -1;
	setvbuf(f, NULL, _IOLBF, 16384);
	lock();
	if (g_trace.file)
		fclose(g_trace.file);
	g_trace.file = f;
	g_trace.env_checked = 1;
	unlock();
	return 0;
}

void n2a32_trace_close(void)
{
	lock();
	if (g_trace.file) {
		fflush(g_trace.file);
		fclose(g_trace.file);
		g_trace.file = NULL;
	}
	unlock();
}

void n2a32_trace_set_elf(struct n2elf_set *s)
{
	atomic_store_explicit(&g_trace.elf, s, memory_order_release);
}

/* Debug aid: emit "r <r0..r6,r12,lr> <flags>" before each instruction line.  Off by default;
 * the extra lines count as events in the ring. */
void n2a32_trace_regs(int on)
{
	atomic_store_explicit(&g_trace.regs, on != 0, memory_order_relaxed);
}

void n2a32_trace_dregs(int on)
{
	atomic_store_explicit(&g_trace.dregs, on != 0, memory_order_relaxed);
}

void n2a32_trace_attach(struct n2a32_cpu *c)
{
	if (!c)
		return;
	lock();
	(void)open_env_locked();
	unlock();
	if (c->trace != &g_trace) {
		c->trace = &g_trace;
		atomic_fetch_add_explicit(&g_trace.attached, 1u, memory_order_relaxed);
	}
}

void n2a32_trace_detach(struct n2a32_cpu *c)
{
	if (c && c->trace == &g_trace) {
		c->trace = NULL;
		atomic_fetch_sub_explicit(&g_trace.attached, 1u, memory_order_relaxed);
	}
}

void n2a32_trace_clear(void)
{
	lock();
	g_trace.seq = 0;
	g_trace.insn = g_trace.hostcall = g_trace.mmio = 0;
	memset(g_trace.line, 0, sizeof(g_trace.line));
	unlock();
}

uint64_t n2a32_trace_events(void)
{
	uint64_t n;

	lock();
	n = g_trace.seq;
	unlock();
	return n;
}

/* Format one event, put it into the ring and, if enabled, into the full-trace file.  Formatting
 * happens outside the lock; only the copy and the file write are serialised. */
static void record(char kind, const char *fmt, ...)
{
	char buf[N2A32_TRACE_LINE];
	va_list ap;
	int n;

	/* MMIO hooks have no CPU and the core calls them unconditionally; do not fill the ring
	 * when nobody is tracing. */
	if (atomic_load_explicit(&g_trace.attached, memory_order_relaxed) == 0)
		return;
	buf[0] = kind;
	buf[1] = ' ';
	va_start(ap, fmt);
	n = vsnprintf(buf + 2, sizeof(buf) - 2, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n >= sizeof(buf) - 2)
		n = (int)sizeof(buf) - 3;

	lock();
	g_trace.line[g_trace.seq % N2A32_TRACE_RING][0] = '\0';
	memcpy(g_trace.line[g_trace.seq % N2A32_TRACE_RING], buf, (size_t)n + 3);
	if (kind == 'i')
		g_trace.insn++;
	else if (kind == 'h')
		g_trace.hostcall++;
	else if (kind == 'm')
		g_trace.mmio++;
	if (g_trace.file) {
		fwrite(buf, 1, (size_t)n + 2, g_trace.file);
		fputc('\n', g_trace.file);
	}
	g_trace.seq++;
	unlock();
}

static void describe(uint32_t addr, char *buf, size_t len)
{
	struct n2elf_set *s = atomic_load_explicit(&g_trace.elf, memory_order_acquire);

	buf[0] = '-';
	buf[1] = '\0';
	if (s && n2elf_describe)
		(void)n2elf_describe(s, addr, buf, len);
}

void n2a32_trace_insn(struct n2a32_cpu *c, uint32_t addr, uint32_t insn, int thumb, int size)
{
	char sym[48];

	describe(addr, sym, sizeof(sym));
	if (atomic_load_explicit(&g_trace.regs, memory_order_relaxed) && c != NULL) {
		record('r', "%08x %08x %08x %08x %08x %08x %08x %08x %u%u%u%u%u",
			c->r[0], c->r[1], c->r[2], c->r[3], c->r[4], c->r[5], c->r[6],
			c->r[12], c->n, c->z, c->cf, c->v, c->q);
		record('r', "lr=%08x sp=%08x ge=%x", c->r[14], c->r[13], c->ge);
	}
	if (atomic_load_explicit(&g_trace.dregs, memory_order_relaxed) && c != NULL) {
		record('d', "0=%016llx 1=%016llx 2=%016llx 3=%016llx",
			(unsigned long long)c->d[0].u64, (unsigned long long)c->d[1].u64,
			(unsigned long long)c->d[2].u64, (unsigned long long)c->d[3].u64);
		record('d', "4=%016llx 5=%016llx 6=%016llx 7=%016llx",
			(unsigned long long)c->d[4].u64, (unsigned long long)c->d[5].u64,
			(unsigned long long)c->d[6].u64, (unsigned long long)c->d[7].u64);
		record('d', "8=%016llx 9=%016llx 10=%016llx 11=%016llx",
			(unsigned long long)c->d[8].u64, (unsigned long long)c->d[9].u64,
			(unsigned long long)c->d[10].u64, (unsigned long long)c->d[11].u64);
		record('d', "12=%016llx 13=%016llx 14=%016llx 15=%016llx",
			(unsigned long long)c->d[12].u64, (unsigned long long)c->d[13].u64,
			(unsigned long long)c->d[14].u64, (unsigned long long)c->d[15].u64);
		record('d', "16=%016llx 17=%016llx 18=%016llx 19=%016llx",
			(unsigned long long)c->d[16].u64, (unsigned long long)c->d[17].u64,
			(unsigned long long)c->d[18].u64, (unsigned long long)c->d[19].u64);
		record('d', "20=%016llx 21=%016llx 22=%016llx 23=%016llx",
			(unsigned long long)c->d[20].u64, (unsigned long long)c->d[21].u64,
			(unsigned long long)c->d[22].u64, (unsigned long long)c->d[23].u64);
	}
	if (size == 2)
		record('i', "%c %08x %04x %s", thumb ? 'T' : 'A', addr, insn & 0xffffu, sym);
	else
		record('i', "%c %08x %08x %s", thumb ? 'T' : 'A', addr, insn, sym);
}

void n2a32_trace_hostcall(struct n2a32_cpu *c, const char *name, int leaving)
{
	(void)c;
	record('h', "%s %s", leaving ? "ret " : "call", name ? name : "?");
}

void n2a32_trace_mmio(struct n2a32_mem *m, uint32_t addr, unsigned bytes, uint32_t value, int write)
{
	char sym[48];

	(void)m;
	describe(addr, sym, sizeof(sym));
	record('m', "%c %08x %u %08x %s", write ? 'W' : 'R', addr, bytes, value, sym);
}

void n2a32_trace_dump(struct n2a32_cpu *c, FILE *out)
{
	uint64_t total, first;
	unsigned i, n;

	if (!out)
		out = stderr;

	lock();
	total = g_trace.seq;
	first = total > N2A32_TRACE_RING ? total - N2A32_TRACE_RING : 0;
	n = (unsigned)(total - first);
	for (i = 0; i < n; i++)
		fprintf(out, "%s\n", g_trace.line[(first + i) % N2A32_TRACE_RING]);
	fprintf(out, "# trace events=%llu insn=%llu hostcall=%llu mmio=%llu shown=%u\n",
		(unsigned long long)total, (unsigned long long)g_trace.insn,
		(unsigned long long)g_trace.hostcall, (unsigned long long)g_trace.mmio, n);
	if (c) {
		fprintf(out, "# stop=%s addr=%08x insn=%08x icount=%llu thumb=%u\n",
			stop_name(c->stop), c->stop_addr, c->stop_insn,
			(unsigned long long)c->icount, (unsigned)c->thumb);
	}
	if (g_trace.file)
		fflush(g_trace.file);
	unlock();
	fflush(out);
}
