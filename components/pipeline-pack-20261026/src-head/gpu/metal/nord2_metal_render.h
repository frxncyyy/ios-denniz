/* Private Nord2 arch9 backend for the reused Tegu Metal frontend. */
#ifndef NORD2_METAL_RENDER_H
#define NORD2_METAL_RENDER_H
#include "tegu/generic_job_ui.h"
#include "nord2_texture_resources.h"
#define N2R_BLOCK 16384u
#define N2R_JOB 1024u
#define N2R_FRAGMENT 1408u
/* Free command-block tail. 256 arch9 plane descriptors fit exactly; the old
 * single-plane builder never touches this region. */
#define N2R_PAYLOAD 8192u
#define N2R_PAYLOAD_BYTES (N2R_BLOCK - N2R_PAYLOAD)
/* All memory windows are resolved from retained handles by the transport. */
struct n2r_addresses {
 uint64_t command,frame,heap,heap_bytes,vertex_code,fragment_code,target,snapshot;
 uint64_t vertex,index,binding[TEGU_GENERIC_DRAW_MAX_BINDINGS];
 /* Tiler configuration. Zero keeps the historical packet: hierarchy 0x82,
  * 2 MiB chunks, IDVS varying shader enabled. heap_bytes is the descriptor
  * Size (the backing BO may be larger and is retained by the transport). */
 uint32_t hierarchy_mask,heap_chunk_enum1,no_secondary;
 /* Additive graphics TLS base (v3/v4).  Ignored when
  * n2r_draw_options.thread_storage_bytes is zero, so a LOAD preload may copy
  * the address struct without changing its TLS0 packet.  When the byte count
  * is nonzero this must be a 4096-aligned GPU address whose BO covers the
  * per-thread stack for every shader core.  The transport owns/retains it. */
 uint64_t tls;
};
/* Mesa pan_select_tiler_hierarchy_mask (arch<12, JM tile_size 0). */
uint32_t n2r_mesa_hierarchy_mask(uint32_t width,uint32_t height,uint32_t tiler_features,uint64_t heap_bytes);

/* Additive v2 texture/sampler sidecar.  The old n2r_build/n2r_addresses and
 * their default descriptor bytes are unchanged.  A v2 binding is keyed by the
 * request binding number; a binding without an entry keeps the old single-2D
 * level-0 path.  If an entry exists, it is authoritative: an invalid entry
 * refuses the draw and never falls back to the old descriptor.
 *
 * layout.level[i].offset and layer_stride are relative to the resolved
 * address of the request binding at its POSITION i:
 * n2r_addresses.binding[i].  Sparse binding numbers (e.g. 3 and 9) do not
 * index this array.  The bridge resolves each position from
 * q->bindings[i].memory.  Render must use mip[level].offset - mip[0].offset
 * for the level offsets and reject negative differences; it must not pass
 * BO-relative offsets.  backing_length bounds the spans from that base and
 * must be <= q->bindings[i].memory.bytes.  first_layer/first_level are
 * physical metadata; level offsets are already view-relative. */
#define N2R_TEXTURE_RESOURCES_VERSION 1u
struct n2r_texture_binding {
    uint32_t binding, kind;             /* matches request binding/kind */
    uint64_t backing_length;            /* bytes available from the base VA */
    struct n2t_texture_layout layout;   /* sampled images; zero for sampler-only */
    struct n2t_sampler_state sampler;   /* required for COMBINED/SAMPLER uses */
};
struct n2r_texture_resources {
    uint32_t version, count;
    const struct n2r_texture_binding *bindings;
};
int n2r_build_v2(uint8_t out[N2R_BLOCK],const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_addresses *,
 const struct n2r_texture_resources *,uint16_t index,uint64_t next);
int n2r_validate_v2(const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *);

/* Additive v3 draw-parameter options.  Default v1/v2 calls pass NULL and keep
 * byte-identical packets.  The caller supplies values from the compiled
 * plan/draw, never from a GPU address.
 *
 * FLAT_SHADING: selects the first provoking vertex in TILER_CONTEXT (arch9
 * word2 bit18).  Pinned v9.xml has no DRAW.flat_shading_vertex field; Mesa's
 * P<=7 line-DRAW variant is not present on arch9.  The line-specific legacy
 * bit is therefore not fabricated here; line-list flat behavior remains an
 * explicit hardware-contract gap in STATUS, while triangle flat uses the
 * arch9 tiler control.
 * WRITE_VERTEX_BASE: write signed vertexStart/baseVertex at vertex FAU byte64.
 *   q->vertex must already be the attribute base adjusted for that start, as
 *   the current render snapshot does; n2r never adds vertex_base again.
 * WRITE_BASE_INSTANCE: write uint32 baseInstance at vertex FAU byte68 and add
 *   baseInstance*stride to each input_rate ATTRIBUTE row.  The caller must not
 *   pre-add that shift to q->vertex. */
#define N2R_DRAW_OPTIONS_VERSION 1u
#define N2R_DRAW_FLAT_SHADING 1u
#define N2R_DRAW_WRITE_VERTEX_BASE 2u
#define N2R_DRAW_WRITE_BASE_INSTANCE 4u
/* Graphics THREAD_STORAGE: max(vs->tls,fs->tls) bytes per thread from the
 * compiled generic pipeline.  Zero preserves every existing packet byte.
 * A nonzero value requires n2r_addresses.tls (4096-aligned) and binds one
 * LOCAL_STORAGE descriptor for the IDVS vertex/secondary and fragment jobs.
 * The transport sizes the BO as next_pow2(align16(bytes)) * 2048 * cores,
 * matching pinned Mesa pan_get_total_stack_size with the conservative arch9
 * thread bound used by the compute bridge. */
#define N2R_THREAD_STORAGE_MAX 32768u
/* Mesa pan_get_stack_shift / pan_get_total_stack_size used by the transport.
 * total = next_pow2(align16(bytes)) * threads_per_core * cores, with overflow
 * and compiler-immediate checks.  bytes=0 yields shift0/total0. */
uint32_t n2r_thread_storage_shift(uint32_t bytes);
int n2r_thread_storage_total(uint32_t bytes,uint32_t threads_per_core,
 uint32_t cores,uint64_t *out);
struct n2r_draw_options {
    uint32_t version, flags;
    int32_t vertex_base;       /* signed vertexStart/baseVertex */
    uint32_t base_instance;
    uint32_t thread_storage_bytes; /* additive; zero is the old packet */
    uint32_t thread_storage_reserved; /* must be zero */
};
int n2r_validate_v3(const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 const struct n2r_draw_options *);
int n2r_build_v3(uint8_t out[N2R_BLOCK],const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_addresses *,
 const struct n2r_texture_resources *,const struct n2r_draw_options *,
 uint16_t index,uint64_t next);

/* Additive true-MRT sidecar (v4).  NULL keeps v1/v2/v3 bytes exactly.  The
 * bounded path admits 1..4 bound colour attachments with the same width,
 * height and sample count; formats use the existing n2r_bpp contract
 * (BGRA8/RGBA8/R8/RGBA16F).  `write_mask` is the Metal colour write mask for
 * this attachment; zero means the attachment is loaded/stored but not shaded
 * (it is excluded from DRAW.render_target_mask).  `clear_words` is packed as
 * by n2r_clear.  Addresses must be resolved/retained by the transport.
 * `sample_locations` must be a 256-byte GPU span when count>2; zero keeps the
 * historical m+256 location for one/two targets.
 *
 * Version 1 preserves the original shared `shader->blend` (with each target's
 * write_mask overlaid).  Version 2 adds an optional per-target blend array:
 * `blends` must point at `count` tegu_blend_inputs records; each record's
 * write_mask must equal the target's write_mask, and n2r builds the BLEND
 * descriptor at m+928+16*i from it.  Version 1 callers may leave `blends`
 * uninitialized because it is never read.  Version 2 with a NULL array is an
 * explicit refusal, never a fallback to the shared state. */
#define N2R_RENDER_TARGETS_VERSION 1u
#define N2R_RENDER_TARGETS_BLEND_VERSION 2u
#define N2R_RENDER_TARGETS_MSAA_VERSION 3u
#define N2R_MAX_RENDER_TARGETS 4u
struct n2r_render_target {
    uint32_t format;            /* Metal pixel format; 0 means legacy BGRA8 */
    uint32_t load_action;       /* 0 DontCare, 1 Load, 2 Clear */
    uint32_t write_mask;        /* Metal color write mask (0..15) */
    uint32_t reserved;
    uint64_t address;
    uint32_t row_bytes;
    uint32_t width, height;
    uint32_t clear_words[4];
};
struct n2r_resolve_target {
    uint64_t address;           /* 0 = no resolve for this attachment */
    uint32_t format;            /* 0 = target format; exact match required */
    uint32_t row_bytes;
    uint32_t width, height;
    uint32_t reserved;
};
struct n2r_render_targets {
    uint32_t version, count;
    uint64_t sample_locations;
    const struct n2r_render_target *targets;
    /* Version 2/3: count per-target blend inputs, one per targets[i]. */
    const struct tegu_blend_inputs *blends;
    /* Version 3 only: count sample counts (1 or 4, all equal) and count
     * resolve descriptors (NULL = no resolves everywhere). */
    const uint32_t *sample_counts;
    const struct n2r_resolve_target *resolves;
};
int n2r_validate_v4(const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_texture_resources *,
 const struct n2r_draw_options *,const struct n2r_render_targets *);
int n2r_build_v4(uint8_t out[N2R_BLOCK],const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_addresses *,
 const struct n2r_texture_resources *,const struct n2r_draw_options *,
 const struct n2r_render_targets *,uint16_t index,uint64_t next);
/* Metal formats used by QuartzCore's ordinary and blur targets. */
static inline unsigned n2r_bpp(unsigned f){
 switch(f){
 case 10:return 1;                 /* R8 */
 case 25:return 2;                 /* R16F */
 case 30:return 2;                 /* RG8 */
 case 43:return 2;                 /* BGR5A1Unorm = PIPE B5G5R5A1_UNORM */
 case 55:return 4;                 /* R32F */
 case 65:return 4;                 /* RG16F */
 case 0:case 70:case 80:case 90:case 94:return 4;
 case 110:case 115:return 8;        /* RGBA16Unorm / RGBA16F */
 case 125:return 16;                /* RGBA32F */
 default:return 0;
 }
}
/* Tile-buffer bytes per pixel (Mesa 81daea66 pan_format_tib_size): blendable
 * UNORM targets always occupy 32 bits in the tile buffer; float and raw
 * targets use their power-of-two memory size. Memory bytes are n2r_bpp. */
static inline unsigned n2r_tib_bytes(unsigned f){
 switch(f){
 case 0:case 10:case 43:case 70:case 80:case 90:case 94:return 4;
 default:{unsigned b=n2r_bpp(f);return b==3?4:b;}
 }
}
int n2r_clear(unsigned format,const double rgba[4],uint32_t out[4]);
/* Source line of the most recent n2r validation/build refusal (0 = none). */
int n2r_last_reject_line(void);
int n2r_validate(const struct tegu_generic_draw_shader *,const struct tegu_generic_draw_request *);
int n2r_build(uint8_t out[N2R_BLOCK],const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_addresses *,uint16_t index,uint64_t next);
#endif
