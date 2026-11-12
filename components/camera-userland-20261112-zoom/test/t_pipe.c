/*
 * Host test of the pictures thread (camera-userland-20261112-pipe, src/rwcam_core.c pipe_*): a fake kernel that, like the real one, keeps only the
 * newest frame (a frame not read before the next is done is lost: its number is skipped), frames every PERIOD ms, a read that costs READ ms (the
 * kernel's copy), and a sink that takes SINK ms (the pictures). READ + SINK > PERIOD > SINK: one after the other the reader loses frames (cam3..cam8:
 * 27.x a second of 30); overlapped it does not.
 * Checks: frames lost (by the numbers the sink sees) under 2 % with the pictures thread and over 5 % with /var/db/nord2-camera-serial (env
 * PIPE_SERIAL=1); the sink runs on another thread than the reads (pipelined) or the same (serial); every frame's bytes are still that frame's when its
 * pictures are done (no read into the buffer the pictures thread has); numbers only go forward; the sink gets a stable copy of the isp; the reader
 * and the pictures thread end with the stop.
 */
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "rwcam_core.h"
#include "rwcam_log.h"

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;

void rwcam_say(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
void rwcam_say_once(const char *what, const char *name, const char *detail) { (void)what; (void)name; (void)detail; }
void rwcam_say_keep(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
unsigned long redwood_cache_slide(void) { return 0; }

/* (compiled with -Daccess=t_access: the switch file by the environment, every other file absent) */
int t_access(const char *path, int mode)
{
	(void)mode;
	if (strcmp(path, "/var/db/nord2-camera-serial") == 0 && getenv("PIPE_SERIAL") != NULL) {
		return 0;
	}
	errno = ENOENT;
	return -1;
}

#define PERIOD_US 30000
#define READ_US   10000
#define SINK_US   24000
#define W 64
#define H 48

static double t0;
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static atomic_long last_idx;
static pthread_t read_thread;
static atomic_int read_thread_set;

static int fake_open(void) { return 3; }
static void fake_close(int fd) { (void)fd; }

static void header(struct redwood_camera_frame *h, uint64_t seq)
{
	memset(h, 0, sizeof(*h));
	memcpy(h->magic, REDWOOD_CAMERA_MAGIC, 8);
	h->header_bytes = sizeof(*h); h->width = W; h->height = H; h->stride = W * 5 / 4; h->format = 1; h->bayer = 0; h->black = 64;
	h->sequence = seq; h->exposure_lines = 500; h->line_ns = 10000; h->gain = 256; h->gain_unity = 256; h->gain_max = 4096;
	h->frame_lines = 3000; h->frame_lines_min = 3000; h->frame_lines_max = 6000; h->margin = 8; h->sensor = 2;
}

static ssize_t fake_read(int fd, void *buf, size_t n)
{
	struct redwood_camera_frame h;
	size_t total = sizeof(h) + (size_t)(W * 5 / 4) * H;
	(void)fd;
	if (!atomic_exchange(&read_thread_set, 1)) { read_thread = pthread_self(); }
	if (n == sizeof(h)) {
		header(&h, (uint64_t)last_idx + 1);
		memcpy(buf, &h, sizeof(h));
		return (ssize_t)sizeof(h);
	}
	/* the newest frame done since the last one read; none: wait for the next */
	long done = (long)((now_us() - t0) / PERIOD_US), idx;

	if (done > last_idx) {
		idx = done;
	} else {
		idx = last_idx + 1;
		double until = t0 + (double)idx * PERIOD_US, d = until - now_us();
		if (d > 0) { usleep((useconds_t)d); }
	}
	last_idx = idx;
	usleep(READ_US);                /* the copy */
	header(&h, (uint64_t)idx);
	memcpy(buf, &h, sizeof(h));
	memset((uint8_t *)buf + sizeof(h), (int)(idx & 255), (n < total ? n : total) - sizeof(h));
	return (ssize_t)(n < total ? n : total);
}

static ssize_t fake_write(int fd, const void *buf, size_t n) { (void)fd; (void)buf; return (ssize_t)n; }

static atomic_int got, lost, torn, backwards, same_thread, other_thread, isp_bad;
static uint64_t last_seen;              /* (the sink's own: one sink call at a time) */
static void sink_frame(void *ctx, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp)
{
	(void)ctx; (void)cam;
	struct isp before;

	memcpy(&before, isp, sizeof(before));
	if (atomic_load(&read_thread_set) && pthread_equal(pthread_self(), read_thread)) { atomic_fetch_add(&same_thread, 1); } else { atomic_fetch_add(&other_thread, 1); }
	uint64_t seq = f->sequence;

	usleep(SINK_US);                /* the pictures */
	/* the frame is still this frame: header and bytes */
	if (f->sequence != seq) { atomic_fetch_add(&torn, 1); }
	for (size_t i = 0; i < (size_t)(W * 5 / 4) * H; i += 97) {
		if (raw[i] != (uint8_t)(seq & 255)) { atomic_fetch_add(&torn, 1); break; }
	}
	if (memcmp(&before, isp, sizeof(before)) != 0) { atomic_fetch_add(&isp_bad, 1); }
	if (last_seen != 0) {
		if (seq <= last_seen) { atomic_fetch_add(&backwards, 1); }
		else if (atomic_load(&got) > 10) { atomic_fetch_add(&lost, (int)(seq - last_seen - 1)); }   /* (after the start) */
	}
	last_seen = seq;
	atomic_fetch_add(&got, 1);
}
static void sink_stopped(void *ctx, int cam, const char *why) { (void)ctx; (void)cam; (void)why; }

int main(void)
{
	struct rwcam_ops ops = { fake_open, fake_read, fake_write, fake_close, NULL };
	struct rwcam_sink sink = { sink_frame, sink_stopped, NULL };
	bool serial = getenv("PIPE_SERIAL") != NULL;

	t0 = now_us();
	rwcam_core_init(&ops, &sink);
	CHECK(rwcam_core_start(RWCAM_FRONT));
	usleep(2500000);
	rwcam_core_stop(RWCAM_FRONT);
	int g = atomic_load(&got), at_stop = g;

	usleep(200000);
	CHECK(atomic_load(&got) == at_stop);         /* nothing after the stop returned: the pictures thread is done too */
	double lost_pct = g > 10 ? 100.0 * lost / (double)(g - 11 + lost) : 100;

	CHECK(g > 40);
	CHECK(torn == 0 && backwards == 0 && isp_bad == 0);
	if (serial) {
		CHECK(lost_pct > 5);
		CHECK(other_thread == 0 && same_thread > 0);
	} else {
		CHECK(lost_pct < 2);
		CHECK(same_thread == 0 && other_thread > 0);
	}
	printf("t_pipe (%s): %s (%d pictures, %d frames lost = %.1f %%, torn %d, backwards %d, sink on the reader's thread %d / another %d)\n",
	    serial ? "serial" : "pipelined", failures ? "FAILED" : "all passed", g, (int)lost, lost_pct, (int)torn, (int)backwards, (int)same_thread,
	    (int)other_thread);
	return failures != 0;
}
