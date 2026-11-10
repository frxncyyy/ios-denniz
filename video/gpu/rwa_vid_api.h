/*
 * The three video kernels of include/n2vk.h, done on the GPU: the interface of the
 * library calls that a future libredwoodair.dylib exposes as rwa_vid_* (the plan's stage 2).
 *
 * Shaped like rwa_isp_api.h beside it: plain C, no Metal and no Mesa header, every call
 * looked up rather than linked, and a failure is a negative number and a sentence in the
 * caller's `why`.
 *
 *     void *lib = dlopen(RWA_VID_LIBRARY, RTLD_NOW);
 *     unsigned (*version)(void) = lib ? dlsym(lib, "rwa_vid_api_version") : NULL;
 *     if (version == NULL || version() < 1) -> the processor reference (n2vk_ref_*.c)
 *
 * The results are the reference's bit for bit: every kernel is exact integer arithmetic,
 * and rwa_vid_backend() hands the same functions to n2vk_use() so the codecs' own calls
 * can take the GPU route without knowing this header.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RWA_VID_API_H
#define RWA_VID_API_H

#include <stddef.h>
#include <stdint.h>

#include "n2vk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RWA_VID_LIBRARY         "/System/Library/Extensions/Nord2Metal.bundle/libredwoodair.dylib"
#define RWA_VID_API_VERSION     1

/* What the calls answer.  The GPU-error values are n2vk.h's own numbers so that a
 * backend built by rwa_vid_backend() needs no translation: -4 makes the dispatcher fall
 * back to the reference and count a strike, -2 falls back without one. */
enum {
	RWA_VID_OK              = 0,
	RWA_VID_E_ARGUMENT      = -1,   /* NULL, a struct whose `size` is too small, a size outside n2vk.h's limits */
	RWA_VID_E_UNSUPPORTED   = -2,   /* a texture size or format this version does not do */
	RWA_VID_E_SURFACE       = -3,   /* an output that cannot be written in place */
	RWA_VID_E_GPU           = -4,   /* the GPU or its driver refused, or a pass did not finish */
	RWA_VID_E_MEMORY        = -5,
};

/* One per thread that uses the kernels, or one shared under the caller's own lock: a
 * context keeps the shaders and the textures of the last size it was asked for. */
struct rwa_vid;

/*
 *   unsigned rwa_vid_api_version(void)
 *       RWA_VID_API_VERSION.
 *
 *   struct rwa_vid *rwa_vid_create(char *why, size_t why_size)
 *       A context, or NULL and a sentence.  Opens the process's GPU device the first time.
 *
 *   void rwa_vid_destroy(struct rwa_vid *v)
 *       Waits for what is queued, gives the textures and the shaders back.  NULL is ignored.
 *
 *   int rwa_vid_me16(struct rwa_vid *v, const n2vk_me16_args *a, int16_t *mvx,
 *                    int16_t *mvy, uint32_t *sad, char *why, size_t why_size)
 *       n2vk_ref_me16()'s answer, computed by the me16 passes (n2vk.h's candidate set,
 *       centre clamping and tie-break).  a->size, strides and limits are checked exactly
 *       as the reference checks them.
 *
 *   int rwa_vid_half(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride,
 *                    uint8_t *dst, uint32_t dst_stride, uint32_t width, uint32_t height,
 *                    char *why, size_t why_size)
 *       n2vk_ref_half()'s answer.
 *
 *   int rwa_vid_fdct8_quant(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride,
 *                           uint32_t width, uint32_t height, const uint16_t qtable[64],
 *                           int16_t *coef, char *why, size_t why_size)
 *       n2vk_ref_fdct8_quant()'s answer (two passes: rows then columns).
 *
 *   int rwa_vid_backend(n2vk_backend *out, struct rwa_vid *v)
 *       Fills `out` with a size, a name and the three wrappers, ctx = v.  The struct is
 *       valid while the context lives.  Returns RWA_VID_OK or an error.
 *
 *   struct rwa_device *rwa_vid_device(struct rwa_vid *v)
 *       The device the context opened, for a caller that wants its name (rwa_device_name
 *       of air_metal.h).  NULL for a NULL context.
 */
unsigned rwa_vid_api_version(void);
struct rwa_vid *rwa_vid_create(char *why, size_t why_size);
void rwa_vid_destroy(struct rwa_vid *v);
int rwa_vid_me16(struct rwa_vid *v, const n2vk_me16_args *a, int16_t *mvx, int16_t *mvy, uint32_t *sad,
                 char *why, size_t why_size);
int rwa_vid_half(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride, uint8_t *dst, uint32_t dst_stride,
                 uint32_t width, uint32_t height, char *why, size_t why_size);
int rwa_vid_fdct8_quant(struct rwa_vid *v, const uint8_t *src, uint32_t src_stride, uint32_t width, uint32_t height,
                        const uint16_t qtable[64], int16_t *coef, char *why, size_t why_size);
int rwa_vid_backend(n2vk_backend *out, struct rwa_vid *v);
struct rwa_device *rwa_vid_device(struct rwa_vid *v);

#ifdef __cplusplus
}
#endif
#endif
