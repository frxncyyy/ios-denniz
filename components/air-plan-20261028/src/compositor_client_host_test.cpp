#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <limits.h>
#include "TeguGPUCompositorRequest.h"
#include "TeguGPUFullscreenAllocation.h"
#include "TeguGPURenderState.inc"
#include "compositor_iosurface_target_policy.h"
#include "native_package_ui1.h"
#include "native_package_ui2.h"
#include "fullscreen_map.c"
#include "compositor_v7_scratch_layout.h"
#include "vertex_fetch.c"
using vm_offset_t=uintptr_t;
static bool PE_parse_boot_argn(const char *name,void *out,int size) {
 assert(!strcmp(name,"tegu_gpu_codecheck")&&size==sizeof(int));
 *(int *)out=0;return true;
}
extern "C" uint64_t ml_vtophys(uintptr_t address) {
 (void)address;
 /* The default-off host stub is not a physical-translation verification. */
 return 0;
}
static unsigned long gPhysBase=0x880000000UL,gPhysSize=0x40000000UL;
static const unsigned page_size=16384;
static uint64_t next_pa=0x883000000ULL;
static uint64_t buffer_live;
static int alloc_fail_after=-1,alloc_calls;
struct TeguGPUBuffer;
static TeguGPUBuffer *scratch_observed=nullptr;
static uintptr_t expected_target_cpu=0;
static uint64_t expected_target_offset=0;
static std::vector<std::pair<uintptr_t,size_t>> flush_events;
static size_t submit_flush_index=SIZE_MAX;
static size_t expected_target_pages=2576;
static uint32_t expected_target_width=1080,expected_target_height=2424,expected_target_stride=4352;
static bool expectedSourceOver=false;
static bool expectedLoad=false;
static bool testV7=false;
static bool testB3=false;
static unsigned expectedResourcePages=6;
static uint32_t expectedTextureWidth=16,expectedTextureHeight=16,expectedTextureStride=64;
static uint32_t expectedTextureFormat=70;
static unsigned surfacePageResolves=0;
void *IOMallocZero(size_t n){return calloc(1,n);} void IOFree(void *p,size_t){free(p);}
void IOLog(const char *,...){}
int64_t OSAddAtomic64(int64_t v,volatile int64_t *p){int64_t old=*p;*p+=v;return old;}
void OSMemoryBarrier(void){__sync_synchronize();}
int OSCompareAndSwapPtr(void *a,void *b,void *volatile *p){return __sync_bool_compare_and_swap(p,a,b);}
class TeguGPUBuffer: public IOBufferMemoryDescriptor {
public:
 static TeguGPUBuffer *allocate(uint64_t n){++alloc_calls;if(alloc_fail_after==alloc_calls)return nullptr;auto*b=new TeguGPUBuffer;b->logical=n;b->rounded=(n+page_size-1)&~((uint64_t)page_size-1);if(posix_memalign(&b->cpu,page_size,b->rounded)){delete b;return nullptr;}memset(b->cpu,0,b->rounded);b->pa=next_pa;next_pa+=b->rounded;++buffer_live;if(n==0x40000||n==0x70000)scratch_observed=b;return b;}
 bool gpuPage(uint64_t off,uint64_t*out){if(!out||(off&4095)||off>rounded||rounded-off<4096)return false;*out=pa+off;return true;}
 uint64_t logicalLength()const{return logical;}void setLogicalLength(uint64_t n){logical=n;}uint64_t getLength()const{return rounded;}void*getBytesNoCopy(){return cpu;}
 uint64_t paBase()const{return pa;}void setPaBase(uint64_t x){pa=x;}void retain(){++refs;}void release(){if(--refs==0){--buffer_live;free(cpu);delete this;}}
private:uint64_t logical=0,rounded=0,pa=0;unsigned refs=1;void*cpu=nullptr;
};
struct sweet_surface_plane {uint32_t width,height,bytesPerRow,offset,size;uint16_t bytesPerElement;uint8_t elementWidth,elementHeight;};
struct sweet_surface_gpu_layout {uint32_t width,height,bytesPerRow,offset,pixelFormat,allocSize,planeCount;uint16_t bytesPerElement;uint8_t elementWidth,elementHeight;sweet_surface_plane plane0;};
static unsigned surface_destroyed;
class SweetSurface:public OSObject {
public:
 uintptr_t cpuAddress=0;
 uintptr_t kernelAddress(){return cpuAddress;}
 ~SweetSurface() override {++surface_destroyed;}
};
static SweetSurface *released_test_surface;
static unsigned gpu_lease_destroyed;
class GpuImportLease:public OSObject {
 SweetSurface *surface_;
public:
 explicit GpuImportLease(SweetSurface *surface):surface_(surface){assert(surface_);surface_->retain();}
 ~GpuImportLease() override {surface_->release();++gpu_lease_destroyed;}
};
struct TeguGPUImportedTarget {uint32_t handle;SweetSurface*surface;OSObject*gpuLease;IOMemoryDescriptor*backing;sweet_surface_gpu_layout layout;uint32_t metalFormat,metadataVersion;};
// An aggregate has no IOBuffer bytes API. Exercise a distinct CPU mapping and
// physically scattered native pages through the production imported path.
class HostSurfaceBacking:public IOMemoryDescriptor {
public:
 static unsigned destroyed;
 uint64_t bytes=0,pa=0;void*data=nullptr;
 explicit HostSurfaceBacking(uint64_t n,uint64_t physical):bytes(n),pa(physical){data=calloc(1,n);assert(data);}
 ~HostSurfaceBacking() override {++destroyed;free(data);}
 uint64_t getLength() const override {return bytes;}
 void*getBytesNoCopy(){return data;}
 uint64_t getPhysicalSegment(uint64_t offset,uint64_t*size,unsigned) override {if(offset>=bytes){*size=0;return 0;}*size=4096;return pa+(offset/16384)*32768+(offset%16384);}
};
unsigned HostSurfaceBacking::destroyed;
static bool tegu_surface_gpu_page(IOMemoryDescriptor*b,uint64_t length,uint64_t off,uint64_t*pa){if(!b||off>length||length-off<4096)return false;uint64_t n=0;*pa=b->getPhysicalSegment(off,&n,0);return n>=4096&&*pa;}
static int tegu_fullscreen_surface_page_resolve(IOMemoryDescriptor*b,uint64_t off,uint64_t*pa){++surfacePageResolves;uint64_t n=0;*pa=b->getPhysicalSegment(off,&n,0);return n>=4096&&*pa?1:0;}
static volatile uint64_t queue_l2[512];
static int lock_begin(void*,uint64_t,unsigned){return 0;}
static int write_entry(void*,volatile uint64_t*p,uint64_t v){*p=v;return 0;}
static int sync_table(void*,volatile uint64_t*,size_t){return 0;}
static int flush_unlock(void*){return 0;}
static struct fs_ops submit_ops={nullptr,lock_begin,write_entry,sync_table,flush_unlock};
static uint32_t read32(const unsigned char*p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int ready=1,submit_mode=0;
static void (*drop_slot_refs)()=nullptr;
static void (*drop_imported_target)()=nullptr;
static GpuImportLease *active_test_lease;
static bool expected_imported_lease;
static unsigned surface_destroyed_at_start,surface_backing_destroyed_at_start;
extern "C" int tegu_gpu_compute_ready(){return ready;}
extern "C" uint64_t tegu_gpu_fullscreen_timeout_ticks(){return 5000000u;}
extern "C" int tegu_gpu_fullscreen_context(struct tegu_fs_queue_context*out){if(!out)return -40;*out={queue_l2,TEGU_FS_QUEUE_L2_PA,0x880100000ULL,0x880200000ULL};return 0;}
extern "C" void FlushPoC_DcacheRegion(vm_offset_t p,size_t n){flush_events.emplace_back((uintptr_t)p,n);}
extern "C" int tegu_gpu_render_fullscreen(const struct tegu_full_render_job*job,uint32_t token,uint32_t*value){
 if(!job||!job->mapping||!job->request||!job->commands||!job->command_count||!job->request->target_count||job->width!=expected_target_width||job->height!=expected_target_height||job->row_stride!=expected_target_stride||!scratch_observed)return -40;
 const unsigned expectedResources=expectedLoad?8u:expectedResourcePages;
 assert(job->request->resource_count==expectedResources);
 if(testV7||testB3){
  assert(job->request->scratch_count==112&&job->target_gpu_va==FS_APERTURE_VA+0x70000);
  const bool heap2m=job->width>16||job->height>16;
  if(heap2m){
   assert(job->request->heap_count==FS_HEAP_PAGES&&job->heap_bytes==0x200000u&&job->timeout_ticks==5000000u);
   assert(job->page_count==112u+job->request->target_count+job->request->resource_count+512u);
   assert(job->heap_gpu_va==FS_APERTURE_VA+(112u+job->request->target_count+job->request->resource_count)*4096u);
  }else{
   assert(job->request->heap_count==0&&job->heap_bytes==0&&job->heap_gpu_va==0&&job->timeout_ticks==0);
   assert(job->page_count==112u+job->request->target_count+job->request->resource_count);
  }
  if(heap2m)assert(job->request->heap&&job->request->heap[0].owner==job->owner);
  else assert(job->request->heap==nullptr);
  assert(job->request->scratch_exec_mask==(testB3?TEGU_B3_CLEAR_EXEC_MASK:
      expectedLoad?TEGU_V7_LOAD_EXEC_MASK:TEGU_V7_CLEAR_EXEC_MASK));
  const unsigned char *memory=(const unsigned char*)scratch_observed->getBytesNoCopy();
  for(unsigned i=0;i<1152;i++)assert(memory[0x2000+i]==0x31);
  const unsigned fsBytes=testB3?8704u:25600u;
  const unsigned char fsPattern=testB3?0x53u:0x72u;
  for(unsigned i=0;i<fsBytes;i++)assert(memory[0x30000+i]==fsPattern);
  for(unsigned i=fsBytes;i<(testB3?3u:7u)*4096;i++)assert(memory[0x30000+i]==0);
  for(unsigned i=0x10000;i<0x20000;i++)assert(memory[i]==0);
  for(unsigned i=0x50000;i<0x70000;i++)assert(memory[i]==0);
  if(heap2m){
   assert(read32(memory+0x1088)==job->heap_gpu_va); // appended tiler heap VA
   assert(read32(memory+0x1084)==0x200000u);
   assert(read32(memory+0x1090)==job->heap_gpu_va+64u);
   assert(read32(memory+0x1098)==job->heap_gpu_va+0x200000u);
   for(unsigned i=0;i<FS_HEAP_PAGES;i++)assert(job->request->heap[i].pa&&job->request->heap[i].owner==job->owner);
  }else{
   assert(read32(memory+0x1088)==FS_APERTURE_VA+0x50000u);
   assert(read32(memory+0x1084)==0x20000u);
   assert(read32(memory+0x1090)==FS_APERTURE_VA+0x50040u);
   assert(read32(memory+0x1098)==FS_APERTURE_VA+0x70000u);
  }
  assert(read32(memory+0x7144)==(testB3?96u:48u));
 }

 if(expectedLoad){for(uint32_t i=0;i<job->command_count;i++)assert(job->commands[i]!=((49ull<<56)|(1ull<<32)));}
 const unsigned char *drawmem=(const unsigned char*)scratch_observed->getBytesNoCopy();
 /* Mesa's FRAGMENT BBOX_MAX is in pixel coordinates. A full-sized FBD
  * cannot compensate for a stale 16x16 fragment-dispatch bounding box. */
 unsigned bbox_writes=0;
 for(uint32_t i=0;i<job->command_count;i++) {
  uint64_t c=job->commands[i];
  if((c>>56)==2&&((c>>48)&255)==43) {
   assert((uint32_t)c==((job->height-1)<<16|(job->width-1)));++bbox_writes;
  }
 }
 assert(bbox_writes>=1);
 assert(read32(drawmem+0xa80)==8 && read32(drawmem+0xa84)==9);
 assert(read32(drawmem+0xbc0)==8 && read32(drawmem+0xbc4)==14);
 assert(read32(drawmem+0xbc8)==0xd241f2);
 const uint32_t textureExtent=(expectedTextureWidth-1u)|((expectedTextureHeight-1u)<<16);
 assert(read32(drawmem+0x7180)==(2u|(2u<<4)|((187u<<12)<<10)|(expectedTextureFormat==80?0x1000u:0u)));
 assert(read32(drawmem+0x7184)==textureExtent);
 assert(read32(drawmem+0x7204)==expectedTextureStride*expectedTextureHeight);
 assert(read32(drawmem+0x7210)==expectedTextureStride);
 assert(read32(drawmem+0x721c)==textureExtent);
 if(testB3){
  assert(read32(drawmem+0x7180)==0x2ec01022u);
  assert(textureExtent==0x00170307u);
  assert(read32(drawmem+0x7204)==74496u&&read32(drawmem+0x7210)==3104u);
 }
 const uint32_t targetExtent=(expected_target_width-1u)|((expected_target_height-1u)<<16);
 assert(read32(drawmem+32)==targetExtent&&read32(drawmem+40)==targetExtent);
 assert(read32(drawmem+168)==expected_target_stride);
 assert(read32(drawmem+172)==(uint64_t)expected_target_stride*expected_target_height);
 const uint32_t clearWords[4]={0x00000a00u,0xf0122122u,0x00000019u,0x000ed000u};
 const uint32_t sourceoverWords[4]={0x00000a01u,0xf0d32d32u,0x0000001au,0x000ed000u};
 const uint32_t *expectedBlend=expectedSourceOver?sourceoverWords:clearWords;
 for(unsigned i=0;i<4;i++){if(read32(drawmem+0x440+4*i)!=expectedBlend[i])fprintf(stderr,"blend mismatch mode=%d i=%u got=%08x expected=%08x\n",expectedSourceOver,i,read32(drawmem+0x440+4*i),expectedBlend[i]);assert(read32(drawmem+0x440+4*i)==expectedBlend[i]);}
 assert(read32(drawmem+0xb30)==expectedBlend[2]&&read32(drawmem+0xb34)==expectedBlend[3]);
 unsigned r57=0;
 for(uint32_t i=0;i<job->command_count;i++)if((job->commands[i]>>56)==2&&((job->commands[i]>>48)&255)==57) {
  assert((uint32_t)job->commands[i]==(expectedSourceOver?2u:3u));++r57;
 }
 assert(r57==1);
 if(job->request->resource_count!=expectedResources || job->request->resources[1].pa!=job->request->resources[2].pa || job->request->resources[2].pa!=job->request->resources[3].pa)return -40;
 size_t target_pre=0;
 for(const auto&e:flush_events)if(e.first>=expected_target_cpu+expected_target_offset&&
     e.first<expected_target_cpu+expected_target_offset+expected_target_pages*4096&&e.second==4096)++target_pre;
 assert(target_pre==expected_target_pages);
 submit_flush_index=flush_events.size();
 if(drop_slot_refs){auto f=drop_slot_refs;drop_slot_refs=nullptr;f();}
 if(submit_mode==1)return -41;
 enum fs_disposition d=fs_map(job->mapping,job->request,&submit_ops);
 if(d!=FS_KEEP_LIVE)return -41;
 if(expected_imported_lease)assert(active_test_lease&&active_test_lease->referencesForTest()==2);
 if(drop_imported_target){auto f=drop_imported_target;drop_imported_target=nullptr;f();
  if(expected_imported_lease){assert(active_test_lease->referencesForTest()==1);
   assert(released_test_surface&&released_test_surface->referencesForTest()==1);}
 }
 if(submit_mode==2)return -100; /* hardware-visible state; caller must quarantine */
 d=fs_unmap(job->mapping);if(d!=FS_RELEASED)return -100;
 if(value)*value=token^0xa5a55a5a;return 0;
}
struct IOExternalMethodArguments {uint64_t*scalarInput;uint32_t scalarInputCount;uint64_t*scalarOutput;uint32_t scalarOutputCount;void*structureInput;size_t structureInputSize;};
using IOReturn=int;
static constexpr int kIOReturnSuccess=0,kIOReturnBadArgument=-1,kIOReturnUnsupported=-2,kIOReturnNoResources=-3,kIOReturnNotFound=-4,kIOReturnNoMemory=-5,kIOReturnNotReady=-6,kIOReturnIOError=-7;
#ifndef TEGU_GPU_COMPOSITOR_V6
#define TEGU_GPU_COMPOSITOR_V6 1
#endif
#ifndef TEGU_GPU_COMPOSITOR_EXEC
#define TEGU_GPU_COMPOSITOR_EXEC 1
#endif
#ifndef TEGU_GPU_COMPOSITOR_IOSURFACE
#define TEGU_GPU_COMPOSITOR_IOSURFACE 1
#endif
#ifndef TEGU_GPU_COMPOSITOR_LOAD
#define TEGU_GPU_COMPOSITOR_LOAD 0
#endif
#define tgSlots 64
#define tgImportedTargetSlots 16
struct TeguGPUUserClient {
 uint64_t nextSerial=1;
 enum { TEGU_RENDER_PIPELINE_TGR=1, TEGU_RENDER_PIPELINE_AC1=2,
        TEGU_RENDER_PIPELINE_UI1=3, TEGU_RENDER_PIPELINE_UI2=4 };
 struct RenderPipeline {uint32_t handle,kind;tegu_render_info*info;void*ac1;
  tegu_ui1_info*ui1;tegu_ui2_info*ui2;TeguGPUBuffer*package;} renderPipelines[16]{};
 struct Slot {uint32_t handle;TeguGPUBuffer*buffer;} slots[tgSlots]{};
 TeguGPUImportedTarget importedTargets[16]{};
 IOReturn renderCompositor(IOExternalMethodArguments*);
 IOReturn renderFixedUI(IOExternalMethodArguments*,RenderPipeline*);
 IOReturn renderFixedUITexture(IOExternalMethodArguments*,RenderPipeline*);
};
IOReturn TeguGPUUserClient::renderFixedUI(IOExternalMethodArguments*,RenderPipeline*){return kIOReturnUnsupported;}
IOReturn TeguGPUUserClient::renderFixedUITexture(IOExternalMethodArguments*,RenderPipeline*){return kIOReturnUnsupported;}
static TeguGPUUserClient *imported_release_client;
static HostSurfaceBacking *released_test_backing;
static unsigned lease_destroyed_at_start;
static void drop_imported_target_record(){
 auto&t=imported_release_client->importedTargets[0];released_test_surface=t.surface;
 released_test_backing=(HostSurfaceBacking*)t.backing;
 if(t.backing)t.backing->release();if(t.gpuLease)t.gpuLease->release();if(t.surface)t.surface->release();memset(&t,0,sizeof(t));
}
static TeguGPUUserClient *release_client;
static void drop_client_slots(){for(auto&s:release_client->slots)if(s.buffer){s.buffer->release();s={};}}
#include "TeguGPUFullscreenAllocation.inc"
#include "TeguGPUCompositorClient.inc"
static TeguGPUBuffer *newbuf(uint64_t n,uint32_t h,TeguGPUUserClient&u){auto*b=TeguGPUBuffer::allocate(n);assert(b);for(auto&s:u.slots)if(!s.buffer){s={h,b};return b;}abort();}
static std::vector<unsigned char> fixture(unsigned slot,unsigned stride,bool sourceover=false){const char*path=getenv("TEGU_COMPOSITOR_TEST_PACKAGE");assert(path&&"wrapper must supply synthetic package path");FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char> p((size_t)n);assert(fread(p.data(),1,p.size(),f)==p.size());fclose(f);for(unsigned i=0;i<2;i++){size_t row=TEGU_RENDER_V6_ATTRIBUTE+i*28;tegu_render_put(p.data()+row+4,slot);tegu_render_put(p.data()+row+16,stride);}if(sourceover){const uint32_t blend[9]={1,1,0,1,5,0,1,5,15};for(unsigned i=0;i<9;i++)tegu_render_put(p.data()+128+4*i,blend[i]);}if(getenv("TEGU_COMPOSITOR_TEST_DISABLED")){const uint32_t blend[9]={1,0,0,1,0,0,1,0,15};for(unsigned i=0;i<9;i++)tegu_render_put(p.data()+128+4*i,blend[i]);}return p;}
static void run_success(unsigned slot,unsigned stride,int mode=0,bool sourceover=false,bool imported=false,bool load=false,
 uint32_t width=1080,uint32_t height=2424,uint32_t rowStride=4352,
 uint32_t textureWidth=16,uint32_t textureHeight=16,uint32_t textureStride=64,
 unsigned b3Mutation=0){
 const uint64_t baseline_live=buffer_live;
 flush_events.clear();submit_flush_index=SIZE_MAX;
 expected_target_width=width;expected_target_height=height;expected_target_stride=rowStride;
 const uint64_t targetBytes=(uint64_t)rowStride*height;
 expected_target_pages=(size_t)((targetBytes+4095u)/4096u);
 TeguGPUUserClient u;auto pkgbytes=fixture(slot,stride,sourceover);tegu_render_info parsed{};assert(!tegu_render_parse_for_import_classes(pkgbytes.data(),pkgbytes.size(),1,(testV7||testB3)?1:0,&parsed));assert(parsed.version==(testB3?8u:testV7?7u:6u)&&parsed.vertex[0].stride==stride&&parsed.vertex[0].buffer_index==slot);
 auto*pkg=TeguGPUBuffer::allocate(TEGU_RENDER_STORAGE_MAX+sizeof(parsed));assert(pkg);memcpy(pkg->getBytesNoCopy(),pkgbytes.data(),pkgbytes.size());
 auto*info=(tegu_render_info*)((unsigned char*)pkg->getBytesNoCopy()+TEGU_RENDER_STORAGE_MAX);*info=parsed;u.renderPipelines[0]={0xc0000001u,TeguGPUUserClient::TEGU_RENDER_PIPELINE_TGR,info,nullptr,nullptr,nullptr,pkg};
 TeguGPUBuffer *target=nullptr;uint64_t targetHandle=10;HostSurfaceBacking *surfaceBacking=nullptr;SweetSurface *surface=nullptr;GpuImportLease *lease=nullptr;
 expected_target_offset=imported?4096:0;
 lease_destroyed_at_start=gpu_lease_destroyed;surface_destroyed_at_start=surface_destroyed;
 surface_backing_destroyed_at_start=HostSurfaceBacking::destroyed;
 active_test_lease=nullptr;expected_imported_lease=imported;
 if(imported){
  imported_release_client=&u;
  const uint64_t allocation=(4096+targetBytes+16383)&~UINT64_C(16383);
  surfaceBacking=new HostSurfaceBacking(allocation,0x8b0000000ULL);surface=new SweetSurface;lease=new GpuImportLease(surface);active_test_lease=lease;
  surface->cpuAddress=(uintptr_t)surfaceBacking->getBytesNoCopy();
  u.importedTargets[0]={0xa0000001u,surface,lease,surfaceBacking,{width,height,rowStride,4096,TEGU_IOSURFACE_PIXELFORMAT_BGRA,(uint32_t)allocation,0,4,1,1,{}},80,TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION};
  targetHandle=0xa0000001u;expected_target_cpu=(uintptr_t)surfaceBacking->getBytesNoCopy();
 }else{
  target=newbuf(testB3?99524u:targetBytes,10,u);target->setLogicalLength(testB3?99524u:targetBytes);
  expected_target_cpu=(uintptr_t)target->getBytesNoCopy();
 }
 newbuf(256,11,u);newbuf(512,12,u);
 const uint64_t level0Bytes=(uint64_t)textureStride*textureHeight;
 const uint64_t textureBackingBytes=testB3?99524u:level0Bytes;
 expectedResourcePages=5u+(unsigned)((level0Bytes+4095u)/4096u);
 expectedTextureWidth=textureWidth;expectedTextureHeight=textureHeight;expectedTextureStride=textureStride;
 expectedTextureFormat=testB3?80u:70u;
 newbuf(textureBackingBytes,13,u);
 expectedLoad=load;tegu_compositor_request q{};q.version=load?TEGU_COMPOSITOR_REQUEST_VERSION_LOAD:TEGU_COMPOSITOR_REQUEST_VERSION;q.width=width;q.height=height;q.row_stride=rowStride;q.load_action=load?TEGU_COMPOSITOR_LOAD_LOAD:TEGU_COMPOSITOR_LOAD_CLEAR;q.store_action=1;q.attachment_mask=1;
 q.vertex.version=1;q.vertex.buffer_mask=1u<<slot;q.vertex.draw={0,3,1,0};q.vertex.buffers[slot]={11,0,3u*stride};
 if(mode==6)q.vertex.buffer_mask=1u<<(1u-slot);
 if(mode==7)q.vertex.buffers[1u-slot]={11,0,3u*stride};
 for(int i=0;i<4;i++)q.ubos[i]={12,(uint64_t[]){8,73,136,270}[i],(uint64_t[]){64,16,testB3?82u:72u,2}[i]};
 q.texture={13,0,level0Bytes};q.texture_format=testB3?80u:70u;q.texture_width=textureWidth;q.texture_height=textureHeight;q.texture_stride=textureStride;q.sampler_class=1;
 if(b3Mutation==1)q.texture.length=99524u;
 if(b3Mutation==2)q.texture_format=70u;
 if(b3Mutation==3)q.texture_width=775u;
 if(b3Mutation==4){q.texture_stride=3072u;q.texture.length=3072u*24u;}
 if(b3Mutation==5)q.sampler_class=0;
 if(b3Mutation==6){q.texture_stride=3136u;q.texture.length=3136u*24u;}
 if(b3Mutation==7)q.row_stride=3136u;
 if(b3Mutation==8)q.width=775u;
 uint64_t in[4]={u.renderPipelines[0].handle,targetHandle,imported?4096u:0u,0xff403020},out[2]={};IOExternalMethodArguments a{in,4,out,2,&q,sizeof(q)};
 if(mode==3)alloc_fail_after=alloc_calls+1;
 if(mode==4)u.slots[2].buffer->setPaBase(target->paBase());
 if(mode==5){release_client=&u;drop_slot_refs=drop_client_slots;}
 if(imported&&(mode==0||(mode==2&&(!load||TEGU_GPU_COMPOSITOR_LOAD)))){imported_release_client=&u;drop_imported_target=drop_imported_target_record;}
 submit_mode=(mode==1?1:mode==2?2:0);expectedSourceOver=sourceover;int r=u.renderCompositor(&a);expectedSourceOver=false;expectedLoad=false;submit_mode=0;alloc_fail_after=-1;
 size_t target_post=0;
 for(size_t i=submit_flush_index==SIZE_MAX?flush_events.size():submit_flush_index;i<flush_events.size();i++)
     if(flush_events[i].first>=expected_target_cpu+expected_target_offset&&
        flush_events[i].first<expected_target_cpu+expected_target_offset+expected_target_pages*4096&&flush_events[i].second==4096)++target_post;
 if(b3Mutation){assert(r!=kIOReturnSuccess&&buffer_live==baseline_live+(imported?4u:5u));assert(target_post==0);}
 else if(load&&!TEGU_GPU_COMPOSITOR_LOAD){assert(r==kIOReturnUnsupported&&buffer_live==baseline_live+(imported?4u:5u));assert(target_post==0);}
 else if(mode==1||mode==4){assert(r==kIOReturnNotReady&&buffer_live==baseline_live+5);assert(target_post==0);}
 else if(mode==3){assert(r==kIOReturnNoMemory&&buffer_live==baseline_live+5);assert(target_post==0);}
 else if(mode==6||mode==7){assert(r==kIOReturnBadArgument&&buffer_live==baseline_live+5);assert(target_post==0);}
 else if(mode==2){assert(r==kIOReturnIOError&&buffer_live>=baseline_live+6);assert(target_post==0);}
 else if(testB3){assert(r==kIOReturnSuccess&&target_post==expected_target_pages);}
 for(auto&s:u.slots)if(s.buffer){s.buffer->release();s={};}pkg->release();
 if(imported&&u.importedTargets[0].surface){drop_imported_target=nullptr;drop_imported_target_record();}
 if(imported&&(mode==2)&&(!load||TEGU_GPU_COMPOSITOR_LOAD)){assert(released_test_surface&&released_test_surface->referencesForTest()>0&&released_test_backing->referencesForTest()>0);}
 if(imported&&(mode==0)&&(!load||TEGU_GPU_COMPOSITOR_LOAD))assert(gpu_lease_destroyed==lease_destroyed_at_start+1);
 if(imported&&(mode==2)&&(!load||TEGU_GPU_COMPOSITOR_LOAD))assert(gpu_lease_destroyed==lease_destroyed_at_start&&surface_destroyed==surface_destroyed_at_start&&active_test_lease&&active_test_lease->referencesForTest()==1&&released_test_surface->referencesForTest()==1);
 if(imported&&(load&&!TEGU_GPU_COMPOSITOR_LOAD))assert(gpu_lease_destroyed==lease_destroyed_at_start+1);
 if(imported&&(mode==0|| (load&&!TEGU_GPU_COMPOSITOR_LOAD))&&surface_destroyed!=surface_destroyed_at_start+1)abort();
 if(imported&&(mode==0|| (load&&!TEGU_GPU_COMPOSITOR_LOAD))&&HostSurfaceBacking::destroyed!=surface_backing_destroyed_at_start+1)abort();
 if(imported&&(mode==2)&&(!load||TEGU_GPU_COMPOSITOR_LOAD))assert(HostSurfaceBacking::destroyed==surface_backing_destroyed_at_start&&released_test_backing->referencesForTest()==1);
 expected_imported_lease=false;active_test_lease=nullptr;
 if(mode<2||mode==3||mode==4||mode==5||mode==6||mode==7||(load&&!TEGU_GPU_COMPOSITOR_LOAD)||b3Mutation)assert(buffer_live==baseline_live);else assert(buffer_live>=baseline_live+5);
}
static void run_bad_bounds(){TeguGPUUserClient u;auto p=fixture(0,32);tegu_render_info x{};assert(!tegu_render_parse_for_import(p.data(),p.size(),1,&x));auto*pkg=TeguGPUBuffer::allocate(TEGU_RENDER_STORAGE_MAX+sizeof(x));assert(pkg);memcpy(pkg->getBytesNoCopy(),p.data(),p.size());auto*info=(tegu_render_info*)((unsigned char*)pkg->getBytesNoCopy()+TEGU_RENDER_STORAGE_MAX);*info=x;u.renderPipelines[0]={1,TeguGPUUserClient::TEGU_RENDER_PIPELINE_TGR,info,nullptr,nullptr,nullptr,pkg};newbuf(4096,2,u);newbuf(256,3,u);newbuf(512,4,u);newbuf(1024,5,u);
 tegu_compositor_request q{};q.version=1;q.width=16;q.height=16;q.row_stride=64;q.load_action=2;q.store_action=1;q.attachment_mask=1;q.vertex.version=1;q.vertex.buffer_mask=1;q.vertex.draw={0,3,1,0};q.vertex.buffers[0]={3,0,1};for(int i=0;i<4;i++)q.ubos[i]={4,(uint64_t)i*64,128};q.texture={5,0,1024};q.texture_format=70;q.texture_width=q.texture_height=16;q.texture_stride=64;q.sampler_class=1;uint64_t in[4]={1,2,0,0},out[2]={};IOExternalMethodArguments a{in,4,out,2,&q,sizeof(q)};int rr=u.renderCompositor(&a);assert(rr==kIOReturnBadArgument);assert(buffer_live==5);for(auto&s:u.slots)if(s.buffer){s.buffer->release();s={};}pkg->release();assert(buffer_live==0);}
static void run_legacy_guards(){auto p=fixture(0,32);tegu_render_info info{};assert(tegu_render_parse_for_import(p.data(),p.size(),0,&info)==TEGU_RENDER_IMPORT_UNSUPPORTED);assert(!tegu_render_parse_for_import(p.data(),p.size(),1,&info));assert(info.version==6&&info.color_count==2);struct tegu_render_addresses a{};struct tegu_render_target_layout l={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};struct tegu_render_state state;memset(&state,0xa5,sizeof(state));struct tegu_render_state before=state;assert(tegu_render_build_target_resources(p.data(),p.size(),&a,0,0,&l,&state)!=0);assert(!memcmp(&state,&before,sizeof(state)));}
int main(){testV7=getenv("TEGU_COMPOSITOR_TEST_V7")!=nullptr;testB3=getenv("TEGU_COMPOSITOR_TEST_B3")!=nullptr;
 if(testB3){
  auto p=fixture(1,48,false);tegu_render_info parsed={};
  assert(tegu_render_parse_for_import(p.data(),p.size(),1,&parsed)==TEGU_RENDER_IMPORT_UNSUPPORTED);
  assert(tegu_render_parse_for_import_classes(p.data(),p.size(),1,0,&parsed)==TEGU_RENDER_IMPORT_UNSUPPORTED);
  assert(!tegu_render_parse_for_import_classes(p.data(),p.size(),0,1,&parsed));
  assert(parsed.version==8&&parsed.compositor.sample_count==4&&parsed.compositor.ubo_window[2]==82);
  run_success(1,48,0,false,false,false,776,24,3104,776,24,3104);
  for(unsigned mutation=1;mutation<=8;mutation++)
   run_success(1,48,0,false,false,false,776,24,3104,776,24,3104,mutation);
  puts("PASS B3 selector16 exact package, sample4/windows64,16,82,2, 776x24 BGRA level0 over 99524-byte backings, descriptor bytes, refusal mutations and lifetimes (host services)");return 0;
 }
 if(testV7){
  auto p=fixture(1,48,true);tegu_render_info parsed={};
  assert(tegu_render_parse_for_import(p.data(),p.size(),1,&parsed)==TEGU_RENDER_IMPORT_UNSUPPORTED);
  assert(tegu_render_parse_for_import_classes(p.data(),p.size(),1,0,&parsed)==TEGU_RENDER_IMPORT_UNSUPPORTED);
  assert(!tegu_render_parse_for_import_classes(p.data(),p.size(),0,1,&parsed));
  assert(parsed.version==7&&parsed.compositor.sample_count==16&&parsed.compositor.ubo_window[2]==32);
  if(getenv("TEGU_COMPOSITOR_TEST_DISABLED")){run_success(1,48,0,false);run_success(1,48,0,false,false,true);puts("PASS v7 disabled-blend clear succeeds and LOAD is refused (host services)");return 0;}
  for(int mode: {0,1,3,4,6,7,5})run_success(1,48,mode,true);
  run_success(1,48,0,true,true);
  run_success(1,48,0,true,true,true);
  run_success(1,48,0,true,false,false,16,16,64);
  run_success(1,48,2,true,true,true);
  /* Full-size target dimensions retain the fixed 16x16 sampled source. */
  run_success(1,48,0,true,false,false,1125,2436,4608);
  run_success(1,48,0,true,false,true,1125,2436,4608);
  run_success(1,48,0,true,true,false,1125,2436,4608);
  puts("PASS v7 actual selector16 mapping, staged code intact, relocated chunk, imports/LOAD/lifetimes/failures (host services)");return 0;
 }
for(unsigned slot=0;slot<=1;slot++)for(unsigned stride: {32u,40u,48u})run_success(slot,stride);for(unsigned slot=0;slot<=1;slot++)for(unsigned stride: {32u,40u,48u})run_success(slot,stride,0,true);run_success(1,48,1);run_success(1,48,3);run_success(1,48,4);run_success(1,48,6);run_success(1,48,7);run_bad_bounds();run_legacy_guards();run_success(1,48,5);run_success(1,48,0,false,true);run_success(1,48,0,true,true);run_success(1,48,0,true,true,true);run_success(1,48,2,true,true,true);puts("compositor selector/legacy/IOSurface guards ASan/UBSan host doubles: passed");}
