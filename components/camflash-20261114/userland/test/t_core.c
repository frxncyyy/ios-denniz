/*
 * Host test of the reader (src/rwcam_core.c) against a fake /dev/redwood-camera that behaves like the kernel header says: three sensors of different frame
 * sizes, one streaming at a time, REDWOOD_CAMERA_SENSOR stops the frames and the next read starts the other sensor (a few frames of the old one may still be
 * queued), a read of the header's size learns the frame's size, EIO after a fault. Checks: the sensor follows the stream started last, no frame of another
 * sensor is ever delivered, a stop of a camera that lost the sensor changes nothing, the last stop closes the device, a fault ends the reader and says so.
 */
#include <stdio.h>
#include <stdarg.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include "rwcam_core.h"
#include "rwcam_log.h"

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;
static int near(float a, float b, float eps) { return a - b <= eps && b - a <= eps; }

void rwcam_say(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
void rwcam_say_once(const char *what, const char *name, const char *detail) { (void)what; (void)name; (void)detail; }
void rwcam_say_keep(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
unsigned long redwood_cache_slide(void) { return 0; }

/* ---- the fake kernel ---- */
static const int fw[4] = { 0, 64, 80, 48 }, fh[4] = { 0, 48, 60, 36 };       /* sensor 1 wide, 2 front, 3 ultra-wide */
static atomic_int cur = 1, stale_left, stale_sensor, opens, closes, sensor_cmds, last_sensor_cmd, exposure_cmds, fault_now;
static atomic_ulong seq;
static atomic_int focus_on, focus_cmds, focus_bad, focus_at;        /* a lens the kernel moves: REDWOOD_CAMERA_FOCUS, the frames' focus / focus_max */
static atomic_int stall_now, stall_saw_other, stall_kicks, stall_exposures;      /* the frames stop (the DMA is not armed again) until the session is started anew */

static int fake_open(void) { atomic_fetch_add(&opens, 1); return 3; }
static void fake_close(int fd) { (void)fd; atomic_fetch_add(&closes, 1); }

static ssize_t fake_read(int fd, void *buf, size_t n)
{
	int s = cur;
	(void)fd;
	usleep(500);
	if (fault_now) { errno = EIO; return -1; }
	for (int waited = 0; stall_now; waited++) {          /* as the kernel: at most 3 s for a frame, then EAGAIN */
		if (waited >= 3000) { errno = EAGAIN; return -1; }
		usleep(1000);
	}
	s = cur;
	if (stale_left > 0) { s = stale_sensor; }
	size_t stride = (size_t)fw[s] * 5 / 4, total = sizeof(struct redwood_camera_frame) + stride * fh[s];
	struct redwood_camera_frame h;

	memset(&h, 0, sizeof(h));
	memcpy(h.magic, REDWOOD_CAMERA_MAGIC, 8);
	h.header_bytes = sizeof(h); h.width = fw[s]; h.height = fh[s]; h.stride = (uint32_t)stride; h.format = 1; h.bayer = (uint32_t)s % 4; h.black = 64;
	h.sequence = atomic_fetch_add(&seq, 1) + 1; h.exposure_lines = 500; h.line_ns = 10000; h.gain = 256; h.gain_unity = 256; h.gain_max = 4096;
	h.frame_lines = 3000; h.frame_lines_min = 3000; h.frame_lines_max = 6000; h.margin = 8; h.sensor = (uint32_t)s;
	h.focus_max = focus_on ? 1023 : 0; h.focus = (uint32_t)focus_at;
	if (n == sizeof(h)) {
		memcpy(buf, &h, sizeof(h));
		return (ssize_t)sizeof(h);
	}
	if (stale_left > 0) { atomic_fetch_sub(&stale_left, 1); }
	size_t m = n < total ? n : total;
	uint8_t *tmp = calloc(1, total);

	memcpy(tmp, &h, sizeof(h));
	memset(tmp + sizeof(h), 0x40, stride * fh[s]);
	memcpy(buf, tmp, m);
	free(tmp);
	return (ssize_t)m;
}

static ssize_t fake_write(int fd, const void *buf, size_t n)
{
	const struct redwood_camera_command *c = buf;
	(void)fd;
	if (n != sizeof(*c)) { errno = EINVAL; return -1; }
	if (c->command == REDWOOD_CAMERA_SENSOR && stall_now) {
		/* another sensor, then the one that streams again: the session is started anew (its queue is empty) and the frames come back */
		if (c->a < 1 || c->a > 3) { errno = EINVAL; return -1; }
		if ((int)c->a != cur) { stall_saw_other = 1; }
		else if (stall_saw_other) { stall_saw_other = 0; atomic_fetch_add(&stall_kicks, 1); stall_now = 0; }
		return (ssize_t)n;
	}
	if (c->command == REDWOOD_CAMERA_EXPOSURE && stall_kicks) { atomic_fetch_add(&stall_exposures, 1); }
	if (c->command == REDWOOD_CAMERA_FOCUS) { if (c->a > 1023 || !focus_on) { focus_bad = 1; } focus_at = (int)c->a; atomic_fetch_add(&focus_cmds, 1); return (ssize_t)n; }
	if (c->command == REDWOOD_CAMERA_SENSOR) {
		if (c->a < 1 || c->a > 3) { errno = EINVAL; return -1; }
		stale_sensor = cur; stale_left = 2;      /* (frames of the old one still in the queue) */
		cur = (int)c->a; last_sensor_cmd = (int)c->a;
		atomic_fetch_add(&sensor_cmds, 1);
	} else if (c->command == REDWOOD_CAMERA_EXPOSURE) {
		atomic_fetch_add(&exposure_cmds, 1);
	}
	return (ssize_t)n;
}

/* ---- the sink ---- */
static atomic_int got[RWCAM_CAMERAS], foreign, stopped_calls;
static void sink_frame(void *ctx, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp)
{
	(void)ctx; (void)raw; (void)isp;
	if (f->sensor != rwcam_sensor_of[cam]) { atomic_fetch_add(&foreign, 1); }
	if (f->width != (uint32_t)fw[f->sensor] || f->height != (uint32_t)fh[f->sensor]) { atomic_fetch_add(&foreign, 1); }
	atomic_fetch_add(&got[cam], 1);
}
static void sink_stopped(void *ctx, int cam, const char *why) { (void)ctx; (void)cam; (void)why; atomic_fetch_add(&stopped_calls, 1); }

static bool wait_for(atomic_int *v, int atleast, int ms)
{
	for (int i = 0; i < ms; i++) {
		if (atomic_load(v) >= atleast) { return true; }
		usleep(1000);
	}
	return false;
}

int main(void)
{
	struct rwcam_ops ops = { fake_open, fake_read, fake_write, fake_close, NULL };
	struct rwcam_sink sink = { sink_frame, sink_stopped, NULL };

	CHECK(rwcam_sensor_of[RWCAM_BACK_WIDE] == 1 && rwcam_sensor_of[RWCAM_FRONT] == 2 && rwcam_sensor_of[RWCAM_BACK_ULTRAWIDE] == 3);
	rwcam_core_init(&ops, &sink);

	/* 1. front first: the kernel runs sensor 1, so one SENSOR(2) */
	CHECK(rwcam_core_start(RWCAM_FRONT));
	CHECK(wait_for(&got[RWCAM_FRONT], 5, 3000));
	CHECK(sensor_cmds == 1 && last_sensor_cmd == 2);
	/* 2. the wide one takes over */
	CHECK(rwcam_core_start(RWCAM_BACK_WIDE));
	CHECK(wait_for(&got[RWCAM_BACK_WIDE], 5, 3000));
	CHECK(sensor_cmds == 2 && last_sensor_cmd == 1);
	/* 3. the ultra-wide */
	CHECK(rwcam_core_start(RWCAM_BACK_ULTRAWIDE));
	CHECK(wait_for(&got[RWCAM_BACK_ULTRAWIDE], 5, 3000));
	CHECK(sensor_cmds == 3 && last_sensor_cmd == 3);
	CHECK(rwcam_core_owner() == RWCAM_BACK_ULTRAWIDE);
	CHECK(atomic_load(&foreign) == 0);
	CHECK(exposure_cmds > 0);          /* AE writes its command to the kernel */
	/* 4. a stop of a camera that lost the sensor (the old graph is torn down late) changes nothing */
	int before = got[RWCAM_BACK_ULTRAWIDE];
	rwcam_core_stop(RWCAM_FRONT);
	rwcam_core_stop(RWCAM_BACK_WIDE);
	CHECK(rwcam_core_owner() == RWCAM_BACK_ULTRAWIDE);
	CHECK(wait_for(&got[RWCAM_BACK_ULTRAWIDE], before + 3, 3000));
	CHECK(opens == 1 && closes == 0);
	/* 5. the last stop puts the sensor down: the device is closed */
	rwcam_core_stop(RWCAM_BACK_ULTRAWIDE);
	CHECK(rwcam_core_owner() == -1);
	CHECK(closes == 1);
	int after = got[RWCAM_BACK_ULTRAWIDE];
	usleep(20000);
	CHECK(got[RWCAM_BACK_ULTRAWIDE] == after);
	/* 6. started again: a second open, the fake still on sensor 3, the wide camera wanted: SENSOR(1) */
	CHECK(rwcam_core_start(RWCAM_BACK_WIDE));
	CHECK(wait_for(&got[RWCAM_BACK_WIDE], 12, 3000));
	CHECK(opens == 2 && last_sensor_cmd == 1 && sensor_cmds == 4);
	/* 7. a fault ends the reader and tells the sink; the next start brings a new one */
	fault_now = 1;
	CHECK(wait_for(&stopped_calls, 1, 3000));
	usleep(20000);
	fault_now = 0;
	rwcam_core_stop(RWCAM_BACK_WIDE);
	CHECK(rwcam_core_start(RWCAM_FRONT));
	CHECK(wait_for(&got[RWCAM_FRONT], 12, 3000));
	CHECK(atomic_load(&foreign) == 0);
	/* 8. the frames stop in the middle of a session (run q10ha): the session is started anew well before the kernel's 3 s, nothing else changes */
	{
		int g0 = got[RWCAM_FRONT], cmds0 = sensor_cmds;
		uint64_t k0 = rwcam_core_kicks();

		stall_now = 1;
		CHECK(wait_for(&stall_kicks, 1, 2000));
		CHECK(wait_for(&got[RWCAM_FRONT], g0 + 5, 3000));
		CHECK(rwcam_core_kicks() == k0 + 1 && stall_kicks == 1);
		CHECK(sensor_cmds == cmds0 && atomic_load(&foreign) == 0 && rwcam_core_owner() == RWCAM_FRONT);
	}
	/* 9. no lens was moved so far (the frames said focus_max 0); with a lens the reader scans its positions and leaves it somewhere in its range */
	CHECK(focus_cmds == 0);
	focus_on = 1;
	rwcam_core_focus_window(RWCAM_FRONT, 0.5f, 0.5f, 0.3f, 0.3f, true);
	CHECK(wait_for(&focus_cmds, 8, 3000));
	CHECK(focus_bad == 0 && focus_at >= 0 && focus_at <= 1023);
	rwcam_core_shutdown();
	usleep(250000);                 /* (the stall watch sees the reader gone and ends) */
	CHECK(closes == opens);
	/*
	 * 10. the ultra-wide camera's zoom (rwcam_view_zoom): all of the sensor at 1, the wide camera's view (the lenses' ratio) at Camera.app's switch to
	 * the wide camera (2.0), even steps between them, no jump at the switch, the graph's rate past it; never under 1; the other cameras' zoom as it is.
	 */
	{
		const float r = RWCAM_UW_RATIO, sw = RWCAM_UW_SWITCH_ZOOM;
		float before = 1;

		CHECK(near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1, r), 1, 1e-5f));
		CHECK(near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, sw, r), r, 1e-4f));
		CHECK(near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, sw - 0.001f, r), r, 2e-3f) && near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, sw + 0.001f, r), r, 2e-3f));
		CHECK(near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 2 * sw, r), 2 * r, 1e-4f));
		/* the same ratio of zoom is the same ratio of crop: sqrt(2) is half-way */
		CHECK(near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1.41421356f, r) * rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1.41421356f, r), r, 1e-3f));
		for (float z = 1; z <= 8; z += 0.01f) {
			float v = rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, z, r);

			CHECK(v >= before - 1e-6f && v >= 1 && v <= z + 1e-5f);
			before = v;
		}
		CHECK(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 0.3f, r) == 1 && rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 0.0f / 1e30f * 0, r) == 1);
		CHECK(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, __builtin_nanf(""), r) == 1);
		/* a ratio that says nothing (not over 1, or the full 2.0 and more): the zoom itself */
		CHECK(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1.5f, 1) == 1.5f && rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1.5f, 2) == 1.5f &&
		    rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1.5f, 0) == 1.5f && rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1.5f, __builtin_nanf("")) == 1.5f);
		CHECK(rwcam_view_zoom(RWCAM_BACK_WIDE, 1.5f, r) == 1.5f && rwcam_view_zoom(RWCAM_FRONT, 3, r) == 3 && rwcam_view_zoom(RWCAM_BACK_WIDE, 0.5f, r) == 1);
		/* another pair of lenses (the marker file's range): 1.25 */
		CHECK(near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, sw, 1.25f), 1.25f, 1e-4f) && near(rwcam_view_zoom(RWCAM_BACK_ULTRAWIDE, 1, 1.25f), 1, 1e-5f));
	}
	printf(failures ? "t_core: %d FAILURES\n" : "t_core: all passed (sensor commands %d, opens %d, frames %d/%d/%d)\n",
	    failures ? failures : sensor_cmds, opens, got[0], got[1], got[2]);
	return failures != 0;
}
