/* Runtime render compilation. Owns artifacts until the last command releases
 * the pipeline; GPU import/submission is a separate encoder operation. */
#include "mali_graphics_compiler.h"
#include "compositor_process.h"
#include "pipeline_cache_frontend.inc"
#include "render_pipeline_format.h"
#include "compositor_request.h"
#include "TeguGPURenderPipelineV6.inc"
#include "native_package_v7.h"
#include "native_package_v7_writer.h"
#include "native_package_v7_writer.c"
#include "native_package_b3.h"
#include "native_package_b3.c"
#include "native_package_b3_writer.h"
#include "native_package_b3_writer.c"
#include "render_pipeline_b3.c"
#include "compositor_typed_gate.h"
#include "quartzcore_pipeline_catalog.h"
#include "compiler_trace.h"
#include "attachment_clear_package.h"
#include "native_package_ui1_writer.h"
#include "native_package_ui1.c"
#include "native_package_ui1_writer.c"
#include "fixed_ui_ui1_diag.h"
#include "native_package_ui2.h"
#include "native_package_ui2_writer.h"
/* This frontend is one Objective-C translation unit.  Namespace the UI2
 * implementation-private helpers so TFU1 and TFU2 remain additive. */
#define rd32 ui2_rd32
#define rd64 ui2_rd64
#define span ui2_span
#define overlap ui2_overlap
#define add64 ui2_add64
#define range_overlap ui2_range_overlap
#define stage_ok ui2_stage_ok
#define pair_ok ui2_pair_ok
#define info_ok ui2_info_ok
#define read_stage ui2_read_stage
#define alloc_ok ui2_alloc_ok
#define rect ui2_rect
#include "native_package_ui2.c"
#undef rd32
#undef rd64
#undef span
#undef overlap
#undef add64
#undef range_overlap
#undef stage_ok
#undef pair_ok
#undef info_ok
#undef read_stage
#undef alloc_ok
#undef rect
#include "native_package_ui2_writer.c"
#include "blend_state.c"
/* TGJ1 userspace job builders: the same sources XNU used for UI1/UI2. */
#include "vertex_fetch.c"
#include "compositor_vertex_state.c"
#include "compositor_v7_scratch_layout.c"
#include "fixed_ui_resources.c"
#include "fixed_ui_render_state.c"
#include "fixed_ui_texture_resources.c"
#include "fixed_ui_texture_render_state.c"
#include "compositor_load_mapping.c"
#include "compositor_load_assets_v6.c"
#include "compositor_load_bundle_v6.c"
#include "generic_job.c"
#include "generic_job_ui.c"
#ifdef TEGU_COMPOSITOR_PROFILE
#include "tegu-compositor-profile.h"
#define TEGU_HAS_COMPOSITOR_PROFILE 1
#else
#define tegu_compositor_profile tegu_native_profile
#define TEGU_HAS_COMPOSITOR_PROFILE 0
#endif
#ifdef TEGU_ATTACHMENT_CLEAR_PROFILE
#include "tegu-attachment-clear-profile.h"
#define TEGU_HAS_ATTACHMENT_CLEAR_PROFILE 1
#else
#define tegu_attachment_clear_profile tegu_compositor_profile
#define TEGU_HAS_ATTACHMENT_CLEAR_PROFILE 0
#endif
@interface NSObject (TeguRenderDescriptor)
- (id)vertexFunction;
- (id)fragmentFunction;
- (id)vertexDescriptor;
- (id)attributes;
- (id)layouts;
- (NSUInteger)format;
- (NSUInteger)bufferIndex;
- (NSUInteger)offset;
- (NSUInteger)stride;
- (NSUInteger)stepFunction;
- (NSUInteger)stepRate;
- (id)colorAttachments;
- (id)objectAtIndexedSubscript:(NSUInteger)index;
- (NSUInteger)pixelFormat;
- (signed char)isBlendingEnabled;
- (NSUInteger)rgbBlendOperation;
- (NSUInteger)sourceRGBBlendFactor;
- (NSUInteger)destinationRGBBlendFactor;
- (NSUInteger)alphaBlendOperation;
- (NSUInteger)sourceAlphaBlendFactor;
- (NSUInteger)destinationAlphaBlendFactor;
- (NSUInteger)rasterSampleCount;
- (NSUInteger)depthAttachmentPixelFormat;
- (NSUInteger)stencilAttachmentPixelFormat;
- (signed char)isAlphaToCoverageEnabled;
- (signed char)isAlphaToOneEnabled;
- (signed char)isRasterizationEnabled;
- (NSUInteger)inputPrimitiveTopology;
- (NSUInteger)maxVertexAmplificationCount;
- (signed char)supportIndirectCommandBuffers;
- (id)label;
@end
@interface TeguRenderColorDescriptor : NSObject
- (NSUInteger)writeMask;
@end
/* One immutable specialized TGJ1 variant. Variants are pipeline-owned and
 * selected by a Dispatch index, so queued draws with different sampler/view
 * facts never share or mutate pipeline->genericShader. */
struct tegu_render_generic_variant {
 uint8_t key[32];
 unsigned rt;
 struct tegu_mali_graphics vertex,fragment;
 struct tegu_mali_graphics_pair2 pair;
 struct tegu_mali_generic_plan compile;
 struct tegu_generic_draw_shader shader;
 TeguAIRGraphicsSpecializationBinding bindings[16];
 struct tegu_mali_varying_info varying;
 uint32_t binding_count,vertex_constant_count,fragment_constant_count,specialization_length;
 uint32_t pair_flags,sysval_mask,options_flags,tls;
 /* Draw-time fallback (tegu_render_variant_fallback): 1 = the pipeline's
  * runtime-specialization compile (its specialization constants read from
  * push words), 2 = a stand-in for the exact variant `key` sharing that
  * code until the exact one is published; shared entries own no code. */
 uint32_t runtime_spec,shared;
 int rs_n[2];struct tegu_spec_push_entry rs[2][TEGU_SPEC_PUSH_MAX];
};
@interface TeguRenderPipeline : NSObject {
@public TeguComputeDevice *owner; id debugLabel;
 TeguAIRRenderPipeline *compiled;
 TeguAIRRenderPush push;
 TeguAIRRenderBlend blend;
 TeguAIRVertexFetch vertexFetch;
 struct tegu_render_texture_plan texture;
 struct tegu_mali_texture_plan maliTexture;
 struct tegu_mali_graphics vertexCode,fragmentCode;
 struct tegu_mali_attachment_clear_plan_v2 attachmentClearPlan;
 struct tegu_mali_graphics_pair2 pair;
 struct tegu_mali_resource_plan compositorResources; int compositorV6,compositorV7,compositorB3;
 unsigned compositorSnapshotOnly,attachmentClearSnapshotOnly,fixedUiSnapshotOnly,fixedUiTextureSnapshotOnly,fixedUiObserverOnly,genericObserverOnly,homeAffordanceObserverOnly,sphereObserverOnly;
 struct tegu_ui2_info fixedUiTextureInfo;
 /* Reflection-driven TGJ1 draw: compiled by tegu_mali_compile_generic_pair,
  * bound per draw from tegu_air_render_pipeline_plan_generic. */
 unsigned genericDraw,genericLayoutCount;char genericName[40];
 /* The linked VS reads SPIR-V BuiltIn VertexIndex. The current TGJ1 request
  * does not expose the Valhall primitive base-vertex offset, so nonzero
  * vertexStart/CPU index expansion would silently change the shader's
  * identity. The encoder refuses that case until an additive request field
  * carries first_vertex/base_vertex without double-applying the attribute
  * rebase. Native indexed vertex-pulling keeps the original index BO. */
 unsigned genericVertexIndex;
 struct tegu_mali_generic_plan genericCompile;
 struct tegu_generic_draw_shader genericShader;
 struct tegu_mali_varying_info genericVaryingInfo;
 uint32_t genericPairFlags,genericSysvalMask;
 TeguAIRVertexLayout genericLayouts[8];
 TeguAIRVertexFetch genericFetch;
 uint8_t *genericVsBytes,*genericFsBytes;
 /* SHA-256 of both modules, computed once when they are set: the per-draw
  * variant key hashes this instead of re-hashing up to ~0.6 MB of SPIR-V
  * (c6jb: ~5 ms of CPU per QuartzCore fixed_frag draw). */
 uint8_t genericModuleDigest[32];
 NSUInteger genericVsLength,genericFsLength;
 struct tegu_render_generic_variant *genericVariants;
 unsigned genericVariantCount,genericVariantCapacity;
 unsigned genericVariantLock;
 /* Runtime-specialization base variant per (rt, tile mode): 1-based index
  * and state (0 none, 1 queued, 2 ready, 3 refused). */
 unsigned rtsBase[4][2],rtsState[4][2];
 /* Background jobs naming this pipeline (no object retain from the worker:
  * dealloc waits for them; vfDying makes queued ones skip). */
 unsigned vfPending,vfDying;
 /* Encode-time memos (exact; see tegu_render_plan_generic_cached and
  * tegu_render_generic_variant_select_locked). */
 struct tegu_render_plan_memo *genericPlanMemo;unsigned genericPlanMemoLock;
 struct tegu_render_variant_memo *genericVariantMemo;
 /* MRT emulation: translated with two BGRA8 targets; genericShader keeps
  * output 0, rt1Shader output 1 (each remapped to DATA0, one job each). */
 unsigned genericMrt,genericTrueMrt,rt1Draw,genericMainRt;
 unsigned genericPathClass,genericRtDescriptorActive,genericRtPreserve;
 /* Colour attachments the fragment module can read through framebuffer
  * fetch: bit i = a SubpassData input with InputAttachmentIndex i (0xf when
  * a subpass input carries no usable index). 0 = no framebuffer fetch. */
 unsigned genericFsSubpass;
 uint32_t genericTls;
 unsigned genericRtCount,genericRtActive;NSUInteger genericRtFormats[4];uint32_t genericRtMasks[4];
 uint32_t genericRtFormats32[TEGU_MALI_GENERIC_MAX_TARGETS];
 TeguAIRRenderBlend genericRtBlends[4];
 struct tegu_mali_graphics rt1Vertex,rt1Fragment;struct tegu_mali_graphics_pair2 rt1Pair;
 struct tegu_mali_generic_plan rt1Compile;struct tegu_generic_draw_shader rt1Shader;
 struct tegu_mali_varying_info rt1VaryingInfo;uint32_t rt1PairFlags,rt1SysvalMask;
 struct tegu_mali_graphics rt2Vertex,rt2Fragment;struct tegu_mali_graphics_pair2 rt2Pair;
 struct tegu_mali_generic_plan rt2Compile;struct tegu_generic_draw_shader rt2Shader;
 struct tegu_mali_varying_info rt2VaryingInfo;uint32_t rt2PairFlags,rt2SysvalMask;
 unsigned rt2Draw;
 struct tegu_mali_graphics rt3Vertex,rt3Fragment;struct tegu_mali_graphics_pair2 rt3Pair;
 struct tegu_mali_generic_plan rt3Compile;struct tegu_generic_draw_shader rt3Shader;
 struct tegu_mali_varying_info rt3VaryingInfo;uint32_t rt3PairFlags,rt3SysvalMask;
 unsigned rt3Draw;
 unsigned inert;char inertReason[160];
 unsigned rasterSampleCount;
 unsigned compositorSampleClass;
 U64 handle,allocated; NSUInteger colorFormat; unsigned depthFormat,stencilFormat; unsigned char *package; NSUInteger packageBytes;
}
- (id)device;
- (id)label;
- (NSUInteger)maxTotalThreadsPerThreadgroup;
- (signed char)supportIndirectCommandBuffers;
@end
static void tegu_render_generic_clear_variants(TeguRenderPipeline *);
static void tegu_vf_prewarm(TeguRenderPipeline *p,unsigned rt);
static void tegu_render_plan_memo_free(TeguRenderPipeline *);
@implementation TeguRenderPipeline
- (id)device {return owner;}
- (id)label {return debugLabel;}
- (NSUInteger)maxTotalThreadsPerThreadgroup {return 0;}
- (signed char)supportIndirectCommandBuffers {return 0;}
- (void)dealloc {
 __atomic_store_n(&vfDying,1,__ATOMIC_RELEASE);
 {unsigned spins=0;extern int sched_yield(void);extern int usleep(unsigned);
  while(__atomic_load_n(&vfPending,__ATOMIC_ACQUIRE)){if(++spins<64)(void)sched_yield();else (void)usleep(200);}}
 if(handle) {unsigned n=0;
  if(!owner->scalar(owner->connection,9,&handle,1,0,&n))
   __atomic_fetch_sub(&owner->allocatedBytes,allocated,__ATOMIC_RELAXED);
 }
 tegu_mali_graphics_free(&vertexCode);tegu_mali_graphics_free(&fragmentCode);
 tegu_mali_graphics_free(&rt1Vertex);tegu_mali_graphics_free(&rt1Fragment);
 tegu_mali_graphics_free(&rt2Vertex);tegu_mali_graphics_free(&rt2Fragment);
 tegu_mali_graphics_free(&rt3Vertex);tegu_mali_graphics_free(&rt3Fragment);
 tegu_render_generic_clear_variants(self);tegu_render_plan_memo_free(self);
 free(genericVsBytes);free(genericFsBytes);
 tegu_air_render_pipeline_close(compiled);free(package);
 [debugLabel release];[owner release];[super dealloc];
}
@end
#ifdef TEGU_INERT_PIPELINES
/* Runtime scope for the opt-in inert PSO: only the CoreAnimation render
 * server gets non-nil placeholders. Every other process keeps the explicit
 * nil/NSError refusal. The process predicate is compute's existing check. */
static int tegu_render_inert_pipeline_allowed(void) {
 return tegu_compositor_process_is_backboardd();
}
/* Bounded first-128 trace for one inert pipeline. `formats` is the descriptor
 * colour-format list in attachment order. */
static void tegu_render_inert_pipeline_trace(const char *vertex,const char *fragment,
 const U64 *formats,unsigned format_count,const char *reason) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=128)return;
 char fmt[64];size_t at=0;fmt[0]=0;
 for(unsigned i=0;i<format_count&&at+8<sizeof(fmt);i++)
  at+=(size_t)snprintf(fmt+at,sizeof(fmt)-at,"%s%lu",i?",":"",(unsigned long)formats[i]);
 if(!format_count)snprintf(fmt,sizeof(fmt),"-");
 char line[480];int n=snprintf(line,sizeof(line),"TEGU_INERT_PIPELINE v=%s f=%s fmt=%s reason=%.220s\n",
  vertex?vertex:"?",fragment?fragment:"?",fmt,reason?reason:"unsupported pipeline");
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static const char *tegu_render_safe_function_name(TeguFunction *f) {
 if(!f||!tegu_function_owned_class(f))return "?";
 id name=f->functionName;const char *s=name?[name UTF8String]:0;return s?s:"?";
}
static TeguRenderPipeline *tegu_render_create_inert(TeguComputeDevice *owner,const char *vertex,
 const char *fragment,const U64 *formats,unsigned format_count,const char *reason) {
 TeguRenderPipeline *p=[[TeguRenderPipeline alloc] init];
 if(!p)return 0;
 p->owner=[owner retain];p->inert=1;
 if(fragment)snprintf(p->genericName,sizeof(p->genericName),"%s",fragment);
 snprintf(p->inertReason,sizeof(p->inertReason),"%s",reason?reason:"unsupported render pipeline descriptor or shader metadata");
 tegu_render_inert_pipeline_trace(vertex,fragment,formats,format_count,
  reason?reason:"unsupported render pipeline descriptor or shader metadata");
 return p;
}
#endif
static int tegu_render_path_fragment(const char *fragment) {
 return fragment&&!__builtin_strncmp(fragment,"path_",5);
}
/* Compile a translated pipeline for the reflection-driven TGJ1 draw.  The
 * vertex fetch comes from the linked translator pipeline; only attributes the
 * compiled vertex shader reads are kept.  Blend is the descriptor's. */
/* Compile through the additive options entry. DRAW_PARAMETERS is always set
 * for generic pipelines so first_vertex/base_instance sysvals are lowered;
 * EXTENDED_VARYINGS permits flat/int/noperspective. Exact tables remain the
 * caller's. Goes through tegu_pc_mali_generic_options (pipeline cache: key
 * covers both modules, both tables and the options struct); variants are also
 * memoized per pipeline below with the options struct in their key. */
static int tegu_render_compile_generic_options(const void *vs,size_t vn,const void *fs,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vc_count,
 const struct tegu_mali_specialization *fc,size_t fc_count,
 unsigned flags,unsigned rt,struct tegu_mali_graphics *vo,struct tegu_mali_graphics *fo,
 struct tegu_mali_graphics_pair2 *pair,struct tegu_mali_generic_plan *plan,
 struct tegu_mali_varying_info *varying,const uint32_t *mrt_formats,unsigned mrt_count,
 char *message,size_t size) {
 memset(varying,0,sizeof(*varying));
 memset(vo,0,sizeof(*vo));memset(fo,0,sizeof(*fo));memset(pair,0,sizeof(*pair));memset(plan,0,sizeof(*plan));
 if(mrt_count) {
  if(!mrt_formats||mrt_count>TEGU_MALI_GENERIC_MAX_TARGETS) {
   snprintf(message,size,"MRT target list unsupported");return 1;
  }
  uint32_t target_formats[TEGU_MALI_GENERIC_MAX_TARGETS]={0};
  for(unsigned i=0;i<mrt_count;i++)target_formats[i]=mrt_formats[i];
  struct tegu_mali_generic_options2 options={TEGU_MALI_GENERIC_OPTIONS2_VERSION,
   (flags&~TEGU_MALI_GENERIC_OPTION_SELECT_RT)|TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|
   TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS|TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE|
   TEGU_MALI_GENERIC_OPTION_MULTI_TARGET,0,0,mrt_count,
   {target_formats[0],target_formats[1],target_formats[2],target_formats[3]},{0,0,0}};
  return tegu_pc_mali_generic_options(vs,vn,fs,fn,vc,vc_count,fc,fc_count,
   (const struct tegu_mali_generic_options *)&options,
   vo,fo,pair,plan,varying,message,size);
 }
 struct tegu_mali_generic_options options={TEGU_MALI_GENERIC_OPTIONS_VERSION,
  flags|TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS|
  TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE,rt,0};
 return tegu_pc_mali_generic_options(vs,vn,fs,fn,vc,vc_count,fc,fc_count,&options,
  vo,fo,pair,plan,varying,message,size);
}
/* Keep memory-fetch artifacts intact unless a separately cached tile
 * variant compiles successfully. Called with the compiler lock held. */
static void tegu_render_try_tile(unsigned rt,const TeguAIRBlob *vs,const TeguAIRBlob *fs,
 struct tegu_mali_graphics *vertex,struct tegu_mali_graphics *fragment,
 struct tegu_mali_graphics_pair2 *pair,struct tegu_mali_generic_plan *plan,
 struct tegu_mali_varying_info *varying) {
#ifdef TEGU_TILE_FETCH_DEFAULT
 if(!tegu_ab_tile_fetch()||plan->reads_framebuffer!=TEGU_MALI_GENERIC_FETCH_MEMORY)return;
 unsigned inputs=0;
 for(unsigned i=0;i<plan->count;i++)if(plan->bindings[i].kind==TEGU_MALI_GENERIC_INPUT_ATTACHMENT) {
  if(plan->bindings[i].input_attachment_index!=rt)return;
  inputs++;
 }
 if(!inputs)return;
 struct tegu_mali_graphics v={0},f={0};struct tegu_mali_graphics_pair2 linked={0};
 struct tegu_mali_generic_plan tile={0};struct tegu_mali_varying_info tv={0};char error[192]={0};
 int rc=tegu_render_compile_generic_options(vs->bytes,vs->length,fs->bytes,fs->length,
  NULL,0,NULL,0,TEGU_MALI_GENERIC_OPTION_SELECT_RT|TEGU_MALI_GENERIC_OPTION_TILE_FETCH,rt,
  &v,&f,&linked,&tile,&tv,NULL,0,error,sizeof(error));
 if(!rc&&tile.reads_framebuffer==TEGU_MALI_GENERIC_FETCH_TILE) {
  tegu_mali_graphics_free(vertex);tegu_mali_graphics_free(fragment);
  *vertex=v;*fragment=f;*pair=linked;*plan=tile;
  if(varying)*varying=tv;
 } else {tegu_mali_graphics_free(&v);tegu_mali_graphics_free(&f);if(!rc)rc=-1;}
 static unsigned traces;
 if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<24) {
  char line[240];int n=snprintf(line,sizeof(line),"TEGU_TILE_PIPELINE rt=%u enabled=%u rc=%d reason=%.150s\n",rt,rc==0,rc,error);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
#else
 (void)rt;(void)vs;(void)fs;(void)vertex;(void)fragment;(void)pair;(void)plan;(void)varying;
#endif
}
static unsigned tegu_render_spirv_declares_subpass(const uint8_t *bytes,size_t length);
/* Tile first. A framebuffer-fetch pipeline used to compile its memory-fetch
 * form, then a tile-fetch form that replaced it whenever the latter compiled
 * to TILE with its input attachments at rt: two driver compiles, the first
 * thrown away on every tile success (c6m: 52 of 168 Mali misses). The
 * accepted result is the same tile compile (same inputs, options, cache key);
 * the attachment-index rule is checked on the tile plan, whose input
 * attachments are the module's own like the memory plan's. Only when the
 * tile compile is refused or not TILE does the memory form compile, and the
 * old second tile attempt is then skipped (it would compile the same refused
 * key again). One difference: a module whose memory form would be refused
 * but whose tile form compiles now draws with the tile form. Called with the
 * compiler lock held; returns 1 with the outputs filled, else 0 untouched. */
static int tegu_render_tile_first(unsigned rt,const void *vs,size_t vn,const void *fs,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vcn,const struct tegu_mali_specialization *fc,size_t fcn,
 unsigned flags,int allAtRt,struct tegu_mali_graphics *vertex,struct tegu_mali_graphics *fragment,
 struct tegu_mali_graphics_pair2 *pair,struct tegu_mali_generic_plan *plan,struct tegu_mali_varying_info *varying,
 int *attempted) {
 *attempted=0;
#ifdef TEGU_TILE_FETCH_DEFAULT
 if(!tegu_ab_tile_fetch()||!tegu_render_spirv_declares_subpass(fs,fn))return 0;
 *attempted=1;
 struct tegu_mali_graphics v={0},f={0};struct tegu_mali_graphics_pair2 linked={0};
 struct tegu_mali_generic_plan tile={0};struct tegu_mali_varying_info tv={0};char error[192]={0};
 int rc=tegu_render_compile_generic_options(vs,vn,fs,fn,vc,vcn,fc,fcn,
  flags|TEGU_MALI_GENERIC_OPTION_SELECT_RT|TEGU_MALI_GENERIC_OPTION_TILE_FETCH,rt,
  &v,&f,&linked,&tile,&tv,NULL,0,error,sizeof(error));
 unsigned inputs=0;int ok=!rc&&tile.reads_framebuffer==TEGU_MALI_GENERIC_FETCH_TILE;
 for(unsigned i=0;ok&&i<tile.count;i++)if(tile.bindings[i].kind==TEGU_MALI_GENERIC_INPUT_ATTACHMENT) {
  if(tile.bindings[i].input_attachment_index==rt)inputs++;else if(allAtRt)ok=0;
 }
 if(ok&&inputs) {
  *vertex=v;*fragment=f;*pair=linked;*plan=tile;if(varying)*varying=tv;
  static unsigned traces;
  if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<24) {
   char line[96];int n=snprintf(line,sizeof(line),"TEGU_TILE_PIPELINE rt=%u enabled=1 rc=0 first=1\n",rt);
   if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
  }
  return 1;
 }
 tegu_mali_graphics_free(&v);tegu_mali_graphics_free(&f);
#else
 (void)rt;(void)vs;(void)vn;(void)fs;(void)fn;(void)vc;(void)vcn;(void)fc;(void)fcn;(void)flags;(void)allAtRt;
 (void)vertex;(void)fragment;(void)pair;(void)plan;(void)varying;
#endif
 return 0;
}
/* Detect a SPIR-V VertexIndex read without trusting name strings. OpDecorate
 * BuiltIn VertexIndex is decoration 11, value 42 (SPIR-V 1.0-1.6). The
 * translator emits it for Metal vertex_id. A conservative scan is intentional:
 * a declaration that no longer reaches the entry point still refuses rather
 * than risking a wrong hardware identity. */
#define TEGU_SPIRV_MAGIC 0x07230203u
#define TEGU_SPIRV_OP_DECORATE 71u
#define TEGU_SPIRV_DECORATION_BUILTIN 11u
#define TEGU_SPIRV_BUILTIN_VERTEX_INDEX 42u
static int tegu_render_spirv_uses_vertex_index(const uint8_t *bytes,size_t length) {
 if(!bytes||length<20||length>0x7fffffffu||(length&3u))return 0;
 const uint32_t *w=(const uint32_t *)bytes;
 if(w[0]!=TEGU_SPIRV_MAGIC||w[4]!=0)return 0; /* schema word is zero */
 size_t words=length/4,at=5; /* five-word SPIR-V header */
 while(at<words) {
  uint32_t first=w[at];size_t count=first>>16;
  if(!count||count>words-at)return 0;
  if((first&0xffffu)==TEGU_SPIRV_OP_DECORATE&&count>=4&&
     w[at+2]==TEGU_SPIRV_DECORATION_BUILTIN&&w[at+3]==TEGU_SPIRV_BUILTIN_VERTEX_INDEX)return 1;
  at+=count;
 }
 return 0;
}
/* OpTypeImage (25) with Dim SubpassData (6): the only SPIR-V form of Metal
 * framebuffer fetch produced by the pinned translator. Specialization cannot
 * add a type, so a module without it can never read an attachment. */
#define TEGU_SPIRV_OP_TYPE_IMAGE 25u
#define TEGU_SPIRV_DIM_SUBPASS_DATA 6u
#define TEGU_SPIRV_DECORATION_INPUT_ATTACHMENT_INDEX 43u
/* Returns the attachment mask a module may read via framebuffer fetch.
 * InputAttachmentIndex is a static decoration, so specialization can only
 * remove reads, never add another attachment. Unknown -> all four. */
static unsigned tegu_render_spirv_declares_subpass(const uint8_t *bytes,size_t length) {
 if(!bytes||length<20||length>0x7fffffffu||(length&3u))return 0xfu;
 const uint32_t *w=(const uint32_t *)bytes;
 if(w[0]!=TEGU_SPIRV_MAGIC||w[4]!=0)return 0xfu;
 size_t words=length/4,at=5;unsigned subpass=0,mask=0;
 while(at<words) {
  uint32_t first=w[at];size_t count=first>>16;
  if(!count||count>words-at)return 0xfu;
  if((first&0xffffu)==TEGU_SPIRV_OP_TYPE_IMAGE&&count>=4&&w[at+3]==TEGU_SPIRV_DIM_SUBPASS_DATA)subpass=1;
  if((first&0xffffu)==TEGU_SPIRV_OP_DECORATE&&count>=4&&w[at+2]==TEGU_SPIRV_DECORATION_INPUT_ATTACHMENT_INDEX)
   mask|=w[at+3]<4?1u<<w[at+3]:0xfu;
  at+=count;
 }
 if(!subpass)return 0;
 return mask?mask:0xfu;
}
static void tegu_render_generic_clear_variants(TeguRenderPipeline *p) {
 if(!p)return;
 for(unsigned i=0;i<p->genericVariantCount;i++) {
  if(p->genericVariants[i].shared)continue;
  tegu_mali_graphics_free(&p->genericVariants[i].vertex);tegu_mali_graphics_free(&p->genericVariants[i].fragment);
 }
 free(p->genericVariants);p->genericVariants=0;
 p->genericVariantCount=p->genericVariantCapacity=0;
 free(p->genericVariantMemo);p->genericVariantMemo=0;
}
/* Copy the selected SPIR-V modules into the pipeline because specialized
 * variants are compiled per draw after the caller's blobs are gone. Replacing
 * the modules invalidates every previously cached variant. */
static int tegu_render_generic_set_modules(TeguRenderPipeline *p,const TeguAIRBlob *vs,const TeguAIRBlob *fs) {
 if(!p||!vs||!fs||!vs->bytes||!fs->bytes)return -1;
 uint8_t *v=malloc(vs->length),*f=malloc(fs->length);
 if(!v||!f){free(v);free(f);return -1;}
 memcpy(v,vs->bytes,vs->length);memcpy(f,fs->bytes,fs->length);
 free(p->genericVsBytes);free(p->genericFsBytes);
 p->genericVsBytes=v;p->genericFsBytes=f;p->genericVsLength=vs->length;p->genericFsLength=fs->length;
 {struct tegu_pc_sha256 h;tegu_pc_sha256_init(&h);tegu_pc_key_str(&h,"TGRGM1");
  tegu_pc_key_bytes(&h,v,vs->length);tegu_pc_key_bytes(&h,f,fs->length);tegu_pc_sha256_final(&h,p->genericModuleDigest);}
 free(p->genericVariantMemo);p->genericVariantMemo=0; /* it names variants of the old modules */
 memset(p->rtsBase,0,sizeof(p->rtsBase));memset(p->rtsState,0,sizeof(p->rtsState));
 /* Do not free old variants here: a queued Dispatch may still hold an index
  * into the fixed array. Variants are keyed by the module bytes, so entries
  * from the previous module generation can never be selected again. */
 return 0;
}
static int tegu_render_generic_prepare(TeguRenderPipeline *p,const TeguAIRBlob *vs,const TeguAIRBlob *fs,
 const TeguAIRVertexLayout *layouts,NSUInteger layoutCount,const struct tegu_blend_inputs *blend,char *message,size_t size) {
 /* layoutCount 0: a vertex-pulling shader (vertex_id + plain buffer). */
 if(!p||!p->compiled||!vs||!fs||!vs->bytes||!fs->bytes||(layoutCount&&!layouts)||layoutCount>8||!blend)return -1;
 tegu_render_plan_memo_free(p); /* layouts/compiled may change below */
 struct tegu_blend_state checkedBlend;
 if(tegu_blend_build_format(p->colorFormat?p->colorFormat:80,blend,&checkedBlend)){snprintf(message,size,"generic blend equation unsupported");return -1;}
 TeguAIRVertexFetch fetch;memset(&fetch,0,sizeof(fetch));
 if(tegu_air_render_pipeline_vertex_fetch(p->compiled,&fetch,message,size))return -1;
 tegu_mali_graphics_free(&p->vertexCode);tegu_mali_graphics_free(&p->fragmentCode);
 memset(&p->pair,0,sizeof(p->pair));memset(&p->genericCompile,0,sizeof(p->genericCompile));
 tegu_lock(&tegu_compiler_lock);
 struct tegu_mali_varying_info varying={0};
 int tileTried=0,failed=0;
 if(!p->genericTrueMrt&&tegu_render_tile_first(p->genericMrt?p->genericMainRt:0,vs->bytes,vs->length,fs->bytes,fs->length,
    NULL,0,NULL,0,0,1,&p->vertexCode,&p->fragmentCode,&p->pair,&p->genericCompile,&varying,&tileTried)) {}
 else {
  failed=tegu_render_compile_generic_options(vs->bytes,vs->length,fs->bytes,fs->length,
   NULL,0,NULL,0,(p->genericMrt&&!p->genericTrueMrt)?TEGU_MALI_GENERIC_OPTION_SELECT_RT:0,
   p->genericTrueMrt?0:(p->genericMrt?p->genericMainRt:0),
   &p->vertexCode,&p->fragmentCode,&p->pair,&p->genericCompile,&varying,
   p->genericTrueMrt?p->genericRtFormats32:NULL,p->genericTrueMrt?p->genericRtCount:0,message,size);
  if(!failed&&!p->genericTrueMrt&&!tileTried)tegu_render_try_tile(p->genericMrt?p->genericMainRt:0,vs,fs,
   &p->vertexCode,&p->fragmentCode,&p->pair,&p->genericCompile,&varying);
 }
 tegu_unlock(&tegu_compiler_lock);
 if(failed)return -1;
 if(p->genericTrueMrt) {
  uint32_t written=p->genericCompile.reserved&~TEGU_MALI_GENERIC_PLAN_MRT;
  uint32_t expected=p->genericRtActive&((1u<<p->genericRtCount)-1u);
  if(!(p->genericCompile.reserved&TEGU_MALI_GENERIC_PLAN_MRT)||!written||(written&~expected)||
     (written!=expected&&!p->genericPathClass)) {
   snprintf(message,size,"MRT written-output mask %x differs from bound mask %x",written,expected);return -1;
  }
  p->genericRtActive=written;
 }
 p->genericVaryingInfo=varying;p->genericPairFlags=p->pair.flags;p->genericSysvalMask=p->vertexCode.sysval_mask;
 p->genericTls=p->vertexCode.tls>p->fragmentCode.tls?p->vertexCode.tls:p->fragmentCode.tls;
 if(p->genericMainRt<4)p->genericRtBlends[p->genericMainRt]=*blend;
 struct tegu_generic_draw_shader *sh=&p->genericShader;memset(sh,0,sizeof(*sh));
 sh->flags=p->genericCompile.reads_framebuffer==TEGU_MALI_GENERIC_FETCH_TILE?TEGU_GENERIC_SHADER_TILE_FETCH:0;
 uint32_t covered=0;
 for(unsigned i=0;i<fetch.count&&i<TEGU_VERTEX_SLOTS;i++) {
  const struct tegu_vertex_attribute *a=&fetch.attributes[i];
  if(a->location>=32||!(p->pair.vs_attribute_mask&(1u<<a->location)))continue;
  if(sh->attribute_count>=TEGU_GENERIC_DRAW_MAX_ATTRIBUTES)return -1;
  sh->attributes[sh->attribute_count++]=*a;covered|=1u<<a->location;
 }
 if((!sh->attribute_count&&p->pair.vs_attribute_mask)||covered!=p->pair.vs_attribute_mask) {
  snprintf(message,size,"generic vertex fetch 0x%x does not cover compiled attributes 0x%x",covered,p->pair.vs_attribute_mask);
  return -1;
 }
 sh->vertex_code=p->vertexCode.code;sh->fragment_code=p->fragmentCode.code;
 sh->vertex_bytes=(uint32_t)p->vertexCode.size;sh->fragment_bytes=(uint32_t)p->fragmentCode.size;
 sh->vertex_registers=p->vertexCode.registers;sh->fragment_registers=p->fragmentCode.registers;
 sh->secondary_registers=p->pair.secondary_registers;
 sh->vertex_fau_words=p->vertexCode.fau_words;sh->fragment_fau_words=p->fragmentCode.fau_words;
 sh->position_offset=p->pair.position_offset;sh->secondary_offset=p->pair.secondary_offset;
 sh->varying_bytes=p->pair.generic_size_B;sh->vertex_preload=p->vertexCode.preload;
 sh->fragment_preload=p->fragmentCode.preload;sh->secondary_preload=p->pair.secondary_preload;
 sh->blend=*blend;
 for(NSUInteger i=0;i<layoutCount;i++)p->genericLayouts[i]=layouts[i];
 p->genericLayoutCount=(unsigned)layoutCount;p->genericVertexIndex=tegu_render_spirv_uses_vertex_index(vs->bytes,vs->length);
 p->genericFsSubpass=tegu_render_spirv_declares_subpass(fs->bytes,fs->length);
 p->genericFetch=fetch;
 if(tegu_render_generic_set_modules(p,vs,fs)) {
  snprintf(message,size,"generic module copy failed");return -1;
 }
 p->genericDraw=1;if(!p->colorFormat)p->colorFormat=80;
 tegu_vf_prewarm(p,p->genericMrt?p->genericMainRt:0); /* runtime-specialization fallback, background */
 return 0;
}
/* Second colour output of an MRT pipeline: same vertex stage and fetch, the
 * fragment keeps output 1. Absent when the shader never writes color(1). */
static int tegu_render_generic_prepare_rt(TeguRenderPipeline *p,unsigned rt,
 const TeguAIRBlob *vs,const TeguAIRBlob *fs,const struct tegu_blend_inputs *blend,
 char *message,size_t size) {
 if(!p||!p->genericDraw||!p->genericMrt||!vs||!fs||!blend||rt<1||rt>3)return -1;
 struct tegu_mali_graphics *rv=0,*rf=0;struct tegu_mali_graphics_pair2 *rp=0;
 struct tegu_mali_generic_plan *rc=0;struct tegu_generic_draw_shader *rs=0;
 struct tegu_mali_varying_info *rvi=0;uint32_t *rpf=0,*rsv=0;unsigned *rd=0;
 if(rt==1){rv=&p->rt1Vertex;rf=&p->rt1Fragment;rp=&p->rt1Pair;rc=&p->rt1Compile;rs=&p->rt1Shader;
  rvi=&p->rt1VaryingInfo;rpf=&p->rt1PairFlags;rsv=&p->rt1SysvalMask;rd=&p->rt1Draw;}
 else if(rt==2){rv=&p->rt2Vertex;rf=&p->rt2Fragment;rp=&p->rt2Pair;rc=&p->rt2Compile;rs=&p->rt2Shader;
  rvi=&p->rt2VaryingInfo;rpf=&p->rt2PairFlags;rsv=&p->rt2SysvalMask;rd=&p->rt2Draw;}
 else {rv=&p->rt3Vertex;rf=&p->rt3Fragment;rp=&p->rt3Pair;rc=&p->rt3Compile;rs=&p->rt3Shader;
  rvi=&p->rt3VaryingInfo;rpf=&p->rt3PairFlags;rsv=&p->rt3SysvalMask;rd=&p->rt3Draw;}
 NSUInteger tf=p->genericRtFormats[rt]?p->genericRtFormats[rt]:p->colorFormat;
 struct tegu_blend_state checked;
 if(tegu_blend_build_format((unsigned)tf,blend,&checked)){snprintf(message,size,"rt%u blend equation unsupported",rt);return -1;}
 tegu_mali_graphics_free(rv);tegu_mali_graphics_free(rf);
 memset(rp,0,sizeof(*rp));memset(rc,0,sizeof(*rc));*rd=0;
 tegu_lock(&tegu_compiler_lock);
 struct tegu_mali_varying_info varying={0};
 int tileTried=0,failed=0;
 if(tegu_render_tile_first(rt,vs->bytes,vs->length,fs->bytes,fs->length,NULL,0,NULL,0,0,1,rv,rf,rp,rc,&varying,&tileTried)) {}
 else {
  failed=tegu_render_compile_generic_options(vs->bytes,vs->length,fs->bytes,fs->length,
   NULL,0,NULL,0,TEGU_MALI_GENERIC_OPTION_SELECT_RT|TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE,rt,
   rv,rf,rp,rc,&varying,NULL,0,message,size);
  if(!failed&&!tileTried)tegu_render_try_tile(rt,vs,fs,rv,rf,rp,rc,&varying);
 }
 tegu_unlock(&tegu_compiler_lock);
 if(failed)return -1;
 *rvi=varying;*rpf=rp->flags;*rsv=rv->sysval_mask;
 if(rv->tls>p->genericTls)p->genericTls=rv->tls;if(rf->tls>p->genericTls)p->genericTls=rf->tls;
 if(rp->vs_attribute_mask!=p->pair.vs_attribute_mask){snprintf(message,size,"rt%u vertex inputs differ",rt);return -1;}
 *rs=p->genericShader;
 rs->flags=rc->reads_framebuffer==TEGU_MALI_GENERIC_FETCH_TILE?TEGU_GENERIC_SHADER_TILE_FETCH:0;
 rs->vertex_code=rv->code;rs->fragment_code=rf->code;
 rs->vertex_bytes=(uint32_t)rv->size;rs->fragment_bytes=(uint32_t)rf->size;
 rs->vertex_registers=rv->registers;rs->fragment_registers=rf->registers;
 rs->secondary_registers=rp->secondary_registers;
 rs->vertex_fau_words=rv->fau_words;rs->fragment_fau_words=rf->fau_words;
 rs->position_offset=rp->position_offset;rs->secondary_offset=rp->secondary_offset;
 rs->varying_bytes=rp->generic_size_B;rs->vertex_preload=rv->preload;
 rs->fragment_preload=rf->preload;rs->secondary_preload=rp->secondary_preload;
 rs->blend=*blend;*rd=1;
 return 0;
}
/* Path pipelines may carry active descriptor masks on attachments the fragment
 * never writes. The compiler's full-MRT plan is authoritative for the written
 * set: preserve unwritten attachments (no clear/job), and use per-RT jobs when
 * the written set is a single target or has holes. Non-path pipelines keep the
 * strict equality check. */
static int tegu_render_path_reconcile(TeguRenderPipeline *p,const TeguAIRBlob *vs,const TeguAIRBlob *fs,
 const TeguAIRVertexLayout *layouts,NSUInteger layoutCount,const struct tegu_blend_inputs *gb,
 char *message,size_t size) {
 if(!p||!p->genericTrueMrt||!p->genericPathClass)return 0;
 uint32_t written=p->genericRtActive&0xfu;
 if(!written){snprintf(message,size,"path MRT wrote no colour target");return -1;}
 unsigned bits=0;for(unsigned rt=0;rt<4;rt++)if(written&(1u<<rt))bits++;
 if(bits==1) {
  unsigned only=0;while(!(written&(1u<<only)))only++;
  p->genericTrueMrt=0;p->genericMrt=1;p->genericMainRt=only;
  p->colorFormat=p->genericRtFormats[only];p->blend=p->genericRtBlends[only];
  if(tegu_render_generic_prepare(p,vs,fs,layouts,layoutCount,&gb[only],message,size))return -1;
  p->genericRtActive=written;p->genericRtPreserve=p->genericRtDescriptorActive&~written;
  return 0;
 }
 unsigned high=0;while(high<3&&(written&(1u<<(high+1))))high++;
 int contiguous=(written==((1u<<(high+1))-1));
 if(contiguous) {
  p->genericRtCount=high+1;p->genericRtPreserve=p->genericRtDescriptorActive&~written;
  return 0;
 }
 unsigned first=0;while(!(written&(1u<<first)))first++;
 p->genericTrueMrt=0;p->genericMrt=1;p->genericMainRt=first;p->genericRtActive=written;
 p->colorFormat=p->genericRtFormats[first];p->blend=p->genericRtBlends[first];
 if(tegu_render_generic_prepare(p,vs,fs,layouts,layoutCount,&gb[first],message,size))return -1;
 for(unsigned rt=0;rt<4;rt++)if(rt!=first&&(written&(1u<<rt))) {
  if(tegu_render_generic_prepare_rt(p,rt,vs,fs,&gb[rt],message,size))return -1;
 }
 p->genericRtPreserve=p->genericRtDescriptorActive&~written;
 return 0;
}
/* Fill the execution shader from one variant's compiler outputs. Attribute
 * coverage is rechecked for that variant because specialization can prune a
 * vertex read; the encoder only accepts a variant whose fetch shape matches
 * the default pipeline (see tegu_render_generic_variant_compatible). */
static int tegu_render_generic_variant_shader(TeguRenderPipeline *p,
 const struct tegu_mali_graphics *vertex,const struct tegu_mali_graphics *fragment,
 const struct tegu_mali_graphics_pair2 *pair,const struct tegu_mali_generic_plan *compile,
 struct tegu_generic_draw_shader *sh,char *message,size_t size) {
 if(!p||!vertex||!fragment||!pair||!compile||!sh)return -1;
 memset(sh,0,sizeof(*sh));
 sh->flags=compile->reads_framebuffer==TEGU_MALI_GENERIC_FETCH_TILE?TEGU_GENERIC_SHADER_TILE_FETCH:0;
 uint32_t covered=0;
 for(unsigned i=0;i<p->genericFetch.count&&i<TEGU_VERTEX_SLOTS;i++) {
  const struct tegu_vertex_attribute *a=&p->genericFetch.attributes[i];
  if(a->location>=32||!(pair->vs_attribute_mask&(1u<<a->location)))continue;
  if(sh->attribute_count>=TEGU_GENERIC_DRAW_MAX_ATTRIBUTES) {
   snprintf(message,size,"specialized vertex fetch exceeds attribute bound");return -1;
  }
  sh->attributes[sh->attribute_count++]=*a;covered|=1u<<a->location;
 }
 if((!sh->attribute_count&&pair->vs_attribute_mask)||covered!=pair->vs_attribute_mask) {
  snprintf(message,size,"specialized vertex fetch 0x%x does not cover compiled attributes 0x%x",
   covered,pair->vs_attribute_mask);return -1;
 }
 sh->vertex_code=vertex->code;sh->fragment_code=fragment->code;
 sh->vertex_bytes=(uint32_t)vertex->size;sh->fragment_bytes=(uint32_t)fragment->size;
 sh->vertex_registers=vertex->registers;sh->fragment_registers=fragment->registers;
 sh->secondary_registers=pair->secondary_registers;
 sh->vertex_fau_words=vertex->fau_words;sh->fragment_fau_words=fragment->fau_words;
 sh->position_offset=pair->position_offset;sh->secondary_offset=pair->secondary_offset;
 sh->varying_bytes=pair->generic_size_B;sh->vertex_preload=vertex->preload;
 sh->fragment_preload=fragment->preload;sh->secondary_preload=pair->secondary_preload;
 sh->blend=p->genericShader.blend;
 return 0;
}
static int tegu_render_generic_variant_compatible(const TeguRenderPipeline *p,
 const struct tegu_generic_draw_shader *sh,char *message,size_t size) {
 const struct tegu_generic_draw_shader *d=&p->genericShader;
 if(sh->attribute_count!=d->attribute_count) {
  snprintf(message,size,"specialized vertex attribute count changed");return 0;
 }
 for(uint32_t i=0;i<sh->attribute_count;i++) {
  const struct tegu_vertex_attribute *a=&sh->attributes[i],*b=&d->attributes[i];
  if(a->location!=b->location||a->format!=b->format||a->offset!=b->offset||
     a->buffer_index!=b->buffer_index||a->stride!=b->stride||a->input_rate!=b->input_rate||
     a->divisor!=b->divisor) {
   snprintf(message,size,"specialized vertex fetch differs from the default layout");return 0;
  }
 }
 return 1;
}
/* Compile one immutable specialized variant. The tables are the planner's
 * exact per-stage values; no nearest/default projection is performed. */
static int tegu_render_generic_variant_compile_modules(TeguRenderPipeline *p,unsigned rt,
 const TeguAIRGraphicsSpecializationPlan *spec,struct tegu_render_generic_variant *v,
 char *message,size_t size,const uint8_t *vsBytes,NSUInteger vsLength,const uint8_t *fsBytes,NSUInteger fsLength,
 unsigned extraFlags) {
 if(!p||!spec||!v||spec->version!=1||spec->vertex_count>TEGU_AIR_GRAPHICS_SPEC_MAX||
    spec->fragment_count>TEGU_AIR_GRAPHICS_SPEC_MAX||spec->specialization_length>512)return -1;
 memset(v,0,sizeof(*v));v->rt=rt;
 v->vertex_constant_count=spec->vertex_count;v->fragment_constant_count=spec->fragment_count;
 v->specialization_length=spec->specialization_length;
 const struct tegu_mali_specialization *vc=(const struct tegu_mali_specialization *)spec->vertex_constants;
 const struct tegu_mali_specialization *fc=(const struct tegu_mali_specialization *)spec->fragment_constants;
#ifdef TEGU_FRONTEND_HOST
 if(getenv("TEGU_TRACE_VARIANTS")) {
  fprintf(stderr,"TEGU_VARIANT_TRACE fs=%s rt=%u vc=",p->genericName,rt);
  for(uint32_t i=0;i<spec->vertex_count;i++){const TeguAIRGraphicsSpecConstant *c=&spec->vertex_constants[i];uint64_t x=0;memcpy(&x,c->data,8);fprintf(stderr,"%u:%u:%llx,",c->id,c->size,(unsigned long long)x);}
  fprintf(stderr," fc=");
  for(uint32_t i=0;i<spec->fragment_count;i++){const TeguAIRGraphicsSpecConstant *c=&spec->fragment_constants[i];uint64_t x=0;memcpy(&x,c->data,8);fprintf(stderr,"%u:%u:%llx,",c->id,c->size,(unsigned long long)x);}
  fprintf(stderr," speclen=%u\n",spec->specialization_length);
 }
#endif
 unsigned compileRt=rt;
 unsigned flags=((p->genericMrt&&!p->genericTrueMrt)?TEGU_MALI_GENERIC_OPTION_SELECT_RT:0)|
  TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE|(p->genericTrueMrt?TEGU_MALI_GENERIC_OPTION_MULTI_TARGET:0)|extraFlags;
 struct tegu_mali_varying_info varying={0};
 int tileTried=0,tileFirst=0,status=0;
 tegu_lock(&tegu_compiler_lock);
 if(!p->genericTrueMrt)tileFirst=tegu_render_tile_first(compileRt,vsBytes,vsLength,fsBytes,fsLength,
  vc,spec->vertex_count,fc,spec->fragment_count,flags,0,&v->vertex,&v->fragment,&v->pair,&v->compile,&varying,&tileTried);
 if(!tileFirst)status=tegu_render_compile_generic_options(vsBytes,vsLength,
  fsBytes,fsLength,vc,spec->vertex_count,fc,spec->fragment_count,
  flags,compileRt,&v->vertex,&v->fragment,&v->pair,&v->compile,&varying,
  p->genericTrueMrt?p->genericRtFormats32:NULL,p->genericTrueMrt?p->genericRtCount:0,message,size);
 tegu_unlock(&tegu_compiler_lock);
 if(status)return -1;
 if(p->genericTrueMrt) {
  uint32_t written=v->compile.reserved&~TEGU_MALI_GENERIC_PLAN_MRT;
  if(!(v->compile.reserved&TEGU_MALI_GENERIC_PLAN_MRT)||written!=p->genericRtActive) {
   snprintf(message,size,"MRT variant written-output mask %x differs from %x",written,p->genericRtActive);
   tegu_mali_graphics_free(&v->vertex);tegu_mali_graphics_free(&v->fragment);return -1;
  }
 }
 v->varying=varying;v->options_flags=flags|TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|
  TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS;
 if(tileFirst)v->options_flags|=TEGU_MALI_GENERIC_OPTION_SELECT_RT|TEGU_MALI_GENERIC_OPTION_TILE_FETCH;
#ifdef TEGU_TILE_FETCH_DEFAULT
 /* Mirror the default path's separately cached tile variant, using the same
  * specialization tables. A tile refusal keeps the memory-fetch variant. */
 if(!tileTried&&tegu_ab_tile_fetch()&&!p->genericTrueMrt&&v->compile.reads_framebuffer==TEGU_MALI_GENERIC_FETCH_MEMORY) {
  unsigned inputs=0;
  for(unsigned i=0;i<v->compile.count;i++)if(v->compile.bindings[i].kind==TEGU_MALI_GENERIC_INPUT_ATTACHMENT&&
     v->compile.bindings[i].input_attachment_index==rt)inputs++;
  if(inputs) {
   struct tegu_mali_graphics tv={0},tf={0};struct tegu_mali_graphics_pair2 tp={0};
   struct tegu_mali_generic_plan tplan={0};struct tegu_mali_varying_info tvarying={0};char terror[192]={0};
   tegu_lock(&tegu_compiler_lock);
   int tile=tegu_render_compile_generic_options(vsBytes,vsLength,
    fsBytes,fsLength,vc,spec->vertex_count,fc,spec->fragment_count,
    flags|TEGU_MALI_GENERIC_OPTION_SELECT_RT|TEGU_MALI_GENERIC_OPTION_TILE_FETCH,compileRt,
    &tv,&tf,&tp,&tplan,&tvarying,NULL,0,terror,sizeof(terror));
   tegu_unlock(&tegu_compiler_lock);
   if(!tile&&tplan.reads_framebuffer==TEGU_MALI_GENERIC_FETCH_TILE) {
    tegu_mali_graphics_free(&v->vertex);tegu_mali_graphics_free(&v->fragment);
    v->vertex=tv;v->fragment=tf;v->pair=tp;v->compile=tplan;v->varying=tvarying;
    v->options_flags|=TEGU_MALI_GENERIC_OPTION_SELECT_RT|TEGU_MALI_GENERIC_OPTION_TILE_FETCH;
   } else {tegu_mali_graphics_free(&tv);tegu_mali_graphics_free(&tf);}
  }
 }
#endif
 v->tls=v->vertex.tls>v->fragment.tls?v->vertex.tls:v->fragment.tls;
 v->pair_flags=v->pair.flags;v->sysval_mask=v->vertex.sysval_mask;
 if(tegu_render_generic_variant_shader(p,&v->vertex,&v->fragment,&v->pair,&v->compile,
    &v->shader,message,size)||
    !tegu_render_generic_variant_compatible(p,&v->shader,message,size)) {
  tegu_mali_graphics_free(&v->vertex);tegu_mali_graphics_free(&v->fragment);return -1;
 }
 if(rt<4)v->shader.blend=p->genericRtBlends[rt];
 v->binding_count=spec->binding_count>TEGU_GENERIC_DRAW_MAX_BINDINGS?
  TEGU_GENERIC_DRAW_MAX_BINDINGS:spec->binding_count;
 memcpy(v->bindings,spec->bindings,v->binding_count*sizeof(v->bindings[0]));
 return 0;
}
/* A specialization plan of a translator variant (reserved[0]==1) compiles
 * the variant's own SPIR-V (tegu_air_render_pipeline_variant_spirv; the
 * variant key already hashes the whole plan); a newly compiled translator
 * variant is stored with its pipeline so caches and packs carry it. */
static int tegu_render_generic_variant_compile(TeguRenderPipeline *p,unsigned rt,
 const TeguAIRGraphicsSpecializationPlan *spec,struct tegu_render_generic_variant *v,
 char *message,size_t size) {
 if(!p||!spec)return -1;
 if(!spec->reserved[0])return tegu_render_generic_variant_compile_modules(p,rt,spec,v,message,size,
  p->genericVsBytes,p->genericVsLength,p->genericFsBytes,p->genericFsLength,0);
 if(spec->reserved[0]!=1||!p->compiled){snprintf(message,size,"unknown translator variant form");return -1;}
 TeguAIRBlob vsb={0},fsb={0},meta={0};char error[192]={0};
 if(tegu_air_render_pipeline_variant_spirv(p->compiled,spec->reserved[1],spec->reserved[2],&vsb,&fsb,&meta,error,sizeof(error))||
    !vsb.bytes||!fsb.bytes) {
  snprintf(message,size,"translator variant SPIR-V unavailable: %s",error);
  tegu_air_blob_free(&vsb);tegu_air_blob_free(&fsb);tegu_air_blob_free(&meta);return -1;
 }
 int status=tegu_render_generic_variant_compile_modules(p,rt,spec,v,message,size,vsb.bytes,vsb.length,fsb.bytes,fsb.length,0);
 tegu_air_blob_free(&vsb);tegu_air_blob_free(&fsb);tegu_air_blob_free(&meta);
 if(!status)tegu_pc_render_variants_store(p->compiled);
 return status;
}
/* The variant key is a SHA-256 over the planner's 3.3 KB specialization
 * record (tens of us per draw); UI draws repeat a few records. Remember the
 * variant chosen for the last identical (rt, options, tile mode, record):
 * every other key input is a pipeline constant, and the modules (the only
 * mutable one) clear this memo when they change. */
#define TEGU_VARIANT_MEMO_WAYS 8u
struct tegu_render_variant_memo {
 struct {unsigned index,rt,flags,tile;TeguAIRGraphicsSpecializationPlan spec;} e[TEGU_VARIANT_MEMO_WAYS];
 unsigned next;
};
#ifdef TEGU_FRONTEND_HOST
/* Host builds re-run the planner/key behind every memo hit and abort on any
 * byte difference (TEGU_PLAN_MEMO_VERIFY=0 turns this off for timing). */
static int tegu_plan_memo_verify(void) {
 static int state=-1;
 if(state<0){const char *env=getenv("TEGU_PLAN_MEMO_VERIFY");state=!(env&&env[0]=='0');}
 return state;
}
#endif
static unsigned tegu_render_variant_tile_mode(void) {
#ifdef TEGU_TILE_FETCH_DEFAULT
 return (unsigned)tegu_ab_tile_fetch();
#else
 return 0;
#endif
}
static void tegu_render_generic_variant_key(TeguRenderPipeline *p,unsigned rt,
 unsigned options_flags,const TeguAIRGraphicsSpecializationPlan *spec,uint8_t key[32]) {
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);
 /* The module digest is a function of exactly the module bytes. */
 tegu_pc_key_str(&s,"TGRGV2");tegu_pc_key_bytes(&s,p->genericModuleDigest,sizeof(p->genericModuleDigest));
 tegu_pc_key_u32(&s,rt);
 tegu_pc_key_u32(&s,p->genericMrt?1u:0u);tegu_pc_key_u32(&s,p->genericMainRt);
 tegu_pc_key_u32(&s,(uint32_t)p->colorFormat);
 if(p->genericTrueMrt) {
  tegu_pc_key_u32(&s,p->genericRtCount);
  for(unsigned i=0;i<TEGU_MALI_GENERIC_MAX_TARGETS;i++)tegu_pc_key_u32(&s,p->genericRtFormats32[i]);
 }
 struct tegu_mali_generic_options options={TEGU_MALI_GENERIC_OPTIONS_VERSION,options_flags,
  (options_flags&TEGU_MALI_GENERIC_OPTION_SELECT_RT)?rt:0,0};
 tegu_pc_key_bytes(&s,&options,sizeof(options));
 unsigned tile_mode=0;
#ifdef TEGU_TILE_FETCH_DEFAULT
 tile_mode=(unsigned)tegu_ab_tile_fetch();
#else
 (void)0;
#endif
 tegu_pc_key_u32(&s,tile_mode);
 tegu_pc_key_bytes(&s,spec,sizeof(*spec));tegu_pc_sha256_final(&s,key);
}
/* True when the exact plan's bound view/compare facts cannot be represented by
 * the legacy draw path and therefore need the v2 sidecar even if the shader
 * code itself has no changed constants. */
static int tegu_render_generic_spec_needs_sidecar(const TeguAIRGraphicsSpecializationPlan *spec,
 const TeguAIRRenderResource *resources,size_t count) {
 if(!spec)return 0;
 for(uint32_t i=0;i<spec->binding_count&&i<TEGU_GENERIC_DRAW_MAX_BINDINGS;i++) {
  const TeguAIRGraphicsSpecializationBinding *b=&spec->bindings[i];
  if(b->flags)return 1;
  if(b->compare_enabled)return 1;
  if(b->view_level||b->view_layer)return 1;
  if(b->kind!=TEGU_AIR_GENERIC_COMBINED&&b->kind!=TEGU_AIR_GENERIC_COMBINED_IMMUTABLE&&
     b->kind!=TEGU_AIR_GENERIC_TEXTURE)continue;
  const TeguAIRRenderResource *r=0;
  for(size_t k=0;k<count;k++)if(resources[k].kind==2&&resources[k].stage==b->stage&&resources[k].index==b->index){r=&resources[k];break;}
  if(r&&b->view_format_metal&&b->view_format_metal!=r->format)return 1;
 }
 return 0;
}
/* ---- Draw-time variant fallback --------------------------------------
 * A draw whose exact specialized variant is not compiled yet draws with the
 * pipeline's runtime-specialization variant (the same modules compiled with
 * TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC: every specialization constant read
 * from push words the encoder fills from the draw's plan), while the exact
 * variant compiles on a low-priority worker and is published for later
 * draws. Same Metal function and values; the driver folds less in the
 * fallback, so a non-exact float operation may contract differently (Metal
 * fast-math permits either). The runtime variant is compiled in the
 * background at pipeline creation; until it is ready a miss compiles the
 * exact variant on the encoding thread as before. Translator variants
 * (spec->reserved[0]) never fall back. NORD2_METAL_VARIANT_FALLBACK=0 turns
 * it off. */
#define TEGU_VF_RING 64u
#define TEGU_VF_VARIANTS 64u
struct tegu_vf_job {
 TeguRenderPipeline *p;unsigned kind,rt,tile;uint8_t key[32],digest[32];
 TeguAIRGraphicsSpecializationPlan spec;double enq;uint8_t *vs,*fs;NSUInteger vn,fn;
};
static struct tegu_vf_job tegu_vf_ring[TEGU_VF_RING];
static unsigned tegu_vf_head,tegu_vf_tail,tegu_vf_lock,tegu_vf_draining;
static U64 tegu_vf_fallback_draws,tegu_vf_standins,tegu_vf_enqueued,tegu_vf_done,tegu_vf_failed,tegu_vf_dropped,
 tegu_vf_prewarmed,tegu_vf_swap_us,tegu_vf_swap_max_us,tegu_vf_stale;
static int tegu_vf_enabled(void) {
 static int state=-1;
 if(state<0){const char *env=getenv("NORD2_METAL_VARIANT_FALLBACK");state=!(env&&env[0]=='0'&&!env[1]);}
 return state;
}
static void tegu_vf_runtime_key(TeguRenderPipeline *p,unsigned rt,unsigned tile,uint8_t key[32]) {
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,"TGRGRT1");
 tegu_pc_key_bytes(&s,p->genericModuleDigest,sizeof(p->genericModuleDigest));
 tegu_pc_key_u32(&s,rt);tegu_pc_key_u32(&s,tile);tegu_pc_sha256_final(&s,key);
}
static void tegu_vf_drain(void *unused);
#ifdef TEGU_FRONTEND_HOST
#include <pthread.h>
static void *tegu_vf_thread(void *a){tegu_vf_drain(a);return 0;}
static void tegu_vf_schedule(void) {
 pthread_t t;pthread_attr_t at;pthread_attr_init(&at);pthread_attr_setdetachstate(&at,PTHREAD_CREATE_DETACHED);
 if(pthread_create(&t,&at,tegu_vf_thread,0))tegu_vf_drain(0);
 pthread_attr_destroy(&at);
}
#else
static void tegu_vf_schedule(void) {
 static void *(*getq)(long,unsigned long);static void (*async)(void *,void *,void (*)(void *));
 if(!getq){void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
  if(system){async=dlsym(system,"dispatch_async_f");getq=dlsym(system,"dispatch_get_global_queue");}}
 void *q=getq?getq(0x11 /* QOS_CLASS_UTILITY */,0):0;
 if(q&&async)async(q,0,tegu_vf_drain);else tegu_vf_drain(0);
}
#endif
/* The fixed variant array (queued jobs and the execute thread hold indices
 * into it, so it is allocated once at its bound and never moved). Called
 * with p->genericVariantLock held. */
static int tegu_vf_variants_ready(TeguRenderPipeline *p) {
 if(p->genericVariants)return 1;
 p->genericVariants=calloc(TEGU_VF_VARIANTS,sizeof(*p->genericVariants));
 if(!p->genericVariants)return 0;
 p->genericVariantCapacity=TEGU_VF_VARIANTS;return 1;
}
/* Called with p->genericVariantLock held. Returns 0 queued (or already
 * queued), -1 dropped (queue full or no memory). */
static int tegu_vf_enqueue(TeguRenderPipeline *p,unsigned kind,unsigned rt,unsigned tile,const uint8_t key[32],
 const TeguAIRGraphicsSpecializationPlan *spec) {
 if(!tegu_vf_variants_ready(p)){__atomic_fetch_add(&tegu_vf_dropped,1,__ATOMIC_RELAXED);return -1;}
 tegu_lock(&tegu_vf_lock);
 for(unsigned i=tegu_vf_head;i!=tegu_vf_tail;i++) {
  const struct tegu_vf_job *j=&tegu_vf_ring[i%TEGU_VF_RING];
  if(j->p==p&&j->kind==kind&&j->rt==rt&&j->tile==tile&&(!key||!memcmp(j->key,key,32))){tegu_unlock(&tegu_vf_lock);return 0;}
 }
 if(tegu_vf_tail-tegu_vf_head>=TEGU_VF_RING){tegu_unlock(&tegu_vf_lock);__atomic_fetch_add(&tegu_vf_dropped,1,__ATOMIC_RELAXED);return -1;}
 uint8_t *vs=malloc(p->genericVsLength),*fs=malloc(p->genericFsLength);
 if(!vs||!fs){tegu_unlock(&tegu_vf_lock);free(vs);free(fs);__atomic_fetch_add(&tegu_vf_dropped,1,__ATOMIC_RELAXED);return -1;}
 memcpy(vs,p->genericVsBytes,p->genericVsLength);memcpy(fs,p->genericFsBytes,p->genericFsLength);
 struct tegu_vf_job *j=&tegu_vf_ring[tegu_vf_tail%TEGU_VF_RING];
 memset(j,0,sizeof(*j));
 __atomic_fetch_add(&p->vfPending,1,__ATOMIC_ACQ_REL);
 j->p=p;j->kind=kind;j->rt=rt;j->tile=tile;if(key)memcpy(j->key,key,32);
 memcpy(j->digest,p->genericModuleDigest,32);if(spec)j->spec=*spec;j->enq=tegu_host_seconds();
 j->vs=vs;j->fs=fs;j->vn=p->genericVsLength;j->fn=p->genericFsLength;
 tegu_vf_tail++;
 int start=!tegu_vf_draining;tegu_vf_draining=1;
 tegu_unlock(&tegu_vf_lock);
 __atomic_fetch_add(&tegu_vf_enqueued,1,__ATOMIC_RELAXED);
 if(start)tegu_vf_schedule();
 return 0;
}
static void tegu_vf_publish(TeguRenderPipeline *p,struct tegu_render_generic_variant *v,const struct tegu_vf_job *j) {
 int kept=0;
 tegu_lock(&p->genericVariantLock);
 if(!memcmp(p->genericModuleDigest,j->digest,32)&&p->genericVariants&&p->genericVariantCount<p->genericVariantCapacity) {
  unsigned dup=0;
  if(j->kind==2)for(unsigned i=0;i<p->genericVariantCount;i++)
   if(!p->genericVariants[i].runtime_spec&&!memcmp(p->genericVariants[i].key,j->key,32)){dup=1;break;}
  if(!dup) {
   p->genericVariants[p->genericVariantCount]=*v;
   if(j->kind==1){p->rtsBase[j->rt][j->tile]=p->genericVariantCount+1;p->rtsState[j->rt][j->tile]=2;}
   __atomic_store_n(&p->genericVariantCount,p->genericVariantCount+1,__ATOMIC_RELEASE);
   kept=1;
  }
 } else __atomic_fetch_add(&tegu_vf_stale,1,__ATOMIC_RELAXED);
 if(!kept&&j->kind==1&&!memcmp(p->genericModuleDigest,j->digest,32))p->rtsState[j->rt][j->tile]=3;
 tegu_unlock(&p->genericVariantLock);
 if(!kept){tegu_mali_graphics_free(&v->vertex);tegu_mali_graphics_free(&v->fragment);}
 else if(j->kind==2) {
  U64 us=(U64)((tegu_host_seconds()-j->enq)*1e6);
  __atomic_fetch_add(&tegu_vf_swap_us,us,__ATOMIC_RELAXED);
  U64 old=__atomic_load_n(&tegu_vf_swap_max_us,__ATOMIC_RELAXED);
  while(us>old&&!__atomic_compare_exchange_n(&tegu_vf_swap_max_us,&old,us,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
 }
}
static void tegu_vf_run(struct tegu_vf_job *j) {
 TeguRenderPipeline *p=j->p;
 if(__atomic_load_n(&p->vfDying,__ATOMIC_ACQUIRE)) {
  free(j->vs);free(j->fs);__atomic_fetch_sub(&p->vfPending,1,__ATOMIC_ACQ_REL);return;
 }
 struct tegu_render_generic_variant v;char message[256]={0};
 TeguAIRGraphicsSpecializationPlan zero;memset(&zero,0,sizeof(zero));zero.version=1;
 const TeguAIRGraphicsSpecializationPlan *spec=j->kind==1?&zero:&j->spec;
 int rc=tegu_render_generic_variant_compile_modules(p,j->rt,spec,&v,message,sizeof(message),j->vs,j->vn,j->fs,j->fn,
  j->kind==1?TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC:0);
 if(!rc&&j->kind==1) {
  v.rs_n[0]=tegu_spec_push_scan((const uint32_t *)(const void *)j->vs,j->vn/4,v.rs[0]);
  v.rs_n[1]=tegu_spec_push_scan((const uint32_t *)(const void *)j->fs,j->fn/4,v.rs[1]);
  if(v.rs_n[0]<0||v.rs_n[1]<0){tegu_mali_graphics_free(&v.vertex);tegu_mali_graphics_free(&v.fragment);rc=-1;}
  else {v.runtime_spec=1;tegu_vf_runtime_key(p,j->rt,j->tile,v.key);}
 } else if(!rc)memcpy(v.key,j->key,32);
 if(rc) {
  __atomic_fetch_add(&tegu_vf_failed,1,__ATOMIC_RELAXED);
  static unsigned traces;
  if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<16) {
   char line[300];int k=snprintf(line,sizeof(line),"TEGU_VARIANT_FALLBACK refused kind=%u fs=%.60s rt=%u: %.180s\n",j->kind,p->genericName,j->rt,message);
   if(k>0)write(2,line,(size_t)k<sizeof(line)?(size_t)k:sizeof(line)-1);
  }
  if(j->kind==1){tegu_lock(&p->genericVariantLock);
   if(!memcmp(p->genericModuleDigest,j->digest,32))p->rtsState[j->rt][j->tile]=3;tegu_unlock(&p->genericVariantLock);}
 } else {
  tegu_vf_publish(p,&v,j);
  __atomic_fetch_add(j->kind==1?&tegu_vf_prewarmed:&tegu_vf_done,1,__ATOMIC_RELAXED);
 }
 free(j->vs);free(j->fs);__atomic_fetch_sub(&p->vfPending,1,__ATOMIC_ACQ_REL);
}
static void tegu_vf_drain(void *unused) {
 (void)unused;
 for(;;) {
  tegu_lock(&tegu_vf_lock);
  if(tegu_vf_head==tegu_vf_tail){tegu_vf_draining=0;tegu_unlock(&tegu_vf_lock);return;}
  struct tegu_vf_job *j=malloc(sizeof(*j));
  if(!j){tegu_vf_draining=0;tegu_unlock(&tegu_vf_lock);return;}
  *j=tegu_vf_ring[tegu_vf_head%TEGU_VF_RING];tegu_vf_head++;
  tegu_unlock(&tegu_vf_lock);
  tegu_vf_run(j);free(j);
 }
}
/* Queue the runtime-specialization compile of (rt, current tile mode) once.
 * Called with p->genericVariantLock held. */
static void tegu_vf_prewarm_locked(TeguRenderPipeline *p,unsigned rt) {
 if(!p||!tegu_vf_enabled()||rt>=4||p->genericTrueMrt||!p->genericVsBytes||!p->genericFsBytes)return;
 const unsigned tile=tegu_render_variant_tile_mode()?1u:0u;
 if(!p->rtsState[rt][tile]){
  struct tegu_spec_push_entry probe[TEGU_SPEC_PUSH_MAX];
  int nv=tegu_spec_push_scan((const uint32_t *)(const void *)p->genericVsBytes,p->genericVsLength/4,probe);
  int nf=tegu_spec_push_scan((const uint32_t *)(const void *)p->genericFsBytes,p->genericFsLength/4,probe);
  if(nv<0||nf<0||(!nv&&!nf))p->rtsState[rt][tile]=3; /* nothing to specialize, or not representable */
  else if(!tegu_vf_enqueue(p,1,rt,tile,0,0))p->rtsState[rt][tile]=1;
 }
}
/* Prewarm at pipeline creation (generic modules ready). */
static void tegu_vf_prewarm(TeguRenderPipeline *p,unsigned rt) {
 if(!p)return;
 tegu_lock(&p->genericVariantLock);tegu_vf_prewarm_locked(p,rt);tegu_unlock(&p->genericVariantLock);
}
/* Write the draw's specialization values into the runtime variant's push words. */
static void tegu_vf_push(const struct tegu_render_generic_variant *v,const TeguAIRGraphicsSpecializationPlan *spec,
 uint8_t *vpush,uint32_t *vbytes,uint8_t *fpush,uint32_t *fbytes) {
 for(unsigned s=0;s<2;s++) {
  const TeguAIRGraphicsSpecConstant *c=s?spec->fragment_constants:spec->vertex_constants;
  uint32_t cn=s?spec->fragment_count:spec->vertex_count;
  uint8_t *out=s?fpush:vpush;uint32_t *bytes=s?fbytes:vbytes;
  for(int k=0;k<v->rs_n[s];k++) {
   const struct tegu_spec_push_entry *e=&v->rs[s][k];
   uint32_t value=e->default_value;
   for(uint32_t i=0;i<cn&&i<TEGU_AIR_GRAPHICS_SPEC_MAX;i++)if(c[i].id==e->spec_id) {
    uint32_t x=0;for(uint32_t b=0;b<c[i].size&&b<4;b++)x|=(uint32_t)c[i].data[b]<<(8*b);
    if(e->is_bool){x=0;for(uint32_t b=0;b<c[i].size&&b<8;b++)x|=c[i].data[b];x=x!=0;}
    value=x;break;
   }
   uint32_t at=TEGU_SPEC_PUSH_BASE+4u*(uint32_t)k;
   memcpy(out+at,&value,4);if(at+4>*bytes)*bytes=at+4;
  }
 }
}
/* Select a pipeline-owned specialized variant, or the byte-identical default
 * when the planner exposes no constants and no sidecar-only view facts. */
static int tegu_render_generic_variant_select_locked(TeguRenderPipeline *p,unsigned rt,
 const TeguAIRGraphicsSpecializationPlan *spec,const TeguAIRRenderResource *resources,size_t resource_count,
 const struct tegu_generic_draw_shader **shader,const struct tegu_mali_generic_plan **compile,
 unsigned *variant_index,int *runtime_spec,char *message,size_t size) {
 if(!p||!spec||!shader||!compile||!variant_index||!runtime_spec)return -1;
 *runtime_spec=0;
 *shader=&p->genericShader;*compile=rt==p->genericMainRt?&p->genericCompile:
  rt==1?&p->rt1Compile:rt==2?&p->rt2Compile:&p->rt3Compile;
 *variant_index=0;
 int needs=tegu_render_generic_spec_needs_sidecar(spec,resources,resource_count);
 if(!spec->vertex_count&&!spec->fragment_count&&!spec->specialization_length&&!needs)return 0;
 unsigned options_flags=((p->genericMrt&&!p->genericTrueMrt)?TEGU_MALI_GENERIC_OPTION_SELECT_RT:0)|
  TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS|
  TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE|(p->genericTrueMrt?TEGU_MALI_GENERIC_OPTION_MULTI_TARGET:0);
 const unsigned tile=tegu_render_variant_tile_mode();
 struct tegu_render_variant_memo *vm=p->genericVariantMemo;
 for(unsigned w=0;vm&&w<TEGU_VARIANT_MEMO_WAYS;w++) {
  unsigned k=vm->e[w].index;
  if(!k||k>p->genericVariantCount||vm->e[w].rt!=rt||vm->e[w].flags!=options_flags||vm->e[w].tile!=tile||
     memcmp(&vm->e[w].spec,spec,sizeof(*spec)))continue;
  *shader=&p->genericVariants[k-1].shader;*compile=&p->genericVariants[k-1].compile;
#ifdef TEGU_FRONTEND_HOST
  if(tegu_plan_memo_verify()){uint8_t key[32];tegu_render_generic_variant_key(p,rt,options_flags,spec,key);
   if(memcmp(key,p->genericVariants[k-1].key,32)){fprintf(stderr,"TEGU_VARIANT_MEMO_MISMATCH rt=%u index=%u\n",rt,k);abort();}}
#endif
  *variant_index=k;return 0;
 }
 uint8_t key[32];tegu_render_generic_variant_key(p,rt,options_flags,spec,key);
 unsigned found=0,standin=0;
 for(unsigned i=0;i<p->genericVariantCount;i++)if(!memcmp(p->genericVariants[i].key,key,32)) {
  if(!p->genericVariants[i].runtime_spec){found=i+1;break;}
  if(p->genericVariants[i].runtime_spec==2&&!standin)standin=i+1;
 }
 if(!found&&tegu_vf_enabled()&&!spec->reserved[0]&&rt<4) {
  /* Draw now with the runtime-specialization variant; the exact one is
   * compiled in the background (tegu_vf_*). */
  if(!standin) {
   unsigned base=p->rtsBase[rt][tile?1:0];
   if(base&&base<=p->genericVariantCount&&p->genericVariantCount<p->genericVariantCapacity) {
    struct tegu_render_generic_variant *s=&p->genericVariants[p->genericVariantCount];
    *s=p->genericVariants[base-1];s->shared=1;s->runtime_spec=2;memcpy(s->key,key,32);
    s->binding_count=spec->binding_count>TEGU_GENERIC_DRAW_MAX_BINDINGS?TEGU_GENERIC_DRAW_MAX_BINDINGS:spec->binding_count;
    memcpy(s->bindings,spec->bindings,s->binding_count*sizeof(s->bindings[0]));
    s->vertex_constant_count=spec->vertex_count;s->fragment_constant_count=spec->fragment_count;
    s->specialization_length=spec->specialization_length;
    __atomic_store_n(&p->genericVariantCount,p->genericVariantCount+1,__ATOMIC_RELEASE);
    standin=p->genericVariantCount;__atomic_fetch_add(&tegu_vf_standins,1,__ATOMIC_RELAXED);
   } else if(!p->rtsState[rt][tile?1:0])tegu_vf_prewarm_locked(p,rt);
  }
  if(standin) {
   (void)tegu_vf_enqueue(p,2,rt,tile?1u:0u,key,spec); /* de-duplicated while queued */
   *shader=&p->genericVariants[standin-1].shader;*compile=&p->genericVariants[standin-1].compile;
   *variant_index=standin;*runtime_spec=1;__atomic_fetch_add(&tegu_vf_fallback_draws,1,__ATOMIC_RELAXED);
   return 0;
  }
 }
 if(found) {
  if(!vm)vm=p->genericVariantMemo=calloc(1,sizeof(*vm));
  if(vm){unsigned w=vm->next++%TEGU_VARIANT_MEMO_WAYS;vm->e[w].index=0;vm->e[w].rt=rt;vm->e[w].flags=options_flags;
   vm->e[w].tile=tile;memcpy(&vm->e[w].spec,spec,sizeof(*spec));vm->e[w].index=found;}
  *shader=&p->genericVariants[found-1].shader;*compile=&p->genericVariants[found-1].compile;
  *variant_index=found;return 0;
 }
 /* Queued jobs and the execute thread hold pointers into this array, so it
  * is allocated once at its full bound and never moved; the count is
  * published only after the new entry is complete. */
 if(!tegu_vf_variants_ready(p)){snprintf(message,size,"specialized variant allocation failed");return -1;}
 if(p->genericVariantCount>=p->genericVariantCapacity) {
  snprintf(message,size,"specialized variant limit reached");return -1;
 }
 struct tegu_render_generic_variant *v=&p->genericVariants[p->genericVariantCount];
 if(tegu_render_generic_variant_compile(p,rt,spec,v,message,size))return -1;
 memcpy(v->key,key,32);
 *shader=&v->shader;*compile=&v->compile;*variant_index=p->genericVariantCount+1;
 __atomic_store_n(&p->genericVariantCount,p->genericVariantCount+1,__ATOMIC_RELEASE);
 return 0;
}
/* Serialize lookup/compile/publish for two app threads encoding one pipeline.
 * The execute thread only reads immutable entries with acquire publication. */
static int tegu_render_generic_variant_select(TeguRenderPipeline *p,unsigned rt,
 const TeguAIRGraphicsSpecializationPlan *spec,const TeguAIRRenderResource *resources,size_t resource_count,
 const struct tegu_generic_draw_shader **shader,const struct tegu_mali_generic_plan **compile,
 unsigned *variant_index,int *runtime_spec,char *message,size_t size) {
 if(!p)return -1;
 tegu_lock(&p->genericVariantLock);
 int rc=tegu_render_generic_variant_select_locked(p,rt,spec,resources,resource_count,shader,compile,variant_index,runtime_spec,message,size);
 tegu_unlock(&p->genericVariantLock);
 return rc;
}
/* ==== lowering-opus: pass-MRT variant (BEGIN) ====
 * A draw of a pipeline with several colour attachments (genericRtCount>1,
 * all bound) compiled for ONE JM job carrying all the pass's targets:
 * MULTI_TARGET over the pipeline's attachment formats (= the pass formats),
 * plus TILE_FETCH when the fragment shader reads the framebuffer, so a fetch
 * of attachment j is LD_TILE rt=j (mali_graphics_compiler MULTI_TARGET+TILE).
 * The written mask is the compiler's (plan.reserved). Keyed like the normal
 * variant plus a pass-MRT tag and the formats; published with the same
 * append/atomic-count protocol (runtime_spec=0, shared=0). A refusal (memory
 * fetch, written mask 0, compile error) returns -1: the caller keeps the
 * per-RT path. */
#define TEGU_PASS_MRT_RT 0x40u
static int tegu_render_variant_pass_mrt_compile(TeguRenderPipeline *p,const TeguAIRGraphicsSpecializationPlan *spec,
 struct tegu_render_generic_variant *v,char *message,size_t size) {
 if(!p||!spec||!v||spec->version!=1||spec->vertex_count>TEGU_AIR_GRAPHICS_SPEC_MAX||
    spec->fragment_count>TEGU_AIR_GRAPHICS_SPEC_MAX||spec->specialization_length>512||spec->reserved[0]||
    p->genericRtCount<2||p->genericRtCount>TEGU_MALI_GENERIC_MAX_TARGETS)return -1;
 for(unsigned i=0;i<p->genericRtCount;i++)if(!p->genericRtFormats32[i])return -1;
 memset(v,0,sizeof(*v));v->rt=TEGU_PASS_MRT_RT;
 v->vertex_constant_count=spec->vertex_count;v->fragment_constant_count=spec->fragment_count;
 v->specialization_length=spec->specialization_length;
 const struct tegu_mali_specialization *vc=(const struct tegu_mali_specialization *)spec->vertex_constants;
 const struct tegu_mali_specialization *fc=(const struct tegu_mali_specialization *)spec->fragment_constants;
 unsigned flags=TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE|TEGU_MALI_GENERIC_OPTION_MULTI_TARGET;
 int fetch=tegu_render_spirv_declares_subpass(p->genericFsBytes,p->genericFsLength);
 if(fetch)flags|=TEGU_MALI_GENERIC_OPTION_TILE_FETCH;
 struct tegu_mali_varying_info varying={0};
 tegu_lock(&tegu_compiler_lock);
 int status=tegu_render_compile_generic_options(p->genericVsBytes,p->genericVsLength,p->genericFsBytes,p->genericFsLength,
  vc,spec->vertex_count,fc,spec->fragment_count,flags,0,&v->vertex,&v->fragment,&v->pair,&v->compile,&varying,
  p->genericRtFormats32,p->genericRtCount,message,size);
 if(status&&fetch) { /* declared but unused subpass input: compile without the tile lowering */
  flags&=~TEGU_MALI_GENERIC_OPTION_TILE_FETCH;
  status=tegu_render_compile_generic_options(p->genericVsBytes,p->genericVsLength,p->genericFsBytes,p->genericFsLength,
   vc,spec->vertex_count,fc,spec->fragment_count,flags,0,&v->vertex,&v->fragment,&v->pair,&v->compile,&varying,
   p->genericRtFormats32,p->genericRtCount,message,size);
 }
 tegu_unlock(&tegu_compiler_lock);
 if(status)return -1;
 uint32_t written=v->compile.reserved&~TEGU_MALI_GENERIC_PLAN_MRT;
 if(!(v->compile.reserved&TEGU_MALI_GENERIC_PLAN_MRT)||!written||(written>>p->genericRtCount)||
    v->compile.reads_framebuffer==TEGU_MALI_GENERIC_FETCH_MEMORY) {
  snprintf(message,size,"pass-MRT variant refused (written %x fetch %u)",written,v->compile.reads_framebuffer);
  tegu_mali_graphics_free(&v->vertex);tegu_mali_graphics_free(&v->fragment);return -1;
 }
 v->varying=varying;v->options_flags=flags|TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS;
 v->tls=v->vertex.tls>v->fragment.tls?v->vertex.tls:v->fragment.tls;
 v->pair_flags=v->pair.flags;v->sysval_mask=v->vertex.sysval_mask;
 if(tegu_render_generic_variant_shader(p,&v->vertex,&v->fragment,&v->pair,&v->compile,&v->shader,message,size)||
    !tegu_render_generic_variant_compatible(p,&v->shader,message,size)) {
  tegu_mali_graphics_free(&v->vertex);tegu_mali_graphics_free(&v->fragment);return -1;
 }
 v->shader.blend=p->genericRtBlends[0];
 v->binding_count=spec->binding_count>TEGU_GENERIC_DRAW_MAX_BINDINGS?TEGU_GENERIC_DRAW_MAX_BINDINGS:spec->binding_count;
 memcpy(v->bindings,spec->bindings,v->binding_count*sizeof(v->bindings[0]));
 return 0;
}
static int tegu_render_variant_pass_mrt(TeguRenderPipeline *p,const TeguAIRGraphicsSpecializationPlan *spec,
 const struct tegu_generic_draw_shader **shader,const struct tegu_mali_generic_plan **compile,
 unsigned *variant_index,char *message,size_t size) {
 if(!p||!spec||!shader||!compile||!variant_index)return -1;
 uint8_t base[32],key[32];
 tegu_render_generic_variant_key(p,TEGU_PASS_MRT_RT,TEGU_MALI_GENERIC_OPTION_MULTI_TARGET,spec,base);
 {struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,"TGPASSMRT1");tegu_pc_key_bytes(&s,base,32);
  tegu_pc_key_u32(&s,p->genericRtCount);
  for(unsigned i=0;i<TEGU_MALI_GENERIC_MAX_TARGETS;i++)tegu_pc_key_u32(&s,p->genericRtFormats32[i]);
  tegu_pc_sha256_final(&s,key);}
 tegu_lock(&p->genericVariantLock);
 int rc=-1;unsigned found=0;
 for(unsigned i=0;i<p->genericVariantCount;i++)
  if(!p->genericVariants[i].runtime_spec&&!memcmp(p->genericVariants[i].key,key,32)){found=i+1;break;}
 if(!found&&tegu_vf_variants_ready(p)&&p->genericVariantCount<p->genericVariantCapacity) {
  struct tegu_render_generic_variant *v=&p->genericVariants[p->genericVariantCount];
  if(!tegu_render_variant_pass_mrt_compile(p,spec,v,message,size)) {
   memcpy(v->key,key,32);found=p->genericVariantCount+1;
   __atomic_store_n(&p->genericVariantCount,p->genericVariantCount+1,__ATOMIC_RELEASE);
  }
 } else if(!found)snprintf(message,size,"pass-MRT variant limit reached");
 if(found){*shader=&p->genericVariants[found-1].shader;*compile=&p->genericVariants[found-1].compile;*variant_index=found;rc=0;}
 tegu_unlock(&p->genericVariantLock);
 return rc;
}
/* ==== lowering-opus: pass-MRT variant (END) ==== */
/* Exact per-pipeline memo of the translator's generic draw plan.
 * tegu_air_render_pipeline_plan_generic_specialized re-runs the whole
 * metal2vulkan plan + instantiate_draw for every draw (~20 us of host CPU at
 * -O2, several times that on the phone), although a UI frame repeats the same
 * few binding shapes. The planner is a pure function of (compiled pipeline,
 * resource facts, vertex layouts, draw record); an entry is reused only when
 * those inputs are identical, so a hit returns exactly the bytes the planner
 * would return. Only successful plans are kept; refusals are recomputed and
 * reported as before.
 *
 * One input is compared in canonical form. For a buffer bound at a vertex
 * layout slot, air_bridge.rs generic_plan_mode reads (offset,length) only to
 * check length in (0,2^32], offset<=length and offset+bytes<=length, then
 * REPLACES that slot's binding by {offset 0, length bytes} before planning.
 * So such a record that passes exactly those checks (re-evaluated here
 * with the same arithmetic) is keyed as a fixed marker; one that fails them
 * bypasses the memo and reaches the planner, which refuses it. The planner
 * copies the raw offset into its output only for a descriptor
 * buffer binding at the same stage/slot; an entry whose plan has one keeps
 * the raw key. CPU index expansion and ring-buffered vertex data move that
 * offset on every draw, which otherwise defeated this memo. Revisit if
 * generic_plan_mode changes how layout slots are bound. */
/* A UI frame draws one pipeline with many binding shapes (CA: a fixed_frag
 * variant for layers of many sizes), and round-robin replacement over fewer
 * ways than the shapes a frame cycles through never hits (c6l: 59% misses
 * with 4 ways).  Each entry is ~7 KB, allocated on first use. */
#define TEGU_PLAN_MEMO_WAYS 16u
#define TEGU_PLAN_MEMO_MAX_RESOURCES 160u
struct tegu_render_plan_memo_entry {
 const TeguAIRRenderPipeline *compiled;
 uint32_t count,layoutCount,valid,canonicalSlots;
 uint64_t hash; /* tegu_plan_memo_hash of the key; 0 = compare in full */
 uint32_t drawCanon; /* every layout slot canonical: the vertex count is not part of the key */
 TeguAIRRenderDraw draw;
 TeguAIRVertexLayout layouts[8];
 TeguAIRGenericPlan plan;
 TeguAIRGraphicsSpecializationPlan spec;
 TeguAIRRenderResource resources[]; /* key copies, count entries */
};
struct tegu_render_plan_memo {
 struct tegu_render_plan_memo_entry *ways[TEGU_PLAN_MEMO_WAYS];
 uint32_t capacity[TEGU_PLAN_MEMO_WAYS];
 unsigned next;
};
static U64 tegu_plan_memo_hits,tegu_plan_memo_misses;
/* Miss causes against the nearest same-pipeline entry with the same record
 * count: only the draw record differs, only texture extents, only buffer
 * offset/length, anything else, or no comparable entry. */
static U64 tegu_plan_miss_draw,tegu_plan_miss_extent,tegu_plan_miss_offset,tegu_plan_miss_other,tegu_plan_miss_cold;
/* Vertex layout slots (bit = Metal buffer index) whose record may be keyed
 * canonically. */
static uint32_t tegu_plan_memo_layout_slots(const TeguRenderPipeline *p,unsigned layoutCount) {
 uint32_t slots=0;
 for(unsigned i=0;i<layoutCount;i++)if(p->genericLayouts[i].index<31)slots|=1u<<p->genericLayouts[i].index;
 return slots;
}
/* The key form of one record: named fields only (TeguAIRRenderResource has 4
 * padding bytes before `length` that compound-literal callers leave
 * unspecified), and the vertex-slot marker described above. Returns -1 when
 * a canonical-slot record fails the planner's own checks (no memo use). */
#define TEGU_PLAN_MEMO_SLOT_MARK 0x4d454d4fu
/* A 2D texture's extent reaches the generic plan only through
 * air_bridge.rs compositor_plan_inputs_impl - the range check 1..16384 per
 * side and the observed 776x24 blur chain (rich_types, the only other
 * extent predicates, never admits type 2) - and the GenericPlan binding's
 * verbatim width/height (generic_plan_mode texture_facts), which a hit
 * rewrites from the current record. TextureFacts carries no extent, so the
 * translator's plan and push fields never read it. Keyed: class 1 (in
 * range), 2 (776x24); out of range stays raw (the planner refuses it).
 * KEEP IN SYNC with compositor_plan_inputs_impl's extent predicates. */
#define TEGU_PLAN_MEMO_EXTENT_MARK 0x45585431u
static uint32_t tegu_plan_memo_extent_class(const TeguAIRRenderResource *r) {
 if(r->kind!=2||r->texture_type!=2||!r->width||!r->height||r->width>16384||r->height>16384)return 0;
 return r->width==776&&r->height==24?2u:1u;
}
static int tegu_plan_memo_key(TeguAIRRenderResource *out,const TeguAIRRenderResource *in,uint32_t canonicalSlots,
 const TeguRenderPipeline *p,unsigned layoutCount,const TeguAIRRenderDraw *draw) {
 const size_t head=offsetof(TeguAIRRenderResource,address_mode_identity)+sizeof(in->address_mode_identity);
 const size_t tail=offsetof(TeguAIRRenderResource,length);
 memset(out,0,sizeof(*out));
 memcpy(out,in,head);
 memcpy((unsigned char *)out+tail,(const unsigned char *)in+tail,sizeof(*in)-tail);
 {uint32_t cls=tegu_plan_memo_extent_class(in);
  if(cls){out->width=cls;out->height=0;out->reserved=TEGU_PLAN_MEMO_EXTENT_MARK;return 0;}}
 if(in->kind!=1||in->stage!=1||in->index>=31||!(canonicalSlots&(1u<<in->index)))return 0;
 /* compositor_plan_inputs_mode: 0<length<=2^32, offset<=length. */
 if(!in->length||in->length>(1ull<<32)||in->offset>in->length)return -1;
 /* generic_plan_mode, every layout naming this slot (u32 sums cannot wrap
  * in u64; a u32 overflow there is refused before this loop). */
 for(unsigned i=0;i<layoutCount;i++) {
  const TeguAIRVertexLayout *l=&p->genericLayouts[i];
  if(l->index!=in->index)continue;
  uint64_t end=l->step_function==1?(uint64_t)draw->vertex_start+draw->vertex_count:(uint64_t)draw->base_instance+draw->instance_count;
  uint64_t bytes=end*(uint64_t)l->stride; /* stride<=4096 in any accepted layout */
  if(l->stride>4096||in->offset+bytes<in->offset||in->offset+bytes>in->length)return -1;
 }
 out->offset=0;out->length=0;out->reserved=TEGU_PLAN_MEMO_SLOT_MARK;
 return 0;
}
/* The planning record's vertex count reaches the plan only through (a) the
 * planner's range check 1..2^24, (b) each layout slot's bounds check, which
 * tegu_plan_memo_key re-evaluates for every canonical slot with the actual
 * record on every lookup, and (c) the length bound at a layout slot, which
 * the plan reads only for a slot that is also a descriptor binding (a raw
 * slot: such an entry keeps the full record). No push field the translator
 * fills reads it (PushField: BaseVertexInstance reads vertex_start and
 * base_instance, which stay keyed; IndexCapacity is indexed-only), and the
 * plan's draw command is checked equal to the record, not copied. So when an
 * entry has no raw layout slot, records that differ only in an in-range
 * vertex count share it (CA draws its quads with varying counts). */
static int tegu_plan_memo_count_ok(const TeguAIRRenderDraw *d) {return d->vertex_count>=1u&&d->vertex_count<=(1u<<24);}
static TeguAIRRenderDraw tegu_plan_memo_draw_key(const TeguAIRRenderDraw *d,int canon) {
 TeguAIRRenderDraw k=*d;if(canon)k.vertex_count=0;return k;
}
/* A cheap pre-check before the full comparison, never a substitute for it:
 * a multiply-xor hash of the (count-canonical) draw record and, per record,
 * its kind/stage/index/format, extent and (outside canonical layout slots)
 * offset/length - the fields CA draws vary. Entries keyed with a raw layout
 * slot store 0 and are always compared in full. */
static inline uint64_t tegu_plan_memo_step(uint64_t h,uint64_t w) {h=(h^w)*0x9e3779b97f4a7c15ull;return h^(h>>29);}
static uint64_t tegu_plan_memo_hash(const TeguAIRRenderDraw *draw,const TeguAIRRenderResource *r,size_t count,uint32_t slots) {
 uint64_t h=0x243f6a8885a308d3ull;
 h=tegu_plan_memo_step(h,(uint64_t)draw->version|((uint64_t)draw->vertex_start<<32));
 h=tegu_plan_memo_step(h,(uint64_t)draw->vertex_count|((uint64_t)draw->instance_count<<32));
 h=tegu_plan_memo_step(h,draw->base_instance);
 for(size_t i=0;i<count;i++) {
  const TeguAIRRenderResource *x=&r[i];
  const int canon=x->kind==1&&x->stage==1&&x->index<31&&(slots&(1u<<x->index));
  h=tegu_plan_memo_step(h,(uint64_t)x->kind|((uint64_t)x->stage<<8)|((uint64_t)x->index<<16)|((uint64_t)x->format<<32));
  {uint32_t cls=tegu_plan_memo_extent_class(x);
   h=tegu_plan_memo_step(h,cls?(uint64_t)cls<<62:(uint64_t)x->width|((uint64_t)x->height<<32));}
  if(!canon)h=tegu_plan_memo_step(h,x->offset^(x->length*0x100000001b3ull));
 }
 return h?h:1;
}
/* Diagnostic only: which input class separates this miss from the closest
 * comparable entry (fewest differing classes). */
static void tegu_plan_memo_classify(const struct tegu_render_plan_memo *m,const TeguRenderPipeline *p,
 const TeguAIRRenderResource *keys,size_t count,unsigned layoutCount,const TeguAIRRenderDraw *draw) {
 unsigned best=0;int seen=0;
 for(unsigned w=0;m&&w<TEGU_PLAN_MEMO_WAYS;w++) {
  const struct tegu_render_plan_memo_entry *e=m->ways[w];
  if(!e||!e->valid||e->compiled!=p->compiled||e->count!=count||e->layoutCount!=layoutCount||
     memcmp(e->layouts,p->genericLayouts,layoutCount*sizeof(e->layouts[0])))continue;
  TeguAIRRenderDraw da=tegu_plan_memo_draw_key(&e->draw,e->drawCanon&&tegu_plan_memo_count_ok(draw)),
   db=tegu_plan_memo_draw_key(draw,e->drawCanon&&tegu_plan_memo_count_ok(draw));
  unsigned diff=memcmp(&da,&db,sizeof(da))?1u:0u;
  for(size_t i=0;i<count&&diff<16u;i++) {
   const TeguAIRRenderResource *a=&e->resources[i],*b=&keys[i];
   if(!memcmp(a,b,sizeof(*a)))continue;
   TeguAIRRenderResource x=*a,y=*b;
   if(x.kind==2&&y.kind==2){x.width=y.width;x.height=y.height;if(!memcmp(&x,&y,sizeof(x))){diff|=2u;continue;}}
   if(x.kind==1&&y.kind==1){x.offset=y.offset;x.length=y.length;x.reserved=y.reserved;if(!memcmp(&x,&y,sizeof(x))){diff|=4u;continue;}}
   diff|=16u;
  }
  if(!seen||__builtin_popcount(diff)<__builtin_popcount(best)){best=diff;seen=1;}
 }
 U64 *c=!seen?&tegu_plan_miss_cold:(best&16u)||__builtin_popcount(best)!=1?&tegu_plan_miss_other:
  best==1u?&tegu_plan_miss_draw:best==2u?&tegu_plan_miss_extent:&tegu_plan_miss_offset;
 __atomic_fetch_add(c,1,__ATOMIC_RELAXED);
}
static void tegu_render_plan_memo_free(TeguRenderPipeline *p) {
 if(!p||!p->genericPlanMemo)return;
 for(unsigned i=0;i<TEGU_PLAN_MEMO_WAYS;i++)free(p->genericPlanMemo->ways[i]);
 free(p->genericPlanMemo);p->genericPlanMemo=0;
}
static int tegu_render_plan_generic_cached(TeguRenderPipeline *p,const TeguAIRRenderResource *resources,size_t count,
 const TeguAIRRenderDraw *draw,TeguAIRGenericPlan *plan,TeguAIRGraphicsSpecializationPlan *spec,char *error,size_t size) {
 const unsigned layoutCount=p->genericLayoutCount<=8?p->genericLayoutCount:8;
 int cacheable=count<=TEGU_PLAN_MEMO_MAX_RESOURCES&&p->genericLayoutCount<=8&&draw;
 /* Key records under the pipeline's full layout-slot mask, hashed once; an
  * entry keyed under another mask (a raw descriptor slot) is compared in
  * full as before. */
 const uint32_t fullSlots=tegu_plan_memo_layout_slots(p,layoutCount);
 uint64_t hash=0;
 if(cacheable) {
  TeguAIRRenderDraw dk=tegu_plan_memo_draw_key(draw,tegu_plan_memo_count_ok(draw));
  hash=tegu_plan_memo_hash(&dk,resources,count,fullSlots);
 }
 if(cacheable) {
  tegu_lock(&p->genericPlanMemoLock);
  struct tegu_render_plan_memo *m=p->genericPlanMemo;
  for(unsigned w=0;m&&w<TEGU_PLAN_MEMO_WAYS;w++) {
   const struct tegu_render_plan_memo_entry *e=m->ways[w];
   if(!e||!e->valid||e->compiled!=p->compiled||e->count!=count||e->layoutCount!=layoutCount||
      (e->hash&&hash&&e->hash!=hash)||
      memcmp(e->layouts,p->genericLayouts,layoutCount*sizeof(e->layouts[0])))continue;
   {const int canon=e->drawCanon&&tegu_plan_memo_count_ok(draw);
    TeguAIRRenderDraw da=tegu_plan_memo_draw_key(&e->draw,canon),db=tegu_plan_memo_draw_key(draw,canon);
    if(memcmp(&da,&db,sizeof(da)))continue;}
   {size_t i=0;
    for(;i<count;i++){TeguAIRRenderResource k;
     if(tegu_plan_memo_key(&k,&resources[i],e->canonicalSlots,p,layoutCount,draw)||memcmp(&k,&e->resources[i],sizeof(k)))break;}
    if(i!=count)continue;}
   memcpy(plan,&e->plan,sizeof(*plan));memcpy(spec,&e->spec,sizeof(*spec));
   /* The bound textures' own extents (the key holds only their class). */
   for(uint32_t b=0;b<plan->binding_count&&b<16;b++){TeguAIRGenericBinding *g=&plan->bindings[b];
    if(g->kind!=TEGU_AIR_GENERIC_TEXTURE&&g->kind!=TEGU_AIR_GENERIC_COMBINED&&g->kind!=TEGU_AIR_GENERIC_COMBINED_IMMUTABLE)continue;
    for(size_t r=0;r<count;r++)if(resources[r].kind==2&&resources[r].stage==g->stage&&resources[r].index==g->index){
     g->width=resources[r].width;g->height=resources[r].height;break;}}
   tegu_unlock(&p->genericPlanMemoLock);
   __atomic_fetch_add(&tegu_plan_memo_hits,1,__ATOMIC_RELAXED);
#ifdef TEGU_FRONTEND_HOST
   if(tegu_plan_memo_verify()) {
    TeguAIRGenericPlan *fp=malloc(sizeof(*fp));TeguAIRGraphicsSpecializationPlan *fs=malloc(sizeof(*fs));char fe[256]={0};
    if(!fp||!fs)abort();
    int frc=tegu_air_render_pipeline_plan_generic_specialized(p->compiled,resources,count,
     p->genericLayouts,p->genericLayoutCount,draw,fp,fs,fe,sizeof(fe));
    if(frc||memcmp(fp,plan,sizeof(*fp))||memcmp(fs,spec,sizeof(*fs))) {
     fprintf(stderr,"TEGU_PLAN_MEMO_MISMATCH rc=%d plan=%d spec=%d %s\n",frc,memcmp(fp,plan,sizeof(*fp))!=0,memcmp(fs,spec,sizeof(*fs))!=0,fe);
     abort();
    }
    free(fp);free(fs);
   }
#endif
   return 0;
  }
  tegu_unlock(&p->genericPlanMemoLock);
 }
 __atomic_fetch_add(&tegu_plan_memo_misses,1,__ATOMIC_RELAXED);
 {TeguAIRRenderResource *keys=cacheable?malloc((count?count:1)*sizeof(*keys)):0;int keyed=keys!=0;
  for(size_t i=0;keys&&i<count;i++)if(tegu_plan_memo_key(&keys[i],&resources[i],fullSlots,p,layoutCount,draw))keyed=0;
  if(keyed) {
   tegu_lock(&p->genericPlanMemoLock);
   tegu_plan_memo_classify(p->genericPlanMemo,p,keys,count,layoutCount,draw);
   tegu_unlock(&p->genericPlanMemoLock);
  } else __atomic_fetch_add(&tegu_plan_miss_other,1,__ATOMIC_RELAXED);
  free(keys);}
 int rc=tegu_air_render_pipeline_plan_generic_specialized(p->compiled,resources,count,
  p->genericLayouts,p->genericLayoutCount,draw,plan,spec,error,size);
 if(rc||!cacheable)return rc;
 /* A descriptor buffer binding at a layout slot carries that slot's raw
  * offset: keep those slots raw in this entry's key. */
 uint32_t canonicalSlots=tegu_plan_memo_layout_slots(p,layoutCount);
 for(uint32_t i=0;i<plan->binding_count&&i<16;i++){const TeguAIRGenericBinding *b=&plan->bindings[i];
  if(b->kind==TEGU_AIR_GENERIC_BUFFER&&b->stage==1&&b->index<31)canonicalSlots&=~(1u<<b->index);}
 tegu_lock(&p->genericPlanMemoLock);
 if(!p->genericPlanMemo)p->genericPlanMemo=calloc(1,sizeof(*p->genericPlanMemo));
 struct tegu_render_plan_memo *m=p->genericPlanMemo;
 if(m) {
  unsigned w=m->next++%TEGU_PLAN_MEMO_WAYS;
  struct tegu_render_plan_memo_entry *e=m->ways[w];
  if(!e||m->capacity[w]<count) {
   uint32_t want=count<16?16u:(uint32_t)count;
   struct tegu_render_plan_memo_entry *grown=malloc(sizeof(*e)+want*sizeof(e->resources[0]));
   free(e);e=grown;m->ways[w]=e;m->capacity[w]=e?want:0;
  }
  if(e) {
   e->valid=0;e->compiled=p->compiled;e->count=(uint32_t)count;e->layoutCount=layoutCount;e->draw=*draw;
   e->canonicalSlots=canonicalSlots;
   memset(e->layouts,0,sizeof(e->layouts));memcpy(e->layouts,p->genericLayouts,layoutCount*sizeof(e->layouts[0]));
   int keyed=1;
   for(size_t i=0;i<count;i++)if(tegu_plan_memo_key(&e->resources[i],&resources[i],canonicalSlots,p,layoutCount,draw))keyed=0;
   memcpy(&e->plan,plan,sizeof(*plan));memcpy(&e->spec,spec,sizeof(*spec));
   e->drawCanon=canonicalSlots==fullSlots&&tegu_plan_memo_count_ok(draw);
   {TeguAIRRenderDraw dk=tegu_plan_memo_draw_key(draw,e->drawCanon);
    e->hash=keyed&&canonicalSlots==fullSlots?tegu_plan_memo_hash(&dk,resources,count,fullSlots):0;}
   e->valid=keyed;
  }
 }
 tegu_unlock(&p->genericPlanMemoLock);
 return 0;
}
/* Re-translate a pipeline for RT0 only and compile it for the generic draw,
 * replacing pipeline->compiled on success.  Used for classes translated with
 * extra attachments (compositor two-target contract) so their draws can take
 * the generic path, which supports any scissor and indexed meshes. */
static int tegu_render_generic_retranslate(TeguRenderPipeline *p,TeguFunction *vertex,TeguFunction *fragment,
 const TeguAIRVertexAttribute *attributes,NSUInteger attributeCount,const TeguAIRVertexLayout *layouts,
 NSUInteger layoutCount,const struct tegu_blend_inputs *blend,char *message,size_t size) {
 const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];
 if(!p||!vn||!fn||!blend)return -1;
 TeguAIRBlob vs={0},fs={0},meta={0};TeguAIRRenderBlend b=*blend;
 TeguAIRRenderPipeline *compiled=tegu_pc_render_create((blend->enabled||blend->write_mask!=15)?TEGU_PC_AIR_BLEND:TEGU_PC_AIR_VERTEX,vertex,fragment,
  p->colorFormat?p->colorFormat:80,0,0,(blend->enabled||blend->write_mask!=15)?&b:0,attributes,attributeCount,layouts,layoutCount,
  tegu_native_profile,sizeof(tegu_native_profile),&vs,&fs,&meta,message,size);
 int status=-1;
 if(compiled) {
  TeguAIRRenderPipeline *previous=p->compiled;p->compiled=compiled;
  status=tegu_render_generic_prepare(p,&vs,&fs,layouts,layoutCount,blend,message,size);
  if(status){p->compiled=previous;tegu_air_render_pipeline_close(compiled);}
  else {tegu_air_render_pipeline_close(previous);snprintf(p->genericName,sizeof(p->genericName),"%s",fn);}
 }
 tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);
 return status;
}
/* Keep each record below the console's short write limit. A capture ID joins
 * shader names and per-attachment facts without relying on a long line that
 * the kernel's per-process console formatter can truncate mid-field. */
static void tegu_render_vertex_capture_row(unsigned capture,unsigned layout,const uint32_t values[4]) {
 char line[224];NSUInteger n=0;
 const char *prefix=layout?"TEGU_RENDER_VERTEX_LAYOUT capture=":"TEGU_RENDER_VERTEX_ATTRIBUTE capture=";
 for(unsigned i=0;prefix[i];i++)line[n++]=prefix[i];
 for(unsigned j=0;j<8;j++)line[n++]="0123456789abcdef"[(capture>>(28-j*4))&15];
 const char *labels[2][4]={{" slot="," format="," offset="," buffer="},{" slot="," stride="," step="," rate="}};
 for(unsigned field=0;field<4;field++) {
  const char *label=labels[layout][field];
  for(unsigned i=0;label[i];i++)line[n++]=label[i];
  for(unsigned j=0;j<8;j++)line[n++]="0123456789abcdef"[(values[field]>>(28-j*4))&15];
 }
 line[n++]='\n';write(2,line,n);
}
static void tegu_render_refusal_inputs(id descriptor,TeguFunction *vertex,TeguFunction *fragment,
 const U64 formats[8],const U64 masks[8],const U64 blend[7],const TeguAIRVertexAttribute *attributes,NSUInteger attributeCount,
 const TeguAIRVertexLayout *layouts,NSUInteger layoutCount) {
 static unsigned reported;
 unsigned capture=__atomic_fetch_add(&reported,1,__ATOMIC_RELAXED);
 if(capture>=64)return;
 char line[256];NSUInteger n=0;
 const char *parts[4]={"TEGU_RENDER_INPUT vertex=",[vertex->functionName UTF8String],
  " fragment=",[fragment->functionName UTF8String]};
 for(unsigned p=0;p<4;p++)for(NSUInteger i=0;parts[p]&&parts[p][i]&&i<64;i++) {
  unsigned char c=(unsigned char)parts[p][i];line[n++]=c>=32&&c<127?(char)c:'.';
 }
 const char idPrefix[]=" capture=";
 for(unsigned i=0;i<sizeof(idPrefix)-1;i++)line[n++]=idPrefix[i];
 for(unsigned j=0;j<8;j++)line[n++]="0123456789abcdef"[(capture>>(28-j*4))&15];
 line[n++]='\n';write(2,line,n);
 const char *stateLabels[9]={" samples="," depth="," stencil="," coverage="," alphaOne="," raster="," topology="," amplification="," indirect="};
 U64 state[9]={[descriptor rasterSampleCount],[descriptor depthAttachmentPixelFormat],
  [descriptor stencilAttachmentPixelFormat],(U64)!![descriptor isAlphaToCoverageEnabled],
  (U64)!![descriptor isAlphaToOneEnabled],(U64)!![descriptor isRasterizationEnabled],
  [descriptor inputPrimitiveTopology],[descriptor maxVertexAmplificationCount],
  (U64)!![descriptor supportIndirectCommandBuffers]};
 for(unsigned row=0;row<2;row++) {
  n=0;const char *statePrefix=row?"TEGU_RENDER_FLAGS capture=":"TEGU_RENDER_RASTER capture=";
  for(unsigned i=0;statePrefix[i];i++)line[n++]=statePrefix[i];
  for(unsigned j=0;j<8;j++)line[n++]="0123456789abcdef"[(capture>>(28-j*4))&15];
  unsigned first=row?5:0,last=row?9:5;
  for(unsigned field=first;field<last;field++) {
   for(unsigned i=0;stateLabels[field][i];i++)line[n++]=stateLabels[field][i];
   for(unsigned j=0;j<16;j++)line[n++]="0123456789abcdef"[(state[field]>>(60-j*4))&15];
  }
  line[n++]='\n';write(2,line,n);
 }
 for(unsigned slot=0;slot<8;slot++) {
  n=0;const char prefix[]="TEGU_RENDER_ATTACHMENT capture=";
  for(unsigned i=0;i<sizeof(prefix)-1;i++)line[n++]=prefix[i];
  for(unsigned j=0;j<8;j++)line[n++]="0123456789abcdef"[(capture>>(28-j*4))&15];
  const char slotPrefix[]=" slot=";
  for(unsigned i=0;i<sizeof(slotPrefix)-1;i++)line[n++]=slotPrefix[i];
  line[n++]=(char)('0'+slot);
  const char *labels[2]={" format="," mask="};U64 values[2]={formats[slot],masks[slot]};
  for(unsigned field=0;field<2;field++) {
   for(unsigned i=0;labels[field][i];i++)line[n++]=labels[field][i];
   for(unsigned j=0;j<16;j++)line[n++]="0123456789abcdef"[(values[field]>>(60-j*4))&15];
  }
  line[n++]='\n';write(2,line,n);
 }
 for(NSUInteger i=0;i<attributeCount;i++) {
  const TeguAIRVertexAttribute *a=&attributes[i];
  const uint32_t row[4]={a->index,a->format,a->offset,a->buffer_index};
  tegu_render_vertex_capture_row(capture,0,row);
 }
 for(NSUInteger i=0;i<layoutCount;i++) {
  const TeguAIRVertexLayout *l=&layouts[i];
  const uint32_t row[4]={l->index,l->stride,l->step_function,l->step_rate};
  tegu_render_vertex_capture_row(capture,1,row);
 }
 n=0;const char blendPrefix[]="TEGU_RENDER_BLEND capture=";
 for(unsigned i=0;i<sizeof(blendPrefix)-1;i++)line[n++]=blendPrefix[i];
 for(unsigned j=0;j<8;j++)line[n++]="0123456789abcdef"[(capture>>(28-j*4))&15];
 const char *blendLabels[7]={" enabled="," rgbOp="," rgbSrc="," rgbDst="," alphaOp="," alphaSrc="," alphaDst="};
 for(unsigned field=0;field<7;field++) {
  for(unsigned i=0;blendLabels[field][i];i++)line[n++]=blendLabels[field][i];
  for(unsigned j=0;j<16;j++)line[n++]="0123456789abcdef"[(blend[field]>>(60-j*4))&15];
 }
 line[n++]='\n';write(2,line,n);
}
static int tegu_render_v6_resource_class_ok(const struct tegu_mali_resource_plan *p) {
 static const uint32_t stage[4]={1,1,2,2}, binding[4]={0,1,2,3}, bytes[4]={80,32,96,32};
 if(!p||p->version!=1||p->table!=1||p->texture_index!=4||p->sampler_index!=5||
    p->normalized_coordinates!=1||p->sample_count!=4)return 0;
 for(unsigned i=0;i<4;i++)if(p->ubos[i].stage!=stage[i]||p->ubos[i].descriptor_set||
    p->ubos[i].binding!=binding[i]||p->ubos[i].table!=1||p->ubos[i].index!=i||
    p->ubos[i].declared_bytes!=bytes[i])return 0;
 return 1;
}
static int tegu_render_v6_make_package(TeguRenderPipeline *p, const struct tegu_mali_resource_plan *rp) {
 const struct tegu_mali_graphics_pair2 *q=&p->pair;
 if(q->version!=2||q->vs_attribute_mask!=3||q->position_offset!=512||q->secondary_offset!=1024||
    q->secondary_registers!=32||q->secondary_preload!=0x3800000000000000ULL||q->generic_size_B!=8||
    p->vertexCode.size!=1152||p->fragmentCode.size!=7424||p->vertexCode.registers!=32||p->fragmentCode.registers!=64||
    p->vertexCode.sysval_mask!=3||p->fragmentCode.sysval_mask!=8||p->vertexCode.push_bytes!=8||
    p->fragmentCode.push_bytes!=76||p->push.vertex_span!=8||p->push.fragment_span!=76||
    !tegu_render_v6_resource_class_ok(rp))return -1;
 NSUInteger fs=TEGU_RENDER_V6_CODE+((p->vertexCode.size+63)&~(NSUInteger)63);
 p->packageBytes=fs+p->fragmentCode.size;
 if(p->packageBytes>TEGU_RENDER_V6_MAX_PACKAGE)return -1;
 p->package=malloc(p->packageBytes);if(!p->package)return -1;memset(p->package,0,p->packageBytes);
 uint32_t h[32]={TEGU_RENDER_MAGIC,6,0xb8a21030u,80,(uint32_t)p->vertexCode.size,(uint32_t)p->fragmentCode.size,8,76};
 const struct tegu_mali_graphics *code[2]={&p->vertexCode,&p->fragmentCode};
 for(unsigned i=0;i<2;i++){uint32_t *x=h+8+i*8;x[0]=code[i]->registers;x[1]=code[i]->fau_words;x[2]=code[i]->sysval_mask;x[3]=code[i]->tls;x[4]=(uint32_t)code[i]->preload;x[5]=(uint32_t)(code[i]->preload>>32);x[6]=code[i]->push_bytes;}
 h[24]=q->vs_attribute_mask;h[25]=2;h[26]=q->position_offset;h[27]=q->secondary_offset;h[28]=q->secondary_registers;
 h[29]=(uint32_t)q->secondary_preload;h[30]=(uint32_t)(q->secondary_preload>>32);h[31]=q->generic_size_B;
 for(unsigned i=0;i<32;i++)tegu_render_put(p->package+4*i,h[i]);
 const uint32_t blend[9]={p->blend.version,p->blend.enabled,p->blend.rgb_op,p->blend.rgb_src,p->blend.rgb_dst,p->blend.alpha_op,p->blend.alpha_src,p->blend.alpha_dst,p->blend.write_mask};
 for(unsigned i=0;i<9;i++)tegu_render_put(p->package+128+4*i,blend[i]);
 const uint32_t texture[7]={1,16,3,0,0,4,1};for(unsigned i=0;i<7;i++)tegu_render_put(p->package+192+4*i,texture[i]);
 const uint32_t resources[26]={1,1,0,4,1,1,4,4,1,1,2,3,2,1,6,3,0,64,16,72,2,0,4,64,68,72};
 for(unsigned i=0;i<26;i++)tegu_render_put(p->package+256+4*i,resources[i]);
 tegu_render_put(p->package+384,80);tegu_render_put(p->package+388,15);tegu_render_put(p->package+392,80);
 tegu_render_put(p->package+396,0);
 for(unsigned i=0;i<p->vertexFetch.count;i++){
  const struct tegu_vertex_attribute *a=&p->vertexFetch.attributes[i];
  uint32_t row[7]={a->location,a->buffer_index,a->format,a->offset,a->stride,a->input_rate,a->divisor};
  for(unsigned j=0;j<7;j++)tegu_render_put(p->package+TEGU_RENDER_V6_ATTRIBUTE+i*28+j*4,row[j]);
 }
 memcpy(p->package+TEGU_RENDER_V6_PUSH,p->push.vertex,128);
 memcpy(p->package+TEGU_RENDER_V6_PUSH+128,p->push.fragment,128);
 /* These are dynamic UBO residuals/texel format, never pipeline-captured data. */
 memset(p->package+TEGU_RENDER_V6_PUSH,0,8);memset(p->package+TEGU_RENDER_V6_PUSH+192,0,12);
 memcpy(p->package+TEGU_RENDER_V6_CODE,p->vertexCode.code,p->vertexCode.size);
 memcpy(p->package+fs,p->fragmentCode.code,p->fragmentCode.size);
 struct tegu_render_info parsed;
 if(tegu_render_parse_for_import(p->package,p->packageBytes,1,&parsed)||parsed.version!=6||
    parsed.vertex_count!=2||parsed.vertex[0].buffer_index!=p->vertexFetch.attributes[0].buffer_index||
    parsed.vertex[1].buffer_index!=p->vertexFetch.attributes[1].buffer_index||
    parsed.vertex[0].stride!=p->vertexFetch.attributes[0].stride||
    parsed.vertex[1].stride!=p->vertexFetch.attributes[1].stride)return -1;
 p->compositorV6=1;p->compositorSnapshotOnly=1;p->colorFormat=80;return 0;
}
static unsigned tegu_render_compositor_sample_class(TeguFunction *vertex,TeguFunction *fragment) {
 if(!vertex||!fragment||![vertex->functionName isEqualToString:[NSString stringWithUTF8String:"downsample_blur_vert_lph"]])return 0;
 if([fragment->functionName isEqualToString:[NSString stringWithUTF8String:"downsample_4_frag_lph"]])return 4;
 if([fragment->functionName isEqualToString:[NSString stringWithUTF8String:"downsample_8_frag_lph"]])return 8;
 return 0;
}
static int tegu_render_attachment_clear_class(TeguFunction *vertex,TeguFunction *fragment) {
 return vertex&&fragment&&
  [vertex->functionName isEqualToString:[NSString stringWithUTF8String:"path_blit_vert_lph"]]&&
  [fragment->functionName isEqualToString:[NSString stringWithUTF8String:"attachment_clear_frag_lph"]];
}
static int tegu_render_home_affordance_class(TeguFunction *vertex,TeguFunction *fragment) {
 return vertex&&fragment&&
  [vertex->functionName isEqualToString:[NSString stringWithUTF8String:"std_vert1_lph"]]&&
  [fragment->functionName isEqualToString:[NSString stringWithUTF8String:"home_affordance_base_lph"]];
}
static int tegu_render_fixed_ui_class(TeguFunction *vertex,TeguFunction *fragment) {
 return vertex&&fragment&&
  [vertex->functionName isEqualToString:[NSString stringWithUTF8String:"fixed_vert_lph_spc"]]&&
  [fragment->functionName isEqualToString:[NSString stringWithUTF8String:"fixed_frag_lph_cph"]];
}
static int tegu_render_sphere_class(TeguFunction *vertex,TeguFunction *fragment) {
 return vertex&&fragment&&
  [vertex->functionName isEqualToString:[NSString stringWithUTF8String:"sphereVert"]]&&
  [fragment->functionName isEqualToString:[NSString stringWithUTF8String:"sphereFrag"]];
}
static int tegu_render_single_pass_blur3_class(TeguFunction *vertex,TeguFunction *fragment) {
 return vertex&&fragment&&
  [vertex->functionName isEqualToString:[NSString stringWithUTF8String:"downsample_blur_vert_lph"]]&&
  [fragment->functionName isEqualToString:[NSString stringWithUTF8String:"single_pass_blur_3_lph"]];
}
static int tegu_render_catalog_profile_exact(const void *profile,size_t bytes) {
 char digest[65];
 return !tegu_compiler_identity_sha256(profile,bytes,digest)&&
  tegu_qc_streq(digest,tegu_qc_catalog_profile_sha256);
}
static int tegu_render_catalog_descriptor(id descriptor,id attachments,
 const TeguAIRVertexAttribute *attributes,NSUInteger attributeCount,
 const TeguAIRVertexLayout *layouts,NSUInteger layoutCount,
 struct tegu_qc_catalog_descriptor *out) {
 if(!descriptor||!attachments||!out||attributeCount>31||layoutCount>31)return -1;
 struct tegu_qc_catalog_descriptor d={0};
 NSUInteger scalar[9]={[descriptor rasterSampleCount],[descriptor depthAttachmentPixelFormat],
  [descriptor stencilAttachmentPixelFormat],(NSUInteger)!![descriptor isAlphaToCoverageEnabled],
  (NSUInteger)!![descriptor isAlphaToOneEnabled],(NSUInteger)!![descriptor isRasterizationEnabled],
  [descriptor inputPrimitiveTopology],[descriptor maxVertexAmplificationCount],
  (NSUInteger)!![descriptor supportIndirectCommandBuffers]};
 for(unsigned i=0;i<9;i++)if(scalar[i]>UINT32_MAX)return -1;
 d.raster_sample_count=(uint32_t)scalar[0];d.depth_format=(uint32_t)scalar[1];
 d.stencil_format=(uint32_t)scalar[2];d.alpha_to_coverage=(uint32_t)scalar[3];
 d.alpha_to_one=(uint32_t)scalar[4];d.rasterization=(uint32_t)scalar[5];
 d.input_topology=(uint32_t)scalar[6];d.max_vertex_amplification=(uint32_t)scalar[7];
 d.indirect_commands=(uint32_t)scalar[8];
 for(unsigned i=0;i<8;i++) {
  TeguRenderColorDescriptor *a=[attachments objectAtIndexedSubscript:i];
  NSUInteger v[9]={[a pixelFormat],[a writeMask],(NSUInteger)!![a isBlendingEnabled],
   [a rgbBlendOperation],[a sourceRGBBlendFactor],[a destinationRGBBlendFactor],
   [a alphaBlendOperation],[a sourceAlphaBlendFactor],[a destinationAlphaBlendFactor]};
  for(unsigned j=0;j<9;j++)if(v[j]>UINT32_MAX)return -1;
  struct tegu_qc_catalog_attachment *x=&d.attachments[i];
  x->format=(uint32_t)v[0];x->write_mask=(uint32_t)v[1];x->blend.enabled=(uint32_t)v[2];
  x->blend.rgb_op=(uint32_t)v[3];x->blend.rgb_src=(uint32_t)v[4];x->blend.rgb_dst=(uint32_t)v[5];
  x->blend.alpha_op=(uint32_t)v[6];x->blend.alpha_src=(uint32_t)v[7];x->blend.alpha_dst=(uint32_t)v[8];
 }
 d.attribute_count=(uint32_t)attributeCount;d.layout_count=(uint32_t)layoutCount;
 for(NSUInteger i=0;i<attributeCount;i++)d.attributes[i]=(struct tegu_qc_catalog_attribute){
  attributes[i].index,attributes[i].format,attributes[i].offset,attributes[i].buffer_index};
 for(NSUInteger i=0;i<layoutCount;i++)d.layouts[i]=(struct tegu_qc_catalog_layout){
  layouts[i].index,layouts[i].stride,layouts[i].step_function,layouts[i].step_rate};
 *out=d;return 0;
}
static int tegu_render_b3_make_package(TeguRenderPipeline *p,
 const struct tegu_mali_single_pass_blur3_plan *rp) {
 const struct tegu_mali_graphics_pair2 *q=p?&p->pair:NULL;
 if(!p||!rp||rp->version!=TEGU_MALI_SINGLE_PASS_BLUR3_PLAN_VERSION||rp->table!=1||
    rp->texture_index!=4||rp->sampler_index!=5||rp->normalized_coordinates!=1||
    rp->sample_count!=4||rp->implicit_lod!=1||p->vertexCode.size!=TEGU_NATIVE_B3_VS_BYTES||
    p->fragmentCode.size!=TEGU_NATIVE_B3_FS_BYTES||p->vertexCode.registers!=32||
    p->fragmentCode.registers!=64||p->vertexCode.fau_words!=34||p->fragmentCode.fau_words!=51||
    p->vertexCode.sysval_mask!=3||p->fragmentCode.sysval_mask!=8||p->vertexCode.push_bytes!=8||
    p->fragmentCode.push_bytes!=76||p->vertexCode.preload!=0x3800000000000000ULL||
    p->fragmentCode.preload!=0x3000000000000000ULL||p->vertexCode.tls||p->fragmentCode.tls||
    !q||q->version!=2||q->flags!=TEGU_MALI_PAIR2_WRITES_POINT_SIZE||q->vs_attribute_mask!=3||
    q->position_offset!=512||q->position_size!=512||q->point_position_size!=512||
    q->secondary_offset!=1024||q->secondary_size!=128||q->secondary_registers!=32||
    q->secondary_preload!=0x3800000000000000ULL||q->generic_size_B!=8||
    q->vs_varying_mask!=1||q->fs_varying_mask!=1||q->varying_count!=1)return -1;
 static const uint32_t stage[4]={1,1,2,2},binding[4]={0,1,2,3},declared[4]={80,32,112,32};
 for(unsigned i=0;i<4;i++)if(rp->ubos[i].stage!=stage[i]||rp->ubos[i].descriptor_set||
    rp->ubos[i].binding!=binding[i]||rp->ubos[i].table!=1||rp->ubos[i].index!=i||
    rp->ubos[i].declared_bytes!=declared[i])return -1;
 uint8_t *bytes=NULL;size_t n=0;
 if(tegu_native_b3_build(p->vertexCode.code,p->vertexCode.size,p->fragmentCode.code,
    p->fragmentCode.size,&bytes,&n)||!bytes||n!=TEGU_NATIVE_B3_PACKAGE_BYTES)return -1;
 struct tegu_native_b3_info info;
 if(tegu_native_b3_parse(bytes,n,&info)||info.ubo_window[2]!=82||info.sample_count!=4) {
  tegu_native_b3_free(&bytes,&n);return -1;
 }
 p->package=bytes;p->packageBytes=n;p->compositorB3=1;p->compositorSnapshotOnly=1;
 p->colorFormat=80;
 return 0;
}
static int tegu_render_v7_compiled_state_supported(const TeguRenderPipeline *p) {
 if(!p)return 0;
 static const uint32_t fau[2]={34,51},tls[2]={0,0};
 static const uint64_t preload[2]={0x3800000000000000ULL,0x3000000000000000ULL};
 const struct tegu_mali_graphics *stage[2]={&p->vertexCode,&p->fragmentCode};
 for(unsigned i=0;i<2;i++)if(stage[i]->fau_words!=fau[i]||stage[i]->tls!=tls[i]||stage[i]->preload!=preload[i])return 0;
 if(p->push.version!=1||p->push.vertex_span!=8||p->push.fragment_span!=76)return 0;
  /* The exact AIR metadata gate has already proved that every push placement
   * is one of the four draw residuals or texel-format field. There is no
   * pipeline-static word to copy; keep the immutable snapshot zero and let
   * the kernel's checked bind step supply those dynamic words. */
 for(unsigned i=0;i<sizeof(p->push.vertex);i++)if(p->push.vertex[i])return 0;
 for(unsigned i=0;i<sizeof(p->push.fragment);i++)if(p->push.fragment[i])return 0;
 return 1;
}
static int tegu_render_v7_make_package(TeguRenderPipeline *p,const struct tegu_mali_resource_plan *rp) {
 const struct tegu_mali_graphics_pair2 *q=p?&p->pair:NULL;
 uint32_t blendClass=p?tegu_native_v7_blend_class(&p->blend):0;
 if(!p||!rp||!tegu_render_v7_compiled_state_supported(p)||p->vertexCode.size!=1152||p->fragmentCode.size!=25600||
    p->vertexCode.registers!=32||p->fragmentCode.registers!=64||
    p->vertexCode.sysval_mask!=3||p->fragmentCode.sysval_mask!=8||
    p->vertexCode.push_bytes!=8||p->fragmentCode.push_bytes!=76||
    p->push.vertex_span!=8||p->push.fragment_span!=76||rp->version!=1||
    rp->table!=1||rp->texture_index!=4||rp->sampler_index!=5||
    rp->normalized_coordinates!=1||rp->sample_count!=16||
    q->version!=2||q->vs_attribute_mask!=3||q->position_offset!=512||q->secondary_offset!=1024||
    q->secondary_registers!=32||q->secondary_preload!=0x3800000000000000ULL||q->generic_size_B!=8||
    q->flags!=TEGU_MALI_PAIR2_WRITES_POINT_SIZE||q->position_size!=512||q->point_position_offset!=0||
    q->point_position_size!=512||q->vs_varying_mask!=1||q->fs_varying_mask!=1||
    q->fs_uses_ld_var||q->noperspective_mask||q->needs_extended_fifo||
    q->point_size_section!=1||q->point_size_offset!=0||q->point_size_alu_type!=144||
    q->varying_count!=1||q->varyings[0].location!=0||q->varyings[0].offset!=0||
    q->varyings[0].components!=2||q->varyings[0].alu_type!=160||
    p->vertexFetch.version!=1||p->vertexFetch.mask!=3||p->vertexFetch.count!=2||!blendClass)return -1;
 static const uint32_t ubo_stage[4]={1,1,2,2},ubo_binding[4]={0,1,2,3};
 static const uint32_t ubo_index[4]={0,1,2,3},expected[4]={80,32,48,32};
 for(unsigned i=0;i<4;i++)if(rp->ubos[i].stage!=ubo_stage[i]||rp->ubos[i].descriptor_set||
    rp->ubos[i].binding!=ubo_binding[i]||rp->ubos[i].table!=1||rp->ubos[i].index!=ubo_index[i]||
    rp->ubos[i].declared_bytes!=expected[i])return -1;
 size_t n=0;uint8_t *bytes=NULL;
 if(tegu_native_v7_build_with_blend(p->vertexCode.code,p->vertexCode.size,p->fragmentCode.code,p->fragmentCode.size,blendClass,&bytes,&n)||!bytes||!n)return -1;
 if(memcmp(bytes+TEGU_NATIVE_V7_PUSH,p->push.vertex,sizeof(p->push.vertex))||
    memcmp(bytes+TEGU_NATIVE_V7_PUSH+128,p->push.fragment,sizeof(p->push.fragment))) {
  tegu_native_v7_free(&bytes,&n);return -1;
 }
 struct tegu_native_v7_info info;
 if(tegu_native_v7_parse(bytes,n,&info)||info.sample_count!=16||info.ubo_declared_bytes[2]!=48) {tegu_native_v7_free(&bytes,&n);return -1;}
 struct tegu_render_info adapted;
 if(tegu_render_parse_for_import_classes(bytes,n,1,1,&adapted)||adapted.version!=7) {tegu_native_v7_free(&bytes,&n);return -1;}
 p->package=(unsigned char *)bytes;p->packageBytes=(NSUInteger)n;
 p->compositorSampleClass=8;p->compositorV7=1;p->compositorSnapshotOnly=1;p->colorFormat=80;return 0;
}
static void tegu_ac1_selector8_marker(const char *phase,int status,unsigned outputs,U64 handle,NSUInteger bytes) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
 char line[192];int n=snprintf(line,sizeof(line),"TEGU_AC1_SELECTOR8 %s status=0x%08x outputs=%u handle=0x%llx bytes=%lu\n",
  phase,(uint32_t)status,outputs,handle,(unsigned long)bytes);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void tegu_b3_lifecycle_marker(const char *phase,int status,unsigned outputs,U64 handle,
 NSUInteger vsBytes,NSUInteger fsBytes,NSUInteger packageBytes) {
 /* Three records describe one B3 construction.  Bound the process-wide trace
  * to four constructions so repeated SpringBoard retries cannot flood USB. */
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=12)return;
 char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_B3_LIFECYCLE %s status=0x%08x outputs=%u handle=0x%llx vs_bytes=%lu fs_bytes=%lu package_bytes=%lu\n",
  phase,(uint32_t)status,outputs,handle,(unsigned long)vsBytes,(unsigned long)fsBytes,
  (unsigned long)packageBytes);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
@interface TeguComputeDevice (TeguPipelineBuild)
- (id)teguBuildRenderPipeline:(id)descriptor error:(id *)error;
- (void)newRenderPipelineStateWithDescriptor:(id)descriptor
 completionHandler:(void (^)(id pipeline,id error))completionHandler;
- (id)newRenderPipelineStateWithTileDescriptor:(id)descriptor options:(NSUInteger)options
 reflection:(id *)reflection error:(id *)error;
@end
@implementation TeguComputeDevice (RenderPipelines)
/* Pipeline builds compile AIR -> SPIR-V -> NIR -> Mali on the CPU, inside the
 * caller's frame; TEGU_PERF reports their time (compile_ms). */
- (id)newRenderPipelineStateWithDescriptor:(id)descriptor error:(id *)error {
 double start=tegu_host_seconds();id pipeline=[self teguBuildRenderPipeline:descriptor error:error];
 if(pipeline&&descriptor&&tegu_owned_class(pipeline,"TeguRenderPipeline"))
  ((TeguRenderPipeline *)pipeline)->rasterSampleCount=(unsigned)[descriptor rasterSampleCount];
 tegu_perf_compile_note(tegu_host_seconds()-start);return pipeline;
}
- (void)newRenderPipelineStateWithDescriptor:(id)descriptor
 completionHandler:(void (^)(id pipeline,id error))completionHandler {
 id error=0;id pipeline=[self newRenderPipelineStateWithDescriptor:descriptor error:&error];
 if(completionHandler)completionHandler(pipeline,error);
 [pipeline release]; /* borrowed by the handler; balance the synchronous +1 */
}
- (id)newRenderPipelineStateWithTileDescriptor:(id)descriptor options:(NSUInteger)options
 reflection:(id *)reflection error:(id *)error {
 (void)descriptor;(void)options;
 if(reflection)*reflection=0;
 if(error) {
  id description=[NSString stringWithUTF8String:"Tile render pipelines are unsupported"];
  id info=description?[NSDictionary dictionaryWithObject:description forKey:@"NSLocalizedDescription"]:0;
  *error=[NSError errorWithDomain:@"TeguRenderPipeline" code:2 userInfo:info];
 }
 return 0;
}
- (id)teguBuildRenderPipeline:(id)descriptor error:(id *)error {
 if(error)*error=0;
 TeguRenderPipeline *pipeline=0;
 TeguAIRRenderBlend blend={1,0,0,1,0,0,1,0,15};
 TeguAIRVertexAttribute vertexAttributes[31];TeguAIRVertexLayout vertexLayouts[31];
 NSUInteger attributeCount=0,layoutCount=0;unsigned usedLayouts=0;
 U64 diagnosticFormats[8]={0},diagnosticMasks[8]={0},diagnosticBlend[7]={0};int diagnosticOwned=0,diagnosticLogged=0,nord2DescriptorValid=0;
 TeguAIRBlob vs={0},fs={0},meta={0};char message[512]={0};
 struct tegu_compiler_trace compilerTrace={0xffffffffu,0,0,0};
 TeguFunction *vertex=[descriptor vertexFunction],*fragment=[descriptor fragmentFunction];
 /* Keep each refusal identifiable on device: donor descriptor defaults can
  * differ from the host fixture, and a generic error hides the failing gate. */
 #define RENDER_REQUIRE(condition, reason) do {if(!(condition)) { \
  __builtin_memcpy(message,reason,sizeof(reason));goto fail;}}while(0)
 RENDER_REQUIRE(descriptor&&vertex&&fragment,"render descriptor missing shader");
 NSUInteger depthFormat=[descriptor depthAttachmentPixelFormat],stencilFormat=[descriptor stencilAttachmentPixelFormat];
 RENDER_REQUIRE(tegu_function_owned_class(vertex)&&tegu_function_owned_class(fragment),"render shader requires native library bridge");
 diagnosticOwned=1;
 NSUInteger rasterSamples=[descriptor rasterSampleCount];
 RENDER_REQUIRE(vertex->kind==1&&fragment->kind==2,"render shader stage mismatch");
 RENDER_REQUIRE(vertex->library->owner==self&&fragment->library->owner==self,"render shader device mismatch");
 /* Snapshot Metal's static vertex descriptor into AIR compiler inputs. Native
  * validation below still refuses any fetch the backend cannot supply. */
 id vertexLayout=[descriptor vertexDescriptor];
 id attributes=[vertexLayout attributes];
 for(NSUInteger i=0;vertexLayout&&i<31;i++) {
  id a=[attributes objectAtIndexedSubscript:i];NSUInteger format=[a format];if(!format)continue;
  NSUInteger offset=[a offset],buffer=[a bufferIndex];
  RENDER_REQUIRE(format<=0xffffffffUL&&offset<=0xffffffffUL&&buffer<31,"render vertex attribute outside static descriptor range");
  vertexAttributes[attributeCount++]=(TeguAIRVertexAttribute){(uint32_t)i,(uint32_t)format,(uint32_t)offset,(uint32_t)buffer};
  usedLayouts|=1u<<buffer;
 }
 id layouts=[vertexLayout layouts];
 for(NSUInteger i=0;vertexLayout&&i<31;i++) {
  id l=[layouts objectAtIndexedSubscript:i];NSUInteger stride=[l stride];
  if(!stride&&!(usedLayouts&(1u<<i)))continue;
  NSUInteger step=[l stepFunction],rate=[l stepRate];
  RENDER_REQUIRE(l&&stride<=0xffffffffUL&&step<=0xffffffffUL&&rate<=0xffffffffUL,"render vertex layout outside static descriptor range");
  vertexLayouts[layoutCount++]=(TeguAIRVertexLayout){(uint32_t)i,(uint32_t)stride,(uint32_t)step,(uint32_t)rate};
 }
 id attachments=[descriptor colorAttachments];
 TeguRenderColorDescriptor *color=[attachments objectAtIndexedSubscript:0];
 for(unsigned i=0;i<8;i++) {
  TeguRenderColorDescriptor *attachment=[attachments objectAtIndexedSubscript:i];
  diagnosticFormats[i]=[attachment pixelFormat];diagnosticMasks[i]=[attachment writeMask];
 }
 diagnosticBlend[0]=[color isBlendingEnabled];diagnosticBlend[1]=[color rgbBlendOperation];
 diagnosticBlend[2]=[color sourceRGBBlendFactor];diagnosticBlend[3]=[color destinationRGBBlendFactor];
 diagnosticBlend[4]=[color alphaBlendOperation];diagnosticBlend[5]=[color sourceAlphaBlendFactor];
 diagnosticBlend[6]=[color destinationAlphaBlendFactor];
#ifndef TEGU_DISABLE_MSAA4 /* see nord2_render_pass.inc: 4x passes are one JM job */
 RENDER_REQUIRE(rasterSamples==1||rasterSamples==4,"render sample count unsupported");
#else
 RENDER_REQUIRE(rasterSamples==1,"render sample count unsupported"); /* see nord2_render_pass.inc */
#endif
#ifdef NORD2_JM_TRANSPORT
 /* The Mali generic job cannot execute a depth/stencil test yet, but a Metal
  * render pipeline that names a depth/stencil attachment is valid. Refusing it
  * made QuartzCore abort after its PSO retry. Admit only real depth/stencil
  * enum values; execution still refuses any draw whose state tests/writes. */
 RENDER_REQUIRE(!depthFormat||((depthFormat>=250&&depthFormat<=255)||depthFormat==260),"render depth format unsupported");
 RENDER_REQUIRE(!stencilFormat||((stencilFormat>=250&&stencilFormat<=255)||stencilFormat==260),"render stencil format unsupported");
#else
 RENDER_REQUIRE(!depthFormat,"render depth format unsupported");
 RENDER_REQUIRE(!stencilFormat,"render stencil format unsupported");
#endif
 RENDER_REQUIRE(![descriptor isAlphaToCoverageEnabled],"render alpha coverage unsupported");
 RENDER_REQUIRE(![descriptor isAlphaToOneEnabled],"render alpha to one unsupported");
 RENDER_REQUIRE([descriptor isRasterizationEnabled],"render rasterization disabled");
 RENDER_REQUIRE([descriptor inputPrimitiveTopology]==0||[descriptor inputPrimitiveTopology]==3,"render topology unsupported");
 RENDER_REQUIRE([descriptor maxVertexAmplificationCount]==1,"render vertex amplification unsupported");
 RENDER_REQUIRE(![descriptor supportIndirectCommandBuffers],"render indirect commands unsupported");
 nord2DescriptorValid=1;
 NSUInteger format=[color pixelFormat];
#ifdef NORD2_JM_TRANSPORT
 /* The CSF package specializations below are not an arch9 hardware ABI.
  * Use their general AIR/reflection compiler for every ordinary BGRA draw. */
 int nord2Single=format&&n2r_bpp((unsigned)format)&&[color writeMask]<=15;
 for(unsigned rt=1;rt<8;rt++)if(diagnosticFormats[rt])nord2Single=0;
 if(nord2Single) {
  struct tegu_blend_inputs bi={1,[color isBlendingEnabled]?1u:0u,
   (uint32_t)[color rgbBlendOperation],(uint32_t)[color sourceRGBBlendFactor],(uint32_t)[color destinationRGBBlendFactor],
   (uint32_t)[color alphaBlendOperation],(uint32_t)[color sourceAlphaBlendFactor],(uint32_t)[color destinationAlphaBlendFactor],(uint32_t)[color writeMask]};
  struct tegu_blend_state checked;
  RENDER_REQUIRE(!tegu_blend_build_format((unsigned)format,&bi,&checked),"arch9 blend equation unsupported");
  pipeline=[[TeguRenderPipeline alloc] init];if(!pipeline)goto fail;
  pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];pipeline->colorFormat=format;
  pipeline->genericRtCount=1;pipeline->genericRtActive=1;pipeline->genericRtFormats[0]=format;
  pipeline->genericRtMasks[0]=(uint32_t)[color writeMask];
  pipeline->depthFormat=(unsigned)depthFormat;pipeline->stencilFormat=(unsigned)stencilFormat;
  if(tegu_render_generic_retranslate(pipeline,vertex,fragment,vertexAttributes,attributeCount,vertexLayouts,layoutCount,&bi,message,sizeof(message)))goto fail;
  return pipeline;
 }
 /* Other attachment combinations enter the generic MRT path below. */
 snprintf(message,sizeof(message),"Nord2 general attachment path");goto fail;
#endif
 unsigned compositorSamples=tegu_render_compositor_sample_class(vertex,fragment);
 int attachmentClear=tegu_render_attachment_clear_class(vertex,fragment);
 int homeAffordance=tegu_render_home_affordance_class(vertex,fragment);
 int fixedUi=tegu_render_fixed_ui_class(vertex,fragment);
 int sphereClass=tegu_render_sphere_class(vertex,fragment);
 int singlePassBlur3=tegu_render_single_pass_blur3_class(vertex,fragment);
 const char *catalogVertex=[vertex->functionName UTF8String],*catalogFragment=[fragment->functionName UTF8String];
 const struct tegu_qc_catalog_entry *catalogNamed=tegu_qc_catalog_find_names(catalogVertex,catalogFragment);
 if((capabilities&TEGU_GPU_CAP_ATTACHMENT_CLEAR)&&attachmentClear) {
  compilerTrace=tegu_compiler_trace_begin();
  tegu_render_refusal_inputs(descriptor,vertex,fragment,diagnosticFormats,diagnosticMasks,diagnosticBlend,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount);diagnosticLogged=1;
  RENDER_REQUIRE(TEGU_HAS_ATTACHMENT_CLEAR_PROFILE,"attachment-clear translator profile unavailable");
  RENDER_REQUIRE(diagnosticFormats[0]==80&&diagnosticMasks[0]==0&&diagnosticFormats[1]==80&&diagnosticMasks[1]==15,
   "attachment-clear targets outside exact RT0-mask0 RT1-mask15 class");
  for(NSUInteger i=2;i<8;i++)RENDER_REQUIRE(!diagnosticFormats[i],"attachment-clear extra color attachment unsupported");
  TeguRenderColorDescriptor *color1=[attachments objectAtIndexedSubscript:1];
  RENDER_REQUIRE(![color isBlendingEnabled]&&![color1 isBlendingEnabled],"attachment-clear blending unsupported");
  pipeline=[[TeguRenderPipeline alloc] init];if(!pipeline)goto fail;
  pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];
  const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];NSUInteger vl=strlen(vn),fl=strlen(fn);
  /* The frozen translator class was produced with two full-mask declarations;
   * logical Metal masks remain attested above and in the exact compiler plan.
   * Supplying asymmetric translator masks requests IndependentBlend and changes
   * the qualified SPIR-V before the native target descriptors exist. */
  TeguAIRRenderColorAttachment ca[2]={{80,15},{80,15}};
  TeguAIRRenderBlend blends[2]={{1,0,1,1,0,1,1,0,15},{1,0,1,1,0,1,1,0,15}};
  (void)vl;(void)fl;
  pipeline->compiled=tegu_pc_render_create(TEGU_PC_AIR_ATTACHMENT_BLENDS,vertex,fragment,0,ca,2,blends,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount,
   tegu_attachment_clear_profile,sizeof(tegu_attachment_clear_profile),&vs,&fs,&meta,message,sizeof(message));
  if(!pipeline->compiled){if(!message[0])__builtin_memcpy(message,"attachment-clear AIR compilation failed",sizeof("attachment-clear AIR compilation failed"));goto fail;}
  tegu_lock(&tegu_compiler_lock);
  int clearCompile=tegu_pc_mali_pair(TEGU_PC_MALI_ATTACHMENT_CLEAR_V2,0,vs.bytes,vs.length,fs.bytes,fs.length,
   &pipeline->vertexCode,&pipeline->fragmentCode,0,&pipeline->attachmentClearPlan,sizeof(pipeline->attachmentClearPlan),
   message,sizeof(message));
  tegu_unlock(&tegu_compiler_lock);
  if(clearCompile){tegu_compile_message(message);goto fail;}
  {
   char line[256];int n=snprintf(line,sizeof(line),
    "TEGU_AC1_PACKAGE_INPUT rev=3 out=%u out_bytes=%lu library=%u profile=%lu vs=%u/%lu fs=%u/%lu\n",
    pipeline->package!=0,(unsigned long)pipeline->packageBytes,
    vertex->library->sourceSHA256[0]!=0,(unsigned long)sizeof(tegu_attachment_clear_profile),
    vs.bytes!=0,(unsigned long)vs.length,fs.bytes!=0,(unsigned long)fs.length);
   if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
  }
  int packageStatus=tegu_attachment_clear_make_package(vertex->library->sourceSHA256,
   tegu_attachment_clear_profile,sizeof(tegu_attachment_clear_profile),&vs,&fs,&pipeline->vertexCode,&pipeline->fragmentCode,
   &pipeline->attachmentClearPlan,&pipeline->package,&pipeline->packageBytes);
  if(packageStatus){snprintf(message,sizeof(message),"attachment-clear native package identity mismatch status=%d",packageStatus);goto fail;}
  /* TAC1 is deliberately sent through the ordinary owned pipeline import.
   * Existing kernels reject its distinct magic.  A kernel which implements
   * TAC1 can return the same owned-handle ABI without a userspace selector
   * change; until then NSError preserves the exact import boundary. */
  unsigned outputs=1;U64 imported=0;
  tegu_ac1_selector8_marker("begin",0,outputs,0,pipeline->packageBytes);
  int importStatus=method?method(connection,8,0,0,pipeline->package,pipeline->packageBytes,&imported,&outputs,0,0):-1;
  tegu_ac1_selector8_marker(importStatus||outputs!=1||!imported?"failure":"success",importStatus,outputs,imported,pipeline->packageBytes);
  if(imported>=0xc0000001u&&imported<=0xffffffffu) {
   pipeline->handle=imported;pipeline->allocated=(pipeline->packageBytes+16383)&~(NSUInteger)16383;
   __atomic_fetch_add(&allocatedBytes,pipeline->allocated,__ATOMIC_RELAXED);
  }
  if(importStatus||outputs!=1||!pipeline->handle) {
   snprintf(message,sizeof(message),"kernel TAC1 pipeline import unavailable status=0x%08x outputs=%u handle=0x%llx bytes=%lu",
    (uint32_t)importStatus,outputs,imported,(unsigned long)pipeline->packageBytes);goto fail;
  }
  pipeline->attachmentClearSnapshotOnly=1;pipeline->colorFormat=80;
  tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);
  return pipeline;
 }
 if((capabilities&TEGU_GPU_CAP_COMPOSITOR)&&compositorSamples) {
  compilerTrace=tegu_compiler_trace_begin();
  tegu_render_refusal_inputs(descriptor,vertex,fragment,diagnosticFormats,diagnosticMasks,diagnosticBlend,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount);diagnosticLogged=1;
  RENDER_REQUIRE(TEGU_HAS_COMPOSITOR_PROFILE,"compositor translator profile unavailable");
  RENDER_REQUIRE(compositorSamples!=8||(capabilities&TEGU_GPU_CAP_COMPOSITOR_V7),"8-tap compositor capability unavailable");
  RENDER_REQUIRE(format==80&&diagnosticMasks[0]==15&&diagnosticFormats[1]==80&&diagnosticMasks[1]==0,"compositor attachments outside captured two-target contract");
  for(NSUInteger i=2;i<8;i++)RENDER_REQUIRE(!diagnosticFormats[i],"compositor extra color attachment unsupported");
  RENDER_REQUIRE(attributeCount>=2&&layoutCount>=1,"compositor vertex descriptor missing active inputs or layout");
  uint32_t metalSlot=vertexAttributes[0].buffer_index;
  TeguAIRVertexLayout *activeLayout=NULL;
  if(metalSlot<=1)for(NSUInteger i=0;i<layoutCount;i++)
    if(vertexLayouts[i].index==metalSlot)activeLayout=&vertexLayouts[i];
  TeguRenderColorDescriptor *color1=[attachments objectAtIndexedSubscript:1];
  TeguAIRRenderBlend compositorBlends[2]={
   {1,[color isBlendingEnabled],[color rgbBlendOperation],[color sourceRGBBlendFactor],[color destinationRGBBlendFactor],[color alphaBlendOperation],[color sourceAlphaBlendFactor],[color destinationAlphaBlendFactor],(uint32_t)diagnosticMasks[0]},
   {1,[color1 isBlendingEnabled],[color1 rgbBlendOperation],[color1 sourceRGBBlendFactor],[color1 destinationRGBBlendFactor],[color1 alphaBlendOperation],[color1 sourceAlphaBlendFactor],[color1 destinationAlphaBlendFactor],(uint32_t)diagnosticMasks[1]}
  };
  int sourceOver=compositorBlends[0].enabled==1&&compositorBlends[0].rgb_op==0&&compositorBlends[0].rgb_src==1&&compositorBlends[0].rgb_dst==5&&compositorBlends[0].alpha_op==0&&compositorBlends[0].alpha_src==1&&compositorBlends[0].alpha_dst==5;
  int disabledBlend=compositorBlends[0].enabled==0&&compositorBlends[0].rgb_op==0&&compositorBlends[0].rgb_src==1&&compositorBlends[0].rgb_dst==0&&compositorBlends[0].alpha_op==0&&compositorBlends[0].alpha_src==1&&compositorBlends[0].alpha_dst==0;
  int clearClass=compositorBlends[0].enabled==0;
  RENDER_REQUIRE(compositorSamples!=8||sourceOver||disabledBlend,"8-tap compositor requires captured source-over or disabled blend descriptor");
  RENDER_REQUIRE((sourceOver||clearClass)&&compositorBlends[0].write_mask==15&&compositorBlends[1].write_mask==0&&
   !compositorBlends[1].enabled&&compositorBlends[1].rgb_src==1&&compositorBlends[1].rgb_dst==0&&compositorBlends[1].alpha_src==1&&compositorBlends[1].alpha_dst==0,
   "compositor blend state outside disabled or exact premultiplied source-over class");
  RENDER_REQUIRE(!sourceOver||(capabilities&64),"kernel fixed-function source-over unavailable");
  RENDER_REQUIRE(metalSlot<=1&&activeLayout&&vertexAttributes[1].buffer_index==metalSlot&&
    vertexAttributes[0].index==0&&vertexAttributes[0].format==31&&vertexAttributes[0].offset==0&&
    vertexAttributes[1].index==1&&vertexAttributes[1].format==29&&vertexAttributes[1].offset==16&&
    (activeLayout->stride==32||activeLayout->stride==40||activeLayout->stride==48)&&
    activeLayout->step_function==1&&activeLayout->step_rate==1,
    "compositor active vertex inputs outside PerVertex slot0/1 stride32/40/48 class");
  RENDER_REQUIRE(compositorSamples!=8||(metalSlot==1&&activeLayout->stride==48),
   "8-tap compositor requires captured Metal vertex slot1 stride48");
  pipeline=[[TeguRenderPipeline alloc] init];if(!pipeline)goto fail;
  pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];
  pipeline->blend=compositorSamples==8||sourceOver?compositorBlends[0]:(TeguAIRRenderBlend){1,0,1,1,0,1,1,0,15};
  const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];NSUInteger vl=strlen(vn),fl=strlen(fn);
  TeguAIRRenderColorAttachment ca[2]={{80,15},{80,0}};
  tegu_compiler_phase(compilerTrace,"air_begin",0,0,0);
  tegu_compiler_memory(compilerTrace,"air_begin");
  (void)vl;(void)fl;
  pipeline->compiled=tegu_pc_render_create(TEGU_PC_AIR_ATTACHMENT_BLENDS,vertex,fragment,0,ca,2,compositorBlends,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount,
   tegu_compositor_profile,sizeof(tegu_compositor_profile),&vs,&fs,&meta,message,sizeof(message));
  tegu_compiler_phase(compilerTrace,"air_end",pipeline->compiled?0:-1,vs.length,fs.length);
  tegu_compiler_memory(compilerTrace,"air_end");
  RENDER_REQUIRE(pipeline->compiled,"compositor AIR compilation failed");
  int metadataStatus=compositorSamples==4?
   tegu_air_render_pipeline_native_check_compositor(pipeline->compiled,message,sizeof(message)):
   tegu_air_render_pipeline_native_check_compositor_v7(pipeline->compiled,message,sizeof(message));
  tegu_compiler_phase(compilerTrace,"metadata_end",metadataStatus,vs.length,fs.length);
  if(metadataStatus){tegu_compile_message(message);goto fail;}
  pipeline->vertexFetch.version=1;pipeline->vertexFetch.mask=3;pipeline->vertexFetch.count=2;
  uint32_t stride=(uint32_t)activeLayout->stride;
  pipeline->vertexFetch.attributes[0]=(struct tegu_vertex_attribute){0,metalSlot,109,0,stride,0,1};
  pipeline->vertexFetch.attributes[1]=(struct tegu_vertex_attribute){1,metalSlot,103,16,stride,0,1};
  /* Exact metadata above proves that the complete push interface consists
   * only of the four per-draw residual offsets and FS texel-format word. Keep
   * their pipeline snapshot zero; the compositor encoder fills them per draw. */
  pipeline->push.version=1;pipeline->push.vertex_span=8;pipeline->push.fragment_span=76;
  TeguAIRRenderResource rr[7];memset(rr,0,sizeof(rr));
  rr[0]=(TeguAIRRenderResource){.kind=1,.stage=1,.index=metalSlot,.length=3u*stride};
  rr[1]=(TeguAIRRenderResource){.kind=1,.stage=1,.index=2,.length=80,.offset=16};
  rr[2]=(TeguAIRRenderResource){.kind=1,.stage=1,.index=3,.length=32,.offset=16};
  rr[3]=(TeguAIRRenderResource){.kind=1,.stage=2,.index=1,.length=96,.offset=24};
  rr[4]=(TeguAIRRenderResource){.kind=1,.stage=2,.index=6,.length=32,.offset=30};
  rr[5]=(TeguAIRRenderResource){.kind=2,.stage=2,.index=3,.format=70,.texture_type=2,.width=64,.height=64,.array_length=1,.mipmap_levels=1,.sample_count=1,.usage=1,.swizzle={2,3,4,5},.address_mode_identity=1,.resource_id=1};
  rr[6]=(TeguAIRRenderResource){.kind=3,.stage=2,.index=0,.normalized_coordinates=1,.max_anisotropy=1};
  TeguAIRRenderDraw draw={1,0,3,1,0};TeguAIRBlob audit={0};TeguAIRRenderTypedPlan typed={0};
  tegu_compiler_phase(compilerTrace,"typed_begin",0,vs.length,fs.length);
  if(tegu_air_render_pipeline_plan_draw_typed(pipeline->compiled,rr,7,&draw,&audit,&typed,message,sizeof(message))){tegu_air_blob_free(&audit);tegu_compile_message(message);goto fail;}
  tegu_air_blob_free(&audit);
  const char *typedError=tegu_compositor_typed_plan_error(&typed,metalSlot,compositorSamples);
  if(typedError){__builtin_memcpy(message,typedError,strlen(typedError)+1);goto fail;}
  tegu_compiler_phase(compilerTrace,"typed_end",0,vs.length,fs.length);
  tegu_compiler_hash(compilerTrace,"mali_input","vertex_spirv",vs.bytes,vs.length);
  tegu_compiler_hash(compilerTrace,"mali_input","fragment_spirv",fs.bytes,fs.length);
  tegu_compiler_phase(compilerTrace,"mali_lock_wait",0,vs.length,fs.length);
  tegu_lock(&tegu_compiler_lock);
  tegu_compiler_phase(compilerTrace,"mali_begin",0,vs.length,fs.length);
  tegu_compiler_memory(compilerTrace,"mali_begin");
  int cfail=compositorSamples==4?
   tegu_pc_mali_pair(TEGU_PC_MALI_RESOURCES,0,vs.bytes,vs.length,fs.bytes,fs.length,&pipeline->vertexCode,&pipeline->fragmentCode,
    &pipeline->pair,&pipeline->compositorResources,sizeof(pipeline->compositorResources),message,sizeof(message)):
   tegu_pc_mali_pair(TEGU_PC_MALI_RESOURCES_V7,0,vs.bytes,vs.length,fs.bytes,fs.length,&pipeline->vertexCode,&pipeline->fragmentCode,
    &pipeline->pair,&pipeline->compositorResources,sizeof(pipeline->compositorResources),message,sizeof(message));
  tegu_unlock(&tegu_compiler_lock);
  tegu_compiler_phase(compilerTrace,"mali_end",cfail,vs.length,fs.length);
  tegu_compiler_memory(compilerTrace,"mali_end");
  if(cfail){tegu_compile_message(message);goto fail;}
  tegu_compiler_hash(compilerTrace,"mali_output","vertex_mali",pipeline->vertexCode.code,pipeline->vertexCode.size);
  tegu_compiler_hash(compilerTrace,"mali_output","fragment_mali",pipeline->fragmentCode.code,pipeline->fragmentCode.size);
  RENDER_REQUIRE(!(compositorSamples==4?tegu_render_v6_make_package(pipeline,&pipeline->compositorResources):
   tegu_render_v7_make_package(pipeline,&pipeline->compositorResources)),"compositor package differs from gated TGR class");
  tegu_compiler_hash(compilerTrace,"package","render_package",pipeline->package,pipeline->packageBytes);
  tegu_compiler_phase(compilerTrace,"package_end",0,vs.length,fs.length);
  unsigned outputs=1;U64 imported=0;
  tegu_compiler_phase(compilerTrace,"import_begin",0,vs.length,fs.length);
  int importStatus=method?method(connection,8,0,0,pipeline->package,pipeline->packageBytes,&imported,&outputs,0,0):-1;
  tegu_compiler_phase(compilerTrace,"import_end",importStatus,vs.length,fs.length);
  /* Record a known owned handle before validating the reply so failure cleanup
   * can release a partially successful import. */
  if(imported>=0xc0000001u&&imported<=0xffffffffu) {
   pipeline->handle=imported;pipeline->allocated=(pipeline->packageBytes+16383)&~(NSUInteger)16383;
   __atomic_fetch_add(&allocatedBytes,pipeline->allocated,__ATOMIC_RELAXED);
  }
  if(importStatus||outputs!=1||!pipeline->handle) {
   snprintf(message,sizeof(message),
    "kernel compositor pipeline import failed status=0x%08x outputs=%u handle=0x%llx bytes=%lu",
    (uint32_t)importStatus,outputs,imported,(unsigned long)pipeline->packageBytes);
   goto fail;
  }
  /* R139: compositor-class meshes (indexed, partial scissor) produced no
   * jobs; give the pipeline the generic draw, which the encoder prefers. */
  if(capabilities&TEGU_GPU_CAP_GENERIC_JOB) {
   char genericMessage[256]={0};
   tegu_render_generic_retranslate(pipeline,vertex,fragment,vertexAttributes,attributeCount,vertexLayouts,layoutCount,
    &compositorBlends[0],genericMessage,sizeof(genericMessage));
  }
  tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
 }
 if((capabilities&TEGU_GPU_CAP_COMPOSITOR)&&fixedUi) {
  tegu_render_refusal_inputs(descriptor,vertex,fragment,diagnosticFormats,diagnosticMasks,diagnosticBlend,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount);diagnosticLogged=1;
  RENDER_REQUIRE(format==80&&diagnosticMasks[0]==15&&diagnosticFormats[1]==0,
   "fixed UI attachments outside exact RT0-only class");
  for(NSUInteger i=2;i<8;i++)RENDER_REQUIRE(!diagnosticFormats[i],"fixed UI extra color attachment unsupported");
  int fixedBlend=!![color isBlendingEnabled];
  /* Other blend equations skip the R80/R85 packages and take the generic
   * TGJ1 draw with the descriptor's own blend state. */
  struct tegu_blend_inputs otherBlend={1,(uint32_t)fixedBlend,(uint32_t)[color rgbBlendOperation],
   (uint32_t)[color sourceRGBBlendFactor],(uint32_t)[color destinationRGBBlendFactor],
   (uint32_t)[color alphaBlendOperation],(uint32_t)[color sourceAlphaBlendFactor],
   (uint32_t)[color destinationAlphaBlendFactor],15};
  int pinnedBlend=(!fixedBlend&&[color rgbBlendOperation]==0&&[color sourceRGBBlendFactor]==1&&[color destinationRGBBlendFactor]==0&&
   [color alphaBlendOperation]==0&&[color sourceAlphaBlendFactor]==1&&[color destinationAlphaBlendFactor]==0)||
   (fixedBlend&&[color rgbBlendOperation]==0&&[color sourceRGBBlendFactor]==1&&[color destinationRGBBlendFactor]==5&&
   [color alphaBlendOperation]==0&&[color sourceAlphaBlendFactor]==1&&[color destinationAlphaBlendFactor]==5);
  RENDER_REQUIRE(pinnedBlend||(capabilities&TEGU_GPU_CAP_GENERIC_JOB),"fixed UI blend descriptor changed");
  RENDER_REQUIRE(attributeCount==4&&layoutCount==1&&
   vertexAttributes[0].index==0&&vertexAttributes[0].format==31&&vertexAttributes[0].offset==0&&vertexAttributes[0].buffer_index==1&&
   vertexAttributes[1].index==1&&vertexAttributes[1].format==29&&vertexAttributes[1].offset==16&&vertexAttributes[1].buffer_index==1&&
   vertexAttributes[2].index==2&&vertexAttributes[2].format==29&&vertexAttributes[2].offset==24&&vertexAttributes[2].buffer_index==1&&
   vertexAttributes[3].index==3&&vertexAttributes[3].format==27&&vertexAttributes[3].offset==32&&vertexAttributes[3].buffer_index==1&&
   vertexLayouts[0].index==1&&vertexLayouts[0].stride==48&&vertexLayouts[0].step_function==1&&vertexLayouts[0].step_rate==1,
   "fixed UI vertex descriptor changed");
  pipeline=[[TeguRenderPipeline alloc] init];if(!pipeline)goto fail;
  pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];pipeline->colorFormat=80;
  const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];NSUInteger vl=strlen(vn),fl=strlen(fn);
  tegu_compiler_phase(compilerTrace,"fixed_ui_air_begin",0,0,0);
  (void)vl;(void)fl;
  pipeline->compiled=tegu_pc_render_create(TEGU_PC_AIR_VERTEX,vertex,fragment,80,0,0,0,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount,
   tegu_native_profile,sizeof(tegu_native_profile),&vs,&fs,&meta,message,sizeof(message));
  tegu_compiler_phase(compilerTrace,"fixed_ui_air_end",pipeline->compiled?0:-1,vs.length,fs.length);
  /* The bridge reports the exact failed translation or policy predicate in
   * message.  Preserve it: replacing it with a generic label made several
   * successfully translated live specializations look like AIR failures. */
  if(!pipeline->compiled)goto fail;
  if(!pinnedBlend) {
   if(tegu_render_generic_prepare(pipeline,&vs,&fs,vertexLayouts,layoutCount,&otherBlend,message,sizeof(message)))goto fail;
   snprintf(pipeline->genericName,sizeof(pipeline->genericName),"%s",fn?fn:"");
   tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
  }
  int fixedMetadataStatus=tegu_air_render_pipeline_native_check_fixed_ui_r80(pipeline->compiled,80,message,sizeof(message));
  if(fixedMetadataStatus) {
   int texturedMetadataStatus=tegu_air_render_pipeline_native_check_fixed_ui_r85_texture(
    pipeline->compiled,80,message,sizeof(message));
   if(!texturedMetadataStatus) {
    /* UI2 encodes and the kernel renders this R85 specialization with the
     * descriptor's premultiplied source-over state.  The common fixed-UI gate
     * above has already pinned every blend factor, so require the enabled
     * branch here rather than rejecting the live BackBoard descriptor. */
    /* The opaque descriptor of this shader (e.g. wallpaper) is encoded only
     * through userspace-built TGJ1 jobs; older kernels keep the refusal. */
    RENDER_REQUIRE(fixedBlend||(capabilities&TEGU_GPU_CAP_GENERIC_JOB),"sampled fixed UI requires source-over blend");
    TeguAIRRenderResource fixedResources[7]={
     {.kind=1,.stage=1,.index=2,.offset=19,.length=512},
     {.kind=1,.stage=1,.index=3,.offset=0,.length=64},
     {.kind=1,.stage=2,.index=1,.offset=277,.length=512},
     {.kind=1,.stage=2,.index=2,.offset=0,.length=64},
     {.kind=1,.stage=1,.index=1,.offset=0,.length=144},
     {.kind=2,.stage=2,.index=3,.format=80,.texture_type=2,.width=194,.height=237,
      .array_length=1,.mipmap_levels=1,.sample_count=1,.usage=1,.swizzle={2,3,4,5},
      .address_mode_identity=1,.resource_id=0x100003},
     {.kind=3,.stage=2,.index=0,.normalized_coordinates=1,.max_anisotropy=1}
    };
    TeguAIRRenderDraw fixedDraw={1,0,3,1,0};TeguAIRFixedUiR85Plan fixedPlan={0};
    if(tegu_air_render_pipeline_plan_fixed_ui_r85_texture(pipeline->compiled,fixedResources,7,
       &fixedDraw,&fixedPlan,message,sizeof(message))){tegu_compile_message(message);goto fail;}
    pipeline->vertexFetch.version=1;pipeline->vertexFetch.mask=7;pipeline->vertexFetch.count=3;
    pipeline->vertexFetch.attributes[0]=(struct tegu_vertex_attribute){0,1,109,0,48,0,1};
    pipeline->vertexFetch.attributes[1]=(struct tegu_vertex_attribute){1,1,103,16,48,0,1};
    pipeline->vertexFetch.attributes[2]=(struct tegu_vertex_attribute){2,1,97,32,48,0,1};
    struct tegu_mali_fixed_ui_texture_r85_plan fixedBackend={0};
    tegu_lock(&tegu_compiler_lock);
    int fixedCompile=tegu_pc_mali_pair(TEGU_PC_MALI_FIXED_UI_R85,0,vs.bytes,vs.length,fs.bytes,fs.length,
     &pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,&fixedBackend,sizeof(fixedBackend),message,sizeof(message));
    tegu_unlock(&tegu_compiler_lock);
    if(fixedCompile){tegu_compile_message(message);goto fail;}
    RENDER_REQUIRE(!tegu_ui2_build(&pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,
     &fixedBackend,&fixedPlan,&pipeline->package,&pipeline->packageBytes),
     "sampled fixed UI native package changed");
    if(!fixedBlend&&pipeline->packageBytes>=UI2_TARGET_OFFSET+12)
     memset(pipeline->package+UI2_TARGET_OFFSET+8,0,4); /* target_blend_enabled */
    RENDER_REQUIRE(!tegu_ui2_parse(pipeline->package,pipeline->packageBytes,&pipeline->fixedUiTextureInfo),
     "sampled fixed UI package parse changed");
    unsigned outputs=1;U64 imported=0;
    /* TGJ1 consumes the userspace package directly. */
    int genericJob=!!(capabilities&TEGU_GPU_CAP_GENERIC_JOB);
    int importStatus=genericJob?0:method?method(connection,8,0,0,pipeline->package,pipeline->packageBytes,
     &imported,&outputs,0,0):-1;
    if(imported>=0xc0000001u&&imported<=0xffffffffu) {
     pipeline->handle=imported;pipeline->allocated=(pipeline->packageBytes+16383)&~(NSUInteger)16383;
     __atomic_fetch_add(&allocatedBytes,pipeline->allocated,__ATOMIC_RELAXED);
    }
    if(!genericJob&&(importStatus||outputs!=1||!pipeline->handle)) {
     snprintf(message,sizeof(message),
      "kernel sampled fixed UI pipeline import failed status=0x%08x outputs=%u handle=0x%llx bytes=%lu",
      (uint32_t)importStatus,outputs,imported,(unsigned long)pipeline->packageBytes);goto fail;
    }
    pipeline->fixedUiTextureSnapshotOnly=1;
    /* Also compile the generic draw: UI2 refuses sources it cannot encode
     * (RGBA8 icons in R128/R131), which then draw generically instead of
     * being dropped.  The UI2 package already holds its own code copy. */
    if(genericJob) {
     const struct tegu_blend_inputs uiBlend={1,fixedBlend?1u:0u,0,1,fixedBlend?5u:0u,0,1,fixedBlend?5u:0u,15};
     char genericMessage[256]={0};
     if(!tegu_render_generic_prepare(pipeline,&vs,&fs,vertexLayouts,layoutCount,&uiBlend,genericMessage,sizeof(genericMessage)))
      snprintf(pipeline->genericName,sizeof(pipeline->genericName),"%s",fn?fn:"");
    }
    tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
   }
   /* Any other fixed-function specialization (extra textures, framebuffer
    * fetch, noise) takes the reflection-driven TGJ1 draw. */
   if(capabilities&TEGU_GPU_CAP_GENERIC_JOB) {
    char genericMessage[512]={0};
    const struct tegu_blend_inputs fixedInputs={1,fixedBlend?1u:0u,0,1,fixedBlend?5u:0u,0,1,fixedBlend?5u:0u,15};
    if(!tegu_render_generic_prepare(pipeline,&vs,&fs,vertexLayouts,layoutCount,&fixedInputs,
       genericMessage,sizeof(genericMessage))) {
     snprintf(pipeline->genericName,sizeof(pipeline->genericName),"%s",fn?fn:"");
   tegu_compiler_phase(compilerTrace,"generic_ready",0,vs.length,fs.length);
     tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
    }
    char line[600];snprintf(line,sizeof(line),"generic: %s",genericMessage);tegu_compile_message(line);
   }
   /* A fixed-function specialization can add texture inputs without changing
    * the public function names.  Keep BackBoard alive long enough to capture
    * the complete translated contract and the encoder bindings in one boot. */
   tegu_compile_message(message);
#ifdef TEGU_RENDER_TRACE
   static unsigned fixedObserverMetadata;
   if(__atomic_fetch_add(&fixedObserverMetadata,1,__ATOMIC_RELAXED)<8) {
    char line[128];int n=snprintf(line,sizeof(line),"TEGU_FIXED_UI_META_BEGIN bytes=%lu blend=%d\n",(unsigned long)meta.length,fixedBlend);
    if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
    if(meta.bytes&&meta.length&&meta.length<=65536)write(2,meta.bytes,meta.length);
    static const char end[]="\nTEGU_FIXED_UI_META_END\n";write(2,end,sizeof(end)-1);
   }
#endif
   pipeline->fixedUiObserverOnly=1;pipeline->colorFormat=80;
   tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
  }
  TeguAIRRenderResource fixedResources[2]={
   {.kind=1,.stage=1,.index=2,.offset=19,.length=512},
   {.kind=1,.stage=2,.index=1,.offset=277,.length=512}
  };
  TeguAIRRenderDraw fixedDraw={1,0,3,1,0};TeguAIRFixedUiR80Plan fixedPlan={0};
  if(tegu_air_render_pipeline_plan_fixed_ui_r80(pipeline->compiled,fixedResources,2,
     &fixedDraw,&fixedPlan,message,sizeof(message))){tegu_compile_message(message);goto fail;}
  pipeline->vertexFetch.version=1;pipeline->vertexFetch.mask=3;pipeline->vertexFetch.count=2;
  pipeline->vertexFetch.attributes[0]=(struct tegu_vertex_attribute){0,1,109,0,48,0,1};
  pipeline->vertexFetch.attributes[1]=(struct tegu_vertex_attribute){1,1,97,32,48,0,1};
  struct tegu_mali_fixed_ui_plan fixedBackend={0};
  tegu_lock(&tegu_compiler_lock);
  int fixedCompile=tegu_pc_mali_pair(TEGU_PC_MALI_FIXED_UI_R80,0,vs.bytes,vs.length,fs.bytes,fs.length,
   &pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,&fixedBackend,sizeof(fixedBackend),message,sizeof(message));
  tegu_unlock(&tegu_compiler_lock);
  if(fixedCompile){tegu_compile_message(message);goto fail;}
  if(tegu_ui1_build(&pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,
     &fixedBackend,&fixedPlan,(uint32_t)fixedBlend,&pipeline->package,&pipeline->packageBytes)) {
#ifdef TEGU_RENDER_TRACE
   /* Name the refusing TFU1 predicate and its fields.  Short records: the USB
    * console corrupts bytes at its 256-byte process framing boundaries. */
   static unsigned ui1Refusals;
   if(__atomic_fetch_add(&ui1Refusals,1,__ATOMIC_RELAXED)<8) {
    char report[2048];int total=tegu_ui1_diagnose(&pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,
     &fixedBackend,&fixedPlan,report,sizeof(report));
    if(total>0&&(size_t)total>=sizeof(report))total=(int)sizeof(report)-1;
    for(int at=0,part=0;total>0&&at<total;at+=160,part++) {
     char line[224];int len=total-at<160?total-at:160;
     int n=snprintf(line,sizeof(line),"TEGU_FIXED_UI_UI1_REFUSAL blend=%d part=%d %.*s\n",fixedBlend,part,len,report+at);
     if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
    }
   }
#endif
   RENDER_REQUIRE(0,"fixed UI native package changed");
  }
  unsigned outputs=1;U64 imported=0;
  int genericJob=!!(capabilities&TEGU_GPU_CAP_GENERIC_JOB);
  int importStatus=genericJob?0:method?method(connection,8,0,0,pipeline->package,pipeline->packageBytes,
   &imported,&outputs,0,0):-1;
  if(imported>=0xc0000001u&&imported<=0xffffffffu) {
   pipeline->handle=imported;pipeline->allocated=(pipeline->packageBytes+16383)&~(NSUInteger)16383;
   __atomic_fetch_add(&allocatedBytes,pipeline->allocated,__ATOMIC_RELAXED);
  }
  if(!genericJob&&(importStatus||outputs!=1||!pipeline->handle)) {
   snprintf(message,sizeof(message),"kernel fixed UI pipeline import failed status=0x%08x outputs=%u handle=0x%llx bytes=%lu",
    (uint32_t)importStatus,outputs,imported,(unsigned long)pipeline->packageBytes);goto fail;
  }
  pipeline->fixedUiSnapshotOnly=1;
  tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
 }
 if((capabilities&TEGU_GPU_CAP_COMPOSITOR)&&homeAffordance) {
  tegu_render_refusal_inputs(descriptor,vertex,fragment,diagnosticFormats,diagnosticMasks,diagnosticBlend,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount);diagnosticLogged=1;
  RENDER_REQUIRE(format==80&&diagnosticMasks[0]==15&&diagnosticFormats[1]==80&&diagnosticMasks[1]==0,
   "home-affordance attachments outside exact RT0-mask15 RT1-mask0 class");
  for(NSUInteger i=2;i<8;i++)RENDER_REQUIRE(!diagnosticFormats[i],"home-affordance extra color attachment unsupported");
  TeguRenderColorDescriptor *color1=[attachments objectAtIndexedSubscript:1];
  RENDER_REQUIRE(![color isBlendingEnabled]&&![color1 isBlendingEnabled]&&
   [color rgbBlendOperation]==0&&[color sourceRGBBlendFactor]==1&&[color destinationRGBBlendFactor]==0&&
   [color alphaBlendOperation]==0&&[color sourceAlphaBlendFactor]==1&&[color destinationAlphaBlendFactor]==0,
   "home-affordance blend descriptor changed");
  RENDER_REQUIRE(attributeCount==4&&layoutCount==1&&
   vertexAttributes[0].index==0&&vertexAttributes[0].format==31&&vertexAttributes[0].offset==0&&vertexAttributes[0].buffer_index==1&&
   vertexAttributes[1].index==1&&vertexAttributes[1].format==29&&vertexAttributes[1].offset==16&&vertexAttributes[1].buffer_index==1&&
   vertexAttributes[2].index==2&&vertexAttributes[2].format==29&&vertexAttributes[2].offset==24&&vertexAttributes[2].buffer_index==1&&
   vertexAttributes[3].index==3&&vertexAttributes[3].format==27&&vertexAttributes[3].offset==32&&vertexAttributes[3].buffer_index==1&&
   vertexLayouts[0].index==1&&vertexLayouts[0].stride==48&&vertexLayouts[0].step_function==1&&vertexLayouts[0].step_rate==1,
   "home-affordance vertex descriptor changed");
  pipeline=[[TeguRenderPipeline alloc] init];if(!pipeline)goto fail;
  pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];pipeline->colorFormat=80;
  const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];NSUInteger vl=strlen(vn),fl=strlen(fn);
  TeguAIRRenderColorAttachment ca[2]={{80,15},{80,0}};
  (void)vl;(void)fl;
  pipeline->compiled=tegu_pc_render_create(TEGU_PC_AIR_ATTACHMENTS,vertex,fragment,0,ca,2,0,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount,
   tegu_compositor_profile,sizeof(tegu_compositor_profile),&vs,&fs,&meta,message,sizeof(message));
  RENDER_REQUIRE(pipeline->compiled,"home-affordance AIR compilation failed");
  RENDER_REQUIRE(!tegu_air_render_pipeline_native_check_home_affordance(pipeline->compiled,message,sizeof(message)),
   "home-affordance translator metadata changed");
  pipeline->homeAffordanceObserverOnly=1;
  /* R146: observers queue no GPU work, so the targets they feed stayed
   * empty and every platter composite came out black. On TGJ1 devices the
   * generic draw (preferred by the encoder) renders RT0. */
  if(capabilities&TEGU_GPU_CAP_GENERIC_JOB) {
   TeguAIRRenderBlend hb={1,[color isBlendingEnabled],[color rgbBlendOperation],[color sourceRGBBlendFactor],
    [color destinationRGBBlendFactor],[color alphaBlendOperation],[color sourceAlphaBlendFactor],
    [color destinationAlphaBlendFactor],15};
   char genericMessage[256]={0};
   tegu_render_generic_retranslate(pipeline,vertex,fragment,vertexAttributes,attributeCount,vertexLayouts,layoutCount,
    &hb,genericMessage,sizeof(genericMessage));
  }
  tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
 }
 if((capabilities&TEGU_GPU_CAP_COMPOSITOR)&&catalogNamed) {
  tegu_render_refusal_inputs(descriptor,vertex,fragment,diagnosticFormats,diagnosticMasks,diagnosticBlend,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount);diagnosticLogged=1;
  RENDER_REQUIRE(tegu_qc_streq(vertex->library->sourceSHA256,tegu_qc_catalog_donor_sha256)&&
                 tegu_qc_streq(fragment->library->sourceSHA256,tegu_qc_catalog_donor_sha256),
   "QuartzCore catalog donor identity changed");
  RENDER_REQUIRE(tegu_render_catalog_profile_exact(tegu_compositor_profile,sizeof(tegu_compositor_profile)),
   "QuartzCore catalog profile identity changed");
  struct tegu_qc_catalog_descriptor catalogDescriptor;
  RENDER_REQUIRE(!tegu_render_catalog_descriptor(descriptor,attachments,vertexAttributes,attributeCount,
   vertexLayouts,layoutCount,&catalogDescriptor),"QuartzCore catalog descriptor outside bounded fields");
  const struct tegu_qc_catalog_entry *catalogEntry=tegu_qc_catalog_find(catalogVertex,catalogFragment,&catalogDescriptor);
  RENDER_REQUIRE(catalogEntry,"QuartzCore catalog descriptor changed");
  const struct tegu_qc_catalog_descriptor *catalogExpected=tegu_qc_catalog_entry_descriptor(catalogEntry);
  RENDER_REQUIRE(catalogExpected&&catalogExpected->attachments[0].format&&
   catalogExpected->attachments[0].format==catalogExpected->attachments[1].format,
   "QuartzCore catalog attachment class invalid");
  pipeline=[[TeguRenderPipeline alloc] init];if(!pipeline)goto fail;
  pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];
  pipeline->colorFormat=catalogExpected->attachments[0].format;
  const struct tegu_qc_catalog_blend *catalogBlend=&catalogExpected->attachments[0].blend;
  pipeline->blend=(TeguAIRRenderBlend){1,catalogBlend->enabled,catalogBlend->rgb_op,catalogBlend->rgb_src,
   catalogBlend->rgb_dst,catalogBlend->alpha_op,catalogBlend->alpha_src,catalogBlend->alpha_dst,
   catalogExpected->attachments[0].write_mask};
  const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];NSUInteger vl=strlen(vn),fl=strlen(fn);
  TeguAIRRenderColorAttachment ca[2]={{catalogExpected->attachments[0].format,catalogExpected->attachments[0].write_mask},
   {catalogExpected->attachments[1].format,catalogExpected->attachments[1].write_mask}};
  (void)vl;(void)fl;
  pipeline->compiled=tegu_pc_render_create(TEGU_PC_AIR_ATTACHMENTS,vertex,fragment,0,ca,2,0,
   vertexAttributes,attributeCount,vertexLayouts,layoutCount,
   tegu_compositor_profile,sizeof(tegu_compositor_profile),&vs,&fs,&meta,message,sizeof(message));
  RENDER_REQUIRE(pipeline->compiled,"narrow-blur AIR compilation failed");
  RENDER_REQUIRE(!tegu_air_render_pipeline_catalog_metadata_check(pipeline->compiled,catalogEntry->metadata_sha256,64,
   message,sizeof(message)),"QuartzCore catalog translator metadata changed");
  if(singlePassBlur3) {
   RENDER_REQUIRE(catalogExpected->attachments[0].format==80&&
    tegu_qc_streq(catalogEntry->descriptor_class,"blur_mrt_bgra_lph_v1"),
    "single-pass-blur-3 catalog class changed");
   struct tegu_mali_single_pass_blur3_plan b3={0};
   tegu_lock(&tegu_compiler_lock);
   int compileStatus=tegu_pc_mali_pair(TEGU_PC_MALI_BLUR3,0,vs.bytes,vs.length,fs.bytes,fs.length,
    &pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,&b3,sizeof(b3),message,sizeof(message));
   tegu_unlock(&tegu_compiler_lock);
   tegu_b3_lifecycle_marker("compile",compileStatus,0,0,vs.length,fs.length,0);
   if(compileStatus){tegu_compile_message(message);goto fail;}
   pipeline->vertexFetch.version=1;pipeline->vertexFetch.mask=3;pipeline->vertexFetch.count=2;
   pipeline->vertexFetch.attributes[0]=(struct tegu_vertex_attribute){0,1,109,0,48,0,1};
   pipeline->vertexFetch.attributes[1]=(struct tegu_vertex_attribute){1,1,103,16,48,0,1};
   pipeline->push.version=1;pipeline->push.vertex_span=8;pipeline->push.fragment_span=76;
   int packageStatus=tegu_render_b3_make_package(pipeline,&b3);
   tegu_b3_lifecycle_marker("package",packageStatus,0,0,vs.length,fs.length,pipeline->packageBytes);
   RENDER_REQUIRE(!packageStatus,"single-pass-blur-3 package differs from exact B3 class");
   unsigned outputs=1;U64 imported=0;
   int importStatus=method?method(connection,8,0,0,pipeline->package,pipeline->packageBytes,&imported,&outputs,0,0):-1;
   tegu_b3_lifecycle_marker("selector8",importStatus,outputs,imported,vs.length,fs.length,pipeline->packageBytes);
   if(imported>=0xc0000001u&&imported<=0xffffffffu) {
    pipeline->handle=imported;pipeline->allocated=(pipeline->packageBytes+16383)&~(NSUInteger)16383;
    __atomic_fetch_add(&allocatedBytes,pipeline->allocated,__ATOMIC_RELAXED);
   }
   if(importStatus||outputs!=1||!pipeline->handle) {
    snprintf(message,sizeof(message),
     "kernel B3 pipeline import failed status=0x%08x outputs=%u handle=0x%llx bytes=%lu",
     (uint32_t)importStatus,outputs,imported,(unsigned long)pipeline->packageBytes);goto fail;
   }
   tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
  }
  /* Observer class 2 snapshots the exact resources and queues no GPU work. */
  pipeline->homeAffordanceObserverOnly=2;
  if((capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&pipeline->colorFormat==80) {
   char genericMessage[256]={0};
   tegu_render_generic_retranslate(pipeline,vertex,fragment,vertexAttributes,attributeCount,vertexLayouts,layoutCount,
    &pipeline->blend,genericMessage,sizeof(genericMessage));
  }
  tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
 }
 RENDER_REQUIRE(format==10||format==70||format==80,"render color format unsupported");
 RENDER_REQUIRE(format!=80||(capabilities&256),"kernel BGRA render unavailable");
 if([color isBlendingEnabled]) {
  NSUInteger values[6]={[color rgbBlendOperation],[color sourceRGBBlendFactor],[color destinationRGBBlendFactor],
   [color alphaBlendOperation],[color sourceAlphaBlendFactor],[color destinationAlphaBlendFactor]};
  for(unsigned i=0;i<6;i++)RENDER_REQUIRE(values[i]<=0xffffffffUL,"render blend enum outside bounded range");
  blend.enabled=1;blend.rgb_op=(uint32_t)values[0];blend.rgb_src=(uint32_t)values[1];blend.rgb_dst=(uint32_t)values[2];
  blend.alpha_op=(uint32_t)values[3];blend.alpha_src=(uint32_t)values[4];blend.alpha_dst=(uint32_t)values[5];
  struct tegu_blend_state checkedBlend;
  RENDER_REQUIRE(!tegu_blend_build_format((uint32_t)format,&blend,&checkedBlend),"render blend equation requires unsupported shader or constants");
  RENDER_REQUIRE(capabilities&64,"kernel render blending unavailable");
 }
 RENDER_REQUIRE([color writeMask]==15,"render color write mask unsupported");
 for(NSUInteger i=1;i<8;i++)RENDER_REQUIRE(![[attachments objectAtIndexedSubscript:i] pixelFormat],"render multiple color attachments unsupported");
 #undef RENDER_REQUIRE
 compilerTrace=tegu_compiler_trace_begin();
 tegu_compiler_phase(compilerTrace,"generic_begin",0,0,0);
 tegu_compiler_memory(compilerTrace,"generic_begin");
 pipeline=[[TeguRenderPipeline alloc] init];if(!pipeline)goto fail;
 pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];pipeline->colorFormat=format;pipeline->blend=blend;
 const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];
 NSUInteger vl=0,fl=0;if(vn)while(vl<4096&&vn[vl])vl++;if(fn)while(fl<4096&&fn[fl])fl++;
 (void)vl;(void)fl;
 pipeline->compiled=tegu_pc_render_create(blend.enabled?TEGU_PC_AIR_BLEND:TEGU_PC_AIR_VERTEX,vertex,fragment,(uint32_t)format,0,0,
  blend.enabled?&pipeline->blend:0,vertexAttributes,attributeCount,vertexLayouts,layoutCount,
  tegu_native_profile,sizeof(tegu_native_profile),&vs,&fs,&meta,message,sizeof(message));
 tegu_compiler_phase(compilerTrace,"generic_air_end",pipeline->compiled?0:-1,vs.length,fs.length);
 tegu_compiler_memory(compilerTrace,"generic_air_end");
 tegu_compiler_hash(compilerTrace,"generic_air","vertex_spirv",vs.bytes,vs.length);
 tegu_compiler_hash(compilerTrace,"generic_air","fragment_spirv",fs.bytes,fs.length);
 if(!pipeline->compiled||tegu_air_render_pipeline_vertex_fetch(pipeline->compiled,&pipeline->vertexFetch,message,sizeof(message)))goto fail;
 if(pipeline->vertexFetch.count&&!(capabilities&16)) {
  __builtin_memcpy(message,"kernel vertex fetch unavailable",sizeof("kernel vertex fetch unavailable"));goto fail;
 }
 int nativeCheck=blend.enabled?
  tegu_air_render_pipeline_native_check_with_blend(pipeline->compiled,(uint32_t)format,&pipeline->blend,message,sizeof(message)):
  pipeline->vertexFetch.count?
  tegu_air_render_pipeline_native_check_with_vertex_fetch(pipeline->compiled,(uint32_t)format,message,sizeof(message)):
  tegu_air_render_pipeline_native_check(pipeline->compiled,(uint32_t)format,message,sizeof(message));
 tegu_compiler_phase(compilerTrace,"generic_native_check",nativeCheck,vs.length,fs.length);
 /* Pipelines the fixed native packages refuse (luma boost, framebuffer
  * fetch, extra textures) take the reflection-driven TGJ1 draw.  The draw
  * path expands indexed draws from the 48-byte slot-1 layout only. */
 if(nativeCheck&&!sphereClass&&format==80&&(capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&
    layoutCount==1&&vertexLayouts[0].index==1&&vertexLayouts[0].stride==48&&vertexLayouts[0].step_function==1) {
  char genericMessage[512]={0};
  struct tegu_blend_inputs genericBlend={1,0,0,1,0,0,1,0,15};
  if(blend.enabled){genericBlend=blend;genericBlend.version=1;genericBlend.write_mask=15;}
  if(!tegu_render_generic_prepare(pipeline,&vs,&fs,vertexLayouts,layoutCount,&genericBlend,
     genericMessage,sizeof(genericMessage))) {
   snprintf(pipeline->genericName,sizeof(pipeline->genericName),"%s",fn?fn:"");
   tegu_compiler_phase(compilerTrace,"generic_ready",0,vs.length,fs.length);
   tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
  }
  char line[600];snprintf(line,sizeof(line),"generic: %s",genericMessage);tegu_compile_message(line);
 }
 /* Capture the translator-owned contract for the exact SpringBoard sphere
  * pipeline before the current native gate refuses its resource-bearing
  * vertex stage.  This is observation only: execution remains on the same
  * refusal path until every reported binding is implemented. */
 if(sphereClass) {
  TeguAIRBlob contract={0};char contractError[256]={0};
  int contractStatus=tegu_air_render_pipeline_resource_contract(pipeline->compiled,&contract,
   contractError,sizeof(contractError));
  tegu_compiler_phase(compilerTrace,"sphere_contract",contractStatus,vs.length,fs.length);
  if(!contractStatus) {
   tegu_compiler_hash(compilerTrace,"sphere_contract","resource_json",contract.bytes,contract.length);
   tegu_compiler_blob(compilerTrace,"sphere_resource_json",contract.bytes,contract.length);
  }
  tegu_air_blob_free(&contract);
 }
#ifdef TEGU_COMPILER_TRACE
 /* Diagnostic-only late-binding observer for the exact R69 sphere identity.
  * It never owns native code or a kernel handle and therefore cannot submit. */
 if(nativeCheck&&sphereClass&&format==10&&!blend.enabled&&diagnosticMasks[0]==15&&
    !attributeCount&&!layoutCount) {
  int extra=0;for(unsigned i=1;i<8;i++)extra|=diagnosticFormats[i]!=0;
  char vsha[65],fsha[65];
  if(!extra&&vs.length==5368&&fs.length==1088&&
     !tegu_compiler_trace_sha256(vs.bytes,vs.length,vsha)&&
     !tegu_compiler_trace_sha256(fs.bytes,fs.length,fsha)&&
     tegu_qc_streq(vsha,"a7e613c4556270b84592c7ac2295b064924d94ea94a07001cde54e3d1fffe737")&&
     tegu_qc_streq(fsha,"d5f0b60b95937df864e56e9ce7b7f95af433848557439ce6036219c488abb0e6")) {
   pipeline->sphereObserverOnly=1;
   tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
  }
 }
#endif
 int texturePipeline=0;
 /* Keep every pipeline accepted by the existing gates on its original path.
  * TGR5 is an additive fallback for the qualified RGBA8 texture contract. */
 if(nativeCheck&&!blend.enabled&&(format==70||format==80)&&pipeline->vertexFetch.count&&(capabilities&128)) {
  char legacyMessage[sizeof(message)];__builtin_memcpy(legacyMessage,message,sizeof(message));
  TeguAIRTexturePlan airTexture={0};char textureMessage[sizeof(message)]={0};
  if(!tegu_air_render_pipeline_native_check_with_texture(pipeline->compiled,(uint32_t)format,
      &airTexture,textureMessage,sizeof(textureMessage))) {
   if(airTexture.version!=1||airTexture.stage!=16||airTexture.texture_index||airTexture.sampler_index||
      airTexture.descriptor_set||airTexture.binding||airTexture.normalized_coordinates!=1) {
    __builtin_memcpy(message,"native texture metadata plan changed",sizeof("native texture metadata plan changed"));goto fail;
   }
   pipeline->texture=(struct tegu_render_texture_plan){airTexture.version,airTexture.stage,
    airTexture.texture_index,airTexture.sampler_index,airTexture.descriptor_set,airTexture.binding,
    airTexture.normalized_coordinates};
   texturePipeline=1;message[0]=0;
  } else __builtin_memcpy(message,legacyMessage,sizeof(message));
 }
 if((nativeCheck&&!texturePipeline)||
    tegu_air_render_pipeline_push(pipeline->compiled,&pipeline->push,message,sizeof(message)))goto fail;
 tegu_lock(&tegu_compiler_lock);
 uint32_t vertexMask=0;
 int failed=0;
 if(texturePipeline) {
  failed=tegu_pc_mali_pair(TEGU_PC_MALI_TEXTURE,0,vs.bytes,vs.length,fs.bytes,fs.length,
   &pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,&pipeline->maliTexture,sizeof(pipeline->maliTexture),message,sizeof(message));
  if(!failed)vertexMask=pipeline->pair.vs_attribute_mask;
 } else {
  failed=pipeline->vertexFetch.count?
   tegu_mali_compile_graphics_with_vertex_inputs(vs.bytes,vs.length,TEGU_GRAPHICS_VERTEX,&pipeline->vertexCode,&vertexMask,message,sizeof(message)):
   tegu_mali_compile_graphics(vs.bytes,vs.length,TEGU_GRAPHICS_VERTEX,&pipeline->vertexCode,message,sizeof(message));
  if(!failed)failed=tegu_mali_compile_graphics(fs.bytes,fs.length,TEGU_GRAPHICS_FRAGMENT,&pipeline->fragmentCode,message,sizeof(message));
 }
 /* Preserve the single-stage path and its ABI for existing pipelines. A
  * varying pipeline must compile both stages against the same linked layout;
  * never retry one stage while retaining the other's unrelated native code. */
 if(failed&&pipeline->vertexFetch.count&&!texturePipeline) {
  tegu_mali_graphics_free(&pipeline->vertexCode);tegu_mali_graphics_free(&pipeline->fragmentCode);
  failed=tegu_mali_compile_graphics_pair2(vs.bytes,vs.length,fs.bytes,fs.length,
   &pipeline->vertexCode,&pipeline->fragmentCode,&pipeline->pair,message,sizeof(message));
  if(!failed)vertexMask=pipeline->pair.vs_attribute_mask;
 }
 tegu_unlock(&tegu_compiler_lock);
 if(!failed&&pipeline->pair.version) {
  const struct tegu_mali_graphics_pair2 *p=&pipeline->pair;
  /* This native draw path qualifies smooth float4 at generic location0. Keep
   * the compiler's broader scalar/vector surface separate from native support. */
  if(p->version!=2||(p->flags&~TEGU_MALI_PAIR2_WRITES_POINT_SIZE)||p->needs_extended_fifo||
     p->generic_size_B!=16||p->vs_varying_mask!=1||p->fs_varying_mask!=1||p->fs_uses_ld_var||
     p->noperspective_mask||p->varying_count!=1||p->varyings[0].location||p->varyings[0].offset||
     p->varyings[0].components!=4||p->varyings[0].alu_type!=160||
     p->position_offset>=p->secondary_offset||p->secondary_offset>=pipeline->vertexCode.size||
     p->position_size!=p->secondary_offset-p->position_offset||
     p->secondary_size!=pipeline->vertexCode.size-p->secondary_offset||
     (p->position_offset&127)||(p->secondary_offset&127)||!p->secondary_registers||p->secondary_registers>32||
     (p->secondary_preload&~0x7f80000000000000ULL)||
     pipeline->vertexCode.sysval_mask!=3||pipeline->fragmentCode.sysval_mask!=8) {
   __builtin_memcpy(message,"native varying layout unsupported",sizeof("native varying layout unsupported"));goto fail;
  }
  if(texturePipeline) {
   const struct tegu_vertex_attribute *a=pipeline->vertexFetch.attributes;
   const struct tegu_mali_texture_plan *m=&pipeline->maliTexture;
   if(pipeline->vertexFetch.mask!=3||pipeline->vertexFetch.count!=2||
      a[0].location!=0||a[0].buffer_index!=1||a[0].format!=109||a[0].offset||a[0].stride!=32||a[0].input_rate||a[0].divisor!=1||
      a[1].location!=1||a[1].buffer_index!=1||a[1].format!=109||a[1].offset!=16||a[1].stride!=32||a[1].input_rate||a[1].divisor!=1||
      p->vs_attribute_mask!=3||m->version!=TEGU_MALI_TEXTURE_PLAN_VERSION||m->table||
      m->texture_index!=1||m->sampler_index||m->normalized_coordinates!=1||m->descriptor_set||m->binding) {
    __builtin_memcpy(message,"native texture attributes or hardware plan unsupported",sizeof("native texture attributes or hardware plan unsupported"));goto fail;
   }
  }
  if(!(capabilities&32)) {
   __builtin_memcpy(message,"kernel varying draw unavailable",sizeof("kernel varying draw unavailable"));goto fail;
  }
 }
 if(failed||vertexMask!=pipeline->vertexFetch.mask||pipeline->vertexCode.push_bytes>pipeline->push.vertex_span||
    pipeline->fragmentCode.push_bytes>pipeline->push.fragment_span)goto fail;
 /* Serialize once into an immutable owned package for kernel import. */
 NSUInteger codeOffset=texturePipeline?TEGU_RENDER_TEXTURE_CODE_OFFSET:blend.enabled?TEGU_RENDER_BLEND_CODE_OFFSET:pipeline->vertexFetch.count?TEGU_RENDER_VERTEX_CODE_OFFSET:TEGU_RENDER_CODE_OFFSET;
 NSUInteger pushOffset=texturePipeline?TEGU_RENDER_TEXTURE_HEADER:blend.enabled?TEGU_RENDER_BLEND_HEADER:128;
 NSUInteger attributeOffset=texturePipeline?TEGU_RENDER_TEXTURE_ATTRIBUTE_OFFSET:blend.enabled?TEGU_RENDER_BLEND_ATTRIBUTE_OFFSET:384;
 NSUInteger fsOffset=codeOffset+((pipeline->vertexCode.size+63)&~(NSUInteger)63);
 pipeline->packageBytes=fsOffset+pipeline->fragmentCode.size;
 if(pipeline->packageBytes>TEGU_RENDER_MAX_PACKAGE)goto fail;
 pipeline->package=malloc(pipeline->packageBytes);if(!pipeline->package)goto fail;
 __builtin_memset(pipeline->package,0,pipeline->packageBytes);
 uint32_t header[32]={TEGU_RENDER_MAGIC,texturePipeline?5u:blend.enabled?4u:pipeline->pair.version?3u:pipeline->vertexFetch.count?2u:1u,0xb8a21030u,(uint32_t)format,
  (uint32_t)pipeline->vertexCode.size,(uint32_t)pipeline->fragmentCode.size,
  pipeline->push.vertex_span,pipeline->push.fragment_span};
 for(unsigned i=0;i<2;i++) {
  struct tegu_mali_graphics *code=i?&pipeline->fragmentCode:&pipeline->vertexCode;
  uint32_t *h=header+8+8*i;
  h[0]=code->registers;h[1]=code->fau_words;h[2]=code->sysval_mask;h[3]=code->tls;
  h[4]=(uint32_t)code->preload;h[5]=(uint32_t)(code->preload>>32);h[6]=code->push_bytes;
 }
 if(pipeline->vertexFetch.count) {
  header[24]=pipeline->vertexFetch.mask;header[25]=pipeline->vertexFetch.count;
  for(unsigned i=0;i<pipeline->vertexFetch.count;i++) {
   const struct tegu_vertex_attribute *a=&pipeline->vertexFetch.attributes[i];
   uint32_t fields[7]={a->location,a->buffer_index,a->format,a->offset,a->stride,a->input_rate,a->divisor};
   for(unsigned j=0;j<7;j++)tegu_render_put(pipeline->package+attributeOffset+i*28+j*4,fields[j]);
  }
 }
 if(pipeline->pair.version) {
  header[26]=pipeline->pair.position_offset;header[27]=pipeline->pair.secondary_offset;
  header[28]=pipeline->pair.secondary_registers;header[29]=(uint32_t)pipeline->pair.secondary_preload;
  header[30]=(uint32_t)(pipeline->pair.secondary_preload>>32);header[31]=pipeline->pair.generic_size_B;
 }
 for(unsigned i=0;i<32;i++)tegu_render_put(pipeline->package+4*i,header[i]);
 if(blend.enabled||texturePipeline) {
  const uint32_t fields[9]={blend.version,blend.enabled,blend.rgb_op,blend.rgb_src,blend.rgb_dst,
   blend.alpha_op,blend.alpha_src,blend.alpha_dst,blend.write_mask};
  for(unsigned i=0;i<9;i++)tegu_render_put(pipeline->package+128+i*4,fields[i]);
 }
 if(texturePipeline) {
  const uint32_t fields[7]={pipeline->texture.version,pipeline->texture.stage,pipeline->texture.texture_index,
   pipeline->texture.sampler_index,pipeline->texture.descriptor_set,pipeline->texture.binding,
   pipeline->texture.normalized_coordinates};
  for(unsigned i=0;i<7;i++)tegu_render_put(pipeline->package+192+i*4,fields[i]);
 }
 memcpy(pipeline->package+pushOffset,pipeline->push.vertex,128);
 memcpy(pipeline->package+pushOffset+128,pipeline->push.fragment,128);
 memcpy(pipeline->package+codeOffset,pipeline->vertexCode.code,pipeline->vertexCode.size);
 memcpy(pipeline->package+fsOffset,pipeline->fragmentCode.code,pipeline->fragmentCode.size);
 struct tegu_render_info checked;
 if(tegu_render_parse(pipeline->package,pipeline->packageBytes,&checked))goto fail;
 unsigned outputs=1;U64 imported=0;
 if(!method||!scalar||method(connection,8,0,0,pipeline->package,pipeline->packageBytes,
    &imported,&outputs,0,0)) {
  __builtin_memcpy(message,"kernel render pipeline import failed",sizeof("kernel render pipeline import failed"));
  goto fail;
 }
 pipeline->handle=imported;
 pipeline->allocated=(pipeline->packageBytes+16383)&~(NSUInteger)16383;
 __atomic_fetch_add(&allocatedBytes,pipeline->allocated,__ATOMIC_RELAXED);
 tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);
 return pipeline;
fail:
 tegu_compiler_phase(compilerTrace,"pipeline_fail",-1,vs.length,fs.length);
 if(diagnosticOwned&&!diagnosticLogged)tegu_render_refusal_inputs(descriptor,vertex,fragment,diagnosticFormats,diagnosticMasks,diagnosticBlend,
  vertexAttributes,attributeCount,vertexLayouts,layoutCount);
 tegu_compile_message(message[0]?message:"unsupported render pipeline descriptor or shader metadata");
/* QuartzCore aborts its process when a pipeline cannot be built (R124:
 * backboardd "Metal failed to build render pipeline", a restart loop behind
 * the cycling boot logo).  Every plausible descriptor therefore gets a
 * pipeline: the reflection-driven TGJ1 draw when it translates and compiles,
 * else a no-op observer whose draws are skipped. */
 /* Any colour format: an unknown one (R133: 551 overdrive LUT) becomes an
  * observer whose draws are dropped, never a nil pipeline. */
 if(diagnosticOwned&&(capabilities&TEGU_GPU_CAP_COMPOSITOR)&&(capabilities&TEGU_GPU_CAP_GENERIC_JOB)) {
  [pipeline release];pipeline=[[TeguRenderPipeline alloc] init];
  if(pipeline) {
   pipeline->owner=[self retain];pipeline->debugLabel=[[descriptor label] copy];
   pipeline->rasterSampleCount=(unsigned)rasterSamples;
   tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);
   const char *ovn=[vertex->functionName UTF8String],*ofn=[fragment->functionName UTF8String];
   NSUInteger ovl=ovn?strlen(ovn):0,ofl=ofn?strlen(ofn):0;(void)ovl;(void)ofl;
   /* Admit up to four contiguous colour attachments.  Formats may differ
    * (QuartzCore's path pass is BGRA8,BGRA8,R8Unorm); an absent slot may carry
    * format0 and mask0.  The native compiler gets a neutral 80 only for an
    * unbound slot; execution never emits a job for it. */
   /* A colour slot is bound iff its pixelFormat is valid. Metal's default
    * writeMask is All (15) on every slot, so QuartzCore's two-target
    * descriptors carry format0/mask15 on slots 2..7; Metal ignores the mask
    * of an unbound slot and so does this admission (c6ec: blur pipelines
    * became inert on exactly this). */
   int extra=!nord2DescriptorValid;
   unsigned rtCount=1;
   for(unsigned rt=0;rt<8;rt++)if(diagnosticFormats[rt]){if(rt>=4)extra=1;else rtCount=rt+1;}
   unsigned activeMask=0,mainRt=0;int mainSet=0;
   memset(pipeline->genericRtFormats32,0,sizeof(pipeline->genericRtFormats32));
   struct tegu_blend_inputs gb[4];TeguAIRRenderColorAttachment mca[4];TeguAIRRenderBlend mblend[4];
   memset(gb,0,sizeof(gb));memset(mca,0,sizeof(mca));memset(mblend,0,sizeof(mblend));
   for(unsigned rt=0;rt<rtCount;rt++) {
    TeguRenderColorDescriptor *c=[[descriptor colorAttachments] objectAtIndexedSubscript:rt];
    NSUInteger fmt=(rt<8)?diagnosticFormats[rt]:0,mask=(rt<8&&fmt)?diagnosticMasks[rt]:0;
    /* Apple XR colour formats (552-555) have no Mali target format; this
     * frontend's XR textures are RGBA16F internally (IOSurface shadows encode
     * to XR codes on write-back), so the pipeline renders RGBA16F. */
    if(fmt>=550&&fmt<=555)fmt=115; /* + RGB10A8_2P_XR10(_sRGB), c6o */
    if(mask>15)extra=1;
    if(fmt&&!n2r_bpp((unsigned)fmt))extra=1;
    gb[rt]=(struct tegu_blend_inputs){1,0,0,1,0,0,1,0,(uint32_t)mask};
    if(c&&fmt&&[c isBlendingEnabled])gb[rt]=(struct tegu_blend_inputs){1,1,(uint32_t)[c rgbBlendOperation],
     (uint32_t)[c sourceRGBBlendFactor],(uint32_t)[c destinationRGBBlendFactor],(uint32_t)[c alphaBlendOperation],
     (uint32_t)[c sourceAlphaBlendFactor],(uint32_t)[c destinationAlphaBlendFactor],(uint32_t)mask};
    gb[rt].write_mask=(uint32_t)mask;
    mca[rt]=(TeguAIRRenderColorAttachment){(uint32_t)(fmt?fmt:80),(uint32_t)mask};
    mblend[rt]=(TeguAIRRenderBlend){(uint32_t)gb[rt].version,gb[rt].enabled,gb[rt].rgb_op,gb[rt].rgb_src,
     gb[rt].rgb_dst,gb[rt].alpha_op,gb[rt].alpha_src,gb[rt].alpha_dst,(uint32_t)mask};
    pipeline->genericRtFormats[rt]=fmt;pipeline->genericRtMasks[rt]=(uint32_t)mask;
    pipeline->genericRtFormats32[rt]=(uint32_t)fmt;
    pipeline->genericRtBlends[rt]=(TeguAIRRenderBlend){(uint32_t)gb[rt].version,gb[rt].enabled,gb[rt].rgb_op,gb[rt].rgb_src,
     gb[rt].rgb_dst,gb[rt].alpha_op,gb[rt].alpha_src,gb[rt].alpha_dst,(uint32_t)mask};
    if(mask){activeMask|=1u<<rt;if(!mainSet){mainRt=rt;mainSet=1;}}
   }
   if(!activeMask)extra=1;
   pipeline->genericPathClass=tegu_render_path_fragment(ofn);
   pipeline->genericRtDescriptorActive=activeMask; pipeline->genericRtPreserve=0;
   unsigned activeBits=0;for(unsigned rt=0;rt<4;rt++)if(activeMask&(1u<<rt))activeBits++;
   int allBound=1;for(unsigned rt=0;rt<rtCount;rt++)if(!pipeline->genericRtFormats[rt])allBound=0;
   pipeline->genericTrueMrt=(activeBits>1&&allBound&&rtCount<=4)?1:0;
   pipeline->genericRtCount=rtCount;pipeline->genericRtActive=activeMask;pipeline->genericMainRt=mainRt;
   pipeline->genericMrt=rtCount>1;
   if(pipeline->genericTrueMrt)mainRt=0;
   if(mainRt<4&&pipeline->genericRtFormats[mainRt])pipeline->colorFormat=pipeline->genericRtFormats[mainRt];
   if(mainRt<4)pipeline->blend=(TeguAIRRenderBlend){1,gb[mainRt].enabled,gb[mainRt].rgb_op,gb[mainRt].rgb_src,
     gb[mainRt].rgb_dst,gb[mainRt].alpha_op,gb[mainRt].alpha_src,gb[mainRt].alpha_dst,pipeline->genericRtMasks[mainRt]};
   else extra=1;
   char fallbackMessage[512]={0};const char *outcome="observer";
   if(!extra&&(capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&[descriptor rasterSampleCount]==1&&
      ovn&&ofn&&(!layoutCount||(layoutCount==1&&vertexLayouts[0].index<31&&vertexLayouts[0].stride&&vertexLayouts[0].stride<=256&&
      vertexLayouts[0].step_function==1))) {
    if(rtCount==1) {
     struct tegu_blend_inputs one=gb[0];
     pipeline->compiled=tegu_pc_render_create((one.enabled||one.write_mask!=15)?TEGU_PC_AIR_BLEND:TEGU_PC_AIR_VERTEX,
      vertex,fragment,(uint32_t)pipeline->colorFormat,0,0,(one.enabled||one.write_mask!=15)?&one:0,
      vertexAttributes,attributeCount,vertexLayouts,layoutCount,
      tegu_native_profile,sizeof(tegu_native_profile),&vs,&fs,&meta,fallbackMessage,sizeof(fallbackMessage));
    } else {
     pipeline->compiled=tegu_pc_render_create(TEGU_PC_AIR_ATTACHMENT_BLENDS,vertex,fragment,0,mca,rtCount,mblend,
      vertexAttributes,attributeCount,vertexLayouts,layoutCount,
      tegu_native_profile,sizeof(tegu_native_profile),&vs,&fs,&meta,fallbackMessage,sizeof(fallbackMessage));
    }
    if(pipeline->compiled&&!tegu_render_generic_prepare(pipeline,&vs,&fs,vertexLayouts,layoutCount,&gb[mainRt],
       fallbackMessage,sizeof(fallbackMessage))) {
     snprintf(pipeline->genericName,sizeof(pipeline->genericName),"%s",ofn);outcome="generic";
     if(pipeline->genericPathClass) {
      char pathMessage[256]={0};
      int pathRC=tegu_render_path_reconcile(pipeline,&vs,&fs,vertexLayouts,layoutCount,gb,
       pathMessage,sizeof(pathMessage));
      if(pathRC){snprintf(fallbackMessage,sizeof(fallbackMessage),"path shader: %.180s",pathMessage);
       pipeline->genericDraw=0;}
     } else for(unsigned rt=0;rt<rtCount&&!pipeline->genericTrueMrt;rt++)if(rt!=mainRt&&(activeMask&(1u<<rt))) {
      char rtMessage[256]={0};
      int rtRC=tegu_render_generic_prepare_rt(pipeline,rt,&vs,&fs,&gb[rt],rtMessage,sizeof(rtMessage));
      if(rtRC){snprintf(fallbackMessage,sizeof(fallbackMessage),"rt%u shader: %.180s",rt,rtMessage);
       pipeline->genericDraw=0;break;}
     }
     if(pipeline->genericDraw)outcome=pipeline->genericMrt?"generic-mrt":"generic";
     /* True-MRT input-attachment/framebuffer fetch still has no exact
      * multi-target snapshot transport. Do not turn it into a generic draw
      * that later invalidates the command; admission becomes inert in
      * backboardd and stays nil/error elsewhere. */
     if(pipeline->genericDraw&&pipeline->genericTrueMrt&&
        pipeline->genericCompile.reads_framebuffer) {
      snprintf(fallbackMessage,sizeof(fallbackMessage),"true-MRT framebuffer fetch unsupported");
      pipeline->genericDraw=0;
     }
    }
   }
   if(pipeline->genericTrueMrt&&!pipeline->genericDraw) {
    static unsigned emitted;
    if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16) {
     char text[300];int n=snprintf(text,sizeof(text),"TEGU_TRUE_MRT_REFUSED reason=%.220s\n",
      fallbackMessage[0]?fallbackMessage:message);
     if(n>0)write(2,text,(size_t)n<sizeof(text)?(size_t)n:sizeof(text)-1);
    }
   }
   char line[900];int n=snprintf(line,sizeof(line),
    "TEGU_PIPELINE_FALLBACK %s vertex=%s fragment=%s format=%lu reason=%s fallback=%s\n",outcome,
    ovn?ovn:"",ofn?ofn:"",(unsigned long)diagnosticFormats[0],
    message[0]?message:"unsupported render pipeline descriptor or shader metadata",fallbackMessage);
   if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
   if(!pipeline->genericDraw) {
    tegu_air_render_pipeline_close(pipeline->compiled);pipeline->compiled=0;
    tegu_mali_graphics_free(&pipeline->vertexCode);tegu_mali_graphics_free(&pipeline->fragmentCode);
#ifdef TEGU_INERT_PIPELINES
    /* Explicit safety object: only the CoreAnimation render server receives a
     * non-nil PSO whose draws are refused one by one with a trace. Every
     * other process keeps the ordinary nil/NSError outcome. */
    if(tegu_render_inert_pipeline_allowed()) {
     const char *reason=fallbackMessage[0]?fallbackMessage:message;
     pipeline->inert=1;
     if(ofn)snprintf(pipeline->genericName,sizeof(pipeline->genericName),"%s",ofn);
     snprintf(pipeline->inertReason,sizeof(pipeline->inertReason),"%s",reason);
     tegu_render_inert_pipeline_trace(ovn,ofn,diagnosticFormats,rtCount,reason);
     tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
    }
#endif
#if defined(NORD2_JM_TRANSPORT)
    snprintf(message,sizeof(message),"Nord2 draw is unsupported: %.180s",fallbackMessage);
    goto nord2_render_error;
#else
    pipeline->genericObserverOnly=1;pipeline->colorFormat=(NSUInteger)diagnosticFormats[0];
#endif
   }
   tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;
  }
 }
#if defined(NORD2_JM_TRANSPORT)
nord2_render_error:
#endif
#ifdef TEGU_INERT_PIPELINES
 /* Descriptor/function-admission failures and missing capability bits still
  * return a bounded inert object in backboardd. Allocation failure remains a
  * nil result. */
 if(tegu_render_inert_pipeline_allowed()) {
  const char *reason=message[0]?message:"unsupported render pipeline descriptor or shader metadata";
  const char *vn=diagnosticOwned?tegu_render_safe_function_name(vertex):"?";
  const char *fn=diagnosticOwned?tegu_render_safe_function_name(fragment):"?";
  unsigned fc=0;for(unsigned i=0;i<8;i++)if(diagnosticFormats[i])fc=i+1;
  [pipeline release];pipeline=tegu_render_create_inert(self,vn,fn,diagnosticFormats,fc,reason);
  if(pipeline){tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);return pipeline;}
 }
#endif
 tegu_air_blob_free(&vs);tegu_air_blob_free(&fs);tegu_air_blob_free(&meta);[pipeline release];
 /* Keep refusal details in NSError as well as the best-effort diagnostic
  * stream. Device log transports can be unavailable during PSO creation. The
  * message buffer is bounded and all bridge writers receive its exact size. */
 message[sizeof(message)-1]=0;
 const char *detail=message[0]?message:"unsupported render pipeline descriptor or shader metadata";
 id description=[NSString stringWithUTF8String:detail];
 id info=description?[NSDictionary dictionaryWithObject:description forKey:@"NSLocalizedDescription"]:0;
 if(error)*error=[NSError errorWithDomain:@"TeguRenderPipeline" code:1 userInfo:info];
 return 0;
}
@end
