#ifndef __APPLE__
#include <stdio.h>       /* (on the phone: the bundle's own declarations, through stdlib.h) */
#endif
#include <stdlib.h>
#include <string.h>
#include "rwcam_render.h"
#include "rwcam_log.h"
#include <pthread.h>

#define GPU_OFF_MS              5000    /* after the GPU failed: the processor alone for this long */
#define GPU_FAILURES            3       /* in a row: the processor for good */
#define SURFACE_FAILURES        30      /* buffers in a row the GPU could not write in place: this stream's buffers are not for it */
#define SAY_FIRST               3

static struct {
	struct rwa_isp_calls calls;
	bool have;
	int prefer;
	void (*apply)(size_t n, void *context, void (*work)(void *, size_t));
	uint64_t (*now_ms)(void);
	unsigned check_first;
	uint64_t check_ms;
	int nr_most;
	bool gpu_nr;            /* the library cleans the picture itself (rwa_isp_params.denoise: API version 2) */
} R = { .prefer = RWCAM_BACKEND_GPU, .check_first = RWCAM_RENDER_CHECK_FIRST, .check_ms = RWCAM_RENDER_CHECK_MS, .nr_most = -1 };

#ifdef __APPLE__
extern int access(const char *, int);
#endif

void
rwcam_render_denoise(int most)
{
	R.nr_most = most < 0 ? 0 : most > 2 ? 2 : most;
}

/* ---- noise --------------------------------------------------------------------------------------------------------- */

/*
 * A plane smoothed, 1 2 1 each way; `step' bytes from one sample to the next of its kind (2: the two colours side by side). near > 0: a
 * sample is changed only when the smoothed one is within `near' of it (not across an edge). In SMOOTH_BANDS bands of rows, on the cores
 * (run user4: one thread took 25 ms of a film's 33 with it, the ISP itself 8 on all of them): a row is smoothed from its neighbours AS THEY
 * WERE, so the rows next to a band's ends are copied before any band begins and the result is the one band's.
 */
#define SMOOTH_BANDS 8
struct smooth_job {
	uint8_t *p, *edges;             /* edges: for each band the row above its first and the row under its last, as they were */
	size_t stride;
	uint32_t bytes, rows;
	unsigned step;
	int near;
};

static void
smooth_band(void *context, size_t band)
{
	const struct smooth_job *j = context;
	uint32_t a = (uint32_t)(j->rows * band / SMOOTH_BANDS), b = (uint32_t)(j->rows * (band + 1) / SMOOTH_BANDS), bytes = j->bytes;
	uint8_t *tmp = malloc(2 * (size_t)bytes), *above = tmp, *here = tmp != NULL ? tmp + bytes : NULL;
	const uint8_t *top = j->edges + (size_t)(2 * band) * bytes, *bottom = top + bytes;

	a = a < 1 ? 1 : a;
	b = b > j->rows - 1 ? j->rows - 1 : b;
	if (tmp == NULL || a >= b) {
		free(tmp);
		return;
	}
	memcpy(above, top, bytes);
	for (uint32_t y = a; y < b; y++) {
		uint8_t *row = j->p + (size_t)y * j->stride;
		const uint8_t *below = y + 1 == b ? bottom : row + j->stride;

		memcpy(here, row, bytes);
		for (uint32_t x = j->step; x + j->step < bytes; x++) {
			int v = (above[x] + below[x] + here[x - j->step] + here[x + j->step] + 4 * here[x] + 4) >> 3;

			if (j->near <= 0 || (v - here[x] < j->near && here[x] - v < j->near)) {
				row[x] = (uint8_t)v;
			}
		}
		uint8_t *t = above;

		above = here;
		here = t;
	}
	free(tmp);
}

static void
smooth(uint8_t *p, size_t stride, uint32_t bytes, uint32_t rows, unsigned step, uint8_t *tmp, int near)
{
	struct smooth_job j = { p, NULL, stride, bytes, rows, step, near };

	(void)tmp;
	if (rows < 3 || bytes < 3 * step || (j.edges = malloc(2 * SMOOTH_BANDS * (size_t)bytes)) == NULL) {
		return;
	}
	for (uint32_t k = 0; k < SMOOTH_BANDS; k++) {
		uint32_t a = rows * k / SMOOTH_BANDS, b = rows * (k + 1) / SMOOTH_BANDS;

		a = a < 1 ? 1 : a;
		b = b > rows - 1 ? rows - 1 : b;
		memcpy(j.edges + (size_t)(2 * k) * bytes, p + (size_t)(a - 1) * stride, bytes);
		memcpy(j.edges + (size_t)(2 * k + 1) * bytes, p + (size_t)b * stride, bytes);
	}
	if (R.apply != NULL) {
		R.apply(SMOOTH_BANDS, &j, smooth_band);
	} else {
		for (size_t k = 0; k < SMOOTH_BANDS; k++) {
			smooth_band(&j, k);
		}
	}
	free(j.edges);
}

/*
 * The picture and the one before it (run q10ia: "things that move drag a trail behind them, very annoying" -- each pixel by itself was drawn
 * 60 % of the way to the picture before wherever it differed from it by less than 26, and something moving through the dark differs by less).
 * By BLOCKS now: the brightness's mean difference from the picture before of each 8 x 8 block (every other pixel of it); the noise is what
 * that is in the calmer blocks; a block that differs by more than RWCAM_NR_MOVES times the noise has something moving in it and is left as it
 * is (and its eight neighbours, and the picture before forgotten there); when over RWCAM_NR_ALL of the blocks move (the camera does), all
 * are. In the blocks that are still, each pixel goes `keep' of 256 of the way to the picture before where it differs by less than `near'.
 *   near, keep: level 1 brightness 10, 90; colours 8, 102. Level 2 brightness 18, 128; colours 14, 140. `near' is at least three times the
 *   noise (three quarters of that for the colours), 40 at most: with what moves left out by the blocks it can be that wide.
 */
#define RWCAM_NR_BLOCK  8
#define RWCAM_NR_MOVES  1.7
#define RWCAM_NR_ALL    0.35
/* with_before()'s last step, the pixels, for the blocks of some of the rows of blocks: each block by itself, on the cores */
struct before_job {
	uint8_t *y, *uv, *by, *buv;
	const uint8_t *moves;
	size_t ys, uvs;
	uint32_t width, height, bw, bh, cw, moving;
	int near_y, keep_y, near_c, keep_c;
};
#define BEFORE_PIECES 8

static void
before_piece(void *context, size_t piece)
{
	const struct before_job *j = context;
	uint8_t *y = j->y, *uv = j->uv, *by = j->by, *buv = j->buv;
	const uint8_t *moves = j->moves;
	size_t ys = j->ys, uvs = j->uvs;
	uint32_t width = j->width, height = j->height, bw = j->bw, bh = j->bh, cw = j->cw, moving = j->moving, n = bw * bh;
	int near_y = j->near_y, keep_y = j->keep_y, near_c = j->near_c, keep_c = j->keep_c;

	for (uint32_t b = (uint32_t)(bh * piece / BEFORE_PIECES) * bw; b < (uint32_t)(bh * (piece + 1) / BEFORE_PIECES) * bw; b++) {
		uint32_t bx = b % bw, byy = b / bw, x0 = bx * RWCAM_NR_BLOCK, y0 = byy * RWCAM_NR_BLOCK;
		bool still = (double)moving <= RWCAM_NR_ALL * n;

		for (int dy = -1; still && dy <= 1; dy++) {
			for (int dx = -1; still && dx <= 1; dx++) {
				int64_t nx = (int64_t)bx + dx, ny = (int64_t)byy + dy;

				still = nx < 0 || ny < 0 || nx >= bw || ny >= bh || !(moves[ny * bw + nx] & 1);
			}
		}
		for (uint32_t yy = y0; yy < y0 + RWCAM_NR_BLOCK && yy < height; yy++) {
			uint8_t *row = y + (size_t)yy * ys, *before = by + (size_t)yy * width;

			for (uint32_t x = x0; x < x0 + RWCAM_NR_BLOCK && x < width; x++) {
				int d = (int)row[x] - (int)before[x], ad = d < 0 ? -d : d;
				int v = still && ad < near_y ? (int)row[x] - ((d * keep_y) >> 8) : (int)row[x];

				row[x] = (uint8_t)v;
				before[x] = (uint8_t)v;
			}
		}
		for (uint32_t yy = y0 / 2; yy < (y0 + RWCAM_NR_BLOCK) / 2 && yy < (height + 1) / 2; yy++) {
			uint8_t *row = uv + (size_t)yy * uvs, *before = buv + (size_t)yy * cw;

			for (uint32_t x = x0; x < x0 + RWCAM_NR_BLOCK && x < cw; x++) {
				int d = (int)row[x] - (int)before[x], ad = d < 0 ? -d : d;
				int v = still && ad < near_c ? (int)row[x] - ((d * keep_c) >> 8) : (int)row[x];

				row[x] = (uint8_t)v;
				before[x] = (uint8_t)v;
			}
		}
	}
}

static void
with_before(uint8_t *y, size_t ys, uint8_t *uv, size_t uvs, uint32_t width, uint32_t height, uint8_t *by, uint8_t *buv, int level)
{
	uint32_t bw = (width + RWCAM_NR_BLOCK - 1) / RWCAM_NR_BLOCK, bh = (height + RWCAM_NR_BLOCK - 1) / RWCAM_NR_BLOCK, cw = (width + 1) & ~1u;
	uint32_t n = bw * bh, moving = 0;
	uint8_t *diff = malloc(2 * (size_t)n), *moves = diff != NULL ? diff + n : NULL;
	uint64_t all = 0, calm = 0, calm_n = 0;
	int near_y = level == 2 ? 18 : 10, keep_y = level == 2 ? 128 : 90, near_c = level == 2 ? 14 : 8, keep_c = level == 2 ? 140 : 102, limit;

	if (diff == NULL) {
		return;
	}
	for (uint32_t b = 0; b < n; b++) {
		uint32_t x0 = b % bw * RWCAM_NR_BLOCK, y0 = b / bw * RWCAM_NR_BLOCK, sum = 0, count = 0;

		for (uint32_t yy = y0; yy < y0 + RWCAM_NR_BLOCK && yy < height; yy += 2) {
			const uint8_t *row = y + (size_t)yy * ys, *before = by + (size_t)yy * width;

			for (uint32_t x = x0; x < x0 + RWCAM_NR_BLOCK && x < width; x += 2) {
				int d = (int)row[x] - (int)before[x];

				sum += (uint32_t)(d < 0 ? -d : d);
				count++;
			}
		}
		sum = count ? sum * 4 / count : 0;      /* (in quarters of a level) */
		diff[b] = (uint8_t)(sum > 255 ? 255 : sum);
		all += diff[b];
	}
	/* the noise: the mean of the blocks that differ by less than twice the mean of all */
	for (uint32_t b = 0; b < n; b++) {
		if ((uint64_t)diff[b] * n <= 2 * all) {
			calm += diff[b];
			calm_n++;
		}
	}
	limit = (int)(RWCAM_NR_MOVES * (calm_n ? (double)calm / (double)calm_n : 0) + 0.5);
	limit = limit < 10 ? 10 : limit > 48 ? 48 : limit;      /* (2.5 to 12 levels) */
	{
		/* (in a block that is still, what a pixel differs by is noise up to three times the blocks' own: at least `near') */
		int noise3 = (int)(3.0 * (calm_n ? (double)calm / (double)calm_n : 0) / 4 + 0.5);

		noise3 = noise3 > 40 ? 40 : noise3;
		near_y = noise3 > near_y ? noise3 : near_y;
		near_c = noise3 * 3 / 4 > near_c ? noise3 * 3 / 4 : near_c;
	}
	for (uint32_t b = 0; b < n; b++) {
		moves[b] = diff[b] > limit;
		moving += moves[b];
	}
	{
		struct before_job j = { y, uv, by, buv, moves, ys, uvs, width, height, bw, bh, cw, moving, near_y, keep_y, near_c, keep_c };

		if (R.apply != NULL) {
			R.apply(BEFORE_PIECES, &j, before_piece);
		} else {
			for (size_t k = 0; k < BEFORE_PIECES; k++) {
				before_piece(&j, k);
			}
		}
	}
	free(diff);
}

/* the level a frame's gain asks for (0: none) */
static int
nr_level(const struct isp *isp, const struct redwood_camera_frame *f)
{
	double gain = f->gain_unity != 0 ? (double)f->gain / f->gain_unity * (isp->digital > 1 ? isp->digital : 1) : 0;
	int level = gain >= RWCAM_NR_GAIN_2 ? 2 : gain >= RWCAM_NR_GAIN_1 ? 1 : 0;

	if (R.nr_most < 0) {
#ifdef __APPLE__
		R.nr_most = access("/var/db/nord2-camera-nodenoise", 0) == 0 ? 0 : 2;
#else
		R.nr_most = 2;
#endif
	}
	return level > R.nr_most ? R.nr_most : level;
}

static void
denoise(struct rwcam_renderer *r, const struct isp *isp, const struct redwood_camera_frame *f, struct rwcam_picture *pic)
{
	int level = nr_level(isp, f);
	uint32_t cw = (pic->width + 1) & ~1u, ch = (pic->height + 1) / 2;
	bool small = (uint64_t)pic->width * pic->height <= RWCAM_RENDER_CHECK_PIXELS;

	r->nr_pictures[level]++;
	if (level == 0 || (pic->format != RWCAM_PF_420F && pic->format != RWCAM_PF_420V)) {
		r->nr_w = 0;                    /* (the picture before is not this one's any more) */
		return;
	}
	if (!pic->mapped) {
		if (pic->map == NULL || !pic->map(pic)) {
			return;
		}
		pic->mapped = true;
	}
	if (pic->plane[0] == NULL || pic->plane[1] == NULL || pic->width < 8 || pic->height < 8) {
		return;
	}
	if (r->nr_tmp == NULL || r->nr_w != pic->width || r->nr_h != pic->height) {
		bool fresh = r->nr_y == NULL || r->nr_w != pic->width || r->nr_h != pic->height;

		if (fresh) {
			free(r->nr_y);
			free(r->nr_uv);
			free(r->nr_tmp);
			r->nr_y = small ? malloc((size_t)pic->width * pic->height) : NULL;
			r->nr_uv = small ? malloc((size_t)cw * ch) : NULL;
			r->nr_tmp = malloc(2 * (size_t)(cw > pic->width ? cw : pic->width));
			if (r->nr_tmp == NULL) {
				return;
			}
			/* (nothing before this picture: it is its own "before") */
			for (uint32_t y = 0; r->nr_y != NULL && y < pic->height; y++) {
				memcpy(r->nr_y + (size_t)y * pic->width, pic->plane[0] + (size_t)y * pic->stride[0], pic->width);
			}
			for (uint32_t y = 0; r->nr_uv != NULL && y < ch; y++) {
				memcpy(r->nr_uv + (size_t)y * cw, pic->plane[1] + (size_t)y * pic->stride[1], cw);
			}
		}
		r->nr_w = pic->width;
		r->nr_h = pic->height;
	}
	/* the colours: smoothed once, twice at level 2 */
	for (int pass = 0; pass < level; pass++) {
		smooth(pic->plane[1], pic->stride[1], cw, ch, 2, r->nr_tmp, 0);
	}
	if (level == 2) {
		smooth(pic->plane[0], pic->stride[0], pic->width, pic->height, 1, r->nr_tmp, 16);
	}
	if (small && r->nr_y != NULL && r->nr_uv != NULL) {
		with_before(pic->plane[0], pic->stride[0], pic->plane[1], pic->stride[1], pic->width, pic->height, r->nr_y, r->nr_uv, level);
	}
}

void
rwcam_render_checks(unsigned first, uint64_t every_ms)
{
	R.check_first = first;
	R.check_ms = every_ms;
}

void
rwcam_render_distrust(struct rwcam_renderer *r, const char *why)
{
	if (!r->gpu_off) {
		r->gpu_off = true;
		snprintf(r->why, sizeof(r->why), "%s", why);
		rwcam_say_keep("render: %s: THE GPU's PICTURE IS NOT ONE (%s): the processor makes this stream's pictures from now on", r->name ? r->name : "?", why);
	}
}

void
rwcam_render_setup(const struct rwa_isp_calls *calls, void (*apply)(size_t n, void *context, void (*work)(void *, size_t)), uint64_t (*now_ms)(void))
{
	memset(&R.calls, 0, sizeof(R.calls));
	R.have = false;
	if (calls != NULL && calls->api_version != NULL && calls->create != NULL && calls->destroy != NULL && calls->render != NULL &&
	    calls->wait != NULL && calls->forget_surfaces != NULL && calls->api_version() >= 1) {
		R.calls = *calls;
		R.have = true;
	}
	R.gpu_nr = R.have && R.calls.api_version() >= 2;
	R.apply = apply;
	R.now_ms = now_ms;
}

bool
rwcam_render_gpu_there(void)
{
	return R.have;
}

void
rwcam_render_prefer(int backend)
{
	R.prefer = backend == RWCAM_BACKEND_CPU ? RWCAM_BACKEND_CPU : RWCAM_BACKEND_GPU;
}

int
rwcam_render_preferred(void)
{
	return R.have ? R.prefer : RWCAM_BACKEND_CPU;
}

/* ---- the processor ------------------------------------------------------------------------------------------------- */

struct cpu_job {
	const struct isp *isp;
	const struct redwood_camera_frame *f;
	const uint8_t *raw;
	const struct rwcam_picture *pic;
	unsigned pieces;
};

static void
cpu_piece(void *context, size_t i)
{
	const struct cpu_job *j = context;
	const struct rwcam_picture *p = j->pic;
	uint32_t rows = (p->height + j->pieces - 1) / j->pieces, first = (uint32_t)i * rows, end = first + rows;

	first &= ~1u;
	end = i + 1 == j->pieces ? p->height : end & ~1u;
	if (first >= p->height) {
		return;
	}
	end = end > p->height ? p->height : end;
	if (p->format == RWCAM_PF_BGRA) {
		isp_render_bgra(j->isp, j->f, j->raw, p->plane[0], p->stride[0], p->width, p->height, first, end);
	} else {
		isp_render_nv12(j->isp, j->f, j->raw, p->plane[0], p->stride[0], p->plane[1], p->stride[1], p->width, p->height, first, end,
		    p->format == RWCAM_PF_420V);
	}
}

static bool
cpu_render(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, struct rwcam_picture *pic, unsigned pieces)
{
	struct cpu_job job = { isp, f, raw, pic, pieces == 0 ? 1 : pieces };

	if (!pic->mapped) {
		if (pic->map == NULL || !pic->map(pic)) {
			return false;
		}
		pic->mapped = true;
	}
	if (pic->plane[0] == NULL || (pic->format != RWCAM_PF_BGRA && pic->plane[1] == NULL)) {
		return false;
	}
	if (R.apply != NULL && job.pieces > 1) {
		R.apply(job.pieces, &job, cpu_piece);
	} else {
		for (unsigned i = 0; i < job.pieces; i++) {
			cpu_piece(&job, i);
		}
	}
	return true;
}

/* ---- the GPU ------------------------------------------------------------------------------------------------------- */

/* RWA_ISP_OK, or the reason (r->why) the processor has to make this picture */
/* (q10jn) the nodes' frame-memory contexts (rwcam_render_frame_memory); their calls under fm_lock (several streams' renderers may use one) */
static struct {
	bool on;
	struct rwa_isp *ctx[2];
	const uint8_t *raw[2][2];       /* where slot k's frame starts (P + header) */
	size_t frame_bytes[2][2];
	uint64_t zero_copies, uploads;
} FM;
static pthread_mutex_t fm_lock = PTHREAD_MUTEX_INITIALIZER;

void
rwcam_render_zero_copy(bool on)
{
	FM.on = on;
}

void *
rwcam_render_frame_memory(int node, unsigned slot, size_t header_bytes, size_t frame_bytes)
{
	char why[256];
	void *m = NULL;

	if (!FM.on || !R.have || R.calls.frame_memory == NULL || node < 0 || node > 1 || slot > 1) {
		return NULL;
	}
	why[0] = 0;
	pthread_mutex_lock(&fm_lock);
	if (FM.ctx[node] == NULL) {
		FM.ctx[node] = R.calls.create(why, sizeof(why));
	}
	if (FM.ctx[node] != NULL) {
		m = R.calls.frame_memory(FM.ctx[node], slot, header_bytes, frame_bytes, why, sizeof(why));
		FM.raw[node][slot] = m != NULL ? (const uint8_t *)m + header_bytes : NULL;
		FM.frame_bytes[node][slot] = m != NULL ? frame_bytes : 0;
	}
	pthread_mutex_unlock(&fm_lock);
	rwcam_say("render: node %d frame memory slot %u, %zu + %zu bytes: %s%s", node, slot, header_bytes, frame_bytes, m != NULL ? "the GPU's" :
	    "REFUSED, the reader's own (", m != NULL ? "" : why[0] ? why : "no reason given");
	return m;
}

/* (q10jn) the node context whose slot this frame lies in, or NULL */
static struct rwa_isp *
fm_context_of(const uint8_t *raw)
{
	for (int n = 0; n < 2; n++) {
		for (int k = 0; k < 2; k++) {
			if (FM.ctx[n] != NULL && FM.raw[n][k] != NULL && raw == FM.raw[n][k]) {
				return FM.ctx[n];
			}
		}
	}
	return NULL;
}

static int
gpu_render(struct rwcam_renderer *r, const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, const struct rwcam_picture *pic,
    int level)
{
	struct rwa_isp *shared = r->zero_copy && FM.on ? fm_context_of(raw) : NULL;

	struct rwa_isp_frame frame;
	struct rwa_isp_params params;
	struct rwa_isp_output output;
	struct rwa_isp_result result;
	int e;

	if (shared == NULL && r->gpu == NULL) {
		r->gpu_tried = true;
		r->why[0] = 0;
		r->gpu = R.calls.create(r->why, sizeof(r->why));
		if (r->gpu == NULL) {
			r->gpu_off = true;
			rwcam_say("render: %s: no GPU context (%s): the processor makes the pictures", r->name ? r->name : "?", r->why[0] ? r->why : "no reason given");
			return RWA_ISP_E_GPU;
		}
	}
	memset(&frame, 0, sizeof(frame));
	frame.size = sizeof(frame);
	frame.width = f->width;
	frame.height = f->height;
	frame.stride = f->stride;
	frame.format = f->format;
	frame.bayer = f->bayer;
	frame.black = f->black;
	frame.raw = raw;
	memset(&params, 0, sizeof(params));
	params.size = sizeof(params);
	params.mode = RWA_ISP_MODE_CELLS;       /* isp_render_nv12: what the processor's way here is */
	params.wb[0] = isp->wb[0];
	params.wb[1] = isp->wb[1];
	params.wb[2] = isp->wb[2];
	params.digital = isp->digital;
	for (int i = 0; i < 9; i++) {
		params.ccm[i] = isp->ccm[i / 3][i % 3];
	}
	params.falloff = isp->falloff;
	params.zoom = isp->zoom;
	params.flip = isp->flip;
	params.video_range = pic->format == RWCAM_PF_420V;
	params.lens_shading = 0;
	params.gamma = isp->gamma;
	/* the noise, by the library when it can (level 0 otherwise: denoise() then does it); the picture before for the viewfinder's sizes only */
	params.denoise = (uint32_t)level;
	params.denoise_before = (uint64_t)pic->width * pic->height <= RWCAM_RENDER_CHECK_PIXELS;
	memset(&output, 0, sizeof(output));
	output.size = sizeof(output);
	output.flags = 0;                       /* waited for in the call: the buffer goes to the graph when it returns */
	output.iosurface = pic->iosurface;
	output.width = pic->width;
	output.height = pic->height;
	memset(&result, 0, sizeof(result));
	result.size = sizeof(result);
	r->why[0] = 0;
	if (shared != NULL) {
		pthread_mutex_lock(&fm_lock);
		e = R.calls.render(shared, &frame, &params, &output, &result, r->why, sizeof(r->why));
		pthread_mutex_unlock(&fm_lock);
		FM.zero_copies += e == RWA_ISP_OK && (result.flags & RWA_ISP_DID_ZERO_COPY) != 0;
		FM.uploads += e == RWA_ISP_OK && (result.flags & RWA_ISP_DID_ZERO_COPY) == 0;
	} else {
		e = R.calls.render(r->gpu, &frame, &params, &output, &result, r->why, sizeof(r->why));
	}
	if (e == RWA_ISP_OK) {
		r->prepare_ns += result.prepare_ns;
		r->queue_ns += result.queue_ns;
		r->wait_ns += result.wait_ns;
		r->sum_prepare_ns += result.prepare_ns;
		r->sum_queue_ns += result.queue_ns;
		r->sum_wait_ns += result.wait_ns;
		r->timed++;
		r->last_flags = result.flags;
	}
	return e;
}

/*
 * The GPU's picture (in pic, just made) against the processor's of the same frame, at a grid of places: the largest difference; -1 when it cannot be
 * looked at. When it is over the limit the processor's picture is put in its place.
 */
static int
check_gpu(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, struct rwcam_picture *pic, unsigned pieces)
{
	struct rwcam_picture mine = *pic;
	size_t ys = (pic->width + 63u) & ~63u, yb = ys * pic->height, cb = ys * ((pic->height + 1) / 2);
	uint8_t *mem = malloc(yb + cb);
	int worst = 0;

	if (mem == NULL) {
		return -1;
	}
	mine.iosurface = NULL;
	mine.map = NULL;
	mine.mapped = true;
	mine.plane[0] = mem;
	mine.plane[1] = mem + yb;
	mine.stride[0] = mine.stride[1] = ys;
	if (!cpu_render(isp, f, raw, &mine, pieces) || pic->map == NULL || !pic->map(pic)) {
		free(mem);
		return -1;
	}
	pic->mapped = true;
	if (pic->plane[0] == NULL || pic->plane[1] == NULL) {
		free(mem);
		return -1;
	}
	for (uint32_t gy = 0; gy < 48; gy++) {
		uint32_t y = (uint32_t)(((uint64_t)pic->height - 1) * gy / 47);

		for (uint32_t gx = 0; gx < 64; gx++) {
			uint32_t x = (uint32_t)(((uint64_t)pic->width - 1) * gx / 63);
			int d = (int)pic->plane[0][(size_t)y * pic->stride[0] + x] - (int)mem[(size_t)y * ys + x];
			size_t c = (size_t)(y / 2) * pic->stride[1] + (x & ~1u), m = yb + (size_t)(y / 2) * ys + (x & ~1u);
			int du = (int)pic->plane[1][c] - (int)mem[m], dv = (int)pic->plane[1][c + 1] - (int)mem[m + 1];

			d = d < 0 ? -d : d;
			du = du < 0 ? -du : du;
			dv = dv < 0 ? -dv : dv;
			worst = d > worst ? d : worst;
			worst = du > worst ? du : worst;
			worst = dv > worst ? dv : worst;
		}
	}
	if (worst > RWCAM_RENDER_CHECK_LIMIT) {
		for (uint32_t y = 0; y < pic->height; y++) {
			memcpy(pic->plane[0] + (size_t)y * pic->stride[0], mem + (size_t)y * ys, pic->width);
		}
		for (uint32_t y = 0; y < (pic->height + 1) / 2; y++) {
			memcpy(pic->plane[1] + (size_t)y * pic->stride[1], mem + yb + (size_t)y * ys, (pic->width + 1) & ~1u);
		}
	}
	free(mem);
	return worst;
}

void
rwcam_render_summary(struct rwcam_renderer *r, char *text, size_t size)
{
	uint64_t g = r->sum_made[RWCAM_BACKEND_GPU];
	int n = snprintf(text, size, "GPU %llu", (unsigned long long)g);

	if (g != 0 && n > 0 && (size_t)n < size) {
		n += snprintf(text + n, size - (size_t)n, " (prepare %.2f ms, queue %.2f ms, wait %.2f ms)", (double)r->sum_prepare_ns / 1e6 / (double)g,
		    (double)r->sum_queue_ns / 1e6 / (double)g, (double)r->sum_wait_ns / 1e6 / (double)g);
	}
	if (n > 0 && (size_t)n < size) {
		n += snprintf(text + n, size - (size_t)n, ", processor %llu", (unsigned long long)r->sum_made[RWCAM_BACKEND_CPU]);
	}
	if (n > 0 && (size_t)n < size && (r->sum_fallbacks != 0 || r->gpu_off || r->gpu_unsupported)) {
		n += snprintf(text + n, size - (size_t)n, " (%llu after the GPU failed%s: %.80s)", (unsigned long long)r->sum_fallbacks,
		    r->gpu_off ? "; GPU off for this stream" : r->gpu_unsupported ? "; not the GPU's as they are" : "", r->why[0] ? r->why : "-");
	}
	if (n > 0 && (size_t)n < size && r->checked) {
		(void)snprintf(text + n, size - (size_t)n, "; GPU pictures against the processor's: %llu compared, largest difference %d, the last %d",
		    (unsigned long long)r->checks, r->check_max, r->check_last);
	}
	{
		size_t at = 0;

		while (at < size && text[at] != 0) {
			at++;
		}

		if (at < size && (r->nr_pictures[1] != 0 || r->nr_pictures[2] != 0)) {
			(void)snprintf(text + at, size - at, "; noise taken out of %llu (a little) and %llu (more) of them", (unsigned long long)r->nr_pictures[1],
			    (unsigned long long)r->nr_pictures[2]);
		}
		r->nr_pictures[0] = r->nr_pictures[1] = r->nr_pictures[2] = 0;
	}
	r->sum_made[0] = r->sum_made[1] = r->sum_fallbacks = r->sum_prepare_ns = r->sum_queue_ns = r->sum_wait_ns = 0;
}

int
rwcam_render(struct rwcam_renderer *r, const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, struct rwcam_picture *pic,
    unsigned pieces)
{
	uint64_t now = R.now_ms != NULL ? R.now_ms() : 0;
	bool gpu = R.have && R.prefer == RWCAM_BACKEND_GPU && !r->gpu_off && !r->gpu_unsupported && now >= r->gpu_off_until && pic->iosurface != NULL &&
	    (pic->format == RWCAM_PF_420F || pic->format == RWCAM_PF_420V) && !isp->lsc.on && !pic->mapped &&
	    isp->shift_x == 0 && isp->shift_y == 0 &&           /* (the window moved off the middle: not in rwa_isp_params) */
	    (f->format == REDWOOD_CAMERA_FORMAT_MIPI_RAW10 || f->format == REDWOOD_CAMERA_FORMAT_LE40_RAW10);

	if (gpu) {
		/*
		 * The GPU cleans the picture too (the library's own smooth() and with_before(): the processor is left with nothing to do), but for
		 * the pictures that are compared with the processor's: those are made without, compared, and cleaned here as before. The first of a
		 * stream are compared whatever the light; the ones every RWCAM_RENDER_CHECK_MS only while there is no noise to clean (a picture
		 * taken out of the GPU's run would show every two seconds).
		 */
		bool large = (uint64_t)pic->width * pic->height > RWCAM_RENDER_CHECK_PIXELS;
		uint64_t every = large ? R.check_ms * 10 : R.check_ms;
		int level = R.gpu_nr ? nr_level(isp, f) : 0;
		bool first = r->checks < (large ? (R.check_first != 0) : R.check_first);
		bool check = first || (every != 0 && r->checks != 0 && now - r->check_at >= every && level == 0);
		int e = gpu_render(r, isp, f, raw, pic, check ? 0 : level);

		if (e == RWA_ISP_OK) {
			r->gpu_failures = r->surface_failures = 0;
			r->sum_made[RWCAM_BACKEND_GPU]++;
			if (check) {
				int d = check_gpu(isp, f, raw, pic, pieces);

				r->checked = true;
				r->checks++;
				r->check_at = now;
				r->check_last = d;
				r->check_max = d < 0 || d > r->check_max || r->checks == 1 ? d : r->check_max;
				if (d < 0 || d > RWCAM_RENDER_CHECK_LIMIT) {
					r->gpu_off = true;
					if (d < 0) {
						/* (nothing says the GPU's picture is one: the processor's, if the buffer can be had, is given) */
						r->check_max = RWCAM_RENDER_CHECK_LIMIT + 1;
						snprintf(r->why, sizeof(r->why), "its picture %llu could not be compared with the processor's",
						    (unsigned long long)r->made[RWCAM_BACKEND_GPU] + 1);
					} else {
						snprintf(r->why, sizeof(r->why), "its picture %llu was up to %d of 255 off the processor's",
						    (unsigned long long)r->made[RWCAM_BACKEND_GPU] + 1, d);
					}
					rwcam_say_keep("render: %s: THE GPU's PICTURE IS NOT THE PROCESSOR's (%s; comparison %llu, at 64x48 places): the processor makes "
					    "this stream's pictures from now on", r->name ? r->name : "?", r->why, (unsigned long long)r->checks);
					r->made[RWCAM_BACKEND_GPU]++;
					r->sum_made[RWCAM_BACKEND_GPU]--;
					if (d < 0 && !cpu_render(isp, f, raw, pic, pieces)) {
						return -1;
					}
					r->made[RWCAM_BACKEND_CPU]++;
					r->sum_made[RWCAM_BACKEND_CPU]++;
					return RWCAM_BACKEND_CPU;
				}
				if (r->checks <= R.check_first) {
					rwcam_say("render: %s: the GPU's picture %llu against the processor's of the same frame, at 64x48 places: largest difference %d "
					    "of 255", r->name ? r->name : "?", (unsigned long long)r->made[RWCAM_BACKEND_GPU] + 1, d);
				}
			}
			if (r->made[RWCAM_BACKEND_GPU]++ < SAY_FIRST) {
				rwcam_say("render: %s: picture %ux%u by the GPU (%s; prepare %.2f ms, queue %.2f ms, wait %.2f ms)", r->name ? r->name : "?",
				    pic->width, pic->height, r->last_flags & RWA_ISP_DID_ZERO_COPY ? "the frame read where it lay" : "the frame uploaded",
				    (double)r->prepare_ns / 1e6 / (double)r->timed, (double)r->queue_ns / 1e6 / (double)r->timed,
				    (double)r->wait_ns / 1e6 / (double)r->timed);
			}
			if (!check && level > 0) {
				r->nr_pictures[level]++;
				r->nr_w = 0;            /* (the processor's picture before is not this one's any more) */
			} else {
				denoise(r, isp, f, pic);
			}
			return RWCAM_BACKEND_GPU;
		}
		/* (after any failure the surface may be partly written: the processor makes the whole picture) */
		r->fallbacks++;
		r->sum_fallbacks++;
		if (e == RWA_ISP_E_UNSUPPORTED) {
			r->gpu_unsupported = true;
		} else if (e == RWA_ISP_E_SURFACE) {
			if (++r->surface_failures >= SURFACE_FAILURES) {
				r->gpu_unsupported = true;
			}
		} else if (!r->gpu_off) {
			r->gpu_off_until = now + GPU_OFF_MS;
			if (++r->gpu_failures >= GPU_FAILURES) {
				r->gpu_off = true;
			}
		}
		if (r->fallbacks <= 6 || r->gpu_off || r->gpu_unsupported) {
			rwcam_say("render: %s: the GPU did not make a picture (%d: %s): the processor does%s", r->name ? r->name : "?", e,
			    r->why[0] ? r->why : "no reason given", r->gpu_off ? ", from now on" : r->gpu_unsupported ?
			    ", for this stream's pictures as they are now" : e == RWA_ISP_E_SURFACE ? " this one" : ", and the next seconds'");
		}
	}
	if (!cpu_render(isp, f, raw, pic, pieces)) {
		return -1;
	}
	r->made[RWCAM_BACKEND_CPU]++;
	r->sum_made[RWCAM_BACKEND_CPU]++;
	denoise(r, isp, f, pic);
	return RWCAM_BACKEND_CPU;
}

void
rwcam_render_forget(struct rwcam_renderer *r)
{
	if (r->gpu != NULL && R.have) {
		R.calls.forget_surfaces(r->gpu);
	}
	if (r->zero_copy && R.have) {
		/* (q10jn) the nodes' contexts import this stream's surfaces too */
		pthread_mutex_lock(&fm_lock);
		for (int n = 0; n < 2; n++) {
			if (FM.ctx[n] != NULL) {
				R.calls.forget_surfaces(FM.ctx[n]);
			}
		}
		pthread_mutex_unlock(&fm_lock);
	}
	r->gpu_unsupported = false;
	r->surface_failures = 0;
	if (!(r->gpu_off && r->check_max > RWCAM_RENDER_CHECK_LIMIT)) {
		r->checked = false;             /* (the next start's first GPU pictures are compared again) */
		r->checks = 0;
	}
}

void
rwcam_render_end(struct rwcam_renderer *r)
{
	if (r->gpu != NULL && R.have) {
		R.calls.destroy(r->gpu);
	}
	r->gpu = NULL;
	r->gpu_tried = false;
	free(r->nr_y);
	free(r->nr_uv);
	free(r->nr_tmp);
	r->nr_y = r->nr_uv = r->nr_tmp = NULL;
	r->nr_w = r->nr_h = 0;
}
