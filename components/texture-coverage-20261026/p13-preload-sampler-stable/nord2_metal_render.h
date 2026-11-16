/* Private Nord2 arch9 backend for the reused Tegu Metal frontend. */
#ifndef NORD2_METAL_RENDER_H
#define NORD2_METAL_RENDER_H
#include "tegu/generic_job_ui.h"
#include "nord2_texture_resources.h"
#define N2R_BLOCK 16384u
#define N2R_JOB 1024u
/* Colour tile-buffer bytes one 16x16 tile may use (Mesa pan_query_tib_size,
 * Valhall v9 16384). Larger MRT/MSAA allocations are refused. */
#define N2R_TIB_BUDGET 16384u
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
 /* Additive pre-frame shader array (FRAMEBUFFER_PARAMETERS.frame_shader_dcds,
  * three 128-byte DRAW descriptors, 64-byte aligned).  Zero keeps every
  * existing packet byte.  Nonzero selects pre_frame_0 = INTERSECT, or ALWAYS
  * when any target forces clean tile writes (Mesa pan_fix_frame_shader_mode,
  * TSIX-2033); pre_frame_1/post_frame stay NEVER.  Built by
  * n2r_build_frame_preload; the transport retains the block. */
 uint64_t frame_shader_dcds;
 /* Additive fragment-job tile bound (ansuz E3 = Mesa pan_job.c draw_extent:
  * union of the draws' viewport/scissor boxes, inclusive pixels minx,miny,
  * maxx,maxy, from n2r_draw_extent).  extent_valid==0 keeps the full-target
  * FRAGMENT_JOB bound.  Honoured only when no target Clears (Clear must
  * write every tile, as Mesa's clear unions the whole framebuffer) and no
  * MSAA resolve exists; tiles outside are then neither preloaded nor written
  * back, so memory keeps the Load contents (DontCare: undefined either way).
  * FRAMEBUFFER_PARAMETERS bounds stay the full target (Mesa
  * frame_bounding_box).  Only the frame-owner block's value is used. */
 uint32_t extent_valid,extent[4];
 /* Additive: which DRAW entries of frame_shader_dcds are pre-frame shaders
  * (bit0 pre_frame_0, bit1 pre_frame_1; both get the same INTERSECT/ALWAYS
  * mode).  0 keeps the historical bit0 only.  Needs frame_shader_dcds. */
 uint32_t frame_shader_mask;
};
/* Inclusive pixel box a draw can touch (viewport ∩ scissor ∩ target), the
 * same box its tiler scissor uses. Returns 0 when the draw is fully clipped. */
int n2r_draw_extent(const struct tegu_generic_draw_request *,uint32_t box[4]);
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
/* Face culling (Metal setCullMode/setFrontFacingWinding): DRAW flags_0
 * cull_front_face / cull_back_face / front_face_ccw (Mesa v9, panvk
 * front_face_ccw = CCW front). The winding is the framebuffer-image
 * orientation, as Vulkan's (MoltenVK maps MTLWinding to VkFrontFace 1:1).
 * Triangles only: lines are never culled (the bits are not packed). */
#define N2R_DRAW_CULL_FRONT 8u
#define N2R_DRAW_CULL_BACK 16u
#define N2R_DRAW_FRONT_CCW 32u
/* Metal setBlendColor: blend_constant[] is valid (else Metal's default
 * 0,0,0,0 is used whenever an equation reads the blend colour). */
#define N2R_DRAW_BLEND_CONSTANT 64u
/* Metal setDepthClipMode Clamp: DEPTH_STENCIL depth_cull_enable=0 and
 * depth_clamp_mode=BOUNDS (Mesa pan_cmdstream v9 depth_clamp); default
 * (Clip) keeps the historical cull + 0_1 clamp. */
#define N2R_DRAW_DEPTH_CLAMP 128u
/* In-pass depth/stencil (n2r_draw_options.zs): the pass's depth/stencil
 * lives only in the tile buffer (Metal store DontCare, the only class the
 * pass admits): no ZS writeback, no preload. zs->load 0 DontCare / 2 Clear
 * starts a frame; 1 continues an earlier job of the SAME frame (the
 * transport refuses it as a frame owner). Every job of a pass with a depth
 * attachment carries the pass state (format, clears) so the frame owner's
 * FRAMEBUFFER_PARAMETERS z/s clear is the pass's; depth_compare 7 with no
 * write and stencil off is the historical no-test state. */
#define N2R_DRAW_DEPTH_STENCIL 256u
#define N2R_DEPTH_STENCIL_VERSION 1u
struct n2r_stencil_face {
    uint32_t compare, fail, depth_fail, pass;   /* Metal MTLCompareFunction / MTLStencilOperation */
    uint32_t read_mask, write_mask, reference;
};
struct n2r_depth_stencil {
    uint32_t version, format;  /* Metal 250 D16, 252 D32F, 253 S8, 255 D24S8, 260 D32FS8 */
    uint32_t load;             /* 0 DontCare, 2 Clear, 1 continue (batch member) */
    uint32_t depth_compare, depth_write, stencil_enable;
    float clear_depth; uint32_t clear_stencil;
    struct n2r_stencil_face front, back;
    float bias_units, bias_slope, bias_clamp;
};
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
    float blend_constant[4];   /* RGBA, with N2R_DRAW_BLEND_CONSTANT */
    const struct n2r_depth_stencil *zs; /* with N2R_DRAW_DEPTH_STENCIL; borrowed */
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
/* Raw colour targets (Mesa get_rt_formats RAW path: formats without a
 * blendable tile format): normalized 16-bit, snorm, integer and 32-bit float
 * formats. They take opaque output only (no blending, full write mask, no
 * multisample resolve); the blend conversion's memory format stores the
 * shader's registers as the format's own bits. */
static inline unsigned n2r_rt_is_raw(unsigned f){
 switch(f){
 case 12:case 13:case 14:case 20:case 22:case 23:case 24:case 32:case 33:case 34:
 case 53:case 54:case 55:case 60:case 62:case 63:case 64:case 72:case 73:case 74:case 91:
 case 103:case 104:case 105:case 110:case 112:case 113:case 114:case 123:case 124:case 125:return 1;
 default:return 0;
 }
}
/* The unsigned-integer twin of a raw target format: the same channel widths
 * and bytes, read through it (uint preload shaders, texelFetch) and stored
 * back through its blend conversion, a LOAD or clear preload copies the
 * target's bits exactly whatever its class (integer, snorm, 16-bit unorm,
 * float32). 0 for non-raw formats (they use the float preload). */
static inline unsigned n2r_rt_raw_twin(unsigned f){
 switch(f){
 case 12:case 13:case 14:return 13;      /* R8 Snorm/Uint/Sint -> R8Uint */
 case 20:case 22:case 23:case 24:return 23; /* R16 -> R16Uint */
 case 32:case 33:case 34:return 33;      /* RG8 -> RG8Uint */
 case 53:case 54:case 55:return 53;      /* R32 -> R32Uint */
 case 60:case 62:case 63:case 64:return 63; /* RG16 -> RG16Uint */
 case 72:case 73:case 74:return 73;      /* RGBA8 -> RGBA8Uint */
 case 91:return 91;                      /* RGB10A2Uint */
 case 103:case 104:case 105:return 103;  /* RG32 -> RG32Uint */
 case 110:case 112:case 113:case 114:return 113; /* RGBA16 -> RGBA16Uint */
 case 123:case 124:case 125:return 123;  /* RGBA32 -> RGBA32Uint */
 default:return 0;
 }
}
/* Whether a target's contents survive the LOAD/clear preload: blendable
 * formats through the float preload, raw formats through their uint twin. */
static inline unsigned n2r_rt_load_exact(unsigned f){
 return !n2r_rt_is_raw(f)||n2r_rt_raw_twin(f)!=0;
}
static inline unsigned n2r_bpp(unsigned f){
 switch(f){
 case 1:case 10:case 11:case 12:case 13:case 14:return 1;  /* A8, R8(+sRGB/Snorm/Uint/Sint) */
 case 25:return 2;                 /* R16F */
 case 20:case 22:case 23:case 24:return 2;  /* R16 Unorm/Snorm/Uint/Sint */
 case 30:case 31:case 32:case 33:case 34:return 2;  /* RG8 (+sRGB/Snorm/Uint/Sint) */
 case 43:return 2;                 /* BGR5A1Unorm = PIPE B5G5R5A1_UNORM */
 case 53:case 54:case 55:return 4; /* R32 Uint/Sint/Float */
 case 60:case 62:case 63:case 64:return 4;  /* RG16 Unorm/Snorm/Uint/Sint */
 case 65:return 4;                 /* RG16F */
 case 40:case 42:return 2;          /* B5G6R5 / ABGR4 (PIPE A4B4G4R4) */
 case 0:case 70:case 71:case 72:case 73:case 74:case 80:case 81:case 90:case 91:case 92:case 94:return 4;
 case 103:case 104:case 105:return 8; /* RG32 Uint/Sint/Float */
 case 110:case 112:case 113:case 114:case 115:return 8; /* RGBA16 Unorm/Snorm/Uint/Sint/Float */
 case 123:case 124:case 125:return 16; /* RGBA32 Uint/Sint/Float */
 default:return 0;
 }
}
/* Tile-buffer bytes per pixel (Mesa 81daea66 pan_format_tib_size): blendable
 * UNORM targets always occupy 32 bits in the tile buffer; float and raw
 * targets use their power-of-two memory size. Memory bytes are n2r_bpp. */
static inline unsigned n2r_tib_bytes(unsigned f){
 switch(f){
 case 0:case 1:case 10:case 11:case 30:case 31:case 40:case 42:case 43:case 70:case 71:case 80:case 81:case 90:case 94:return 4;
 default:{unsigned b=n2r_bpp(f);return b==3?4:b;}
 }
}
int n2r_clear(unsigned format,const double rgba[4],uint32_t out[4]);
/* Source line of the most recent n2r validation/build refusal (0 = none). */
int n2r_last_reject_line(void);
int n2r_validate(const struct tegu_generic_draw_shader *,const struct tegu_generic_draw_request *);
int n2r_build(uint8_t out[N2R_BLOCK],const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_addresses *,uint16_t index,uint64_t next);
/* Pre-frame colour preload (Mesa 81daea66 pan_fb_preload.c v9 DCD and panvk
 * frame shaders).  Builds the single-target LOAD preload exactly as
 * n2r_build would (same resources, blend, depth/stencil, SPD and FAU at
 * a->command), but emits no job: the block holds the 3-entry DRAW array at
 * a->command+N2R_FRAME_DCDS.  DCD0 is the colour preload (clean fragment
 * write, render target 0, one blend), DCD1/DCD2 are zero.  Only the fragment
 * program is used and a->vertex_code may be zero.  The fragment shader must
 * address the target by pixel position (texelFetch at gl_FragCoord): a frame
 * shader has no vertex stage or varyings.  Requires q->load_action==1, one
 * single-sampled target.  The owning draw passes
 * frame_shader_dcds = a->command+N2R_FRAME_DCDS. */
#define N2R_FRAME_DCDS (N2R_JOB+128u)
int n2r_build_frame_preload(uint8_t out[N2R_BLOCK],const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_addresses *);
/* True-MRT LOAD: the pre-frame preload of ONE target `rt` of the draw's
 * target set (same rts as the owning n2r_build_v4, single-sampled, no
 * resolve, rts->targets[rt].load_action==1, its write_mask is ignored).
 * q describes the preload as for n2r_build_frame_preload, with binding 0 the
 * target's own image (q->target_format = its format).  The DCD at
 * N2R_FRAME_DCDS writes only that target: blend_count = rts->count,
 * render_target_mask = 1<<rt, and FAU blend word 0 is that target's internal
 * blend word with fixed_function.rt = rt (Mesa pan_blend_get_internal_desc),
 * the same word the preload program's BLEND reads for colour 0.  Pass blends
 * in rts whose entry rt is a plain replace with mask 15 (version 1 uses the
 * shader blend and the target write masks: set mask 15 at rt, 0 elsewhere).
 * The transport places up to two such DCDs as DRAW 0/1 of one array and sets
 * n2r_addresses.frame_shader_mask accordingly. */
int n2r_build_frame_preload_rt(uint8_t out[N2R_BLOCK],const struct tegu_generic_draw_shader *,
 const struct tegu_generic_draw_request *,const struct n2r_addresses *,
 const struct n2r_render_targets *,uint32_t rt);
#endif
