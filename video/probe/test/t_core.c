/*
 * t_core.c -- host test of the probe's runner and portable helpers.  Linux/POSIX; no Apple call.
 *
 * Runs a fixed set of six tests through probe_run() (pass, fail, skip, hang, crash, escaping), checks the JSON lines
 * it wrote, then checks the deterministic helpers against hand-computed values (RFC 1321 test vectors, the literal
 * samples of probe_fill_nv12, the PSNR of two known pictures) and the output numbering.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include "probe_core.h"

#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* probe_core.c, deliberately not in the frozen header (probe_main.c declares it the same way). */
unsigned probe_next_index(const char *dir);

static int checks, failures;

static void check(int ok, const char *what)
{
	checks++;
	if (!ok)
		failures++;
	printf("%s %d - %s\n", ok ? "ok" : "not ok", checks, what);
	fflush(stdout);
}

/* ------------------------------------------------------------------ the six tests ----------------------------------------- */

static int t_pass(struct probe_ctx *c)
{
	probe_say(c, "answer", "%d", 42);
	probe_say(c, "pi", "%.2f", 3.14);
	return 0;
}

static int t_fail(struct probe_ctx *c)
{
	probe_say(c, "why", "because");
	return 1;
}

static int t_skip(struct probe_ctx *c)
{
	probe_say(c, "reason", "not here");
	return 2;
}

static int t_hang(struct probe_ctx *c)
{
	(void)c;
	for (;;)
		sleep(100);
}

static int t_crash(struct probe_ctx *c)
{
	probe_say(c, "before", "the crash");
	raise(SIGKILL);
	return 0;
}

static int t_escape(struct probe_ctx *c)
{
	probe_say(c, "s", "a\"b\\c%sd%se", "\n", "\t");
	return 0;
}

static int t_long(struct probe_ctx *c)
{
	char big[5000];
	memset(big, 'x', sizeof big - 1);
	big[sizeof big - 1] = 0;
	probe_say(c, "long", "%s", big);
	return 0;
}

static int t_many(struct probe_ctx *c)
{
	int i;
	char key[16];
	for (i = 0; i < 200; i++) {
		snprintf(key, sizeof key, "k%d", i);
		probe_say(c, key, "%d", i);
	}
	return 0;
}

static const struct probe_test tests[] = {
	{ "t.pass",   t_pass,   5 },
	{ "t.fail",   t_fail,   5 },
	{ "t.skip",   t_skip,   5 },
	{ "t.hang",   t_hang,   1 },
	{ "t.crash",  t_crash,  5 },
	{ "t.escape", t_escape, 5 },
	{ "t.long",   t_long,   5 },
	{ "t.many",   t_many,   5 },
};
#define NTESTS (sizeof tests / sizeof tests[0])

/* ------------------------------------------------------------------ helpers ------------------------------------------------ */

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *p;
	long n;

	if (!f)
		return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0) {
		fclose(f);
		return NULL;
	}
	p = malloc((size_t)n + 1);
	if (!p) {
		fclose(f);
		return NULL;
	}
	if (fread(p, 1, (size_t)n, f) != (size_t)n) {
		free(p);
		fclose(f);
		return NULL;
	}
	p[n] = 0;
	fclose(f);
	if (len)
		*len = (size_t)n;
	return p;
}

/* count the lines that contain needle and no other line does */
static int count_lines_with(const char *text, const char *needle)
{
	int n = 0;
	const char *p = text;

	while ((p = strstr(p, needle)) != NULL) {
		n++;
		p += strlen(needle);
	}
	return n;
}

static int line_for(const char *text, const char *test, char *out, size_t cap)
{
	const char *p = text;

	while (p && *p) {
		const char *e = strchr(p, '\n');
		size_t n = e ? (size_t)(e - p) : strlen(p);
		char tmp[4096];
		if (n >= sizeof tmp)
			n = sizeof tmp - 1;
		memcpy(tmp, p, n);
		tmp[n] = 0;
		if (strstr(tmp, test)) {
			if (n >= cap)
				return -1;
			memcpy(out, tmp, n + 1);
			return 0;
		}
		p = e ? e + 1 : NULL;
	}
	return -1;
}

static int write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;
	fputs(text, f);
	fclose(f);
	return 0;
}

/* ------------------------------------------------------------------ main --------------------------------------------------- */

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char outpath[1024], text[65536], line[4096];
	size_t len = 0;
	char hex[33];
	uint8_t a[64 * 32], b[64 * 32];
	int rc;

	snprintf(outpath, sizeof outpath, "%s/probe-1.jsonl", dir);
	unlink(outpath);
	rc = probe_run(tests, NTESTS, outpath);
	check(rc == 1, "probe_run returns non-zero when a test fails");

	{
		char *t = slurp(outpath, &len);
		check(t != NULL, "the runner wrote the output file");
		if (!t) {
			printf("1..%d\n", checks);
			return 1;
		}
		snprintf(text, sizeof text, "%s", t);
		free(t);
	}
	check(count_lines_with(text, "\"test\":") == (int)NTESTS, "one JSON line per test");
	check(count_lines_with(text, "\"result\":\"pass\"") == 4, "the four passing tests are the only pass lines");
	check(count_lines_with(text, "\"result\":\"fail\"") == 1, "exactly one fail line");
	check(count_lines_with(text, "\"result\":\"skip\"") == 1, "exactly one skip line");
	check(count_lines_with(text, "\"result\":\"timeout\"") == 1, "exactly one timeout line");
	check(count_lines_with(text, "\"result\":\"crash\"") == 1, "exactly one crash line");
	check(strstr(text, "a\\\"b\\\\c\\nd\\te") != NULL, "quote, backslash, newline and tab are JSON-escaped");
	check(line_for(text, "t.pass", line, sizeof line) == 0, "the pass line is found by name");
	check(strstr(line, "\"answer\":\"42\"") != NULL, "fields carry the formatted value (answer=42)");
	check(strstr(line, "\"pi\":\"3.14\"") != NULL, "printf formatting reaches the field (pi=3.14)");
	check(line_for(text, "t.hang", line, sizeof line) == 0 && strstr(line, "\"signal\":9") != NULL,
	      "the timeout line reports the kill signal 9");
	check(line_for(text, "t.crash", line, sizeof line) == 0 && strstr(line, "\"before\":\"the crash\"") != NULL,
	      "fields written before a crash are kept");
	check(line_for(text, "t.skip", line, sizeof line) == 0 && strstr(line, "\"reason\":\"not here\"") != NULL,
	      "the skip line carries its reason");
	check(line_for(text, "t.long", line, sizeof line) == 0 && strstr(line, "\"long\":\"xxx") != NULL,
	      "a 5000-character field is truncated, not dropped");
	check(line_for(text, "t.many", line, sizeof line) == 0, "a test with 200 fields is in the file");

	/* the console file (N2VIDEO_CONSOLE is set by run_tests.sh; without it this reads /dev/console and fails) */
	{
		const char *cp = getenv("N2VIDEO_CONSOLE");
		if (cp && *cp) {
			char *t = slurp(cp, &len);
			check(t != NULL, "the console line file exists");
			if (t) {
				check(count_lines_with(t, "N2VIDEO probe: t.pass pass") == 1,
				      "console line for a passing test");
				check(count_lines_with(t, "N2VIDEO probe: t.hang timeout") == 1,
				      "console line for a timeout");
				free(t);
			}
		}
	}

	/* MD5: RFC 1321 section A.5 */
	probe_md5("", 0, hex);
	check(strcmp(hex, "d41d8cd98f00b204e9800998ecf8427e") == 0, "md5(\"\") is d41d8cd98f00b204e9800998ecf8427e");
	probe_md5("abc", 3, hex);
	check(strcmp(hex, "900150983cd24fb0d6963f7d28e17f72") == 0, "md5(\"abc\") is 900150983cd24fb0d6963f7d28e17f72");
	probe_md5("message digest", 14, hex);
	check(strcmp(hex, "f96b697d7cb7938d525a2f31aaf161d0") == 0, "md5(\"message digest\") matches RFC 1321");
	{
		/* 1,000,000 'a' -- the long RFC 1321 vector, exercises chunking and the length field */
		char *big = malloc(1000000);
		if (big) {
			memset(big, 'a', 1000000);
			probe_md5(big, 1000000, hex);
			check(strcmp(hex, "7707d6ae4e027c70eea2a935c2296f21") == 0,
			      "md5 of one million 'a' matches RFC 1321");
			free(big);
		}
	}

	/* probe_fill_nv12: the literal samples of the task */
	{
		uint8_t y[32 * 32], uv[16 * 32];
		memset(y, 0xAA, sizeof y);
		memset(uv, 0xAA, sizeof uv);
		probe_fill_nv12(y, 32, uv, 32, 32, 32, 0);
		check(y[0 * 32 + 0] == 0, "nv12 frame 0 Y(0,0) = 0");
		check(y[7 * 32 + 5] == 19, "nv12 frame 0 Y(5,7) = 19");
		check(y[20 * 32 + 20] == 235, "nv12 frame 0 Y(20,20) = 235 (the square)");
		check(uv[0 * 32 + 0] == 128, "nv12 frame 0 Cb(0,0) = 128");
		check(uv[3 * 32 + 1] == 125, "nv12 frame 0 Cr(0,3) = 125");
		/* frame 1 moves the square to (20,18): (16,16) is picture again, (20,18) is 235 */
		probe_fill_nv12(y, 32, uv, 32, 32, 32, 1);
		check(y[16 * 32 + 16] == (uint8_t)((16 + 32 + 3) & 255), "nv12 frame 1 Y(16,16) is no longer the square");
		check(y[18 * 32 + 20] == 235, "nv12 frame 1 Y(20,18) is the square");
		check(uv[0 * 32 + 2] == 130, "nv12 frame 1 Cb(1,0) = 130");
	}

	/* PSNR: identical -> 99.0; differ by 1 everywhere -> 48.13 */
	{
		int w = 64, h = 32, x, yy;
		for (yy = 0; yy < h; yy++)
			for (x = 0; x < w; x++) {
				a[yy * w + x] = (uint8_t)(x + yy);
				b[yy * w + x] = (uint8_t)(x + yy + 1);
			}
		check(probe_psnr(a, w, a, w, w, h) == 99.0, "psnr of identical pictures is 99.0");
		{
			double p = probe_psnr(a, w, b, w, w, h);
			check(p > 48.12 && p < 48.14, "psnr of pictures differing by 1 is 48.13 +- 0.01");
		}
		/* one sample differing by 16 in 64x32: mse = 256/2048 = 0.125 -> 10*log10(65025/0.125) = 57.16 */
		memcpy(b, a, (size_t)w * h);
		b[0] = (uint8_t)(a[0] + 16);
		{
			double p = probe_psnr(a, w, b, w, w, h);
			check(p > 57.15 && p < 57.17, "psnr of a single 16-level error is 57.16");
		}
	}

	/* output numbering */
	{
		char d[1024], p[1200];
		DIR *dh;
		snprintf(d, sizeof d, "%s/num", dir);
		dh = opendir(d);
		if (dh) {
			struct dirent *de;
			while ((de = readdir(dh)) != NULL) {
				if (de->d_name[0] != '.') {
					snprintf(p, sizeof p, "%s/%s", d, de->d_name);
					unlink(p);
				}
			}
			closedir(dh);
		} else {
			mkdir(d, 0755);
		}
		check(probe_next_index(d) == 1, "numbering: an empty directory gives 1");
		snprintf(p, sizeof p, "%s/probe-1.jsonl", d);
		write_file(p, "{}\n");
		check(probe_next_index(d) == 2, "numbering: probe-1.jsonl gives 2");
		snprintf(p, sizeof p, "%s/probe-7.jsonl", d);
		write_file(p, "{}\n");
		check(probe_next_index(d) == 8, "numbering: probe-7.jsonl gives 8");
		snprintf(p, sizeof p, "%s/probe-12.jsonl", d);
		write_file(p, "{}\n");
		snprintf(p, sizeof p, "%s/probe-3.txt", d);
		write_file(p, "{}\n");
		snprintf(p, sizeof p, "%s/other-99.jsonl", d);
		write_file(p, "{}\n");
		snprintf(p, sizeof p, "%s/probe-x.jsonl", d);
		write_file(p, "{}\n");
		check(probe_next_index(d) == 13, "numbering: highest wins, non-matching names are ignored");
		snprintf(p, sizeof p, "%s/probe-1.jsonl", d);
		unlink(p);
		check(probe_next_index(d) == 13, "numbering: removing a low number does not lower the answer");
	}

	printf("1..%d\n", checks);
	if (failures) {
		printf("checks: %d of %d failed\n", failures, checks);
		return 1;
	}
	return 0;
}
