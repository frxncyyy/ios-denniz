/*
 * The pictures of the cameras' frames, behind one call with two ways of making them:
 *   - the processor (camera_isp.c: isp_render_nv12 / isp_render_bgra, in pieces on several cores), always there;
 *   - the GPU (the graphics library's rwa_isp_*, GPU_ISP_API.md / rwa_isp_api.h), when the library has it: found at run time, nothing linked.
 * What is measured of a frame (levels, exposure, white balance, sharpness) is not here: that stays the reader's, on a sixteenth of the frame; the
 * GPU is given the numbers already worked out (struct isp with the stream's zoom and flip).
 * A picture the GPU cannot or does not make is made by the processor, whole: rwcam_render() only fails when neither can. No Apple types in here
 * (the IOSurface is a pointer handed through): built unchanged into the bundle and into the host test (test/t_render.c).
 */
#ifndef RWCAM_RENDER_H
#define RWCAM_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "camera_isp.h"
#include "rwa_isp_api.h"

#define RWCAM_PF_420F   0x34323066u     /* '420f' */
#define RWCAM_PF_420V   0x34323076u     /* '420v' */
#define RWCAM_PF_BGRA   0x42475241u     /* 'BGRA' */

enum { RWCAM_BACKEND_CPU = 0, RWCAM_BACKEND_GPU = 1, RWCAM_BACKENDS = 2 };

struct rwcam_picture {
	uint32_t        format;         /* RWCAM_PF_* */
	uint32_t        width, height;
	void            *iosurface;     /* the buffer's IOSurface, for the GPU (NULL: it has none) */
	/* for the processor: the planes' memory, made reachable by map() (the buffer locked) when it is the one to write; context is map's */
	bool            (*map)(struct rwcam_picture *pic);
	void            *context;
	uint8_t         *plane[2];
	size_t          stride[2];
	bool            mapped;
};

/* one for each stream output that makes pictures (a GPU context is meant for one stream: GPU_ISP_API.md) */
struct rwcam_renderer {
	const char      *name;
	struct rwa_isp  *gpu;
	bool            gpu_tried;      /* a context was asked for (and gpu is NULL: it was refused) */
	float           *stats;         /* (q10jo) where the GPU puts the frame's statistics (rwcam_render_stats_sink) */
	uint64_t        stats_made;
	bool            zero_copy;      /* (q10jn) a viewfinder/film output: its frames may be rendered where the reader read them (rwcam_render_frame_memory) */
	bool            gpu_off;        /* for good: no context, or three failures of the GPU in a row */
	bool            gpu_unsupported;        /* this configuration is not the GPU's (until rwcam_render_forget) */
	unsigned        gpu_failures, surface_failures;
	uint64_t        gpu_off_until;  /* (now_ms) after a failure of the GPU: not asked again before */
	uint64_t        made[RWCAM_BACKENDS], fallbacks;
	uint64_t        prepare_ns, queue_ns, wait_ns, timed;   /* the GPU's own figures, summed over `timed' pictures */
	uint32_t        last_flags;
	char            why[256];       /* the last reason the GPU did not make a picture */
	/*
	 * The GPU's first picture of a stream is made by the processor as well and the two compared at a grid of places (the shaders' arithmetic is
	 * the processor's: 0 is expected; nothing of this had run on the phone's GPU when it was written). check_max: the largest difference in 8-bit
	 * steps (-1: not compared yet). Over RWCAM_RENDER_CHECK_LIMIT the GPU is not this stream's any more and that picture is the processor's.
	 * Not the first alone (run q10hl: a black viewfinder with the session running and nothing in the log to say why): the first
	 * RWCAM_RENDER_CHECK_FIRST pictures of a stream (a pool's buffers are several surfaces, each imported by the GPU on its own) and one every
	 * RWCAM_RENDER_CHECK_MS after that, for as long as the GPU makes them; a picture that cannot be compared counts as wrong.
	 */
	int             check_max;      /* the largest difference of all comparisons */
	int             check_last;
	bool            checked;
	uint64_t        checks, check_at;       /* how many comparisons, and when the last was (now_ms) */
	/*
	 * Noise (run q10hz: "in little light there is a great deal of noise, on all lenses"). A picture taken with gain (analog x digital of
	 * RWCAM_NR_GAIN_1 or more: level 1; of RWCAM_NR_GAIN_2 or more: level 2) is cleaned after it is made, by whichever backend:
	 *   - its colour planes smoothed (a sensor's noise is worst there, and the eye does not miss the detail);
	 *   - each pixel drawn towards the picture before where the two differ by little (noise) and left where they differ by much (something
	 *     moved): pictures of up to RWCAM_RENDER_CHECK_PIXELS only, the stream's viewfinder ones (nr_y, nr_uv: the picture before);
	 *   - at level 2 the brightness smoothed too where its neighbours are near it (not across an edge).
	 * rwcam_render_denoise(0): none of it.
	 */
	uint8_t         *nr_y, *nr_uv, *nr_tmp;
	uint32_t        nr_w, nr_h;
	uint64_t        nr_pictures[3]; /* pictures at each level, since the last summary */
	/* since the last rwcam_render_summary() */
	uint64_t        sum_made[RWCAM_BACKENDS], sum_fallbacks, sum_prepare_ns, sum_queue_ns, sum_wait_ns;
};
#define RWCAM_RENDER_CHECK_LIMIT        24
#define RWCAM_RENDER_CHECK_PIXELS       (4u << 20)      /* pictures larger than this (the 12 MP stills): the first one and one every ten checks' time */
#define RWCAM_RENDER_CHECK_FIRST        8
#define RWCAM_RENDER_CHECK_MS           2000

/*
 * calls: the library's seven entry points (RWA_ISP_LOAD), or NULL: no GPU. apply: runs work(context, 0 .. n-1), on several cores if it can (NULL:
 * one after the other). now_ms: a clock that only goes forward. May be called again (the test does).
 */
void rwcam_render_setup(const struct rwa_isp_calls *calls, void (*apply)(size_t n, void *context, void (*work)(void *, size_t)), uint64_t (*now_ms)(void));
bool rwcam_render_gpu_there(void);
void rwcam_render_prefer(int backend);  /* RWCAM_BACKEND_CPU: the GPU is never asked; RWCAM_BACKEND_GPU: it is whenever it can (as at first) */
int rwcam_render_preferred(void);
/* how many first GPU pictures of a stream are compared with the processor's, and how often after that (0: never again); the test's */
void rwcam_render_checks(unsigned first, uint64_t every_ms);
#define RWCAM_NR_GAIN_1 3.0
#define RWCAM_NR_GAIN_2 8.0
void rwcam_render_denoise(int most);    /* the highest level used: 0 none, 2 all (as at first) */
/*
 * The caller has seen that the GPU's picture is not one (black while the sensor's levels say the frame is not): the GPU is off for this stream.
 * The caller then asks for the picture again (the buffer mapped by then: the processor makes it).
 */
void rwcam_render_distrust(struct rwcam_renderer *r, const char *why);
/* the backend that made the picture, or -1 (nothing made: the picture's memory could not be had) */
int rwcam_render(struct rwcam_renderer *r, const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, struct rwcam_picture *pic,
    unsigned pieces);
/* "GPU n (prepare a ms, queue b ms, wait c ms), processor m, k of them the GPU's failures (reason)" for the pictures since the last call */
void rwcam_render_summary(struct rwcam_renderer *r, char *text, size_t size);
void rwcam_render_forget(struct rwcam_renderer *r);     /* the stream stopped or its buffers changed: the GPU's kept surfaces dropped */
void rwcam_render_end(struct rwcam_renderer *r);
/*
 * (q10jn) Zero copy: the reader of node `node` (0, 1) reads its frames into memory of a GPU context of that node's own, slot 0 or 1 (its two
 * buffers), and a zero_copy renderer given a frame that lies there renders it with that context (the GPU reads the frame where it is: no upload).
 * NULL: not on (rwcam_render_zero_copy(false), no GPU, or the library refused): the reader mallocs as before. The memory is the library's: never
 * free()d, valid while the process lives (the node's context is never destroyed); a slot asked again with a larger size moves.
 */
void rwcam_render_zero_copy(bool on);
/* (q10jo) would the GPU be asked for this renderer's next picture (a library, preferred, not off or refused for this stream)? */
bool rwcam_render_gpu_ready(const struct rwcam_renderer *r);
/*
 * (q10jo) the frame's statistics from the GPU (libredwoodair y2s64+: rwa_isp_output.stats), asked of every GPU picture of a zero_copy (viewfinder
 * / film) renderer when a sink is set; the sink gets them with the frame they are of (on the thread that made the picture). An older library
 * makes none (stats_w 0): the sink is not called.
 */
void rwcam_render_stats_sink(void (*sink)(const struct redwood_camera_frame *f, const float *stats, uint32_t w, uint32_t h, uint32_t step));
void *rwcam_render_frame_memory(int node, unsigned slot, size_t header_bytes, size_t frame_bytes);        /* the GPU's context given back (not while a render of it is under way) */

#endif
