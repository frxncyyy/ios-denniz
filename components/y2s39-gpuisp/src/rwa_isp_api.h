/*
 * The camera's ISP on the GPU: the interface of libredwoodair.dylib's rwa_isp_* calls (y2s39), for the camera bundle.
 *
 * What camera_isp.c's isp_render_nv12() and isp_render_full_nv12() do on the processor -- a RAW10 Bayer frame as the kernel
 * hands it over, made into the two planes of an NV12 picture -- done by the GPU, written straight into the picture's
 * IOSurface. The specification is GPU_ISP_API.md beside the sources; this header is the whole of what a caller compiles
 * against. Plain C; nothing here needs Objective-C, Metal or Mesa's headers.
 *
 * The library is NOT linked: every call is looked up, so that a library without them (or no library, or no GPU) is found
 * out at run time and the processor's ISP goes on as before:
 *
 *     void *lib = dlopen(RWA_ISP_LIBRARY, RTLD_NOW);
 *     unsigned (*version)(void) = lib ? dlsym(lib, "rwa_isp_api_version") : NULL;
 *     if (version == NULL || version() < 1) -> no GPU ISP here
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RWA_ISP_API_H
#define RWA_ISP_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RWA_ISP_LIBRARY         "/System/Library/Extensions/Nord2Metal.bundle/libredwoodair.dylib"
#define RWA_ISP_API_VERSION     1

/* What the calls answer. 0 is success; every failure is negative, leaves the output surface in an unspecified state (some of it
 * may have been written) and comes with a sentence in `why`. The caller renders that frame on the processor. */
enum {
	RWA_ISP_OK              = 0,
	RWA_ISP_E_ARGUMENT      = -1,   /* a NULL, a struct whose `size` is too small, a frame smaller than 4x4, a picture of no size */
	RWA_ISP_E_UNSUPPORTED   = -2,   /* a frame format, a mode or a parameter this version does not do (lens shading table on, ...) */
	RWA_ISP_E_SURFACE       = -3,   /* the output IOSurface cannot be written in place: not '420f'/'420v' in two planes, a plane's row
	                                 * length or offset not a multiple of 64, not the size asked for, or the GPU would not import it */
	RWA_ISP_E_GPU           = -4,   /* the GPU or its driver refused, or did not finish within two seconds: after this, expect the
	                                 * same again and stop asking for a while */
	RWA_ISP_E_MEMORY        = -5,
};

/* One of these per camera stream (or per thread that renders): it keeps what is made once and used for every frame -- the
 * shaders' tables for the zoom and the tone curve, the intermediate picture, the imports of the output surfaces. All contexts
 * of a process share one GPU device, which the first rwa_isp_create() opens. */
struct rwa_isp;

/* The frame, as struct redwood_camera_frame describes it (redwood_camera.h); `raw` is the first byte after that header. */
struct rwa_isp_frame {
	uint32_t        size;           /* sizeof(struct rwa_isp_frame) */
	uint32_t        width, height;  /* pixels: at least 4x4, at most 8192x8192, both even */
	uint32_t        stride;         /* bytes from one line to the next: at least (width + 3) / 4 * 5 */
	uint32_t        format;         /* REDWOOD_CAMERA_FORMAT_MIPI_RAW10 (1) or REDWOOD_CAMERA_FORMAT_LE40_RAW10 (4) */
	uint32_t        bayer;          /* 0 RGGB, 1 GRBG, 2 GBRG, 3 BGGR */
	uint32_t        black;          /* the black level, in 10-bit steps */
	const uint8_t   *raw;           /* height lines of stride bytes */
};

#define RWA_ISP_MODE_CELLS      0u      /* isp_render_nv12(): each 2x2 Bayer cell (or, zoomed or flipped, any 2x2 pixels of the
                                         * sensor) one pixel, the nearest for each pixel of the picture; any picture size */
#define RWA_ISP_MODE_FULL       1u      /* isp_render_full_nv12(): every pixel, bilinear demosaic; the picture is the frame's own
                                         * size; zoom and flip are not looked at (as on the processor) */

#define RWA_ISP_FLIP_H          1u      /* ISP_FLIP_H */
#define RWA_ISP_FLIP_V          2u      /* ISP_FLIP_V */

/* What struct isp holds for a picture (camera_isp.h), by value: nothing is kept after the call but the tables made from it. */
struct rwa_isp_params {
	uint32_t        size;           /* sizeof(struct rwa_isp_params) */
	uint32_t        mode;           /* RWA_ISP_MODE_* */
	float           wb[3];          /* isp->wb: the white balance's gains for red, green, blue */
	float           digital;        /* isp->digital */
	float           ccm[9];         /* isp->ccm, row by row */
	float           falloff;        /* isp->falloff */
	float           zoom;           /* isp->zoom (the stream's view: what isp_render_nv12 is given) */
	uint32_t        flip;           /* isp->flip */
	uint32_t        video_range;    /* 0: full range ('420f'); not 0: video range ('420v') -- must be the output surface's own */
	uint32_t        lens_shading;   /* isp->lsc.on: must be 0 (RWA_ISP_E_UNSUPPORTED otherwise: the table is not done on the GPU yet) */
	const uint8_t   *gamma;         /* isp->gamma: 4096 bytes; read during the call, uploaded again only when its bytes change */
};

#define RWA_ISP_OUT_ASYNC       1u      /* do not wait for the GPU: the planes are written when rwa_isp_wait() has returned 0 */

struct rwa_isp_output {
	uint32_t        size;           /* sizeof(struct rwa_isp_output) */
	uint32_t        flags;          /* RWA_ISP_OUT_* */
	void            *iosurface;     /* IOSurfaceRef: CVPixelBufferGetIOSurface(pixelBuffer). '420f' or '420v', two planes. */
	uint32_t        width, height;  /* the picture: the surface's own size */
};

#define RWA_ISP_DID_ZERO_COPY   1u      /* the frame was read by the GPU where it lay (rwa_isp_frame_memory) */
#define RWA_ISP_DID_IMPORT      2u      /* the output surface was imported by this call (not found among the context's kept ones) */

struct rwa_isp_result {                 /* filled in on success; may be NULL */
	uint32_t        size;           /* sizeof(struct rwa_isp_result), set by the caller */
	uint32_t        flags;          /* RWA_ISP_DID_* */
	uint64_t        prepare_ns;     /* the frame's upload (0 with zero copy), tables, imports */
	uint64_t        queue_ns;       /* building and submitting the three passes */
	uint64_t        wait_ns;        /* waiting for the GPU to finish them (0 with RWA_ISP_OUT_ASYNC) */
};

/*
 * The functions, as the library exports them (look each up by its name with dlsym; RWA_ISP_DECLARE_POINTERS gives a struct of
 * pointers of the right types and rwa_isp_load() fills it).
 *
 *   unsigned rwa_isp_api_version(void)
 *       RWA_ISP_API_VERSION of the library. A library without this symbol has no GPU ISP.
 *
 *   struct rwa_isp *rwa_isp_create(char *why, size_t why_size)
 *       A context, or NULL and a sentence (no GPU in this process, out of memory). Opens the process's ISP device the first time.
 *
 *   void rwa_isp_destroy(struct rwa_isp *isp)
 *       Waits for what is queued, then gives everything of the context back (its frame memory too). NULL is ignored.
 *
 *   void *rwa_isp_frame_memory(struct rwa_isp *isp, unsigned slot, size_t header_bytes, size_t frame_bytes, char *why, size_t why_size)
 *       Optional, for zero copy: memory of the GPU's for one frame, where the caller has the kernel put it. Returns a pointer P to
 *       header_bytes + frame_bytes writable bytes; a read() of the header and the frame's bytes at P leaves the frame's first byte at
 *       P + header_bytes, which is where the GPU can read it without a copy (pass that as rwa_isp_frame.raw). slot: 0 to 3, each its
 *       own memory, kept until the context is destroyed or the slot is asked again with other sizes. NULL and a sentence if it
 *       cannot be had; then (and for any `raw` that is not in such memory) the frame is uploaded, which costs a copy of it.
 *       The memory of a slot must not be written while a render of it is queued and not yet waited for.
 *
 *   int rwa_isp_render(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params,
 *                      const struct rwa_isp_output *output, struct rwa_isp_result *result, char *why, size_t why_size)
 *       The picture. RWA_ISP_OK: both planes of the surface are written (or, with RWA_ISP_OUT_ASYNC, will be once rwa_isp_wait
 *       has returned). `raw` is read during the call when it is uploaded, and until the GPU has finished when it is zero copy.
 *
 *   int rwa_isp_wait(struct rwa_isp *isp, char *why, size_t why_size)
 *       Waits for every render of this context queued with RWA_ISP_OUT_ASYNC. RWA_ISP_OK, or RWA_ISP_E_GPU after two seconds.
 *
 *   void rwa_isp_forget_surfaces(struct rwa_isp *isp)
 *       Lets go of the output surfaces the context has imported (it keeps the last 8 it was given, by IOSurface ID, so that a
 *       pool's surfaces are imported once). Call it when a pool is released or the stream stops; the surfaces' memory stays
 *       mapped for the GPU until then.
 */
#define RWA_ISP_DECLARE_POINTERS \
	unsigned (*api_version)(void); \
	struct rwa_isp *(*create)(char *why, size_t why_size); \
	void (*destroy)(struct rwa_isp *isp); \
	void *(*frame_memory)(struct rwa_isp *isp, unsigned slot, size_t header_bytes, size_t frame_bytes, char *why, size_t why_size); \
	int (*render)(struct rwa_isp *isp, const struct rwa_isp_frame *frame, const struct rwa_isp_params *params, \
		      const struct rwa_isp_output *output, struct rwa_isp_result *result, char *why, size_t why_size); \
	int (*wait)(struct rwa_isp *isp, char *why, size_t why_size); \
	void (*forget_surfaces)(struct rwa_isp *isp);

struct rwa_isp_calls {
	RWA_ISP_DECLARE_POINTERS
};

/*
 * Every call out of `library` (a dlopen handle of RWA_ISP_LIBRARY), with the caller's own dlsym: 1 when all are there and the
 * version is at least `need`, else 0 and the struct zeroed. Written as a macro so that this header needs no <dlfcn.h>:
 *
 *     struct rwa_isp_calls gpu;
 *     int have;
 *     RWA_ISP_LOAD(&gpu, dlsym, library, RWA_ISP_API_VERSION, have);
 */
#define RWA_ISP_LOAD(calls, dlsym_, library, need, have) do { \
	struct rwa_isp_calls *c_ = (calls); \
	void *l_ = (library); \
	*(void **)&c_->api_version = l_ ? dlsym_(l_, "rwa_isp_api_version") : NULL; \
	*(void **)&c_->create = l_ ? dlsym_(l_, "rwa_isp_create") : NULL; \
	*(void **)&c_->destroy = l_ ? dlsym_(l_, "rwa_isp_destroy") : NULL; \
	*(void **)&c_->frame_memory = l_ ? dlsym_(l_, "rwa_isp_frame_memory") : NULL; \
	*(void **)&c_->render = l_ ? dlsym_(l_, "rwa_isp_render") : NULL; \
	*(void **)&c_->wait = l_ ? dlsym_(l_, "rwa_isp_wait") : NULL; \
	*(void **)&c_->forget_surfaces = l_ ? dlsym_(l_, "rwa_isp_forget_surfaces") : NULL; \
	(have) = c_->api_version && c_->create && c_->destroy && c_->frame_memory && c_->render && c_->wait && \
		 c_->forget_surfaces && c_->api_version() >= (unsigned)(need); \
	if (!(have)) { \
		c_->api_version = NULL; c_->create = NULL; c_->destroy = NULL; c_->frame_memory = NULL; c_->render = NULL; \
		c_->wait = NULL; c_->forget_surfaces = NULL; \
	} \
} while (0)

#ifdef __cplusplus
}
#endif

#endif /* RWA_ISP_API_H */
