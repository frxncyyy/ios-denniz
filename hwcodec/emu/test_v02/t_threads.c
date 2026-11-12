/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE 1
/*
 * t_threads.c -- four host threads, four guest CPUs, one 4 GiB guest address space.  Built with TSan by
 * run_tests.sh.  Each thread allocates its own guest stack concurrently (the bump allocator is mutex protected),
 * registers the same host call concurrently (the slot table is mutex protected), runs the same ARM loop for a
 * fixed instruction budget, then all four race one LDREX/STREX pair.  Exactly one CAS may win.
 */
#include "n2a32.h"
#include "n2enc.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTHREADS 4
#define ITERS 20000

static struct n2a32_mem *g_mem;
static pthread_barrier_t g_barrier;
static int g_pass, g_fail;

#define CHECK(cond, ...) do { \
	if (cond) { g_pass++; } \
	else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* The only classes the loop executes. */
int n2a32_arm_dp(struct n2a32_cpu *c, uint32_t insn)
{
	if ((insn & 0x0FE00000u) != 0x02800000u || (insn & 0x000F0000u) != 0x00000000u)
		return N2A32_UNDEF;
	/* add r0, r0, #imm (Rn = r0 is the only form the loop uses) */
	if (((insn >> 16) & 0xFu) != 0u || ((insn >> 12) & 0xFu) != 0u)
		return N2A32_UNDEF;
	{
		uint32_t imm = insn & 0xFFu;
		uint32_t r = c->r[0] + imm;
		c->r[0] = r;
		n2a32_set_nz(c, r);
	}
	return N2A32_OK;
}

int n2a32_arm_misc(struct n2a32_cpu *c, uint32_t insn)
{
	if ((insn & 0x0E000000u) == 0x0A000000u) {
		uint32_t pc = c->r[15] - 8u;
		uint32_t off = (uint32_t)((int32_t)(insn << 8) >> 6);
		n2a32_branch(c, pc + 8u + off);
		return N2A32_OK;
	}
	return N2A32_UNDEF;
}

static void hostcall_noop(struct n2a32_cpu *c)
{
	c->r[0] = 0xFEEDFACEu;
}

struct thread_arg {
	int id;
	uint32_t stack;
	uint32_t slot_first;
	int slot_ok;
	enum n2a32_stop stop;
	uint64_t icount;
	uint32_t r0;
	int excl_result;
};

static void *thread_main(void *opaque)
{
	struct thread_arg *t = (struct thread_arg *)opaque;
	struct n2a32_cpu cpu;
	unsigned i;

	/* Concurrent bump allocation of this thread's guest stack. */
	t->stack = n2a32_mem_alloc(g_mem, 1u << 16, 16);
	t->slot_first = n2a32_hostcall_register(hostcall_noop, "thread.same");
	t->slot_ok = (t->slot_first != 0);
	for (i = 0; i < 100; i++) {
		uint32_t a = n2a32_hostcall_register(hostcall_noop, "thread.same");
		if (a != t->slot_first)
			t->slot_ok = 0;
		if (n2a32_hostcall_name(t->slot_first) == NULL ||
		    strcmp(n2a32_hostcall_name(t->slot_first), "thread.same") != 0)
			t->slot_ok = 0;
	}

	n2a32_cpu_init(&cpu, g_mem);
	cpu.r[13] = t->stack + (1u << 16);
	cpu.r[15] = 0x1000u;
	cpu.ilimit = ITERS;
	t->stop = n2a32_run(&cpu);
	t->icount = cpu.icount;
	t->r0 = cpu.r[0];

	/* All four mark the same zero word, then store 1 at the same instant. */
	n2a32_excl_mark(&cpu, 0x2000u, 4u, 0u);
	pthread_barrier_wait(&g_barrier);
	t->excl_result = n2a32_excl_store(&cpu, 0x2000u, 4u, 1u);
	return NULL;
}

int main(void)
{
	struct thread_arg t[NTHREADS];
	pthread_t th[NTHREADS];
	int i, winners = 0;
	uint32_t word;

	g_mem = n2a32_mem_create();
	if (!g_mem) {
		printf("FATAL: n2a32_mem_create failed\n");
		return 1;
	}
	if (n2a32_mem_map(g_mem, 0x1000, 0x3000, 1) != 0) {
		printf("FATAL: map failed\n");
		return 1;
	}
	memcpy(g_mem->base + 0x1000, loop_prog_bytes, LOOP_PROG_SIZE);
	memset(g_mem->base + 0x2000, 0, 8);

	CHECK(pthread_barrier_init(&g_barrier, NULL, NTHREADS) == 0, "barrier init");
	for (i = 0; i < NTHREADS; i++) {
		memset(&t[i], 0, sizeof(t[i]));
		t[i].id = i;
		CHECK(pthread_create(&th[i], NULL, thread_main, &t[i]) == 0, "pthread_create %d", i);
	}
	for (i = 0; i < NTHREADS; i++)
		pthread_join(th[i], NULL);

	for (i = 0; i < NTHREADS; i++) {
		CHECK(t[i].stack != 0 && (t[i].stack & 0xFFFu) == 0, "thread %d stack %08x", i, t[i].stack);
		if (i > 0)
			CHECK(t[i].stack != t[i - 1].stack, "threads %d and %d share a stack", i - 1, i);
		CHECK(t[i].slot_ok, "thread %d host-call registration was not stable", i);
		CHECK(t[i].slot_first == t[0].slot_first, "thread %d got slot %08x, thread 0 got %08x",
		      i, t[i].slot_first, t[0].slot_first);
		CHECK(t[i].stop == N2A32_STOP_LIMIT, "thread %d stop %d", i, (int)t[i].stop);
		CHECK(t[i].icount == ITERS, "thread %d icount %llu", i, (unsigned long long)t[i].icount);
		CHECK(t[i].r0 == ITERS / 2u, "thread %d r0=%u, expected %u", i, t[i].r0, ITERS / 2u);
		if (t[i].excl_result == 0)
			winners++;
	}
	CHECK(winners == 1, "%d STREX winners, expected exactly 1", winners);
	memcpy(&word, g_mem->base + 0x2000, 4);
	CHECK(word == 1u, "exclusive word %u", word);

	pthread_barrier_destroy(&g_barrier);
	n2a32_mem_destroy(g_mem);
	printf("TESTS %d/%d\n", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
