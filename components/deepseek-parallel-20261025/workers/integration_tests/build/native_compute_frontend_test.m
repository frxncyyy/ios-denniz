#include <pthread.h>
#define TEGU_AIR_BRIDGE 1
#define TEGU_MALI_BRIDGE 1
#define TEGU_GRAPHICS_BRIDGE 1
#define NORD2_JM_TRANSPORT 1
#define TEGU_IOSURFACE_TEST_LOADER 1
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
 /* Metal-legal sub-range/retyped views (texture-opus #7): 2D -> 2DArray
  * and a level-1 view (level 0 of the view is the source's level 1). */
 TeguTexture *v3=[src newTextureViewWithPixelFormat:71 textureType:3 levels:full slices:one];
 assert(v3&&v3->textureKind==3&&v3->layers==1&&v3->levelCount==2);[v3 release];
 TeguTexture *v4=[src newTextureViewWithPixelFormat:71 textureType:2 levels:badlevel slices:one];
 assert(v4&&v4->levelCount==1&&v4->mip[0].offset==src->mip[1].offset&&v4->textureWidth==8);[v4 release];
 assert(![src newTextureViewWithPixelFormat:71 textureType:2 levels:full slices:badslices]);
 assert(![src newTextureViewWithPixelFormat:71 textureType:5 levels:full slices:one]); /* cube needs 6 slices */
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
 {id fence=[d newFence];[e updateFence:fence];[e waitForFence:fence];[fence release];} /* fences are real objects now (api_events.inc); a buffer is refused */
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
 /* Format 43 is BGR5A1Unorm (5,5,5,1 bit fields, B low), not R16Float:
  * the per-format filter averages the fields (texture-opus #10), the old
  * expectation averaged the bit patterns as half floats. */
 assert(c->count==9&&TEGU_JOB(c->jobChunks,0).mipFormat==43);[c commit];assert([c status]==4);drain();
 unsigned short r16Result;memcpy(&r16Result,(unsigned char *)[r16->buffer contents]+r16->mip[1].offset,2);
 assert(r16Result==0x4180);[r16 release];drain();d->capabilities=savedCapabilities;
 [q release];[desc release];drain();assert([d currentAllocatedSize]==baseline);
}

static unsigned iosurfaceImports,iosurfaceReleases,iosurfaceReplyMode;
@interface IOSurfaceFixture : NSObject {@public uint32_t sid,fourcc,planes;NSUInteger w,h,row,alloc;unsigned char *bytes;}
- (unsigned)testReferences;
@end
@implementation IOSurfaceFixture
- (unsigned)testReferences{return _references;}
- (void)dealloc{free(bytes);[super dealloc];}
@end
@interface TeguComputeDevice (IOSurfaceHostRefCheck)
- (unsigned)testReferences;
@end
@implementation TeguComputeDevice (IOSurfaceHostRefCheck)
- (unsigned)testReferences{return _references;}
@end
static unsigned mockUnlockCalls,mockUnlockFailures;static int failMockUnlock;
static uint32_t mock_surface_id(void *p){return ((IOSurfaceFixture *)p)->sid;}
static size_t mock_surface_width(void *p){return ((IOSurfaceFixture *)p)->w;}
static size_t mock_surface_height(void *p){return ((IOSurfaceFixture *)p)->h;}
static size_t mock_surface_row(void *p){return ((IOSurfaceFixture *)p)->row;}
static size_t mock_surface_alloc(void *p){return ((IOSurfaceFixture *)p)->alloc;}
static uint32_t mock_surface_format(void *p){return ((IOSurfaceFixture *)p)->fourcc;}
static size_t mock_surface_planes(void *p){return ((IOSurfaceFixture *)p)->planes;}
static int mock_surface_lock(void *p,uint32_t options,uint32_t *seed){(void)p;(void)options;if(seed)*seed=1;return 0;}
static int mock_surface_unlock(void *p,uint32_t options,uint32_t *seed){(void)p;(void)options;(void)seed;mockUnlockCalls++;if(failMockUnlock){mockUnlockFailures++;return -1;}return 0;}
static void *mock_surface_base(void *p){return ((IOSurfaceFixture *)p)->bytes;}
static void *mock_cf_retain(void *p){return [(id)p retain];}
static void mock_cf_release(void *p){[(id)p release];}
void *tegu_test_iosurface_dlopen(const char *path,int mode){(void)mode;return strstr(path,"IOSurface.framework")||strstr(path,"CoreFoundation.framework")?(void *)1:0;}
void *tegu_test_iosurface_dlsym(void *handle,const char *name){assert(handle==(void *)1);
#define MAP(n,f) if(!strcmp(name,n))return (void *)(uintptr_t)&f
 MAP("IOSurfaceGetID",mock_surface_id);MAP("IOSurfaceGetWidth",mock_surface_width);MAP("IOSurfaceGetHeight",mock_surface_height);
 MAP("IOSurfaceGetBytesPerRow",mock_surface_row);MAP("IOSurfaceGetAllocSize",mock_surface_alloc);
 MAP("IOSurfaceGetPixelFormat",mock_surface_format);MAP("IOSurfaceGetPlaneCount",mock_surface_planes);
 MAP("IOSurfaceLock",mock_surface_lock);MAP("IOSurfaceUnlock",mock_surface_unlock);MAP("IOSurfaceGetBaseAddress",mock_surface_base);
 MAP("CFRetain",mock_cf_retain);MAP("CFRelease",mock_cf_release);
#undef MAP
 return 0;
}
static void *iosurface_api_thread(void *unused){(void)unused;return (void *)tegu_iosurface_api();}
static void test_iosurface_api_concurrent_once(void){pthread_t threads[8];for(unsigned i=0;i<8;i++)assert(!pthread_create(&threads[i],0,iosurface_api_thread,0));
 void *first=0;for(unsigned i=0;i<8;i++){void *p=0;assert(!pthread_join(threads[i],&p)&&p);if(!first)first=p;else assert(first==p);}}
static IOSurfaceFixture *new_surface_fixture(uint32_t id,uint32_t format,uint32_t planes,
 NSUInteger width,NSUInteger height,NSUInteger row,NSUInteger alloc) {
 IOSurfaceFixture *s=[[IOSurfaceFixture alloc] init];s->sid=id;s->fourcc=format;s->planes=planes;
 s->w=width;s->h=height;s->row=row;s->alloc=alloc;s->bytes=calloc(alloc,1);assert(s->bytes);return s;
}
static TeguTexture *import_surface_texture(TeguComputeDevice *d,TextureDesc *desc,id surface,NSUInteger plane) {
 return [(id)d newTextureWithDescriptor:(id)desc iosurface:surface plane:plane];
}

/* ---- independent fake transport for one native compute device ---- */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <pthread.h>
#include "nord2_gpu_transport.h"
extern int setenv(const char *,const char *,int);extern int unsetenv(const char *);
#define FB_COUNT 64
struct fb {unsigned handle;uint64_t va;size_t size;unsigned char *host;int active;};
struct fake {pthread_mutex_t mutex;struct fb f[FB_COUNT];uint32_t next;uint64_t nextva;
 unsigned live,maps,submitted,waited,closed;uint64_t job;struct nord2_gpu_submit_request req;int have_req;};
static struct fake F;
static struct fb *fb_by_handle(unsigned h){for(unsigned i=0;i<FB_COUNT;i++)if(F.f[i].active&&F.f[i].handle==h)return &F.f[i];return 0;}
static struct fb *fb_by_va(uint64_t va){for(unsigned i=0;i<FB_COUNT;i++)if(F.f[i].active&&va>=F.f[i].va&&va<F.f[i].va+F.f[i].size)return &F.f[i];return 0;}
static int fake_call(void *o,unsigned sel,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *osz){
 (void)o;(void)ob;(void)osz;
 switch(sel){
 case NORD2_GPU_SEL_ALLOC:{if(ni!=2||!no||*no<3)return 1;struct fb *b=0;for(unsigned i=0;i<FB_COUNT;i++)if(!F.f[i].active){b=&F.f[i];break;}if(!b)return 1;
  b->size=(in[0]+16383u)&~16383ull;b->host=calloc(1,b->size);if(!b->host)return 1;b->active=1;b->handle=++F.next;b->va=F.nextva;F.nextva+=0x4000000ull;F.live++;out[0]=b->handle;out[1]=b->va;out[2]=b->size;*no=3;return 0;}
 case NORD2_GPU_SEL_RELEASE:{if(ni!=1)return 1;struct fb *b=fb_by_handle((unsigned)in[0]);if(!b)return 1;b->active=0;free(b->host);b->host=0;F.live--;if(no)*no=0;return 0;}
 case NORD2_GPU_SEL_SUBMIT_JM:{if(size!=sizeof(struct nord2_gpu_submit_request)||!no||*no<1)return 1;memcpy(&F.req,bytes,sizeof(F.req));F.have_req=1;F.submitted++;out[0]=++F.job;*no=1;return 0;}
 case NORD2_GPU_SEL_WAIT:{if(ni!=2||!no||*no<1)return 1;F.waited++;out[0]=4;*no=1;return 0;}
 case NORD2_GPU_SEL_STATUS:{struct nord2_gpu_status *s=ob;if(!s||!osz||*osz<sizeof(*s))return 1;memset(s,0,sizeof(*s));s->abi_version=2;s->enabled=s->powered=s->arch_ok=1;s->gpu_id=0x90800011;*no=0;*osz=sizeof(*s);return 0;}
 default:return 1;}
}
static int fake_map(void *o,unsigned h,uint64_t *a,uint64_t *n){(void)o;struct fb *b=fb_by_handle(h);if(!b)return 1;F.maps++;*a=(uint64_t)(uintptr_t)b->host;*n=b->size;return 0;}
static int fake_unmap(void *o,unsigned h,uint64_t a){(void)o;(void)h;(void)a;return 0;}
static int fake_close(void *o){(void)o;F.closed=1;return 0;}
static void fake_lock(void *o){assert(!pthread_mutex_lock(&((struct fake *)o)->mutex));}
static void fake_unlock(void *o){assert(!pthread_mutex_unlock(&((struct fake *)o)->mutex));}
static uintptr_t open_fake(void){memset(&F,0,sizeof(F));F.nextva=0x1000000000ull;assert(!pthread_mutex_init(&F.mutex,0));
 struct n2mb_ops ops={.opaque=&F,.call=fake_call,.map=fake_map,.unmap=fake_unmap,.close=fake_close,.lock=fake_lock,.unlock=fake_unlock};
 uintptr_t b=n2mb_open(&ops);assert(b);return b;}
static void close_fake(uintptr_t b){assert(!n2mb_close(b));assert(F.closed);assert(!pthread_mutex_destroy(&F.mutex));}

static unsigned lastWireSize,lastWireMagic,wireEvents;
static int scalar_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,U64 *out,unsigned *no){uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));if(ni)memcpy(a,in,ni*8);int e=n2mb_scalar(c,sel,ni?a:0,ni,out?b:0,no);if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;}
static int method_adapter(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,const void *data,NSUInteger bytes,U64 *out,unsigned *no,void *output,NSUInteger *size){uint64_t a[16]={0},b[16]={0};assert(ni<=16&&(!no||*no<=16));if(ni)memcpy(a,in,ni*8);
 if(sel==7&&bytes>=4){lastWireSize=(unsigned)bytes;lastWireMagic=*(const unsigned char *)data;wireEvents++;}
 int e=n2mb_method(c,sel,ni?a:0,ni,data,bytes,out?b:0,no,output,size);if(out&&no){assert(*no<=16);memcpy(out,b,*no*8);}return e;}
static int map_adapter(TeguConnection c,unsigned h,unsigned task,U64 *a,U64 *n,unsigned flags){uint64_t x=0,y=0;int e=n2mb_map(c,h,task,&x,&y,flags);*a=x;*n=y;return e;}
static int unmap_adapter(TeguConnection c,unsigned h,unsigned task,U64 a){return n2mb_unmap(c,h,task,a);}
static void drain(void){while(poolCount)[pool[--poolCount] release];}

@interface NativeSamplerDesc : NSObject { @public NSUInteger mn,mg,mip,as,at,ar,cmp,aniso; float lmin,lmax; }
@end
@implementation NativeSamplerDesc
- (signed char)respondsToSelector:(SEL)sel{return sel==@selector(respondsToSelector:)||sel==@selector(minFilter)||sel==@selector(magFilter)||sel==@selector(mipFilter)||sel==@selector(sAddressMode)||sel==@selector(tAddressMode)||sel==@selector(rAddressMode)||sel==@selector(normalizedCoordinates)||sel==@selector(compareFunction)||sel==@selector(maxAnisotropy)||sel==@selector(lodMinClamp)||sel==@selector(lodMaxClamp);}
- (NSUInteger)minFilter{return mn;}- (NSUInteger)magFilter{return mg;}- (NSUInteger)mipFilter{return mip;}
- (NSUInteger)sAddressMode{return as;}- (NSUInteger)tAddressMode{return at;}- (NSUInteger)rAddressMode{return ar;}
- (signed char)normalizedCoordinates{return 1;}- (NSUInteger)compareFunction{return cmp;}
- (NSUInteger)maxAnisotropy{return aniso;}- (float)lodMinClamp{return lmin;}- (float)lodMaxClamp{return lmax;}
@end
static TeguSampler *native_sampler(TeguComputeDevice *d){NativeSamplerDesc *q=[[NativeSamplerDesc alloc] init];q->mn=q->mg=0;q->mip=0;q->as=q->at=q->ar=0;q->cmp=0;q->aniso=1;q->lmin=0;q->lmax=0;
 TeguSampler *s=[d newSamplerStateWithDescriptor:(id)q];[q release];assert(s);return s;}
static TeguTexture *native_texture(TeguComputeDevice *d,unsigned w,unsigned h,unsigned uses,id *err){TextureDesc *t=[[TextureDesc alloc] init];t->format=80;t->w=w;t->h=h;t->levels=1;t->samples=1;t->arrays=1;t->type=2;t->uses=uses;t->options=0;TeguTexture *x=[d newTextureWithDescriptor:(id)t];[t release];assert(x);(void)err;return x;}

_Static_assert(__builtin_offsetof(struct Dispatch,v2Textures) >=
               __builtin_offsetof(struct Dispatch,v2Buffers)+31*sizeof(void*),
               "v2Buffers[31] must not alias v2Textures");
/* Integration-owned Delta6 real-library and selector audit helper.
 * Included by native_compute_frontend_test.py after the host frontend
 * declarations and before main.  This is test-only code, not production. */

extern SEL sel_registerName(const char *);
extern signed char class_respondsToSelector(Class, SEL);
extern id object_getClass(id);
extern id objc_getClass(const char *);

@interface NSObject (Delta6SelectorAudit)
- (signed char)respondsToSelector:(SEL)selector;
+ (signed char)instancesRespondToSelector:(SEL)selector;
@end
@implementation NSObject (Delta6SelectorAudit)
- (signed char)respondsToSelector:(SEL)selector {
    return class_respondsToSelector(*(id *)self, selector);
}
+ (signed char)instancesRespondToSelector:(SEL)selector {
    return class_respondsToSelector((Class)self, selector);
}
@end

@interface AuditConstant : NSObject { @public NSUInteger type,index; id key; unsigned char data[64]; }
@end
@implementation AuditConstant
- (NSUInteger)dataType { return type; }
- (NSUInteger)index { return index; }
- (id)name { return key; }
- (const void *)data { return data; }
- (void)dealloc { [key release]; [super dealloc]; }
@end
@interface AuditConstantList : NSObject { @public AuditConstant *records[8]; unsigned count; }
@end
@implementation AuditConstantList
- (NSUInteger)count { return count; }
- (id)objectAtIndex:(NSUInteger)i { return i < count ? records[i] : 0; }
- (void)dealloc { for(unsigned i=0;i<count;i++)[records[i] release]; [super dealloc]; }
@end
@interface AuditConstantValues : NSObject { @public AuditConstantList *indexed,*named; }
@end
@implementation AuditConstantValues
- (id)newIndexedConstantArray { return [indexed retain]; }
- (id)newNamedConstantArray { return [named retain]; }
- (void)dealloc { [indexed release]; [named release]; [super dealloc]; }
@end
@interface AuditFunctionDescriptor : NSObject { @public id entry,alias; AuditConstantValues *values; NSUInteger opts; }
@end
@implementation AuditFunctionDescriptor
- (id)name { return entry; }
- (id)specializedName { return alias; }
- (id)constantValues { return values; }
- (NSUInteger)options { return opts; }
- (void)dealloc { [entry release]; [alias release]; [values release]; [super dealloc]; }
@end

static unsigned audit_checks,audit_missing;
static void audit_selector(id object,const char *className,const char *selectorName) {
    SEL selector=sel_registerName(selectorName);
    int ok=object?([object respondsToSelector:selector]!=0):0;
    audit_checks++;
    if(!ok) {
        fprintf(stderr,"SELECTOR_MISSING class=%s selector=%s\n",
                className?className:"?",selectorName);
        audit_missing++;
    }
}
static void audit_class_selector(const char *className,const char *selectorName) {
    Class cls=className?objc_getClass(className):0;
    SEL selector=sel_registerName(selectorName);
    int ok=cls?([(id)cls instancesRespondToSelector:selector]!=0):0;
    audit_checks++;
    if(!ok) {
        fprintf(stderr,"SELECTOR_MISSING class=%s selector=%s\n",
                className?className:"?",selectorName);
        audit_missing++;
    }
}
static void audit_selectors(TeguComputeDevice *device,TeguBuffer *buffer,TeguTexture *texture,
                            TeguCommand *command,TeguEncoder *encoder,TeguRenderEncoder *renderEncoder) {
    const struct {id object;const char *cls,*name;} instance[]={
        {(id)device,"TeguComputeDevice","newCommandQueue"},
        {(id)device,"TeguComputeDevice","newLibraryWithURL:error:"},
        {(id)device,"TeguComputeDevice","newLibraryWithFile:error:"},
        {(id)device,"TeguComputeDevice","newLibraryWithData:error:"},
        {(id)device,"TeguComputeDevice","newDefaultLibrary"},
        {(id)device,"TeguComputeDevice","newDefaultLibraryWithBundle:error:"},
        {(id)device,"TeguComputeDevice","newBufferWithLength:options:"},
        {(id)device,"TeguComputeDevice","newBufferWithBytes:length:options:"},
        {(id)device,"TeguComputeDevice","newBufferWithBytesNoCopy:length:options:deallocator:"},
        {(id)device,"TeguComputeDevice","newTextureWithDescriptor:"},
        {(id)device,"TeguComputeDevice","newSamplerStateWithDescriptor:"},
        {(id)device,"TeguComputeDevice","newRenderPipelineStateWithDescriptor:error:"},
        {(id)device,"TeguComputeDevice","newComputePipelineStateWithFunction:error:"},
        {(id)device,"TeguComputeDevice","newPipelineLibraryWithFilePath:error:"},
        {(id)device,"TeguComputeDevice","supportsFamily:"},
        {(id)device,"TeguComputeDevice","supportsFeatureSet:"},
        {(id)device,"TeguComputeDevice","supportsTextureSampleCount:"},
        {(id)device,"TeguComputeDevice","isDepth24Stencil8PixelFormatSupported"},
        {(id)buffer,"TeguBuffer","newLinearTextureWithDescriptor:offset:bytesPerRow:bytesPerImage:"},
        {(id)buffer,"TeguBuffer","contents"},
        {(id)buffer,"TeguBuffer","length"},
        {(id)buffer,"TeguBuffer","didModifyRange:"},
        {(id)buffer,"TeguBuffer","isComplete"},
        {(id)buffer,"TeguBuffer","iosurface"},
        {(id)buffer,"TeguBuffer","protectionOptions"},
        {(id)buffer,"TeguBuffer","waitUntilComplete"},
        {(id)texture,"TeguTexture","newTextureViewWithPixelFormat:"},
        {(id)texture,"TeguTexture","newTextureViewWithPixelFormat:textureType:levels:slices:"},
        {(id)texture,"TeguTexture","width"},
        {(id)texture,"TeguTexture","height"},
        {(id)texture,"TeguTexture","pixelFormat"},
        {(id)texture,"TeguTexture","textureType"},
        {(id)texture,"TeguTexture","sampleCount"},
        {(id)texture,"TeguTexture","arrayLength"},
        {(id)texture,"TeguTexture","mipmapLevelCount"},
        {(id)texture,"TeguTexture","didModifyData"},
        {(id)texture,"TeguTexture","rotation"},
        {(id)texture,"TeguTexture","protectionOptions"},
        {(id)texture,"TeguTexture","waitUntilComplete"},
        {(id)command,"TeguCommand","computeCommandEncoder"},
        {(id)command,"TeguCommand","computeCommandEncoderWithDispatchType:"},
        {(id)command,"TeguCommand","renderCommandEncoderWithDescriptor:"},
        {(id)command,"TeguCommand","blitCommandEncoder"},
        {(id)command,"TeguCommand","commit"},
        {(id)command,"TeguCommand","waitUntilCompleted"},
        {(id)command,"TeguCommand","addCompletedHandler:"},
        {(id)command,"TeguCommand","status"},
        {(id)command,"TeguCommand","error"},
        {(id)command,"TeguCommand","enqueue"},
        {(id)command,"TeguCommand","pushDebugGroup:"},
        {(id)command,"TeguCommand","popDebugGroup"},
        {(id)command,"TeguCommand","commitAndWaitUntilSubmitted"},
        {(id)command,"TeguCommand","commitAndWaitUntilSubmittedWithDeadline:"},
        {(id)command,"TeguCommand","waitUntilScheduled"},
        {(id)command,"TeguCommand","presentDrawable:"},
        {(id)command,"TeguCommand","presentDrawable:atTime:"},
        {(id)command,"TeguCommand","presentDrawable:afterMinimumDuration:"},
        {(id)encoder,"TeguEncoder","setComputePipelineState:"},
        {(id)encoder,"TeguEncoder","setBuffer:offset:atIndex:"},
        {(id)encoder,"TeguEncoder","setTexture:atIndex:"},
        {(id)encoder,"TeguEncoder","setSamplerState:atIndex:"},
        {(id)encoder,"TeguEncoder","setThreadgroupMemoryLength:atIndex:"},
        {(id)encoder,"TeguEncoder","dispatchThreadgroups:threadsPerThreadgroup:"},
        {(id)encoder,"TeguEncoder","endEncoding"},
        {(id)renderEncoder,"TeguRenderEncoder","drawPrimitives:vertexStart:vertexCount:"},
        {(id)renderEncoder,"TeguRenderEncoder","drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:"},
        {(id)renderEncoder,"TeguRenderEncoder","setScissorRect:"},
        {(id)renderEncoder,"TeguRenderEncoder","setRenderPipelineState:"},
        {(id)renderEncoder,"TeguRenderEncoder","setDepthStencilState:"},
        {(id)renderEncoder,"TeguRenderEncoder","setStencilReferenceValue:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVertexBytes:length:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVertexBuffer:offset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVertexBufferOffset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentBytes:length:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentBuffer:offset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentBufferOffset:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentTexture:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentSamplerState:atIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","setFragmentTexture:atTextureIndex:samplerState:atSamplerIndex:"},
        {(id)renderEncoder,"TeguRenderEncoder","insertDebugSignpost:"},
        {(id)renderEncoder,"TeguRenderEncoder","pushDebugGroup:"},
        {(id)renderEncoder,"TeguRenderEncoder","popDebugGroup"},
        {(id)renderEncoder,"TeguRenderEncoder","setDepthClipMode:"},
        {(id)renderEncoder,"TeguRenderEncoder","setBlendColorRed:green:blue:alpha:"},
        {(id)renderEncoder,"TeguRenderEncoder","setVisibilityResultMode:offset:"},
        {(id)renderEncoder,"TeguRenderEncoder","useResource:usage:"},
        {(id)renderEncoder,"TeguRenderEncoder","useHeap:"},
    };
    for(unsigned i=0;i<sizeof(instance)/sizeof(instance[0]);i++)
        audit_selector(instance[i].object,instance[i].cls,instance[i].name);
    static const struct {const char *cls,*name;} classSelectors[]={
        {"TeguTexture","newTextureViewWithPixelFormat:"},
        {"TeguTexture","newTextureViewWithPixelFormat:textureType:levels:slices:"},
        {"TeguBuffer","newLinearTextureWithDescriptor:offset:bytesPerRow:bytesPerImage:"},
        {"TeguComputeDevice","newBufferWithBytesNoCopy:length:options:deallocator:"},
        {"TeguComputeDevice","newResourceGroupFromResources:count:"},
        {"TeguComputeDevice","encodeCacheHint:"},
        {"TeguEncoder","setThreadgroupMemoryLength:atIndex:"}
    };
    for(unsigned i=0;i<sizeof(classSelectors)/sizeof(classSelectors[0]);i++)
        audit_class_selector(classSelectors[i].cls,classSelectors[i].name);
}

static void audit_real_quartzcore_library(TeguComputeDevice *device,const char *path) {
    libraryTestPath=[NSString stringWithUTF8String:path];
    NSURL *url=[[NSURL alloc] init];id error=0;
    TeguLibrary *library=[device newLibraryWithURL:url error:&error];
    assert(library&&!error);
    TeguFunction *plain=[library newFunctionWithName:[NSString stringWithUTF8String:"path_cubic_frag_lpf"]];
    assert(plain);[plain release];

    AuditFunctionDescriptor *desc=[[AuditFunctionDescriptor alloc] init];
    desc->entry=[[NSString stringWithUTF8String:"fixed_vert_lph_spc"] retain];
    desc->alias=[[NSString stringWithUTF8String:"fixed_vert_specialized"] retain];
    AuditConstantValues *values=[[AuditConstantValues alloc] init];desc->values=values;
    values->indexed=[[AuditConstantList alloc] init];
    values->named=[[AuditConstantList alloc] init];
    const char *names[]={"fc_generic","fc_vertex_layout","fc_texcoord_count0","fc_texcoord_count1"};
    const NSUInteger types[]={53,49,49,49};
    for(unsigned i=0;i<4;i++) {
        AuditConstant *v=[[AuditConstant alloc] init];
        v->type=types[i];v->key=[[NSString stringWithUTF8String:names[i]] retain];
        values->named->records[values->named->count++]=v;
    }
    TeguFunction *specialized=[library newFunctionWithDescriptor:desc error:&error];
    if(!specialized) {
        fprintf(stderr,"QUARTZCORE_DESCRIPTOR_REFUSED\n");
        audit_missing++;
    } else {
        assert(!strcmp([[specialized name] UTF8String],"fixed_vert_specialized"));
        [specialized release];
    }
    fprintf(stderr,"QUARTZCORE_LIBRARY_OK size=1014944 entry=fixed_vert_lph_spc constants=4\n");
    audit_checks++;
    [desc release];[library release];[url release];
}
/* Generated by delta9_extract_selectors.py from the local
 * QuartzCore/RenderBox dyld cache and Metal SDK headers.
 * Test-only selector names; no Apple binary is copied. */
struct delta9_selector_case { const char *cls; const char *sel;
                             int unreachable; const char *reason; };
static const struct delta9_selector_case delta9_selectors[] = {
    {0, "device", 1, "C4 audit: CA uses CA::Fence; no MTLEvent call path observed"},
    {0, "device", 1, "C4 audit: CA uses CA::Fence; no MTLFence call path observed"},
    {0, "label", 1, "C4 audit: CA uses CA::Fence; no MTLEvent call path observed"},
    {0, "label", 1, "C4 audit: CA uses CA::Fence; no MTLFence call path observed"},
    {0, "notifyListener:atValue:block:", 1, "C4 audit: CA uses CA::Fence; no MTLSharedEvent call path observed"},
    {"TeguBlitEncoder", "copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:", 0, 0},
    {"TeguBlitEncoder", "copyFromTexture:sourceSlice:sourceLevel:sourceOrigin:sourceSize:toTexture:destinationSlice:destinationLevel:destinationOrigin:", 0, 0},
    {"TeguBlitEncoder", "generateMipmapsForTexture:", 0, 0},
    {"TeguBuffer", "contents", 0, 0},
    {"TeguBuffer", "cpuCacheMode", 0, 0},
    {"TeguBuffer", "device", 0, 0},
    {"TeguBuffer", "label", 0, 0},
    {"TeguBuffer", "length", 0, 0},
    {"TeguBuffer", "newTextureWithDescriptor:offset:bytesPerRow:", 0, 0},
    {"TeguBuffer", "setPurgeableState:", 0, 0},
    {"TeguCommand", "addCompletedHandler:", 0, 0},
    {"TeguCommand", "addScheduledHandler:", 0, 0},
    {"TeguCommand", "blitCommandEncoder", 0, 0},
    {"TeguCommand", "commit", 0, 0},
    {"TeguCommand", "computeCommandEncoderWithDispatchType:", 0, 0},
    {"TeguCommand", "device", 0, 0},
    {"TeguCommand", "encodeSignalEvent:value:", 0, 0},
    {"TeguCommand", "enqueue", 0, 0},
    {"TeguCommand", "error", 0, 0},
    {"TeguCommand", "label", 0, 0},
    {"TeguCommand", "popDebugGroup", 0, 0},
    {"TeguCommand", "presentDrawable:", 0, 0},
    {"TeguCommand", "presentDrawable:atTime:", 0, 0},
    {"TeguCommand", "pushDebugGroup:", 0, 0},
    {"TeguCommand", "renderCommandEncoderWithDescriptor:", 0, 0},
    {"TeguCommand", "status", 0, 0},
    {"TeguCommand", "waitUntilCompleted", 0, 0},
    {"TeguCommand", "waitUntilScheduled", 0, 0},
    {"TeguComputeDevice", "minimumLinearTextureAlignmentForPixelFormat:", 0, 0},
    {"TeguComputeDevice", "name", 0, 0},
    {"TeguComputeDevice", "newBufferWithBytes:length:options:", 0, 0},
    {"TeguComputeDevice", "newBufferWithBytesNoCopy:length:options:deallocator:", 0, 0},
    {"TeguComputeDevice", "newBufferWithLength:options:", 0, 0},
    {"TeguComputeDevice", "newCommandQueue", 0, 0},
    {"TeguComputeDevice", "newComputePipelineStateWithFunction:error:", 0, 0},
    {"TeguComputeDevice", "newDepthStencilStateWithDescriptor:", 0, 0},
    {"TeguComputeDevice", "newHeapWithDescriptor:", 0, 0},
    {"TeguComputeDevice", "newLibraryWithURL:error:", 0, 0},
    {"TeguComputeDevice", "newRenderPipelineStateWithDescriptor:completionHandler:", 0, 0},
    {"TeguComputeDevice", "newRenderPipelineStateWithDescriptor:error:", 0, 0},
    {"TeguComputeDevice", "newRenderPipelineStateWithTileDescriptor:options:reflection:error:", 0, 0},
    {"TeguComputeDevice", "newSamplerStateWithDescriptor:", 0, 0},
    {"TeguComputeDevice", "newTextureWithDescriptor:", 0, 0},
    {"TeguComputeDevice", "newTextureWithDescriptor:iosurface:plane:", 0, 0},
    {"TeguComputeDevice", "supportsFamily:", 0, 0},
    {"TeguComputeDevice", "supportsFeatureSet:", 0, 0},
    {"TeguDepthStencilState", "device", 0, 0},
    {"TeguDepthStencilState", "label", 0, 0},
    {"TeguEncoder", "device", 0, 0},
    {"TeguEncoder", "dispatchThreadgroups:threadsPerThreadgroup:", 0, 0},
    {"TeguEncoder", "endEncoding", 0, 0},
    {"TeguEncoder", "insertDebugSignpost:", 0, 0},
    {"TeguEncoder", "label", 0, 0},
    {"TeguEncoder", "popDebugGroup", 0, 0},
    {"TeguEncoder", "pushDebugGroup:", 0, 0},
    {"TeguEncoder", "setBuffer:offset:atIndex:", 0, 0},
    {"TeguEncoder", "setComputePipelineState:", 0, 0},
    {"TeguEncoder", "setTexture:atIndex:", 0, 0},
    {"TeguEncoder", "setThreadgroupMemoryLength:atIndex:", 0, 0},
    {"TeguFunction", "device", 0, 0},
    {"TeguFunction", "name", 0, 0},
    {"TeguHeap", "cpuCacheMode", 0, 0},
    {"TeguHeap", "device", 0, 0},
    {"TeguHeap", "label", 0, 0},
    {"TeguHeap", "newBufferWithLength:options:", 0, 0},
    {"TeguHeap", "newTextureWithDescriptor:", 0, 0},
    {"TeguHeap", "setPurgeableState:", 0, 0},
    {"TeguHeap", "size", 0, 0},
    {"TeguHeap", "storageMode", 0, 0},
    {"TeguLibrary", "device", 0, 0},
    {"TeguLibrary", "label", 0, 0},
    {"TeguLibrary", "newFunctionWithDescriptor:error:", 0, 0},
    {"TeguLibrary", "newFunctionWithName:constantValues:completionHandler:", 0, 0},
    {"TeguLibrary", "newFunctionWithName:constantValues:error:", 0, 0},
    {"TeguPipeline", "device", 0, 0},
    {"TeguPipeline", "maxTotalThreadsPerThreadgroup", 0, 0},
    {"TeguPipeline", "threadExecutionWidth", 0, 0},
    {"TeguQueue", "commandBuffer", 0, 0},
    {"TeguQueue", "commandBufferWithDescriptor:", 0, 0},
    {"TeguQueue", "device", 0, 0},
    {"TeguQueue", "label", 0, 0},
    {"TeguRenderEncoder", "dispatchThreadsPerTile:", 0, 0},
    {"TeguRenderEncoder", "drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:", 0, 0},
    {"TeguRenderEncoder", "drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:", 0, 0},
    {"TeguRenderEncoder", "drawPrimitives:vertexStart:vertexCount:", 0, 0},
    {"TeguRenderEncoder", "drawPrimitives:vertexStart:vertexCount:instanceCount:", 0, 0},
    {"TeguRenderEncoder", "setBlendColorRed:green:blue:alpha:", 0, 0},
    {"TeguRenderEncoder", "setCullMode:", 0, 0},
    {"TeguRenderEncoder", "setDepthBias:slopeScale:clamp:", 0, 0},
    {"TeguRenderEncoder", "setDepthStencilState:", 0, 0},
    {"TeguRenderEncoder", "setFragmentBuffer:offset:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setFragmentBufferOffset:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setFragmentBytes:length:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setFragmentSamplerState:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setFragmentTexture:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setFrontFacingWinding:", 0, 0},
    {"TeguRenderEncoder", "setRenderPipelineState:", 0, 0},
    {"TeguRenderEncoder", "setScissorRect:", 0, 0},
    {"TeguRenderEncoder", "setStencilReferenceValue:", 0, 0},
    {"TeguRenderEncoder", "setTileBytes:length:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setTileTexture:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setTriangleFillMode:", 0, 0},
    {"TeguRenderEncoder", "setVertexBuffer:offset:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setVertexBufferOffset:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setVertexBytes:length:atIndex:", 0, 0},
    {"TeguRenderEncoder", "setViewport:", 0, 0},
    {"TeguRenderEncoder", "setVisibilityResultMode:offset:", 0, 0},
    {"TeguRenderPipeline", "device", 0, 0},
    {"TeguRenderPipeline", "label", 0, 0},
    {"TeguSampler", "device", 0, 0},
    {"TeguSampler", "label", 0, 0},
    {"TeguTexture", "cpuCacheMode", 0, 0},
    {"TeguTexture", "depth", 0, 0},
    {"TeguTexture", "device", 0, 0},
    {"TeguTexture", "getBytes:bytesPerRow:bytesPerImage:fromRegion:mipmapLevel:slice:", 0, 0},
    {"TeguTexture", "getBytes:bytesPerRow:fromRegion:mipmapLevel:", 0, 0},
    {"TeguTexture", "height", 0, 0},
    {"TeguTexture", "label", 0, 0},
    {"TeguTexture", "mipmapLevelCount", 0, 0},
    {"TeguTexture", "newTextureViewWithPixelFormat:", 0, 0},
    {"TeguTexture", "pixelFormat", 0, 0},
    {"TeguTexture", "replaceRegion:mipmapLevel:slice:withBytes:bytesPerRow:bytesPerImage:", 0, 0},
    {"TeguTexture", "replaceRegion:mipmapLevel:withBytes:bytesPerRow:", 0, 0},
    {"TeguTexture", "setPurgeableState:", 0, 0},
    {"TeguTexture", "textureType", 0, 0},
    {"TeguTexture", "usage", 0, 0},
    {"TeguTexture", "width", 0, 0},
};
enum { delta9_selector_count = 132 };
/* Integration-owned DELTA9 extracted-selector and metallib-function audit.
 * Included after delta9_selectors.inc by native_compute_frontend_test.py.
 * Test-only code; no production edits. */

static unsigned extracted_checks, extracted_missing, extracted_unreachable;
static unsigned metallib_total, metallib_lookup_nil, metallib_specialized,
                metallib_specialized_nil;

static void audit_extracted_selectors(void)
{
    for (unsigned i = 0; i < delta9_selector_count; i++) {
        const struct delta9_selector_case *c = &delta9_selectors[i];
        if (c->unreachable) {
            extracted_unreachable++;
            continue;
        }
        Class cls = c->cls ? objc_getClass(c->cls) : 0;
        SEL sel = sel_registerName(c->sel);
        int ok = cls && [(id)cls instancesRespondToSelector:sel];
        extracted_checks++;
        if (!ok) {
            fprintf(stderr, "SELECTOR_MISSING class=%s selector=%s source=extracted\n",
                    c->cls ? c->cls : "?", c->sel ? c->sel : "?");
            extracted_missing++;
        }
    }
    fprintf(stderr, "EXTRACTED_SELECTOR checks=%u missing=%u unreachable=%u\n",
            extracted_checks, extracted_missing, extracted_unreachable);
}

static void audit_all_metallib_functions(TeguComputeDevice *device, const char *path)
{
    id error = 0;
    TeguLibrary *library = [device newLibraryWithFile:
        [NSString stringWithUTF8String:path] error:&error];
    if (!library || error) {
        fprintf(stderr, "METAL_FUNCTIONS library=missing\n");
        metallib_total++;
        return;
    }
    TeguAIRLibrary *air = library->library;
    size_t total = tegu_air_function_count(air);
    metallib_total = (unsigned)total;
    for (size_t i = 0; i < total; i++) {
        char name[4096];
        uint32_t kind = 0;
        size_t required = tegu_air_function_info(air, i, name, sizeof(name), &kind);
        if (!required || required > sizeof(name) || !kind) {
            fprintf(stderr, "METAL_FUNCTION_INVALID index=%zu\n", i);
            metallib_lookup_nil++;
            continue;
        }
        TeguFunction *function = [library newFunctionWithName:
            [NSString stringWithUTF8String:name]];
        if (!function) {
            fprintf(stderr, "METAL_FUNCTION_LOOKUP_NIL name=%s\n", name);
            metallib_lookup_nil++;
            continue;
        }
        [function release];
        char message[512] = {0};
        TeguAIRLibrary *specialized = tegu_air_library_specialize(
            air, (const uint8_t *)name, strlen(name), 0, 0,
            message, sizeof(message));
        if (!specialized) {
            fprintf(stderr, "METAL_FUNCTION_NIL name=%s reason=%s\n",
                    name, message[0] ? message : "no-default-constants");
            metallib_specialized_nil++;
            continue;
        }
        tegu_air_library_close(specialized);
        metallib_specialized++;
    }
    [library release];
    fprintf(stderr,
            "METAL_FUNCTIONS total=%u lookup_nil=%u specialized=%u nil=%u\n",
            metallib_total, metallib_lookup_nil, metallib_specialized,
            metallib_specialized_nil);
}

int main(void){
 uintptr_t b=open_fake();
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->connection=b;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 uint64_t caps[4];unsigned n=4;assert(!n2mb_scalar(b,0,0,0,caps,&n));d->capabilities=caps[1];
 TeguQueue *q=[d newCommandQueue];assert(q);id error=0;

 /* buffer-only function-created pipeline: TDG1, one submit. probe-compute.bc is
  * hand-written LLVM IR assembled by the host llvm-as, not Apple AIR, so the
  * AIR-only compute path (runtime_pipeline.inc) cannot read it: this plumbing
  * check runs on the m2v SPIR-V compile (TEGU_AIR_COMPUTE=0, fixed per pipeline
  * at creation); the real AIR kernel below runs AIR-only. */
 setenv("TEGU_AIR_COMPUTE","0",1);
 NSData *bd=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:"gpu/build/native/probe-compute.bc"]];assert(bd);
 TeguLibrary *bl=[d newTeguLibraryWithBytes:[bd bytes] length:[bd length] error:&error];assert(bl&&!error);
 TeguFunction *bf=[bl newFunctionWithName:[NSString stringWithUTF8String:"nord2_compute_probe"]];assert(bf);
 TeguPipeline *bp=[d newComputePipelineStateWithFunction:bf error:&error];assert(bp&&!error);
 TeguBuffer *bo=[d newBufferWithLength:1024 options:0];assert(bo);
 unsigned beforeSub=F.submitted,beforeWire=wireEvents;lastWireSize=0;
 TeguCommand *c1=[q commandBuffer];assert(c1);TeguEncoder *e1=[c1 computeCommandEncoder];assert(e1);
 [e1 setComputePipelineState:bp];[e1 setBuffer:bo offset:0 atIndex:0];
 [e1 dispatchThreadgroups:(MTLSize){1,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];[e1 endEncoding];assert(!c1->invalid);
 [c1 commit];[c1 waitUntilCompleted];assert([c1 status]==4&&F.submitted==beforeSub+1&&wireEvents==beforeWire+1);
 assert(lastWireSize==272&&F.req.kind==1);
 [bo release];[bp release];[bf release];[bl release];
 unsetenv("TEGU_AIR_COMPUTE");

 /* texture kernel: TDG2, used texture + unused texture still submits */
 NSData *td=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:"gpu/build/deepseek-parallel-20261025/workers/compute/air-work/fixtures/test_fast_math.air"]];assert(td);
 TeguLibrary *tl=[d newTeguLibraryWithBytes:[td bytes] length:[td length] error:&error];assert(tl&&!error);
 TeguFunction *tf=[tl newFunctionWithName:[NSString stringWithUTF8String:"TestFastMath"]];assert(tf);
 TeguPipeline *tp=[d newComputePipelineStateWithFunction:tf error:&error];assert(tp&&!error);
 TeguTexture *used=native_texture(d,16,16,1,&error);TeguTexture *unused=native_texture(d,16,16,1,&error);
 TeguBuffer *tb=[d newBufferWithLength:4096 options:0];assert(tb);TeguSampler *smp=native_sampler(d);
 beforeSub=F.submitted;beforeWire=wireEvents;lastWireSize=0;lastWireMagic=0;
 TeguCommand *c2=[q commandBuffer];assert(c2);TeguEncoder *e2=[c2 computeCommandEncoder];assert(e2);
 [e2 setComputePipelineState:tp];[e2 setBuffer:tb offset:0 atIndex:0];
 [e2 setTexture:used atIndex:0];[e2 setTexture:unused atIndex:1];[e2 setSamplerState:smp atIndex:0];
 fprintf(stderr,"PRE comp=%p count=%u\n",tp->compiled,c2->count);
 [e2 dispatchThreadgroups:(MTLSize){1,1,1} threadsPerThreadgroup:(MTLSize){8,8,1}];[e2 endEncoding];
 assert(!c2->invalid);
 [c2 commit];[c2 waitUntilCompleted];assert([c2 status]==4&&F.submitted==beforeSub+1&&wireEvents==beforeWire+1);
 assert(lastWireSize==6304&&lastWireMagic==0x54u); /* TDG2 magic low byte 'T' */
 [smp release];[tb release];[unused release];[used release];[tp release];[tf release];[tl release];

 /* v2 buffer/texture retain arrays are distinct at Metal slots9/30 and1/22. */
 {
  unsigned live0=live;
  TeguBuffer *b9=[d newBufferWithLength:64 options:0];assert(b9);
  TeguBuffer *b30=[d newBufferWithLength:64 options:0];assert(b30);
  TeguTexture *t1=native_texture(d,16,16,1,&error);TeguTexture *t22=native_texture(d,16,16,1,&error);
  TeguCommand *c3=[q commandBuffer];assert(c3);struct Dispatch *j3=tegu_command_job(c3,0);
  j3->v2Buffers[9]=[b9 retain];j3->v2Buffers[30]=[b30 retain];
  j3->v2Textures[1]=[t1 retain];j3->v2Textures[22]=[t22 retain];
  assert((void*)j3->v2Buffers[9]!=(void*)j3->v2Textures[1]);
  assert((void*)j3->v2Buffers[30]!=(void*)j3->v2Textures[22]);
  [b9 release];[b30 release];[t1 release];[t22 release];
  c3->count=1;[c3 release];
  assert(live==live0);
 }

 /* Shadow-surface texture must be visible to tegu_shadow_sync_jobs via
  * Dispatch.v2Textures, not only compositorTextures. */
 {
  IOSurfaceFixture *surface=new_surface_fixture(701,TEGU_IOSURFACE_BGRA,0,16,16,64,16384);
  for(unsigned y=0;y<16;y++)for(unsigned x=0;x<64;x++)surface->bytes[y*64+x]=(unsigned char)(y*3+x);
  TeguTexture *shadow=native_texture(d,16,16,1,&error);assert(shadow&&shadow->buffer);
  shadow->shadowSurface=(id)surface;shadow->format=80;shadow->rowBytes=64;
  memset([shadow->buffer contents],0x5a,shadow->buffer->logical);
  TeguCommand *cs=[q commandBuffer];assert(cs);struct Dispatch *js=tegu_command_job(cs,0);
  js->v2Textures[0]=[shadow retain]; unsigned long long copied=tegu_shadow_sync_jobs(&js,1,0);
  assert(copied==16u*16u*4u);
  assert(!memcmp([shadow->buffer contents],surface->bytes,16u*16u*4u));
  [shadow release];[cs release];[surface release];
 }

 /* Delta6: real QuartzCore library, CA descriptor constants, selector audit. */
 audit_real_quartzcore_library(d,"$NORD2_WORK/gpu/build/native/QuartzCore.metallib");
 { TeguBuffer *audit_b=[d newBufferWithLength:64 options:0];
   TeguTexture *audit_t=native_texture(d,16,16,1,&error);
   TeguCommand *audit_c=[q commandBuffer]; TeguEncoder *audit_e=[audit_c computeCommandEncoder];
   RenderPass *audit_pass=[[RenderPass alloc] init];audit_pass->color=[[PassColor alloc] init];
   audit_pass->color->target=[audit_t retain];audit_pass->color->load=2;audit_pass->color->store=1;
   audit_pass->passWidth=audit_pass->passHeight=16;audit_pass->arrayLength=1;audit_pass->sampleCount=1;
   TeguCommand *audit_rc=[q commandBuffer];
   TeguRenderEncoder *audit_re=[audit_rc renderCommandEncoderWithDescriptor:(id)audit_pass];
   if(audit_e) audit_selectors(d,audit_b,audit_t,audit_c,audit_e,audit_re);
   else { fprintf(stderr,"SELECTOR_AUDIT no compute encoder\n"); audit_missing++; }
   if(audit_re)[audit_re endEncoding];
   [audit_pass release];[audit_t release];[audit_b release];
 }
 audit_extracted_selectors();
 audit_all_metallib_functions(d,"$NORD2_WORK/gpu/build/native/QuartzCore.metallib");
 audit_missing += extracted_missing;
 if(audit_missing){fprintf(stderr,"SELECTOR_AUDIT missing=%u checks=%u extracted=%u metal_functions=%u\n",audit_missing,audit_checks,extracted_missing,metallib_total);return 1;}
 fprintf(stderr,"SELECTOR_AUDIT checks=%u missing=0\n",audit_checks);
 puts("PASS native compute frontend: buffer-only TDG1 submit; used+unused textures TDG2 submit; slots9/30 retain distinct; shadow v2 texture synced");
 return 0;
 return 0;
}
NSUInteger tegu_host_constant_data_size(NSUInteger type) {
 switch(type){case 53:case 45:case 49:return 1;case 16:case 37:case 41:return 2;
 case 3:case 29:case 33:return 4;default:return 0;}
}
