#include "rwcam_abi.h"
#include "rwcam_log.h"
#include "rwcam_loglimit.h"

#define CACHE_BASE 0x180000000UL        /* where the iOS 16.x cache is built to be before its slide */

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

unsigned long
redwood_cache_slide(void)
{
	size_t length = 0;
	const void *base = _dyld_get_shared_cache_range(&length);

	return base != NULL ? (unsigned long)base - CACHE_BASE : 0;
}

/*
 * /dev/console is opened for every line: mediaserverd does not keep the descriptors it had when the plug-in was loaded (run q10gm: nothing said
 * after the constructor reached the console), so a descriptor kept from the first line is somebody else's file later.
 */
static void
emit(const char *line, int n)
{
	int fd;

	pthread_mutex_lock(&log_lock);
	fd = open("/dev/console", O_WRONLY | O_NONBLOCK | O_NOCTTY);
	if (fd >= 0) {
		(void)write(fd, line, (unsigned long)n);
		close(fd);
	}
	pthread_mutex_unlock(&log_lock);
}

/* milliseconds since the first line (mach_absolute_time's units through the timebase) */
struct log_timebase { uint32_t numer, denom; };
extern uint64_t mach_absolute_time(void);
extern int mach_timebase_info(struct log_timebase *);

static uint64_t
log_ms(void)
{
	static struct log_timebase tb;
	static uint64_t first;
	uint64_t now = mach_absolute_time();

	if (tb.denom == 0) {
		struct log_timebase t = { 0, 0 };

		(void)mach_timebase_info(&t);
		if (t.denom == 0 || t.numer == 0) {
			t.numer = t.denom = 1;
		}
		tb = t;
		first = now;
	}
	now -= first;
	return now / 1000000u * tb.numer / tb.denom + now % 1000000u * tb.numer / tb.denom / 1000000u;
}

/* every line: held back by rwcam_loglimit.c (shape and budget) unless kept */
static void
say(bool keep, const char *format, __builtin_va_list ap)
{
	char line[480], note[96];
	int n = snprintf(line, 400, "redwood: camera: ");
	bool go;

	n += vsnprintf(line + n, 400 - (unsigned long)n, format, ap);
	if (n > 398) {
		n = 398;
	}
	line[n] = 0;
	pthread_mutex_lock(&log_lock);
	go = rwcam_loglimit(line, (size_t)n + 1, log_ms(), keep, note, sizeof(note));
	pthread_mutex_unlock(&log_lock);
	if (!go) {
		return;
	}
	for (const char *c = note; *c && n < (int)sizeof(line) - 2; c++) {
		line[n++] = *c;
	}
	line[n++] = '\n';
	emit(line, n);
}

void
rwcam_say(const char *format, ...)
{
	__builtin_va_list ap;

	__builtin_va_start(ap, format);
	say(false, format, ap);
	__builtin_va_end(ap);
}

void
rwcam_say_keep(const char *format, ...)
{
	__builtin_va_list ap;

	__builtin_va_start(ap, format);
	say(true, format, ap);
	__builtin_va_end(ap);
}

void
rwcam_say_once(const char *what, const char *name, const char *detail)
{
	static struct { uint32_t hash; } seen[256];
	static unsigned used;
	uint32_t h = 2166136261u;

	for (const char *p = what; *p; p++) {
		h = (h ^ (uint8_t)*p) * 16777619u;
	}
	for (const char *p = name; *p; p++) {
		h = (h ^ (uint8_t)*p) * 16777619u;
	}
	pthread_mutex_lock(&log_lock);
	for (unsigned i = 0; i < used; i++) {
		if (seen[i].hash == h) {
			pthread_mutex_unlock(&log_lock);
			return;
		}
	}
	if (used < 256) {
		seen[used++].hash = h;
	}
	pthread_mutex_unlock(&log_lock);
	rwcam_say("%s %s%s%s", what, name, detail != NULL && detail[0] ? " = " : "", detail != NULL ? detail : "");
}
