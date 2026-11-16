@interface TextureDesc : NSObject {@public NSUInteger type,format,w,h,dep,samples,arrays,options,uses,levels;}
@end
@implementation TextureDesc
- (signed char)respondsToSelector:(SEL)s {return s==@selector(respondsToSelector:)||s==@selector(textureType)||
 s==@selector(pixelFormat)||s==@selector(width)||s==@selector(height)||s==@selector(depth)||
 s==@selector(mipmapLevelCount)||s==@selector(sampleCount)||s==@selector(arrayLength)||
 s==@selector(resourceOptions)||s==@selector(usage);}
- (id)init {self=[super init];if(self){type=2;dep=samples=arrays=1;}return self;}
- (NSUInteger)textureType{return type;}
- (NSUInteger)pixelFormat{return format;}
- (NSUInteger)width{return w;}
- (NSUInteger)height{return h;}
- (NSUInteger)depth{return dep;}
- (NSUInteger)mipmapLevelCount{return levels?levels:1;}
- (NSUInteger)sampleCount{return samples;}
- (NSUInteger)arrayLength{return arrays;}
- (NSUInteger)resourceOptions{return options;}
- (NSUInteger)usage{return uses;}
@end
@interface PassColor : NSObject {@public TeguTexture *target,*resolve;NSUInteger load,store,lvl,slc,dpl,rlvl,rslc,rdpl;unsigned delta;}
@end
@implementation PassColor
- (signed char)respondsToSelector:(SEL)s {return s==@selector(respondsToSelector:)||s==@selector(texture)||s==@selector(resolveTexture)||s==@selector(level)||s==@selector(slice)||s==@selector(depthPlane)||s==@selector(resolveLevel)||s==@selector(resolveSlice)||s==@selector(resolveDepthPlane)||s==@selector(loadAction)||s==@selector(storeAction)||s==@selector(clearColor);}
- (id)texture{return target;}
- (id)resolveTexture{return resolve;}
- (NSUInteger)level{return lvl;}
- (NSUInteger)slice{return slc;}
- (NSUInteger)depthPlane{return dpl;}
- (NSUInteger)resolveLevel{return rlvl;}
- (NSUInteger)resolveSlice{return rslc;}
- (NSUInteger)resolveDepthPlane{return rdpl;}
- (NSUInteger)loadAction{return load;}
- (NSUInteger)storeAction{return store;}
- (TeguClearColor)clearColor{return (TeguClearColor){(17.+delta)/255,34./255,51./255,68./255};}
- (void)dealloc{[target release];[resolve release];[super dealloc];}
@end
@interface RenderPass : NSObject {@public PassColor *color,*second;NSUInteger passWidth,passHeight,arrayLength,sampleCount;}
@end
@implementation RenderPass
- (signed char)respondsToSelector:(SEL)s {return s==@selector(respondsToSelector:)||s==@selector(colorAttachments)||s==@selector(objectAtIndexedSubscript:)||s==@selector(renderTargetArrayLength)||s==@selector(renderTargetWidth)||s==@selector(renderTargetHeight)||s==@selector(defaultRasterSampleCount)||s==@selector(depthAttachment)||s==@selector(stencilAttachment)||s==@selector(visibilityResultBuffer);}
- (id)colorAttachments{return self;}
- (id)objectAtIndexedSubscript:(NSUInteger)i {assert(i<8);return i==0?color:i==1?second:0;}
- (id)depthAttachment{return 0;}
- (id)stencilAttachment{return 0;}
- (NSUInteger)renderTargetArrayLength{return arrayLength;}
- (NSUInteger)renderTargetWidth{return passWidth;}
- (NSUInteger)renderTargetHeight{return passHeight;}
- (NSUInteger)defaultRasterSampleCount{return sampleCount;}
- (id)visibilityResultBuffer{return 0;}
- (void)dealloc{[color release];[second release];[super dealloc];}
@end
/* Texture views are zero-copy: format aliases use the shared audited policy,
 * while the view retains the exact source/backing and can outlive it. */
static void test_texture_views(TeguComputeDevice *d) {
 TextureDesc *desc=[[TextureDesc alloc] init];
 desc->format=70;desc->w=desc->h=16;desc->uses=1;desc->levels=2;desc->type=2;
 TeguTexture *src=[d newTextureWithDescriptor:(id)desc];assert(src);
 TeguTexture *v=[src newTextureViewWithPixelFormat:71];assert(v);
 assert(v->format==71&&v->buffer==src->buffer&&[v device]==d);
 assert(v->textureWidth==src->textureWidth&&v->textureHeight==src->textureHeight);
 assert(v->levelCount==src->levelCount&&v->mip[1].offset==src->mip[1].offset);
 assert(v->rowBytes==src->rowBytes&&v->textureUsage==src->textureUsage);
 uint32_t *srcp=(uint32_t *)[src->buffer contents];uint32_t *vp=(uint32_t *)[v->buffer contents];
 srcp[0]=0x1234abcd;assert(vp[0]==0x1234abcd);
 assert(![src newTextureViewWithPixelFormat:80]);  /* RGB vs BGR order */
 assert(![src newTextureViewWithPixelFormat:250]); /* depth */
 assert(![src newTextureViewWithPixelFormat:115]); /* different texel size */
 TeguTextureViewRange full={0,2},one={0,1},badlevel={1,1},badslices={0,2};
 TeguTexture *v2=[src newTextureViewWithPixelFormat:71 textureType:2 levels:full slices:one];
 assert(v2&&v2->format==71&&v2->buffer==src->buffer);
 assert(![src newTextureViewWithPixelFormat:71 textureType:3 levels:full slices:one]);
 assert(![src newTextureViewWithPixelFormat:71 textureType:2 levels:badlevel slices:one]);
 assert(![src newTextureViewWithPixelFormat:71 textureType:2 levels:full slices:badslices]);
 TeguBuffer *back=[src->buffer retain];[src release];
 assert(v->buffer==back&&((uint32_t*)[v->buffer contents])[0]==0x1234abcd);
 [back release];[v release];[v2 release];[desc release];
}

static void drain(void);
static void test_blit_completeness(TeguComputeDevice *d) {
 NSUInteger baseline=[d currentAllocatedSize];
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=70;desc->w=desc->h=8;desc->uses=5;desc->levels=2;
 TeguTexture *src=[d newTextureWithDescriptor:(id)desc];assert(src);
 TeguTexture *dst=[d newTextureWithDescriptor:(id)desc];assert(dst);
 unsigned char *sp=(unsigned char *)[src->buffer contents],*dp=(unsigned char *)[dst->buffer contents];
 uint32_t *sp32=(uint32_t *)sp,*dp32=(uint32_t *)dp;
 NSUInteger spr=src->rowBytes/4,dpr=dst->rowBytes/4;
 for(unsigned i=0;i<256;i++)sp[i]=(unsigned char)(i^0x5a);
 memset(dp,0xee,256);
 TeguBuffer *buf=[d newBufferWithLength:512 options:0],*out=[d newBufferWithLength:512 options:0];
 assert(buf&&out);unsigned char *bp=(unsigned char *)[buf contents],*op=(unsigned char *)[out contents];
 uint32_t *op32=(uint32_t *)op,*bp32=(uint32_t *)bp;
 for(unsigned i=0;i<512;i++)bp[i]=(unsigned char)(i*7);
 memset(op,0xcc,512);
 TeguQueue *q=[d newCommandQueue];TeguCommand *c=[q commandBuffer];TeguBlitEncoder *e=[c blitCommandEncoder];
 assert(e);
 [e copyFromBuffer:buf sourceOffset:0 sourceBytesPerRow:32 sourceBytesPerImage:256
  sourceSize:(MTLSize){8,8,1} toTexture:dst destinationSlice:0 destinationLevel:0
  destinationOrigin:(TeguOrigin){0,0,0}];
 [e copyFromTexture:dst sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0}
  sourceSize:(MTLSize){8,8,1} toBuffer:out destinationOffset:0
  destinationBytesPerRow:32 destinationBytesPerImage:256];
 [e updateFence:(id)buf];[e waitForFence:(id)buf];
 [e synchronizeResource:dst];[e synchronizeTexture:dst slice:0 level:0];
 [e optimizeContentsForGPUAccess:dst];[e optimizeContentsForGPUAccess:dst slice:0 level:0];
 [e endEncoding];assert(!c->invalid);[c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)assert(dp32[y*dpr+x]==bp32[y*8+x]);
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)assert(op32[y*8+x]==bp32[y*8+x]);
 /* The compatible sRGB view aliases dst; copy it only after checking the
  * full original region, then assert exactly the view's 4x4 overlay. */
 TeguTexture *view=[dst newTextureViewWithPixelFormat:71];assert(view);
 TeguCommand *vc=[q commandBuffer];TeguBlitEncoder *ve=[vc blitCommandEncoder];assert(ve);
 [ve copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0}
  sourceSize:(MTLSize){4,4,1} toTexture:view destinationSlice:0 destinationLevel:0
  destinationOrigin:(TeguOrigin){0,0,0}];
 [ve endEncoding];[vc commit];[vc waitUntilCompleted];assert([vc status]==4&&![vc error]);
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)
  assert(dp32[y*dpr+x]==sp32[y*spr+x]);
 /* fillBuffer remains ordered CPU work. */
 TeguBuffer *fill=[d newBufferWithLength:16 options:0];assert(fill);
 c=[q commandBuffer];e=[c blitCommandEncoder];assert(e);
 [e fillBuffer:fill range:(TeguRange){2,5} value:0xab];[e endEncoding];
 [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
 for(unsigned i=0;i<16;i++)assert(((unsigned char *)[fill contents])[i]==(i>=2&&i<7?0xab:0));
 [fill release];
 drain();
 /* Bounds and format-compatible-view refusals are command errors, never
  * partial copies. */
 TeguTexture *bad=[d newTextureWithDescriptor:(id)desc];assert(bad);bad->format=80;
 c=[q commandBuffer];e=[c blitCommandEncoder];
 [e copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0}
  sourceSize:(MTLSize){8,8,1} toTexture:bad destinationSlice:0 destinationLevel:0 destinationOrigin:(TeguOrigin){0,0,0}];
 [e endEncoding];[c commit];[c waitUntilCompleted];assert([c status]==5);
 c=[q commandBuffer];e=[c blitCommandEncoder];
 [e copyFromBuffer:buf sourceOffset:0 sourceBytesPerRow:4 sourceBytesPerImage:0
  sourceSize:(MTLSize){8,8,1} toTexture:dst destinationSlice:0 destinationLevel:0 destinationOrigin:(TeguOrigin){0,0,0}];
 [e endEncoding];[c commit];[c waitUntilCompleted];assert([c status]==5);
 c=[q commandBuffer];e=[c blitCommandEncoder];
 [e copyFromBuffer:buf sourceOffset:0 sourceBytesPerRow:32 sourceBytesPerImage:256
  sourceSize:(MTLSize){8,8,1} toTexture:dst destinationSlice:0 destinationLevel:0
  destinationOrigin:(TeguOrigin){0,0,0} options:1];
 [e endEncoding];[c commit];[c waitUntilCompleted];assert([c status]==5);
 [view release];[src release];[dst release];[bad release];[buf release];[out release];
 [desc release];[q release];drain();assert([d currentAllocatedSize]==baseline);
}

static void drain(void);
static void test_texture_blits(TeguComputeDevice *d) {
 NSUInteger baseline=[d currentAllocatedSize];
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=80;desc->w=32;desc->h=19;desc->uses=1;
 TeguTexture *a=[d newTextureWithDescriptor:(id)desc];assert(a&&[a width]==32&&[a height]==19&&[a pixelFormat]==80);
 desc->w=24;desc->h=23;desc->uses=5;
 TeguTexture *b=[d newTextureWithDescriptor:(id)desc];assert(b&&[b usage]==5);
 unsigned *ap=[a->buffer contents],*bp=[b->buffer contents];
 NSUInteger bpr=b->rowBytes/4,apr=a->rowBytes/4;
 for(unsigned i=0;i<32*19;i++)ap[i]=0x12340000+i;
 for(unsigned y=0;y<23;y++)for(unsigned x=0;x<bpr;x++)bp[y*bpr+x]=0xdeadbeef;
 TeguQueue *q=[d newCommandQueue];TeguCommand *c=[q commandBuffer];TeguBlitEncoder *e=[c blitCommandEncoder];
 [e copyFromTexture:a sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){3,4,0} sourceSize:(MTLSize){7,9,1}
  toTexture:b destinationSlice:0 destinationLevel:0 destinationOrigin:(TeguOrigin){11,6,0}];
 [e endEncoding];assert(bp[6*(b->rowBytes/4)+11]==0xdeadbeef);
 [c commit];assert([c status]==4);
 for(unsigned y=0;y<23;y++)for(unsigned x=0;x<24;x++)
  assert(bp[y*bpr+x]==(x>=11&&x<18&&y>=6&&y<15?ap[(y-6+4)*apr+x-11+3]:0xdeadbeef));
 drain();
 for(unsigned scenario=0;scenario<5;scenario++) {
  c=[q commandBuffer];e=[c blitCommandEncoder];
  TeguOrigin origin={scenario==0?~0UL:11,6,0};MTLSize size={scenario==1?99:7,9,1};
  if(scenario==4)b->format=70;
  [e copyFromTexture:a sourceSlice:scenario==2?1:0 sourceLevel:0 sourceOrigin:(TeguOrigin){3,4,0} sourceSize:size
   toTexture:scenario==3?a:b destinationSlice:0 destinationLevel:0 destinationOrigin:scenario==3?(TeguOrigin){4,5,0}:origin];
  b->format=80;[e endEncoding];[c commit];assert([c status]==5);drain();
 }
 c=[q commandBuffer];e=[c blitCommandEncoder];
 [e copyFromTexture:a sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0} sourceSize:(MTLSize){4,4,1}
  toTexture:b destinationSlice:0 destinationLevel:0 destinationOrigin:(TeguOrigin){0,0,0}];
 [e endEncoding];[a release];[b release];[c commit];assert([c status]==4&&bp[0]==ap[0]);
 [desc release];[q release];drain();assert([d currentAllocatedSize]==baseline);
}

static void test_mipmaps(TeguComputeDevice *d) {
 NSUInteger baseline=[d currentAllocatedSize];
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=70;desc->w=desc->h=4;desc->uses=5;desc->levels=3;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];assert(t&&[t mipmapLevelCount]==3);
 desc->levels=4;assert(![d newTextureWithDescriptor:(id)desc]);desc->levels=3;
 unsigned char base[64];for(unsigned i=0;i<64;i++)base[i]=(unsigned char)i;
 [t replaceRegion:(TeguRegion){{0,0,0},{4,4,1}} mipmapLevel:0 withBytes:base bytesPerRow:16];
 TeguQueue *q=[d newCommandQueue];TeguCommand *c=[q commandBuffer];TeguBlitEncoder *e=[c blitCommandEncoder];
 [e generateMipmapsForTexture:t];[e endEncoding];
 unsigned char result[24];memset(result,0xcc,sizeof(result));
 [t getBytes:result bytesPerRow:12 fromRegion:(TeguRegion){{0,0,0},{2,2,1}} mipmapLevel:1];
 assert(result[0]==0);[c commit];assert([c status]==4);drain();
 memset(result,0xcc,sizeof(result));
 [t getBytes:result bytesPerRow:12 fromRegion:(TeguRegion){{0,0,0},{2,2,1}} mipmapLevel:1];
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)for(unsigned ch=0;ch<4;ch++)
  assert(result[y*12+x*4+ch]==10+y*32+x*8+ch);
 assert(result[8]==0xcc&&result[20]==0xcc);
 [t getBytes:result bytesPerRow:4 fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:2];
 for(unsigned ch=0;ch<4;ch++)assert(result[ch]==30+ch);
 // Copy between nonzero levels, then regenerate in the same ordered command.
 c=[q commandBuffer];e=[c blitCommandEncoder];
 [e copyFromTexture:t sourceSlice:0 sourceLevel:2 sourceOrigin:(TeguOrigin){0,0,0} sourceSize:(MTLSize){1,1,1}
  toTexture:t destinationSlice:0 destinationLevel:1 destinationOrigin:(TeguOrigin){0,0,0}];
 [e generateMipmapsForTexture:t];[e endEncoding];[c commit];assert([c status]==4);drain();
 [t getBytes:result bytesPerRow:4 fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:2];
 assert(result[0]==30);
 // Non-square, odd dimensions and one-dimensional tails retain all source texels.
 [t release];desc->w=3;desc->h=1;desc->levels=2;t=[d newTextureWithDescriptor:(id)desc];assert(t);
 unsigned char odd[12]={0,0,0,0,30,60,90,120,60,120,180,240};
 [t replaceRegion:(TeguRegion){{0,0,0},{3,1,1}} mipmapLevel:0 withBytes:odd bytesPerRow:12];
 c=[q commandBuffer];e=[c blitCommandEncoder];[e generateMipmapsForTexture:t];[e endEncoding];
 [t release];[c commit];assert([c status]==4);
 // Retained backing storage survives release of the texture itself.
 unsigned char *mapped=[TEGU_JOB(c->jobChunks,0).buffers[1] contents];
 assert(mapped[64]==30&&mapped[65]==60&&mapped[66]==90&&mapped[67]==120);
 drain();
 /* BackBoard generates the private 776x24 blur mip chains in the same
  * command as its render passes.  Half-float components must be averaged as
  * numbers rather than as their two encoded bytes. */
 U64 savedCapabilities=d->capabilities;d->capabilities|=TEGU_GPU_CAP_COMPOSITOR;
 assert(tegu_mip_float_to_half(2.5f)==0x4100&&tegu_mip_half_to_float(0x4100)==2.5f);
 for(unsigned encoded=0;encoded<65536;encoded++) {
  unsigned short roundTrip=tegu_mip_float_to_half(tegu_mip_half_to_float((unsigned short)encoded));
  if((encoded&0x7c00u)!=0x7c00u||(encoded&0x03ffu)==0)assert(roundTrip==encoded);
  else assert(roundTrip==(unsigned short)((encoded&0x8000u)|0x7e00u));
 }
 desc->w=776;desc->h=24;desc->levels=10;desc->uses=5;desc->options=0x20;desc->format=115;
 TeguTexture *half=[d newTextureWithDescriptor:(id)desc];assert(half);
 unsigned short *half0=(unsigned short *)((unsigned char *)[half->buffer contents]+half->mip[0].offset);
 static const unsigned short rgbaValues[4]={0x3c00,0x4000,0x4200,0x4400};
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)for(unsigned channel=0;channel<4;channel++)
  *(unsigned short *)((unsigned char *)half0+y*half->mip[0].stride+(x*4+channel)*2)=rgbaValues[y*2+x];
 c=[q commandBuffer];e=[c blitCommandEncoder];[e generateMipmapsForTexture:half];[e endEncoding];
 assert(c->count==9&&TEGU_JOB(c->jobChunks,0).mipComponentBytes==2);[c commit];assert([c status]==4);drain();
 unsigned short *half1=(unsigned short *)((unsigned char *)[half->buffer contents]+half->mip[1].offset);
 for(unsigned channel=0;channel<4;channel++)assert(half1[channel]==0x4100);
 [half release];drain();
 desc->format=43;
 TeguTexture *r16=[d newTextureWithDescriptor:(id)desc];assert(r16);
 static const unsigned short rValues[4]={0x3c00,0x4200,0x4500,0x4700};
 for(unsigned y=0;y<2;y++)for(unsigned x=0;x<2;x++)
  *(unsigned short *)((unsigned char *)[r16->buffer contents]+r16->mip[0].offset+y*r16->mip[0].stride+x*2)=rValues[y*2+x];
 c=[q commandBuffer];e=[c blitCommandEncoder];[e generateMipmapsForTexture:r16];[e endEncoding];
 assert(c->count==9&&TEGU_JOB(c->jobChunks,0).mipComponentBytes==2);[c commit];assert([c status]==4);drain();
 unsigned short r16Result;memcpy(&r16Result,(unsigned char *)[r16->buffer contents]+r16->mip[1].offset,2);
 assert(r16Result==0x4400);[r16 release];drain();d->capabilities=savedCapabilities;
 [q release];[desc release];drain();assert([d currentAllocatedSize]==baseline);
}

@class IOSurfaceFixture;
static IOSurfaceFixture *new_surface_fixture(uint32_t,uint32_t,uint32_t,NSUInteger,NSUInteger,NSUInteger,NSUInteger);
static TeguTexture *import_surface_texture(TeguComputeDevice *,TextureDesc *,id,NSUInteger);
static unsigned renderDraws,failDraw;

static void test_render_commands(TeguComputeDevice *d,TeguRenderPipeline *pipeline) {
 NSUInteger baseline=[d currentAllocatedSize];
 test_texture_views(d);
 assert([d supportsSampleCount:1]&&![d supportsSampleCount:4]);
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=70;desc->w=desc->h=16;desc->uses=4;
 desc->w=0;assert(![d newTextureWithDescriptor:(id)desc]);desc->w=16;
 desc->options=16;assert(![d newTextureWithDescriptor:(id)desc]);desc->options=0;
 desc->format=81;assert(![d newTextureWithDescriptor:(id)desc]);desc->format=70;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];assert(t&&[t device]==d);
 assert([d currentAllocatedSize]==baseline+16384);
 // CPU shared-texture copy semantics, not a simulated shader.
 unsigned *pixels=[t->buffer contents];for(unsigned i=0;i<256;i++)pixels[i]=i+100;
 unsigned preserved=pixels[0],rejectedWrite=0xabcdef01,rejectedRead=0x13579bdf;
 [t replaceRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0
   withBytes:&rejectedWrite bytesPerRow:0];
 [t getBytes:&rejectedRead bytesPerRow:0
   fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0];
 assert(pixels[0]==preserved&&rejectedRead==0x13579bdf);
 unsigned output[12];for(unsigned i=0;i<12;i++)output[i]=0xdeadbeef;
 [t getBytes:output bytesPerRow:24 fromRegion:(TeguRegion){{2,3,0},{3,2,1}} mipmapLevel:0];
 assert(output[0]==150&&output[1]==151&&output[2]==152&&output[3]==0xdeadbeef);
 assert(output[6]==166&&output[7]==167&&output[8]==168&&output[9]==0xdeadbeef);
 unsigned copy[12];memcpy(copy,output,sizeof(copy));
 [t getBytes:output bytesPerRow:24 fromRegion:(TeguRegion){{15,3,0},{3,2,1}} mipmapLevel:0];
 assert(!memcmp(copy,output,sizeof(copy)));
 TeguQueue *q=[d newCommandQueue];
 RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];
 pass->color->target=[t retain];pass->color->load=2;pass->color->store=1;
 pass->passWidth=16;pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
#ifdef TEGU_RENDER_TRACE
	 /* First trace a real two-target descriptor with an unsupported resolve.
	  * This proves the trace sees RT1 before the resolve-target gate. */
 TextureDesc *secondDesc=[[TextureDesc alloc] init];secondDesc->format=70;secondDesc->w=secondDesc->h=16;secondDesc->uses=4;
 IOSurfaceFixture *diagSurface=new_surface_fixture(509,TEGU_IOSURFACE_RGBA,0,16,16,64,16384);
 U64 savedCaps=d->capabilities;d->capabilities=savedCaps|512;
 TeguTexture *secondTarget=import_surface_texture(d,secondDesc,(id)diagSurface,0);assert(secondTarget&&secondTarget->surfaceImported);
 d->capabilities=savedCaps;
 pass->second=[[PassColor alloc] init];pass->second->target=[secondTarget retain];pass->second->resolve=[secondTarget retain];pass->second->load=2;pass->second->store=1;pass->second->delta=17;
 TeguCommand *multiTarget=[q commandBuffer];
 assert(![multiTarget renderCommandEncoderWithDescriptor:(id)pass]&&multiTarget->invalid&&multiTarget->count==0);
 [pass->second release];pass->second=0;[secondTarget release];[diagSurface release];[secondDesc release];
 /* Exercise the production trace writer through real descriptor and draw
  * entrypoints. Overdrive both global caps so output bounds are testable. */
 for(unsigned i=0;i<20;i++) {
  TeguCommand *traceCommand=[q commandBuffer];assert(traceCommand);
  TeguRenderEncoder *traceEncoder=[traceCommand renderCommandEncoderWithDescriptor:(id)pass];assert(traceEncoder);
  [traceEncoder setRenderPipelineState:pipeline];
  if(i==0)for(unsigned j=0;j<40;j++)
   [traceEncoder drawPrimitives:3 vertexStart:j+1 vertexCount:j+1];
  [traceEncoder endEncoding];
 }
 /* Normal pass detail is now exhausted.  A valid late encoder must retain
  * bounded visibility for begin, bind, draw and end. */
 unsigned lateEventsBefore=tegu_render_trace_late_encoder_events;
 TeguCommand *lateTraceCommand=[q commandBuffer];assert(lateTraceCommand);
 TeguRenderEncoder *lateTraceEncoder=[lateTraceCommand renderCommandEncoderWithDescriptor:(id)pass];assert(lateTraceEncoder);
 [lateTraceEncoder setRenderPipelineState:pipeline];
 [lateTraceEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 [lateTraceEncoder endEncoding];
 assert(tegu_render_trace_late_encoder_events==lateEventsBefore+4);
 /* Exhaust the general pass trace budget, then verify an RT1 pass receives
  * one separately bounded capture record without creating a render job. */
 TextureDesc *lateDesc=[[TextureDesc alloc] init];lateDesc->format=70;lateDesc->w=lateDesc->h=16;lateDesc->uses=4;
 IOSurfaceFixture *lateSurface=new_surface_fixture(510,TEGU_IOSURFACE_RGBA,0,16,16,64,16384);
 U64 lateCaps=d->capabilities;d->capabilities=lateCaps|512;
 TeguTexture *lateTarget=import_surface_texture(d,lateDesc,(id)lateSurface,0);assert(lateTarget&&lateTarget->surfaceImported);
 d->capabilities=lateCaps;
 pass->second=[[PassColor alloc] init];pass->second->target=[lateTarget retain];pass->second->resolve=[lateTarget retain];pass->second->load=2;pass->second->store=1;
 TeguCommand *lateMulti=[q commandBuffer];
 assert(![lateMulti renderCommandEncoderWithDescriptor:(id)pass]&&lateMulti->invalid&&lateMulti->count==0);
 [pass->second release];pass->second=0;[lateTarget release];[lateSurface release];[lateDesc release];
#endif
 /* An empty render encoder is a legal no-op and must not poison later work in
  * the same command buffer.  BackBoard emits these load/store-only passes. */
 TeguCommand *empty=[q commandBuffer];
 TeguRenderEncoder *emptyEncoder=[empty renderCommandEncoderWithDescriptor:(id)pass];assert(emptyEncoder);
 [emptyEncoder endEncoding];assert(!empty->invalid&&!empty->count&&!empty->encoding);
 [empty commit];assert([empty status]==4&&![empty error]&&!renderDraws);
 // Multiple encoders on one command buffer are executed in recording order.
 TeguCommand *command=[q commandBuffer];
 for(unsigned i=0;i<2;i++) {
  pass->color->delta=i;
  TeguRenderEncoder *encoder=[command renderCommandEncoderWithDescriptor:(id)pass];assert(encoder);
  if(!i) {
   [encoder setViewport:(TeguViewport){0,0,16,16,0,1}];
   [encoder setScissorRect:(TeguScissorRect){0,0,16,16}];
   [encoder setCullMode:0];[encoder setFrontFacingWinding:1];[encoder setTriangleFillMode:0];
   assert(!command->invalid);
  }
  [encoder setRenderPipelineState:pipeline];
  [encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 }
 pass->color->delta=0;
 assert(command->count==2&&TEGU_JOB(command->jobChunks,0).renderTexture==t&&TEGU_JOB(command->jobChunks,1).renderPipeline==pipeline);
 [command commit];assert([command status]==4&&![command error]&&renderDraws==2);
 TeguCommand *badViewport=[q commandBuffer];
 TeguRenderEncoder *badViewportEncoder=[badViewport renderCommandEncoderWithDescriptor:(id)pass];
 /* A viewport the backend cannot express drops its draws, not the command. */
 [badViewportEncoder setViewport:(TeguViewport){0,0,15,16,0,1}];
 assert(!badViewport->invalid&&!badViewport->count);
 [badViewportEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(!badViewport->invalid&&!badViewport->count);[badViewportEncoder endEncoding];
 [badViewport commit];assert([badViewport status]==4&&renderDraws==2);
 /* Legal partial state is retained without poisoning work already recorded.
  * A backend draw rejects it only when that unsupported state would become
  * observable in a submitted job. */
 TeguCommand *partialScissor=[q commandBuffer];
 TeguRenderEncoder *partialEncoder=[partialScissor renderCommandEncoderWithDescriptor:(id)pass];
 [partialEncoder setScissorRect:(TeguScissorRect){1,0,15,16}];
 assert(!partialScissor->invalid&&!partialScissor->count);
 [partialEncoder setRenderPipelineState:pipeline];
 [partialEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
 assert(partialScissor->invalid&&!partialScissor->count);
 [partialEncoder endEncoding];[partialScissor commit];
 assert([partialScissor status]==5&&renderDraws==2);
 TeguCommand *badScissor=[q commandBuffer];
 TeguRenderEncoder *badScissorEncoder=[badScissor renderCommandEncoderWithDescriptor:(id)pass];
 [badScissorEncoder setScissorRect:(TeguScissorRect){15,0,2,16}];
 assert(badScissor->invalid&&!badScissor->count);[badScissorEncoder endEncoding];
 [badScissor commit];assert([badScissor status]==5&&renderDraws==2);
 uint16_t identityValues[3]={0,1,2};TeguBuffer *identity=[d newBufferWithBytes:identityValues length:sizeof(identityValues) options:0];
 TeguCommand *indexed=[q commandBuffer];TeguRenderEncoder *indexedEncoder=[indexed renderCommandEncoderWithDescriptor:(id)pass];
 assert(indexedEncoder&&identity&&
  [indexedEncoder respondsToSelector:@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:)]&&
  [indexedEncoder respondsToSelector:@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:)]&&
  [indexedEncoder respondsToSelector:@selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:)]);
 [indexedEncoder setRenderPipelineState:pipeline];
 [indexedEncoder drawIndexedPrimitives:3 indexCount:3 indexType:0 indexBuffer:identity indexBufferOffset:0];
 [indexedEncoder endEncoding];assert(!indexed->invalid&&indexed->count==1);
 [indexed commit];assert([indexed status]==4&&![indexed error]&&renderDraws==3);[identity release];
 for(unsigned scenario=0;scenario<5;scenario++) {
  TeguCommand *bad=[q commandBuffer];unsigned before=renderDraws;
  if(scenario==0)pass->color->load=1;
  TeguRenderEncoder *encoder=[bad renderCommandEncoderWithDescriptor:(id)pass];
  if(scenario==0){assert(!encoder);pass->color->load=2;}
  else {
   assert(encoder);[encoder setRenderPipelineState:pipeline];
   [encoder drawPrimitives:3 vertexStart:scenario==1?1:0 vertexCount:3];
   if(scenario==2)[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];
   if(scenario!=3)[encoder endEncoding];
   if(scenario==4)failDraw=1;
  }
  [bad commit];assert([bad status]==5&&[bad error]);
  assert(renderDraws==before+(scenario==4));failDraw=0;
 }
 // Record then release the descriptor and application references before commit.
 TeguCommand *last=[q commandBuffer];
 TeguRenderEncoder *encoder=[last renderCommandEncoderWithDescriptor:(id)pass];
 [encoder setRenderPipelineState:pipeline];[encoder drawPrimitives:3 vertexStart:0 vertexCount:3];[encoder endEncoding];
 [desc release];[pass release];[t release];[q release];
 assert([d currentAllocatedSize]==baseline+16384);
 [last commit];assert([last status]==4&&![last error]);
 drain();assert([d currentAllocatedSize]==baseline&&allocated==freed);
}

static void test_ui_private_texture_allocations(TeguComputeDevice *d) {
 NSUInteger baseline=[d currentAllocatedSize];U64 capabilities=d->capabilities;
 d->capabilities|=TEGU_GPU_CAP_COMPOSITOR;
 TextureDesc *desc=[[TextureDesc alloc] init];desc->w=776;desc->h=24;desc->uses=5;desc->options=0x20;
 desc->format=115;desc->levels=10;
 TeguTexture *half=[d newTextureWithDescriptor:(id)desc];assert(half);
 assert([half pixelFormat]==115&&[half resourceOptions]==0x20&&[half storageMode]==2&&
   [half mipmapLevelCount]==10&&half->rowBytes==6208&&[half->buffer length]==198792);
 desc->options=0x30;
 TeguTexture *memoryless=[d newTextureWithDescriptor:(id)desc];assert(memoryless);
 assert([memoryless resourceOptions]==0x30&&[memoryless storageMode]==3&&[memoryless->buffer length]==198792);
 desc->format=550;desc->levels=1;desc->options=0x20;
 TeguTexture *planes=[d newTextureWithDescriptor:(id)desc];assert(planes);
 assert(planes->rowBytes==3104&&[planes->buffer length]==93312);
 desc->format=554;
 TeguTexture *xr=[d newTextureWithDescriptor:(id)desc];assert(xr);
 assert(xr->rowBytes==3104&&[xr->buffer length]==74496);
 desc->format=80;desc->levels=10;desc->options=0x20;
 TeguTexture *r44BGRA=[d newTextureWithDescriptor:(id)desc];assert(r44BGRA);
 static const struct tegu_texture_level bgraLayout[10]={
  {776,24,3104,0},{388,12,1552,74496},{194,6,776,93120},{97,3,388,97792},{48,1,192,99008},
  {24,1,96,99200},{12,1,48,99328},{6,1,24,99392},{3,1,12,99456},{1,1,4,99520}};
 assert([r44BGRA pixelFormat]==80&&[r44BGRA storageMode]==2&&[r44BGRA mipmapLevelCount]==10&&
   r44BGRA->rowBytes==3104&&[r44BGRA->buffer length]==99524&&r44BGRA->buffer->owner==d);
 for(unsigned i=0;i<10;i++)assert(!memcmp(&r44BGRA->mip[i],&bgraLayout[i],sizeof(bgraLayout[i]))&&
   r44BGRA->mip[i].offset<=r44BGRA->buffer->logical&&
   r44BGRA->mip[i].stride*r44BGRA->mip[i].height<=r44BGRA->buffer->logical-r44BGRA->mip[i].offset);
 desc->options=0x30;
 TeguTexture *r47MemorylessBGRA10=[d newTextureWithDescriptor:(id)desc];assert(r47MemorylessBGRA10);
 assert([r47MemorylessBGRA10 pixelFormat]==80&&[r47MemorylessBGRA10 storageMode]==3&&
   [r47MemorylessBGRA10 resourceOptions]==0x30&&[r47MemorylessBGRA10 mipmapLevelCount]==10&&
   r47MemorylessBGRA10->rowBytes==3104&&[r47MemorylessBGRA10->buffer length]==99524&&
   r47MemorylessBGRA10->buffer->owner==d);
 for(unsigned i=0;i<10;i++)assert(!memcmp(&r47MemorylessBGRA10->mip[i],&bgraLayout[i],sizeof(bgraLayout[i])));
 desc->levels=1;
 TeguTexture *r47MemorylessBGRA1=[d newTextureWithDescriptor:(id)desc];assert(r47MemorylessBGRA1);
 assert([r47MemorylessBGRA1 pixelFormat]==80&&[r47MemorylessBGRA1 storageMode]==3&&
   [r47MemorylessBGRA1 resourceOptions]==0x30&&[r47MemorylessBGRA1 mipmapLevelCount]==1&&
   r47MemorylessBGRA1->rowBytes==3136&&[r47MemorylessBGRA1->buffer length]==75264&&
   r47MemorylessBGRA1->buffer->owner==d);
 desc->options=0x20;
 desc->format=43;desc->levels=1;
 TeguTexture *r44R16=[d newTextureWithDescriptor:(id)desc];assert(r44R16);
 assert([r44R16 pixelFormat]==43&&[r44R16 storageMode]==2&&[r44R16 mipmapLevelCount]==1&&
   r44R16->rowBytes==1600&&r44R16->mip[0].width==776&&r44R16->mip[0].height==24&&
   r44R16->mip[0].offset==0&&[r44R16->buffer length]==38400&&r44R16->buffer->owner==d);
 /* R56 requests the same private R16Float surface with its complete mip
  * chain. Verify the exact unpadded row strides, aligned level starts and
  * logical allocation before any render-path support is inferred from it. */
 NSUInteger r56Baseline=[d currentAllocatedSize];desc->levels=10;
 TeguTexture *r56R16=[d newTextureWithDescriptor:(id)desc];assert(r56R16);
 static const struct tegu_texture_level r16MipLayout[10]={
  {776,24,1552,0},{388,12,776,37248},{194,6,388,46592},{97,3,194,48960},{48,1,96,49600},
  {24,1,48,49728},{12,1,24,49792},{6,1,12,49856},{3,1,6,49920},{1,1,2,49984}};
 assert([r56R16 textureType]==2&&[r56R16 pixelFormat]==43&&[r56R16 storageMode]==2&&
   [r56R16 resourceOptions]==0x20&&[r56R16 usage]==5&&[r56R16 mipmapLevelCount]==10&&
   [r56R16 width]==776&&[r56R16 height]==24&&[r56R16 depth]==1&&r56R16->rowBytes==1552&&
   [r56R16->buffer length]==49986&&r56R16->buffer->logical==49986&&r56R16->buffer->owner==d&&
   [d currentAllocatedSize]==r56Baseline+65536);
 for(unsigned i=0;i<10;i++)assert(!memcmp(&r56R16->mip[i],&r16MipLayout[i],sizeof(r16MipLayout[i]))&&
   r56R16->mip[i].offset<=r56R16->buffer->logical&&
   r56R16->mip[i].stride*r56R16->mip[i].height<=r56R16->buffer->logical-r56R16->mip[i].offset);
 /* Private storage remains inaccessible through CPU texture selectors. */
 unsigned char r56Before=*(unsigned char *)[r56R16->buffer contents],r56Byte=0x6d,r56Out=0xa7;
 [r56R16 replaceRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:9 withBytes:&r56Byte bytesPerRow:2];
 [r56R16 getBytes:&r56Out bytesPerRow:2 fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:9];
 assert(*(unsigned char *)[r56R16->buffer contents]==r56Before&&r56Out==0xa7);
 /* The backing allocation outlives its texture wrapper when retained, then
  * returns the complete allocation accounting once its final owner drops. */
 TeguBuffer *r56Backing=[r56R16->buffer retain];[r56R16 release];r56R16=0;drain();
 assert(r56Backing->logical==49986&&[r56Backing contents]&&[d currentAllocatedSize]==r56Baseline+65536);
 [r56Backing release];drain();assert([d currentAllocatedSize]==r56Baseline);
 /* R58's exact Siri sphere attachment is a memoryless 4x BGRA multisample
  * texture. It has device identity but no addressable backing; executable
  * pipelines remain single-sample until the tile-memory contract exists. */
 desc->format=80;desc->type=4;desc->w=desc->h=594;desc->dep=1;desc->levels=1;
 desc->samples=4;desc->arrays=1;desc->options=0x30;desc->uses=4;
 NSUInteger r58Baseline=[d currentAllocatedSize];
 TeguTexture *r58MSAA=[d newTextureWithDescriptor:(id)desc];assert(r58MSAA);
 assert([r58MSAA textureType]==4&&[r58MSAA pixelFormat]==80&&[r58MSAA width]==594&&
   [r58MSAA height]==594&&[r58MSAA depth]==1&&[r58MSAA sampleCount]==4&&
   [r58MSAA arrayLength]==1&&[r58MSAA storageMode]==3&&[r58MSAA resourceOptions]==0x30&&
   [r58MSAA usage]==4&&[r58MSAA mipmapLevelCount]==1&&r58MSAA->rowBytes==2432&&
   r58MSAA->mip[0].width==594&&r58MSAA->mip[0].height==594&&
   r58MSAA->mip[0].stride==2432&&!r58MSAA->mip[0].offset&&
   !r58MSAA->buffer&&r58MSAA->transientOwner==d&&[r58MSAA device]==d&&
   [r58MSAA allocatedSize]==0&&[d currentAllocatedSize]==r58Baseline);
 unsigned char r58Input=0x5c,r58Output=0xa9;
 [r58MSAA replaceRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0
   withBytes:&r58Input bytesPerRow:4];
 [r58MSAA getBytes:&r58Output bytesPerRow:4
   fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0];
 assert(r58Output==0xa9);
 assert(![d supportsSampleCount:4]&&![d supportsTextureSampleCount:4]);
 TeguQueue *r58Queue=[d newCommandQueue];TeguCommand *r58Command=[r58Queue commandBuffer];
 RenderPass *r58Pass=[[RenderPass alloc] init];r58Pass->color=[[PassColor alloc] init];
 r58Pass->color->target=[r58MSAA retain];r58Pass->color->load=2;r58Pass->color->store=1;
 r58Pass->passWidth=r58Pass->passHeight=594;r58Pass->arrayLength=1;r58Pass->sampleCount=4;
 assert(![r58Command renderCommandEncoderWithDescriptor:(id)r58Pass]&&r58Command->invalid&&!r58Command->count);
 [r58Pass release];[r58Queue release];
 /* A zero-draw Clear+Resolve pass writes the single-sample destination in
  * command order. It never exposes or fabricates multisample backing. */
 desc->format=80;desc->type=2;desc->w=desc->h=594;desc->dep=1;desc->levels=1;
 desc->samples=1;desc->arrays=1;desc->options=0x20;desc->uses=5;
 TeguTexture *r58Resolve=[d newTextureWithDescriptor:(id)desc];assert(r58Resolve);
 memset([r58Resolve->buffer contents],0xa5,r58Resolve->buffer->logical);
 TeguQueue *r58BlitQueue=[d newCommandQueue];TeguCommand *r58BlitCommand=[r58BlitQueue commandBuffer];
 TeguBlitEncoder *r58Blit=[r58BlitCommand blitCommandEncoder];
 [r58Blit copyFromTexture:r58MSAA sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0}
   sourceSize:(MTLSize){1,1,1} toTexture:r58Resolve destinationSlice:0 destinationLevel:0
   destinationOrigin:(TeguOrigin){0,0,0}];
 [r58Blit endEncoding];assert(r58BlitCommand->invalid&&!r58BlitCommand->count);
 [r58BlitCommand commit];assert([r58BlitCommand status]==5);[r58BlitQueue release];drain();
 TeguQueue *r58ResolveQueue=[d newCommandQueue];TeguCommand *r58ResolveCommand=[r58ResolveQueue commandBuffer];
 RenderPass *r58ResolvePass=[[RenderPass alloc] init];r58ResolvePass->color=[[PassColor alloc] init];
 r58ResolvePass->color->target=[r58MSAA retain];r58ResolvePass->color->resolve=[r58Resolve retain];
 r58ResolvePass->color->load=2;r58ResolvePass->color->store=2;
 r58ResolvePass->passWidth=r58ResolvePass->passHeight=594;r58ResolvePass->arrayLength=1;r58ResolvePass->sampleCount=4;
 TeguRenderEncoder *r58ResolveEncoder=[r58ResolveCommand renderCommandEncoderWithDescriptor:(id)r58ResolvePass];
 assert(r58ResolveEncoder&&!r58ResolveCommand->invalid&&r58ResolveCommand->encoding&&!r58ResolveCommand->count);
 [r58ResolvePass release];[r58Resolve release];[r58ResolveEncoder endEncoding];
 assert(r58ResolveCommand->count==1&&TEGU_JOB(r58ResolveCommand->jobChunks,0).blit==5);
 unsigned char *resolved=[TEGU_JOB(r58ResolveCommand->jobChunks,0).buffers[1] contents];
 assert(resolved[0]==0xa5&&resolved[3]==0xa5&&resolved[2376]==0xa5);
 [r58ResolveCommand commit];assert([r58ResolveCommand status]==4&&![r58ResolveCommand error]&&r58ResolveCommand->count==1);
 assert(resolved[0]==51&&resolved[1]==34&&resolved[2]==17&&resolved[3]==68&&
   resolved[2375]==68&&resolved[2376]==0xa5&&resolved[2431]==0xa5&&
   resolved[2432]==51&&resolved[1444551]==68&&resolved[1444552]==0xa5);
 [r58ResolveQueue release];drain();
 /* DontCare+Resolve has no color to preserve and legally records no job. */
 desc->format=80;desc->type=2;desc->w=desc->h=594;desc->samples=1;desc->options=0x20;desc->uses=5;
 TeguTexture *r58DontCareResolve=[d newTextureWithDescriptor:(id)desc];assert(r58DontCareResolve);
 TeguQueue *r58DontCareQueue=[d newCommandQueue];TeguCommand *r58DontCareCommand=[r58DontCareQueue commandBuffer];
 RenderPass *r58DontCarePass=[[RenderPass alloc] init];r58DontCarePass->color=[[PassColor alloc] init];
 r58DontCarePass->color->target=[r58MSAA retain];r58DontCarePass->color->resolve=[r58DontCareResolve retain];
 r58DontCarePass->color->load=0;r58DontCarePass->color->store=2;
 r58DontCarePass->passWidth=r58DontCarePass->passHeight=594;r58DontCarePass->arrayLength=1;r58DontCarePass->sampleCount=4;
 TeguRenderEncoder *r58DontCareEncoder=[r58DontCareCommand renderCommandEncoderWithDescriptor:(id)r58DontCarePass];
 assert(r58DontCareEncoder&&!r58DontCareCommand->invalid);[r58DontCareEncoder endEncoding];
 assert(!r58DontCareCommand->invalid&&!r58DontCareCommand->count);
 [r58DontCareCommand commit];assert([r58DontCareCommand status]==4);
 [r58DontCarePass release];[r58DontCareQueue release];drain();
 /* Every target and resolve subresource selector must be present and zero. */
 for(unsigned subresource=0;subresource<6;subresource++) {
  TeguQueue *invalidQueue=[d newCommandQueue];TeguCommand *invalidCommand=[invalidQueue commandBuffer];
  RenderPass *invalidPass=[[RenderPass alloc] init];invalidPass->color=[[PassColor alloc] init];
  invalidPass->color->target=[r58MSAA retain];invalidPass->color->resolve=[r58DontCareResolve retain];
  invalidPass->color->load=0;invalidPass->color->store=2;invalidPass->passWidth=invalidPass->passHeight=594;
  invalidPass->arrayLength=1;invalidPass->sampleCount=4;
  NSUInteger *fields[6]={&invalidPass->color->lvl,&invalidPass->color->slc,&invalidPass->color->dpl,
    &invalidPass->color->rlvl,&invalidPass->color->rslc,&invalidPass->color->rdpl};
  *fields[subresource]=1;
  assert(![invalidCommand renderCommandEncoderWithDescriptor:(id)invalidPass]&&invalidCommand->invalid&&!invalidCommand->count);
  [invalidPass release];[invalidCommand commit];assert([invalidCommand status]==5);
  [invalidQueue release];drain();
 }
 [r58DontCareResolve release];
 /* Every adjacent descriptor remains outside this allocation-only class. */
 desc->format=80;desc->type=4;desc->w=desc->h=594;desc->dep=1;desc->levels=1;
 desc->samples=4;desc->arrays=1;desc->options=0x30;desc->uses=4;
 desc->w=593;assert(![d newTextureWithDescriptor:(id)desc]);desc->w=594;
 desc->h=593;assert(![d newTextureWithDescriptor:(id)desc]);desc->h=594;
 desc->format=70;assert(![d newTextureWithDescriptor:(id)desc]);desc->format=80;
 desc->options=0x20;assert(![d newTextureWithDescriptor:(id)desc]);desc->options=0x30;
 desc->uses=5;assert(![d newTextureWithDescriptor:(id)desc]);desc->uses=4;
 desc->type=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->type=4;
 desc->samples=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->samples=4;
 desc->arrays=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->arrays=1;
 desc->levels=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->levels=1;
 [r58MSAA release];drain();assert([d currentAllocatedSize]==r58Baseline);
 /* R58 also records three exact private 2D intermediates.  They receive
  * lifetime-owned storage, but remain unavailable to CPU copies, fragment
  * resource binding and render-pass construction. */
 static const struct {
  NSUInteger width,height,format,usage,rowBytes,logical,allocated;
 } r58PrivateSpec[4]={
  {594,594,10,5,640,380160,393216},
  {594,594,80,5,2432,1444608,1458176},
  {297,297,80,7,1216,361152,376832},
  {594,594,10,7,640,380160,393216}
 };
 for(unsigned i=0;i<4;i++) {
  desc->format=r58PrivateSpec[i].format;desc->type=2;
  desc->w=r58PrivateSpec[i].width;desc->h=r58PrivateSpec[i].height;desc->dep=1;
  desc->levels=1;desc->samples=1;desc->arrays=1;desc->options=0x20;desc->uses=r58PrivateSpec[i].usage;
  NSUInteger privateBaseline=[d currentAllocatedSize];
  TeguTexture *privateTexture=[d newTextureWithDescriptor:(id)desc];assert(privateTexture);
  assert([privateTexture textureType]==2&&[privateTexture pixelFormat]==r58PrivateSpec[i].format&&
    [privateTexture width]==r58PrivateSpec[i].width&&[privateTexture height]==r58PrivateSpec[i].height&&
    [privateTexture depth]==1&&[privateTexture sampleCount]==1&&[privateTexture arrayLength]==1&&
    [privateTexture storageMode]==2&&[privateTexture resourceOptions]==0x20&&
    [privateTexture usage]==r58PrivateSpec[i].usage&&[privateTexture mipmapLevelCount]==1&&
    privateTexture->rowBytes==r58PrivateSpec[i].rowBytes&&
    privateTexture->mip[0].stride==r58PrivateSpec[i].rowBytes&&!privateTexture->mip[0].offset&&
    privateTexture->buffer->logical==r58PrivateSpec[i].logical&&
    [privateTexture allocatedSize]==r58PrivateSpec[i].allocated&&
    [d currentAllocatedSize]==privateBaseline+r58PrivateSpec[i].allocated&&
    tegu_r58_private_allocation_only(privateTexture,d));
  unsigned char privateBefore=*(unsigned char *)[privateTexture->buffer contents];
  unsigned char privateInput=0x39,privateOutput=0xb6;
  [privateTexture replaceRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0
    withBytes:&privateInput bytesPerRow:r58PrivateSpec[i].format==10?1:4];
  [privateTexture getBytes:&privateOutput bytesPerRow:r58PrivateSpec[i].format==10?1:4
    fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0];
  assert(*(unsigned char *)[privateTexture->buffer contents]==privateBefore&&privateOutput==0xb6);
  TeguAIRRenderResource privateResource;memset(&privateResource,0xa5,sizeof(privateResource));
  assert(!tegu_compositor_texture_resource(privateTexture,d,3,&privateResource));
  TeguQueue *privateQueue=[d newCommandQueue];
  /* The sphere subtest drains the shared host autorelease pool before this
   * command is submitted.  Keep the surrounding command alive explicitly. */
  TeguCommand *privateCommand=[[privateQueue commandBuffer] retain];
  RenderPass *privatePass=[[RenderPass alloc] init];privatePass->color=[[PassColor alloc] init];
  privatePass->color->target=[privateTexture retain];privatePass->color->load=2;privatePass->color->store=1;
  privatePass->passWidth=r58PrivateSpec[i].width;privatePass->passHeight=r58PrivateSpec[i].height;
  privatePass->arrayLength=privatePass->sampleCount=1;
  TeguRenderEncoder *privateEncoder=[privateCommand renderCommandEncoderWithDescriptor:(id)privatePass];
  if(r58PrivateSpec[i].format==10) {
   assert(privateEncoder&&privateEncoder->r8ClearOnly&&!privateCommand->invalid&&!privateCommand->count);
   [privateEncoder endEncoding];
   assert(!privateCommand->invalid&&!privateCommand->encoding&&privateCommand->count==1&&
     TEGU_JOB(privateCommand->jobChunks,0).blit==2&&TEGU_JOB(privateCommand->jobChunks,0).buffers[1]==privateTexture->buffer&&
     TEGU_JOB(privateCommand->jobChunks,0).destinationOffset==0&&
     TEGU_JOB(privateCommand->jobChunks,0).byteCount==r58PrivateSpec[i].logical&&TEGU_JOB(privateCommand->jobChunks,0).fillValue==17);
   if(i==0) {
    TeguCommand *bindRefusal=[privateQueue commandBuffer];
    TeguRenderEncoder *bindEncoder=[bindRefusal renderCommandEncoderWithDescriptor:(id)privatePass];
    TeguRenderPipeline *r8Pipeline=[[TeguRenderPipeline alloc] init];
    r8Pipeline->owner=[d retain];r8Pipeline->colorFormat=10;
    assert(bindEncoder&&bindEncoder->r8ClearOnly);[bindEncoder setRenderPipelineState:r8Pipeline];
    [r8Pipeline release];
    assert(bindRefusal->invalid&&!bindRefusal->count);[bindEncoder endEncoding];
   /* Only the diagnostic sphere class may cross the otherwise clear-only R8
    * pipeline gate.  Its vertex-stage resources remain owned by the encoder
    * even after the application releases its references. */
   TeguCommand *sphereCommand=[[privateQueue commandBuffer] retain];
   TeguRenderEncoder *sphereEncoder=[[sphereCommand renderCommandEncoderWithDescriptor:(id)privatePass] retain];
   TeguRenderPipeline *spherePipeline=[[TeguRenderPipeline alloc] init];
   spherePipeline->owner=[d retain];spherePipeline->colorFormat=10;spherePipeline->sphereObserverOnly=1;
#ifdef TEGU_RENDER_TRACE
   unsigned sphereEventsBefore=tegu_render_trace_sphere_events;
#endif
   assert(sphereEncoder&&sphereEncoder->r8ClearOnly);[sphereEncoder setRenderPipelineState:spherePipeline];
   assert(!sphereCommand->invalid&&sphereEncoder->pipeline==spherePipeline);[spherePipeline release];
   assert([sphereEncoder respondsToSelector:@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:)]&&
     [sphereEncoder respondsToSelector:@selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:)]);
   TeguBuffer *sphereStorage=[d newBufferWithLength:128 options:0];
   TeguBuffer *sphereUniform=[d newBufferWithLength:96 options:0];assert(sphereStorage&&sphereUniform);
   U64 sphereStorageHandle=sphereStorage->handle,sphereUniformHandle=sphereUniform->handle;
   TextureDesc *sphereTextureDesc=[[TextureDesc alloc] init];
   sphereTextureDesc->format=10;sphereTextureDesc->type=7;sphereTextureDesc->w=sphereTextureDesc->h=64;
   sphereTextureDesc->dep=64;sphereTextureDesc->levels=1;sphereTextureDesc->samples=1;
   sphereTextureDesc->arrays=1;sphereTextureDesc->options=0;sphereTextureDesc->uses=1;
   TeguTexture *sphereTexture=[d newTextureWithDescriptor:(id)sphereTextureDesc];assert(sphereTexture);
   U64 sphereTextureHandle=sphereTexture->buffer->handle;
   TeguAIRRenderResource sphereTextureResource={0};
   assert(tegu_sphere_texture_resource(sphereTexture,d,0,&sphereTextureResource)&&
     sphereTextureResource.kind==2&&sphereTextureResource.stage==1&&!sphereTextureResource.index&&
     sphereTextureResource.format==10&&sphereTextureResource.texture_type==7&&
     sphereTextureResource.width==64&&sphereTextureResource.height==64&&sphereTextureResource.usage==1);
   /* A neighboring 2D class and one-field mutations are not sphere samples. */
   sphereTexture->textureKind=2;assert(!tegu_sphere_texture_resource(sphereTexture,d,0,&sphereTextureResource));
   sphereTexture->textureKind=7;sphereTexture->format=11;
   assert(!tegu_sphere_texture_resource(sphereTexture,d,0,&sphereTextureResource));sphereTexture->format=10;
   sphereTexture->textureWidth=63;
   assert(!tegu_sphere_texture_resource(sphereTexture,d,0,&sphereTextureResource));sphereTexture->textureWidth=64;
   sphereTexture->textureUsage=5;
   assert(!tegu_sphere_texture_resource(sphereTexture,d,0,&sphereTextureResource));sphereTexture->textureUsage=1;
   TeguSampler *sphereSampler=[[TeguSampler alloc] init];sphereSampler->owner=[d retain];
   TeguBuffer *sphereBuffers[3]={sphereStorage,0,sphereUniform};NSUInteger sphereOffsets[3]={0,0,4};
   TeguTexture *sphereTextures[1]={sphereTexture};TeguSampler *sphereSamplers[1]={sphereSampler};
   [sphereEncoder setVertexBuffers:sphereBuffers offsets:sphereOffsets withRange:(TeguRange){0,3}];
   [sphereEncoder setVertexTextures:sphereTextures withRange:(TeguRange){0,1}];
   [sphereEncoder setVertexSamplerStates:sphereSamplers withRange:(TeguRange){0,1}];
   assert(!sphereCommand->invalid&&sphereEncoder->vertexBuffers[0]==sphereStorage&&
     sphereEncoder->vertexBuffers[2]==sphereUniform&&sphereEncoder->vertexOffsets[2]==4&&
     sphereEncoder->vertexTextures[0]==sphereTexture&&sphereEncoder->vertexSamplers[0]==sphereSampler);
   assert([sphereEncoder respondsToSelector:@selector(setVertexBuffers:offsets:withRange:)]&&
     [sphereEncoder respondsToSelector:@selector(setFragmentBuffers:offsets:withRange:)]&&
     [sphereEncoder respondsToSelector:@selector(setVertexTextures:withRange:)]&&
     [sphereEncoder respondsToSelector:@selector(setFragmentTextures:withRange:)]&&
     [sphereEncoder respondsToSelector:@selector(setVertexSamplerStates:withRange:)]&&
     [sphereEncoder respondsToSelector:@selector(setFragmentSamplerStates:withRange:)]);
   [sphereStorage release];[sphereUniform release];[sphereTexture release];[sphereSampler release];
   assert(memory[sphereStorageHandle]&&memory[sphereUniformHandle]&&memory[sphereTextureHandle]);
   [sphereEncoder endEncoding];assert(!sphereCommand->invalid&&sphereCommand->count==1&&
     TEGU_JOB(sphereCommand->jobChunks,0).blit==2);[sphereEncoder release];drain();
#ifdef TEGU_RENDER_TRACE
   assert(tegu_render_trace_sphere_events==sphereEventsBefore+2);
#endif
   assert(!memory[sphereStorageHandle]&&!memory[sphereUniformHandle]&&!memory[sphereTextureHandle]);
   [sphereCommand commit];assert([sphereCommand status]==4);[sphereCommand release];[sphereTextureDesc release];
   /* Public vertex resource setters preserve the same bounded slot policy as
    * their fragment counterparts. */
   TeguCommand *badTexture=[privateQueue commandBuffer];
   TeguRenderEncoder *badTextureEncoder=[badTexture renderCommandEncoderWithDescriptor:(id)privatePass];
   [badTextureEncoder setVertexTexture:0 atIndex:31];assert(badTexture->invalid&&!badTexture->count);
   [badTextureEncoder endEncoding];
   TeguCommand *badSampler=[privateQueue commandBuffer];
   TeguRenderEncoder *badSamplerEncoder=[badSampler renderCommandEncoderWithDescriptor:(id)privatePass];
   [badSamplerEncoder setVertexSamplerState:0 atIndex:16];assert(badSampler->invalid&&!badSampler->count);
   [badSamplerEncoder endEncoding];
   TeguCommand *badBulk=[privateQueue commandBuffer];
   TeguRenderEncoder *badBulkEncoder=[badBulk renderCommandEncoderWithDescriptor:(id)privatePass];
   TeguBuffer *badBulkBuffers[2]={0,0};NSUInteger badBulkOffsets[2]={0,0};
   [badBulkEncoder setVertexBuffers:badBulkBuffers offsets:badBulkOffsets withRange:(TeguRange){30,2}];
   assert(badBulk->invalid&&!badBulk->count);[badBulkEncoder endEncoding];
   /* The observer flag alone cannot turn an uncompiled or neighboring PSO
    * into an accepted draw.  Translation-owned reflection remains required. */
   TeguCommand *uncompiledSphere=[privateQueue commandBuffer];
   TeguRenderEncoder *uncompiledEncoder=[uncompiledSphere renderCommandEncoderWithDescriptor:(id)privatePass];
   TeguRenderPipeline *uncompiledPipeline=[[TeguRenderPipeline alloc] init];
   uncompiledPipeline->owner=[d retain];uncompiledPipeline->colorFormat=10;uncompiledPipeline->sphereObserverOnly=1;
   [uncompiledEncoder setRenderPipelineState:uncompiledPipeline];[uncompiledPipeline release];
   assert(!uncompiledSphere->invalid&&!uncompiledSphere->count);
   [uncompiledEncoder drawPrimitives:3 vertexStart:0 vertexCount:3];
   assert(uncompiledSphere->invalid&&!uncompiledSphere->count);[uncompiledEncoder endEncoding];
    TeguCommand *capacity=[privateQueue commandBuffer];capacity->count=TEGU_COMMAND_MAX_JOBS;
    TeguRenderEncoder *capacityEncoder=[capacity renderCommandEncoderWithDescriptor:(id)privatePass];
    assert(capacityEncoder&&capacityEncoder->r8ClearOnly);[capacityEncoder endEncoding];
    assert(capacity->invalid&&capacity->count==TEGU_COMMAND_MAX_JOBS&&!capacity->encoding);
   }
  } else assert(!privateEncoder&&privateCommand->invalid&&!privateCommand->count);
  /* One-field neighbors cannot widen this allocation-only class. */
  /* R117 admits any-size private single-level BGRA8 render/sample targets
   * (usage 1, 4 or 5); every other private class stays exact. */
  #define TEGU_DISPLAY_PRIVATE(x) ((x)->format==80&&((x)->uses==1||(x)->uses==4||(x)->uses==5))
  #define TEGU_NEIGHBOR(x) do{id t=[d newTextureWithDescriptor:(id)(x)];assert(TEGU_DISPLAY_PRIVATE(x)?t!=0:t==0);[t release];}while(0)
  desc->w--;TEGU_NEIGHBOR(desc);desc->w++;
  desc->h--;TEGU_NEIGHBOR(desc);desc->h++;
  desc->format=r58PrivateSpec[i].format==10?11:81;
  assert(![d newTextureWithDescriptor:(id)desc]);desc->format=r58PrivateSpec[i].format;
  desc->options=0x30;TEGU_NEIGHBOR(desc);desc->options=0x20; /* memoryless BGRA8 targets are admitted too */
  desc->uses--;TEGU_NEIGHBOR(desc);desc->uses++;
  desc->type=3;assert(![d newTextureWithDescriptor:(id)desc]);desc->type=2;
  desc->samples=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->samples=1;
  desc->arrays=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->arrays=1;
  desc->levels=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->levels=1;
  desc->dep=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->dep=1;
  TeguBuffer *privateBacking=[privateTexture->buffer retain];[privateTexture release];
  if(r58PrivateSpec[i].format!=10)drain();
  assert(privateBacking->logical==r58PrivateSpec[i].logical&&[privateBacking contents]&&
    [d currentAllocatedSize]==privateBaseline+r58PrivateSpec[i].allocated);
  if(r58PrivateSpec[i].format==10) {
   [privatePass release];privatePass=0;
   [privateCommand commit];[privateCommand waitUntilCompleted];
   assert([privateCommand status]==4&&![privateCommand error]);
   const unsigned char *filled=[privateBacking contents];
   for(NSUInteger byte=0;byte<privateBacking->logical;byte++)assert(filled[byte]==17);
  }
  [privatePass release];[privateCommand release];[privateQueue release];
  [privateBacking release];drain();assert([d currentAllocatedSize]==privateBaseline);
 }
 /* A single-sample R8 attachment cannot resolve into BGRA. */
 NSUInteger r63Baseline=[d currentAllocatedSize];
 desc->format=10;desc->type=2;desc->w=desc->h=594;desc->dep=1;desc->levels=1;
 desc->samples=1;desc->arrays=1;desc->options=0x20;desc->uses=7;
 TeguTexture *r63Target=[d newTextureWithDescriptor:(id)desc];assert(r63Target);
 desc->format=80;desc->uses=5;
 TeguTexture *r63Resolve=[d newTextureWithDescriptor:(id)desc];assert(r63Resolve);
 assert([d currentAllocatedSize]==r63Baseline+393216+1458176);
 TeguQueue *r63Queue=[d newCommandQueue];TeguCommand *r63Command=[r63Queue commandBuffer];
 RenderPass *r63Pass=[[RenderPass alloc] init];r63Pass->color=[[PassColor alloc] init];
 r63Pass->color->target=[r63Target retain];r63Pass->color->resolve=[r63Resolve retain];
 r63Pass->color->load=2;r63Pass->color->store=2;
 r63Pass->passWidth=r63Pass->passHeight=594;r63Pass->arrayLength=r63Pass->sampleCount=1;
 TeguRenderEncoder *r63Encoder=[r63Command renderCommandEncoderWithDescriptor:(id)r63Pass];
 assert(!r63Encoder&&r63Command->invalid&&!r63Command->encoding&&!r63Command->count);
 [r63Pass release];[r63Target release];[r63Resolve release];
 assert([d currentAllocatedSize]==r63Baseline);
 [r63Command commit];assert([r63Command status]==5&&[r63Command error]&&!r63Command->count);
 [r63Queue release];drain();assert([d currentAllocatedSize]==r63Baseline);
 desc->format=43;desc->type=2;desc->w=776;desc->h=24;desc->dep=1;desc->levels=10;
 desc->samples=1;desc->arrays=1;desc->options=0x20;desc->uses=5;
 /* Admission requires the compositor capability and every captured field. */
 d->capabilities&=~TEGU_GPU_CAP_COMPOSITOR;assert(![d newTextureWithDescriptor:(id)desc]);
 d->capabilities|=TEGU_GPU_CAP_COMPOSITOR;
 desc->levels=9;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->levels=11;assert(![d newTextureWithDescriptor:(id)desc]);desc->levels=10;
 desc->w=775;assert(![d newTextureWithDescriptor:(id)desc]);desc->w=776;
 desc->h=23;assert(![d newTextureWithDescriptor:(id)desc]);desc->h=24;
 desc->options=0x30;assert(![d newTextureWithDescriptor:(id)desc]);desc->options=0x20;
 desc->uses=4;assert(![d newTextureWithDescriptor:(id)desc]);desc->uses=5;
 desc->type=3;assert(![d newTextureWithDescriptor:(id)desc]);desc->type=2;
 desc->dep=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->dep=1;
 /* R52 creates five exact shared, shader-read 1D lookup tables before its
  * compositor PSOs. Preserve their Metal format widths and CPU upload path. */
 static const NSUInteger lookupFormat[5]={23,25,55,55,105};
 static const NSUInteger lookupWidth[5]={3072,4096,1024,4096,1024};
 static const NSUInteger lookupBytes[5]={6144,8192,4096,16384,8192};
 desc->type=0;desc->h=1;desc->options=0;desc->uses=1;desc->levels=1;
 for(unsigned i=0;i<5;i++) {
  desc->format=lookupFormat[i];desc->w=lookupWidth[i];
  TeguTexture *lookup=[d newTextureWithDescriptor:(id)desc];assert(lookup);
  assert([lookup textureType]==0&&[lookup pixelFormat]==lookupFormat[i]&&[lookup width]==lookupWidth[i]&&
   [lookup height]==1&&[lookup usage]==1&&![lookup resourceOptions]&&[lookup mipmapLevelCount]==1&&
   lookup->rowBytes==lookupBytes[i]&&lookup->mip[0].stride==lookupBytes[i]&&
   !lookup->mip[0].offset&&[lookup->buffer length]==lookupBytes[i]&&lookup->buffer->owner==d);
  unsigned char *input=malloc(lookupBytes[i]),*output=malloc(lookupBytes[i]);assert(input&&output);
  for(NSUInteger j=0;j<lookupBytes[i];j++)input[j]=(unsigned char)(j*29u+i);
  memset(output,0,lookupBytes[i]);
  [lookup replaceRegion:(TeguRegion){{0,0,0},{lookupWidth[i],1,1}} mipmapLevel:0 slice:0
    withBytes:input bytesPerRow:lookupBytes[i] bytesPerImage:lookupBytes[i]];
  [lookup getBytes:output bytesPerRow:lookupBytes[i]
    fromRegion:(TeguRegion){{0,0,0},{lookupWidth[i],1,1}} mipmapLevel:0];
  assert(!memcmp(input,output,lookupBytes[i]));free(output);free(input);[lookup release];
 }
 d->capabilities&=~TEGU_GPU_CAP_COMPOSITOR;desc->format=23;desc->w=3072;
 assert(![d newTextureWithDescriptor:(id)desc]);d->capabilities|=TEGU_GPU_CAP_COMPOSITOR;
 desc->format=23;desc->w=3071;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->w=3072;desc->format=25;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->format=23;desc->type=2;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->type=0;desc->h=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->h=1;
 desc->options=0x20;assert(![d newTextureWithDescriptor:(id)desc]);desc->options=0;
 desc->levels=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->levels=1;
 desc->uses=5;assert(![d newTextureWithDescriptor:(id)desc]);
 /* R54's exact format-10 allocations retain their texture kinds and use the
  * shared CPU upload/readback path with one byte per texel. */
 desc->format=10;desc->type=0;desc->w=16;desc->h=1;desc->options=0;desc->uses=1;desc->levels=1;
 TeguTexture *r54Lookup=[d newTextureWithDescriptor:(id)desc];assert(r54Lookup);
 assert([r54Lookup textureType]==0&&[r54Lookup pixelFormat]==10&&r54Lookup->rowBytes==16&&
   r54Lookup->mip[0].stride==16&&[r54Lookup->buffer length]==16&&r54Lookup->buffer->owner==d);
 unsigned char lookupInput[16],lookupOutput[16];
 for(unsigned i=0;i<16;i++)lookupInput[i]=(unsigned char)(i*11u+3u);
 memset(lookupOutput,0,sizeof(lookupOutput));
 [r54Lookup replaceRegion:(TeguRegion){{0,0,0},{16,1,1}} mipmapLevel:0 slice:0
   withBytes:lookupInput bytesPerRow:0 bytesPerImage:0];
 [r54Lookup getBytes:lookupOutput bytesPerRow:0 bytesPerImage:0
   fromRegion:(TeguRegion){{0,0,0},{16,1,1}} mipmapLevel:0 slice:0];
 assert(!memcmp(lookupInput,lookupOutput,sizeof(lookupInput)));
 for(unsigned i=0;i<16;i++)lookupInput[i]=(unsigned char)(0xf0u-i*7u);
 memset(lookupOutput,0,sizeof(lookupOutput));
 [r54Lookup replaceRegion:(TeguRegion){{0,0,0},{16,1,1}} mipmapLevel:0
   withBytes:lookupInput bytesPerRow:0];
 [r54Lookup getBytes:lookupOutput bytesPerRow:0
   fromRegion:(TeguRegion){{0,0,0},{16,1,1}} mipmapLevel:0];
 assert(!memcmp(lookupInput,lookupOutput,sizeof(lookupInput)));
 desc->type=7;desc->w=desc->h=64;desc->dep=64;
 TeguTexture *r54Image=[d newTextureWithDescriptor:(id)desc];assert(r54Image);
 const NSUInteger volumeBytes=64*64*64;
 assert([r54Image textureType]==7&&[r54Image pixelFormat]==10&&[r54Image depth]==64&&
   r54Image->textureDepth==64&&r54Image->rowBytes==64&&r54Image->mip[0].stride==64&&
   [r54Image->buffer length]==volumeBytes&&r54Image->buffer->owner==d);
 unsigned char *imageInput=malloc(volumeBytes),*imageOutput=malloc(volumeBytes);assert(imageInput&&imageOutput);
 for(NSUInteger i=0;i<volumeBytes;i++)imageInput[i]=(unsigned char)(i*19u+7u);
 memset(imageOutput,0,volumeBytes);
 [r54Image replaceRegion:(TeguRegion){{0,0,0},{64,64,64}} mipmapLevel:0 slice:0
   withBytes:imageInput bytesPerRow:64 bytesPerImage:4096];
 [r54Image getBytes:imageOutput bytesPerRow:64 bytesPerImage:4096
   fromRegion:(TeguRegion){{0,0,0},{64,64,64}} mipmapLevel:0 slice:0];
 assert(!memcmp(imageInput,imageOutput,volumeBytes));
 /* Nonzero z origins and padded host layouts use bytesPerImage between depth
  * planes while the backing remains tightly packed by its own row stride. */
 unsigned char patch[50],patchOut[40];memset(patch,0xee,sizeof(patch));
 for(NSUInteger z=0;z<2;z++)for(NSUInteger y=0;y<3;y++)for(NSUInteger x=0;x<4;x++)
  patch[z*25+y*7+x]=(unsigned char)(0x40+z*24+y*4+x);
 [r54Image replaceRegion:(TeguRegion){{5,6,7},{4,3,2}} mipmapLevel:0 slice:0
   withBytes:patch bytesPerRow:7 bytesPerImage:25];
 memset(patchOut,0xcc,sizeof(patchOut));
 [r54Image getBytes:patchOut bytesPerRow:6 bytesPerImage:20
   fromRegion:(TeguRegion){{5,6,7},{4,3,2}} mipmapLevel:0 slice:0];
 for(NSUInteger z=0;z<2;z++)for(NSUInteger y=0;y<3;y++) {
  assert(!memcmp(patch+z*25+y*7,patchOut+z*20+y*6,4));
  assert(patchOut[z*20+y*6+4]==0xcc&&patchOut[z*20+y*6+5]==0xcc);
 }
 assert(patchOut[18]==0xcc&&patchOut[19]==0xcc&&patchOut[38]==0xcc&&patchOut[39]==0xcc);
 unsigned char snapshot=*(unsigned char *)[r54Image->buffer contents];
 unsigned char rejectedVolumeWrite=0x5a,rejectedVolumeRead=0xa5;
 [r54Image replaceRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0 slice:0
   withBytes:&rejectedVolumeWrite bytesPerRow:0 bytesPerImage:0];
 [r54Image getBytes:&rejectedVolumeRead bytesPerRow:0 bytesPerImage:0
   fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0 slice:0];
 assert(*(unsigned char *)[r54Image->buffer contents]==snapshot&&rejectedVolumeRead==0xa5);
 [r54Image replaceRegion:(TeguRegion){{0,0,63},{1,1,2}} mipmapLevel:0 slice:0
   withBytes:patch bytesPerRow:1 bytesPerImage:1];
 assert(*(unsigned char *)[r54Image->buffer contents]==snapshot);
 free(imageOutput);free(imageInput);
 d->capabilities&=~TEGU_GPU_CAP_COMPOSITOR;
 assert(![d newTextureWithDescriptor:(id)desc]);
 d->capabilities|=TEGU_GPU_CAP_COMPOSITOR;
 desc->w=63;assert(![d newTextureWithDescriptor:(id)desc]);desc->w=64;
 desc->dep=0;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->dep=65;assert(![d newTextureWithDescriptor:(id)desc]);desc->dep=64;
 desc->samples=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->samples=1;
 desc->arrays=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->arrays=1;
 desc->type=0;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->type=7;desc->options=0x20;assert(![d newTextureWithDescriptor:(id)desc]);desc->options=0;
 desc->uses=5;assert(![d newTextureWithDescriptor:(id)desc]);desc->uses=1;
 desc->type=0;desc->dep=1;desc->w=16;desc->h=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->h=1;
 desc->levels=2;assert(![d newTextureWithDescriptor:(id)desc]);desc->levels=1;
 [r54Lookup release];[r54Image release];
 desc->type=2;desc->uses=5;desc->w=776;desc->h=24;desc->options=0x20;
 /* The backing owns its mapped allocation independently of the texture
  * wrapper, which is required when command snapshots retain the resource. */
 TeguBuffer *retainedBacking=[r44BGRA->buffer retain];[r44BGRA release];r44BGRA=0;
 assert(retainedBacking->owner==d&&retainedBacking->logical==99524&&[retainedBacking contents]);
 [retainedBacking release];
 /* Allocation support does not imply CPU access or GPU format support. */
 unsigned char before=*(unsigned char *)[half->buffer contents],byte=0xa5,out=0x7c;
 [half replaceRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0 withBytes:&byte bytesPerRow:1];
 [half getBytes:&out bytesPerRow:1 fromRegion:(TeguRegion){{0,0,0},{1,1,1}} mipmapLevel:0];
 assert(*(unsigned char *)[half->buffer contents]==before&&out==0x7c);
 desc->format=115;desc->levels=9;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->levels=10;desc->w=775;assert(![d newTextureWithDescriptor:(id)desc]);desc->w=776;
 desc->options=0x10;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->format=80;desc->levels=2;desc->options=0x30;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->levels=10;desc->uses=4;assert(![d newTextureWithDescriptor:(id)desc]);desc->uses=5;
 desc->w=775;assert(![d newTextureWithDescriptor:(id)desc]);desc->w=776;
 desc->options=0x20;desc->levels=9;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->levels=10;desc->uses=4;assert(![d newTextureWithDescriptor:(id)desc]);desc->uses=5;
 desc->format=43;desc->levels=1;desc->w=775;assert(![d newTextureWithDescriptor:(id)desc]);desc->w=776;
 desc->options=0x30;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->options=0x20;desc->levels=2;assert(![d newTextureWithDescriptor:(id)desc]);
 desc->levels=1;desc->options=0;assert(![d newTextureWithDescriptor:(id)desc]);
 [half release];[memoryless release];[planes release];[xr release];[r47MemorylessBGRA10 release];
 [r47MemorylessBGRA1 release];[r44R16 release];[desc release];
 drain();assert([d currentAllocatedSize]==baseline);d->capabilities=capabilities;
}
