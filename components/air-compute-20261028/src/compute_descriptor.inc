/* MTLComputePipelineDescriptor inputs beyond the kernel function: linkedFunctions,
 * stageInputDescriptor and maxCallStackDepth (included by runtime_pipeline.inc).
 *
 * tegu_compute_create_state_init reads the public descriptor into the bridge's
 * versioned TeguAIRComputeCreateOptions (air_bridge.h, tegu_air_pipeline_create_ex).
 * It returns NULL when the descriptor is representable and a static reason when it is
 * not; a reason is a refusal of the pipeline, never a silent drop of the input.
 * `used` is 0 for a plain descriptor, which keeps the byte-identical
 * tegu_air_pipeline_create path.
 *
 *  - linked functions: every MTLFunction of functions / privateFunctions /
 *    binaryFunctions is passed by (its own library handle, its own name, list); a
 *    function-constant specialization already is a TeguFunction over a specialized
 *    library snapshot, so its handle is the specialized one. Groups, preloaded and
 *    inserted dynamic libraries have no translator input and are refused.
 *  - stage input: attributes (format, offset, bufferIndex) and layouts (stride,
 *    stepFunction incl. ThreadPositionInGridX.., stepRate), indexType and
 *    indexBufferIndex, only when the descriptor carries a stageInputDescriptor.
 *  - maxCallStackDepth: values above Metal's default of 1 are forwarded. */
#define TEGU_COMPUTE_MAX_LINKED 256u
typedef struct {
 TeguAIRComputeCreateOptions options;
 TeguAIRLinkedFunction linked[TEGU_COMPUTE_MAX_LINKED];
 TeguAIRVertexAttribute attributes[31];
 TeguAIRVertexLayout layouts[31];
 /* Name storage stays valid because each name is borrowed from a live TeguFunction
  * the descriptor retains for the whole creation call. */
 int used;
} TeguComputeCreateState;
@interface NSObject (TeguComputeDescriptorOptions)
- (id)linkedFunctions;
- (id)stageInputDescriptor;
- (NSUInteger)maxCallStackDepth;
- (id)functions;
- (id)privateFunctions;
- (id)binaryFunctions;
- (id)groups;
- (id)preloadedLibraries;
- (id)insertLibraries;
- (id)attributes;
- (id)layouts;
- (NSUInteger)format;
- (NSUInteger)bufferIndex;
- (NSUInteger)offset;
- (NSUInteger)stride;
- (NSUInteger)stepFunction;
- (NSUInteger)stepRate;
- (NSUInteger)indexType;
- (NSUInteger)indexBufferIndex;
- (id)objectAtIndexedSubscript:(NSUInteger)index;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
- (signed char)respondsToSelector:(SEL)selector;
@end
/* Donor/mock descriptors may lack newer properties: absent means the Metal default. */
#define TEGU_DESC_HAS(d,sel) ([d respondsToSelector:@selector(sel)])
static const char *tegu_compute_linked_list(TeguComputeDevice *owner,id list,uint32_t kind,
 TeguComputeCreateState *st) {
 if(!list)return 0;
 NSUInteger count=[list count];
 for(NSUInteger i=0;i<count;i++) {
  TeguFunction *f=[list objectAtIndex:i];
  if(!f||!tegu_function_owned_class(f)||!f->library||f->library->owner!=owner||!f->library->library)
   return "linked function is not a function of this device";
  if(f->kind!=3&&f->kind!=5)return "linked function is neither a visible function nor a kernel";
  if(st->options.linked_count>=TEGU_COMPUTE_MAX_LINKED)return "too many linked functions";
  const char *name=[f->functionName UTF8String];size_t length=0;
  if(!name)return "linked function has no name";
  while(length<4096&&name[length])length++;
  if(!length||length==4096)return "linked function name is out of range";
  /* name: the library function; link_name: its MTLFunctionDescriptor.specializedName (MTLFunction.name) when set. */
  const char *shown=f->displayName?[f->displayName UTF8String]:0;size_t shownLength=0;
  if(shown){while(shownLength<4096&&shown[shownLength])shownLength++;if(shownLength==4096)return "linked function specialized name is out of range";}
  if(shown&&shownLength==length&&!__builtin_memcmp(shown,name,length))shown=0;
  if(shown&&!shownLength)shown=0;
  st->linked[st->options.linked_count++]=(TeguAIRLinkedFunction){f->library->library,(const uint8_t *)name,length,
   (const uint8_t *)shown,shown?shownLength:0,kind,0};
 }
 return 0;
}
static const char *tegu_compute_create_state_init(TeguComputeDevice *owner,id descriptor,
 TeguComputeCreateState *st) {
 __builtin_memset(st,0,sizeof(*st));
 st->options.version=1;
 if(!descriptor)return 0;
 id linked=TEGU_DESC_HAS(descriptor,linkedFunctions)?[descriptor linkedFunctions]:0;
 if(linked) {
  id groups=TEGU_DESC_HAS(linked,groups)?[linked groups]:0;
  if(groups&&[groups count])return "linked function groups are not supported";
  const char *why;
  if((why=tegu_compute_linked_list(owner,[linked functions],0,st)))return why;
  if((why=tegu_compute_linked_list(owner,[linked privateFunctions],1,st)))return why;
  if((why=tegu_compute_linked_list(owner,[linked binaryFunctions],2,st)))return why;
  if(st->options.linked_count)st->options.linked=st->linked;
 }
 id libraries=TEGU_DESC_HAS(descriptor,insertLibraries)?[descriptor insertLibraries]:0;
 if(libraries&&[libraries count])return "dynamic libraries are not supported";
 libraries=TEGU_DESC_HAS(descriptor,preloadedLibraries)?[descriptor preloadedLibraries]:0;
 if(libraries&&[libraries count])return "dynamic libraries are not supported";
 NSUInteger depth=TEGU_DESC_HAS(descriptor,maxCallStackDepth)?[descriptor maxCallStackDepth]:0;
 if(depth>1) {
  if(depth>0xffffffffUL)return "maxCallStackDepth is out of range";
  st->options.max_call_stack_depth=(uint32_t)depth;
 }
 id stage=TEGU_DESC_HAS(descriptor,stageInputDescriptor)?[descriptor stageInputDescriptor]:0;
 if(stage) {
  id attributes=[stage attributes],layouts=[stage layouts];
  size_t attributeCount=0,layoutCount=0;uint32_t usedLayouts=0;
  for(NSUInteger i=0;attributes&&i<31;i++) {
   id a=[attributes objectAtIndexedSubscript:i];NSUInteger format=a?[a format]:0;if(!format)continue;
   NSUInteger offset=[a offset],buffer=[a bufferIndex];
   if(format>0xffffffffUL||offset>0xffffffffUL||buffer>=31)return "stage input attribute is out of range";
   st->attributes[attributeCount++]=(TeguAIRVertexAttribute){(uint32_t)i,(uint32_t)format,(uint32_t)offset,(uint32_t)buffer};
   usedLayouts|=1u<<buffer;
  }
  for(NSUInteger i=0;layouts&&i<31;i++) {
   id l=[layouts objectAtIndexedSubscript:i];if(!l)continue;
   NSUInteger stride=[l stride],step=[l stepFunction],rate=[l stepRate];
   if(!stride&&!(usedLayouts&(1u<<i)))continue;
   if(stride>0xffffffffUL||step>0xffffffffUL||rate>0xffffffffUL)return "stage input layout is out of range";
   st->layouts[layoutCount++]=(TeguAIRVertexLayout){(uint32_t)i,(uint32_t)stride,(uint32_t)step,(uint32_t)rate};
  }
  NSUInteger indexType=[stage indexType],indexBuffer=[stage indexBufferIndex];
  if(indexType>1||indexBuffer>=31)return "stage input index description is out of range";
  st->options.stage_attributes=attributeCount?st->attributes:0;st->options.stage_attribute_count=attributeCount;
  st->options.stage_layouts=layoutCount?st->layouts:0;st->options.stage_layout_count=layoutCount;
  st->options.stage_index_type=(uint32_t)indexType;st->options.stage_index_buffer_index=(uint32_t)indexBuffer;
  st->options.has_stage_input=1;
 }
 st->used=st->options.linked_count||st->options.has_stage_input||st->options.max_call_stack_depth;
 return 0;
}
