/* SPDX-License-Identifier: MIT */
/*
 * n2perf_mix.c -- guest instruction-mix profiler (opus-perf).  Link it INSTEAD of emu/n2a32_trace.c: it
 * implements the core's trace hooks (n2a32.h) and T01's trace API (tests/emu/n2a32_trace.h) so any runner
 * that attaches a trace (bench, the IV host runner) counts instead of tracing.
 *
 * Counts, per phase (n2perf_mark() starts a new phase), every executed (thumb, encoding), every executed pc,
 * every host call by name and every MMIO access.  N2PERF_OUT=FILE (default /tmp/n2hw-perf/mix.txt) receives:
 *   M <phase> <label>                                  phase names
 *   I <phase> <thumb> <raw hex> <count> <fastop>       fastop: the predecoded fast path (0 = class function)
 *   P <phase> <pc hex> <count>
 *   H <phase> <name> <count>
 *   X <phase> <mmio reads> <mmio writes>
 * tests/perf/mix_report.py turns it into tables.  N2PERF_ALL=1 also counts threads that never attach.
 */
#define _POSIX_C_SOURCE 200809L
#include "n2a32.h"
#include "n2elf.h"
#include "n2a32_trace.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*class_fn)(struct n2a32_cpu *, uint32_t);
#include "n2a32_fast.h"          /* -I gpu/hwcodec/emu */

#define MAXPH 32
struct ent { uint64_t key; uint64_t n; };
struct tab { struct ent *e; size_t cap, used; };
struct hname { const char *name; uint32_t ph; uint64_t n; };

static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static struct tab tins, tpc;           /* shared, protected by mu (per-thread caches below batch the updates) */
static struct hname hn[8192];
static unsigned nhn;
static uint64_t mmio_n[MAXPH][2];
static char phname[MAXPH][64] = { "start" };
static volatile unsigned phase, nph = 1;
static int marker;
static int registered;

static void tab_add(struct tab *t, uint64_t key, uint64_t n)
{
	size_t i;

	if (t->used * 2 >= t->cap) {
		struct tab nt = { calloc(t->cap ? t->cap * 2 : 65536, sizeof(struct ent)), t->cap ? t->cap * 2 : 65536, 0 };
		if (!nt.e)
			return;
		for (i = 0; i < t->cap; i++)
			if (t->e[i].n)
				tab_add(&nt, t->e[i].key, t->e[i].n);
		free(t->e);
		*t = nt;
	}
	i = (size_t)((key * 0x9E3779B97F4A7C15ull) >> 20) & (t->cap - 1);
	while (t->e[i].n && t->e[i].key != key)
		i = (i + 1) & (t->cap - 1);
	if (!t->e[i].n) {
		t->e[i].key = key;
		t->used++;
	}
	t->e[i].n += n;
}

/* per-thread batching: a small direct-mapped cache of (key, count) flushed into the shared tables */
#define LC 4096
struct local { uint64_t ik[LC], in[LC], pk[LC], pn[LC]; };
static _Thread_local struct local *tl;

static void flush_local(struct local *l)
{
	pthread_mutex_lock(&mu);
	for (unsigned i = 0; i < LC; i++) {
		if (l->in[i]) { tab_add(&tins, l->ik[i], l->in[i]); l->in[i] = 0; }
		if (l->pn[i]) { tab_add(&tpc, l->pk[i], l->pn[i]); l->pn[i] = 0; }
	}
	pthread_mutex_unlock(&mu);
}

#define MAXT 256
static struct local *all_locals[MAXT];
static unsigned nlocals;

static struct local *get_local(void)
{
	if (!tl) {
		tl = calloc(1, sizeof(*tl));
		if (!tl)
			return NULL;
		pthread_mutex_lock(&mu);
		if (nlocals < MAXT)
			all_locals[nlocals++] = tl;
		pthread_mutex_unlock(&mu);
	}
	return tl;
}

static void dump(void)
{
	const char *path = getenv("N2PERF_OUT");
	FILE *f;
	size_t i;

	for (unsigned k = 0; k < nlocals; k++)
		flush_local(all_locals[k]);
	f = fopen(path ? path : "/tmp/n2hw-perf/mix.txt", "w");
	if (!f)
		return;
	pthread_mutex_lock(&mu);
	for (unsigned p = 0; p < nph && p < MAXPH; p++) {
		fprintf(f, "M %u %s\n", p, phname[p]);
		fprintf(f, "X %u %llu %llu\n", p, (unsigned long long)mmio_n[p][0], (unsigned long long)mmio_n[p][1]);
	}
	for (i = 0; i < tins.cap; i++) {
		if (tins.e[i].n) {
			uint64_t k = tins.e[i].key;
			uint32_t raw = (uint32_t)k, thumb = (uint32_t)(k >> 32) & 1u, ph = (uint32_t)(k >> 33);
			struct dent e;
			memset(&e, 0, sizeof(e));
			if (thumb)
				(raw >= 0xE8000000u ? fast_t32 : fast_t16)(&e, raw);
			fprintf(f, "I %u %u %08x %llu %u\n", ph, thumb, raw, (unsigned long long)tins.e[i].n, e.op);
		}
	}
	for (i = 0; i < tpc.cap; i++)
		if (tpc.e[i].n)
			fprintf(f, "P %u %08x %llu\n", (unsigned)(tpc.e[i].key >> 33), (uint32_t)tpc.e[i].key,
				(unsigned long long)tpc.e[i].n);
	for (i = 0; i < nhn; i++)
		fprintf(f, "H %u %s %llu\n", hn[i].ph, hn[i].name, (unsigned long long)hn[i].n);
	pthread_mutex_unlock(&mu);
	fclose(f);
}

static void ensure_registered(void)
{
	pthread_mutex_lock(&mu);
	if (!registered) {
		registered = 1;
		atexit(dump);
	}
	pthread_mutex_unlock(&mu);
}

void n2perf_mark(const char *label)
{
	pthread_mutex_lock(&mu);
	if (nph < MAXPH) {
		snprintf(phname[nph], sizeof(phname[nph]), "%s", label);
		__atomic_store_n(&phase, nph, __ATOMIC_RELEASE);
		nph++;
	}
	pthread_mutex_unlock(&mu);
}

/* ---- the core's hooks ---- */
void n2a32_trace_insn(struct n2a32_cpu *c, uint32_t addr, uint32_t insn, int thumb, int size)
{
	struct local *l = get_local();
	uint64_t ph = __atomic_load_n(&phase, __ATOMIC_RELAXED);
	uint64_t ik = (uint64_t)insn | (uint64_t)(thumb ? 1u : 0u) << 32 | ph << 33;
	uint64_t pk = (uint64_t)addr | ph << 33;
	unsigned i;

	(void)c; (void)size;
	if (!l)
		return;
	i = (unsigned)((ik * 0x9E3779B97F4A7C15ull) >> 52) & (LC - 1);
	if (l->ik[i] != ik || !l->in[i]) {
		if (l->in[i]) { pthread_mutex_lock(&mu); tab_add(&tins, l->ik[i], l->in[i]); pthread_mutex_unlock(&mu); }
		l->ik[i] = ik; l->in[i] = 0;
	}
	l->in[i]++;
	i = (unsigned)((pk * 0x9E3779B97F4A7C15ull) >> 52) & (LC - 1);
	if (l->pk[i] != pk || !l->pn[i]) {
		if (l->pn[i]) { pthread_mutex_lock(&mu); tab_add(&tpc, l->pk[i], l->pn[i]); pthread_mutex_unlock(&mu); }
		l->pk[i] = pk; l->pn[i] = 0;
	}
	l->pn[i]++;
}

void n2a32_trace_hostcall(struct n2a32_cpu *c, const char *name, int leaving)
{
	uint32_t ph = __atomic_load_n(&phase, __ATOMIC_RELAXED);
	(void)c;
	if (!leaving || !name)        /* T01 convention: leaving=1 is written on entry */
		return;
	pthread_mutex_lock(&mu);
	for (unsigned i = 0; i < nhn; i++)
		if (hn[i].ph == ph && hn[i].name == name) { hn[i].n++; pthread_mutex_unlock(&mu); return; }
	if (nhn < sizeof(hn) / sizeof(hn[0]))
		hn[nhn++] = (struct hname){ name, ph, 1 };
	pthread_mutex_unlock(&mu);
}

void n2a32_trace_mmio(struct n2a32_mem *m, uint32_t addr, unsigned bytes, uint32_t value, int write)
{
	(void)m; (void)addr; (void)bytes; (void)value;
	__atomic_fetch_add(&mmio_n[__atomic_load_n(&phase, __ATOMIC_RELAXED) % MAXPH][write ? 1 : 0], 1, __ATOMIC_RELAXED);
}

/* ---- T01's API, reduced to "count" ---- */
void n2a32_trace_attach(struct n2a32_cpu *c) { ensure_registered(); if (c) c->trace = &marker; }
void n2a32_trace_detach(struct n2a32_cpu *c) { if (c) c->trace = NULL; }
void n2a32_trace_set_elf(struct n2elf_set *s) { (void)s; }
void n2a32_trace_regs(int on) { (void)on; }
void n2a32_trace_dregs(int on) { (void)on; }
void n2a32_trace_clear(void) {}
void n2a32_trace_dump(struct n2a32_cpu *c, FILE *out) { (void)c; (void)out; }
uint64_t n2a32_trace_events(void) { return 0; }
int n2a32_trace_open(const char *path) { (void)path; ensure_registered(); return 0; }
void n2a32_trace_close(void) {}
