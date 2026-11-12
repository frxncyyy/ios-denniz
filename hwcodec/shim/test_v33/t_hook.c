/* SPDX-License-Identifier: MIT */
/*
 * test_v33/t_hook.c -- the one path the main suite cannot see: exit()/abort()/__stack_chk_fail() must ask the
 * thread package to stop every live guest cpu.  n2shim_stdio.c calls `n2shim_thread_request_stop()` weakly, so
 * the main suite links without V32; here a strong stand-in takes its place and must be called exactly once per
 * fatal call.
 *
 * Build with the same sources as the main suite plus this file (run_tests.sh does that); last line on success:
 * N2TEST V33-HOOK PASS <passed>/<total>
 */
#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"

extern void (*n2shim_log)(int prio, const char *tag, const char *text);
int n2shim_stdio_exit_reason(void);
void n2shim_stdio_exit_clear(void);

static int g_stop_calls;

unsigned n2shim_thread_request_stop(void)      /* strong: replaces V32's implementation for this test */
{
	g_stop_calls++;
	return 7;
}

static int g_pass, g_fail;

#define CHECK(cond, msg) do { if (cond) g_pass++; else { g_fail++; printf("FAIL %d: %s\n", __LINE__, msg); } } while (0)

int main(void)
{
	struct n2a32_mem *m = n2a32_mem_create();
	struct n2a32_cpu c;
	const struct n2shim_entry *e = n2shim_stdio;

	memset(&c, 0, sizeof c);
	c.mem = m;
	c.r[13] = n2a32_mem_alloc(m, 64 * 1024, 16) + 64 * 1024;
	c.r[15] = 0x00404040u;

	for (; e->name; e++) {
		if (!strcmp(e->name, "exit")) {
			c.r[0] = 9;
			e->fn(&c);
			break;
		}
	}
	CHECK(e->name != NULL, "exit is in n2shim_stdio[]");
	CHECK(g_stop_calls == 1, "exit asks the thread package once");
	CHECK(c.stop_request == 1, "exit sets the calling cpu's stop_request");
	CHECK(n2shim_stdio_exit_reason() == 1, "reason is EXIT");
	g_stop_calls = 0;
	c.stop_request = 0;
	n2shim_stdio_exit_clear();

	for (e = n2shim_stdio; e->name; e++) {
		if (!strcmp(e->name, "abort")) {
			e->fn(&c);
			break;
		}
	}
	CHECK(e->name != NULL, "abort is in n2shim_stdio[]");
	CHECK(g_stop_calls == 1, "abort asks the thread package once");
	CHECK(n2shim_stdio_exit_reason() == 2, "reason is ABORT");
	g_stop_calls = 0;
	c.stop_request = 0;
	n2shim_stdio_exit_clear();

	for (e = n2shim_stdio; e->name; e++) {
		if (!strcmp(e->name, "__stack_chk_fail")) {
			e->fn(&c);
			break;
		}
	}
	CHECK(e->name != NULL, "__stack_chk_fail is in n2shim_stdio[]");
	CHECK(g_stop_calls == 1, "__stack_chk_fail asks the thread package once");
	CHECK(n2shim_stdio_exit_reason() == 3, "reason is STACK_CHK");

	n2a32_mem_destroy(m);
	if (g_fail) {
		printf("N2TEST V33-HOOK FAIL %d/%d\n", g_pass, g_pass + g_fail);
		return 1;
	}
	printf("N2TEST V33-HOOK PASS %d/%d\n", g_pass, g_pass + g_fail);
	return 0;
}
