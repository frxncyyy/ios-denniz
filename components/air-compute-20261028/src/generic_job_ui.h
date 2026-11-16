#ifndef TEGU_GENERIC_JOB_UI_H
#define TEGU_GENERIC_JOB_UI_H
#include "generic_job.h"
#include "native_package_ui1.h"
#include "native_package_ui2.h"
#include "blend_state.h"
#include "vertex_fetch.h"
#include <stddef.h>
#include <stdint.h>

/* Builds the TGJ1 job that the selector-16 UI1/UI2 clients would have built
 * in XNU.  The caller owns two shared allocations: the scratch image source
 * (TEGU_GENERIC_JOB_SCRATCH_BYTES) and, for LOAD passes, the 8 KiB preload
 * resource/DCD pages.  Returns 0 and fills both images and *out. */
#define TEGU_GENERIC_JOB_LOAD_BYTES 8192u
/* The render target descriptor inside the job's descriptor page (scratch
 * offset 0): after the 128-byte framebuffer descriptor. */
#define TEGU_GENERIC_JOB_RT_OFFSET 128u

struct tegu_generic_job_images {
    uint64_t scratch_token, load_token;
    uint8_t *scratch;  /* TEGU_GENERIC_JOB_SCRATCH_BYTES */
    uint8_t *load;     /* TEGU_GENERIC_JOB_LOAD_BYTES, LOAD passes only */
    uint32_t sampler_linear; /* UI2: bit 0 minify, bit 1 magnify linear */
};

/* Reflection-driven draw of any pipeline the bridge's generic planner and
 * tegu_mali_compile_generic_pair admit.  Descriptor set 0 binding N is
 * resource table 1 entry N (buffers, textures, framebuffer-fetch inputs) and
 * table 2 entry N (samplers); table 0 holds the vertex attributes.  Binding
 * kinds mirror the bridge's TeguAIRGenericBinding kinds. */
#define TEGU_GENERIC_DRAW_MAX_BINDINGS 16u
#define TEGU_GENERIC_DRAW_MAX_ATTRIBUTES 8u
#define TEGU_GENERIC_DRAW_PUSH_BYTES 128u
#define TEGU_GENERIC_DRAW_CONSTANT_BYTES 512u
enum {
    TEGU_GENERIC_DRAW_BUFFER = 1, TEGU_GENERIC_DRAW_CONSTANT = 2, TEGU_GENERIC_DRAW_COMBINED = 3,
    TEGU_GENERIC_DRAW_COMBINED_IMMUTABLE = 4, TEGU_GENERIC_DRAW_TEXTURE = 5, TEGU_GENERIC_DRAW_SAMPLER = 6,
    TEGU_GENERIC_DRAW_ATTACHMENT = 7, TEGU_GENERIC_DRAW_NEUTRAL_IMAGE = 8, TEGU_GENERIC_DRAW_NEUTRAL_BUFFER = 9,
    TEGU_GENERIC_DRAW_TILE_ATTACHMENT = 10 /* compiler-certified active tile, no memory descriptor */
};
/* Sampler word: bit0 minify linear, bit1 magnify linear, then Mali wrap
 * modes S/T/R in bits 4..7, 8..11, 12..15 (0 means clamp to edge). */
#define TEGU_GENERIC_DRAW_SAMPLER_WORD(min, mag, s, t, r) \
    ((uint32_t)(min) | (uint32_t)(mag) << 1 | (uint32_t)(s) << 4 | (uint32_t)(t) << 8 | (uint32_t)(r) << 12)

#define TEGU_GENERIC_SHADER_TILE_FETCH 1u
struct tegu_generic_draw_shader {
    const uint8_t *vertex_code, *fragment_code;
    uint32_t vertex_bytes, fragment_bytes;
    uint32_t vertex_registers, fragment_registers, secondary_registers;
    uint32_t vertex_fau_words, fragment_fau_words;
    uint32_t position_offset, secondary_offset, varying_bytes;
    uint64_t vertex_preload, fragment_preload, secondary_preload;
    uint32_t attribute_count, flags;
    struct tegu_vertex_attribute attributes[TEGU_GENERIC_DRAW_MAX_ATTRIBUTES];
    struct tegu_blend_inputs blend;
};
struct tegu_generic_draw_memory {
    uint64_t token, offset, bytes; /* owned allocation and the window read */
};
struct tegu_generic_draw_binding {
    uint32_t binding, kind;
    struct tegu_generic_draw_memory memory;     /* buffer window or texel rows */
    uint32_t width, height, row_bytes, sampler; /* textures; sampler word */
    uint32_t format, reserved;                  /* Metal format: 80 BGRA8 (default 0), 10 R8 */
};
#define TEGU_GENERIC_DRAW_TRIANGLES 0u
#define TEGU_GENERIC_DRAW_LINES 1u
#define TEGU_GENERIC_DRAW_TRIANGLE_STRIP 2u
/* Vertices per draw: IDVS writes position (16 B) and varyings per vertex into
 * the 64 KiB geometry heap; keep a quarter of it free. */
#define TEGU_GENERIC_DRAW_MAX_VERTICES 2048u /* the geometry budget below is the real bound */
#define TEGU_GENERIC_DRAW_GEOMETRY_BUDGET 0xc000u
struct tegu_generic_draw_request {
    /* Nord2 userspace extension, never a donor CSF wire structure. */
    uint32_t viewport_valid;float viewport[6];
    uint32_t target_format, clear_words[4]; /* zero format: legacy BGRA8 clear_color */
    uint32_t width, height, row_bytes, load_action, clear_color, vertex_count;
    /* TEGU_GENERIC_DRAW_TRIANGLES (0), _LINES (1) or _TRIANGLE_STRIP (2). */
    uint32_t primitive;
    uint32_t scissor[4];
    uint64_t target_token, target_offset, target_length;
    struct tegu_generic_draw_memory vertex;
    /* Native index buffer (2- or 4-byte indices; index_size 0 = none). Used
     * for vertex-pulling shaders, whose IDs come from the indices. */
    struct tegu_generic_draw_memory index;
    uint32_t index_size;
    uint32_t binding_count, vertex_push_bytes, fragment_push_bytes, constant_bytes;
    struct tegu_generic_draw_binding bindings[TEGU_GENERIC_DRAW_MAX_BINDINGS];
    /* Push images indexed by AIR push offset; FAU word 32 onwards. */
    uint8_t vertex_push[TEGU_GENERIC_DRAW_PUSH_BYTES], fragment_push[TEGU_GENERIC_DRAW_PUSH_BYTES];
    uint8_t constant[TEGU_GENERIC_DRAW_CONSTANT_BYTES];
};

#ifdef __cplusplus
extern "C" {
#endif
/* 0, or -1 invalid request, -2 more than TGJ1 spans, -3 spans exceed the
 * resource pages, -4 code exceeds its region, -5 vertex fetch, -7 state. */
int tegu_generic_draw_job_build(const struct tegu_generic_draw_shader *shader,
    const struct tegu_generic_draw_request *request, const struct tegu_generic_job_images *images,
    struct tegu_generic_job_request *out);
/* Several consecutive draws of one pass in one job: one tiler context, one
 * LOAD/CLEAR and one store of the target.  Draw 0 sets the target and load
 * action; later draws must draw the same target with LOAD and must not read
 * its memory (framebuffer fetch).  Builds the longest prefix whose spans,
 * code, geometry, tiler heap estimate and command stream fit one job and
 * returns its draw count; 0 when fewer than two draws fit (*out untouched):
 * build those one per job.  Per-draw 4 KiB descriptor blocks allow 32 draws,
 * 64 when the job has the 2 MiB heap (any target over 16x16). */
#define TEGU_GENERIC_BATCH_MAX_DRAWS 64u
#define TEGU_GENERIC_BATCH_TGJ1_DRAWS 32u
/* Heap bytes a batch may plan for, of the 2 MiB (see
 * tegu_generic_batch_heap_frame/_draw in generic_job_ui.c). */
#define TEGU_GENERIC_BATCH_HEAP_BUDGET 0x1c0000u
#define TEGU_GENERIC_SMALL_HEAP_BUDGET 0x1e0000u /* leaves128KiB of2MiB */
struct tegu_generic_batch_limits {
    uint32_t version;      /* TEGU_GENERIC_JOB_VERSION (TGJ1) or TEGU_GENERIC_JOB2_VERSION */
    uint32_t max_commands; /* the kernel's submit cap: 128 (old), 160 (CAP_GENERIC_JOB_160), 400 (TGJ2) */
    uint32_t max_draws;    /* at most TEGU_GENERIC_BATCH_MAX_DRAWS */
    uint64_t heap_budget;  /* legacy scaled estimate budget */
    uint64_t small_heap_budget; /* optional bounded small-triangle model;0=off */
};
/* Why a batch stopped before the next offered draw (info->stop; counted
 * in TEGU_PERF_BREAK).  Diagnostic only: the built job does not depend on it. */
enum {
    TEGU_BATCH_STOP_NONE = 0,     /* every offered draw fit */
    TEGU_BATCH_STOP_INVALID,      /* the draw is not buildable on its own */
    TEGU_BATCH_STOP_READS_TARGET, /* framebuffer fetch, or vertex/index/binding memory in the target */
    TEGU_BATCH_STOP_SHAPE,        /* target size/row/offset/length differ, or not LOAD */
    TEGU_BATCH_STOP_SPANS,        /* allocation spans (8 TGJ1, 32 TGJ2) */
    TEGU_BATCH_STOP_GEOMETRY,     /* summed IDVS geometry budget (48 KiB) */
    TEGU_BATCH_STOP_HEAP,         /* tiler heap estimate over the budget */
    TEGU_BATCH_STOP_CODE,         /* shader code regions (8 KiB VS + 28 KiB FS) */
    TEGU_BATCH_STOP_BLOCKS,       /* descriptor blocks / max draws */
    TEGU_BATCH_STOP_PLAN,         /* resource pages or the job plan */
    TEGU_BATCH_STOP_COMMANDS,     /* command stream over the cap */
    TEGU_BATCH_STOP_BUILD,        /* the stream build failed */
    TEGU_BATCH_STOP_REASONS
};
struct tegu_generic_batch_info { uint64_t heap_estimate; uint32_t commands; uint32_t stop; uint32_t small_heap; };
int tegu_generic_draw_batch_build2(const struct tegu_generic_draw_shader *const *shaders,
    const struct tegu_generic_draw_request *const *requests, unsigned count,
    const struct tegu_generic_job_images *images, const struct tegu_generic_batch_limits *limits,
    struct tegu_generic_job2_request *out, struct tegu_generic_batch_info *info);
/* TGJ1 with the 128-command cap of kernels before 0204. */
int tegu_generic_draw_batch_build(const struct tegu_generic_draw_shader *const *shaders,
    const struct tegu_generic_draw_request *const *requests, unsigned count,
    const struct tegu_generic_job_images *images, struct tegu_generic_job_request *out);
/* A version-1 request built in TGJ2 form, as the TGJ1 ABI; 0 or -1. */
int tegu_generic_job_to_v1(const struct tegu_generic_job2_request *request, struct tegu_generic_job_request *out);
/* Tiler heap estimate parts (bytes): per job for the target, per draw. */
uint64_t tegu_generic_batch_heap_frame(uint32_t width, uint32_t height);
uint64_t tegu_generic_batch_heap_draw(const struct tegu_generic_draw_shader *shader,
    const struct tegu_generic_draw_request *request);
/* Opt-in (default off): the fragment pass of a LOAD job covers only the
 * union of its draws' scissors (RUN_FRAGMENT bbox registers 42/43), as
 * panfrost's CSF backend does (batch->minx..maxy); tiles outside it keep
 * their memory.  CLEAR/DontCare passes always cover the whole target.
 * Off, every stream is byte-identical to before. */
void tegu_generic_set_fragment_bbox(int enabled);
int tegu_generic_fragment_bbox(void);
/* Opt-in (default off; on in --batch-merge builds unless TEGU_BATCH_SHARE=0):
 * a batch places byte-identical shader code once, whichever pipeline object
 * holds it (QuartzCore's fixed_frag variants share ~10 vertex shaders of
 * ~4.5 KiB across hundreds of pipelines, R203 pipeline cache).  Without it
 * only the same code pointer is shared and the 8+28 KiB code regions end a
 * batch after ~5 distinct pipelines.  Off, every stream is byte-identical. */
void tegu_generic_set_code_share(int enabled);
int tegu_generic_code_share(void);
/* Opt-in immutable resource-table sharing within one batch; default off. */
void tegu_generic_set_resource_share(int enabled);
int tegu_generic_resource_share(void);
/* Single-sample BGRA8 generic jobs only. Default256; accepts256 or1024. */
int tegu_generic_set_tile_pixels(unsigned pixels);
unsigned tegu_generic_tile_pixels(void);

/* GPU floor microbenchmark (gpu_floor_bench.inc): rewrite a built generic
 * draw job's command stream into one variant.  Every variant keeps only
 * opcodes of the kernel allowlist.  0, or -1 when the stream does not have
 * the shape the variant needs or the result would exceed cap commands.
 * bbox (BBOX only): inclusive pixels {x0, y0, x1, y1} inside the target. */
enum tegu_generic_bench_variant {
    TEGU_BENCH_AS_BUILT,  /* the production stream */
    TEGU_BENCH_NOP,       /* one NOP: doorbell, kernel prologue flush, token store */
    TEGU_BENCH_FLUSH1,    /* the job tail's FLUSH_CACHE2 0x211 and its wait, once */
    TEGU_BENCH_FLUSH8,    /* ... eight times */
    TEGU_BENCH_REQRES,    /* REQ_RESOURCE tiler|idvs|fragment, then release */
    TEGU_BENCH_NOFLUSH,   /* production without the tail FLUSH_CACHE2 + wait */
    TEGU_BENCH_BBOX,      /* production, fragment bbox registers 42/43 = bbox */
    TEGU_BENCH_KEEPRES,   /* production without the final REQ_RESOURCE 0 */
    TEGU_BENCH_NODRAW,    /* production without its RUN_IDVS commands */
    TEGU_BENCH_NOFRAG,    /* production without RUN_FRAGMENT and its wait */
    TEGU_BENCH_FRAG2,     /* production with RUN_FRAGMENT + wait issued twice */
};
int tegu_generic_bench_transform(uint64_t *commands, uint32_t *count, uint32_t cap,
    unsigned variant, const uint32_t bbox[4]);
int tegu_ui1_generic_job_build(const struct tegu_ui1_info *info, const void *package,
    size_t package_bytes, const struct tegu_ui1_bind_request *request,
    const struct tegu_generic_job_images *images, struct tegu_generic_job_request *out);
int tegu_ui2_generic_job_build(const struct tegu_ui2_info *info, const void *package,
    size_t package_bytes, const struct tegu_ui2_bind_request *request,
    const struct tegu_generic_job_images *images, struct tegu_generic_job_request *out);
#ifdef __cplusplus
}
#endif
#endif
