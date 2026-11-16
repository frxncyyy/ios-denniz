/*
 * Host test of nord2_applog.c (Linux, no phone): stand-ins for libsystem_trace's hook, the global block class, np_log/np_ready and the process name.
 *   cc -O1 -Wall -Wextra -rdynamic -o /tmp/applog_test gpu/y2s/nord2_applog_test.c -ldl -lpthread && /tmp/applog_test
 * What it cannot show: the real message layout (+0x58/+0x80/+0x88), the sandbox, dyld's behaviour inside the hook.
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *test_progname = "Camera";
static char test_marker[64], test_console[64];
static uint64_t test_clock = 1000;
#define getprogname()                   test_progname
#define CLOCK_UPTIME_RAW                0
#define clock_gettime_nsec_np(c)        (test_clock)
#define APPLOG_MARKER                   test_marker
#define APPLOG_CONSOLE                  test_console
#define APPLOG_LIBRARY                  NULL            /* dlopen(NULL): this executable, where the stand-ins are */
#define dlopen(path, mode)              (test_library_loaded ? dlopen(path, RTLD_NOW) : NULL)
static int test_library_loaded;

/* the stand-ins, found by dlsym as on the phone (-rdynamic) */
void *_NSConcreteGlobalBlock[4];
static void *test_hook, *test_previous_block;
static int test_set_hook_calls;
void *os_log_set_hook(uint8_t level, void *block) { assert(level == 1); test_hook = block; test_set_hook_calls++; return test_previous_block; }
struct test_msg { char pad[0x58]; const char *format; char pad2[0x80 - 0x60]; const char *subsystem, *category; const char *text; };
static int test_copies;
char *os_log_copy_message_string(void *msg) { test_copies++; return strdup(((struct test_msg *)msg)->text); }
static char test_out[9000][200];
static unsigned test_nout;
static int test_up;
int np_ready(void) { return test_up; }
void np_log(const char *text, size_t length)
{
	assert(length <= 170 && length > 0 && memchr(text, '\n', length) == NULL);   /* one kernel record, never cut by np_log itself */
	assert(memcmp(text, "CAMLOG", 6) == 0);
	memcpy(test_out[test_nout], text, length);
	test_out[test_nout++][length] = 0;
}

#include "nord2_applog.c"

static int previous_calls;
static void previous_invoke(void *block, uint8_t type, void *msg) { (void)block; (void)type; (void)msg; previous_calls++; }
static struct applog_block previous_block = { NULL, 0, 0, (void *)previous_invoke, NULL };

static void
say(uint8_t type, const char *format, const char *subsystem, const char *category, const char *text)
{
	struct test_msg m;

	memset(&m, 0, sizeof(m));
	m.format = format; m.subsystem = subsystem; m.category = category; m.text = text;
	((void (*)(void *, uint8_t, void *))((struct applog_block *)test_hook)->invoke)(test_hook, type, &m);
}

int
main(void)
{
	char big[700];
	unsigned n;

	assert(__builtin_offsetof(struct test_msg, subsystem) == 0x80 && __builtin_offsetof(struct test_msg, category) == 0x88);
	snprintf(test_marker, sizeof(test_marker), "/tmp/applog-test-marker-%d", (int)getpid());
	snprintf(test_console, sizeof(test_console), "/tmp/applog-test-console-%d", (int)getpid());
	unlink(test_marker); unlink(test_console);

	/* 1. inert: another process with the marker, the Camera without it */
	fclose(fopen(test_marker, "w"));
	test_progname = "SpringBoard"; nord2_applog_install(); assert(test_set_hook_calls == 0);
	test_progname = "Camera2"; nord2_applog_install(); assert(test_set_hook_calls == 0);
	test_progname = "camera"; nord2_applog_install(); assert(test_set_hook_calls == 0);
	unlink(test_marker);
	test_progname = "Camera"; nord2_applog_install(); assert(test_set_hook_calls == 0 && applog_npending == 0 && applog_console == -1);

	/* 2. Camera with the marker: hook set, the earlier hook kept, the first line queued (no library yet); no console file here: refused */
	fclose(fopen(test_marker, "w"));
	test_previous_block = &previous_block;
	nord2_applog_install();
	assert(test_set_hook_calls == 1 && test_hook == &applog_block && applog_previous == &previous_block && applog_console == -1);
	assert(applog_block.isa == _NSConcreteGlobalBlock && applog_block.flags == 1 << 28 && test_nout == 0 && applog_npending == 1);

	/* 3. the filter; the earlier hook sees every message */
	say(0, "plain %d", "com.apple.UIKit", "x", "not ours");
	say(2, "plain", NULL, NULL, "not ours either");
	say(0, NULL, "com.apple.camera", NULL, "no format");
	assert(test_copies == 0 && previous_calls == 3 && applog_npending == 1);
	say(0, "a %d", "com.apple.camera", "Capture", "one\ntwo\rthree");           /* 1 */
	say(1, "b", "com.apple.coremedia", NULL, "coremedia line");                 /* 2 */
	say(0, "c", "com.apple.AVFCapture", "s", "avf line");                       /* 3 */
	say(0, "d", "com.apple.avfoundation", "s", "avfoundation line");            /* 4 */
	say(0, "<<<< FigCaptureSession >>>> x", NULL, NULL, "<<<< FigCaptureSession >>>> x");       /* 5 */
	say(16, "e", "com.apple.UIKit", "err", "an error");                        /* 6 */
	say(17, "f", NULL, NULL, "a fault");                                        /* 7 */
	say(0, "<<< three only", NULL, NULL, "no");
	assert(test_copies == 7 && previous_calls == 11 && test_nout == 0 && applog_npending == 8);

	/* 4. the library appears but its connection is not up; then it is: everything leaves in order at the next message of any kind */
	test_clock += APPLOG_RETRY_NS; test_library_loaded = 1;
	say(0, "plain", "com.apple.UIKit", "x", "not ours");
	assert(test_nout == 0 && applog_np_log == np_log && applog_np_ready == np_ready);
	test_up = 1;
	say(0, "plain", "com.apple.UIKit", "x", "not ours");
	assert(test_nout == 8 && applog_npending == 0 && test_copies == 7);
	{
		char want[96];

		snprintf(want, sizeof(want), "CAMLOG: hook installed in Camera pid %d (console no)", (int)getpid());
		assert(strcmp(test_out[0], want) == 0);
	}
	assert(strcmp(test_out[1], "CAMLOG 1 0 [com.apple.camera/Capture] one two three") == 0);
	assert(strcmp(test_out[2], "CAMLOG 2 1 [com.apple.coremedia/-] coremedia line") == 0);
	assert(strcmp(test_out[5], "CAMLOG 5 0 [-/-] <<<< FigCaptureSession >>>> x") == 0);
	assert(strcmp(test_out[6], "CAMLOG 6 16 [com.apple.UIKit/err] an error") == 0);
	assert(strcmp(test_out[7], "CAMLOG 7 17 [-/-] a fault") == 0);

	/* 5. a long message: 300 bytes of text at most, in records of 160 that each say CAMLOG and the number; long names cut */
	for (unsigned i = 0; i < sizeof(big) - 1; i++) {
		big[i] = (char)('a' + i % 26);
	}
	big[sizeof(big) - 1] = 0;
	n = test_nout;
	say(16, "g", "com.apple.a-very-long-subsystem-name.camera", "a-very-long-category-name", big);
	assert(test_nout > n + 1);
	{
		char joined[1200] = "";
		const char *head = "CAMLOG 8 16 [com.apple.a-very-long-su/a-very-long-cate] ";

		assert(strncmp(test_out[n], head, strlen(head)) == 0 && strlen(test_out[n]) == APPLOG_PIECE);
		strcat(joined, test_out[n] + strlen(head));
		for (unsigned i = n + 1; i < test_nout; i++) {
			assert(strncmp(test_out[i], "CAMLOG 8 + ", 11) == 0 && strlen(test_out[i]) <= APPLOG_PIECE);
			strcat(joined, test_out[i] + 11);
		}
		assert(strlen(joined) == APPLOG_TEXT && strncmp(joined, big, APPLOG_TEXT) == 0);
	}
	say(16, "h", NULL, NULL, "");                                               /* an empty text is still a line */
	assert(strcmp(test_out[test_nout - 1], "CAMLOG 9 16 [-/-] ") == 0);

	/* 6. the limit: APPLOG_LINES messages, a last note, then no copy is even made; the earlier hook still sees everything */
	n = previous_calls;
	for (unsigned i = 0; i < APPLOG_LINES + 50; i++) {
		say(16, "i", NULL, NULL, "x");
	}
	assert(applog_lines == APPLOG_LINES && test_copies == APPLOG_LINES && previous_calls == (int)n + APPLOG_LINES + 50);
	assert(strcmp(test_out[test_nout - 1], "CAMLOG: line limit reached, nothing more is forwarded") == 0);
	{
		char want[40];

		snprintf(want, sizeof(want), "CAMLOG %u 16 [-/-] x", APPLOG_LINES);
		assert(strcmp(test_out[test_nout - 2], want) == 0);
	}

	/* 7. the queue overflows while np_log is away: the first APPLOG_PENDING records are kept, the rest counted */
	applog_lines = 0; test_up = 0; n = test_nout;
	for (unsigned i = 0; i < APPLOG_PENDING + 30; i++) {
		say(16, "j", NULL, NULL, "y");
	}
	assert(test_nout == n && applog_npending == APPLOG_PENDING && applog_dropped == 30);
	test_up = 1;
	say(0, "plain", NULL, NULL, "not ours");
	assert(test_nout == n + APPLOG_PENDING + 1 && applog_npending == 0 && applog_dropped == 0);
	assert(strcmp(test_out[n], "CAMLOG 1 16 [-/-] y") == 0);
	assert(strcmp(test_out[test_nout - 1], "CAMLOG: 30 early records dropped (np_log was not ready)") == 0);

	/* 8. a message made inside the hook on the same thread is passed on, not taken */
	pthread_setspecific(applog_busy, &applog_busy);
	n = test_nout; say(17, "k", NULL, NULL, "inside");
	assert(test_nout == n);
	pthread_setspecific(applog_busy, NULL);

	/* 9. the console, when the process may open it: every record as a line of its own */
	{
		FILE *f;
		char l[256];

		fclose(fopen(test_console, "w"));
		applog_console = open(test_console, O_WRONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
		assert(applog_console >= 0);
		say(16, "l", "com.apple.camera", "c", "to the console");
		say(0, "plain", NULL, NULL, "not ours");
		close(applog_console); applog_console = -1;
		f = fopen(test_console, "r");
		assert(fgets(l, sizeof(l), f) != NULL && strcmp(l, "CAMLOG 127 16 [com.apple.camera/c] to the console\n") == 0 && fgets(l, sizeof(l), f) == NULL);
		fclose(f);
	}
	unlink(test_marker); unlink(test_console);
	printf("nord2_applog_test: all passed (%u records forwarded)\n", test_nout);
	return 0;
}
