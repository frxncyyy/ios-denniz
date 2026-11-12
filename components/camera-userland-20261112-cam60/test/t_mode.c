/*
 * (q10jm) t_mode: t_pipe's fake kernel with sensor modes (REDWOOD_CAMERA_MODE): mode 0 frames 64x48, mode 1 32x18 (and twice the rate), mode 2
 * 64x36; the main camera (sensor 1). Checks: the reader gives the kernel the mode rwcam_core_set_mode() asks, once per change, and every picture
 * after the change is of the new size; a kernel without modes (env NOMODES=1: EINVAL) is asked once and the frames go on.
 * Below: t_pipe's original description.
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
static atomic_int mode_now, mode_cmds, mode_refused;
/* (q10jn) env FRAMEMEM=1: the readers' buffers from a made-up frame memory (static, never to be free()d; a slot asked larger moves, the old place
 * filled with 0xEE so that a reader still using it shows as torn) */
static uint8_t fm_pool[2][2][3][16384];
static int fm_gen[2][2];
static size_t fm_size[2][2];
static atomic_int fm_calls, fm_same_slot;
static void *fake_frame_memory(int node, unsigned slot, size_t header, size_t frame)
{
	atomic_fetch_add(&fm_calls, 1);
	if (node < 0 || node > 1 || slot > 1 || header + frame > sizeof(fm_pool[0][0][0])) { return NULL; }
	if (fm_size[node][slot] < header + frame) {
		if (fm_size[node][slot] != 0) { memset(fm_pool[node][slot][fm_gen[node][slot] % 3], 0xee, sizeof(fm_pool[0][0][0])); fm_gen[node][slot]++; }
		fm_size[node][slot] = header + frame;
	}
	return fm_pool[node][slot][fm_gen[node][slot] % 3];
}
static int slot_of(const void *raw)
{
	for (int k = 0; k < 2; k++) { for (int g = 0; g < 3; g++) { const uint8_t *b = fm_pool[0][k][g]; if ((const uint8_t *)raw >= b && (const uint8_t *)raw < b + sizeof(fm_pool[0][0][0])) { return k; } } }
	return -1;
}
static int last_slot = -2;
static const int MW[3] = { 64, 32, 64 }, MH[3] = { 48, 18, 36 };
#define W (MW[atomic_load(&mode_now)])
#define H (MH[atomic_load(&mode_now)])

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
	h->frame_lines = 3000; h->frame_lines_min = 3000; h->frame_lines_max = 6000; h->margin = 8; h->sensor = 1;
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

static ssize_t fake_write(int fd, const void *buf, size_t n)
{
	const struct redwood_camera_command *c = buf;
	(void)fd;
	if (c->command == REDWOOD_CAMERA_MODE) {
		atomic_fetch_add(&mode_cmds, 1);
		if (getenv("NOMODES") != NULL || c->a > 2) { atomic_fetch_add(&mode_refused, 1); errno = EINVAL; return -1; }
		atomic_store(&mode_now, (int)c->a);
	}
	return (ssize_t)n;
}

static atomic_int got, lost, torn, backwards, same_thread, other_thread, isp_bad, wrong_size, after_change, check_from;
static atomic_int expect_w, expect_h;
static uint64_t last_seen;              /* (the sink's own: one sink call at a time) */
static void sink_frame(void *ctx, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp)
{
	(void)ctx; (void)cam;
	struct isp before;

	memcpy(&before, isp, sizeof(before));
	if (atomic_load(&read_thread_set) && pthread_equal(pthread_self(), read_thread)) { atomic_fetch_add(&same_thread, 1); } else { atomic_fetch_add(&other_thread, 1); }
	uint64_t seq = f->sequence;

	if (getenv("FRAMEMEM") != NULL) {
		int k = slot_of(raw);

		if (k < 0) { atomic_fetch_add(&fm_same_slot, 1000); }                        /* (not in the frame memory at all) */
		else if (k == last_slot) { atomic_fetch_add(&fm_same_slot, 1); }               /* (two pictures running from one slot) */
		last_slot = k;
	}
	if (atomic_load(&check_from) && seq >= (uint64_t)atomic_load(&check_from)) {
		atomic_fetch_add(&after_change, 1);
		if ((int)f->width != atomic_load(&expect_w) || (int)f->height != atomic_load(&expect_h)) { atomic_fetch_add(&wrong_size, 1); if (getenv("VERBOSE")) printf("   wrong: seq %llu %ux%u (from %d, want %dx%d)\n", (unsigned long long)seq, f->width, f->height, atomic_load(&check_from), atomic_load(&expect_w), atomic_load(&expect_h)); }
	}
	usleep(SINK_US / 4);                /* the pictures */
	/* the frame is still this frame: header and bytes */
	if (f->sequence != seq) { atomic_fetch_add(&torn, 1); }
	for (size_t i = 0; i < (size_t)f->stride * f->height; i += 97) {
		if (raw[i] != (uint8_t)(seq & 255)) { atomic_fetch_add(&torn, 1); break; }
	}
	(void)before;
	if (last_seen != 0) {
		if (seq <= last_seen) { atomic_fetch_add(&backwards, 1); }
		else { atomic_fetch_add(&lost, (int)(seq - last_seen - 1)); }
	}
	last_seen = seq;
	atomic_fetch_add(&got, 1);
}
static void sink_stopped(void *ctx, int cam, const char *why) { (void)ctx; (void)cam; (void)why; }

int main(void)
{
	struct rwcam_ops ops = { fake_open, fake_read, fake_write, fake_close, NULL };
	struct rwcam_sink sink = { sink_frame, sink_stopped, NULL };
	bool nomodes = getenv("NOMODES") != NULL;

	t0 = now_us();
	rwcam_core_init(&ops, &sink);
	if (getenv("FRAMEMEM") != NULL) { rwcam_core_frame_memory(fake_frame_memory); }
	CHECK(rwcam_core_start(RWCAM_BACK_WIDE));
	usleep(600000);
	int g0 = atomic_load(&got), c0 = atomic_load(&mode_cmds);
	CHECK(g0 > 5);
	CHECK(c0 == 1);                          /* (mode 0 given once at the start) */
	rwcam_core_set_mode(1);
	usleep(100000);
	atomic_store(&expect_w, nomodes ? 64 : 32); atomic_store(&expect_h, nomodes ? 48 : 18);
	atomic_store(&check_from, (int)last_idx + 3);
	usleep(700000);
	int c1 = atomic_load(&mode_cmds);
	atomic_store(&check_from, 0);            /* (no checks while the size changes) */
	rwcam_core_set_mode(2);
	usleep(100000);
	atomic_store(&expect_w, 64); atomic_store(&expect_h, nomodes ? 48 : 36);
	atomic_store(&check_from, (int)last_idx + 3);
	usleep(700000);
	rwcam_core_stop(RWCAM_BACK_WIDE);
	int g = atomic_load(&got), c2 = atomic_load(&mode_cmds);

	CHECK(g > g0 + 10);
	CHECK(torn == 0 && backwards == 0 && wrong_size == 0 && after_change > 5);
	if (nomodes) {
		CHECK(c2 == 1 && mode_refused == 1);  /* asked once, refused, never again */
	} else {
		CHECK(c1 == 2 && c2 == 3 && mode_refused == 0);
		CHECK(atomic_load(&mode_now) == 2);
	}
	if (getenv("FRAMEMEM") != NULL) {
		CHECK(fm_calls >= 2 && fm_same_slot == 0);   /* (every picture from the frame memory; consecutive ones from the two slots in turn) */
		printf("   frame memory: %d calls, pictures not alternating or outside it %d\n", (int)fm_calls, (int)fm_same_slot);
	}
	printf("t_mode (%s%s): %s (%d pictures, %d of them checked after a change, wrong size %d, mode commands %d, refused %d, torn %d)\n",
	    nomodes ? "kernel without modes" : "kernel with modes", getenv("FRAMEMEM") ? ", frame memory" : "", failures ? "FAILED" : "all passed", g, (int)after_change, (int)wrong_size, c2,
	    (int)mode_refused, (int)torn);
	return failures != 0;
}
