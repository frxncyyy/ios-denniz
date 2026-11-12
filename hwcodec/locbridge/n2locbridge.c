/* SPDX-License-Identifier: MIT
 * n2locbridge (12 Nov 2026): NMEA -> locationd for the Nord2 iOS port.
 * Reads NMEA 0183 sentences (RMC/GGA, nmea.c) from a FIFO (any producer: the future GNSS daemon, a serial relay, a replay)
 * or replays a recorded file, and hands every valid fix to locationd through CoreLocation's simulation SPI
 * (CLSimulationManager -> locationd's com.apple.locationd.simulation service, the path Xcode's "simulate location" uses):
 *   [m clearSimulatedLocations]; [m appendSimulatedLocation:CLLocation]; [m flush]; first fix: [m startLocationSimulation].
 * Every app then sees the fix through CLLocationManager (Camera geotags, Maps, ...).
 * Build: build_ios.sh (iOS, signed with the com.apple.locationd.simulation entitlement) and test_host.sh (-DN2_HOST:
 * the sink prints "N2LOC FIX ..." instead of calling CoreLocation).
 * usage: n2locbridge [--fifo PATH] [--replay FILE [--period-ms N] [--loops N]] [--min-interval-ms N] [--gate FILE] [--log FILE]
 *   default --fifo /private/var/run/n2gnss.nmea (created 0622 if missing). Console lines "N2LOC ...".
 */
#include "nmea.h"

typedef unsigned long size_t; typedef long ssize_t;
extern int open(const char *, int, ...);
extern ssize_t read(int, void *, size_t);
extern ssize_t write(int, const void *, size_t);
extern int close(int);
extern int fsync(int);
extern int usleep(unsigned int);
extern int access(const char *, int);
extern int mkfifo(const char *, unsigned int);
extern int snprintf(char *, size_t, const char *, ...);
extern int strcmp(const char *, const char *);
extern int atoi(const char *);
extern size_t strlen(const char *);
extern int clock_gettime(int, void *);

#ifdef N2_HOST
#define O_NONBLOCK_ 04000
#define O_NOCTTY_ 0400
#define O_CREAT_ 0100
#define O_APPEND_ 02000
#define CLOCK_MONO 1
#else
#define O_NONBLOCK_ 4
#define O_NOCTTY_ 0x20000
#define O_CREAT_ 0x200
#define O_APPEND_ 8
#define CLOCK_MONO 6
#endif

static int g_log = -1;
static void say(const char *s)
{
	size_t n = strlen(s);
#ifdef N2_HOST
	write(1, s, n); write(1, "\n", 1);
#else
	int fd = open("/dev/console", 1 | O_NONBLOCK_ | O_NOCTTY_);
	if (fd >= 0) { write(fd, s, n); write(fd, "\n", 1); close(fd); }
#endif
	if (g_log >= 0) { write(g_log, s, n); write(g_log, "\n", 1); fsync(g_log); }
}
static long long now_ms(void)
{
	struct { long long s; long ns; } ts = {0, 0};
	clock_gettime(CLOCK_MONO, &ts);
	return ts.s * 1000 + ts.ns / 1000000;
}

/* ------------------------------------------------------------------ sink */
#ifdef N2_HOST
static int sink_init(void) { say("N2LOC sink host"); return 0; }
static int sink_fix(const struct n2_fix *f)
{
	char l[256];
	snprintf(l, sizeof l, "N2LOC FIX lat=%.6f lon=%.6f alt=%.1f hacc=%.1f vacc=%.1f speed=%.2f course=%.1f utc_ms=%lld sats=%d q=%d",
	         f->lat, f->lon, f->alt, f->h_acc, f->v_acc, f->speed_mps, f->course_deg, f->utc_ms, f->sats, f->quality);
	say(l);
	return 0;
}
#else
typedef void *id; typedef void *SEL;
typedef struct { double latitude, longitude; } CLLocationCoordinate2D;
extern id objc_getClass(const char *);
extern SEL sel_registerName(const char *);
extern void objc_msgSend(void);
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *);
extern void *dlopen(const char *, int);
#define S(n) sel_registerName(n)
static id g_mgr; static int g_started;
static int sink_init(void)
{
	void *a = dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", 1);
	void *b = dlopen("/System/Library/Frameworks/CoreLocation.framework/CoreLocation", 1);
	id cls = objc_getClass("CLSimulationManager");
	char l[160]; snprintf(l, sizeof l, "N2LOC sink CoreLocation foundation=%d corelocation=%d CLSimulationManager=%d", a != 0, b != 0, cls != 0); say(l);
	if (!cls) return -1;
	id o = ((id (*)(id, SEL))objc_msgSend)(cls, S("alloc"));
	g_mgr = ((id (*)(id, SEL))objc_msgSend)(o, S("init"));
	return g_mgr ? 0 : -1;
}
static int sink_fix(const struct n2_fix *f)
{
	void *pool = objc_autoreleasePoolPush();
	id NSDate = objc_getClass("NSDate"), CLLocation = objc_getClass("CLLocation");
	double t = f->utc_ms > 0 ? f->utc_ms / 1000.0 : 0;
	id date = t > 0 ? ((id (*)(id, SEL, double))objc_msgSend)(NSDate, S("dateWithTimeIntervalSince1970:"), t)
	                : ((id (*)(id, SEL))objc_msgSend)(NSDate, S("date"));
	CLLocationCoordinate2D c = { f->lat, f->lon };
	id loc = ((id (*)(id, SEL))objc_msgSend)(CLLocation, S("alloc"));
	loc = ((id (*)(id, SEL, CLLocationCoordinate2D, double, double, double, double, double, id))objc_msgSend)(loc,
	      S("initWithCoordinate:altitude:horizontalAccuracy:verticalAccuracy:course:speed:timestamp:"),
	      c, f->alt, f->h_acc, f->v_acc, f->course_deg, f->speed_mps, date);
	((void (*)(id, SEL))objc_msgSend)(g_mgr, S("clearSimulatedLocations"));
	((void (*)(id, SEL, id))objc_msgSend)(g_mgr, S("appendSimulatedLocation:"), loc);
	((void (*)(id, SEL))objc_msgSend)(g_mgr, S("flush"));
	if (!g_started) { ((void (*)(id, SEL))objc_msgSend)(g_mgr, S("startLocationSimulation")); g_started = 1; say("N2LOC simulation started"); }
	((void (*)(id, SEL))objc_msgSend)(loc, S("release"));
	objc_autoreleasePoolPop(pool);
	return 0;
}
#endif

/* ------------------------------------------------------------------ input */
static struct n2_fix g_fix; static long long g_last_emit = -1000000; static int g_min_ms = 900; static long g_fixes, g_lines, g_bad;
static void line_in(const char *s)
{
	g_lines++;
	int r = n2_nmea_feed(&g_fix, s);
	if (r < 0) { g_bad++; return; }
	if (r != N2_NMEA_OK) return;
	/* emit on RMC (end of an epoch in most receivers); a GGA-only stream (two GGAs without an RMC between them)
	   emits on GGA */
	static int gga_since_rmc;
	int is_rmc = s[3] == 'R' && s[4] == 'M' && s[5] == 'C';
	if (is_rmc) gga_since_rmc = 0;
	else if (++gga_since_rmc < 2) return;
	if (!g_fix.valid) return;
	long long t = now_ms();
	if (t - g_last_emit < g_min_ms) return;
	g_last_emit = t;
	n2_fix_accuracy(&g_fix);
	sink_fix(&g_fix);
	g_fixes++;
	if (g_fixes == 1 || g_fixes % 60 == 0) {
		char l[200]; snprintf(l, sizeof l, "N2LOC stats fixes=%ld lines=%ld bad=%ld last %.6f,%.6f", g_fixes, g_lines, g_bad, g_fix.lat, g_fix.lon); say(l);
	}
}
/* splits a byte stream into lines; returns 0 at EOF */
static int pump(int fd)
{
	static char buf[4096]; static int have;
	ssize_t n = read(fd, buf + have, sizeof buf - 1 - have);
	if (n <= 0) return 0;
	have += (int)n;
	int st = 0;
	for (int i = 0; i < have; i++) {
		if (buf[i] == '\n') {
			buf[i] = 0; line_in(buf + st); st = i + 1;
		}
	}
	if (st < have) { for (int i = st; i < have; i++) buf[i - st] = buf[i]; }
	have -= st;
	if (have >= (int)sizeof buf - 1) have = 0;          /* overlong garbage line */
	return 1;
}

int main(int argc, char **argv)
{
	const char *fifo = "/private/var/run/n2gnss.nmea", *replay = 0, *gate = 0; int period = 1000, loops = 1;
	for (int i = 1; i + 1 < argc; i += 2) {
		if (!strcmp(argv[i], "--fifo")) fifo = argv[i + 1];
		else if (!strcmp(argv[i], "--replay")) replay = argv[i + 1];
		else if (!strcmp(argv[i], "--period-ms")) period = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--loops")) loops = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--min-interval-ms")) g_min_ms = atoi(argv[i + 1]);
		else if (!strcmp(argv[i], "--gate")) gate = argv[i + 1];
		else if (!strcmp(argv[i], "--log")) g_log = open(argv[i + 1], 1 | O_CREAT_ | O_APPEND_, 0644);
		else { say("N2LOC bad argument"); return 2; }
	}
	if (gate && access(gate, 0) != 0) return 0;
	n2_fix_init(&g_fix);
	if (sink_init() != 0) { say("N2LOC sink unavailable, exiting"); return 1; }
	if (replay) {                                       /* recorded log: one RMC epoch per period */
		g_min_ms = 0;
		for (int l = 0; loops <= 0 || l < loops; l++) {
			int fd = open(replay, 0);
			if (fd < 0) { say("N2LOC replay file missing"); return 1; }
			static char buf[1 << 16]; int have = 0; ssize_t n;
			while ((n = read(fd, buf + have, sizeof buf - 1 - have)) > 0) have += (int)n;
			close(fd); buf[have] = 0;
			int st = 0;
			for (int i = 0; i <= have; i++) {
				if (i == have || buf[i] == '\n') {
					char c = buf[i]; buf[i] = 0;
					if (i > st) {
						long before = g_fixes; line_in(buf + st);
						if (g_fixes != before && period > 0) usleep((unsigned)period * 1000);
					}
					buf[i] = c; st = i + 1;
				}
			}
		}
		char l[160]; snprintf(l, sizeof l, "N2LOC replay done fixes=%ld lines=%ld bad=%ld", g_fixes, g_lines, g_bad); say(l);
		return 0;
	}
	if (access(fifo, 0) != 0 && mkfifo(fifo, 0622) != 0) { say("N2LOC cannot create fifo"); return 1; }
	{ char l[200]; snprintf(l, sizeof l, "N2LOC listening on %s", fifo); say(l); }
	for (;;) {                                          /* reopen after each writer goes away */
		int fd = open(fifo, 0);
		if (fd < 0) { usleep(1000000); continue; }
		while (pump(fd)) { }
		close(fd);
	}
}
