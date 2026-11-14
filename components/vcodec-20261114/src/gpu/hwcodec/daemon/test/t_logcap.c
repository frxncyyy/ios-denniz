/* SPDX-License-Identifier: MIT */
/*
 * t_logcap.c -- host test of daemon/n2vcd_logcap.h (vcodecd.log size cap, opus-robust 12 Nov 2026).
 *
 *   t_logcap DIR     (DIR is created/emptied; everything happens there)
 *
 * The process points its own fd 2 at DIR/vcodecd.log exactly as launchd does for the daemon, then checks: no cap for a
 * NULL path / cap 0 / an fd that is not the file; rotation at start of an oversized file (old bytes intact in .1,
 * marker line in the new file, fd 1 and fd 2 both follow); rotation while running with no torn or reordered lines and
 * the newest line always in the live file; four writer threads (run under TSan too); a failed rotation (read-only
 * directory) stops trying and keeps logging.
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "n2vcd_logcap.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_checks, g_fail;
static FILE *g_out;                     /* stdout dup: fd 1/2 are redirected during the test */

#define CHECK(c) do { g_checks++; if (!(c)) { g_fail++; fprintf(g_out, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static char g_dir[400], g_log[480], g_log1[490];

static long fsize(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

static int same_file(int fd, const char *p)
{
	struct stat a, b;

	return fstat(fd, &a) == 0 && stat(p, &b) == 0 && a.st_ino == b.st_ino && a.st_dev == b.st_dev;
}

static void redirect(const char *p, int trunc)
{
	int fd = open(p, O_WRONLY | O_CREAT | O_APPEND | (trunc ? O_TRUNC : 0), 0644);

	(void)dup2(fd, 2);
	(void)dup2(fd, 1);
	(void)close(fd);
}

static void log_line(struct n2vcd_logcap *c, const char *tag, long n)
{
	char buf[160];
	int len = snprintf(buf, sizeof(buf), "N2VCD %s %08ld ................................................................\n",
	                   tag, n);

	(void)!write(2, buf, (size_t)len);
	n2vcd_logcap_account(c, (size_t)len);
}

/* Read a file's "N2VCD <tag> <n>" lines: count, first and last n, and whether they are consecutive and untorn. */
struct scan { long lines, first, last, torn, gaps, markers; };

static struct scan scan_file(const char *p, const char *tag)
{
	struct scan s = { 0, -1, -1, 0, 0, 0 };
	FILE *f = fopen(p, "r");
	char line[512];
	char want[64];

	snprintf(want, sizeof(want), "N2VCD %s ", tag);
	if (!f)
		return s;
	while (fgets(line, sizeof(line), f)) {
		long n;
		size_t L = strlen(line);

		if (strncmp(line, "N2VCD log rotated:", 18) == 0) {
			s.markers++;
			continue;
		}
		if (strncmp(line, want, strlen(want)) != 0 || L < 20 || line[L - 1] != '\n') {
			s.torn++;
			continue;
		}
		n = strtol(line + strlen(want), NULL, 10);
		if (s.last >= 0 && n != s.last + 1)
			s.gaps++;
		if (s.first < 0)
			s.first = n;
		s.last = n;
		s.lines++;
	}
	fclose(f);
	return s;
}

static void test_not_armed(void)
{
	struct n2vcd_logcap c;
	char note[700];

	redirect(g_log, 1);
	CHECK(n2vcd_logcap_init(&c, NULL, 4096, 2, note, sizeof(note)) == 0);
	CHECK(n2vcd_logcap_init(&c, g_log, 0, 2, note, sizeof(note)) == 0);
	redirect("/dev/null", 0);
	CHECK(n2vcd_logcap_init(&c, g_log, 4096, 2, note, sizeof(note)) == 0);
	CHECK(strstr(note, "is not") != NULL);
	log_line(&c, "x", 1);                   /* not armed: account is a no-op */
	CHECK(atomic_load(&c.rotations) == 0);
}

static void test_start_rotation(void)
{
	struct n2vcd_logcap c;
	char note[700];
	long big = 5L * 1024 * 1024 + 123;
	FILE *f;
	long i;

	unlink(g_log1);
	f = fopen(g_log, "w");
	for (i = 0; i < big; i++)
		fputc(i % 61 == 60 ? '\n' : 'a' + (int)(i % 26), f);
	fclose(f);
	redirect(g_log, 0);                     /* launchd appends to the existing file */
	CHECK(n2vcd_logcap_init(&c, g_log, N2VCD_LOGCAP_DEFAULT_MAX, 2, note, sizeof(note)) == 1);
	CHECK(strstr(note, "rotated 5243003 bytes at start") != NULL);
	CHECK(fsize(g_log1) == big);            /* the old file, byte for byte in size */
	CHECK(same_file(2, g_log));             /* fd 2 follows the new file */
	CHECK(same_file(1, g_log));             /* fd 1 too (StandardOutPath = the same file) */
	CHECK(fsize(g_log) > 0 && fsize(g_log) < 200);
	log_line(&c, "s", 0);
	{
		struct scan s = scan_file(g_log, "s");
		CHECK(s.markers == 1 && s.lines == 1 && s.torn == 0);
	}
	/* below the cap: armed, no rotation */
	redirect(g_log, 1);
	CHECK(n2vcd_logcap_init(&c, g_log, N2VCD_LOGCAP_DEFAULT_MAX, 2, note, sizeof(note)) == 1);
	CHECK(atomic_load(&c.rotations) == 0);
}

static void test_runtime_rotation(void)
{
	struct n2vcd_logcap c;
	char note[700];
	long i, n = 2000;                       /* ~110 bytes each: ~220 KB through a 16 KB cap */

	unlink(g_log1);
	redirect(g_log, 1);
	CHECK(n2vcd_logcap_init(&c, g_log, 16384, 2, note, sizeof(note)) == 1);
	for (i = 0; i < n; i++)
		log_line(&c, "r", i);
	CHECK(atomic_load(&c.rotations) >= 10);
	CHECK(fsize(g_log) <= 16384 + 200);
	CHECK(fsize(g_log1) <= 16384 + 200 && fsize(g_log1) > 8000);
	{
		struct scan a = scan_file(g_log1, "r"), b = scan_file(g_log, "r");
		CHECK(a.torn == 0 && b.torn == 0 && a.gaps == 0 && b.gaps == 0);
		CHECK(b.last == n - 1);             /* the newest line is in the live file */
		CHECK(a.last + 1 == b.first);       /* .1 ends exactly where the live file starts */
		CHECK(b.markers == 1);
	}
}

struct thr { struct n2vcd_logcap *c; int id; };

static void *writer(void *arg)
{
	struct thr *t = (struct thr *)arg;
	char tag[8];
	long i;

	snprintf(tag, sizeof(tag), "t%d", t->id);
	for (i = 0; i < 1500; i++)
		log_line(t->c, tag, i);
	return NULL;
}

static void test_threads(void)
{
	struct n2vcd_logcap c;
	struct thr t[4];
	pthread_t th[4];
	char note[700];
	int i;

	unlink(g_log1);
	redirect(g_log, 1);
	CHECK(n2vcd_logcap_init(&c, g_log, 65536, 2, note, sizeof(note)) == 1);
	for (i = 0; i < 4; i++) {
		t[i].c = &c;
		t[i].id = i;
		pthread_create(&th[i], NULL, writer, &t[i]);
	}
	for (i = 0; i < 4; i++)
		pthread_join(th[i], NULL);
	CHECK(atomic_load(&c.rotations) >= 3);
	/* While one thread rotates, the others keep appending to the old descriptor (now .1): a bounded overshoot. */
	CHECK(fsize(g_log) <= 2 * 65536);
	CHECK(fsize(g_log1) <= 2 * 65536);
	{
		/* .1 then the live file: no torn line, and every thread's numbers only go up (nothing reordered) */
		const char *files[2] = { g_log1, g_log };
		long last[4] = { -1, -1, -1, -1 }, lines = 0;
		int torn = 0, back = 0, k;
		char line[512];

		for (k = 0; k < 2; k++) {
			FILE *f = fopen(files[k], "r");
			while (f && fgets(line, sizeof(line), f)) {
				int id;
				long n;
				if (strncmp(line, "N2VCD log rotated:", 18) == 0)
					continue;
				if (sscanf(line, "N2VCD t%d %ld", &id, &n) != 2 || id < 0 || id > 3 ||
				    line[strlen(line) - 1] != '\n') {
					torn++;
					continue;
				}
				if (n <= last[id])
					back++;
				last[id] = n;
				lines++;
			}
			if (f)
				fclose(f);
		}
		CHECK(torn == 0);
		CHECK(back == 0);
		CHECK(lines > 65536 / 100);
	}
}

static void test_rotation_failure(void)
{
	struct n2vcd_logcap c;
	char note[700];
	long i, before;

	if (geteuid() == 0) {
		fprintf(g_out, "t_logcap: running as root, read-only directory check skipped\n");
		return;
	}
	unlink(g_log1);
	redirect(g_log, 1);
	CHECK(n2vcd_logcap_init(&c, g_log, 4096, 2, note, sizeof(note)) == 1);
	chmod(g_dir, 0555);                     /* rename() will fail */
	for (i = 0; i < 200; i++)
		log_line(&c, "f", i);
	chmod(g_dir, 0755);
	CHECK(atomic_load(&c.off) == 1);
	CHECK(atomic_load(&c.rotations) == 0);
	before = fsize(g_log);
	CHECK(before > 4096 * 4);               /* nothing lost: logging went on in the same file */
	{
		struct scan s = scan_file(g_log, "f");
		CHECK(s.lines == 200 && s.gaps == 0 && s.torn == 0);
	}
}

int main(int argc, char **argv)
{
	int out_fd;

	if (argc != 2) {
		fprintf(stderr, "usage: t_logcap DIR\n");
		return 2;
	}
	out_fd = dup(1);
	g_out = fdopen(out_fd, "w");
	setvbuf(g_out, NULL, _IONBF, 0);
	snprintf(g_dir, sizeof(g_dir), "%s", argv[1]);
	mkdir(g_dir, 0755);
	snprintf(g_log, sizeof(g_log), "%s/vcodecd.log", g_dir);
	snprintf(g_log1, sizeof(g_log1), "%s/vcodecd.log.1", g_dir);
	fprintf(g_out, "t_logcap: vcodecd.log cap in %s\n", g_dir);
	test_not_armed();
	test_start_rotation();
	test_runtime_rotation();
	test_threads();
	test_rotation_failure();
	redirect("/dev/null", 0);
	if (g_fail) {
		fprintf(g_out, "N2TEST LOGCAP FAIL %d/%d\n", g_checks - g_fail, g_checks);
		return 1;
	}
	fprintf(g_out, "N2TEST LOGCAP PASS %d/%d\n", g_checks, g_checks);
	return 0;
}
