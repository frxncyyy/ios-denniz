// Host doubles test the actual patched user-client logic, not XNU VM internals.
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
#include "native_package_b3_writer.h"
#include <atomic>
#include <cstdio>
#include <map>
#include <string>
#include <type_traits>
extern "C" {
#include "native_package_v7_writer.h"
}
static int metalBootFlag;
static bool PE_parse_boot_argn(const char *name,void *out,int size){
 if(!strcmp(name,"tegu_gpu_codecheck")&&size==sizeof(int)){
  *(int *)out=0;return true;
 }
 assert(!strcmp(name,"tegu_metal") && size==sizeof(int));
 *(int *)out=metalBootFlag;return true;
}
using UInt32=uint32_t; using SInt64=int64_t; using IOOptionBits=uint32_t;
using IOByteCount=uint64_t; using IODirection=int; using vm_offset_t=uintptr_t;
using task_t=void*; using IOReturn=int;
extern "C" uint64_t ml_vtophys(uintptr_t address) {
 (void)address;
 /* The default-off host stub is not a physical-translation verification. */
 return 0;
}
constexpr int kIOReturnSuccess=0,kIOReturnNotFound=1,kIOReturnNotOpen=2,
 kIOReturnBadArgument=3,kIOReturnNoResources=4,kIOReturnNoMemory=5,kIOReturnUnsupported=6,kIOReturnNotReady=7,kIOReturnIOError=8;
constexpr int kIODirectionOut=2,kIODirectionInOut=1,kIOMemoryKernelUserShared=2,kIOMapDefaultCache=0,kIOMemoryMapperNone=4;
constexpr size_t page_size=16384;
#define round_page(n) (((size_t)(n)+(page_size-1))&~(page_size-1))
#ifndef TEGU_GPU_LOAD_ACTION
#define TEGU_GPU_LOAD_ACTION 0
#endif
static void *kernel_task=nullptr;
static const char *kIOUserClientClassKey="IOUserClientClass";
static bool failAllocation=false,uniquePhysical=false;
static uint64_t nextPhysical=0x883000000ULL;
extern "C" { unsigned long gPhysBase=0x882000000ULL,gPhysSize=0x7e000000ULL; }
static uint64_t physicalBase=gPhysBase,segmentBytes=0x1000;
static uint64_t badPageOffset=UINT64_MAX,badPagePA=0;
static uint64_t badPageLength=0;
static bool forceAliasLoadBacking=false;
static unsigned physicalCalls=0;
#define APPLE_KEXT_OVERRIDE override
#define OSDeclareDefaultStructors(T) public: T()=default
#define OSDefineMetaClassAndStructors(T,B)
#define OSSafeReleaseNULL(p) do {if(p) {(p)->release();(p)=nullptr;}} while(0)
#define IOLog(...) ((void)0)
using IOLock=std::mutex;
static IOLock *IOLockAlloc(){return new IOLock;}
static void IOLockFree(IOLock *p){delete p;}
static void IOLockLock(IOLock *p){p->lock();}
static void IOLockUnlock(IOLock *p){p->unlock();}
static SInt64 OSAddAtomic64(SInt64 n,volatile SInt64 *p){return __atomic_fetch_add(p,n,__ATOMIC_SEQ_CST);}
static void OSMemoryBarrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);}
static int OSCompareAndSwapPtr(void *oldValue,void *newValue,void * volatile *address){
 return __atomic_compare_exchange_n(address,&oldValue,newValue,false,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);
}
class OSObject {
 std::atomic<unsigned> refs{1};
public:
 virtual ~OSObject()=default;
 virtual void free(){delete this;}
 void retain(){++refs;}
 void release(){if(--refs==0)free();}
 unsigned referencesForTest() const{return refs.load();}
};
static bool failOOLCopyAllocation=false;
static unsigned oolCopyAllocations=0,oolCopyFrees=0;
static void *IOMallocData(size_t n){++oolCopyAllocations;if(failOOLCopyAllocation)return nullptr;return malloc(n);}
static void IOFreeData(void*p,size_t){++oolCopyFrees;free(p);}
static void *IOMallocZero(size_t n){return calloc(1,n);}
static void IOFree(void *p,size_t){free(p);}
class IOService:public OSObject {
public:
 std::map<std::string,std::string> properties;
 virtual bool start(IOService*){return true;}
 void setProperty(const char *key,const char *value){properties[key]=value;}
 void setProperty(const char*,uint64_t,unsigned){}
 void registerService(){}
 void terminate(){}
};
class IOMemoryDescriptor:public OSObject {
public:
 virtual uint64_t getLength(){return 0;}
 virtual uint64_t getPhysicalSegment(uint64_t,IOByteCount *size,IOOptionBits){if(size)*size=0;return 0;}
 virtual uint64_t readBytes(uint64_t,void*,uint64_t){return 0;}
 virtual IOReturn prepare(IODirection){return kIOReturnSuccess;}
 virtual IOReturn complete(IODirection){return kIOReturnSuccess;}
};
class IOBufferMemoryDescriptor;
static thread_local IOBufferMemoryDescriptor *lastDescriptor;
static IOBufferMemoryDescriptor *badPageDescriptor;
static std::vector<IOBufferMemoryDescriptor *> liveDescriptors;
static std::mutex descriptorMutex;
static unsigned descriptorFreeCalls=0,surfaceFreeCalls=0;
static uint32_t trackedBackingID=0,trackedSurfaceID=0;
static unsigned trackedBackingFreeCalls=0,trackedSurfaceFreeCalls=0;
class IOBufferMemoryDescriptor:public IOMemoryDescriptor {
 void *data=nullptr;
 uint64_t length=0,mockPA=0;
 static constexpr size_t testCanaryBytes=32;
public:
 uint32_t testID=0;
 bool initWithPhysicalMask(task_t,unsigned flags,uint64_t bytes,uint64_t alignment,uint64_t mask){
  assert(flags & kIOMemoryMapperNone);
  assert(alignment==1 && mask==0x8bfffc000ULL);
  if(failAllocation)return false;
  if(uniquePhysical){mockPA=nextPhysical;nextPhysical+=bytes;}
  if(forceAliasLoadBacking&&badPageLength==bytes){badPageDescriptor=this;badPageOffset=4096;badPagePA=mockPA;}
  lastDescriptor=this;{std::lock_guard<std::mutex> guard(descriptorMutex);liveDescriptors.push_back(this);}length=bytes;size_t alloc=(size_t)((bytes+testCanaryBytes+4095u)&~UINT64_C(4095));data=aligned_alloc(4096,alloc); if(data){memset(data,0xcc,bytes);memset((unsigned char*)data+bytes,0xa7,testCanaryBytes);} return data!=nullptr;
 }
 uint64_t getPhysicalSegment(uint64_t offset,IOByteCount *size,IOOptionBits flags) override {
  assert(flags==kIOMemoryMapperNone);++physicalCalls;*size=segmentBytes;
  return offset==badPageOffset && (!badPageDescriptor || badPageDescriptor==this) &&
    (!badPageLength||badPageLength==length) ? badPagePA : (mockPA?mockPA:physicalBase)+offset;
 }
 void *getBytesNoCopy(){return data;}
 uint64_t getLength() override{return length;}
 bool testCanaryIntact() const {for(size_t i=0;i<testCanaryBytes;i++)if(((const unsigned char*)data)[length+i]!=0xa7)return false;return true;}
 void free()override{if(data){std::lock_guard<std::mutex> guard(descriptorMutex);for(auto it=liveDescriptors.begin();it!=liveDescriptors.end();++it)if(*it==this){liveDescriptors.erase(it);break;}}std::free(data);++descriptorFreeCalls;if(testID==trackedBackingID&&testID)++trackedBackingFreeCalls; OSObject::free();}
};
struct sweet_surface_plane {uint32_t width,height,bytesPerRow,offset,size;uint16_t bytesPerElement;uint8_t elementWidth,elementHeight;};
struct sweet_surface_gpu_layout {
 uint32_t width,height,bytesPerRow,offset,pixelFormat,allocSize,planeCount;
 uint16_t bytesPerElement;uint8_t elementWidth,elementHeight;
 sweet_surface_plane plane0;
};
class SweetSurface:public OSObject {
public:
 uint32_t id=0;bool gpuShared=false;IOMemoryDescriptor *memory=nullptr;
 sweet_surface_gpu_layout layout={};
 uintptr_t kernelAddress(){auto*b=dynamic_cast<IOBufferMemoryDescriptor*>(memory);return b?(uintptr_t)b->getBytesNoCopy():0;}
 void free()override{if(memory){memory->release();memory=nullptr;}++surfaceFreeCalls;if(id==trackedSurfaceID&&id)++trackedSurfaceFreeCalls;OSObject::free();}
};
static std::mutex surfaceMutex;
static SweetSurface *surfaceTable[4096];
static SweetSurface *sweet_surface_copy_gpu_shared(uint32_t id,sweet_surface_gpu_layout *layout){
 if(!layout||!id||id>=4096)return nullptr;
 std::lock_guard<std::mutex> guard(surfaceMutex);SweetSurface *s=surfaceTable[id];
 if(!s||!s->gpuShared)return nullptr;s->retain();*layout=s->layout;return s;
}
// Service doubles for selector17. Production classification/lease accounting
// are exercised separately against the actual SweetSurface.cpp methods.
static SweetSurface *sweet_surface_copy_gpu_import_candidate(uint32_t id,sweet_surface_gpu_layout *layout){
 if(!layout||!id||id>=4096)return nullptr;
 std::lock_guard<std::mutex> guard(surfaceMutex);SweetSurface *s=surfaceTable[id];
 if(!s||!dynamic_cast<IOBufferMemoryDescriptor*>(s->memory))return nullptr;
 s->retain();*layout=s->layout;return s;
}
static bool failImportLease=false;
class HostImportLease:public OSObject {
 SweetSurface *surface;
public:
 explicit HostImportLease(SweetSurface *s):surface(s){surface->retain();}
 void free()override{surface->release();OSObject::free();}
};
static OSObject *sweet_surface_gpu_import_lease_create(SweetSurface *surface){
 return failImportLease?nullptr:new HostImportLease(surface);
}
#define OSDynamicCast(T,p) dynamic_cast<T *>(p)
class HostOOLDescriptor final:public IOMemoryDescriptor {
public:
 std::vector<unsigned char> bytes;size_t readable=SIZE_MAX;unsigned reads=0,prepares=0,completes=0;
 bool failPrepare=false,failComplete=false;
 explicit HostOOLDescriptor(const std::vector<unsigned char>&b):bytes(b){}
 uint64_t getLength() override{return bytes.size();}
 IOReturn prepare(IODirection d) override{assert(d==kIODirectionOut);++prepares;return failPrepare?kIOReturnNoMemory:kIOReturnSuccess;}
 IOReturn complete(IODirection d) override{assert(d==kIODirectionOut);++completes;return failComplete?kIOReturnIOError:kIOReturnSuccess;}
 uint64_t readBytes(uint64_t off,void *dst,uint64_t n) override{++reads;if(off>bytes.size())return 0;size_t copy=(size_t)n;if(copy>bytes.size()-off)copy=bytes.size()-off;if(copy>readable)copy=readable;if(copy)memcpy(dst,bytes.data()+off,copy);return copy;}
};
struct IOExternalMethodDispatch {};
struct IOExternalMethodArguments {
 const void *structureInput=nullptr;
 uint32_t asyncWakePort=0,scalarInputCount=0,scalarOutputCount=0;
 uint64_t *scalarInput=nullptr,*scalarOutput=nullptr;
 size_t structureInputSize=0,structureOutputSize=0;
 IOMemoryDescriptor *structureInputDescriptor=nullptr,*structureOutputDescriptor=nullptr;
};
class IOUserClient:public IOService {
public:
 virtual bool initWithTask(task_t,void*,UInt32){return true;}
 virtual IOReturn clientClose(){return 0;}
 virtual IOReturn clientDied(){return 0;}
 virtual IOReturn clientMemoryForType(UInt32,IOOptionBits*,IOMemoryDescriptor**){return 0;}
 virtual IOReturn externalMethod(uint32_t,IOExternalMethodArguments*,IOExternalMethodDispatch*,OSObject*,void*){return 0;}
};
static bool gpuReady=false;
static int gpuStatus=0;
static unsigned gpuCalls=0;
extern "C" int tegu_gpu_ready(){return gpuReady;}
extern "C" int tegu_gpu_store(uint32_t token,uint32_t *value){
 ++gpuCalls;if(!gpuReady)return -41;if(gpuStatus)return gpuStatus;*value=token;return 0;
}
static unsigned cacheCalls,computeCalls;
static uintptr_t cachePage;
extern "C" void FlushPoC_DcacheRegion(vm_offset_t page,size_t size){
 assert(size&&!(size&4095));cachePage=page;++cacheCalls;
}
extern "C" int tegu_gpu_compute_ready(){return gpuReady;}
extern "C" int tegu_gpu_compute(uint64_t physical,unsigned offset,uint32_t token,uint32_t *value){
 ++computeCalls;assert(cacheCalls&1);assert(physical>=gPhysBase && !(physical&4095));
 if(!gpuReady)return -41;
 if(gpuStatus)return gpuStatus;
 auto *words=(uint32_t*)(cachePage+offset);
 for(unsigned i=0;i<256;i++)words[i]=token+i*i;
 *value=token;return 0;
}
#include "pipeline_format.h"
#include "render_pipeline_format.h"
static unsigned dispatchCalls;
static struct tegu_pipeline_job lastJob;
extern "C" int tegu_gpu_dispatch(const struct tegu_pipeline_job *job,uint32_t token,uint32_t *value){
 ++dispatchCalls;lastJob=*job;if(!gpuReady)return -41;if(gpuStatus)return gpuStatus;*value=token;return 0;
}
#include "TeguGPURenderJob.h"
#include "TeguGPURenderState.inc"
#include "render_load.h"
#include "load_mesa_templates.h"
#include "TeguGPUVertexFetch.inc"
#include "TeguGPUTextureState.inc"
#include "TeguGPURenderVertexRequest.h"
static unsigned renderCalls;
static uint64_t lastRenderTarget;
static struct tegu_render_job lastRender;
static unsigned char lastVertexDescriptors[4096];
static uint64_t lastVertexSRT;
static IOBufferMemoryDescriptor *renderScratch,*renderTextureDescriptors;
static IOBufferMemoryDescriptor *renderLoadBacking;
static bool expectBadLoadAlias=false;
static bool expectVaryingState,expectBlendState,expectTextureState,expectBGRAState,expectRGBA16FTextureState;
static uint64_t expectedTexturePhysical=0;
static uint64_t expectedTargetPhysical=0;static unsigned char *expectedTargetBytes=nullptr;
extern "C" int tegu_gpu_render(const struct tegu_render_job *job,uint32_t token,uint32_t *value) {
 ++renderCalls;lastRender=*job;
 // Find scratch by the submitted command pointer: sampled descriptor allocation
 // intentionally follows scratch and must not be confused with it.
 auto *scratchBytes=(unsigned char *)job->commands-0x4000-4352;
 renderScratch=nullptr;
 {std::lock_guard<std::mutex> guard(descriptorMutex);for(auto *d:liveDescriptors)if(d->getBytesNoCopy()==scratchBytes)renderScratch=d;}
 assert(renderScratch);
 assert((job->command_count==57u||job->command_count==58u)&&cacheCalls>=65);
 assert(job->page_count>=65&&job->page_count<=256);
 unsigned targetEnd=job->target_page_start+job->target_page_count;
 assert(job->target_page_start==64&&targetEnd<=job->page_count);
 if(job->load_page_count) {
  assert(job->load_page_start==TEGU_LOAD_PAGE_START&&job->load_page_count==TEGU_LOAD_PAGE_COUNT);
  assert(job->readonly_load_mask==3&&job->executable_load_mask==4&&job->page_count==68);
  renderLoadBacking=lastDescriptor;assert(renderLoadBacking&&renderLoadBacking!=renderScratch);
  assert(renderLoadBacking->getLength()>=TEGU_LOAD_STAGE_OFFSET+sizeof(struct tegu_render_load_state));
  uint64_t loadBytes=renderLoadBacking->getLength();
  assert(loadBytes==round_page(TEGU_LOAD_STAGE_OFFSET+TEGU_LOAD_STAGE_CAPACITY));
  for(unsigned i=0;i<TEGU_LOAD_PAGE_COUNT;i++) {
   assert(job->pages[TEGU_LOAD_PAGE_START+i]==renderLoadBacking->getPhysicalSegment((uint64_t)i*4096,&segmentBytes,kIOMemoryMapperNone));
   for(unsigned j=0;j<TEGU_LOAD_PAGE_START;j++)assert(job->pages[TEGU_LOAD_PAGE_START+i]!=job->pages[j]);
  }
  const unsigned char *backing=(const unsigned char *)renderLoadBacking->getBytesNoCopy();
  const struct tegu_render_load_state *staged=(const struct tegu_render_load_state *)(backing+TEGU_LOAD_STAGE_OFFSET);
  assert(!memcmp(backing,staged->resources,4096));
  assert(!memcmp(backing+4096,staged->dcd,4096));
  assert(!memcmp(backing+8192,staged->shader_page,4096));
  assert(staged->page_start==job->load_page_start&&staged->page_count==job->load_page_count);
  if(expectBadLoadAlias){assert(job->pages[65]==job->pages[66]);return -1;}
 } else {assert(!job->readonly_load_mask&&!job->executable_load_mask);renderLoadBacking=nullptr;}
 if(expectedTargetPhysical){assert(job->pages[64]==expectedTargetPhysical);assert(cachePage==(uintptr_t)expectedTargetBytes);}
 for(unsigned i=0;i<job->page_count;i++) {
  assert(job->pages[i]>=gPhysBase&&!(job->pages[i]&4095));
  for(unsigned j=0;j<i;j++)assert(job->pages[i]!=job->pages[j]||(i>=targetEnd&&j>=targetEnd));
 }
 const unsigned char *scratch=(const unsigned char *)renderScratch->getBytesNoCopy();
 assert(tegu_render_word(scratch+132)==(expectBGRAState?0x860a0299u:0x86880299u));
 if(expectBlendState) {
  assert(tegu_render_word(scratch+0x440)==0xa01&&tegu_render_word(scratch+0x444)==0xf0503503);
  assert(tegu_render_word(scratch+0x448)==0x1a&&tegu_render_word(scratch+0xb30)==0x1a);
 }
 if(expectVaryingState) {
  assert(tegu_render_word(scratch+0x700)==0x80000038u&&tegu_render_word(scratch+0x704)==0x3800);
  auto qword=[&](unsigned at) {return (uint64_t)tegu_render_word(scratch+at)|(uint64_t)tegu_render_word(scratch+at+4)<<32;};
  assert(qword(0x608)==TEGU_RENDER_SCRATCH_VA+0x2000+384);
  assert(qword(0x708)==TEGU_RENDER_SCRATCH_VA+0x2000+768);
  uint32_t regs[256]={};bool idvs=false;
  for(unsigned i=0;i<job->command_count;i++) {
   uint64_t cmd=job->commands[i];unsigned op=cmd>>56,r=(cmd>>48)&255;
   if(op==1){regs[r]=(uint32_t)cmd;regs[r+1]=(cmd>>32)&0xffff;}
   if(op==2)regs[r]=(uint32_t)cmd;
   if(op==6){assert((cmd&(1ULL<<18))&&!(cmd&(7ULL<<35)));idvs=true;break;}
  }
  assert(idvs&&regs[57]==(expectBlendState?2u:3u)&&regs[48]==16&&((uint64_t)regs[18]|(uint64_t)regs[19]<<32)==TEGU_RENDER_SCRATCH_VA+0x700);
 }
 if(expectTextureState) {
  renderTextureDescriptors=lastDescriptor;
  assert(renderTextureDescriptors!=renderScratch&&job->page_count==69);
  const unsigned char *d=(const unsigned char *)renderTextureDescriptors->getBytesNoCopy();
  uint64_t descriptorVA=TEGU_RENDER_SCRATCH_VA+68*4096;
  uint64_t texelVA=TEGU_RENDER_SCRATCH_VA+67*4096;
  struct tegu_texture_input in={};in.version=1;in.format=expectRGBA16FTextureState?115:70;in.width=16;in.height=16;
  in.levels=1;in.layers=1;in.row_stride=expectRGBA16FTextureState?128:64;in.normalized=1;in.max_anisotropy=1;
  in.descriptor_address=descriptorVA;in.texel_address=texelVA;in.texel_length=expectRGBA16FTextureState?2048:1024;
  if(expectedTexturePhysical)assert(job->pages[67]==expectedTexturePhysical);
  struct tegu_texture_state expected;assert(!tegu_texture_state_build(&in,&expected));
  assert(!memcmp(d,expected.descriptors,4096));
  uint64_t fsSRT=0;
  for(unsigned i=0;i<job->command_count;i++)if((job->commands[i]>>48)==0x104)fsSRT=job->commands[i]&0xffffffffffffULL;
  assert(fsSRT==(descriptorVA|4));
 }
 memcpy(lastVertexDescriptors,scratch+0x6000,4096);lastVertexSRT=0;
 for(unsigned i=0;i<job->command_count;i++) {
  if((job->commands[i]>>48)==0x100)lastVertexSRT=job->commands[i]&0xffffffffffffULL;
  if((job->commands[i]>>48)==0x200)lastVertexSRT=(uint32_t)job->commands[i];
  if((job->commands[i]>>48)==0x201)lastVertexSRT|=(uint64_t)(uint32_t)job->commands[i]<<32;
 }
 assert(job->commands==(const uint64_t *)(scratch+0x4000+4352));
 assert(tegu_render_word(scratch+0x2000)==0x12&&tegu_render_word(scratch+0x3000)==0x34);
 if(!job->load_page_count)assert(tegu_render_word(scratch+176)==0x11223344);
 lastRenderTarget=(uint64_t)tegu_render_word(scratch+160)|((uint64_t)tegu_render_word(scratch+164)<<32);
 for(unsigned i=0x10000;i<0x40000;i++)assert(!scratch[i]);
 if(!gpuReady)return -41;if(gpuStatus)return gpuStatus;*value=token;return 0;
}
#include "TeguGPUImportedTargetPolicy.h"
#include "TeguSurfaceGPUPage.h"
struct SurfaceFixture {SweetSurface *surface;IOBufferMemoryDescriptor *backing;};
static SurfaceFixture makeSurface(uint32_t id,uint32_t format=TEGU_IOSURFACE_PIXELFORMAT_RGBA,uint32_t row=64,
 uint32_t width=16,uint32_t height=16,uint32_t planes=1,uint32_t planeSize=1024,
 uint32_t allocationBytes=page_size,uint32_t surfaceOffset=0){
 auto *backing=new IOBufferMemoryDescriptor;
 assert(backing->initWithPhysicalMask(kernel_task,kIODirectionInOut|kIOMemoryKernelUserShared|kIOMemoryMapperNone,
   allocationBytes,1,0x8bfffc000ULL));
 auto *surface=new SweetSurface;backing->testID=id;surface->id=id;surface->gpuShared=true;surface->memory=backing;backing->retain();
 surface->layout.width=width;surface->layout.height=height;surface->layout.bytesPerRow=row;
 surface->layout.offset=surfaceOffset;surface->layout.pixelFormat=format;surface->layout.allocSize=allocationBytes;
 surface->layout.planeCount=planes;surface->layout.bytesPerElement=4;surface->layout.elementWidth=1;surface->layout.elementHeight=1;
 surface->layout.plane0={width,height,row,surfaceOffset,planeSize,4,1,1};
 {std::lock_guard<std::mutex> guard(surfaceMutex);assert(!surfaceTable[id]);surfaceTable[id]=surface;}
 return {surface,backing};
}
static void removeSurface(uint32_t id){SweetSurface *s=nullptr;{std::lock_guard<std::mutex> guard(surfaceMutex);s=surfaceTable[id];surfaceTable[id]=nullptr;}if(s)s->release();}
#include "native_package_ui1.h"
#include "native_package_ui2.h"
#include "userclient-under-test.inc"
IOReturn TeguGPUUserClient::renderFixedUI(IOExternalMethodArguments *,RenderPipeline *){return kIOReturnUnsupported;}
IOReturn TeguGPUUserClient::renderFixedUITexture(IOExternalMethodArguments *,RenderPipeline *){return kIOReturnUnsupported;}
extern "C" void sweet_fb_note_gpu_write(void){}
static volatile uint64_t ac1TestL2[512];
static unsigned ac1FullscreenCalls;
static size_t lastJobCommandCount;static uint64_t lastJobFirstCommand;static uint32_t lastJobWidth,lastJobHeight,lastJobHeapBytes;
static bool lastJobScratchMatches;static IOBufferMemoryDescriptor *lastGenericScratchSource;
static uint32_t ac1ExpectedWidth,ac1ExpectedHeight,ac1ExpectedPages,ac1ExpectedClear;
extern "C" int tegu_gpu_fullscreen_context(struct tegu_fs_queue_context *out){
 if(!out)return -1;*out=(struct tegu_fs_queue_context){ac1TestL2,TEGU_FS_QUEUE_L2_PA,
  0x880000000ULL,0x880100000ULL};return 0;
}
extern "C" uint64_t tegu_gpu_fullscreen_timeout_ticks(void){return 123456;}
extern "C" int tegu_gpu_render_fullscreen(const struct tegu_full_render_job *job,
 uint32_t token,uint32_t *value){
 if(!job||!job->mapping||!value)return -40;
 ++ac1FullscreenCalls;
#if TEGU_GPU_ATTACHMENT_CLEAR
 assert(job->width==ac1ExpectedWidth&&job->height==ac1ExpectedHeight&&job->timeout_ticks==123456);
 assert(job->target_page_count==ac1ExpectedPages&&job->heap_bytes==TEGU_AC1_RT1_HEAP_BYTES);
 bool sawScratch=false;{std::lock_guard<std::mutex> guard(descriptorMutex);for(auto *d:liveDescriptors)
  if(d->getLength()==TEGU_AC1_RT1_SCRATCH_BYTES){uint64_t clear0=0,clear1=0;
   memcpy(&clear0,(const uint8_t *)d->getBytesNoCopy()+0xf0,8);
   memcpy(&clear1,(const uint8_t *)d->getBytesNoCopy()+0xf8,8);
   uint64_t expected=(uint64_t)ac1ExpectedClear|((uint64_t)ac1ExpectedClear<<32);
   if(clear0==expected&&clear1==expected)sawScratch=true;}}
 assert(sawScratch);
 assert(job->command_count==TEGU_AC1_RT1_COMMAND_COUNT&&job->request->resource_count==1);
 assert(job->request->heap_count==FS_HEAP_PAGES);
#endif
 lastJobCommandCount=job->command_count;lastJobFirstCommand=job->command_count?job->commands[0]:0;
 lastJobWidth=job->width;lastJobHeight=job->height;lastJobHeapBytes=job->heap_bytes;lastJobScratchMatches=false;
 {std::lock_guard<std::mutex> guard(descriptorMutex);for(auto *d:liveDescriptors)
  if(d->getLength()==TEGU_GENERIC_JOB_SCRATCH_BYTES&&d!=lastGenericScratchSource&&lastGenericScratchSource&&
     !memcmp(d->getBytesNoCopy(),lastGenericScratchSource->getBytesNoCopy(),TEGU_GENERIC_JOB_SCRATCH_BYTES))
   lastJobScratchMatches=true;}
 job->mapping->active=0;job->mapping->disposition=FS_RELEASED;*value=token;return 0;
}
static int compute(TeguGPUUserClient *c,uint64_t handle,uint64_t offset,uint64_t count,uint64_t seed,uint64_t *out){
 uint64_t input[4]={handle,offset,count,seed};IOExternalMethodArguments a;
 a.scalarInputCount=4;a.scalarInput=input;a.scalarOutputCount=2;a.scalarOutput=out;
 return c->externalMethod(4,&a,nullptr,nullptr,nullptr);
}
static int submit(TeguGPUUserClient *c,uint64_t handle,uint64_t offset,uint64_t token,uint64_t *out){
 uint64_t input[3]={handle,offset,token};IOExternalMethodArguments a;
 a.scalarInputCount=3;a.scalarInput=input;a.scalarOutputCount=2;a.scalarOutput=out;
 return c->externalMethod(3,&a,nullptr,nullptr,nullptr);
}
static TeguGPUUserClient *client(){auto *c=new TeguGPUUserClient;assert(c->initWithTask(nullptr,nullptr,0));return c;}
static int call(TeguGPUUserClient *c,unsigned sel,uint64_t input,uint64_t *out){
 IOExternalMethodArguments a;
 a.scalarInputCount=sel?1:0;a.scalarInput=&input;
 a.scalarOutputCount=sel==0?4:sel==1?2:0;a.scalarOutput=out;
 return c->externalMethod(sel,&a,nullptr,nullptr,nullptr);
}
static int importPipeline(TeguGPUUserClient *c,const void *packet,size_t size,uint64_t *handle){
 IOExternalMethodArguments a;a.structureInput=packet;a.structureInputSize=size;
 a.scalarOutputCount=1;a.scalarOutput=handle;
 return c->externalMethod(5,&a,nullptr,nullptr,nullptr);
}
static int dispatch(TeguGPUUserClient *c,uint64_t handle,const void *packet,size_t size,uint64_t *out){
 IOExternalMethodArguments a;a.structureInput=packet;a.structureInputSize=size;
 a.scalarInputCount=1;a.scalarInput=&handle;a.scalarOutputCount=2;a.scalarOutput=out;
 return c->externalMethod(7,&a,nullptr,nullptr,nullptr);
}
static int importRender(TeguGPUUserClient *c,const void *packet,size_t size,uint64_t *out) {
 IOExternalMethodArguments a;a.structureInput=packet;a.structureInputSize=size;
 a.scalarOutputCount=1;a.scalarOutput=out;
 return c->externalMethod(8,&a,nullptr,nullptr,nullptr);
}
#if TEGU_GPU_COMPOSITOR_V7
static void testV7PackageImport()
{
 std::vector<uint8_t> vs(TEGU_NATIVE_V7_VS_BYTES),fs(TEGU_NATIVE_V7_FS_BYTES);
 for(size_t i=0;i<vs.size();i++)vs[i]=(uint8_t)(i*17u+3u);
 for(size_t i=0;i<fs.size();i++)fs[i]=(uint8_t)(i*29u+7u);
 uint8_t *package=nullptr;size_t packageBytes=0;
 assert(tegu_native_v7_build(vs.data(),vs.size(),fs.data(),fs.size(),&package,&packageBytes)==0);
 assert(packageBytes==TEGU_NATIVE_V7_PACKAGE_BYTES&&packageBytes==28416);
 tegu_render_info parsed{};assert(tegu_render_parse(package,packageBytes,&parsed)==0&&parsed.version==7);
 TeguGPUUserClient *c=client();uint64_t out[1]={0};
 assert(importRender(c,package,packageBytes,out)==kIOReturnSuccess&&out[0]==0xc0000001u);
 auto *stored=lastDescriptor;assert(stored&&stored->getLength()==round_page(TEGU_RENDER_STORAGE_MAX+sizeof(parsed)));
 const auto *bytes=(const uint8_t *)stored->getBytesNoCopy();
 assert(TEGU_RENDER_STORAGE_MAX>=packageBytes);
 assert(!memcmp(bytes,package,packageBytes));
 const auto *metadata=(const tegu_render_info *)(bytes+TEGU_RENDER_STORAGE_MAX);
 assert((const uint8_t *)metadata>=bytes+packageBytes);
 assert((const uint8_t *)(metadata+1)<=bytes+stored->getLength());
 assert(!memcmp(metadata,&parsed,sizeof(parsed)));
 assert(stored->testCanaryIntact());
 c->clientClose();c->release();assert(tgWired==0);
 tegu_native_v7_free(&package,&packageBytes);assert(!package&&!packageBytes);
 puts("PASS selector-8 v7 import preserves all 28,416 package bytes and stores metadata after package within allocation");
}
static void testB3PackageImport()
{
 std::vector<uint8_t> vs(TEGU_NATIVE_B3_VS_BYTES),fs(TEGU_NATIVE_B3_FS_BYTES);
 for(size_t i=0;i<vs.size();i++)vs[i]=(uint8_t)(i*17u+3u);
 for(size_t i=0;i<fs.size();i++)fs[i]=(uint8_t)(i*29u+7u);
 uint8_t *package=nullptr;size_t packageBytes=0;
 assert(tegu_native_b3_build(vs.data(),vs.size(),fs.data(),fs.size(),&package,&packageBytes)==0);
 assert(packageBytes==TEGU_NATIVE_B3_PACKAGE_BYTES&&packageBytes==11520);
 tegu_render_info parsed{};assert(tegu_render_parse(package,packageBytes,&parsed)==0&&
  parsed.version==TEGU_RENDER_B3_VERSION&&parsed.compositor.sample_count==4&&
  parsed.compositor.ubo_window[2]==82);
 TeguGPUUserClient *c=client();uint64_t out[1]={0};
 assert(importRender(c,package,packageBytes,out)==kIOReturnSuccess&&out[0]==0xc0000001u);
 auto *stored=lastDescriptor;assert(stored&&stored->getLength()==round_page(TEGU_RENDER_STORAGE_MAX+sizeof(parsed)));
 const auto *bytes=(const uint8_t *)stored->getBytesNoCopy();
 assert(!memcmp(bytes,package,packageBytes));
 const auto *metadata=(const tegu_render_info *)(bytes+TEGU_RENDER_STORAGE_MAX);
 assert((const uint8_t *)metadata>=bytes+packageBytes&&
  (const uint8_t *)(metadata+1)<=bytes+stored->getLength()&&!memcmp(metadata,&parsed,sizeof(parsed)));
 assert(stored->testCanaryIntact());
 c->clientClose();c->release();assert(tgWired==0);
 tegu_native_b3_free(&package,&packageBytes);assert(!package&&!packageBytes);
 puts("PASS selector-8 B3 import preserves exact package and metadata ownership");
}
#endif
static int importRenderOOL(TeguGPUUserClient*c,IOMemoryDescriptor*d,const void*inlinep,size_t n,uint64_t*out){IOExternalMethodArguments a;a.structureInput=inlinep;a.structureInputSize=n;a.structureInputDescriptor=d;a.scalarOutputCount=1;a.scalarOutput=out;return c->externalMethod(8,&a,nullptr,nullptr,nullptr);}
static int importPipelineOOL(TeguGPUUserClient*c,IOMemoryDescriptor*d,const void*inlinep,size_t n,uint64_t*out){IOExternalMethodArguments a;a.structureInput=inlinep;a.structureInputSize=n;a.structureInputDescriptor=d;a.scalarOutputCount=1;a.scalarOutput=out;return c->externalMethod(5,&a,nullptr,nullptr,nullptr);}
#if TEGU_GPU_COMPOSITOR_V6
static void testOOLPackageImports(){
 const char*path=getenv("TEGU_OOL_TGR6_PACKAGE");assert(path);FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n==10240);std::vector<unsigned char>pkg((size_t)n);assert(fread(pkg.data(),1,pkg.size(),f)==pkg.size());fclose(f);
 std::vector<unsigned char> pipe(64+512,0);uint32_t h[16]={TEGU_PIPELINE_MAGIC,1,512,TEGU_PIPELINE_GPU,32,43,0,0x02000000,7,1,1,1,0,0,1,0};for(unsigned i=0;i<16;i++)tegu_pipeline_put(pipe.data()+4*i,h[i]);
 uint64_t in[2]={},out[2]={};TeguGPUUserClient*c=client();assert(importPipeline(c,pipe.data(),pipe.size(),in)==0);c->clientClose();c->release();
 unsigned a0=oolCopyAllocations,f0=oolCopyFrees;auto*pd=new HostOOLDescriptor(pipe);c=client();assert(importPipelineOOL(c,pd,(void*)(uintptr_t)1,0,out)==0&&out[0]==in[0]);assert(pd->reads==1&&pd->prepares==1&&pd->completes==1&&oolCopyAllocations==a0+1&&oolCopyFrees==f0+1);pd->release();c->clientClose();c->release();assert(!tgWired);
 a0=oolCopyAllocations;f0=oolCopyFrees;auto*rd=new HostOOLDescriptor(pkg);c=client();assert(importRenderOOL(c,rd,(void*)(uintptr_t)1,0,in)==0&&in[0]==0xc0000001u);assert(rd->reads==1&&rd->prepares==1&&rd->completes==1&&oolCopyAllocations==a0+1&&oolCopyFrees==f0+1);rd->release();c->clientClose();c->release();assert(!tgWired);
 c=client();assert(importRender(c,pkg.data(),pkg.size(),out)==0&&out[0]==in[0]);c->clientClose();c->release();assert(!tgWired);
 auto bad=pkg;bad[0]^=0x80;a0=oolCopyAllocations;f0=oolCopyFrees;rd=new HostOOLDescriptor(bad);c=client();assert(importRenderOOL(c,rd,nullptr,0,out)==kIOReturnBadArgument);assert(oolCopyAllocations==a0+1&&oolCopyFrees==f0+1&&rd->completes==1);rd->release();c->clientClose();c->release();c=client();assert(importRender(c,bad.data(),bad.size(),out)==kIOReturnBadArgument);c->clientClose();c->release();assert(!tgWired);
 auto over=pkg;over.resize(TEGU_RENDER_STORAGE_MAX+1);a0=oolCopyAllocations;rd=new HostOOLDescriptor(over);c=client();assert(importRenderOOL(c,rd,nullptr,0,out)==kIOReturnBadArgument&&oolCopyAllocations==a0&&rd->reads==0&&rd->prepares==0);rd->release();c->clientClose();c->release();
 rd=new HostOOLDescriptor(pkg);rd->readable=pkg.size()-1;a0=oolCopyAllocations;f0=oolCopyFrees;c=client();assert(importRenderOOL(c,rd,nullptr,0,out)==kIOReturnBadArgument);assert(oolCopyAllocations==a0+1&&oolCopyFrees==f0+1&&rd->reads==1&&rd->prepares==1&&rd->completes==1);rd->release();c->clientClose();c->release();assert(!tgWired);
 rd=new HostOOLDescriptor(pkg);rd->failPrepare=true;a0=oolCopyAllocations;f0=oolCopyFrees;c=client();assert(importRenderOOL(c,rd,nullptr,0,out)==kIOReturnNoMemory);assert(oolCopyAllocations==a0+1&&oolCopyFrees==f0+1&&rd->prepares==1&&rd->reads==0&&rd->completes==0);rd->release();c->clientClose();c->release();
 rd=new HostOOLDescriptor(pkg);rd->failComplete=true;a0=oolCopyAllocations;f0=oolCopyFrees;c=client();assert(importRenderOOL(c,rd,nullptr,0,out)==kIOReturnIOError);assert(oolCopyAllocations==a0+1&&oolCopyFrees==f0+1&&rd->prepares==1&&rd->reads==1&&rd->completes==1);rd->release();c->clientClose();c->release();assert(!tgWired);
 failOOLCopyAllocation=true;a0=oolCopyAllocations;f0=oolCopyFrees;rd=new HostOOLDescriptor(pkg);c=client();assert(importRenderOOL(c,rd,nullptr,0,out)==kIOReturnNoMemory);assert(oolCopyAllocations==a0+1&&oolCopyFrees==f0&&rd->reads==0&&rd->prepares==0);failOOLCopyAllocation=false;rd->release();c->clientClose();c->release();
 a0=oolCopyAllocations;rd=new HostOOLDescriptor(pkg);c=client();assert(importRenderOOL(c,rd,pkg.data(),pkg.size(),out)==kIOReturnBadArgument&&oolCopyAllocations==a0);uint64_t hnd=1;IOExternalMethodArguments a;a.structureInputDescriptor=rd;a.scalarInput=&hnd;a.scalarInputCount=1;a.scalarOutput=out;a.scalarOutputCount=2;assert(c->externalMethod(7,&a,nullptr,nullptr,nullptr)==kIOReturnBadArgument&&oolCopyAllocations==a0);rd->release();c->clientClose();c->release();assert(!tgWired);
 puts("PASS OOL import snapshot: inline/OOL parity; bounds; parser/read/prepare/complete/allocation errors; balanced cleanup");
}
#endif
static int drawRender(TeguGPUUserClient *c,uint64_t pipeline,uint64_t target,uint64_t offset,uint64_t clear,uint64_t *out) {
 uint64_t input[4]={pipeline,target,offset,clear};IOExternalMethodArguments a;
 a.scalarInput=input;a.scalarInputCount=4;a.scalarOutput=out;a.scalarOutputCount=2;
 return c->externalMethod(10,&a,nullptr,nullptr,nullptr);
}
static void testRenderImport(bool bgra=false) {
 expectBGRAState=bgra;expectBlendState=false;expectVaryingState=false;
 auto *c=client();uint64_t out[2]={};
 std::vector<unsigned char> bytes(TEGU_RENDER_CODE_OFFSET+64+8,0);
 uint32_t h[32]={TEGU_RENDER_MAGIC,1,0xb8a21030u,bgra?80u:70u,8,8,0,68,
  32,9,7,0,0,0x18000000,0,0,32,49,8,0,0,0x30000000,68,0};
 for(unsigned i=0;i<32;i++)tegu_render_put(bytes.data()+4*i,h[i]);
 tegu_render_put(bytes.data()+256+64,0x2222);
 bytes[384]=0x12;bytes[448]=0x34;
 struct tegu_render_info info;
 assert(!tegu_render_parse(bytes.data(),bytes.size(),&info));
 assert(info.stages[0].code_offset==384&&info.stages[1].code_offset==448);
 // Unaligned inputs, padding, limits and reserved fields; errors preserve output.
 std::vector<unsigned char> unaligned(bytes.size()+1);memcpy(unaligned.data()+1,bytes.data(),bytes.size());
 assert(!tegu_render_parse(unaligned.data()+1,bytes.size(),&info));
 for(unsigned word:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,17u,18u,19u,20u,21u,22u,23u,24u,31u}) {
  auto bad=bytes;tegu_render_put(bad.data()+word*4,UINT32_MAX);
  auto prior=info;assert(tegu_render_parse(bad.data(),bad.size(),&info));
  assert(!memcmp(&prior,&info,sizeof(info)));
  assert(importRender(c,bad.data(),bad.size(),out)==kIOReturnBadArgument&&!tgWired);
 }
 for(unsigned offset:{128u,324u,400u}) {
  auto bad=bytes;bad[offset]=1;assert(importRender(c,bad.data(),bad.size(),out)==kIOReturnBadArgument&&!tgWired);
 }
 assert(importRender(c,bytes.data(),bytes.size()-1,out)==kIOReturnBadArgument);
 failAllocation=true;assert(importRender(c,bytes.data(),bytes.size(),out)==kIOReturnNoMemory&&!tgWired);failAllocation=false;
 unsigned oldGpuCalls=gpuCalls,oldDispatchCalls=dispatchCalls,oldCacheCalls=cacheCalls;
 assert(!importRender(c,bytes.data(),bytes.size(),out)&&out[0]==0xc0000001u&&tgWired==page_size);
 uint64_t handle=out[0];auto *stored=lastDescriptor;
 uniquePhysical=true;
 assert(!call(c,1,4096,out));uint64_t targetHandle=out[0];auto *targetDescriptor=lastDescriptor;
 unsigned beforeRender=renderCalls;cacheCalls=0;
 for(uint64_t offset:{1ULL,3136ULL,16384ULL,~0ULL})
  assert(drawRender(c,handle,targetHandle,offset,0x11223344,out)==kIOReturnBadArgument);
 assert(drawRender(c,handle,targetHandle,0,0x100000000ULL,out)==kIOReturnBadArgument);
 assert(drawRender(c,handle,0,0,0x11223344,out)==kIOReturnNotFound);
 assert(drawRender(c,0,targetHandle,0,0x11223344,out)==kIOReturnNotFound);
 assert(renderCalls==beforeRender&&!cacheCalls);
 failAllocation=true;
 assert(drawRender(c,handle,targetHandle,0,0x11223344,out)==kIOReturnNoMemory);failAllocation=false;
 assert(tgWired==2*page_size);
 gpuReady=true;gpuStatus=0;
 assert(!drawRender(c,handle,targetHandle,128,0x11223344,out)&&out[0]==1&&out[1]==1);
 assert(renderCalls==beforeRender+1&&cacheCalls==130&&tgWired==2*page_size);
 assert(lastRenderTarget==TEGU_RENDER_TARGET_VA+128);
 gpuStatus=-44;cacheCalls=0;
 assert(drawRender(c,handle,targetHandle,0,0x11223344,out)==kIOReturnIOError);
 assert(cacheCalls==130&&tgWired==2*page_size);
 gpuReady=false;gpuStatus=0;cacheCalls=0;
 assert(drawRender(c,handle,targetHandle,0,0x11223344,out)==kIOReturnNotReady);
 assert(cacheCalls==130&&tgWired==2*page_size);
 gpuReady=true;gpuStatus=-100;cacheCalls=0;
 assert(drawRender(c,handle,targetHandle,0,0x11223344,out)==kIOReturnIOError);
 auto *quarantinedScratch=renderScratch;
 assert(cacheCalls==65&&tgWired==2*page_size+TEGU_RENDER_SCRATCH_BYTES);
 assert(!call(c,2,targetHandle,nullptr)); // extra ref keeps target alive
 assert(tgWired==2*page_size+TEGU_RENDER_SCRATCH_BYTES);
 // Simulate platform reset for test teardown only; real kernel retains to reboot.
 quarantinedScratch->release();targetDescriptor->release();
 assert(tgWired==page_size);uniquePhysical=false;gpuStatus=0;gpuReady=false;
 oldCacheCalls=cacheCalls;

 assert(!memcmp(stored->getBytesNoCopy(),bytes.data(),bytes.size()));
 bytes[384]=0x99;assert(((unsigned char *)stored->getBytesNoCopy())[384]==0x12);bytes[384]=0x12;
 IOOptionBits options=0;IOMemoryDescriptor *mapping=nullptr;
 assert(c->clientMemoryForType(handle,&options,&mapping)==kIOReturnNotFound&&!mapping);
 assert(call(c,6,handle,nullptr)==kIOReturnNotFound); // compute and render handles differ
 auto *other=client();assert(call(other,9,handle,nullptr)==kIOReturnNotFound);other->release();
 assert(!call(c,9,handle,nullptr)&&!tgWired);
 assert(call(c,9,handle,nullptr)==kIOReturnNotFound);
 for(unsigned i=0;i<16;i++)assert(!importRender(c,bytes.data(),bytes.size(),out)&&out[0]>handle);
 assert(importRender(c,bytes.data(),bytes.size(),out)==kIOReturnNoResources);
 c->clientClose();assert(!tgWired);
 assert(importRender(c,bytes.data(),bytes.size(),out)==kIOReturnNotOpen);c->release();
 assert(gpuCalls==oldGpuCalls&&dispatchCalls==oldDispatchCalls&&cacheCalls==oldCacheCalls);
}

static int drawVertex(TeguGPUUserClient *c,uint64_t pipeline,uint64_t target,
 const struct tegu_render_vertex_request *request,uint64_t *out,size_t size=TEGU_RENDER_VERTEX_REQUEST_BYTES) {
 uint64_t args[4]={pipeline,target,0,0x11223344};IOExternalMethodArguments a;
 a.scalarInput=args;a.scalarInputCount=4;a.scalarOutput=out;a.scalarOutputCount=2;
 a.structureInput=request;a.structureInputSize=size;
 return c->externalMethod(11,&a,nullptr,nullptr,nullptr);
}
static void testRenderVertexBuffers(bool varying,bool blending=false,bool bgra=false) {
 expectBGRAState=bgra;
 expectVaryingState=varying;expectBlendState=blending;
 auto *c=client();uint64_t out[4]={};uniquePhysical=true;gpuReady=true;gpuStatus=0;
 assert(!call(c,0,0,out)&&(out[1]&48)==48);
 std::vector<unsigned char> bytes(TEGU_RENDER_VERTEX_CODE_OFFSET+(varying?1024+1664:64+8),0);
 uint32_t header[32]={TEGU_RENDER_MAGIC,2,0xb8a21030u,bgra?80u:70u,8,8,0,68,
  32,9,7,0,0,0x18000000,0,0,32,49,8,0,0,0x30000000,68,0,1,1};
 if(varying) {
  header[1]=3;header[4]=1024;header[5]=1664;header[9]=7;header[10]=3;
  header[24]=3;header[25]=2;header[26]=384;header[27]=768;header[28]=32;header[30]=0x38000000;header[31]=16;
 }
 for(unsigned i=0;i<32;i++)tegu_render_put(bytes.data()+4*i,header[i]);
 tegu_render_put(bytes.data()+320,0x2222);
 uint32_t attr[7]={0,1,109,16,32,0,1};
 for(unsigned i=0;i<7;i++)tegu_render_put(bytes.data()+384+4*i,attr[i]);
 if(varying) {
  tegu_render_put(bytes.data()+384+12,0);
  const uint32_t second[7]={1,1,109,16,32,0,1};
  for(unsigned i=0;i<7;i++)tegu_render_put(bytes.data()+412+4*i,second[i]);
 }
 bytes[1280]=0x12;bytes[varying?2304:1344]=0x34;
 if(blending) {
  auto prior=bytes;bytes.assign(prior.size()+64,0);
  memcpy(bytes.data(),prior.data(),128);memcpy(bytes.data()+192,prior.data()+128,prior.size()-128);
  tegu_render_put(bytes.data()+4,4);
  const uint32_t blend[9]={1,1,0,4,5,0,4,5,15};
  for(unsigned i=0;i<9;i++)tegu_render_put(bytes.data()+128+4*i,blend[i]);
 }
 unsigned attributeOffset=blending?448:384;
 struct tegu_render_info info;
 assert(!tegu_render_parse(bytes.data(),bytes.size(),&info)&&info.version==(blending?4u:varying?3u:2u)&&info.vertex_mask==(varying?3u:1u));
 // No metadata/code truncation may read beyond input; refusals preserve output.
 for(size_t size=0;size<bytes.size();size++) {
  auto prior=info;assert(tegu_render_parse(bytes.data(),size,&info));assert(!memcmp(&prior,&info,sizeof(info)));
 }
 for(unsigned field=0;field<7;field++) {
  auto bad=bytes;tegu_render_put(bad.data()+attributeOffset+4*field,UINT32_MAX);
  assert(importRender(c,bad.data(),bad.size(),out)==kIOReturnBadArgument&&!tgWired);
 }
 for(unsigned at:{96u,100u,varying?105u:104u,blending?476u:412u,blending?1343u:1279u}) {
  auto bad=bytes;bad[at]^=0x80;
  assert(importRender(c,bad.data(),bad.size(),out)==kIOReturnBadArgument&&!tgWired);
 }
 assert(!importRender(c,bytes.data(),bytes.size(),out));uint64_t pipeline=out[0];
 assert(!call(c,1,4096,out));uint64_t target=out[0];auto *targetDescriptor=lastDescriptor;
 assert(!call(c,1,32768,out));uint64_t source=out[0];auto *sourceDescriptor=lastDescriptor;
 uint64_t wired=tgWired;
 struct tegu_render_vertex_request request={};request.version=1;request.buffer_mask=2;
 request.draw={0,3,1,0};request.buffers[1]={source,4092,96};
 assert(drawRender(c,pipeline,target,0,0x11223344,out)==kIOReturnUnsupported);
 unsigned before=renderCalls;cacheCalls=0;
 assert(drawVertex(c,pipeline,target,&request,out,sizeof(request)-1)==kIOReturnBadArgument);
 for(unsigned scenario=0;scenario<13;scenario++) {
  auto bad=request;
  switch(scenario) {
   case 0:bad.version=2;break;case 1:bad.buffer_mask|=4;break;case 2:bad.reserved[1]=1;break;
   case 3:bad.draw.vertex_count=4;break;case 4:bad.draw.base_instance=1;break;
   case 5:bad.buffers[0].handle=source;break;case 6:bad.buffers[1].offset=4093;break;
   case 7:bad.buffers[1].length=95;break;case 8:bad.buffers[1].offset=32768;break;
   case 9:bad.buffers[1].handle=target;break;case 10:bad.buffers[1].handle=UINT64_MAX;break;
   case 11:bad.draw.instance_count=2;break;case 12:bad.draw.first_vertex=1;break;
  }
  assert(drawVertex(c,pipeline,target,&bad,out)==kIOReturnBadArgument&&tgWired==wired);
 }
 auto bad=request;bad.buffers[1].handle=0x123456;
 assert(drawVertex(c,pipeline,target,&bad,out)==kIOReturnNotFound);
 auto *other=client();assert(drawVertex(other,pipeline,target,&request,out)==kIOReturnNotFound);other->release();
 assert(renderCalls==before&&!cacheCalls);
 assert(!drawVertex(c,pipeline,target,&request,out)&&out[0]==1&&out[1]==1);
 assert(lastRender.page_count==67&&lastRender.pages[65]+4096==lastRender.pages[66]);
 assert(lastRender.pages[65]==sourceDescriptor->getPhysicalSegment(0,&segmentBytes,kIOMemoryMapperNone));
 assert(lastVertexSRT==(TEGU_RENDER_SCRATCH_VA+0x6004));
 const unsigned char *buffer=lastVertexDescriptors+0x100+33*32;
 assert(tegu_render_word(buffer)==0x19&&tegu_render_word(buffer+4)==96);
 assert(tegu_render_word(buffer+8)==TEGU_RENDER_VERTEX_VA+4092&&tegu_render_word(buffer+12)==0);
 assert(tegu_render_word(lastVertexDescriptors+0x108)==(varying?0u:16u)&&tegu_render_word(lastVertexDescriptors+0x110)==32);
 assert(cacheCalls==132&&tgWired==wired);
 // Imported metadata is immutable and unused caller tail bytes cannot alter it.
 bytes[attributeOffset]=30;request.buffers[1].offset=0;cacheCalls=0;
 assert(!drawVertex(c,pipeline,target,&request,out)&&out[0]==2&&lastRender.page_count==66&&cacheCalls==131);
 gpuStatus=-44;cacheCalls=0;
 assert(drawVertex(c,pipeline,target,&request,out)==kIOReturnIOError&&cacheCalls==131&&tgWired==wired);
 // Read-only aliases can overlap each other but cannot alias target or scratch.
 // The actual MMU permission and duplicate-page checks run in submit_host_test.
 gpuStatus=-100;cacheCalls=0;
 assert(drawVertex(c,pipeline,target,&request,out)==kIOReturnIOError&&cacheCalls==66);
 auto *heldScratch=renderScratch;
 assert(!call(c,2,target,nullptr)&&!call(c,2,source,nullptr));
 assert(tgWired==wired+TEGU_RENDER_SCRATCH_BYTES);
 c->clientClose();c->release();assert(tgWired==wired-page_size+TEGU_RENDER_SCRATCH_BYTES);
 // Test teardown simulates a platform reset. Production retains all until reboot.
 heldScratch->release();targetDescriptor->release();sourceDescriptor->release();
 assert(!tgWired);uniquePhysical=false;gpuReady=false;gpuStatus=0;
}

static int drawTexture(TeguGPUUserClient *c,uint64_t pipeline,uint64_t target,
 const struct tegu_render_texture_request *request,uint64_t *out,size_t size=TEGU_RENDER_TEXTURE_REQUEST_BYTES) {
 uint64_t args[4]={pipeline,target,0,0x11223344};IOExternalMethodArguments a;
 a.scalarInput=args;a.scalarInputCount=4;a.scalarOutput=out;a.scalarOutputCount=2;
 a.structureInput=request;a.structureInputSize=size;
 return c->externalMethod(12,&a,nullptr,nullptr,nullptr);
}
static void testRenderSampledTexture() {
 expectVaryingState=true;expectBlendState=false;expectTextureState=true;
 auto *c=client();uint64_t out[4]={};uniquePhysical=true;gpuReady=true;gpuStatus=0;
 assert(!call(c,0,0,out)&&(out[1]&1023)==1023);
 std::vector<unsigned char> bytes(TEGU_RENDER_TEXTURE_CODE_OFFSET+1024+1664,0);
 uint32_t h[32]={TEGU_RENDER_MAGIC,5,0xb8a21030u,70,1024,1664,0,68,
  32,7,3,0,0,0x18000000,0,0,32,49,8,0,0,0x30000000,68,0,
  (1u<<7)|(1u<<8),2,384,768,32,0,0x38000000,16};
 for(unsigned i=0;i<32;i++)tegu_render_put(bytes.data()+4*i,h[i]);
 const uint32_t blend[9]={1,0,0,1,0,0,1,0,15};
 for(unsigned i=0;i<9;i++)tegu_render_put(bytes.data()+128+4*i,blend[i]);
 const uint32_t texture[7]={1,16,0,0,0,0,1};
 for(unsigned i=0;i<7;i++)tegu_render_put(bytes.data()+192+4*i,texture[i]);
 tegu_render_put(bytes.data()+384+64,0x2222);
 for(unsigned a=0;a<2;a++) {
  const uint32_t attr[7]={7+a,1,109,a*16,32,0,1};
  for(unsigned i=0;i<7;i++)tegu_render_put(bytes.data()+512+a*28+i*4,attr[i]);
 }
 bytes[1408]=0x12;bytes[2432]=0x34;
 struct tegu_render_info info={};assert(!tegu_render_parse(bytes.data(),bytes.size(),&info));
 assert(info.version==5&&info.texture.version==1&&info.push_offset==256);
 for(size_t size=0;size<bytes.size();size++) {
  auto prior=info;assert(tegu_render_parse(bytes.data(),size,&info));assert(!memcmp(&prior,&info,sizeof(info)));
 }
 for(unsigned at:{132u,164u,192u,196u,200u,204u,208u,212u,216u,220u,255u,568u,1407u}) {
  auto bad=bytes;bad[at]^=1;auto prior=info;
  assert(tegu_render_parse(bad.data(),bad.size(),&info)&&!memcmp(&prior,&info,sizeof(info)));
  assert(importRender(c,bad.data(),bad.size(),out)==kIOReturnBadArgument&&!tgWired);
 }
 assert(!importRender(c,bytes.data(),bytes.size(),out));uint64_t pipeline=out[0];
 assert(!call(c,1,4096,out));uint64_t target=out[0];auto *targetDescriptor=lastDescriptor;
 assert(!call(c,1,32768,out));uint64_t vertices=out[0];auto *vertexDescriptor=lastDescriptor;
 assert(!call(c,1,8192,out));uint64_t texels=out[0];auto *texelDescriptor=lastDescriptor;
 uint64_t wired=tgWired;
 struct tegu_render_texture_request request={};request.version=1;
 request.vertex.version=1;request.vertex.buffer_mask=2;request.vertex.draw={0,3,1,0};
 request.vertex.buffers[1]={vertices,4092,96};request.texture={texels,4096,1024};
 assert(drawRender(c,pipeline,target,0,0x11223344,out)==kIOReturnUnsupported);
 assert(drawVertex(c,pipeline,target,&request.vertex,out)==kIOReturnUnsupported);
 unsigned before=renderCalls;cacheCalls=0;
 assert(drawTexture(c,pipeline,target,&request,out,sizeof(request)-1)==kIOReturnBadArgument);
 for(unsigned scenario=0;scenario<15;scenario++) {
  auto bad=request;int expected=kIOReturnBadArgument;
  switch(scenario) {
   case 0:bad.version=2;expected=TEGU_GPU_RGBA16F_SOURCE?kIOReturnBadArgument:kIOReturnUnsupported;break;case 1:bad.reserved[0]=1;break;
   case 2:bad.reserved[1]=1;break;case 3:bad.reserved[2]=1;break;
   case 4:bad.texture.handle=0;break;case 5:bad.texture.handle=UINT64_MAX;break;
   case 6:bad.texture.offset=64;break;case 7:bad.texture.length=1023;break;
   case 8:bad.texture.length=1025;break;case 9:bad.texture.handle=target;break;
   case 10:bad.texture.offset=16384;break;case 11:bad.vertex.draw.vertex_count=4;break;
   case 12:bad.vertex.reserved[0]=1;break;case 13:bad.texture.handle=0x123456;expected=kIOReturnNotFound;break;
   case 14:bad.texture.offset=UINT64_MAX-4095;break;
  }
  assert(drawTexture(c,pipeline,target,&bad,out)==expected&&tgWired==wired);
 }
 assert(renderCalls==before&&!cacheCalls);
 assert(!drawTexture(c,pipeline,target,&request,out)&&out[0]==1&&out[1]==1);
 assert(renderCalls==before+1&&lastRender.page_count==69&&cacheCalls==134&&tgWired==wired);
 assert(lastRender.pages[67]==texelDescriptor->getPhysicalSegment(4096,&segmentBytes,kIOMemoryMapperNone));
 gpuStatus=-44;cacheCalls=0;
 assert(drawTexture(c,pipeline,target,&request,out)==kIOReturnIOError&&cacheCalls==134&&tgWired==wired);
 gpuStatus=-100;cacheCalls=0;
 assert(drawTexture(c,pipeline,target,&request,out)==kIOReturnIOError&&cacheCalls==69);
 auto *heldScratch=renderScratch,*heldDescriptors=renderTextureDescriptors;
 assert(tgWired==wired+TEGU_RENDER_SCRATCH_BYTES+page_size);
 assert(!call(c,2,texels,nullptr)&&!call(c,2,target,nullptr)&&!call(c,2,vertices,nullptr));
 c->clientClose();c->release();
 assert(tgWired==wired+TEGU_RENDER_SCRATCH_BYTES); // pipeline alone released
 // Simulate reset only for teardown: production quarantines every GPU resource.
 heldScratch->release();heldDescriptors->release();targetDescriptor->release();vertexDescriptor->release();texelDescriptor->release();
 assert(!tgWired);uniquePhysical=false;gpuReady=false;gpuStatus=0;expectTextureState=false;expectVaryingState=false;
}


static int importSurfaceCall(TeguGPUUserClient *c,uint64_t surfaceID,uint64_t *out){
 uint64_t input=surfaceID;IOExternalMethodArguments a;
 a.scalarInput=&input;a.scalarInputCount=1;a.scalarOutput=out;a.scalarOutputCount=1;
 return c->externalMethod(13,&a,nullptr,nullptr,nullptr);
}
static int importCompositorSurfaceCall(TeguGPUUserClient *c,uint64_t surfaceID,uint64_t version,uint64_t out[4]){
 uint64_t input[2]={surfaceID,version};IOExternalMethodArguments a;
 a.scalarInput=input;a.scalarInputCount=2;a.scalarOutput=out;a.scalarOutputCount=4;
 return c->externalMethod(17,&a,nullptr,nullptr,nullptr);
}
static int releaseSurfaceCall(TeguGPUUserClient *c,uint64_t handle){
 IOExternalMethodArguments a;a.scalarInput=&handle;a.scalarInputCount=1;
 return c->externalMethod(14,&a,nullptr,nullptr,nullptr);
}
static std::vector<unsigned char> simpleRenderPackage(uint32_t format,bool vertex=false,bool texture=false){
 if(texture){
  std::vector<unsigned char> bytes(TEGU_RENDER_TEXTURE_CODE_OFFSET+1024+1664,0);
  uint32_t h[32]={TEGU_RENDER_MAGIC,5,0xb8a21030u,format,1024,1664,0,68,
   32,7,3,0,0,0x18000000,0,0,32,49,8,0,0,0x30000000,68,0,
   (1u<<7)|(1u<<8),2,384,768,32,0,0x38000000,16};
  for(unsigned i=0;i<32;i++)tegu_render_put(bytes.data()+4*i,h[i]);
  const uint32_t blend[9]={1,0,0,1,0,0,1,0,15};
  for(unsigned i=0;i<9;i++)tegu_render_put(bytes.data()+128+4*i,blend[i]);
  const uint32_t tx[7]={1,16,0,0,0,0,1};
  for(unsigned i=0;i<7;i++)tegu_render_put(bytes.data()+192+4*i,tx[i]);
  tegu_render_put(bytes.data()+384+64,0x2222);
  for(unsigned a=0;a<2;a++){const uint32_t attr[7]={7+a,1,109,a*16,32,0,1};
   for(unsigned i=0;i<7;i++)tegu_render_put(bytes.data()+512+a*28+i*4,attr[i]);}
  bytes[1408]=0x12;bytes[2432]=0x34;return bytes;
 }
 if(vertex){
  std::vector<unsigned char> bytes(TEGU_RENDER_VERTEX_CODE_OFFSET+64+8,0);
  uint32_t h[32]={TEGU_RENDER_MAGIC,2,0xb8a21030u,format,8,8,0,68,
   32,9,7,0,0,0x18000000,0,0,32,49,8,0,0,0x30000000,68,0,1,1};
  for(unsigned i=0;i<32;i++)tegu_render_put(bytes.data()+4*i,h[i]);
  const uint32_t attr[7]={0,0,109,0,32,0,1};
  for(unsigned i=0;i<7;i++)tegu_render_put(bytes.data()+384+4*i,attr[i]);
  bytes[1280]=0x12;bytes[1344]=0x34;return bytes;
 }
 std::vector<unsigned char> bytes(TEGU_RENDER_CODE_OFFSET+64+8,0);
 uint32_t h[32]={TEGU_RENDER_MAGIC,1,0xb8a21030u,format,8,8,0,68,
  32,9,7,0,0,0x18000000,0,0,32,49,8,0,0,0x30000000,68,0};
 for(unsigned i=0;i<32;i++)tegu_render_put(bytes.data()+4*i,h[i]);
 tegu_render_put(bytes.data()+256+64,0x2222);bytes[384]=0x12;bytes[448]=0x34;return bytes;
}
static void testRenderSampledRGBA16FSource(){
 expectVaryingState=true;expectBlendState=false;expectTextureState=true;expectRGBA16FTextureState=TEGU_GPU_RGBA16F_SOURCE!=0;
 auto *c=client();uint64_t out[4]={};uniquePhysical=true;gpuReady=true;gpuStatus=0;
 assert(!call(c,0,0,out)&&!!(out[1]&2048)==!!TEGU_GPU_RGBA16F_SOURCE);
 auto package=simpleRenderPackage(70,false,true);uint64_t pipeline=0;
 assert(!importRender(c,package.data(),package.size(),&pipeline));
 assert(!call(c,1,4096,out));uint64_t target=out[0];
 assert(!call(c,1,32768,out));uint64_t vertices=out[0];
 assert(!call(c,1,8192,out));uint64_t source=out[0];auto *sourceBacking=lastDescriptor;
 /* Caller can allocate a larger source buffer, but only the exact 2K FP16
  * subrange is accepted and mapped read-only at one page-aligned offset. */
 uint64_t expectedPhysical=sourceBacking->getPhysicalSegment(4096,&segmentBytes,kIOMemoryMapperNone);
 expectedTexturePhysical=expectedPhysical;
 auto *data=(unsigned char *)sourceBacking->getBytesNoCopy()+4096;
 for(unsigned i=0;i<2048;i++)data[i]=(unsigned char)(i*37u+11u);
 struct tegu_render_texture_request req={};req.version=2;req.reserved[0]=115;req.reserved[1]=128;req.reserved[2]=2048;
 req.vertex.version=1;req.vertex.buffer_mask=2;req.vertex.draw={0,3,1,0};
 req.vertex.buffers[1]={vertices,4092,96};req.texture={source,4096,2048};
 uint64_t wired=tgWired;unsigned calls=renderCalls;cacheCalls=0;physicalCalls=0;
#if !TEGU_GPU_RGBA16F_SOURCE
 assert(drawTexture(c,pipeline,target,&req,out)==kIOReturnUnsupported);
 assert(renderCalls==calls&&!cacheCalls&&!physicalCalls&&tgWired==wired);
 assert(!call(c,2,source,nullptr)&&!call(c,2,target,nullptr)&&!call(c,2,vertices,nullptr));
 c->clientClose();c->release();assert(!tgWired);expectTextureState=false;expectVaryingState=false;
 gpuReady=false;gpuStatus=0;uniquePhysical=false;return;
#endif
 /* Current qualification only covers FP16 sources with RGBA8 target70.
  * Reject BGRA80 in the kernel even for hand-built selector12 requests. */
 auto bgraTexturePackage=simpleRenderPackage(80,false,true);uint64_t bgraPipeline=0;
 assert(!importRender(c,bgraTexturePackage.data(),bgraTexturePackage.size(),&bgraPipeline));
 calls=renderCalls;wired=tgWired;cacheCalls=physicalCalls=0;
 assert(drawTexture(c,bgraPipeline,target,&req,out)==kIOReturnUnsupported);
 assert(renderCalls==calls&&!cacheCalls&&!physicalCalls&&tgWired==wired);
 for(unsigned bad=0;bad<8;bad++) {
  auto r=req;
  switch(bad) {
   case 0:r.version=1;break;case 1:r.reserved[0]=70;break;case 2:r.reserved[1]=64;break;
   case 3:r.reserved[2]=1024;break;case 4:r.texture.length=2047;break;
   case 5:r.texture.length=2049;break;case 6:r.texture.offset=8192;break;
   case 7:r.texture.handle=0xa0000001u;break;
  }
  int want=bad==7?kIOReturnNotFound:kIOReturnBadArgument;
  assert(drawTexture(c,pipeline,target,&r,out)==want&&tgWired==wired);
 }
 assert(renderCalls==calls&&!cacheCalls&&!physicalCalls);
 assert(!drawTexture(c,pipeline,target,&req,out));
 assert(renderCalls==calls+1&&lastRender.page_count==69&&lastRender.pages[67]==expectedPhysical);
 assert(cacheCalls==134&&tgWired==wired);
 /* With variable targets enabled, the same format restriction applies to
  * the nested v2 texture payload transported through selector15. */
#if TEGU_GPU_TARGET_DIMENSIONS
 struct tegu_render_target_request sized={};sized.version=1;sized.width=16;sized.height=16;
 sized.row_stride=64;sized.kind=TEGU_RENDER_TARGET_REQUEST_TEXTURE;
 memcpy(sized.payload,&req,sizeof(req));
 uint64_t targetArgs[4]={bgraPipeline,target,0,0x11223344};IOExternalMethodArguments targetArgsView;
 targetArgsView.scalarInput=targetArgs;targetArgsView.scalarInputCount=4;
 targetArgsView.scalarOutput=out;targetArgsView.scalarOutputCount=2;
 targetArgsView.structureInput=&sized;targetArgsView.structureInputSize=sizeof(sized);
 cacheCalls=physicalCalls=0;
 assert(c->externalMethod(15,&targetArgsView,nullptr,nullptr,nullptr)==kIOReturnUnsupported);
 assert(renderCalls==calls+1&&!cacheCalls&&!physicalCalls&&tgWired==wired);
#endif
 assert(!call(c,2,source,nullptr)&&!call(c,2,target,nullptr)&&!call(c,2,vertices,nullptr));
 c->clientClose();c->release();assert(!tgWired);
 expectedTexturePhysical=0;expectRGBA16FTextureState=false;expectTextureState=false;expectVaryingState=false;
 gpuReady=false;gpuStatus=0;uniquePhysical=false;
}

static int drawSized(TeguGPUUserClient *c,uint64_t pipeline,uint64_t target,
 const struct tegu_render_target_request *request,uint64_t *out,size_t size=TEGU_RENDER_TARGET_REQUEST_BYTES) {
 uint64_t args[4]={pipeline,target,0,0x11223344};IOExternalMethodArguments a;
 a.scalarInput=args;a.scalarInputCount=4;a.scalarOutput=out;a.scalarOutputCount=2;
 a.structureInput=request;a.structureInputSize=size;
 return c->externalMethod(15,&a,nullptr,nullptr,nullptr);
}
static void testVariableRenderTargetABI() {
 expectVaryingState=false;expectBlendState=false;expectTextureState=false;
 uniquePhysical=true;gpuReady=true;gpuStatus=0;cacheCalls=renderCalls=0;
 auto *c=client();uint64_t out[4]={};
 assert(!call(c,0,0,out));assert(!!(out[1]&1024)==!!TEGU_GPU_TARGET_DIMENSIONS);
 assert(!!(out[1]&2048)==!!TEGU_GPU_RGBA16F_SOURCE);
 auto package=simpleRenderPackage(70);assert(!importRender(c,package.data(),package.size(),out));
 uint64_t pipeline=out[0];assert(!call(c,1,256u*256u*4u,out));uint64_t target=out[0];
 if(!TEGU_GPU_TARGET_DIMENSIONS) {
  struct tegu_render_target_request refused={};refused.version=1;refused.width=16;refused.height=16;refused.row_stride=64;
  assert(drawSized(c,pipeline,target,&refused,out)==kIOReturnUnsupported);
  c->clientClose();c->release();assert(!tgWired);uniquePhysical=false;gpuReady=false;return;
 }
 auto *targetDescriptor=lastDescriptor;uint64_t wired=tgWired;
 struct tegu_render_target_request request={};request.version=1;
 request.width=256;request.height=256;request.row_stride=1024;
 // Bad metadata must fail before allocating scratch or touching GPU submission.
 for(unsigned scenario=0;scenario<7;scenario++) {
  auto bad=request;
  switch(scenario) {
   case 0:bad.version=3;break;case 1:bad.width=257;break;
   case 2:bad.height=0;break;case 3:bad.row_stride=1000;break;
   case 4:bad.row_stride=2048;break;case 5:bad.kind=3;break;
   case 6:bad.reserved[1]=1;break;
  }
  assert(drawSized(c,pipeline,target,&bad,out)==kIOReturnBadArgument);
  assert(tgWired==wired&&!cacheCalls&&!renderCalls);
 }
 assert(drawSized(c,pipeline,target,&request,out)==kIOReturnSuccess);
 assert(lastRender.page_count==128&&lastRender.target_page_start==64&&lastRender.target_page_count==64);
 assert(lastRender.pages[64]==targetDescriptor->getPhysicalSegment(0,&segmentBytes,kIOMemoryMapperNone));
 assert(lastRender.pages[127]==targetDescriptor->getPhysicalSegment(63u*4096u,&segmentBytes,kIOMemoryMapperNone));
 assert(tgWired==wired&&cacheCalls==256&&renderCalls==1);
 // Selector 15 also accepts the legacy shape, while selectors 10..12 remain
 // exercised by the unchanged cases below.
 request.width=16;request.height=16;request.row_stride=64;
 assert(drawSized(c,pipeline,target,&request,out)==kIOReturnSuccess);
 assert(lastRender.page_count==65&&lastRender.target_page_count==1);
 assert(tgWired==wired&&renderCalls==2);
 c->clientClose();c->release();assert(!tgWired);
 uniquePhysical=false;gpuReady=false;
}
static void testNativeLoad() {
 expectVaryingState=false;expectBlendState=false;expectTextureState=false;
 uniquePhysical=true;gpuReady=true;gpuStatus=0;cacheCalls=renderCalls=physicalCalls=0;
 auto *c=client();uint64_t out[4]={};
 assert(!call(c,0,0,out));
 assert(!!(out[1]&TEGU_GPU_CAP_LOAD_PASS)==!!TEGU_GPU_LOAD_ACTION);
 auto package=simpleRenderPackage(70);assert(!importRender(c,package.data(),package.size(),out));
 uint64_t pipeline=out[0];assert(!call(c,1,4096,out));uint64_t target=out[0];
 auto *targetDescriptor=lastDescriptor;uint64_t wired=tgWired;
 cacheCalls=physicalCalls=0;
 struct tegu_render_target_request request={};request.version=TEGU_RENDER_TARGET_REQUEST_VERSION_LOAD;
 request.width=16;request.height=16;request.row_stride=64;request.reserved[0]=TEGU_RENDER_PASS_LOAD;
 unsigned before=renderCalls;
#if TEGU_GPU_LOAD_ACTION
#if TEGU_GPU_TARGET_DIMENSIONS
 assert(drawSized(c,pipeline,target,&request,out,sizeof(request)-1)==kIOReturnBadArgument);
 auto bad=request;bad.version=7;
 assert(drawSized(c,pipeline,target,&bad,out)==kIOReturnBadArgument);
 bad=request;bad.reserved[1]=1;
 assert(drawSized(c,pipeline,target,&bad,out)==kIOReturnBadArgument);
 bad=request;bad.payload[0]=1;
 assert(drawSized(c,pipeline,target,&bad,out)==kIOReturnBadArgument);
 bad=request;bad.width=32;
 assert(drawSized(c,pipeline,target,&bad,out)==kIOReturnUnsupported);
 assert(tgWired==wired&&renderCalls==before&&!cacheCalls&&!physicalCalls);
 assert(drawSized(c,pipeline,target,&request,out)==kIOReturnSuccess);
 assert(renderCalls==++before&&lastRender.page_count==68&&lastRender.target_page_start==64&&
        lastRender.target_page_count==1&&lastRender.load_page_start==65&&lastRender.load_page_count==3);
 assert(lastRender.readonly_load_mask==3&&lastRender.executable_load_mask==4);
 assert(lastRender.pages[64]==targetDescriptor->getPhysicalSegment(0,&segmentBytes,kIOMemoryMapperNone));
 for(unsigned i=65;i<68;i++)for(unsigned j=0;j<i;j++)assert(lastRender.pages[i]!=lastRender.pages[j]);
 assert(tgWired==wired); // all temporary LOAD storage is released after safe completion
 // The mock submitter rejects an aliased LOAD page; the client releases every
 // temporary object after this safe refusal.
 badPageLength=round_page(TEGU_LOAD_STAGE_OFFSET+TEGU_LOAD_STAGE_CAPACITY);
 forceAliasLoadBacking=true;expectBadLoadAlias=true;physicalCalls=0;
 assert(drawSized(c,pipeline,target,&request,out)==kIOReturnIOError);
 assert(renderCalls==++before&&tgWired==wired&&physicalCalls>0);
 forceAliasLoadBacking=false;expectBadLoadAlias=false;badPageLength=0;
 // Unsafe shutdown retains scratch, LOAD backing, and target through client close.
 gpuStatus=-100;
 assert(drawSized(c,pipeline,target,&request,out)==kIOReturnIOError);
 assert(renderCalls==++before&&tgWired==wired+TEGU_RENDER_SCRATCH_BYTES+
        round_page(TEGU_LOAD_STAGE_OFFSET+TEGU_LOAD_STAGE_CAPACITY));
 auto *heldScratch=renderScratch;auto *heldLoad=renderLoadBacking;
 assert(heldScratch&&heldLoad);
 c->clientClose();c->release();
 assert(tgWired==TEGU_RENDER_SCRATCH_BYTES+
        round_page(TEGU_LOAD_STAGE_OFFSET+TEGU_LOAD_STAGE_CAPACITY)+page_size);
 heldScratch->release();heldLoad->release();targetDescriptor->release();
 assert(!tgWired);
#else
 assert(drawSized(c,pipeline,target,&request,out)==kIOReturnUnsupported);
 assert(renderCalls==before&&tgWired==wired);
 c->clientClose();c->release();assert(!tgWired);
#endif
#else
 assert(drawSized(c,pipeline,target,&request,out)==kIOReturnUnsupported);
 assert(renderCalls==before&&tgWired==wired);
 c->clientClose();c->release();assert(!tgWired);
#endif
 uniquePhysical=false;gpuReady=false;gpuStatus=0;
}
static void testImportedSurfaceTargets(){
 uniquePhysical=true;gpuReady=true;gpuStatus=0;expectBGRAState=false;expectBlendState=false;
 auto *c=client();uint64_t out[4]={};auto package=simpleRenderPackage(70);
 assert(!importRender(c,package.data(),package.size(),out));uint64_t pipeline=out[0];
 trackedBackingID=trackedSurfaceID=101;unsigned goodSurfaceFrees=trackedSurfaceFreeCalls,goodBackingFrees=trackedBackingFreeCalls;
 auto good=makeSurface(101,TEGU_IOSURFACE_PIXELFORMAT_RGBA);uint64_t target=0;
 gpuReady=false;assert(!call(c,0,0,out)&&!(out[1]&512));
 assert(importSurfaceCall(c,101,&target)==kIOReturnNotReady&&target==0);gpuReady=true;
 assert(!call(c,0,0,out)&&(out[1]&512));
 assert(importSurfaceCall(c,0,&target)==kIOReturnBadArgument);
 assert(importSurfaceCall(c,999,&target)==kIOReturnNotFound);
 auto bad=makeSurface(102,TEGU_IOSURFACE_PIXELFORMAT_RGBA,128);assert(importSurfaceCall(c,102,&target)==kIOReturnUnsupported);
 removeSurface(102);bad.backing->release();
 auto wrongFormat=makeSurface(104,37);assert(importSurfaceCall(c,104,&target)==kIOReturnUnsupported);
 removeSurface(104);wrongFormat.backing->release();
 auto fakeMetalRGBA=makeSurface(107,70);assert(importSurfaceCall(c,107,&target)==kIOReturnUnsupported);
 removeSurface(107);fakeMetalRGBA.backing->release();
 auto fakeMetalBGRA=makeSurface(108,80);assert(importSurfaceCall(c,108,&target)==kIOReturnUnsupported);
 removeSurface(108);fakeMetalBGRA.backing->release();
 auto wrongPlane=makeSurface(105,TEGU_IOSURFACE_PIXELFORMAT_RGBA,64,16,16,1,2048);
 assert(importSurfaceCall(c,105,&target)==kIOReturnUnsupported);
 removeSurface(105);wrongPlane.backing->release();
 auto ineligible=makeSurface(106);ineligible.surface->gpuShared=false;
 assert(importSurfaceCall(c,106,&target)==kIOReturnNotFound);
 removeSurface(106);ineligible.backing->release();
 assert(!importSurfaceCall(c,101,&target)&&tegu_gpu_import_handle_valid((uint32_t)target));
 assert(target==0xa0000001u&&good.surface->referencesForTest()==2&&good.backing->referencesForTest()==3);
 IOOptionBits options=0;IOMemoryDescriptor *mapping=nullptr;
 assert(c->clientMemoryForType((UInt32)target,&options,&mapping)==kIOReturnNotFound&&!mapping);
 assert(compute(c,target,0,256,7,out)==kIOReturnNotFound);
 auto *other=client();uint64_t foreignPipeline=0;
 assert(!importRender(other,package.data(),package.size(),&foreignPipeline));
 assert(drawRender(other,foreignPipeline,target,0,0x11223344,out)==kIOReturnNotFound);
 assert(releaseSurfaceCall(other,target)==kIOReturnNotFound);
 // Existing global IOSurface IDs are not owner capabilities: a second client
 // can import this ID. It still cannot use the first client's tagged handle.
 uint64_t otherTarget=0;assert(!importSurfaceCall(other,101,&otherTarget)&&otherTarget==0xa0000001u);
 assert(otherTarget==target);
 // The client lock namespaces handles; the second client's own handle works.
 assert(!drawRender(other,foreignPipeline,otherTarget,0,0x11223344,out));
 assert(!releaseSurfaceCall(other,otherTarget));other->clientClose();other->release();
 assert(good.surface->referencesForTest()==2&&good.backing->referencesForTest()==3);
 // Imported surfaces are not accepted as vertex buffers or sample textures.
 auto vertexPackage=simpleRenderPackage(70,true);uint64_t vertexPipeline=0;
 assert(!importRender(c,vertexPackage.data(),vertexPackage.size(),&vertexPipeline));
 struct tegu_render_vertex_request vr={};vr.version=1;vr.buffer_mask=1;vr.draw={0,3,1,0};
 vr.buffers[0]={target,0,1024};
 assert(drawVertex(c,vertexPipeline,target,&vr,out)==kIOReturnNotFound);
 auto texturePackage=simpleRenderPackage(70,false,true);uint64_t texturePipeline=0;
 assert(!importRender(c,texturePackage.data(),texturePackage.size(),&texturePipeline));
 assert(!call(c,1,4096,out));uint64_t normalTarget=out[0];
 assert(!call(c,1,4096,out));uint64_t vertexBuffer=out[0];
 struct tegu_render_texture_request tr={};tr.version=1;tr.vertex.version=1;tr.vertex.buffer_mask=1;
 tr.vertex.draw={0,3,1,0};tr.vertex.buffers[0]={vertexBuffer,0,1024};tr.texture={target,0,1024};
 assert(drawTexture(c,texturePipeline,normalTarget,&tr,out)==kIOReturnNotFound);
 // The imported target keeps the existing draw offset contract; malformed
 // target ranges are refused before physical queries or GPU submission.
 unsigned renders=renderCalls;cacheCalls=0;physicalCalls=0;
 for(uint64_t offset:{1ULL,3136ULL,16384ULL,~0ULL})
  assert(drawRender(c,pipeline,target,offset,0x11223344,out)==kIOReturnBadArgument);
 assert(renderCalls==renders&&!cacheCalls&&!physicalCalls);
 assert(releaseSurfaceCall(c,0x100000001ULL)==kIOReturnBadArgument);
 // Out-of-policy page mappings are rechecked at submission and never reach GPU.
 physicalCalls=0;badPageDescriptor=good.backing;badPageOffset=0;badPagePA=0x8c0000000ULL;
 int invalidPageResult=drawRender(c,pipeline,target,0,0x11223344,out);assert(invalidPageResult==kIOReturnBadArgument);
 assert(renderCalls==renders&&physicalCalls==129);badPageOffset=UINT64_MAX;badPageDescriptor=nullptr;
 // Target handle alone resolves to this exact descriptor's physical backing.
 expectedTargetPhysical=good.backing->getPhysicalSegment(0,&segmentBytes,kIOMemoryMapperNone);
 expectedTargetBytes=(unsigned char*)good.backing->getBytesNoCopy();cacheCalls=0;
 assert(!drawRender(c,pipeline,target,128,0x11223344,out)&&lastRenderTarget==TEGU_RENDER_TARGET_VA+128);
 assert(lastRender.pages[64]==expectedTargetPhysical&&cachePage==(uintptr_t)expectedTargetBytes);
 assert(good.surface->referencesForTest()==2&&good.backing->referencesForTest()==3);
 expectedTargetPhysical=0;expectedTargetBytes=nullptr;
 // Handle release makes the old tag stale; reimport receives a new monotonic tag.
 assert(!releaseSurfaceCall(c,target));assert(releaseSurfaceCall(c,target)==kIOReturnNotFound);
 assert(drawRender(c,pipeline,target,0,0x11223344,out)==kIOReturnNotFound);
 uint64_t fresh=0;assert(!importSurfaceCall(c,101,&fresh)&&fresh==0xa0000002u);
 // Fill all 16 imported slots and verify the seventeenth is refused.
 uint64_t handles[16]={fresh};
 for(unsigned i=1;i<16;i++)assert(!importSurfaceCall(c,101,&handles[i]));
 assert(importSurfaceCall(c,101,&target)==kIOReturnNoResources);
 for(unsigned i=0;i<16;i++)assert(!releaseSurfaceCall(c,handles[i]));
 c->clientClose();c->release();assert(!tgWired);
 removeSurface(101);good.backing->release();
 assert(trackedSurfaceFreeCalls==goodSurfaceFrees+1&&trackedBackingFreeCalls==goodBackingFrees+1);
 trackedBackingID=trackedSurfaceID=0;
 // A format-80 pipeline imports only a format-80 target and submits that page.
 expectBGRAState=true;auto bgraPackage=simpleRenderPackage(80);c=client();
 assert(!importRender(c,bgraPackage.data(),bgraPackage.size(),out));pipeline=out[0];
 auto bgra=makeSurface(103,TEGU_IOSURFACE_PIXELFORMAT_BGRA);assert(!importSurfaceCall(c,103,&target));
 expectedTargetPhysical=bgra.backing->getPhysicalSegment(0,&segmentBytes,kIOMemoryMapperNone);
 expectedTargetBytes=(unsigned char*)bgra.backing->getBytesNoCopy();
 assert(!drawRender(c,pipeline,target,0,0x11223344,out));
 auto rgbaPackage=simpleRenderPackage(70);uint64_t wrongPipeline=0;
 assert(!importRender(c,rgbaPackage.data(),rgbaPackage.size(),&wrongPipeline));
 assert(drawRender(c,wrongPipeline,target,0,0x11223344,out)==kIOReturnUnsupported);
 expectedTargetPhysical=0;expectedTargetBytes=nullptr;
 // Unsafe DMA leaves the actual Surface, backing, and render scratch alive
 // after handle release, client close, ID removal, and external owner release.
 unsigned surfaceFrees=surfaceFreeCalls,backingFrees=descriptorFreeCalls;
 trackedBackingID=trackedSurfaceID=103;unsigned trackedSurfaceFrees=trackedSurfaceFreeCalls,trackedBackingFrees=trackedBackingFreeCalls;gpuStatus=-100;assert(drawRender(c,pipeline,target,0,0x11223344,out)==kIOReturnIOError);
 auto *quarantinedScratch=renderScratch;assert(quarantinedScratch);
 c->clientClose();c->release();removeSurface(103);bgra.backing->release();
 assert(trackedSurfaceFreeCalls==trackedSurfaceFrees&&trackedBackingFreeCalls==trackedBackingFrees);
 assert(tgWired==TEGU_RENDER_SCRATCH_BYTES);
 // Simulate platform reset only for test teardown; production retains until reset.
 bgra.surface->release();bgra.backing->release();quarantinedScratch->release();
 assert(trackedSurfaceFreeCalls==trackedSurfaceFrees+1&&trackedBackingFreeCalls==trackedBackingFrees+1&&!tgWired);trackedBackingID=trackedSurfaceID=0;
 gpuStatus=0;gpuReady=false;uniquePhysical=false;expectBGRAState=false;
}

#if TEGU_GPU_COMPOSITOR_IOSURFACE
static void testImportedCompositorSurfaceKernel(){
 uniquePhysical=true;gpuReady=true;gpuStatus=0;
 constexpr uint32_t width=1080,height=2424,row=4352,offset=4096;
 constexpr uint32_t bytes=row*height,allocation=round_page(offset+bytes);
 auto surface=makeSurface(120,TEGU_IOSURFACE_PIXELFORMAT_BGRA,row,width,height,0,0,allocation,offset);
 auto *c=client();uint64_t out[4]={0xdead,0xbeef,0xdead,0xbeef};
 assert(importCompositorSurfaceCall(c,0,TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION,out)==kIOReturnBadArgument);
 assert(importCompositorSurfaceCall(c,120,2,out)==kIOReturnBadArgument&&out[0]==0xdead);
 assert(importCompositorSurfaceCall(c,999,TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION,out)==kIOReturnNotFound);
 auto wrongFormat=makeSurface(121,TEGU_IOSURFACE_PIXELFORMAT_RGBA,row,width,height,0,0,allocation,offset);
 assert(importCompositorSurfaceCall(c,121,1,out)==kIOReturnUnsupported);
 removeSurface(121);wrongFormat.backing->release();
 auto wrongPlanes=makeSurface(122,TEGU_IOSURFACE_PIXELFORMAT_BGRA,row,width,height,2,bytes,allocation,offset);
 assert(importCompositorSurfaceCall(c,122,1,out)==kIOReturnUnsupported);
 removeSurface(122);wrongPlanes.backing->release();
 auto unshared=makeSurface(123,TEGU_IOSURFACE_PIXELFORMAT_BGRA,row,width,height,0,0,allocation,offset);
 unshared.surface->gpuShared=false;
 assert(!importCompositorSurfaceCall(c,123,1,out));
 assert(!releaseSurfaceCall(c,out[0]));
 removeSurface(123);unshared.backing->release();
 auto badPage=makeSurface(124,TEGU_IOSURFACE_PIXELFORMAT_BGRA,row,width,height,0,0,allocation,offset);
 badPageDescriptor=badPage.backing;badPageOffset=offset+0x1000;badPagePA=0x1000;
 unsigned pagesBefore=physicalCalls;
 assert(importCompositorSurfaceCall(c,124,1,out)==kIOReturnUnsupported);
 assert(physicalCalls>pagesBefore+1);badPageDescriptor=nullptr;badPageOffset=UINT64_MAX;
 removeSurface(124);badPage.backing->release();
 /* The production importer walks the complete nonzero-offset target span. */
 failImportLease=true;
 assert(importCompositorSurfaceCall(c,120,1,out)==kIOReturnNoResources);
 assert(surface.surface->referencesForTest()==1&&surface.backing->referencesForTest()==2);
 failImportLease=false;
 unsigned surfaceFreesBefore=surfaceFreeCalls,backingFreesBefore=descriptorFreeCalls;
 unsigned before=physicalCalls;out[0]=0;
 assert(!importCompositorSurfaceCall(c,120,1,out));
 uint64_t handle=out[0];assert(handle==0xa0000002u&&out[1]==offset&&out[2]==row&&out[3]==allocation);
 assert(physicalCalls-before==(bytes+0xfff)/4096);
 assert(surface.surface->referencesForTest()==3&&surface.backing->referencesForTest()==3);
 IOOptionBits options=0;IOMemoryDescriptor *memory=nullptr;
 assert(c->clientMemoryForType((UInt32)handle,&options,&memory)==kIOReturnNotFound&&!memory);
 removeSurface(120);surface.backing->release();
 assert(surface.surface->referencesForTest()==2&&surface.backing->referencesForTest()==2);
 assert(!releaseSurfaceCall(c,handle));
 assert(surfaceFreeCalls==surfaceFreesBefore+1&&descriptorFreeCalls==backingFreesBefore+1);
 assert(releaseSurfaceCall(c,handle)==kIOReturnNotFound);
 c->clientClose();c->release();assert(!tgWired);
 uniquePhysical=false;gpuReady=false;
}
#endif


#if TEGU_GPU_ATTACHMENT_CLEAR
static int drawAttachmentClear(TeguGPUUserClient *c,uint64_t pipeline,
 const struct tegu_attachment_clear_request *request,uint64_t *out){
 IOExternalMethodArguments a;a.scalarInputCount=1;a.scalarInput=&pipeline;
 a.scalarOutputCount=2;a.scalarOutput=out;a.structureInput=request;
 a.structureInputSize=sizeof(*request);
 return c->externalMethod(TEGU_ATTACHMENT_CLEAR_SELECTOR,&a,nullptr,nullptr,nullptr);
}
static std::vector<unsigned char> readAC1Fixture(){
 const char *path=getenv("TEGU_AC1_PACKAGE");assert(path);FILE *f=fopen(path,"rb");assert(f);
 fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);assert(n==TEGU_AC1_PACKAGE_BYTES);
 std::vector<unsigned char> p((size_t)n);assert(fread(p.data(),1,p.size(),f)==p.size());fclose(f);return p;
}
static void testAttachmentClearKernelRoute(){
 uniquePhysical=true;gpuReady=true;gpuStatus=0;ac1FullscreenCalls=0;ac1ExpectedWidth=16;ac1ExpectedHeight=16;ac1ExpectedPages=1;ac1ExpectedClear=0x44332211;
 auto package=readAC1Fixture();TeguGPUUserClient *c=client();uint64_t out[4]={0};
 uint64_t quota=tgWired;
 auto corrupt=package;corrupt[448]^=1;out[0]=0xfeed;
 assert(importRender(c,corrupt.data(),corrupt.size(),out)==kIOReturnBadArgument&&out[0]==0xfeed&&tgWired==quota);
 assert(importRender(c,package.data(),package.size()-1,out)==kIOReturnBadArgument&&tgWired==quota);
 assert(importRender(c,package.data(),package.size(),out)==kIOReturnSuccess);
 uint64_t pipeline=out[0];auto *stored=lastDescriptor;
 assert(stored&&stored->getLength()==round_page(TEGU_AC1_PACKAGE_BYTES+sizeof(tegu_ac1_info)));
 assert(!memcmp(stored->getBytesNoCopy(),package.data(),package.size()));
 auto *metadata=(const tegu_ac1_info *)((const uint8_t *)stored->getBytesNoCopy()+TEGU_AC1_PACKAGE_BYTES);
 assert(metadata->version==1&&stored->testCanaryIntact());
 // OOL import copies before parsing and releases its temporary copy.
 unsigned allocations=oolCopyAllocations,frees=oolCopyFrees;
 auto *ool=new HostOOLDescriptor(package);uint64_t oolOut[1]={};
 assert(importRenderOOL(c,ool,nullptr,0,oolOut)==kIOReturnSuccess&&oolOut[0]!=pipeline);
 assert(ool->prepares==1&&ool->reads==1&&ool->completes==1&&oolCopyAllocations==allocations+1&&oolCopyFrees==frees+1);
 ool->release();assert(call(c,9,oolOut[0],nullptr)==kIOReturnSuccess);

 struct Owned {uint64_t handle;IOBufferMemoryDescriptor *descriptor;};
 auto allocate=[&](uint64_t bytes){assert(call(c,1,bytes,out)==kIOReturnSuccess);return Owned{out[0],lastDescriptor};};
 Owned rt0=allocate(4096),rt1=allocate(4096),rect=allocate(16),matrix=allocate(320);
 memset(rect.descriptor->getBytesNoCopy(),0,16);memset(matrix.descriptor->getBytesNoCopy(),0,320);
 struct tegu_attachment_clear_request req={};
 req.version=TEGU_ATTACHMENT_CLEAR_REQUEST_VERSION;req.width=req.height=16;req.sample_count=1;
 req.attachment_count=2;req.target_mask=req.draw_write_mask=TEGU_ATTACHMENT_CLEAR_TARGET_MASK;
 req.targets[0].memory={rt0.handle,0,4096};req.targets[0].format=80;req.targets[0].row_stride=64;
 req.targets[0].load_action=TEGU_AC1_LOAD_DONTCARE;req.targets[0].store_action=TEGU_AC1_STORE_DONTCARE;
 req.targets[0].write_mask=TEGU_ATTACHMENT_CLEAR_RT0_MASK;
 req.targets[1].memory={rt1.handle,0,4096};req.targets[1].format=80;req.targets[1].row_stride=64;
 req.targets[1].load_action=TEGU_AC1_LOAD_CLEAR;req.targets[1].store_action=TEGU_AC1_STORE;
 req.targets[1].write_mask=TEGU_ATTACHMENT_CLEAR_RT1_MASK;req.targets[1].clear_rgba8=0x44332211;
 req.rect={rect.handle,0,16};req.matrix={matrix.handle,0,320};
 req.primitive_type=3;req.vertex_count=3;req.instance_count=1;
 out[0]=out[1]=0;{int status=drawAttachmentClear(c,pipeline,&req,out);if(status)fprintf(stderr,"AC1 draw status=%d\n",status);assert(status==kIOReturnSuccess);}
 assert(out[0]!=0&&out[1]!=0&&ac1FullscreenCalls==1);
 // A Backboard-sized shape exercises the dynamic target map while preserving one resource page and the 2 MiB heap.
 Owned wide0=allocate(77824),wide1=allocate(77824);auto wide=req;wide.width=776;wide.height=24;
 wide.targets[0].memory={wide0.handle,0,77824};wide.targets[1].memory={wide1.handle,0,77824};
 wide.targets[0].row_stride=wide.targets[1].row_stride=3136;
 ac1ExpectedWidth=776;ac1ExpectedHeight=24;ac1ExpectedPages=19;
 assert(drawAttachmentClear(c,pipeline,&wide,out)==kIOReturnSuccess&&ac1FullscreenCalls==2);
 assert(call(c,2,wide0.handle,nullptr)==kIOReturnSuccess&&call(c,2,wide1.handle,nullptr)==kIOReturnSuccess);
#if TEGU_GPU_COMPOSITOR_V6 && TEGU_GPU_COMPOSITOR_EXEC && TEGU_GPU_COMPOSITOR_IOSURFACE
 // Selector-17 targets retain the exact surface/backing lease through the same selector-18 completion.
 auto surface=makeSurface(130,TEGU_IOSURFACE_PIXELFORMAT_BGRA,64,16,16,0,0,4096,0);
 uint64_t importedOut[4]={};assert(importCompositorSurfaceCall(c,130,TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION,importedOut)==kIOReturnSuccess);
 auto imported=req;imported.targets[1].memory.handle=importedOut[0];
 ac1ExpectedWidth=16;ac1ExpectedHeight=16;ac1ExpectedPages=1;
 assert(drawAttachmentClear(c,pipeline,&imported,out)==kIOReturnSuccess&&ac1FullscreenCalls==3);
 assert(releaseSurfaceCall(c,importedOut[0])==kIOReturnSuccess);removeSurface(130);surface.backing->release();
#endif
 ac1ExpectedWidth=16;ac1ExpectedHeight=16;ac1ExpectedPages=1;
 auto refuse=[&](const tegu_attachment_clear_request &bad){uint64_t reject[2]={0xaaaa,0xbbbb};unsigned calls=ac1FullscreenCalls;
  assert(drawAttachmentClear(c,pipeline,&bad,reject)!=kIOReturnSuccess);
  assert(reject[0]==0xaaaa&&reject[1]==0xbbbb&&ac1FullscreenCalls==calls);};
 auto bad=req;bad.targets[1].memory.length=1024;refuse(bad);
 bad=req;bad.targets[1].memory.offset=4;refuse(bad);
 bad=req;bad.targets[1].clear_rgba8=1;bad.targets[1].load_action=TEGU_AC1_LOAD_DONTCARE;bad.targets[1].store_action=TEGU_AC1_STORE_DONTCARE;refuse(bad);
 bad=req;bad.rect.handle=rt1.handle;refuse(bad);
 Owned shortTarget=allocate(1000);bad=req;bad.targets[1].memory.handle=shortTarget.handle;refuse(bad);
 assert(call(c,2,shortTarget.handle,nullptr)==kIOReturnSuccess);
 assert(call(c,9,pipeline,nullptr)==kIOReturnSuccess);
 for(uint64_t h:{rt0.handle,rt1.handle,rect.handle,matrix.handle})assert(call(c,2,h,nullptr)==kIOReturnSuccess);
 c->clientClose();c->release();assert(tgWired==quota);uniquePhysical=false;gpuReady=false;
 puts("PASS selector-8 TAC1 ownership and selector-18 request/full-size lifetime route");
}
#endif

#if TEGU_GPU_COMPOSITOR_V6 && TEGU_GPU_COMPOSITOR_EXEC && TEGU_GPU_COMPOSITOR_V7
/* Selector 19 through the real client: capability, scratch copy, spans,
 * screened stream, completion, and refusals that never reach the GPU. */
static void testGenericJobKernelRoute(){
 uniquePhysical=true;gpuReady=true;gpuStatus=0;ac1FullscreenCalls=0;
 TeguGPUUserClient *c=client();uint64_t out[4]={};uint64_t quota=tgWired;
 assert(call(c,0,0,out)==0&&(out[1]&TEGU_GPU_CAP_GENERIC_JOB));
 struct Owned {uint64_t handle;IOBufferMemoryDescriptor *descriptor;};
 auto allocate=[&](uint64_t bytes){assert(call(c,1,bytes,out)==kIOReturnSuccess);return Owned{out[0],lastDescriptor};};
 Owned target=allocate(256*64),scratch=allocate(TEGU_GENERIC_JOB_SCRATCH_BYTES),a=allocate(4196),b=allocate(8192);
 uint8_t *image=(uint8_t *)scratch.descriptor->getBytesNoCopy();
 for(uint64_t i=0;i<TEGU_GENERIC_JOB_SCRATCH_BYTES;i++)image[i]=(uint8_t)(i*131u+7u);
 lastGenericScratchSource=scratch.descriptor;
 struct tegu_generic_job_request req={};req.version=TEGU_GENERIC_JOB_VERSION;req.flags=TEGU_GENERIC_JOB_HEAP2M;
 req.width=req.height=64;req.row_stride=256;req.target_token=target.handle;req.target_length=256*64;req.target_bytes=256*64;
 req.scratch_token=scratch.handle;req.exec_mask=UINT64_C(0x007f00000000000c); /* v7 CLEAR exec pages */req.span_count=2;
 req.spans[0]={a.handle,0,4196};req.spans[1]={b.handle,0,8192};
 req.command_count=2;req.commands[0]=(2ull<<56)|(33ull<<48)|3;req.commands[1]=34ull<<56;
 auto submit=[&](const tegu_generic_job_request &r,uint64_t *reply){IOExternalMethodArguments x;
  x.scalarInputCount=0;x.scalarInput=nullptr;x.scalarOutputCount=2;x.scalarOutput=reply;
  x.structureInput=&r;x.structureInputSize=sizeof(r);
  return c->externalMethod(TEGU_GENERIC_JOB_SELECTOR,&x,nullptr,nullptr,nullptr);};
 out[0]=out[1]=0;assert(submit(req,out)==kIOReturnSuccess&&out[0]&&ac1FullscreenCalls==1);
 assert(lastJobCommandCount==2&&lastJobFirstCommand==req.commands[0]&&lastJobWidth==64&&lastJobHeight==64&&
  lastJobHeapBytes==0x200000u&&lastJobScratchMatches);
 auto refuse=[&](const tegu_generic_job_request &bad){uint64_t reject[2]={0xaaaa,0xbbbb};unsigned calls=ac1FullscreenCalls;
  assert(submit(bad,reject)!=kIOReturnSuccess&&reject[0]==0xaaaa&&reject[1]==0xbbbb&&ac1FullscreenCalls==calls);};
 auto bad=req;bad.commands[1]=21ull<<56;refuse(bad);              /* STORE_MULTIPLE */
 bad=req;bad.commands[0]=22ull<<56;refuse(bad);                    /* BRANCH */
 bad=req;bad.spans[1].token=0x7fff;refuse(bad);                   /* unknown handle */
 bad=req;bad.spans[1].token=target.handle;refuse(bad);            /* target as resource */
 bad=req;bad.spans[0].length=8192;refuse(bad);                    /* beyond allocation */
 bad=req;bad.target_length=4096;refuse(bad);                      /* allocation mismatch */
 bad=req;bad.scratch_token=a.handle;refuse(bad);                  /* short scratch image */
 bad=req;bad.scratch_token=target.handle;refuse(bad);             /* scratch aliases target */
 bad=req;bad.exec_mask|=2;refuse(bad);                            /* exec outside v7 set */
 bad=req;bad.flags=0;refuse(bad);                                 /* 2 MiB heap required */
 lastGenericScratchSource=nullptr;
 for(uint64_t h:{target.handle,scratch.handle,a.handle,b.handle})assert(call(c,2,h,nullptr)==kIOReturnSuccess);
 c->clientClose();c->release();assert(tgWired==quota);uniquePhysical=false;gpuReady=false;
 puts("PASS selector-19 TGJ1 scratch copy, plan-checked spans, screened stream and refusal route");
}
#endif

int main(int argc,char **argv){
#if TEGU_GPU_ATTACHMENT_CLEAR
 if(argc==2&&!strcmp(argv[1],"ac1-route-only")){testAttachmentClearKernelRoute();return 0;}
#endif
#if TEGU_GPU_COMPOSITOR_V7
 if(argc==2&&!strcmp(argv[1],"v7-import-only")){testV7PackageImport();return 0;}
 if(argc==2&&!strcmp(argv[1],"b3-import-only")){testB3PackageImport();return 0;}
 if(argc==2&&!strcmp(argv[1],"tgj1-route-only")){testGenericJobKernelRoute();return 0;}
#endif
#if TEGU_GPU_COMPOSITOR_V6
 if(argc==2&&!strcmp(argv[1],"ool-import-only")){testOOLPackageImports();return 0;}
#endif
#if TEGU_GPU_COMPOSITOR_IOSURFACE
 if(argc==2&&!strcmp(argv[1],"iosurface-import-only")){testImportedCompositorSurfaceKernel();return 0;}
#else
 (void)argc;(void)argv;
#endif
 testNativeLoad();testVariableRenderTargetABI();testImportedSurfaceTargets();testRenderSampledTexture();testRenderSampledRGBA16FSource();
 testRenderImport();testRenderVertexBuffers(false);testRenderVertexBuffers(true);testRenderVertexBuffers(true,true);
 testRenderImport(true);testRenderVertexBuffers(false,false,true);testRenderVertexBuffers(true,false,true);testRenderVertexBuffers(true,true,true);
 static_assert(std::is_base_of<IOAcceleratorES,TeguGPU>::value);
 TeguGPU disabled;assert(disabled.start(nullptr));
 assert(disabled.properties.count("MetalPluginName")==0);
 metalBootFlag=1;TeguGPU enabled;assert(enabled.start(nullptr));
 assert(enabled.properties["MetalPluginName"]=="TeguMetal");
 assert(enabled.properties["MetalPluginClassName"]=="TeguComputeDevice");
 metalBootFlag=0;
 auto *c=client();uint64_t out[4]={};
 assert(call(c,0,0,out)==0 && out[0]==1 && out[1]==1);
 assert(call(c,1,0,out)==kIOReturnBadArgument);
 assert(call(c,1,UINT64_MAX,out)==kIOReturnBadArgument);
 assert(c->externalMethod(99,nullptr,nullptr,nullptr,nullptr)==kIOReturnUnsupported);
 assert(c->externalMethod(0,nullptr,nullptr,nullptr,nullptr)==kIOReturnBadArgument);
 IOExternalMethodArguments bad;bad.scalarOutputCount=4;bad.scalarOutput=out;bad.asyncWakePort=1;
 assert(c->externalMethod(0,&bad,nullptr,nullptr,nullptr)==kIOReturnBadArgument);
 failAllocation=true;assert(call(c,1,1,out)==kIOReturnNoMemory && tgWired==0);failAllocation=false;
 // Reject unsafe physical pages, including interior pages, and roll back quota.
 for(uint64_t pa:{0ULL,0x881fff000ULL,0x882000001ULL,0x8c0000000ULL,0xffffffffffffffffULL}){
  physicalBase=pa;assert(call(c,1,1,out)==kIOReturnNoMemory && tgWired==0);
 }
 physicalBase=gPhysBase;
 segmentBytes=4095;assert(call(c,1,1,out)==kIOReturnNoMemory && tgWired==0);segmentBytes=4096;
 badPageOffset=0x2000;badPagePA=0x8c0000000ULL;physicalCalls=0;
 assert(call(c,1,1,out)==kIOReturnNoMemory && tgWired==0 && physicalCalls==3);
 badPageOffset=UINT64_MAX;
 // Managed-RAM end is an independent restriction, even inside GB34.
 gPhysSize=0x3000;assert(call(c,1,1,out)==kIOReturnNoMemory && tgWired==0);
 gPhysSize=0x7e000000ULL;
 physicalBase=0x8bfffc000ULL;assert(call(c,1,1,out)==0);
 assert(call(c,2,out[0],nullptr)==0 && tgWired==0);physicalBase=gPhysBase;
 physicalCalls=0;
 assert(call(c,1,1,out)==0 && out[1]==page_size && physicalCalls==4);uint64_t first=out[0];
 IOMemoryDescriptor *mapping=nullptr;IOOptionBits options;
 assert(c->clientMemoryForType(first,&options,&mapping)==0);
 auto *buffer=static_cast<TeguGPUBuffer*>(mapping);uint64_t pa=123;
 assert(!buffer->gpuPage(1,&pa) && pa==0);
 assert(!buffer->gpuPage(page_size,&pa) && pa==0);
 assert(!buffer->gpuPage(UINT64_MAX,&pa) && pa==0);
 assert(!buffer->gpuPage(0,nullptr));
 assert(buffer->gpuPage(0x3000,&pa) && pa==gPhysBase+0x3000);
 auto *bytes=(unsigned char*)static_cast<TeguGPUBuffer*>(mapping)->getBytesNoCopy();
 for(size_t i=0;i<page_size;i++)assert(bytes[i]==0);
 bytes[0]=42;
 assert(submit(c,first,0,123,out)==kIOReturnNotReady && bytes[0]==42);
 gpuReady=true;gpuCalls=0;
 assert(submit(c,first,1,123,out)==kIOReturnBadArgument);
 assert(submit(c,first,page_size-2,123,out)==kIOReturnBadArgument);
 assert(submit(c,first,UINT64_MAX,123,out)==kIOReturnBadArgument);
 assert(submit(c,first,0,1ULL<<32,out)==kIOReturnBadArgument);
 assert(submit(c,first+1,0,123,out)==kIOReturnNotFound && gpuCalls==0);
 assert(submit(c,first,4,0x11223344,out)==0 && out[0]==1 && out[1]==0x11223344);
 assert(*(uint32_t*)(bytes+4)==0x11223344 && bytes[0]==42);
 gpuStatus=-44;
 assert(submit(c,first,4,0xdeadbeef,out)==kIOReturnIOError);
 assert(*(uint32_t*)(bytes+4)==0x11223344);
 gpuStatus=0;
 assert(submit(c,first,page_size-4,0xabcdef01,out)==0 && out[0]==2);
 assert(*(uint32_t*)(bytes+page_size-4)==0xabcdef01);
 // Direct compute: validation must precede cache work and GPU calls.
 cacheCalls=computeCalls=0;
 assert(compute(c,first,1,256,123,out)==kIOReturnBadArgument);
 assert(compute(c,first,3076,256,123,out)==kIOReturnBadArgument);
 assert(compute(c,first,page_size,256,123,out)==kIOReturnBadArgument);
 assert(compute(c,first,UINT64_MAX,256,123,out)==kIOReturnBadArgument);
 assert(compute(c,first,0,255,123,out)==kIOReturnBadArgument);
 assert(compute(c,first,0,256,1ULL<<32,out)==kIOReturnBadArgument);
 assert(compute(c,first+1,0,256,123,out)==kIOReturnNotFound);
 assert(!cacheCalls && !computeCalls);
 assert(compute(c,first,4096+128,256,0x12345678,out)==0 && out[0]==3 && out[1]==0x12345678);
 assert(cacheCalls==2 && computeCalls==1);
 for(unsigned i=0;i<256;i++)assert(*(uint32_t*)(bytes+4096+128+i*4)==0x12345678+i*i);
 gpuStatus=-44;assert(compute(c,first,0,256,123,out)==kIOReturnIOError);
 assert(cacheCalls==4);gpuStatus=0;
 gpuReady=false;
 assert(call(c,2,first,nullptr)==0);
 assert(call(c,2,first,nullptr)==kIOReturnNotFound);
 assert(tgWired==page_size && bytes[0]==42);
 assert(call(c,1,1,out)==0 && out[0]!=first);
 auto *other=client();IOMemoryDescriptor *none=nullptr;
 assert(other->clientMemoryForType(out[0],&options,&none)==kIOReturnNotFound);
 other->release();
 c->clientClose();assert(call(c,0,0,out)==kIOReturnNotOpen);c->release();
 assert(tgWired==page_size && bytes[0]==42);mapping->release();assert(tgWired==0);
 // Keep mappings alive across connection destruction: quota must still apply.
 std::vector<IOMemoryDescriptor*> held;
 for(unsigned i=0;i<8;i++){
  c=client();assert(call(c,1,tgMaxBuffer,out)==0);
  assert(c->clientMemoryForType(out[0],&options,&mapping)==0);held.push_back(mapping);c->release();
 }
 assert(tgWired==tgMaxWired);c=client();assert(call(c,1,1,out)==kIOReturnNoMemory);
 assert(tgWired==tgMaxWired);for(auto *m:held)m->release();assert(tgWired==0);
 // Capacity and stale handles.
 for(unsigned i=0;i<tgSlots;i++)assert(call(c,1,1,out)==0);
 assert(call(c,1,1,out)==kIOReturnNoResources);c->release();assert(tgWired==0);
 // Calls racing with close must either complete safely or report closed.
 c=client();std::vector<std::thread> workers;
 for(unsigned n=0;n<4;n++)workers.emplace_back([c]{
  for(unsigned i=0;i<100;i++){
   uint64_t local[4];int r=call(c,1,1,local);
   assert(r==0 || r==kIOReturnNotOpen);
   if(r==0){r=call(c,2,local[0],nullptr);assert(r==0 || r==kIOReturnNotOpen);}
  }
 });
 c->clientClose();for(auto &w:workers)w.join();c->release();assert(tgWired==0);
 // Pipeline handles are distinct and code cannot be mapped through the buffer ABI.
 c=client();std::vector<unsigned char> packet(64+512,0);
 uint32_t header[16]={TEGU_PIPELINE_MAGIC,1,512,TEGU_PIPELINE_GPU,32,43,0,0x02000000,7,1,1,1,0,0,1,0};
 for(unsigned i=0;i<16;i++)tegu_pipeline_put(packet.data()+i*4,header[i]);
 assert(importPipeline(c,packet.data(),packet.size()-1,out)==kIOReturnBadArgument && !tgWired);
 failAllocation=true;assert(importPipeline(c,packet.data(),packet.size(),out)==kIOReturnNoMemory && !tgWired);failAllocation=false;
 assert(importPipeline(c,packet.data(),packet.size(),out)==0 && out[0]==0x80000001u);
 first=out[0];assert(tgWired==page_size);
 assert(c->clientMemoryForType(first,&options,&mapping)==kIOReturnNotFound);
 assert(call(c,2,first,nullptr)==kIOReturnNotFound);
 other=client();assert(call(other,6,first,nullptr)==kIOReturnNotFound);other->release();
 // One imported pipeline with three resource slots bound to owned buffers.
 uint64_t pipelineHandle=first;
 assert(call(c,1,4096,out)==0);uint32_t bufferHandle=out[0];
 unsigned char wire[TEGU_PIPELINE_DISPATCH_BYTES]={};
 tegu_pipeline_put(wire,1);tegu_pipeline_put(wire+4,256);
 tegu_pipeline_put(wire+8,1);tegu_pipeline_put(wire+12,1);
 for(unsigned i=0;i<3;i++) {tegu_pipeline_put(wire+16+i*16,bufferHandle);tegu_pipeline_put(wire+24+i*16,1024);}
 gpuReady=true;gpuStatus=0;dispatchCalls=cacheCalls=0;
 assert(dispatch(c,pipelineHandle,wire,sizeof(wire)-1,out)==kIOReturnBadArgument);
 tegu_pipeline_put(wire+4,0);assert(dispatch(c,pipelineHandle,wire,sizeof(wire),out)==kIOReturnBadArgument);
 tegu_pipeline_put(wire+4,256);tegu_pipeline_put(wire+20,4092);
 assert(dispatch(c,pipelineHandle,wire,sizeof(wire),out)==kIOReturnBadArgument);
 tegu_pipeline_put(wire+20,0);tegu_pipeline_put(wire+28,1);
 assert(dispatch(c,pipelineHandle,wire,sizeof(wire),out)==kIOReturnBadArgument);
 tegu_pipeline_put(wire+28,0);assert(!dispatchCalls && !cacheCalls);
 assert(dispatch(c,pipelineHandle,wire,sizeof(wire),out)==0 && out[0]==1 && out[1]==1);
 assert(dispatchCalls==1 && cacheCalls==8 && lastJob.info.bindings==7 && lastJob.groups[0]==256);
 gpuStatus=-44;assert(dispatch(c,pipelineHandle,wire,sizeof(wire),out)==kIOReturnIOError);
 assert(cacheCalls==16);gpuStatus=0;
 assert(call(c,2,bufferHandle,nullptr)==0);
 assert(dispatch(c,pipelineHandle,wire,sizeof(wire),out)==kIOReturnBadArgument);
 assert(call(c,6,first,nullptr)==0 && !tgWired);
 assert(call(c,6,first,nullptr)==kIOReturnNotFound);
 for(unsigned i=0;i<16;i++)assert(importPipeline(c,packet.data(),packet.size(),out)==0 && out[0]>first);
 assert(importPipeline(c,packet.data(),packet.size(),out)==kIOReturnNoResources);
 c->clientClose();assert(!tgWired);c->release();
 // Unsafe compiled dispatch quarantines code AND resource descriptors.
 c=client();tegu_pipeline_put(packet.data()+8*4,1);
 assert(importPipeline(c,packet.data(),packet.size(),out)==0);
 pipelineHandle=out[0];auto *quarantinedCode=lastDescriptor;
 assert(call(c,1,4096,out)==0);bufferHandle=out[0];auto *quarantinedBuffer=lastDescriptor;
 memset(wire,0,sizeof(wire));tegu_pipeline_put(wire,1);
 for(unsigned i=0;i<3;i++)tegu_pipeline_put(wire+4+i*4,1);
 tegu_pipeline_put(wire+16,bufferHandle);tegu_pipeline_put(wire+24,4);
 gpuStatus=-100;cacheCalls=0;
 assert(dispatch(c,pipelineHandle,wire,sizeof(wire),out)==kIOReturnIOError && cacheCalls==2);
 c->release();assert(tgWired==2*page_size);
 quarantinedCode->release();quarantinedBuffer->release();assert(!tgWired);gpuStatus=0;
 // Unsafe backend shutdown retains the descriptor and quota across close.
 c=client();assert(call(c,1,1,out)==0);first=out[0];
 assert(c->clientMemoryForType(first,&options,&mapping)==0);
 gpuReady=true;gpuStatus=-100;cacheCalls=0;
 assert(compute(c,first,0,256,123,out)==kIOReturnIOError && cacheCalls==1);
 c->release();mapping->release();assert(tgWired==page_size);
 // Simulate eventual platform reset only for leak-checker teardown.
 mapping->release();assert(tgWired==0);
 puts("Tegu user-client ownership, quota, validation, failure and close-race tests passed");
}
