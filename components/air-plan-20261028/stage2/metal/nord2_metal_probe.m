/* Offline-built device test: no GL and no CPU shader implementation.
 * Uses the loaded bundle's Metal APIs, then reads pixels back independently.
 * Run explicitly; never installed/launched by the build script. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CoreFoundation/CoreFoundation.h>
/* iOS 16 exports IOSurface C symbols but does not ship these private headers.
 * The opaque reference is declared by Metal's own texture header. */
extern IOSurfaceRef IOSurfaceCreate(CFDictionaryRef);
extern void *IOSurfaceGetBaseAddress(IOSurfaceRef);
extern size_t IOSurfaceGetWidth(IOSurfaceRef),IOSurfaceGetHeight(IOSurfaceRef),IOSurfaceGetBytesPerRow(IOSurfaceRef),IOSurfaceGetAllocSize(IOSurfaceRef);
extern uint32_t IOSurfaceGetPixelFormat(IOSurfaceRef);
extern int IOSurfaceLock(IOSurfaceRef,uint32_t,uint32_t *);
extern int IOSurfaceUnlock(IOSurfaceRef,uint32_t,uint32_t *);
extern const CFStringRef kIOSurfaceWidth,kIOSurfaceHeight,kIOSurfaceBytesPerElement,kIOSurfaceBytesPerRow,kIOSurfaceAllocSize,kIOSurfacePixelFormat;
#define kIOSurfaceLockReadOnly 1
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mach/mach.h>
#include "../include/nord2_gpu_transport.h"
#include "nord2_probe_memory.h"
static unsigned logPort;
static int (*logCall)(unsigned,unsigned,const uint64_t *,unsigned,const void *,size_t,uint64_t *,unsigned *,void *,size_t *);
static void logInit(void){
 void *io=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",2),*sys=dlopen("/usr/lib/libSystem.B.dylib",2);
 if(!io||!sys)return;
 void *(*matching)(const char *)=dlsym(io,"IOServiceMatching");unsigned (*get)(unsigned,void *)=dlsym(io,"IOServiceGetMatchingService");
 int (*open)(unsigned,unsigned,unsigned,unsigned *)=dlsym(io,"IOServiceOpen");int (*release)(unsigned)=dlsym(io,"IOObjectRelease");
 const unsigned *task=dlsym(sys,"mach_task_self_");logCall=dlsym(io,"IOConnectCallMethod");
 if(!matching||!get||!open||!release||!task||!logCall)return;
 unsigned service=get(0,matching("Nord2GPU"));if(!service)return;
 if(open(service,*task,0,&logPort))logPort=0;release(service);
}
/* The kernel LOG selector accepts 16384 bytes per user client
 * (Nord2GPU.cpp); batch-surface-v8 logged ~16.3 KB, so its final lines were
 * refused and never reached the USB capture. Diagnostic lines (logLocal, or
 * once the budget is used) stay on stdout; the reserve keeps room for the
 * summary/final records. */
#define N2_LOG_QUOTA 16384u
#define N2_LOG_RESERVE 2048u
static unsigned logUsed,logDropped,logLocal;
static int logEssential(const char *t){
 static const char *const keep[]={"NORD2_METAL batch complete","NORD2_METAL status phase=batch-complete",
  "NORD2_METAL memory phase=batch-complete","NORD2_METAL summary","NORD2_METAL feature="};
 for(unsigned i=0;i<sizeof(keep)/sizeof(keep[0]);i++)if(!strncmp(t,keep[i],strlen(keep[i])))return 1;
 return 0;
}
static int report(FILE *stream,const char *format,...){
 char text[1024];va_list ap;va_start(ap,format);int n=vsnprintf(text,sizeof(text),format,ap);va_end(ap);
 if(n<0)return n;size_t len=(size_t)n<sizeof(text)?(size_t)n:sizeof(text)-1;
 fwrite(text,1,len,stream);fflush(stream);
 unsigned limit=logEssential(text)?N2_LOG_QUOTA:N2_LOG_QUOTA-N2_LOG_RESERVE;
 if(logPort&&logCall&&(logLocal||logUsed+len>limit))logDropped++;
 else if(logPort&&logCall){logUsed+=(unsigned)len;
  for(size_t offset=0;offset<len;){size_t size=len-offset;if(size>240)size=240;
  unsigned count=0;size_t bytes=0;logCall(logPort,11,NULL,0,text+offset,size,NULL,&count,NULL,&bytes);offset+=size;}}
 return n;
}
#define printf(...) report(stdout,__VA_ARGS__)
#define fprintf(stream,...) report(stream,__VA_ARGS__)
static int memorySetup(void) {
 n2_probe_memory_control control=(n2_probe_memory_control)dlsym(RTLD_DEFAULT,"memorystatus_control");
 struct n2_probe_limits before={0},after={0};unsigned stage=0;errno=0;
 int rc=n2_probe_memory_setup(control,getpid(),&before,&after,&stage),saved_errno=errno;
 printf("NORD2_METAL memory-policy rc=%d stage=%u errno=%d before=%d/%d requested=%d active=%d/%u inactive=%d/%u\n",
  rc,stage,saved_errno,before.active,before.inactive,N2_PROBE_MEMORY_MB,
  after.active,after.active_attr,after.inactive,after.inactive_attr);
 return rc;
}
static void memoryStatus(const char *phase) {
 task_vm_info_data_t info={0};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;
 kern_return_t rc=task_info(mach_task_self(),TASK_VM_INFO,(task_info_t)&info,&count);
 if(rc||count<TASK_VM_INFO_REV1_COUNT){printf("NORD2_METAL memory phase=%s error=%d count=%u\n",phase,rc,count);return;}
 printf("NORD2_METAL memory phase=%s footprint=%llu resident=%llu peak=%llu\n",phase,
  (unsigned long long)info.phys_footprint,(unsigned long long)info.resident_size,
  count>=TASK_VM_INFO_REV3_COUNT?(unsigned long long)info.ledger_phys_footprint_peak:0);
}
static void gpuStatus(const char *phase) {
 /* Only the phases the analyzer qualifies go to the kernel log. */
 unsigned saved=logLocal;
 if(strcmp(phase,"configured")&&strcmp(phase,"compute-complete")&&strcmp(phase,"batch-complete"))logLocal=1;
 struct nord2_gpu_status status={0};unsigned count=0;size_t size=sizeof(status);
 if(logPort&&logCall&&!logCall(logPort,NORD2_GPU_SEL_STATUS,NULL,0,NULL,0,NULL,&count,&status,&size)&&size==sizeof(status))
  printf("NORD2_METAL status phase=%s complete=%u fault=%u timeout=%u live=%u quarantine=%u\n",phase,status.jobs_completed,status.jobs_faulted,status.jobs_timed_out,status.buffers_live,status.quarantine);
 memoryStatus(phase);logLocal=saved;
}
static void pixelSample(const uint8_t *pixels,unsigned bpp,unsigned x,unsigned y) {
 uint64_t bits=0;memcpy(&bits,pixels+(y*64+x)*bpp,bpp);
 unsigned saved=logLocal;logLocal=1;
 printf("NORD2_METAL pixel x=%u y=%u bytes=%u raw_le=%016llx\n",x,y,bpp,(unsigned long long)bits);
 logLocal=saved;
}
/* Seed LOAD independently: a failed earlier draw must not contaminate its
 * expected background. Also exercise the public shared-texture CPU ABI. */
static int seedTexture(id<MTLTexture> target,unsigned bpp,int loadBackground) {
 uint8_t seed[64*64*8],back[64*64*8];const float sentinel[4]={.0625,.125,.875,.375},background[4]={.25,.5,.75,1};
 const float *color=loadBackground?background:sentinel;
 for(unsigned i=0;i<64*64;i++)for(unsigned k=0;k<(bpp==1?1u:4u);k++){
  unsigned channel=target.pixelFormat==MTLPixelFormatBGRA8Unorm&&k<3?2-k:k;
  if(bpp==8){_Float16 v=color[channel];memcpy(seed+i*bpp+k*2,&v,2);}
  else seed[i*bpp+k]=(uint8_t)(color[channel]*255+.5);
 }
 [target replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:seed bytesPerRow:64*bpp];
 memset(back,0x55,sizeof(back));[target getBytes:back bytesPerRow:64*bpp fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];
 int bad=memcmp(seed,back,64*64*bpp)!=0;
 printf("NORD2_METAL shared-texture format=%lu roundtrip=%s\n",(unsigned long)target.pixelFormat,bad?"FAIL":"PASS");
 if(bad){pixelSample(back,bpp,0,0);pixelSample(back,bpp,32,32);}return bad;
}
static int completed(id<MTLCommandBuffer> c) {
 [c commit];[c waitUntilCompleted];
 if(c.status!=MTLCommandBufferStatusCompleted||c.error){fprintf(stderr,"NORD2_METAL command failed: %s\n",c.error.description.UTF8String);return 1;}return 0;
}
static int compute(id<MTLDevice> d,id<MTLLibrary> lib){
 NSError *error=nil;id<MTLFunction> f=[lib newFunctionWithName:@"nord2_compute_probe"];
 id<MTLComputePipelineState> p=[d newComputePipelineStateWithFunction:f error:&error];[f release];
 if(!p){fprintf(stderr,"compute pipeline: %s\n",error.description.UTF8String);return 1;}
 id<MTLBuffer> b=[d newBufferWithLength:256 options:MTLResourceStorageModeShared];if(!b)return 1;
 memset(b.contents,0,256);id<MTLCommandQueue> q=[d newCommandQueue];id<MTLCommandBuffer> c=[q commandBuffer];
 id<MTLComputeCommandEncoder> e=[c computeCommandEncoder];[e setComputePipelineState:p];[e setBuffer:b offset:0 atIndex:0];
 [e dispatchThreadgroups:MTLSizeMake(2,1,1) threadsPerThreadgroup:MTLSizeMake(32,1,1)];[e endEncoding];
 if(completed(c))return 1;unsigned bad=0;const uint32_t *words=b.contents;
 for(unsigned i=0;i<64;i++)bad+=words[i]!=(0x12345678u^i);
 printf("NORD2_METAL compute checked=64 mismatches=%u\n",bad);fflush(stdout);
 [b release];[q release];[p release];return bad!=0;
}
/* Separate draw/tiling from the framebuffer's hardware clear. These draws
 * write a colour unlike the CPU seed; DontCare variants issue no LOAD job.
 * The quad is entirely inside the clip volume and can bypass vertex fetch. */
static int geometry(id<MTLDevice> d,id<MTLLibrary> lib,id<MTLTexture> target) {
 const char *names[]={"attribute-triangle","attribute-quad","generated-quad","generated-quad-load"};
 const float xy[6][2]={{-1,-1},{1,-1},{-1,1},{-1,1},{1,-1},{1,1}};
 id<MTLCommandQueue> q=[d newCommandQueue];unsigned failed=0;
 for(unsigned mode=0;mode<4;mode++){
  MTLRenderPipelineDescriptor *p=[MTLRenderPipelineDescriptor new];NSError *error=nil;
  p.vertexFunction=[[lib newFunctionWithName:mode>=2?@"nord2_quad_vertex":@"varying_vertex"] autorelease];
  p.fragmentFunction=[[lib newFunctionWithName:@"varying_fragment"] autorelease];
  p.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
  if(mode<2){p.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];
   for(unsigned k=0;k<2;k++){p.vertexDescriptor.attributes[7+k].format=MTLVertexFormatFloat4;p.vertexDescriptor.attributes[7+k].offset=k*16;p.vertexDescriptor.attributes[7+k].bufferIndex=1;}
   p.vertexDescriptor.layouts[1].stride=32;p.vertexDescriptor.layouts[1].stepFunction=MTLVertexStepFunctionPerVertex;}
  id<MTLRenderPipelineState> pipeline=[d newRenderPipelineStateWithDescriptor:p error:&error];
  if(!pipeline){fprintf(stderr,"NORD2_METAL geometry pipeline %s: %s\n",names[mode],error.description.UTF8String);return -1;}
  float vertices[48];for(unsigned i=0;i<6;i++){
   const float v[8]={xy[i][0],xy[i][1],.5,1,.25,.5,.75,1};memcpy(vertices+i*8,v,sizeof(v));}
  if(!mode){const float triangle[6]={-1,-1,3,-1,-1,3};for(unsigned i=0;i<3;i++){vertices[i*8]=triangle[i*2];vertices[i*8+1]=triangle[i*2+1];vertices[i*8+2]=0;}}
  id<MTLBuffer> buffer=mode<2?[d newBufferWithBytes:vertices length:sizeof(vertices) options:MTLResourceStorageModeShared]:nil;
  if((mode<2&&!buffer)||seedTexture(target,4,0))return -1;
  MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=target;
  pass.colorAttachments[0].loadAction=mode==3?MTLLoadActionLoad:MTLLoadActionDontCare;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
  id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:pass];if(!e)return -1;
  [e setRenderPipelineState:pipeline];if(buffer)[e setVertexBuffer:buffer offset:0 atIndex:1];
  [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:mode?6:3];[e endEncoding];if(completed(c))return -1;
  uint8_t pixels[64*64*4];memset(pixels,0x55,sizeof(pixels));[target getBytes:pixels bytesPerRow:256 fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];
  unsigned bad=0;
  for(unsigned i=0;i<4096;i++){
   /* Pixel centres, with the Metal viewport's downward framebuffer Y. */
   const unsigned expected[4]={191,mode>=2?(unsigned)((1.0f-((i/64)+.5f)/64)*255+.5f):128,
    mode>=2?(unsigned)((((i%64)+.5f)/64)*255+.5f):64,255};
   for(unsigned k=0;k<4;k++){int delta=(int)pixels[i*4+k]-(int)expected[k];if(delta< -1||delta>1){bad++;break;}}
  }
  printf("NORD2_METAL geometry=%s checked=4096 mismatches=%u\n",names[mode],bad);gpuStatus("geometry-complete");
  if(bad){failed++;pixelSample(pixels,4,16,16);pixelSample(pixels,4,32,32);}
  [buffer release];[pipeline release];[p release];
 }
 [q release];return failed?1:0;
}
/* Shared expected-pixel oracle: direct IOSurface checks must use the current
 * pass, never the colour left by an earlier version of the test sequence. */
static unsigned pixelMismatches(const uint8_t *pixels,size_t stride,unsigned bpp,int bgra,unsigned pass) {
 unsigned mismatches=0;
 for(unsigned y=0;y<64;y++)for(unsigned x=0;x<64;x++){
  const float expected[4][4]={{.25,.5,.75,1},{1,0,0,1},{.125,.25,.5,1},{.5625,.25,.25,1}};
  int inside=x>=16&&x<48&&y>=16&&y<48;
  if(pass==5||pass==9)inside=0;
  if(pass==6)inside=x<16&&y>=16&&y<48;
  if(pass==7)inside=x>=24&&x<40&&y>=16&&y<48;
  unsigned mode=pass==4?3:pass==3?(inside?1:2):pass==2?2:pass==1&&inside?1:0;
  if(pass>=5)mode=pass==9?0:inside?1:2;
  for(unsigned k=0;k<(bpp==1?1u:4u);k++){
   unsigned channel=bgra&&k<3?2-k:k;float got;
   if(bpp==8){_Float16 half;memcpy(&half,pixels+y*stride+x*bpp+k*2,2);got=(float)half;}
   else got=pixels[y*stride+x*bpp+k]/255.0f;
   float delta=got-expected[mode][channel],tolerance=bpp==8?.001f:.0041f;
   if(!(delta>=-tolerance&&delta<=tolerance)){mismatches++;break;}
  }
 }
 return mismatches;
}
static int surfacePixels(IOSurfaceRef surface,unsigned pass,const char *name) {
 size_t width=IOSurfaceGetWidth(surface),height=IOSurfaceGetHeight(surface);
 size_t stride=IOSurfaceGetBytesPerRow(surface),allocation=IOSurfaceGetAllocSize(surface);
 int layout=width==64&&height==64&&stride>=256&&stride<=allocation/64&&IOSurfaceGetPixelFormat(surface)==0x42475241;
 int lock=IOSurfaceLock(surface,kIOSurfaceLockReadOnly,NULL),unlock=-1;
 unsigned checked=0,bad=0;uint32_t center=0;
 if(!lock){
  const uint8_t *base=IOSurfaceGetBaseAddress(surface);
  if(layout&&base){checked=4096;bad=pixelMismatches(base,stride,4,1,pass);memcpy(&center,base+32*stride+32*4,4);}
  else layout=0;
  unlock=IOSurfaceUnlock(surface,kIOSurfaceLockReadOnly,NULL);
 }
 printf("NORD2_METAL IOSurface pass=%s checked=%u mismatches=%u lock=%d unlock=%d layout=%d stride=%zu center=%08x\n",name,checked,bad,lock,unlock,layout,stride,center);
 return lock||unlock||!layout||checked!=4096||bad;
}
static int render(id<MTLDevice> d,id<MTLLibrary> lib,id<MTLTexture> target,IOSurfaceRef surface,unsigned *directFailures) {
 NSError *error=nil;MTLRenderPipelineDescriptor *p=[MTLRenderPipelineDescriptor new];
 p.vertexFunction=[lib newFunctionWithName:@"varying_vertex"];p.fragmentFunction=[lib newFunctionWithName:@"varying_fragment"];
 p.colorAttachments[0].pixelFormat=target.pixelFormat;
 p.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];
 for(int i=0;i<2;i++){p.vertexDescriptor.attributes[7+i].format=MTLVertexFormatFloat4;p.vertexDescriptor.attributes[7+i].offset=i*16;p.vertexDescriptor.attributes[7+i].bufferIndex=1;}
 p.vertexDescriptor.layouts[1].stride=32;p.vertexDescriptor.layouts[1].stepFunction=MTLVertexStepFunctionPerVertex;
 id<MTLRenderPipelineState> pipeline=[d newRenderPipelineStateWithDescriptor:p error:&error];
 if(!pipeline){fprintf(stderr,"NORD2_METAL pipeline: %s\n",error.description.UTF8String);return 1;}
 id<MTLCommandQueue> queue=[d newCommandQueue];
 float vertices[24]={-1,-1,0,1,.25,.5,.75,1, 3,-1,0,1,.25,.5,.75,1, -1,3,0,1,.25,.5,.75,1};
 id<MTLBuffer> buffer=[d newBufferWithBytes:vertices length:sizeof(vertices) options:MTLResourceStorageModeShared];
 unsigned bpp=target.pixelFormat==MTLPixelFormatRGBA16Float?8:target.pixelFormat==MTLPixelFormatR8Unorm?1:4;
 uint8_t pixels[64*64*8];
 if(seedTexture(target,bpp,0))return -1;
 unsigned failedPasses=0;
 const char *passNames[]={"CLEAR+varying","LOAD+scissor","clear-only","viewport","blend+channel-mask",
  "viewport-empty-clear","viewport-negative","viewport-scissor","viewport-depth-reversed","viewport-empty-load"};
 for(unsigned pass=0;pass<10;pass++) {
  if(pass)for(unsigned i=0;i<3;i++){vertices[i*8+4]=1;vertices[i*8+5]=0;vertices[i*8+6]=0;}
  if((pass==1||pass==9)&&seedTexture(target,bpp,1))return -1;
  if(pass==4){
   for(unsigned i=0;i<3;i++)vertices[i*8+7]=.5;
   p.colorAttachments[0].blendingEnabled=YES;
   p.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
   p.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
   p.colorAttachments[0].writeMask=MTLColorWriteMaskRed|MTLColorWriteMaskBlue;
   [pipeline release];pipeline=[d newRenderPipelineStateWithDescriptor:p error:&error];if(!pipeline)return 1;
  }
  if(pass==5){
   for(unsigned i=0;i<3;i++)vertices[i*8+7]=1;
   p.colorAttachments[0].blendingEnabled=NO;p.colorAttachments[0].writeMask=MTLColorWriteMaskAll;
   [pipeline release];pipeline=[d newRenderPipelineStateWithDescriptor:p error:&error];if(!pipeline)return 1;
  }
  memcpy(buffer.contents,vertices,sizeof(vertices));
  MTLRenderPassDescriptor *r=[MTLRenderPassDescriptor renderPassDescriptor];r.colorAttachments[0].texture=target;
  r.colorAttachments[0].loadAction=(pass==1||pass==9)?MTLLoadActionLoad:MTLLoadActionClear;r.colorAttachments[0].storeAction=MTLStoreActionStore;
  r.colorAttachments[0].clearColor=pass>=2?MTLClearColorMake(.125,.25,.5,1):MTLClearColorMake(0,0,0,1);
  id<MTLCommandBuffer> c=[queue commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:r];
  if(!e)return 1;[e setRenderPipelineState:pipeline];[e setVertexBuffer:buffer offset:0 atIndex:1];
  if(pass==1||pass==2)[e setScissorRect:(MTLScissorRect){16,16,32,32}];
  if(pass==3)[e setViewport:(MTLViewport){16,16,32,32,0,1}];
  if(pass==5||pass==9)[e setViewport:(MTLViewport){80,80,16,16,0,1}];
  if(pass==6)[e setViewport:(MTLViewport){-16,16,32,32,0,1}];
  if(pass==7){[e setViewport:(MTLViewport){16,16,32,32,0,1}];[e setScissorRect:(MTLScissorRect){24,8,16,48}];}
  if(pass==8)[e setViewport:(MTLViewport){16,16,32,32,.8,.2}];
  if(pass!=2)[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];if(completed(c))return -1;
  gpuStatus("render-complete");
  memset(pixels,0x55,sizeof(pixels));[target getBytes:pixels bytesPerRow:64*bpp fromRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0];
  unsigned mismatches=pixelMismatches(pixels,64*bpp,bpp,target.pixelFormat==MTLPixelFormatBGRA8Unorm,pass);
  printf("NORD2_METAL format=%lu pass=%s checked=4096 mismatches=%u\n",(unsigned long)target.pixelFormat,passNames[pass],mismatches);fflush(stdout);
  if(mismatches){failedPasses++;pixelSample(pixels,bpp,0,0);pixelSample(pixels,bpp,16,16);pixelSample(pixels,bpp,32,32);pixelSample(pixels,bpp,63,63);}
  if(surface)*directFailures+=surfacePixels(surface,pass,passNames[pass])!=0;
 }
 [buffer release];[queue release];[pipeline release];[p release];return failedPasses?1:0;
}
/* ---- batch-features-v9: exact-pixel readback of the C6k/C6m features ----
 * Every expected image is computed on the CPU from UNORM8 bytes; colours are
 * byte/255 so every conversion is exact. Drawn pixels allow +-1 (varying
 * interpolation of equal values), preserved pixels must be bit-exact. Each
 * test prints one tagged record:
 *   NORD2_METAL feature=<tag> checked=N mismatches=M result=PASS|FAIL [..]  */
static unsigned featureCount,featureFailed;
static double featureStart;
static double nowSeconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void featureResult(const char *tag,unsigned checked,unsigned mismatches,const char *extra){
 featureCount++;featureFailed+=mismatches!=0||!checked;
 printf("NORD2_METAL feature=%s checked=%u mismatches=%u result=%s%s%s\n",tag,checked,mismatches,
  mismatches||!checked?"FAIL":"PASS",extra?" ":"",extra?extra:"");
}
static void featureSummary(const char *after){
 printf("NORD2_METAL summary after=%s features=%u failed=%u elapsed_ms=%u log_used=%u log_dropped=%u\n",after,
  featureCount,featureFailed,(unsigned)((nowSeconds()-featureStart)*1000),logUsed,logDropped);
}
static void featureSample(const char *tag,const uint8_t *px,unsigned w,unsigned x,unsigned y,const uint8_t *want){
 unsigned saved=logLocal;logLocal=1;
 printf("NORD2_METAL feature-pixel tag=%s x=%u y=%u got=%02x%02x%02x%02x want=%02x%02x%02x%02x\n",tag,x,y,
  px[(y*w+x)*4],px[(y*w+x)*4+1],px[(y*w+x)*4+2],px[(y*w+x)*4+3],want[0],want[1],want[2],want[3]);
 logLocal=saved;
}
/* BGRA8 bytes of a colour given as R,G,B (alpha 255). */
static void bgra(uint8_t *p,unsigned r,unsigned g,unsigned b){p[0]=(uint8_t)b;p[1]=(uint8_t)g;p[2]=(uint8_t)r;p[3]=255;}
static void fillRect(uint8_t *img,unsigned w,unsigned x0,unsigned y0,unsigned rw,unsigned rh,const uint8_t c[4]){
 for(unsigned y=y0;y<y0+rh;y++)for(unsigned x=x0;x<x0+rw;x++)memcpy(img+(y*w+x)*4,c,4);
}
static void invertRect(uint8_t *img,unsigned w,unsigned x0,unsigned y0,unsigned rw,unsigned rh){
 for(unsigned y=y0;y<y0+rh;y++)for(unsigned x=x0;x<x0+rw;x++)for(unsigned k=0;k<3;k++)img[(y*w+x)*4+k]=(uint8_t)(255-img[(y*w+x)*4+k]);
}
static void markRect(uint8_t *drawn,unsigned w,unsigned x0,unsigned y0,unsigned rw,unsigned rh){
 for(unsigned y=y0;y<y0+rh;y++)for(unsigned x=x0;x<x0+rw;x++)drawn[y*w+x]=1;
}
/* drawn: per-pixel mask, 1 = produced by a draw (tolerance 1), 0 = must be exact. */
static unsigned compareImage(const char *tag,const uint8_t *got,const uint8_t *want,const uint8_t *drawn,unsigned w,unsigned h){
 unsigned bad=0,shown=0;
 for(unsigned i=0;i<w*h;i++){
  int tol=drawn&&drawn[i]?1:0,diff=0;
  for(unsigned k=0;k<4;k++){int d=(int)got[i*4+k]-(int)want[i*4+k];if(d<-tol||d>tol)diff=1;}
  if(diff){bad++;if(shown++<3)featureSample(tag,got,w,i%w,i/w,want+i*4);}
 }
 return bad;
}
static id<MTLTexture> featureTarget(id<MTLDevice> d,unsigned w,unsigned h){
 MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
 td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
 return [d newTextureWithDescriptor:td];
}
static void upload(id<MTLTexture> t,const uint8_t *img,unsigned w,unsigned h){
 [t replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0 withBytes:img bytesPerRow:w*4];
}
static void readback(id<MTLTexture> t,uint8_t *img,unsigned w,unsigned h){
 memset(img,0x55,(size_t)w*h*4);[t getBytes:img bytesPerRow:w*4 fromRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0];
}
static id<MTLRenderPipelineState> featurePipeline(id<MTLDevice> d,id<MTLLibrary> lib,NSString *fragment){
 MTLRenderPipelineDescriptor *p=[[MTLRenderPipelineDescriptor new] autorelease];NSError *error=nil;
 p.vertexFunction=[[lib newFunctionWithName:@"varying_vertex"] autorelease];
 p.fragmentFunction=[[lib newFunctionWithName:fragment] autorelease];
 if(!p.vertexFunction||!p.fragmentFunction){fprintf(stderr,"NORD2_METAL feature function missing: %s\n",fragment.UTF8String);return nil;}
 p.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
 p.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];
 for(unsigned k=0;k<2;k++){p.vertexDescriptor.attributes[7+k].format=MTLVertexFormatFloat4;p.vertexDescriptor.attributes[7+k].offset=k*16;p.vertexDescriptor.attributes[7+k].bufferIndex=1;}
 p.vertexDescriptor.layouts[1].stride=32;p.vertexDescriptor.layouts[1].stepFunction=MTLVertexStepFunctionPerVertex;
 id<MTLRenderPipelineState> r=[d newRenderPipelineStateWithDescriptor:p error:&error];
 if(!r)fprintf(stderr,"NORD2_METAL feature pipeline %s: %s\n",fragment.UTF8String,error.description.UTF8String);
 return r;
}
/* Vertex record: float4 position, float4 colour (byte/255). */
static void vtx(float *v,float x,float y,unsigned r,unsigned g,unsigned b){
 const float rec[8]={x,y,0,1,r/255.0f,g/255.0f,b/255.0f,1};memcpy(v,rec,sizeof(rec));
}
static void fullscreen(float *v,unsigned r,unsigned g,unsigned b){vtx(v,-1,-1,r,g,b);vtx(v+8,3,-1,r,g,b);vtx(v+16,-1,3,r,g,b);}
static MTLRenderPassDescriptor *featurePass(id<MTLTexture> t,MTLLoadAction load){
 MTLRenderPassDescriptor *r=[MTLRenderPassDescriptor renderPassDescriptor];r.colorAttachments[0].texture=t;
 r.colorAttachments[0].loadAction=load;r.colorAttachments[0].storeAction=MTLStoreActionStore;
 r.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);return r;
}
static void fillAll(uint8_t *img,unsigned n,unsigned r,unsigned g,unsigned b){uint8_t c[4];bgra(c,r,g,b);for(unsigned i=0;i<n;i++)memcpy(img+i*4,c,4);}
static int features(id<MTLDevice> d,id<MTLLibrary> lib){
 id<MTLRenderPipelineState> pv=featurePipeline(d,lib,@"varying_fragment"),pf=featurePipeline(d,lib,@"nord2_fetch_fragment");
 if(!pv||!pf)return -1;
 id<MTLCommandQueue> q=[d newCommandQueue];
 static uint8_t img[128*128*4],want[128*128*4],drawn[128*128];
 float *verts=calloc(3*128*8,sizeof(float));if(!verts)return -1;
 id<MTLBuffer> vb=[d newBufferWithLength:3*128*8*sizeof(float) options:MTLResourceStorageModeShared];if(!vb)return -1;
 unsigned w,h,bad;

 /* 1. Tile-buffer fetch batching: ordinary draws and three framebuffer
  * fetches (invert) interleaved in ONE LOAD pass; a fetch that reads target
  * memory instead of the tile buffer misses the earlier draws. */
 {w=h=64;id<MTLTexture> t=featureTarget(d,w,h);
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)bgra(want+(y*w+x)*4,128,y*4,x*4);
  upload(t,want,w,h);memset(drawn,0,sizeof(drawn));
  fullscreen(verts,255,0,0);fullscreen(verts+24,0,0,255);memcpy(vb.contents,verts,48*sizeof(float));
  id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:featurePass(t,MTLLoadActionLoad)];
  [e setVertexBuffer:vb offset:0 atIndex:1];
  static const unsigned ops[5][5]={{0,8,8,24,24},{1,0,0,64,64},{2,24,24,24,24},{1,0,0,40,40},{1,16,16,48,48}};
  uint8_t red[4],blue[4];bgra(red,255,0,0);bgra(blue,0,0,255);
  for(unsigned i=0;i<5;i++){
   const unsigned *o=ops[i];[e setScissorRect:(MTLScissorRect){o[1],o[2],o[3],o[4]}];
   if(o[0]==1){[e setRenderPipelineState:pf];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];invertRect(want,w,o[1],o[2],o[3],o[4]);}
   else{[e setRenderPipelineState:pv];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:o[0]==0?0:3 vertexCount:3];fillRect(want,w,o[1],o[2],o[3],o[4],o[0]==0?red:blue);}
   markRect(drawn,w,o[1],o[2],o[3],o[4]);
  }
  [e endEncoding];int failed=completed(c);readback(t,img,w,h);
  bad=failed?w*h:compareImage("tilefetch-batch",img,want,drawn,w,h);
  featureResult("tilefetch-batch",w*h,bad,"draws=5 fetches=3");[t release];}

 /* 2. INTERSECT pre-frame preload: partial-coverage LOAD over a unique
  * per-pixel seed; every untouched pixel keeps the seed bit-exactly. */
 {w=h=128;id<MTLTexture> t=featureTarget(d,w,h);
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)bgra(want+(y*w+x)*4,(x^y)&255,y*2,x*2);
  upload(t,want,w,h);memset(drawn,0,sizeof(drawn));
  fullscreen(verts,10,220,30);fullscreen(verts+24,200,40,90);memcpy(vb.contents,verts,48*sizeof(float));
  id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:featurePass(t,MTLLoadActionLoad)];
  [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
  static const unsigned boxes[2][4]={{37,45,20,9},{100,4,10,30}};
  for(unsigned i=0;i<2;i++){const unsigned *o=boxes[i];uint8_t col[4];bgra(col,i?200:10,i?40:220,i?90:30);
   [e setScissorRect:(MTLScissorRect){o[0],o[1],o[2],o[3]}];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:3*i vertexCount:3];
   fillRect(want,w,o[0],o[1],o[2],o[3],col);markRect(drawn,w,o[0],o[1],o[2],o[3]);}
  [e endEncoding];int failed=completed(c);readback(t,img,w,h);
  bad=failed?w*h:compareImage("preframe-partial",img,want,drawn,w,h);
  unsigned untouched=0;for(unsigned i=0;i<w*h;i++)untouched+=!drawn[i];
  char extra[64];snprintf(extra,sizeof(extra),"preserved=%u tiles=64",untouched);
  featureResult("preframe-partial",w*h,bad,extra);[t release];}

 /* 3. Batch cap: 100 draws in one LOAD pass, each in its own 10x10 cell
  * with its own colour (one job when the cap is 128; the bridge's
  * NORD2_METAL batch trace lines report draws and tiler-heap use). */
 {w=h=128;id<MTLTexture> t=featureTarget(d,w,h);
  fillAll(want,w*h,96,64,32);upload(t,want,w,h);memset(drawn,0,sizeof(drawn));
  for(unsigned i=0;i<100;i++)fullscreen(verts+24*i,(i*37+11)&255,(i*91+50)&255,(255-i*2)&255);
  memcpy(vb.contents,verts,100*24*sizeof(float));
  id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:featurePass(t,MTLLoadActionLoad)];
  [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
  for(unsigned i=0;i<100;i++){unsigned x0=(i%10)*12+4,y0=(i/10)*12+4;uint8_t col[4];bgra(col,(i*37+11)&255,(i*91+50)&255,(255-i*2)&255);
   [e setScissorRect:(MTLScissorRect){x0,y0,10,10}];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:3*i vertexCount:3];
   fillRect(want,w,x0,y0,10,10,col);markRect(drawn,w,x0,y0,10,10);}
  [e endEncoding];int failed=completed(c);readback(t,img,w,h);
  bad=failed?w*h:compareImage("batch-cap",img,want,drawn,w,h);
  featureResult("batch-cap",w*h,bad,"draws=100");[t release];}

 /* 4. Native indexed draws with vertex attributes: a uint16 triangle list
  * quad, and two triangle-strip quads joined by the 0xFFFF restart index
  * (no restart = bridging triangles across the gap). */
 {w=h=64;id<MTLTexture> t=featureTarget(d,w,h);
  float iv[12*8];
  vtx(iv,-.5f,-.5f,40,200,120);vtx(iv+8,.5f,-.5f,40,200,120);vtx(iv+16,-.5f,.5f,40,200,120);vtx(iv+24,.5f,.5f,40,200,120);
  const float sx[4]={-1,-.25f,.25f,1};
  for(unsigned q2=0;q2<2;q2++){float l=sx[q2*2],r=sx[q2*2+1];unsigned base=4+q2*4;
   vtx(iv+base*8,l,-.5f,250,120,10);vtx(iv+(base+1)*8,r,-.5f,250,120,10);vtx(iv+(base+2)*8,l,.5f,250,120,10);vtx(iv+(base+3)*8,r,.5f,250,120,10);}
  id<MTLBuffer> ib=[d newBufferWithLength:64 options:MTLResourceStorageModeShared];if(!ib)return -1;
  const uint16_t idx[16]={0,1,2,2,1,3, 4,5,6,7,0xffff,8,9,10,11,0};memcpy(ib.contents,idx,sizeof(idx));
  memcpy(vb.contents,iv,sizeof(iv));
  for(unsigned mode=0;mode<2;mode++){
   fillAll(want,w*h,0,0,0);memset(drawn,0,sizeof(drawn));uint8_t col[4];
   if(!mode){bgra(col,40,200,120);fillRect(want,w,16,16,32,32,col);markRect(drawn,w,16,16,32,32);}
   else{bgra(col,250,120,10);fillRect(want,w,0,16,24,32,col);fillRect(want,w,40,16,24,32,col);
    markRect(drawn,w,0,16,24,32);markRect(drawn,w,40,16,24,32);}
   id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:featurePass(t,MTLLoadActionClear)];
   [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
   if(!mode)[e drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:6 indexType:MTLIndexTypeUInt16 indexBuffer:ib indexBufferOffset:0];
   else [e drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip indexCount:9 indexType:MTLIndexTypeUInt16 indexBuffer:ib indexBufferOffset:12];
   [e endEncoding];int failed=completed(c);readback(t,img,w,h);
   const char *tag=mode?"indexed-strip-restart":"indexed-attr";
   bad=failed?w*h:compareImage(tag,img,want,drawn,w,h);featureResult(tag,w*h,bad,mode?"indices=9 restart=1":"indices=6");
  }
  [ib release];[t release];}

 /* 5. Asynchronous submission: 24 command buffers committed back to back
  * (each LOADs the shared target and draws its own 2-row band), plus 8
  * independent targets cleared and drawn; only the last is waited for,
  * then every target is read back. */
 {w=h=64;id<MTLTexture> t=featureTarget(d,w,h);
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)bgra(want+(y*w+x)*4,x*3,y*3,77);
  upload(t,want,w,h);memset(drawn,0,sizeof(drawn));
  for(unsigned k=0;k<24;k++)fullscreen(verts+24*k,(k*10)&255,255-k*9,(k*53)&255);
  for(unsigned k=0;k<8;k++)fullscreen(verts+24*(24+k),(k*29+7)&255,(k*61)&255,200-k*20);
  memcpy(vb.contents,verts,32*24*sizeof(float));
  id<MTLTexture> sep[8];id<MTLCommandBuffer> last=nil;unsigned failed=0;
  for(unsigned k=0;k<24;k++){
   id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:featurePass(t,MTLLoadActionLoad)];
   [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];[e setScissorRect:(MTLScissorRect){0,k*2,64,2}];
   [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:3*k vertexCount:3];[e endEncoding];[c commit];
   uint8_t col[4];bgra(col,(k*10)&255,255-k*9,(k*53)&255);fillRect(want,w,0,k*2,64,2,col);markRect(drawn,w,0,k*2,64,2);
  }
  for(unsigned k=0;k<8;k++){
   sep[k]=featureTarget(d,32,32);if(!sep[k])return -1;
   id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:featurePass(sep[k],MTLLoadActionClear)];
   [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];[e setScissorRect:(MTLScissorRect){k,k,16,16}];
   [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:3*(24+k) vertexCount:3];[e endEncoding];[c commit];last=c;
  }
  [last waitUntilCompleted];if(last.status!=MTLCommandBufferStatusCompleted||last.error)failed=1;
  readback(t,img,w,h);bad=failed?w*h:compareImage("async-many",img,want,drawn,w,h);unsigned checked=w*h;
  for(unsigned k=0;k<8;k++){
   static uint8_t sw[32*32*4],sg[32*32*4],sd[32*32];uint8_t col[4];bgra(col,(k*29+7)&255,(k*61)&255,200-k*20);
   fillAll(sw,32*32,0,0,0);fillRect(sw,32,k,k,16,16,col);memset(sd,0,sizeof(sd));markRect(sd,32,k,k,16,16);
   readback(sep[k],sg,32,32);bad+=failed?32*32:compareImage("async-many",sg,sw,sd,32,32);checked+=32*32;[sep[k] release];
  }
  featureResult("async-many",checked,bad,"commands=32 waited=1");[t release];}

 /* 6. Command-buffer pool reuse: 40 sequential command buffers alternating
  * one- and three-draw passes; every frame read back. */
 {w=h=64;id<MTLTexture> t=featureTarget(d,w,h);unsigned checked=0;bad=0;
  for(unsigned k=0;k<40;k++){
   unsigned draws=k&1?3:1;
   for(unsigned j=0;j<draws;j++)fullscreen(verts+24*j,(k*7+j*60)&255,(k*13+40)&255,(j*90+k)&255);
   memcpy(vb.contents,verts,3*24*sizeof(float));
   fillAll(want,w*h,0,0,0);memset(drawn,0,sizeof(drawn));
   id<MTLCommandBuffer> c=[q commandBuffer];id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:featurePass(t,MTLLoadActionClear)];
   [e setRenderPipelineState:pv];[e setVertexBuffer:vb offset:0 atIndex:1];
   for(unsigned j=0;j<draws;j++){unsigned x0=(k+j*17)%40,y0=(k*3+j*11)%40;uint8_t col[4];bgra(col,(k*7+j*60)&255,(k*13+40)&255,(j*90+k)&255);
    [e setScissorRect:(MTLScissorRect){x0,y0,20,20}];[e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:3*j vertexCount:3];
    fillRect(want,w,x0,y0,20,20,col);markRect(drawn,w,x0,y0,20,20);}
   [e endEncoding];int failed=completed(c);readback(t,img,w,h);
   bad+=failed?w*h:compareImage("cmdpool-reuse",img,want,drawn,w,h);checked+=w*h;
  }
  featureResult("cmdpool-reuse",checked,bad,"commands=40");[t release];}

 /* A deliberately faulting job could quarantine this GPU context (and every
  * later test in it); attribution is covered host-side by the bridge test. */
 printf("NORD2_METAL feature=async-fault result=SKIP reason=context-unsafe\n");
 [vb release];free(verts);[q release];[pv release];[pf release];
 return featureFailed?1:0;
}
int main(int argc,char **argv) {@autoreleasepool {
 featureStart=nowSeconds();
 setenv("NORD2_METAL_TRACE","1",1);
 /* Feature toggles under test (a runner may preset them; never the UI's
  * in-boot A/B schedule: the probe must be deterministic). */
 setenv("TEGU_AB_SCHEDULE","/var/empty/nord2-probe-no-ab-schedule",1);
 setenv("NORD2_METAL_PREFRAME","1",0);setenv("TEGU_TILE_FETCH","1",0);setenv("TEGU_JM_BATCH","1",0);
 setenv("NORD2_METAL_BATCH_MAX","128",0);setenv("TEGU_NATIVE_INDEXED","1",0);setenv("NORD2_METAL_ASYNC","1",0);
 /* One-shot tiler differential inside the bundle's GPU context, before the
  * first draw; a passing descriptor-only variant is used for this batch. */
 setenv("NORD2_METAL_TILER_MATRIX","1",1);
 logInit();printf("NORD2_METAL probe start batch-features-v9 PF=%s TF=%s JB=%s BM=%s NI=%s AS=%s\n",getenv("NORD2_METAL_PREFRAME"),
  getenv("TEGU_TILE_FETCH"),getenv("TEGU_JM_BATCH"),getenv("NORD2_METAL_BATCH_MAX"),getenv("TEGU_NATIVE_INDEXED"),getenv("NORD2_METAL_ASYNC"));
 if(argc!=3){fprintf(stderr,"usage: nord2-metal-probe /path/Nord2Metal.bundle /path/varying.bc\n");return 2;}
 if(memorySetup())return 3;
 memoryStatus("configured");
 NSBundle *b=[NSBundle bundleWithPath:[NSString stringWithUTF8String:argv[1]]];NSError *error=nil;
 if(![b loadAndReturnError:&error]){fprintf(stderr,"bundle load: %s\n",error.description.UTF8String);return 1;}
 id<MTLDevice> d=nil;for(unsigned i=0;i<25&&!d;i++){d=[[b principalClass] new];if(!d)usleep(200000);}
 if(!d){fprintf(stderr,"Nord2GPU not ready\n");return 1;}
 NSData *air=[NSData dataWithContentsOfFile:[NSString stringWithUTF8String:argv[2]]];if(!air)return 2;
 dispatch_data_t data=dispatch_data_create(air.bytes,air.length,NULL,DISPATCH_DATA_DESTRUCTOR_DEFAULT);
 id<MTLLibrary> lib=[d newLibraryWithData:data error:&error];dispatch_release(data);
 if(!lib){fprintf(stderr,"AIR parse: %s\n",error.description.UTF8String);return 1;}
 if(compute(d,lib))return 1;gpuStatus("compute-complete");featureSummary("compute");
 unsigned failedTargets=0;
 MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
 id<MTLTexture> texture=[d newTextureWithDescriptor:td];if(!texture)return 1;
 int geometryResult=geometry(d,lib,texture);[texture release];if(geometryResult<0)return 1;failedTargets+=geometryResult!=0;
 featureSummary("geometry");
 const MTLPixelFormat formats[]={MTLPixelFormatBGRA8Unorm,MTLPixelFormatRGBA8Unorm,MTLPixelFormatR8Unorm,MTLPixelFormatRGBA16Float};
 for(unsigned i=0;i<4;i++){td.pixelFormat=formats[i];texture=[d newTextureWithDescriptor:td];if(!texture)return 1;
  int result=render(d,lib,texture,NULL,NULL);[texture release];if(result<0)return 1;failedTargets+=result!=0;}
 td.pixelFormat=MTLPixelFormatBGRA8Unorm;
 NSDictionary *props=@{(id)kIOSurfaceWidth:@64,(id)kIOSurfaceHeight:@64,(id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@256,(id)kIOSurfaceAllocSize:@16384,(id)kIOSurfacePixelFormat:@(0x42475241)};
 IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)props);if(!surface)return 1;
 texture=[d newTextureWithDescriptor:td iosurface:surface plane:0];if(!texture)return 1;
 unsigned directFailures=0;
 int surfaceResult=render(d,lib,texture,surface,&directFailures);if(surfaceResult<0)return 1;failedTargets+=surfaceResult!=0;
 int mismatch=directFailures!=0;featureSummary("formats");
 /* New features run after the existing passes, which consume the bridge's
  * 64 traced (synchronous) draws, so these exercise the async path. */
 int featureResultCode=features(d,lib);if(featureResultCode<0)failedTargets++;
 featureSummary("features");
 printf("NORD2_METAL IOSurface direct backing=%s\n",mismatch?"FAIL":"PASS");
 printf("NORD2_METAL batch complete failed-targets=%u iosurface-direct=%s features=%u features-failed=%u\n",failedTargets,mismatch?"FAIL":"PASS",featureCount,featureFailed);
 gpuStatus("batch-complete");[texture release];CFRelease(surface);[lib release];[d release];return mismatch||failedTargets||featureFailed;
}}
