/* Local-only transport for the reused Metal frontend. No CSF command reaches
 * Nord2: only arch9 JM jobs are submitted. See TEGU_PROVENANCE.json. */
#ifndef NORD2_METAL_BRIDGE_H
#define NORD2_METAL_BRIDGE_H
#include <stdint.h>
#include <stddef.h>
struct n2mb_ops {
    void *opaque;
    int (*call)(void *,unsigned,const uint64_t *,unsigned,const void *,size_t,
                uint64_t *,unsigned *,void *,size_t *);
    int (*map)(void *,unsigned,uint64_t *,uint64_t *);
    int (*unmap)(void *,unsigned,uint64_t);
    int (*close)(void *);
    void (*lock)(void *);
    void (*unlock)(void *);
    void (*destroy)(void *);
};
/* Serializes one device, not all clients. close requires the frontend's last
 * reference (no concurrent API calls), matching Metal device ownership. */
uintptr_t n2mb_open(const struct n2mb_ops *);
int n2mb_close(uintptr_t);
int n2mb_scalar(uintptr_t,unsigned,const uint64_t *,unsigned,uint64_t *,unsigned *);
int n2mb_method(uintptr_t,unsigned,const uint64_t *,unsigned,const void *,size_t,
                uint64_t *,unsigned *,void *,size_t *);
int n2mb_map(uintptr_t,unsigned,unsigned,uint64_t *,uint64_t *,unsigned);
int n2mb_unmap(uintptr_t,unsigned,unsigned,uint64_t);
struct tegu_generic_draw_shader;
struct tegu_generic_draw_request;
int n2mb_draw(uintptr_t,const struct tegu_generic_draw_shader *,const struct tegu_generic_draw_request *,uint64_t *,unsigned *);
/* Additive v2 texture/sampler sidecar for render. Pass NULL to preserve the
 * old n2mb_draw bytes; the sidecar is validated before any descriptor write. */
struct n2r_texture_resources;
int n2mb_draw_v2(uintptr_t,const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 uint64_t *,unsigned *);
/* Additive v3 draw options. Preserves v1/v2 when opts is NULL; v3 requires
 * both the texture sidecar and a validated n2r_draw_options. */
struct n2r_draw_options;
int n2mb_draw_v3(uintptr_t,const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 const struct n2r_draw_options *,uint64_t *,unsigned *);
/* Additive true-MRT bridge sidecar.  NULL keeps v1/v2/v3 bytes exactly.
 * Targets are user BO handles plus allocation-relative spans; the transport
 * resolves the address and retains every target for the submitted job.  The
 * bounded path is 1..4 bound colour attachments; unbound RT0/RT1 with only
 * RT2 written must use the existing per-RT one-target draw path instead of a
 * fabricated target.  `length` is the available bytes from `offset` (the same
 * meaning as q->target_length).  Version1 keeps the shared shader blend with
 * the target write mask overlaid.  Version2 adds per-target blend inputs,
 * passed unchanged to n2r's BLEND descriptors; the caller owns the array for
 * the draw call.  Version2 requires blends != NULL and matching write masks.
 */
#define N2MB_RENDER_TARGETS_VERSION 1u
#define N2MB_RENDER_TARGETS_BLEND_VERSION 2u
#define N2MB_RENDER_TARGETS_MSAA_VERSION 3u
#define N2MB_MAX_RENDER_TARGETS 4u
#define N2MB_TARGET_DISCARD 1u
struct tegu_blend_inputs;
struct n2mb_render_target {
    uint64_t token, offset, length;
    uint32_t format;            /* Metal format; 0 means legacy BGRA8 */
    uint32_t load_action;       /* 0 DontCare, 1 Load, 2 Clear */
    uint32_t write_mask;        /* Metal colour write mask (0..15) */
    uint32_t reserved;          /* bit0 N2MB_TARGET_DISCARD: Metal store DontCare (no writeback) */
    uint32_t row_bytes, width, height;
    uint32_t clear_words[4];
};
/* Version 3 resolve transport.  The bridge resolves the caller-owned token
 * through its own BO table; no raw GPU address is accepted.  token==0 means
 * this attachment has no resolve; otherwise offset/length describe the
 * resolve allocation span and the target/sample rules below apply. */
struct n2mb_resolve_target {
    uint64_t token, offset, length;
    uint32_t format;            /* 0 means the attachment's target format */
    uint32_t row_bytes, width, height, reserved;
};
struct n2mb_render_targets {
    uint32_t version, count;
    const struct n2mb_render_target *targets;
    /* Version 2/3: count per-target blend inputs, pointer owned by the
     * caller for the lifetime of this draw call.  Version 1 ignores it.
     * Version 3 may leave it NULL to keep the shared shader blend. */
    const struct tegu_blend_inputs *blends;
    /* Version 3 only: count sample counts (all equal, 1 or 4) and count
     * resolve records (NULL means no resolves).  The caller owns both arrays
     * for the duration of the draw; the bridge copies them into its local
     * n2r sidecar before returning. */
    const uint32_t *sample_counts;
    const struct n2mb_resolve_target *resolves;
};
int n2mb_draw_v4(uintptr_t,const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 const struct n2r_draw_options *,const struct n2mb_render_targets *,
 uint64_t *,unsigned *);
/* One render pass segment as ONE JM job: the draws' MALLOC_VERTEX tiler jobs
 * are chained (preload first when draw 0 LOADs) into a single frame whose one
 * fragment job covers the target. Every draw targets the same single-target
 * request (token/offset/length/format/size/row), draws 1.. continue the pass
 * (load_action 1), and none reads the target (no framebuffer fetch, no target
 * texture binding) nor needs thread storage, MRT or MSAA. resources/options
 * may be NULL per draw (legacy) or set (v2/v3). Refusals happen before any
 * GPU submission, so the caller may fall back to per-draw jobs. */
#define N2MB_BATCH_MAX 32u
/* Array capacity of one n2mb_draw_batch call. The draws actually chained are
 * bounded by the runtime cap n2mb_batch_capacity (default N2MB_BATCH_MAX,
 * NORD2_METAL_BATCH_MAX=2..128 at open), by the 32-handle submission and by
 * a summed geometry budget: sum (16+varying_bytes)*vertex_count of the
 * chained draws <= N2MB_BATCH_GEOMETRY, i.e. the worst case of 32 draws at
 * TEGU_GENERIC_DRAW_GEOMETRY_BUDGET each that the 2 MiB tiler heap already
 * carries. A larger cap is a device A/B: polygon-list heap use per chain is
 * not modelled on the host. */
#define N2MB_BATCH_CAPACITY 128u
#define N2MB_BATCH_GEOMETRY (32u*TEGU_GENERIC_DRAW_GEOMETRY_BUDGET)
struct n2mb_batch_draw {
    const struct tegu_generic_draw_shader *shader;
    const struct tegu_generic_draw_request *request;
    const struct n2r_texture_resources *resources;
    const struct n2r_draw_options *options;
};
int n2mb_draw_batch(uintptr_t,const struct n2mb_batch_draw *,unsigned,uint64_t *,unsigned *);
/* Additive true-MRT / MSAA batch: targets[k] is draw k's target set
 * (targets==NULL is exactly n2mb_draw_batch). Every member then carries the
 * SAME set (count, tokens, spans, formats, layout, version; version 3: equal
 * sample counts and identical resolve records), resources != NULL, no
 * TILE_FETCH program and no binding of a target or ATTACHMENT. Draw 0's load
 * actions own the job (Clear/DontCare; Load only single-sampled without
 * resolves, at most two targets, as pre-frame DCDs, which needs
 * n2mb_preframe 1); members 1.. Load (continue) every target. Write masks
 * and blends are per member (own BLEND array, render_target_mask). A 4x pass
 * whose draws do not fit one call cannot continue: a later 4x Load job is
 * refused before submission (the samples are never in memory). */
int n2mb_draw_batch_mrt(uintptr_t,const struct n2mb_batch_draw *,const struct n2mb_render_targets *const *targets,
 unsigned,uint64_t *,unsigned *);
/* Draws (a prefix of the request) the last successful batch submitted. */
unsigned n2mb_batch_last_count(uintptr_t);
/* Runtime draws-per-batch cap: cap 2..N2MB_BATCH_CAPACITY sets it, 0 only
 * queries; returns the previous cap. Callers size arrays by
 * N2MB_BATCH_CAPACITY and stop runs at this cap. */
unsigned n2mb_batch_capacity(uintptr_t,unsigned cap);
/* Kernel 0096 CPU-access tracking. n2mb_scalar(1) accepts {bytes, flags} with
 * flags bit1 = GPU-only: the kernel refuses CPU mappings of the buffer and
 * never cache-maintains it (n2mb_map returns BAD). The bridge itself
 * allocates the tiler heap (unless the geometry trace maps it), graphics
 * TLS and compute TLS/WLS GPU-only; the tiler matrix uses a separate mapped
 * heap. On a kernel without 0096 the flag is dropped (one refused attempt).
 * Imported IOSurfaces are acquired with SYNC before any bridge CPU read
 * (n2mb_copy source, feedback snapshot); callers bracket their own CPU access
 * with IOSurfaceLock/Unlock. Returns 1 GPU-only accepted, 2 unsupported by
 * the kernel, 0 not yet tried. */
unsigned n2mb_gpu_only_state(uintptr_t);
/* GPU VA and size of the client buffer with this frontend handle (0, or BAD for unknown/internal). */
int n2mb_buffer_va(uintptr_t,uint32_t handle,uint64_t *va,uint64_t *size);
/* LOAD-pass colour preload mode for single-target draws and batches.
 * 0: chained full-screen tiler preload job (historical); 1: pre-frame shader
 * DCD (FRAMEBUFFER_PARAMETERS pre_frame_0 INTERSECT, Mesa pan_fb_preload v9):
 * no preload job, and tiles without geometry are neither read nor written.
 * Default from NORD2_METAL_PREFRAME=0/1 at open, else NORD2_PREFRAME_DEFAULT
 * (0 unless defined). Returns the previous mode; mode>1 only queries. */
unsigned n2mb_preframe(uintptr_t,unsigned mode);
/* Fragment-job tile bounding (Mesa draw_extent, ansuz E3). 1: a draw's or a
 * batch's FRAGMENT job covers only the tiles of the union of its draws'
 * viewport/scissor boxes when no target Clears and no MSAA resolve exists;
 * other tiles are not loaded or written back (Load contents stay in memory).
 * 0: full target (historical). Default NORD2_METAL_EXTENT=0/1 at open, else
 * NORD2_EXTENT_DEFAULT (1). Returns the previous mode; mode>1 only queries. */
unsigned n2mb_extent(uintptr_t,unsigned mode);
/* Cumulative since open: [0] graphics jobs submitted, [1] their target tiles
 * (16x16), [2] tiles their FRAGMENT jobs cover (= [1] without bounding). */
void n2mb_tile_stats(uintptr_t,uint64_t out[3]);
/* Asynchronous graphics submission (kernel 0090 WAIT_SEQ). Mode 1: draws,
 * batches and clears return after SUBMIT_JM (reply = job id) without waiting;
 * the bridge keeps the newest id and reaps with one WAIT_SEQ. Every bridge
 * path that needs completed GPU work waits first (snapshot copy, n2mb_copy,
 * n2mb_method compute/pipeline, tiler matrix, traces, close). The caller
 * MUST call n2mb_wait_pending before CPU access to GPU-written memory
 * (CPU blits, shadow sync, readback) and before reporting a command buffer
 * complete; a failure there is the GPU failure of an earlier draw
 * (failed_job = first failing job id) and breaks the bridge like a failed
 * synchronous wait. Enabling probes WAIT_SEQ and stays 0 on an older kernel.
 * NORD2_METAL_ASYNC=1 requests it at open. Returns the previous mode (2 =
 * requested, not yet probed); mode>1 only queries. Mode 0 reaps first. */
unsigned n2mb_async(uintptr_t,unsigned mode);
int n2mb_wait_pending(uintptr_t,uint64_t *failed_job);
/* Wait for (and reap) the jobs with id <= seq (the reply ids of draws,
 * batches and clears) without holding the bridge lock; other threads may keep
 * submitting. 0 when async is off or nothing is queued. */
int n2mb_wait_seq(uintptr_t,uint64_t seq,uint64_t *failed_job);
int n2mb_copy(uintptr_t,uint64_t,uint64_t,uint64_t,uint64_t);
/* Chained submission (NORD2_METAL_CHAIN=1|2, default off; A/B toggle).
 * Between n2mb_chain_begin and n2mb_chain_end the frames of draws, batches
 * and clears that qualify are not submitted one by one: each is linked into
 * one kernel job (every frame's tiler jobs on the JS1 chain, its fragment job
 * on the JS0 chain behind a barrier and a dependency on the previous one;
 * every frame on its own tiler-heap slice). Their reply ids are 0 (no kernel
 * job yet); n2mb_chain_end submits what is open and returns that job's id
 * (0 when nothing was chained) - the command's completion id. Every other
 * bridge call that needs ordering or CPU access (waits, copies, maps,
 * scalar/method selectors, the release of a chained buffer, a frame that
 * cannot join) submits the open chain first, so ids stay ordered.
 * Mode 1 also chains a frame that reads what an earlier frame of the chain
 * wrote (a blur level sampling the previous one) when its vertex stage reads
 * none of it; mode 2 only independent frames. At the first begin
 * (NORD2_METAL_CHAIN_PROBE, default 1) a 4-frame probe checks on the GPU that
 * a fragment job sees the previous one's writes inside one chain (texture
 * cache); otherwise mode 1 falls back to 2. NORD2_METAL_CHAIN_FRAMES (2..8,
 * default 4) frames per job, each with a full tiler-heap slice. */
#define N2MB_HAVE_CHAIN 1 /* compute_frontend.m guards its n2mb_chain_begin/end hook on this */
#define N2MB_CHAIN_MAX_FRAMES 8u
/* tegu_generic_draw_binding.reserved: the encoder knows each binding's stage
 * (compiled stage_mask). STAGED marks the information present, VERTEX that
 * the vertex stage reads it (an unstaged binding counts as vertex-read). */
#define N2MB_BINDING_STAGED 1u
#define N2MB_BINDING_VERTEX 2u
unsigned n2mb_chain_begin(uintptr_t);
int n2mb_chain_end(uintptr_t,uint64_t *job);
/* Set the mode (0 off, 1, 2) and frames per job (0: keep), skipping the
 * probe; returns the previous mode. Tests and A/B. */
unsigned n2mb_chain_mode(uintptr_t,unsigned mode,unsigned frames);
/* [0] chains submitted [1] frames in them [2] dependent links [3] flush:
 * dependency [4] flush: handles [5] flush: full [6] flush: other call or
 * frame [7] flush: end [8] most frames in one job [9] probe (0 not run,
 * 1 visible, 2 stale, 3 failed) [10] mode. */
#define N2MB_CHAIN_STATS 11u
void n2mb_chain_stats(uintptr_t,uint64_t out[N2MB_CHAIN_STATS]);
int n2mb_clear(uintptr_t,const struct tegu_generic_draw_request *,uint64_t *,unsigned *);
/* Diagnostic: bridge line of the last refused draw, and n2r's refusal line. */
int n2mb_last_failure(uintptr_t,int *);
unsigned n2mb_code_cache_count(uintptr_t);
/* Mapped BOs the bridge keeps for reuse: pooled command blocks plus the
 * clear-colour slab. */
unsigned n2mb_cmdpool_count(uintptr_t);
/* Cumulative command-pool hits (reused) and misses (fresh allocation). */
void n2mb_cmdpool_stats(uintptr_t,uint64_t *hits,uint64_t *misses);
/* Diagnostic: live private (incl. internal and code-cache) and imported bytes, BO count. */
void n2mb_memory_stats(uintptr_t,uint64_t *,uint64_t *,uint64_t *,uint64_t *,unsigned *);
unsigned n2mb_memory_top(uintptr_t,uint64_t *,unsigned,unsigned [3]);
/* Pure arch9 resource-table pointer encoder: address must be 64-byte aligned,
 * entries is a multiple of4 in [4,60], and address+entries*16 <= limit. The
 * low six bits carry the table count (pinned Mesa pan_cmdstream). Returns -1
 * on refusal and never guesses a count. */
int n2mb_compute_srt_pointer(uint64_t address,uint64_t limit,unsigned entries,uint64_t *out);
/* CPU address belongs to the IOSurface retained by the texture until release. */
int n2mb_import_surface(uintptr_t,uint32_t,uint64_t,uint64_t,uint64_t *);

#endif
