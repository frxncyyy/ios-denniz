/*
 * Host test of the group's policy (src/rwcam_pair.c) on the real reader (src/rwcam_core.c) against the fake two-node kernel: the calls are the
 * ones rwcam_device.c's group object and streams make when CMCapture sets the dual-wide device up (ActiveStreams with both streams,
 * MasterConfiguration, the streams' Start), changes master at the zoom's switch-over and tears it down. Checks: both sensors run and only the
 * master's pictures are made; a change of master is immediate; the slave's pictures only when its configuration asks for them, at its ratio;
 * without a second node (or after it gave up) the same calls give the master alone on the one node, a change of master a change of sensor;
 * one active stream is not the group's business; everything stops and the nodes are closed at the end.
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
#include "rwcam_pair.h"
#include "rwcam_log.h"

#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)
static int failures;

void rwcam_say(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
void rwcam_say_once(const char *what, const char *name, const char *detail) { (void)what; (void)name; (void)detail; }
void rwcam_say_keep(const char *format, ...) { if (getenv("VERBOSE")) { va_list ap; va_start(ap, format); printf("   | "); vprintf(format, ap); printf("\n"); va_end(ap); } }
unsigned long redwood_cache_slide(void) { return 0; }

#include "fake_kernel2.h"

static struct rwcam_pair pair;
static atomic_int got[RWCAM_CAMERAS], given[RWCAM_CAMERAS], stopped_calls;
/* as rwcam_device.c's sink: a frame of a back camera is a picture only when the group says so */
static void sink_frame(void *ctx, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp)
{
	(void)ctx; (void)f; (void)raw; (void)isp;
	atomic_fetch_add(&got[cam], 1);
	if (rwcam_pair_delivers(&pair, cam)) { atomic_fetch_add(&given[cam], 1); }
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
static int kint(int *p) { pthread_mutex_lock(&klock); int v = *p; pthread_mutex_unlock(&klock); return v; }

int main(void)
{
	struct rwcam_ops ops = { fake_open, fake_read, fake_write, fake_close, fake_open_node };
	struct rwcam_sink sink = { sink_frame, sink_stopped, NULL };
	const int W = RWCAM_BACK_WIDE, U = RWCAM_BACK_ULTRAWIDE;

	rwcam_core_init(&ops, &sink);
	rwcam_core_align_set(1.6f, 0, 0);       /* (the two cameras' pictures are not compared here: the hidden one is never read whole for it) */
	rwcam_pair_init(&pair);

	/* 1. the group set up and not started: nothing runs; one stream active is not the group's business */
	rwcam_pair_set_active(&pair, true, true);
	rwcam_pair_set_master(&pair, W);
	CHECK(rwcam_pair_wanted(&pair) && rwcam_pair_mode(&pair) == RWCAM_PAIR_IDLE && kint(&kn[0].opens) == 0 && kint(&kn[1].opens) == 0);
	rwcam_pair_set_active(&pair, true, false);
	CHECK(!rwcam_pair_wanted(&pair) && rwcam_pair_set_started(&pair, W, true) && rwcam_pair_mode(&pair) == RWCAM_PAIR_IDLE && kint(&kn[0].opens) == 0);
	CHECK(rwcam_pair_set_started(&pair, W, false));
	rwcam_pair_set_active(&pair, true, true);

	/* 2. started (the master's Start, then the slave's): both sensors, the wide camera's pictures, none of the ultra-wide one */
	CHECK(rwcam_pair_set_started(&pair, W, true) && rwcam_pair_mode(&pair) == RWCAM_PAIR_BOTH && rwcam_core_pair_running());
	CHECK(rwcam_pair_set_started(&pair, U, true));
	CHECK(wait_for(&given[W], 20, 3000) && got[U] == 0 && knum(&kn[1].header_reads) > 5 && knum(&kn[1].whole_reads) == 0);
	CHECK(rwcam_pair_delivers(&pair, W) && !rwcam_pair_delivers(&pair, U) && !rwcam_pair_delivers(&pair, RWCAM_FRONT));
	/* 3. the zoom goes under the switch-over: MasterConfiguration names the ultra-wide stream; its pictures at once, the wide one's stop */
	int w = given[W];
	/* (each node's sensor was asked for once, before its first read: a change of master asks for nothing more) */
	uint64_t s0 = knum(&kn[0].sensor_cmds), s1 = knum(&kn[1].sensor_cmds);

	CHECK(s0 == 1 && s1 == 1);
	rwcam_pair_set_master(&pair, U);
	CHECK(wait_for(&given[U], 1, 60) && wait_for(&given[U], 20, 3000) && rwcam_pair_master(&pair) == U);
	usleep(10000);
	w = given[W];
	usleep(15000);
	CHECK(given[W] == w && knum(&kn[0].sensor_cmds) == s0 && knum(&kn[1].sensor_cmds) == s1);
	/* 4. and back; then the slave's pictures asked for, one for every two of the master's frames */
	rwcam_pair_set_master(&pair, W);
	CHECK(wait_for(&given[W], w + 10, 3000));
	int u = given[U];

	usleep(15000);
	CHECK(given[U] <= u + 1);
	rwcam_pair_set_slave(&pair, true, 2);
	u = given[U];
	w = given[W];
	CHECK(wait_for(&given[U], u + 10, 3000) && rwcam_pair_delivers(&pair, U));
	CHECK(given[W] - w >= (given[U] - u) && given[W] - w <= 3 * (given[U] - u) + 8);
	rwcam_pair_set_slave(&pair, false, 2);
	usleep(10000);
	u = given[U];
	usleep(15000);
	CHECK(given[U] == u);
	/*
	 * 4b. the device's zoom says which SENSOR is shown, whoever is the master (run q10ht): under RWCAM_UW_SWITCH_ZOOM the ultra-wide one, from
	 * it on the wide one; its pictures are the master stream's.
	 */
	rwcam_pair_set_master(&pair, W);
	rwcam_pair_set_zoom(&pair, 1.5f);
	CHECK(rwcam_pair_shown(&pair) == U && rwcam_pair_route(&pair, U) == W && rwcam_pair_route(&pair, W) == -1);
	u = given[U];
	CHECK(wait_for(&given[U], u + 10, 3000));
	w = given[W];
	usleep(15000);
	CHECK(given[W] == w);
	{
		/* away from the change the hidden wide sensor is read by its headers; near it, whole too (and still no picture of it): ready */
		uint64_t whole = knum(&kn[0].whole_reads);

		usleep(20000);
		CHECK(knum(&kn[0].whole_reads) == whole);
		rwcam_pair_set_zoom(&pair, 1.8f);
		usleep(5000);
		whole = knum(&kn[0].whole_reads);
		w = given[W];
		for (int i = 0; i < 3000 && knum(&kn[0].whole_reads) < whole + 10; i++) { usleep(1000); }
		CHECK(knum(&kn[0].whole_reads) >= whole + 10 && given[W] == w && rwcam_pair_shown(&pair) == U);
	}
	rwcam_pair_set_zoom(&pair, 1.999f);
	CHECK(rwcam_pair_shown(&pair) == U);
	rwcam_pair_set_zoom(&pair, 2.0f);
	CHECK(rwcam_pair_shown(&pair) == W && rwcam_pair_route(&pair, W) == W && rwcam_pair_route(&pair, U) == -1);
	w = given[W];
	CHECK(wait_for(&given[W], w + 10, 3000));
	rwcam_pair_set_zoom(&pair, 3.0f);        /* (far from the change again: the ultra-wide sensor's headers only) */
	usleep(10000);
	{
		uint64_t whole = knum(&kn[1].whole_reads);

		usleep(20000);
		CHECK(knum(&kn[1].whole_reads) == whole);
	}
	rwcam_pair_set_master(&pair, U);        /* (CMCapture's master is the ultra-wide stream while the zoom is still the wide sensor's) */
	rwcam_pair_set_zoom(&pair, 2.5f);
	CHECK(rwcam_pair_shown(&pair) == W && rwcam_pair_route(&pair, W) == U && rwcam_pair_route(&pair, U) == -1);
	rwcam_pair_set_zoom(&pair, 1.0f);
	CHECK(rwcam_pair_shown(&pair) == U && rwcam_pair_route(&pair, U) == U);
	rwcam_pair_set_master(&pair, W);
	rwcam_pair_set_zoom(&pair, 2.0f);
	{
		/* the ultra-wide camera's window: all of its sensor at 1, the wide camera's field and axis at the switch, evenly between, no jump */
		float z, x, y, before = 1;

		rwcam_uw_view(1.0f, 1.7f, 0.03f, -0.02f, &z, &x, &y);
		CHECK(z == 1 && x == 0 && y == 0);
		rwcam_uw_view(RWCAM_UW_SWITCH_ZOOM, 1.7f, 0.03f, -0.02f, &z, &x, &y);
		CHECK(z > 1.699f && z < 1.701f && x > 0.0299f && x < 0.0301f && y < -0.0199f && y > -0.0201f);
		for (float d = 1.0f; d <= 4.0f; d += 0.01f) {
			rwcam_uw_view(d, 1.7f, 0.03f, -0.02f, &z, &x, &y);
			CHECK(z >= before && z < before * 1.012f && x >= 0 && x <= 0.0301f);
			before = z;
		}
		rwcam_uw_view(1.5f, 0, 0.5f, 0.5f, &z, &x, &y);            /* (nothing known: the nominal ratio, the middle) */
		CHECK(z > 1.3f && z < 1.33f && x == 0 && y == 0);
	}
	/* 5. the streams stopped: everything closed */
	CHECK(rwcam_pair_set_started(&pair, U, false) && rwcam_pair_mode(&pair) == RWCAM_PAIR_BOTH);
	CHECK(rwcam_pair_set_started(&pair, W, false) && rwcam_pair_mode(&pair) == RWCAM_PAIR_IDLE && !rwcam_core_pair_running());
	CHECK(kint(&kn[0].opens) == 0 && kint(&kn[1].opens) == 0 && rwcam_core_owner() == -1);

	/* 6. no second node: the same calls, the master alone; a change of master is node 0 changing sensor */
	node2_there = 0;
	w = given[W];
	CHECK(rwcam_pair_set_started(&pair, W, true) && rwcam_pair_set_started(&pair, U, true) && rwcam_pair_mode(&pair) == RWCAM_PAIR_ALONE);
	CHECK(wait_for(&given[W], w + 10, 3000) && kint(&kn[1].opens) == 0 && !rwcam_core_pair_running());
	u = given[U];
	s0 = knum(&kn[0].sensor_cmds);
	rwcam_pair_set_master(&pair, U);
	CHECK(wait_for(&given[U], u + 10, 3000) && kint(&kn[0].sensor) == 3 && knum(&kn[0].sensor_cmds) == s0 + 1);
	w = given[W];
	usleep(10000);
	CHECK(given[W] == w);
	rwcam_pair_set_master(&pair, W);
	CHECK(wait_for(&given[W], w + 10, 3000) && kint(&kn[0].sensor) == 1);
	rwcam_pair_set_active(&pair, false, false);     /* (the group taken apart: ActiveStreams emptied) */
	CHECK(rwcam_pair_mode(&pair) == RWCAM_PAIR_IDLE && kint(&kn[0].opens) == 0);
	CHECK(rwcam_pair_set_started(&pair, W, false) && rwcam_pair_set_started(&pair, U, false));
	node2_there = 1;

	/* 7. the second node gives up while the pair runs: the master goes on alone; a later change of master does not try the pair again */
	rwcam_pair_set_active(&pair, true, true);
	w = given[W];
	CHECK(rwcam_pair_set_started(&pair, W, true) && rwcam_pair_set_started(&pair, U, true) && rwcam_pair_mode(&pair) == RWCAM_PAIR_BOTH);
	CHECK(wait_for(&given[W], w + 10, 3000));
	node2_dead = 1;
	for (int i = 0; i < 3000 && rwcam_core_pair_running(); i++) { usleep(1000); }
	CHECK(!rwcam_core_pair_running() && stopped_calls == 1);
	w = given[W];
	CHECK(wait_for(&given[W], w + 10, 3000));
	u = given[U];
	uint64_t opens2 = knum(&kn[1].header_reads) + knum(&kn[1].whole_reads);

	rwcam_pair_set_master(&pair, U);
	CHECK(wait_for(&given[U], u + 10, 3000) && rwcam_pair_mode(&pair) == RWCAM_PAIR_ALONE && kint(&kn[1].opens) == 0);
	CHECK(knum(&kn[1].header_reads) + knum(&kn[1].whole_reads) == opens2);
	CHECK(rwcam_pair_set_started(&pair, W, false) && rwcam_pair_set_started(&pair, U, false) && rwcam_pair_mode(&pair) == RWCAM_PAIR_IDLE);
	node2_dead = 0;
	/* ... and the next time the group is started the pair is tried again */
	rwcam_pair_set_master(&pair, W);
	w = given[W];
	CHECK(rwcam_pair_set_started(&pair, W, true) && rwcam_pair_mode(&pair) == RWCAM_PAIR_BOTH && wait_for(&given[W], w + 5, 3000));
	CHECK(rwcam_pair_set_started(&pair, W, false));

	rwcam_core_shutdown();
	usleep(250000);
	CHECK(total_opens == total_closes);
	printf(failures ? "t_pair: %d FAILURES\n" : "t_pair: all passed (pictures %d wide, %d ultra-wide; master changed %lu times)\n", failures ? failures : given[W],
	    given[U], pair.masters);
	return failures != 0;
}
