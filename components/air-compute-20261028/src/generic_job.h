#ifndef TEGU_GENERIC_JOB_H
#define TEGU_GENERIC_JOB_H
#include "fullscreen_job_contract.h"
#include <stddef.h>
#include <stdint.h>

/* TGJ1: one render job built entirely in userspace.  The kernel maps the
 * listed allocations into the per-job v7 backing, copies the scratch image,
 * screens the command stream and submits it.  GPU VAs are a pure function of
 * page counts, so userspace computes them with tegu_generic_job_plan(). */
#define TEGU_GENERIC_JOB_SELECTOR 19u
#define TEGU_GPU_CAP_GENERIC_JOB (1ULL<<18)
#define TEGU_GENERIC_JOB_VERSION 1u
#define TEGU_GENERIC_JOB_MAX_SPANS 8u
#define TEGU_GENERIC_JOB_MAX_COMMANDS 160u
/* Kernels before 0204 (no TEGU_GPU_CAP_GENERIC_JOB_160) refuse streams over
 * 128 commands in tg_submit, whatever the request holds. */
#define TEGU_GENERIC_JOB_SUBMIT_COMMANDS 128u
#define TEGU_GENERIC_JOB_SCRATCH_BYTES ((uint64_t)FS_V7_SCRATCH_PAGES * FS_PAGE_SIZE)
#define TEGU_GENERIC_JOB_HEAP2M 1u
/* Per-command-buffer target batching (kernels advertising
 * TEGU_GPU_CAP_GENERIC_JOB_BATCH).  CONTINUES: a later job of the same
 * command buffer draws this target, so skip the post-completion target cache
 * flush and display notification.  CLEAN: the previous job drew this target
 * and the CPU has not touched it since, so skip the pre-submit flush.  The
 * last job on a target clears CONTINUES, which flushes and notifies once. */
#define TEGU_GENERIC_JOB_TARGET_CONTINUES 2u
#define TEGU_GENERIC_JOB_TARGET_CLEAN 4u
#define TEGU_GENERIC_JOB_FLAGS (TEGU_GENERIC_JOB_HEAP2M | TEGU_GENERIC_JOB_TARGET_CONTINUES | TEGU_GENERIC_JOB_TARGET_CLEAN)
#define TEGU_GPU_CAP_GENERIC_JOB_BATCH (1ULL<<19)
/* Resource spans may name imported IOSurfaces (their selector-17 handle):
 * sampled client surfaces such as wallpaper and app content. */
#define TEGU_GPU_CAP_GENERIC_JOB_SURFACE_SPANS (1ULL<<20)
/* 0204: the kernel submits TGJ1 streams up to TEGU_GENERIC_JOB_MAX_COMMANDS
 * (older kernels refuse more than 128 in tg_submit). */
#define TEGU_GPU_CAP_GENERIC_JOB_160 (1ULL<<21)

/* TGJ2 (selector 20, 0204): one render pass in one job.  Same header and
 * rules as TGJ1, room for 32 allocations and 400 commands (the request stays
 * within the 4096-byte inline IOConnectCallMethod structure).  Three scalar
 * outputs: serial, fence, and the tiler heap bytes the job used (read back
 * from the job's heap descriptor; UINT64_MAX when unknown). */
#define TEGU_GENERIC_JOB2_SELECTOR 20u
#define TEGU_GPU_CAP_GENERIC_JOB2 (1ULL<<22)
#define TEGU_GENERIC_JOB2_VERSION 2u
#define TEGU_GENERIC_JOB2_MAX_SPANS 32u
#define TEGU_GENERIC_JOB2_MAX_COMMANDS 400u
#define TEGU_GENERIC_JOB2_OUTPUTS 3u
/* fs_map locks AS1 at FS_APERTURE_VA with at most a 32 MiB window
 * (tg_fullscreen_lock_begin): a job may map no more leaves than that. */
#define TEGU_GENERIC_JOB_APERTURE_PAGES 8192u

struct tegu_generic_job_span {
    uint64_t token, offset, length; /* owned buffer; page-aligned offset */
};

struct tegu_generic_job_request {
    uint32_t version, flags;
    uint32_t width, height, row_stride, span_count, command_count, reserved;
    uint64_t target_token, target_offset, target_length, target_bytes;
    uint64_t scratch_token, scratch_offset; /* source of the 112-page image */
    uint64_t exec_mask;                     /* executable scratch pages */
    struct tegu_generic_job_span spans[TEGU_GENERIC_JOB_MAX_SPANS];
    uint64_t commands[TEGU_GENERIC_JOB_MAX_COMMANDS];
};

/* Header identical to TGJ1's; only the array capacities differ. */
struct tegu_generic_job2_request {
    uint32_t version, flags;
    uint32_t width, height, row_stride, span_count, command_count, reserved;
    uint64_t target_token, target_offset, target_length, target_bytes;
    uint64_t scratch_token, scratch_offset;
    uint64_t exec_mask;
    struct tegu_generic_job_span spans[TEGU_GENERIC_JOB2_MAX_SPANS];
    uint64_t commands[TEGU_GENERIC_JOB2_MAX_COMMANDS];
};

struct tegu_generic_job_plan {
    struct tegu_fs_surface_layout layout;
    uint32_t span_pages[TEGU_GENERIC_JOB2_MAX_SPANS];
    uint64_t span_va[TEGU_GENERIC_JOB2_MAX_SPANS];
    uint64_t scratch_va, target_va, heap_va;
    uint32_t resource_pages;
};

#ifdef __cplusplus
extern "C" {
#endif
/* 0 when the command is an allowlisted CSF instruction. */
int tegu_generic_job_command_allowed(uint64_t command);
/* Validates the request shape and computes the per-job GPU VA layout.
 * Leaves *out untouched on refusal. */
int tegu_generic_job_plan(const struct tegu_generic_job_request *request,
    struct tegu_generic_job_plan *out);
/* The same for TGJ2 (version 2, its span and command limits). */
int tegu_generic_job2_plan(const struct tegu_generic_job2_request *request,
    struct tegu_generic_job_plan *out);
#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(struct tegu_generic_job_request) == 1560, "TGJ1 request ABI");
#else
_Static_assert(sizeof(struct tegu_generic_job_request) == 1560, "TGJ1 request ABI");
#endif
#if defined(__cplusplus)
static_assert(sizeof(struct tegu_generic_job2_request) == 4056, "TGJ2 request ABI");
#else
_Static_assert(sizeof(struct tegu_generic_job2_request) == 4056, "TGJ2 request ABI");
#endif
#endif
