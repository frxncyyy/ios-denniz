/* SPDX-License-Identifier: MIT */
/*
 * diffrun.c -- load the ARM differential objects through V03 and call every kernel for every
 * seed.  One output line per call: "kernel seed result" (both hexadecimal, fixed width), or
 * "kernel seed STOP:<reason>" when the interpreter did not return normally.
 *
 * The two 128-bit AEABI division results (__aeabi_uldivmod, __aeabi_ldivmod) are host shims:
 * clang cannot express that return ABI in C on ARM32.  They are real implementations; the
 * guest still executes the PLT call and the host-call page entry, so the loader and host-call
 * dispatch are part of the test.
 */
#define _POSIX_C_SOURCE 200809L
#include "n2a32.h"
#include "n2elf.h"
#include "n2a32_trace.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LIBS 8
#define MAX_KERNELS 512
#define MAX_SEEDS 64
#define STACK_SIZE (256u * 1024u)

static void shim_uldivmod(struct n2a32_cpu *c)
{
	uint64_t n = n2shim_arg(c, 0) | ((uint64_t)n2shim_arg(c, 1) << 32);
	uint64_t d = n2shim_arg(c, 2) | ((uint64_t)n2shim_arg(c, 3) << 32);
	uint64_t q = 0, r = 0;

	if (d) {
		q = n / d;
		r = n % d;
	}
	c->r[0] = (uint32_t)q;
	c->r[1] = (uint32_t)(q >> 32);
	c->r[2] = (uint32_t)r;
	c->r[3] = (uint32_t)(r >> 32);
}

static void shim_ldivmod(struct n2a32_cpu *c)
{
	int64_t n = (int64_t)(n2shim_arg(c, 0) | ((uint64_t)n2shim_arg(c, 1) << 32));
	int64_t d = (int64_t)(n2shim_arg(c, 2) | ((uint64_t)n2shim_arg(c, 3) << 32));
	int64_t q = 0, r = 0;

	if (d) {
		if (n == (-9223372036854775807LL - 1) && d == -1) {
			q = n;
			r = 0;
		} else {
			q = n / d;
			r = n % d;
		}
	}
	c->r[0] = (uint32_t)(uint64_t)q;
	c->r[1] = (uint32_t)((uint64_t)q >> 32);
	c->r[2] = (uint32_t)(uint64_t)r;
	c->r[3] = (uint32_t)((uint64_t)r >> 32);
}

static const struct n2shim_entry shims[] = {
	{ "__aeabi_uldivmod", shim_uldivmod },
	{ "__aeabi_ldivmod", shim_ldivmod },
	{ NULL, NULL }
};

static int read_names(const char *path, char names[][64], int max)
{
	FILE *f = fopen(path, "r");
	int n = 0;
	char line[128];

	if (!f) {
		perror(path);
		return -1;
	}
	while (fgets(line, sizeof(line), f)) {
		size_t l = strlen(line);

		while (l && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' '))
			line[--l] = '\0';
		if (!l || line[0] == '#')
			continue;
		if (n >= max) {
			fprintf(stderr, "diffrun: too many kernels (max %d)\n", max);
			fclose(f);
			return -1;
		}
		snprintf(names[n], 64, "%s", line);
		n++;
	}
	fclose(f);
	return n;
}

static int parse_seeds(const char *s, uint32_t *seeds, int max)
{
	char buf[1024];
	char *p, *tok;
	int n = 0;

	snprintf(buf, sizeof(buf), "%s", s);
	for (tok = strtok_r(buf, ",", &p); tok; tok = strtok_r(NULL, ",", &p)) {
		char *end = NULL;
		unsigned long v;

		if (n >= max)
			return -1;
		errno = 0;
		v = strtoul(tok, &end, 0);
		if (!end || *end != '\0' || errno)
			return -1;
		seeds[n++] = (uint32_t)v;
	}
	return n;
}

int main(int argc, char **argv)
{
	const char *libs[MAX_LIBS];
	const char *kernfile = NULL, *outpath = NULL, *seedstr = NULL;
	const char *trace_kernel = NULL, *trace_out = NULL;
	uint32_t trace_seed = 0;
	int have_trace_seed = 0;
	int nlibs = 0, i;
	char names[MAX_KERNELS][64];
	int nk;
	uint32_t seeds[MAX_SEEDS];
	int ns;
	struct n2a32_mem *mem;
	struct n2elf_set *set;
	struct n2elf_image *images[MAX_LIBS];
	struct n2a32_cpu cpu;
	FILE *out = stdout;
	unsigned unresolved = 0;
	char err[160];
	uint32_t stack;
	int failures = 0;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--lib") == 0 && i + 1 < argc) {
			if (nlibs >= MAX_LIBS) {
				fprintf(stderr, "diffrun: too many --lib\n");
				return 2;
			}
			libs[nlibs++] = argv[++i];
		} else if (strcmp(argv[i], "--kernels") == 0 && i + 1 < argc) {
			kernfile = argv[++i];
		} else if (strcmp(argv[i], "--seeds") == 0 && i + 1 < argc) {
			seedstr = argv[++i];
		} else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
			outpath = argv[++i];
		} else if (strcmp(argv[i], "--trace-kernel") == 0 && i + 1 < argc) {
			trace_kernel = argv[++i];
		} else if (strcmp(argv[i], "--trace-seed") == 0 && i + 1 < argc) {
			trace_seed = (uint32_t)strtoul(argv[++i], NULL, 0);
			have_trace_seed = 1;
		} else if (strcmp(argv[i], "--trace-out") == 0 && i + 1 < argc) {
			trace_out = argv[++i];
		} else if (strcmp(argv[i], "--trace-regs") == 0) {
			n2a32_trace_regs(1);
		} else if (strcmp(argv[i], "--trace-dregs") == 0) {
			n2a32_trace_dregs(1);
		} else {
			fprintf(stderr, "usage: %s --lib OBJ [--lib OBJ] --kernels FILE --seeds LIST [--out FILE]\n",
				argv[0]);
			return 2;
		}
	}
	if (!nlibs || !kernfile || !seedstr) {
		fprintf(stderr, "diffrun: --lib, --kernels and --seeds are required\n");
		return 2;
	}
	if (outpath) {
		out = fopen(outpath, "w");
		if (!out) {
			perror(outpath);
			return 2;
		}
	}
	nk = read_names(kernfile, names, MAX_KERNELS);
	if (nk <= 0) {
		fprintf(stderr, "diffrun: no kernel names in %s\n", kernfile);
		return 2;
	}
	ns = parse_seeds(seedstr, seeds, MAX_SEEDS);
	if (ns <= 0) {
		fprintf(stderr, "diffrun: bad seed list\n");
		return 2;
	}

	mem = n2a32_mem_create();
	if (!mem) {
		fprintf(stderr, "diffrun: cannot reserve guest memory\n");
		return 2;
	}
	set = n2elf_set_create(mem);
	if (!set) {
		fprintf(stderr, "diffrun: n2elf_set_create failed\n");
		n2a32_mem_destroy(mem);
		return 2;
	}
	n2elf_set_add_shims(set, shims);
	for (i = 0; i < nlibs; i++) {
		char soname[32];

		snprintf(soname, sizeof(soname), "lib%d.so", i);
		err[0] = '\0';
		images[i] = n2elf_load(set, libs[i], soname, err);
		if (!images[i]) {
			fprintf(stderr, "diffrun: load %s: %s\n", libs[i], err);
			return 3;
		}
		fprintf(stderr, "diffrun: loaded %s base=0x%08x\n", libs[i], n2elf_base(images[i]));
	}
	err[0] = '\0';
	if (n2elf_link(set, &unresolved, err) != 0) {
		fprintf(stderr, "diffrun: link: %s\n", err);
		return 3;
	}
	fprintf(stderr, "diffrun: objects=%d unresolved=%u\n", nlibs, unresolved);
	if (unresolved) {
		fprintf(stderr, "diffrun: unresolved imports would make this run meaningless\n");
		return 3;
	}
	n2a32_trace_set_elf(set);

	stack = n2a32_mem_alloc(mem, STACK_SIZE, 8);
	if (!stack) {
		fprintf(stderr, "diffrun: cannot allocate the guest stack\n");
		return 3;
	}
	stack += STACK_SIZE - 8;

	for (i = 0; i < nk; i++) {
		uint32_t fn = n2elf_sym(set, names[i]);
		int s;

		if (!fn) {
			fprintf(out, "%s 00000000 MISSING\n", names[i]);
			fprintf(stderr, "diffrun: kernel %s not found\n", names[i]);
			failures++;
			continue;
		}
		for (s = 0; s < ns; s++) {
			uint32_t arg = seeds[s], res;
			FILE *tf = NULL;

			n2a32_cpu_init(&cpu, mem);
			cpu.r[13] = stack;
			cpu.thumb = (uint8_t)(fn & 1u);
			cpu.ilimit = 5000000;   /* a kernel that loops is a bug, not a test */
			if (trace_kernel && have_trace_seed && arg == trace_seed &&
			    strcmp(trace_kernel, names[i]) == 0) {
				tf = fopen(trace_out ? trace_out : "/dev/stdout", "w");
				if (!tf) {
					perror("diffrun: trace-out");
					return 3;
				}
				n2a32_trace_clear();
				n2a32_trace_attach(&cpu);
			}
			res = n2a32_call(&cpu, fn, &arg, 1, NULL);
			if (tf) {
				n2a32_trace_dump(&cpu, tf);
				fclose(tf);
				n2a32_trace_detach(&cpu);
			}
			if (cpu.stop != N2A32_STOP_RETURN) {
				fprintf(out, "%s %08x STOP:%d\n", names[i], seeds[s], (int)cpu.stop);
				fprintf(stderr, "diffrun: %s seed=%08x stop=%d addr=%08x insn=%08x\n",
					names[i], seeds[s], (int)cpu.stop,
					cpu.stop_addr, cpu.stop_insn);
				failures++;
			} else {
				fprintf(out, "%s %08x %08x\n", names[i], seeds[s], res);
			}
		}
	}
	if (out != stdout)
		fclose(out);
	n2a32_trace_close();
	n2elf_set_destroy(set);
	n2a32_mem_destroy(mem);
	fprintf(stderr, "diffrun: done, %d failed calls\n", failures);
	return failures ? 1 : 0;
}
