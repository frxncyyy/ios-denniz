/* MTLParallelRenderCommandEncoder.
 *
 * Metal lets several threads encode one render pass: -renderCommandEncoder
 * returns sub-encoders that may be used concurrently and are executed in the
 * order they were created.  This frontend executes a pass as an ordered list of
 * jobs from ONE TeguRenderEncoder, so the parallel encoder:
 *   - creates the real pass encoder (load/store actions of the descriptor) when
 *     it is created;
 *   - hands out recorder sub-encoders.  Each call is captured as a plain record
 *     (opcode, retained objects, scalar arguments, copied byte data) in the
 *     sub-encoder's private list, so concurrent threads never touch the real
 *     encoder.  Records, not blocks: no Blocks-runtime link dependency for the
 *     host harnesses that build this frontend;
 *   - at -endEncoding replays every sub-encoder's list into the real encoder in
 *     creation order, then ends it.  Every state a sub-encoder changed (viewport,
 *     scissor, cull, winding, fill, depth/stencil state) is restored to the
 *     pass default before the next one starts, so a sub-encoder never inherits
 *     another's fixed-function state, exactly like Metal.
 * Failure semantics are the real encoder's: a refused draw poisons the command
 * buffer when it is replayed.  Tests: metal/tegu/api_surface_host_test.inc. */
#ifdef TEGU_GRAPHICS_BRIDGE

enum {
 TEGU_PAR_PIPELINE=1,TEGU_PAR_VIEWPORT_OP,TEGU_PAR_SCISSOR_OP,TEGU_PAR_CULL_OP,TEGU_PAR_WINDING_OP,TEGU_PAR_FILL_OP,
 TEGU_PAR_DEPTHCLIP_OP,TEGU_PAR_DEPTHBIAS_OP,TEGU_PAR_BLEND_OP,TEGU_PAR_DSS_OP,TEGU_PAR_STENCILREF_OP,TEGU_PAR_STENCILREF2_OP,
 TEGU_PAR_VISIBILITY_OP,TEGU_PAR_VBUF,TEGU_PAR_FBUF,TEGU_PAR_VBUFOFF,TEGU_PAR_FBUFOFF,TEGU_PAR_VBYTES,TEGU_PAR_FBYTES,
 TEGU_PAR_VTEX,TEGU_PAR_FTEX,TEGU_PAR_VSAMP,TEGU_PAR_FSAMP,TEGU_PAR_VSAMPLOD,TEGU_PAR_FSAMPLOD,
 TEGU_PAR_DRAW,TEGU_PAR_DRAW_INST,TEGU_PAR_DRAW_INST_BASE,TEGU_PAR_DRAWX,TEGU_PAR_DRAWX_INST,TEGU_PAR_DRAWX_FULL,
 TEGU_PAR_DRAW_IND,TEGU_PAR_DRAWX_IND,
 TEGU_PAR_USERES,TEGU_PAR_USERES_ST,TEGU_PAR_USEHEAP,TEGU_PAR_UPDFENCE,TEGU_PAR_WAITFENCE,TEGU_PAR_MEMBAR
};
enum { TEGU_PAR_VIEWPORT=1,TEGU_PAR_SCISSOR=2,TEGU_PAR_CULL=4,TEGU_PAR_WINDING=8,TEGU_PAR_FILL=16,TEGU_PAR_DEPTHSTENCIL=32 };
struct TeguParallelRecord { unsigned op;id obj,obj2;NSUInteger u[6];double d[6];void *owned; };

@interface TeguRenderSubEncoder : NSObject {
@public TeguCommand *command;struct TeguParallelRecord *ops;unsigned count,capacity,ended,dirty;id debugLabel;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
- (void)endEncoding;
@end
@interface TeguParallelRenderEncoder : NSObject {
@public TeguCommand *command;TeguRenderEncoder *main;TeguRenderSubEncoder **subs;unsigned subCount,subCapacity,ended;id debugLabel;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
- (id)renderCommandEncoder;
- (void)setColorStoreAction:(NSUInteger)action atIndex:(NSUInteger)index;
- (void)setDepthStoreAction:(NSUInteger)action;
- (void)setStencilStoreAction:(NSUInteger)action;
- (void)setColorStoreActionOptions:(NSUInteger)options atIndex:(NSUInteger)index;
- (void)setDepthStoreActionOptions:(NSUInteger)options;
- (void)setStencilStoreActionOptions:(NSUInteger)options;
- (void)insertDebugSignpost:(id)string;
- (void)pushDebugGroup:(id)string;
- (void)popDebugGroup;
- (void)endEncoding;
@end

static void tegu_parallel_record_release(struct TeguParallelRecord *r) {
 [r->obj release];[r->obj2 release];free(r->owned);r->obj=r->obj2=0;r->owned=0;
}

@implementation TeguRenderSubEncoder
- (id)device { return command->queue->owner; }
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id copy=[value copy];[debugLabel release];debugLabel=copy; }
/* Append one record (objects retained).  dirty: fixed-function state bits the call changes. */
- (struct TeguParallelRecord *)next:(unsigned)op dirty:(unsigned)bits {
 if(ended||[command teguCommitted]){TEGU_INVALID(command);return 0;}
 if(count==capacity) {
  unsigned grown=capacity?capacity*2:32;
  struct TeguParallelRecord *bigger=realloc(ops,grown*sizeof(*ops));
  if(!bigger){TEGU_INVALID(command);return 0;}
  ops=bigger;capacity=grown;
 }
 struct TeguParallelRecord *r=&ops[count++];
 __builtin_memset(r,0,sizeof(*r));r->op=op;dirty|=bits;
 return r;
}
- (void)endEncoding {
 if(ended){TEGU_INVALID(command);return;}
 ended=1;
}
- (void)dealloc {
 for(unsigned i=0;i<count;i++)tegu_parallel_record_release(&ops[i]);
 free(ops);[debugLabel release];[command release];[super dealloc];
}
#define TEGU_PAR_NEW(opcode,bits) struct TeguParallelRecord *r=[self next:opcode dirty:bits];if(!r)return;
#define TEGU_PAR_OBJ(a,b) r->obj=[(id)(a) retain];r->obj2=[(id)(b) retain];
/* ---- pipeline and fixed-function state ---- */
- (void)setRenderPipelineState:(TeguRenderPipeline *)p { TEGU_PAR_NEW(TEGU_PAR_PIPELINE,0) TEGU_PAR_OBJ(p,0) }
- (void)setViewport:(TeguViewport)v {
 TEGU_PAR_NEW(TEGU_PAR_VIEWPORT_OP,0) r->d[0]=v.originX;r->d[1]=v.originY;r->d[2]=v.width;r->d[3]=v.height;r->d[4]=v.znear;r->d[5]=v.zfar;dirty|=TEGU_PAR_VIEWPORT;
}
- (void)setViewports:(const TeguViewport *)v count:(NSUInteger)n {
 if(!v||n!=1){TEGU_API_REFUSE(command,"multiple_viewports");return;}
 [self setViewport:v[0]];
}
- (void)setScissorRect:(TeguScissorRect)s {
 TEGU_PAR_NEW(TEGU_PAR_SCISSOR_OP,0) r->u[0]=s.x;r->u[1]=s.y;r->u[2]=s.width;r->u[3]=s.height;dirty|=TEGU_PAR_SCISSOR;
}
- (void)setScissorRects:(const TeguScissorRect *)s count:(NSUInteger)n {
 if(!s||n!=1){TEGU_API_REFUSE(command,"multiple_scissors");return;}
 [self setScissorRect:s[0]];
}
- (void)setCullMode:(NSUInteger)m { TEGU_PAR_NEW(TEGU_PAR_CULL_OP,0) r->u[0]=m;dirty|=TEGU_PAR_CULL; }
- (void)setFrontFacingWinding:(NSUInteger)w { TEGU_PAR_NEW(TEGU_PAR_WINDING_OP,0) r->u[0]=w;dirty|=TEGU_PAR_WINDING; }
- (void)setTriangleFillMode:(NSUInteger)m { TEGU_PAR_NEW(TEGU_PAR_FILL_OP,0) r->u[0]=m;dirty|=TEGU_PAR_FILL; }
- (void)setDepthClipMode:(NSUInteger)m { TEGU_PAR_NEW(TEGU_PAR_DEPTHCLIP_OP,0) r->u[0]=m; }
- (void)setDepthBias:(float)bias slopeScale:(float)slope clamp:(float)clamp {
 TEGU_PAR_NEW(TEGU_PAR_DEPTHBIAS_OP,0) r->d[0]=bias;r->d[1]=slope;r->d[2]=clamp;
}
- (void)setBlendColorRed:(float)red green:(float)green blue:(float)blue alpha:(float)alpha {
 TEGU_PAR_NEW(TEGU_PAR_BLEND_OP,0) r->d[0]=red;r->d[1]=green;r->d[2]=blue;r->d[3]=alpha;
}
- (void)setDepthStencilState:(id)s { TEGU_PAR_NEW(TEGU_PAR_DSS_OP,0) TEGU_PAR_OBJ(s,0) dirty|=TEGU_PAR_DEPTHSTENCIL; }
- (void)setStencilReferenceValue:(uint32_t)v { TEGU_PAR_NEW(TEGU_PAR_STENCILREF_OP,0) r->u[0]=v; }
- (void)setStencilFrontReferenceValue:(uint32_t)f backReferenceValue:(uint32_t)b { TEGU_PAR_NEW(TEGU_PAR_STENCILREF2_OP,0) r->u[0]=f;r->u[1]=b; }
- (void)setVisibilityResultMode:(NSUInteger)m offset:(NSUInteger)o { TEGU_PAR_NEW(TEGU_PAR_VISIBILITY_OP,0) r->u[0]=m;r->u[1]=o; }
/* ---- resources ---- */
- (void)setVertexBuffer:(TeguBuffer *)b offset:(NSUInteger)o atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_VBUF,0) TEGU_PAR_OBJ(b,0) r->u[0]=o;r->u[1]=i; }
- (void)setFragmentBuffer:(TeguBuffer *)b offset:(NSUInteger)o atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_FBUF,0) TEGU_PAR_OBJ(b,0) r->u[0]=o;r->u[1]=i; }
- (void)setVertexBufferOffset:(NSUInteger)o atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_VBUFOFF,0) r->u[0]=o;r->u[1]=i; }
- (void)setFragmentBufferOffset:(NSUInteger)o atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_FBUFOFF,0) r->u[0]=o;r->u[1]=i; }
/* Byte data is copied at record time (Metal's setBytes contract); the copy belongs to the record. */
- (void)setVertexBytes:(const void *)bytes length:(NSUInteger)n atIndex:(NSUInteger)i {
 if((n&&!bytes)||n>4096){TEGU_INVALID(command);return;}
 TEGU_PAR_NEW(TEGU_PAR_VBYTES,0)
 r->u[1]=i;
 if(n){r->owned=malloc(n);if(!r->owned){TEGU_INVALID(command);return;}memcpy(r->owned,bytes,n);r->u[0]=n;}
}
- (void)setFragmentBytes:(const void *)bytes length:(NSUInteger)n atIndex:(NSUInteger)i {
 if((n&&!bytes)||n>4096){TEGU_INVALID(command);return;}
 TEGU_PAR_NEW(TEGU_PAR_FBYTES,0)
 r->u[1]=i;
 if(n){r->owned=malloc(n);if(!r->owned){TEGU_INVALID(command);return;}memcpy(r->owned,bytes,n);r->u[0]=n;}
}
- (void)setVertexBuffers:(TeguBuffer *const *)b offsets:(const NSUInteger *)o withRange:(TeguRange)r {
 if(r.location>31||r.length>31-r.location||(r.length&&(!b||!o))){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<r.length;k++)[self setVertexBuffer:b[k] offset:o[k] atIndex:r.location+k];
}
- (void)setFragmentBuffers:(TeguBuffer *const *)b offsets:(const NSUInteger *)o withRange:(TeguRange)r {
 if(r.location>31||r.length>31-r.location||(r.length&&(!b||!o))){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<r.length;k++)[self setFragmentBuffer:b[k] offset:o[k] atIndex:r.location+k];
}
- (void)setVertexTexture:(TeguTexture *)t atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_VTEX,0) TEGU_PAR_OBJ(t,0) r->u[0]=i; }
- (void)setFragmentTexture:(TeguTexture *)t atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_FTEX,0) TEGU_PAR_OBJ(t,0) r->u[0]=i; }
- (void)setVertexTextures:(TeguTexture *const *)t withRange:(TeguRange)r {
 if(r.location>31||r.length>31-r.location||(r.length&&!t)){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<r.length;k++)[self setVertexTexture:t[k] atIndex:r.location+k];
}
- (void)setFragmentTextures:(TeguTexture *const *)t withRange:(TeguRange)r {
 if(r.location>31||r.length>31-r.location||(r.length&&!t)){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<r.length;k++)[self setFragmentTexture:t[k] atIndex:r.location+k];
}
- (void)setVertexSamplerState:(TeguSampler *)s atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_VSAMP,0) TEGU_PAR_OBJ(s,0) r->u[0]=i; }
- (void)setFragmentSamplerState:(TeguSampler *)s atIndex:(NSUInteger)i { TEGU_PAR_NEW(TEGU_PAR_FSAMP,0) TEGU_PAR_OBJ(s,0) r->u[0]=i; }
- (void)setVertexSamplerState:(id)s lodMinClamp:(float)a lodMaxClamp:(float)b atIndex:(NSUInteger)i {
 TEGU_PAR_NEW(TEGU_PAR_VSAMPLOD,0) TEGU_PAR_OBJ(s,0) r->d[0]=a;r->d[1]=b;r->u[0]=i;
}
- (void)setFragmentSamplerState:(id)s lodMinClamp:(float)a lodMaxClamp:(float)b atIndex:(NSUInteger)i {
 TEGU_PAR_NEW(TEGU_PAR_FSAMPLOD,0) TEGU_PAR_OBJ(s,0) r->d[0]=a;r->d[1]=b;r->u[0]=i;
}
- (void)setVertexSamplerStates:(TeguSampler *const *)s withRange:(TeguRange)r {
 if(r.location>16||r.length>16-r.location||(r.length&&!s)){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<r.length;k++)[self setVertexSamplerState:s[k] atIndex:r.location+k];
}
- (void)setFragmentSamplerStates:(TeguSampler *const *)s withRange:(TeguRange)r {
 if(r.location>16||r.length>16-r.location||(r.length&&!s)){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<r.length;k++)[self setFragmentSamplerState:s[k] atIndex:r.location+k];
}
- (void)setFragmentTexture:(TeguTexture *)t atTextureIndex:(NSUInteger)ti samplerState:(TeguSampler *)s atSamplerIndex:(NSUInteger)si {
 [self setFragmentTexture:t atIndex:ti];[self setFragmentSamplerState:s atIndex:si];
}
/* ---- draws ---- */
- (void)drawPrimitives:(NSUInteger)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n {
 TEGU_PAR_NEW(TEGU_PAR_DRAW,0) r->u[0]=t;r->u[1]=s;r->u[2]=n;
}
- (void)drawPrimitives:(NSUInteger)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n instanceCount:(NSUInteger)c {
 TEGU_PAR_NEW(TEGU_PAR_DRAW_INST,0) r->u[0]=t;r->u[1]=s;r->u[2]=n;r->u[3]=c;
}
- (void)drawPrimitives:(NSUInteger)t vertexStart:(NSUInteger)s vertexCount:(NSUInteger)n instanceCount:(NSUInteger)c baseInstance:(NSUInteger)bi {
 TEGU_PAR_NEW(TEGU_PAR_DRAW_INST_BASE,0) r->u[0]=t;r->u[1]=s;r->u[2]=n;r->u[3]=c;r->u[4]=bi;
}
- (void)drawIndexedPrimitives:(NSUInteger)t indexCount:(NSUInteger)n indexType:(NSUInteger)it indexBuffer:(TeguBuffer *)b indexBufferOffset:(NSUInteger)o {
 TEGU_PAR_NEW(TEGU_PAR_DRAWX,0) TEGU_PAR_OBJ(b,0) r->u[0]=t;r->u[1]=n;r->u[2]=it;r->u[3]=o;
}
- (void)drawIndexedPrimitives:(NSUInteger)t indexCount:(NSUInteger)n indexType:(NSUInteger)it indexBuffer:(TeguBuffer *)b indexBufferOffset:(NSUInteger)o instanceCount:(NSUInteger)c {
 TEGU_PAR_NEW(TEGU_PAR_DRAWX_INST,0) TEGU_PAR_OBJ(b,0) r->u[0]=t;r->u[1]=n;r->u[2]=it;r->u[3]=o;r->u[4]=c;
}
- (void)drawIndexedPrimitives:(NSUInteger)t indexCount:(NSUInteger)n indexType:(NSUInteger)it indexBuffer:(TeguBuffer *)b indexBufferOffset:(NSUInteger)o instanceCount:(NSUInteger)c baseVertex:(long)bv baseInstance:(NSUInteger)bi {
 TEGU_PAR_NEW(TEGU_PAR_DRAWX_FULL,0) TEGU_PAR_OBJ(b,0) r->u[0]=t;r->u[1]=n;r->u[2]=it;r->u[3]=o;r->u[4]=c;r->u[5]=bi;r->d[0]=(double)bv;
}
- (void)drawPrimitives:(NSUInteger)t indirectBuffer:(TeguBuffer *)b indirectBufferOffset:(NSUInteger)o {
 TEGU_PAR_NEW(TEGU_PAR_DRAW_IND,0) TEGU_PAR_OBJ(b,0) r->u[0]=t;r->u[1]=o;
}
- (void)drawIndexedPrimitives:(NSUInteger)t indexType:(NSUInteger)it indexBuffer:(TeguBuffer *)b indexBufferOffset:(NSUInteger)o indirectBuffer:(TeguBuffer *)ib indirectBufferOffset:(NSUInteger)io {
 TEGU_PAR_NEW(TEGU_PAR_DRAWX_IND,0) TEGU_PAR_OBJ(b,ib) r->u[0]=t;r->u[1]=it;r->u[2]=o;r->u[3]=io;
}
/* ---- residency, fences, barriers, debug ---- */
- (void)useResource:(id)res usage:(NSUInteger)u { TEGU_PAR_NEW(TEGU_PAR_USERES,0) TEGU_PAR_OBJ(res,0) r->u[0]=u; }
- (void)useResource:(id)res usage:(NSUInteger)u stages:(NSUInteger)s { TEGU_PAR_NEW(TEGU_PAR_USERES_ST,0) TEGU_PAR_OBJ(res,0) r->u[0]=u;r->u[1]=s; }
- (void)useHeap:(id)h { TEGU_PAR_NEW(TEGU_PAR_USEHEAP,0) TEGU_PAR_OBJ(h,0) }
- (void)useResources:(id const *)rs count:(NSUInteger)n usage:(NSUInteger)u {
 if(n&&!rs){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<n;k++)[self useResource:rs[k] usage:u];
}
- (void)useHeaps:(id const *)hs count:(NSUInteger)n {
 if(n&&!hs){TEGU_INVALID(command);return;}
 for(NSUInteger k=0;k<n;k++)[self useHeap:hs[k]];
}
- (void)updateFence:(id)f afterStages:(NSUInteger)s { TEGU_PAR_NEW(TEGU_PAR_UPDFENCE,0) TEGU_PAR_OBJ(f,0) r->u[0]=s; }
- (void)waitForFence:(id)f beforeStages:(NSUInteger)s { TEGU_PAR_NEW(TEGU_PAR_WAITFENCE,0) TEGU_PAR_OBJ(f,0) r->u[0]=s; }
- (void)memoryBarrierWithScope:(NSUInteger)sc afterStages:(NSUInteger)a beforeStages:(NSUInteger)b {
 TEGU_PAR_NEW(TEGU_PAR_MEMBAR,0) r->u[0]=sc;r->u[1]=a;r->u[2]=b;
}
- (void)insertDebugSignpost:(id)string { (void)string; }
- (void)pushDebugGroup:(id)string { (void)string; }
- (void)popDebugGroup {}
#undef TEGU_PAR_NEW
#undef TEGU_PAR_OBJ
@end

/* Apply one record to the real encoder. */
static void tegu_parallel_apply(TeguRenderEncoder *e,const struct TeguParallelRecord *r) {
 switch(r->op) {
  case TEGU_PAR_PIPELINE:[e setRenderPipelineState:(TeguRenderPipeline *)r->obj];break;
  case TEGU_PAR_VIEWPORT_OP:[e setViewport:(TeguViewport){r->d[0],r->d[1],r->d[2],r->d[3],r->d[4],r->d[5]}];break;
  case TEGU_PAR_SCISSOR_OP:[e setScissorRect:(TeguScissorRect){r->u[0],r->u[1],r->u[2],r->u[3]}];break;
  case TEGU_PAR_CULL_OP:[e setCullMode:r->u[0]];break;
  case TEGU_PAR_WINDING_OP:[e setFrontFacingWinding:r->u[0]];break;
  case TEGU_PAR_FILL_OP:[e setTriangleFillMode:r->u[0]];break;
  case TEGU_PAR_DEPTHCLIP_OP:[e setDepthClipMode:r->u[0]];break;
  case TEGU_PAR_DEPTHBIAS_OP:[e setDepthBias:(float)r->d[0] slopeScale:(float)r->d[1] clamp:(float)r->d[2]];break;
  case TEGU_PAR_BLEND_OP:[e setBlendColorRed:(float)r->d[0] green:(float)r->d[1] blue:(float)r->d[2] alpha:(float)r->d[3]];break;
  case TEGU_PAR_DSS_OP:[e setDepthStencilState:r->obj];break;
  case TEGU_PAR_STENCILREF_OP:[e setStencilReferenceValue:(uint32_t)r->u[0]];break;
  case TEGU_PAR_STENCILREF2_OP:[e setStencilFrontReferenceValue:(uint32_t)r->u[0] backReferenceValue:(uint32_t)r->u[1]];break;
  case TEGU_PAR_VISIBILITY_OP:[e setVisibilityResultMode:r->u[0] offset:r->u[1]];break;
  case TEGU_PAR_VBUF:[e setVertexBuffer:(TeguBuffer *)r->obj offset:r->u[0] atIndex:r->u[1]];break;
  case TEGU_PAR_FBUF:[e setFragmentBuffer:(TeguBuffer *)r->obj offset:r->u[0] atIndex:r->u[1]];break;
  case TEGU_PAR_VBUFOFF:[e setVertexBufferOffset:r->u[0] atIndex:r->u[1]];break;
  case TEGU_PAR_FBUFOFF:[e setFragmentBufferOffset:r->u[0] atIndex:r->u[1]];break;
  case TEGU_PAR_VBYTES:[e setVertexBytes:r->owned length:r->u[0] atIndex:r->u[1]];break;
  case TEGU_PAR_FBYTES:[e setFragmentBytes:r->owned length:r->u[0] atIndex:r->u[1]];break;
  case TEGU_PAR_VTEX:[e setVertexTexture:(TeguTexture *)r->obj atIndex:r->u[0]];break;
  case TEGU_PAR_FTEX:[e setFragmentTexture:(TeguTexture *)r->obj atIndex:r->u[0]];break;
  case TEGU_PAR_VSAMP:[e setVertexSamplerState:(TeguSampler *)r->obj atIndex:r->u[0]];break;
  case TEGU_PAR_FSAMP:[e setFragmentSamplerState:(TeguSampler *)r->obj atIndex:r->u[0]];break;
  case TEGU_PAR_VSAMPLOD:[e setVertexSamplerState:r->obj lodMinClamp:(float)r->d[0] lodMaxClamp:(float)r->d[1] atIndex:r->u[0]];break;
  case TEGU_PAR_FSAMPLOD:[e setFragmentSamplerState:r->obj lodMinClamp:(float)r->d[0] lodMaxClamp:(float)r->d[1] atIndex:r->u[0]];break;
  case TEGU_PAR_DRAW:[e drawPrimitives:r->u[0] vertexStart:r->u[1] vertexCount:r->u[2]];break;
  case TEGU_PAR_DRAW_INST:[e drawPrimitives:r->u[0] vertexStart:r->u[1] vertexCount:r->u[2] instanceCount:r->u[3]];break;
  case TEGU_PAR_DRAW_INST_BASE:[e drawPrimitives:r->u[0] vertexStart:r->u[1] vertexCount:r->u[2] instanceCount:r->u[3] baseInstance:r->u[4]];break;
  case TEGU_PAR_DRAWX:[e drawIndexedPrimitives:r->u[0] indexCount:r->u[1] indexType:r->u[2] indexBuffer:(TeguBuffer *)r->obj indexBufferOffset:r->u[3]];break;
  case TEGU_PAR_DRAWX_INST:[e drawIndexedPrimitives:r->u[0] indexCount:r->u[1] indexType:r->u[2] indexBuffer:(TeguBuffer *)r->obj indexBufferOffset:r->u[3] instanceCount:r->u[4]];break;
  case TEGU_PAR_DRAWX_FULL:[e drawIndexedPrimitives:r->u[0] indexCount:r->u[1] indexType:r->u[2] indexBuffer:(TeguBuffer *)r->obj indexBufferOffset:r->u[3] instanceCount:r->u[4] baseVertex:(long)r->d[0] baseInstance:r->u[5]];break;
  case TEGU_PAR_DRAW_IND:[e drawPrimitives:r->u[0] indirectBuffer:(TeguBuffer *)r->obj indirectBufferOffset:r->u[1]];break;
  case TEGU_PAR_DRAWX_IND:[e drawIndexedPrimitives:r->u[0] indexType:r->u[1] indexBuffer:(TeguBuffer *)r->obj indexBufferOffset:r->u[2] indirectBuffer:(TeguBuffer *)r->obj2 indirectBufferOffset:r->u[3]];break;
  case TEGU_PAR_USERES:[e useResource:r->obj usage:r->u[0]];break;
  case TEGU_PAR_USERES_ST:[e useResource:r->obj usage:r->u[0] stages:r->u[1]];break;
  case TEGU_PAR_USEHEAP:[e useHeap:r->obj];break;
  case TEGU_PAR_UPDFENCE:[e updateFence:r->obj afterStages:r->u[0]];break;
  case TEGU_PAR_WAITFENCE:[e waitForFence:r->obj beforeStages:r->u[0]];break;
  case TEGU_PAR_MEMBAR:[e memoryBarrierWithScope:r->u[0] afterStages:r->u[1] beforeStages:r->u[2]];break;
  default:break;
 }
}

/* Replay one recorded list; restore the pass defaults the sub-encoder changed. */
static void tegu_parallel_replay(TeguRenderEncoder *main,TeguRenderSubEncoder *sub) {
 for(unsigned i=0;i<sub->count;i++) {
  tegu_parallel_apply(main,&sub->ops[i]);
  tegu_parallel_record_release(&sub->ops[i]);
 }
 sub->count=0;
 if(main->inert||!main->target)return;
 double w=(double)main->target->textureWidth,h=(double)main->target->textureHeight;
 if(sub->dirty&TEGU_PAR_VIEWPORT)[main setViewport:(TeguViewport){0,0,w,h,0,1}];
 if(sub->dirty&TEGU_PAR_SCISSOR)[main setScissorRect:(TeguScissorRect){0,0,main->target->textureWidth,main->target->textureHeight}];
 if(sub->dirty&TEGU_PAR_CULL)[main setCullMode:0];
 if(sub->dirty&TEGU_PAR_WINDING)[main setFrontFacingWinding:0];
 if(sub->dirty&TEGU_PAR_FILL)[main setTriangleFillMode:0];
 if(sub->dirty&TEGU_PAR_DEPTHSTENCIL)[main setDepthStencilState:0];
 sub->dirty=0;
}

@implementation TeguParallelRenderEncoder
- (id)device { return command->queue->owner; }
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id copy=[value copy];[debugLabel release];debugLabel=copy; }
- (id)renderCommandEncoder {
 if(ended||[command teguCommitted]){TEGU_INVALID(command);return 0;}
 TeguRenderSubEncoder *sub=[[TeguRenderSubEncoder alloc] init];if(!sub){TEGU_INVALID(command);return 0;}
 sub->command=[command retain];
 if(subCount==subCapacity) {
  unsigned grown=subCapacity?subCapacity*2:8;
  TeguRenderSubEncoder **bigger=realloc(subs,grown*sizeof(*subs));
  if(!bigger){[sub release];TEGU_INVALID(command);return 0;}
  subs=bigger;subCapacity=grown;
 }
 subs[subCount++]=sub;
 return sub; /* owned by the list; the caller borrows it */
}
- (void)setColorStoreAction:(NSUInteger)a atIndex:(NSUInteger)i { if(!ended)[main setColorStoreAction:a atIndex:i]; }
- (void)setDepthStoreAction:(NSUInteger)a { if(!ended)[main setDepthStoreAction:a]; }
- (void)setStencilStoreAction:(NSUInteger)a { if(!ended)[main setStencilStoreAction:a]; }
- (void)setColorStoreActionOptions:(NSUInteger)o atIndex:(NSUInteger)i { if(!ended)[main setColorStoreActionOptions:o atIndex:i]; }
- (void)setDepthStoreActionOptions:(NSUInteger)o { if(!ended)[main setDepthStoreActionOptions:o]; }
- (void)setStencilStoreActionOptions:(NSUInteger)o { if(!ended)[main setStencilStoreActionOptions:o]; }
- (void)insertDebugSignpost:(id)string { (void)string; }
- (void)pushDebugGroup:(id)string { (void)string; }
- (void)popDebugGroup {}
- (void)endEncoding {
 if(ended){TEGU_INVALID(command);return;}
 ended=1;
 for(unsigned i=0;i<subCount;i++)if(!subs[i]->ended)TEGU_INVALID(command);
 for(unsigned i=0;i<subCount;i++)tegu_parallel_replay(main,subs[i]);
 [main endEncoding];
}
- (void)dealloc {
 if(!ended&&command)TEGU_INVALID(command);
 for(unsigned i=0;i<subCount;i++)[subs[i] release];
 free(subs);[main release];[debugLabel release];[command release];[super dealloc];
}
@end

@interface TeguCommand (APIParallel)
- (id)parallelRenderCommandEncoderWithDescriptor:(id)descriptor;
@end
@implementation TeguCommand (APIParallel)
- (id)parallelRenderCommandEncoderWithDescriptor:(id)descriptor {
 if([self teguCommitted]||encoding){TEGU_INVALID(self);return 0;}
 TeguRenderEncoder *main=[self renderCommandEncoderWithDescriptor:(id)descriptor];
 if(!main)return 0;
 TeguParallelRenderEncoder *parallel=[[TeguParallelRenderEncoder alloc] init];
 if(!parallel){TEGU_INVALID(self);return 0;}
 parallel->command=[self retain];parallel->main=[main retain];
 return [parallel autorelease];
}
@end
TEGU_API_RESOLVER(TeguParallelRenderEncoder) TEGU_API_RESOLVER(TeguRenderSubEncoder)
#endif
