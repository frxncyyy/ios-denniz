/* Runtime compute pipeline integration. No shader-name/fixture dispatch.
 * Each pipeline owns the AIR artifact and an eight-entry specialization cache.
 * Commands retain imported variants independently of cache eviction.
 * Compilation is serialized because the Mesa bridge uses shared type state.
 */
#include "tegu-air-profile.h"
static unsigned tegu_compiler_lock;
static void tegu_lock(unsigned *lock) {
 /* Spin, then yield: the compiler lock can now be held by the background
  * variant compiler for tens of ms (render_pipeline.inc tegu_vf_*). */
 unsigned spins=0;
 while(__sync_lock_test_and_set(lock,1)) {
  __asm__ volatile("" ::: "memory");
  if(++spins>=4096u){extern int sched_yield(void);extern int usleep(unsigned);if(spins<16384u)(void)sched_yield();else (void)usleep(50);}
 }
}
static void tegu_unlock(unsigned *lock) { __sync_lock_release(lock); }
static void tegu_compile_message(const char *message) {
 NSUInteger n=0;while(n<511&&message[n])n++;
 write(2,"TEGU_PIPELINE ",14);write(2,message,n);write(2,"\n",1);
}
#if defined(__APPLE__)
#define TEGU_MALI_V2_WEAK __attribute__((weak_import))
#elif defined(__clang__) || defined(__GNUC__)
#define TEGU_MALI_V2_WEAK __attribute__((weak))
#else
#define TEGU_MALI_V2_WEAK
#endif
extern int tegu_mali_compile_compute_v2(const void *,size_t,
 const struct tegu_mali_specialization *,size_t,struct tegu_mali_blob *,char *,size_t)
 TEGU_MALI_V2_WEAK;
@interface NSObject (TeguComputePipelineDescriptorInput)
- (id)computeFunction;
@end
#include "compute_descriptor.inc"
extern TeguAIRPipeline *tegu_air_pipeline_create_ex(const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRComputeCreateOptions *,const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,char *,size_t)
 TEGU_MALI_V2_WEAK;
/* ---- AIR-only compute code generation (air-compute-opus) ------------------
 * Every compute package (creation check, TGP1 dispatch variant, TGP2 region
 * variant) compiles from the kernel's AIR through tegu_mali_compile_compute_air
 * with the m2v metadata of the same translation; the m2v SPIR-V is no longer
 * compiled. TRANSITIONAL: m2v still translates at creation and per dispatch
 * variant, for the planner (bindings, push, region constants) only. A refusal
 * fails the pipeline/dispatch with TEGU_AIR_REFUSED (no m2v code fallback).
 * TEGU_AIR_COMPUTE=0 (env; else TEGU_AIR_DIRECT) or -DTEGU_AIR_COMPUTE_DEFAULT=0
 * restores the m2v SPIR-V compile for A/B. */
#ifndef TEGU_AIR_COMPUTE_DEFAULT
#define TEGU_AIR_COMPUTE_DEFAULT 1
#endif
extern char *getenv(const char *);
struct tegu_compute_air_source {
 TeguLibrary *library;char entry[256];const char *refused;
 struct tegu_mali_air_kernel_constant constants[64];uint32_t constantCount;
 char names[4096];size_t namesUsed;
 uint8_t *meta;size_t metaLength;
};
static U64 tegu_air_compute_ok,tegu_air_compute_refused;
/* Read at each pipeline creation (cheap next to a compile); a pipeline keeps
 * the mode it was created with: AIR iff it has an airSource. */
static int tegu_air_compute_enabled(void) {
 const char *e=getenv("TEGU_AIR_COMPUTE");if(!e||!e[0])e=getenv("TEGU_AIR_DIRECT");
 return e&&e[0]?e[0]!='0':TEGU_AIR_COMPUTE_DEFAULT;
}
static void tegu_compute_air_source_free(void *p) {
 struct tegu_compute_air_source *s=p;
 if(!s)return;
 [s->library release];free(s->meta);free(s);
}
/* Creation: the kernel's AIR module and specialized constants, and the m2v
 * metadata (taken over from the caller). Never fails the creation itself; a
 * missing source is a refusal when something is compiled. */
static struct tegu_compute_air_source *tegu_compute_air_source_new(TeguFunction *function,const char *name,
 TeguAIRBlob *metadata,const TeguComputeCreateState *createState) {
 struct tegu_compute_air_source *s=calloc(1,sizeof(*s));
 if(!s)return 0;
 if(!name||strlen(name)>=sizeof(s->entry)){s->refused="kernel name";return s;}
 snprintf(s->entry,sizeof(s->entry),"%s",name);
 if(createState&&createState->used&&(createState->options.linked_count||createState->options.has_stage_input))
  s->refused="linked functions / stage input are not adapted to AIR yet";
 s->meta=metadata->bytes;s->metaLength=metadata->length;metadata->bytes=0;metadata->length=0;
 TeguAIRSourceConstant raw[64];size_t n=0;const uint8_t *bytes=0;size_t length=0;
 int rc=tegu_air_library_air_source(function->library->library,(const uint8_t *)name,strlen(name),&bytes,&length,raw,64,&n);
 if(rc||!bytes||!length){if(!s->refused)s->refused=rc==2?"function constant wider than 8 bytes":"no AIR source";return s;}
 for(size_t i=0;i<n;i++) {
  const char *cn=0;
  if(raw[i].name) {
   if(s->namesUsed+raw[i].name_len+1>sizeof(s->names)){if(!s->refused)s->refused="function constant names";return s;}
   memcpy(s->names+s->namesUsed,raw[i].name,raw[i].name_len);s->names[s->namesUsed+raw[i].name_len]=0;
   cn=s->names+s->namesUsed;s->namesUsed+=raw[i].name_len+1;
  }
  s->constants[i]=(struct tegu_mali_air_kernel_constant){cn,raw[i].index,raw[i].size,raw[i].bits};
 }
 s->constantCount=(uint32_t)n;
 s->library=[function->library retain];
 return s;
}
static void tegu_air_compute_trace(const struct tegu_compute_air_source *s,int rc,const char *why) {
 static unsigned traces;
 __atomic_fetch_add(&tegu_air_compute_refused,1,__ATOMIC_RELAXED);
 if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)>=64)return;
 char line[400];int n=snprintf(line,sizeof(line),"TEGU_AIR_REFUSED kernel %s rc=%d: %.300s\n",s&&s->entry[0]?s->entry:"?",rc,why&&why[0]?why:"?");
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
/* One compute package. AIR-only by default: `spirv` is used only by the m2v
 * opt-out. `meta` is the metadata of the translation whose plan is being
 * instantiated (the base pipeline's unless a dispatch variant). The caller holds
 * tegu_compiler_lock. */
static int tegu_compute_compile(TeguPipeline *pipeline,int v2,const unsigned char *spirv,NSUInteger spirvLength,
 const struct tegu_mali_specialization *constants,size_t count,const uint8_t *meta,size_t metaLength,
 struct tegu_mali_blob *package,char *message,size_t cap) {
 if(!pipeline->airSource) {
  if(v2)return tegu_mali_compile_compute_v2?tegu_mali_compile_compute_v2(spirv,(size_t)spirvLength,constants,count,package,message,cap):2;
  return tegu_mali_compile_specialized(spirv,(size_t)spirvLength,constants,count,package,message,cap);
 }
 const struct tegu_compute_air_source *s=pipeline->airSource;
 const char *why=!s?"no AIR source":s->refused?s->refused:!meta||!metaLength?"no m2v metadata for this translation":0;
 if(why){snprintf(message,cap,"air: %s",why);tegu_air_compute_trace(s,2,message);return 2;}
 const uint8_t *bytes=0;size_t length=0,unused=0;
 if(tegu_air_library_air_source(s->library->library,(const uint8_t *)s->entry,strlen(s->entry),&bytes,&length,NULL,0,&unused)==1||!bytes) {
  snprintf(message,cap,"air: no AIR source");tegu_air_compute_trace(s,2,message);return 2;
 }
 struct tegu_mali_air_compute air={TEGU_MALI_AIR_COMPUTE_VERSION,v2?TEGU_MALI_AIR_COMPUTE_V2:0u,bytes,length,s->entry,
  s->constants,s->constantCount,0,(const char *)meta,metaLength};
 int rc=tegu_mali_compile_compute_air(&air,constants,count,package,message,cap);
 /* the creation check tries TGP1 first and TGP2 next: its TGP1 refusal is not one */
 if(rc){if(v2||count)tegu_air_compute_trace(s,rc,message);}
 else __atomic_fetch_add(&tegu_air_compute_ok,1,__ATOMIC_RELAXED);
 return rc;
}
@interface TeguComputeDevice (RuntimePipelineCreate)
- (id)teguNewComputePipelineWithFunction:(TeguFunction *)function descriptor:(id)descriptor error:(id *)error;
@end
static void tegu_compute_pipeline_entry(const char *selectorName,TeguFunction *function,NSUInteger options) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
 const char *name=tegu_function_owned_class(function)?[function->functionName UTF8String]:0;
 char line[256];int n=snprintf(line,sizeof(line),
  "TEGU_COMPUTE_PIPELINE_ENTRY selector=%s options=0x%lx owned=%u function=%s\n",
  selectorName,(unsigned long)options,name!=0,name?name:"(foreign-or-nil)");
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
/* Convert the encoder's 31 Metal threadgroup-memory lengths into the
 * planner's sparse u32 table. NULL means the legacy no-dynamic path. */
static int tegu_runtime_threadgroup_words(const NSUInteger *lengths,uint32_t words[31]) {
 int any=0;
 for(unsigned i=0;i<31;i++) {
  NSUInteger length=lengths?lengths[i]:0;
  if(length>32768||(length&&length%16))return -1;
  words[i]=(uint32_t)length;any|=length!=0;
 }
 return any;
}
#ifdef TEGU_INERT_PIPELINES
/* Compute counterpart of render's inert PSO policy. Only backboardd gets a
 * non-nil state for a failed compute pipeline creation; other processes and
 * flag-off builds keep the explicit nil/NSError result. The object carries no
 * compiled package and its dispatches are dropped with a bounded trace. */
static int tegu_compute_inert_pipeline_allowed(void) {
 return tegu_compositor_process_is_backboardd();
}
static void tegu_compute_inert_pipeline_trace(const char *selector,const char *reason) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=128)return;
 char line[320];int n=snprintf(line,sizeof(line),
  "TEGU_INERT_COMPUTE_PIPELINE selector=%.40s reason=%.220s\n",
  selector?selector:"compute-pipeline",reason&&reason[0]?reason:"unsupported compute pipeline");
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void tegu_compute_mark_inert(TeguPipeline *pipeline,const char *reason) {
 (void)reason;
 pipeline->inert=1;
 pipeline->info.local[0]=pipeline->info.local[1]=pipeline->info.local[2]=1;
#ifdef TEGU_MALI_BRIDGE
 tegu_air_pipeline_close(pipeline->compiled);pipeline->compiled=0;
 tegu_air_blob_free(&pipeline->spirv);
 pipeline->limits[0]=pipeline->limits[1]=pipeline->limits[2]=0;
#endif
}
static TeguPipeline *tegu_compute_create_inert(TeguComputeDevice *owner,id *error,
 const char *selector,const char *reason) {
 TeguPipeline *pipeline=[[TeguPipeline alloc] init];
 if(!pipeline)return 0;
 pipeline->owner=[owner retain];
 tegu_compute_mark_inert(pipeline,reason);
 tegu_compute_inert_pipeline_trace(selector,reason);
 if(error)*error=0;
 return pipeline;
}
#endif
@implementation TeguComputeDevice (RuntimePipelines)
- (id)newComputePipelineStateWithFunction:(TeguFunction *)function error:(id *)error {
 return [self teguNewComputePipelineWithFunction:function descriptor:0 error:error];
}
/* The pipeline creation shared by the function and descriptor entry points. A descriptor
 * with linked functions, stage input or a call stack depth goes through
 * tegu_air_pipeline_create_ex; any other input keeps the byte-identical plain create. */
- (id)teguNewComputePipelineWithFunction:(TeguFunction *)function descriptor:(id)descriptor error:(id *)error {
 if(error) *error=0;
 tegu_compute_pipeline_entry("function_error",function,0);
 if(!function||!tegu_function_owned_class(function)||function->library->owner!=self||function->kind!=3) {
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed())
   return tegu_compute_create_inert(self,error,"function_error","invalid or foreign compute function");
#endif
  if(error) *error=[NSError errorWithDomain:@"TeguComputePipeline" code:1 userInfo:0];
  return 0;
 }
 TeguPipeline *pipeline=[[TeguPipeline alloc] init];
 if(!pipeline) return 0;
 pipeline->owner=[self retain];
 const char *name=[function->functionName UTF8String];NSUInteger length=0;
 if(name) while(name[length]&&length<4096) length++;
 char message[512]={0};TeguAIRBlob metadata={0};
 TeguComputeCreateState createState;
 const char *optionsRefused=tegu_compute_create_state_init(self,descriptor,&createState);
 if(optionsRefused) {
  snprintf(message,sizeof(message),"compute pipeline descriptor: %s",optionsRefused);
 } else if(createState.used) {
  if(tegu_air_pipeline_create_ex)
   pipeline->compiled=tegu_air_pipeline_create_ex(function->library->library,(const unsigned char *)name,length,
    &createState.options,tegu_native_profile,sizeof(tegu_native_profile),&pipeline->spirv,&metadata,message,sizeof(message));
  else snprintf(message,sizeof(message),"this native compiler build has no linked-function/stage-input support");
 } else {
  pipeline->compiled=tegu_air_pipeline_create(function->library->library,(const unsigned char *)name,length,
   tegu_native_profile,sizeof(tegu_native_profile),&pipeline->spirv,&metadata,message,sizeof(message));
 }
 if(pipeline->compiled&&tegu_air_compute_enabled()) {
  pipeline->airSource=tegu_compute_air_source_new(function,name,&metadata,optionsRefused?0:&createState);
  if(!pipeline->airSource){snprintf(message,sizeof(message),"air: compute source allocation failed");
   tegu_air_pipeline_close(pipeline->compiled);pipeline->compiled=0;}
 }
 tegu_air_blob_free(&metadata);
 int failed=!pipeline->compiled;
 if(!failed) failed=tegu_air_pipeline_limits(pipeline->compiled,pipeline->limits);
 int dispatchable=failed?-1:tegu_air_pipeline_dispatchable(pipeline->compiled);
 if(dispatchable<0) failed=1;
 /* Validate ordinary lowering at pipeline creation. A storage-texture
  * stand-in exists only so Metal may create the state before resources are
  * bound; sending it to the backend would compile a guessed format. */
 if(!failed&&dispatchable) {
  struct tegu_mali_blob package={0};
  tegu_lock(&tegu_compiler_lock);
  const struct tegu_compute_air_source *air=pipeline->airSource;
  const uint8_t *meta=air?air->meta:0;size_t metaLength=air?air->metaLength:0;
  failed=tegu_compute_compile(pipeline,0,pipeline->spirv.bytes,pipeline->spirv.length,0,0,meta,metaLength,
   &package,message,sizeof(message));
  /* A kernel with texture/image resources cannot be represented by TGP1.
   * The explicit v2 entry is preferred only after the legacy entry refuses;
   * buffer-only kernels keep byte-identical TGP1 behavior. */
  if(failed&&(pipeline->airSource||tegu_mali_compile_compute_v2)) {
   tegu_mali_free(&package);
   failed=tegu_compute_compile(pipeline,1,pipeline->spirv.bytes,pipeline->spirv.length,0,0,meta,metaLength,
    &package,message,sizeof(message));
  }
  tegu_mali_free(&package);tegu_unlock(&tegu_compiler_lock);
 }
 if(failed) {
  tegu_compile_message(message);
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed()) {
   const char *reason=message[0]?message:"compute pipeline lowering/compilation refused";
   tegu_compute_mark_inert(pipeline,reason);
   tegu_compute_inert_pipeline_trace("function_error",reason);
   if(error)*error=0;
   return pipeline;
  }
#endif
  if(error) *error=[NSError errorWithDomain:@"TeguComputePipeline" code:2 userInfo:0];
  [pipeline release];return 0;
 }
 return pipeline;
}
- (id)newComputePipelineStateWithFunction:(TeguFunction *)function options:(NSUInteger)options reflection:(id *)reflection error:(id *)error {
 if(reflection)*reflection=0;if(error)*error=0;
 tegu_compute_pipeline_entry("function_options_reflection",function,options);
 /* Reflection contents are not implemented. Option zero has the same pipeline
  * contract as the public synchronous function/error entry. */
 if(options) {
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed())
   return tegu_compute_create_inert(self,error,"function_options_reflection",
    "compute reflection options are unsupported");
#endif
  if(error)*error=[NSError errorWithDomain:@"TeguComputePipeline" code:3 userInfo:0];return 0;
 }
 return [self newComputePipelineStateWithFunction:function error:error];
}
- (id)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options reflection:(id *)reflection error:(id *)error {
 if(reflection)*reflection=0;if(error)*error=0;
 TeguFunction *function=descriptor?[descriptor computeFunction]:0;
 tegu_compute_pipeline_entry("descriptor_options_reflection",function,options);
 if(!descriptor||options) {
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed())
   return tegu_compute_create_inert(self,error,"descriptor_options_reflection",
    "compute pipeline descriptor or reflection options are unsupported");
#endif
  if(error)*error=[NSError errorWithDomain:@"TeguComputePipeline" code:4 userInfo:0];return 0;
 }
 return [self teguNewComputePipelineWithFunction:function descriptor:descriptor error:error];
}
@end
static int tegu_runtime_dispatch(TeguPipeline *pipeline,TeguBuffer **buffers,NSUInteger *offsets,
 const NSUInteger *threadgroupLengths,MTLSize groups,MTLSize local,struct Dispatch *job) {
 if(!local.width||!local.height||!local.depth||local.width>0xffffffffUL||local.height>0xffffffffUL||local.depth>0xffffffffUL) return 0;
 uint32_t lengths[31];int any=tegu_runtime_threadgroup_words(threadgroupLengths,lengths);
 if(any<0)return 0;
 TeguAIRBufferBinding bindings[31];unsigned count=0;
 for(unsigned i=0;i<31;i++) if(buffers[i]) {
  bindings[count++]=(TeguAIRBufferBinding){i,0,buffers[i]->logical,offsets[i]};
 }
 uint32_t grid[3]={(uint32_t)groups.width,(uint32_t)groups.height,(uint32_t)groups.depth};
 uint32_t threads[3]={(uint32_t)local.width,(uint32_t)local.height,(uint32_t)local.depth};
 TeguAIRNativeDispatch plan;char message[512]={0};
 if(tegu_air_pipeline_native_dispatch_tg(pipeline->compiled,bindings,count,any?lengths:NULL,
    grid,threads,&plan,message,sizeof(message))) {
  tegu_compile_message(message);return 0;
 }
 if(plan.version!=1||plan.constant_count>64||plan.bindings_mask&~255u) return 0;
 for(unsigned i=0;i<8;i++) if(plan.bindings_mask&(1u<<i)) {
  TeguAIRNativeBinding *b=&plan.bindings[i];
  if(b->index>=31||!buffers[b->index]||b->offset>0xffffffffUL||b->length>0xffffffffUL) return 0;
  TeguBuffer *buffer=buffers[b->index];
  U64 padded=(buffer->logical+15ULL)&~15ULL;
  if(buffer->owner!=pipeline->owner||b->offset>=padded||b->length>padded-b->offset||
     b->offset>=buffer->mapped||b->length>buffer->mapped-b->offset) return 0;
 }
 TeguPipeline *selected=0;
 tegu_lock(&pipeline->cacheLock);
 for(unsigned i=0;i<8;i++) {
  struct TeguVariant *v=&pipeline->variants[i];
  if(v->pipeline&&v->count==plan.constant_count&&
     !__builtin_memcmp(v->constants,plan.constants,plan.constant_count*sizeof(plan.constants[0]))) {
   selected=[v->pipeline retain];break;
  }
 }
 if(!selected) {
  struct tegu_mali_specialization constants[64];
  for(unsigned i=0;i<plan.constant_count;i++) {
   constants[i].id=plan.constants[i].id;constants[i].size=plan.constants[i].size;
   __builtin_memcpy(constants[i].data,plan.constants[i].data,8);
  }
  struct tegu_mali_blob package={0};
  tegu_lock(&tegu_compiler_lock);
  const struct tegu_compute_air_source *air=pipeline->airSource;
  int failed=tegu_compute_compile(pipeline,0,pipeline->spirv.bytes,pipeline->spirv.length,
   constants,plan.constant_count,air?air->meta:0,air?air->metaLength:0,&package,message,sizeof(message));
  tegu_unlock(&tegu_compiler_lock);
  TeguPipeline *variant=failed?0:[pipeline->owner newTeguPipelineWithPackage:package.data length:package.size];
  tegu_mali_free(&package);
  if(variant) {
   struct TeguVariant *v=&pipeline->variants[pipeline->nextVariant++%8];
   [v->pipeline release];v->pipeline=variant;v->count=plan.constant_count;
   __builtin_memcpy(v->constants,plan.constants,plan.constant_count*sizeof(plan.constants[0]));
   selected=[variant retain];
  } else tegu_compile_message(message);
 }
 tegu_unlock(&pipeline->cacheLock);
 if(!selected) return 0;
 if((selected->info.bindings&~plan.bindings_mask)||selected->info.local[0]!=local.width||
    selected->info.local[1]!=local.height||selected->info.local[2]!=local.depth) { [selected release];return 0; }
 job->pipeline=selected;
 job->wire[0]=1;for(unsigned i=0;i<3;i++) job->wire[i+1]=plan.groups[i];
 __builtin_memcpy(&job->wire[36],plan.push,128);
 for(unsigned i=0;i<8;i++) if(selected->info.bindings&(1u<<i)) {
  TeguAIRNativeBinding *b=&plan.bindings[i];TeguBuffer *buffer=buffers[b->index];
  job->buffers[i]=[buffer retain];
  job->wire[4+4*i]=(unsigned)buffer->handle;
  job->wire[5+4*i]=(unsigned)b->offset;job->wire[6+4*i]=(unsigned)b->length;

 }
 return 1;
}
/* ---- v2 compute resources ------------------------------------------------
 * Native identity is the SPIR-V set0 binding N: table1[N] textures/images,
 * table2[N] samplers, FAU slot N for buffers 0..7, dummy sampler table2[256].
 * The object array is opaque here; the caller supplies validated Metal facts,
 * the retained objects and the backing token for each resource. */
#define TEGU_V2_KIND_UBO 1u
#define TEGU_V2_KIND_SSBO 2u
#define TEGU_V2_KIND_TEXTURE 3u
#define TEGU_V2_KIND_IMAGE 4u
#define TEGU_V2_KIND_SAMPLER 5u
#define TEGU_V2_KIND_COMBINED 6u
typedef int (*tegu_v2_compile_fn)(const void *,size_t,const struct tegu_mali_specialization *,
 size_t,struct tegu_mali_blob *,char *,size_t);
static void tegu_v2_put64(unsigned char *p,uint64_t v)
{
 tegu_pipeline_put(p,(uint32_t)v);tegu_pipeline_put(p+4,(uint32_t)(v>>32));
}
static const TeguAIRComputeResource *tegu_v2_find(const TeguAIRComputeResource *r,unsigned n,
 unsigned kind,unsigned index)
{
 for(unsigned i=0;i<n;i++)if(r[i].kind==kind&&r[i].index==index)return &r[i];
 return 0;
}
static const struct tegu_pipeline_manifest *tegu_v2_manifest(
 const struct tegu_pipeline_info2 *m,unsigned table,unsigned entry)
{
 for(unsigned i=0;i<m->manifest_count;i++)
  if(m->manifest[i].table==table&&m->manifest[i].entry==entry)return &m->manifest[i];
 return 0;
}
/* Exact correspondence between the package manifest and the selected plan.
 * A combined wire descriptor requires both table1[N] and table2[N] records. */
static int tegu_v2_manifest_match(const struct tegu_pipeline_info2 *m,
 const TeguAIRNativeDispatchV2 *plan,char *message,NSUInteger cap)
{
 unsigned expected=0,required=m->manifest_count;
 if(m->flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER) {
  if(!required)return 0;
  required--;
 }
 for(unsigned d=0;d<plan->descriptor_count;d++)expected+=plan->descriptors[d].kind==TEGU_V2_KIND_COMBINED?2u:1u;
 if(expected!=required) {
  snprintf(message,(size_t)cap,"compute v2 manifest count %u does not cover plan records %u",required,expected);
  return 0;
 }
 for(unsigned d=0;d<plan->descriptor_count;d++) {
  const TeguAIRComputeDescriptor *w=&plan->descriptors[d];
  unsigned binding=w->binding;const struct tegu_pipeline_manifest *rec=0;
  if(w->kind<=TEGU_V2_KIND_SSBO) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_FAU,binding);
   if(!rec||rec->kind!=w->kind||rec->binding!=binding){snprintf(message,(size_t)cap,"compute v2 buffer manifest mismatch at binding %u",binding);return 0;}
  } else if(w->kind==TEGU_V2_KIND_TEXTURE||w->kind==TEGU_V2_KIND_IMAGE) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_TEXTURE,binding);
   if(!rec||rec->kind!=w->kind||rec->binding!=binding||
      (rec->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)){snprintf(message,(size_t)cap,"compute v2 texture/image manifest mismatch at binding %u",binding);return 0;}
  } else if(w->kind==TEGU_V2_KIND_SAMPLER) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_SAMPLER,binding);
   if(!rec||rec->kind!=TEGU_V2_KIND_SAMPLER||rec->binding!=binding||rec->access!=0||
      (rec->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
      (((rec->flags&TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE)!=0)!=((w->sampler_flags&1u)!=0))){snprintf(message,(size_t)cap,"compute v2 sampler manifest mismatch at binding %u",binding);return 0;}
  } else if(w->kind==TEGU_V2_KIND_COMBINED) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_TEXTURE,binding);
   const struct tegu_pipeline_manifest *s=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_SAMPLER,binding);
   if(!rec||rec->kind!=TEGU_V2_KIND_TEXTURE||rec->binding!=binding||
      !(rec->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||!s||
      s->kind!=TEGU_V2_KIND_SAMPLER||s->binding!=binding||s->access!=0||
      !(s->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
      (((s->flags&TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE)!=0)!=((w->sampler_flags&1u)!=0))){
    snprintf(message,(size_t)cap,"compute v2 combined manifest mismatch at binding %u",binding);return 0;
   }
  } else {snprintf(message,(size_t)cap,"compute v2 unknown descriptor kind");return 0;}
  unsigned need=rec->access&(TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_WRITE|
   TEGU_AIR_COMPUTE_ACCESS_SAMPLE|TEGU_AIR_COMPUTE_ACCESS_STORAGE|0x1000u);
  if((w->flags&need)!=need){snprintf(message,(size_t)cap,"compute v2 plan lacks manifest access at binding %u",binding);return 0;}
 }
 return 1;
}
static TeguPipeline *tegu_v2_variant(TeguPipeline *pipeline,const TeguAIRNativeDispatchV2 *plan,
 unsigned region,const unsigned char *spirv,NSUInteger spirv_length,tegu_v2_compile_fn compile,
 unsigned require_v2,char *message,NSUInteger cap)
{
 TeguPipeline *selected=0;int valid=1;
 tegu_lock(&pipeline->cacheLock);
 for(unsigned i=0;i<8;i++) {
  struct TeguVariant *v=&pipeline->variants[i];
  if(v->pipeline&&v->count==plan->regions[region].constant_count&&
     (!require_v2||v->pipeline->packageVersion==2)&&
     !__builtin_memcmp(v->variantHash,plan->variant_hash,32)&&
     !__builtin_memcmp(v->constants,plan->regions[region].constants,
       plan->regions[region].constant_count*sizeof(plan->regions[region].constants[0]))) {
   selected=[v->pipeline retain];break;
  }
 }
 if(!selected) {
  struct tegu_mali_blob package={0};
  tegu_lock(&tegu_compiler_lock);
  /* AIR: the base metadata describes this plan when the dispatch did not
   * select a translator variant (spirv is then the base pipeline's); otherwise
   * the bridge returns the variant's own metadata. */
  const struct tegu_compute_air_source *air=pipeline->airSource;
  int base=spirv==pipeline->spirv.bytes;
  int failed=!spirv||!spirv_length||!compile;
  if(!failed&&pipeline->airSource) {
   TeguAIRBlob variantMeta={0};
   const uint8_t *meta=air?air->meta:0;size_t metaLength=air?air->metaLength:0;
   if(!base) {
    /* the translator variant this plan belongs to (same SPIR-V words) */
    int vm=tegu_air_pipeline_variant_metadata(pipeline->compiled,spirv,(size_t)spirv_length,&variantMeta);
    meta=vm?0:variantMeta.bytes;metaLength=vm?0:variantMeta.length;
   }
   failed=tegu_compute_compile(pipeline,require_v2?1:0,spirv,spirv_length,
    (const struct tegu_mali_specialization *)plan->regions[region].constants,plan->regions[region].constant_count,
    meta,metaLength,&package,message,(size_t)cap);
   tegu_air_blob_free(&variantMeta);
  } else if(!failed) failed=compile(spirv,(size_t)spirv_length,
    (const struct tegu_mali_specialization *)plan->regions[region].constants,
    plan->regions[region].constant_count,&package,message,(size_t)cap);
  tegu_unlock(&tegu_compiler_lock);
  TeguPipeline *variant=failed?0:[pipeline->owner newTeguPipelineWithPackage:package.data length:package.size];
  tegu_mali_free(&package);
  if(!variant){tegu_unlock(&pipeline->cacheLock);return 0;}
  struct TeguVariant *slot=&pipeline->variants[pipeline->nextVariant++%8];
  [slot->pipeline release];slot->pipeline=[variant retain];slot->count=plan->regions[region].constant_count;
  __builtin_memcpy(slot->variantHash,plan->variant_hash,32);
  __builtin_memcpy(slot->constants,plan->regions[region].constants,
   plan->regions[region].constant_count*sizeof(plan->regions[region].constants[0]));
  selected=variant;
 }
 if(require_v2&&selected->packageVersion!=2) {
  snprintf(message,(size_t)cap,"Mali compiler did not emit TGP2 for a texture/sampler/image dispatch");
  valid=0;
 } else if(selected->packageVersion==2) {
  valid=tegu_v2_manifest_match(&selected->info2,plan,message,cap);
 } else {
  unsigned planned=0;
  for(unsigned d=0;d<plan->descriptor_count;d++)if(plan->descriptors[d].kind<=TEGU_V2_KIND_SSBO)
   planned|=1u<<plan->descriptors[d].binding;
  if(planned!=selected->info.bindings){
   snprintf(message,(size_t)cap,"compute v2 buffer-only TGP1 package binding mask differs from its plan");
   valid=0;
  }
 }
 if(!valid){[selected release];selected=0;}
 tegu_unlock(&pipeline->cacheLock);
 return selected;
}
static int __attribute__((unused)) tegu_runtime_dispatch_v2(TeguPipeline *pipeline,const TeguAIRComputeResource *resources,
 id *objects,const uint64_t *tokens,const NSUInteger *threadgroupLengths,unsigned count,unsigned mode,
 MTLSize extents,MTLSize local,TeguCommand *command,char *message,NSUInteger messageCapacity)
{
 if(!pipeline||!pipeline->compiled||!resources||!objects||!tokens||count>32||mode>1||!command)return 0;
 uint32_t lengths[31];int any=tegu_runtime_threadgroup_words(threadgroupLengths,lengths);
 if(any<0)return 0;
 uint32_t grid[3]={(uint32_t)extents.width,(uint32_t)extents.height,(uint32_t)extents.depth};
 uint32_t threads[3]={(uint32_t)local.width,(uint32_t)local.height,(uint32_t)local.depth};
 TeguAIRBlob variant={0};TeguAIRNativeDispatchV2 plan;
 int status=tegu_air_pipeline_native_dispatch_v2_tg(pipeline->compiled,resources,count,any?lengths:NULL,
  mode,grid,threads,&variant,&plan,message,(size_t)messageCapacity);
 if(status){tegu_air_blob_free(&variant);return 0;}
 const unsigned char *spirv=variant.bytes?variant.bytes:pipeline->spirv.bytes;
 NSUInteger spirv_length=variant.bytes?variant.length:pipeline->spirv.length;
 /* This path always emits a TDG2 wire, which the bridge accepts only for a
  * TGP2 package, so every variant selected here is compiled with the v2 entry. */
 int require_v2=1;
 tegu_v2_compile_fn compile=tegu_mali_compile_specialized;
 if(require_v2) {
  if(!tegu_mali_compile_compute_v2){
   snprintf(message,(size_t)messageCapacity,"compiler v2 entry unavailable (tegu_mali_compile_compute_v2); v2 dispatch refused");
   goto fail;
  }
  compile=tegu_mali_compile_compute_v2;
 }
 for(unsigned r=0;r<plan.region_count;r++) {
  if(command->count>=TEGU_COMMAND_MAX_JOBS){snprintf(message,(size_t)messageCapacity,"compute v2 command capacity");goto fail;}
  TeguPipeline *selected=tegu_v2_variant(pipeline,&plan,r,spirv,spirv_length,compile,
   (unsigned)require_v2,message,messageCapacity);
  if(!selected)goto fail;
  if(selected->info.local[0]!=plan.regions[r].local[0]||
     selected->info.local[1]!=plan.regions[r].local[1]||
     selected->info.local[2]!=plan.regions[r].local[2]) {
   snprintf(message,(size_t)messageCapacity,"compute v2 package local size differs from the specialized region local");
   [selected release];goto fail;
  }
  unsigned char *wire=malloc(TEGU_PIPELINE_DISPATCH2_BYTES);
  if(!wire){snprintf(message,(size_t)messageCapacity,"compute v2 wire allocation failed");[selected release];goto fail;}
  __builtin_memset(wire,0,TEGU_PIPELINE_DISPATCH2_BYTES);
  tegu_pipeline_put(wire+0,TEGU_PIPELINE_DISPATCH2_MAGIC);
  tegu_pipeline_put(wire+4,TEGU_PIPELINE_DISPATCH2_VERSION);
  tegu_pipeline_put(wire+8,plan.descriptor_count);
  for(unsigned a=0;a<3;a++)tegu_pipeline_put(wire+12+4*a,plan.regions[r].groups[a]);
  tegu_pipeline_put(wire+24,selected->info.push_words);
  int bad=0;
  for(unsigned d=0;d<plan.descriptor_count&&!bad;d++) {
   const TeguAIRComputeDescriptor *desc=&plan.descriptors[d];
   unsigned char *w=wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+d*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
   unsigned rk=desc->kind<=2?TEGU_AIR_COMPUTE_BUFFER:
    (desc->kind<=4||desc->kind==TEGU_V2_KIND_COMBINED)?TEGU_AIR_COMPUTE_TEXTURE:TEGU_AIR_COMPUTE_SAMPLER;
   unsigned at=count;
   for(unsigned i=0;i<count;i++)if(resources[i].kind==rk&&resources[i].index==desc->metal_index){at=i;break;}
   if(at==count){snprintf(message,(size_t)messageCapacity,"compute v2 binding %u has no resource",desc->binding);bad=1;break;}
   const TeguAIRComputeResource *res=&resources[at];
   if(desc->kind!=TEGU_V2_KIND_SAMPLER&&!tokens[at]){snprintf(message,(size_t)messageCapacity,"compute v2 binding %u has no backing token",desc->binding);bad=1;break;}
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_BINDING,desc->binding);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_KIND,desc->kind);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_METAL_INDEX,desc->metal_index);
   uint32_t dflags=desc->flags;
   if(res->swizzle_valid)dflags|=TEGU_PIPELINE2_DESC_SWIZZLE_VALID;
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_FLAGS,dflags);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SAMPLER_INDEX,desc->sampler_index);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SAMPLER_FLAGS,desc->sampler_flags);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_TOKEN,tokens[at]);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_OFFSET,desc->range_offset);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_SPAN,desc->range_length);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_FORMAT,res->format);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_FORMAT,desc->view_format);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_DIM,res->texture_type);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_WIDTH,res->width);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_HEIGHT,res->height);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_DEPTH,res->depth);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_LAYERS,res->array_layers);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_LEVELS,res->mipmap_levels);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SAMPLES,res->sample_count);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_USAGE,res->usage);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_ROW_STRIDE,res->row_stride);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_STORAGE_MODE,res->storage_mode);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_LEVEL_OFFSET,0);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SLICE_STRIDE,res->slice_stride);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_LAYER_STRIDE,res->layer_stride);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SWIZZLE,res->swizzle_word);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_LEVEL,desc->view_level);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_LAYER,desc->view_layer);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_FORMAT_VK,desc->view_format_vk);
   if(desc->kind==TEGU_V2_KIND_SAMPLER||desc->kind==TEGU_V2_KIND_COMBINED) {
    unsigned si=desc->kind==TEGU_V2_KIND_SAMPLER?desc->metal_index:desc->sampler_index;
    const TeguAIRComputeResource *s=tegu_v2_find(resources,count,TEGU_AIR_COMPUTE_SAMPLER,si);
    if(!s){snprintf(message,(size_t)messageCapacity,"compute v2 sampler %u missing",si);bad=1;break;}
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MIN_FILTER,s->min_filter);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MAG_FILTER,s->mag_filter);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MIP_FILTER,s->mip_filter);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_ADDRESS_S,s->address_s);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_ADDRESS_T,s->address_t);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_ADDRESS_R,s->address_r);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_BORDER_COLOR,s->border_color);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_NORMALIZED,s->normalized_coordinates);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_COMPARE,s->compare_function);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MAX_ANISOTROPY,s->max_anisotropy);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_REDUCTION,s->reduction_mode);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_LOD_AVERAGE,s->lod_average);
    __builtin_memcpy(w+TEGU_PIPELINE_D2_LOD_MIN,&s->lod_min_clamp,4);
    __builtin_memcpy(w+TEGU_PIPELINE_D2_LOD_MAX,&s->lod_max_clamp,4);
   }
  }
  if(bad){free(wire);[selected release];goto fail;}
  __builtin_memcpy(wire+TEGU_PIPELINE_DISPATCH2_PUSH_OFFSET,plan.regions[r].push,128);
  struct Dispatch *job=tegu_command_job(command,command->count);
  job->pipeline=[selected retain];job->wire2=wire;job->wire2Bytes=TEGU_PIPELINE_DISPATCH2_BYTES;
  for(unsigned d=0;d<plan.descriptor_count;d++) {
   const TeguAIRComputeDescriptor *desc=&plan.descriptors[d];
   unsigned rk=desc->kind<=2?TEGU_AIR_COMPUTE_BUFFER:
    (desc->kind<=4||desc->kind==TEGU_V2_KIND_COMBINED)?TEGU_AIR_COMPUTE_TEXTURE:TEGU_AIR_COMPUTE_SAMPLER;
   for(unsigned i=0;i<count;i++)if(resources[i].kind==rk&&resources[i].index==desc->metal_index) {
    id object=objects[i];
    if(rk==TEGU_AIR_COMPUTE_BUFFER) {
     if(!job->v2Buffers[desc->metal_index])job->v2Buffers[desc->metal_index]=[object retain];
    } else if(rk==TEGU_AIR_COMPUTE_TEXTURE) {
     if(!job->v2Textures[desc->metal_index])job->v2Textures[desc->metal_index]=[object retain];
    } else if(!job->v2Samplers[desc->metal_index])job->v2Samplers[desc->metal_index]=[object retain];
    break;
   }
   if(desc->kind==TEGU_V2_KIND_COMBINED&&!job->v2Samplers[desc->sampler_index]) {
    for(unsigned i=0;i<count;i++)if(resources[i].kind==TEGU_AIR_COMPUTE_SAMPLER&&
       resources[i].index==desc->sampler_index){job->v2Samplers[desc->sampler_index]=[objects[i] retain];break;}
   }
  }
  command->count++;
  [selected release];
 }
 tegu_air_blob_free(&variant);
 return 1;
fail:
 tegu_air_blob_free(&variant);
 return 0;
}
