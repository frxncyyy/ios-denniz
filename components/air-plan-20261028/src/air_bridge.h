#ifndef TEGU_AIR_BRIDGE_H
#define TEGU_AIR_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#include "vertex_fetch.h"
#include "blend_state.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Handles/owned outputs must be disposed with the matching function. Calls on
 * a library may not race its destruction. Caller owns valid input spans.
 * Compile outputs must be empty on entry; error strings are bounded/NUL-ended.
 * This bridge compiles AIR -> SPIR-V only; backend must honor ALL metadata. */
typedef struct TeguAIRLibrary TeguAIRLibrary;
typedef struct { uint8_t *bytes; size_t length; } TeguAIRBlob;
TeguAIRLibrary *tegu_air_library_open(const uint8_t *,size_t,char *,size_t);
void tegu_air_library_close(TeguAIRLibrary *);
/* AIR-direct sources: the library's metallib bytes (borrowed while it lives)
 * and the function constants its specialization of `name` holds (names
 * borrowed, not NUL-terminated). 0 ok, 1 invalid, 2 a value wider than 8
 * bytes, 3 more constants than cap (bytes/length are still set). */
typedef struct {const uint8_t *name;size_t name_len;uint32_t index,size;uint64_t bits;} TeguAIRSourceConstant;
int32_t tegu_air_library_air_source(const TeguAIRLibrary *,const uint8_t *,size_t,const uint8_t **,size_t *,
 TeguAIRSourceConstant *,size_t,size_t *);
/* kind0=index, kind1=name. Caller supplies valid name spans; bytes are copied.
 * Snapshots own their parsed-library reference. At most256 constants,64bytes each. */
typedef struct {uint32_t kind,type,index,size;const uint8_t *name;size_t name_len;uint8_t data[64];} TeguAIRFunctionConstant;
TeguAIRLibrary *tegu_air_library_specialize(const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRFunctionConstant *,size_t,char *,size_t);
size_t tegu_air_function_count(const TeguAIRLibrary *);
size_t tegu_air_function_info(const TeguAIRLibrary *,size_t,char *,size_t,uint32_t *);
int32_t tegu_air_compile_compute(const TeguAIRLibrary *,const uint8_t *,size_t,
 const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
typedef struct TeguAIRPipeline TeguAIRPipeline;
typedef struct TeguAIRRenderPipeline TeguAIRRenderPipeline;
/* Static Metal vertex fetch descriptors; indices are Metal attribute/buffer
 * slots, not backend descriptor bindings. Counts <=31; indices range0..30.
 * The compiler owns a snapshot; native backends must still supply every fetch. */
typedef struct {uint32_t index,format,offset,buffer_index;} TeguAIRVertexAttribute;
typedef struct {uint32_t index,stride,step_function,step_rate;} TeguAIRVertexLayout;
/* Explicit bounded color targets. The new render ABI keeps inactive
 * attachments (write_mask=0) present; it never truncates the array to slot0. */
typedef struct {uint32_t format,write_mask;} TeguAIRRenderColorAttachment;
/* Every target's format and write mask is retained in requested_attachments
 * metadata, including zero-mask slots. Supply 1..8 contiguous attachments in
 * formats RGBA8Unorm(70), BGRA8Unorm(80), or RGBA16Float(115), unblended.
 * This compiler entry does not approve native MRT or render-pass semantics. The three
 * output blobs must be empty and nonaliasing; output/error/input spans may not
 * alias. */
TeguAIRRenderPipeline *tegu_air_render_pipeline_create_with_attachments(
 const TeguAIRLibrary *,const uint8_t *,size_t,const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRRenderColorAttachment *,size_t,const TeguAIRVertexAttribute *,size_t,
 const TeguAIRVertexLayout *,size_t,const uint8_t *,size_t,
 TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
/* Initial render compiler surface: one R8Unorm(10), RGBA8Unorm(70), or
 * BGRA8Unorm(80) attachment, one sample, no depth/stencil. Legacy entries are
 * unblended; the blend entry remains limited to bounded RGBA8/BGRA8 equations.
 * The basic entry takes no vertex
 * descriptor; the extended entry accepts a static descriptor. Outputs contain
 * linked vertex/fragment SPIR-V and metadata, NOT a native draw binding plan.
 * Libraries may close after creation. Empty, distinct output blobs required. */
TeguAIRRenderPipeline *tegu_air_render_pipeline_create(
 const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRLibrary *,const uint8_t *,size_t,uint32_t,
 const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
TeguAIRRenderPipeline *tegu_air_render_pipeline_create_with_vertex_descriptor(
 const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRLibrary *,const uint8_t *,size_t,uint32_t,
 const TeguAIRVertexAttribute *,size_t,const TeguAIRVertexLayout *,size_t,
 const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
/* Additive real blend descriptor path. Layout matches the bounded native
 * packer; compile before checking native metadata, never discard blend state.
 * Old entry points retain their unblended contract. Nonnull blend required. */
typedef struct tegu_blend_inputs TeguAIRRenderBlend;
TeguAIRRenderPipeline *tegu_air_render_pipeline_create_with_blend(
 const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRLibrary *,const uint8_t *,size_t,uint32_t,
 const TeguAIRVertexAttribute *,size_t,const TeguAIRVertexLayout *,size_t,
 const TeguAIRRenderBlend *,const uint8_t *,size_t,
 TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
int32_t tegu_air_render_pipeline_native_check_with_blend(const TeguAIRRenderPipeline *,
 uint32_t color_format,const TeguAIRRenderBlend *,char *,size_t);
/* One blend record per declared attachment (1..8), including zero-mask targets.
 * Each blend write_mask must equal its attachment write_mask. Bounded blend
 * equations/formats follow the existing blend bridge; partial masks are
 * preserved here for compilation only. No native execution gate is relaxed.
 * Outputs/error must not overlap inputs (including the complete blend array).
 * Metadata preserves requested_attachments and requested_attachment_blends. */
TeguAIRRenderPipeline *tegu_air_render_pipeline_create_with_attachment_blends(
 const TeguAIRLibrary *,const uint8_t *,size_t,const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRRenderColorAttachment *,size_t,const TeguAIRRenderBlend *,
 const TeguAIRVertexAttribute *,size_t,const TeguAIRVertexLayout *,size_t,
 const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
/* Extract only the fixed-function vertex fetch part of a linked pipeline.
 * Uses actual linked Vulkan locations and Metal buffer indices. This is NOT
 * native execution validation: resources/varyings/push/blend still need their
 * own checks. Compare mask to the extended Mali compiler's required mask. */
typedef struct {uint32_t version,mask,count,reserved;
 struct tegu_vertex_attribute attributes[TEGU_VERTEX_SLOTS];} TeguAIRVertexFetch;
/* Exact translator immutable (constexpr) sampler of set0/binding: Vulkan raw
 * enums and the VkSampler desc's floats, as the translated shader expects. */
typedef struct {
 uint32_t version,binding,min_filter,mag_filter,mipmap_mode,address[3],border_color;
 uint32_t compare_enabled,compare_op,anisotropy_enabled,reduction,border_components_set;
 float mip_lod_bias,max_anisotropy,min_lod,max_lod;
} TeguAIRImmutableSampler;
#if defined(__cplusplus)
static_assert(sizeof(TeguAIRImmutableSampler)==72,"immutable sampler ABI");
#else
_Static_assert(sizeof(TeguAIRImmutableSampler)==72,"immutable sampler ABI");
#endif
int32_t tegu_air_render_pipeline_immutable_sampler(const TeguAIRRenderPipeline *,uint32_t,
 TeguAIRImmutableSampler *,char *,size_t);
/* Translator variants of a render pipeline (sRGB raw views, cross-class reads,
 * ...). A GraphicsSpecializationPlan with reserved[0]==1 names the variant
 * reserved[2]:reserved[1]; its SPIR-V/metadata come from _variant_spirv.
 * _variant_count changes when a variant was compiled (store the serialized
 * pipeline again); _attach_source gives a cache-rebuilt pipeline its
 * creation descriptor so it can compile variants it has not cached. */
int32_t tegu_air_render_pipeline_variant_spirv(const TeguAIRRenderPipeline *,uint32_t,uint32_t,
 TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
uint32_t tegu_air_render_pipeline_variant_count(const TeguAIRRenderPipeline *);
/* The bytes (and length) of the buffer a TEGU_AIR_GENERIC_COLOR_TABLE binding
 * binds: the translator's constant colour table, process lifetime. */
const uint8_t *tegu_air_color_write_table(size_t *length);
int32_t tegu_air_render_pipeline_attach_source(const TeguAIRRenderPipeline *,
 const TeguAIRLibrary *,const uint8_t *,size_t,const TeguAIRLibrary *,const uint8_t *,size_t,
 uint32_t,const TeguAIRVertexAttribute *,size_t,const TeguAIRVertexLayout *,size_t,const TeguAIRRenderBlend *,
 const TeguAIRRenderColorAttachment *,size_t,const TeguAIRRenderBlend *,char *,size_t);
int32_t tegu_air_render_pipeline_vertex_fetch(const TeguAIRRenderPipeline *,
 TeguAIRVertexFetch *,char *,size_t);
/* Pipeline-static AIR push bytes, indexed by absolute AIR offset. Mali's FAU
 * base is NOT included. Span is the exclusive range end, including leading
 * padding. Refuses fields needing draw/resource bindings. This resolves only
 * push constants: caller must still honor graphics/resources/specialization
 * metadata and backend limits. Output is zero on failure. */
typedef struct {
 uint32_t version, vertex_span, fragment_span;
 uint8_t vertex[128], fragment[128];
} TeguAIRRenderPush;
int32_t tegu_air_render_pipeline_push(const TeguAIRRenderPipeline *,
 TeguAIRRenderPush *,char *,size_t);
/* Check current native metadata contract; backend SPIR-V compilation and draw
 * state validation are still required. No execution or GPU allocation. */
int32_t tegu_air_render_pipeline_native_check(const TeguAIRRenderPipeline *,
 uint32_t color_format,char *,size_t);
int32_t tegu_air_render_pipeline_native_check_with_vertex_fetch(const TeguAIRRenderPipeline *,
 uint32_t color_format,char *,size_t);
/* Exact R80 fixed-UI metadata gate: BGRA8 target; vertex buffer2 has a
 * translator-bounded 64-byte uniform window, fragment buffer1 a 168-byte
 * window, and the reflected offset push fields are at 0/64/68. This checks
 * pipeline metadata only; each draw still needs a typed bound-resource plan. */
int32_t tegu_air_render_pipeline_native_check_fixed_ui_r80(const TeguAIRRenderPipeline *,
 uint32_t color_format,char *,size_t);
/* Exact sampled R85 fixed-UI metadata gate: BGRA8 target, four bounded
 * uniform windows, FS texture3/sampler0, fragment constant blob and normalized
 * sampler specialization. Each draw still requires the execution plan below. */
int32_t tegu_air_render_pipeline_native_check_fixed_ui_r85_texture(const TeguAIRRenderPipeline *,
 uint32_t color_format,char *,size_t);
/* Retrieve the translator-owned resource contract after pipeline creation.
 * This additive diagnostic returns canonical JSON containing the exact layout,
 * interface, runtime dependencies and variant bits retained by the opaque
 * pipeline. It does not approve execution or replace plan_draw. Caller frees
 * the returned blob with tegu_air_blob_free. Output must be empty and must not
 * alias the handle or error span. */
int32_t tegu_air_render_pipeline_resource_contract(const TeguAIRRenderPipeline *,
 TeguAIRBlob *,char *,size_t);
/* Exact full translator metadata contract for the one captured two-target compositor class. */
int32_t tegu_air_render_pipeline_native_check_compositor(const TeguAIRRenderPipeline *,char *,size_t);
/* Exact metadata-only gate for the separately versioned captured eight-tap class. */
int32_t tegu_air_render_pipeline_native_check_compositor_v7(const TeguAIRRenderPipeline *,char *,size_t);
/* Exact captured home-affordance metadata gate. Metadata inspection only: no
 * native code-generation or execution approval is implied. */
int32_t tegu_air_render_pipeline_native_check_home_affordance(const TeguAIRRenderPipeline *,char *,size_t);
/* Shared byte-exact metadata gate for the separately name-checked low-precision
 * narrow-blur family and single-pass-blur-3. Observer-only; no execution approval. */
int32_t tegu_air_render_pipeline_native_check_narrow_blur_11(const TeguAIRRenderPipeline *,char *,size_t);
/* Compare translated metadata with one identity-pinned generated catalog
 * contract. The frontend first selects the entry by exact donor, function
 * names and complete Metal descriptor. Observer-only: no execution approval. */
int32_t tegu_air_render_pipeline_catalog_metadata_check(const TeguAIRRenderPipeline *,
 const char *metadata_sha256,size_t,char *,size_t);
/* Additive bounded texture gate. Accepts RGBA8Unorm/BGRA8Unorm targets(70/80), no enabled blend
 * (even One/Zero), qualified static fetch, and EXACTLY one fragment runtime
 * float texture2d<sample> [[texture(0)]] + runtime [[sampler(0)]] combined at
 * set0/binding0 with the default SamplerState spec constant (0). Plan fields
 * are Metal slots/Vulkan set+binding, NOT Mali descriptor indices/addresses:
 * {version1,stage16,texture0,sampler0,set0,binding0,normalized1}. Binding must
 * still enforce a normalized nearest clamp WeightedAverage non-anisotropic
 * non-compare sampler and an RGBA8 sampled texture. Output untouched on
 * refusal; error must not overlap output. Metadata check only, no execution. */
typedef struct {uint32_t version,stage,texture_index,sampler_index,
 descriptor_set,binding,normalized_coordinates;} TeguAIRTexturePlan;
int32_t tegu_air_render_pipeline_native_check_with_texture(const TeguAIRRenderPipeline *,
 uint32_t color_format,TeguAIRTexturePlan *,char *,size_t);
void tegu_air_render_pipeline_close(TeguAIRRenderPipeline *);
/* AIR-planned render pipeline (no translation): planner metadata from
 * tegu_mali_air_render_metadata (libNord2Mali), same descriptor inputs as the
 * create_with_* entries (color_format+blend, or targets+attachment_blends).
 * SPIR-V blobs are interface-facts modules (generator "TGAP", decorations only:
 * BuiltIn VertexIndex, SubpassData + InputAttachmentIndex); metadata as
 * render_outputs gives it (also after a cache round trip).
 * No translator variant is compiled for it. */
TeguAIRRenderPipeline *tegu_air_render_pipeline_create_planned(
 const TeguAIRLibrary *,const uint8_t *,size_t,const TeguAIRLibrary *,const uint8_t *,size_t,uint32_t,
 const TeguAIRVertexAttribute *,size_t,const TeguAIRVertexLayout *,size_t,const TeguAIRRenderBlend *,
 const TeguAIRRenderColorAttachment *,size_t,const TeguAIRRenderBlend *,
 const uint8_t *,size_t,const uint8_t *,size_t,
 TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
/* Pipeline cache: bytes that rebuild a created render pipeline (its handle
 * and the vertex SPIR-V, fragment SPIR-V and metadata blobs its creation
 * returned, byte for byte) under the same translator profile.  Deserialize
 * checks its own format version and the translator's artifact versions, key,
 * profile fingerprint and checksum; NULL means compile again. */
int32_t tegu_air_render_pipeline_serialize(const TeguAIRRenderPipeline *,TeguAIRBlob *,char *,size_t);
TeguAIRRenderPipeline *tegu_air_render_pipeline_deserialize(const uint8_t *,size_t,const uint8_t *,size_t,
 TeguAIRBlob *,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
typedef struct { uint32_t index, reserved; uint64_t length, offset; } TeguAIRBufferBinding;
/* Pipeline owns its device/compiler artifact independently of the library. A
 * storage-format stand-in may be used for creation-time compiler queries when
 * the profile needs a declared storage image format. Such a creation compile
 * is identified in metadata and is not dispatchable; a bound-format variant
 * must be compiled before dispatch can be planned.
 * Plan returns complete JSON resource/push/specialization commands. This is not
 * an executable GPU packet; the backend must consume or refuse every requirement.
 * Buffer GPU addresses are not supplied by this initial planner interface. */
TeguAIRPipeline *tegu_air_pipeline_create(const TeguAIRLibrary *,const uint8_t *,size_t,
 const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
/* MTLComputePipelineDescriptor inputs beyond the kernel (version 1; shader-coverage-opus).
 * linked: MTLLinkedFunctions entries, each a function of its own library handle by name
 * (list 0 functions, 1 privateFunctions, 2 binaryFunctions; a table's functions belong in 0/2).
 * Stage input (has_stage_input=1): stageInputDescriptor attributes (index, MTLAttributeFormat,
 * offset, bufferIndex) and layouts (bufferIndex, stride, MTLStepFunction incl.
 * ThreadPositionInGridX.. 5..8, stepRate), indexType/indexBufferIndex. max_call_stack_depth 0 =
 * Metal's 1. NULL options == tegu_air_pipeline_create. The same handle/plan/dispatch calls apply;
 * every dispatch-time variant compiles under the same options. */
/* link_name: MTLFunctionDescriptor.specializedName when the MTLFunction was made under another
 * name (its MTLFunction.name); NULL/0 = the library function's own name. */
typedef struct {const TeguAIRLibrary *library;const uint8_t *name;size_t name_len;
 const uint8_t *link_name;size_t link_name_len;uint32_t list,reserved;} TeguAIRLinkedFunction;
typedef struct {uint32_t version,max_call_stack_depth;const TeguAIRLinkedFunction *linked;size_t linked_count;
 const TeguAIRVertexAttribute *stage_attributes;size_t stage_attribute_count;
 const TeguAIRVertexLayout *stage_layouts;size_t stage_layout_count;
 uint32_t stage_index_type,stage_index_buffer_index,has_stage_input,reserved;} TeguAIRComputeCreateOptions;
TeguAIRPipeline *tegu_air_pipeline_create_ex(const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRComputeCreateOptions *,const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,char *,size_t);
/* MTLFunction.patchType (0 none,1 triangle,2 quad) / patchControlPointCount (-1 none). 0 ok. */
int32_t tegu_air_function_patch(const TeguAIRLibrary *,const uint8_t *,size_t,uint32_t *,int32_t *);
/* functionHandleWithFunction: table word of a linked function (library, name, optional
 * link_name/specializedName as linked). 0 ok, 1 not placeable in a table, -1 invalid. */
int32_t tegu_air_pipeline_function_handle(const TeguAIRPipeline *,const TeguAIRLibrary *,const uint8_t *,size_t,
 const uint8_t *,size_t,uint32_t *);
/* max threads, execution width, static threadgroup bytes. */
int32_t tegu_air_pipeline_limits(const TeguAIRPipeline *,uint32_t limits[3]);
/* One for an ordinary executable creation variant, zero for a creation-only
 * storage-texture stand-in, negative for an invalid handle. */
int32_t tegu_air_pipeline_dispatchable(const TeguAIRPipeline *);
void tegu_air_pipeline_close(TeguAIRPipeline *);
int32_t tegu_air_pipeline_plan(const TeguAIRPipeline *,const TeguAIRBufferBinding *,size_t,
 const uint32_t groups[3],const uint32_t local[3],TeguAIRBlob *,char *,size_t);
typedef struct { uint32_t index, reserved; uint64_t offset, length; } TeguAIRNativeBinding;
typedef struct { uint32_t id, size; uint8_t data[8]; } TeguAIRNativeConstant;
/* ---- v2 compute resources (additive; the v1 API above is unchanged) ----
 * Input: one record per Metal slot. `kind` is COMPUTE_BUFFER/TEXTURE/SAMPLER.
 * Buffers carry the retained allocation length and the Metal bind offset.
 * Textures carry their own immutable facts plus the backing allocation's
 * length/offset/row stride; no GPU address is accepted. Samplers carry the
 * exact MTLSamplerDescriptor state (Metal enum numbering). `access` repeats
 * the intended use (READ/SAMPLE or STORAGE/READ/WRITE) so a mismatch between
 * a texture's usage facts and the translator's descriptor class is refused,
 * never silently upgraded. */
#define TEGU_AIR_COMPUTE_RESOURCE_VERSION 1u
#define TEGU_AIR_COMPUTE_BUFFER 1u
#define TEGU_AIR_COMPUTE_TEXTURE 2u
#define TEGU_AIR_COMPUTE_SAMPLER 3u
#define TEGU_AIR_COMPUTE_MAX_RESOURCES 32u
#define TEGU_AIR_COMPUTE_MAX_REGIONS 8u
#define TEGU_AIR_COMPUTE_MAX_DESCRIPTORS 32u
/* Extended buffer-table contract: native set0 buffer bindings 0..30.
 * Descriptors remain the existing 192-byte TDG2 records; no wire resize. */
#define TEGU_AIR_COMPUTE_MAX_BUFFERS 31u
#define TEGU_AIR_COMPUTE_MAX_LEVELS 16u
#define TEGU_AIR_COMPUTE_ACCESS_READ 0x100u
#define TEGU_AIR_COMPUTE_ACCESS_WRITE 0x200u
#define TEGU_AIR_COMPUTE_ACCESS_SAMPLE 0x400u
#define TEGU_AIR_COMPUTE_ACCESS_STORAGE 0x800u
typedef struct {
 uint32_t version, kind, index, access;
 uint32_t format, texture_type, width, height;
 uint32_t depth, array_length, mipmap_levels, sample_count;
 uint32_t usage, row_stride, storage_mode, swizzle_word;
 uint32_t slice_stride, layer_stride, array_layers, reserved;
 uint32_t min_filter, mag_filter, mip_filter, address_s, address_t, address_r, border_color;
 uint32_t normalized_coordinates, lod_average, max_anisotropy, compare_function, reduction_mode;
 float lod_min_clamp, lod_max_clamp;
 uint64_t length, offset, resource_id;
 /* Additive per-level facts. Offsets are relative to the resource offset;
  * zero-filled for buffers/samplers and for the simple levels==1 path. */
 uint32_t swizzle_valid, reserved2;
 uint64_t level_offset[TEGU_AIR_COMPUTE_MAX_LEVELS];
 uint32_t level_row_stride[TEGU_AIR_COMPUTE_MAX_LEVELS];
 uint32_t level_slice_stride[TEGU_AIR_COMPUTE_MAX_LEVELS];
 uint32_t level_layer_stride[TEGU_AIR_COMPUTE_MAX_LEVELS];
} TeguAIRComputeResource;
/* One wire resource: the Vulkan set-0 binding it answers, the Metal slot it
 * came from, the exact use class and, for buffers, the descriptor range the
 * translator proved (relative to the Metal bind offset). Texture/image
 * entries additionally carry the complete backing span and immutable facts;
 * the native packer validates them against the retained allocation. */
typedef struct {
 uint32_t binding, kind, metal_index, flags;
 uint32_t sampler_index, sampler_flags, view_format, view_level;
 uint32_t view_layer, reserved, view_format_vk, reserved2;
 uint64_t range_offset, range_length;
} TeguAIRComputeDescriptor;
/* One regular Vulkan dispatch of the Metal dispatch. Every region carries its
 * own complete specialization constants, merged push bytes and group count,
 * because a non-uniform dispatchThreads edge is a different specialization,
 * not a padded grid. */
typedef struct {
 uint32_t groups[3];
 uint32_t local[3];
 uint32_t flags;
 uint32_t constant_count;
 TeguAIRNativeConstant constants[64];
 uint8_t push[128];
} TeguAIRComputeRegion;
typedef struct {
 uint32_t version, region_count, descriptor_count, local[3];
 uint32_t flags, reserved[2];
 uint8_t variant_hash[32];
 TeguAIRComputeDescriptor descriptors[TEGU_AIR_COMPUTE_MAX_DESCRIPTORS];
 TeguAIRComputeRegion regions[TEGU_AIR_COMPUTE_MAX_REGIONS];
} TeguAIRNativeDispatchV2;
#if defined(__cplusplus)
static_assert(sizeof(TeguAIRComputeResource)==488&&sizeof(TeguAIRComputeDescriptor)==64&&
 sizeof(TeguAIRComputeRegion)==1184&&sizeof(TeguAIRNativeDispatchV2)==11592,"compute v2 C ABI");
#else
_Static_assert(TEGU_AIR_COMPUTE_MAX_BUFFERS<=TEGU_AIR_COMPUTE_MAX_DESCRIPTORS,
 "extended buffer bindings must fit the TDG2 descriptor capacity");
_Static_assert(sizeof(TeguAIRComputeResource)==488&&sizeof(TeguAIRComputeDescriptor)==64&&
 sizeof(TeguAIRComputeRegion)==1184&&sizeof(TeguAIRNativeDispatchV2)==11592,"compute v2 C ABI");
#endif
typedef struct {
 uint32_t version, bindings_mask, groups[3], constant_count;
 TeguAIRNativeBinding bindings[8];
 uint8_t push[128];
 TeguAIRNativeConstant constants[64];
} TeguAIRNativeDispatch;
/* Converts supported compute plans to native ABI. Output is zero on refusal.
 * Binding array indexed by GPU descriptor, index field names the Metal slot.
 * Caller verifies padded allocation bounds, retains buffers, and applies all
 * constants before native pipeline import. GPU-page bounds checked here. */
int32_t tegu_air_pipeline_native_dispatch(const TeguAIRPipeline *,const TeguAIRBufferBinding *,size_t,
 const uint32_t groups[3],const uint32_t local[3],TeguAIRNativeDispatch *,char *,size_t);
/* Additive dynamic threadgroup-memory planners. `lengths` has one Metal
 * setThreadgroupMemoryLength value per argument index; NULL and all-zero
 * tables mean the legacy no-dynamic-memory dispatch. Each nonzero length must
 * be a multiple of 16 and at most 32768. The Rust side copies the values into
 * the kernel StageTable before planning, so every region's TgLength
 * specializations and the resulting TGP2 WLS size are exact. The old symbols
 * above/below remain byte-compatible and behave as if lengths were NULL. */
#define TEGU_AIR_COMPUTE_MAX_THREADGROUP_LENGTHS 31u
int32_t tegu_air_pipeline_native_dispatch_tg(const TeguAIRPipeline *,const TeguAIRBufferBinding *,size_t,
 const uint32_t lengths[TEGU_AIR_COMPUTE_MAX_THREADGROUP_LENGTHS],const uint32_t groups[3],
 const uint32_t local[3],TeguAIRNativeDispatch *,char *,size_t);
/* Additive v2 planner. Accepts every bound set-0 resource, returns the exact
 * descriptor correspondence, complete per-region specialization and the
 * merged pushes. dispatch_mode 0 is dispatchThreadgroups (the array is group
 * count), 1 is dispatchThreads (the array is the exact thread grid; the
 * translator may return several edge regions, never a rounded-up grid).
 * If the bound format/sampler facts require a translator variant,
 * `variant_spirv` is non-empty and contains the SPIR-V of that variant, which
 * the caller must compile to a native package and import; otherwise it is
 * left empty. `out` is zeroed before any refusal, so a failed call never
 * looks like a dispatch plan. No GPU address, no ownership. */
#define TEGU_AIR_COMPUTE_DISPATCH_THREADGROUPS 0u
#define TEGU_AIR_COMPUTE_DISPATCH_THREADS 1u
int32_t tegu_air_pipeline_native_dispatch_v2(const TeguAIRPipeline *,
 const TeguAIRComputeResource *,size_t,uint32_t dispatch_mode,const uint32_t extents[3],
 const uint32_t local[3],TeguAIRBlob *variant_spirv,TeguAIRNativeDispatchV2 *,char *,size_t);
int32_t tegu_air_pipeline_native_dispatch_v2_tg(const TeguAIRPipeline *,
 const TeguAIRComputeResource *,size_t,const uint32_t lengths[TEGU_AIR_COMPUTE_MAX_THREADGROUP_LENGTHS],
 uint32_t dispatch_mode,const uint32_t extents[3],const uint32_t local[3],
 TeguAIRBlob *variant_spirv,TeguAIRNativeDispatchV2 *,char *,size_t);
/* Metadata-only bounded render draw planner. Stage=1 vertex,2 fragment;
 * resource kind=1 buffer,2 texture,3 sampler. A stage/kind/index appears at
 * most once; buffers and textures use indices0..30, samplers0..15 per the
 * pinned translator Metal sampler table; resource count<=186. Buffer GPU-address fields
 * must be zero; GPU VAs are not accepted. Overlapping backing allocations are
 * allowed and ownership belongs to the native binder. Texture facts are limited to one 2D mip level and do not attest
 * backing span, row stride, storage layout, depth, or ownership; native import must
 * validate those facts from its owned texture object. Texture class is
 * restricted to listed 2D formats/facts plus the exact R8 64x64 Type3D sphere
 * class; a constexpr sampler is pipeline-owned and has no sampler resource.
 * Missing bindings may resolve to
 * translator Neutral resources, which are reported rather than treated as
 * caller-owned resources;
 * sampler fields must be nearest, normalized, clamp-to-edge, mip0, no compare,
 * no anisotropy, weighted-average. The JSON reports the complete translator
 * BindingPlan/DrawCommands and requirements, not native execution approval.
 * Draw counts are bounded (vertices <=2^24, instances <=2^20). Caller passes
 * empty output blobs. Output/error spans must not alias each other or inputs;
 * rejected aliased spans are left untouched. */
typedef struct {
 uint32_t kind,stage,index,format;
 uint32_t texture_type,width,height,array_length,mipmap_levels,sample_count,usage;
 uint32_t sparse,depth_twin,swizzle[4],parent_level,parent_width,parent_height,parent_depth,own_format_views,address_mode_identity;
 uint64_t length,offset,gpu_address,resource_id;
 uint32_t stride,reserved;
 uint32_t min_filter,mag_filter,mip_filter,address_s,address_t,address_r,border_color;
 uint32_t normalized_coordinates,lod_average,max_anisotropy,compare_function,reduction_mode;
 float lod_min_clamp,lod_max_clamp;
} TeguAIRRenderResource;
typedef struct {uint32_t version,vertex_start,vertex_count,instance_count,base_instance;} TeguAIRRenderDraw;
int32_t tegu_air_render_pipeline_plan_draw(const TeguAIRRenderPipeline *,
 const TeguAIRRenderResource *,size_t,const TeguAIRRenderDraw *,
 TeguAIRBlob *,char *,size_t);
/* Typed companion used by object-retained adapters; values come directly from
 * BindingPlan::uses and pipeline sampler dependencies, not JSON inspection.
 * Use kind=1/2/3 buffer/texture/sampler; stage=1/2 VS/FS. class_mask bit N
 * means AccessClass code N+1 is present (codes Storage=1,Uniform=2,Sampled=3,
 * StorageImage=4,VertexInput=5,Index=6,Indirect=7,Attachment=8); sampler mask
 * is zero. Flags: read=1,write=2,bounded-range=4,sampler-coordinates=8,
 * sampler-reduction=16,whole-range=32. Bounded range offset/length is relative
 * to the Metal binding offset. Whole-range dominates all bounded uses of a key
 * and has zero range fields. */
typedef struct {uint32_t kind,stage,index,class_mask,flags,reserved;
 uint64_t range_offset,range_length;} TeguAIRRenderResourceUse;
typedef struct {uint32_t version,count,attachment_count,diagnostic_count,neutral_count,reserved[3];
 TeguAIRRenderColorAttachment attachments[8];TeguAIRRenderResourceUse uses[186];} TeguAIRRenderTypedPlan;
int32_t tegu_air_render_pipeline_plan_draw_typed(const TeguAIRRenderPipeline *,
 const TeguAIRRenderResource *,size_t,const TeguAIRRenderDraw *,
 TeguAIRBlob *,TeguAIRRenderTypedPlan *,char *,size_t);
/* Execution-facing projection for the exact R80 fixed-UI class. Success means
 * the metadata gate, bound-resource plan and direct draw all agree: precisely
 * VS buffer2/set0 binding0 and FS buffer1/set0 binding1, two resolved push
 * spans, and one eight-byte stage constant blob. Descriptor ranges are
 * relative to the retained allocation; bind_offset remains the Metal offset.
 * The caller maps stage/index back to its encoder-retained buffer object;
 * buffer resource IDs remain zero under the existing RenderResource ABI. */
typedef struct {
 uint32_t stage,index,descriptor_set,binding;
 uint64_t bind_offset,resource_length,descriptor_offset,descriptor_length;
} TeguAIRFixedUiR80Binding;
typedef struct {uint32_t stage,offset,length,bytes_offset;} TeguAIRFixedUiR80PushSpan;
typedef struct {
 uint32_t version,binding_count,push_count,push_bytes_length;
 uint32_t constant_stage,constant_length,reserved[2];
 TeguAIRFixedUiR80Binding bindings[2];
 TeguAIRFixedUiR80PushSpan pushes[2];
 uint8_t push_bytes[12],constant_bytes[8];
} TeguAIRFixedUiR80Plan;
int32_t tegu_air_render_pipeline_plan_fixed_ui_r80(const TeguAIRRenderPipeline *,
 const TeguAIRRenderResource *,size_t,const TeguAIRRenderDraw *,
 TeguAIRFixedUiR80Plan *,char *,size_t);
/* Complete execution projection for the exact sampled R85 fixed-UI class.
 * Four buffer bindings correspond to set0 bindings 0..3. The combined binding
 * identifies retained FS texture3/sampler0 and carries the texture token. */
typedef struct {
 uint32_t stage,index,descriptor_set,binding;
 uint64_t bind_offset,resource_length,descriptor_offset,descriptor_length;
} TeguAIRFixedUiR85Binding;
typedef struct {uint32_t stage,offset,length,bytes_offset;} TeguAIRFixedUiR85PushSpan;
typedef struct {
 uint32_t texture_stage,texture_index,sampler_stage,sampler_index;
 uint32_t descriptor_set,binding,format,texture_type,width,height;
 uint64_t resource_id;
} TeguAIRFixedUiR85TextureBinding;
typedef struct {
 uint32_t version,binding_count,push_count,push_bytes_length;
 uint32_t constant_stage,constant_length,specialization_id,specialization_length;
 TeguAIRFixedUiR85Binding bindings[4];
 TeguAIRFixedUiR85TextureBinding texture;
 TeguAIRFixedUiR85PushSpan pushes[2];
 uint8_t push_bytes[20],constant_bytes[8],specialization_bytes[4];
} TeguAIRFixedUiR85Plan;
int32_t tegu_air_render_pipeline_plan_fixed_ui_r85_texture(const TeguAIRRenderPipeline *,
 const TeguAIRRenderResource *,size_t,const TeguAIRRenderDraw *,
 TeguAIRFixedUiR85Plan *,char *,size_t);
/* Reflection-driven projection of any single-BGRA8-target direct draw whose
 * set-0 descriptors are buffers, the constant blob, sampled textures,
 * samplers or framebuffer-fetch input attachments (generic_plan in
 * air_bridge.rs).  Bindings are in binding order; kinds below.  Buffer
 * descriptor ranges are allocation-relative.  Immutable samplers carry their
 * Vulkan filter/mipmap/address/border enums.  Pushes cover every push write;
 * specialization must equal the module defaults the Mali compile assumes.
 * layouts are the pipeline's vertex buffer layouts. */
#define TEGU_AIR_GENERIC_BUFFER 1u
#define TEGU_AIR_GENERIC_CONSTANT 2u
#define TEGU_AIR_GENERIC_COMBINED 3u
#define TEGU_AIR_GENERIC_COMBINED_IMMUTABLE 4u
#define TEGU_AIR_GENERIC_TEXTURE 5u
#define TEGU_AIR_GENERIC_SAMPLER 6u
#define TEGU_AIR_GENERIC_ATTACHMENT 7u
#define TEGU_AIR_GENERIC_NEUTRAL_IMAGE 8u  /* unbound: zero texture */
#define TEGU_AIR_GENERIC_NEUTRAL_BUFFER 9u /* unbound: zero buffer */
#define TEGU_AIR_GENERIC_COLOR_TABLE 10u  /* fragment sRGB colour table: bind tegu_air_color_write_table's bytes */
typedef struct {
 uint32_t binding,kind,descriptor_type,stage,index,sampler_stage,sampler_index,attachment;
 uint32_t min_filter,mag_filter,mipmap_mode,address[3],border_color;
 uint32_t format,texture_type,width,height,reserved;
 uint64_t resource_id,bind_offset,resource_length,descriptor_offset,descriptor_length;
} TeguAIRGenericBinding;
typedef struct {
 uint32_t version,binding_count,push_count,push_bytes_length;
 uint32_t constant_stage,constant_length,reads_framebuffer,reserved;
 TeguAIRGenericBinding bindings[16];
 TeguAIRFixedUiR85PushSpan pushes[8];
 uint8_t push_bytes[256],constant_bytes[512];
} TeguAIRGenericPlan;
int32_t tegu_air_render_pipeline_plan_generic(const TeguAIRRenderPipeline *,
 const TeguAIRRenderResource *,size_t,const TeguAIRVertexLayout *,size_t,const TeguAIRRenderDraw *,
 TeguAIRGenericPlan *,char *,size_t);
#if defined(__cplusplus)
static_assert(sizeof(TeguAIRGenericBinding)==120&&sizeof(TeguAIRGenericPlan)==2848,"generic plan ABI");
#else
_Static_assert(sizeof(TeguAIRGenericBinding)==120&&sizeof(TeguAIRGenericPlan)==2848,"generic plan ABI");
#endif
/* Additive exact graphics specialization sidecar.  GenericPlan v1 is not
 * changed.  Per-stage tables are canonical SpecId order, unique within the
 * stage, size1/2/4/8, little-endian.  view_format_vk is the selected Vulkan
 * view format from the planner, view_format_metal is the exact Metal alias for
 * that view, view_type/aspect are the selected VkImageViewType/aspect,
 * view_level is the selected view level, and compare_enabled is the plan's
 * actual per-use compare state.  flags bit0 means the view uses the depth twin
 * and bit1 means the shader applies the texture swizzle (the view mapping is
 * then identity).  Non-identity component mappings without shader swizzle are
 * refused, never silently copied.  specialization_bytes is the planner key for
 * cache identity; callers must also include the selected module bytes.  If VS
 * and FS declare the same SpecId with different values, specialization_bytes
 * alone is ambiguous: include both per-stage tables in the cache key.  The new
 * planner admits real linear/mip/compare/anisotropy/LOD sampler facts, bounded
 * 2D-array/mip-chain facts (new-only), and installed ASTC LDR/sRGB 2D facts;
 * old plan_generic remains default-specialization-only.  Callers must pass the
 * exact tables to tegu_mali_compile_generic_pair_specialized or its _rt form,
 * never to the unspecialized compiler entry. */
typedef struct {uint32_t id,size;uint8_t data[8];} TeguAIRGraphicsSpecConstant;
typedef struct {
 uint32_t binding,stage,index,kind;
 uint32_t view_format_vk,view_format_metal;
 uint32_t compare_enabled,view_type;
 uint32_t view_level,view_layer,flags,aspect;
} TeguAIRGraphicsSpecializationBinding;
#define TEGU_AIR_GRAPHICS_VIEW_TWIN 1u
#define TEGU_AIR_GRAPHICS_VIEW_SHADER_SWIZZLE 2u
/* The binding's sampler is an integer use: nearest filters and mip mode, no
 * anisotropy, level clamp min>=0, integer border colour (translator
 * metal/sampler.rs vk_sampler). */
#define TEGU_AIR_GRAPHICS_SAMPLER_INTEGER 4u
/* The view reads a depth aspect as colour, (d, d, d, 1): texture descriptor
 * swizzle R,R,R,1 (translator DEPTH_AS_COLOR). */
#define TEGU_AIR_GRAPHICS_VIEW_DEPTH_AS_COLOR 8u
#define TEGU_AIR_GRAPHICS_SPEC_MAX 64u
typedef struct {
 uint32_t version,binding_count,vertex_count,fragment_count;
 uint32_t specialization_length,reserved[3];
 TeguAIRGraphicsSpecializationBinding bindings[16];
 TeguAIRGraphicsSpecConstant vertex_constants[TEGU_AIR_GRAPHICS_SPEC_MAX];
 TeguAIRGraphicsSpecConstant fragment_constants[TEGU_AIR_GRAPHICS_SPEC_MAX];
 uint8_t specialization_bytes[512];
} TeguAIRGraphicsSpecializationPlan;
int32_t tegu_air_render_pipeline_plan_generic_specialized(const TeguAIRRenderPipeline *,
 const TeguAIRRenderResource *,size_t,const TeguAIRVertexLayout *,size_t,const TeguAIRRenderDraw *,
 TeguAIRGenericPlan *,TeguAIRGraphicsSpecializationPlan *,char *,size_t);
#if defined(__cplusplus)
static_assert(sizeof(TeguAIRGraphicsSpecConstant)==16&&sizeof(TeguAIRGraphicsSpecializationBinding)==48,
 "graphics specialization ABI");
static_assert(sizeof(TeguAIRGraphicsSpecializationPlan)==3360,"graphics specialization plan ABI");
#else
_Static_assert(sizeof(TeguAIRGraphicsSpecConstant)==16&&sizeof(TeguAIRGraphicsSpecializationBinding)==48,
 "graphics specialization ABI");
_Static_assert(sizeof(TeguAIRGraphicsSpecializationPlan)==3360,"graphics specialization plan ABI");
#endif
/* Buffer-offset templates of a generic plan (air_bridge.rs
 * generic_buffer_templates): per used descriptor-form buffer slot, its window
 * (kind 1 bounded [start,start+len), 2 whole), descriptor base alignment and
 * maximum range, its set-0 binding, and the push_bytes offsets of its
 * SlotBindOffset residual (u32) and SlotGuestBase (u64) copies.  -1 when the
 * plan is not restated by these (written or device-address slots, ...). */
typedef struct {
 uint32_t stage,index,binding,window_kind;
 int64_t window_start;uint64_t window_len,alignment,max_range;
 uint32_t residual_count,guest_count,residual_at[4],guest_at[4];
} TeguAIRGenericBufferTemplate;
typedef struct {
 uint32_t version,count,max_vertex_alignment,reserved;
 TeguAIRGenericBufferTemplate slots[16];
} TeguAIRGenericBufferTemplates;
int32_t tegu_air_render_pipeline_plan_generic_buffer_templates(const TeguAIRRenderPipeline *,
 const TeguAIRRenderResource *,size_t,const TeguAIRVertexLayout *,size_t,const TeguAIRRenderDraw *,
 const TeguAIRGenericPlan *,TeguAIRGenericBufferTemplates *,char *,size_t);
#if defined(__cplusplus)
static_assert(sizeof(TeguAIRGenericBufferTemplate)==88&&sizeof(TeguAIRGenericBufferTemplates)==1424,"buffer template ABI");
#else
_Static_assert(sizeof(TeguAIRGenericBufferTemplate)==88&&sizeof(TeguAIRGenericBufferTemplates)==1424,"buffer template ABI");
#endif
void tegu_air_blob_free(TeguAIRBlob *);
#ifdef __cplusplus
}
#endif
#endif
