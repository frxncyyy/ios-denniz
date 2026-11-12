#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "rwcam_core.h"
#ifdef __APPLE__
#define RW_SETNAME(n) pthread_setname_np(n)
#else
#define RW_SETNAME(n) pthread_setname_np(pthread_self(), n)
#endif
#include "rwcam_log.h"
#include "rwcam_align.h"
#include "rwa_isp_api.h"      /* (q10jo: the GPU statistics' layout) */

/* milliseconds that only go forward, and the kernel's frame stamps (mach_absolute_time()'s units) as milliseconds */
#ifdef __APPLE__
struct rw_timebase { uint32_t numer, denom; };
extern uint64_t mach_absolute_time(void);
extern int mach_timebase_info(struct rw_timebase *);
static uint64_t
units_ms(uint64_t units)
{
	static struct rw_timebase tb;

	if (tb.denom == 0) {
		struct rw_timebase t = { 0, 0 };

		(void)mach_timebase_info(&t);
		if (t.denom == 0 || t.numer == 0) {
			t.numer = t.denom = 1;
		}
		tb = t;
	}
	return units / 1000000u * tb.numer / tb.denom + units % 1000000u * tb.numer / tb.denom / 1000000u;
}
uint64_t
rwcam_now_ms(void)
{
	return units_ms(mach_absolute_time());
}
uint64_t
rwcam_ns_units(uint64_t ns)
{
	struct rw_timebase t = { 0, 0 };

	(void)mach_timebase_info(&t);
	return t.numer != 0 && t.denom != 0 ? ns * t.denom / t.numer : ns;
}
#else
#include <time.h>
static uint64_t
units_ms(uint64_t units)
{
	return units / 1000000u;
}
uint64_t
rwcam_ns_units(uint64_t ns)
{
	return ns;
}
uint64_t
rwcam_now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
#endif

/*
 * Run q10ha: the kernel's frames stop after one to eight of them (its DMA is not armed again: "NORD2 CAMERA: no frame for 3 s"), a read then waits
 * its 3 s, the session is started anew (another 1.5 s), and after five such sessions in a row the kernel gives the camera up for good (EIO). Until the
 * kernel is mended: a read that has waited KICK_MS for a frame of a session that HAS given frames is taken as that stall, and the session is started
 * anew at once through the one way the ABI has -- REDWOOD_CAMERA_SENSOR for another sensor and, 20 ms later (the kernel's session thread looks every
 * 3 ms and then takes over 40 ms to put the sensor down), for ours again. A session ended like that is not one of the kernel's five failures.
 * Never while a session is starting (no frame of it yet): then the kernel's own 3 s decide. /var/db/nord2-camera-nokick switches this off.
 */
#define KICK_MS         700
#define KICK_PAUSE_US   20000
#define SAY_FRAMES      3               /* the first frames of a reader are said with their times; after that a summary every SAY_MS */
#define SAY_MS          5000
#define SAY_KICKS       40

const uint32_t rwcam_sensor_of[RWCAM_CAMERAS] = { 1, 3, 2 };

void
rwcam_uw_view(float device_zoom, float ratio, float dx, float dy, float *zoom, float *shift_x, float *shift_y)
{
	float z = !(device_zoom >= 1) ? 1 : device_zoom, v, part;

	if (!(ratio > 1.05f)) {
		ratio = RWCAM_UW_RATIO;
		dx = dy = 0;
	}
	/* z^(log ratio / log switch): 1 at 1, ratio at the switch; past the switch the device's zoom over the switch's, times the ratio */
	v = z >= RWCAM_UW_SWITCH_ZOOM ? z * ratio / RWCAM_UW_SWITCH_ZOOM : (float)pow((double)z, log((double)ratio) / log((double)RWCAM_UW_SWITCH_ZOOM));
	/* the middle: the sensor's at 1, the wide camera's axis from the switch on -- by how much of the window's way to the switch is done */
	part = v >= ratio ? 1 : (v - 1) / (ratio - 1);
	*zoom = v;
	*shift_x = dx * part;
	*shift_y = dy * part;
}

float
rwcam_view_zoom(int cam, float graph_zoom, float uw_ratio)
{
	float z = !(graph_zoom >= 1) ? 1 : graph_zoom;

	if (cam != RWCAM_BACK_ULTRAWIDE || !(uw_ratio > 1) || uw_ratio >= RWCAM_UW_SWITCH_ZOOM) {
		return z;       /* (lenses a full RWCAM_UW_SWITCH_ZOOM apart, or a ratio that says nothing: nothing to make up) */
	}
	if (z >= RWCAM_UW_SWITCH_ZOOM) {
		return z * uw_ratio / RWCAM_UW_SWITCH_ZOOM;
	}
	/* z^(log ratio / log switch): 1 at 1, ratio at the switch */
	return (float)pow((double)z, log((double)uw_ratio) / log((double)RWCAM_UW_SWITCH_ZOOM));
}

/*
 * Two readers, one for each of the kernel's nodes (KERNEL_ABI_DUAL.md). Node 0 (/dev/redwood-camera) is the one there has always been: it runs
 * whichever camera is wanted, one at a time (rwcam_core_start). The PAIR (rwcam_core_pair_start) is the two back cameras at once, for
 * CMCapture's dual-wide device: the wide camera on node 0 and the ultra-wide one on node 1 (/dev/redwood-camera2), both sensors running. Only
 * the SHOWN camera of the pair is read whole and given to the sink; of the other one only the frames' headers are read (120 bytes: no picture
 * is copied or looked at), its exposure kept right by the levels the kernel puts into the header and its white balance by the shown camera's
 * (the same light). Its frame buffer is there already, so when it becomes the shown one its next frame is read whole: one frame's time.
 * A node 1 that is not there, cannot be opened or gives no frames ends the pair: node 0 goes on alone with the camera that is shown, as
 * before there was a pair.
 */
#define NODES 2
#define HIDDEN_FULL_EVERY       10      /* a kernel whose headers have no levels: the hidden camera's every 10th frame is read whole for them */
#define BUSY_TRIES              40      /* REDWOOD_CAMERA_SENSOR refused with EBUSY (the other node still has that sensor): asked again, 50 ms apart */

struct node {
	int index;
	int owner;                      /* the camera this node is to run, or -1 */
	bool thread_running;
	pthread_t thread;
	/* the stall watch */
	uint64_t read_since;            /* when the reader went into the read it is in (0: not in one) */
	uint64_t        wait_said;              /* how many times 5 s of this read without a frame were said */
	uint32_t session_frames;        /* frames (or headers) read since the kernel's session was last known to start anew */
	uint32_t want_sensor;           /* the sensor the reader is on */
	bool have_ae;
	struct redwood_camera_command last_ae;  /* AE's last command: given again to a session started anew */
};

/* the two back cameras' pictures on one another (rwcam_core.h); not in C: that has initial values and would be in the file, small pictures and all */
static pthread_mutex_t align_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	struct rwcam_align_filter filter;
	struct rwcam_alignment now;     /* (ratio 0: nothing found yet) */
	unsigned found;
	bool manual;
	float manual_ratio, manual_dx, manual_dy;
	uint8_t thumb[2][RWCAM_ALIGN_W * RWCAM_ALIGN_H];        /* [0] the wide camera's, [1] the ultra-wide one's */
	uint64_t thumb_ms[2];
	bool want[2];          /* a small picture of this camera's next frame is asked for */
	uint64_t next_ms, tries, good;
	bool busy;                      /* a comparison is under way (align_worker) */
} A;

static struct {
	pthread_mutex_t lock;
	struct rwcam_ops ops;
	struct rwcam_sink sink;
	bool inited;
	struct node node[NODES];
	bool pair;                      /* the two back cameras on the two nodes */
	int shown;                      /* the pair's camera whose frames are read whole and given to the sink */
	uint32_t flicker_ns;
	bool flicker_set, no_align;
	unsigned slave_every;           /* the pair's other camera: every n-th frame of it is read whole and given to the sink too (0: none) */
	uint64_t pair_ends, shows;
	float pair_wb[3];               /* the shown camera's white balance, for the hidden one */
	float pair_aim;                 /* ... and the share of its target its AE goes for (camera_isp.h: the highlights); 0: not known */
	bool pair_wb_valid;
	bool warm;                      /* the hidden camera's frames are read whole too (rwcam_core_pair_warm) */
	uint64_t warm_until;            /* ... until then (ms): the zoom has to keep moving for it to go on */
	struct rwcam_status status[RWCAM_CAMERAS];
	uint64_t switches;
	uint64_t frames[RWCAM_CAMERAS], headers[RWCAM_CAMERAS];
	struct isp isp[RWCAM_CAMERAS];  /* each camera's own exposure and white balance */
	bool isp_ready[RWCAM_CAMERAS];
	struct isp_af af[RWCAM_CAMERAS];        /* (the reader's) */
	struct { float cx, cy, w, h; bool again, set, hold; } af_win[RWCAM_CAMERAS];  /* (under lock) */
	struct { float cx, cy, w, h; bool set, locked; } ae_win[RWCAM_CAMERAS];
	bool stop_all;
	bool watch_running, no_kick, wb_free, no_falloff;
	uint32_t frames_long;           /* frames up to this many times the mode's own length, in the dark */
	/*
	 * How each camera was exposed when it was last steady (run user1: a camera started from the sensor's own 16.7 ms x 2 and climbed, at most
	 * 2.6 times a step with two frames between steps: about a second in the dark before the picture was right). It starts with that again:
	 * the exposure asked of the sensor before its first frame is read, the white balance and the gain of the picture as they were. Kept
	 * while mediaserverd lives. /var/db/nord2-camera-coldstart: not.
	 */
	struct { bool have; uint32_t lines, gain, frame_lines; float wb[3], digital; } last[RWCAM_CAMERAS];
	bool cold;
	bool even_rate;                 /* a film is being made: the frames keep their length (rwcam_core_even_rate) */
	unsigned want_mode;             /* (q10jm) the main camera's sensor mode (rwcam_core_set_mode) */
	void *(*frame_mem)(int node, unsigned slot, size_t header_bytes, size_t frame_bytes);   /* (q10jn) rwcam_core_frame_memory */
	bool no_modes;                  /* (q10jm) the kernel refused REDWOOD_CAMERA_MODE (EINVAL): an older kernel, never asked again */
	uint64_t kicks;
} C = { .lock = PTHREAD_MUTEX_INITIALIZER, .node = { { .index = 0, .owner = -1 }, { .index = 1, .owner = -1 } }, .shown = -1 };

void
rwcam_core_alignment(float *ratio, float *dx, float *dy, unsigned *found)
{
	pthread_mutex_lock(&align_lock);
	if (A.manual) {
		*ratio = A.manual_ratio;
		*dx = A.manual_dx;
		*dy = A.manual_dy;
	} else if (A.found != 0) {
		*ratio = A.now.ratio;
		*dx = A.now.dx;
		*dy = A.now.dy;
	} else {
		*ratio = RWCAM_UW_RATIO;
		*dx = *dy = 0;
	}
	if (found != NULL) {
		*found = A.manual ? 0 : A.found;
	}
	pthread_mutex_unlock(&align_lock);
}

void
rwcam_core_align_set(float ratio, float dx, float dy)
{
	pthread_mutex_lock(&align_lock);
	__atomic_store_n(&A.manual, ratio > 1.05f, __ATOMIC_RELAXED);
	A.manual_ratio = ratio;
	A.manual_dx = dx;
	A.manual_dy = dy;
	pthread_mutex_unlock(&align_lock);
}

void
rwcam_core_flicker(uint32_t ns)
{
	C.flicker_ns = ns;
	C.flicker_set = true;
}

void
rwcam_core_even_rate(bool even)
{
	C.even_rate = even;
}

void
rwcam_core_frame_memory(void *(*fn)(int node, unsigned slot, size_t header_bytes, size_t frame_bytes))
{
	pthread_mutex_lock(&C.lock);
	C.frame_mem = fn;
	pthread_mutex_unlock(&C.lock);
}

/*
 * (q10jo) the GPU's statistics of each camera's latest frame (rwcam_core_gpu_stats), on the heap (not in C: that is in the file), under gs_lock:
 * isp_measure()'s numbers summed from the blocks, and each block's sharpness sums for the focus window.
 */
#define GS_BLOCKS       (64 * 64)
struct gpu_stats {
	uint64_t seq;                   /* the frame's number (0: none yet) */
	uint32_t fw, fh, w, h, step;
	struct isp_stats st;
	float edges[GS_BLOCKS], light[GS_BLOCKS], n[GS_BLOCKS];
};
static struct gpu_stats *gs[RWCAM_CAMERAS];
static pthread_mutex_t gs_lock = PTHREAD_MUTEX_INITIALIZER;
static int gs_off = -1;

void
rwcam_core_gpu_stats(const struct redwood_camera_frame *f, const float *stats, uint32_t w, uint32_t h, uint32_t step)
{
	int cam = -1;
	double sum[3][4] = { { 0 } };
	struct gpu_stats *g;

	for (int c = 0; c < RWCAM_CAMERAS; c++) {
		cam = rwcam_sensor_of[c] == f->sensor ? c : cam;
	}
	if (cam < 0 || w == 0 || h == 0 || (uint64_t)w * h > GS_BLOCKS || step == 0) {
		return;
	}
	pthread_mutex_lock(&gs_lock);
	if (gs[cam] == NULL) {
		gs[cam] = calloc(1, sizeof(*gs[cam]));
	}
	g = gs[cam];
	if (g == NULL) {
		pthread_mutex_unlock(&gs_lock);
		return;
	}
	memset(&g->st, 0, sizeof(g->st));
	for (uint32_t bi = 0; bi < w * h; bi++) {
		const float *k = stats + (size_t)bi * RWA_ISP_STATS_KINDS * 4;

		for (int t = 0; t < 3; t++) {
			for (int c = 0; c < 4; c++) {
				sum[t][c] += k[t * 4 + c];
			}
		}
		for (int b = 0; b < 64; b++) {
			g->st.hist[b] += (uint32_t)k[(3 + b / 4) * 4 + b % 4];
		}
		g->edges[bi] = k[19 * 4];
		g->light[bi] = k[19 * 4 + 1];
		g->n[bi] = k[19 * 4 + 2];
	}
	g->st.cells = (uint32_t)sum[0][3];
	g->st.fair_cells = (uint32_t)sum[1][3];
	g->st.grey_cells = (uint32_t)sum[2][3];
	for (int c = 0; c < 3; c++) {
		g->st.mean[c] = g->st.cells ? sum[0][c] / g->st.cells : 0;
		g->st.fair[c] = g->st.fair_cells ? sum[1][c] / g->st.fair_cells : 0;
		g->st.grey[c] = g->st.grey_cells ? sum[2][c] / g->st.grey_cells : 0;
	}
	for (int i = 63, above = 0; i >= 0; i--) {             /* (isp_measure's own rule) */
		above += (int)g->st.hist[i];
		if (above * 100 >= (int)g->st.cells) {
			g->st.bright = i * 16 + 8;
			break;
		}
	}
	g->seq = f->sequence;
	g->fw = f->width;
	g->fh = f->height;
	g->w = w;
	g->h = h;
	g->step = step;
	pthread_mutex_unlock(&gs_lock);
}

/* (q10jo) the GPU's statistics for frame f of camera cam, when there are recent ones of a frame of the same size: true and *st */
static bool
gs_take(int cam, const struct redwood_camera_frame *f, struct isp_stats *st)
{
	bool ok = false;

	if (gs_off < 0) {
		gs_off = access("/var/db/nord2-camera-cpu-stats", F_OK) == 0;
	}
	if (gs_off == 1 || cam < 0 || cam >= RWCAM_CAMERAS) {
		return false;
	}
	pthread_mutex_lock(&gs_lock);
	struct gpu_stats *g = gs[cam];

	if (g != NULL && g->seq != 0 && g->seq <= f->sequence && f->sequence - g->seq <= 3 && g->fw == f->width && g->fh == f->height) {
		*st = g->st;
		ok = true;
	}
	pthread_mutex_unlock(&gs_lock);
	return ok;
}

/* (q10jo) the sharpness of the window (x, y, w, h: pixels) from the GPU's blocks, as isp_sharpness() reckons it; < 0: none */
static double
gs_sharpness(int cam, const struct redwood_camera_frame *f, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	double edges = 0, light = 0, n = 0, out = -1;

	pthread_mutex_lock(&gs_lock);
	struct gpu_stats *g = cam >= 0 && cam < RWCAM_CAMERAS ? gs[cam] : NULL;

	if (g != NULL && g->seq != 0 && g->fw == f->width && g->fh == f->height && g->step != 0) {
		uint32_t span = g->step * RWA_ISP_STATS_BLOCK * 2;      /* (pixels a block covers each way) */
		uint32_t bx0 = x / span, by0 = y / span, bx1 = (x + w + span - 1) / span, by1 = (y + h + span - 1) / span;

		bx1 = bx1 > g->w ? g->w : bx1;
		by1 = by1 > g->h ? g->h : by1;
		for (uint32_t by = by0; by < by1; by++) {
			for (uint32_t bx = bx0; bx < bx1; bx++) {
				edges += g->edges[by * g->w + bx];
				light += g->light[by * g->w + bx];
				n += g->n[by * g->w + bx];
			}
		}
		if (n > 0) {
			out = light < n * 16.0 ? 0 : edges / n / ((light / n) * (light / n)) * 1e4;
		}
	}
	pthread_mutex_unlock(&gs_lock);
	return out;
}

/* (q10jn) a frame buffer: slot `slot` of the frame memory when there is one, else the heap; fm_free: only what came from the heap */
static uint8_t *
fm_alloc(int node, int slot, size_t header, size_t bytes, uint8_t *slotp[2])
{
	pthread_mutex_lock(&C.lock);
	void *(*fn)(int, unsigned, size_t, size_t) = C.frame_mem;
	pthread_mutex_unlock(&C.lock);
	if (fn != NULL && header < bytes) {
		uint8_t *m = fn(node, (unsigned)slot, header, bytes - header);

		if (m != NULL) {
			slotp[slot] = m;
			return m;
		}
	}
	return malloc(bytes);
}

static void
fm_free(uint8_t *b, uint8_t *const slotp[2])
{
	if (b != NULL && b != slotp[0] && b != slotp[1]) {
		free(b);
	}
}

void
rwcam_core_set_mode(unsigned mode)
{
	pthread_mutex_lock(&C.lock);
	if (C.want_mode != mode) {
		rwcam_say("core: the main camera's sensor mode %u asked (%s)", mode, mode == 0 ? "4096x3072, 30 a second" : mode == 1 ?
		    "2048x1152, 60 a second" : "4096x2304, 60 a second");
	}
	C.want_mode = mode;
	pthread_mutex_unlock(&C.lock);
}

/* a small picture of the whole of a frame, brightness only */
static void
align_thumb(int cam, const struct redwood_camera_frame *f, const uint8_t *raw, uint64_t now)
{
	static struct isp view[2];              /* (one for each reader's thread: an isp is 20 kB) */
	int k = cam == RWCAM_BACK_ULTRAWIDE;
	uint8_t uv[RWCAM_ALIGN_W * (RWCAM_ALIGN_H / 2 + 1)];
	uint8_t y[RWCAM_ALIGN_W * RWCAM_ALIGN_H];

	if ((uint64_t)f->width * 3 != (uint64_t)f->height * 4) {
		return;         /* (q10jm: a 16:9 sensor mode is not the whole of the lens's view: no comparison; the last alignment stays) */
	}
	view[k] = C.isp[cam];
	view[k].zoom = 1;
	view[k].flip = 0;
	view[k].shift_x = view[k].shift_y = 0;
	view[k].digital = 1;
	isp_render_nv12(&view[k], f, raw, y, RWCAM_ALIGN_W, uv, RWCAM_ALIGN_W, RWCAM_ALIGN_W, RWCAM_ALIGN_H, 0, RWCAM_ALIGN_H, false);
	pthread_mutex_lock(&align_lock);
	memcpy(A.thumb[k], y, sizeof(y));
	A.thumb_ms[k] = now;
	pthread_mutex_unlock(&align_lock);
}

/*
 * The comparison itself, on a thread of its own (run q10hy: it took 160 to 416 ms on the hidden camera's reader, and a change of the camera
 * shown while it ran had to wait for it -- "a small freeze" at the crossover).
 */
static void *
align_worker(void *arg)
{
	int cam = (int)(intptr_t)arg, k = cam == RWCAM_BACK_ULTRAWIDE, other = cam == RWCAM_BACK_ULTRAWIDE ? RWCAM_BACK_WIDE : RWCAM_BACK_ULTRAWIDE;
	static uint8_t pictures[2][RWCAM_ALIGN_W * RWCAM_ALIGN_H];      /* (one comparison at a time: A.busy) */
	struct rwcam_alignment found, now;
	uint64_t t0, t1;
	bool have = false, better;

	RW_SETNAME("redwood-camalign");
	pthread_mutex_lock(&align_lock);
	t0 = A.thumb_ms[k];
	pthread_mutex_unlock(&align_lock);
	for (int i = 0; i < 40 && !have; i++) {
		usleep(4000);
		pthread_mutex_lock(&align_lock);
		have = A.thumb_ms[1 - k] + 5 >= t0;
		pthread_mutex_unlock(&align_lock);
	}
	__atomic_store_n(&A.want[1 - k], false, __ATOMIC_RELAXED);
	pthread_mutex_lock(&align_lock);
	A.tries++;
	memcpy(pictures[0], A.thumb[0], sizeof(pictures[0]));
	memcpy(pictures[1], A.thumb[1], sizeof(pictures[1]));
	pthread_mutex_unlock(&align_lock);
	if (have) {
		rwcam_align(pictures[0], pictures[1], &found);
		t1 = rwcam_now_ms();
		pthread_mutex_lock(&align_lock);
		A.good += found.ok;
		better = rwcam_align_add(&A.filter, &found, &now);
		if (better && A.filter.n >= 3) {        /* (three findings at least: the first two were 6 % off in run q10hy) */
			A.now = now;
			A.found = A.filter.n;
		}
		pthread_mutex_unlock(&align_lock);
		rwcam_say_keep("core: the two back cameras' pictures compared (camera %d's reader asked, %d the other; %llu ms, on a thread of its own): %s -- "
		    "the wide picture is 1/%.3f of the ultra-wide one, its middle %.4f, %.4f off the middle (correlation %.2f, elsewhere at most %.2f); in "
		    "use now: %s 1/%.3f, %.4f, %.4f (of %u)", cam, other, (unsigned long long)(t1 - t0), found.ok ? "FOUND" : "not clear enough, not used",
		    (double)found.ratio, (double)found.dx, (double)found.dy, (double)found.score, (double)found.second, A.found ? "found" : "nominal",
		    A.found ? (double)A.now.ratio : (double)RWCAM_UW_RATIO, A.found ? (double)A.now.dx : 0.0, A.found ? (double)A.now.dy : 0.0, A.found);
	}
	__atomic_store_n(&A.busy, false, __ATOMIC_RELEASE);
	return NULL;
}

/* a reader has a whole frame of a camera that is not the one shown: its small picture, the shown camera's asked for, and the two compared */
static void
align_now(int cam, const struct redwood_camera_frame *f, const uint8_t *raw)
{
	int k = cam == RWCAM_BACK_ULTRAWIDE;
	uint64_t t0 = rwcam_now_ms();
	pthread_t t;
	bool idle = false;

	if (!__atomic_compare_exchange_n(&A.busy, &idle, true, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
		return;
	}
	pthread_mutex_lock(&align_lock);
	__atomic_store_n(&A.next_ms, t0 + (A.good < 8 ? 2500 : A.good < 16 ? 8000 : 30000), __ATOMIC_RELAXED);
	pthread_mutex_unlock(&align_lock);
	align_thumb(cam, f, raw, t0);
	__atomic_store_n(&A.want[1 - k], true, __ATOMIC_RELAXED);
	if (pthread_create(&t, NULL, align_worker, (void *)(intptr_t)cam) != 0) {
		__atomic_store_n(&A.want[1 - k], false, __ATOMIC_RELAXED);
		__atomic_store_n(&A.busy, false, __ATOMIC_RELEASE);
		return;
	}
	pthread_detach(t);
}


static int
real_open(void)
{
	return open(REDWOOD_CAMERA_DEVICE, O_RDWR);
}

static int
real_open_node(int node)
{
	return open(node == 0 ? REDWOOD_CAMERA_DEVICE : REDWOOD_CAMERA_DEVICE2, O_RDWR);
}

static ssize_t
real_read(int fd, void *buf, size_t n)
{
	return read(fd, buf, n);
}

static ssize_t
real_write(int fd, const void *buf, size_t n)
{
	return write(fd, buf, n);
}

static void
real_close(int fd)
{
	close(fd);
}

static int
open_node(int node)
{
	if (C.ops.open_node != NULL) {
		return C.ops.open_node(node);
	}
	if (node == 0 && C.ops.open_dev != NULL) {
		return C.ops.open_dev();
	}
	errno = ENOENT;
	return -1;
}

void
rwcam_core_init(const struct rwcam_ops *ops, const struct rwcam_sink *sink)
{
	pthread_mutex_lock(&C.lock);
	if (ops != NULL) {
		C.ops = *ops;
	} else {
		C.ops.open_dev = real_open;
		C.ops.open_node = real_open_node;
		C.ops.read_dev = real_read;
		C.ops.write_dev = real_write;
		C.ops.close_dev = real_close;
	}
	if (sink != NULL) {
		C.sink = *sink;
	}
	C.inited = true;
	pthread_mutex_unlock(&C.lock);
}

int
rwcam_core_owner(void)
{
	pthread_mutex_lock(&C.lock);
	int o = C.pair ? C.shown : C.node[0].owner;

	pthread_mutex_unlock(&C.lock);
	return o;
}

uint64_t
rwcam_core_switches(void)
{
	return __atomic_load_n(&C.switches, __ATOMIC_RELAXED);
}

uint64_t
rwcam_core_frames(int cam)
{
	return cam >= 0 && cam < RWCAM_CAMERAS ? __atomic_load_n(&C.frames[cam], __ATOMIC_RELAXED) : 0;
}

uint64_t
rwcam_core_headers(int cam)
{
	return cam >= 0 && cam < RWCAM_CAMERAS ? __atomic_load_n(&C.headers[cam], __ATOMIC_RELAXED) : 0;
}

void
rwcam_core_status(int cam, struct rwcam_status *out)
{
	memset(out, 0, sizeof(*out));
	if (cam >= 0 && cam < RWCAM_CAMERAS) {
		pthread_mutex_lock(&C.lock);
		*out = C.status[cam];
		pthread_mutex_unlock(&C.lock);
	}
}

#define FAIL_LIMIT 15           /* consecutive reads that gave nothing (each waits up to a second in the kernel) */

/*
 * What the user asks of the focus (a tap, a lock) is asked of the sensor that is SHOWN: on the dual-wide device that is not always the camera
 * of the stream it was said to (rwcam_pair.h). The ultra-wide camera's lens does not move: a tap while it is shown moves nothing.
 */
static int
focus_camera(int cam)
{
	int shown;

	pthread_mutex_lock(&C.lock);
	shown = C.pair && (cam == RWCAM_BACK_WIDE || cam == RWCAM_BACK_ULTRAWIDE) && C.shown >= 0 ? C.shown : cam;
	pthread_mutex_unlock(&C.lock);
	return shown;
}

void
rwcam_core_focus_window(int cam, float cx, float cy, float w, float h, bool again)
{
	if (cam < 0 || cam >= RWCAM_CAMERAS) {
		return;
	}
	cam = again ? focus_camera(cam) : cam;
	pthread_mutex_lock(&C.lock);
	C.af_win[cam].cx = cx < 0 ? 0 : cx > 1 ? 1 : cx;
	C.af_win[cam].cy = cy < 0 ? 0 : cy > 1 ? 1 : cy;
	C.af_win[cam].w = w < 0.02f ? 0.02f : w > 1 ? 1 : w;
	C.af_win[cam].h = h < 0.02f ? 0.02f : h > 1 ? 1 : h;
	C.af_win[cam].again = C.af_win[cam].again || again;
	C.af_win[cam].set = true;
	pthread_mutex_unlock(&C.lock);
}

void
rwcam_core_focus_hold(int cam, bool hold)
{
	cam = focus_camera(cam);
	if (cam >= 0 && cam < RWCAM_CAMERAS) {
		pthread_mutex_lock(&C.lock);
		C.af_win[cam].hold = hold;
		pthread_mutex_unlock(&C.lock);
	}
}

void
rwcam_core_meter_window(int cam, float cx, float cy, float w, float h)
{
	if (cam < 0 || cam >= RWCAM_CAMERAS) {
		return;
	}
	pthread_mutex_lock(&C.lock);
	C.ae_win[cam].set = w > 0.01f && h > 0.01f && (w < 0.8f || h < 0.8f);
	C.ae_win[cam].cx = cx < 0 ? 0 : cx > 1 ? 1 : cx;
	C.ae_win[cam].cy = cy < 0 ? 0 : cy > 1 ? 1 : cy;
	C.ae_win[cam].w = w < 0.05f ? 0.05f : w;
	C.ae_win[cam].h = h < 0.05f ? 0.05f : h;
	C.ae_win[cam].locked = false;
	pthread_mutex_unlock(&C.lock);
}

void
rwcam_core_exposure_lock(int cam, bool locked)
{
	if (cam >= 0 && cam < RWCAM_CAMERAS) {
		pthread_mutex_lock(&C.lock);
		C.ae_win[cam].locked = locked;
		pthread_mutex_unlock(&C.lock);
	}
}

uint64_t
rwcam_core_kicks(void)
{
	pthread_mutex_lock(&C.lock);
	uint64_t k = C.kicks;

	pthread_mutex_unlock(&C.lock);
	return k;
}

/* the reader goes into a read / has come out of it (frames: 1 if it brought a frame or a header, 0 if nothing: the kernel's session ended) */
static void
read_begins(struct node *n, uint32_t want)
{
	pthread_mutex_lock(&C.lock);
	n->read_since = rwcam_now_ms();
	n->wait_said = 0;
	n->want_sensor = want;
	pthread_mutex_unlock(&C.lock);
}

static void
read_ended(struct node *n, bool frame)
{
	pthread_mutex_lock(&C.lock);
	n->read_since = 0;
	n->session_frames = frame ? n->session_frames + 1 : 0;
	pthread_mutex_unlock(&C.lock);
}

static void *
watch(void *unused)
{
	(void)unused;
	RW_SETNAME("redwood-camwatch");
	for (;;) {
		bool any = false;

		usleep(100000);
		for (int i = 0; i < NODES; i++) {
			struct node *n = &C.node[i], *o = &C.node[1 - i];

			pthread_mutex_lock(&C.lock);
			bool running = n->thread_running, kick = false;
			uint64_t waited = n->read_since != 0 ? rwcam_now_ms() - n->read_since : 0, kicks = C.kicks;
			uint32_t want = n->want_sensor, frames = n->session_frames, other = 0;
			struct redwood_camera_command ae = n->last_ae;
			bool have_ae = n->have_ae;

			any = any || running;
			/* another sensor to ask for in between: not ours, and not the one the other node runs (EBUSY) */
			for (uint32_t s = 1; s <= 3 && other == 0; s++) {
				other = s != want && !(o->thread_running && s == o->want_sensor) ? s : 0;
			}
			if (running && !C.no_kick && want != 0 && other != 0 && frames > 0 && waited >= KICK_MS && waited < 60000) {
				kick = true;
				n->session_frames = 0;          /* (not again before the new session has given a frame) */
				C.kicks++;
			}
			/*
			 * a read that gives nothing: said every 5 s, whether or not the session is started anew for it (a sensor that never gives its first
			 * frame is not: run q10hl's front camera had its session running and a black viewfinder, and nothing here would have said so)
			 */
			bool waiting = running && waited >= (n->wait_said + 1) * 5000u;

			if (waiting) {
				n->wait_said = waited / 5000u;
			}
			pthread_mutex_unlock(&C.lock);
			if (waiting) {
				rwcam_say_keep("core: node %d: NO FRAME from the kernel for %llu ms (sensor %u asked for; %u frames of this kernel session before; %llu "
				    "sessions started anew so far)", i, (unsigned long long)waited, want, frames, (unsigned long long)kicks);
			}
			if (!kick) {
				continue;
			}
			/* a descriptor of its own: the reader is inside read() on its one */
			int fd = open_node(i);
			struct redwood_camera_command c;
			bool ok = false;

			if (fd >= 0) {
				memset(&c, 0, sizeof(c));
				c.command = REDWOOD_CAMERA_SENSOR;
				c.a = other;
				c.b = ~0u;
				ok = C.ops.write_dev(fd, &c, sizeof(c)) == (ssize_t)sizeof(c);
				usleep(KICK_PAUSE_US);
				c.a = want;
				ok = C.ops.write_dev(fd, &c, sizeof(c)) == (ssize_t)sizeof(c) && ok;
				if (have_ae) {
					(void)C.ops.write_dev(fd, &ae, sizeof(ae));     /* (taken by the new session's first look) */
				}
				C.ops.close_dev(fd);
			}
			if (kicks < SAY_KICKS) {
				rwcam_say("core: node %d: no frame for %llu ms after %u of this kernel session: session started anew (#%llu, SENSOR %u then %u%s)%s",
				    i, (unsigned long long)waited, frames, (unsigned long long)kicks + 1, other, want, have_ae ? ", exposure given again" : "",
				    ok ? "" : " -- NOT TAKEN");
			}
		}
		if (!any) {
			pthread_mutex_lock(&C.lock);
			if (!C.node[0].thread_running && !C.node[1].thread_running) {
				C.watch_running = false;
				pthread_mutex_unlock(&C.lock);
				break;
			}
			pthread_mutex_unlock(&C.lock);
		}
	}
	return NULL;
}

/* what is known of a camera's frames, for whoever asks (rwcam_core_status); stable: AE has asked for nothing for some frames */
static void
status_of(int cam, const struct redwood_camera_frame *f, double green, bool ae_asked, bool whole)
{
	pthread_mutex_lock(&C.lock);
	struct rwcam_status *st = &C.status[cam];
	double white = 1023.0 - (double)f->black;

	st->frames++;
	st->quiet = ae_asked ? 0 : st->quiet < 1000 ? st->quiet + 1 : st->quiet;
	st->ae_stable = st->quiet >= 3;
	st->sequence = f->sequence;
	st->time = f->time;
	st->exposure_ns = (uint64_t)f->exposure_lines * f->line_ns;
	st->gain = f->gain_unity ? (float)f->gain / (float)f->gain_unity : 1;
	st->digital = C.isp[cam].digital;
	st->level = white > 0 ? (float)(green * C.isp[cam].digital / white) : 0;
	st->target = 0.15f;
	st->fps = f->frame_lines != 0 && f->line_ns != 0 ? (float)(1e9 / ((double)f->frame_lines * f->line_ns)) : 0;
	st->focus = f->focus;
	st->focus_max = f->focus_max;
	st->focus_idle = C.af[cam].state == 0;
	st->whole = whole;
	st->wb[0] = C.isp[cam].wb[0];
	st->wb[1] = C.isp[cam].wb[1];
	st->wb[2] = C.isp[cam].wb[2];
	pthread_mutex_unlock(&C.lock);
}

/* a frame of a hidden camera, its header only: the exposure by the header's levels, the white balance the shown camera's */
static void
hidden_frame(struct node *n, int fd, int cam, const struct redwood_camera_frame *f)
{
	struct isp_stats st;
	struct redwood_camera_command c;
	double white = 1023.0 - (double)f->black;
	bool asked = false, wb;
	float pair_wb[3];

	(void)__atomic_add_fetch(&C.headers[cam], 1, __ATOMIC_RELAXED);
	pthread_mutex_lock(&C.lock);
	wb = C.pair_wb_valid;
	pair_wb[0] = C.pair_wb[0];
	pair_wb[2] = C.pair_wb[2];
	C.isp[cam].hidden_aim = C.pair_aim >= 0.5f ? C.pair_aim : 1;
	pthread_mutex_unlock(&C.lock);
	if (wb) {
		C.isp[cam].wb[0] = pair_wb[0];
		C.isp[cam].wb[2] = pair_wb[2];
	}
	if (f->stat_samples == 0) {
		return;
	}
	memset(&st, 0, sizeof(st));
	st.cells = f->stat_samples;
	st.mean[0] = f->stat_red;
	st.mean[1] = f->stat_green;
	st.mean[2] = f->stat_blue;
	/* (64 cells are no histogram: when their mean is already most of the way to white some are clipped; AE is told so) */
	st.bright = st.mean[1] > 0.6 * white ? white : 0;
	C.isp[cam].max_frame_lines = f->frame_lines_min * (C.even_rate ? 1 : C.frames_long);
	if (isp_ae(&C.isp[cam], f, &st, &c)) {
		(void)C.ops.write_dev(fd, &c, sizeof(c));
		pthread_mutex_lock(&C.lock);
		n->last_ae = c;
		n->have_ae = true;
		pthread_mutex_unlock(&C.lock);
		asked = c.a != f->exposure_lines || c.b != f->gain;     /* (at its limits AE asks for what there is already: that is settled) */
	}
	status_of(cam, f, st.mean[1], asked, false);
}

/*
 * The pictures on a thread of their own (camera-userland-20261112-pipe). Runs cam3..cam8: the back camera's reader did read (~4.5 ms), measure +
 * AE + focus (~12 ms) and the sink's pictures (~19.7 ms) one after the other, ~36 ms a frame for frames 33.3 ms apart, so 11 to 15 frames in 150
 * were never read and films were 27.2-27.6 a second (the front camera, ~3.5 ms of measure, fitted: 30.0). Now the reader hands the frame it has
 * measured to this thread and reads the next one into its other buffer while the pictures are made: the two halves, each well under 33 ms, overlap.
 * One frame at a time is with the pictures thread; the reader waits for it to be free before it hands it another (never slower than before).
 * The sink gets a copy of the camera's isp as it was after this frame's AE/AWB (the reader goes on with the next frame's).
 * /var/db/nord2-camera-serial: the pictures on the reader's thread, as before.
 */
struct pipe {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	pthread_t thread;
	bool running, quit, busy;               /* busy: a frame is handed over and its pictures are not done */
	int cam;
	const struct redwood_camera_frame *f;
	const uint8_t *raw;
	struct isp *isp;                       /* (the copy: on the heap, not in the file's data with the initialised locks) */
	uint64_t made, made_ms, made_max, waits, wait_ms, wait_max;     /* for the reader's summary (reset by it) */
};
static struct pipe pipes[NODES] = { { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER },
	{ .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER } };
static int pipe_serial;                         /* (set once, by pipe_once) */
static pthread_once_t pipe_once_done = PTHREAD_ONCE_INIT;

static void
pipe_once(void)
{
	pipe_serial = access("/var/db/nord2-camera-serial", F_OK) == 0;
	rwcam_say("core: a frame's pictures %s", pipe_serial ? "on the reader's thread (/var/db/nord2-camera-serial)" :
	    "on a thread of their own while the reader reads and measures the next frame");
}

static void *
pipe_worker(void *arg)
{
	struct pipe *p = arg;

	RW_SETNAME(p == &pipes[0] ? "redwood-campic" : "redwood-campic2");
	pthread_mutex_lock(&p->lock);
	for (;;) {
		while (!p->busy && !p->quit) {
			pthread_cond_wait(&p->cond, &p->lock);
		}
		if (!p->busy) {
			break;
		}
		pthread_mutex_unlock(&p->lock);
		uint64_t t0 = rwcam_now_ms();

		C.sink.frame(C.sink.context, p->cam, p->f, p->raw, p->isp);
		uint64_t t = rwcam_now_ms() - t0;

		pthread_mutex_lock(&p->lock);
		p->made++;
		p->made_ms += t;
		p->made_max = t > p->made_max ? t : p->made_max;
		p->busy = false;
		pthread_cond_broadcast(&p->cond);
	}
	pthread_mutex_unlock(&p->lock);
	return NULL;
}

/* (the reader's thread) wait until the pictures thread has no frame: its buffer is the reader's again */
static void
pipe_wait(struct pipe *p)
{
	pthread_mutex_lock(&p->lock);
	if (p->busy) {
		uint64_t t0 = rwcam_now_ms(), t;

		while (p->busy) {
			pthread_cond_wait(&p->cond, &p->lock);
		}
		t = rwcam_now_ms() - t0;
		p->waits++;
		p->wait_ms += t;
		p->wait_max = t > p->wait_max ? t : p->wait_max;
	}
	pthread_mutex_unlock(&p->lock);
}

/* (the reader's thread) this frame's pictures, on the pictures thread; false: not handed over (no thread), the caller makes them */
static bool
pipe_give(struct pipe *p, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp)
{
	pipe_wait(p);
	if (p->isp == NULL && (p->isp = malloc(sizeof(*p->isp))) == NULL) {
		return false;           /* (only the reader of this node touches p->isp outside the worker, and the worker only while busy) */
	}
	pthread_mutex_lock(&p->lock);
	if (!p->running) {
		p->quit = false;
		if (pthread_create(&p->thread, NULL, pipe_worker, p) != 0) {
			pthread_mutex_unlock(&p->lock);
			return false;
		}
		p->running = true;
	}
	p->cam = cam;
	p->f = f;
	p->raw = raw;
	memcpy(p->isp, isp, sizeof(*p->isp));
	p->busy = true;
	pthread_cond_broadcast(&p->cond);
	pthread_mutex_unlock(&p->lock);
	return true;
}

/* (the reader's thread, as it ends) the last pictures made, the thread gone */
static void
pipe_end(struct pipe *p)
{
	bool join;

	pipe_wait(p);
	pthread_mutex_lock(&p->lock);
	join = p->running;
	p->quit = true;
	pthread_cond_broadcast(&p->cond);
	pthread_mutex_unlock(&p->lock);
	if (join) {
		pthread_join(p->thread, NULL);
	}
	pthread_mutex_lock(&p->lock);
	p->running = false;
	p->quit = false;
	pthread_mutex_unlock(&p->lock);
}

/* this thread is node n's reader, or its pictures thread (which runs the sink: what the sink asks must not wait for the reader that waits for it) */
static bool
on_reader(struct node *n)
{
	struct pipe *p = &pipes[n->index];
	bool pic;

	pthread_mutex_lock(&p->lock);
	pic = p->running && pthread_equal(pthread_self(), p->thread);
	pthread_mutex_unlock(&p->lock);
	return pic || pthread_equal(pthread_self(), n->thread);
}

static void *
reader(void *arg)
{
	struct node *n = arg;
	struct redwood_camera_frame head;
	uint8_t *buffer = NULL;
	size_t bytes = 0;
	bool chosen = false;                    /* this camera's sensor has been asked for on this node (before its first read) */
	int fd = -1, failures = 0, asked = -1, asked_tries = 0, busy = 0, last_cam = -1; /* asked: the sensor we have sent REDWOOD_CAMERA_SENSOR for, and how many frames of the old one we have dropped since */
	struct isp_stats st;
	uint64_t said = 0, last_done = 0, last_time_ms = 0, last_seq = 0, ae_said = 0, hidden_count = 0;
	bool was_hidden = false, gave_up = false;
	struct {
		uint64_t since, frames, skipped, read, read_max, measure, measure_max, sink, sink_max, gap_max, here_max, ae, headers;
		uint64_t gpu_stats, gpu_sharp;  /* (q10jo) frames measured by the GPU's statistics / focus sharpness from them */
		uint32_t gaps[4];               /* between the kernel's stamps of the frames read: up to 40, 70, 100 ms, more */
	} w;

	struct pipe *pp = &pipes[n->index];
	int sent_mode = -1;                     /* (q10jm) the sensor mode last given to the kernel on this descriptor (-1: none yet) */
	uint8_t *slotp[2] = { NULL, NULL };     /* (q10jn) the frame memory's two slots as last given (fm_alloc); never free()d */
	size_t hdr = sizeof(struct redwood_camera_frame);
	uint8_t *spare = NULL;                  /* the other buffer: the one the pictures thread has, or will be read into next */
	size_t spare_bytes = 0;

	memset(&w, 0, sizeof(w));
	pthread_once(&pipe_once_done, pipe_once);
	RW_SETNAME(n->index == 0 ? "redwood-camera" : "redwood-camera2");
	read_ended(n, false);
	for (;;) {
		pthread_mutex_lock(&C.lock);
		int cam = n->owner;
		bool stop = C.stop_all, fresh = cam >= 0 && !C.isp_ready[cam], hidden = cam >= 0 && C.pair && C.shown != cam, pair = C.pair;
		unsigned slave_every = C.slave_every;
		bool warm = C.warm && rwcam_now_ms() < C.warm_until;

		if (fresh) {
			C.isp_ready[cam] = true;
		}
		pthread_mutex_unlock(&C.lock);
		if (cam < 0 || stop) {
			break;
		}
		if (cam != last_cam) {
			/* (another camera on this node: its frames are another size; the last camera's pictures done first) */
			pipe_wait(pp);
			fm_free(buffer, slotp);
			buffer = NULL;
			last_cam = cam;
			sent_mode = -1;
			hidden_count = 0;
			chosen = false;
		}
		if (fd < 0) {
			fd = open_node(n->index);
			if (fd < 0) {
				rwcam_say("core: cannot open %s (%s)", n->index == 0 ? REDWOOD_CAMERA_DEVICE : REDWOOD_CAMERA_DEVICE2, strerror(errno));
				gave_up = true;
				if (C.sink.stopped) {
					C.sink.stopped(C.sink.context, cam, "open failed");
				}
				break;
			}
			asked = -1;
			chosen = false;
			sent_mode = -1;
		}
		uint32_t want = rwcam_sensor_of[cam];

		/*
		 * The camera's sensor is asked for BEFORE the first read of it on this node (run q10ho: at the change to the front camera the kernel
		 * started the node's own sensor, the IMX766, for the one frame whose header said so). A read starts whatever sensor the node has; the
		 * command first, and the read starts the one wanted. Refused (the other node has that sensor still, or anything else): as before, the
		 * header of the first read says which sensor runs and the command is given then.
		 */
		if (!chosen) {
			struct redwood_camera_command c;

			chosen = true;
			memset(&c, 0, sizeof(c));
			c.command = REDWOOD_CAMERA_SENSOR;
			c.a = want;
			c.b = ~0u;
			if (C.ops.write_dev(fd, &c, sizeof(c)) == (ssize_t)sizeof(c)) {
				asked = (int)want;
				asked_tries = 0;
				pthread_mutex_lock(&C.lock);
				n->have_ae = false;
				n->session_frames = 0;
				pthread_mutex_unlock(&C.lock);
			}
		}

		if (fresh) {
			isp_init(&C.isp[cam]);      /* (only this thread uses an isp, and only of the camera that streams) */
			if (C.flicker_set) {
				C.isp[cam].flicker_ns = C.flicker_ns;
			}
			memset(&C.af[cam], 0, sizeof(C.af[cam]));
			{
				/* the lenses' falloff, by eye of what such lenses do (no calibration of this phone's): the corners' gain less 1 */
				static const float falloff[RWCAM_CAMERAS] = { [RWCAM_BACK_WIDE] = 0.25f, [RWCAM_BACK_ULTRAWIDE] = 0.40f, [RWCAM_FRONT] = 0.25f };

				C.isp[cam].wb_free = C.wb_free;
				C.isp[cam].falloff = C.no_falloff ? 0 : falloff[cam];
				C.isp[cam].wb[0] = C.isp[cam].wb_day[0];        /* (the first pictures: daylight's gains, not none) */
				C.isp[cam].wb[2] = C.isp[cam].wb_day[1];
				if (C.last[cam].have && !C.cold) {
					struct redwood_camera_command c;

					memset(&c, 0, sizeof(c));
					c.command = REDWOOD_CAMERA_EXPOSURE;
					c.a = C.last[cam].lines;
					c.b = C.last[cam].gain;
					c.c = C.last[cam].frame_lines;
					memcpy(C.isp[cam].wb, C.last[cam].wb, sizeof(C.isp[cam].wb));
					C.isp[cam].digital = C.last[cam].digital;
					rwcam_say("core: camera %d starts as it was last: %u lines, gain %u, frames of %u lines (%s)", cam, (unsigned)c.a,
					    (unsigned)c.b, (unsigned)c.c, C.ops.write_dev(fd, &c, sizeof(c)) == (ssize_t)sizeof(c) ? "asked" : "REFUSED: from the sensor's own");
				}
			}
			pthread_mutex_lock(&C.lock);
			memset(&C.status[cam], 0, sizeof(C.status[cam]));
			pthread_mutex_unlock(&C.lock);
		}
		if (was_hidden != hidden && said >= SAY_FRAMES) {
			said = SAY_FRAMES - 1;          /* (the first frame after the change is said, with its times) */
		}
		was_hidden = hidden;
		/*
		 * A hidden camera: its header only, but for every slave_every-th frame when its pictures are asked for too, and every
		 * HIDDEN_FULL_EVERY-th when the kernel's headers have no levels (then the exposure needs the frame itself).
		 */
		bool align = hidden && pair && buffer != NULL && hidden_count > 20 && !__atomic_load_n(&A.manual, __ATOMIC_RELAXED) &&
		    !C.no_align && rwcam_now_ms() >= __atomic_load_n(&A.next_ms, __ATOMIC_RELAXED);
		bool whole_hidden = hidden && buffer != NULL && ((slave_every != 0 && hidden_count % slave_every == 0) ||
		    (head.stat_samples == 0 && hidden_count % HIDDEN_FULL_EVERY == 0) || align || (warm && hidden_count % 3 == 0));     /* (warm: a third of its frames keep it exposed and focused) */

		if (buffer == NULL || (hidden && !whole_hidden)) {
			/* a read of the header's size learns the frame's size (the sensor's own, which differs from one camera to the next) */
			read_begins(n, want);
			ssize_t got = C.ops.read_dev(fd, &head, sizeof(head));
			int head_errno = got < 0 ? errno : 0;

			read_ended(n, got == (ssize_t)sizeof(head));
			if (got != (ssize_t)sizeof(head)) {
				if (got < 0 && head_errno == EIO) {
					rwcam_say("core: the kernel's camera has had a fault: no frames");
					gave_up = true;
					if (C.sink.stopped) {
						C.sink.stopped(C.sink.context, cam, "EIO");
					}
					break;
				}
				if (got < 0 && head_errno == EBUSY) {
					usleep(50000);          /* (the other node has this node's sensor still: it comes back at once) */
				}
				if (++failures >= FAIL_LIMIT) {
					rwcam_say("core: node %d: %d reads without a frame (%s): giving up", n->index, failures, got < 0 ? strerror(head_errno) : "short");
					gave_up = true;
					if (C.sink.stopped) {
						C.sink.stopped(C.sink.context, cam, "no frames");
					}
					break;
				}
				continue;
			}
			failures = 0;
			if (head.sensor == want) {
				asked = -1;
			} else if (asked == (int)want && asked_tries < 50 && head.stride != 0 && head.height != 0 && head.stride <= (1u << 20) &&
			    head.height <= 16384) {
				/* we have asked already and the old sensor's queue is not empty yet: drop its frame, do not power-cycle again */
				size_t n_old = (size_t)head.header_bytes + (size_t)head.stride * head.height;
				uint8_t *scratch = malloc(n_old);

				asked_tries++;
				if (scratch != NULL) {
					(void)C.ops.read_dev(fd, scratch, n_old);
					free(scratch);
				}
				continue;
			}
			if (head.sensor != want) {
				/* the other camera is the one running: ask for ours (the frames stop, the next read starts it) */
				struct redwood_camera_command c;

				memset(&c, 0, sizeof(c));
				c.command = REDWOOD_CAMERA_SENSOR;
				c.a = want;
				c.b = ~0u;
				if (busy == 0) {
					rwcam_say("core: node %d: sensor %u is streaming, camera %d wants %u: SENSOR", n->index, head.sensor, cam, want);
				}
				if (C.ops.write_dev(fd, &c, sizeof(c)) != (ssize_t)sizeof(c)) {
					int e = errno;

					if (e == EBUSY && ++busy < BUSY_TRIES) {
						usleep(50000);  /* the other node has not let that sensor go yet */
						continue;
					}
					rwcam_say("core: node %d: REDWOOD_CAMERA_SENSOR %u refused (%s)", n->index, want, strerror(e));
					gave_up = true;
					if (C.sink.stopped) {
						C.sink.stopped(C.sink.context, cam, e == EBUSY ? "SENSOR busy" : "SENSOR refused");
					}
					break;
				}
				busy = 0;
				__atomic_add_fetch(&C.switches, 1, __ATOMIC_RELAXED);
				asked = (int)want;
				pthread_mutex_lock(&C.lock);
				n->have_ae = false;     /* (the other camera's exposure is not this one's) */
				n->session_frames = 0;
				pthread_mutex_unlock(&C.lock);
				asked_tries = 0;
				fm_free(buffer, slotp);
				buffer = NULL;
				continue;
			}
			if (head.magic[0] != 'R' || memcmp(head.magic, REDWOOD_CAMERA_MAGIC, 8) != 0 || head.header_bytes < sizeof(head) ||
			    head.stride == 0 || head.height == 0 || head.stride > (1u << 20) || head.height > 16384) {
				rwcam_say("core: a frame header that is not one (magic %.8s, header %u, %ux%u stride %u)", head.magic, head.header_bytes,
				    head.width, head.height, head.stride);
				if (++failures >= FAIL_LIMIT) {
					gave_up = true;
					break;
				}
				continue;
			}
			if (buffer == NULL || bytes != head.header_bytes + (size_t)head.stride * head.height) {
				fm_free(buffer, slotp);
				bytes = head.header_bytes + (size_t)head.stride * head.height;
				hdr = head.header_bytes;
				/* (a hidden camera's too: there when it is shown, and not touched until then; q10jn: the slot the other buffer is not in) */
				buffer = fm_alloc(n->index, spare != NULL && spare == slotp[0] ? 1 : 0, hdr, bytes, slotp);
				if (buffer == NULL) {
					gave_up = true;
					break;
				}
				rwcam_say("core: camera %d (node %d, sensor %u%s): frames of %ux%u, stride %u, bayer %u, black %u%s", cam, n->index, head.sensor,
				    hidden ? ", HIDDEN: headers only" : "", head.width, head.height, head.stride, head.bayer, head.black,
				    head.stat_samples ? ", levels in the header" : "");
				if (!hidden) {
					continue;
				}
			}
			if (hidden) {
				hidden_count++;
				hidden_frame(n, fd, cam, &head);
				w.headers++;
			}
			continue;
		}
		/* (q10jm) the main camera's sensor mode, when it is not the one the kernel was last given on this descriptor */
		if (cam == RWCAM_BACK_WIDE && !hidden) {
			pthread_mutex_lock(&C.lock);
			unsigned wm = C.want_mode;
			bool no_modes = C.no_modes;
			pthread_mutex_unlock(&C.lock);
			if (!no_modes && (int)wm != sent_mode) {
				struct redwood_camera_command mc;

				memset(&mc, 0, sizeof(mc));
				mc.command = REDWOOD_CAMERA_MODE;
				mc.a = wm;
				if (C.ops.write_dev(fd, &mc, sizeof(mc)) == (ssize_t)sizeof(mc)) {
					rwcam_say("core: node %d: sensor mode %u given to the kernel", n->index, wm);
					sent_mode = (int)wm;
					if (pipe_serial == 0) {
						pipe_wait(pp);  /* (the frames change size: the buffers are learned again) */
					}
					fm_free(buffer, slotp);
					buffer = NULL;
					continue;
				}
				rwcam_say("core: node %d: REDWOOD_CAMERA_MODE %u refused (%s): this kernel has no sensor modes", n->index, wm, strerror(errno));
				pthread_mutex_lock(&C.lock);
				C.no_modes = true;
				pthread_mutex_unlock(&C.lock);
			}
		}
		uint64_t t_read = rwcam_now_ms();

		read_begins(n, want);
		ssize_t got = C.ops.read_dev(fd, buffer, bytes);
		int read_errno = got < 0 ? errno : 0;
		uint64_t t_got = rwcam_now_ms();

		read_ended(n, got == (ssize_t)bytes);
		if (got != (ssize_t)bytes) {
			if (got <= 0 && read_errno != EIO) {
				/* the kernel's session ended without a frame (its 3 s): the next read starts one, with the sensor's first exposure: AE's again */
				struct redwood_camera_command again;

				if (said < 4 * SAY_FRAMES) {
					said++;
					rwcam_say("core: node %d: a read of %llu ms gave no frame (%s): the kernel starts its session anew", n->index,
					    (unsigned long long)(t_got - t_read), got == 0 ? "0 bytes" : strerror(read_errno));
				}
				if (isp_ae_again(&C.isp[cam], &again)) {
					(void)C.ops.write_dev(fd, &again, sizeof(again));
				}
				if (got < 0 && read_errno == EBUSY) {
					usleep(50000);
				}
			}
			if (got < 0 && read_errno == EIO) {
				rwcam_say("core: the kernel's camera has had a fault: no more frames");
				gave_up = true;
				if (C.sink.stopped) {
					C.sink.stopped(C.sink.context, cam, "EIO");
				}
				break;
			}
			if (got > 0 || ++failures >= FAIL_LIMIT) {
				if (got > 0) {
					fm_free(buffer, slotp);
					buffer = NULL;      /* (a different size than we learned: learn it again) */
					continue;
				}
				rwcam_say("core: node %d: %d reads without a frame: giving up", n->index, failures);
				gave_up = true;
				if (C.sink.stopped) {
					C.sink.stopped(C.sink.context, cam, "no frames");
				}
				break;
			}
			continue;
		}
		failures = 0;

		const struct redwood_camera_frame *f = (const void *)buffer;
		const uint8_t *raw = buffer + f->header_bytes;

		if (memcmp(f->magic, REDWOOD_CAMERA_MAGIC, 8) != 0 || f->sensor != want || f->header_bytes + (size_t)f->stride * f->height != bytes) {
			/* a frame of the camera we were on, still in the kernel's queue, or of another size: not ours; learn again */
			fm_free(buffer, slotp);
			buffer = NULL;
			continue;
		}
		asked = -1;
		if (hidden) {
			hidden_count++;
		}
		/* (q10jo) the GPU's numbers of a frame or two before when its pictures make them, else the processor's measure */
		bool gpu_st = !hidden && gs_take(cam, f, &st);

		if (gpu_st) {
			w.gpu_stats++;
		} else {
			isp_measure(f, raw, &st);
		}
		if (!hidden && C.isp[cam].steady) {
			C.last[cam].have = true;
			C.last[cam].lines = f->exposure_lines;
			C.last[cam].gain = f->gain;
			C.last[cam].frame_lines = f->frame_lines;
			memcpy(C.last[cam].wb, C.isp[cam].wb, sizeof(C.last[cam].wb));
			C.last[cam].digital = C.isp[cam].digital;
		}
		if (pair && (cam == RWCAM_BACK_WIDE || cam == RWCAM_BACK_ULTRAWIDE)) {
			if (hidden && align) {
				align_now(cam, f, raw);
			} else if (__atomic_load_n(&A.want[cam == RWCAM_BACK_ULTRAWIDE], __ATOMIC_RELAXED)) {
				align_thumb(cam, f, raw, rwcam_now_ms());
			}
		}
		if (hidden) {
			/* (a hidden camera read whole: its exposure by the frame, its white balance still the shown camera's) */
			pthread_mutex_lock(&C.lock);
			if (C.pair_wb_valid) {
				C.isp[cam].wb[0] = C.pair_wb[0];
				C.isp[cam].wb[2] = C.pair_wb[2];
			}
			pthread_mutex_unlock(&C.lock);
		} else {
			isp_awb(&C.isp[cam], &st);
			if (pair) {
				pthread_mutex_lock(&C.lock);
				C.pair_aim = C.isp[cam].aim;
				C.pair_wb[0] = C.isp[cam].wb[0];
				C.pair_wb[1] = C.isp[cam].wb[1];
				C.pair_wb[2] = C.isp[cam].wb[2];
				C.pair_wb_valid = true;
				pthread_mutex_unlock(&C.lock);
			}
		}

		struct redwood_camera_command c;
		C.isp[cam].max_frame_lines = f->frame_lines_min * (C.even_rate ? 1 : C.frames_long);
		/* the exposure: by the whole frame, by the window of a tap (2 parts of it to 1 of the whole frame: the rest does not go black or
		 * white), or left as it is (locked) */
		struct isp_stats spot;
		const struct isp_stats *by = &st;
		bool ae_locked, ae_spot;
		float ax, ay, aw, ah;

		pthread_mutex_lock(&C.lock);
		ae_locked = C.ae_win[cam].locked;
		ae_spot = C.ae_win[cam].set;
		ax = C.ae_win[cam].cx; ay = C.ae_win[cam].cy; aw = C.ae_win[cam].w; ah = C.ae_win[cam].h;
		pthread_mutex_unlock(&C.lock);
		if (ae_spot && !ae_locked) {
			uint32_t pw = (uint32_t)(aw * (float)f->width), ph = (uint32_t)(ah * (float)f->height);
			uint32_t px = (uint32_t)(ax * (float)f->width), py = (uint32_t)(ay * (float)f->height);

			isp_measure_window(f, raw, px > pw / 2 ? px - pw / 2 : 0, py > ph / 2 ? py - ph / 2 : 0, pw, ph, &spot);
			if (spot.cells >= 16) {
				spot.mean[1] = (2 * spot.mean[1] + st.mean[1]) / 3;
				by = &spot;
			}
		}
		bool ae = !ae_locked && isp_ae(&C.isp[cam], f, by, &c);

		if (ae) {
			(void)C.ops.write_dev(fd, &c, sizeof(c));
			pthread_mutex_lock(&C.lock);
			n->last_ae = c;
			n->have_ae = true;
			pthread_mutex_unlock(&C.lock);
			w.ae++;
			if (ae_said < 12) {
				ae_said++;
				rwcam_say("core: AE asks %u lines, gain %.2fx, frame %u lines (frame %llu had %u lines, %.2fx, green %.0f; digital %.2fx)", c.a,
				    f->gain_unity ? (double)c.b / f->gain_unity : 0.0, c.c, (unsigned long long)f->sequence, f->exposure_lines,
				    f->gain_unity ? (double)f->gain / f->gain_unity : 0.0, st.mean[1], C.isp[cam].digital);
			}
		}
		/* autofocus, where the kernel moves the lens (not of a hidden camera: nobody sees what it would be sharp on) */
		if (f->focus_max != 0 && !hidden) {
			float cx = 0.5f, cy = 0.5f, ww = 0.25f, wh = 0.25f;
			bool again;
			uint32_t move = 0, state = C.af[cam].state;

			pthread_mutex_lock(&C.lock);
			if (C.af_win[cam].set) {
				cx = C.af_win[cam].cx; cy = C.af_win[cam].cy; ww = C.af_win[cam].w; wh = C.af_win[cam].h;
			}
			again = C.af_win[cam].again;
			C.af_win[cam].again = false;
			C.af[cam].hold = C.af_win[cam].hold;
			pthread_mutex_unlock(&C.lock);
			if (again) {
				isp_af_again(&C.af[cam]);
			}
			uint32_t pw = (uint32_t)(ww * (float)f->width), ph = (uint32_t)(wh * (float)f->height);
			uint32_t px = (uint32_t)(cx * (float)f->width), py = (uint32_t)(cy * (float)f->height);
			/* (every frame's sharpness now: the watching needs it, and the look over the range takes it while the lens moves) */
			double sharp = gpu_st ? gs_sharpness(cam, f, px > pw / 2 ? px - pw / 2 : 0, py > ph / 2 ? py - ph / 2 : 0, pw, ph) : -1;

			if (sharp < 0) {
				sharp = isp_sharpness(f, raw, px > pw / 2 ? px - pw / 2 : 0, py > ph / 2 ? py - ph / 2 : 0, pw, ph);
			} else {
				w.gpu_sharp++;
			}
			bool moved;

			if (!C.af[cam].pos_known) {
				C.af[cam].pos = f->focus;       /* (where the kernel has the lens: the search starts from there) */
			}
			C.af[cam].seen = f->focus;
			/* (the scene's own brightness, whatever the exposure: for the focus to notice that the camera looks at something else) */
			C.af[cam].light = f->exposure_lines != 0 && f->gain != 0 ? st.mean[1] * f->gain_unity / ((double)f->exposure_lines * f->gain) : 0;
			moved = isp_af(&C.af[cam], sharp, f->focus_max, &move);
			if (moved) {
				struct redwood_camera_command fc;

				memset(&fc, 0, sizeof(fc));
				fc.command = REDWOOD_CAMERA_FOCUS;
				fc.a = move;
				(void)C.ops.write_dev(fd, &fc, sizeof(fc));
			}
			if (state == 0 && C.af[cam].state != 0 && (C.af[cam].scans < 10 || C.af[cam].scans % 20 == 0)) {
				rwcam_say("core: focus: camera %d looks for the sharpest position from %u of %u (search %u%s; window %.2f,%.2f %.2fx%.2f; sharpness "
				    "%.2f, was left with %.2f)", cam, C.af[cam].origin, f->focus_max, C.af[cam].scans + 1, C.af[cam].tapped ? ", a tap" : "",
				    (double)cx, (double)cy, (double)ww, (double)wh, sharp, C.af[cam].held);
			} else if (state != 5 && C.af[cam].state == 5) {
				rwcam_say("core: focus: camera %d: a small step either way tells nothing and the picture is far from sharp: over the range", cam);
			} else if (state != 0 && C.af[cam].state == 0 && (C.af[cam].scans <= 10 || C.af[cam].scans % 20 == 0)) {
				rwcam_say("core: focus: camera %d: lens at %u of %u (the frame says %u; it was at %u), sharpness %.2f (best of the search %.2f)", cam,
				    C.af[cam].pos, f->focus_max, f->focus, C.af[cam].origin, sharp, C.af[cam].best);
			}
		}
		status_of(cam, f, st.mean[1], ae && (c.a != f->exposure_lines || c.b != f->gain), true);
		uint64_t t_measured = rwcam_now_ms();

		(void)__atomic_add_fetch(&C.frames[cam], 1, __ATOMIC_RELAXED);

		/* (a hidden camera's frame read whole for its levels only is nobody's picture) */
		bool given = false;

		if (C.sink.frame && (!hidden || (slave_every != 0 && (hidden_count - 1) % slave_every == 0))) {
			/* (handed over: f and raw are in `buffer`, which the reader reads into again only after the swap below and the next hand-over's wait) */
			given = pipe_serial == 0 && pipe_give(pp, cam, f, raw, &C.isp[cam]);
			if (!given) {
				C.sink.frame(C.sink.context, cam, f, raw, &C.isp[cam]);
			}
		}
		uint64_t t_done = rwcam_now_ms(), time_ms = units_ms(f->time);

		/*
		 * The frame's times, for the first frames of a reader and now and then: how far apart the kernel's frames are (its stamps and numbers: a
		 * sensor at 30 a second gives 33 ms and +1), how long the read took and how old the frame was when the read returned (a read that waited
		 * for the frame: the kernel's copy; one that found it ready: plus the wait in the kernel's queue), and what was done with it here.
		 */
		if (said < SAY_FRAMES) {
			said++;
			rwcam_say("core: camera %d frame %llu (+%llu, %lld ms after the last by the kernel's stamp, %llu ms after it here): read %llu ms, frame %lld ms old "
			    "then; measure+AE %llu ms, pictures %llu ms; %.1f ms x %.1f (digital %.2f), green %.0f%s", cam, (unsigned long long)f->sequence,
			    (unsigned long long)(last_seq ? f->sequence - last_seq : 0), last_time_ms ? (long long)(time_ms - last_time_ms) : 0LL,
			    (unsigned long long)(last_done ? t_done - last_done : 0), (unsigned long long)(t_got - t_read),
			    f->time ? (long long)(t_got - time_ms) : -1LL, (unsigned long long)(t_measured - t_got), (unsigned long long)(t_done - t_measured),
			    f->exposure_lines * (double)f->line_ns / 1e6, f->gain_unity ? (double)f->gain / f->gain_unity : 0.0, C.isp[cam].digital, st.mean[1],
			    hidden ? " (hidden camera, read whole)" : pair ? " (the pair's shown camera)" : "");
		}
		/* the summary: what the kernel gave and what was read of it, where the time went (mean/longest), how even the frames are */
		{
			uint64_t gap = last_time_ms ? time_ms - last_time_ms : 0, here = last_done ? t_done - last_done : 0;

			if (w.since == 0) {
				w.since = t_read;
			}
			w.frames++;
			w.skipped += last_seq && f->sequence > last_seq ? f->sequence - last_seq - 1 : 0;
			w.read += t_got - t_read;
			w.read_max = t_got - t_read > w.read_max ? t_got - t_read : w.read_max;
			w.measure += t_measured - t_got;
			w.measure_max = t_measured - t_got > w.measure_max ? t_measured - t_got : w.measure_max;
			w.sink += t_done - t_measured;
			w.sink_max = t_done - t_measured > w.sink_max ? t_done - t_measured : w.sink_max;
			w.gap_max = gap > w.gap_max ? gap : w.gap_max;
			w.here_max = here > w.here_max ? here : w.here_max;
			if (last_time_ms) {
				w.gaps[gap <= 40 ? 0 : gap <= 70 ? 1 : gap <= 100 ? 2 : 3]++;
			}
			if (t_done - w.since >= SAY_MS) {
				double secs = (double)(t_done - w.since) / 1000.0;
				uint64_t p_made, p_ms, p_max, p_waits, p_wait_ms, p_wait_max;

				pthread_mutex_lock(&pp->lock);
				p_made = pp->made; p_ms = pp->made_ms; p_max = pp->made_max;
				p_waits = pp->waits; p_wait_ms = pp->wait_ms; p_wait_max = pp->wait_max;
				pp->made = pp->made_ms = pp->made_max = pp->waits = pp->wait_ms = pp->wait_max = 0;
				pthread_mutex_unlock(&pp->lock);
				rwcam_say_keep("core: node %d camera %d: pictures thread: %llu made, %.1f ms each (longest %llu); the reader handed over in %.1f ms "
				    "(longest %llu), waited for it %llu times (%llu ms in all, longest %llu)%s; exposure statistics by the GPU for %llu of %llu frames, "
				    "focus sharpness for %llu", n->index, cam, (unsigned long long)p_made,
				    p_made ? (double)p_ms / (double)p_made : 0.0, (unsigned long long)p_max, (double)w.sink / (double)w.frames,
				    (unsigned long long)w.sink_max, (unsigned long long)p_waits, (unsigned long long)p_wait_ms, (unsigned long long)p_wait_max,
				    pipe_serial ? " (serial: pictures on the reader's thread)" : "", (unsigned long long)w.gpu_stats, (unsigned long long)w.frames,
				    (unsigned long long)w.gpu_sharp);
				if (p_made != 0) {
					w.sink = p_ms * w.frames / p_made;      /* (the summary's "pictures": their own time, on their thread) */
					w.sink_max = p_max;
				}
				rwcam_say_keep("core: node %d camera %d: %.1f s: %llu frames read (%.1f a second) of %llu the kernel made (%.1f a second, %llu not read; %llu "
				    "headers only); a frame: read %.1f ms (longest %llu), measure+AE %.1f (%llu), pictures %.1f (%llu); between frames by the "
				    "kernel's stamp up to 40/70/100/more ms: %u/%u/%u/%u (longest %llu, here %llu); %.1f ms x %.1f x %.2f, green %.0f, AE %llu; wb "
				    "%.2f/%.2f (grey world %.2f/%.2f)", n->index, cam, secs,
				    (unsigned long long)w.frames, (double)w.frames / secs, (unsigned long long)(w.frames + w.skipped),
				    (double)(w.frames + w.skipped) / secs, (unsigned long long)w.skipped, (unsigned long long)w.headers,
				    (double)w.read / (double)w.frames,
				    (unsigned long long)w.read_max, (double)w.measure / (double)w.frames, (unsigned long long)w.measure_max,
				    (double)w.sink / (double)w.frames, (unsigned long long)w.sink_max, w.gaps[0], w.gaps[1], w.gaps[2], w.gaps[3],
				    (unsigned long long)w.gap_max, (unsigned long long)w.here_max, f->exposure_lines * (double)f->line_ns / 1e6,
				    f->gain_unity ? (double)f->gain / f->gain_unity : 0.0, C.isp[cam].digital, st.mean[1], (unsigned long long)w.ae,
				    (double)C.isp[cam].wb[0], (double)C.isp[cam].wb[2], st.fair[0] > 1 ? st.fair[1] / st.fair[0] : 0.0,
				    st.fair[2] > 1 ? st.fair[1] / st.fair[2] : 0.0);
				memset(&w, 0, sizeof(w));
			}
		}
		last_seq = f->sequence;
		last_time_ms = time_ms;
		last_done = t_done;
		if (given) {
			/* the next frame into the other buffer (the pictures thread is done with it: it was free when this one was handed over) */
			if (spare == NULL || spare_bytes != bytes) {
				fm_free(spare, slotp);
				spare = fm_alloc(n->index, buffer == slotp[0] ? 1 : 0, hdr, bytes, slotp);     /* (q10jn: not the slot the pictures thread has) */
				spare_bytes = spare != NULL ? bytes : 0;
			}
			if (spare != NULL) {
				uint8_t *t = buffer;

				buffer = spare;
				spare = t;
				spare_bytes = bytes;
			} else {
				pipe_wait(pp);          /* (no memory for a second buffer: this one again, once its pictures are made) */
			}
		}
	}
	pipe_end(pp);
	fm_free(spare, slotp);
	fm_free(buffer, slotp);
	if (fd >= 0) {
		C.ops.close_dev(fd);            /* the last close puts the sensor down */
	}
	pthread_mutex_lock(&C.lock);
	n->thread_running = false;
	n->read_since = 0;
	n->session_frames = 0;
	n->have_ae = false;
	n->owner = C.stop_all || gave_up ? -1 : n->owner;
	if (gave_up && C.pair) {
		/*
		 * A node of the pair is gone (its descriptor is closed: its sensor is free). The pair ends; node 0 goes on alone with the camera that
		 * is shown -- if it is node 0 that is gone there is nothing to go on with.
		 */
		int shown = C.shown;

		C.pair = false;
		C.pair_ends++;
		C.pair_wb_valid = false;
		C.node[1].owner = -1;
		if (n->index == 1 && shown >= 0) {
			if (C.node[0].owner != shown) {
				C.isp_ready[shown] = shown == C.node[0].owner;
			}
			C.node[0].owner = shown;
		}
		rwcam_say("core: node %d gave up: the pair of back cameras is over, %s", n->index, n->index == 1 && shown >= 0 ?
		    "node 0 goes on alone with the camera shown" : "no camera");
	}
	uint64_t kicks = C.kicks;

	pthread_mutex_unlock(&C.lock);
	rwcam_say("core: node %d: the reader is done (%llu/%llu/%llu frames, %llu/%llu/%llu headers only, %llu sessions started anew for a stall)", n->index,
	    (unsigned long long)rwcam_core_frames(0), (unsigned long long)rwcam_core_frames(1), (unsigned long long)rwcam_core_frames(2),
	    (unsigned long long)rwcam_core_headers(0), (unsigned long long)rwcam_core_headers(1), (unsigned long long)rwcam_core_headers(2),
	    (unsigned long long)kicks);
	return NULL;
}

/* lock held: node n is to have a reader */
static bool
run_node(struct node *n)
{
	if (n->thread_running) {
		return true;
	}
	n->thread_running = true;
	if (pthread_create(&n->thread, NULL, reader, n) != 0) {
		n->thread_running = false;
		return false;
	}
	pthread_detach(n->thread);
	if (!C.node[1 - n->index].thread_running) {
		C.no_kick = access("/var/db/nord2-camera-nokick", F_OK) == 0;
		/* AE keeps the sensor's frame rate (never a frame longer than the mode's own; gain, then digital gain, for the dark) unless this is there:
		 * then frames up to twice as long before the last of the gain, as until run q10hc (15 a second in room light) */
		/*
		 * In the dark the frames get up to twice as long (15 a second: the least CMCapture's own formats of this device say) before the
		 * gain goes over ISP_AE_GAIN_FIRST. /var/db/nord2-camera-10fps: three times (10 a second); /var/db/nord2-camera-30fps: never
		 * longer, gain only, as until run q10hz.
		 */
		C.frames_long = access("/var/db/nord2-camera-30fps", F_OK) == 0 ? 1 : access("/var/db/nord2-camera-10fps", F_OK) == 0 ? 3 : 2;
		C.cold = access("/var/db/nord2-camera-coldstart", F_OK) == 0;
		C.wb_free = access("/var/db/nord2-camera-awb-grayworld", F_OK) == 0;     /* the white balance of the runs up to q10hg */
		C.no_falloff = access("/var/db/nord2-camera-nofalloff", F_OK) == 0;
		C.no_align = access("/var/db/nord2-camera-noalign", F_OK) == 0;         /* the two back cameras' pictures are not compared */
		/* the mains: 50 Hz unless this says 60; /var/db/nord2-camera-noantiflicker: any exposure time, as until run q10ht */
		C.flicker_ns = access("/var/db/nord2-camera-noantiflicker", F_OK) == 0 ? 0 : access("/var/db/nord2-camera-60hz", F_OK) == 0 ? 8333333 : 10000000;
		C.flicker_set = true;
	}
	if (!C.watch_running) {
		pthread_t w;

		C.watch_running = true;
		if (pthread_create(&w, NULL, watch, NULL) == 0) {
			pthread_detach(w);
		} else {
			C.watch_running = false;
		}
	}
	return true;
}

/* the reader is detached: wait (at most ~4 s, a read in the kernel takes up to a second) until it has put the sensor down */
static void
wait_for_reader(struct node *n)
{
	for (int i = 0; i < 2000; i++) {
		pthread_mutex_lock(&C.lock);
		bool running = n->thread_running;

		pthread_mutex_unlock(&C.lock);
		if (!running) {
			return;
		}
		usleep(2000);
	}
	rwcam_say("core: node %d: the reader did not stop in 4 s", n->index);
}

/* the pair is over (lock NOT held): node 1's reader is told to go and waited for, so that its sensor is free for node 0 */
static void
end_pair(void)
{
	bool wait;

	pthread_mutex_lock(&C.lock);
	if (C.pair) {
		C.pair = false;
		C.pair_ends++;
		C.pair_wb_valid = false;
	}
	C.node[1].owner = -1;
	wait = C.node[1].thread_running && !on_reader(&C.node[1]);
	pthread_mutex_unlock(&C.lock);
	if (wait) {
		wait_for_reader(&C.node[1]);
	}
}

bool
rwcam_core_start(int cam)
{
	struct node *n = &C.node[0];

	if (cam < 0 || cam >= RWCAM_CAMERAS || !C.inited) {
		return false;
	}
	end_pair();
	pthread_mutex_lock(&C.lock);
	if (n->owner != cam) {
		C.isp_ready[cam] = false;       /* (a camera taken up again starts with a new exposure and white balance) */
	}
	n->owner = cam;
	C.stop_all = false;
	if (!run_node(n)) {
		n->owner = -1;
		pthread_mutex_unlock(&C.lock);
		return false;
	}
	pthread_mutex_unlock(&C.lock);
	return true;
}

bool
rwcam_core_pair_possible(void)
{
	int fd;

	if (!C.inited || C.ops.open_node == NULL) {
		return false;
	}
	if (C.ops.open_node == real_open_node) {
		return access(REDWOOD_CAMERA_DEVICE2, F_OK) == 0;
	}
	fd = C.ops.open_node(1);
	if (fd < 0) {
		return false;
	}
	C.ops.close_dev(fd);
	return true;
}

bool
rwcam_core_pair_start(int shown)
{
	bool ok;

	if ((shown != RWCAM_BACK_WIDE && shown != RWCAM_BACK_ULTRAWIDE) || !rwcam_core_pair_possible()) {
		return false;
	}
	pthread_mutex_lock(&C.lock);
	if (C.pair) {
		pthread_mutex_unlock(&C.lock);
		return true;
	}
	pthread_mutex_unlock(&C.lock);
	/* node 0 may be on the ultra-wide sensor (shown alone until now): it has to let it go before node 1 can have it */
	pthread_mutex_lock(&C.lock);
	bool wait0 = C.node[0].thread_running && C.node[0].owner != RWCAM_BACK_WIDE && !on_reader(&C.node[0]);

	if (wait0) {
		C.node[0].owner = -1;
	}
	pthread_mutex_unlock(&C.lock);
	if (wait0) {
		wait_for_reader(&C.node[0]);
	}
	end_pair();
	pthread_mutex_lock(&C.lock);
	C.stop_all = false;
	if (C.node[0].owner != RWCAM_BACK_WIDE) {
		C.isp_ready[RWCAM_BACK_WIDE] = false;
	}
	C.isp_ready[RWCAM_BACK_ULTRAWIDE] = false;
	C.node[0].owner = RWCAM_BACK_WIDE;
	C.node[1].owner = RWCAM_BACK_ULTRAWIDE;
	C.warm = false;
	C.shown = shown;
	C.pair = true;
	C.pair_wb_valid = false;
	ok = run_node(&C.node[0]) && run_node(&C.node[1]);
	if (!ok) {
		C.pair = false;
		C.node[1].owner = -1;
		C.node[0].owner = C.node[0].thread_running ? shown : -1;
		if (shown != RWCAM_BACK_WIDE) {
			C.isp_ready[shown] = false;
		}
	}
	pthread_mutex_unlock(&C.lock);
	rwcam_say("core: the pair of back cameras %s (shown: camera %d)", ok ? "starts: wide on node 0, ultra-wide on node 1" : "CANNOT start", shown);
	return ok;
}

bool
rwcam_core_pair_running(void)
{
	pthread_mutex_lock(&C.lock);
	bool pair = C.pair;

	pthread_mutex_unlock(&C.lock);
	return pair;
}

void
rwcam_core_pair_show(int cam)
{
	bool alone = false;

	if (cam != RWCAM_BACK_WIDE && cam != RWCAM_BACK_ULTRAWIDE) {
		return;
	}
	pthread_mutex_lock(&C.lock);
	if (C.pair) {
		if (C.shown != cam) {
			C.shown = cam;
			C.shows++;
		}
	} else if (C.node[0].owner >= 0 && C.node[0].owner != cam) {
		alone = true;                   /* (the pair is over: node 0 changes sensor, as it always did) */
	}
	pthread_mutex_unlock(&C.lock);
	if (alone) {
		(void)rwcam_core_start(cam);
	}
}

void
rwcam_core_pair_warm(bool warm)
{
	pthread_mutex_lock(&C.lock);
	C.warm = warm;
	C.warm_until = rwcam_now_ms() + RWCAM_WARM_MS;
	pthread_mutex_unlock(&C.lock);
}

void
rwcam_core_pair_slave_every(unsigned n)
{
	pthread_mutex_lock(&C.lock);
	C.slave_every = n;
	pthread_mutex_unlock(&C.lock);
}

uint64_t
rwcam_core_pair_shows(void)
{
	pthread_mutex_lock(&C.lock);
	uint64_t n = C.shows;

	pthread_mutex_unlock(&C.lock);
	return n;
}

void
rwcam_core_pair_stop(void)
{
	bool wait;

	end_pair();
	pthread_mutex_lock(&C.lock);
	C.node[0].owner = -1;
	wait = C.node[0].thread_running && !on_reader(&C.node[0]);
	pthread_mutex_unlock(&C.lock);
	if (wait) {
		wait_for_reader(&C.node[0]);
	}
}

void
rwcam_core_stop(int cam)
{
	struct node *n = &C.node[0];
	bool wait = false;

	pthread_mutex_lock(&C.lock);
	if (n->owner == cam && !C.pair) {
		n->owner = -1;                  /* (another camera that took over is not touched) */
		wait = n->thread_running && !on_reader(n);
	}
	pthread_mutex_unlock(&C.lock);
	if (wait) {
		wait_for_reader(n);
	}
}

void
rwcam_core_shutdown(void)
{
	pthread_mutex_lock(&C.lock);
	C.stop_all = true;
	C.pair = false;
	C.node[0].owner = -1;
	C.node[1].owner = -1;
	pthread_mutex_unlock(&C.lock);
	for (int i = 0; i < NODES; i++) {
		pthread_mutex_lock(&C.lock);
		bool wait = C.node[i].thread_running && !on_reader(&C.node[i]);

		pthread_mutex_unlock(&C.lock);
		if (wait) {
			wait_for_reader(&C.node[i]);
		}
	}
}
