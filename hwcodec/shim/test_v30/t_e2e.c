/* SPDX-License-Identifier: MIT */
/*
 * t_e2e.c -- V30 end-to-end check: a real ARM shared object (arm/t_e2e_guest.c) importing the V30 shims, loaded
 * by V03's real loader, executed by V02's real core.
 *
 * This is the strongest host evidence for the shims: the guest varargs are produced by clang itself under the
 * soft-float AAPCS (double and 64-bit integer on 8-aligned stack slots), qsort calls back into a real ARM
 * comparator, and malloc/free/strlen/strcmp/memcpy go through the shim tables.  The expected snprintf output is
 * computed on the host from the same format and values.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "n2a32.h"
#include "n2elf.h"

static int checks, fails;

static void ck(int cond, const char *what)
{
	checks++;
	if (cond)
		return;
	fails++;
	printf("FAIL: %s\n", what);
}

static const struct n2shim_data data[] = {
	{ "__stack_chk_guard", 4, NULL },
	{ "stderr", 4, NULL },
	{ NULL, 0, NULL }
};

int main(int argc, char **argv)
{
	struct n2a32_mem *m;
	struct n2elf_set *s;
	struct n2a32_cpu c;
	char err[160], expect[160];
	unsigned unresolved = 0;
	uint32_t fn, sp, rc, a_out, a_len, a_ok, r1 = 0;
	int elen;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <libv30e2e.so>\n", argv[0]);
		return 2;
	}
	m = n2a32_mem_create();
	s = n2elf_set_create(m);
	if (!m || !s) {
		printf("N2TEST V30-E2E FAIL cannot create memory/set\n");
		return 1;
	}
	n2elf_set_add_shims(s, n2shim_string);
	n2elf_set_add_shims(s, n2shim_heap);
	n2elf_set_add_data(s, data);

	if (!n2elf_load(s, argv[1], "libv30e2e.so", err)) {
		printf("N2TEST V30-E2E FAIL load: %s\n", err);
		return 1;
	}
	if (n2elf_link(s, &unresolved, err) != 0) {
		printf("N2TEST V30-E2E FAIL link: %s\n", err);
		return 1;
	}
	ck(unresolved == 0, "every import of the guest is covered by n2shim_string[]/n2shim_heap[]");

	fn = n2elf_sym(s, "run");
	ck(fn != 0, "run symbol");
	if (!fn) {
		printf("N2TEST V30-E2E FAIL no run\n");
		return 1;
	}
	n2a32_cpu_init(&c, m);
	sp = n2a32_mem_alloc(m, 65536, 16);
	c.r[13] = sp + 65536 - 8;
	c.r[14] = N2A32_RETURN_ADDR;
	c.ilimit = 10000000;
	rc = n2a32_call(&c, fn, NULL, 0, &r1);
	ck(c.stop == N2A32_STOP_RETURN, "the guest ran to its return address");
	ck(rc == 1, "the guest reported success");
	ck(c.icount > 1000, "the interpreter executed the guest");

	a_out = n2elf_sym(s, "g_out");
	a_len = n2elf_sym(s, "g_out_len");
	a_ok = n2elf_sym(s, "g_ok");
	ck(a_out && a_len && a_ok, "guest result symbols");
	if (a_out && a_len && a_ok) {
		elen = snprintf(expect, sizeof expect, "%d/%s/%llx/%.2f/%c/%%/%08x", -7, "arm",
				0xdeadbeefcafeULL, 3.5, 'Z', 0x1234u);
		printf("guest: [%s] len=%d\n", (char *)(m->base + a_out), (int)n2a32_ld32(&c, a_len));
		printf("host : [%s] len=%d\n", expect, elen);
		ck(strcmp((char *)(m->base + a_out), expect) == 0, "snprintf output matches the host");
		ck((int)n2a32_ld32(&c, a_len) == elen, "snprintf return value matches the host");
		ck(n2a32_ld32(&c, a_ok) == 1, "qsort/malloc/strlen/strcmp/memcpy path reported success");
	}
	printf("N2TEST V30-E2E %s %d/%d\n", fails ? "FAIL" : "PASS", checks - fails, checks);
	return fails ? 1 : 0;
}
