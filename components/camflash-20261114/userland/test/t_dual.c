/*
 * Host test of the reader's PAIR (src/rwcam_core.c) against a fake kernel with the two nodes of KERNEL_ABI_DUAL.md: /dev/redwood-camera (by itself
 * sensor 1) and /dev/redwood-camera2 (sensor 3), each with its own session, exposure and frame counter; a sensor the other node has is EBUSY (for
 * REDWOOD_CAMERA_SENSOR and for a read that would start it); a read of the header's size copies no frame; the header carries the frame's levels,
 * which follow the exposure asked for two frames earlier (a scene of a fixed brightness for each sensor).
 * Checks: no second node -> no pair, the single reader as ever; the pair reads the shown camera whole and the hidden one's headers only, and the
 * hidden one's exposure settles all the same; changing the shown camera gives its frames at once, already exposed, with the other one's white
 * balance; the hidden camera's pictures every n-th frame when asked; EBUSY is waited out; a single camera takes over from the pair and the pair from
 * a single camera on either sensor; a second node that stops giving frames ends the pair and node 0 goes on alone; headers without levels.
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

void rwcam_say(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
void rwcam_say_once(const char *what, const char *name, const char *detail) { (void)what; (void)name; (void)detail; }
void rwcam_say_keep(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
unsigned long redwood_cache_slide(void) { return 0; }

#include "fake_kernel2.h"

/* ---- the sink ---- */
static atomic_int got[RWCAM_CAMERAS], foreign, stopped_calls;
static atomic_int first_level_permille[RWCAM_CAMERAS];  /* the level (green over white, with the digital gain) of a camera's first frame after `mark' */
static atomic_int mark[RWCAM_CAMERAS];
static atomic_int seen_wb[RWCAM_CAMERAS][3];      /* (thousandths) */
static void sink_frame(void *ctx, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp)
{
	(void)ctx; (void)raw;
	if (f->sensor != rwcam_sensor_of[cam] || f->width != (uint32_t)fw[f->sensor]) { atomic_fetch_add(&foreign, 1); }
	if (atomic_exchange(&mark[cam], 0)) {
		atomic_store(&first_level_permille[cam], (int)(1000.0 * f->stat_green * isp->digital / (1023 - f->black)));
	}
	atomic_store(&seen_wb[cam][0], (int)(isp->wb[0] * 1000)); atomic_store(&seen_wb[cam][2], (int)(isp->wb[2] * 1000));
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
static uint64_t knum(uint64_t *p) { pthread_mutex_lock(&klock); uint64_t v = *p; pthread_mutex_unlock(&klock); return v; }
static bool wait_stable(int cam, int ms)
{
	struct rwcam_status st;

	for (int i = 0; i < ms; i++) {
		rwcam_core_status(cam, &st);
		if (st.frames > 0 && st.ae_stable && st.level > 0.08f && st.level < 0.21f) { return true; }
		usleep(1000);
	}
	rwcam_core_status(cam, &st);
	printf("   camera %d not settled: frames %llu, stable %d (quiet %u), level %.3f, exposure %llu ns x %.2f x %.2f\n", cam, (unsigned long long)st.frames,
	    st.ae_stable, st.quiet, (double)st.level, (unsigned long long)st.exposure_ns, (double)st.gain, (double)st.digital);
	return false;
}

int main(void)
{
	struct rwcam_ops ops = { fake_open, fake_read, fake_write, fake_close, fake_open_node };
	struct rwcam_sink sink = { sink_frame, sink_stopped, NULL };
	struct rwcam_status st;
	const int W = RWCAM_BACK_WIDE, U = RWCAM_BACK_ULTRAWIDE, F = RWCAM_FRONT;

	rwcam_core_init(&ops, &sink);
	rwcam_core_align_set(1.6f, 0, 0);       /* (the two cameras' pictures are not compared here: the hidden one is never read whole for it) */

	/* 1. no second node: no pair; the single reader as ever */
	node2_there = 0;
	CHECK(!rwcam_core_pair_possible() && !rwcam_core_pair_start(W) && !rwcam_core_pair_running());
	CHECK(rwcam_core_start(W) && wait_for(&got[W], 5, 3000));
	CHECK(knum(&kn[1].header_reads) == 0 && knum(&kn[1].whole_reads) == 0 && rwcam_core_owner() == W);
	rwcam_core_stop(W);
	node2_there = 1;

	/* 2. the pair, the wide camera shown: its frames whole to the sink; the ultra-wide one's headers only, no picture of it, and its exposure settles */
	int w0 = got[W];
	CHECK(rwcam_core_pair_possible() && rwcam_core_pair_start(W) && rwcam_core_pair_running() && rwcam_core_owner() == W);
	CHECK(wait_for(&got[W], w0 + 30, 5000));
	CHECK(wait_stable(U, 5000) && wait_stable(W, 5000));
	CHECK(got[U] == 0 && knum(&kn[1].whole_reads) == 0 && knum(&kn[1].header_reads) > 20 && knum(&kn[1].exposures) > 0);
	CHECK(rwcam_core_headers(U) > 20 && rwcam_core_frames(U) == 0);
	rwcam_core_status(U, &st);
	CHECK(st.frames > 0 && !st.whole && st.exposure_ns > 0 && st.fps > 29 && st.fps < 31);
	rwcam_core_status(W, &st);
	CHECK(st.whole && st.ae_stable);
	CHECK(knum(&kn[0].sensor_cmds) == 2 && knum(&kn[1].sensor_cmds) == 1);       /* each node its own sensor, asked for before the first read of each start (node 0: step 1 and this one) and never again */

	/* 3. the ultra-wide camera shown: its frames at once (a few frames' time), exposed already, with the wide camera's white balance; the wide one hidden */
	uint64_t whole0 = knum(&kn[0].whole_reads);
	int wide_r = seen_wb[W][0], wide_b = seen_wb[W][2];

	mark[U] = 1;
	rwcam_core_pair_show(U);
	CHECK(wait_for(&got[U], 1, 60));                /* (a frame of the fake is 0.7 ms and more; a camera that had to be started takes far longer) */
	CHECK(first_level_permille[U] > 80 && first_level_permille[U] < 210);
	CHECK(wait_for(&got[U], 20, 3000) && rwcam_core_owner() == U && rwcam_core_pair_shows() == 1);
	CHECK(seen_wb[U][0] > wide_r * 9 / 10 && seen_wb[U][0] < wide_r * 11 / 10 && seen_wb[U][2] > wide_b * 9 / 10 && seen_wb[U][2] < wide_b * 11 / 10);
	usleep(20000);
	uint64_t whole1 = knum(&kn[0].whole_reads), head1 = knum(&kn[0].header_reads);

	usleep(30000);
	CHECK(knum(&kn[0].whole_reads) == whole1 && whole1 <= whole0 + 2 && knum(&kn[0].header_reads) > head1 + 5);
	int w1 = got[W];

	usleep(10000);
	CHECK(got[W] == w1);                            /* no picture of the hidden wide camera */
	CHECK(knum(&kn[0].sensor_cmds) == 2 && knum(&kn[1].sensor_cmds) == 1 && foreign == 0);

	/* 4. and back, and the hidden camera's pictures every 3rd frame when they are asked for */
	mark[W] = 1;
	rwcam_core_pair_show(W);
	CHECK(wait_for(&got[W], w1 + 1, 60) && first_level_permille[W] > 100 && first_level_permille[W] < 210);
	int u1 = got[U];
	uint64_t h1 = rwcam_core_headers(U);

	usleep(20000);
	CHECK(got[U] <= u1 + 1);
	rwcam_core_pair_slave_every(3);
	u1 = got[U];
	h1 = rwcam_core_headers(U);
	w1 = got[W];
	CHECK(wait_for(&got[U], u1 + 6, 3000));
	{
		int du = got[U] - u1, dw = got[W] - w1;
		uint64_t dh = rwcam_core_headers(U) - h1;

		CHECK(dw > 0 && dh >= (uint64_t)du && dh <= (uint64_t)du * 3 + 6);   /* two headers to each whole frame */
	}
	rwcam_core_pair_slave_every(0);

	/* 5. a single camera takes over (the front one: node 0 changes sensor, node 1 is closed) ... */
	int f0 = got[F];

	CHECK(rwcam_core_start(F) && wait_for(&got[F], f0 + 5, 3000) && !rwcam_core_pair_running());
	pthread_mutex_lock(&klock);
	CHECK(kn[1].opens == 0 && kn[0].sensor == 2);
	pthread_mutex_unlock(&klock);
	/* ... the pair from it, and from the ultra-wide camera alone on node 0 (which has sensor 3 until it lets go: no EBUSY for ever) */
	u1 = got[U];
	CHECK(rwcam_core_pair_start(U) && wait_for(&got[U], u1 + 5, 3000));
	pthread_mutex_lock(&klock);
	CHECK(kn[0].sensor == 1 && kn[1].sensor == 3 && kn[0].opens == 1 && kn[1].opens == 1);
	pthread_mutex_unlock(&klock);
	rwcam_core_pair_stop();
	CHECK(!rwcam_core_pair_running() && rwcam_core_owner() == -1);
	CHECK(rwcam_core_start(U));
	u1 = got[U];
	CHECK(wait_for(&got[U], u1 + 5, 3000));
	pthread_mutex_lock(&klock);
	CHECK(kn[0].sensor == 3 && kn[1].opens == 0);
	pthread_mutex_unlock(&klock);
	w1 = got[W];
	CHECK(rwcam_core_pair_start(W) && wait_for(&got[W], w1 + 5, 3000) && rwcam_core_pair_running());
	CHECK(wait_for(&got[W], w1 + 10, 3000) && rwcam_core_headers(U) > h1);
	pthread_mutex_lock(&klock);
	CHECK(kn[0].sensor == 1 && kn[1].sensor == 3);
	pthread_mutex_unlock(&klock);
	CHECK(stopped_calls == 0);
	/* a SENSOR refused with EBUSY a few times (the other node letting go late) is asked again */
	rwcam_core_pair_stop();
	CHECK(rwcam_core_start(F));
	f0 = got[F];
	CHECK(wait_for(&got[F], f0 + 3, 3000));
	busy_first = 3;
	w1 = got[W];
	CHECK(rwcam_core_start(W) && wait_for(&got[W], w1 + 3, 3000) && busy_first == 0 && stopped_calls == 0);
	rwcam_core_stop(W);

	/* 6. the second node stops giving frames while the ultra-wide camera is shown: the pair ends, node 0 goes on alone with it */
	u1 = got[U];
	CHECK(rwcam_core_pair_start(U) && wait_for(&got[U], u1 + 5, 3000));
	node2_dead = 1;
	for (int i = 0; i < 3000 && rwcam_core_pair_running(); i++) { usleep(1000); }
	CHECK(!rwcam_core_pair_running() && stopped_calls == 1);
	u1 = got[U];
	CHECK(wait_for(&got[U], u1 + 5, 3000) && rwcam_core_owner() == U);
	pthread_mutex_lock(&klock);
	CHECK(kn[0].sensor == 3 && kn[1].opens == 0);
	pthread_mutex_unlock(&klock);
	/* showing the other camera now is node 0 changing sensor, as it always did; a new pair is not there */
	w1 = got[W];
	rwcam_core_pair_show(W);
	CHECK(wait_for(&got[W], w1 + 5, 3000) && rwcam_core_owner() == W);
	rwcam_core_stop(W);
	node2_dead = 0;

	/* 7. headers without levels (an older kernel): the hidden camera's every 10th frame is read whole for its exposure, and is nobody's picture */
	no_levels = 1;
	u1 = got[U];
	uint64_t uw0 = rwcam_core_frames(U), uh0 = rwcam_core_headers(U);

	w1 = got[W];
	CHECK(rwcam_core_pair_start(W) && wait_for(&got[W], w1 + 60, 5000));
	CHECK(got[U] == u1 && rwcam_core_frames(U) > uw0 && rwcam_core_frames(U) - uw0 <= (rwcam_core_headers(U) - uh0) / 5 + 2);
	CHECK(wait_stable(U, 5000));
	no_levels = 0;

	rwcam_core_shutdown();
	usleep(250000);
	CHECK(total_opens == total_closes && foreign == 0);
	printf(failures ? "t_dual: %d FAILURES\n" : "t_dual: all passed (frames to the sink %d/%d/%d, headers only %llu/%llu, shown changed %llu times)\n",
	    failures ? failures : got[0], got[1], got[2], (unsigned long long)rwcam_core_headers(W), (unsigned long long)rwcam_core_headers(U),
	    (unsigned long long)rwcam_core_pair_shows());
	return failures != 0;
}
