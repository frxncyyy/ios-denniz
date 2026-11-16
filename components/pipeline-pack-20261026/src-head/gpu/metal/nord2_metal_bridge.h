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
struct tegu_blend_inputs;
struct n2mb_render_target {
    uint64_t token, offset, length;
    uint32_t format;            /* Metal format; 0 means legacy BGRA8 */
    uint32_t load_action;       /* 0 DontCare, 1 Load, 2 Clear */
    uint32_t write_mask;        /* Metal colour write mask (0..15) */
    uint32_t reserved;
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
struct n2mb_batch_draw {
    const struct tegu_generic_draw_shader *shader;
    const struct tegu_generic_draw_request *request;
    const struct n2r_texture_resources *resources;
    const struct n2r_draw_options *options;
};
int n2mb_draw_batch(uintptr_t,const struct n2mb_batch_draw *,unsigned,uint64_t *,unsigned *);
/* Draws (a prefix of the request) the last successful batch submitted. */
unsigned n2mb_batch_last_count(uintptr_t);
int n2mb_copy(uintptr_t,uint64_t,uint64_t,uint64_t,uint64_t);
int n2mb_clear(uintptr_t,const struct tegu_generic_draw_request *,uint64_t *,unsigned *);
/* Diagnostic: bridge line of the last refused draw, and n2r's refusal line. */
int n2mb_last_failure(uintptr_t,int *);
unsigned n2mb_code_cache_count(uintptr_t);
/* Diagnostic: live private (incl. internal and code-cache) and imported bytes, BO count. */
void n2mb_memory_stats(uintptr_t,uint64_t *,uint64_t *,uint64_t *,uint64_t *,unsigned *);
/* Pure arch9 resource-table pointer encoder: address must be 64-byte aligned,
 * entries is a multiple of4 in [4,60], and address+entries*16 <= limit. The
 * low six bits carry the table count (pinned Mesa pan_cmdstream). Returns -1
 * on refusal and never guesses a count. */
int n2mb_compute_srt_pointer(uint64_t address,uint64_t limit,unsigned entries,uint64_t *out);
/* CPU address belongs to the IOSurface retained by the texture until release. */
int n2mb_import_surface(uintptr_t,uint32_t,uint64_t,uint64_t,uint64_t *);

#endif
