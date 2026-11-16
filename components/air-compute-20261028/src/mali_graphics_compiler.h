#ifndef TEGU_MALI_GRAPHICS_COMPILER_H
#define TEGU_MALI_GRAPHICS_COMPILER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The legacy single-stage result ABI is preserved. Paired entry points export
 * linked generic-varying layout and IDVS program offsets, without GPU addresses.
 * Caller must bind every required descriptor/program/push field and retain all
 * backing storage. Compiler success alone is not permission for native draw.
 *
 * Initial callable graphics compiler for trusted validated SPIR-V. Caller
 * serializes compilation. Backend assertions remain possible for unsupported
 * IR: this is not an untrusted shader sandbox. No filesystem or GPU operations.
 * Outputs must be zero-initialized; success owns code independently of input.
 * Single-stage scope: vertex inputs/position and one fragment color, no
 * resources or user varyings. Render/AIR metadata must ALSO be validated by
 * the pipeline.
 */
enum tegu_graphics_stage { TEGU_GRAPHICS_VERTEX=1, TEGU_GRAPHICS_FRAGMENT=2 };
enum tegu_graphics_fau_offset {
 TEGU_GRAPHICS_VIEWPORT_SCALE=0, TEGU_GRAPHICS_VIEWPORT_OFFSET=16,
 TEGU_GRAPHICS_OUTPUT_FLAGS=32, TEGU_GRAPHICS_BLEND_DESCRIPTOR=48,
 TEGU_GRAPHICS_AIR_PUSH=128, TEGU_GRAPHICS_AIR_PUSH_LIMIT=128,
};
/* sysval_mask bit0=viewportscale, bit1=offset, bit2=outputflags, bit3=blend,
 * bit4=vertex base, bit5=base instance (options entry with draw parameters only).
 * fau_words is a count of 32bit words; CSF pointer count uses ceil(words/2).
 * push_bytes is required AIR push span before adding AIR_PUSH offset.
 * Values carry no GPU address; bind per draw with correct ownership/barriers.
 */
struct tegu_mali_graphics {
 unsigned char *code;
 size_t size;
 uint32_t stage, registers, tls, fau_words, sysval_mask, push_bytes;
 uint64_t preload;
};
int tegu_mali_compile_graphics(const void *,size_t,uint32_t,
 struct tegu_mali_graphics *,char *,size_t);
/* Extended vertex-input compiler. Returns the exact hardware attribute-slot
 * mask separately, preserving the original result struct ABI. Vertex inputs
 * use resource table0 entries at their SPIR-V locations (0..30). Caller must
 * bind matching ATTRIBUTE and BUFFER descriptors for every requested slot.
 * Fragment varyings and shader buffer/texture resources remain unsupported.
 * Mask is zero on refusal. This is code generation, not native execution. */
int tegu_mali_compile_graphics_with_vertex_inputs(const void *,size_t,uint32_t,
 struct tegu_mali_graphics *,uint32_t *,char *,size_t);
void tegu_mali_graphics_free(struct tegu_mali_graphics *);

/* Paired VS+FS compile with generic varyings linked through ONE layout built
 * from the VS (mirrors panvk compile_shaders: FS compiled against the VS
 * pan_varying_layout). vs->code holds the IDVS position program at offset0
 * followed by the varying (secondary) program at secondary_offset; vs->
 * registers/preload describe the position program only. vs->sysval_mask
 * bit2 is never set: the output-flags sysval is specialised per IDVS variant
 * by the backend, so no 0xa20 FAU word may be relied on for this pair.
 *
 * Varying scope: 1..TEGU_MALI_PAIR_POLICY_VARYINGS user locations, each a
 * 32bit float scalar/vector, smooth (perspective) interpolation, no centroid/
 * sample/flat/noperspective/component/mediump. VS output set must equal FS
 * input set with identical component counts. No PSIZ, no FS builtin inputs,
 * no shader resources. FS must read varyings with LD_VAR_BUF (no varying
 * attribute descriptors): fs_uses_ld_var is checked and always 0 on success.
 *
 * Hardware binding contract (float4 path verified in standalone GPU tests): varying SPD
 * stage vertex, warp limit Full, allocation from secondary_registers, preload
 * r48..r63 = secondary_preload>>48, binary = vs code + secondary_offset; IDVS
 * VARY_SIZE = generic_size_B; RUN_IDVS FlagsOverride Secondary Shader (bit18) set.
 * FS offsets in varyings[] are bytes into the per-vertex generic section.
 * All outputs zero on refusal; pair->version must be 0 on entry. */
#define TEGU_MALI_GRAPHICS_PAIR_VERSION 1u
#define TEGU_MALI_PAIR_MAX_VARYINGS 4
#define TEGU_MALI_PAIR_POLICY_VARYINGS TEGU_MALI_PAIR_MAX_VARYINGS
struct tegu_mali_varying_slot {
 uint32_t location;   /* SPIR-V Location (VARYING_SLOT_VAR0 relative) */
 uint32_t offset;     /* byte offset in generic section */
 uint32_t components; /* 1..4 */
 uint32_t alu_type;   /* nir_alu_type, 160 = float32 */
};
struct tegu_mali_graphics_pair {
 uint32_t version;
 uint32_t position_size;      /* == secondary_offset */
 uint32_t secondary_offset;   /* 128-byte aligned, nonzero, < vs->size */
 uint32_t secondary_size;
 uint32_t secondary_registers;
 uint32_t generic_size_B;     /* VARY_SIZE: exact per-vertex generic bytes */
 uint64_t secondary_preload;
 uint32_t vs_attribute_mask;  /* hardware attribute slots, as single-stage */
 uint32_t vs_varying_mask;    /* bit n: Location n written by VS */
 uint32_t fs_varying_mask;    /* bit n: Location n read by FS */
 uint32_t fs_uses_ld_var;     /* 0 on success */
 uint32_t noperspective_mask; /* 0 on success */
 uint32_t varying_count;
 struct tegu_mali_varying_slot varyings[TEGU_MALI_PAIR_MAX_VARYINGS];
};
int tegu_mali_compile_graphics_pair(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,
 struct tegu_mali_graphics_pair *,char *,size_t);

/* Version2 pair: identical scope and refusals to version1, except that the VS
 * may also write BuiltIn PointSize (the AIR translator always stores 1.0).
 * Mesa then emits three programs in vs->code, all 128-byte aligned:
 *   [0, point_position_size)                     PSIZ-writing position program
 *   [position_offset, secondary_offset)          same program, PSIZ store NOPed
 *   [secondary_offset, vs->size)                 varying program
 * position_offset/position_size always describe the program to use for the
 * qualified triangle draw (panvk pos_triangles: no_psiz_offset when PSIZ is
 * written, else 0). The PSIZ variant is kept for a future point-list draw
 * only; it needs an extended-FIFO position buffer (point_size_* below) that no
 * current caller provides. Both position programs share vs->registers and
 * vs->preload. Without PSIZ, flags=0, position_offset=0 and point_* are 0, so
 * the code and fields match version1 exactly. needs_extended_fifo (Mesa's
 * "extended FIFO even with PSIZ patched out") is always 0 on success; any
 * other builtin output is still refused. All outputs zero on refusal;
 * pair->version must be 0 on entry. */
#define TEGU_MALI_GRAPHICS_PAIR2_VERSION 2u
#define TEGU_MALI_PAIR2_WRITES_POINT_SIZE 1u
struct tegu_mali_graphics_pair2 {
 uint32_t version;              /* 2 */
 uint32_t flags;                /* TEGU_MALI_PAIR2_WRITES_POINT_SIZE */
 uint32_t position_offset;      /* triangle position program, 128-aligned */
 uint32_t position_size;        /* secondary_offset - position_offset */
 uint32_t point_position_offset;/* 0 (PSIZ program); valid only with flag */
 uint32_t point_position_size;  /* == Mesa no_psiz_offset with flag, else 0 */
 uint32_t secondary_offset;     /* 128-aligned, > position_offset, < vs->size */
 uint32_t secondary_size;
 uint32_t secondary_registers;
 uint32_t generic_size_B;       /* VARY_SIZE: exact per-vertex generic bytes */
 uint64_t secondary_preload;
 uint32_t vs_attribute_mask;
 uint32_t vs_varying_mask;
 uint32_t fs_varying_mask;
 uint32_t fs_uses_ld_var;       /* 0 on success */
 uint32_t noperspective_mask;   /* 0 on success */
 uint32_t needs_extended_fifo;  /* 0 on success */
 uint32_t point_size_section;   /* pan_varying_section (1=ATTRIBS) with flag */
 uint32_t point_size_offset;    /* byte offset in that section */
 uint32_t point_size_alu_type;  /* nir_alu_type (144=float16) with flag */
 uint32_t varying_count;
 struct tegu_mali_varying_slot varyings[TEGU_MALI_PAIR_MAX_VARYINGS];
};
int tegu_mali_compile_graphics_pair2(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,
 struct tegu_mali_graphics_pair2 *,char *,size_t);

/* Textured pair: pair2 scope and outputs, plus exactly ONE fragment-stage
 * combined image sampler (SPIR-V sampler2D, float, non-array, non-shadow,
 * single-sampled) at DescriptorSet0 Binding0, read only by one explicit-LOD
 * sample with constant LOD 0.0 and no offset/bias/grad/min-LOD/compare.
 * Specialization constants keep their defaults: the AIR SamplerCoordinates
 * SpecId0 default0 selects normalized coordinates, and the dead
 * ImageQuerySizeLod must fold away; any surviving query or other texture
 * operation is refused. The VS stays resource free.
 *
 * Resource binding contract (copies the hardware-qualified texture-r1 probe):
 * fragment resource table plan->table (0), SAMPLER descriptor at
 * sampler_index (0) and TEXTURE descriptor at texture_index (1); TEX_SINGLE
 * uses the packed narrow handle 0x10000. The fragment SRT is separate from
 * the vertex SRT. Caller must bind a sampler whose normalized-coordinate state
 * matches normalized_coordinates. No GPU addresses are reported. Compiler
 * success is not permission to submit a textured draw.
 * All outputs zero on refusal; pair->version and plan->version must be 0 on
 * entry; outputs and inputs must not overlap. */
#define TEGU_MALI_TEXTURE_PLAN_VERSION 1u
struct tegu_mali_texture_plan {
 uint32_t version;                /* 1 */
 uint32_t table;                  /* fragment resource table, 0 */
 uint32_t texture_index;          /* TEXTURE descriptor entry, 1 */
 uint32_t sampler_index;          /* SAMPLER descriptor entry, 0 */
 uint32_t normalized_coordinates; /* 1: default SpecId0 */
 uint32_t descriptor_set;         /* SPIR-V DescriptorSet, 0 */
 uint32_t binding;                /* SPIR-V Binding, 0 */
};
int tegu_mali_compile_graphics_pair_texture(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,
 struct tegu_mali_graphics_pair2 *,struct tegu_mali_texture_plan *,char *,size_t);
/* Additive bounded compositor resource codegen; not native/GPU qualified.
 * Version1 requires VS UBO set0 bindings0/1, FS UBO bindings2/3 and exactly
 * four surviving explicit-LOD0 samples of combined sampler2D binding4. No descriptor
 * arrays/reindexing, SSBOs or writable resources. Dynamic UBO byte offsets
 * remain; the caller must validate AIR binding windows/offsets and retain all
 * mapped descriptor backing. Buffer descriptors are bounded and padded using
 * owned storage. This opt-in path allows up to8KiB code per stage, TLS=0;
 * existing compiler entry points retain4KiB and their old resource refusals.
 * Outputs must not overlap inputs/each other; invalid preflight leaves them
 * untouched. After preflight failure, code/pair are empty and plan unchanged.
 * Four UBOs bound to user table1 slots0..3; combined sampler binding4 uses
 * slots4(TEXTURE),5(SAMPLER). Declaration extents below are conservative SPIR-V
 * type spans, NOT dynamic AIR offset minima. Native must validate metadata,
 * bind retained owned padded ranges and install both stage SRTs. */
#define TEGU_MALI_RESOURCE_PLAN_VERSION 1u
#define TEGU_MALI_RESOURCE_CODE_LIMIT 8192u
struct tegu_mali_resource_ubo {
 uint32_t stage,descriptor_set,binding,table,index,declared_bytes;
};
struct tegu_mali_resource_plan {
 uint32_t version,table,texture_index,sampler_index,normalized_coordinates,sample_count;
 struct tegu_mali_resource_ubo ubos[4];
};
int tegu_mali_compile_graphics_pair_resources(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_resource_plan *,char *,size_t);
/* Separate exact captured 8-tap class: sixteen samples, VS 1152B, FS 25600B.
 * Existing v1 entry point remains capped at four samples/8192B. */
int tegu_mali_compile_graphics_pair_resources_v7(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_resource_plan *,char *,size_t);

/* Exact R82 reconstruction of the R80 fixed UI resource shape.  After
 * SPIR-V-to-NIR, VS has one 320-byte UBO declaration at set0/binding0 and FS
 * has two aliased 432-byte UBO declarations at binding1.  AIR proves that the
 * caller-owned live windows are only 64 and 168 bytes.  FS also has one
 * readonly, unsized uint storage declaration at binding2 for its 8-byte
 * constant blob; the generated program addresses 84 words and relies on the
 * bounded descriptor returning zero beyond the padded blob.  All three are
 * lowered to table1 indices 0..2.  Any other resource shape is refused. */
#define TEGU_MALI_FIXED_UI_PLAN_VERSION 1u
#define TEGU_MALI_FIXED_UI_CODE_LIMIT 65536u
struct tegu_mali_fixed_ui_plan {
 uint32_t version,table,resource_count,texture_count;
 uint32_t constant_bytes,constant_access_bytes;
 /* Entry2 describes the readonly source SSBO after lowering to a hardware
  * UBO descriptor.  Its declared_bytes is zero because SPIR-V is unsized. */
 struct tegu_mali_resource_ubo ubos[3];
};
int tegu_mali_compile_fixed_ui_pair_r80(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_fixed_ui_plan *,char *,size_t);

/* Exact R85 sampled fixed-UI class, pinned to the AIR bridge's two SPIR-V
 * modules. Default SpecId0 is folded before resource inspection. The fragment
 * admits exactly one implicit-LOD 2D sample carrying one bias
 * operand. Table1 maps the four UBOs to entries0..3, the synthetic readonly
 * constant source to entry4, texture3 to entry5 and sampler0 to entry6. */
#define TEGU_MALI_FIXED_UI_TEXTURE_R85_PLAN_VERSION 1u
struct tegu_mali_fixed_ui_texture_r85_plan {
 uint32_t version,table,resource_count,texture_index,sampler_index;
 uint32_t normalized_coordinates,sample_count,implicit_lod,has_bias;
 uint32_t constant_bytes,constant_access_bytes;
 /* entries0..3 are the stage UBO bindings; entry4 is the synthetic constant
  * source lowered from readonly storage to a hardware UBO descriptor. */
 struct tegu_mali_resource_ubo buffers[5];
};
int tegu_mali_compile_fixed_ui_texture_pair_r85(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_fixed_ui_texture_r85_plan *,char *,size_t);
/* Generic reflection-driven pair.  Descriptor set 0, binding N maps to
 * Valhall resource table 1 entry N for buffers, textures and input
 * attachments, and table 2 entry N for samplers (panvk's per-binding rule).
 * Input attachments (Metal framebuffer fetch) become texel fetches at the
 * fragment coordinate; the caller binds the render target's texture there.
 * The plan reflects every live binding so userspace fills the descriptors.
 * TGJ1 currently requires one instance with base_instance == 0; the compiler
 * folds that driver sysval to zero. BaseVertex (first_vertex) is refused
 * before Mesa. tegu_mali_compile_generic_pair_options with DRAW_PARAMETERS
 * replaces both with real per-draw FAU words (see below). Any surviving
 * driver sysval and any scratch (private memory/TLS) is refused before the
 * backend instead of reaching a Bifrost assert. */
#define TEGU_MALI_GENERIC_PLAN_VERSION 1u
#define TEGU_MALI_GENERIC_MAX_BINDINGS 16u
#define TEGU_MALI_GENERIC_TEXTURE_TABLE 1u
#define TEGU_MALI_GENERIC_SAMPLER_TABLE 2u
enum tegu_mali_generic_kind {
 TEGU_MALI_GENERIC_UBO=1,TEGU_MALI_GENERIC_READONLY_STORAGE=2,TEGU_MALI_GENERIC_TEXTURE=3,
 TEGU_MALI_GENERIC_SAMPLER=4,TEGU_MALI_GENERIC_COMBINED=5,TEGU_MALI_GENERIC_INPUT_ATTACHMENT=6
};
struct tegu_mali_generic_binding {
 uint32_t binding,kind,stage_mask,declared_bytes,input_attachment_index,reserved[3];
};
struct tegu_mali_generic_plan {
 uint32_t version,count,reads_framebuffer,reserved;
 struct tegu_mali_generic_binding bindings[TEGU_MALI_GENERIC_MAX_BINDINGS];
};
int tegu_mali_compile_generic_pair(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,char *,size_t);
/* Same, for one colour output `rt` of a multi-target fragment shader: that
 * output becomes DATA0, the others are discarded (MRT emulation, one job per
 * target). Fails when output rt is not written. plan->reserved = rt+1. */
int tegu_mali_compile_generic_pair_rt(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,unsigned,char *,size_t);

/* Experimental tile-local active-target fetch. No production caller until
 * overlapping-draw device readback qualifies ordering and conversion. The
 * existing plan ABI is unchanged; reads_framebuffer distinguishes the path.
 * Refuses cross-attachment/MSAA/sparse/offset fetch and no-fetch shaders. */
#define TEGU_MALI_GENERIC_FETCH_MEMORY 1u
#define TEGU_MALI_GENERIC_FETCH_TILE 2u
int tegu_mali_compile_generic_pair_tile_rt(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,unsigned,char *,size_t);

/* Additive per-stage specialization. Include mali_compute_compiler.h for the
 * unchanged scalar record definition. Each stage has its own SpecId namespace:
 * <=64 unique entries, little-endian scalar bytes matching that module's
 * declared type (1/2/4/8 bytes, bool uses VkBool32). Unknown IDs, nonscalars,
 * mismatched sizes and malformed declarations are refused before Mesa.
 * Unprovided IDs retain module defaults. NULL/0 and explicit defaults preserve
 * legacy bytes, including conservative resource declarations. Stages whose
 * values change prune dead resources before reflecting their selected output.
 * Specialization precedes resource reflection and varying linkage: bind the
 * returned plan using the SAME selected translator variant and sampler facts.
 * Cache keys must include both modules, both tables, RT selection and mode.
 * All output and ownership rules of the corresponding default entry apply.
 * Tables are borrowed only during the call and must not overlap outputs. */
#define TEGU_MALI_GENERIC_SPECIALIZATION_VERSION 1u
struct tegu_mali_specialization;
int tegu_mali_compile_generic_pair_specialized(const void *,size_t,const void *,size_t,
 const struct tegu_mali_specialization *,size_t,const struct tegu_mali_specialization *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,char *,size_t);
int tegu_mali_compile_generic_pair_specialized_rt(const void *,size_t,const void *,size_t,
 const struct tegu_mali_specialization *,size_t,const struct tegu_mali_specialization *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,unsigned,char *,size_t);
int tegu_mali_compile_generic_pair_specialized_tile_rt(const void *,size_t,const void *,size_t,
 const struct tegu_mali_specialization *,size_t,const struct tegu_mali_specialization *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,unsigned,char *,size_t);

/* Additive options entry: the specialized generic pair plus opt-in vertex
 * draw parameters. Without TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS it is
 * exactly the _specialized, _specialized_rt (SELECT_RT) or _specialized_tile_rt
 * (SELECT_RT|TILE_FETCH) entry: same code, plan and refusals.
 *
 * DRAW_PARAMETERS, vertex stage only (Vulkan semantics, which the AIR
 * translator uses for Metal [[vertex_id]]/[[base_vertex]]/[[base_instance]]):
 *   VertexIndex   = hardware vertex id + VB
 *   BaseVertex    = VB
 *   InstanceIndex = hardware instance id + BI,  BaseInstance = BI
 * VB is the signed32 word at vertex FAU byte TEGU_GRAPHICS_VERTEX_BASE and BI
 * the uint32 word at TEGU_GRAPHICS_BASE_INSTANCE. vs->sysval_mask reports
 * TEGU_GRAPHICS_SYSVAL_VERTEX_BASE / _BASE_INSTANCE when the compiled VS reads
 * them; fau_words then covers the word. The caller MUST write VB = vertexStart
 * (non-indexed) or baseVertex (indexed) and BI = baseInstance for every draw
 * with that bit set, keep MALI_PRIMITIVE.base_vertex_offset ZERO (the hardware
 * id is then 0-based, or the raw index for indexed draws), and rebase only the
 * hardware-fetched per-vertex attributes (vs_attribute_mask) by VB*stride and
 * per-instance attributes by BI*stride (step 1). Buffers the shader reads by
 * VertexIndex/InstanceIndex itself must NOT be rebased. Without the bit the
 * shader does not depend on VB/BI. FAU bytes 64..127 are otherwise unused.
 * The default (no-option) entries keep base_instance folded to zero and refuse
 * BaseVertex before Mesa. Instance count stays a transport policy.
 * options is borrowed for the call, must be valid, version1, reserved0;
 * rt 0..7 only with SELECT_RT (else 0); TILE_FETCH requires SELECT_RT.
 * Invalid options return 1 without writing any output or error byte. */
#define TEGU_GRAPHICS_VERTEX_BASE 64u
#define TEGU_GRAPHICS_BASE_INSTANCE 68u
#define TEGU_GRAPHICS_SYSVAL_VERTEX_BASE (1u << 4)
#define TEGU_GRAPHICS_SYSVAL_BASE_INSTANCE (1u << 5)
#define TEGU_MALI_GENERIC_OPTIONS_VERSION 1u
#define TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS 1u
#define TEGU_MALI_GENERIC_OPTION_SELECT_RT 2u
#define TEGU_MALI_GENERIC_OPTION_TILE_FETCH 4u
#define TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS 8u
/* THREAD_STORAGE: allow register spills / private arrays. vs->tls and fs->tls
 * report Mesa's per-thread stack bytes (<0x8000; scratch <0x7000). When either
 * is nonzero the native draw MUST bind a LOCAL_STORAGE with TLS for the IDVS
 * vertex job AND the fragment job, exactly as pinned Mesa gallium/panvk: one
 * stack of max(vs->tls,fs->tls) bytes, tls_size=pan_get_stack_shift(max),
 * tls_address_mode=PACKED, tls_base_pointer=ptr>>8 (4096-aligned), BO bytes >=
 * pan_get_total_stack_size(max,threads_per_core,core_id_range) (pan_desc.c
 * pan_emit_tls, pan_scratch.c). Without the flag TLS stays refused. */
#define TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE 16u
/* MULTI_TARGET (requires struct tegu_mali_generic_options2, version 2; not
 * combinable with SELECT_RT/TILE_FETCH): compile the FULL fragment shader for
 * one true-MRT job. target_formats[0..target_count) are the bound Metal colour
 * attachment formats (10 R8Unorm, 70 RGBA8Unorm, 80 BGRA8Unorm, 115 RGBA16Float;
 * 0 = unbound slot), at most 4; unused entries and reserved2 must be zero.
 * Outputs to unbound slots are discarded; outputs past target_count, integer
 * outputs and dual-source (Index1) outputs are refused. On success
 * plan->reserved = TEGU_MALI_GENERIC_PLAN_MRT | written-target mask (bits0..3)
 * and fs->sysval_mask bit3 is set. Native contract (arch9, same as panvk
 * fs.blend_descs[rt]): for EVERY written target rt the fragment FAU 64-bit word
 * at byte TEGU_GRAPHICS_BLEND_DESCRIPTOR+8*rt (48,56,64,72) MUST hold BLEND
 * descriptor words2..3 (the internal conversion/mode word) built for THAT
 * target's format and blend state; DRAW.blend points to target_count 16-byte
 * BLEND descriptors in target order with blend_count=target_count, and the
 * framebuffer carries the same render targets in the same order. Targets not in
 * the written mask get no BLEND instruction (contents per load/store action).
 * The compiled code does not depend on the formats beyond these checks, so a
 * cache key must still include the format list (it changes the written mask). */
#define TEGU_MALI_GENERIC_OPTION_MULTI_TARGET 32u
#define TEGU_MALI_GENERIC_OPTIONS2_VERSION 2u
#define TEGU_MALI_GENERIC_MAX_TARGETS 4u
#define TEGU_MALI_GENERIC_PLAN_MRT 0x80000000u
struct tegu_mali_generic_options2 {
 uint32_t version,flags,rt,reserved;               /* version 2; rt 0 */
 uint32_t target_count,target_formats[TEGU_MALI_GENERIC_MAX_TARGETS],reserved2[3];
};
struct tegu_mali_generic_options { uint32_t version,flags,rt,reserved; };
/* EXTENDED_VARYINGS (requires a non-NULL zeroed varyings output; without the
 * flag it must be NULL): up to TEGU_MALI_EXT_MAX_VARYINGS user locations of
 * 32-bit float/int/uint scalars/vectors; float may be smooth, noperspective or
 * flat, int/uint must be flat in the FS; VS/FS types and component counts
 * must match; centroid/sample/component/relaxed precision stay refused.
 * Varyings are read with LD_VAR_BUF(_FLAT); fs_uses_ld_var is still 0.
 * Noperspective is lowered by Mesa itself (VS *w, FS *frag_coord.w);
 * pair->noperspective_mask then reports it. pair->varying_count/varyings[]
 * stay ZERO; the full linked list is varyings->slots[0..count). When
 * pair->flags has TEGU_MALI_PAIR2_FLAT_SHADING the native draw MUST select
 * the Vulkan/Metal FIRST provoking vertex: arch9 TILER_CONTEXT
 * first_provoking_vertex=1 (word2 bit18), and for line lists also
 * DRAW.flat_shading_vertex=1 (Mesa pan_jm.c/panvk jm). Without that the last
 * vertex silently provides flat values. VARY_SIZE stays pair->generic_size_B. */
#define TEGU_MALI_PAIR2_FLAT_SHADING 2u
#define TEGU_MALI_EXT_MAX_VARYINGS 16u
#define TEGU_MALI_VARYING_INFO_VERSION 1u
enum tegu_mali_varying_interp { TEGU_MALI_VARYING_SMOOTH=1, TEGU_MALI_VARYING_NOPERSPECTIVE=2, TEGU_MALI_VARYING_FLAT=3 };
struct tegu_mali_varying_slot2 {
 uint32_t location,offset,components,alu_type; /* as tegu_mali_varying_slot; 34=int32,36=uint32 */
 uint32_t interpolation,reserved;              /* FS mode; 0 when only the VS writes it */
};
struct tegu_mali_varying_info {
 uint32_t version,count,vs_mask,fs_mask,flat_mask,noperspective_mask,generic_size_B,uses_flat_shading;
 struct tegu_mali_varying_slot2 slots[TEGU_MALI_EXT_MAX_VARYINGS];
};
int tegu_mali_compile_generic_pair_options(const void *,size_t,const void *,size_t,
 const struct tegu_mali_specialization *,size_t,const struct tegu_mali_specialization *,size_t,
 const struct tegu_mali_generic_options *,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,struct tegu_mali_varying_info *,char *,size_t);

/* Exact single_pass_blur_3 class. Unlike the generic resource entries, this
 * class accepts exactly four implicit-LOD samples and pins the captured input
 * modules. The resource table layout remains table1: UBO0..3, texture4,
 * sampler5. implicit_lod is always 1 on success. */
#define TEGU_MALI_SINGLE_PASS_BLUR3_PLAN_VERSION 1u
struct tegu_mali_single_pass_blur3_plan {
 uint32_t version,table,texture_index,sampler_index,normalized_coordinates;
 uint32_t sample_count,implicit_lod;
 struct tegu_mali_resource_ubo ubos[4];
};
int tegu_mali_compile_single_pass_blur3_pair(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_single_pass_blur3_plan *,char *,size_t);

/* Experimental exact path_blit_vert_lph + attachment_clear_frag_lph compiler
 * result. This is deliberately separate from the generic pair/resource ABI:
 * the VS emits position+PSIZ but no generic varyings or secondary program. */
#define TEGU_MALI_ATTACHMENT_CLEAR_VERSION 2u
enum tegu_mali_attachment_clear_role_v1 {
 TEGU_MALI_AC_ROLE_RECT_SSBO=1,TEGU_MALI_AC_ROLE_MATRIX_UBO=2
};
enum tegu_mali_attachment_clear_source_v1 {
 TEGU_MALI_AC_SOURCE_STORAGE_BUFFER=1,TEGU_MALI_AC_SOURCE_UNIFORM_BUFFER=2
};
enum tegu_mali_attachment_clear_backend_v1 { TEGU_MALI_AC_BACKEND_UBO=1 };
enum tegu_mali_attachment_clear_access_v1 { TEGU_MALI_AC_ACCESS_READ=1 };
struct tegu_mali_attachment_clear_resource_v2 {
 uint32_t role,stage,descriptor_set,binding;
 uint32_t source_class,backend_class,table,index;
 /* declared_bytes=0 means the SPIR-V declaration is unsized. offset_push_byte
  * selects the AIR push word containing the runtime byte offset. The caller
  * aligns the descriptor base down to descriptor_base_alignment and supplies
  * the matching shader residual; window_bytes are required after that runtime
  * offset. offset_alignment validates the original AIR offset (the matrix
  * shader divides its offset by 16, so it cannot accept a residual). */
 uint32_t declared_bytes,offset_push_byte,offset_alignment,descriptor_base_alignment,window_bytes,access;
};
struct tegu_mali_attachment_clear_plan_v2 {
 /* rt*_write_mask, target*_format/blend, and draw fields are caller-enforced
  * class requirements. target_descriptors_seen is the compiler-observed mask
  * of blend-descriptor indices actually consumed by the fragment code. */
 uint32_t version,output_count,rt0_write_mask,rt1_write_mask;
 uint32_t position_offset,secondary_offset,secondary_enabled,generic_size_B;
 uint32_t draw_vertex_count,draw_instance_count,draw_base_instance,draw_indexed;
 uint32_t target0_format,target1_format,target0_blend_enabled,target1_blend_enabled;
 uint32_t target_descriptors_seen,resource_count;
 struct tegu_mali_attachment_clear_resource_v2 resources[2];
};
/* On success, caller owns stage code and frees it with tegu_mali_graphics_free.
 * Inputs are borrowed only for the call. Plan contains values, never pointers.
 * Every output and error buffer must be disjoint from both inputs and all other
 * outputs. Invalid span/alias returns 1 without writing any caller memory;
 * compile refusal returns nonzero, leaves code empty and plan zero, and may
 * write a bounded error message. Success returns 0. This exact userspace
 * class pins code SHA-256 and does not imply package or GPU support. */
int tegu_mali_compile_attachment_clear_pair_v2(const void *,size_t,const void *,size_t,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,
 struct tegu_mali_attachment_clear_plan_v2 *,char *,size_t);
/* RUNTIME_SPEC (tegu_mali_compile_generic_pair_options flag; no
 * specialization tables): every specialization constant of both modules is
 * read from push words instead of folded, so one compile serves every value
 * set (the frontend's fallback while the specialized variant compiles in the
 * background). A module qualifies when every specialization constant is a
 * 32-bit integer or a boolean with a SpecId, used only inside the entry
 * function and by OpSpecConstantOps of integer/logical/compare/select kind.
 * Values: push byte TEGU_SPEC_PUSH_BASE + 4*k, k = the constant's position in
 * ascending SpecId order (tegu_spec_push_scan), booleans as 0/1; a module
 * whose own push block reaches past the base is refused (status 2). */
#define TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC 64u
#define TEGU_SPEC_PUSH_BASE 96u
#define TEGU_SPEC_PUSH_MAX 8u

struct tegu_spec_push_entry {
 uint32_t spec_id, result_id, is_bool, reserved;
 uint32_t default_value;
};

/* SPIR-V opcodes used here. */
enum {
 TEGU_SPV_OP_TYPE_BOOL=20, TEGU_SPV_OP_TYPE_INT=21, TEGU_SPV_OP_SPEC_TRUE=48, TEGU_SPV_OP_SPEC_FALSE=49,
 TEGU_SPV_OP_SPEC_CONSTANT=50, TEGU_SPV_OP_SPEC_COMPOSITE=51, TEGU_SPV_OP_SPEC_OP=52,
 TEGU_SPV_OP_DECORATE=71, TEGU_SPV_DECORATION_SPEC_ID=1
};

/* The module's specialization constants in layout order. Returns the count,
 * 0 when the module has none, or -1 when it does not qualify (a constant of
 * another type or width, a composite specialization constant, a constant
 * without SpecId, duplicate SpecIds, more than TEGU_SPEC_PUSH_MAX, or a
 * malformed stream). */
static inline int tegu_spec_push_scan(const uint32_t *w, size_t words, struct tegu_spec_push_entry *out) {
 if(!w||words<5||w[0]!=0x07230203u)return -1;
 const uint32_t bound=w[3];
 unsigned count=0;
 /* pass 1: integer type widths, constants */
 for(size_t at=5;at<words;) {
  uint32_t head=w[at],op=head&0xffffu,len=head>>16;
  if(!len||at+len>words)return -1;
  if(op==TEGU_SPV_OP_SPEC_COMPOSITE)return -1;
  if(op==TEGU_SPV_OP_SPEC_CONSTANT||op==TEGU_SPV_OP_SPEC_TRUE||op==TEGU_SPV_OP_SPEC_FALSE) {
   if(len<3||count>=TEGU_SPEC_PUSH_MAX)return -1;
   uint32_t type=w[at+1],id=w[at+2];
   if(id>=bound)return -1;
   int is_bool=op!=TEGU_SPV_OP_SPEC_CONSTANT,typed=0;
   /* the result type must be a bool for True/False and a 32-bit int otherwise */
   for(size_t t=5;t<at;) {
    uint32_t th=w[t],to=th&0xffffu,tl=th>>16;if(!tl)return -1;
    if(tl>=2&&w[t+1]==type) {
     if(is_bool&&to==TEGU_SPV_OP_TYPE_BOOL)typed=1;
     if(!is_bool&&to==TEGU_SPV_OP_TYPE_INT&&tl>=4&&w[t+2]==32u)typed=1;
     break;
    }
    t+=tl;
   }
   if(!typed||(!is_bool&&len!=4))return -1;
   out[count]=(struct tegu_spec_push_entry){0xffffffffu,id,(uint32_t)is_bool,0,
    is_bool?(op==TEGU_SPV_OP_SPEC_TRUE?1u:0u):w[at+3]};
   count++;
  }
  at+=len;
 }
 if(!count)return 0;
 /* pass 2: SpecId decorations */
 for(size_t at=5;at<words;) {
  uint32_t head=w[at],op=head&0xffffu,len=head>>16;
  if(op==TEGU_SPV_OP_DECORATE&&len>=4&&w[at+2]==TEGU_SPV_DECORATION_SPEC_ID)
   for(unsigned k=0;k<count;k++)if(out[k].result_id==w[at+1])out[k].spec_id=w[at+3];
  at+=len;
 }
 /* ascending SpecId, all present and distinct */
 for(unsigned i=0;i<count;i++) {
  if(out[i].spec_id==0xffffffffu)return -1;
  for(unsigned j=i+1;j<count;j++) {
   if(out[j].spec_id==out[i].spec_id)return -1;
   if(out[j].spec_id<out[i].spec_id){struct tegu_spec_push_entry x=out[i];out[i]=out[j];out[j]=x;}
  }
 }
 return (int)count;
}

/* AIR-direct pair (built with TEGU_MALI_AIR; air/ is the vendored ansuz-metal
 * AIR->NIR frontend, MIT). Same options, outputs, ownership and native
 * contract as tegu_mali_compile_generic_pair_options for the SAME translation:
 * vd/fd are that translation's m2v SPIR-V (only the vc/fc tables are checked
 * against them, exactly as the SPIR-V entry does) and metadata is the m2v
 * metadata JSON the bridge returned with them. The NIR comes from the AIR
 * functions instead and is adapted to the m2v plan ABI described by that
 * metadata (set-0 binding N -> table1/table2 N, SlotBindOffset push residuals,
 * SamplerState SpecIds, input attachments, vertex locations), so the caller
 * binds and draws exactly as for the SPIR-V result. Anything the adapter or
 * the backend checks do not cover is refused (nonzero, message prefixed
 * "air:"); the caller then compiles the SPIR-V entry. RUNTIME_SPEC is refused.
 * Without TEGU_MALI_AIR the entry exists and always refuses (status 2). */
#define TEGU_MALI_AIR_PAIR_VERSION 1u
struct tegu_mali_air_constant {
 const char *name;      /* Metal function constant name, or NULL: match index */
 uint32_t index,size;   /* size 1/2/4/8 bytes */
 uint64_t bits;         /* little-endian value bits */
};
struct tegu_mali_air_function {
 const void *bitcode;size_t size; /* one AIR module (0x0B17C0DE) or a whole metallib ("MTLB") */
 const char *entry;               /* function name (required for a metallib) */
 const struct tegu_mali_air_constant *constants;uint32_t constant_count,reserved;
};
struct tegu_mali_air_vertex_attribute { uint32_t index,format,offset,buffer_index; }; /* MTLVertexDescriptor */
struct tegu_mali_air_pair {
 uint32_t version,attribute_count;
 struct tegu_mali_air_function vertex,fragment;
 const struct tegu_mali_air_vertex_attribute *attributes;
 const char *metadata;size_t metadata_size;
};
int tegu_mali_compile_air_pair_options(const struct tegu_mali_air_pair *air,
 const void *,size_t,const void *,size_t,
 const struct tegu_mali_specialization *,size_t,const struct tegu_mali_specialization *,size_t,
 const struct tegu_mali_generic_options *,
 struct tegu_mali_graphics *,struct tegu_mali_graphics *,struct tegu_mali_graphics_pair2 *,
 struct tegu_mali_generic_plan *,struct tegu_mali_varying_info *,char *,size_t);
#ifdef __cplusplus
}
#endif
#endif
