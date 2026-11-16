/* Tessellation runtime (MTLRenderCommandEncoder drawPatches): CPU tessellator + encoder state.
 *
 * What exists and what does not (be exact):
 *  - tessellator.h generates the vertices and triangle list of one patch (tested against an independent
 *    oracle, tests/tessellator_test.py).
 *  - tegu_tess_generate reads Metal's half-precision factor structs (step functions, scale, instance
 *    stride) and produces, for every patch x instance, one stream record per generated vertex plus a 32-bit
 *    index buffer.  The record is the contract of tegu_air_render_pipeline_create_tessellated
 *    (shader-coverage-opus, air_bridge.rs): the pipeline's vertex stage is the translator's EVALUATION stage and
 *    its non-buffer inputs are vertex attributes of the stream:
 *        location 0       uint    Metal patch index (patchStart / patch index buffer applied)
 *        location 1       uint    instance index, baseInstance included
 *        location 2 + g   uint4   control-point element indices, g < ceil(N/4): patch*N+k, or the value read
 *                                 from the control point index buffer at patch*N+k (unused lanes 0)
 *        location 30      float3  position_in_patch (quad: u,v,0; triangle: barycentrics)
 *    record layout: u32 patch; u32 instance; u32 cp[4*G]; f32 position[3]; stride 20+16*G.  The pipeline's own
 *    vertex buffers bind as usual (control points are pulled from them by the shader).
 *    tegu_tess_stream_layout fills the TeguAIRVertexAttribute / TeguAIRVertexLayout the pipeline creation must
 *    append so the Mali vertex fetch reads the stream from vertex buffer slot streamSlot.
 *  - The encoder keeps setTessellationFactorBuffer / setTessellationFactorScale state and validates every
 *    drawPatches / drawIndexedPatches variant (argument ranges, patch index buffer extent, control point index
 *    buffer extent, CPU-written indirect arguments verified again at execution, exactly like indirect draws).
 *  - A draw needs a tessellation pipeline.  tegu_pipeline_tess_info returns NULL until render_pipeline.inc
 *    creates such pipelines (it must call tegu_air_render_pipeline_create_tessellated with the appended stream
 *    attributes, read MTLFunction.patchType/patchControlPointCount, and fill TeguTessInfo from the
 *    descriptor): every drawPatches then fails the command with a logged reason.  Nothing here is claimed as
 *    working tessellation until a pipeline provides TeguTessInfo.  The host test drives the draw path with an
 *    injected info and a spy to prove the glue (stream/index buffers, slot save/restore, one indexed draw). */
#ifdef TEGU_GRAPHICS_BRIDGE
#include "tessellator.h"

struct TeguTessInfo {
 uint32_t domain;          /* TESS_TRIANGLE / TESS_QUAD, from the post-tessellation function's patch attribute */
 uint32_t controlPoints;   /* patch_control_point count N (1..32) */
 uint32_t partition;       /* MTLTessellationPartitionMode */
 uint32_t winding;         /* MTLWinding of tessellationOutputWindingOrder */
 uint32_t stepFunction;    /* MTLTessellationFactorStepFunction: 0 constant, 1 perPatch, 2 perInstance, 3 both */
 uint32_t controlPointIndexType; /* MTLTessellationControlPointIndexType: 0 none, 1 uint16, 2 uint32 */
 uint32_t streamSlot;      /* vertex buffer slot of the stream (proposed 30) */
 float maxFactor;          /* maxTessellationFactor (Metal default 16) */
};
#define TEGU_TESS_LOC_PATCH 0u
#define TEGU_TESS_LOC_INSTANCE 1u
#define TEGU_TESS_LOC_CONTROL_POINTS 2u
#define TEGU_TESS_LOC_POSITION 30u
static unsigned tegu_tess_groups(uint32_t controlPoints) { return (controlPoints+3u)/4u; }
static uint32_t tegu_tess_stride(uint32_t controlPoints) { return 20u+16u*tegu_tess_groups(controlPoints); }
/* The attributes/layout the pipeline must append for the stream (UInt=36, UInt4=39, Float3=30; MTLVertexStepFunctionPerVertex=1).
 * `attributes` holds 3+G entries, `layout` one. */
__attribute__((unused)) static unsigned tegu_tess_stream_layout(const struct TeguTessInfo *info,TeguAIRVertexAttribute *attributes,TeguAIRVertexLayout *layout) {
 unsigned g=tegu_tess_groups(info->controlPoints),n=0;
 attributes[n++]=(TeguAIRVertexAttribute){TEGU_TESS_LOC_PATCH,36,0,info->streamSlot};
 attributes[n++]=(TeguAIRVertexAttribute){TEGU_TESS_LOC_INSTANCE,36,4,info->streamSlot};
 for(unsigned i=0;i<g;i++)attributes[n++]=(TeguAIRVertexAttribute){TEGU_TESS_LOC_CONTROL_POINTS+i,39,8u+16u*i,info->streamSlot};
 attributes[n++]=(TeguAIRVertexAttribute){TEGU_TESS_LOC_POSITION,30,8u+16u*g,info->streamSlot};
 *layout=(TeguAIRVertexLayout){info->streamSlot,tegu_tess_stride(info->controlPoints),1,1};
 return n;
}
#define TEGU_TESS_MAX_VERTICES (1u<<22)
#define TEGU_TESS_MAX_INDICES (1u<<24)

#ifdef TEGU_FRONTEND_HOST
static const struct TeguTessInfo *tegu_tess_test_info;
#endif
/* Replace with the pipeline's own description when the bridge exports patch info. */
static const struct TeguTessInfo *tegu_pipeline_tess_info(TeguRenderPipeline *pipeline) {
 (void)pipeline;
#ifdef TEGU_FRONTEND_HOST
 return tegu_tess_test_info;
#else
 return 0;
#endif
}

/* 0 ok, -1 invalid input (out-of-range factor / control point index read), -2 output too large, -3 out of memory.
 * `factors` NULL means "no factor buffer": every patch uses factor 1 on all edges.  `cpIndex` NULL: element index
 * patch*N+k; otherwise cpIndex holds uint16/uint32 values (info->controlPointIndexType) at byte offset cpOffset. */
static int tegu_tess_generate(const struct TeguTessInfo *info,const unsigned char *factors,NSUInteger factorLength,
 NSUInteger factorOffset,NSUInteger instanceStride,float scale,NSUInteger patchStart,NSUInteger patchCount,
 const uint32_t *patchIndex,NSUInteger instanceCount,NSUInteger baseInstance,
 const unsigned char *cpIndex,NSUInteger cpLength,NSUInteger cpOffset,
 unsigned char **streamOut,uint32_t *vertexCountOut,uint32_t **indicesOut,uint32_t *indexCountOut) {
 *streamOut=0;*indicesOut=0;*vertexCountOut=*indexCountOut=0;
 TessConfig cfg={info->domain,info->partition,info->winding,info->maxFactor};
 size_t factorSize=tess_factors_size(info->domain);
 const uint32_t n=info->controlPoints,stride=tegu_tess_stride(n),groups=tegu_tess_groups(n);
 const size_t cpSize=info->controlPointIndexType==1?2:4;
 unsigned char *stream=0;uint32_t *indices=0;uint32_t nv=0,ni=0,vcap=0,icap=0;
 TessVertex *scratch=0;uint32_t scratchCap=0;
 int status=0;
 for(NSUInteger instance=0;instance<instanceCount&&!status;instance++) for(NSUInteger i=0;i<patchCount;i++) {
  uint32_t patch=patchIndex?patchIndex[patchStart+i]:(uint32_t)(patchStart+i);
  float edge[4]={1,1,1,1},inside[2]={1,1};
  if(factors) {
   NSUInteger p=(info->stepFunction&1u)?patch:0,k=(info->stepFunction&2u)?instance:0;
   if(k&&instanceStride>(~(NSUInteger)0)/k){status=-1;break;}
   NSUInteger at=factorOffset+k*instanceStride;
   if(at<factorOffset||p>(~(NSUInteger)0)/factorSize){status=-1;break;}
   NSUInteger tail=at+p*factorSize;
   if(tail<at||tail>factorLength||factorLength-tail<factorSize){status=-1;break;}
   tess_read_factors(info->domain,factors+tail,scale,edge,inside);
  }
  TessOut count={0,0,0,0,0,0,0,0};
  int r=tess_patch(&cfg,edge,inside,&count);
  if(r<0){status=-1;break;}
  if(r>0)continue; /* culled patch: no output */
  uint32_t cps[32];
  for(uint32_t c=0;c<n;c++) {
   uint64_t element=(uint64_t)patch*n+c;
   if(!cpIndex){cps[c]=(uint32_t)element;if(element>0xffffffffull){status=-1;break;}continue;}
   uint64_t at=(uint64_t)cpOffset+element*cpSize;
   if(at>cpLength||cpLength-at<cpSize){status=-1;break;}
   cps[c]=cpSize==2?(uint32_t)(cpIndex[at]|(cpIndex[at+1]<<8)):
    (uint32_t)(cpIndex[at]|(cpIndex[at+1]<<8)|(cpIndex[at+2]<<16)|((uint32_t)cpIndex[at+3]<<24));
  }
  if(status)break;
  if(count.vertex_count>TEGU_TESS_MAX_VERTICES-nv||count.index_count>TEGU_TESS_MAX_INDICES-ni){status=-2;break;}
  if(nv+count.vertex_count>vcap) {
   uint32_t want=vcap?vcap:1024;while(want<nv+count.vertex_count)want*=2;
   void *grown=realloc(stream,(NSUInteger)want*stride);if(!grown){status=-3;break;}
   stream=grown;vcap=want;
  }
  if(ni+count.index_count>icap) {
   uint32_t want=icap?icap:3072;while(want<ni+count.index_count)want*=2;
   void *grown=realloc(indices,(NSUInteger)want*sizeof(*indices));if(!grown){status=-3;break;}
   indices=grown;icap=want;
  }
  if(count.vertex_count>scratchCap) {
   void *grown=realloc(scratch,(NSUInteger)count.vertex_count*sizeof(*scratch));if(!grown){status=-3;break;}
   scratch=grown;scratchCap=count.vertex_count;
  }
  TessOut out={scratch,indices+ni,count.vertex_count,count.index_count,0,0,nv,0};
  r=tess_patch(&cfg,edge,inside,&out);
  if(r||out.vertex_count!=count.vertex_count||out.index_count!=count.index_count){status=-1;break;}
  uint32_t instanceIndex=(uint32_t)(instance+baseInstance);
  for(uint32_t v=0;v<count.vertex_count;v++) {
   unsigned char *record=stream+(NSUInteger)(nv+v)*stride;
   uint32_t *words=(uint32_t *)(void *)record;
   words[0]=patch;words[1]=instanceIndex;
   for(uint32_t c=0;c<4*groups;c++)words[2+c]=c<n?cps[c]:0;
   float *position=(float *)(void *)(record+8+16u*groups);
   position[0]=scratch[v].u;position[1]=scratch[v].v;position[2]=scratch[v].w;
  }
  nv+=count.vertex_count;ni+=count.index_count;
 }
 free(scratch);
 if(status||!ni){free(stream);free(indices);return status;}
 *streamOut=stream;*vertexCountOut=nv;*indicesOut=indices;*indexCountOut=ni;
 return 0;
}

@interface TeguRenderEncoder (APITessellation)
- (void)setTessellationFactorBuffer:(id)buffer offset:(NSUInteger)offset instanceStride:(NSUInteger)instanceStride;
- (void)setTessellationFactorScale:(float)scale;
- (void)drawPatches:(NSUInteger)controlPoints patchStart:(NSUInteger)patchStart patchCount:(NSUInteger)patchCount
 patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 instanceCount:(NSUInteger)instanceCount baseInstance:(NSUInteger)baseInstance;
- (void)drawPatches:(NSUInteger)controlPoints patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 indirectBuffer:(id)indirectBuffer indirectBufferOffset:(NSUInteger)indirectBufferOffset;
- (void)drawIndexedPatches:(NSUInteger)controlPoints patchStart:(NSUInteger)patchStart patchCount:(NSUInteger)patchCount
 patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 controlPointIndexBuffer:(id)controlPointIndexBuffer controlPointIndexBufferOffset:(NSUInteger)controlPointIndexBufferOffset
 instanceCount:(NSUInteger)instanceCount baseInstance:(NSUInteger)baseInstance;
- (void)drawIndexedPatches:(NSUInteger)controlPoints patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 controlPointIndexBuffer:(id)controlPointIndexBuffer controlPointIndexBufferOffset:(NSUInteger)controlPointIndexBufferOffset
 indirectBuffer:(id)indirectBuffer indirectBufferOffset:(NSUInteger)indirectBufferOffset;
@end

/* Common body of drawPatches / drawIndexedPatches: validates, generates, binds the stream, issues one indexed draw. */
static void tegu_tess_draw(TeguRenderEncoder *e,NSUInteger controlPoints,NSUInteger patchStart,NSUInteger patchCount,
 id patchIndexBuffer,NSUInteger patchIndexOffset,id cpIndexBuffer,NSUInteger cpIndexOffset,int indexed,
 NSUInteger instanceCount,NSUInteger baseInstance) {
 TeguCommand *command=e->command;
 if(e->inert)return;
 if(e->ended||[command teguCommitted]||!e->pipeline){TEGU_INVALID(command);return;}
 const struct TeguTessInfo *info=tegu_pipeline_tess_info(e->pipeline);
 if(!info){TEGU_API_REFUSE(command,"tessellation_pipeline");return;}
 /* Metal: drawPatches needs a pipeline without control point indices, drawIndexedPatches one with them. */
 if(controlPoints<1||controlPoints>32||controlPoints!=info->controlPoints||info->streamSlot>=31||
    !!info->controlPointIndexType!=!!indexed||info->controlPointIndexType>2||
    patchStart>0xffffffffUL||patchCount>0xffffffffUL||instanceCount>0xffffffffUL||baseInstance>0xffffffffUL){
  TEGU_INVALID(command);return;
 }
 const uint32_t *patchIndex=0;
 TeguBuffer *pib=(TeguBuffer *)patchIndexBuffer;
 if(patchIndexBuffer) {
  if(!tegu_api_class_is(patchIndexBuffer,"TeguBuffer",0)||pib->owner!=command->queue->owner||patchIndexOffset%4||
     patchIndexOffset>pib->logical||patchStart+patchCount<patchStart||
     (patchStart+patchCount)>(pib->logical-patchIndexOffset)/4u){TEGU_INVALID(command);return;}
  const unsigned char *bytes=(const unsigned char *)tegu_buffer_bytes(pib);
  if(!bytes){TEGU_INVALID(command);return;}
  patchIndex=(const uint32_t *)(void *)(bytes+patchIndexOffset);
 }
 const unsigned char *cpBytes=0;NSUInteger cpLength=0;
 if(indexed) {
  TeguBuffer *cpb=(TeguBuffer *)cpIndexBuffer;
  NSUInteger size=info->controlPointIndexType==1?2u:4u;
  if(!cpIndexBuffer||!tegu_api_class_is(cpIndexBuffer,"TeguBuffer",0)||cpb->owner!=command->queue->owner||cpIndexOffset%size||
     cpIndexOffset>cpb->logical){TEGU_INVALID(command);return;}
  cpBytes=(const unsigned char *)tegu_buffer_bytes(cpb);cpLength=cpb->logical;
  if(!cpBytes){TEGU_INVALID(command);return;}
 }
 if(!patchCount||!instanceCount)return; /* nothing to draw */
 const unsigned char *factors=0;NSUInteger factorLength=0;
 if(e->tessFactorBuffer) {
  factors=(const unsigned char *)tegu_buffer_bytes(e->tessFactorBuffer);
  factorLength=e->tessFactorBuffer->logical;
  if(!factors){TEGU_INVALID(command);return;}
 }
 unsigned char *stream;uint32_t nv,*indices,ni;
 int status=tegu_tess_generate(info,factors,factorLength,e->tessFactorOffset,e->tessFactorStride,1.0f+e->tessFactorScaleM1,
  patchStart,patchCount,patchIndex,instanceCount,baseInstance,cpBytes,cpLength,cpIndexOffset,&stream,&nv,&indices,&ni);
 if(status){TEGU_API_REFUSE(command,status==-2?"tessellation_output_too_large":"tessellation_input");return;}
 if(!ni)return; /* every patch culled */
 TeguComputeDevice *device=command->queue->owner;
 TeguBuffer *vertexStream=[device newBufferWithBytes:stream length:(NSUInteger)nv*tegu_tess_stride(info->controlPoints) options:0];
 TeguBuffer *index=[device newBufferWithBytes:indices length:(NSUInteger)ni*sizeof(*indices) options:0];
 free(stream);free(indices);
 if(!vertexStream||!index){[vertexStream release];[index release];TEGU_INVALID(command);return;}
 unsigned slot=info->streamSlot;
 TeguBuffer *saved=[e->vertexBuffers[slot] retain];
 NSUInteger savedOffset=e->vertexOffsets[slot],savedBase=e->vertexBytesBase[slot],savedLength=e->vertexBytesLength[slot];
 [e setVertexBuffer:vertexStream offset:0 atIndex:slot];
 /* baseInstance is already in the stream's instance attribute; the draw itself is one instance. */
 [e drawIndexedPrimitives:3 indexCount:ni indexType:1 indexBuffer:index indexBufferOffset:0
  instanceCount:1 baseVertex:0 baseInstance:0];
 [e setVertexBuffer:saved offset:savedOffset atIndex:slot];
 e->vertexBytesBase[slot]=savedBase;e->vertexBytesLength[slot]=savedLength;
 [saved release];[vertexStream release];[index release];
}

/* MTLDrawPatchIndirectArguments {patchCount, instanceCount, patchStart, baseInstance}.  CPU-written arguments are read now and
 * verified again before the command runs; GPU-produced ones have no encode-time value. */
static int tegu_tess_indirect_args(TeguRenderEncoder *e,id indirectBuffer,NSUInteger indirectBufferOffset,uint32_t args[4]) {
 TeguCommand *command=e->command;
 if(e->ended||[command teguCommitted]||!indirectBuffer||!tegu_api_class_is(indirectBuffer,"TeguBuffer",0)||
    ((TeguBuffer *)indirectBuffer)->owner!=command->queue->owner||indirectBufferOffset%4||
    indirectBufferOffset>((TeguBuffer *)indirectBuffer)->logical||
    ((TeguBuffer *)indirectBuffer)->logical-indirectBufferOffset<16u){TEGU_INVALID(command);return -1;}
 TeguBuffer *buffer=(TeguBuffer *)indirectBuffer;
 if(!e->pipeline||!tegu_pipeline_tess_info(e->pipeline)){TEGU_API_REFUSE(command,"tessellation_pipeline");return -1;}
 if(tegu_indirect_args_gpu_written(command,buffer)){TEGU_API_REFUSE(command,"indirect_draw_gpu_arguments");return -1;}
 const unsigned char *bytes=(const unsigned char *)tegu_buffer_bytes(buffer);
 struct TeguIndirectVerify *node=bytes?calloc(1,sizeof(*node)):0;
 if(!node){TEGU_INVALID(command);return -1;}
 __builtin_memcpy(node->expected,bytes+indirectBufferOffset,16u);
 node->buffer=[buffer retain];node->offset=indirectBufferOffset;node->words=4;
 node->next=(struct TeguIndirectVerify *)command->indirectVerifies;command->indirectVerifies=node;
 __builtin_memcpy(args,node->expected,16u);
 return 0;
}

@implementation TeguRenderEncoder (APITessellation)
- (void)setTessellationFactorBuffer:(id)buffer offset:(NSUInteger)offset instanceStride:(NSUInteger)instanceStride {
 if(inert)return;
 if(ended||[command teguCommitted]||offset%4||instanceStride%4||
    (buffer&&(!tegu_api_class_is(buffer,"TeguBuffer",0)||((TeguBuffer *)buffer)->owner!=command->queue->owner||
     offset>((TeguBuffer *)buffer)->logical))){TEGU_INVALID(command);return;}
 [buffer retain];[tessFactorBuffer release];
 tessFactorBuffer=(TeguBuffer *)buffer;tessFactorOffset=buffer?offset:0;tessFactorStride=buffer?instanceStride:0;
}
- (void)setTessellationFactorScale:(float)scale {
 if(inert)return;
 if(ended||[command teguCommitted]||!(scale>=0.0f)){TEGU_INVALID(command);return;} /* negative or NaN */
 tessFactorScaleM1=scale-1.0f;
}
- (void)drawPatches:(NSUInteger)controlPoints patchStart:(NSUInteger)patchStart patchCount:(NSUInteger)patchCount
 patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 instanceCount:(NSUInteger)instanceCount baseInstance:(NSUInteger)baseInstance {
 tegu_tess_draw(self,controlPoints,patchStart,patchCount,patchIndexBuffer,patchIndexBufferOffset,0,0,0,instanceCount,baseInstance);
}
- (void)drawPatches:(NSUInteger)controlPoints patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 indirectBuffer:(id)indirectBuffer indirectBufferOffset:(NSUInteger)indirectBufferOffset {
 if(inert)return;
 uint32_t a[4];
 if(tegu_tess_indirect_args(self,indirectBuffer,indirectBufferOffset,a))return;
 tegu_tess_draw(self,controlPoints,a[2],a[0],patchIndexBuffer,patchIndexBufferOffset,0,0,0,a[1],a[3]);
}
- (void)drawIndexedPatches:(NSUInteger)controlPoints patchStart:(NSUInteger)patchStart patchCount:(NSUInteger)patchCount
 patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 controlPointIndexBuffer:(id)controlPointIndexBuffer controlPointIndexBufferOffset:(NSUInteger)controlPointIndexBufferOffset
 instanceCount:(NSUInteger)instanceCount baseInstance:(NSUInteger)baseInstance {
 tegu_tess_draw(self,controlPoints,patchStart,patchCount,patchIndexBuffer,patchIndexBufferOffset,controlPointIndexBuffer,
  controlPointIndexBufferOffset,1,instanceCount,baseInstance);
}
- (void)drawIndexedPatches:(NSUInteger)controlPoints patchIndexBuffer:(id)patchIndexBuffer patchIndexBufferOffset:(NSUInteger)patchIndexBufferOffset
 controlPointIndexBuffer:(id)controlPointIndexBuffer controlPointIndexBufferOffset:(NSUInteger)controlPointIndexBufferOffset
 indirectBuffer:(id)indirectBuffer indirectBufferOffset:(NSUInteger)indirectBufferOffset {
 if(inert)return;
 uint32_t a[4];
 if(tegu_tess_indirect_args(self,indirectBuffer,indirectBufferOffset,a))return;
 tegu_tess_draw(self,controlPoints,a[2],a[0],patchIndexBuffer,patchIndexBufferOffset,controlPointIndexBuffer,
  controlPointIndexBufferOffset,1,a[1],a[3]);
}
@end
#endif
