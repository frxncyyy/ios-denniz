/*
 * DIAGNOSTICS: what the Camera application says to its own os_log, on the kernel log.
 *
 * Camera.app opens with a black preview: it commits a capture session with no inputs, and everything that can be seen from mediaserverd is in
 * order.  What CameraUI / AVFCapture / CMCapture say about it goes to the application's os_log, which nothing shows on this phone.  The Metal
 * plug-in is loaded into every process that uses Metal, the Camera included, so this constructor is a way in that needs no change to the
 * application: in the process named exactly "Camera", and only when the marker file `applog-camera` is in the bundle, it sets libsystem_trace's
 * os_log hook (the same os_log_set_hook / os_log_copy_message_string / hand-made global block as gpu/build/camera-userland-20261109/src/
 * rwcam_diag.c, which runs in mediaserverd) and forwards the lines of interest through libredwoodair's np_log (kernel log, "NORD2_RW CAMLOG ...")
 * and, when the process may open it, to /dev/console.
 *
 * Everywhere else (SpringBoard, backboardd, ...) and without the marker the constructor returns at once: one strcmp, at most one access().
 * Remove the marker to turn it off.
 *
 * The hook calls no code of the application and none of Metal: libc, dyld's dlopen(RTLD_NOLOAD)/dlsym to find np_log, np_ready and np_log
 * themselves (one IOKit call, no GPU work), os_log_copy_message_string.  A hook that was there before is called after ours, always.
 *
 * np_log says nothing until libredwoodair is loaded (the driver loads it when Metal makes the device, after the constructors) and its Nord2GPU
 * connection is up, so lines made before that wait in a small queue (PENDING of them; later ones are counted and dropped) and leave in order
 * at the first hook call that finds it ready.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef APPLOG_PROCESS
#define APPLOG_PROCESS  "Camera"
#endif
#ifndef APPLOG_MARKER
#define APPLOG_MARKER   "/System/Library/Extensions/Nord2Metal.bundle/applog-camera"
#endif
#ifndef APPLOG_LIBRARY
#define APPLOG_LIBRARY  "/System/Library/Extensions/Nord2Metal.bundle/libredwoodair.dylib"
#endif
#ifndef APPLOG_CONSOLE
#define APPLOG_CONSOLE  "/dev/console"
#endif
#define APPLOG_LINES    4000            /* forwarded messages, then silence */
#define APPLOG_TEXT     300             /* bytes of a message's text */
#define APPLOG_PIECE    160             /* bytes of a kernel log record: np_log cuts at 170 and its later pieces would carry no CAMLOG prefix */
#define APPLOG_PENDING  96              /* records kept while np_log is not there yet */
#define APPLOG_RETRY_NS 20000000ull     /* how often the hook asks again for np_log */

/* libsystem_trace's message as the hook gets it (iOS 16.7, as rwcam_diag.c reads it) */
#define MSG_FORMAT      0x58
#define MSG_SUBSYSTEM   0x80
#define MSG_CATEGORY    0x88

struct applog_block { void *isa; int32_t flags, reserved; void *invoke; const void *descriptor; };
static const unsigned long applog_block_descriptor[2] = { 0, sizeof(struct applog_block) };
static struct applog_block applog_block;
static struct applog_block *applog_previous;
static char *(*applog_copy)(void *);

static pthread_mutex_t applog_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t applog_busy;                       /* set while this thread is inside the hook: a message made in there is not ours to take */
static unsigned applog_lines, applog_dropped;
static int applog_console = -1;
static void (*applog_np_log)(const char *, size_t);
static int (*applog_np_ready)(void);
static uint64_t applog_asked;
static struct { uint16_t length; char text[APPLOG_PIECE + 2]; } applog_pending[APPLOG_PENDING];
static unsigned applog_npending;

static bool
applog_has(const char *text, const char *word)
{
	if (text == NULL) {
		return false;
	}
	for (; *text; text++) {
		size_t i = 0;

		while (word[i] && text[i] == word[i]) {
			i++;
		}
		if (word[i] == 0) {
			return true;
		}
	}
	return false;
}

static bool
applog_wanted(uint8_t type, const char *format, const char *subsystem)
{
	if (format == NULL) {
		return false;
	}
	return type >= 16 || (format[0] == '<' && format[1] == '<' && format[2] == '<' && format[3] == '<') || applog_has(subsystem, "amera") ||
	       applog_has(subsystem, "coremedia") || applog_has(subsystem, "AVF") || applog_has(subsystem, "avfoundation");
}

/* lock held.  Is np_log there and its connection up?  Asked again every APPLOG_RETRY_NS at most until it is. */
static bool
applog_ready(void)
{
	if (applog_np_log == NULL || applog_np_ready == NULL) {
		uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
		void *h;

		if (applog_asked != 0 && now - applog_asked < APPLOG_RETRY_NS) {
			return false;
		}
		applog_asked = now ? now : 1;
		h = dlopen(APPLOG_LIBRARY, RTLD_NOW | RTLD_NOLOAD);     /* never loads it: only finds it once the driver has */
		if (h == NULL) {
			return false;
		}
		applog_np_ready = (int (*)(void))dlsym(h, "np_ready");
		applog_np_log = (void (*)(const char *, size_t))dlsym(h, "np_log");
		if (applog_np_log == NULL || applog_np_ready == NULL) {
			return false;
		}
	}
	return applog_np_ready() != 0;
}

/* lock held.  One record (no newline, APPLOG_PIECE bytes at most) to the kernel log, or to the queue while np_log is not ready. */
static void
applog_record(const char *text, size_t length)
{
	if (length > APPLOG_PIECE) {
		length = APPLOG_PIECE;
	}
	if (applog_console >= 0 && length != 0) {
		char line[APPLOG_PIECE + 2];

		memcpy(line, text, length);
		line[length] = '\n';
		(void)write(applog_console, line, length + 1);
	}
	if (applog_ready()) {
		if (applog_npending != 0 || applog_dropped != 0) {
			unsigned n = applog_npending, dropped = applog_dropped;

			applog_npending = 0;
			applog_dropped = 0;
			for (unsigned i = 0; i < n; i++) {
				applog_np_log(applog_pending[i].text, applog_pending[i].length);
			}
			if (dropped != 0) {
				char note[80];
				int m = snprintf(note, sizeof(note), "CAMLOG: %u early records dropped (np_log was not ready)", dropped);

				applog_np_log(note, (size_t)m);
			}
		}
		if (length != 0) {
			applog_np_log(text, length);
		}
	} else if (length != 0) {
		if (applog_npending < APPLOG_PENDING) {
			memcpy(applog_pending[applog_npending].text, text, length);
			applog_pending[applog_npending++].length = (uint16_t)length;
		} else {
			applog_dropped++;
		}
	}
}

/* lock held.  "CAMLOG <n> <type> [subsystem/category] text", cut into records that each begin with CAMLOG and the message's number. */
static void
applog_say(unsigned number, uint8_t type, const char *subsystem, const char *category, char *text)
{
	char piece[APPLOG_PIECE + 1];
	size_t length = strlen(text), at = 0;
	int head;

	if (length > APPLOG_TEXT) {
		length = APPLOG_TEXT;
	}
	for (size_t i = 0; i < length; i++) {
		if (text[i] == '\n' || text[i] == '\r') {
			text[i] = ' ';
		}
	}
	head = snprintf(piece, sizeof(piece), "CAMLOG %u %u [%.24s/%.16s] ", number, (unsigned)type, subsystem ? subsystem : "-", category ? category : "-");
	do {
		size_t room = APPLOG_PIECE - (size_t)head, n = length - at < room ? length - at : room;

		memcpy(piece + head, text + at, n);
		applog_record(piece, (size_t)head + n);
		at += n;
		head = snprintf(piece, sizeof(piece), "CAMLOG %u + ", number);
	} while (at < length);
}

static void
applog_hook(struct applog_block *block, uint8_t type, void *msg)
{
	(void)block;
	if (msg != NULL && pthread_getspecific(applog_busy) == NULL) {
		const char *format = *(const char **)((char *)msg + MSG_FORMAT);
		const char *subsystem = *(const char **)((char *)msg + MSG_SUBSYSTEM);
		const char *category = *(const char **)((char *)msg + MSG_CATEGORY);
		bool wanted = applog_lines < APPLOG_LINES && applog_wanted(type, format, subsystem);

		if (wanted || applog_npending != 0) {
			char *text = NULL;

			pthread_setspecific(applog_busy, &applog_busy);
			if (wanted) {
				text = applog_copy(msg);
			}
			pthread_mutex_lock(&applog_lock);
			if (text != NULL && applog_lines < APPLOG_LINES) {
				applog_say(++applog_lines, type, subsystem, category, text);
				if (applog_lines == APPLOG_LINES) {
					static const char last[] = "CAMLOG: line limit reached, nothing more is forwarded";

					applog_record(last, sizeof(last) - 1);
				}
			} else {
				applog_record("", 0);           /* nothing new: only lets the queued records out once np_log is ready */
			}
			pthread_mutex_unlock(&applog_lock);
			free(text);
			pthread_setspecific(applog_busy, NULL);
		}
	}
	if (applog_previous != NULL) {
		((void (*)(struct applog_block *, uint8_t, void *))applog_previous->invoke)(applog_previous, type, msg);
	}
}

__attribute__((constructor)) static void
nord2_applog_install(void)
{
	const char *name = getprogname();
	void *(*set_hook)(uint8_t, void *);
	void *global_block;
	char line[96];
	int n;

	if (name == NULL || strcmp(name, APPLOG_PROCESS) != 0) {
		return;                         /* every process but the Camera: nothing at all */
	}
	if (access(APPLOG_MARKER, F_OK) != 0) {
		return;                         /* diagnostics only: the marker file `applog-camera` in the bundle turns it on */
	}
	set_hook = (void *(*)(uint8_t, void *))dlsym(RTLD_DEFAULT, "os_log_set_hook");
	global_block = dlsym(RTLD_DEFAULT, "_NSConcreteGlobalBlock");
	applog_copy = (char *(*)(void *))dlsym(RTLD_DEFAULT, "os_log_copy_message_string");
	if (set_hook == NULL || global_block == NULL || applog_copy == NULL || pthread_key_create(&applog_busy, NULL) != 0) {
		return;
	}
	/* once: a sandboxed application is refused, and every refusal is a line of the sandbox's own in the kernel log */
	applog_console = open(APPLOG_CONSOLE, O_WRONLY | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
	n = snprintf(line, sizeof(line), "CAMLOG: hook installed in %s pid %d (console %s)", APPLOG_PROCESS, (int)getpid(), applog_console >= 0 ? "yes" : "no");
	pthread_mutex_lock(&applog_lock);
	applog_record(line, (size_t)n);         /* queued: libredwoodair is not loaded yet when the constructors run */
	pthread_mutex_unlock(&applog_lock);
	applog_block.isa = global_block;
	applog_block.flags = 1 << 28;           /* BLOCK_IS_GLOBAL */
	applog_block.invoke = (void *)applog_hook;
	applog_block.descriptor = applog_block_descriptor;
	applog_previous = set_hook(1, &applog_block);
}
