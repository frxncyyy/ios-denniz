/* SPDX-License-Identifier: MIT */
/* Integration test: n2a32_trace.c symbolises through the real V03 loader.  The V03 test
 * stand-in supplies only guest memory and host-call slots; no instruction is executed. */
#include "n2a32.h"
#include "n2elf.h"
#include "n2a32_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	struct n2a32_mem *m;
	struct n2elf_set *s;
	struct n2a32_cpu c;
	struct n2elf_image *im;
	char err[160], line[512];
	unsigned unresolved = 0;
	uint32_t sym;
	FILE *f;
	int found = 0;

	if (argc != 2) {
		fprintf(stderr, "usage: %s fixture.so\n", argv[0]);
		return 2;
	}
	m = n2a32_mem_create();
	if (!m)
		return 2;
	s = n2elf_set_create(m);
	if (!s)
		return 2;
	err[0] = '\0';
	im = n2elf_load(s, argv[1], "libfixture.so", err);
	if (!im) {
		fprintf(stderr, "load: %s\n", err);
		return 2;
	}
	if (n2elf_link(s, &unresolved, err) != 0) {
		fprintf(stderr, "link: %s\n", err);
		return 2;
	}
	if (unresolved != 0) {
		fprintf(stderr, "unresolved=%u\n", unresolved);
		return 2;
	}
	sym = n2elf_sym(s, "trace_symbol");
	if (!sym) {
		fprintf(stderr, "symbol not found\n");
		return 2;
	}

	memset(&c, 0, sizeof(c));
	c.mem = m;
	n2a32_trace_set_elf(s);
	n2a32_trace_clear();
	n2a32_trace_attach(&c);
	n2a32_trace_insn(&c, sym, 0xe1a00000u, 0, 4);
	f = tmpfile();
	if (!f)
		return 2;
	n2a32_trace_dump(&c, f);
	rewind(f);
	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "libfixture.so:trace_symbol+0x0")) {
			found = 1;
			break;
		}
	}
	fclose(f);
	n2a32_trace_detach(&c);
	n2a32_trace_set_elf(NULL);
	n2elf_set_destroy(s);
	n2a32_mem_destroy(m);
	printf("T01 trace-elf: %s\n", found ? "symbolised" : "FAILED");
	return found ? 0 : 1;
}
