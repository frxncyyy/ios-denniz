#define TEGU_AIR_BRIDGE 1
#define TEGU_MALI_BRIDGE 1
#define TEGU_GRAPHICS_BRIDGE 1
#define NORD2_JM_TRANSPORT 1
/* Host ownership/recording test with a simulated transport. This does not
 * qualify Metal or GPU execution; the native probe performs that check. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define TEGU_FRONTEND_HOST 1
#include "compute_frontend.m"
extern id class_createInstance(id, size_t);
extern id object_dispose(id);
static id pool[4096]; static unsigned poolCount,live;
@implementation NSObject
+ (id)alloc { NSObject *o=class_createInstance(self,0); o->_references=1; live++; return o; }
- (id)init { return self; }
- (id)retain { _references++; return self; }
- (oneway void)release { assert(_references); if(!--_references) [self dealloc]; }
- (id)autorelease { assert(poolCount<4096); pool[poolCount++]=self; return self; }
- (void)dealloc { live--; object_dispose(self); }
@end
__attribute__((objc_root_class)) @interface NXConstantString { void *isa; const char *bytes; unsigned length; } @end
@implementation NXConstantString @end
static id testErrorDescription;
#ifdef TEGU_AIR_BRIDGE
static id testDictionaryValue;
@implementation NSDictionary
+ (id)dictionaryWithObject:(id)object forKey:(id)key { assert(!strcmp([(NSString *)key UTF8String],"NSLocalizedDescription"));testDictionaryValue=object;return [[[self alloc] init] autorelease]; }
- (id)objectForKey:(id)key { (void)key;return testDictionaryValue; }
@end
#endif
@implementation NSError
+ (id)errorWithDomain:(id)domain code:(long)code userInfo:(id)info {
 (void)domain;(void)code;
#ifdef TEGU_AIR_BRIDGE
 testErrorDescription=[info objectForKey:@"NSLocalizedDescription"];
#else
 (void)info;
#endif
 return [[[self alloc] init] autorelease];
}
- (id)localizedDescription { return testErrorDescription; }
@end
@interface TestLabel : NSObject { @public unsigned value; }
@end
@implementation TestLabel
- (id)copy { TestLabel *duplicate=[[TestLabel alloc] init];duplicate->value=value;return duplicate; }
@end
static id strings[1024];static char *text[1024];
@implementation NSString
+ (id)stringWithUTF8String:(const char *)bytes {
 NSString *s=[[self alloc] init];
 for(unsigned i=0;i<1024;i++) if(!strings[i]) {strings[i]=s;text[i]=strdup(bytes);return [s autorelease];}
 abort();
}
- (id)copy {return [self retain];}
- (const char *)UTF8String { for(unsigned i=0;i<1024;i++) if(strings[i]==self)return text[i];abort(); }
- (signed char)isEqualToString:(id)other {return !strcmp([self UTF8String],[other UTF8String]);}
- (void)dealloc {for(unsigned i=0;i<1024;i++)if(strings[i]==self){free(text[i]);strings[i]=0;}[super dealloc];}
@end
@implementation NXConstantString (RuntimeString)
- (const char *)UTF8String {return bytes;}
@end
@interface RuntimeComputeDescriptor : NSObject { @public id function; }
- (id)computeFunction;
@end
@implementation RuntimeComputeDescriptor
- (id)computeFunction{return function;}
- (void)dealloc{[function release];[super dealloc];}
@end
@implementation NSMutableArray
+ (id)array {abort();}
- (void)addObject:(id)o {(void)o;abort();}
@end
@implementation NSData
+ (id)dataWithContentsOfFile:(id)p {
 if(!p)return 0;FILE *file=fopen([p UTF8String],"rb");if(!file)return 0;
 fseek(file,0,SEEK_END);long size=ftell(file);rewind(file);assert(size>=0);
 NSData *data=[[self alloc] init];data->testLength=(NSUInteger)size;
 data->testBytes=malloc(data->testLength?data->testLength:1);assert(data->testBytes);
 assert(fread(data->testBytes,1,data->testLength,file)==data->testLength);fclose(file);
 return [data autorelease];
}
- (const void *)bytes {return testBytes;}
- (NSUInteger)length {return testLength;}
- (void)dealloc {free(testBytes);[super dealloc];}
@end
static id libraryTestBundle,libraryTestPath;
static signed char libraryTestFileURL=1;
@implementation NSBundle
+ (id)mainBundle{return libraryTestBundle;}
- (id)pathForResource:(id)name ofType:(id)type {
 assert(!strcmp([name UTF8String],"default")&&!strcmp([type UTF8String],"metallib"));return libraryTestPath;
}
@end
@implementation NSURL
- (signed char)isFileURL{return libraryTestFileURL;}
- (id)path{return libraryTestPath;}
@end
@interface RenderColor : NSObject { @public NSUInteger format,mask,rgbOp,rgbSrc,rgbDst,alphaOp,alphaSrc,alphaDst; signed char blend; }
- (NSUInteger)writeMask;
@end
@implementation RenderColor
- (id)init {self=[super init];if(self){rgbSrc=alphaSrc=1;rgbDst=alphaDst=0;}return self;}
- (NSUInteger)pixelFormat{return format;}
- (NSUInteger)writeMask{return mask;}
- (signed char)isBlendingEnabled{return blend;}
- (NSUInteger)rgbBlendOperation{return rgbOp;}
- (NSUInteger)sourceRGBBlendFactor{return rgbSrc;}
- (NSUInteger)destinationRGBBlendFactor{return rgbDst;}
- (NSUInteger)alphaBlendOperation{return alphaOp;}
- (NSUInteger)sourceAlphaBlendFactor{return alphaSrc;}
- (NSUInteger)destinationAlphaBlendFactor{return alphaDst;}
@end
@interface RenderColors : NSObject { @public RenderColor *colors[8]; }
@end
@implementation RenderColors
- (id)objectAtIndexedSubscript:(NSUInteger)i {assert(i<8);return colors[i];}
- (void)dealloc {for(unsigned i=0;i<8;i++)[colors[i] release];[super dealloc];}
@end
@interface RenderAttribute : NSObject { @public NSUInteger value,byteOffset,bufferSlot,layoutStride,step,rate; }
@end
@implementation RenderAttribute
- (NSUInteger)format{return value;}
- (NSUInteger)offset{return byteOffset;}
- (NSUInteger)bufferIndex{return bufferSlot;}
- (NSUInteger)stride{return layoutStride;}
- (NSUInteger)stepFunction{return step;}
- (NSUInteger)stepRate{return rate;}
@end
@interface RenderLayout : NSObject { @public RenderAttribute *entries[31];RenderLayout *buffers; }
@end
@implementation RenderLayout
- (id)attributes{return self;}
- (id)layouts{return buffers;}
- (id)objectAtIndexedSubscript:(NSUInteger)i {assert(i<31);return entries[i];}
- (void)dealloc{for(unsigned i=0;i<31;i++)[entries[i] release];[buffers release];[super dealloc];}
@end
@interface RenderDescriptor : NSObject {
@public TeguFunction *vertex,*fragment;RenderColors *colors;id debugLabel,layout;
 NSUInteger samples,depth,stencil,topology,amplification;
 signed char coverage,alpha,raster,indirect;
}
@end
@implementation RenderDescriptor
- (id)vertexFunction{return vertex;}
- (id)fragmentFunction{return fragment;}
- (id)vertexDescriptor{return layout;}
- (id)colorAttachments{return colors;}
- (NSUInteger)rasterSampleCount{return samples;}
- (NSUInteger)depthAttachmentPixelFormat{return depth;}
- (NSUInteger)stencilAttachmentPixelFormat{return stencil;}
- (signed char)isAlphaToCoverageEnabled{return coverage;}
- (signed char)isAlphaToOneEnabled{return alpha;}
- (signed char)isRasterizationEnabled{return raster;}
- (NSUInteger)inputPrimitiveTopology{return topology;}
- (NSUInteger)maxVertexAmplificationCount{return amplification;}
- (signed char)supportIndirectCommandBuffers{return indirect;}
- (id)label{return debugLabel;}
- (void)dealloc{[vertex release];[fragment release];[colors release];[debugLabel release];[layout release];[super dealloc];}
@end
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

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* Production bridge tested against an independent transport model. It checks
 * packet/metadata/lifetime contracts; it does NOT emulate shader execution. */
#include "nord2_metal_bridge.h"
#include "tegu/pipeline_format.h"
#include "nord2_gpu_transport.h"
#include "nord2_metal_render.h"
#include "nord2_preload.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COUNT 600
struct buffer {uint64_t va,size;unsigned char *data;unsigned mapped,active,released,exec,gpu_only,surface;};
struct fake {
 unsigned last_chain,last_preframe;
 pthread_mutex_t mutex;struct buffer buffers[COUNT];uint32_t next;
 unsigned live,maps,closed,submitted,waited,bad_state,fail_alloc,fail_map,fail_submit,fail_wait,fail_release;
 unsigned geometry_fixture,heap_logs,before_logs,after_logs,log_bytes,nonzero_before_logs;
 unsigned sync_calls,fail_sync_at;
 /* tiler matrix model */
 unsigned render_model,tm_logs,adopt_logs,draw_submits,heap_allocs,const_fs_seen,nosec_seen,mask_seen[4];
 uint64_t heap_va,last_cmd_va;uint32_t last_mask,last_chunk,last_heap_size;char adopt_text[240];
 uint64_t job;unsigned pending;const unsigned char *pipeline;size_t pipeline_size;
 /* kernel 0090 model: WAIT_SEQ, busy-consumed release, in-flight window */
 unsigned seq_model,seq_waits,inflight,window,fail_seq,busy_releases,max_inflight;
 /* kernel 0096 model: ALLOC bit1 GPU-only (no_gpu_only = older kernel that
  * refuses it), and imported surfaces with a separate CPU view that only an
  * acquire (SYNC to CPU) refreshes: data = what the GPU sees/wrote,
  * surf_cpu[sid] = the CPU's (possibly stale) lines. */
 unsigned no_gpu_only,gpu_only_allocs,refused_maps,surf_model,surf_acquires;
 unsigned char *surf_cpu[4];size_t surf_bytes[4];
};
#define KBUSY_TEST ((int)0xe00002d5u)
static uint64_t get64(const void *v) {const unsigned char *p=v;return tegu_pipeline_word(p)|((uint64_t)tegu_pipeline_word(p+4)<<32);}
static void collect(struct fake *f,unsigned h) {struct buffer *b=&f->buffers[h];assert(b->active);free(b->data);b->data=NULL;b->active=0;f->live--;}
static int call(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os) {
 struct fake *f=p;
 switch(s) {
 case NORD2_GPU_SEL_LOG:
  /* Plain log writes remain valid when a wait failed; no GPU memory read. */
  assert(!ni&&bytes&&size>0&&size<240);
  if(size>=17&&!memcmp(bytes,"NORD2_METAL heap+",17))f->heap_logs++;
  f->log_bytes+=(unsigned)size;assert(f->log_bytes<=16384);
  if(size>=27&&!memcmp(bytes,"NORD2_METAL geometry-before ",27)){
   f->before_logs++;
   char text[240];memcpy(text,bytes,size);text[size]=0;
   unsigned ordinal=0,nonzero=0;
   assert(sscanf(text,"NORD2_METAL geometry-before ordinal=%u sampled=65792 nonzero=%u",&ordinal,&nonzero)==2);
   assert(ordinal>=1&&ordinal<=8&&nonzero<=65792);
   f->nonzero_before_logs+=nonzero!=0;
  }
  if(size>=26&&!memcmp(bytes,"NORD2_METAL geometry-after ",26))f->after_logs++;
  if(size>=15&&!memcmp(bytes,"NORD2_METAL tm ",15)){f->tm_logs++;if(getenv("TM_DUMP")&&f->render_model)fwrite(bytes,1,size,stderr);}
  if(size>=21&&!memcmp(bytes,"NORD2_METAL tm adopt=",21)){f->adopt_logs++;memcpy(f->adopt_text,bytes,size);f->adopt_text[size]=0;}
  return 0;
 case NORD2_GPU_SEL_STATUS: {
  assert(!ni&&!bytes&&!size&&ob&&*os==sizeof(struct nord2_gpu_status));
  struct nord2_gpu_status status={.abi_version=2,.enabled=1,.powered=1,.arch_ok=1,.gpu_id=0x90800011,.tiler_features=0x809};
  if(f->bad_state)status.powered=0;memcpy(ob,&status,sizeof(status));return 0;
 }
 case NORD2_GPU_SEL_ALLOC: {
  assert(ni==2&&*no==3&&!size);if(f->fail_alloc){f->fail_alloc--;return -10;}
  if(in[1]>(f->no_gpu_only?1u:3u))return (int)0xe00002c2u; /* kIOReturnBadArgument */
  unsigned h=++f->next;assert(h<COUNT);struct buffer *b=&f->buffers[h];
  b->size=(in[0]+16383)&~16383ull;b->va=0x1000000000ull+h*0x2000000ull; /* 32 MiB apart: room for a 16 MiB heap */
  if(in[0]>=(2u<<20))f->heap_allocs++;
  b->data=calloc(1,b->size);assert(b->data);b->active=1;b->exec=(unsigned)in[1];f->live++;
  b->gpu_only=(in[1]&2)!=0;b->surface=0;f->gpu_only_allocs+=b->gpu_only;
  out[0]=h;out[1]=b->va;out[2]=b->size;*no=3;return 0;
 }
 case NORD2_GPU_SEL_SYNC:
  assert(ni==2&&in[0]<COUNT&&in[1]==0&&!f->pending);
  if(f->surf_model&&f->buffers[in[0]].surface){unsigned sid=f->buffers[in[0]].surface;
   memcpy(f->surf_cpu[sid],f->buffers[in[0]].data,f->surf_bytes[sid]);f->surf_acquires++;} /* acquire: CPU sees GPU writes */
  return ++f->sync_calls==f->fail_sync_at?-16:0;
 case NORD2_GPU_SEL_RELEASE: {
  assert(ni==1&&in[0]<COUNT&&!size);if(f->fail_release){f->fail_release--;return -15;}
  struct buffer *b=&f->buffers[in[0]];assert(b->active&&!b->mapped);
  if(f->pending){b->released=1;if(f->seq_model){f->busy_releases++;return KBUSY_TEST;}}else collect(f,(unsigned)in[0]);return 0;
 }
 case NORD2_GPU_SEL_SUBMIT_JM: {
  assert(!ni&&size==160&&bytes&&*no==1);
  const struct nord2_gpu_submit_request *r=bytes;
  assert(r->version==2);
  if(r->kind==2){
   assert(r->nr_handles>=5&&r->heads[0]&&r->heads[1]);
   unsigned command=0;
   for(unsigned k=0;k<r->nr_handles;k++){
    unsigned h=r->handles[k];assert(h<COUNT&&f->buffers[h].active);
    if(r->heads[1]>=f->buffers[h].va&&r->heads[1]<f->buffers[h].va+f->buffers[h].size)command=h;
   }
   assert(command);struct buffer *c=&f->buffers[command];
   if(f->geometry_fixture){
    uint64_t base=get64(c->data+768+8),bottom=base+(f->geometry_fixture==2?UINT64_C(0x400000):65536);
    memcpy(c->data+768+16,&bottom,8);
    for(unsigned k=0;k<r->nr_handles;k++){
     struct buffer *heap=&f->buffers[r->handles[k]];
     if(heap->va==base){assert(heap->size==2u<<20);if(f->geometry_fixture!=3)memset(heap->data,0x37,65536);}
    }
   }
   assert(r->heads[1]==c->va+N2R_FRAGMENT);
   {/* Capture tiler configuration from the packet (v9.xml word offsets). */
    uint32_t w;memcpy(&w,c->data+512+8,4);f->last_mask=w&0x1fff;memcpy(&w,c->data+768,4);f->last_chunk=(w>>8)&3;
    assert((w&0xff)==0x29);memcpy(&f->last_heap_size,c->data+768+4,4);f->last_cmd_va=c->va;
    uint64_t base=get64(c->data+768+8);assert((f->geometry_fixture||get64(c->data+768+16)==base)&&get64(c->data+768+24)==base+f->last_heap_size);
    /* One persistent heap: it changes only after the previous one was
     * released (the tiler matrix's own mapped heap, kernel 0096). */
    if(f->heap_va&&base!=f->heap_va){unsigned live=0;for(unsigned k=1;k<=f->next;k++)live|=f->buffers[k].active&&f->buffers[k].va==f->heap_va;
     assert(!live);f->heap_va=0;}
    if(!f->heap_va)f->heap_va=base;assert(base==f->heap_va);
    unsigned found=0;for(unsigned k=0;k<r->nr_handles;k++){struct buffer *hb=&f->buffers[r->handles[k]];
     if(hb->va==base){found=1;assert(r->write_mask&(1u<<k));assert(f->last_heap_size<=hb->size);}}
    assert(found);
    if(f->last_mask==0x01)f->mask_seen[0]++;if(f->last_mask==0x82)f->mask_seen[1]++;if(f->last_mask==0xff)f->mask_seen[2]++;
    memcpy(&w,c->data+N2R_JOB+32,4);unsigned sec=(w>>18)&1;memcpy(&w,c->data+N2R_JOB+52,4);
    if(!sec){f->nosec_seen++;assert(w==16);for(unsigned k=0;k<64;k++)assert(!c->data[N2R_JOB+320+k]);}
    else assert((w&0xffff)==(w>>16)+16);
    uint64_t fs_code=get64(c->data+1600+8);
    for(unsigned k=0;k<r->nr_handles;k++){struct buffer *hb=&f->buffers[r->handles[k]];
     if(hb->va==fs_code&&hb->data[0]==192&&hb->data[5]==192&&hb->data[32]==124)f->const_fs_seen++;}
    f->draw_submits++;
    if(f->render_model){
     /* Model: only 2 MiB heap with 256 KiB chunks "rasterises" (green). */
     uint64_t rt=get64(c->data+128+32);int green=f->last_chunk==0&&f->last_heap_size==(2u<<20)&&f->last_mask==0x82;
     for(unsigned k=0;k<r->nr_handles;k++){struct buffer *hb=&f->buffers[r->handles[k]];
      if(hb->va==rt&&hb->size>=16384)for(unsigned i=0;i<4096;i++){uint32_t v=green?0xff00ff00u:0xffff0000u;memcpy(hb->data+4*i,&v,4);}}
    }
   }
   assert(get64(c->data+N2R_FRAGMENT+40)==(c->va|1));
   {/* Tiler chain inside the command buffer: every job at a block's JOB
     * offset, indices 1.. ascending along JOB_HEADER.next, each depending on
     * its predecessor, block 0 (the frame owner) included, ending at next=0. */
    uint64_t h=r->heads[0];unsigned steps=0,seen0=0;
    while(h){
     assert(h>=c->va&&h<c->va+c->size&&((h-c->va)%N2R_BLOCK)==N2R_JOB&&steps<=N2MB_BATCH_CAPACITY+1);
     const uint8_t *jh=c->data+(h-c->va);uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
     steps++;assert((w4>>16)==steps&&(w5&0xffff)==(steps>1?steps-1:0));
     if(h==c->va+N2R_JOB)seen0=1;
     h=get64(jh+24);
    }
    assert(seen0&&steps>=1);f->last_chain=steps;
   }
   {/* Pre-frame preload: FRAMEBUFFER_PARAMETERS word0 pre_frame_0 (bits 0-2,
     * pre_frame_1/post_frame NEVER) and frame_shader_dcds (word 6) at
     * another block's DCD array, whose job slot is not a job. */
    uint32_t w0;memcpy(&w0,c->data,4);uint64_t d=get64(c->data+24);f->last_preframe=w0&7;
    if(w0&0x1ff){
     assert(!(w0&0x1f8)&&d>=c->va+N2R_BLOCK&&d<c->va+c->size&&((d-c->va)%N2R_BLOCK)==N2R_FRAME_DCDS);
     const uint8_t *dcd=c->data+(d-c->va);
     for(unsigned k=0;k<128;k++)assert(!(dcd-128)[k]&&!dcd[128+k]&&!dcd[256+k]); /* no job header, DCD1/DCD2 zero */
     uint32_t f0,f1;memcpy(&f0,dcd,4);memcpy(&f1,dcd+4,4);
     assert((f0&(1u<<9))&&((f1>>16)&0xff)==1&&!(get64(dcd+8)&1)); /* clean fragment write, RT0, no vertex packet */
     uint64_t spd=get64(dcd+64+40); /* Shader Environment word 10 */assert(spd>=c->va&&spd<c->va+c->size&&(spd-d)==1600-N2R_FRAME_DCDS);
    } else assert(!d);
   }
   if(f->fail_submit){f->fail_submit--;return -12;}
   if(f->window&&f->inflight>=f->window)return KBUSY_TEST;
   f->inflight++;if(f->inflight>f->max_inflight)f->max_inflight=f->inflight;
   f->submitted++;f->pending=1;out[0]=++f->job;*no=1;return 0;
  }
  assert(r->version==2&&r->kind==1&&r->nr_handles==3&&r->write_mask==4&&!r->heads[1]);
  unsigned hc=r->handles[0],hs=r->handles[1],hr=r->handles[2];
  assert(hc<COUNT&&hs<COUNT&&hr<COUNT);struct buffer *c=&f->buffers[hc],*shader=&f->buffers[hs],*res=&f->buffers[hr];
  assert(c->active&&shader->active&&res->active&&shader->exec&&!c->exec&&!res->exec);
  assert(r->heads[0]==c->va);
  /* SHADER_ENV at COMPUTE_JOB+64, offset32 onward: resource, program,
   * TLS, FAU. Test against independent word offsets from v9.xml. */
  assert(get64(c->data+104)==c->va+128);assert(get64(c->data+112)==c->va+192);
  uint64_t fau=get64(c->data+120);assert(fau==c->va+256);
  assert(tegu_pipeline_word(c->data+68)==17); /* real compiler reports33 FAU words */
  assert(tegu_pipeline_word(c->data+128)==0x80000018u);
  assert(tegu_pipeline_word(c->data+132)==0x1000);assert(get64(c->data+136)==shader->va);
  assert(get64(c->data+256)==res->va+32);assert(tegu_pipeline_word(c->data+264)==1024);assert(!tegu_pipeline_word(c->data+268));
  assert(tegu_pipeline_word(c->data+384)==0x12345678);assert(tegu_pipeline_word(c->data+512)==3);
  assert(!memcmp(shader->data,f->pipeline+64,f->pipeline_size-64));
  if(f->fail_submit){f->fail_submit--;return -12;}
  f->submitted++;f->pending=1;out[0]=++f->job;*no=1;return 0;
 }
 case 12: { /* WAIT_SEQ (kernel 0090) */
  if(!f->seq_model)return (int)0xe00002c7u;
  assert(ni==2&&*no==3&&in[1]<=5000&&(in[0]==0||in[0]==f->job));
  if(!in[0]){out[0]=4;out[1]=0;out[2]=0;return 0;}
  f->seq_waits++;out[1]=f->inflight;
  if(f->fail_seq){f->fail_seq--;out[0]=5;out[2]=f->job-f->inflight+1;}else{out[0]=4;out[2]=0;}
  f->pending=0;f->inflight=0;
  for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active&&f->buffers[h].released){f->buffers[h].released=0;collect(f,h);}
  return 0;
 }
 case NORD2_GPU_SEL_WAIT: {
  assert(ni==2&&in[0]==f->job&&in[1]==5000&&*no==1);
  f->waited++;if(f->fail_wait){f->fail_wait--;return -13;}
  if(f->bad_state){out[0]=2;return 0;}f->pending=0;f->inflight=0;out[0]=4;return 0;
 }
 default:
  if(s==NORD2_GPU_SEL_IMPORT_SURFACE&&f->surf_model){
   assert(ni==4&&*no==3&&in[0]>0&&in[0]<4&&f->surf_cpu[in[0]]&&in[2]==f->surf_bytes[in[0]]);
   unsigned h=++f->next;assert(h<COUNT);struct buffer *b=&f->buffers[h];
   b->size=(in[2]+16383)&~16383ull;b->va=0x1000000000ull+h*0x2000000ull;
   b->data=calloc(1,b->size);assert(b->data);memcpy(b->data,f->surf_cpu[in[0]],in[2]);
   b->active=1;b->exec=0;b->gpu_only=0;b->surface=(unsigned)in[0];f->live++;
   out[0]=h;out[1]=b->va;out[2]=in[2];*no=3;return 0;
  }
  assert(!"unexpected kernel selector");return -99;
 }
}
static int map(void *p,unsigned h,uint64_t *address,uint64_t *size) {
 struct fake *f=p;if(f->fail_map){f->fail_map--;return -11;}
 assert(h<COUNT);struct buffer *b=&f->buffers[h];assert(b->active&&!b->mapped);
 if(b->gpu_only){f->refused_maps++;return (int)0xe00002f0u;} /* kIOReturnNotFound: no CPU mapping */
 b->mapped=1;f->maps++;*address=(uintptr_t)b->data;*size=b->size;return 0;
}
static int unmap(void *p,unsigned h,uint64_t address) {
 struct fake *f=p;assert(h<COUNT);struct buffer *b=&f->buffers[h];
 assert(b->active&&b->mapped&&address==(uintptr_t)b->data);b->mapped=0;f->maps--;return 0;
}
static int close_raw(void *p) {
 struct fake *f=p;assert(!f->maps);f->closed=1;
 for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active)collect(f,h);
 return 0;
}
static void lock(void *p){assert(!pthread_mutex_lock(&((struct fake *)p)->mutex));}
static void unlock(void *p){assert(!pthread_mutex_unlock(&((struct fake *)p)->mutex));}
static uintptr_t open_bridge(struct fake *f) {
 assert(!pthread_mutex_init(&f->mutex,NULL));struct n2mb_ops ops={.opaque=f,.call=call,.map=map,.unmap=unmap,.close=close_raw,.lock=lock,.unlock=unlock};
 uintptr_t b=n2mb_open(&ops);assert(b);uint64_t caps[4];unsigned n=4;
 assert(!n2mb_scalar(b,0,NULL,0,caps,&n)&&n==4&&(caps[1]&8));return b;
}
static void close_bridge(uintptr_t b,struct fake *f){assert(!n2mb_close(b)&&f->closed&&!f->live&&!f->maps);assert(!pthread_mutex_destroy(&f->mutex));}

/* ---- pipeline replay main ---- */
#include "pipeline_replay_host_test.inc"
static int scalar_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,U64 *out,unsigned *no){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_scalar(c,sel,ni?a:0,ni,out?b:0,no);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int method_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,const void *data,NSUInteger bytes,U64 *out,unsigned *no,void *output,NSUInteger *size){
 uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));
 if(ni)memcpy(a,in,ni*8);int e=n2mb_method(c,sel,ni?a:0,ni,data,bytes,out?b:0,no,output,size);
 if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;
}
static int map_adapter(TeguConnection c,unsigned h,unsigned task,U64 *a,U64 *n,unsigned flags){uint64_t x=0,y=0;int e=n2mb_map(c,h,task,&x,&y,flags);*a=x;*n=y;return e;}
static int unmap_adapter(TeguConnection c,unsigned h,unsigned task,U64 a){return n2mb_unmap(c,h,task,a);}
int main(int argc,char **argv){
 if(argc!=4){fprintf(stderr,"usage: %s <metallib dir> <records> <out dir>\n",argv[0]);return 2;}
 const char *build=getenv("TEGU_PIPELINE_CACHE_BUILD_OVERRIDE");
 if(!build||strncmp(build,"nord2-arch9-",12)){fprintf(stderr,"TEGU_PIPELINE_CACHE_BUILD_OVERRIDE must be the device build id\n");return 2;}
 struct fake f={0};uintptr_t b=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->connection=b;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 uint64_t caps[4];unsigned n=4;assert(!n2mb_scalar(b,0,0,0,caps,&n));d->capabilities=caps[1];
 test_pipeline_replay(d,argv[1],argv[2],argv[3]);
 d->connection=0;return 0;
}
NSUInteger tegu_host_constant_data_size(NSUInteger type) {
 switch(type){case 53:case 45:case 49:return 1;case 16:case 37:case 41:return 2;
 case 3:case 29:case 33:return 4;default:return 0;}
}
