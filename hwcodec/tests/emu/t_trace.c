/* SPDX-License-Identifier: MIT */
/* Host test for emu/n2a32_trace.c: ring content, overflow policy, dump format, full-trace file,
 * and the weak symboliser hook.  No interpreter core is needed; the hooks are called directly. */
#include "n2a32.h"
#include "n2a32_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int checks, failures;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
		fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

/* Strong symbol for the weak reference inside n2a32_trace.c. */
const char *n2elf_describe(struct n2elf_set *s, uint32_t addr, char *buf, size_t len)
{
	(void)s;
	snprintf(buf, len, "fake+0x%x", (unsigned)addr & 0xffu);
	return buf;
}

static char *read_all(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	char *p;

	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
	n = ftell(f);
	if (n < 0) { fclose(f); return NULL; }
	rewind(f);
	p = malloc((size_t)n + 1);
	if (!p) { fclose(f); return NULL; }
	if (fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); fclose(f); return NULL; }
	p[n] = '\0';
	fclose(f);
	return p;
}

static unsigned count_prefix(const char *s, const char *prefix)
{
	size_t pl = strlen(prefix);
	unsigned n = 0;

	while ((s = strstr(s, prefix)) != NULL) {
		n++;
		s += pl;
	}
	return n;
}

int main(void)
{
	struct n2a32_cpu c;
	char path[256];
	char *text;
	unsigned i;

	memset(&c, 0, sizeof(c));
	c.stop = N2A32_STOP_UNDEF;
	c.stop_addr = 0x10008;
	c.stop_insn = 0xe7f00ffe;
	c.icount = 3;

	n2a32_trace_clear();
	n2a32_trace_mmio(c.mem, 0x40000000u, 4, 1, 0);
	CHECK(n2a32_trace_events() == 0, "MMIO event recorded with no CPU attached");
	n2a32_trace_attach(&c);
	n2a32_trace_set_elf((struct n2elf_set *)&c);
	CHECK(c.trace != NULL, "attach did not set c->trace");
	n2a32_trace_insn(&c, 0x10000, 0xe3a00001u, 0, 4);
	n2a32_trace_insn(&c, 0x10004, 0x2001u, 1, 2);
	n2a32_trace_insn(&c, 0x10006, 0xf000f8d0u, 1, 4);
	n2a32_trace_hostcall(&c, "memcpy", 0);
	n2a32_trace_hostcall(&c, "memcpy", 1);
	n2a32_trace_mmio(c.mem, 0x40000000u, 4, 0xdeadbeefu, 0);
	n2a32_trace_mmio(c.mem, 0x40000004u, 2, 0x1234u, 1);

	CHECK(n2a32_trace_events() == 7, "events=%llu want 7", (unsigned long long)n2a32_trace_events());

	snprintf(path, sizeof(path), "/tmp/n2hw-T01-trace-test-%ld.txt", (long)getpid());
	{
		FILE *out = fopen(path, "w");
		CHECK(out != NULL, "fopen dump");
		if (out) {
			n2a32_trace_dump(&c, out);
			fclose(out);
		}
	}
	text = read_all(path);
	CHECK(text != NULL, "read dump");
	if (text) {
		CHECK(strstr(text, "i A 00010000 e3a00001 fake+0x0") != NULL, "A32 line: %s", text);
		CHECK(strstr(text, "i T 00010004 2001 fake+0x4") != NULL, "T32 16-bit line");
		CHECK(strstr(text, "i T 00010006 f000f8d0 fake+0x6") != NULL, "T32 32-bit line");
		CHECK(strstr(text, "h call memcpy") != NULL, "hostcall call line");
		CHECK(strstr(text, "h ret  memcpy") != NULL, "hostcall ret line");
		CHECK(strstr(text, "m R 40000000 4 deadbeef") != NULL, "mmio read line");
		CHECK(strstr(text, "m W 40000004 2 00001234") != NULL, "mmio write line");
		CHECK(strstr(text, "# trace events=7 insn=3 hostcall=2 mmio=2 shown=7") != NULL, "counter line");
		CHECK(strstr(text, "# stop=UNDEF addr=00010008 insn=e7f00ffe icount=3 thumb=0") != NULL, "stop line");
		CHECK(count_prefix(text, "i ") == 3, "three insn lines");
	}
	free(text);
	remove(path);

	/* Overflow: 5000 events must leave exactly the last 4096, starting at event 904. */
	n2a32_trace_clear();
	for (i = 0; i < 5000; i++)
		n2a32_trace_insn(&c, 0x10000 + 4 * i, 0xe1a00000u, 0, 4);
	CHECK(n2a32_trace_events() == 5000, "events after overflow");
	{
		FILE *out = tmpfile();
		CHECK(out != NULL, "tmpfile");
		if (out) {
			n2a32_trace_dump(NULL, out);
			rewind(out);
			text = malloc(1u << 20);
			CHECK(text != NULL, "buffer");
			if (text) {
				size_t n = fread(text, 1, (1u << 20) - 1, out);
				text[n] = '\0';
				CHECK(count_prefix(text, "i ") == 4096, "ring keeps 4096, got %u",
				      count_prefix(text, "i "));
				CHECK(strstr(text, "i A 00010e20") != NULL, "oldest kept is event 904 (0x10000+904*4)");
				CHECK(strstr(text, "i A 00010000 ") == NULL, "event 0 dropped");
			}
			free(text);
			fclose(out);
		}
	}

	/* Full trace file. */
	snprintf(path, sizeof(path), "/tmp/n2hw-T01-trace-full-%ld.txt", (long)getpid());
	CHECK(n2a32_trace_open(path) == 0, "trace_open");
	n2a32_trace_clear();
	n2a32_trace_insn(&c, 0x20000, 0xe1a01002u, 0, 4);
	n2a32_trace_hostcall(&c, "puts", 0);
	n2a32_trace_close();
	text = read_all(path);
	CHECK(text != NULL, "read full trace");
	if (text) {
		CHECK(count_prefix(text, "i ") == 1, "full trace has one insn");
		CHECK(strstr(text, "h call puts") != NULL, "full trace has hostcall");
	}
	free(text);
	remove(path);

	/* Symboliser cleared: no crash, line uses '-'. */
	n2a32_trace_set_elf(NULL);
	n2a32_trace_clear();
	n2a32_trace_insn(&c, 0x1234, 0xe1a00000u, 0, 4);
	{
		FILE *out = tmpfile();
		CHECK(out != NULL, "tmpfile 2");
		if (out) {
			n2a32_trace_dump(NULL, out);
			rewind(out);
			text = malloc(4096);
			if (text) {
				size_t n = fread(text, 1, 4095, out);
				text[n] = '\0';
				CHECK(strstr(text, "i A 00001234 e1a00000 -") != NULL, "no symbol -> '-'");
			}
			free(text);
			fclose(out);
		}
	}

	n2a32_trace_detach(&c);
	CHECK(c.trace == NULL, "detach");
	n2a32_trace_clear();

	printf("T01 trace: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
