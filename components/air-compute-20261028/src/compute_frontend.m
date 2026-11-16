/* Experimental native Metal-selector compute frontend. Explicit construction
 * or opt-in experimental system registration; NOT a complete MTLDevice.
 * Optional runtime AIR/Mali pipeline support accompanies offline TGP import. No CPU
 * shader implementation: commit submits compute7 or experimental render10/15.
 * Manual reference counting, asynchronous target queues, shared storage only.
 */
#include "pipeline_format.h"
#include "submission_queue.h"
#include "target_device_info.h"
#include "device_render_query_caps.h"
#include "compositor_process.h"
#ifdef TEGU_GRAPHICS_BRIDGE
#include "render_vertex_request.h"
#include "render_target_request.h"
#include "compositor_request.h"
#include "attachment_clear_request.h"
#include "native_package_ui1.h"
#include "native_package_ui2.h"
#include "generic_job_ui.h"
#ifdef __APPLE__
/* dladdr anchor for TEGU_BUNDLE_BASE (Darwin Dl_info layout; no dlfcn.h in
 * the freestanding bundle build). */
struct tegu_dl_info {const char *fname;void *fbase;const char *sname;void *saddr;};
extern int tegu_dladdr(const void *,struct tegu_dl_info *) __asm__("_dladdr");
static __attribute__((unused)) void tegu_bundle_base_marker(void) {}
#endif
#endif
#if defined(TEGU_METAL_PROTOCOLS) && !defined(TEGU_METAL_FEATURE_INIT)
#error "Protocol registration requires the validated driver limits initializer"
#endif
#ifdef TEGU_METAL_FEATURE_INIT
#include "device_limits.h"
#endif
typedef unsigned long NSUInteger;
typedef unsigned long long U64;
#ifdef NORD2_JM_TRANSPORT
typedef uintptr_t TeguConnection;
#else
typedef unsigned TeguConnection;
#endif
typedef struct { NSUInteger width, height, depth; } MTLSize;
extern void *malloc(NSUInteger);
extern void free(void *);
extern void *calloc(NSUInteger,NSUInteger);
typedef void (^TeguCompletionHandler)(id);
struct TeguHandler { struct TeguHandler *next; TeguCompletionHandler block; };
#define TEGU_COMMAND_MAX_JOBS 1024u
#define TEGU_JOB_CHUNK 8u
extern void *memcpy(void *,const void *,NSUInteger);
extern void *memset(void *,int,NSUInteger);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
/* P1 throttle outcomes (teguExecute): waits for an unfinished command, and
 * waits that ran out (100 ms) - TEGU_PERF_FRAME early_wait/early_forced. */
static U64 tegu_early_throttled,tegu_early_forced;
/* TEGU_PERF_ASYNC (per perf window): commands submitted asynchronously
 * (bridge WAIT_SEQ mode) or job by job, deferred to the finish FIFO, reported
 * scheduled at submission (early), not deferred because shadows are read
 * back, mid-command reaps before CPU work, the bridge's async state, and the
 * commit -> scheduled-handler latency (early: CA's thread is released at
 * submission; otherwise at GPU completion). */
static U64 tegu_async_cmds,tegu_sync_cmds,tegu_deferred_cmds,tegu_early_cmds,tegu_nodefer_shadow,tegu_mid_reaps;
/* B2: mid-command CPU work by cause (blit, index re-expansion, preclear,
 * event) and how many of those / of the shadow refreshes had to wait for a
 * GPU job touching their buffers (the rest proceeded at once). */
static U64 tegu_mid_cause[4],tegu_mid_waits,tegu_shadow_waits,tegu_shadow_checks,tegu_early_commits;
static U64 tegu_sched_us,tegu_sched_n,tegu_sched_max_us;static unsigned tegu_async_state;
static void tegu_sched_note(double committed,double now) {
 if(committed<=0||now<committed)return;
 U64 us=(U64)((now-committed)*1e6);
 __atomic_fetch_add(&tegu_sched_us,us,__ATOMIC_RELAXED);__atomic_fetch_add(&tegu_sched_n,1,__ATOMIC_RELAXED);
 U64 old=__atomic_load_n(&tegu_sched_max_us,__ATOMIC_RELAXED);
 while(us>old&&!__atomic_compare_exchange_n(&tegu_sched_max_us,&old,us,0,__ATOMIC_RELAXED,__ATOMIC_RELAXED)){}
}
/* TEGU_PERF_KCALL_SITE: which frontend path a client buffer's kernel memory
 * calls belong to (TeguBuffer.kcallSite, set at allocation; 0 = API buffer). */
enum {TEGU_KSITE_BUFFER,TEGU_KSITE_TEXTURE,TEGU_KSITE_EXPAND,TEGU_KSITE_STAGING,TEGU_KSITE_MRTSNAP,TEGU_KSITE_HEAP,TEGU_KSITE_NOCOPY,TEGU_KSITE_N};
static __thread unsigned char tegu_kcall_fsite;
#define TEGU_KSITE_ALLOC(site,expr) ({unsigned char ksaved_=tegu_kcall_fsite;tegu_kcall_fsite=(unsigned char)(site);\
 __typeof__(expr) kr_=(expr);tegu_kcall_fsite=ksaved_;kr_;})
#ifdef NORD2_JM_TRANSPORT
#include "nord2_transport.inc"
/* Asynchronous submission (kernel 0090 WAIT_SEQ): the bridge returns from a
 * draw/batch/clear right after SUBMIT_JM and reaps with one WAIT_SEQ. Enabled
 * at device open unless NORD2_METAL_ASYNC=0 (or TEGU_ASYNC_DEFAULT=0 at
 * build); the bridge probes WAIT_SEQ once and stays synchronous on kernels
 * without it (P1/P2). teguExecute reaps before every CPU access to memory a
 * queued job may write and once at the end of every command buffer. */
#ifndef TEGU_ASYNC_DEFAULT
#define TEGU_ASYNC_DEFAULT 1
#endif
#ifndef TEGU_ASYNC_PIPELINE_DEFAULT
#define TEGU_ASYNC_PIPELINE_DEFAULT 0
#endif
static void tegu_async_configure(uintptr_t connection) {
 extern char *getenv(const char *);
 const char *env=getenv("NORD2_METAL_ASYNC");
 int on=env&&(env[0]=='0'||env[0]=='1')&&!env[1]?env[0]=='1':TEGU_ASYNC_DEFAULT;
 (void)n2mb_async(connection,on?1u:0u);
}
/* Reap every submitted job; on a GPU failure name the job of this command
 * whose kernel id is the first failing one (ids are per-device monotonic and
 * a batch shares its job's id). */
static int tegu_async_reap(uintptr_t connection,const U64 *ids,unsigned count,unsigned *failedIndex) {
 U64 failed=0;int e=n2mb_wait_pending(connection,&failed);
 if(e&&failed&&ids)for(unsigned i=0;i<count;i++)if(ids[i]&&ids[i]>=failed){*failedIndex=i;break;}
 return e;
}
#endif
extern long write(int, const void *, NSUInteger);
#ifndef TEGU_FRONTEND_HOST
extern int usleep(unsigned usec);
extern int access(const char *path, int mode);
extern const char *getprogname(void);
#endif
#ifdef __APPLE__
extern uint64_t mach_absolute_time(void);
struct tegu_timebase { uint32_t numer, denom; };
extern int mach_timebase_info(struct tegu_timebase *);
#endif
#if defined(TEGU_FRONTEND_HOST)&&!defined(__APPLE__)
#include <time.h>
static int tegu_host_clock; /* host tests that check TEGU_PERF timing/windows */
#endif
/* Seconds on the mach_absolute_time base CACurrentMediaTime uses. */
static double tegu_host_seconds(void) {
#ifdef __APPLE__
 static double scale;
 if(!scale){struct tegu_timebase tb={0,0};if(mach_timebase_info(&tb)||!tb.denom)return 0;scale=(double)tb.numer/(double)tb.denom/1e9;}
 return (double)mach_absolute_time()*scale;
#else
#ifdef TEGU_FRONTEND_HOST
 if(tegu_host_clock){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec+(double)t.tv_nsec*1e-9;}
#endif
 return 0;
#endif
}
extern int snprintf(char *, NSUInteger, const char *, ...);
#include "ab_schedule.inc"
#include "xr_decode_table.h"
#include "mip_codec.h"
typedef int (*TeguScalarFunction)(TeguConnection,unsigned,const U64 *,unsigned,U64 *,unsigned *);

/* The Nord2 service is registered before its PMIC owner has finished powering
 * the GPU.  backboardd opens the service during that window.  Bound the wait
 * instead of treating the first NOTREADY as a permanent device failure. */
#define TEGU_CAP_NOTREADY ((int)0xe00002d8u)
#define TEGU_CAP_RETRY_ATTEMPTS 250u /* 250 * 20 ms ~= 5 s */
#define TEGU_CAP_RETRY_US 20000u

static void tegu_capability_retry_wait(void) {
#ifdef TEGU_FRONTEND_HOST
 /* Host retry tests count attempts without sleeping; production uses the
  * platform delay below. */
#else
 (void)usleep(TEGU_CAP_RETRY_US);
#endif
}

static int tegu_capabilities_with_retry(TeguConnection connection,
                                        TeguScalarFunction scalar_fn,
                                        U64 info[4], unsigned *attempts) {
 if(!scalar_fn||!info)return -1;
 for(unsigned attempt=1;attempt<=TEGU_CAP_RETRY_ATTEMPTS;attempt++) {
  U64 values[4]={0,0,0,0};unsigned n=4;
  int result=scalar_fn(connection,0,0,0,values,&n);
  if(!result&&n==4&&values[0]==1&&(values[1]&8u)) {
   for(unsigned i=0;i<4;i++)info[i]=values[i];
   if(attempts)*attempts=attempt;
   return 0;
  }
  if(result&&(uint32_t)result!=(uint32_t)TEGU_CAP_NOTREADY) {
   if(attempts)*attempts=attempt;
   return -1;
  }
  if(!result) { /* malformed success: never spin on an unusable reply */
   if(attempts)*attempts=attempt;
   return -1;
  }
  if(attempts)*attempts=attempt;
  if(attempt<TEGU_CAP_RETRY_ATTEMPTS)tegu_capability_retry_wait();
 }
 return -1;
}

/* The CoreAnimation render server must not receive a Nord2 Metal device by
 * default.  Only backboardd is gated, and only an explicit root-created file
 * opts it in. */
#define TEGU_COMPOSITOR_OPT_IN "/private/var/db/nord2-metal/compositor-enable"

#ifdef TEGU_FRONTEND_HOST
static int tegu_host_process_backboardd;
static int tegu_host_compositor_file;
static unsigned tegu_host_compositor_gate_emitted;
#endif
#ifdef TEGU_FRONTEND_HOST
static int tegu_host_init_test_mode;
static TeguScalarFunction tegu_host_init_test_scalar;
static TeguConnection tegu_host_init_test_connection;
#endif

static int tegu_process_is_backboardd(void) {
#ifdef TEGU_FRONTEND_HOST
 return tegu_host_process_backboardd?1:0;
#else
 const char *name=getprogname();
 return name&&!__builtin_strcmp(name,"backboardd");
#endif
}

static int tegu_compositor_opt_in_exists(void) {
#ifdef TEGU_COMPOSITOR_DEFAULT_ON
 /* Explicit experiment build: allow backboardd without a rootfs flag file. */
 return 1;
#elif defined(TEGU_FRONTEND_HOST)
 return tegu_host_compositor_file?1:0;
#else
 return access(TEGU_COMPOSITOR_OPT_IN,0)==0;
#endif
}

/* Shared by render's DELTA8 backboardd gating; one process-name policy. */
int tegu_compositor_process_is_backboardd(void) {
 return tegu_process_is_backboardd();
}

static int tegu_compositor_gate_decision(int process_is_backboardd,int opt_in_exists) {
 return !process_is_backboardd||opt_in_exists;
}

static void tegu_log_compositor_gate(int enabled) {
#ifdef TEGU_FRONTEND_HOST
 unsigned previous=tegu_host_compositor_gate_emitted++;
#else
 static unsigned emitted;
 unsigned previous=__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED);
#endif
 if(previous)return;
 char line[64];int n=snprintf(line,sizeof(line),"TEGU_COMPOSITOR_GATE enabled=%d\n",enabled?1:0);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
#ifdef TEGU_FRONTEND_HOST
static __attribute__((unused)) void tegu_host_compositor_gate_reset(void) { tegu_host_compositor_gate_emitted=0; }
#endif
#ifdef TEGU_RENDER_TRACE
static void tegu_command_capacity_marker(const char *encoder,unsigned count) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
 char line[128];int n=snprintf(line,sizeof(line),
  "TEGU_COMMAND_CAPACITY encoder=%s count=%u limit=%u\n",encoder,count,TEGU_COMMAND_MAX_JOBS);
 if(n>0&&(NSUInteger)n<sizeof(line))write(2,line,(NSUInteger)n);
}
#else
#define tegu_command_capacity_marker(encoder,count) ((void)0)
#endif
#define MARK(s) ((void)write(2,"TEGU_FRONTEND " s "\n",sizeof("TEGU_FRONTEND " s "\n")-1))
#ifdef TEGU_GRAPHICS_BRIDGE
static void tegu_attachment_clear_selector18_marker(const char *phase,int status,unsigned outputs,U64 handle) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
 char line[144];int n=snprintf(line,sizeof(line),"TEGU_FRONTEND TAC1_SELECTOR18 %s status=0x%08x outputs=%u handle=0x%llx\n",
  phase,(uint32_t)status,outputs,handle);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
#endif
#if defined(TEGU_GRAPHICS_BRIDGE) && defined(TEGU_RENDER_TRACE)
static void tegu_device_query_marker(const char *query,U64 argument,int result) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=32)return;
 char line[128];int n=snprintf(line,sizeof(line),
  "TEGU_DEVICE_QUERY query=%s argument=%llu result=%d\n",query,argument,result);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
#else
#define tegu_device_query_marker(query,argument,result) ((void)0)
#endif
/* Capability-query trace requested by the compositor/QG4 work.  This is
 * always on (not gated by TEGU_RENDER_TRACE), but is strictly bounded to the
 * first 64 queries in each process so a warmup loop cannot flood stderr. */
#ifdef TEGU_FRONTEND_HOST
static unsigned tegu_host_devq_emitted;
#define TEGU_DEVQ_EMITTED tegu_host_devq_emitted
#else
static unsigned tegu_devq_emitted;
#define TEGU_DEVQ_EMITTED tegu_devq_emitted
#endif
static void tegu_device_query_trace(const char *selector,U64 argument,int result) {
 if(__atomic_fetch_add(&TEGU_DEVQ_EMITTED,1,__ATOMIC_RELAXED)>=64)return;
 char line[128];int n=snprintf(line,sizeof(line),"TEGU_DEVQ sel=%s arg=%llu ret=%d\n",
  selector,(unsigned long long)argument,result);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
#ifdef TEGU_FRONTEND_HOST
static __attribute__((unused)) void tegu_host_devq_reset(void) { tegu_host_devq_emitted=0; }
#endif

/* Map deprecated MTLFeatureSet iOS values to the Apple1/Apple2 compatibility
 * tier returned by supportsFamily.  Feature sets are family+version pairs:
 * iOS GPU Family1 and Family2 (all versions) correspond to the two Apple
 * tiers this frontend claims; Family3/4/5 and non-iOS families remain false
 * until their requirements are implemented.  The values are the fixed
 * MTLFeatureSet enum values from the iOS 16.5 SDK. */
static signed char tegu_supports_ios_feature_set(uint64_t feature_set,
                                                  int render_backend_built) {
 if(!render_backend_built)return 0;
 switch(feature_set) {
  /* iOS_GPUFamily1_v1,v2,v3,v4,v5 = 0,2,5,8,12 */
  case 0:case 2:case 5:case 8:case 12:
  /* iOS_GPUFamily2_v1,v2,v3,v4,v5 = 1,3,6,9,13 */
  case 1:case 3:case 6:case 9:case 13:return 1;
  /* iOS_GPUFamily3_v1..v4, Family4_v1/v2, Family5_v1 and all macOS/tvOS
   * feature sets are outside the claimed Apple1/Apple2 compatibility. */
  default:return 0;
 }
}
#ifdef TEGU_DEVICE_CREATE_DIAG
#define DEVICE_MARK(s) MARK("DEVICE_CREATE " s)
#else
#define DEVICE_MARK(s) ((void)0)
#endif
__attribute__((objc_root_class))
@interface NSObject {
 void *_isa; /* NSObject's object header is part of the superclass ABI. */
#ifdef TEGU_FRONTEND_HOST
 unsigned _references;
#endif
}
+ (id)alloc;
- (id)init;
- (id)retain;
- (oneway void)release;
- (id)autorelease;
- (void)dealloc;
@end
@interface NSObject (TeguHeapChargeDeclaration)
- (void)teguReleaseHeapCharge:(NSUInteger)bytes;
@end
@interface NSObject (TeguCopyDeclaration)
- (id)copy;
@end
@interface NSObject (TeguDrawableDeclaration)
- (void)present;
- (void)presentAtTime:(double)time;
- (void)presentAfterMinimumDuration:(double)duration;
@end
struct TeguPresentation { struct TeguPresentation *next; id drawable; double time; unsigned mode; };
@interface NSError : NSObject
+ (id)errorWithDomain:(id)domain code:(long)code userInfo:(id)info;
@end
@interface NSDictionary : NSObject
+ (id)dictionaryWithObject:(id)object forKey:(id)key;
- (id)objectForKey:(id)key;
@end
#ifdef TEGU_AIR_BRIDGE
#include "air_bridge.h"
@interface NSString : NSObject
+ (id)stringWithUTF8String:(const char *)bytes;
- (const char *)UTF8String;
- (signed char)isEqualToString:(id)other;
@end
@interface NSMutableArray : NSObject
+ (id)array;
- (void)addObject:(id)object;
@end
@interface NSData : NSObject {
#ifdef TEGU_FRONTEND_HOST
@public void *testBytes;NSUInteger testLength;
#endif
}
+ (id)dataWithContentsOfFile:(id)path;
- (const void *)bytes;
- (NSUInteger)length;
@end
@class TeguLibrary, TeguFunction;
#endif
@class TeguBuffer, TeguPipeline, TeguQueue, TeguCommand, TeguEncoder;
#ifdef TEGU_METAL_BASE
/* Opaque superclass layout: Apple's non-fragile ObjC ABI relocates subclass
 * ivars to its runtime size. Probe checks that relocation before allocation. */
@interface _MTLDevice : NSObject
- (id)initWithAcceleratorPort:(unsigned)port;
- (void)initLimits;
- (void)initFeatureQueries;
#ifdef TEGU_METAL_FEATURE_INIT
- (const struct tegu_device_limits *)limits;
#endif
- (signed char)supportsFamily:(long long)family;
@end
#define TEGU_DEVICE_BASE _MTLDevice
#else
#define TEGU_DEVICE_BASE NSObject
#endif
@interface TeguComputeDevice : TEGU_DEVICE_BASE {
@public
 TeguConnection connection; unsigned task;
 U64 allocatedBytes,capabilities;
#ifdef TEGU_METAL_BASE
 unsigned accelerator; U64 registry;
#ifdef TEGU_METAL_FEATURE_INIT
 int limits_ready;
#endif
 int (*releaseAccelerator)(unsigned);
#endif
 int (*scalar)(TeguConnection,unsigned,const U64 *,unsigned,U64 *,unsigned *);
 int (*method)(TeguConnection,unsigned,const U64 *,unsigned,const void *,NSUInteger,U64 *,unsigned *,void *,NSUInteger *);
 int (*map)(TeguConnection,unsigned,unsigned,U64 *,U64 *,unsigned);
 int (*unmap)(TeguConnection,unsigned,unsigned,U64);
 int (*closeService)(TeguConnection);
}
- (id)name;
#ifdef TEGU_METAL_BASE
- (id)initWithAcceleratorPort:(unsigned)port;
- (id)newPipelineLibraryWithFilePath:(id)path error:(id *)error;
- (U64)registryID;
- (unsigned)acceleratorPort;
- (signed char)supportsFeatureSet:(NSUInteger)featureSet;
- (signed char)supportsFamily:(long long)family;
- (signed char)supportsTextureSampleCount:(NSUInteger)count;
#ifdef TEGU_METAL_FEATURE_INIT
- (void)initLimits;
- (U64)featureProfile;
- (NSUInteger)maxBufferLength;
- (NSUInteger)indirectArgumentBufferCapabilities;
#endif
#endif
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)options;
- (id)newBufferWithBytes:(const void *)bytes length:(NSUInteger)length options:(NSUInteger)options;
/* QuartzCore selector-gate completeness. The current driver has no BO import
 * for arbitrary CPU pages, so no-copy creation is an explicit nil refusal
 * (never a silent copy); linear textures and resource groups have no exact
 * backend representation yet. encodeCacheHint is a semantic no-op. */
- (id)newBufferWithBytesNoCopy:(void *)bytes length:(NSUInteger)length options:(NSUInteger)options
 deallocator:(void (^)(void *,NSUInteger))deallocator;
- (id)newResourceGroupFromResources:(id const *)resources count:(NSUInteger)count;
- (void)encodeCacheHint:(id)hint;
- (NSUInteger)currentAllocatedSize;
- (MTLSize)maxThreadsPerThreadgroup;
- (NSUInteger)maxThreadgroupMemoryLength;
/* Buffer-backed linear texture path is not implemented; 64 is the row/offset
 * alignment any future exact 2D alias would require. Zero for unsupported
 * formats. deviceLinearReadOnlyTextureAlignmentBytes stays 0 because no
 * arbitrary CPU-page/read-only texture import is advertised. */
- (NSUInteger)minimumLinearTextureAlignmentForPixelFormat:(NSUInteger)pixelFormat;
- (signed char)isLowPower;
- (signed char)isHeadless;
- (signed char)isRemovable;
- (signed char)hasUnifiedMemory;
- (signed char)isDepth24Stencil8PixelFormatSupported;
- (NSUInteger)recommendedMaxWorkingSetSize;
- (signed char)supportsSampleCount:(NSUInteger)count;
- (const struct MTLTargetDeviceArch *)targetDeviceInfo;
- (id)newCommandQueue;
- (id)newTeguPipelineWithPackage:(const void *)bytes length:(NSUInteger)length;
@end
#ifdef TEGU_AIR_BRIDGE
@interface TeguComputeDevice (Libraries)
- (id)newLibraryWithData:(id)data error:(id *)error;

- (id)newLibraryWithFile:(id)path error:(id *)error;
- (id)newTeguLibraryWithBytes:(const void *)bytes length:(NSUInteger)length error:(id *)error;
@end
#endif
#ifdef TEGU_MALI_BRIDGE
@interface TeguComputeDevice (RuntimePipelines)
- (id)newComputePipelineStateWithFunction:(TeguFunction *)function error:(id *)error;
- (id)newComputePipelineStateWithFunction:(TeguFunction *)function options:(NSUInteger)options reflection:(id *)reflection error:(id *)error;
- (id)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options reflection:(id *)reflection error:(id *)error;
@end
#endif
#ifdef TEGU_GRAPHICS_BRIDGE
#ifndef TEGU_MALI_BRIDGE
#error "Graphics pipelines require the AIR/Mali runtime bridge"
#endif
@interface TeguComputeDevice (RenderPipelines)
- (id)newRenderPipelineStateWithDescriptor:(id)descriptor error:(id *)error;
@end
#endif
@interface TeguComputeDevice (DepthStencil)
- (id)newDepthStencilStateWithDescriptor:(id)descriptor;
@end
@interface TeguBuffer : NSObject {
@public TeguComputeDevice *owner; id debugLabel,heapOwner; U64 handle, address, mapped, allocated; NSUInteger logical,heapCharge,bufferResourceOptions;
 /* newBufferWithBytesNoCopy (api_surface.inc): the caller's memory; the BO is its GPU-side copy. */
 unsigned char *external;NSUInteger externalLength;void *externalDeallocator;unsigned externalEpoch;
 unsigned mapLock; /* lazy CPU mapping (tegu_buffer_bytes) */
 U64 gpuVA; /* MTLBuffer.gpuAddress, fetched once from the bridge (api_argbuf.inc); 0 = not yet / unavailable */
 unsigned char kcallSite; /* TEGU_KSITE_*: the path that allocated it (kernel-call attribution) */
}
- (void *)contents;
- (NSUInteger)length;
- (id)label;
- (void)setLabel:(id)value;
- (id)device;
- (id)heap;
/* No exact buffer-backed linear-texture representation exists yet. Preserve
 * the selector with a nil refusal rather than allocating/copying a texture. */
- (id)newLinearTextureWithDescriptor:(id)descriptor offset:(NSUInteger)offset
 bytesPerRow:(NSUInteger)bytesPerRow bytesPerImage:(NSUInteger)bytesPerImage;
- (id)newTextureWithDescriptor:(id)descriptor offset:(NSUInteger)offset
 bytesPerRow:(NSUInteger)bytesPerRow;
@end
/* The driver's own CPU view of a buffer. -contents is nil for private
 * storage, as Metal requires, but private buffers are mapped like any other:
 * blits into them and index expansion from them must still reach the bytes
 * (R174: CoreAnimation's private quad buffers dropped every indexed draw). */
/* Lazy CPU mapping (kernel 0096): a BO the CPU never touches is never
 * mapped, so the kernel never cleans or invalidates it on a job. The first
 * CPU access maps it once; later callers see the published address. */
static unsigned char *tegu_buffer_map_slow(TeguBuffer *b);
static inline unsigned char *tegu_buffer_bytes(TeguBuffer *b){
 if(!b)return 0;
 U64 a=__atomic_load_n(&b->address,__ATOMIC_ACQUIRE);
 return a?(unsigned char *)(NSUInteger)a:tegu_buffer_map_slow(b);
}
#ifdef TEGU_MALI_BRIDGE
#include "mali_compute_compiler.h"
#if defined(TEGU_GRAPHICS_BRIDGE) && defined(TEGU_AIR_BRIDGE)
/* Defined in sampler_state.inc later in this translation unit; device
 * construction runs before that include and must enable the factory once. */
static void tegu_sampler_enable_exact_factory(int enabled);
#endif
struct tegu_compute_air_source;
static void tegu_compute_air_source_free(void *);
struct TeguVariant { TeguPipeline *pipeline; unsigned count; TeguAIRNativeConstant constants[64]; uint8_t variantHash[32]; };
#endif
@interface TeguPipeline : NSObject {
@public TeguComputeDevice *owner; U64 handle, allocated; struct tegu_pipeline_info info;
 unsigned inert; /* TEGU_INERT_PIPELINES: nonnil state whose dispatch is dropped */
#ifdef TEGU_MALI_BRIDGE
 TeguAIRPipeline *compiled; TeguAIRBlob spirv; unsigned limits[3], cacheLock, nextVariant;
 unsigned packageVersion; struct tegu_pipeline_info2 info2;
 struct TeguVariant variants[8];
 struct tegu_compute_air_source *airSource; /* runtime_pipeline.inc: AIR-only compute codegen */
#endif
}
- (id)device;
- (NSUInteger)maxTotalThreadsPerThreadgroup;
- (NSUInteger)threadExecutionWidth;
- (NSUInteger)staticThreadgroupMemoryLength;
@end
@interface TeguQueue : NSObject {
@public TeguComputeDevice *owner; id debugLabel;
 struct tegu_submission_queue submissions;
 void *(*groupCreate)(void);
 void (*groupEnter)(void *), (*groupLeave)(void *);
 long (*groupWait)(void *,U64);
 void *(*poolPush)(void);
 void (*poolPop)(void *);
 void *completionTarget;
 void *(*blockCopy)(const void *);
 void (*blockRelease)(const void *);
 unsigned submissionFailed;
 /* Cross-command GPU pipelining (async submission, TEGU_ASYNC_PIPELINE=1):
  * completion work of every command runs on this serial FIFO, so a command's
  * wait for its own jobs overlaps the submission of the next one while
  * completions stay in commit order. Unused (no target) when off. */
 struct tegu_submission_queue finishes;unsigned asyncPipeline;
 /* Pipelined commands reported scheduled at submission whose completion is
  * still pending (P2/P1 throttle, see teguExecute). */
 unsigned earlyInflight;
}
- (signed char)setGPUPriority:(NSUInteger)priority;
- (signed char)setBackgroundGPUPriority:(NSUInteger)priority;
- (void)setSubmissionQueue:(void *)target;
- (void)setCompletionQueue:(void *)target;
- (id)label;
- (void)setLabel:(id)value;
- (id)commandBuffer;
- (id)commandBufferWithDescriptor:(id)descriptor;
- (id)device;
@end
#ifdef TEGU_GRAPHICS_BRIDGE
struct TeguCompositorBufferFact {
 U64 handle,offset,available,bindingResidual,rangeOffset,rangeLength;
 uint32_t classMask,usageFlags;
};
#endif
struct Dispatch { TeguPipeline *pipeline; TeguBuffer *buffers[8]; unsigned wire[68];
 /* v2 compute dispatch: owned 3.2KiB wire + retained resources. v1 jobs leave
  * wire2 null and use wire unchanged. */
 unsigned char *wire2; NSUInteger wire2Bytes;
 TeguBuffer *v2Buffers[31]; id v2Textures[31],v2Samplers[16];
 /* TEGU_PERF_BATCH only: 1-based render encoder of the command that queued
  * this job (stamped at endEncoding), 0 for other encoders. */
 unsigned perfPass;
 /* Event pseudo-job (api_events.inc): 1 wait, 2 signal (+4 once run); such a job
  * carries no GPU work and is a barrier for reorder/batch passes. */
 unsigned eventKind;U64 eventValue;id eventObject;
 unsigned blit;NSUInteger sourceOffset,destinationOffset,byteCount,sourceStride,destinationStride,rows,sourceRows,sourceByteCount;
 unsigned bytesPerPixel,mipComponentBytes,mipFormat;unsigned char fillBytes[4],fillValue;
#ifdef TEGU_GRAPHICS_BRIDGE
 id renderPipeline,renderTexture;U64 renderArgs[4];
 TeguBuffer *vertexBuffers[31];struct tegu_render_vertex_request vertexRequest;
 id sampledTexture,fragmentSampler;struct tegu_render_texture_request textureRequest;
 struct tegu_render_target_request targetRequest;
 /* Compositor draw snapshot. Retain only reflected resources; selector16
  * execution additionally requires an imported PSO and kernel capability. */
 unsigned compositorSnapshot,compositorVertexBufferMask,compositorFragmentBufferMask;
 unsigned compositorTextureMask,compositorSamplerMask;
 unsigned compositorAttachmentCount;
 struct { uint32_t format,writeMask; } compositorAttachments[8];
 unsigned compositorLoadAction,compositorStoreAction;
 TeguBuffer *compositorVertexBuffers[31],*compositorFragmentBuffers[31];
 id compositorTextures[31],compositorSamplers[16];
 struct TeguCompositorBufferFact compositorVertexFacts[31],compositorFragmentFacts[31];
 struct { U64 handle,offset,span;NSUInteger stride,width,height,format; } compositorTextureFacts[31];
 TeguAIRBlob compositorPlan;
 unsigned fixedUiSnapshot;TeguAIRFixedUiR80Plan fixedUiPlan;
 unsigned fixedUiTextureSnapshot;TeguAIRFixedUiR85Plan fixedUiTexturePlan;
 struct tegu_ui2_bind_request fixedUiTextureRequest;
 /* Fixed UI Metal scissor in target pixels: x, y, width, height. */
 uint32_t fixedUiScissor[4];
 /* Bound fragment sampler filters for userspace-built fixed-UI descriptors. */
 uint32_t fixedUiSamplerLinear;
 /* Reflection-driven TGJ1 draw; resources are retained in compositor*[]. */
 unsigned nord2ClearOnly;U64 nord2CopySources[2],nord2CopyOffsets[2];
 unsigned genericDrawSnapshot;struct tegu_generic_draw_request genericDrawRequest;char genericName[40];
 /* Additive draw-parameter sidecar for n2mb_draw_v3. */
 uint32_t genericDrawFlags;int32_t genericVertexBase;uint32_t genericBaseInstance;
 float genericBlendConstant[4]; /* setBlendColor at the draw (N2R_DRAW_BLEND_CONSTANT) */
#ifdef NORD2_JM_TRANSPORT
 struct n2r_depth_stencil genericZs; /* tile-buffer depth/stencil state (N2R_DRAW_DEPTH_STENCIL) */
#endif
 /* CPU clear of the owned target before this job (framebuffer fetch reads
  * target memory, so a clearing first job is run as LOAD after the fill). */
 unsigned genericPreclear;
 /* TGJ1 target batching flags computed at execution (0180 kernels). */
 uint32_t batchFlags;
 /* Indexed draws expanded at encode are re-expanded at execution: Metal lets
  * the app write vertex/index data until commit (R142 stale vertices). */
 TeguBuffer *expandIndex,*expandSource,*expandOut;NSUInteger expandIndexOffset,expandSourceOffset,expandOutOffset;
 uint32_t expandElement,expandStride,expandCount;int64_t expandBaseVertex;
 /* Exact TAC1 draw snapshot. Targets and buffers stay retained until command
  * completion; the request itself contains only owned handles and ranges. */
 unsigned attachmentClearSnapshot;U64 attachmentClearPipelineHandle;
 id attachmentClearTargets[2];TeguBuffer *attachmentClearBuffers[2];
 struct tegu_attachment_clear_request attachmentClearRequest;
#endif
 /* compositor_submit.inc tegu_shadow_job_has: 0 not scanned, 1 no shadow
  * texture in any slot, 2 has one (jobs are immutable once committed). */
 unsigned char shadowScan;
};
#ifdef TEGU_GRAPHICS_BRIDGE
/* Re-run the encode-time index expansion with the bytes present at execution.
 * Out-of-range indices keep the encode-time copy (already validated). */
static void tegu_dispatch_reexpand(struct Dispatch *j)
{
 const unsigned char *indices=(const unsigned char *)tegu_buffer_bytes(j->expandIndex);
 const unsigned char *vertices=(const unsigned char *)tegu_buffer_bytes(j->expandSource);
 unsigned char *out=tegu_buffer_bytes(j->expandOut)?(unsigned char *)tegu_buffer_bytes(j->expandOut)+j->expandOutOffset:0;
 NSUInteger stride=j->expandStride,count=j->expandCount,element=j->expandElement;
 if(!indices||!vertices||!out||(element!=2&&element!=4)||!stride||j->expandOut->logical<j->expandOutOffset||j->expandOut->logical-j->expandOutOffset<count*stride||
    j->expandIndexOffset>j->expandIndex->logical||count>(j->expandIndex->logical-j->expandIndexOffset)/element)return;
 for(NSUInteger i=0;i<count;i++) {
  uint64_t index=element==2?((const uint16_t *)(indices+j->expandIndexOffset))[i]:((const uint32_t *)(indices+j->expandIndexOffset))[i];
  uint64_t byte=(uint64_t)j->expandSourceOffset+((uint64_t)j->expandBaseVertex+index)*stride;
  if(byte>j->expandSource->logical||stride>j->expandSource->logical-byte)return;
 }
 unsigned changed=0;
 for(NSUInteger i=0;i<count;i++) {
  uint64_t index=element==2?((const uint16_t *)(indices+j->expandIndexOffset))[i]:((const uint32_t *)(indices+j->expandIndexOffset))[i];
  changed+=memcmp(out+i*stride,vertices+j->expandSourceOffset+((uint64_t)j->expandBaseVertex+index)*stride,stride)!=0;
  memcpy(out+i*stride,vertices+j->expandSourceOffset+((uint64_t)j->expandBaseVertex+index)*stride,stride);
 }
#ifdef TEGU_RENDER_TRACE
 static unsigned traces;
 if(changed&&__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)<12) {
  char line[160];int n=snprintf(line,sizeof(line),"TEGU_REEXPAND fs=%s changed=%u/%u stride=%u\n",
   j->genericName,changed,(unsigned)count,(unsigned)stride);
  if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 }
#else
 (void)changed;
#endif
}
static int tegu_compositor_request_for_job(const struct Dispatch *,TeguComputeDevice *,
 struct tegu_compositor_request *,U64[4]);
static int tegu_fixed_ui_request_for_job(const struct Dispatch *,TeguComputeDevice *,
 struct tegu_ui1_bind_request *,U64[4]);
static int tegu_generic_job_submit(const struct Dispatch *,TeguComputeDevice *,U64 *,unsigned *);
static int tegu_generic_draw_submit(const struct Dispatch *,TeguComputeDevice *,U64 *,unsigned *);
static int tegu_generic_draw_batch_submit(struct Dispatch *const *,unsigned,unsigned,TeguComputeDevice *,U64 *,unsigned *,unsigned *,unsigned *);
static unsigned tegu_reorder_draws(struct Dispatch *const *,unsigned);
/* Opt-in one-shot GPU floor microbenchmark (gpu_floor_bench.inc). */
static void tegu_gpu_floor_bench_maybe(const struct Dispatch *,TeguComputeDevice *);
/* Shadow copies of IOSurfaces the kernel could not import (pages outside the
 * GPU window): refresh before the jobs run, write back targets afterwards. */
static U64 tegu_shadow_sync_jobs(struct Dispatch *const *,unsigned,int);
static U64 tegu_shadow_refresh_jobs(struct Dispatch *const *,unsigned,TeguComputeDevice *); /* A3 */
static int tegu_shadow_jobs_any(struct Dispatch *const *,unsigned,int);
static unsigned tegu_shadow_refresh_handles(struct Dispatch *const *,unsigned,U64 *,unsigned);
static void tegu_command_failed_job_detail(const struct Dispatch *,unsigned,char *,size_t);
#ifdef TEGU_RENDER_TRACE
static int tegu_fixed_ui_local_bind_for_job(const struct Dispatch *,
 const struct tegu_ui1_bind_request *);
static void tegu_fixed_ui_trace_ui1(const struct Dispatch *,const struct tegu_ui1_bind_request *,
 unsigned,unsigned);
static void tegu_fixed_ui_trace_ui2(const struct Dispatch *,const struct tegu_ui2_bind_request *,
 unsigned,unsigned);
#endif
#endif
#ifdef TEGU_GRAPHICS_BRIDGE
/* TEGU_PERF_BATCH: why a generic GPU job ended before the command's next job
 * (counted once per generic kernel job).  Frontend reasons first, then the
 * batch builder's TEGU_BATCH_STOP_* (TEGU_BREAK_BUILDER + stop). */
enum { TEGU_BREAK_END,TEGU_BREAK_BLIT,TEGU_BREAK_PRECLEAR,TEGU_BREAK_OTHER_JOB,TEGU_BREAK_MRT,TEGU_BREAK_TARGET,
 TEGU_BREAK_CLEAR,TEGU_BREAK_MAX_DRAWS,TEGU_BREAK_NO_IMAGES,TEGU_BREAK_NO_BATCH,TEGU_BREAK_BUILDER,
 /* JM split reasons (c6k: 'mrt' and 'rdtgt' could not be told apart):
  * msaa = single-target MSAA job, memfetch = in-place framebuffer fetch
  * (memory variant), owntex = the draw samples its own target. */
 TEGU_BREAK_MSAA=TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_REASONS,TEGU_BREAK_MEMFETCH,TEGU_BREAK_OWNTEX,
 TEGU_BREAK_REASONS};
static const char *const tegu_break_names[TEGU_BREAK_REASONS]={"end","blit","preclear","other","mrt","target",
 "clear","maxdraw","nores","nobatch","fit","invalid","rdtgt","shape","spans","geom","heap","code","blocks","plan","cmds","build",
 "msaa","memfetch","owntex"};
/* Targets of at least this many pixels count as full-screen (1125x2436 is
 * 2.74 M); smaller ones as offscreen. */
#define TEGU_PERF_FULL_PIXELS 2000000u
/* Per-command counters behind TEGU_PERF_BATCH / TEGU_PERF_BREAK.  Encoder
 * fields are filled at encode time, the rest at execution. */
struct tegu_perf_batch {
 unsigned encoders,encFull,encOff,encEmpty,encSkip,encLoad,encClear,encDontCare;
 unsigned draws,genericJobs,fullLoad,fullClear,offLoad,offClear,otherJobs,skipped,splitSame,splitNew;
 unsigned breaks[TEGU_BREAK_REASONS];
 /* TEGU_PERF_PASS (render encoders, at endEncoding): colour attachments
  * 1/2/3+, attachment load actions dontcare/load/clear, draws per pass
  * 0/1/2-4/5-16/17+, clear-only (no draw, a clear), empty (no draw, no
  * clear), MSAA passes, passes on an imported (display) target. */
 unsigned passAtt[3],attLoad[3],passDraws[5],passClearOnly,passEmpty,passMsaa,passImported;
 /* Commands rendering into an imported (display) surface: commit ->
  * GPU-complete and commit -> execution start, in microseconds (sums). */
 unsigned presentCmds,presentLatUs,presentQueueUs;
 /* Per attachment index 0/1 as Metal requested: load dontcare/load/clear,
  * store dontcare/store/other, full-size targets (TEGU_PERF_RT). */
 unsigned rtRawLoad[2][3],rtStore[2][3],rtFull[2];
 /* Bridge graphics jobs, their target tiles and fragment-job tiles (the
  * n2mb_tile_stats delta since the previous completed command). */
 unsigned tileJobs,tilesTarget,tilesFrag;
};
#endif
/* teguExecute state its completion half (teguFinishGPU/teguFinishReport)
 * needs; the completion half may run later on the queue's finish FIFO. */
struct tegu_exec_tail {
 int error,async,deferred,queued,early;unsigned perfJob,perfBatched,commandTrace;
 U64 perfShadow,asyncSeq,*asyncIds;double perfGpu,perfCpu,perfMark,perfMaxJob;const char *perfMaxName;
};
@interface TeguCommand : NSObject {
@public TeguQueue *queue; struct Dispatch *jobChunks[TEGU_COMMAND_MAX_JOBS/TEGU_JOB_CHUNK]; unsigned count, status;
 int encoding, invalid; const char *invalidFunction; unsigned invalidLine; id failure,debugLabel;
 struct TeguHandler *handlers, *lastHandler, *scheduledHandlers, *lastScheduledHandler;
 struct TeguPresentation *presentations,*lastPresentation;
 void *completionTarget; unsigned callbacksDone,scheduledCallbacksDone;
 unsigned commitLock, waitLock; NSUInteger protectionOptions; void *completion; struct tegu_submission_job submission;
 /* Host-time seconds around execution (MTLCommandBuffer GPU/kernel times). */
 double executeStart,executeEnd,createdAt,committedAt;
 /* B1: reported scheduled at commit under a 0112 surface reservation. */
 unsigned earlyCommit;U64 reservation;
#ifdef TEGU_GRAPHICS_BRIDGE
 struct tegu_perf_batch perfBatch;
#endif
 struct tegu_exec_tail tail;struct tegu_submission_job finishJob;
 /* A/B toggle word snapshotted at creation (ab_schedule.inc). */
 unsigned abWord;
 /* Indirect-draw argument records re-verified before execution (api_events.inc). */
 void *indirectVerifies;
 /* Event/indirect-dispatch pseudo-jobs encoded (api_events.inc): lets the post-loop flush skip a scan. */
 unsigned eventJobs;
}
- (id)computeCommandEncoder;
- (void)commit;
- (_Bool)commitAndWaitUntilSubmitted;
- (_Bool)commitAndWaitUntilSubmittedWithDeadline:(U64)deadline;
- (void)commitWithDeadline:(U64)deadline;
- (void)enqueue;
- (void)presentDrawable:(id)drawable;
- (void)presentDrawable:(id)drawable atTime:(double)time;
- (void)presentDrawable:(id)drawable afterMinimumDuration:(double)duration;
- (void)encodeSignalEvent:(id)event value:(uint64_t)value;
- (void)teguQueueDrawable:(id)drawable mode:(unsigned)mode time:(double)time;
- (void)teguExecute;
- (void)teguFinishGPU;
- (void)teguFinishReport;
- (void)addCompletedHandler:(TeguCompletionHandler)handler;
- (void)addScheduledHandler:(TeguCompletionHandler)handler;
- (void)waitUntilScheduled;
- (void)waitUntilCompleted;
- (NSUInteger)status;
- (id)error;
- (double)GPUStartTime;
- (double)GPUEndTime;
- (double)kernelStartTime;
- (double)kernelEndTime;
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
- (NSUInteger)protectionOptions;
- (void)setProtectionOptions:(NSUInteger)value;
- (int)teguCommitted;
@end
/* Jobs live in fixed chunks that never move (a Dispatch is ~12 KiB): encoders
 * hold a job pointer while filling it, and a command uses only the chunks it
 * needs. backboardd's busiest frames exceeded 64 jobs (R178/R179) and lost the
 * whole command. */
#define TEGU_JOB(chunks,i) ((chunks)[(i)/TEGU_JOB_CHUNK][(i)%TEGU_JOB_CHUNK])
struct TeguComputeRange {NSUInteger location,length;};
static struct Dispatch *tegu_command_job(TeguCommand *command,unsigned index) {
 struct Dispatch **chunk=&command->jobChunks[index/TEGU_JOB_CHUNK];
 if(!*chunk&&!(*chunk=calloc(TEGU_JOB_CHUNK,sizeof(struct Dispatch))))__builtin_trap();
 return &(*chunk)[index%TEGU_JOB_CHUNK];
}
/* Keep the first rejected operation: a later encoder often observes only the
 * sticky invalid bit, which otherwise hides the operation that set it. */
static void tegu_command_invalidate(TeguCommand *command,const char *function,unsigned line) {
 int expected=0;
 if(!command||!__atomic_compare_exchange_n(&command->invalid,&expected,1,0,
    __ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return;
 command->invalidFunction=function;command->invalidLine=line;
#ifdef TEGU_RENDER_TRACE
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16) {
  char message[256];int n=snprintf(message,sizeof(message),
   "TEGU_COMMAND_INVALID first=%s:%u jobs=%u status=%u encoding=%d\n",
   function,line,command->count,__atomic_load_n(&command->status,__ATOMIC_ACQUIRE),command->encoding);
  if(n>0)write(2,message,(NSUInteger)n<sizeof(message)?(NSUInteger)n:sizeof(message)-1);
 }
#endif
}
#define TEGU_INVALID(command) tegu_command_invalidate((command),__func__,__LINE__)
@interface TeguEncoder : NSObject {
 TeguCommand *command; TeguPipeline *pipeline; TeguBuffer *buffers[31]; NSUInteger offsets[31]; id textures[31]; id samplers[16];
 NSUInteger threadgroupLengths[31]; int ended;
}
- (id)initWithCommand:(TeguCommand *)command;
- (void)setComputePipelineState:(TeguPipeline *)state;
- (void)setBuffer:(TeguBuffer *)buffer offset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index;
- (void)setBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index;
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index;
#ifdef TEGU_FRONTEND_HOST
- (NSUInteger)teguHostThreadgroupMemoryLengthAtIndex:(NSUInteger)index;
#endif
- (void)setSamplerState:(id)sampler atIndex:(NSUInteger)index;
- (void)setSamplerStates:(id const *)samplers withRange:(struct TeguComputeRange)range;
- (void)dispatchThreadgroups:(MTLSize)groups threadsPerThreadgroup:(MTLSize)local;
- (void)dispatchThreads:(MTLSize)threads threadsPerThreadgroup:(MTLSize)local;
- (void)endEncoding;
@end
#ifdef TEGU_MALI_BRIDGE
static int tegu_runtime_dispatch(TeguPipeline *,TeguBuffer **,NSUInteger *,const NSUInteger *,
 MTLSize,MTLSize,struct Dispatch *);
static int tegu_runtime_dispatch_v2(TeguPipeline *,const TeguAIRComputeResource *,id *,const uint64_t *,
 const NSUInteger *,unsigned,unsigned,MTLSize,MTLSize,TeguCommand *,char *,NSUInteger);
static int tegu_compute_encoder_dispatch(TeguCommand *,TeguPipeline *,TeguBuffer **,NSUInteger *,id *,id *,
 const NSUInteger *,unsigned,MTLSize,MTLSize);
#endif
#ifndef TEGU_FRONTEND_HOST
static int tegu_queue_dispatch_init(TeguQueue *q) {
 void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
 void *objc=dlopen("/usr/lib/libobjc.A.dylib",2);
 if(!system||!objc)return 0;
 q->submissions.ops.retain=dlsym(system,"dispatch_retain");
 q->submissions.ops.release=dlsym(system,"dispatch_release");
 q->submissions.ops.schedule=dlsym(system,"dispatch_async_f");
 q->groupCreate=dlsym(system,"dispatch_group_create");
 q->groupEnter=dlsym(system,"dispatch_group_enter");
 q->groupLeave=dlsym(system,"dispatch_group_leave");
 q->groupWait=dlsym(system,"dispatch_group_wait");
 q->blockCopy=dlsym(system,"_Block_copy");
 q->blockRelease=dlsym(system,"_Block_release");
 q->poolPush=dlsym(objc,"objc_autoreleasePoolPush");
 q->poolPop=dlsym(objc,"objc_autoreleasePoolPop");
 q->finishes.ops=q->submissions.ops;
#ifdef NORD2_JM_TRANSPORT
 {/* The finish FIFO exists when pipelining is on by default or an A/B
   * schedule may turn it on; each command decides (tail.queued/deferred). */
  int on=tegu_ab_may_enable(TEGU_AB_ASYNC_PIPELINE);
  void *(*create)(const char *,void *)=dlsym(system,"dispatch_queue_create");
  void *target=on&&create&&q->submissions.ops.release?create("com.tegu.metal.finish",0):0;
  if(target){q->asyncPipeline=tegu_submission_set_target(&q->finishes,target);q->submissions.ops.release(target);}}
#endif
 return q->submissions.ops.retain&&q->submissions.ops.release&&q->submissions.ops.schedule&&
  q->groupCreate&&q->groupEnter&&q->groupLeave&&q->groupWait&&q->poolPush&&q->poolPop&&q->blockCopy&&q->blockRelease;
}
#endif
static void tegu_command_complete(void *context) {
 TeguCommand *command=context; TeguQueue *q=command->queue;
 void *pool=q->poolPush?q->poolPush():0;
 struct TeguHandler *node=command->handlers;
 command->handlers=command->lastHandler=0;
 while(node) {
  struct TeguHandler *next=node->next;
  node->block(command);q->blockRelease(node->block);free(node);node=next;
 }
 if(q->poolPop)q->poolPop(pool);
 __atomic_store_n(&command->callbacksDone,1,__ATOMIC_RELEASE);
 if(command->completion)q->groupLeave(command->completion);
 [command release];
}
static void tegu_command_scheduled(TeguCommand *command) {
 struct TeguHandler *node=command->scheduledHandlers;
 command->scheduledHandlers=command->lastScheduledHandler=0;
 while(node) {
  struct TeguHandler *next=node->next;
  node->block(command);command->queue->blockRelease(node->block);free(node);node=next;
 }
 __atomic_store_n(&command->scheduledCallbacksDone,1,__ATOMIC_RELEASE);
}
static void tegu_command_deliver(TeguCommand *command) {
 [command retain]; /* delivery owns lifetime independently of the submission */
 if(command->completionTarget)
  command->queue->submissions.ops.schedule(command->completionTarget,command,tegu_command_complete);
 else tegu_command_complete(command);
}
static void tegu_command_finish_run(struct tegu_submission_job *job) {
 TeguCommand *command=(id)job->context;
 void *pool=command->queue->poolPush?command->queue->poolPush():0;
 if(command->tail.deferred)[command teguFinishGPU];
 [command teguFinishReport];
 if(command->queue->poolPop)command->queue->poolPop(pool);
 tegu_command_deliver(command);
}
static void tegu_command_finish_dispose(struct tegu_submission_job *job) { [(id)job->context release]; }
static void tegu_command_run(struct tegu_submission_job *job) {
 TeguCommand *command=(id)job->context;
 void *pool=command->queue->poolPush?command->queue->poolPush():0;
 [command teguExecute];
 if(command->queue->poolPop)command->queue->poolPop(pool);
 if(command->tail.queued) {
  [command retain];command->finishJob.context=command;
  command->finishJob.run=tegu_command_finish_run;command->finishJob.dispose=tegu_command_finish_dispose;
  tegu_submission_enqueue(&command->queue->finishes,&command->finishJob);
  return;
 }
 tegu_command_deliver(command);
}
static void tegu_command_dispose(struct tegu_submission_job *job) { [(id)job->context release]; }
#ifdef TEGU_METAL_PROTOCOLS
/* Adopt the actual loaded donor protocols, never empty local replacements.
 * This enables experimental framework discovery; it does NOT prove that every
 * required selector is implemented (the native probe reports that separately). */
static int tegu_device_protocols_ready;
static int tegu_adopt_device_protocols(void *cls) {
 void *objc=dlopen("/usr/lib/libobjc.A.dylib",2);
 void *(*protocol)(const char *)=objc?dlsym(objc,"objc_getProtocol"):0;
 signed char (*add)(void *,void *)=objc?dlsym(objc,"class_addProtocol"):0;
 signed char (*conforms)(void *,void *)=objc?dlsym(objc,"class_conformsToProtocol"):0;
 if(!protocol||!add||!conforms)return 0;
 void *publicProtocol=protocol("MTLDevice"),*privateProtocol=protocol("MTLDeviceSPI");
 if(!publicProtocol||!privateProtocol)return 0;
 if(!conforms(cls,publicProtocol)&&!add(cls,publicProtocol))return 0;
 if(!conforms(cls,privateProtocol)&&!add(cls,privateProtocol))return 0;
 return conforms(cls,publicProtocol)&&conforms(cls,privateProtocol);
}
#endif
/* MTLResourceOptions this device honours for buffers: CPU cache mode default/write-combined,
 * storage shared/private, hazard tracking default/untracked/tracked.  Execution is strictly in
 * order and memory is one coherent pool, so the cache and hazard bits change nothing observable;
 * managed (macOS only), memoryless (textures only) and undefined bits are nil. */
static int tegu_buffer_options_ok(NSUInteger options) {
 NSUInteger cache=options&15,storage=(options>>4)&15,hazard=(options>>8)&3;
 return !(options>>10)&&cache<=1&&(storage==0||storage==2)&&hazard<=2;
}
static id tegu_device_new_nocopy(TeguComputeDevice *,void *,NSUInteger,NSUInteger,void (^)(void *,NSUInteger));
static void tegu_buffer_external_free(TeguBuffer *);
static void tegu_nocopy_sync_in(TeguCommand *);
static void tegu_nocopy_sync_out(TeguCommand *);
@implementation TeguComputeDevice
- (const struct MTLTargetDeviceArch *)targetDeviceInfo {
 static const struct MTLTargetDeviceArch info={0,0,TEGU_TARGET_CACHE_NAME};
 tegu_device_query_marker("targetDeviceInfo",0,1);
 return &info;
}
#ifdef TEGU_METAL_PROTOCOLS
+ (void)initialize {
 DEVICE_MARK("class_initialize enter");
 void *objc=dlopen("/usr/lib/libobjc.A.dylib",2);
 void *(*getClass)(const char *)=objc?dlsym(objc,"objc_getClass"):0;
 if(getClass && (void *)self==getClass("TeguComputeDevice"))
  tegu_device_protocols_ready=tegu_adopt_device_protocols((void *)self);
}
#endif
- (id)init {
#ifdef TEGU_METAL_BASE
 DEVICE_MARK("init factory entry");
 return [self initWithAcceleratorPort:0];
}
- (id)initWithAcceleratorPort:(unsigned)port {
#endif
 DEVICE_MARK("initWithAcceleratorPort enter");
#ifdef TEGU_BUNDLE_ID
 /* Identifies the loaded executable, e.g. after a USB hotfix push. */
 {static const char id[]="TEGU_BUNDLE_ID " TEGU_BUNDLE_ID "\n";write(2,id,sizeof(id)-1);
  /* Load address for symbolizing crash backtraces against the bundle. */
#ifdef __APPLE__
  struct tegu_dl_info self;if(tegu_dladdr((const void *)&tegu_bundle_base_marker,&self)&&self.fbase){char line[96];
   int n=snprintf(line,sizeof(line),"TEGU_BUNDLE_BASE %p\n",self.fbase);if(n>0)write(2,line,(size_t)n);}
#endif
 }
#endif
 DEVICE_MARK("super_init begin");
 self=[super init];
 DEVICE_MARK("super_init returned");
 if (!self) return 0;
 {int is_backboardd=tegu_process_is_backboardd();
  int opt_in=is_backboardd?tegu_compositor_opt_in_exists():0;
  int enabled=tegu_compositor_gate_decision(is_backboardd,opt_in);
  if(is_backboardd)tegu_log_compositor_gate(enabled);
  if(!enabled){DEVICE_MARK("compositor gate disabled");goto fail;}}
#ifdef TEGU_FRONTEND_HOST
 if(tegu_host_init_test_mode) {
  scalar=tegu_host_init_test_scalar;
  connection=tegu_host_init_test_connection;
  task=0;
  goto capabilities_query;
 }
#endif
#ifdef TEGU_METAL_PROTOCOLS
 DEVICE_MARK("protocol_adoption check begin");
 if(!tegu_device_protocols_ready){DEVICE_MARK("protocol_adoption refused");goto fail;}
 DEVICE_MARK("protocol_adoption ready");
#endif
#ifdef TEGU_METAL_FEATURE_INIT
 DEVICE_MARK("limits initializer begin");
 [self initLimits];
 DEVICE_MARK("limits initializer returned");
 if(!limits_ready){DEVICE_MARK("limits initializer refused");goto fail;}
#endif
 DEVICE_MARK("IOKit and libSystem dlopen begin");
 void *io=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",2);
 void *sys=dlopen("/usr/lib/libSystem.B.dylib",2);
 DEVICE_MARK("IOKit and libSystem dlopen returned");
 if (!io || !sys) goto fail;
 void *(*matching)(const char *)=dlsym(io,"IOServiceMatching");
 unsigned (*get)(unsigned,void *)=dlsym(io,"IOServiceGetMatchingService");
 int (*open)(unsigned,unsigned,unsigned,unsigned *)=dlsym(io,"IOServiceOpen");
 int (*release)(unsigned)=dlsym(io,"IOObjectRelease");
 const unsigned *taskPtr=dlsym(sys,"mach_task_self_");
#ifdef NORD2_JM_TRANSPORT
 scalar=n2mb_scalar;method=n2mb_method;map=n2mb_map;unmap=n2mb_unmap;closeService=n2mb_close;
 if(!matching||!get||!open||!release||!taskPtr)goto fail;
#else
 scalar=dlsym(io,"IOConnectCallScalarMethod"); method=dlsym(io,"IOConnectCallMethod");
 map=dlsym(io,"IOConnectMapMemory64"); unmap=dlsym(io,"IOConnectUnmapMemory64");
 closeService=dlsym(io,"IOServiceClose");
 if (!matching||!get||!open||!release||!taskPtr||!scalar||!method||!map||!unmap||!closeService) goto fail;
#endif
 task=*taskPtr;
 unsigned service=0;
#ifdef TEGU_METAL_BASE
 if(port) {
  int (*retain)(unsigned)=dlsym(io,"IOObjectRetain");
  if(!retain||retain(port))goto fail;
  service=port;
 } else
#endif
 {
  DEVICE_MARK("IOServiceMatching begin");
  #ifdef NORD2_JM_TRANSPORT
  void *dict=matching("Nord2GPU");
#else
  void *dict=matching("TeguGPU");
#endif
  DEVICE_MARK("IOServiceMatching returned");
  if(!dict)goto fail;
  DEVICE_MARK("IOServiceGetMatchingService begin");
  service=get(0,dict);
  DEVICE_MARK("IOServiceGetMatchingService returned");
  if(!service)goto fail;
 }
#ifdef TEGU_METAL_BASE
 accelerator=service;releaseAccelerator=release;
 int (*entryID)(unsigned,U64 *)=dlsym(io,"IORegistryEntryGetRegistryEntryID");
 if(!entryID||entryID(service,&registry)||!registry)goto fail;
 DEVICE_MARK("IOServiceOpen begin");
#ifdef NORD2_JM_TRANSPORT
 connection=nord2_transport_open(io,sys,service,task);
 int result=connection?0:-1;
#else
 int result=open(service,task,0,&connection);
#endif
 DEVICE_MARK("IOServiceOpen returned");
 if(result)goto fail;
#else
 DEVICE_MARK("IOServiceOpen begin");
#ifdef NORD2_JM_TRANSPORT
 connection=nord2_transport_open(io,sys,service,task);
 int result=connection?0:-1;
#else
 int result=open(service,task,0,&connection);
#endif
 DEVICE_MARK("IOServiceOpen returned");
 release(service); if (result) goto fail;
#endif
#ifdef TEGU_FRONTEND_HOST
capabilities_query: ;
#endif
 U64 info[4]={0,0,0,0};
 DEVICE_MARK("scalar method 0 capabilities begin");
 unsigned attempts=0;
 if(tegu_capabilities_with_retry(connection,scalar,info,&attempts)) goto fail;
#if defined(NORD2_JM_TRANSPORT)&&!defined(TEGU_FRONTEND_HOST)
 tegu_async_configure(connection);
#endif
 DEVICE_MARK("scalar method 0 capabilities returned");
 capabilities=info[1];
 /* Exact sampler state must exist before the application creates its first
  * sampler, not at the first v2 compute submit.  The v2 compiler is the
  * readiness gate: without it a v2 texture dispatch refuses explicitly and
  * render legacy consumers keep their guarded legacy path.  The factory is
  * part of the graphics sampler module, so non-graphics host builds omit it. */
#if defined(TEGU_GRAPHICS_BRIDGE) && defined(TEGU_AIR_BRIDGE)
 if(&tegu_mali_compile_compute_v2)tegu_sampler_enable_exact_factory(1);
#endif
 DEVICE_MARK("constructor returning device");
 return self;
fail: DEVICE_MARK("constructor failure cleanup"); [self release]; return 0;
}
#ifdef TEGU_METAL_BASE
- (id)name {
#ifdef NORD2_JM_TRANSPORT
 return @"Nord2 Mali-G77";
#else
 return @"Tegu Mali GPU";
#endif
}
- (U64)registryID { return registry; }
- (unsigned)acceleratorPort { return accelerator; }
#ifdef TEGU_METAL_FEATURE_INIT
/* Experimental registration preflight. No Apple GPU family has been qualified.
 * The donor's family initializer leaves its vector empty for this unknown
 * profile. Do not substitute profile zero: it advertises real Apple features. */
- (void)initLimits {
 DEVICE_MARK("initLimits enter");
 limits_ready=0;
 void *objc=dlopen("/usr/lib/libobjc.A.dylib",2);
 void *(*getClass)(const char *)=objc?dlsym(objc,"objc_getClass"):0;
 void *(*getIvar)(void *,const char *)=objc?dlsym(objc,"class_getInstanceVariable"):0;
 long (*offset)(void *)=objc?dlsym(objc,"ivar_getOffset"):0;
 const char *(*encoding)(void *)=objc?dlsym(objc,"ivar_getTypeEncoding"):0;
 NSUInteger (*size)(void *)=objc?dlsym(objc,"class_getInstanceSize"):0;
 if(!getClass||!getIvar||!offset||!encoding||!size)return;
 DEVICE_MARK("initLimits runtime metadata ready");
 void *base=getClass("_MTLDevice");
 void *ivar=base?getIvar(base,"_limits"):0;
 void *next=base?getIvar(base,"_supportedGPUFamilies"):0;
 if(!ivar||!next||!tegu_limits_span(encoding(ivar),offset(ivar),offset(next),size(base))) {
  MARK("FAIL framework_limits_ABI");return;
 }
 struct tegu_device_limits *destination=(void *)((char *)self+offset(ivar));
 *destination=tegu_compute_limits();
 limits_ready=1;
 DEVICE_MARK("initLimits complete");
}
- (U64)featureProfile {
 U64 value=TEGU_METAL_FEATURE_PROFILE_UNQUALIFIED;
 tegu_device_query_marker("featureProfile",0,(int)value);
 return value;
}
- (NSUInteger)maxBufferLength { return 16UL<<20; } /* transport allocation limit */
- (NSUInteger)indirectArgumentBufferCapabilities { return 0; }
#endif
#endif
- (void)dealloc {
 if(connection) closeService(connection);
#ifdef TEGU_METAL_BASE
 if(accelerator&&releaseAccelerator)releaseAccelerator(accelerator);
#endif
 [super dealloc];
}
- (NSUInteger)currentAllocatedSize { return __atomic_load_n(&allocatedBytes,__ATOMIC_RELAXED); }
- (MTLSize)maxThreadsPerThreadgroup { return (MTLSize){256,256,256}; } /* total <=256 */
- (NSUInteger)maxThreadgroupMemoryLength { return 32768; } /* WLS, including dynamic [[threadgroup(n)]] */
- (NSUInteger)minimumLinearTextureAlignmentForPixelFormat:(NSUInteger)format {
 switch(format) {
 case 1:case 10:case 25:case 30:case 40:case 42:case 55:case 65:case 70:case 71:case 80:case 81:case 90:case 92:case 94:case 110:case 115:case 125:return 64;
 default:return 0;
 }
}
- (signed char)isLowPower { return 1; }
- (signed char)isHeadless {
#ifdef TEGU_GRAPHICS_BRIDGE
 signed char headless=0;
#else
 signed char headless=1;
#endif
 tegu_device_query_marker("isHeadless",0,headless);
 return headless;
}
- (signed char)isRemovable { return 0; }
- (signed char)hasUnifiedMemory { return 1; }
- (signed char)isDepth24Stencil8PixelFormatSupported {
 tegu_device_query_trace("isDepth24Stencil8PixelFormatSupported",0,0);
 return 0;
}
- (signed char)supportsFeatureSet:(NSUInteger)featureSet {
#ifdef TEGU_GRAPHICS_BRIDGE
 signed char supported=tegu_supports_ios_feature_set(featureSet,1);
#else
 signed char supported=tegu_supports_ios_feature_set(featureSet,0);
#endif
 tegu_device_query_trace("supportsFeatureSet",featureSet,supported);
 return supported;
}
- (signed char)supportsFamily:(long long)family {
#ifdef TEGU_GRAPHICS_BRIDGE
 signed char supported=tegu_supports_gpu_family(family,1);
#else
 signed char supported=tegu_supports_gpu_family(family,0);
#endif
 tegu_device_query_marker("supportsFamily",(U64)family,supported);
 tegu_device_query_trace("supportsFamily",(U64)family,supported);
 return supported;
}
- (signed char)supportsTextureSampleCount:(NSUInteger)count {
#ifdef TEGU_GRAPHICS_BRIDGE
 signed char supported=tegu_supports_render_sample_count(count,1);
#else
 signed char supported=tegu_supports_render_sample_count(count,0);
#endif
 tegu_device_query_marker("supportsTextureSampleCount",count,supported);
 tegu_device_query_trace("supportsTextureSampleCount",count,supported);
 return supported;
}
- (NSUInteger)recommendedMaxWorkingSetSize { return 128UL<<20; } /* shared kernel allocation cap */
- (signed char)supportsSampleCount:(NSUInteger)count {
#ifdef TEGU_GRAPHICS_BRIDGE
 signed char supported=tegu_supports_render_sample_count(count,1);
#else
 signed char supported=tegu_supports_render_sample_count(count,0);
#endif
 tegu_device_query_marker("supportsSampleCount",count,supported);
 tegu_device_query_trace("supportsSampleCount",count,supported);
 return supported;
}
- (id)newBufferWithBytes:(const void *)bytes length:(NSUInteger)length options:(NSUInteger)options {
 if(!bytes||!length||((options>>4)&15)!=0)return 0; /* initial contents need CPU-visible storage */
 TeguBuffer *buffer=[self newBufferWithLength:length options:options];
 if(buffer)memcpy([buffer contents],bytes,length);
 return buffer;
}
static void tegu_frontend_selector_refused(const char *selector,const char *reason) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=32)return;
 char line[192];int n=snprintf(line,sizeof(line),"TEGU_SELECTOR_REFUSED selector=%s reason=%s\n",
  selector?selector:"?",reason?reason:"unsupported");
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
- (id)newBufferWithBytesNoCopy:(void *)bytes length:(NSUInteger)length options:(NSUInteger)options
 deallocator:(void (^)(void *,NSUInteger))deallocator {
 return tegu_device_new_nocopy(self,bytes,length,options,deallocator);
}
- (id)newResourceGroupFromResources:(id const *)resources count:(NSUInteger)count {
 (void)resources;(void)count;
 tegu_frontend_selector_refused("newResourceGroupFromResources:count:","no residency model");
 return 0;
}
- (void)encodeCacheHint:(id)hint { (void)hint; }
- (id)newBufferWithLength:(NSUInteger)length options:(NSUInteger)options {
 if (!length||!tegu_buffer_options_ok(options)) return 0;
 TeguBuffer *b=[[TeguBuffer alloc] init]; if(!b) return 0;
 b->owner=[self retain]; b->logical=length;b->bufferResourceOptions=options;b->kcallSite=tegu_kcall_fsite;
 U64 request=length,reply[2]={0}; unsigned n=2;
 if(scalar(connection,1,&request,1,reply,&n)) { [b release]; return 0; }
 b->handle=reply[0];
 if(n==2&&b->handle) {b->allocated=reply[1];__atomic_fetch_add(&allocatedBytes,b->allocated,__ATOMIC_RELAXED);}
 if(n!=2||!b->handle||reply[1]<length) { [b release]; return 0; }
 /* Not mapped here (tegu_buffer_bytes maps on first CPU access). `mapped`
  * is the span a mapping covers: the allocation, which bounds checks use. */
 b->mapped=reply[1];
 return b;
}
/* (C function inside the implementation: it reads the device's ivars.) */
static unsigned char *tegu_buffer_map_slow(TeguBuffer *b){
 TeguComputeDevice *d=b->owner;
 if(!d||!b->handle||!d->map)return 0;
 while(__atomic_exchange_n(&b->mapLock,1,__ATOMIC_ACQUIRE)){}
 U64 a=__atomic_load_n(&b->address,__ATOMIC_ACQUIRE),size=0;unsigned char fsite=tegu_kcall_fsite;tegu_kcall_fsite=b->kcallSite;
 if(!a&&!d->map(d->connection,(unsigned)b->handle,d->task,&a,&size,1)&&a&&size>=b->logical) {
  b->mapped=size;__atomic_store_n(&b->address,a,__ATOMIC_RELEASE);
 } else if(!__atomic_load_n(&b->address,__ATOMIC_ACQUIRE))a=0;
 tegu_kcall_fsite=fsite;
 __atomic_store_n(&b->mapLock,0,__ATOMIC_RELEASE);
 return (unsigned char *)(NSUInteger)a;
}

- (id)newPipelineLibraryWithFilePath:(id)path error:(id *)error {
 (void)path;
 if(error)*error=[NSError errorWithDomain:@"TeguPipelineLibrary" code:1 userInfo:0];
 return 0;
}
- (id)newCommandQueue {
 TeguQueue *q=[[TeguQueue alloc] init];if(!q)return 0;
 q->owner=[self retain];
#ifndef TEGU_FRONTEND_HOST
 if(!tegu_queue_dispatch_init(q)){[q release];return 0;}
#endif
 return q;
}
- (id)newTeguPipelineWithPackage:(const void *)bytes length:(NSUInteger)length {
 struct tegu_pipeline_info info;struct tegu_pipeline_info2 info2={0};unsigned version=0;
 if(tegu_pipeline_parse_any(bytes,length,&version,&info,&info2)) return 0;
 TeguPipeline *p=[[TeguPipeline alloc] init]; if(!p) return 0;
 p->owner=[self retain]; p->info=info;p->packageVersion=version;p->info2=info2; unsigned n=1;
 if(method(connection,5,0,0,bytes,length,&p->handle,&n,0,0)||n!=1||!p->handle) { [p release]; return 0; }
 /* Selector5 owns one native16KiB code allocation per imported pipeline. */
 p->allocated=16384;__atomic_fetch_add(&allocatedBytes,p->allocated,__ATOMIC_RELAXED);
 return p;
}
@end
#ifndef TEGU_METAL_BASE
@implementation TeguComputeDevice (TeguSelectors)
- (id)name {
#ifdef NORD2_JM_TRANSPORT
 return @"Nord2 Mali-G77";
#else
 return @"Tegu Mali GPU";
#endif
}
@end
#endif
#include "depth_stencil.m"

static id tegu_buffer_make_texture(TeguBuffer *buffer,id descriptor,NSUInteger offset,NSUInteger bytesPerRow);

@implementation TeguBuffer
- (id)newLinearTextureWithDescriptor:(id)descriptor offset:(NSUInteger)offset
 bytesPerRow:(NSUInteger)bytesPerRow bytesPerImage:(NSUInteger)bytesPerImage {
 (void)bytesPerImage; /* only meaningful for 3D/array linear layouts, which are refused */
 return tegu_buffer_make_texture(self,descriptor,offset,bytesPerRow);
}
- (id)newTextureWithDescriptor:(id)descriptor offset:(NSUInteger)offset
 bytesPerRow:(NSUInteger)bytesPerRow {
 return tegu_buffer_make_texture(self,descriptor,offset,bytesPerRow);
}
- (void *)contents { if(external)return external;return ((bufferResourceOptions>>4)&15)==2?0:(void *)tegu_buffer_bytes(self); }
- (NSUInteger)length { return logical; }
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id replacement=[value copy];[debugLabel release];debugLabel=replacement; }
- (id)device { return owner; }
- (id)heap { return heapOwner; }
- (void)dealloc {
 tegu_buffer_external_free(self);
 unsigned char fsite=tegu_kcall_fsite;tegu_kcall_fsite=kcallSite;
 if(address) owner->unmap(owner->connection,(unsigned)handle,owner->task,address);
 int physicalReleased=!handle;
 if(handle) { unsigned n=0;
  if(!owner->scalar(owner->connection,2,&handle,1,0,&n)) {
   physicalReleased=1;
   __atomic_fetch_sub(&owner->allocatedBytes,allocated,__ATOMIC_RELAXED);
  }
 }
 tegu_kcall_fsite=fsite;
 if(physicalReleased&&heapOwner&&heapCharge)[heapOwner teguReleaseHeapCharge:heapCharge];
 [heapOwner release];[debugLabel release];[owner release]; [super dealloc];
}
@end
@implementation TeguPipeline
- (id)device { return owner; }
- (NSUInteger)maxTotalThreadsPerThreadgroup {
#ifdef TEGU_MALI_BRIDGE
 if(compiled) return limits[0];
#endif
 return info.local[0]*info.local[1]*info.local[2];
}
- (NSUInteger)threadExecutionWidth {
#ifdef TEGU_MALI_BRIDGE
 if(compiled) return limits[1];
#endif
 return 32;
}
- (NSUInteger)staticThreadgroupMemoryLength {
#ifdef TEGU_MALI_BRIDGE
 if(compiled) return limits[2];
#endif
 return 0;
}
- (void)dealloc {
#ifdef TEGU_MALI_BRIDGE
 for(unsigned i=0;i<8;i++) [variants[i].pipeline release];
 tegu_air_pipeline_close(compiled);tegu_air_blob_free(&spirv);
 tegu_compute_air_source_free(airSource);airSource=0;
#endif
 if(handle) { unsigned n=0;
  if(!owner->scalar(owner->connection,6,&handle,1,0,&n))
   __atomic_fetch_sub(&owner->allocatedBytes,allocated,__ATOMIC_RELAXED);
 }
 [owner release]; [super dealloc];
}
@end
@implementation TeguQueue
/* The current kernel has one serialized submission stream and no priority
 * selector. Donor setters return BOOL and retain old state on backend failure.
 * Report unsupported honestly; never invalidate ordinary commands or redirect
 * caller-owned dispatch queues to pretend GPU priority was applied. */
- (signed char)setGPUPriority:(NSUInteger)priority {
 if(priority>5||priority==3)MARK("FAIL invalid_gpu_priority");
 else MARK("UNSUPPORTED gpu_priority");
 return 0;
}
- (signed char)setBackgroundGPUPriority:(NSUInteger)priority {
 if(priority>5)MARK("FAIL invalid_background_gpu_priority");
 else MARK("UNSUPPORTED background_gpu_priority");
 return 0;
}
- (void)setCompletionQueue:(void *)target {
 if(target&&(!submissions.ops.retain||!submissions.ops.release||!submissions.ops.schedule)) {
  MARK("FAIL completion_queue_backend");return;
 }
 if(target)submissions.ops.retain(target);
 tegu_submission_lock(&submissions);
 void *old=completionTarget;completionTarget=target;
 tegu_submission_unlock(&submissions);
 if(old)submissions.ops.release(old);
}
- (void)setSubmissionQueue:(void *)target {
 int ok=tegu_submission_set_target(&submissions,target);
 __atomic_store_n(&submissionFailed,!ok,__ATOMIC_RELEASE);
 if(!ok)MARK("FAIL submission_queue_backend");
}
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id replacement=[value copy];[debugLabel release];debugLabel=replacement; }
- (id)device { return owner; }
- (id)commandBufferWithDescriptor:(id)descriptor {
 /* Retained references/error options are not implemented; the command keeps
  * the existing completion/ownership contract and ignores the descriptor
  * safely. A nil descriptor is the ordinary commandBuffer request. */
 (void)descriptor;
 return [self commandBuffer];
}
- (id)commandBuffer {
 if(__atomic_load_n(&submissionFailed,__ATOMIC_ACQUIRE))return 0;
 TeguCommand *c=[[TeguCommand alloc] init];if(!c)return 0;
 c->queue=[self retain];c->createdAt=tegu_host_seconds();c->abWord=tegu_ab_current();
 if(groupCreate) {c->completion=groupCreate();if(!c->completion){[c release];return 0;}}
 return [c autorelease];
}
- (void)dealloc { [self setCompletionQueue:0];tegu_submission_set_target(&submissions,0);tegu_submission_set_target(&finishes,0);[debugLabel release];[owner release]; [super dealloc]; }
@end
/* Fill whole rows with a 4-byte pattern, writing the same bytes as the old
 * per-pixel loop (floor(bytes/4) patterns per row): the first row by
 * doubling, the others as row copies.  Overlapping rows keep the old loop. */
static void tegu_fill_rows(unsigned char *d,NSUInteger stride,NSUInteger bytes,NSUInteger rows,const unsigned char p[4]) {
 bytes&=~(NSUInteger)3;
 if(!d||!rows||!bytes)return;
 if(stride<bytes){for(NSUInteger y=0;y<rows;y++)for(NSUInteger x=0;x<bytes;x+=4)memcpy(d+y*stride+x,p,4);return;}
 memcpy(d,p,4);
 for(NSUInteger have=4;have<bytes;){NSUInteger n=have<bytes-have?have:bytes-have;memcpy(d+have,d,n);have+=n;}
 for(NSUInteger y=1;y<rows;y++)memcpy(d+y*stride,d,bytes);
}
/* Always-on frame-cost summary: per process, one TEGU_PERF line per ~2 s
 * of wall time aggregating every command executed in it (capped at 90
 * lines).  exec_ms close to window_ms means the submission thread is
 * saturated; gpu_ms is time inside kernel job calls (they return after
 * GPU completion), cpu_ms CPU blits/fills, shadow_kb IOSurface copies,
 * batched the draws that rode in an earlier draw's TGJ1 job (kernel jobs =
 * gpu_jobs - batched). */
static struct tegu_perf_window {unsigned lock,lines,commands,jobs,gpuJobs,chained,clean,blits,batched;
 double windowStart,exec,gpu,cpu,maxCommand,maxJob,encode;U64 shadowBytes;char maxName[40];
 double lastEnd;unsigned ab; /* A/B word of the window's commands (ab_schedule.inc) */
#ifdef TEGU_GRAPHICS_BRIDGE
 struct tegu_perf_batch batch;
#endif
} tegu_perf;
#ifdef TEGU_GRAPHICS_BRIDGE
/* The window's batching structure, printed after its TEGU_PERF line:
 *  TEGU_PERF_BATCH cmds= enc= enc_fs= enc_off= enc_empty= enc_skip= enc_load= enc_clear= enc_dc=
 *                  draws= jobs= jobs_fs=LOAD/CLEAR jobs_off=LOAD/CLEAR
 *  TEGU_PERF_BREAK other= skipped= same_enc= new_enc= <reason>=<jobs> ... (nonzero reasons only)
 * enc*: render encoders (fs: target >= TEGU_PERF_FULL_PIXELS, empty: queued
 * no job, skip: refused pass, dc: DontCare run as CLEAR).  draws: generic
 * draws submitted; jobs: generic kernel jobs (fs/off by target, LOAD/CLEAR
 * by the job's load action); other: other kernel jobs (ui1/ui2/compositor/
 * clear/legacy/compute); skipped: draws the builder refused (-96).  BREAK
 * counts each generic job once by why it ended before the next job:
 * same_enc/new_enc whether that next job came from the same encoder.  Lines
 * stay under ~200 bytes (the console cuts at ~255 with the process prefix);
 * a long BREAK record continues on another TEGU_PERF_BREAK line. */
static void tegu_perf_batch_add(struct tegu_perf_batch *sum,const struct tegu_perf_batch *b) {
 const unsigned *from=(const unsigned *)b;unsigned *to=(unsigned *)sum;
 for(size_t i=0;i<sizeof(*b)/sizeof(unsigned);i++)to[i]+=from[i];
}
/* compositor_submit.inc: the tiler heap estimate scale in percent. */
static unsigned tegu_batch_heap_scale;
static unsigned tegu_perf_batch_lines(const struct tegu_perf_batch *b,unsigned commands,char lines[][224],unsigned room) {
 unsigned count=0;
 if(room<3)return 0;
 int n=snprintf(lines[0],sizeof(lines[0]),"TEGU_PERF_BATCH cmds=%u enc=%u enc_fs=%u enc_off=%u enc_empty=%u enc_skip=%u enc_load=%u enc_clear=%u enc_dc=%u "
  "draws=%u jobs=%u jobs_fs=%u/%u jobs_off=%u/%u\n",
  commands,b->encoders,b->encFull,b->encOff,b->encEmpty,b->encSkip,b->encLoad,b->encClear,b->encDontCare,
  b->draws,b->genericJobs,b->fullLoad,b->fullClear,b->offLoad,b->offClear);
 if(n<=0||(size_t)n>=sizeof(lines[0]))return 0;
 count=1;
 n=snprintf(lines[1],sizeof(lines[0]),"TEGU_PERF_PASS att=%u/%u/%u load=%u/%u/%u draws=%u/%u/%u/%u/%u clear_only=%u empty=%u msaa=%u imported=%u "
  "present=%u present_us=%u queue_us=%u\n",
  b->passAtt[0],b->passAtt[1],b->passAtt[2],b->attLoad[0],b->attLoad[1],b->attLoad[2],
  b->passDraws[0],b->passDraws[1],b->passDraws[2],b->passDraws[3],b->passDraws[4],b->passClearOnly,b->passEmpty,b->passMsaa,b->passImported,
  b->presentCmds,b->presentCmds?b->presentLatUs/b->presentCmds:0,b->presentCmds?b->presentQueueUs/b->presentCmds:0);
 if(n>0&&(size_t)n<sizeof(lines[0]))count=2;
 n=snprintf(lines[count],sizeof(lines[0]),"TEGU_PERF_RT rt0_load=%u/%u/%u rt0_store=%u/%u/%u rt0_full=%u rt1_load=%u/%u/%u rt1_store=%u/%u/%u rt1_full=%u "
  "tiles=%u/%u/%u\n",
  b->rtRawLoad[0][0],b->rtRawLoad[0][1],b->rtRawLoad[0][2],b->rtStore[0][0],b->rtStore[0][1],b->rtStore[0][2],b->rtFull[0],
  b->rtRawLoad[1][0],b->rtRawLoad[1][1],b->rtRawLoad[1][2],b->rtStore[1][0],b->rtStore[1][1],b->rtStore[1][2],b->rtFull[1],
  b->tileJobs,b->tilesTarget,b->tilesFrag);
 if(n>0&&(size_t)n<sizeof(lines[0])&&count+1<room)count++;
 size_t at=0;char *line=lines[count];
 n=snprintf(line,sizeof(lines[0]),"TEGU_PERF_BREAK other=%u skipped=%u same_enc=%u new_enc=%u heap_scale=%u",b->otherJobs,b->skipped,b->splitSame,b->splitNew,
  __atomic_load_n(&tegu_batch_heap_scale,__ATOMIC_RELAXED));
 if(n<=0)return count;
 at=(size_t)n;
 for(unsigned r=0;r<TEGU_BREAK_REASONS;r++) {
  if(!b->breaks[r])continue;
  char item[32];int k=snprintf(item,sizeof(item)," %s=%u",tegu_break_names[r],b->breaks[r]);
  if(k<=0)continue;
  if(at+(size_t)k+2>200) {
   line[at++]='\n';line[at]=0;
   if(++count>=room)return count;
   line=lines[count];n=snprintf(line,sizeof(lines[0]),"TEGU_PERF_BREAK+");at=n>0?(size_t)n:0;
  }
  memcpy(line+at,item,(size_t)k);at+=(size_t)k;line[at]=0;
 }
 line[at++]='\n';line[at]=0;
 return count+1;
}
#endif
/* Pipeline compile time (any thread), drained into the next TEGU_PERF line. */
static U64 tegu_perf_compile_ns,tegu_perf_compiles;
/* Inside compile_ms: AIR translation and Mali compile time actually spent
 * (cache misses), and pipeline cache lookups that hit memory, the disk, the
 * bundle's pack or missed (AIR and Mali stages together). */
static U64 tegu_perf_air_ns,tegu_perf_mali_ns,tegu_perf_pc_memory,tegu_perf_pc_disk,tegu_perf_pc_pack,tegu_perf_pc_miss;
#ifdef TEGU_GRAPHICS_BRIDGE
static int tegu_render_cache_disk_line(char *,size_t);
#endif
static void __attribute__((unused)) tegu_perf_compile_note(double seconds) {
 if(seconds>0){__atomic_fetch_add(&tegu_perf_compile_ns,(U64)(seconds*1e9),__ATOMIC_RELAXED);__atomic_fetch_add(&tegu_perf_compiles,1,__ATOMIC_RELAXED);}
}
static void tegu_perf_note(double start,double end,unsigned jobs,unsigned gpuJobs,unsigned chained,unsigned clean,
 unsigned blits,unsigned batched,double gpu,double cpu,double maxJob,U64 shadowBytes,const char *maxName,double encode,
 const void *batchStats,unsigned ab) {
 if(start<=0||end<start)return;
 while(__atomic_exchange_n(&tegu_perf.lock,1u,__ATOMIC_ACQUIRE)){}
 char line[400],psoLine[160],frameLine[192],diskLine[256],asyncLine[240];int n=0,psoBytes=0,frameBytes=0,diskBytes=0,asyncBytes=0;
#ifdef TEGU_GRAPHICS_BRIDGE
 char batchLines[6][224];unsigned batchCount=0;
#else
 (void)batchStats;
#endif
 /* An A/B schedule runs for minutes: allow more windows. A change of the
  * toggle word closes the window first, so no window mixes variants. */
 unsigned cap=tegu_ab.active?600u:90u;
 if(tegu_perf.lines<cap) {
  int cut=tegu_perf.commands&&tegu_perf.ab!=ab;
  double windowEnd=cut?tegu_perf.lastEnd:end;
  if(!cut) {
  if(!tegu_perf.windowStart)tegu_perf.windowStart=start;
#ifdef TEGU_GRAPHICS_BRIDGE
   if(batchStats)tegu_perf_batch_add(&tegu_perf.batch,(const struct tegu_perf_batch *)batchStats);
#endif
   tegu_perf.commands++;tegu_perf.jobs+=jobs;tegu_perf.gpuJobs+=gpuJobs;tegu_perf.chained+=chained;tegu_perf.clean+=clean;
   tegu_perf.encode+=encode;tegu_perf.blits+=blits;tegu_perf.batched+=batched;tegu_perf.exec+=end-start;tegu_perf.gpu+=gpu;tegu_perf.cpu+=cpu;tegu_perf.shadowBytes+=shadowBytes;
   if(end-start>tegu_perf.maxCommand)tegu_perf.maxCommand=end-start;
   /* The slowest job's shader names the pass worth optimising next. */
   if(maxJob>tegu_perf.maxJob){tegu_perf.maxJob=maxJob;
    for(unsigned c=0;c<sizeof(tegu_perf.maxName)-1;c++){tegu_perf.maxName[c]=maxName?maxName[c]:0;if(!tegu_perf.maxName[c])break;}
    tegu_perf.maxName[sizeof(tegu_perf.maxName)-1]=0;}
   tegu_perf.lastEnd=end;tegu_perf.ab=ab;
  }
  if(cut||end-tegu_perf.windowStart>=2.0) {
   char abTag[64];tegu_ab_tag(tegu_perf.ab,abTag,sizeof(abTag));
   n=snprintf(line,sizeof(line),"TEGU_PERF %s window_ms=%u commands=%u jobs=%u gpu_jobs=%u chained=%u clean=%u cpu_blits=%u "
    "exec_ms=%u gpu_ms=%u cpu_ms=%u max_command_ms=%u max_job_us=%u shadow_kb=%llu batched=%u max_job_fs=%s encode_ms=%u compile_ms=%llu compiles=%llu\n",
    abTag,(unsigned)((windowEnd-tegu_perf.windowStart)*1e3),tegu_perf.commands,tegu_perf.jobs,tegu_perf.gpuJobs,tegu_perf.chained,
    tegu_perf.clean,tegu_perf.blits,(unsigned)(tegu_perf.exec*1e3),(unsigned)(tegu_perf.gpu*1e3),(unsigned)(tegu_perf.cpu*1e3),
    (unsigned)(tegu_perf.maxCommand*1e3),(unsigned)(tegu_perf.maxJob*1e6),tegu_perf.shadowBytes>>10,tegu_perf.batched,
    tegu_perf.maxName[0]?tegu_perf.maxName:"-",(unsigned)(tegu_perf.encode*1e3),
    __atomic_exchange_n(&tegu_perf_compile_ns,0,__ATOMIC_RELAXED)/1000000,__atomic_exchange_n(&tegu_perf_compiles,0,__ATOMIC_RELAXED));
   /* A short untruncated timing record. Completion rate is NOT panel FPS. */
   frameBytes=snprintf(frameLine,sizeof(frameLine),
    "TEGU_PERF_FRAME %s window_ms=%u commands=%u mean_exec_us=%u mean_gpu_us=%u kernel_jobs=%u max_exec_us=%u early_wait=%u early_forced=%u\n",
    abTag,(unsigned)((windowEnd-tegu_perf.windowStart)*1e3),tegu_perf.commands,
    (unsigned)(tegu_perf.exec*1e6/tegu_perf.commands),(unsigned)(tegu_perf.gpu*1e6/tegu_perf.commands),
    tegu_perf.gpuJobs-tegu_perf.batched,(unsigned)(tegu_perf.maxCommand*1e6),
    (unsigned)__atomic_exchange_n(&tegu_early_throttled,0,__ATOMIC_RELAXED),(unsigned)__atomic_exchange_n(&tegu_early_forced,0,__ATOMIC_RELAXED));
   {U64 sn=__atomic_exchange_n(&tegu_sched_n,0,__ATOMIC_RELAXED),su=__atomic_exchange_n(&tegu_sched_us,0,__ATOMIC_RELAXED);
    asyncBytes=snprintf(asyncLine,sizeof(asyncLine),
     "TEGU_PERF_ASYNC %s bridge_async=%u async_cmds=%llu sync_cmds=%llu deferred=%llu early=%llu nodefer_shadow=%llu mid_reaps=%llu "
     "mid=%llu/%llu/%llu/%llu mid_waits=%llu shadow_waits=%llu/%llu early_commit=%llu sched_us=%llu/%llu\n",
     abTag,__atomic_load_n(&tegu_async_state,__ATOMIC_RELAXED),
     __atomic_exchange_n(&tegu_async_cmds,0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_sync_cmds,0,__ATOMIC_RELAXED),
     __atomic_exchange_n(&tegu_deferred_cmds,0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_early_cmds,0,__ATOMIC_RELAXED),
     __atomic_exchange_n(&tegu_nodefer_shadow,0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_mid_reaps,0,__ATOMIC_RELAXED),
     __atomic_exchange_n(&tegu_mid_cause[0],0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_mid_cause[1],0,__ATOMIC_RELAXED),
     __atomic_exchange_n(&tegu_mid_cause[2],0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_mid_cause[3],0,__ATOMIC_RELAXED),
     __atomic_exchange_n(&tegu_mid_waits,0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_shadow_waits,0,__ATOMIC_RELAXED),
     __atomic_exchange_n(&tegu_shadow_checks,0,__ATOMIC_RELAXED),
#ifdef NORD2_JM_TRANSPORT
     __atomic_exchange_n(&tegu_early_commits,0,__ATOMIC_RELAXED),
#else
     0ull,
#endif
     sn?su/sn:0ull,__atomic_exchange_n(&tegu_sched_max_us,0,__ATOMIC_RELAXED));}
   /* Pipeline build split and cache outcome, a separate short record so the
    * console cannot cut it off the end of the long line above. */
   U64 pso[6]={__atomic_exchange_n(&tegu_perf_air_ns,0,__ATOMIC_RELAXED)/1000000,__atomic_exchange_n(&tegu_perf_mali_ns,0,__ATOMIC_RELAXED)/1000000,
    __atomic_exchange_n(&tegu_perf_pc_memory,0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_perf_pc_disk,0,__ATOMIC_RELAXED),
    __atomic_exchange_n(&tegu_perf_pc_pack,0,__ATOMIC_RELAXED),__atomic_exchange_n(&tegu_perf_pc_miss,0,__ATOMIC_RELAXED)};
   if(pso[2]|pso[3]|pso[4]|pso[5])
    psoBytes=snprintf(psoLine,sizeof(psoLine),"TEGU_PERF_PSO air_ms=%llu mali_ms=%llu cache_mem=%llu cache_disk=%llu cache_pack=%llu cache_miss=%llu\n",
     pso[0],pso[1],pso[2],pso[3],pso[4],pso[5]);
#ifdef TEGU_GRAPHICS_BRIDGE
   if(psoBytes>0)diskBytes=tegu_render_cache_disk_line(diskLine,sizeof(diskLine));
#endif
#ifdef TEGU_GRAPHICS_BRIDGE
   batchCount=tegu_perf_batch_lines(&tegu_perf.batch,tegu_perf.commands,batchLines,6);
#endif
   struct tegu_perf_window next={0};next.lock=1;next.lines=tegu_perf.lines+1;next.windowStart=cut?start:end;tegu_perf=next;
   if(cut) { /* this command opens the next window */
  if(!tegu_perf.windowStart)tegu_perf.windowStart=start;
#ifdef TEGU_GRAPHICS_BRIDGE
    if(batchStats)tegu_perf_batch_add(&tegu_perf.batch,(const struct tegu_perf_batch *)batchStats);
#endif
    tegu_perf.commands++;tegu_perf.jobs+=jobs;tegu_perf.gpuJobs+=gpuJobs;tegu_perf.chained+=chained;tegu_perf.clean+=clean;
    tegu_perf.encode+=encode;tegu_perf.blits+=blits;tegu_perf.batched+=batched;tegu_perf.exec+=end-start;tegu_perf.gpu+=gpu;tegu_perf.cpu+=cpu;tegu_perf.shadowBytes+=shadowBytes;
    if(end-start>tegu_perf.maxCommand)tegu_perf.maxCommand=end-start;
    /* The slowest job's shader names the pass worth optimising next. */
    if(maxJob>tegu_perf.maxJob){tegu_perf.maxJob=maxJob;
     for(unsigned c=0;c<sizeof(tegu_perf.maxName)-1;c++){tegu_perf.maxName[c]=maxName?maxName[c]:0;if(!tegu_perf.maxName[c])break;}
     tegu_perf.maxName[sizeof(tegu_perf.maxName)-1]=0;}
    tegu_perf.lastEnd=end;tegu_perf.ab=ab;
   }
  }
 }
 __atomic_store_n(&tegu_perf.lock,0u,__ATOMIC_RELEASE);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 if(frameBytes>0)write(2,frameLine,(size_t)frameBytes<sizeof(frameLine)?(size_t)frameBytes:sizeof(frameLine)-1);
 if(asyncBytes>0)write(2,asyncLine,(size_t)asyncBytes<sizeof(asyncLine)?(size_t)asyncBytes:sizeof(asyncLine)-1);
 if(psoBytes>0)write(2,psoLine,(size_t)psoBytes<sizeof(psoLine)?(size_t)psoBytes:sizeof(psoLine)-1);
 if(diskBytes>0)write(2,diskLine,(size_t)diskBytes<sizeof(diskLine)?(size_t)diskBytes:sizeof(diskLine)-1);
#ifdef TEGU_GRAPHICS_BRIDGE
 for(unsigned i=0;i<batchCount;i++){size_t k=0;while(k<sizeof(batchLines[i])&&batchLines[i][k])k++;if(k)write(2,batchLines[i],k);}
#endif
}
#ifdef TEGU_GRAPHICS_BRIDGE
/* One generic kernel job of draws first..last (or a skipped draw): its size
 * class and why it ended (TEGU_BREAK_*; another target in the same encoder
 * is the MRT RT0/RT1 interleave).  Counting only; never fails a frame. */
static void tegu_perf_batch_job(struct tegu_perf_batch *b,struct Dispatch *const *jobs,unsigned first,unsigned last,
 unsigned count,unsigned why,int skipped) {
 if(skipped){b->skipped++;return;}
 const struct Dispatch *j0=&TEGU_JOB(jobs,first),*jl=&TEGU_JOB(jobs,last);
 const struct tegu_generic_draw_request *q=&j0->genericDrawRequest;
 int full=(U64)q->width*q->height>=TEGU_PERF_FULL_PIXELS,load=q->load_action==1;
 b->genericJobs++;b->draws+=last-first+1;
 if(full){if(load)b->fullLoad++;else b->fullClear++;}else if(load)b->offLoad++;else b->offClear++;
 if(last+1>=count){b->breaks[TEGU_BREAK_END]++;return;}
 const struct Dispatch *next=&TEGU_JOB(jobs,last+1);
 int same=jl->perfPass&&next->perfPass==jl->perfPass;
 if(why==TEGU_BREAK_TARGET&&same)why=TEGU_BREAK_MRT;
 if(why>=TEGU_BREAK_REASONS)why=TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_BUILD;
 b->breaks[why]++;
 if(same)b->splitSame++;else b->splitNew++;
}
#endif
static void tegu_command_encode_event(TeguCommand *,id,U64,unsigned);
static int tegu_event_job_run(struct Dispatch *);
static void tegu_command_events_flush(TeguCommand *);
static int tegu_command_indirect_verify(TeguCommand *);
static void tegu_command_indirect_free(TeguCommand *);
@implementation TeguCommand
/* Events are TeguEvent/TeguSharedEvent objects only (api_events.inc); a foreign
 * MTLSharedEvent is never forwarded through a fake ordering primitive: the
 * encode helper refuses it explicitly and retains an NSError. */
- (void)encodeSignalEvent:(id)event value:(uint64_t)value {
 tegu_command_encode_event(self,event,value,2);
}
/* Retain until GPU work has completed; presenting at encode time could expose
 * unfinished pixels. The synchronous backend schedules after execution. */
- (void)teguQueueDrawable:(id)drawable mode:(unsigned)mode time:(double)time {
 while(__atomic_exchange_n(&commitLock,1,__ATOMIC_ACQUIRE)){}
 if(status>=2||!drawable||!(time>=0&&time<=1.7976931348623157e308)) {
  if(status<2)TEGU_INVALID(self);
  __atomic_store_n(&commitLock,0,__ATOMIC_RELEASE);return;
 }
 struct TeguPresentation *node=malloc(sizeof(*node));
 if(!node)TEGU_INVALID(self);
 else {
  *node=(struct TeguPresentation){0,[drawable retain],time,mode};
  if(lastPresentation)lastPresentation->next=node;else presentations=node;
  lastPresentation=node;
 }
 __atomic_store_n(&commitLock,0,__ATOMIC_RELEASE);
}
- (void)presentDrawable:(id)drawable { [self teguQueueDrawable:drawable mode:0 time:0]; }
- (void)presentDrawable:(id)drawable atTime:(double)time { [self teguQueueDrawable:drawable mode:1 time:time]; }
- (void)presentDrawable:(id)drawable afterMinimumDuration:(double)duration { [self teguQueueDrawable:drawable mode:2 time:duration]; }
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id replacement=[value copy];[debugLabel release];debugLabel=replacement; }
- (NSUInteger)protectionOptions { return protectionOptions; }
- (void)setProtectionOptions:(NSUInteger)value {
 if(value) { TEGU_INVALID(self); return; }
 protectionOptions=0;
}
- (int)teguCommitted { return [self status]>=2; }
- (void)enqueue {
 while(__atomic_exchange_n(&commitLock,1,__ATOMIC_ACQUIRE)){}
 if(!__atomic_load_n(&status,__ATOMIC_RELAXED))
  __atomic_store_n(&status,1,__ATOMIC_RELEASE);
 __atomic_store_n(&commitLock,0,__ATOMIC_RELEASE);
}
- (void)addCompletedHandler:(TeguCompletionHandler)handler {
 while(__atomic_exchange_n(&commitLock,1,__ATOMIC_ACQUIRE)){}
 if(status>=2) { MARK("FAIL completed_handler_after_commit"); }
 else {
  struct TeguHandler *node=handler&&queue->blockCopy&&queue->blockRelease?malloc(sizeof(*node)):0;
  if(node) {
   node->block=queue->blockCopy(handler);node->next=0;
   if(!node->block){free(node);node=0;}
  }
  if(!node){TEGU_INVALID(self);MARK("FAIL completed_handler_copy");}
  else {if(lastHandler)lastHandler->next=node;else handlers=node;lastHandler=node;}
 }
 __atomic_store_n(&commitLock,0,__ATOMIC_RELEASE);
}
- (void)addScheduledHandler:(TeguCompletionHandler)handler {
 while(__atomic_exchange_n(&commitLock,1,__ATOMIC_ACQUIRE)){}
 if(status>=2) { MARK("FAIL scheduled_handler_after_commit"); }
 else {
  struct TeguHandler *node=handler&&queue->blockCopy&&queue->blockRelease?malloc(sizeof(*node)):0;
  if(node) {
   node->block=queue->blockCopy(handler);node->next=0;
   if(!node->block){free(node);node=0;}
  }
  if(!node){TEGU_INVALID(self);MARK("FAIL scheduled_handler_copy");}
  else {if(lastScheduledHandler)lastScheduledHandler->next=node;else scheduledHandlers=node;lastScheduledHandler=node;}
 }
 __atomic_store_n(&commitLock,0,__ATOMIC_RELEASE);
}
- (id)device { return queue->owner; }
- (id)computeCommandEncoder {
 if([self teguCommitted]||encoding) { TEGU_INVALID(self); return 0; }
 encoding=1; return [[[TeguEncoder alloc] initWithCommand:self] autorelease];
}
static float tegu_mip_half_to_float(unsigned short value) {
 uint32_t sign=(uint32_t)(value&0x8000u)<<16;
 uint32_t exponent=(value>>10)&31u,mantissa=value&1023u,bits;
 if(!exponent) {
  if(!mantissa)bits=sign;
  else {
   int unbiased=-14;
   while(!(mantissa&1024u)){mantissa<<=1;unbiased--;}
   bits=sign|((uint32_t)(unbiased+127)<<23)|((mantissa&1023u)<<13);
  }
 } else if(exponent==31u)bits=sign|0x7f800000u|(mantissa<<13);
 else bits=sign|((exponent+112u)<<23)|(mantissa<<13);
 float out;memcpy(&out,&bits,sizeof(out));return out;
}
static unsigned short tegu_mip_float_to_half(float value) {
 uint32_t bits;memcpy(&bits,&value,sizeof(bits));
 uint32_t sign=(bits>>16)&0x8000u,exponent=(bits>>23)&255u,mantissa=bits&0x7fffffu;
 if(exponent==255u)return (unsigned short)(sign|(mantissa?0x7e00u:0x7c00u));
 int unbiased=(int)exponent-127;
 if(unbiased>15)return (unsigned short)(sign|0x7c00u);
 if(unbiased<-25)return (unsigned short)sign;
 if(unbiased<-14) {
  uint32_t significand=mantissa|0x800000u;
  unsigned shift=(unsigned)(-unbiased-1),result=significand>>shift;
  uint32_t remainder=significand&((1u<<shift)-1u),halfway=1u<<(shift-1);
  if(remainder>halfway||(remainder==halfway&&(result&1u)))result++;
  return (unsigned short)(sign+result);
 }
 uint32_t halfExponent=(uint32_t)(unbiased+15),result=mantissa>>13,remainder=mantissa&0x1fffu;
 if(remainder>0x1000u||(remainder==0x1000u&&(result&1u))) {
  if(++result==0x400u){result=0;if(++halfExponent>=31u)return (unsigned short)(sign|0x7c00u);}
 }
 return (unsigned short)(sign|(halfExponent<<10)|result);
}
#ifdef NORD2_JM_TRANSPORT
static void tegu_commit_early(TeguCommand *c); /* B1, after the render includes */
#endif
- (void)commit {
 committedAt=tegu_host_seconds();
 while(__atomic_exchange_n(&commitLock,1,__ATOMIC_ACQUIRE)){}
 if(__atomic_load_n(&status,__ATOMIC_RELAXED)>=2) {__atomic_store_n(&commitLock,0,__ATOMIC_RELEASE);return;}
 /* Capture routing at commit; later queue replacement affects future commands. */
 tegu_submission_lock(&queue->submissions);
 completionTarget=queue->completionTarget;
 if(completionTarget)queue->submissions.ops.retain(completionTarget);
 tegu_submission_unlock(&queue->submissions);
 if(completion)queue->groupEnter(completion);
 submission.context=[self retain];submission.run=tegu_command_run;submission.dispose=tegu_command_dispose;
 __atomic_store_n(&status,2,__ATOMIC_RELEASE);
 __atomic_store_n(&commitLock,0,__ATOMIC_RELEASE);
#ifdef NORD2_JM_TRANSPORT
 tegu_commit_early(self); /* B1: may report scheduled now (status 3) */
#endif
 tegu_submission_enqueue(&queue->submissions,&submission);
}
- (_Bool)commitAndWaitUntilSubmitted {
 [self commit];
 [self waitUntilScheduled];
 return [self status]!=5;
}
- (_Bool)commitAndWaitUntilSubmittedWithDeadline:(U64)deadline {
 (void)deadline;return [self commitAndWaitUntilSubmitted];
}
- (void)commitWithDeadline:(U64)deadline {
 (void)deadline;[self commit];
}
/* P2 (ansuz CA-METAL 8 "Pipelining"): a pipelined command (async submission,
 * completion on the finish FIFO, no CPU shadow write-back) is reported
 * scheduled as soon as all of its jobs are submitted, instead of after GPU
 * completion. backboardd then swaps a surface the GPU may still be drawing:
 * kernel 0097(c) holds such a swap (the bridge declares every render target,
 * imported surfaces included, in the job's write_mask) until the jobs that
 * write it are done, so the panel keeps the previous complete frame. Presents
 * we issue ourselves still run at completion (teguFinishReport). P1: at most
 * two such commands per queue may be unfinished; a third waits up to 100 ms
 * for the oldest (then proceeds, as ansuz's display-frame throttle).
 * NORD2_METAL_EARLY_SCHEDULED=0 reports scheduling at completion as before. */
#ifndef TEGU_EARLY_SCHEDULED_DEFAULT
#define TEGU_EARLY_SCHEDULED_DEFAULT 1
#endif
#define TEGU_EARLY_SCHEDULED_DEPTH 2u
static int tegu_early_scheduled(void) {
 static int state=-1;
 if(state<0){extern char *getenv(const char *);const char *env=getenv("NORD2_METAL_EARLY_SCHEDULED");
  state=env&&(env[0]=='0'||env[0]=='1')&&!env[1]?env[0]=='1':TEGU_EARLY_SCHEDULED_DEFAULT;}
 return state;
}
static void tegu_wait_backoff(unsigned *spins);
- (void)teguExecute {
 /* Jobs submit synchronously and return after GPU completion, so the command
  * is reported scheduled only once every job has run (below).  Callers that
  * wait for scheduling before presenting (commitAndWaitUntilSubmitted,
  * waitUntilScheduled; backboardd's swap) then never present a surface the
  * GPU is still drawing (R128 flicker; Snapintosh made the same fix). */
 int error=invalid||encoding||__atomic_load_n(&queue->submissionFailed,__ATOMIC_ACQUIRE) ? -1:0;
 if(earlyCommit==1)tegu_command_scheduled(self); /* B1: reported scheduled at commit; handlers run here */
 if(!error)error=tegu_command_indirect_verify(self); /* indirect-draw arguments must be unchanged */
 if(!error)tegu_nocopy_sync_in(self); /* no-copy buffers: caller memory -> GPU copy (api_surface.inc) */
#ifdef TEGU_RENDER_TRACE
 static unsigned commandTraces;
 unsigned commandTrace=__atomic_fetch_add(&commandTraces,1,__ATOMIC_RELAXED);
 if(commandTrace<96) {
  char line[256];int length=snprintf(line,sizeof(line),
   "TEGU_COMMAND_EXEC id=%u phase=begin jobs=%u invalid=%d encoding=%d queue_failed=%u error=%d\n",
   commandTrace,count,invalid,encoding,__atomic_load_n(&queue->submissionFailed,__ATOMIC_ACQUIRE),error);
  if(length>0&&(NSUInteger)length<sizeof(line))write(2,line,(NSUInteger)length);
 }
#endif
#ifdef TEGU_GRAPHICS_BRIDGE
 if(!error)tegu_reorder_draws(jobChunks,count);
#endif
#ifdef TEGU_GRAPHICS_BRIDGE
 /* Consecutive TGJ1 jobs on one target skip the kernel's full-target cache
  * maintenance between them; CPU blits break the chain. */
 if(!error&&(queue->owner->capabilities&TEGU_GPU_CAP_GENERIC_JOB_BATCH)) {
  for(unsigned a=0;a<count;a++) {
   TEGU_JOB(jobChunks,a).batchFlags=0;
   int tgj=TEGU_JOB(jobChunks,a).genericDrawSnapshot||TEGU_JOB(jobChunks,a).fixedUiSnapshot||TEGU_JOB(jobChunks,a).fixedUiTextureSnapshot;
   if(!tgj||!TEGU_JOB(jobChunks,a).renderArgs[1])continue;
   for(unsigned b=a+1;b<count;b++) {
    if(TEGU_JOB(jobChunks,b).blit||TEGU_JOB(jobChunks,b).genericPreclear||TEGU_JOB(jobChunks,b).eventKind)break;
    int next=TEGU_JOB(jobChunks,b).genericDrawSnapshot||TEGU_JOB(jobChunks,b).fixedUiSnapshot||TEGU_JOB(jobChunks,b).fixedUiTextureSnapshot;
    if(next&&TEGU_JOB(jobChunks,b).renderArgs[1]==TEGU_JOB(jobChunks,a).renderArgs[1]){TEGU_JOB(jobChunks,a).batchFlags|=TEGU_GENERIC_JOB_TARGET_CONTINUES;break;}
   }
   for(unsigned b=a;!TEGU_JOB(jobChunks,a).genericPreclear&&b-->0;) {
    if(TEGU_JOB(jobChunks,b).blit||TEGU_JOB(jobChunks,b).eventKind)break;
    int prev=TEGU_JOB(jobChunks,b).genericDrawSnapshot||TEGU_JOB(jobChunks,b).fixedUiSnapshot||TEGU_JOB(jobChunks,b).fixedUiTextureSnapshot;
    if(prev&&TEGU_JOB(jobChunks,b).renderArgs[1]==TEGU_JOB(jobChunks,a).renderArgs[1]){TEGU_JOB(jobChunks,a).batchFlags|=TEGU_GENERIC_JOB_TARGET_CLEAN;break;}
   }
  }
 }
#endif
 tegu_ab_exec_word=abWord;tegu_ab_exec_set=1;
#ifdef NORD2_JM_TRANSPORT
 /* A/B: the bridge's pre-frame mode follows this command's word. */
 if(tegu_ab_scheduled(abWord,TEGU_AB_PREFRAME))(void)n2mb_preframe(queue->owner->connection,(unsigned)tegu_ab_value(abWord,TEGU_AB_PREFRAME));
 if(tegu_ab_scheduled(abWord,TEGU_AB_EXTENT))(void)n2mb_extent(queue->owner->connection,(unsigned)tegu_ab_value(abWord,TEGU_AB_EXTENT));
 if(tegu_ab_scheduled(abWord,TEGU_AB_BATCH_MAX))(void)n2mb_batch_capacity(queue->owner->connection,tegu_ab_value(abWord,TEGU_AB_BATCH_MAX)?128u:N2MB_BATCH_MAX);
#endif
 executeStart=tegu_host_seconds();
 U64 perfShadow=0;double perfGpu=0,perfCpu=0,perfMaxJob=0,perfMark=executeStart;unsigned perfJob=UINT32_MAX,perfBatched=0;const char *perfMaxName=0;
#ifdef TEGU_GRAPHICS_BRIDGE
 /* Pipelining: a previous command's queued jobs may still read a shadow this
  * refresh rewrites; let them finish first. */
#ifdef NORD2_JM_TRANSPORT
 if(queue->asyncPipeline&&tegu_shadow_jobs_any(jobChunks,count,0)) {
#ifdef N2MB_HAVE_WAIT_HANDLES
  /* B2: only the jobs listing a shadow this refresh really rewrites (its
   * surface changed since the last sync); none when every seed is current. */
  U64 handles[32];unsigned nh=tegu_shadow_refresh_handles(jobChunks,count,handles,32);
  __atomic_fetch_add(&tegu_shadow_checks,1,__ATOMIC_RELAXED);
  if(nh==UINT32_MAX){__atomic_fetch_add(&tegu_shadow_waits,1,__ATOMIC_RELAXED);(void)n2mb_wait_pending(queue->owner->connection,0);}
  else if(nh){unsigned waited=0;(void)n2mb_wait_handles(queue->owner->connection,handles,nh,0,&waited);
   if(waited)__atomic_fetch_add(&tegu_shadow_waits,1,__ATOMIC_RELAXED);}
#else
  (void)n2mb_wait_pending(queue->owner->connection,0);
#endif
 }
#endif
 perfShadow+=tegu_shadow_refresh_jobs(jobChunks,count,queue->owner); /* A3: GPU decode-shadow conversion */
 perfCpu+=tegu_host_seconds()-perfMark;perfMark=tegu_host_seconds();
#endif
 /* Each job's wall time goes to the CPU (blits) or GPU (kernel call) sum. */
#ifdef TEGU_GRAPHICS_BRIDGE
#define TEGU_PERF_JOB_NAME(i) (TEGU_JOB(jobChunks,i).genericName[0]?TEGU_JOB(jobChunks,i).genericName: \
  TEGU_JOB(jobChunks,i).fixedUiSnapshot?"ui1":TEGU_JOB(jobChunks,i).fixedUiTextureSnapshot?"ui2":"other")
#else
#define TEGU_PERF_JOB_NAME(i) "compute"
#endif
#ifdef NORD2_JM_TRANSPORT
 const int async=!error&&count&&n2mb_async(queue->owner->connection,2)==1;
 U64 *asyncIds=async?calloc(count,sizeof(U64)):0;
 if(async&&!asyncIds)error=-1;
 /* Chained submission (bridge n2mb_chain_begin/end, NORD2_METAL_CHAIN; off
  * by default): qualifying draws/batches/clears of this command join one
  * kernel job and reply id 0 ("in the chain"); chain_end submits it and its
  * id completes the command. Bridges without the chain API never chain. */
#ifdef N2MB_HAVE_CHAIN
 const unsigned chain=!error&&count?n2mb_chain_begin(queue->owner->connection):0u;
#else
 const unsigned chain=0u;
#endif
#else
 const unsigned chain=0u;
#endif
#define TEGU_PERF_ACCOUNT() do{double now_=tegu_host_seconds(),spent_=now_-perfMark;perfMark=now_; \
  if(perfJob<count){if(TEGU_JOB(jobChunks,perfJob).blit)perfCpu+=spent_;else{perfGpu+=spent_;if(spent_>perfMaxJob){perfMaxJob=spent_;perfMaxName=TEGU_PERF_JOB_NAME(perfJob);}}}}while(0)
 for(unsigned i=0;!error&&i<count;i++) {
  struct Dispatch *j=&TEGU_JOB(jobChunks,i); U64 reply[2]={0}; unsigned n=2;
  TEGU_PERF_ACCOUNT();perfJob=i;
#ifdef NORD2_JM_TRANSPORT
  /* CPU work below reads or writes memory a queued GPU job may access. */
  if(async&&(j->blit||j->expandOut||j->genericPreclear||j->eventKind)) {
   __atomic_fetch_add(&tegu_mid_reaps,1,__ATOMIC_RELAXED);
   __atomic_fetch_add(&tegu_mid_cause[j->eventKind?3:j->blit?0:j->expandOut?1:2],1,__ATOMIC_RELAXED);
#ifdef N2MB_HAVE_WAIT_HANDLES
   /* B2: the CPU touches only this job's buffers; wait for the GPU jobs that
    * list one of them. An event publishes all earlier work: full reap.
    * C6s: buffers the CPU only READS (a blit source, the index and vertex
    * buffers of a re-expansion) wait only for GPU jobs that WRITE them; a
    * re-expansion writes only its own, not yet submitted range of the
    * per-encoder arena (earlier jobs read other ranges of it), so it waits
    * for nothing else (C6r device: 381 of 410 mid-command reaps). */
   U64 handles[3]={0},reads[2]={0};unsigned nh=0,nr=0;
   TeguBuffer *readOnly[2]={j->blit?j->buffers[0]:j->expandOut?j->expandIndex:0,j->expandOut&&!j->blit?j->expandSource:0};
   TeguBuffer *written=j->blit||!j->expandOut?j->buffers[1]:0;
   for(unsigned k=0;k<2;k++)if(readOnly[k]&&readOnly[k]->handle)reads[nr++]=readOnly[k]->handle;
   if(written&&written->handle)handles[nh++]=written->handle;
   if(!j->eventKind) {
    U64 failed=0;unsigned waited=0,waitedR=0;
#ifdef N2MB_HAVE_WAIT_WRITTEN
    int we=nr?n2mb_wait_handles_written(queue->owner->connection,reads,nr,&failed,&waitedR):0;
#else
    int we=nr?n2mb_wait_handles(queue->owner->connection,reads,nr,&failed,&waitedR):0;
#endif
    if(!we)we=n2mb_wait_handles(queue->owner->connection,handles,nh,&failed,&waited);waited|=waitedR;
    if(waited)__atomic_fetch_add(&tegu_mid_waits,1,__ATOMIC_RELAXED);
    if(we){error=we;if(failed)for(unsigned k=0;k<count;k++)if(asyncIds[k]&&asyncIds[k]>=failed){perfJob=k;break;}break;}
   } else
#endif
   {__atomic_fetch_add(&tegu_mid_waits,1,__ATOMIC_RELAXED);
    if((error=tegu_async_reap(queue->owner->connection,asyncIds,count,&perfJob)))break;}
  }
#endif
#ifdef TEGU_GRAPHICS_BRIDGE
  if(j->expandOut)tegu_dispatch_reexpand(j);
#endif
  /* Event wait/signal pseudo-job (api_events.inc): earlier GPU jobs were
   * reaped above, so a signal publishes completed work. */
  if(j->eventKind) {
   error=tegu_event_job_run(j);
   continue;
  }
  /* Shared-storage blits execute on the CPU in command order. GPU submissions
   * return only after completion/cache invalidation; subsequent submissions
   * publish CPU writes. These transfers are not GPU rendering. */
  if(j->blit) {
   unsigned char *destination=(unsigned char *)tegu_buffer_bytes(j->buffers[1])+j->destinationOffset;
   if(j->blit==5)tegu_fill_rows(destination,j->destinationStride,j->byteCount,j->rows,j->fillBytes);
   else if(j->blit==4) {
    const unsigned char *source=(unsigned char *)tegu_buffer_bytes(j->buffers[0])+j->sourceOffset;
    NSUInteger width=j->byteCount/j->bytesPerPixel,sw=j->sourceByteCount/j->bytesPerPixel;
    if(!width||!sw||!j->rows||!j->sourceRows)continue;
    /* Per-format box filter (mip_codec.h): decode, average, encode. */
    struct tegu_mip_format mipCodec;
    if(j->mipFormat&&tegu_mip_codec(j->mipFormat,&mipCodec)&&mipCodec.bytes==j->bytesPerPixel) {
     tegu_mip_downsample(&mipCodec,source,j->sourceStride,sw,j->sourceRows,destination,j->destinationStride,width,j->rows);
     continue;
    }
    for(NSUInteger y=0;y<j->rows;y++)for(NSUInteger x=0;x<width;x++) {
     NSUInteger x0=x*sw/width,x1=(x+1)*sw/width;
     NSUInteger y0=y*j->sourceRows/j->rows,y1=(y+1)*j->sourceRows/j->rows;
     unsigned samples=(unsigned)((x1-x0)*(y1-y0));
     if(j->mipComponentBytes==2) {
      for(unsigned channel=0;channel<j->bytesPerPixel/2;channel++) {
       float sum=0;
       for(NSUInteger sy=y0;sy<y1;sy++)for(NSUInteger sx=x0;sx<x1;sx++) {
        unsigned short encoded;
        memcpy(&encoded,source+sy*j->sourceStride+sx*j->bytesPerPixel+channel*2,2);
        sum+=tegu_mip_half_to_float(encoded);
       }
       unsigned short encoded=tegu_mip_float_to_half(sum/(float)samples);
       memcpy(destination+y*j->destinationStride+x*j->bytesPerPixel+channel*2,&encoded,2);
      }
     } else for(unsigned channel=0;channel<j->bytesPerPixel;channel++) {
       unsigned sum=0;
       for(NSUInteger sy=y0;sy<y1;sy++)for(NSUInteger sx=x0;sx<x1;sx++)
        sum+=source[sy*j->sourceStride+sx*j->bytesPerPixel+channel];
       destination[y*j->destinationStride+x*j->bytesPerPixel+channel]=(unsigned char)((sum+samples/2)/samples);
      }
    }
   }
   else if(j->blit==3) {
    const unsigned char *source=(unsigned char *)tegu_buffer_bytes(j->buffers[0])+j->sourceOffset;
    for(NSUInteger row=0;row<j->rows;row++)
     memcpy(destination+row*j->destinationStride,source+row*j->sourceStride,j->byteCount);
   }
   else if(j->blit==1)memcpy(destination,(unsigned char *)tegu_buffer_bytes(j->buffers[0])+j->sourceOffset,j->byteCount);
   else memset(destination,j->fillValue,j->byteCount);
   continue;
  }
  TeguComputeDevice *d=queue->owner;
#ifdef TEGU_GRAPHICS_BRIDGE
  if(j->attachmentClearSnapshot) {
   tegu_attachment_clear_selector18_marker("begin",0,2,j->attachmentClearPipelineHandle);
   error=d->method(d->connection,TEGU_ATTACHMENT_CLEAR_SELECTOR,&j->attachmentClearPipelineHandle,1,
    &j->attachmentClearRequest,sizeof(j->attachmentClearRequest),reply,&n,0,0);
   if(!error&&(n!=2||!reply[0]))error=-2;
   tegu_attachment_clear_selector18_marker(error?"failure":"success",error,n,reply[0]);
   continue;
  }
  if(j->fixedUiSnapshot) {
   struct tegu_ui1_bind_request request;U64 args[4];
   error=tegu_fixed_ui_request_for_job(j,d,&request,args);
   if(error){MARK("UNSUPPORTED fixed_ui_request");break;}
#ifdef TEGU_RENDER_TRACE
   int localBind=tegu_fixed_ui_local_bind_for_job(j,&request);
   tegu_fixed_ui_trace_ui1(j,&request,commandTrace,i);
#endif
   const unsigned selector=(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB)?TEGU_GENERIC_JOB_SELECTOR:16u;
   error=selector==16u?d->method(d->connection,16,args,4,&request,sizeof(request),reply,&n,0,0):
    tegu_generic_job_submit(j,d,reply,&n);
   if(!error&&(n!=2||!reply[0]))error=-2;
#ifdef TEGU_RENDER_TRACE
   if(commandTrace<96) {char line[256];int length=snprintf(line,sizeof(line),
    "TEGU_COMMAND_EXEC id=%u job=%u kind=ui1 selector=%u local_bind=%d result=%d replies=%u serial=%llu fence=%llu target=%llu load=%u clear=%llu\n",
    commandTrace,i,selector,localBind,error,n,(U64)reply[0],(U64)reply[1],(U64)args[1],j->compositorLoadAction,(U64)args[3]);
    if(length>0&&(NSUInteger)length<sizeof(line))write(2,line,(NSUInteger)length);}
   /* Diagnostic traced bundles keep executing after the known UI1
    * unsupported reply so the following independently validated UI2 job can
    * expose its own kernel result. Untraced bundles retain all-or-nothing
    * command-buffer failure semantics. */
   if(error==(int)0xe00002c7u) {
    if(commandTrace<96) {char line[128];int length=snprintf(line,sizeof(line),
     "TEGU_COMMAND_EXEC id=%u job=%u kind=ui1 action=diagnostic-elide\n",commandTrace,i);
     if(length>0&&(NSUInteger)length<sizeof(line))write(2,line,(NSUInteger)length);}
    error=0;
   }
#endif
   continue;
  }
#ifdef NORD2_JM_TRANSPORT
  for(unsigned rt=0;!error&&rt<2;rt++)if(j->nord2CopySources[rt])
   error=n2mb_copy(d->connection,j->nord2CopySources[rt],j->nord2CopyOffsets[rt],j->buffers[6+rt]->handle,j->buffers[6+rt]->logical);
  if(error)break;
  if(j->nord2ClearOnly){error=n2mb_clear(d->connection,&j->genericDrawRequest,reply,&n);if(asyncIds&&!error)asyncIds[i]=reply[0];continue;}
#endif
  if(j->genericDrawSnapshot) {
   if(j->genericPreclear&&j->buffers[1]) {
    unsigned char *destination=(unsigned char *)tegu_buffer_bytes(j->buffers[1])+j->destinationOffset;
    tegu_fill_rows(destination,j->destinationStride,j->byteCount,j->rows,j->fillBytes);
   }
   tegu_gpu_floor_bench_maybe(j,d);
   /* Later draws of the same pass ride in this draw's job when they fit. */
   unsigned batched=0,why=TEGU_BREAK_NO_BATCH,first=i;
   error=tegu_generic_draw_batch_submit(jobChunks,i,count,d,reply,&n,&batched,&why);
   if(!batched)error=tegu_generic_draw_submit(j,d,reply,&n);
   else{perfBatched+=batched-1;i+=batched-1;}
   /* -96: the userspace builder refused this draw (e.g. more allocations
    * than TGJ1 spans).  Skip it; the kernel never saw it. */
#ifdef NORD2_JM_TRANSPORT
   int skipped=0; /* Refusal is a command error, never a successful missing draw. */
#else
   int skipped=error==-96;if(skipped){error=0;n=2;reply[0]=reply[1]=0;}
#endif
   tegu_perf_batch_job(&perfBatch,jobChunks,first,i,count,why,skipped);
   if(!error&&!skipped&&(n!=2||(!reply[0]&&!chain)))error=-2;
#ifdef NORD2_JM_TRANSPORT
   if(asyncIds&&!error)for(unsigned k=first;k<=i;k++)asyncIds[k]=reply[0];
#endif
#ifdef TEGU_RENDER_TRACE
   if(commandTrace<96) {char line[256];int length=snprintf(line,sizeof(line),
    "TEGU_COMMAND_EXEC id=%u job=%u kind=generic result=%d skipped=%d replies=%u serial=%llu fence=%llu target=%llu load=%u bindings=%u fs=%s\n",
    commandTrace,i,error,skipped,n,(U64)reply[0],(U64)reply[1],(U64)j->renderArgs[1],j->compositorLoadAction,
    j->genericDrawRequest.binding_count,j->genericName);
    if(length>0&&(NSUInteger)length<sizeof(line))write(2,line,(NSUInteger)length);}
#endif
   continue;
  }
  if(j->fixedUiTextureSnapshot) {
   const struct tegu_ui2_bind_request *request=&j->fixedUiTextureRequest;
   const unsigned selector=(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB)?TEGU_GENERIC_JOB_SELECTOR:16u;
   if(!j->renderPipeline||(!j->renderArgs[0]&&selector==16u)||request->version!=2||
      !request->target.allocation.token||request->target.allocation.token!=request->target.allocation.object_id||
      j->renderArgs[1]!=request->target.allocation.token||j->renderArgs[2]!=request->target.allocation.offset||
      j->renderArgs[3]!=request->pass.clear_color[0]) {
    MARK("UNSUPPORTED fixed_ui_texture_request");error=-95;break;
   }
#ifdef TEGU_RENDER_TRACE
   tegu_fixed_ui_trace_ui2(j,request,commandTrace,i);
#endif
   error=selector==16u?d->method(d->connection,16,j->renderArgs,4,request,sizeof(*request),reply,&n,0,0):
    tegu_generic_job_submit(j,d,reply,&n);
   if(!error&&(n!=2||!reply[0]))error=-2;
#ifdef TEGU_RENDER_TRACE
   if(commandTrace<96) {char line[256];int length=snprintf(line,sizeof(line),
    "TEGU_COMMAND_EXEC id=%u job=%u kind=ui2 selector=%u result=%d replies=%u serial=%llu fence=%llu target=%llu load=%u clear=%llu\n",
    commandTrace,i,selector,error,n,(U64)reply[0],(U64)reply[1],(U64)j->renderArgs[1],j->compositorLoadAction,(U64)j->renderArgs[3]);
    if(length>0&&(NSUInteger)length<sizeof(line))write(2,line,(NSUInteger)length);}
#endif
   continue;
  }
  if(j->compositorSnapshot) {
   struct tegu_compositor_request request;U64 args[4];
   error=tegu_compositor_request_for_job(j,d,&request,args);
   if(error){MARK("UNSUPPORTED compositor_request");break;}
   error=d->method(d->connection,16,args,4,&request,sizeof(request),reply,&n,0,0);
   if(!error&&(n!=2||!reply[0]))error=-2;
   continue;
  }
  if(j->renderPipeline) {
   if(j->targetRequest.version)error=d->method(d->connection,15,j->renderArgs,4,
    &j->targetRequest,sizeof(j->targetRequest),reply,&n,0,0);
   else if(j->textureRequest.version)error=d->method(d->connection,12,j->renderArgs,4,
    &j->textureRequest,sizeof(j->textureRequest),reply,&n,0,0);
   else if(j->vertexRequest.version)error=d->method(d->connection,11,j->renderArgs,4,
    &j->vertexRequest,sizeof(j->vertexRequest),reply,&n,0,0);
   else error=d->scalar(d->connection,10,j->renderArgs,4,reply,&n);
  }
  else
#endif
  {const void *wire=j->wire2?j->wire2:(const void *)j->wire;NSUInteger wire_bytes=j->wire2?j->wire2Bytes:sizeof(j->wire);
  error=d->method(d->connection,7,&j->pipeline->handle,1,wire,wire_bytes,reply,&n,0,0);}
  if(!error&&(n!=2||!reply[0])) error=-2;
 }
 TEGU_PERF_ACCOUNT();
#undef TEGU_PERF_ACCOUNT
#ifdef NORD2_JM_TRANSPORT
 U64 chainSeq=0;
 /* Close the chain even after a failure (its frames were linked, not run);
  * a submit error of the chain fails the command. */
#ifdef N2MB_HAVE_CHAIN
 if(chain){int ce=n2mb_chain_end(queue->owner->connection,&chainSeq);if(ce&&!error)error=ce;}
#endif
#endif
 tegu_command_events_flush(self); /* signals skipped by an earlier failure */
 tail=(struct tegu_exec_tail){.error=error,.perfJob=perfJob,.perfBatched=perfBatched,.perfShadow=perfShadow,
  .perfGpu=perfGpu,.perfCpu=perfCpu,.perfMark=perfMark,.perfMaxJob=perfMaxJob,.perfMaxName=perfMaxName};
#ifdef TEGU_RENDER_TRACE
 tail.commandTrace=commandTrace;
#endif
#ifdef NORD2_JM_TRANSPORT
 tail.async=async;tail.asyncIds=asyncIds;
 for(unsigned i=count;asyncIds&&i-->0;)if(asyncIds[i]){tail.asyncSeq=asyncIds[i];break;}
 if(chainSeq>tail.asyncSeq)tail.asyncSeq=chainSeq; /* the chain's job: the command's newest */
#ifdef N2MB_HAVE_SURFACE_RESERVE
 /* B1: every job is submitted; the reservation now follows the newest one
  * (a failed command cancels it: its surfaces are not swapped to a frame
  * nothing will draw - the kernel keeps them busy only for queued jobs). */
 if(earlyCommit==1&&reservation){(void)n2mb_surface_retire(queue->owner->connection,reservation,error?0:tail.asyncSeq);reservation=0;}
#endif
 /* Pipelined: completion work goes to the finish FIFO (commit order). Its
  * wait for this command's jobs runs there too, unless shadows must be read
  * back before a later command may write them (or nothing was queued). */
 /* A/B: pipelining follows the command's word; while earlier completions
  * are still queued, this one queues too (commit order across a switch). */
 {int pipeline=tegu_ab_value(abWord,TEGU_AB_ASYNC_PIPELINE);
  tail.queued=queue->asyncPipeline&&(pipeline||__atomic_load_n(&queue->finishes.active,__ATOMIC_ACQUIRE));
  tail.deferred=tail.queued&&pipeline&&async&&!error&&tail.asyncSeq&&!tegu_shadow_jobs_any(jobChunks,count,1);
  if(count){__atomic_fetch_add(async?&tegu_async_cmds:&tegu_sync_cmds,1,__ATOMIC_RELAXED);
   __atomic_store_n(&tegu_async_state,n2mb_async(queue->owner->connection,2),__ATOMIC_RELAXED);}
  if(tail.deferred)__atomic_fetch_add(&tegu_deferred_cmds,1,__ATOMIC_RELAXED);
  else if(tail.queued&&pipeline&&async&&!error&&tail.asyncSeq)__atomic_fetch_add(&tegu_nodefer_shadow,1,__ATOMIC_RELAXED);}
#endif
 tegu_ab_exec_set=0;
#ifdef NORD2_JM_TRANSPORT
 if(earlyCommit==1)tail.early=1; /* B1: throttled and counted at commit */
 else if(tail.deferred&&tegu_early_scheduled()) {
  if(earlyCommit!=2&&__atomic_load_n(&queue->earlyInflight,__ATOMIC_ACQUIRE)>=TEGU_EARLY_SCHEDULED_DEPTH) {
   /* 100 ms; a build without a clock (host default) bounds it by backoff
    * steps instead (~5000 sleeps of >= 20 us). */
   double began=tegu_host_seconds();unsigned spins=0;
   __atomic_fetch_add(&tegu_early_throttled,1,__ATOMIC_RELAXED);
   while(__atomic_load_n(&queue->earlyInflight,__ATOMIC_ACQUIRE)>=TEGU_EARLY_SCHEDULED_DEPTH) {
    if(began>0?tegu_host_seconds()-began>=0.1:spins>=5600u){__atomic_fetch_add(&tegu_early_forced,1,__ATOMIC_RELAXED);break;}
    tegu_wait_backoff(&spins);
   }
  }
  __atomic_fetch_add(&queue->earlyInflight,1,__ATOMIC_ACQ_REL);tail.early=1;
 }
#endif
 if(!tail.deferred)[self teguFinishGPU];
 if(tail.early&&earlyCommit!=1) {
  /* Every job is submitted: scheduled now; completion follows on the FIFO. */
  __atomic_store_n(&status,3,__ATOMIC_RELEASE);
  __atomic_fetch_add(&tegu_early_cmds,1,__ATOMIC_RELAXED);tegu_sched_note(committedAt,tegu_host_seconds());
  tegu_command_scheduled(self);
 }
 if(!tail.queued)[self teguFinishReport];
}
/* Completion, GPU half: every job of this command (including those before a
 * failed one) is reaped before shadows are read back; a GPU failure seen here
 * belongs to this command. Deferred (pipelined): only jobs up to this
 * command's last id, waited outside the bridge lock. */
- (void)teguFinishGPU {
 int error=tail.error;unsigned perfJob=tail.perfJob;double perfGpu=tail.perfGpu,perfMark=tail.perfMark;U64 perfShadow=tail.perfShadow;
#ifdef NORD2_JM_TRANSPORT
 U64 *asyncIds=tail.asyncIds;
 if(tail.async) {
  unsigned failedIndex=UINT32_MAX;int e;
  if(tail.deferred) {
   U64 failed=0;e=n2mb_wait_seq(queue->owner->connection,tail.asyncSeq,&failed);
   if(e&&failed)for(unsigned i=0;i<count;i++)if(asyncIds[i]&&asyncIds[i]>=failed){failedIndex=i;break;}
  } else e=tegu_async_reap(queue->owner->connection,asyncIds,count,&failedIndex);
  if(e&&!error){error=e;if(failedIndex<count)perfJob=failedIndex;}
  double now=tegu_host_seconds();perfGpu+=now-perfMark;perfMark=now;
 }
 free(asyncIds);tail.asyncIds=0;
#endif
#ifdef TEGU_GRAPHICS_BRIDGE
 if(!error)perfShadow+=tegu_shadow_sync_jobs(jobChunks,count,1);
#endif
 if(!error)tegu_nocopy_sync_out(self); /* blit writes into no-copy buffers -> caller memory */
 executeEnd=tegu_host_seconds();
 tail.error=error;tail.perfJob=perfJob;tail.perfGpu=perfGpu;tail.perfMark=perfMark;tail.perfShadow=perfShadow;
}
/* Completion, report half: perf, status, presents, scheduled handlers. */
- (void)teguFinishReport {
 int error=tail.error;unsigned perfJob=tail.perfJob,perfBatched=tail.perfBatched;U64 perfShadow=tail.perfShadow;(void)perfJob; /* used only by quiet-build failure detail */
 double perfGpu=tail.perfGpu,perfCpu=tail.perfCpu,perfMark=tail.perfMark,perfMaxJob=tail.perfMaxJob;const char *perfMaxName=tail.perfMaxName;
#ifdef TEGU_RENDER_TRACE
 unsigned commandTrace=tail.commandTrace;
#endif
 perfCpu+=executeEnd-perfMark;
 {unsigned gpuJobs=0,chained=0,clean=0,blits=0;
  for(unsigned i=0;i<count;i++){const struct Dispatch *j=&TEGU_JOB(jobChunks,i);if(j->blit){blits++;continue;}gpuJobs++;
#ifdef TEGU_GRAPHICS_BRIDGE
   chained+=(j->batchFlags&TEGU_GENERIC_JOB_TARGET_CONTINUES)!=0;clean+=(j->batchFlags&TEGU_GENERIC_JOB_TARGET_CLEAN)!=0;
   perfBatch.otherJobs+=!j->genericDrawSnapshot;
#endif
  }
#ifdef TEGU_GRAPHICS_BRIDGE
  /* Per-frame present latency: a command that drew into the display surface. */
  if(perfBatch.passImported&&committedAt>0&&executeEnd>committedAt) {
   perfBatch.presentCmds++;perfBatch.presentLatUs+=(unsigned)((executeEnd-committedAt)*1e6);
   if(executeStart>committedAt)perfBatch.presentQueueUs+=(unsigned)((executeStart-committedAt)*1e6);
  }
  {/* Process-wide deltas (one bridge per device; a racing completion only
    * moves a few jobs between adjacent windows). */
   static U64 last[3];U64 now[3];unsigned *to[3]={&perfBatch.tileJobs,&perfBatch.tilesTarget,&perfBatch.tilesFrag};
   n2mb_tile_stats(queue->owner->connection,now);
   for(unsigned i=0;i<3;i++){U64 old=__atomic_exchange_n(&last[i],now[i],__ATOMIC_RELAXED);if(now[i]>=old)*to[i]+=(unsigned)(now[i]-old);}}
  const void *batchStats=&perfBatch;
#else
  const void *batchStats=0;
#endif
  tegu_perf_note(executeStart,executeEnd,count,gpuJobs,chained,clean,blits,perfBatched,perfGpu,perfCpu,perfMaxJob,perfShadow,perfMaxName,committedAt>createdAt&&createdAt>0?committedAt-createdAt:0,batchStats,abWord);}
 if(error&&!failure) {
  /* Metal's own domain and codes, so clients that branch on them behave: 2 = Timeout (an event wait that
   * never completed), 1 = Internal (everything else: refused state, kernel failure). */
  id info=0;
#ifdef TEGU_AIR_BRIDGE
  char text[96];int tn=snprintf(text,sizeof(text),"Tegu command buffer failed (internal code %d)",error);
  id description=tn>0?[NSString stringWithUTF8String:text]:0;
  if(description)info=[NSDictionary dictionaryWithObject:description forKey:@"NSLocalizedDescription"];
#endif
  failure=[[NSError errorWithDomain:@"MTLCommandBufferErrorDomain" code:error==-3?2:1 userInfo:info] retain];
 }
 if(error)tegu_ab_note_failure(abWord,error);
 __atomic_store_n(&status,3,__ATOMIC_RELEASE);
 struct TeguPresentation *presentation=presentations;presentations=lastPresentation=0;
 while(presentation) {
  struct TeguPresentation *next=presentation->next;
  if(!error) {
   if(presentation->mode==1)[presentation->drawable presentAtTime:presentation->time];
   else if(presentation->mode==2)[presentation->drawable presentAfterMinimumDuration:presentation->time];
   else [presentation->drawable present];
  }
  [presentation->drawable release];free(presentation);presentation=next;
 }
 if(!tail.early){tegu_sched_note(committedAt,tegu_host_seconds());tegu_command_scheduled(self);} /* else run at submission (teguExecute) */
 __atomic_store_n(&status,error?5:4,__ATOMIC_RELEASE);
 if(tail.early)__atomic_fetch_sub(&queue->earlyInflight,1,__ATOMIC_ACQ_REL);
#ifndef TEGU_RENDER_TRACE
 /* Quiet builds report only failures, capped. */
 /* Capped per A/B phase (16 each), so the failure behind every
  * TEGU_AB_DISABLED is logged (c6k: a process-wide 16 hid phase 9's). */
 if(error) {static unsigned failures[34];unsigned phase=(abWord&TEGU_AB_SCHEDULED)?((abWord>>8)&0xffu):0u;if(phase>33)phase=33;
  if(__atomic_fetch_add(&failures[phase],1,__ATOMIC_RELAXED)<16){char line[400];char abTag[64]="";(void)tegu_ab_tag(abWord,abTag,sizeof(abTag));
  /* Name the job the transport refused (c6fc: a bare bridge BAD, jobs=3). */
  char job[240]="";
#ifdef TEGU_GRAPHICS_BRIDGE
  if(!invalid&&perfJob<count)tegu_command_failed_job_detail(&TEGU_JOB(jobChunks,perfJob),perfJob,job,sizeof(job));
#endif
  int length=snprintf(line,sizeof(line),"TEGU_COMMAND_FAILED jobs=%u error=%d first=%s:%u %s%s\n",count,error,
   invalidFunction?invalidFunction:"-",invalidLine,abTag,job);
  if(length>0&&(NSUInteger)length<sizeof(line))write(2,line,(NSUInteger)length);}}
#endif
#ifdef TEGU_RENDER_TRACE
 if(commandTrace<96||error) {char line[160];int length=snprintf(line,sizeof(line),
  "TEGU_COMMAND_EXEC id=%u phase=end jobs=%u status=%u error=%d\n",
  commandTrace,count,error?5u:4u,error);
  if(length>0&&(NSUInteger)length<sizeof(line))write(2,line,(NSUInteger)length);}
#endif
}
/* Waiters poll a status another thread publishes. A bare spin kept one core
 * busy for the whole GPU time of the command (backboardd waits for scheduling
 * before every swap) and competed with the execute thread; back off to yields
 * and then short sleeps (adds <= ~20 us to a long wait's wake-up). */
static void tegu_wait_backoff(unsigned *spins) {
 extern int sched_yield(void);extern int usleep(unsigned);
 unsigned n=++*spins;
 if(n<64)return;
 if(n<512){(void)sched_yield();return;}
 (void)usleep(20);
}
- (void)waitUntilScheduled {
 unsigned spins=0;
 while(__atomic_load_n(&status,__ATOMIC_ACQUIRE)==2)tegu_wait_backoff(&spins);
}
- (void)waitUntilCompleted {
 /* libdispatch documents concurrent waits on one group as undefined. Only one
  * caller enters groupWait; additional callers observe callback completion. */
 while(__atomic_exchange_n(&waitLock,1,__ATOMIC_ACQUIRE)) {
  if(__atomic_load_n(&callbacksDone,__ATOMIC_ACQUIRE))return;
 }
 NSUInteger state=[self status];
 if(state>=2&&!__atomic_load_n(&callbacksDone,__ATOMIC_ACQUIRE)) {
  if(completion)queue->groupWait(completion,~(U64)0);
  else {unsigned spins=0;while(!__atomic_load_n(&callbacksDone,__ATOMIC_ACQUIRE))tegu_wait_backoff(&spins);}
 }
 __atomic_store_n(&waitLock,0,__ATOMIC_RELEASE);
}
/* R157: chronod (widgets) aborted on -[TeguCommand GPUEndTime]. Metal reports
 * these in CACurrentMediaTime seconds; jobs run synchronously in execute. */
- (double)GPUStartTime { return executeStart; }
- (double)GPUEndTime { return executeEnd; }
- (double)kernelStartTime { return executeStart; }
- (double)kernelEndTime { return executeEnd; }
- (NSUInteger)status { return __atomic_load_n(&status,__ATOMIC_ACQUIRE); }
- (id)error { return [self status]>=4?failure:0; }
- (void)dealloc {
 for(unsigned i=0;i<count;i++) {
  if(!jobChunks[i/TEGU_JOB_CHUNK])continue; /* count set without jobs (capacity tests) */
  [TEGU_JOB(jobChunks,i).pipeline release];for(unsigned b=0;b<8;b++)[TEGU_JOB(jobChunks,i).buffers[b] release];
  free(TEGU_JOB(jobChunks,i).wire2);[TEGU_JOB(jobChunks,i).eventObject release];
  for(unsigned b=0;b<31;b++)[TEGU_JOB(jobChunks,i).v2Buffers[b] release];
  for(unsigned b=0;b<31;b++)[TEGU_JOB(jobChunks,i).v2Textures[b] release];
  for(unsigned s=0;s<16;s++)[TEGU_JOB(jobChunks,i).v2Samplers[s] release];
#ifdef TEGU_GRAPHICS_BRIDGE
  [TEGU_JOB(jobChunks,i).renderPipeline release];[TEGU_JOB(jobChunks,i).renderTexture release];
  [TEGU_JOB(jobChunks,i).sampledTexture release];[TEGU_JOB(jobChunks,i).fragmentSampler release];
  for(unsigned a=0;a<2;a++){[TEGU_JOB(jobChunks,i).attachmentClearTargets[a] release];[TEGU_JOB(jobChunks,i).attachmentClearBuffers[a] release];}
  [TEGU_JOB(jobChunks,i).expandIndex release];[TEGU_JOB(jobChunks,i).expandSource release];[TEGU_JOB(jobChunks,i).expandOut release];
  for(unsigned b=0;b<31;b++)[TEGU_JOB(jobChunks,i).vertexBuffers[b] release];
  for(unsigned b=0;b<31;b++){[TEGU_JOB(jobChunks,i).compositorVertexBuffers[b] release];[TEGU_JOB(jobChunks,i).compositorFragmentBuffers[b] release];[TEGU_JOB(jobChunks,i).compositorTextures[b] release];}
  for(unsigned s=0;s<16;s++)[TEGU_JOB(jobChunks,i).compositorSamplers[s] release];
  if(TEGU_JOB(jobChunks,i).compositorPlan.bytes)tegu_air_blob_free(&TEGU_JOB(jobChunks,i).compositorPlan);
#endif
 }
 struct TeguPresentation *presentation=presentations;
 while(presentation){struct TeguPresentation *next=presentation->next;[presentation->drawable release];free(presentation);presentation=next;}
 struct TeguHandler *node=handlers;
 while(node){struct TeguHandler *next=node->next;queue->blockRelease(node->block);free(node);node=next;}
 node=scheduledHandlers;
 while(node){struct TeguHandler *next=node->next;queue->blockRelease(node->block);free(node);node=next;}
 if(completionTarget)queue->submissions.ops.release(completionTarget);
 if(completion)queue->submissions.ops.release(completion);
 for(unsigned c=0;c<TEGU_COMMAND_MAX_JOBS/TEGU_JOB_CHUNK;c++)free(jobChunks[c]);
 tegu_command_indirect_free(self);
 [debugLabel release]; [failure release]; [queue release]; [super dealloc];
}
@end
static int tegu_threadgroup_lengths_present(const NSUInteger *lengths) {
 if(!lengths)return 0;
 for(unsigned i=0;i<31;i++)if(lengths[i])return 1;
 return 0;
}
#ifdef TEGU_FRONTEND_HOST
/* Host test seam: observes the exact per-index table handed to the native
 * planner. It has no effect on device builds or planner behavior. */
static NSUInteger tegu_host_forwarded_threadgroup_lengths[31];
static void tegu_host_record_threadgroup_lengths(const NSUInteger *lengths) {
 for(unsigned i=0;i<31;i++)tegu_host_forwarded_threadgroup_lengths[i]=lengths?lengths[i]:0;
}
#endif
static void tegu_compute_inert_trace(unsigned groups,unsigned local,unsigned dynamic) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=32)return;
 char line[160];int n=snprintf(line,sizeof(line),
  "TEGU_INERT_COMPUTE_DISPATCH groups=%u local=%u dynamic=%u\n",groups,local,dynamic);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
@implementation TeguEncoder
- (id)initWithCommand:(TeguCommand *)value { self=[super init]; if(self){command=[value retain];memset(threadgroupLengths,0,sizeof(threadgroupLengths));} return self; }
- (void)setComputePipelineState:(TeguPipeline *)value {
 if(ended||[command teguCommitted]||!value||value->owner!=command->queue->owner) { TEGU_INVALID(command); return; }
 [value retain]; [pipeline release]; pipeline=value;
}
- (void)setBuffer:(TeguBuffer *)value offset:(NSUInteger)offset atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||(value&&(value->owner!=command->queue->owner||offset>=value->logical||offset%4))) { TEGU_INVALID(command); return; }
 [value retain]; [buffers[index] release]; buffers[index]=value; offsets[index]=offset;
}
- (void)setBufferOffset:(NSUInteger)offset atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||!buffers[index]||offset>=buffers[index]->logical||offset%4) {
  TEGU_INVALID(command);return;
 }
 offsets[index]=offset;
}
- (void)setBytes:(const void *)bytes length:(NSUInteger)length atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||length>4096||(!bytes&&length)){TEGU_INVALID(command);return;}
 TeguBuffer *buffer=length?[command->queue->owner newBufferWithBytes:bytes length:length options:0]:0;
 if(length&&!buffer){TEGU_INVALID(command);return;}
 [self setBuffer:buffer offset:0 atIndex:index];[buffer release];
}
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||length>32768||(length&&length%16)){TEGU_INVALID(command);return;}
 threadgroupLengths[index]=length;
}
#ifdef TEGU_FRONTEND_HOST
- (NSUInteger)teguHostThreadgroupMemoryLengthAtIndex:(NSUInteger)index {
 return index<31?threadgroupLengths[index]:0;
}
#endif
- (void)setSamplerState:(id)sampler atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=16){TEGU_INVALID(command);return;}
 [sampler retain];[samplers[index] release];samplers[index]=sampler;
}
- (void)setSamplerStates:(id const *)values withRange:(struct TeguComputeRange)range {
 if(ended||[command teguCommitted]||range.location>16||range.length>16-range.location||(!values&&range.length)){TEGU_INVALID(command);return;}
 for(NSUInteger i=0;i<range.length;i++)[self setSamplerState:values[i] atIndex:range.location+i];
}
- (void)dispatchThreadgroups:(MTLSize)groups threadsPerThreadgroup:(MTLSize)local {
#if defined(TEGU_GRAPHICS_BRIDGE) && defined(TEGU_RENDER_TRACE)
 unsigned textureMask=0;for(unsigned i=0;i<31;i++)if(textures[i])textureMask|=1u<<i;
 static unsigned emitted;if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16){char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_COMPUTE_DISPATCH variant=threadgroups groups=%lux%lux%lu local=%lux%lux%lu textures=0x%08x jobs=%u\n",
  groups.width,groups.height,groups.depth,local.width,local.height,local.depth,textureMask,command->count);
  if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);}
#endif
 if(command->count>=TEGU_COMMAND_MAX_JOBS)tegu_command_capacity_marker("compute",command->count);
 if(ended||[command teguCommitted]||!pipeline||command->count>=TEGU_COMMAND_MAX_JOBS||!groups.width||!groups.height||!groups.depth||groups.width>65536||groups.height>65536||groups.depth>65536) { TEGU_INVALID(command); return; }
 if(pipeline->inert) { tegu_compute_inert_trace((unsigned)groups.width,(unsigned)local.width,
  tegu_threadgroup_lengths_present(threadgroupLengths)); return; }
#ifdef TEGU_MALI_BRIDGE
 if(!tegu_compute_encoder_dispatch(command,pipeline,buffers,offsets,textures,samplers,threadgroupLengths,
  0,groups,local))TEGU_INVALID(command);
#else
 /* The offline v1 package has no WLS transport; never drop a dynamic length. */
 if(tegu_threadgroup_lengths_present(threadgroupLengths)) { TEGU_INVALID(command); return; }
 if(local.width!=pipeline->info.local[0]||local.height!=pipeline->info.local[1]||local.depth!=pipeline->info.local[2]) { TEGU_INVALID(command); return; }
 /* v1 exposes one GPU page per binding. Refuse larger visible ranges rather
  * than silently truncate Metal buffer semantics. Application push bytes zero. */
 for(unsigned b=0;b<8;b++) if(pipeline->info.bindings&(1u<<b)) {
  TeguBuffer *v=buffers[b]; NSUInteger o=offsets[b];
  if(!v||o>=v->logical||v->logical-o>4096-(o%4096)||o>0xffffffffUL) { TEGU_INVALID(command); return; }
 }
 struct Dispatch *j=tegu_command_job(command,command->count++);
 j->pipeline=[pipeline retain]; j->wire[0]=1; j->wire[1]=groups.width; j->wire[2]=groups.height; j->wire[3]=groups.depth;
 for(unsigned b=0;b<8;b++) if(pipeline->info.bindings&(1u<<b)) {
  TeguBuffer *v=buffers[b]; j->buffers[b]=[v retain];
  j->wire[4+b*4]=(unsigned)v->handle; j->wire[5+b*4]=(unsigned)offsets[b]; j->wire[6+b*4]=(unsigned)(v->logical-offsets[b]);
 }
#endif
}
- (void)dispatchThreads:(MTLSize)threads threadsPerThreadgroup:(MTLSize)local {
#if defined(TEGU_GRAPHICS_BRIDGE) && defined(TEGU_RENDER_TRACE)
 unsigned textureMask=0;for(unsigned i=0;i<31;i++)if(textures[i])textureMask|=1u<<i;
 static unsigned emitted;if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)<16){char line[224];int n=snprintf(line,sizeof(line),
  "TEGU_COMPUTE_DISPATCH variant=threads grid=%lux%lux%lu local=%lux%lux%lu textures=0x%08x jobs=%u\n",
  threads.width,threads.height,threads.depth,local.width,local.height,local.depth,textureMask,command->count);
  if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);}
#endif
 if(command->count>=TEGU_COMMAND_MAX_JOBS)tegu_command_capacity_marker("compute",command->count);
 if(ended||[command teguCommitted]||!pipeline||command->count>=TEGU_COMMAND_MAX_JOBS||
    threads.width>0xffffffffUL||threads.height>0xffffffffUL||threads.depth>0xffffffffUL){TEGU_INVALID(command);return;}
 if(pipeline->inert){tegu_compute_inert_trace((unsigned)threads.width,(unsigned)local.width,
  tegu_threadgroup_lengths_present(threadgroupLengths));return;}
 /* A zero-thread grid executes no thread, matching Metal, and is not an error. */
 if(!threads.width||!threads.height||!threads.depth)return;
#ifdef TEGU_MALI_BRIDGE
 if(!tegu_compute_encoder_dispatch(command,pipeline,buffers,offsets,textures,samplers,threadgroupLengths,
  1,threads,local))TEGU_INVALID(command);
#else
 /* Exact nonuniform-grid execution needs the translator's regions and the
  * v2 transport; an offline v1 package cannot express it. */
 (void)local;TEGU_INVALID(command);
#endif
}
- (void)endEncoding { if(ended) { TEGU_INVALID(command); return; } ended=1; command->encoding=0; }
- (void)dealloc { if(!ended) TEGU_INVALID(command); [pipeline release]; for(unsigned b=0;b<31;b++){[buffers[b] release];[textures[b] release];}for(unsigned s=0;s<16;s++)[samplers[s] release]; [command release]; [super dealloc]; }
@end

#ifdef TEGU_AIR_BRIDGE
#include "library_frontend.inc"
#endif

#ifdef TEGU_AIR_BRIDGE
#include "api_events.inc"
#else
/* Compute-only builds have no event/fence objects: refuse explicitly. */
static void tegu_command_encode_event(TeguCommand *command,id event,U64 value,unsigned kind) {
 (void)event;(void)value;(void)kind;TEGU_INVALID(command);
}
static int tegu_event_job_run(struct Dispatch *job) { (void)job;return -1; }
static void tegu_command_events_flush(TeguCommand *command) { (void)command; }
static int tegu_command_indirect_verify(TeguCommand *command) { (void)command;return 0; }
static void tegu_command_indirect_free(TeguCommand *command) { (void)command; }
static int tegu_command_use_resource(TeguCommand *command,id object) { (void)command;(void)object;return 0; }
static void tegu_nocopy_sync_in(TeguCommand *command) { (void)command; }
static void tegu_nocopy_sync_out(TeguCommand *command) { (void)command; }
static void tegu_buffer_external_free(TeguBuffer *buffer) { (void)buffer; }
static id tegu_device_new_nocopy(TeguComputeDevice *device,void *bytes,NSUInteger length,NSUInteger options,void (^deallocator)(void *,NSUInteger)) {
 (void)device;(void)bytes;(void)length;(void)options;(void)deallocator;
 tegu_frontend_selector_refused("newBufferWithBytesNoCopy:length:options:deallocator:","compute-only build");return 0;
}
static void tegu_fence_update(TeguCommand *command,id fence) { (void)fence;TEGU_INVALID(command); }
static void tegu_fence_wait(TeguCommand *command,id fence) { (void)fence;TEGU_INVALID(command); }
static id tegu_buffer_make_texture(TeguBuffer *buffer,id descriptor,NSUInteger offset,NSUInteger bytesPerRow) {
 (void)buffer;(void)descriptor;(void)offset;(void)bytesPerRow;
 tegu_frontend_selector_refused("newTextureWithDescriptor:offset:bytesPerRow:","no exact buffer-backed 2D texture layout");return 0;
}
#endif

#if defined(TEGU_GRAPHICS_BRIDGE) && defined(TEGU_AIR_BRIDGE)
typedef struct TeguComputeRange TeguComputeRange;
@interface TeguEncoder (TextureBindings)
- (void)setTexture:(TeguTexture *)texture atIndex:(NSUInteger)index;
- (void)setTextures:(TeguTexture * const *)values withRange:(TeguComputeRange)range;
@end
static void tegu_compute_texture_marker(NSUInteger index,TeguTexture *texture) {
#ifdef TEGU_RENDER_TRACE
 static unsigned emitted;if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=24)return;
 char line[288];int n=snprintf(line,sizeof(line),
  "TEGU_COMPUTE_TEXTURE index=%lu present=%u format=%lu type=%lu width=%lu height=%lu depth=%lu samples=%lu levels=%lu usage=0x%lx stride=%lu offset=%llu alloc=%lu\n",
  index,(unsigned)(texture!=0),texture?texture->format:0,texture?texture->textureKind:0,
  texture?texture->textureWidth:0,texture?texture->textureHeight:0,texture?texture->textureDepth:0,
  texture?texture->textureSamples:0,texture?texture->levelCount:0,texture?texture->textureUsage:0,
  texture?texture->rowBytes:0,texture?(U64)texture->mip[0].offset:0,texture&&texture->buffer?texture->buffer->logical:0);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
#else
 (void)index;(void)texture;
#endif
}
@implementation TeguEncoder (TextureBindings)
- (void)setTexture:(TeguTexture *)texture atIndex:(NSUInteger)index {
 if(ended||[command teguCommitted]||index>=31||(texture&&
    (!tegu_owned_class(texture,"TeguTexture")||[texture device]!=command->queue->owner))){TEGU_INVALID(command);return;}
 [texture retain];[textures[index] release];textures[index]=texture;tegu_compute_texture_marker(index,texture);
}
- (void)setTextures:(TeguTexture * const *)values withRange:(TeguComputeRange)range {
 if(ended||[command teguCommitted]||range.location>31||range.length>31-range.location||(!values&&range.length)){
  TEGU_INVALID(command);return;
 }
 for(NSUInteger i=0;i<range.length;i++)if(values[i]&&
    (!tegu_owned_class(values[i],"TeguTexture")||[values[i] device]!=command->queue->owner)){
  TEGU_INVALID(command);return;
 }
 for(NSUInteger i=0;i<range.length;i++)[self setTexture:values[i] atIndex:range.location+i];
}
@end
#endif

#ifdef TEGU_MALI_BRIDGE
#if defined(TEGU_GRAPHICS_BRIDGE) && defined(TEGU_AIR_BRIDGE)
static int tegu_compute_encoder_dispatch(TeguCommand *command,TeguPipeline *pipeline,TeguBuffer **buffers,
 NSUInteger *offsets,id *textures,id *samplers,const NSUInteger *threadgroupLengths,
 unsigned mode,MTLSize extents,MTLSize local)
{
#ifdef TEGU_FRONTEND_HOST
 tegu_host_record_threadgroup_lengths(threadgroupLengths);
#endif
 if(!command||!pipeline||!pipeline->compiled||!buffers||!offsets||!textures||!samplers)return 0;
 TeguAIRComputeResource resources[32];id objects[32];uint64_t tokens[32];unsigned count=0;
 char message[512]={0};
 for(unsigned i=0;i<31;i++)if(buffers[i]) {
  TeguBuffer *buffer=buffers[i];
  uint64_t padded=((uint64_t)buffer->logical+15)&~15ull;
  if(count>=32||!padded||offsets[i]>=padded)return 0;
  resources[count]=(TeguAIRComputeResource){.version=1,.kind=TEGU_AIR_COMPUTE_BUFFER,.index=i,
   .access=TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_WRITE,
   .length=padded,.offset=offsets[i]};
  objects[count]=(id)buffer;tokens[count]=buffer->handle;count++;
 }
 for(unsigned i=0;i<31;i++)if(textures[i]) {
  if(count>=32)return 0;
  TeguTexture *texture=(TeguTexture *)textures[i];
  if(!tegu_owned_class((id)texture,"TeguTexture")||[texture device]!=command->queue->owner)return 0;
  uint64_t token=0,length=0,offset=0;uint32_t storage=0;
  if(texture->surfaceImported) {
   token=texture->importedHandle;length=texture->surfaceAllocSize;
   offset=texture->mip[0].offset;storage=0;
  } else if(texture->buffer) {
   token=texture->buffer->handle;length=((uint64_t)texture->buffer->logical+15)&~15ull;
   offset=texture->mip[0].offset;storage=(uint32_t)[texture storageMode];
  } else return 0;
  if(!token||!length||length<=offset||!texture->mip[0].stride)return 0;
  uint32_t access=0;
  if(texture->textureUsage&1)access|=TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_SAMPLE;
  if(texture->textureUsage&2)access|=TEGU_AIR_COMPUTE_ACCESS_STORAGE|TEGU_AIR_COMPUTE_ACCESS_WRITE;
  if(!access)return 0;
  uint32_t layers=(uint32_t)(texture->layers?texture->layers:1);
  uint32_t array=(uint32_t)(texture->arrayLength?texture->arrayLength:1);
  uint32_t depth=(uint32_t)(texture->levelDepth[0]?texture->levelDepth[0]:texture->textureDepth);
  resources[count]=(TeguAIRComputeResource){.version=1,.kind=TEGU_AIR_COMPUTE_TEXTURE,.index=i,
   .access=access,.format=(uint32_t)texture->format,.texture_type=(uint32_t)texture->textureKind,
   .width=(uint32_t)texture->textureWidth,.height=(uint32_t)texture->textureHeight,.depth=depth,
   .array_length=array,.mipmap_levels=(uint32_t)texture->levelCount,
   .sample_count=(uint32_t)texture->textureSamples,.usage=(uint32_t)texture->textureUsage,
   .row_stride=(uint32_t)texture->mip[0].stride,.storage_mode=storage,.swizzle_word=0,
   .slice_stride=(uint32_t)texture->levelSliceStride[0],
   .layer_stride=(uint32_t)texture->levelLayerStride[0],.array_layers=layers,
   .length=length,.offset=offset,.resource_id=(token<<32)|(uint32_t)(offset?offset:1)};
  {
   uint64_t base=(uint64_t)texture->mip[0].offset;
   for(NSUInteger level=0;level<texture->levelCount&&level<TEGU_AIR_COMPUTE_MAX_LEVELS;level++) {
    resources[count].level_offset[level]=(uint64_t)texture->mip[level].offset-base;
    resources[count].level_row_stride[level]=(uint32_t)texture->mip[level].stride;
    resources[count].level_slice_stride[level]=(uint32_t)texture->levelSliceStride[level];
    resources[count].level_layer_stride[level]=(uint32_t)texture->levelLayerStride[level];
   }
   resources[count].swizzle_valid=0;
  }
  objects[count]=(id)texture;tokens[count]=token;count++;
 }
 for(unsigned i=0;i<16;i++)if(samplers[i]) {
  if(count>=32)return 0;
  TeguSampler *sampler=(TeguSampler *)samplers[i];
  if(!tegu_owned_class((id)sampler,"TeguSampler")||sampler->owner!=command->queue->owner)return 0;
  const struct n2t_sampler_state *st=&sampler->state;
  resources[count]=(TeguAIRComputeResource){.version=1,.kind=TEGU_AIR_COMPUTE_SAMPLER,.index=i,
   .min_filter=st->min_filter,.mag_filter=st->mag_filter,.mip_filter=st->mip_filter,
   .address_s=st->address_s,.address_t=st->address_t,.address_r=st->address_r,
   .border_color=st->border_color_kind,.normalized_coordinates=st->normalized,
   .lod_average=0,.max_anisotropy=st->max_anisotropy,.compare_function=st->compare_function,
   .reduction_mode=st->reduction,.lod_min_clamp=st->lod_min,.lod_max_clamp=st->lod_max};
  objects[count]=(id)sampler;tokens[count]=0;count++;
 }
 /* Buffer-only dispatches keep the hardware-qualified TGP1/TDG1 path; the
  * v2 transport always emits TDG2 and therefore needs a TGP2 package.
  * Dynamic threadgroup memory is a TGP2/TgLength feature, so any nonzero
  * length forces v2 even for an otherwise buffer-only pipeline. */
 int buffer_only=1;
 for(unsigned i=0;i<count;i++)if(resources[i].kind!=TEGU_AIR_COMPUTE_BUFFER)buffer_only=0;
 int dynamic=tegu_threadgroup_lengths_present(threadgroupLengths);
 if(mode==0&&!dynamic&&pipeline->packageVersion!=2&&(pipeline->packageVersion==1||buffer_only)) {
  if(tegu_runtime_dispatch(pipeline,buffers,offsets,threadgroupLengths,extents,local,
     tegu_command_job(command,command->count))){command->count++;return 1;}
  return 0;
 }
 return tegu_runtime_dispatch_v2(pipeline,resources,objects,tokens,threadgroupLengths,count,mode,extents,local,
  command,message,sizeof(message));
}
#else
static int tegu_compute_encoder_dispatch(TeguCommand *command,TeguPipeline *pipeline,TeguBuffer **buffers,
 NSUInteger *offsets,id *textures,id *samplers,const NSUInteger *threadgroupLengths,
 unsigned mode,MTLSize extents,MTLSize local)
{
#ifdef TEGU_FRONTEND_HOST
 tegu_host_record_threadgroup_lengths(threadgroupLengths);
#endif
 (void)textures;(void)samplers;(void)threadgroupLengths;
 if(mode)return 0;
 if(tegu_runtime_dispatch(pipeline,buffers,offsets,threadgroupLengths,extents,local,
    tegu_command_job(command,command->count))){command->count++;return 1;}
 return 0;
}
#endif
#endif

#ifdef TEGU_GRAPHICS_BRIDGE
#include "heap_frontend.inc"
#endif

#ifdef TEGU_PIPELINE_FIXTURE
#include "tegu-pipeline-fixture.h"
int tegu_frontend_package_probe(const void *package, NSUInteger package_length) {
 MARK("begin explicit_device supplied_pipeline synchronous_compute");
 TeguComputeDevice *device=[[TeguComputeDevice alloc] init];
 if(!device) { MARK("FAIL device"); return 1; }
 TeguQueue *queue=[device newCommandQueue];
 TeguPipeline *pipeline=[device newTeguPipelineWithPackage:package length:package_length];
 TeguBuffer *input=[device newBufferWithLength:1152 options:0];
 TeguBuffer *output=[device newBufferWithLength:1152 options:0];
 TeguBuffer *scale=[device newBufferWithLength:4 options:0];
 int ok=queue&&pipeline&&input&&output&&scale;
 for(unsigned run=0;ok&&run<4;run++) {
  float factor=run==0?2:run==1?3:run==2?-1:0.5f;
  float *in=[input contents],*out=[output contents];
  for(unsigned i=0;i<288;i++) { in[i]=i<32?-999:(float)(i-32+7); out[i]=-777; }
  *(float *)[scale contents]=factor;
  TeguCommand *command=[queue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  if(!command||!encoder) { ok=0; break; }
  [encoder setComputePipelineState:pipeline];
  [encoder setBuffer:input offset:128 atIndex:0];
  [encoder setBuffer:output offset:128 atIndex:1];
  [encoder setBuffer:scale offset:0 atIndex:2];
  [encoder dispatchThreadgroups:(MTLSize){256,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];
  [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
  ok=[command status]==4 && ![command error];
  for(unsigned i=0;i<288;i++) if(in[i]!=(i<32?-999:(float)(i-32+7)) || out[i]!=(i<32?-777:(float)(i-32+7)*factor+(i-32))) ok=0;
  /* Verify the remaining mapped output page and scalar allocation were untouched. */
  unsigned *guard=[output contents],*sg=[scale contents];
  for(unsigned i=288;i<output->mapped/4;i++) if(guard[i]) ok=0;
  for(unsigned i=1;i<scale->mapped/4;i++) if(sg[i]) ok=0;
  if(*(float *)[scale contents]!=factor) ok=0;
 }
 [scale release]; [output release]; [input release]; [pipeline release]; [queue release]; [device release];
 if(ok) MARK("PASS 4_dispatches 1024_GPU_outputs supplied_pipeline NOT_system_Metal_device");
 else MARK("FAIL compute_or_guards");
 return ok?0:1;
}
int tegu_frontend_probe(void) {
 MARK("offline_pipeline_fixture");
 return tegu_frontend_package_probe(tegu_pipeline_fixture,sizeof(tegu_pipeline_fixture));
}
#endif

/* The shared blit file now also carries texture-copy methods whose types are
 * defined only by the render frontend. Compute-only host builds do not use the
 * blit encoder; device builds, including compute-only plugins, still include
 * it. Graphics host builds include the render types first. */
#if !defined(TEGU_FRONTEND_HOST) || defined(TEGU_GRAPHICS_BRIDGE)
#include "blit_encoder.inc"
#endif

/* Apple1/Apple2-baseline API completeness (events/fences live in api_events.inc). */
#ifdef TEGU_AIR_BRIDGE
#include "api_surface.inc"
#endif

#if defined(TEGU_METAL_BASE) && defined(TEGU_PIPELINE_FIXTURE)
#include "framework_device_probe.inc"
#endif
#ifdef NORD2_JM_TRANSPORT
/* B1 (C6p lag analysis): CA's thread was held until our executor had
 * submitted every job (sched 16-45 ms per heavy frame). With kernel 0112 a
 * pipelined command reserves the imported surfaces its jobs will write at
 * commit: the framebuffer then holds a swap of them exactly as if those jobs
 * were already queued, so the command is reported scheduled here, before
 * execution; the executor submits on its own thread and retires the
 * reservation with the command's newest job id. The depth-2 throttle moves
 * here too (CA at most two commands ahead). Not taken - today's behaviour:
 * pre-0112 kernel, synchronous bridge, pipeline A/B off, a shadow written
 * back at completion, more than 8 imported surfaces, or
 * NORD2_METAL_EARLY_COMMIT=0. */
#ifndef TEGU_EARLY_COMMIT_DEFAULT
#define TEGU_EARLY_COMMIT_DEFAULT 1
#endif
static int tegu_early_commit_enabled(void) {
 static int state=-1;
 if(state<0){extern char *getenv(const char *);const char *env=getenv("NORD2_METAL_EARLY_COMMIT");
  state=env&&(env[0]=='0'||env[0]=='1')&&!env[1]?env[0]=='1':TEGU_EARLY_COMMIT_DEFAULT;}
 return state;
}
static void tegu_commit_early(TeguCommand *c) {
 if(!c->count||c->invalid||c->encoding||!c->queue->asyncPipeline||!tegu_early_scheduled()||!tegu_early_commit_enabled())return;
 uintptr_t conn=c->queue->owner->connection;
 if(n2mb_async(conn,2)!=1||!tegu_ab_value(c->abWord,TEGU_AB_ASYNC_PIPELINE))return;
#ifdef TEGU_GRAPHICS_BRIDGE
 if(tegu_shadow_jobs_any(c->jobChunks,c->count,1))return;
#endif
 /* Every buffer a job may write that can be an imported surface: targets
  * (incl. true-MRT/MSAA sidecar targets and resolves), clear targets, blit
  * destinations. The bridge keeps only this context's imports. */
 U64 h[64];unsigned n=0;
#define TEGU_EARLY_ADD(x) do{U64 v_=(x);if(v_){if(n==64)return;h[n++]=v_;}}while(0)
 for(unsigned i=0;i<c->count;i++) {
  struct Dispatch *j=&TEGU_JOB(c->jobChunks,i);
  if(j->blit&&j->buffers[1])TEGU_EARLY_ADD(j->buffers[1]->handle);
#ifdef TEGU_GRAPHICS_BRIDGE
  TEGU_EARLY_ADD(j->genericDrawRequest.target_token);
  TEGU_EARLY_ADD(j->renderArgs[1]);
  TeguTexture *t=(TeguTexture *)j->renderTexture;
  if(t&&tegu_owned_class(t,"TeguTexture"))TEGU_EARLY_ADD(t->surfaceImported?t->importedHandle:(t->buffer?t->buffer->handle:0));
  for(unsigned a=0;a<2;a++){TeguTexture *x=(TeguTexture *)j->attachmentClearTargets[a];
   if(x&&tegu_owned_class(x,"TeguTexture"))TEGU_EARLY_ADD(x->surfaceImported?x->importedHandle:(x->buffer?x->buffer->handle:0));}
  const struct tegu_render_generic_sidecar *sc=tegu_jm_sidecar(j);
  for(unsigned k=0;sc&&k<sc->count&&k<4;k++){TEGU_EARLY_ADD(sc->targets[k].token);if(sc->msaa)TEGU_EARLY_ADD(sc->resolves[k].token);}
#endif
 }
#undef TEGU_EARLY_ADD
 /* A kernel without 0112 answers the (cached) probe Unsupported: today's
  * path, which throttles at execution. */
 U64 id=0;
 if(n2mb_surface_reserve(conn,NULL,0,&id))return;
 if(__atomic_load_n(&c->queue->earlyInflight,__ATOMIC_ACQUIRE)>=TEGU_EARLY_SCHEDULED_DEPTH) {
  double began=tegu_host_seconds();unsigned spins=0;
  __atomic_fetch_add(&tegu_early_throttled,1,__ATOMIC_RELAXED);
  while(__atomic_load_n(&c->queue->earlyInflight,__ATOMIC_ACQUIRE)>=TEGU_EARLY_SCHEDULED_DEPTH) {
   if(began>0?tegu_host_seconds()-began>=0.1:spins>=5600u){__atomic_fetch_add(&tegu_early_forced,1,__ATOMIC_RELAXED);break;}
   tegu_wait_backoff(&spins);
  }
 }
 c->earlyCommit=2; /* throttled here: execution must not wait again */
 if(n2mb_surface_reserve(conn,h,n,&id))return;
 c->earlyCommit=1;c->reservation=id;
 __atomic_fetch_add(&c->queue->earlyInflight,1,__ATOMIC_ACQ_REL);
 __atomic_fetch_add(&tegu_early_commits,1,__ATOMIC_RELAXED);__atomic_fetch_add(&tegu_early_cmds,1,__ATOMIC_RELAXED);
 __atomic_store_n(&c->status,3,__ATOMIC_RELEASE);
 tegu_sched_note(c->committedAt,tegu_host_seconds());
}
#endif
