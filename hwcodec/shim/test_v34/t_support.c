/* SPDX-License-Identifier: MIT */
/*
 * t_support.c -- guest memory, shim driving and the log recorder for the V34 tests.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"
#include "t_v34.h"
#include "t_guest_blob.h"

int t_checks;
int t_fails;

struct t_log_rec t_log;

void t_log_reset(void)
{
	memset(&t_log, 0, sizeof t_log);
}

void t_log_sink(int prio, const char *tag, const char *text)
{
	t_log.calls++;
	t_log.prio = prio;
	if (tag == NULL || tag[0] == '\0') {
		t_log.tag[0] = '\0';
	} else {
		snprintf(t_log.tag, sizeof t_log.tag, "%s", tag);
	}
	snprintf(t_log.text, sizeof t_log.text, "%s", text != NULL ? text : "(null)");
}

struct n2a32_mem *t_mem_new(void)
{
	struct n2a32_mem *m = n2a32_mem_create();

	if (m == NULL) {
		printf("FATAL: n2a32_mem_create failed\n");
		exit(2);
	}
	if (n2a32_mem_map(m, T_DATA_ADDR, T_DATA_LEN, 1) != 0 ||
	    n2a32_mem_map(m, T_STACK_ADDR, T_STACK_LEN, 1) != 0 ||
	    n2a32_mem_map(m, T_HEAP_ADDR, T_HEAP_LEN, 1) != 0) {
		printf("FATAL: n2a32_mem_map failed\n");
		exit(2);
	}
	memcpy(m->base + T_DATA_ADDR, t_guest_blob, sizeof t_guest_blob);
	return m;
}

void t_cpu_setup(struct n2a32_cpu *c, struct n2a32_mem *m)
{
	memset(c, 0, sizeof *c);
	n2a32_cpu_init(c, m);
	c->r[13] = T_STACK_ADDR + T_STACK_LEN - 0x100;   /* 8-aligned stack top */
	c->r[14] = N2A32_RETURN_ADDR;
}

uint32_t t_guest_const(struct n2a32_mem *m, unsigned off)
{
	(void)m;
	return T_DATA_ADDR + off;
}

uint32_t t_guest_fn(struct n2a32_mem *m, unsigned off)
{
	return t_guest_const(m, off) | 1u;   /* all blob symbols are Thumb functions */
}

const struct n2shim_entry *t_find(const struct n2shim_entry *tab, const char *name)
{
	const struct n2shim_entry *e;

	for (e = tab; e->name != NULL; e++) {
		if (strcmp(e->name, name) == 0)
			return e;
	}
	return NULL;
}

void t_call(struct n2a32_cpu *c, const struct n2shim_entry *tab, const char *name,
	    const uint32_t args[4], const uint32_t *stack_args, unsigned nstack)
{
	const struct n2shim_entry *e = t_find(tab, name);
	unsigned i;
	uint32_t sp = c->r[13];

	if (e == NULL || e->fn == NULL) {
		printf("FATAL: shim %s not in the table\n", name);
		exit(2);
	}
	for (i = 0; i < 4; i++)
		c->r[i] = args != NULL ? args[i] : 0;
	for (i = 0; i < nstack; i++)
		n2a32_st32(c, sp + 4 * i, stack_args[i]);
	e->fn(c);
}
