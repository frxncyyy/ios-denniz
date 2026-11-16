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
static int report(FILE *stream,const char *format,...){
 char text[1024];va_list ap;va_start(ap,format);int n=vsnprintf(text,sizeof(text),format,ap);va_end(ap);
 if(n<0)return n;size_t len=(size_t)n<sizeof(text)?(size_t)n:sizeof(text)-1;
 fwrite(text,1,len,stream);fflush(stream);
 if(logPort&&logCall)for(size_t offset=0;offset<len;){size_t size=len-offset;if(size>240)size=240;
  unsigned count=0;size_t bytes=0;logCall(logPort,11,NULL,0,text+offset,size,NULL,&count,NULL,&bytes);offset+=size;}
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
 struct nord2_gpu_status status={0};unsigned count=0;size_t size=sizeof(status);
 if(logPort&&logCall&&!logCall(logPort,NORD2_GPU_SEL_STATUS,NULL,0,NULL,0,NULL,&count,&status,&size)&&size==sizeof(status))
  printf("NORD2_METAL status phase=%s complete=%u fault=%u timeout=%u live=%u quarantine=%u\n",phase,status.jobs_completed,status.jobs_faulted,status.jobs_timed_out,status.buffers_live,status.quarantine);
 memoryStatus(phase);
}
static void pixelSample(const uint8_t *pixels,unsigned bpp,unsigned x,unsigned y) {
 uint64_t bits=0;memcpy(&bits,pixels+(y*64+x)*bpp,bpp);
 printf("NORD2_METAL pixel x=%u y=%u bytes=%u raw_le=%016llx\n",x,y,bpp,(unsigned long long)bits);
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
int main(int argc,char **argv) {@autoreleasepool {
 setenv("NORD2_METAL_TRACE","1",1);
 /* One-shot tiler differential inside the bundle's GPU context, before the
  * first draw; a passing descriptor-only variant is used for this batch. */
 setenv("NORD2_METAL_TILER_MATRIX","1",1);
 logInit();printf("NORD2_METAL probe start batch-surface-v8\n");
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
 if(compute(d,lib))return 1;gpuStatus("compute-complete");
 unsigned failedTargets=0;
 MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:64 height:64 mipmapped:NO];td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
 id<MTLTexture> texture=[d newTextureWithDescriptor:td];if(!texture)return 1;
 int geometryResult=geometry(d,lib,texture);[texture release];if(geometryResult<0)return 1;failedTargets+=geometryResult!=0;
 const MTLPixelFormat formats[]={MTLPixelFormatBGRA8Unorm,MTLPixelFormatRGBA8Unorm,MTLPixelFormatR8Unorm,MTLPixelFormatRGBA16Float};
 for(unsigned i=0;i<4;i++){td.pixelFormat=formats[i];texture=[d newTextureWithDescriptor:td];if(!texture)return 1;
  int result=render(d,lib,texture,NULL,NULL);[texture release];if(result<0)return 1;failedTargets+=result!=0;}
 td.pixelFormat=MTLPixelFormatBGRA8Unorm;
 NSDictionary *props=@{(id)kIOSurfaceWidth:@64,(id)kIOSurfaceHeight:@64,(id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfaceBytesPerRow:@256,(id)kIOSurfaceAllocSize:@16384,(id)kIOSurfacePixelFormat:@(0x42475241)};
 IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)props);if(!surface)return 1;
 texture=[d newTextureWithDescriptor:td iosurface:surface plane:0];if(!texture)return 1;
 unsigned directFailures=0;
 int surfaceResult=render(d,lib,texture,surface,&directFailures);if(surfaceResult<0)return 1;failedTargets+=surfaceResult!=0;
 int mismatch=directFailures!=0;
 printf("NORD2_METAL IOSurface direct backing=%s\n",mismatch?"FAIL":"PASS");
 printf("NORD2_METAL batch complete failed-targets=%u iosurface-direct=%s\n",failedTargets,mismatch?"FAIL":"PASS");
 gpuStatus("batch-complete");[texture release];CFRelease(surface);[lib release];[d release];return mismatch||failedTargets;
}}
