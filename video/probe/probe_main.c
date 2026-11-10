/*
 * probe_main.c -- entry point of /usr/libexec/n2vt_probe on the phone.
 *
 *   - exits 0 silently unless /private/var/db/nord2-video-probe exists (the switch the installer creates);
 *   - waits 45 s so that it runs after SpringBoard and mediaserverd have settled, unless argv contains --now;
 *   - writes /private/var/db/nord2-video/probe-<n>.jsonl with n = 1 + the highest existing number;
 *   - runs the VideoToolbox, ImageIO and AudioToolbox tests, and the AVFoundation tests unless built with -DPROBE_NO_AV;
 *   - ends with one 'N2VIDEO probe: DONE pass=.. fail=.. skip=.. crash=.. file=..' console line.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include "probe_core.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define PROBE_VERSION "wp07-1.0"
#define PROBE_GATE    "/private/var/db/nord2-video-probe"
#define PROBE_DIR     "/private/var/db/nord2-video"

/* WP07's test arrays (probe_vt.c, probe_img.c, probe_audio.c). */
extern const struct probe_test probe_vt_tests[];
extern const unsigned probe_vt_tests_count;
extern const struct probe_test probe_img_tests[];
extern const unsigned probe_img_tests_count;
extern const struct probe_test probe_audio_tests[];
extern const unsigned probe_audio_tests_count;
#ifndef PROBE_NO_AV
/* WP06's file (probe/av/probe_av.c). */
extern const struct probe_test probe_av_tests[];
extern const unsigned probe_av_test_count;
#endif

/* probe_core.c: not in the frozen header, declared here and in test/t_core.c. */
unsigned probe_next_index(const char *dir);

static const char *console_path(void)
{
	static const char *path;
	if (!path) {
		const char *env = getenv("N2VIDEO_CONSOLE");
		path = (env && env[0]) ? env : "/dev/console";
	}
	return path;
}

static void say_line(const char *msg)
{
	const char *path = console_path();
	size_t n = strlen(msg);
	int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (fd < 0)
		return;
	while (n) {
		ssize_t w = write(fd, msg, n);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			break;
		}
		msg += w;
		n -= (size_t)w;
	}
	close(fd);
}

static int append_line(const char *path, const char *line)
{
	size_t n = strlen(line);
	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
	if (fd < 0)
		return -1;
	while (n) {
		ssize_t w = write(fd, line, n);
		if (w <= 0) {
			if (w < 0 && errno == EINTR)
				continue;
			close(fd);
			return -1;
		}
		line += w;
		n -= (size_t)w;
	}
	fsync(fd);
	close(fd);
	return 0;
}

/* count the result lines of the file we just wrote */
static void count_results(const char *path, unsigned *pass, unsigned *fail, unsigned *skip,
                          unsigned *crash, unsigned *tests)
{
	FILE *f = fopen(path, "r");
	char line[65536];

	*pass = *fail = *skip = *crash = *tests = 0;
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		if (!strstr(line, "\"test\":"))
			continue;
		(*tests)++;
		if (strstr(line, "\"result\":\"pass\""))
			(*pass)++;
		else if (strstr(line, "\"result\":\"fail\""))
			(*fail)++;
		else if (strstr(line, "\"result\":\"skip\""))
			(*skip)++;
		else
			(*crash)++;
	}
	fclose(f);
}

extern const char *probe_exec_path;
extern int probe_child(const struct probe_test *tests, unsigned count, const char *name, int fd);

int main(int argc, char **argv)
{
	char outpath[512], header[2048], done[1024], iso[64];
	struct utsname un;
	struct tm tmv;
	time_t now;
	unsigned pass, fail, skip, crash, tests;
	int i, now_flag = 0;
	int rc = 0;

	if (argc == 4 && strcmp(argv[1], "--child") == 0) {     /* (lead) one test, exec'ed by the runner: see probe_core.c */
		int fd = 0;
		const char *d;
		for (d = argv[3]; *d >= '0' && *d <= '9'; d++)
			fd = fd * 10 + (*d - '0');
		int r = probe_child(probe_vt_tests, probe_vt_tests_count, argv[2], fd);
		if (r < 0)
			r = probe_child(probe_img_tests, probe_img_tests_count, argv[2], fd);
		if (r < 0)
			r = probe_child(probe_audio_tests, probe_audio_tests_count, argv[2], fd);
#ifndef PROBE_NO_AV
		if (r < 0)
			r = probe_child(probe_av_tests, probe_av_test_count, argv[2], fd);
#endif
		_exit(r < 0 ? 1 : r);
	}
	if (access(PROBE_GATE, F_OK) != 0)
		return 0;
#ifdef __APPLE__
	probe_exec_path = "/usr/libexec/n2vt_probe";
#endif
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--now") == 0)
			now_flag = 1;
	}
	if (!now_flag)
		sleep(45);

	if (mkdir(PROBE_DIR, 0777) != 0 && errno != EEXIST) {
		snprintf(done, sizeof done, "N2VIDEO probe: DONE pass=0 fail=0 skip=0 crash=0 file=ERR(mkdir %s)\n",
		         PROBE_DIR);
		say_line(done);
		return 1;
	}
	snprintf(outpath, sizeof outpath, "%s/probe-%u.jsonl", PROBE_DIR, probe_next_index(PROBE_DIR));

	now = time(NULL);
	if (gmtime_r(&now, &tmv))
		snprintf(iso, sizeof iso, "%04d-%02d-%02dT%02d:%02d:%02dZ", tmv.tm_year + 1900, tmv.tm_mon + 1,
		         tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
	else
		snprintf(iso, sizeof iso, "?");
	if (uname(&un) != 0) {
		memset(&un, 0, sizeof un);
		snprintf(un.sysname, sizeof un.sysname, "?");
		snprintf(un.release, sizeof un.release, "?");
		snprintf(un.machine, sizeof un.machine, "?");
	}
	snprintf(header, sizeof header,
	         "{\"probe\":\"%s\",\"kind\":\"header\",\"time\":%lld,\"time_iso\":\"%s\","
	         "\"sysname\":\"%s\",\"release\":\"%s\",\"machine\":\"%s\",\"pid\":%ld}\n",
	         PROBE_VERSION, (long long)now, iso, un.sysname, un.release, un.machine, (long)getpid());
	if (append_line(outpath, header) != 0) {
		snprintf(done, sizeof done, "N2VIDEO probe: DONE pass=0 fail=0 skip=0 crash=0 file=ERR(open %s)\n", outpath);
		say_line(done);
		return 1;
	}

	rc |= probe_run(probe_vt_tests, probe_vt_tests_count, outpath);
	rc |= probe_run(probe_img_tests, probe_img_tests_count, outpath);
	rc |= probe_run(probe_audio_tests, probe_audio_tests_count, outpath);
#ifndef PROBE_NO_AV
	rc |= probe_run(probe_av_tests, probe_av_test_count, outpath);
#endif

	count_results(outpath, &pass, &fail, &skip, &crash, &tests);
	snprintf(done, sizeof done, "N2VIDEO probe: DONE pass=%u fail=%u skip=%u crash=%u file=%s\n",
	         pass, fail, skip, crash, outpath);
	say_line(done);
	/* (lead) one run per installation: the gate is taken away after a complete run, so the probe does not run again at the
	 * following boots (the installer puts the gate back). /private/var/db/nord2-video-probe-always keeps it. */
	if (access("/private/var/db/nord2-video-probe-always", F_OK) != 0) {
		if (unlink(PROBE_GATE) == 0)
			say_line("N2VIDEO probe: gate removed (one run per installation)\n");
	}
	fwrite(done, 1, strlen(done), stderr);
	fflush(stderr);

	{
		char endline[512];
		snprintf(endline, sizeof endline,
		         "{\"kind\":\"end\",\"probe\":\"%s\",\"tests\":%u,\"pass\":%u,\"fail\":%u,\"skip\":%u,"
		         "\"crash\":%u,\"file\":\"%s\"}\n",
		         PROBE_VERSION, tests, pass, fail, skip, crash, outpath);
		append_line(outpath, endline);
	}
	/* The probe is a measurement tool: a failing test is a result, not an error of the job.  Only an output file
	 * that could not be opened (probe_run returns -1) makes the job itself fail. */
	return rc < 0 ? 1 : 0;
}
