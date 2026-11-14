/* SPDX-License-Identifier: MIT */
/*
 * asmrun.c -- run a tiny assembled snippet under the AArch32 interpreter.
 *
 * asmrun.sh assembles the snippet with llvm-mc, extracts .text and invokes this program with
 * --load <raw>.  The bytes are placed at --base (default 0x10000).  Execution stops on the
 * sentinel `udf #254` (0xe7f00ffe in A32, 0xdefe in T32) or on `bx lr` when lr is still
 * N2A32_RETURN_ADDR.  Output is one "name=value" line per register/flag/memory byte group.
 */
#include "n2a32.h"
#include "n2a32_trace.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CODE_BASE 0x10000u
#define MAP_BASE  0x10000u
#define MAP_LEN   0x30000u       /* 0x10000..0x40000: code, stack, scratch */
#define MAX_MEM   32

struct memdump { uint32_t addr, len; };

static unsigned long long parse_num(const char *s, int *ok)
{
	char *end = NULL;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &end, 0);
	*ok = (end && *end == '\0' && errno == 0);
	return v;
}

static int reg_index(const char *name)
{
	if (name[0] == 'r' || name[0] == 'R') {
		char *end = NULL;
		long n = strtol(name + 1, &end, 10);
		if (end && *end == '\0' && n >= 0 && n <= 15)
			return (int)n;
	}
	if (strcmp(name, "sp") == 0) return 13;
	if (strcmp(name, "lr") == 0) return 14;
	if (strcmp(name, "pc") == 0) return 15;
	return -1;
}

static const char *reg_name(unsigned i)
{
	static const char *n[16] = { "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
		"r8", "r9", "r10", "r11", "r12", "sp", "lr", "pc" };
	return n[i];
}

static int usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s --load FILE [options]\n"
		"  --base 0xADDR     load address (default 0x%x)\n"
		"  --thumb           start in T32 (the assembler was thumbv7a)\n"
		"  --reg NAME=VALUE  initial register: r0-r15, sp, lr (repeatable)\n"
		"  --print LIST      registers to print, comma separated (default all r0-r15)\n"
		"  --mem ADDR:LEN    dump LEN bytes of guest memory (repeatable, max %d)\n"
		"  --stack 0xADDR    initial sp (default 0x2f000)\n"
		"  --max N           instruction limit (default 100000)\n"
		"  --trace           dump the trace ring after the run\n",
		argv0, CODE_BASE, MAX_MEM);
	return 2;
}

int main(int argc, char **argv)
{
	const char *load = NULL;
	uint32_t base = CODE_BASE, stack = 0x2f000u;
	uint64_t max_insn = 100000;
	int thumb = 0, dotrace = 0, i, k;
	int *want = NULL;
	unsigned nwant = 0;
	struct memdump md[MAX_MEM];
	unsigned nmd = 0;
	struct n2a32_mem *mem;
	struct n2a32_cpu cpu;
	unsigned char *buf = NULL;
	long sz;
	FILE *f;
	uint32_t regs[16];
	unsigned char isset[16];
	int ok;
	int failed = 0;

	memset(regs, 0, sizeof(regs));
	memset(isset, 0, sizeof(isset));

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--load") == 0 && i + 1 < argc)
			load = argv[++i];
		else if (strcmp(argv[i], "--base") == 0 && i + 1 < argc)
			base = (uint32_t)parse_num(argv[++i], &ok);
		else if (strcmp(argv[i], "--stack") == 0 && i + 1 < argc)
			stack = (uint32_t)parse_num(argv[++i], &ok);
		else if (strcmp(argv[i], "--max") == 0 && i + 1 < argc)
			max_insn = parse_num(argv[++i], &ok);
		else if (strcmp(argv[i], "--thumb") == 0)
			thumb = 1;
		else if (strcmp(argv[i], "--armv8") == 0)
			;                       /* assembler selection only; the runner is the same */
		else if (strcmp(argv[i], "--trace") == 0)
			dotrace = 1;
		else if (strcmp(argv[i], "--reg") == 0 && i + 1 < argc) {
			char *eq = strchr(argv[i + 1], '=');
			int r;
			if (!eq) return usage(argv[0]);
			*eq = '\0';
			r = reg_index(argv[i + 1]);
			if (r < 0) return usage(argv[0]);
			regs[r] = (uint32_t)parse_num(eq + 1, &ok);
			if (!ok) return usage(argv[0]);
			isset[r] = 1;
			i++;
		} else if (strcmp(argv[i], "--print") == 0 && i + 1 < argc) {
			char *p = argv[++i], *tok;
			want = calloc(16, sizeof(*want));
			if (!want) return 2;
			for (tok = strtok(p, ","); tok; tok = strtok(NULL, ",")) {
				int r;
				if (strcmp(tok, "all") == 0) {
					free(want);
					want = NULL;
					nwant = 0;
					break;
				}
				r = reg_index(tok);
				if (r < 0) return usage(argv[0]);
				want[nwant++] = r;
			}
		} else if (strcmp(argv[i], "--mem") == 0 && i + 1 < argc) {
			char *colon = strchr(argv[i + 1], ':');
			if (!colon || nmd >= MAX_MEM) return usage(argv[0]);
			*colon = '\0';
			md[nmd].addr = (uint32_t)parse_num(argv[i + 1], &ok);
			if (!ok) return usage(argv[0]);
			md[nmd].len = (uint32_t)parse_num(colon + 1, &ok);
			if (!ok) return usage(argv[0]);
			nmd++;
			i++;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			return usage(argv[0]);
		} else {
			fprintf(stderr, "unknown argument: %s\n", argv[i]);
			return usage(argv[0]);
		}
	}
	if (!load)
		return usage(argv[0]);

	f = fopen(load, "rb");
	if (!f) { perror(load); return 2; }
	if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return 2;
	}
	if (sz == 0 || (unsigned long)sz > MAP_LEN || base < MAP_BASE || base + (unsigned long)sz > MAP_BASE + MAP_LEN) {
		fprintf(stderr, "asmrun: bad load size %ld at 0x%x (map 0x%x..0x%x)\n",
			sz, base, MAP_BASE, MAP_BASE + MAP_LEN);
		fclose(f);
		return 2;
	}
	buf = malloc((size_t)sz);
	if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		fprintf(stderr, "asmrun: short read from %s\n", load);
		free(buf);
		fclose(f);
		return 2;
	}
	fclose(f);

	mem = n2a32_mem_create();
	if (!mem) {
		fprintf(stderr, "asmrun: cannot reserve guest memory\n");
		free(buf);
		return 2;
	}
	if (n2a32_mem_map(mem, MAP_BASE, MAP_LEN, 1) != 0) {
		fprintf(stderr, "asmrun: n2a32_mem_map failed\n");
		free(buf);
		n2a32_mem_destroy(mem);
		return 2;
	}
	memcpy(mem->base + base, buf, (size_t)sz);
	free(buf);

	n2a32_cpu_init(&cpu, mem);
	cpu.thumb = (uint8_t)thumb;
	for (i = 0; i < 16; i++)
		if (isset[i])
			cpu.r[i] = regs[i];
	if (!isset[13])
		cpu.r[13] = stack;
	if (!isset[14])
		cpu.r[14] = N2A32_RETURN_ADDR;
	cpu.r[15] = base;
	cpu.next_pc = base;
	cpu.ilimit = max_insn ? max_insn : 0;

	if (dotrace)
		n2a32_trace_attach(&cpu);

	(void)n2a32_run(&cpu);
	if (dotrace)
		n2a32_trace_dump(&cpu, stdout);

	{
		uint32_t sentinel = thumb ? 0xdefeu : 0xe7f00ffeu;
		int normal = (cpu.stop == N2A32_STOP_RETURN) ||
			(cpu.stop == N2A32_STOP_UNDEF && cpu.stop_insn == sentinel);
		printf("stop=%s\n", normal ? "ok" : "error");
		printf("stop_reason=%d\n", (int)cpu.stop);
		printf("stop_addr=0x%08x\n", cpu.stop_addr);
		printf("stop_insn=0x%08x\n", cpu.stop_insn);
		printf("icount=%llu\n", (unsigned long long)cpu.icount);
		printf("thumb=%u\n", (unsigned)cpu.thumb);
		printf("stop_name=");
		switch (cpu.stop) {
		case N2A32_STOP_NONE: printf("NONE"); break;
		case N2A32_STOP_RETURN: printf("RETURN"); break;
		case N2A32_STOP_UNDEF: printf("UNDEF"); break;
		case N2A32_STOP_FAULT: printf("FAULT"); break;
		case N2A32_STOP_SVC: printf("SVC"); break;
		case N2A32_STOP_BKPT: printf("BKPT"); break;
		case N2A32_STOP_LIMIT: printf("LIMIT"); break;
		case N2A32_STOP_REQUEST: printf("REQUEST"); break;
		case N2A32_STOP_NOIMPORT: printf("NOIMPORT"); break;
		}
		putchar('\n');
		if (!normal)
			failed = 1;
	}
	for (i = 0; i < 16; i++) {
		int print_it = (want == NULL);
		for (k = 0; k < (int)nwant; k++)
			if (want[k] == i)
				print_it = 1;
		if (print_it)
			printf("%s=0x%08x\n", reg_name((unsigned)i), cpu.r[i]);
	}
	printf("cpsr=0x%08x\n", n2a32_apsr(&cpu));
	printf("n=%u\nz=%u\nc=%u\nv=%u\nq=%u\nge=0x%x\n",
		(unsigned)cpu.n, (unsigned)cpu.z, (unsigned)cpu.cf, (unsigned)cpu.v,
		(unsigned)cpu.q, (unsigned)cpu.ge);
	for (i = 0; i < (int)nmd; i++) {
		uint32_t a;
		printf("mem[0x%08x]=", md[i].addr);
		for (a = 0; a < md[i].len; a++) {
			uint32_t g = md[i].addr + a;
			if (g < MAP_BASE || g >= MAP_BASE + MAP_LEN) {
				printf("??");
				break;
			}
			printf("%02x", mem->base[g]);
		}
		putchar('\n');
	}

	free(want);
	n2a32_trace_close();
	n2a32_mem_destroy(mem);
	return failed;
}
