/* SPDX-License-Identifier: MIT */
/*
 * n2vt_log.c -- one line to /dev/console, at most N2VT_LOG_MAX_LINES per process, thread-safe, never loud.
 *
 * The numbers in N2_O_* are Darwin's, and they are what the phone's open() gets; the host test links this file with the
 * host's open() renamed to a test double and checks that exactly these flags are asked for. On the phone the values are
 * checked against <fcntl.h> at compile time, so a wrong copy cannot survive.
 */
#include "n2vt_abi.h"
#include "n2vt_log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

/* libSystem's getprogname(); the host build has no header for it, and the test renames it to a double. */
extern const char *getprogname(void);

#define N2_O_WRONLY   0x0001
#define N2_O_NONBLOCK 0x0004
#define N2_O_NOCTTY   0x20000
#define N2_F_OK       0

#if defined(__APPLE__) && !defined(N2_HOST_FAKE)
_Static_assert(O_WRONLY == N2_O_WRONLY, "Darwin O_WRONLY");
_Static_assert(O_NONBLOCK == N2_O_NONBLOCK, "Darwin O_NONBLOCK");
_Static_assert(O_NOCTTY == N2_O_NOCTTY, "Darwin O_NOCTTY");
_Static_assert(F_OK == N2_F_OK, "Darwin F_OK");
_Static_assert(sizeof(pthread_mutex_t) == 64, "Darwin pthread_mutex_t is 64 bytes");  /* sysinc's layout vs the real header */
#endif

#define N2_CONSOLE "/dev/console"
#define N2_LINE_MAX 480
#define N2_TEXT_MAX 400

static pthread_mutex_t n2_log_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned       n2_log_lines;         /* lines written, counting the suppression line */
static int            n2_log_quiet = -1;    /* -1 = not decided, 0 = speak, 1 = silent */

static void
n2_write_all(int fd, const char *text, size_t length)
{
	while (length > 0) {
		ssize_t written = write(fd, text, length);

		if (written > 0) {
			text += written;
			length -= (size_t)written;
			continue;
		}
		if (written < 0 && errno == EINTR) {
			continue;
		}
		break;
	}
}

/* One line, opened and closed per line (see the header). Caller holds the lock or is the only writer. */
static void
n2_emit(const char *line, size_t length)
{
	int fd = open(N2_CONSOLE, N2_O_WRONLY | N2_O_NONBLOCK | N2_O_NOCTTY);

	if (fd < 0) {
		return;
	}
	n2_write_all(fd, line, length);
	(void)close(fd);
}

void
n2vt_say(const char *format, ...)
{
	char text[N2_TEXT_MAX];
	char line[N2_LINE_MAX];
	const char *prog;
	int n;

	if (format == NULL) {
		return;
	}
	{
		va_list ap;

		va_start(ap, format);
		(void)vsnprintf(text, sizeof text, format, ap);
		va_end(ap);
	}
	prog = getprogname();
	if (prog == NULL || prog[0] == '\0') {
		prog = "?";
	}

	pthread_mutex_lock(&n2_log_lock);
	if (n2_log_quiet < 0) {
		n2_log_quiet = access(N2VT_SWITCH_QUIET, N2_F_OK) == 0 ? 1 : 0;
	}
	if (n2_log_quiet || n2_log_lines > N2VT_LOG_MAX_LINES) {
		pthread_mutex_unlock(&n2_log_lock);
		return;
	}
	if (n2_log_lines == N2VT_LOG_MAX_LINES) {
		n = snprintf(line, sizeof line, "N2VIDEO %s[%d]: (further lines suppressed)\n", prog, (int)getpid());
		n2_log_lines = N2VT_LOG_MAX_LINES + 1;
	} else {
		n = snprintf(line, sizeof line, "N2VIDEO %s[%d]: %s\n", prog, (int)getpid(), text);
		n2_log_lines++;
	}
	if (n > 0) {
		size_t length = (size_t)n < sizeof line ? (size_t)n : sizeof line - 1;

		n2_emit(line, length);
	}
	pthread_mutex_unlock(&n2_log_lock);
}
