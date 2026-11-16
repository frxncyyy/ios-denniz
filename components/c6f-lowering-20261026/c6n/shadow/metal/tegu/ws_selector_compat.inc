/* WindowServer/CoreAnimation compatibility selectors whose behavior is
 * independent of a GPU operation.  Keep functional rendering state out of
 * this file: unsupported state must still fail rather than silently render a
 * different result. */
@interface TeguCommand (TeguWSCompatibility)
- (id)computeCommandEncoderWithDispatchType:(NSUInteger)type;
- (void)pushDebugGroup:(id)string;
- (void)popDebugGroup;
@end
@implementation TeguCommand (TeguWSCompatibility)
- (id)computeCommandEncoderWithDispatchType:(NSUInteger)type {
 if(type){TEGU_INVALID(self);return 0;}
 return [self computeCommandEncoder];
}
- (void)pushDebugGroup:(id)string {(void)string;}
- (void)popDebugGroup {}
@end

@interface TeguEncoder (TeguWSCompatibility)
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
- (void)insertDebugSignpost:(id)string;
- (void)pushDebugGroup:(id)string;
- (void)popDebugGroup;
@end
@implementation TeguEncoder (TeguWSCompatibility)
- (id)device {return command->queue->owner;}
- (id)label {return 0;}
- (void)setLabel:(id)value {(void)value;}
- (void)insertDebugSignpost:(id)string {(void)string;}
- (void)pushDebugGroup:(id)string {(void)string;}
- (void)popDebugGroup {}
@end

@interface TeguBlitEncoder (TeguWSCompatibility)
#ifdef TEGU_GRAPHICS_BRIDGE
- (void)copyFromTexture:(TeguTexture *)source toTexture:(TeguTexture *)destination;
#endif
- (void)synchronizeResource:(id)resource;
- (void)insertDebugSignpost:(id)string;
- (void)pushDebugGroup:(id)string;
- (void)popDebugGroup;
@end
@implementation TeguBlitEncoder (TeguWSCompatibility)
#ifdef TEGU_GRAPHICS_BRIDGE
- (void)copyFromTexture:(TeguTexture *)source toTexture:(TeguTexture *)destination {
 if(!source||!destination||source->textureKind!=2||destination->textureKind!=2||
    source->textureDepth!=1||destination->textureDepth!=1||
    source->levelCount!=1||destination->levelCount!=1||
    source->textureWidth!=destination->textureWidth||
    source->textureHeight!=destination->textureHeight){TEGU_INVALID(command);return;}
 [self copyFromTexture:source sourceSlice:0 sourceLevel:0
  sourceOrigin:(TeguOrigin){0,0,0}
  sourceSize:(MTLSize){source->textureWidth,source->textureHeight,1}
  toTexture:destination destinationSlice:0 destinationLevel:0
  destinationOrigin:(TeguOrigin){0,0,0}];
}
#endif
- (void)synchronizeResource:(id)resource {
 /* Tegu exposes coherent shared memory and GPU-only private/memoryless memory;
  * none has Metal's managed-storage CPU synchronization operation. */
#ifdef TEGU_GRAPHICS_BRIDGE
 int buffer=tegu_owned_class(resource,"TeguBuffer");
 int texture=tegu_owned_class(resource,"TeguTexture");
 int valid=buffer||texture;
 id owner=buffer?[(TeguBuffer *)resource device]:texture?[(TeguTexture *)resource device]:0;
#else
 int valid=resource!=0;
 id owner=valid?[(TeguBuffer *)resource device]:0;
#endif
 if(ended||[command teguCommitted]||!valid||owner!=command->queue->owner)
  TEGU_INVALID(command);
}
- (void)insertDebugSignpost:(id)string {(void)string;}
- (void)pushDebugGroup:(id)string {(void)string;}
- (void)popDebugGroup {}
@end

@interface TeguBuffer (TeguWSCompatibility)
- (void)didModifyRange:(TeguRange)range;
- (id)iosurface;
- (signed char)isComplete;
- (NSUInteger)protectionOptions;
- (void)waitUntilComplete;
@end
@implementation TeguBuffer (TeguWSCompatibility)
- (void)didModifyRange:(TeguRange)range {(void)range;}
- (id)iosurface {return 0;}
- (signed char)isComplete {return 1;}
- (NSUInteger)protectionOptions {return 0;}
- (void)waitUntilComplete {}
@end

@interface TeguPipeline (TeguWSCompatibility)
- (NSUInteger)allocatedSize;
- (id)label;
@end
@implementation TeguPipeline (TeguWSCompatibility)
- (NSUInteger)allocatedSize {return allocated;}
- (id)label {return 0;}
@end

#ifdef TEGU_GRAPHICS_BRIDGE
@interface TeguRenderEncoder (TeguWSCompatibility)
- (void)setFragmentTexture:(TeguTexture *)texture atTextureIndex:(NSUInteger)textureIndex
 samplerState:(TeguSampler *)sampler atSamplerIndex:(NSUInteger)samplerIndex;
- (void)insertDebugSignpost:(id)string;
- (void)pushDebugGroup:(id)string;
- (void)popDebugGroup;
- (void)setDepthStencilState:(id)state;
- (void)setStencilReferenceValue:(uint32_t)value;
- (void)setStencilFrontReferenceValue:(uint32_t)front backReferenceValue:(uint32_t)back;
- (void)setDepthBias:(float)bias slopeScale:(float)slope clamp:(float)clamp;
- (void)setDepthClipMode:(NSUInteger)mode;
- (void)setBlendColorRed:(float)red green:(float)green blue:(float)blue alpha:(float)alpha;
- (void)setVisibilityResultMode:(NSUInteger)mode offset:(NSUInteger)offset;
- (void)useResource:(id)resource usage:(NSUInteger)usage;
- (void)useResource:(id)resource usage:(NSUInteger)usage stages:(NSUInteger)stages;
- (void)useHeap:(id)heap;
@end
@implementation TeguRenderEncoder (TeguWSCompatibility)
/* The generic pass may ignore a DontCare depth/stencil attachment, but it
 * cannot silently execute a depth/stencil test or write. Keep the immutable
 * Metal state so the draw path can accept the no-test case exactly and refuse
 * every observable case with a trace. */
- (void)setDepthStencilState:(id)state {
 if(inert||ended||[command teguCommitted])return;
 if(state&&(!tegu_owned_class(state,"TeguDepthStencilState")||
    ((TeguDepthStencilState *)state)->owner!=command->queue->owner)){TEGU_INVALID(command);return;}
 [state retain];[depthState release];depthState=state;
}
- (void)setStencilReferenceValue:(uint32_t)value {(void)value;}
- (void)setStencilFrontReferenceValue:(uint32_t)front backReferenceValue:(uint32_t)back {(void)front;(void)back;}
- (void)setDepthBias:(float)bias slopeScale:(float)slope clamp:(float)clamp {(void)bias;(void)slope;(void)clamp;}
- (void)setDepthClipMode:(NSUInteger)mode {(void)mode;}
- (void)setBlendColorRed:(float)red green:(float)green blue:(float)blue alpha:(float)alpha {(void)red;(void)green;(void)blue;(void)alpha;}
- (void)setVisibilityResultMode:(NSUInteger)mode offset:(NSUInteger)offset {(void)mode;(void)offset;}
- (void)useResource:(id)resource usage:(NSUInteger)usage {(void)resource;(void)usage;}
- (void)useResource:(id)resource usage:(NSUInteger)usage stages:(NSUInteger)stages {(void)resource;(void)usage;(void)stages;}
- (void)useHeap:(id)heap {(void)heap;}
- (void)setFragmentTexture:(TeguTexture *)texture atTextureIndex:(NSUInteger)textureIndex
 samplerState:(TeguSampler *)sampler atSamplerIndex:(NSUInteger)samplerIndex {
 [self setFragmentTexture:texture atIndex:textureIndex];
 if(!command->invalid)[self setFragmentSamplerState:sampler atIndex:samplerIndex];
}
- (void)insertDebugSignpost:(id)string {(void)string;}
- (void)pushDebugGroup:(id)string {(void)string;}
- (void)popDebugGroup {}
@end

@interface TeguTexture (TeguWSCompatibility)
- (void)didModifyData;
- (signed char)isComplete;
- (NSUInteger)protectionOptions;
- (NSUInteger)rotation;
- (void)waitUntilComplete;
@end
@implementation TeguTexture (TeguWSCompatibility)
- (void)didModifyData {}
- (signed char)isComplete {return 1;}
- (NSUInteger)protectionOptions {return 0;}
- (NSUInteger)rotation {return 0;}
- (void)waitUntilComplete {}
@end

@interface TeguRenderPipeline (TeguWSCompatibility)
- (NSUInteger)allocatedSize;
@end
@implementation TeguRenderPipeline (TeguWSCompatibility)
- (NSUInteger)allocatedSize {return allocated;}
@end
#endif
