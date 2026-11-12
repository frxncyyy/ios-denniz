/* SPDX-License-Identifier: MIT */
/*
 * t_v03 -- host driver for the V03 loader tests.
 *
 * Usage: t_v03 run <manifest>
 *
 * Manifest (one command per line, blank lines and '#' comments ignored):
 *   obj <soname> <host path>
 *   shim <name>                  a function shim, all sharing one dummy implementation
 *   data <name> <size> <hex>     a data shim with an initial byte string
 *   sym <name>                   print n2elf_sym(name)
 *   describe <hex address>       print n2elf_describe(address)
 *   mem <hex address> <len>      print guest bytes as hex
 *   perm <hex address>           print the guest page permission (0 RO, 1 RW)
 *   relink                       call n2elf_link() a second time after a successful link (must be a no-op)
 *   retry                        after a failed link, call n2elf_link() again (must refuse)
 *   run-init                     run n2elf_run_init() on a fake cpu
 *
 * Output is line oriented (see loader/test_v03/oracle.py, which compares it with llvm-readelf):
 *   IMAGE <soname> <base> <span> <map_lo> <entry>
 *   RELOC <where> <type> <value>         (one per applied relocation, through n2elf_reloc_trace)
 *   SHIM <name> <addr> | DATA <name> <addr>
 *   HOSTCALL <slot> <name>
 *   UNRESOLVED <n>
 *   SYM <name> <addr>
 *   DESCRIBE <addr> <text>
 *   MEM <addr> <len> <hex bytes> | PERM <addr> <0|1> | RELINK <rc> <unresolved> | RETRY <rc> <message>
 *   INIT_CALL <addr> | INIT_RET <rc>
 *   ERROR <stage> <message>
 *   END
 *
 * The program always exits 0; the oracle decides whether ERROR is expected.
 */
#include "n2elf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Defined by the stub (or by V02's real emu when linked): test-only introspection. */
extern uint32_t n2a32_stub_calls[512];
extern unsigned n2a32_stub_ncalls;
void n2a32_stub_reset(void);
int n2a32_stub_perm(uint32_t addr);
int n2a32_stub_hostcall_is_null(uint32_t guest_addr);

/* Private bridge exported by n2elf_reloc.c / n2elf.c for the tests. */
extern void (*n2elf_reloc_trace)(uint32_t where, uint32_t type, uint32_t value);
uint32_t n2elf__span(const struct n2elf_image *im);
uint32_t n2elf__map_lo(const struct n2elf_image *im);
const char *n2elf__soname(const struct n2elf_image *im);

#define MAX_CMDS 8192
#define MAX_SHIMS 256
#define MAX_DATA 32
#define LINE_MAX_ 1024

struct cmd {
	char kind[16];
	char a[256];
	char b[256];
	unsigned size;
	uint8_t init[64];
};

static struct cmd cmds[MAX_CMDS];
static int ncmds;

static int split(char *line, char *tok[], int max)
{
	int n = 0;
	char *p = line;

	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		if (n == max)
			break;
		tok[n++] = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		if (*p)
			*p++ = '\0';
	}
	return n;
}

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static int parse_hex_bytes(const char *s, uint8_t *out, unsigned cap, unsigned *n)
{
	unsigned k = 0;

	while (s[0] && s[1]) {
		int hi = hexval(s[0]);
		int lo = hexval(s[1]);

		if (hi < 0 || lo < 0 || k >= cap)
			return -1;
		out[k++] = (uint8_t)((hi << 4) | lo);
		s += 2;
	}
	if (*s)
		return -1;
	*n = k;
	return 0;
}

static void trace_reloc(uint32_t where, uint32_t type, uint32_t value)
{
	printf("RELOC %08x %u %08x\n", where, type, value);
}

static void dummy_shim(struct n2a32_cpu *c)
{
	n2shim_ret(c, 0);
}

int main(int argc, char **argv)
{
	FILE *f;
	char line[LINE_MAX_];
	struct n2a32_mem *mem;
	struct n2elf_set *set;
	struct n2shim_entry shim_tab[MAX_SHIMS + 1];
	struct n2shim_data data_tab[MAX_DATA + 1];
	int nshim = 0, ndata = 0, failed = 0;
	unsigned unresolved = 0;
	char err[160];

	if (argc != 3 || strcmp(argv[1], "run") != 0) {
		fprintf(stderr, "usage: %s run <manifest>\n", argv[0]);
		return 2;
	}
	f = fopen(argv[2], "r");
	if (!f) {
		fprintf(stderr, "cannot open %s\n", argv[2]);
		return 2;
	}
	while (fgets(line, sizeof(line), f)) {
		char *tok[8];
		int n;
		size_t len = strlen(line);

		while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
			line[--len] = '\0';
		if (line[0] == '#' || line[0] == '\0')
			continue;
		n = split(line, tok, 8);
		if (n == 0)
			continue;
		if (ncmds >= MAX_CMDS) {
			fprintf(stderr, "too many manifest commands\n");
			fclose(f);
			return 2;
		}
		memset(&cmds[ncmds], 0, sizeof(cmds[ncmds]));
		snprintf(cmds[ncmds].kind, sizeof(cmds[ncmds].kind), "%s", tok[0]);
		if (n > 1)
			snprintf(cmds[ncmds].a, sizeof(cmds[ncmds].a), "%s", tok[1]);
		if (n > 2)
			snprintf(cmds[ncmds].b, sizeof(cmds[ncmds].b), "%s", tok[2]);
		if (strcmp(tok[0], "data") == 0 && n >= 4) {
			unsigned k = 0;

			cmds[ncmds].size = (unsigned)strtoul(tok[2], NULL, 0);
			if (parse_hex_bytes(tok[3], cmds[ncmds].init, sizeof(cmds[ncmds].init), &k) != 0 ||
			    cmds[ncmds].size > sizeof(cmds[ncmds].init)) {
				fprintf(stderr, "bad data initialiser on line: %s\n", line);
				fclose(f);
				return 2;
			}
		}
		ncmds++;
	}
	fclose(f);

	mem = n2a32_mem_create();
	if (!mem) {
		fprintf(stderr, "cannot reserve guest memory\n");
		return 2;
	}
	set = n2elf_set_create(mem);
	if (!set) {
		fprintf(stderr, "cannot create the loader set\n");
		return 2;
	}

	/* Pass 1: shim tables. */
	memset(shim_tab, 0, sizeof(shim_tab));
	memset(data_tab, 0, sizeof(data_tab));
	for (int i = 0; i < ncmds; i++) {
		if (strcmp(cmds[i].kind, "shim") == 0) {
			if (nshim >= MAX_SHIMS) {
				fprintf(stderr, "too many shims\n");
				return 2;
			}
			shim_tab[nshim].name = cmds[i].a;
			shim_tab[nshim].fn = NULL;	/* filled below */
			nshim++;
		} else if (strcmp(cmds[i].kind, "data") == 0) {
			if (ndata >= MAX_DATA) {
				fprintf(stderr, "too many data shims\n");
				return 2;
			}
			data_tab[ndata].name = cmds[i].a;
			data_tab[ndata].size = cmds[i].size;
			data_tab[ndata].init = cmds[i].init;
			ndata++;
		}
	}
	/* One shared implementation, so every (fn,name) pair is distinct in the host-call table. */
	for (int i = 0; i < nshim; i++)
		shim_tab[i].fn = dummy_shim;
	if (nshim)
		n2elf_set_add_shims(set, shim_tab);
	if (ndata)
		n2elf_set_add_data(set, data_tab);

	/* Pass 2: load. */
	for (int i = 0; i < ncmds && !failed; i++) {
		struct n2elf_image *im;

		if (strcmp(cmds[i].kind, "obj") != 0)
			continue;
		err[0] = '\0';
		im = n2elf_load(set, cmds[i].b,
			strcmp(cmds[i].a, "(null)") == 0 ? NULL : cmds[i].a, err);
		if (!im) {
			printf("ERROR load %s\n", err);
			failed = 1;
			break;
		}
		printf("IMAGE %s %08x %08x %08x %08x\n", n2elf__soname(im), n2elf_base(im),
		       n2elf__span(im), n2elf__map_lo(im), n2elf_entry(im));
	}
	if (!failed) {
		n2elf_reloc_trace = trace_reloc;
		err[0] = '\0';
		if (n2elf_link(set, &unresolved, err) != 0) {
			printf("ERROR link %s\n", err);
			failed = 1;
		}
		n2elf_reloc_trace = NULL;
	}
	if (failed) {
		for (int i = 0; i < ncmds; i++) {
			if (strcmp(cmds[i].kind, "retry") == 0) {
				unsigned u2 = 0;
				int rc;

				err[0] = '\0';
				rc = n2elf_link(set, &u2, err);
				printf("RETRY %d %s\n", rc, err);
				break;
			}
		}
	}
	if (!failed) {
		printf("UNRESOLVED %u\n", unresolved);
		for (int i = 0; i < nshim; i++)
			printf("SHIM %s %08x\n", shim_tab[i].name, n2elf_sym(set, shim_tab[i].name));
		for (int i = 0; i < ndata; i++) {
			uint32_t a = n2elf_sym(set, data_tab[i].name);
			uint32_t w = 0;

			if (a)
				memcpy(&w, mem->base + a, 4);
			printf("DATA %s %08x %08x\n", data_tab[i].name, a, w);
		}
		{
			int misses = 0, hits = 0;

			for (uint32_t k = 1; k < 512u; k++) {
				uint32_t a = N2A32_HOSTCALL_BASE + 4u * k;
				const char *n = n2a32_hostcall_name(a);

				if (n) {
					printf("HOSTCALL %08x %s %d\n", a, n, n2a32_stub_hostcall_is_null(a));
					hits++;
					misses = 0;
				} else if (hits && ++misses > 16) {
					break;
				}
			}
		}
		for (int i = 0; i < ncmds && !failed; i++) {
			if (strcmp(cmds[i].kind, "sym") == 0) {
				printf("SYM %s %08x\n", cmds[i].a, n2elf_sym(set, cmds[i].a));
			} else if (strcmp(cmds[i].kind, "describe") == 0) {
				char buf[220];
				uint32_t a = (uint32_t)strtoul(cmds[i].a, NULL, 16);

				n2elf_describe(set, a, buf, sizeof(buf));
				printf("DESCRIBE %08x %s\n", a, buf);
			} else if (strcmp(cmds[i].kind, "mem") == 0) {
				uint32_t a = (uint32_t)strtoul(cmds[i].a, NULL, 16);
				unsigned n = (unsigned)strtoul(cmds[i].b, NULL, 10);

				printf("MEM %08x %u ", a, n);
				for (unsigned k = 0; k < n; k++)
					printf("%02x", mem->base[a + k]);
				printf("\n");
			} else if (strcmp(cmds[i].kind, "perm") == 0) {
				uint32_t a = (uint32_t)strtoul(cmds[i].a, NULL, 16);

				printf("PERM %08x %d\n", a, n2a32_stub_perm(a));
			} else if (strcmp(cmds[i].kind, "relink") == 0) {
				unsigned u2 = 0;
				int rc;

				err[0] = '\0';
				n2elf_reloc_trace = trace_reloc;
				rc = n2elf_link(set, &u2, err);
				n2elf_reloc_trace = NULL;
				printf("RELINK %d %u\n", rc, u2);
			} else if (strcmp(cmds[i].kind, "run-init") == 0) {
				struct n2a32_cpu cpu;
				uint32_t stack = n2a32_mem_alloc(mem, 64 * 1024, 16);
				int rc;

				n2a32_cpu_init(&cpu, mem);
				cpu.r[13] = stack + 64 * 1024 - 8;
				n2a32_stub_reset();
				rc = n2elf_run_init(set, &cpu);
				printf("INIT_RET %d\n", rc);
				for (unsigned k = 0; k < n2a32_stub_ncalls; k++)
					printf("INIT_CALL %08x\n", n2a32_stub_calls[k]);
			}
		}
	}
	printf("END\n");
	n2elf_set_destroy(set);
	n2a32_mem_destroy(mem);
	return 0;
}
