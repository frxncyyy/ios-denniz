#define TEGU_AIR_BRIDGE 1
#define TEGU_MALI_BRIDGE 1
#define TEGU_GRAPHICS_BRIDGE 1
#define NORD2_JM_TRANSPORT 1
#define TEGU_IOSURFACE_TEST_LOADER 1

#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct TestIOSurface { unsigned char data[64*64*4]; size_t width,height,row,alloc,planes; uint32_t fourcc; };
static struct TestIOSurface test_surface;
static unsigned test_surface_locks;
static uint32_t test_si_id(void *p){(void)p;return 1;}
static size_t test_si_width(void *p){return ((struct TestIOSurface*)p)->width;}
static size_t test_si_height(void *p){return ((struct TestIOSurface*)p)->height;}
static size_t test_si_row(void *p){return ((struct TestIOSurface*)p)->row;}
static size_t test_si_alloc(void *p){return ((struct TestIOSurface*)p)->alloc;}
static uint32_t test_si_fourcc(void *p){return ((struct TestIOSurface*)p)->fourcc;}
static size_t test_si_planes(void *p){return ((struct TestIOSurface*)p)->planes;}
static int test_si_lock(void *p,uint32_t o,uint32_t *seed){(void)p;(void)o;(void)seed;test_surface_locks++;return 0;}
static int test_si_unlock(void *p,uint32_t o,uint32_t *seed){(void)p;(void)o;(void)seed;return 0;}
static void *test_si_base(void *p){return ((struct TestIOSurface*)p)->data;}
static void *test_si_retain(void *p){return p;}
static void test_si_release(void *p){(void)p;}
__attribute__((unused)) static void *tegu_test_iosurface_dlopen(const char *p,int m){(void)p;(void)m;return (void*)1;}
__attribute__((unused)) static void *tegu_test_iosurface_dlsym(void *h,const char *n){
 (void)h;
 if(!strcmp(n,"IOSurfaceGetID"))return (void*)test_si_id;
 if(!strcmp(n,"IOSurfaceGetWidth"))return (void*)test_si_width;
 if(!strcmp(n,"IOSurfaceGetHeight"))return (void*)test_si_height;
 if(!strcmp(n,"IOSurfaceGetBytesPerRow"))return (void*)test_si_row;
 if(!strcmp(n,"IOSurfaceGetAllocSize"))return (void*)test_si_alloc;
 if(!strcmp(n,"IOSurfaceGetPixelFormat"))return (void*)test_si_fourcc;
 if(!strcmp(n,"IOSurfaceGetPlaneCount"))return (void*)test_si_planes;
 if(!strcmp(n,"IOSurfaceLock"))return (void*)test_si_lock;
 if(!strcmp(n,"IOSurfaceUnlock"))return (void*)test_si_unlock;
 if(!strcmp(n,"IOSurfaceGetBaseAddress"))return (void*)test_si_base;
 if(!strcmp(n,"CFRetain"))return (void*)test_si_retain;
 if(!strcmp(n,"CFRelease"))return (void*)test_si_release;
 return 0;
}
/* Host ownership/recording test with a simulated transport. This does not
 * qualify Metal or GPU execution; the native probe performs that check. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define TEGU_FRONTEND_HOST 1
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
/* Native Metal library/function selectors backed by the Rust AIR parser. */
#include "compiler_trace_sha256.h"
#include "pipeline_cache.c"
#ifdef TEGU_RENDER_TRACE
#include "fixed_ui_constant_trace.h"
#endif
@interface TeguLibrary : NSObject {
@public TeguComputeDevice *owner; TeguAIRLibrary *library;
 id libraryLabel;
 char sourceSHA256[65];
 /* Pipeline cache identity: SHA-256 of the source metallib, plus for a
  * specialization snapshot its entry and every constant (kind, type, index,
  * name, value).  Invalid -> pipelines from it are never cached. */
 unsigned char cacheIdentity[32];unsigned cacheIdentityValid;
 /* Specialization as text (entry|count|kind:type:index:name:hex;...), for
  * the pipeline cache's miss records; NULL for a whole metallib. */
 char *cacheReplay;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)label;
- (id)functionNames;
- (id)newFunctionWithName:(id)name;
- (id)newFunctionWithName:(id)name constantValues:(id)values error:(id *)error;
- (void)newFunctionWithName:(id)name constantValues:(id)values
 completionHandler:(void (^)(id function,NSError *error))completionHandler;
- (id)newFunctionWithDescriptor:(id)descriptor error:(id *)error;
@end
@interface TeguFunction : NSObject {
@public TeguLibrary *library; id functionName,displayName,functionLabel; unsigned kind;
}
- (id)device;
- (id)name;
- (id)label;
- (void)setLabel:(id)label;
- (NSUInteger)functionType;
@end
/* Public library loaders may still return donor function objects. Establish
 * class identity before accessing our ivars; public functionType is not proof
 * that an object has TeguFunction's layout. */
#ifdef TEGU_FRONTEND_HOST
/* GNU libobjc implements object_getClass inline; host NSObject has a plain isa. */
static id tegu_host_object_class(id object){return object?*(id *)object:0;}
#endif
/* Encoder setters and draws call this ~10-15 times per draw. On the device it
 * used to dlopen libobjc and dlsym three symbols, then objc_getClass by name,
 * on every call. The runtime entry points and the classes are process
 * constants: resolve the entry points once and keep each expected class
 * after its first successful lookup (keyed by the literal's address). */
struct tegu_owned_class_cache {const char *name;void *cls;};
static int tegu_owned_class(id function,const char *expectedName) {
 static id (*objectClass)(id);static id (*getClass)(const char *);static const char *(*className)(id);
 static struct tegu_owned_class_cache classes[16];static unsigned resolved;
 if(!__atomic_load_n(&resolved,__ATOMIC_ACQUIRE)) {
#ifdef TEGU_FRONTEND_HOST
  extern id objc_getClass(const char *);
  extern const char *class_getName(id);
  id (*oc)(id)=tegu_host_object_class;id (*gc)(const char *)=objc_getClass;const char *(*cn)(id)=class_getName;
#else
  void *runtime=dlopen("/usr/lib/libobjc.A.dylib",2);
  id (*oc)(id)=runtime?dlsym(runtime,"object_getClass"):0;
  id (*gc)(const char *)=runtime?dlsym(runtime,"objc_getClass"):0;
  const char *(*cn)(id)=runtime?dlsym(runtime,"class_getName"):0;
#endif
  if(!oc||!gc)return 0;
  /* Benign race: every thread stores the same values. */
  __atomic_store_n(&objectClass,oc,__ATOMIC_RELAXED);__atomic_store_n(&getClass,gc,__ATOMIC_RELAXED);
  __atomic_store_n(&className,cn,__ATOMIC_RELAXED);__atomic_store_n(&resolved,1,__ATOMIC_RELEASE);
 }
 if(!function)return 0;
 id expected=0;
 for(unsigned i=0;i<16;i++) {
  const char *n=__atomic_load_n(&classes[i].name,__ATOMIC_ACQUIRE);
  if(!n)break;
  if(n==expectedName){expected=(id)__atomic_load_n(&classes[i].cls,__ATOMIC_RELAXED);break;}
 }
 if(!expected) {
  expected=getClass(expectedName);
  if(expected) {
   /* Insert under a lock; readers match the name (release-published after
    * its class) without one. A full table only costs the lookup above. */
   static unsigned lock;
   while(__sync_lock_test_and_set(&lock,1)){}
   for(unsigned i=0;i<16;i++) {
    const char *n=__atomic_load_n(&classes[i].name,__ATOMIC_RELAXED);
    if(n==expectedName)break;
    if(!n){__atomic_store_n(&classes[i].cls,(void *)expected,__ATOMIC_RELAXED);__atomic_store_n(&classes[i].name,expectedName,__ATOMIC_RELEASE);break;}
   }
   __sync_lock_release(&lock);
  }
 }
 id actual=objectClass(function);
 if(expected&&actual==expected)return 1;
 const char *name=className?className(actual):"unknown";
 NSUInteger length=0;while(name&&name[length]&&length<128)length++;
 write(2,"TEGU_SHADER foreign_class=",26);if(name)write(2,name,length);write(2,"\n",1);
 return 0;
}
static int tegu_function_owned_class(id function){return tegu_owned_class(function,"TeguFunction");}
static int tegu_pc_library_identity(const char *sourceSHA256,unsigned char out[32]) {
 if(!sourceSHA256||!sourceSHA256[0])return -1;
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,"TPC-LIB-1");tegu_pc_key_str(&s,sourceSHA256);
 tegu_pc_sha256_final(&s,out);return 0;
}
static char *tegu_pc_specialized_replay(const char *entry,NSUInteger entryLength,const TeguAIRFunctionConstant *constants,NSUInteger count) {
 size_t cap=entryLength+32,n=0;
 for(NSUInteger i=0;i<count;i++)cap+=48+(constants[i].kind?constants[i].name_len:0)+2*sizeof(constants[i].data);
 char *out=malloc(cap);if(!out)return 0;
 static const char hex[]="0123456789abcdef";
 n+=(size_t)snprintf(out+n,cap-n,"%.*s|%lu|",(int)entryLength,entry,(unsigned long)count);
 for(NSUInteger i=0;i<count&&n<cap;i++) {
  const TeguAIRFunctionConstant *c=&constants[i];
  n+=(size_t)snprintf(out+n,cap-n,"%u:%u:%u:%.*s:",c->kind,c->type,c->kind?0:c->index,(int)(c->kind?c->name_len:0),c->kind?(const char *)c->name:"");
  for(uint32_t b=0;b<c->size&&b<sizeof(c->data)&&n+3<cap;b++){out[n++]=hex[c->data[b]>>4];out[n++]=hex[c->data[b]&15];}
  if(n+2<cap)out[n++]=';';
 }
 out[n<cap?n:cap-1]=0;return out;
}
static void tegu_pc_specialized_identity(const unsigned char parent[32],const char *entry,NSUInteger entryLength,
 const TeguAIRFunctionConstant *constants,NSUInteger count,unsigned char out[32]) {
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,"TPC-SPEC-1");
 tegu_pc_key_bytes(&s,parent,32);tegu_pc_key_bytes(&s,entry,entryLength);tegu_pc_key_u32(&s,(uint32_t)count);
 for(NSUInteger i=0;i<count;i++) {
  const TeguAIRFunctionConstant *c=&constants[i];
  tegu_pc_key_u32(&s,c->kind);tegu_pc_key_u32(&s,c->type);tegu_pc_key_u32(&s,c->kind?0:c->index);
  tegu_pc_key_bytes(&s,c->kind?c->name:0,c->kind?c->name_len:0);
  tegu_pc_key_bytes(&s,c->data,c->size<=sizeof(c->data)?c->size:sizeof(c->data));
 }
 tegu_pc_sha256_final(&s,out);
}
@interface NSBundle : NSObject
+ (id)mainBundle;
- (id)pathForResource:(id)name ofType:(id)type;
@end
@interface NSURL : NSObject
- (signed char)isFileURL;
- (id)path;
@end
@interface TeguComputeDevice (LibraryLoaders)
- (id)newDefaultLibrary;
- (id)newDefaultLibraryWithBundle:(id)bundle error:(id *)error;
- (id)newLibraryWithURL:(NSURL *)url error:(id *)error;
- (id)newLibraryWithFile:(id)path error:(id *)error;
@end
@implementation TeguComputeDevice (Libraries)
- (id)newTeguLibraryWithBytes:(const void *)bytes length:(NSUInteger)length error:(id *)error {
 if(error) *error=0;
 char message[512]={0};
 TeguAIRLibrary *handle=tegu_air_library_open(bytes,length,message,sizeof(message));
 if(!handle) {
  MARK("library_parse_refused");
  if(error) *error=[NSError errorWithDomain:@"TeguAIRLibrary" code:1 userInfo:0];
  return 0;
 }
 TeguLibrary *result=[[TeguLibrary alloc] init];
 if(!result) { tegu_air_library_close(handle); return 0; }
 result->owner=[self retain]; result->library=handle;
 /* Keep only an immutable identity, never the caller's metallib bytes.  Exact
  * native package classes can consequently bind their generated code to the
  * library which produced it without extending the library lifetime contract. */
 if(tegu_compiler_identity_sha256(bytes,length,result->sourceSHA256))result->sourceSHA256[0]=0;
 result->cacheIdentityValid=!tegu_pc_library_identity(result->sourceSHA256,result->cacheIdentity);
 return result;
}
/* Override loaders that _MTLDevice sends straight to its own libraryBuilder,
 * otherwise the returned functions belong to Apple's compiler, not our bridge. */
- (id)newDefaultLibrary {return [self newDefaultLibraryWithBundle:[NSBundle mainBundle] error:0];}
- (id)newDefaultLibraryWithBundle:(id)bundle error:(id *)error {
 if(error)*error=0;
 id path=[bundle pathForResource:@"default" ofType:@"metallib"];
 if(!path) {
  if(error)*error=[NSError errorWithDomain:@"TeguAIRLibrary" code:2 userInfo:0];
  return 0;
 }
 return [self newLibraryWithFile:path error:error];
}
- (id)newLibraryWithURL:(NSURL *)url error:(id *)error {
 if(error)*error=0;
 if(!url||![url isFileURL]) {
  if(error)*error=[NSError errorWithDomain:@"TeguAIRLibrary" code:3 userInfo:0];
  return 0;
 }
 return [self newLibraryWithFile:[url path] error:error];
}
- (id)newLibraryWithFile:(id)path error:(id *)error {
 NSData *data=path?[NSData dataWithContentsOfFile:path]:0;
 return [self newTeguLibraryWithBytes:[data bytes] length:[data length] error:error];
}
- (id)newLibraryWithData:(id)data error:(id *)error {
 /* Metal takes dispatch_data_t, NOT NSData. Mapping may allocate a contiguous
  * temporary; library_open copies what it needs before releasing the mapping. */
 void *sys=dlopen("/usr/lib/libSystem.B.dylib",2);
 id (*mapData)(id,const void **,NSUInteger *)=sys?dlsym(sys,"dispatch_data_create_map"):0;
 void (*releaseData)(id)=sys?dlsym(sys,"dispatch_release"):0;
 const void *bytes=0; NSUInteger length=0;
 id mapped=data&&mapData&&releaseData?mapData(data,&bytes,&length):0;
 id result=[self newTeguLibraryWithBytes:bytes length:length error:error];
 if(mapped) releaseData(mapped);
 return result;
}
@end
@interface NSObject (TeguFunctionInputs)
- (id)constantValues;
- (id)specializedName;
- (NSUInteger)options;
- (id)newNamedConstantArray;
- (id)newIndexedConstantArray;
- (NSUInteger)count;
- (id)objectAtIndex:(NSUInteger)index;
- (NSUInteger)dataType;
- (NSUInteger)index;
- (const void *)data;
@end
static void tegu_function_error(id *error,const char *message) {
 NSUInteger n=0;while(message[n]&&n<511)n++;
 write(2,"TEGU_FUNCTION ",14);write(2,message,n);write(2,"\n",1);
 if(error)*error=[NSError errorWithDomain:@"TeguAIRFunction" code:1 userInfo:0];
}
static void tegu_function_lookup_missing(const char *name,NSUInteger nameLength,TeguLibrary *library) {
 /* MPS asserts after a nil function lookup and otherwise hides whether the
  * metallib inventory or compute translation failed. Emit the lookup boundary
  * once per request, before any compute-pipeline method can run. */
 static unsigned emitted;
 if(!name||!library||__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
 char line[320];NSUInteger shown=nameLength<96?nameLength:96;
 int n=snprintf(line,sizeof(line),
  "TEGU_FUNCTION_LOOKUP result=missing name=%.*s name_bytes=%lu entries=%lu source_sha16=%.16s\n",
  (int)shown,name,(unsigned long)nameLength,
  (unsigned long)tegu_air_function_count(library->library),library->sourceSHA256);
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
@implementation TeguLibrary
- (id)device { return owner; }
- (id)label { return libraryLabel; }
- (void)setLabel:(id)label { id value=[label copy];[libraryLabel release];libraryLabel=value; }
- (id)functionNames {
 id names=[NSMutableArray array];
 for(NSUInteger i=0;i<tegu_air_function_count(library);i++) {
  char name[4096]; unsigned kind;
  NSUInteger required=tegu_air_function_info(library,i,name,sizeof(name),&kind);
  if(required>0&&required<=sizeof(name)&&kind) [names addObject:[NSString stringWithUTF8String:name]];
 }
 return names;
}
- (id)newFunctionWithName:(id)requested {
 if(!requested) return 0;
 const char *requestedName=[requested UTF8String];
 if(!requestedName) return 0;
 NSUInteger requestedLength=0;
 while(requestedLength<4096&&requestedName[requestedLength])requestedLength++;
 if(requestedLength==4096) return 0;
 for(NSUInteger i=0;i<tegu_air_function_count(library);i++) {
  char name[4096]; unsigned kind;
  NSUInteger required=tegu_air_function_info(library,i,name,sizeof(name),&kind);
  if(!required||required>sizeof(name)||!kind) continue;
  /* The inventory can contain hundreds of entries. Compare its bounded C
   * spelling before constructing an Objective-C string so one lookup does
   * not allocate a temporary NSString for every preceding function. */
  if(required!=requestedLength+1||memcmp(name,requestedName,requestedLength+1)) continue;
  id entry=[NSString stringWithUTF8String:name];if(!entry)return 0;
  TeguFunction *function=[[TeguFunction alloc] init];
  if(function) { function->library=[self retain];function->functionName=[entry retain];function->kind=kind; }
  return function;
 }
 tegu_function_lookup_missing(requestedName,requestedLength,self);
 return 0;
}
- (id)newFunctionWithName:(id)name constantValues:(id)values error:(id *)error {
 if(error)*error=0;
 TeguFunction *function=[self newFunctionWithName:name];
 if(!function){tegu_function_error(error,"function name not found");return 0;}
 id indexed=values?[values newIndexedConstantArray]:0,named=values?[values newNamedConstantArray]:0;
 NSUInteger counts[2]={[indexed count],[named count]};
 TeguAIRFunctionConstant *constants=0;TeguAIRLibrary *specialized=0;char message[512]={0};
 if(counts[0]>256||counts[1]>256-counts[0])goto fail;
 NSUInteger count=counts[0]+counts[1];
 constants=count?malloc(count*sizeof(*constants)):0;if(count&&!constants)goto fail;
 if(count) {
#ifdef TEGU_FRONTEND_HOST
  extern NSUInteger tegu_host_constant_data_size(NSUInteger);
  NSUInteger (*sizeOf)(NSUInteger)=tegu_host_constant_data_size;
#else
  void *metal=dlopen("/System/Library/Frameworks/Metal.framework/Metal",2);
  NSUInteger (*sizeOf)(NSUInteger)=metal?dlsym(metal,"_MTLConstantDataSize"):0;
#endif
  if(!sizeOf)goto fail;
  for(unsigned kind=0;kind<2;kind++)for(NSUInteger i=0;i<counts[kind];i++) {
   id value=[(kind?named:indexed) objectAtIndex:i];
   NSUInteger type=[value dataType],size=sizeOf(type);
   if(type>0xffffffffUL||!size||size>64||![value data])goto fail;
   TeguAIRFunctionConstant *c=&constants[(kind?counts[0]:0)+i];
   __builtin_memset(c,0,sizeof(*c));c->kind=kind;c->type=(uint32_t)type;c->size=(uint32_t)size;
   if(kind) {
    c->name=(const uint8_t *)[[value name] UTF8String];if(!c->name)goto fail;
    while(c->name_len<4096&&c->name[c->name_len])c->name_len++;
    if(!c->name_len||c->name_len==4096)goto fail;
   } else {NSUInteger index=[value index];if(index>0xffffffffUL)goto fail;c->index=(uint32_t)index;}
   memcpy(c->data,[value data],size);
  }
 }
 const char *entry=[function->functionName UTF8String];NSUInteger length=0;
 while(entry&&entry[length]&&length<4096)length++;
 /* Preserve the exact QuartzCore specialization values needed to reproduce
  * R80's fixed UI pair off-device. Keep this bounded per process and emit each
  * constant as one parseable record so concurrent specializations can be
  * grouped by serial. Render-trace builds only: 1400 records filled R171's
  * whole USB log ring before SpringBoard unlocked. */
#ifdef TEGU_RENDER_TRACE
 unsigned fixedIndex=entry&&!__builtin_strcmp(entry,"fixed_frag_lph_cph")?1:
  (entry&&!__builtin_strcmp(entry,"fixed_vert_lph_spc")?0:2);
 if(fixedIndex<2) {
  static unsigned emitted[2],serials;
  unsigned serial=__atomic_fetch_add(&serials,1,__ATOMIC_RELAXED);
  /* Lock-screen variants arrive after the first eight fragment specializations. */
  if(__atomic_fetch_add(&emitted[fixedIndex],1,__ATOMIC_RELAXED)<(fixedIndex?24u:8u)) {
   char header[192];int hn=snprintf(header,sizeof(header),
    "TEGU_FIXED_UI_FC_BEGIN entry=%s serial=%u defined_count=%lu\n",entry,serial,(unsigned long)count);
   if(hn>0)write(2,header,(NSUInteger)hn<sizeof(header)?(NSUInteger)hn:sizeof(header)-1);
   for(NSUInteger i=0;i<count;i++) {
    char line[384];TeguAIRFunctionConstant *c=&constants[i];
    size_t bytes=tegu_fixed_ui_constant_trace_format(line,sizeof(line),entry,serial,i,
     c->kind,c->index,c->name,c->name_len,c->type,c->data,c->size);
    if(bytes)write(2,line,bytes);
   }
   int en=snprintf(header,sizeof(header),"TEGU_FIXED_UI_FC_END entry=%s serial=%u\n",entry,serial);
   if(en>0)write(2,header,(NSUInteger)en<sizeof(header)?(NSUInteger)en:sizeof(header)-1);
  }
 }
#endif
 specialized=tegu_air_library_specialize(library,(const uint8_t *)entry,length,constants,count,message,sizeof(message));
 if(!specialized)goto fail;
 TeguLibrary *snapshot=[[TeguLibrary alloc] init];if(!snapshot)goto fail;
 snapshot->owner=[owner retain];snapshot->library=specialized;specialized=0;
 /* Specialization changes the parsed AIR library, but its immutable source
  * provenance remains the metallib supplied to the public loader.  Preserve
  * that identity so exact native packages can bind the specialized SPIR-V and
  * generated code to the original metallib instead of seeing an empty hash. */
 __builtin_memcpy(snapshot->sourceSHA256,sourceSHA256,sizeof(sourceSHA256));
 if(cacheIdentityValid) {
  tegu_pc_specialized_identity(cacheIdentity,entry,length,constants,count,snapshot->cacheIdentity);
  snapshot->cacheIdentityValid=1;snapshot->cacheReplay=tegu_pc_specialized_replay(entry,length,constants,count);
 }
 [function->library release];function->library=snapshot;
 free(constants);[indexed release];[named release];return function;
fail:
 tegu_air_library_close(specialized);free(constants);[indexed release];[named release];[function release];
 tegu_function_error(error,message[0]?message:"invalid or unsupported function constants");return 0;
}
- (id)newFunctionWithDescriptor:(id)descriptor error:(id *)error {
 if(error)*error=0;
 if(!descriptor||[descriptor options]){tegu_function_error(error,"unsupported function descriptor options");return 0;}
 TeguFunction *function=[self newFunctionWithName:[descriptor name] constantValues:[descriptor constantValues] error:error];
 if(function)function->displayName=[[descriptor specializedName] copy];
 return function;
}
- (void)newFunctionWithName:(id)name constantValues:(id)values
 completionHandler:(void (^)(id,NSError *))completionHandler {
 NSError *error=0;
 id function=[self newFunctionWithName:name constantValues:values error:&error];
 /* Completion is exactly-once. This bridge has no asynchronous compiler
  * queue, so the handler runs after the synchronous specialization result is
  * known; it still receives the same nil-or-function and NSError contract. */
 if(completionHandler)completionHandler(function,error);
 /* The handler receives a borrowed reference (Metal async contract); drop the
  * +1 from the synchronous new... call so each async creation does not leak. */
 [function release];
}
- (void)dealloc { tegu_air_library_close(library); free(cacheReplay); [libraryLabel release]; [owner release]; [super dealloc]; }
@end
@implementation TeguFunction
- (id)device { return library->owner; }
- (id)name { return displayName?displayName:functionName; }
- (id)label { return functionLabel; }
- (void)setLabel:(id)label { id value=[label copy];[functionLabel release];functionLabel=value; }
- (NSUInteger)functionType { return kind; }
- (void)dealloc { [functionLabel release];[displayName release];[functionName release]; [library release]; [super dealloc]; }
@end

#ifdef TEGU_MALI_BRIDGE
/* Runtime compute pipeline integration. No shader-name/fixture dispatch.
 * Each pipeline owns the AIR artifact and an eight-entry specialization cache.
 * Commands retain imported variants independently of cache eviction.
 * Compilation is serialized because the Mesa bridge uses shared type state.
 */
#include "tegu-air-profile.h"
static unsigned tegu_compiler_lock;
static void tegu_lock(unsigned *lock) {
 /* Spin, then yield: the compiler lock can now be held by the background
  * variant compiler for tens of ms (render_pipeline.inc tegu_vf_*). */
 unsigned spins=0;
 while(__sync_lock_test_and_set(lock,1)) {
  __asm__ volatile("" ::: "memory");
  if(++spins>=4096u){extern int sched_yield(void);extern int usleep(unsigned);if(spins<16384u)(void)sched_yield();else (void)usleep(50);}
 }
}
static void tegu_unlock(unsigned *lock) { __sync_lock_release(lock); }
static void tegu_compile_message(const char *message) {
 NSUInteger n=0;while(n<511&&message[n])n++;
 write(2,"TEGU_PIPELINE ",14);write(2,message,n);write(2,"\n",1);
}
#if defined(__APPLE__)
#define TEGU_MALI_V2_WEAK __attribute__((weak_import))
#elif defined(__clang__) || defined(__GNUC__)
#define TEGU_MALI_V2_WEAK __attribute__((weak))
#else
#define TEGU_MALI_V2_WEAK
#endif
extern int tegu_mali_compile_compute_v2(const void *,size_t,
 const struct tegu_mali_specialization *,size_t,struct tegu_mali_blob *,char *,size_t)
 TEGU_MALI_V2_WEAK;
@interface NSObject (TeguComputePipelineDescriptorInput)
- (id)computeFunction;
@end
#include "compute_descriptor.inc"
extern TeguAIRPipeline *tegu_air_pipeline_create_ex(const TeguAIRLibrary *,const uint8_t *,size_t,
 const TeguAIRComputeCreateOptions *,const uint8_t *,size_t,TeguAIRBlob *,TeguAIRBlob *,char *,size_t)
 TEGU_MALI_V2_WEAK;
/* ---- AIR-only compute code generation (air-compute-opus) ------------------
 * Every compute package (creation check, TGP1 dispatch variant, TGP2 region
 * variant) compiles from the kernel's AIR through tegu_mali_compile_compute_air
 * with the m2v metadata of the same translation; the m2v SPIR-V is no longer
 * compiled. TRANSITIONAL: m2v still translates at creation and per dispatch
 * variant, for the planner (bindings, push, region constants) only. A refusal
 * fails the pipeline/dispatch with TEGU_AIR_REFUSED (no m2v code fallback).
 * TEGU_AIR_COMPUTE=0 (env; else TEGU_AIR_DIRECT) or -DTEGU_AIR_COMPUTE_DEFAULT=0
 * restores the m2v SPIR-V compile for A/B. */
#ifndef TEGU_AIR_COMPUTE_DEFAULT
#define TEGU_AIR_COMPUTE_DEFAULT 1
#endif
extern char *getenv(const char *);
struct tegu_compute_air_source {
 TeguLibrary *library;char entry[256];const char *refused;
 struct tegu_mali_air_kernel_constant constants[64];uint32_t constantCount;
 char names[4096];size_t namesUsed;
 uint8_t *meta;size_t metaLength;
};
static U64 tegu_air_compute_ok,tegu_air_compute_refused;
/* Read at each pipeline creation (cheap next to a compile); a pipeline keeps
 * the mode it was created with: AIR iff it has an airSource. */
static int tegu_air_compute_enabled(void) {
 const char *e=getenv("TEGU_AIR_COMPUTE");if(!e||!e[0])e=getenv("TEGU_AIR_DIRECT");
 return e&&e[0]?e[0]!='0':TEGU_AIR_COMPUTE_DEFAULT;
}
static void tegu_compute_air_source_free(void *p) {
 struct tegu_compute_air_source *s=p;
 if(!s)return;
 [s->library release];free(s->meta);free(s);
}
/* Creation: the kernel's AIR module and specialized constants, and the m2v
 * metadata (taken over from the caller). Never fails the creation itself; a
 * missing source is a refusal when something is compiled. */
static struct tegu_compute_air_source *tegu_compute_air_source_new(TeguFunction *function,const char *name,
 TeguAIRBlob *metadata,const TeguComputeCreateState *createState) {
 struct tegu_compute_air_source *s=calloc(1,sizeof(*s));
 if(!s)return 0;
 if(!name||strlen(name)>=sizeof(s->entry)){s->refused="kernel name";return s;}
 snprintf(s->entry,sizeof(s->entry),"%s",name);
 /* An empty stageInputDescriptor (MPS passes one) changes nothing: m2v's
  * metadata is identical with and without it. */
 if(createState&&createState->used&&(createState->options.linked_count||createState->options.stage_attribute_count))
  s->refused="linked functions / stage input are not adapted to AIR yet";
 s->meta=metadata->bytes;s->metaLength=metadata->length;metadata->bytes=0;metadata->length=0;
 TeguAIRSourceConstant raw[64];size_t n=0;const uint8_t *bytes=0;size_t length=0;
 int rc=tegu_air_library_air_source(function->library->library,(const uint8_t *)name,strlen(name),&bytes,&length,raw,64,&n);
 if(rc||!bytes||!length){if(!s->refused)s->refused=rc==2?"function constant wider than 8 bytes":"no AIR source";return s;}
 for(size_t i=0;i<n;i++) {
  const char *cn=0;
  if(raw[i].name) {
   if(s->namesUsed+raw[i].name_len+1>sizeof(s->names)){if(!s->refused)s->refused="function constant names";return s;}
   memcpy(s->names+s->namesUsed,raw[i].name,raw[i].name_len);s->names[s->namesUsed+raw[i].name_len]=0;
   cn=s->names+s->namesUsed;s->namesUsed+=raw[i].name_len+1;
  }
  s->constants[i]=(struct tegu_mali_air_kernel_constant){cn,raw[i].index,raw[i].size,raw[i].bits};
 }
 s->constantCount=(uint32_t)n;
 s->library=[function->library retain];
 return s;
}
static void tegu_air_compute_trace(const struct tegu_compute_air_source *s,int rc,const char *why) {
 static unsigned traces;
 __atomic_fetch_add(&tegu_air_compute_refused,1,__ATOMIC_RELAXED);
 if(__atomic_fetch_add(&traces,1,__ATOMIC_RELAXED)>=64)return;
 char line[400];int n=snprintf(line,sizeof(line),"TEGU_AIR_REFUSED kernel %s rc=%d: %.300s\n",s&&s->entry[0]?s->entry:"?",rc,why&&why[0]?why:"?");
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
/* One compute package. AIR-only by default: `spirv` is used only by the m2v
 * opt-out. `meta` is the metadata of the translation whose plan is being
 * instantiated (the base pipeline's unless a dispatch variant). The caller holds
 * tegu_compiler_lock. */
static int tegu_compute_compile(TeguPipeline *pipeline,int v2,const unsigned char *spirv,NSUInteger spirvLength,
 const struct tegu_mali_specialization *constants,size_t count,const uint8_t *meta,size_t metaLength,
 struct tegu_mali_blob *package,char *message,size_t cap) {
 if(!pipeline->airSource) {
  if(v2)return tegu_mali_compile_compute_v2?tegu_mali_compile_compute_v2(spirv,(size_t)spirvLength,constants,count,package,message,cap):2;
  return tegu_mali_compile_specialized(spirv,(size_t)spirvLength,constants,count,package,message,cap);
 }
 const struct tegu_compute_air_source *s=pipeline->airSource;
 const char *why=!s?"no AIR source":s->refused?s->refused:!meta||!metaLength?"no m2v metadata for this translation":0;
 if(why){snprintf(message,cap,"air: %s",why);tegu_air_compute_trace(s,2,message);return 2;}
 const uint8_t *bytes=0;size_t length=0,unused=0;
 if(tegu_air_library_air_source(s->library->library,(const uint8_t *)s->entry,strlen(s->entry),&bytes,&length,NULL,0,&unused)==1||!bytes) {
  snprintf(message,cap,"air: no AIR source");tegu_air_compute_trace(s,2,message);return 2;
 }
 struct tegu_mali_air_compute air={TEGU_MALI_AIR_COMPUTE_VERSION,v2?TEGU_MALI_AIR_COMPUTE_V2:0u,bytes,length,s->entry,
  s->constants,s->constantCount,0,(const char *)meta,metaLength};
 int rc=tegu_mali_compile_compute_air(&air,constants,count,package,message,cap);
 /* the creation check tries TGP1 first and TGP2 next: its TGP1 refusal is not one */
 if(rc){if(v2||count)tegu_air_compute_trace(s,rc,message);}
 else __atomic_fetch_add(&tegu_air_compute_ok,1,__ATOMIC_RELAXED);
 return rc;
}
@interface TeguComputeDevice (RuntimePipelineCreate)
- (id)teguNewComputePipelineWithFunction:(TeguFunction *)function descriptor:(id)descriptor error:(id *)error;
@end
static void tegu_compute_pipeline_entry(const char *selectorName,TeguFunction *function,NSUInteger options) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=8)return;
 const char *name=tegu_function_owned_class(function)?[function->functionName UTF8String]:0;
 char line[256];int n=snprintf(line,sizeof(line),
  "TEGU_COMPUTE_PIPELINE_ENTRY selector=%s options=0x%lx owned=%u function=%s\n",
  selectorName,(unsigned long)options,name!=0,name?name:"(foreign-or-nil)");
 if(n>0)write(2,line,(NSUInteger)n<sizeof(line)?(NSUInteger)n:sizeof(line)-1);
}
/* Convert the encoder's 31 Metal threadgroup-memory lengths into the
 * planner's sparse u32 table. NULL means the legacy no-dynamic path. */
static int tegu_runtime_threadgroup_words(const NSUInteger *lengths,uint32_t words[31]) {
 int any=0;
 for(unsigned i=0;i<31;i++) {
  NSUInteger length=lengths?lengths[i]:0;
  if(length>32768||(length&&length%16))return -1;
  words[i]=(uint32_t)length;any|=length!=0;
 }
 return any;
}
#ifdef TEGU_INERT_PIPELINES
/* Compute counterpart of render's inert PSO policy. Only backboardd gets a
 * non-nil state for a failed compute pipeline creation; other processes and
 * flag-off builds keep the explicit nil/NSError result. The object carries no
 * compiled package and its dispatches are dropped with a bounded trace. */
static int tegu_compute_inert_pipeline_allowed(void) {
 return tegu_compositor_process_is_backboardd();
}
static void tegu_compute_inert_pipeline_trace(const char *selector,const char *reason) {
 static unsigned emitted;
 if(__atomic_fetch_add(&emitted,1,__ATOMIC_RELAXED)>=128)return;
 char line[320];int n=snprintf(line,sizeof(line),
  "TEGU_INERT_COMPUTE_PIPELINE selector=%.40s reason=%.220s\n",
  selector?selector:"compute-pipeline",reason&&reason[0]?reason:"unsupported compute pipeline");
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
static void tegu_compute_mark_inert(TeguPipeline *pipeline,const char *reason) {
 (void)reason;
 pipeline->inert=1;
 pipeline->info.local[0]=pipeline->info.local[1]=pipeline->info.local[2]=1;
#ifdef TEGU_MALI_BRIDGE
 tegu_air_pipeline_close(pipeline->compiled);pipeline->compiled=0;
 tegu_air_blob_free(&pipeline->spirv);
 pipeline->limits[0]=pipeline->limits[1]=pipeline->limits[2]=0;
#endif
}
static TeguPipeline *tegu_compute_create_inert(TeguComputeDevice *owner,id *error,
 const char *selector,const char *reason) {
 TeguPipeline *pipeline=[[TeguPipeline alloc] init];
 if(!pipeline)return 0;
 pipeline->owner=[owner retain];
 tegu_compute_mark_inert(pipeline,reason);
 tegu_compute_inert_pipeline_trace(selector,reason);
 if(error)*error=0;
 return pipeline;
}
#endif
@implementation TeguComputeDevice (RuntimePipelines)
- (id)newComputePipelineStateWithFunction:(TeguFunction *)function error:(id *)error {
 return [self teguNewComputePipelineWithFunction:function descriptor:0 error:error];
}
/* The pipeline creation shared by the function and descriptor entry points. A descriptor
 * with linked functions, stage input or a call stack depth goes through
 * tegu_air_pipeline_create_ex; any other input keeps the byte-identical plain create. */
- (id)teguNewComputePipelineWithFunction:(TeguFunction *)function descriptor:(id)descriptor error:(id *)error {
 if(error) *error=0;
 tegu_compute_pipeline_entry("function_error",function,0);
 if(!function||!tegu_function_owned_class(function)||function->library->owner!=self||function->kind!=3) {
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed())
   return tegu_compute_create_inert(self,error,"function_error","invalid or foreign compute function");
#endif
  if(error) *error=[NSError errorWithDomain:@"TeguComputePipeline" code:1 userInfo:0];
  return 0;
 }
 TeguPipeline *pipeline=[[TeguPipeline alloc] init];
 if(!pipeline) return 0;
 pipeline->owner=[self retain];
 const char *name=[function->functionName UTF8String];NSUInteger length=0;
 if(name) while(name[length]&&length<4096) length++;
 char message[512]={0};TeguAIRBlob metadata={0};
 TeguComputeCreateState createState;
 const char *optionsRefused=tegu_compute_create_state_init(self,descriptor,&createState);
 if(optionsRefused) {
  snprintf(message,sizeof(message),"compute pipeline descriptor: %s",optionsRefused);
 } else if(createState.used) {
  if(tegu_air_pipeline_create_ex)
   pipeline->compiled=tegu_air_pipeline_create_ex(function->library->library,(const unsigned char *)name,length,
    &createState.options,tegu_native_profile,sizeof(tegu_native_profile),&pipeline->spirv,&metadata,message,sizeof(message));
  else snprintf(message,sizeof(message),"this native compiler build has no linked-function/stage-input support");
 } else {
  pipeline->compiled=tegu_air_pipeline_create(function->library->library,(const unsigned char *)name,length,
   tegu_native_profile,sizeof(tegu_native_profile),&pipeline->spirv,&metadata,message,sizeof(message));
 }
 if(pipeline->compiled&&tegu_air_compute_enabled()) {
  pipeline->airSource=tegu_compute_air_source_new(function,name,&metadata,optionsRefused?0:&createState);
  if(!pipeline->airSource){snprintf(message,sizeof(message),"air: compute source allocation failed");
   tegu_air_pipeline_close(pipeline->compiled);pipeline->compiled=0;}
 }
 tegu_air_blob_free(&metadata);
 int failed=!pipeline->compiled;
 if(!failed) failed=tegu_air_pipeline_limits(pipeline->compiled,pipeline->limits);
 int dispatchable=failed?-1:tegu_air_pipeline_dispatchable(pipeline->compiled);
 if(dispatchable<0) failed=1;
 /* Validate ordinary lowering at pipeline creation. A storage-texture
  * stand-in exists only so Metal may create the state before resources are
  * bound; sending it to the backend would compile a guessed format. */
 if(!failed&&dispatchable) {
  struct tegu_mali_blob package={0};
  tegu_lock(&tegu_compiler_lock);
  const struct tegu_compute_air_source *air=pipeline->airSource;
  const uint8_t *meta=air?air->meta:0;size_t metaLength=air?air->metaLength:0;
  failed=tegu_compute_compile(pipeline,0,pipeline->spirv.bytes,pipeline->spirv.length,0,0,meta,metaLength,
   &package,message,sizeof(message));
  /* A kernel with texture/image resources cannot be represented by TGP1.
   * The explicit v2 entry is preferred only after the legacy entry refuses;
   * buffer-only kernels keep byte-identical TGP1 behavior. */
  if(failed&&(pipeline->airSource||tegu_mali_compile_compute_v2)) {
   tegu_mali_free(&package);
   failed=tegu_compute_compile(pipeline,1,pipeline->spirv.bytes,pipeline->spirv.length,0,0,meta,metaLength,
    &package,message,sizeof(message));
  }
  tegu_mali_free(&package);tegu_unlock(&tegu_compiler_lock);
 }
 if(failed) {
  tegu_compile_message(message);
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed()) {
   const char *reason=message[0]?message:"compute pipeline lowering/compilation refused";
   tegu_compute_mark_inert(pipeline,reason);
   tegu_compute_inert_pipeline_trace("function_error",reason);
   if(error)*error=0;
   return pipeline;
  }
#endif
  if(error) *error=[NSError errorWithDomain:@"TeguComputePipeline" code:2 userInfo:0];
  [pipeline release];return 0;
 }
 return pipeline;
}
- (id)newComputePipelineStateWithFunction:(TeguFunction *)function options:(NSUInteger)options reflection:(id *)reflection error:(id *)error {
 if(reflection)*reflection=0;if(error)*error=0;
 tegu_compute_pipeline_entry("function_options_reflection",function,options);
 /* Reflection contents are not implemented. Option zero has the same pipeline
  * contract as the public synchronous function/error entry. */
 if(options) {
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed())
   return tegu_compute_create_inert(self,error,"function_options_reflection",
    "compute reflection options are unsupported");
#endif
  if(error)*error=[NSError errorWithDomain:@"TeguComputePipeline" code:3 userInfo:0];return 0;
 }
 return [self newComputePipelineStateWithFunction:function error:error];
}
- (id)newComputePipelineStateWithDescriptor:(id)descriptor options:(NSUInteger)options reflection:(id *)reflection error:(id *)error {
 if(reflection)*reflection=0;if(error)*error=0;
 TeguFunction *function=descriptor?[descriptor computeFunction]:0;
 tegu_compute_pipeline_entry("descriptor_options_reflection",function,options);
 if(!descriptor||options) {
#ifdef TEGU_INERT_PIPELINES
  if(tegu_compute_inert_pipeline_allowed())
   return tegu_compute_create_inert(self,error,"descriptor_options_reflection",
    "compute pipeline descriptor or reflection options are unsupported");
#endif
  if(error)*error=[NSError errorWithDomain:@"TeguComputePipeline" code:4 userInfo:0];return 0;
 }
 return [self teguNewComputePipelineWithFunction:function descriptor:descriptor error:error];
}
@end
static int tegu_runtime_dispatch(TeguPipeline *pipeline,TeguBuffer **buffers,NSUInteger *offsets,
 const NSUInteger *threadgroupLengths,MTLSize groups,MTLSize local,struct Dispatch *job) {
 if(!local.width||!local.height||!local.depth||local.width>0xffffffffUL||local.height>0xffffffffUL||local.depth>0xffffffffUL) return 0;
 uint32_t lengths[31];int any=tegu_runtime_threadgroup_words(threadgroupLengths,lengths);
 if(any<0)return 0;
 TeguAIRBufferBinding bindings[31];unsigned count=0;
 for(unsigned i=0;i<31;i++) if(buffers[i]) {
  bindings[count++]=(TeguAIRBufferBinding){i,0,buffers[i]->logical,offsets[i]};
 }
 uint32_t grid[3]={(uint32_t)groups.width,(uint32_t)groups.height,(uint32_t)groups.depth};
 uint32_t threads[3]={(uint32_t)local.width,(uint32_t)local.height,(uint32_t)local.depth};
 TeguAIRNativeDispatch plan;char message[512]={0};
 if(tegu_air_pipeline_native_dispatch_tg(pipeline->compiled,bindings,count,any?lengths:NULL,
    grid,threads,&plan,message,sizeof(message))) {
  tegu_compile_message(message);return 0;
 }
 if(plan.version!=1||plan.constant_count>64||plan.bindings_mask&~255u) return 0;
 for(unsigned i=0;i<8;i++) if(plan.bindings_mask&(1u<<i)) {
  TeguAIRNativeBinding *b=&plan.bindings[i];
  if(b->index>=31||!buffers[b->index]||b->offset>0xffffffffUL||b->length>0xffffffffUL) return 0;
  TeguBuffer *buffer=buffers[b->index];
  U64 padded=(buffer->logical+15ULL)&~15ULL;
  if(buffer->owner!=pipeline->owner||b->offset>=padded||b->length>padded-b->offset||
     b->offset>=buffer->mapped||b->length>buffer->mapped-b->offset) return 0;
 }
 TeguPipeline *selected=0;
 tegu_lock(&pipeline->cacheLock);
 for(unsigned i=0;i<8;i++) {
  struct TeguVariant *v=&pipeline->variants[i];
  if(v->pipeline&&v->count==plan.constant_count&&
     !__builtin_memcmp(v->constants,plan.constants,plan.constant_count*sizeof(plan.constants[0]))) {
   selected=[v->pipeline retain];break;
  }
 }
 if(!selected) {
  struct tegu_mali_specialization constants[64];
  for(unsigned i=0;i<plan.constant_count;i++) {
   constants[i].id=plan.constants[i].id;constants[i].size=plan.constants[i].size;
   __builtin_memcpy(constants[i].data,plan.constants[i].data,8);
  }
  struct tegu_mali_blob package={0};
  tegu_lock(&tegu_compiler_lock);
  const struct tegu_compute_air_source *air=pipeline->airSource;
  int failed=tegu_compute_compile(pipeline,0,pipeline->spirv.bytes,pipeline->spirv.length,
   constants,plan.constant_count,air?air->meta:0,air?air->metaLength:0,&package,message,sizeof(message));
  tegu_unlock(&tegu_compiler_lock);
  TeguPipeline *variant=failed?0:[pipeline->owner newTeguPipelineWithPackage:package.data length:package.size];
  tegu_mali_free(&package);
  if(variant) {
   struct TeguVariant *v=&pipeline->variants[pipeline->nextVariant++%8];
   [v->pipeline release];v->pipeline=variant;v->count=plan.constant_count;
   __builtin_memcpy(v->constants,plan.constants,plan.constant_count*sizeof(plan.constants[0]));
   selected=[variant retain];
  } else tegu_compile_message(message);
 }
 tegu_unlock(&pipeline->cacheLock);
 if(!selected) return 0;
 if((selected->info.bindings&~plan.bindings_mask)||selected->info.local[0]!=local.width||
    selected->info.local[1]!=local.height||selected->info.local[2]!=local.depth) { [selected release];return 0; }
 job->pipeline=selected;
 job->wire[0]=1;for(unsigned i=0;i<3;i++) job->wire[i+1]=plan.groups[i];
 __builtin_memcpy(&job->wire[36],plan.push,128);
 for(unsigned i=0;i<8;i++) if(selected->info.bindings&(1u<<i)) {
  TeguAIRNativeBinding *b=&plan.bindings[i];TeguBuffer *buffer=buffers[b->index];
  job->buffers[i]=[buffer retain];
  job->wire[4+4*i]=(unsigned)buffer->handle;
  job->wire[5+4*i]=(unsigned)b->offset;job->wire[6+4*i]=(unsigned)b->length;

 }
 return 1;
}
/* ---- v2 compute resources ------------------------------------------------
 * Native identity is the SPIR-V set0 binding N: table1[N] textures/images,
 * table2[N] samplers, FAU slot N for buffers 0..7, dummy sampler table2[256].
 * The object array is opaque here; the caller supplies validated Metal facts,
 * the retained objects and the backing token for each resource. */
#define TEGU_V2_KIND_UBO 1u
#define TEGU_V2_KIND_SSBO 2u
#define TEGU_V2_KIND_TEXTURE 3u
#define TEGU_V2_KIND_IMAGE 4u
#define TEGU_V2_KIND_SAMPLER 5u
#define TEGU_V2_KIND_COMBINED 6u
typedef int (*tegu_v2_compile_fn)(const void *,size_t,const struct tegu_mali_specialization *,
 size_t,struct tegu_mali_blob *,char *,size_t);
static void tegu_v2_put64(unsigned char *p,uint64_t v)
{
 tegu_pipeline_put(p,(uint32_t)v);tegu_pipeline_put(p+4,(uint32_t)(v>>32));
}
static const TeguAIRComputeResource *tegu_v2_find(const TeguAIRComputeResource *r,unsigned n,
 unsigned kind,unsigned index)
{
 for(unsigned i=0;i<n;i++)if(r[i].kind==kind&&r[i].index==index)return &r[i];
 return 0;
}
static const struct tegu_pipeline_manifest *tegu_v2_manifest(
 const struct tegu_pipeline_info2 *m,unsigned table,unsigned entry)
{
 for(unsigned i=0;i<m->manifest_count;i++)
  if(m->manifest[i].table==table&&m->manifest[i].entry==entry)return &m->manifest[i];
 return 0;
}
/* Exact correspondence between the package manifest and the selected plan.
 * A combined wire descriptor requires both table1[N] and table2[N] records. */
static int tegu_v2_manifest_match(const struct tegu_pipeline_info2 *m,
 const TeguAIRNativeDispatchV2 *plan,char *message,NSUInteger cap)
{
 unsigned expected=0,required=m->manifest_count;
 if(m->flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER) {
  if(!required)return 0;
  required--;
 }
 for(unsigned d=0;d<plan->descriptor_count;d++)expected+=plan->descriptors[d].kind==TEGU_V2_KIND_COMBINED?2u:1u;
 if(expected!=required) {
  snprintf(message,(size_t)cap,"compute v2 manifest count %u does not cover plan records %u",required,expected);
  return 0;
 }
 for(unsigned d=0;d<plan->descriptor_count;d++) {
  const TeguAIRComputeDescriptor *w=&plan->descriptors[d];
  unsigned binding=w->binding;const struct tegu_pipeline_manifest *rec=0;
  if(w->kind<=TEGU_V2_KIND_SSBO) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_FAU,binding);
   if(!rec||rec->kind!=w->kind||rec->binding!=binding){snprintf(message,(size_t)cap,"compute v2 buffer manifest mismatch at binding %u",binding);return 0;}
  } else if(w->kind==TEGU_V2_KIND_TEXTURE||w->kind==TEGU_V2_KIND_IMAGE) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_TEXTURE,binding);
   if(!rec||rec->kind!=w->kind||rec->binding!=binding||
      (rec->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)){snprintf(message,(size_t)cap,"compute v2 texture/image manifest mismatch at binding %u",binding);return 0;}
  } else if(w->kind==TEGU_V2_KIND_SAMPLER) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_SAMPLER,binding);
   if(!rec||rec->kind!=TEGU_V2_KIND_SAMPLER||rec->binding!=binding||rec->access!=0||
      (rec->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
      (((rec->flags&TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE)!=0)!=((w->sampler_flags&1u)!=0))){snprintf(message,(size_t)cap,"compute v2 sampler manifest mismatch at binding %u",binding);return 0;}
  } else if(w->kind==TEGU_V2_KIND_COMBINED) {
   rec=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_TEXTURE,binding);
   const struct tegu_pipeline_manifest *s=tegu_v2_manifest(m,TEGU_PIPELINE2_TABLE_SAMPLER,binding);
   if(!rec||rec->kind!=TEGU_V2_KIND_TEXTURE||rec->binding!=binding||
      !(rec->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||!s||
      s->kind!=TEGU_V2_KIND_SAMPLER||s->binding!=binding||s->access!=0||
      !(s->flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED)||
      (((s->flags&TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE)!=0)!=((w->sampler_flags&1u)!=0))){
    snprintf(message,(size_t)cap,"compute v2 combined manifest mismatch at binding %u",binding);return 0;
   }
  } else {snprintf(message,(size_t)cap,"compute v2 unknown descriptor kind");return 0;}
  unsigned need=rec->access&(TEGU_AIR_COMPUTE_ACCESS_READ|TEGU_AIR_COMPUTE_ACCESS_WRITE|
   TEGU_AIR_COMPUTE_ACCESS_SAMPLE|TEGU_AIR_COMPUTE_ACCESS_STORAGE|0x1000u);
  if((w->flags&need)!=need){snprintf(message,(size_t)cap,"compute v2 plan lacks manifest access at binding %u",binding);return 0;}
 }
 return 1;
}
static TeguPipeline *tegu_v2_variant(TeguPipeline *pipeline,const TeguAIRNativeDispatchV2 *plan,
 unsigned region,const unsigned char *spirv,NSUInteger spirv_length,tegu_v2_compile_fn compile,
 unsigned require_v2,char *message,NSUInteger cap)
{
 TeguPipeline *selected=0;int valid=1;
 tegu_lock(&pipeline->cacheLock);
 for(unsigned i=0;i<8;i++) {
  struct TeguVariant *v=&pipeline->variants[i];
  if(v->pipeline&&v->count==plan->regions[region].constant_count&&
     (!require_v2||v->pipeline->packageVersion==2)&&
     !__builtin_memcmp(v->variantHash,plan->variant_hash,32)&&
     !__builtin_memcmp(v->constants,plan->regions[region].constants,
       plan->regions[region].constant_count*sizeof(plan->regions[region].constants[0]))) {
   selected=[v->pipeline retain];break;
  }
 }
 if(!selected) {
  struct tegu_mali_blob package={0};
  tegu_lock(&tegu_compiler_lock);
  /* AIR: the base metadata describes this plan when the dispatch did not
   * select a translator variant (spirv is then the base pipeline's); otherwise
   * the bridge returns the variant's own metadata. */
  const struct tegu_compute_air_source *air=pipeline->airSource;
  int base=spirv==pipeline->spirv.bytes;
  int failed=!spirv||!spirv_length||!compile;
  if(!failed&&pipeline->airSource) {
   TeguAIRBlob variantMeta={0};
   const uint8_t *meta=air?air->meta:0;size_t metaLength=air?air->metaLength:0;
   if(!base) {
    /* the translator variant this plan belongs to (same SPIR-V words) */
    int vm=tegu_air_pipeline_variant_metadata(pipeline->compiled,spirv,(size_t)spirv_length,&variantMeta);
    meta=vm?0:variantMeta.bytes;metaLength=vm?0:variantMeta.length;
   }
   failed=tegu_compute_compile(pipeline,require_v2?1:0,spirv,spirv_length,
    (const struct tegu_mali_specialization *)plan->regions[region].constants,plan->regions[region].constant_count,
    meta,metaLength,&package,message,(size_t)cap);
   tegu_air_blob_free(&variantMeta);
  } else if(!failed) failed=compile(spirv,(size_t)spirv_length,
    (const struct tegu_mali_specialization *)plan->regions[region].constants,
    plan->regions[region].constant_count,&package,message,(size_t)cap);
  tegu_unlock(&tegu_compiler_lock);
  TeguPipeline *variant=failed?0:[pipeline->owner newTeguPipelineWithPackage:package.data length:package.size];
  tegu_mali_free(&package);
  if(!variant){tegu_unlock(&pipeline->cacheLock);return 0;}
  struct TeguVariant *slot=&pipeline->variants[pipeline->nextVariant++%8];
  [slot->pipeline release];slot->pipeline=[variant retain];slot->count=plan->regions[region].constant_count;
  __builtin_memcpy(slot->variantHash,plan->variant_hash,32);
  __builtin_memcpy(slot->constants,plan->regions[region].constants,
   plan->regions[region].constant_count*sizeof(plan->regions[region].constants[0]));
  selected=variant;
 }
 if(require_v2&&selected->packageVersion!=2) {
  snprintf(message,(size_t)cap,"Mali compiler did not emit TGP2 for a texture/sampler/image dispatch");
  valid=0;
 } else if(selected->packageVersion==2) {
  valid=tegu_v2_manifest_match(&selected->info2,plan,message,cap);
 } else {
  unsigned planned=0;
  for(unsigned d=0;d<plan->descriptor_count;d++)if(plan->descriptors[d].kind<=TEGU_V2_KIND_SSBO)
   planned|=1u<<plan->descriptors[d].binding;
  if(planned!=selected->info.bindings){
   snprintf(message,(size_t)cap,"compute v2 buffer-only TGP1 package binding mask differs from its plan");
   valid=0;
  }
 }
 if(!valid){[selected release];selected=0;}
 tegu_unlock(&pipeline->cacheLock);
 return selected;
}
static int __attribute__((unused)) tegu_runtime_dispatch_v2(TeguPipeline *pipeline,const TeguAIRComputeResource *resources,
 id *objects,const uint64_t *tokens,const NSUInteger *threadgroupLengths,unsigned count,unsigned mode,
 MTLSize extents,MTLSize local,TeguCommand *command,char *message,NSUInteger messageCapacity)
{
 if(!pipeline||!pipeline->compiled||!resources||!objects||!tokens||count>32||mode>1||!command)return 0;
 uint32_t lengths[31];int any=tegu_runtime_threadgroup_words(threadgroupLengths,lengths);
 if(any<0)return 0;
 uint32_t grid[3]={(uint32_t)extents.width,(uint32_t)extents.height,(uint32_t)extents.depth};
 uint32_t threads[3]={(uint32_t)local.width,(uint32_t)local.height,(uint32_t)local.depth};
 TeguAIRBlob variant={0};TeguAIRNativeDispatchV2 plan;
 int status=tegu_air_pipeline_native_dispatch_v2_tg(pipeline->compiled,resources,count,any?lengths:NULL,
  mode,grid,threads,&variant,&plan,message,(size_t)messageCapacity);
 if(status){tegu_air_blob_free(&variant);return 0;}
 const unsigned char *spirv=variant.bytes?variant.bytes:pipeline->spirv.bytes;
 NSUInteger spirv_length=variant.bytes?variant.length:pipeline->spirv.length;
 /* This path always emits a TDG2 wire, which the bridge accepts only for a
  * TGP2 package, so every variant selected here is compiled with the v2 entry. */
 int require_v2=1;
 tegu_v2_compile_fn compile=tegu_mali_compile_specialized;
 if(require_v2) {
  if(!tegu_mali_compile_compute_v2){
   snprintf(message,(size_t)messageCapacity,"compiler v2 entry unavailable (tegu_mali_compile_compute_v2); v2 dispatch refused");
   goto fail;
  }
  compile=tegu_mali_compile_compute_v2;
 }
 for(unsigned r=0;r<plan.region_count;r++) {
  if(command->count>=TEGU_COMMAND_MAX_JOBS){snprintf(message,(size_t)messageCapacity,"compute v2 command capacity");goto fail;}
  TeguPipeline *selected=tegu_v2_variant(pipeline,&plan,r,spirv,spirv_length,compile,
   (unsigned)require_v2,message,messageCapacity);
  if(!selected)goto fail;
  if(selected->info.local[0]!=plan.regions[r].local[0]||
     selected->info.local[1]!=plan.regions[r].local[1]||
     selected->info.local[2]!=plan.regions[r].local[2]) {
   snprintf(message,(size_t)messageCapacity,"compute v2 package local size differs from the specialized region local");
   [selected release];goto fail;
  }
  unsigned char *wire=malloc(TEGU_PIPELINE_DISPATCH2_BYTES);
  if(!wire){snprintf(message,(size_t)messageCapacity,"compute v2 wire allocation failed");[selected release];goto fail;}
  __builtin_memset(wire,0,TEGU_PIPELINE_DISPATCH2_BYTES);
  tegu_pipeline_put(wire+0,TEGU_PIPELINE_DISPATCH2_MAGIC);
  tegu_pipeline_put(wire+4,TEGU_PIPELINE_DISPATCH2_VERSION);
  tegu_pipeline_put(wire+8,plan.descriptor_count);
  for(unsigned a=0;a<3;a++)tegu_pipeline_put(wire+12+4*a,plan.regions[r].groups[a]);
  tegu_pipeline_put(wire+24,selected->info.push_words);
  int bad=0;
  for(unsigned d=0;d<plan.descriptor_count&&!bad;d++) {
   const TeguAIRComputeDescriptor *desc=&plan.descriptors[d];
   unsigned char *w=wire+TEGU_PIPELINE_DISPATCH2_DESC_OFFSET+d*TEGU_PIPELINE_DISPATCH2_DESC_BYTES;
   unsigned rk=desc->kind<=2?TEGU_AIR_COMPUTE_BUFFER:
    (desc->kind<=4||desc->kind==TEGU_V2_KIND_COMBINED)?TEGU_AIR_COMPUTE_TEXTURE:TEGU_AIR_COMPUTE_SAMPLER;
   unsigned at=count;
   for(unsigned i=0;i<count;i++)if(resources[i].kind==rk&&resources[i].index==desc->metal_index){at=i;break;}
   if(at==count){snprintf(message,(size_t)messageCapacity,"compute v2 binding %u has no resource",desc->binding);bad=1;break;}
   const TeguAIRComputeResource *res=&resources[at];
   if(desc->kind!=TEGU_V2_KIND_SAMPLER&&!tokens[at]){snprintf(message,(size_t)messageCapacity,"compute v2 binding %u has no backing token",desc->binding);bad=1;break;}
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_BINDING,desc->binding);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_KIND,desc->kind);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_METAL_INDEX,desc->metal_index);
   uint32_t dflags=desc->flags;
   if(res->swizzle_valid)dflags|=TEGU_PIPELINE2_DESC_SWIZZLE_VALID;
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_FLAGS,dflags);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SAMPLER_INDEX,desc->sampler_index);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SAMPLER_FLAGS,desc->sampler_flags);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_TOKEN,tokens[at]);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_OFFSET,desc->range_offset);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_SPAN,desc->range_length);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_FORMAT,res->format);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_FORMAT,desc->view_format);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_DIM,res->texture_type);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_WIDTH,res->width);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_HEIGHT,res->height);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_DEPTH,res->depth);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_LAYERS,res->array_layers);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_LEVELS,res->mipmap_levels);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SAMPLES,res->sample_count);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_USAGE,res->usage);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_ROW_STRIDE,res->row_stride);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_STORAGE_MODE,res->storage_mode);
   tegu_v2_put64(w+TEGU_PIPELINE_D2_LEVEL_OFFSET,0);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SLICE_STRIDE,res->slice_stride);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_LAYER_STRIDE,res->layer_stride);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_SWIZZLE,res->swizzle_word);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_LEVEL,desc->view_level);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_LAYER,desc->view_layer);
   tegu_pipeline_put(w+TEGU_PIPELINE_D2_VIEW_FORMAT_VK,desc->view_format_vk);
   if(desc->kind==TEGU_V2_KIND_SAMPLER||desc->kind==TEGU_V2_KIND_COMBINED) {
    unsigned si=desc->kind==TEGU_V2_KIND_SAMPLER?desc->metal_index:desc->sampler_index;
    const TeguAIRComputeResource *s=tegu_v2_find(resources,count,TEGU_AIR_COMPUTE_SAMPLER,si);
    if(!s){snprintf(message,(size_t)messageCapacity,"compute v2 sampler %u missing",si);bad=1;break;}
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MIN_FILTER,s->min_filter);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MAG_FILTER,s->mag_filter);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MIP_FILTER,s->mip_filter);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_ADDRESS_S,s->address_s);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_ADDRESS_T,s->address_t);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_ADDRESS_R,s->address_r);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_BORDER_COLOR,s->border_color);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_NORMALIZED,s->normalized_coordinates);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_COMPARE,s->compare_function);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_MAX_ANISOTROPY,s->max_anisotropy);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_REDUCTION,s->reduction_mode);
    tegu_pipeline_put(w+TEGU_PIPELINE_D2_LOD_AVERAGE,s->lod_average);
    __builtin_memcpy(w+TEGU_PIPELINE_D2_LOD_MIN,&s->lod_min_clamp,4);
    __builtin_memcpy(w+TEGU_PIPELINE_D2_LOD_MAX,&s->lod_max_clamp,4);
   }
  }
  if(bad){free(wire);[selected release];goto fail;}
  __builtin_memcpy(wire+TEGU_PIPELINE_DISPATCH2_PUSH_OFFSET,plan.regions[r].push,128);
  struct Dispatch *job=tegu_command_job(command,command->count);
  job->pipeline=[selected retain];job->wire2=wire;job->wire2Bytes=TEGU_PIPELINE_DISPATCH2_BYTES;
  for(unsigned d=0;d<plan.descriptor_count;d++) {
   const TeguAIRComputeDescriptor *desc=&plan.descriptors[d];
   unsigned rk=desc->kind<=2?TEGU_AIR_COMPUTE_BUFFER:
    (desc->kind<=4||desc->kind==TEGU_V2_KIND_COMBINED)?TEGU_AIR_COMPUTE_TEXTURE:TEGU_AIR_COMPUTE_SAMPLER;
   for(unsigned i=0;i<count;i++)if(resources[i].kind==rk&&resources[i].index==desc->metal_index) {
    id object=objects[i];
    if(rk==TEGU_AIR_COMPUTE_BUFFER) {
     if(!job->v2Buffers[desc->metal_index])job->v2Buffers[desc->metal_index]=[object retain];
    } else if(rk==TEGU_AIR_COMPUTE_TEXTURE) {
     if(!job->v2Textures[desc->metal_index])job->v2Textures[desc->metal_index]=[object retain];
    } else if(!job->v2Samplers[desc->metal_index])job->v2Samplers[desc->metal_index]=[object retain];
    break;
   }
   if(desc->kind==TEGU_V2_KIND_COMBINED&&!job->v2Samplers[desc->sampler_index]) {
    for(unsigned i=0;i<count;i++)if(resources[i].kind==TEGU_AIR_COMPUTE_SAMPLER&&
       resources[i].index==desc->sampler_index){job->v2Samplers[desc->sampler_index]=[objects[i] retain];break;}
   }
  }
  command->count++;
  [selected release];
 }
 tegu_air_blob_free(&variant);
 return 1;
fail:
 tegu_air_blob_free(&variant);
 return 0;
}

#ifdef TEGU_GRAPHICS_BRIDGE
#include "sampler_state.inc"
#include "render_pipeline.inc"
#include "render_encoder.inc"
#endif
#endif

#ifdef TEGU_AIR_FIXTURE
#include "tegu-air-fixture.h"
#ifdef TEGU_MALI_BRIDGE
#include "runtime_pipeline_probe.inc"
#endif
int tegu_library_frontend_probe(void) {
 MARK("library_begin dispatch_data");
 TeguComputeDevice *device=[[TeguComputeDevice alloc] init];
 void *sys=dlopen("/usr/lib/libSystem.B.dylib",2);
 id (*create)(const void *,NSUInteger,void *,void *)=sys?dlsym(sys,"dispatch_data_create"):0;
 void (*dispose)(id)=sys?dlsym(sys,"dispatch_release"):0;
 if(!device||!create||!dispose) { [device release]; MARK("FAIL library_device_or_dispatch"); return 1; }
 id data=create(tegu_air_fixture,sizeof(tegu_air_fixture),0,0),error=0;
 TeguLibrary *library=[device newLibraryWithData:data error:&error];
 if(data) dispose(data);
 TeguFunction *function=[library newFunctionWithName:@"scale_and_offset"];
 int ok=library&&function&&!error&&[function functionType]==3&&[function device]==device;
 id missing=[library newFunctionWithName:@"tegu_missing_function"];
 if(missing) { ok=0; [missing release]; }
 [library release]; // function must keep library alive
 if(function&&![[function name] isEqualToString:@"scale_and_offset"]) ok=0;
 [function release];
 error=0;
 id invalid=[device newLibraryWithData:0 error:&error];
 if(invalid||!error) ok=0;
 [invalid release];[device release];
 if(ok) MARK("PASS library_dispatch_data function_lookup retained_lifetime invalid_input");
 else MARK("FAIL library_dispatch_data_or_function");
 return ok?0:1;
}
#endif

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
#include <time.h>
#define COUNT 600
struct buffer {uint64_t va,size;unsigned char *data;unsigned mapped,active,released,exec,gpu_only,surface;};
struct fake {
 unsigned last_chain,last_preframe,last_preframe1,last_dcd_rt[2];
 uint32_t last_fmin,last_fmax; /* FRAGMENT_JOB bound words (tiles) */
 pthread_mutex_t mutex;struct buffer buffers[COUNT];uint32_t next;
 unsigned live,maps,closed,submitted,waited,bad_state,fail_alloc,fail_map,fail_submit,fail_wait,fail_release;
 unsigned geometry_fixture,heap_logs,before_logs,after_logs,log_bytes,nonzero_before_logs;
 unsigned sync_calls,fail_sync_at;
 /* kernel 0112 model (selectors 13/14); off = a pre-0112 kernel (Unsupported) */
 unsigned resv_model,resv_calls,resv_last_n,retire_calls,resv_order_ok;uint64_t resv_next,retire_last[2];uint32_t resv_handles[8];
 /* chained submission model: verify the multi-frame chains of one job */
 unsigned chain_model,last_frames,last_tjobs;uint64_t last_heaps[8];
 unsigned handle_limit; /* per-context handle cap (P8 256, P9 1024); 0 = unlimited */
 /* tiler matrix model */
 unsigned render_model,tm_logs,adopt_logs,draw_submits,heap_allocs,const_fs_seen,nosec_seen,mask_seen[4];
 uint64_t heap_va,last_cmd_va;uint32_t last_mask,last_chunk,last_heap_size;char adopt_text[240];
 uint64_t job;unsigned pending;const unsigned char *pipeline;size_t pipeline_size;
 /* kernel 0090 model: WAIT_SEQ, busy-consumed release, in-flight window */
 unsigned seq_model,seq_waits,inflight,window,fail_seq,busy_releases,max_inflight;
 /* Optional device time model (frame_bench FB_SUBMIT_US/FB_GPU_US; 0 = off):
  * every SUBMIT_JM takes submit_us of wall time (job preparation) and queues
  * gpu_us of GPU time behind the previous job; WAIT and WAIT_SEQ sleep
  * until the waited job's simulated completion. */
 unsigned submit_us,gpu_us;double gpu_free,done_at[256];
 unsigned seq0_notfound; /* pre-0108 kernel: WAIT_SEQ(0) = kIOReturnNotFound */
 /* kernel 0096 model: ALLOC bit1 GPU-only (no_gpu_only = older kernel that
  * refuses it), and imported surfaces with a separate CPU view that only an
  * acquire (SYNC to CPU) refreshes: data = what the GPU sees/wrote,
  * surf_cpu[sid] = the CPU's (possibly stale) lines. */
 unsigned no_gpu_only,gpu_only_allocs,refused_maps,surf_model,surf_acquires;
 unsigned char *surf_cpu[4];size_t surf_bytes[4];
 /* fault diagnostics: SEL_FAULT model (ctx MMU fault) and the bridge's NORD2_METAL fault lines */
 unsigned fault_queries,fault_lines;uint32_t mmu_status,js_status;uint64_t mmu_address;char fault_text[240];
 unsigned lost_calls,fault_wait; /* n2mb_ops.lost calls; WAIT answers FAULTED (5) for the next job */
};
#define KBUSY_TEST ((int)0xe00002d5u)
static unsigned fake_chain_env(void){static int v=-1;
 if(v<0){const char *e=getenv("NORD2_METAL_CHAIN");v=e&&(e[0]=='1'||e[0]=='2')&&!e[1];}return (unsigned)v;}
static unsigned fake_bo_of(struct fake *f,const struct nord2_gpu_submit_request *r,uint64_t va){
 for(unsigned k=0;k<r->nr_handles;k++){struct buffer *b=&f->buffers[r->handles[k]];if(va>=b->va&&va<b->va+b->size)return r->handles[k];}
 assert(0);return 0;}
/* Chained-submission model: the listed BO holding a GPU address. */
static const unsigned char *fake_at(struct fake *f,const struct nord2_gpu_submit_request *r,uint64_t va){
 for(unsigned k=0;k<r->nr_handles;k++){struct buffer *b=&f->buffers[r->handles[k]];
  if(va>=b->va&&va+64<=b->va+b->size)return b->data+(va-b->va);}
 assert(0);return NULL;
}

static double fake_now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec+(double)t.tv_nsec*1e-9;}
static void fake_timed_submit(struct fake *f) {
 /* Wall time, not a spin: the host runs in a CPU-quota slice beside other jobs. */
 if(f->submit_us){struct timespec d={0,(long)f->submit_us*1000L};nanosleep(&d,0);}
 if(!f->gpu_us)return;
 double now=fake_now(),start=f->gpu_free>now?f->gpu_free:now;
 f->gpu_free=start+f->gpu_us*1e-6;f->done_at[f->job&255u]=f->gpu_free;
}
static void fake_wait_job(struct fake *f,uint64_t job) {
 if(!f->gpu_us||!job)return;
 double until=f->done_at[job&255u],now;
 while((now=fake_now())<until){struct timespec d={0,(long)((until-now)*1e9)+1};nanosleep(&d,0);}
}
static uint64_t get64(const void *v) {const unsigned char *p=v;return tegu_pipeline_word(p)|((uint64_t)tegu_pipeline_word(p+4)<<32);}
static void collect(struct fake *f,unsigned h) {struct buffer *b=&f->buffers[h];assert(b->active);free(b->data);b->data=NULL;b->active=0;f->live--;}
/* MALI_FRAMEBUFFER_POINTER: bit0 set, render_target_count-1 in bits 2..4
 * (arch9 FRAMEBUFFER_POINTER); single-target jobs are exactly va|1. */
static int rtc_ok(uint64_t p){return (p&1)&&((p>>2)&7)<4;}
static int call(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os) {
 struct fake *f=p;
 switch(s) {
 case NORD2_GPU_SEL_LOG:
  /* Plain log writes remain valid when a wait failed; no GPU memory read. */
  assert(!ni&&bytes&&size>0&&size<240);
  if(size>=17&&!memcmp(bytes,"NORD2_METAL heap+",17))f->heap_logs++;
  f->log_bytes+=(unsigned)size;assert(f->log_bytes<=16384);
  if(size>=18&&!memcmp(bytes,"NORD2_METAL fault ",18)){f->fault_lines++;memcpy(f->fault_text,bytes,size);f->fault_text[size-1]=0;}
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
  if(f->handle_limit&&f->live>=f->handle_limit)return (int)0xe00002beu; /* N2C_E_NO_HANDLE -> kIOReturnNoResources */
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
   if(f->chain_model||fake_chain_env()){
    /* JS1: tiler jobs 1..n, each depending on its predecessor; JS0: one
     * fragment job per frame, 1..F, barrier and dependency on the previous;
     * every frame's tiler heap (FBD -> its frame block -> TILER_HEAP base)
     * distinct. All addresses inside listed BOs. */
    unsigned steps=0;
    unsigned frame_bo=0,frame_steps=0;
    for(uint64_t h=r->heads[0];h;steps++){const unsigned char *jh=fake_at(f,r,h);{unsigned bo=fake_bo_of(f,r,h);if(bo!=frame_bo){frame_bo=bo;frame_steps=0;}frame_steps++;}uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
     assert((w4>>16)==steps+1&&(w5&0xffff)==steps&&steps<4096);h=get64(jh+24);}
    unsigned k=0;
    for(uint64_t h=r->heads[1];h;k++){const unsigned char *jh=fake_at(f,r,h);uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
     assert((w4>>16)==k+1&&((w4>>8)&1)==(k>0)&&(w5&0xffff)==k&&((w4>>1)&0x7f)==9&&k<8);
     uint64_t fbp=get64(jh+40)&~63ull;const unsigned char *fr=fake_at(f,r,fbp);uint64_t base=get64(fr+768+8);
     for(unsigned j=0;j<k;j++)assert(f->last_heaps[j]!=base);f->last_heaps[k]=base;h=get64(jh+24);}
    f->last_tjobs=steps;f->last_frames=k;f->draw_submits++;f->last_chain=frame_steps;
    goto accept_render;
   }
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
   {uint64_t fbp=get64(c->data+N2R_FRAGMENT+40); /* tagged pointer: low bits carry the RT count */
    assert((fbp&~63ull)==c->va&&(fbp&63u)&&((fbp&63u)==1||rtc_ok(fbp)));}
   memcpy(&f->last_fmin,c->data+N2R_FRAGMENT+32,4);memcpy(&f->last_fmax,c->data+N2R_FRAGMENT+36,4);
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
    uint32_t w0;memcpy(&w0,c->data,4);uint64_t d=get64(c->data+24);f->last_preframe=w0&7;f->last_preframe1=(w0>>3)&7;
    f->last_dcd_rt[0]=f->last_dcd_rt[1]=0;
    if(w0&0x1ff){
     /* post_frame NEVER; pre_frame_1 only with pre_frame_0 (true-MRT LOAD of
      * a second target, DRAW 1 of the same array, mode equal to DRAW 0). */
     assert(!(w0&0x1c0)&&(w0&7)&&(!((w0>>3)&7)||((w0>>3)&7)==(w0&7)));
     assert(d>=c->va+N2R_BLOCK&&d<c->va+c->size&&((d-c->va)%N2R_BLOCK)==N2R_FRAME_DCDS);
     const uint8_t *dcd=c->data+(d-c->va);
     for(unsigned k=0;k<128;k++)assert(!(dcd-128)[k]&&!dcd[256+k]); /* no job header, DCD2 zero */
     for(unsigned slot=0;slot<2;slot++){
      const uint8_t *x=dcd+128*slot;
      if(!(slot?(w0>>3)&7:w0&7)){for(unsigned k=0;k<128;k++)assert(!x[k]);continue;}
      uint32_t f0,f1;memcpy(&f0,x,4);memcpy(&f1,x+4,4);unsigned mask=(f1>>16)&0xff;
      assert((f0&(1u<<9))&&mask&&!(mask&(mask-1))&&!(get64(x+8)&1)); /* clean fragment write, one RT, no vertex packet */
      f->last_dcd_rt[slot]=mask;
      uint64_t spd=get64(x+64+40); /* Shader Environment word 10 */
      assert(spd>=c->va+N2R_BLOCK&&spd<c->va+c->size&&((spd-c->va)%N2R_BLOCK)==1600);
      if(!slot)assert(spd-d==1600-N2R_FRAME_DCDS);
     }
    } else assert(!d);
   }
   accept_render:
   if(f->fail_submit){f->fail_submit--;return -12;}
   if(f->window&&f->inflight>=f->window)return KBUSY_TEST;
   f->inflight++;if(f->inflight>f->max_inflight)f->max_inflight=f->inflight;
   f->submitted++;f->pending=1;out[0]=++f->job;*no=1;fake_timed_submit(f);return 0;
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
  if(!in[0]){ /* the availability probe: 0108+ answers {4,0,0}; 0090-0107 refused seq 0 (N2C_E_NO_JOB) */
   assert(ni==2&&*no==3);
   if(f->seq0_notfound)return (int)0xe00002f0u;
   out[0]=4;out[1]=0;out[2]=0;return 0;
  }
  fake_wait_job(f,in[0]);
  if(f->gpu_us&&in[0]<f->job){ /* time model only: a pipelined finish reaps jobs <= seq (frame_bench CA loop) */
   assert(ni==2&&*no==3&&in[1]<=5000);
   uint64_t first=f->job-f->inflight;unsigned reaped=in[0]>first?(unsigned)(in[0]-first):0u;
   if(reaped>f->inflight)reaped=f->inflight;
   f->seq_waits++;f->inflight-=reaped;out[0]=4;out[1]=reaped;out[2]=0;return 0;
  }
  assert(ni==2&&*no==3&&in[1]<=5000&&in[0]==f->job);
  f->seq_waits++;out[1]=f->inflight;
  if(f->fail_seq){f->fail_seq--;out[0]=5;out[2]=f->job-f->inflight+1;
   if(ob&&os&&*os>=sizeof(struct nord2_gpu_fault_info)){struct nord2_gpu_fault_info fi={0};fi.job_id_lo=(uint32_t)out[2];fi.job_id_hi=(uint32_t)(out[2]>>32);
    fi.state=5;fi.status=f->js_status;fi.fault_status=f->mmu_status;fi.fault_address=f->mmu_address;memcpy(ob,&fi,sizeof(fi));*os=sizeof(fi);}}
  else{out[0]=4;out[2]=0;}
  f->pending=0;f->inflight=0;
  for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active&&f->buffers[h].released){f->buffers[h].released=0;collect(f,h);}
  return 0;
 }
 case NORD2_GPU_SEL_WAIT: {
  assert(ni==2&&in[0]==f->job&&in[1]==5000&&*no==1);
  f->waited++;fake_wait_job(f,in[0]);if(f->fail_wait){f->fail_wait--;return -13;}
  if(f->bad_state){out[0]=2;return 0;}f->pending=0;f->inflight=0;
  if(f->fault_wait){f->fault_wait--;out[0]=5; /* terminal and reaped, as the kernel answers a faulted job */
   if(ob&&os&&*os>=sizeof(struct nord2_gpu_fault_info)){struct nord2_gpu_fault_info fi={0};fi.job_id_lo=(uint32_t)in[0];fi.job_id_hi=(uint32_t)(in[0]>>32);
    fi.state=5;fi.status=f->js_status;fi.fault_status=f->mmu_status;fi.fault_address=f->mmu_address;memcpy(ob,&fi,sizeof(fi));*os=sizeof(fi);}
   for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active&&f->buffers[h].released){f->buffers[h].released=0;collect(f,h);}
   return 0;}
  out[0]=4;return 0;
 }
 case NORD2_GPU_SEL_SURFACE_RESERVE: {
  if(!f->resv_model)return (int)0xe00002c7u;
  assert(ni==1&&*no==1&&in[0]<=8&&size==in[0]*4&&(!in[0]||bytes));
  f->resv_calls++;f->resv_last_n=(unsigned)in[0];if(in[0])memcpy(f->resv_handles,bytes,size);
  for(unsigned k=0;k<in[0];k++)assert(f->resv_handles[k]<COUNT&&f->buffers[f->resv_handles[k]].active);
  out[0]=in[0]?++f->resv_next:0;return 0;
 }
 case NORD2_GPU_SEL_FAULT: { /* the context's MMU fault (kernel ctxs[].fault_status/address) */
  assert(!ni&&!size&&ob&&os&&*os>=sizeof(struct nord2_gpu_fault_info));
  struct nord2_gpu_fault_info fi={0};fi.fault_status=f->mmu_status;fi.fault_address=f->mmu_address;
  memcpy(ob,&fi,sizeof(fi));*os=sizeof(fi);f->fault_queries++;return 0;
 }
 case NORD2_GPU_SEL_SURFACE_RETIRE:
  if(!f->resv_model)return (int)0xe00002c7u;
  assert(ni==2&&in[0]&&in[0]<=f->resv_next);f->retire_calls++;f->retire_last[0]=in[0];f->retire_last[1]=in[1];
  f->resv_order_ok=!in[1]||in[1]==f->job;return 0;
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
static void fake_lost(void *p){((struct fake *)p)->lost_calls++;}
static void lock(void *p){assert(!pthread_mutex_lock(&((struct fake *)p)->mutex));}
static void unlock(void *p){assert(!pthread_mutex_unlock(&((struct fake *)p)->mutex));}
static uintptr_t open_bridge(struct fake *f) {
 assert(!pthread_mutex_init(&f->mutex,NULL));struct n2mb_ops ops={.opaque=f,.call=call,.map=map,.unmap=unmap,.close=close_raw,.lock=lock,.unlock=unlock,.lost=fake_lost};
 uintptr_t b=n2mb_open(&ops);assert(b);uint64_t caps[4];unsigned n=4;
 assert(!n2mb_scalar(b,0,NULL,0,caps,&n)&&n==4&&(caps[1]&8));return b;
}
static void close_bridge(uintptr_t b,struct fake *f){assert(!n2mb_close(b)&&f->closed&&!f->live&&!f->maps);assert(!pthread_mutex_destroy(&f->mutex));}
/* Linux uint64_t is unsigned long; the Darwin frontend ABI uses U64. */
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
static void drain(void){while(poolCount)[pool[--poolCount] release];}

static void fill_texture(TeguTexture *t,uint32_t value){
 assert(t&&t->buffer&&t->rowBytes&&t->textureHeight);
 uint32_t *p=(uint32_t *)[t->buffer contents]+t->mip[0].offset/4;
 for(NSUInteger i=0;i<t->rowBytes*t->textureHeight/4;i++)p[i]=value;
}
static TeguTexture *new_sample_ex(TeguComputeDevice *d,NSUInteger format,NSUInteger width,NSUInteger height,
 NSUInteger levels,NSUInteger type,NSUInteger depth,NSUInteger arrays,NSUInteger samples){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=format;desc->w=width;desc->h=height;
 desc->levels=levels;desc->type=type;desc->dep=depth;desc->samples=samples;desc->arrays=arrays;desc->uses=1;desc->options=0;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}
static TeguTexture *new_sample(TeguComputeDevice *d,NSUInteger format,NSUInteger width,NSUInteger height,
 NSUInteger levels,NSUInteger type,NSUInteger depth){
 return new_sample_ex(d,format,width,height,levels,type,depth,1,1);
}
static TeguTexture *new_target(TeguComputeDevice *d){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=80;desc->w=desc->h=16;desc->uses=4;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}
/* Auxiliary depth/stencil attachment used only to exercise pass admission.
 * The generic job has no depth/stencil execution, so the pass accepts this
 * only with DontCare storage and the draw path checks the bound state. */
static TeguTexture *new_aux_texture(TeguComputeDevice *d,NSUInteger format,NSUInteger options){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=80;desc->w=desc->h=16;desc->uses=4;desc->options=options;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];
 if(t)t->format=format;
 return t;
}
@interface AuxPass : RenderPass { @public PassColor *auxDepth,*auxStencil; }
@end
@implementation AuxPass
- (id)depthAttachment{return auxDepth;}
- (id)stencilAttachment{return auxStencil;}
- (void)dealloc{[auxDepth release];[auxStencil release];[super dealloc];}
@end
@interface MrtPass : RenderPass { @public PassColor *third,*fourth; }
@end
@implementation MrtPass
- (id)objectAtIndexedSubscript:(NSUInteger)i {return i==0?color:i==1?second:i==2?third:i==3?fourth:0;}
- (void)dealloc{[third release];[fourth release];[super dealloc];}
@end
@interface PassDepthDesc : NSObject { @public NSUInteger compare; signed char write; id front,back; }
@end
@implementation PassDepthDesc
- (NSUInteger)depthCompareFunction{return compare;}
- (signed char)isDepthWriteEnabled{return write;}
- (id)frontFaceStencil{return front;}
- (id)backFaceStencil{return back;}
- (id)label{return 0;}
@end
@interface PassStencilFace : NSObject { @public NSUInteger compare,fail,depthFail,pass; unsigned read,write; }
@end
@implementation PassStencilFace
- (NSUInteger)stencilCompareFunction{return compare;}
- (NSUInteger)stencilFailureOperation{return fail;}
- (NSUInteger)depthFailureOperation{return depthFail;}
- (NSUInteger)depthStencilPassOperation{return pass;}
- (unsigned)readMask{return read;}
- (unsigned)writeMask{return write;}
@end
@interface TestSamplerDesc : NSObject { @public NSUInteger mn,mg,mip,as,at,ar,cmp,aniso; float lmin,lmax; }
@end
@implementation TestSamplerDesc
- (signed char)respondsToSelector:(SEL)sel{
 return sel==@selector(respondsToSelector:)||sel==@selector(minFilter)||sel==@selector(magFilter)||
  sel==@selector(mipFilter)||sel==@selector(sAddressMode)||sel==@selector(tAddressMode)||
  sel==@selector(rAddressMode)||sel==@selector(normalizedCoordinates)||sel==@selector(compareFunction)||
  sel==@selector(maxAnisotropy)||sel==@selector(lodMinClamp)||sel==@selector(lodMaxClamp);
}
- (NSUInteger)minFilter{return mn;}- (NSUInteger)magFilter{return mg;}- (NSUInteger)mipFilter{return mip;}
- (NSUInteger)sAddressMode{return as;}- (NSUInteger)tAddressMode{return at;}- (NSUInteger)rAddressMode{return ar;}
- (signed char)normalizedCoordinates{return 1;}- (NSUInteger)compareFunction{return cmp;}
- (NSUInteger)maxAnisotropy{return aniso;}- (float)lodMinClamp{return lmin;}- (float)lodMaxClamp{return lmax;}
@end
@interface TeguBuffer (TestReferences) - (unsigned)testReferences; @end
@implementation TeguBuffer (TestReferences)
- (unsigned)testReferences{return _references;}
@end
static TeguSampler *new_sampler(TeguComputeDevice *d,unsigned linear){
 TestSamplerDesc *desc=[[TestSamplerDesc alloc] init];assert(desc);
 desc->mn=linear&1?1:0;desc->mg=linear&2?1:0;desc->mip=0;desc->as=desc->at=desc->ar=0;
 desc->cmp=0;desc->aniso=1;desc->lmin=0;desc->lmax=0;
 TeguSampler *s=[d newSamplerStateWithDescriptor:(id)desc];[desc release];
 assert(s&&s->state.version==N2T_TEXTURE_RESOURCES_VERSION);
 return s;
}
#ifdef TEGU_HOST_STUB_V2
static unsigned stubV2Calls,stubV2Levels;
int n2mb_draw_v2(uintptr_t b,const struct tegu_generic_draw_shader *sh,
 const struct tegu_generic_draw_request *q,const struct n2r_texture_resources *tr,
 uint64_t *out,unsigned *count){
 (void)b;(void)sh;(void)q;stubV2Calls++;
 if(tr&&tr->count)stubV2Levels=tr->bindings[0].layout.levels;
 out[0]=1;out[1]=2;*count=2;return 0;
}
#endif
static void bind_all(TeguRenderEncoder *e,TeguBuffer *vb,TeguRenderPipeline *p,TeguTexture *vs,
 TeguTexture *fs,TeguSampler *vss,TeguSampler *fss){
 [e setRenderPipelineState:p];
 [e setVertexBuffer:vb offset:0 atIndex:0];
 [e setVertexTexture:vs atIndex:0];[e setVertexSamplerState:vss atIndex:0];
 [e setFragmentTexture:fs atIndex:0];[e setFragmentSamplerState:fss atIndex:0];
}


#include <pthread.h>
#include <unistd.h>
/* API-surface cases (included by tests/api_surface_frontend_test.py after the render
 * harness prelude; the production frontend is inline above).  Host only. */
#include <stdio.h>
extern int tegu_host_clock_for_api;
static void api_sleep_ms(unsigned ms){usleep(ms*1000u);}
static TeguComputeDevice *api_device(struct fake *f,uintptr_t bridge){
 (void)f;
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 return d;
}
/* ---- listener / dispatch fakes ---- */
struct ApiQueue{unsigned retains,releases;};
struct ApiPending{void *queue;void *context;void (*work)(void *);};
static struct ApiPending api_pending[16];static unsigned api_pending_count,api_block_copies,api_block_releases;
extern void *_Block_copy(const void *);extern void _Block_release(const void *);
static void api_q_retain(void *q){((struct ApiQueue *)q)->retains++;}
static void api_q_release(void *q){((struct ApiQueue *)q)->releases++;}
static void api_q_schedule(void *q,void *context,void (*work)(void *)){
 assert(api_pending_count<16);api_pending[api_pending_count++]=(struct ApiPending){q,context,work};
}
static void *api_block_copy(const void *b){__atomic_fetch_add(&api_block_copies,1u,__ATOMIC_RELAXED);return _Block_copy(b);}
static void api_block_release(const void *b){__atomic_fetch_add(&api_block_releases,1u,__ATOMIC_RELAXED);_Block_release(b);}
static void api_pump(void){
 while(api_pending_count){struct ApiPending p=api_pending[0];
  for(unsigned i=1;i<api_pending_count;i++)api_pending[i-1]=api_pending[i];
  api_pending_count--;p.work(p.context);}
}
@interface ApiListener : NSObject { @public struct ApiQueue *queue; }
- (void *)dispatchQueue;
@end
@implementation ApiListener
- (void *)dispatchQueue{return queue;}
@end
@interface ApiForeignEvent : NSObject { @public U64 value;unsigned sets; }
- (U64)signaledValue;- (void)setSignaledValue:(U64)v;
@end
@implementation ApiForeignEvent
- (U64)signaledValue{return __atomic_load_n(&value,__ATOMIC_ACQUIRE);}
- (void)setSignaledValue:(U64)v{__atomic_store_n(&value,v,__ATOMIC_RELEASE);sets++;}
@end
@interface ApiPlainEvent : NSObject @end
@implementation ApiPlainEvent @end
static unsigned api_notified;static U64 api_notified_values[8];static id api_notified_events[8];
static TeguQueue *api_queue(TeguComputeDevice *d){TeguQueue *q=[d newCommandQueue];assert(q);return q;}
struct ApiCommitJob{TeguCommand *command;};
static void *api_commit_thread(void *p){struct ApiCommitJob *j=p;[j->command commit];[j->command waitUntilCompleted];return 0;}

static void api_test_events(TeguComputeDevice *d){
 /* factories, identity, labels, ownership */
 TeguFence *fence=[d newFence];TeguEvent *plain=[d newEvent];TeguSharedEvent *shared=[d newSharedEvent];
 assert(fence&&plain&&shared&&[fence device]==d&&[plain device]==d&&[shared device]==d);
 assert(![fence label]&&![plain label]);
 TestLabel *lab=[[TestLabel alloc] init];lab->value=31;[shared setLabel:lab];lab->value=0;[lab release];
 assert(((TestLabel *)[shared label])->value==31);
 assert([shared signaledValue]==0);
 [shared setSignaledValue:7];assert([shared signaledValue]==7);
 [shared setSignaledValue:2];assert([shared signaledValue]==2);
 assert([shared newSharedEventHandle]==0);
 assert([d newSharedEventWithHandle:0]==0);
 [shared setSignaledValue:0];

 TeguQueue *q=api_queue(d);
 TeguBuffer *buf=[d newBufferWithLength:256 options:0];assert(buf);
 unsigned char *bytes=[buf contents];

 /* signal after prior work, in the same command: blit fill, then signal, then wait (satisfied) */
 {
  memset(bytes,0,256);
  TeguCommand *c=[q commandBuffer];
  TeguBlitEncoder *b=[c blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){0,64} value:0xAB];[b endEncoding];
  [c encodeSignalEvent:shared value:3];
  [c encodeWaitForEvent:shared value:3];
  [c encodeWaitForEvent:plain value:0];
  b=[c blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){64,64} value:0xCD];[b endEncoding];
  assert(!c->invalid&&c->count==5);
  [c commit];[c waitUntilCompleted];
  assert([c status]==4&&![c error]);
  assert([shared signaledValue]==3&&bytes[0]==0xAB&&bytes[64]==0xCD);
  assert(tegu_command_job(c,1)->eventKind==(2|4)&&tegu_command_job(c,2)->eventKind==(1|4));
 }
 /* plain MTLEvent signalled by a command (value semantics) and re-signalled monotonic */
 {
  TeguCommand *c=[q commandBuffer];[c encodeSignalEvent:plain value:9];[c commit];[c waitUntilCompleted];
  assert([c status]==4&&plain->signaled==9);
  c=[q commandBuffer];[c encodeWaitForEvent:plain value:9];[c commit];[c waitUntilCompleted];assert([c status]==4);
 }
 /* a wait genuinely blocks the queue until another thread signals; order is preserved around it */
 {
  memset(bytes,0,256);[shared setSignaledValue:0];
  TeguCommand *c=[[q commandBuffer] retain];
  TeguBlitEncoder *b=[c blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){0,16} value:0x11];[b endEncoding];
  [c encodeWaitForEvent:shared value:1];
  b=[c blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){16,16} value:0x22];[b endEncoding];
  [c encodeSignalEvent:plain value:50];
  struct ApiCommitJob job={c};pthread_t th;assert(!pthread_create(&th,0,api_commit_thread,&job));
  api_sleep_ms(120);
  assert([c status]==2);                     /* still executing: blocked in the wait */
  assert(bytes[0]==0x11&&bytes[16]==0);      /* work before the wait ran; work after did not */
  assert(plain->signaled==9);                /* the later signal did not run early */
  [shared setSignaledValue:1];
  pthread_join(th,0);
  assert([c status]==4&&bytes[16]==0x22&&plain->signaled==50);
  [c release];
 }
 /* cross-queue handoff: queue B waits for the value queue A's command signals */
 {
  TeguQueue *qb=api_queue(d);memset(bytes,0,256);[shared setSignaledValue:0];
  TeguCommand *cb=[[qb commandBuffer] retain];
  [cb encodeWaitForEvent:shared value:5];
  TeguBlitEncoder *b=[cb blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){32,16} value:0x77];[b endEncoding];
  struct ApiCommitJob job={cb};pthread_t th;assert(!pthread_create(&th,0,api_commit_thread,&job));
  api_sleep_ms(60);assert(bytes[32]==0);
  TeguCommand *ca=[q commandBuffer];
  b=[ca blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){48,16} value:0x66];[b endEncoding];
  [ca encodeSignalEvent:shared value:5];[ca commit];[ca waitUntilCompleted];
  pthread_join(th,0);
  assert([cb status]==4&&bytes[32]==0x77&&bytes[48]==0x66);
  [cb release];[qb release];
 }
 /* timeout: an unsatisfiable wait fails the command with an error instead of hanging the queue */
 {
  TeguCommand *c=[q commandBuffer];[c encodeWaitForEvent:shared value:1000];
  TeguBlitEncoder *b=[c blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){80,8} value:0x99];[b endEncoding];
  double t0=tegu_host_seconds();[c commit];[c waitUntilCompleted];double t1=tegu_host_seconds();
  assert([c status]==5&&[c error]&&bytes[80]!=0x99);
  assert(t1-t0>=0.9&&t1-t0<6.0);
 }
 /* a failed producer still delivers its signal (no stalled consumer), and an invalid encode state fails */
 {
  TeguCommand *c=[q commandBuffer];[c encodeSignalEvent:shared value:11];
  [c computeCommandEncoder]; /* never ended: command invalid at commit */
  [c commit];[c waitUntilCompleted];
  assert([c status]==5&&[shared signaledValue]==11);
 }
 /* refusals: foreign object, wrong device, nil, active encoder, after commit */
 {
  TeguComputeDevice *d2=[TeguComputeDevice alloc]; /* identity only: no connection */
  TeguEvent *other=[d2 newEvent];TeguFence *otherFence=[d2 newFence];
  NSObject *foreign=[[NSObject alloc] init];
  TeguCommand *c=[q commandBuffer];[c encodeSignalEvent:other value:1];assert(c->invalid);
  c=[q commandBuffer];[c encodeWaitForEvent:foreign value:1];assert(c->invalid);
  c=[q commandBuffer];[c encodeWaitForEvent:0 value:1];assert(c->invalid);
  c=[q commandBuffer];[c encodeSignalEvent:(id)fence value:1];assert(c->invalid); /* a fence is not an event */
  c=[q commandBuffer];TeguBlitEncoder *b=[c blitCommandEncoder];[c encodeSignalEvent:shared value:1];assert(c->invalid);[b endEncoding];
  c=[q commandBuffer];[c commit];[c waitUntilCompleted];[c encodeSignalEvent:shared value:1];assert([c status]==4&&c->count==0);
  /* fences: valid same-device fence accepted by blit/compute encoders, others invalidate */
  c=[q commandBuffer];b=[c blitCommandEncoder];[b updateFence:fence];[b waitForFence:fence];[b endEncoding];
  assert(!c->invalid&&fence->updates==1&&fence->waits==1);
  c=[q commandBuffer];b=[c blitCommandEncoder];[b updateFence:otherFence];[b endEncoding];assert(c->invalid);
  c=[q commandBuffer];b=[c blitCommandEncoder];[b waitForFence:(id)plain];[b endEncoding];assert(c->invalid);
  c=[q commandBuffer];b=[c blitCommandEncoder];[b updateFence:0];[b endEncoding];assert(c->invalid);
  [foreign release];[other release];[otherFence release];[d2 release];
 }
 /* a foreign MTLSharedEvent (another implementation) is honoured through the public protocol */
 {
  ApiForeignEvent *fe=[[ApiForeignEvent alloc] init];ApiPlainEvent *pe=[[ApiPlainEvent alloc] init];
  TeguCommand *c=[q commandBuffer];[c encodeSignalEvent:(id)fe value:5];[c encodeWaitForEvent:(id)fe value:5];[c commit];[c waitUntilCompleted];
  assert([c status]==4&&fe->value==5&&fe->sets==1);
  memset(bytes,0,256);fe->value=0;
  c=[[q commandBuffer] retain];[c encodeWaitForEvent:(id)fe value:2];
  TeguBlitEncoder *b=[c blitCommandEncoder];[b fillBuffer:buf range:(TeguRange){96,8} value:0x5a];[b endEncoding];
  struct ApiCommitJob job={c};pthread_t th;assert(!pthread_create(&th,0,api_commit_thread,&job));
  api_sleep_ms(80);assert([c status]==2&&bytes[96]==0);
  fe->value=2;pthread_join(th,0);assert([c status]==4&&bytes[96]==0x5a);[c release];
  c=[q commandBuffer];[c encodeSignalEvent:(id)pe value:1];assert(c->invalid); /* plain foreign MTLEvent: no readable value */
  [fe release];[pe release];
 }
 /* shared event wait/timeout API */
 {
  [shared setSignaledValue:4];
  assert([shared waitUntilSignaledValue:4 timeoutMS:0]==1);
  assert([shared waitUntilSignaledValue:5 timeoutMS:30]==0);
  [shared setSignaledValue:6];assert([shared waitUntilSignaledValue:5 timeoutMS:30]==1);
 }
 /* listeners: threshold delivery, ordering, immediate delivery, balanced block/queue lifetime */
 {
  tegu_event_ops=(struct TeguEventOps){api_q_retain,api_q_release,api_q_schedule,api_block_copy,api_block_release,1};
  struct ApiQueue aq={0,0};ApiListener *l=[[ApiListener alloc] init];l->queue=&aq;
  api_notified=0;api_block_copies=api_block_releases=0;[shared setSignaledValue:0];
  TeguEventNotification mk5=^(id e,U64 v){api_notified_events[api_notified]=e;api_notified_values[api_notified++]=v;};
  [shared notifyListener:l atValue:5 block:mk5];
  [shared notifyListener:l atValue:2 block:^(id e,U64 v){api_notified_events[api_notified]=e;api_notified_values[api_notified++]=v+1000;}];
  assert(api_pending_count==0&&aq.retains==2);
  [shared setSignaledValue:1];api_pump();assert(api_notified==0);
  [shared setSignaledValue:3];api_pump();assert(api_notified==1&&api_notified_values[0]==1003&&api_notified_events[0]==(id)shared);
  [shared setSignaledValue:9];api_pump();assert(api_notified==2&&api_notified_values[1]==9);
  [shared setSignaledValue:10];api_pump();assert(api_notified==2); /* each listener fires once */
  [shared notifyListener:l atValue:7 block:^(id e,U64 v){(void)e;api_notified_values[api_notified++]=v+2000;}]; /* already reached */
  api_pump();assert(api_notified==3&&api_notified_values[2]==2010);
  assert(api_block_copies==3&&api_block_releases==3&&aq.retains==3&&aq.releases==3);
  /* unreached listener on a released event: blocks and queue are released with it */
  TeguSharedEvent *tmp=[d newSharedEvent];
  [tmp notifyListener:l atValue:99 block:^(id e,U64 v){(void)e;(void)v;api_notified++;}];
  assert(aq.retains==4&&aq.releases==3);
  [tmp release];assert(aq.releases==4&&api_block_releases==4);
  [shared notifyListener:0 atValue:1 block:mk5];[shared notifyListener:l atValue:1 block:0];
  assert(api_block_copies==4&&api_pending_count==0);
  [l release];
  tegu_event_ops=(struct TeguEventOps){0};
 }
 [buf release];[q release];[fence release];[plain release];[shared release];drain();
}

@interface ApiHeapDescriptor : NSObject { @public NSUInteger size,storage,cpu,hazard; }
- (NSUInteger)size;- (NSUInteger)storageMode;- (NSUInteger)cpuCacheMode;
- (NSUInteger)hazardTrackingMode;- (NSUInteger)resourceOptions;- (NSUInteger)type;
@end
@implementation ApiHeapDescriptor
- (NSUInteger)size{return size;}- (NSUInteger)storageMode{return storage;}- (NSUInteger)cpuCacheMode{return cpu;}
- (NSUInteger)hazardTrackingMode{return hazard;}- (NSUInteger)resourceOptions{return (storage<<4)|cpu|(hazard<<8);}- (NSUInteger)type{return 0;}
@end
/* ============================ queue / command / device ============================ */
extern void *class_getInstanceMethod(id klass,SEL selector);
@implementation NSObject (ApiFakeIntrospection)
- (signed char)respondsToSelector:(SEL)selector { return class_getInstanceMethod(tegu_host_object_class(self),selector)!=0; }
@end
static const char *api_air_path;
@interface ApiFakeAppleLibrary : NSObject @end
@implementation ApiFakeAppleLibrary
- (signed char)serializeToURL:(id)url error:(id *)error {
 (void)error;FILE *in=fopen(api_air_path,"rb");assert(in);fseek(in,0,SEEK_END);long n=ftell(in);rewind(in);
 void *b=malloc((size_t)n);assert(fread(b,1,(size_t)n,in)==(size_t)n);fclose(in);
 FILE *out=fopen([[url path] UTF8String],"wb");assert(out);assert(fwrite(b,1,(size_t)n,out)==(size_t)n);fclose(out);free(b);return 1;
}
@end
@interface ApiFakeBadLibrary : NSObject @end
@implementation ApiFakeBadLibrary @end /* no serializeToURL:error: */
static int api_source_mode; /* 0 good, 1 compiler error, 2 library that cannot serialize */
static unsigned api_source_calls;
@implementation NSObject (ApiFakeSourceBase)
- (id)newLibraryWithSource:(id)source options:(id)options error:(id *)error {
 (void)source;(void)options;api_source_calls++;
 if(api_source_mode==1){if(error)*error=[NSError errorWithDomain:@"Fake" code:5 userInfo:0];return 0;}
 return api_source_mode==2?[[ApiFakeBadLibrary alloc] init]:[[ApiFakeAppleLibrary alloc] init];
}
@end
@implementation NSURL (TeguAPISurfaceFileURL)
+ (id)fileURLWithPath:(id)path { libraryTestPath=path;libraryTestFileURL=1;return [[[self alloc] init] autorelease]; }
@end
@interface ApiPassDescriptor : NSObject { @public NSUInteger dispatch; }
- (NSUInteger)dispatchType;
@end
@implementation ApiPassDescriptor - (NSUInteger)dispatchType{return dispatch;} @end
@interface ApiFakeArray : NSObject { @public NSUInteger n; }
- (NSUInteger)count;
@end
@implementation ApiFakeArray - (NSUInteger)count{return n;} @end
@interface ApiArchiveDescriptor : NSObject { @public id url; }
- (id)url;
@end
@implementation ApiArchiveDescriptor - (id)url{return url;} - (void)dealloc{[url release];[super dealloc];} @end
@interface ApiMissDescriptor : RenderDescriptor { @public ApiFakeArray *archives; }
- (id)binaryArchives;
@end
@implementation ApiMissDescriptor - (id)binaryArchives{return archives;} - (void)dealloc{[archives release];[super dealloc];} @end

static unsigned api_handler_calls;static id api_handler_pipeline,api_handler_error,api_handler_reflection;
static TeguLibrary *api_probe_library(TeguComputeDevice *d){
 FILE *in=fopen(api_air_path,"rb");assert(in);fseek(in,0,SEEK_END);long n=ftell(in);rewind(in);
 void *b=malloc((size_t)n);assert(fread(b,1,(size_t)n,in)==(size_t)n);fclose(in);
 id error=0;TeguLibrary *lib=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&error];free(b);assert(lib&&!error);return lib;
}
extern id objc_getClass(const char *name);
static RenderDescriptor *api_render_descriptor(TeguLibrary *lib,const char *className){
 TeguFunction *vf=[lib newFunctionWithName:[NSString stringWithUTF8String:"vt_vertex"]];
 TeguFunction *ff=[lib newFunctionWithName:[NSString stringWithUTF8String:"vt_fragment"]];assert(vf&&ff);
 RenderDescriptor *pd=[[objc_getClass(className) alloc] init];
 pd->vertex=vf;pd->fragment=ff;pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];
 pd->colors->colors[0]=[[RenderColor alloc] init];pd->colors->colors[0]->format=80;pd->colors->colors[0]->mask=15;
 return pd;
}

static void api_test_device_queue_command(TeguComputeDevice *d){
 TeguQueue *q=[d newCommandQueue];
 assert(![d newCommandQueueWithMaxCommandBufferCount:0]);
 { TeguQueue *dq=[d newCommandQueueWithDescriptor:0];assert(dq&&[dq device]==d);[dq release]; }
 TeguQueue *q64=[d newCommandQueueWithMaxCommandBufferCount:64];assert(q64&&[q64 device]==d);[q64 release];
 TeguCommand *c=[q commandBufferWithUnretainedReferences];assert(c&&c->queue==q);
 [q insertDebugCaptureBoundary];
 assert([c commandQueue]==q&&[c retainedReferences]&&![c errorOptions]&&![c logs]);
 ApiPassDescriptor *pd=[[ApiPassDescriptor alloc] init];
 pd->dispatch=0;id e=[c computeCommandEncoderWithDescriptor:(id)pd];assert(e&&!c->invalid);[e endEncoding];
 pd->dispatch=1;e=[c computeCommandEncoderWithDescriptor:(id)pd];assert(e&&!c->invalid);[e endEncoding];
 assert([e dispatchType]==0);
 pd->dispatch=7;assert(![c computeCommandEncoderWithDescriptor:(id)pd]&&c->invalid);
 [pd release];
 /* refused encoders: nil + command invalid, never a crash */
 c=[q commandBuffer];assert(![c resourceStateCommandEncoder]&&c->invalid);
 c=[q commandBuffer];assert(![c resourceStateCommandEncoderWithDescriptor:0]&&c->invalid);
 c=[q commandBuffer];assert(![c accelerationStructureCommandEncoder]&&c->invalid);
 c=[q commandBuffer];assert(![c accelerationStructureCommandEncoderWithDescriptor:0]&&c->invalid);
 c=[q commandBuffer];id b=[c blitCommandEncoderWithDescriptor:0];assert(b&&!c->invalid);[b endEncoding];

 /* timestamps */
 U64 cpu=0,gpu=1;[d sampleTimestamps:&cpu gpuTimestamp:&gpu];assert(cpu==gpu&&cpu>1000);
 U64 cpu2=0;[d sampleTimestamps:&cpu2 gpuTimestamp:0];assert(cpu2>=cpu);[d sampleTimestamps:0 gpuTimestamp:0];
 /* counters, dynamic libraries, shared textures, rate maps: honest nil/NO with an error */
 assert(![d counterSets]&&![d supportsCounterSampling:0]&&![d supportsCounterSampling:4]);
 id err=0;assert(![d newCounterSampleBufferWithDescriptor:0 error:&err]&&err);
 err=0;assert(![d newDynamicLibrary:0 error:&err]&&err);err=0;assert(![d newDynamicLibraryWithURL:0 error:&err]&&err);
 assert(![d newSharedTextureWithDescriptor:0]&&![d newSharedTextureWithHandle:0]);
 assert(![d supportsRasterizationRateMapWithLayerCount:1]&&![d newRasterizationRateMapWithDescriptor:0]);
 assert(![d readWriteTextureSupport]&&![d areRasterOrderGroupsSupported]&&![d supports32BitFloatFiltering]&&![d supports32BitMSAA]&&
  ![d supportsQueryTextureLOD]&&![d supportsBCTextureCompression]&&![d supportsPullModelInterpolation]&&![d areBarycentricCoordsSupported]&&
  ![d supportsShaderBarycentricCoordinates]&&![d areProgrammableSamplePositionsSupported]&&![d supportsDynamicLibraries]&&
  ![d supportsRenderDynamicLibraries]&&![d supportsRaytracing]&&![d supportsRaytracingFromRender]&&![d supportsFunctionPointers]&&
  ![d supportsFunctionPointersFromRender]&&![d supportsPrimitiveMotionBlur]&&![d maxArgumentBufferSamplerCount]&&![d sparseTileSizeInBytes]);
 assert([d supportsVertexAmplificationCount:1]&&![d supportsVertexAmplificationCount:2]&&[d minimumTextureBufferAlignmentForPixelFormat:80]==[d minimumLinearTextureAlignmentForPixelFormat:80]);
 id ioerr=0;assert(![d newIOHandleWithURL:0 error:&ioerr]&&ioerr);ioerr=0;assert(![d newIOCommandQueueWithDescriptor:0 error:&ioerr]&&ioerr);
 assert(![d newLibraryWithStitchedDescriptor:0 error:&ioerr]&&![d newArgumentEncoderWithArguments:0]&&![d newIndirectCommandBufferWithDescriptor:0 maxCommandCount:1 options:0]);
 assert([d dedicatedMemorySize]==0&&[d sharedMemorySize]==[d recommendedMaxWorkingSetSize]&&![d requiresIABEmulation]);
 assert([d productName]==[d name]&&[d familyName]&&[d vendorName]&&[d minLinearTextureAlignmentForPixelFormat:80]==[d minimumLinearTextureAlignmentForPixelFormat:80]);
 float pos[8];
 [d getDefaultSamplePositions:pos count:1];assert(pos[0]==0.5f&&pos[1]==0.5f);
 [d getDefaultSamplePositions:pos count:2];assert(pos[0]==0.75f&&pos[2]==0.25f);
 [d getDefaultSamplePositions:pos count:4];assert(pos[0]==0.375f&&pos[7]==0.875f);
 pos[0]=-1;[d getDefaultSamplePositions:pos count:8];assert(pos[0]==-1);
 [q release];
}

static void api_test_source_and_archive(TeguComputeDevice *d){
 /* MSL source: Apple's front end output is rebuilt as OUR library (functions belong to the bridge) */
 api_source_mode=0;api_source_calls=0;id error=(id)1;
 TeguLibrary *lib=[d newLibraryWithSource:@"kernel void k(){}" options:0 error:&error];
 assert(lib&&!error&&api_source_calls==1&&lib->owner==d);
 TeguFunction *fn=[lib newFunctionWithName:[NSString stringWithUTF8String:"vt_vertex"]];assert(fn&&[fn device]==d);
 assert([fn patchType]==0&&[fn patchControlPointCount]<0&&[fn options]==0&&![fn functionConstantsDictionary]&&![fn vertexAttributes]&&![fn stageInputAttributes]);
 assert([lib type]==0&&![lib installName]);
 /* async library + function creation: handler once, borrowed reference */
 api_handler_calls=0;
 [d newLibraryWithSource:@"x" options:0 completionHandler:^(id l,id e){api_handler_calls++;api_handler_pipeline=l;api_handler_error=e;}];
 assert(api_handler_calls==1&&api_handler_pipeline&&!api_handler_error);
 api_handler_calls=0;
 [lib newFunctionWithDescriptor:0 completionHandler:^(id f,id e){api_handler_calls++;api_handler_pipeline=f;api_handler_error=e;}];
 assert(api_handler_calls==1&&!api_handler_pipeline&&api_handler_error);
 [fn release];[lib release];
 /* compiler error and a library that cannot serialize: nil + NSError, nothing foreign escapes */
 api_source_mode=1;error=0;assert(![d newLibraryWithSource:@"bad" options:0 error:&error]&&error);
 api_source_mode=2;error=0;assert(![d newLibraryWithSource:@"bad" options:0 error:&error]&&error);
 api_source_mode=1;api_handler_calls=0;
 [d newLibraryWithSource:@"bad" options:0 completionHandler:^(id l,id e){api_handler_calls++;api_handler_pipeline=l;api_handler_error=e;}];
 assert(api_handler_calls==1&&!api_handler_pipeline&&api_handler_error);
 assert(![d newLibraryWithSource:0 options:0 error:&error]);
 api_source_mode=0;

 /* pipelines through completion handlers and reflection variants (real planner + Mali compile) */
 TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 api_handler_calls=0;api_handler_pipeline=0;api_handler_error=(id)1;api_handler_reflection=(id)1;
 [d newRenderPipelineStateWithDescriptor:(id)pd completionHandler:^(id p,id e){api_handler_calls++;api_handler_pipeline=[p retain];api_handler_error=e;}];
 assert(api_handler_calls==1&&api_handler_pipeline&&!api_handler_error&&((TeguRenderPipeline *)api_handler_pipeline)->genericDraw);
 [api_handler_pipeline release];
 api_handler_calls=0;api_handler_pipeline=0;
 [d newRenderPipelineStateWithDescriptor:(id)pd options:0 completionHandler:^(id p,id r,id e){api_handler_calls++;api_handler_pipeline=[p retain];api_handler_reflection=r;api_handler_error=e;}];
 assert(api_handler_calls==1&&api_handler_pipeline&&!api_handler_reflection&&!api_handler_error);
 [api_handler_pipeline release];
 id reflection=(id)1;error=0;
 TeguRenderPipeline *rp=[d newRenderPipelineStateWithDescriptor:(id)pd options:3 reflection:&reflection error:&error];
 assert(rp&&!reflection&&!error);[rp release];
 /* FailOnBinaryArchiveMiss against descriptor archives can never hit (archives store nothing) */
 ApiMissDescriptor *miss=(ApiMissDescriptor *)api_render_descriptor(pl,"ApiMissDescriptor");
 miss->archives=[[ApiFakeArray alloc] init];miss->archives->n=1;
 reflection=(id)1;error=0;assert(![d newRenderPipelineStateWithDescriptor:(id)miss options:4 reflection:&reflection error:&error]&&error&&!reflection);
 rp=[d newRenderPipelineStateWithDescriptor:(id)miss options:0 reflection:&reflection error:&error];assert(rp);[rp release];
 miss->archives->n=0;error=0;
 rp=[d newRenderPipelineStateWithDescriptor:(id)miss options:4 reflection:&reflection error:&error];assert(rp&&!error);[rp release];
 [miss release];[pd release];
 /* compute completion handlers: nil function path delivers exactly one nil + error */
 api_handler_calls=0;api_handler_pipeline=(id)1;api_handler_error=0;
 [d newComputePipelineStateWithFunction:0 completionHandler:^(id p,id e){api_handler_calls++;api_handler_pipeline=p;api_handler_error=e;}];
 assert(api_handler_calls==1&&!api_handler_pipeline&&api_handler_error);
 api_handler_calls=0;api_handler_reflection=(id)1;
 [d newComputePipelineStateWithFunction:0 options:0 completionHandler:^(id p,id r,id e){api_handler_calls++;api_handler_pipeline=p;api_handler_reflection=r;api_handler_error=e;}];
 assert(api_handler_calls==1&&!api_handler_pipeline&&!api_handler_reflection&&api_handler_error);
 api_handler_calls=0;
 [d newComputePipelineStateWithDescriptor:0 options:0 completionHandler:^(id p,id r,id e){api_handler_calls++;api_handler_pipeline=p;api_handler_reflection=r;api_handler_error=e;}];
 assert(api_handler_calls==1&&!api_handler_pipeline&&api_handler_error);
 [pl release];

 /* binary archive: honest functional no-op cache */
 TeguBinaryArchive *a=[d newBinaryArchiveWithDescriptor:0 error:&error];assert(a&&[a device]==d&&![a label]);
 TestLabel *lab=[[TestLabel alloc] init];lab->value=5;[a setLabel:lab];[lab release];assert(((TestLabel *)[a label])->value==5);
 error=0;assert(![a addComputePipelineFunctionsWithDescriptor:0 error:&error]&&error);
 error=0;assert([a addRenderPipelineFunctionsWithDescriptor:[[[NSObject alloc] init] autorelease] error:&error]&&!error);
 error=0;assert([a addComputePipelineFunctionsWithDescriptor:[[[NSObject alloc] init] autorelease] error:&error]&&!error);
 error=0;assert(![a addTileRenderPipelineFunctionsWithDescriptor:[[[NSObject alloc] init] autorelease] error:&error]&&error);
 assert(a->added==2);
 const char *tmp="/tmp/api-surface-archive.bin";unlink(tmp);
 NSURL *url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:tmp]];
 error=0;assert([a serializeToURL:url error:&error]&&!error);
 ApiArchiveDescriptor *ad=[[ApiArchiveDescriptor alloc] init];ad->url=[url retain];
 error=0;TeguBinaryArchive *loaded=[d newBinaryArchiveWithDescriptor:(id)ad error:&error];assert(loaded&&!error&&loaded->added==2);
 [loaded release];
 FILE *junk=fopen(tmp,"wb");fputs("not an archive",junk);fclose(junk);
 error=0;assert(![d newBinaryArchiveWithDescriptor:(id)ad error:&error]&&error);
 unlink(tmp);error=0;assert(![d newBinaryArchiveWithDescriptor:(id)ad error:&error]&&error);
 [ad release];[a release];
}

@interface TeguEncoder (ApiTest) - (id)apiSampler:(unsigned)i; - (id)apiBuffer:(unsigned)i; - (NSUInteger)apiOffset:(unsigned)i; @end
@implementation TeguEncoder (ApiTest)
- (id)apiSampler:(unsigned)i{return samplers[i];}
- (id)apiBuffer:(unsigned)i{return buffers[i];}
- (NSUInteger)apiOffset:(unsigned)i{return offsets[i];}
@end
static void api_test_compute_encoder(TeguComputeDevice *d){
 TeguQueue *q=[d newCommandQueue];
 TeguBuffer *b0=[d newBufferWithLength:256 options:0],*b1=[d newBufferWithLength:256 options:0];
 TeguTexture *tex=new_sample(d,80,8,8,1,2,1);TeguSampler *smp=new_sampler(d,3);
 TeguFence *fence=[d newFence];
 TeguComputeDevice *other=[TeguComputeDevice alloc];TeguBuffer *foreign=0;(void)foreign;
 /* setBuffers:offsets:withRange: binds in order; bounds and null arrays are refused */
 TeguCommand *c=[q commandBuffer];TeguEncoder *e=[c computeCommandEncoder];
 TeguBuffer *list[3]={b0,0,b1};NSUInteger offs[3]={4,0,8};
 [e setBuffers:list offsets:offs withRange:(struct TeguComputeRange){2,3}];
 assert(!c->invalid&&[e apiBuffer:2]==b0&&[e apiOffset:2]==4&&![e apiBuffer:3]&&[e apiBuffer:4]==b1&&[e apiOffset:4]==8);
 [e setBuffers:list offsets:offs withRange:(struct TeguComputeRange){30,2}];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e setBuffers:0 offsets:offs withRange:(struct TeguComputeRange){0,1}];assert(c->invalid);
 /* LOD-clamped samplers: exact Metal state with the new clamp, original untouched */
 c=[q commandBuffer];e=[c computeCommandEncoder];
 [e setSamplerState:smp lodMinClamp:1.0f lodMaxClamp:3.0f atIndex:2];
 TeguSampler *cl=[e apiSampler:2];assert(!c->invalid&&cl&&cl!=smp&&cl->state.lod_min==1.0f&&cl->state.lod_max==3.0f);
 assert(smp->state.lod_min==0.0f&&cl->state.min_filter==smp->state.min_filter&&cl->state.mag_filter==smp->state.mag_filter);
 { id ss[2]={smp,smp};float mn[2]={0.5f,2.0f},mx[2]={4.0f,6.0f};
   [e setSamplerStates:ss lodMinClamps:mn lodMaxClamps:mx withRange:(struct TeguComputeRange){4,2}];
   assert(!c->invalid&&((TeguSampler *)[e apiSampler:4])->state.lod_min==0.5f&&((TeguSampler *)[e apiSampler:5])->state.lod_max==6.0f); }
 [e setSamplerState:smp lodMinClamp:5.0f lodMaxClamp:1.0f atIndex:0];assert(c->invalid); /* min > max */
 c=[q commandBuffer];e=[c computeCommandEncoder];[e setSamplerState:smp lodMinClamp:0 lodMaxClamp:1 atIndex:16];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e setSamplerState:0 lodMinClamp:0 lodMaxClamp:1 atIndex:3];assert(!c->invalid&&![e apiSampler:3]);
 /* residency, barriers, fences, dispatch type */
 c=[q commandBuffer];e=[c computeCommandEncoder];
 [e useResource:b0 usage:3];[e useResource:tex usage:1];
 id rs[2]={b0,tex};[e useResources:rs count:2 usage:7];[e useResources:0 count:0 usage:1];
 [e memoryBarrierWithScope:1];[e memoryBarrierWithScope:7];[e memoryBarrierWithResources:rs count:2];
 [e updateFence:fence];[e waitForFence:fence];[e endEncoding];
 assert(!c->invalid&&fence->updates>=1&&fence->waits>=1);
 /* the command retains what it uses: dropping the app's reference does not free it before the command */
 { TeguBuffer *rb=[d newBufferWithLength:64 options:0];
   TeguCommand *rc=[q commandBuffer];TeguEncoder *re=[rc computeCommandEncoder];
   [re useResource:rb usage:1];[re useResource:rb usage:1];[re useResources:(id const *)&rb count:1 usage:3];[re endEncoding];
   assert(!rc->invalid);
   unsigned held=live;[rb release];assert(live==held);
   drain();assert(live<held); }
 c=[q commandBuffer];e=[c computeCommandEncoder];[e useResource:0 usage:1];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e useResource:b0 usage:0];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e memoryBarrierWithScope:0];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e memoryBarrierWithScope:8];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e updateFence:(id)other];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e useHeaps:0 count:0];[e useHeap:0];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e useResources:rs count:2 usage:9];assert(c->invalid);
 /* heap residency: a heap of this device is accepted, a foreign object is not */
 {
  ApiHeapDescriptor *hd=[[ApiHeapDescriptor alloc] init];hd->size=65536;
  TeguHeap *heap=[d newHeapWithDescriptor:(id)hd];[hd release];assert(heap);
  c=[q commandBuffer];e=[c computeCommandEncoder];
  [e useHeap:heap];id hs[1]={heap};[e useHeaps:hs count:1];assert(!c->invalid);
  [e useHeap:(id)b0];assert(c->invalid); /* a buffer is not a heap */
  [heap release];
 }
 /* explicit refusals: defined failure, never a crash and never a silent no-op */
 c=[q commandBuffer];e=[c computeCommandEncoder];[e dispatchThreadgroupsWithIndirectBuffer:b0 indirectBufferOffset:0 threadsPerThreadgroup:(MTLSize){1,1,1}];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e setStageInRegion:(struct TeguComputeRegion){{0,0,0},{1,1,1}}];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e setImageblockWidth:8 height:8];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e executeCommandsInBuffer:0 withRange:(struct TeguComputeRange){0,1}];assert(c->invalid);
 c=[q commandBuffer];e=[c computeCommandEncoder];[e sampleCountersInBuffer:0 atSampleIndex:0 withBarrier:1];assert(c->invalid);
 [other release];[fence release];[smp release];[tex release];[b0 release];[b1 release];[q release];
}

static void api_test_render_blit_texture(TeguComputeDevice *d){
 id error=0;
 TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];
 TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0];
 TeguTexture *tex=new_sample(d,80,16,16,1,2,1);TeguSampler *smp=new_sampler(d,3);
 TeguFence *fence=[d newFence];TeguBuffer *aux=[d newBufferWithLength:64 options:0];
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 /* viewports/scissors: exactly one, forwarded to the ordinary setters */
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e);
 TeguViewport vp={2,3,8,9,0,1};[e setViewports:&vp count:1];
 assert(!c->invalid&&e->nord2HasViewport&&e->nord2Viewport.originX==2&&e->nord2Viewport.height==9);
 TeguScissorRect sr={1,2,5,6};[e setScissorRects:&sr count:1];
 assert(!c->invalid&&e->scissor.x==1&&e->scissor.width==5&&e->scissor.height==6);
 /* LOD-clamped samplers: fresh exact-state clones, original untouched */
 [e setFragmentSamplerState:smp lodMinClamp:1 lodMaxClamp:4 atIndex:0];
 assert(!c->invalid&&e->fragmentSamplers[0]!=smp&&e->fragmentSamplers[0]->state.lod_min==1&&e->fragmentSamplers[0]->state.lod_max==4&&smp->state.lod_min==0);
 [e setVertexSamplerState:smp lodMinClamp:0.5f lodMaxClamp:2 atIndex:1];
 assert(!c->invalid&&e->vertexSamplers[1]->state.lod_min==0.5f&&e->vertexSamplers[1]->state.lod_max==2);
 { id ss[2]={smp,smp};float mn[2]={0,1},mx[2]={2,3};
   [e setVertexSamplerStates:ss lodMinClamps:mn lodMaxClamps:mx withRange:(TeguRange){2,2}];
   [e setFragmentSamplerStates:ss lodMinClamps:mn lodMaxClamps:mx withRange:(TeguRange){3,2}];
   assert(!c->invalid&&e->vertexSamplers[3]->state.lod_max==3&&e->fragmentSamplers[4]->state.lod_min==1); }
 [e setVertexTexture:tex atTextureIndex:2 samplerState:smp atSamplerIndex:5];assert(!c->invalid&&e->vertexTextures[2]==tex&&e->vertexSamplers[5]==smp);
 /* store actions, options, residency, fences, barriers */
 [e setDepthStoreAction:0];[e setStencilStoreAction:1];[e setDepthStoreActionOptions:0];[e setStencilStoreActionOptions:0];
 [e setColorStoreActionOptions:0 atIndex:0];assert(!c->invalid);
 { id rs[2]={aux,tex};[e useResources:rs count:2 usage:3];[e useResources:rs count:2 usage:3 stages:3];
   [e memoryBarrierWithScope:7 afterStages:2 beforeStages:1];[e memoryBarrierWithResources:rs count:2 afterStages:3 beforeStages:3]; }
 [e updateFence:fence afterStages:2];[e waitForFence:fence beforeStages:1];
 [e useHeaps:0 count:0];[e useHeaps:0 count:0 stages:1];[e textureBarrier];assert(![e tileWidth]&&![e tileHeight]);
 assert(!c->invalid&&fence->updates==1&&fence->waits==1);
 /* the encoder still draws and the command completes */
 bind_all(e,vb,pipeline,tex,tex,smp,smp);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
 [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
 /* refusals poison the command (defined failure) */
#define API_BAD(...) do{TeguCommand *bc=[q commandBuffer];TeguRenderEncoder *be=[bc renderCommandEncoderWithDescriptor:(id)pass];assert(be);\
  __VA_ARGS__;assert(bc->invalid);[be endEncoding];}while(0)
 API_BAD([be setViewports:&vp count:2]);
 API_BAD([be setViewports:0 count:1]);
 API_BAD([be setScissorRects:&sr count:0]);
 API_BAD([be setColorStoreActionOptions:1 atIndex:0]);
 API_BAD([be setColorStoreActionOptions:0 atIndex:4]);
 API_BAD([be setFragmentSamplerState:smp lodMinClamp:5 lodMaxClamp:1 atIndex:0]);
 API_BAD([be setVertexSamplerState:smp lodMinClamp:-1 lodMaxClamp:1 atIndex:0]);
 API_BAD([be useResources:0 count:1 usage:1]);
 API_BAD([be useResources:&aux count:1 usage:0]);
 API_BAD([be useHeap:(id)aux stages:1]);
 API_BAD([be updateFence:fence afterStages:0]);
 API_BAD([be waitForFence:fence beforeStages:8]);
 API_BAD([be updateFence:0 afterStages:1]);
 API_BAD([be memoryBarrierWithScope:0 afterStages:1 beforeStages:1]);
 API_BAD([be memoryBarrierWithScope:1 afterStages:0 beforeStages:1]);
 API_BAD([be executeCommandsInBuffer:0 withRange:(TeguRange){0,1}]);
 API_BAD([be sampleCountersInBuffer:0 atSampleIndex:0 withBarrier:1]);
#undef API_BAD
 /* blit: sliceCount/levelCount whole-subresource copy walks every level at its extent */
 {
  TeguTexture *src=new_sample(d,80,8,8,3,2,1),*dst=new_sample(d,80,8,8,3,2,1);assert(src&&dst&&src->levelCount==3);
  unsigned char *sb=[src->buffer contents],*db=[dst->buffer contents];
  for(unsigned l=0;l<3;l++){NSUInteger w=8>>l;for(NSUInteger y=0;y<w;y++)memset(sb+src->mip[l].offset+y*src->mip[l].stride,0x30+(int)l*0x10+(int)y,w*4);}
  TeguCommand *bc=[q commandBuffer];TeguBlitEncoder *b=[bc blitCommandEncoder];
  [b copyFromTexture:src sourceSlice:0 sourceLevel:0 toTexture:dst destinationSlice:0 destinationLevel:0 sliceCount:1 levelCount:3];
  [b endEncoding];assert(!bc->invalid&&bc->count==3);
  [bc commit];[bc waitUntilCompleted];assert([bc status]==4);
  for(unsigned l=0;l<3;l++){NSUInteger w=8>>l;
   for(NSUInteger y=0;y<w;y++)for(NSUInteger x=0;x<w*4;x++)
    assert(db[dst->mip[l].offset+y*dst->mip[l].stride+x]==(unsigned char)(0x30+(int)l*0x10+(int)y));}
  /* partial: level 1..2 only */
  memset(db,0,dst->buffer->logical);
  bc=[q commandBuffer];b=[bc blitCommandEncoder];
  [b copyFromTexture:src sourceSlice:0 sourceLevel:1 toTexture:dst destinationSlice:0 destinationLevel:1 sliceCount:1 levelCount:2];[b endEncoding];
  assert(!bc->invalid&&bc->count==2);[bc commit];[bc waitUntilCompleted];assert([bc status]==4);
  assert(db[dst->mip[0].offset]==0&&db[dst->mip[1].offset]==0x40&&db[dst->mip[2].offset]==0x50);
  bc=[q commandBuffer];b=[bc blitCommandEncoder];
  [b copyFromTexture:src sourceSlice:0 sourceLevel:2 toTexture:dst destinationSlice:0 destinationLevel:0 sliceCount:1 levelCount:2];[b endEncoding];assert(bc->invalid);
  bc=[q commandBuffer];b=[bc blitCommandEncoder];
  [b copyFromTexture:src sourceSlice:0 sourceLevel:0 toTexture:dst destinationSlice:0 destinationLevel:0 sliceCount:0 levelCount:1];[b endEncoding];assert(bc->invalid);
  bc=[q commandBuffer];b=[bc blitCommandEncoder];
  [b copyFromTexture:0 sourceSlice:0 sourceLevel:0 toTexture:dst destinationSlice:0 destinationLevel:0 sliceCount:1 levelCount:1];[b endEncoding];assert(bc->invalid);
  /* texture accessors */
  assert(![src setOwnerWithIdentity:7]&&![aux setOwnerWithIdentity:7]);
  assert(![src parentTexture]&&![src rootResource]&&![src isShareable]&&[src allowGPUOptimizedContents]&&![src compressionType]);
  assert([src swizzle]==(2u|(3u<<8)|(4u<<16)|(5u<<24)));
  TeguTexture *v1=[src newTextureViewWithPixelFormat:80];TeguTexture *v2=[v1 newTextureViewWithPixelFormat:80];
  assert(v1&&v2&&[v1 parentTexture]==src&&[v2 parentTexture]==v1&&[v2 rootResource]==src&&[v1 rootResource]==src);
  TeguTextureViewRange lv={0,3},sl={0,1};
  TeguTexture *vi=[src newTextureViewWithPixelFormat:80 textureType:2 levels:lv slices:sl swizzle:(2u|(3u<<8)|(4u<<16)|(5u<<24))];assert(vi);
  assert(!([src newTextureViewWithPixelFormat:80 textureType:2 levels:lv slices:sl swizzle:(5u|(4u<<8)|(3u<<16)|(2u<<24))]));
  { TeguTextureViewRange l12={1,2},s0={0,1};
    TeguTexture *sub=[src newTextureViewWithPixelFormat:80 textureType:2 levels:l12 slices:s0];
    assert(sub&&[sub parentTexture]==src&&[sub parentRelativeLevel]==1&&[sub parentRelativeSlice]==0&&[sub mipmapLevelCount]==2);
    TeguTextureViewRange l1={1,1};TeguTexture *subsub=[sub newTextureViewWithPixelFormat:80 textureType:2 levels:l1 slices:s0];
    assert(subsub&&[subsub parentTexture]==sub&&[subsub parentRelativeLevel]==1&&[subsub rootResource]==src); /* relative to the DIRECT parent */
    assert([v1 parentRelativeLevel]==0&&[v1 parentRelativeSlice]==0&&![src parentRelativeLevel]);
    [subsub release];[sub release]; }
  assert(![src newSharedTextureHandle]);
  [vi release];[v2 release];[v1 release];[src release];[dst release];
  [aux addDebugMarker:@"m" range:(TeguRange){0,8}];[aux removeAllDebugMarkers];
 }
 [pass release];[fence release];[aux release];[smp release];[tex release];[vb release];[target release];
 [pipeline release];[pd release];[pl release];[q release];
}

/* ============================ parallel render encoder ============================ */
struct ApiParArg{TeguRenderSubEncoder *sub;TeguRenderPipeline *pipeline;TeguBuffer *vb;TeguTexture *tex;TeguSampler *smp;unsigned variant;};
static void api_par_record(struct ApiParArg *a){
 TeguRenderSubEncoder *s=a->sub;
 [s setRenderPipelineState:a->pipeline];[s setVertexBuffer:a->vb offset:0 atIndex:0];
 [s setVertexTexture:a->tex atIndex:0];[s setVertexSamplerState:a->smp atIndex:0];
 [s setFragmentTexture:a->tex atIndex:0];[s setFragmentSamplerState:a->smp atIndex:0];
 if(a->variant==0){
  TeguViewport vp={2,3,8,9,0,1};[s setViewport:vp];[s setScissorRect:(TeguScissorRect){1,2,6,7}];
  [s setCullMode:0];[s setFrontFacingWinding:1];
  [s drawPrimitives:3 vertexStart:0 vertexCount:3];
 } else {
  [s drawPrimitives:3 vertexStart:3 vertexCount:3];
 }
}
static void api_par_ref(TeguRenderEncoder *e,TeguRenderPipeline *p,TeguBuffer *vb,TeguTexture *t,TeguSampler *smp,unsigned variant){
 bind_all(e,vb,p,t,t,smp,smp);
 if(variant==0){
  TeguViewport vp={2,3,8,9,0,1};[e setViewport:vp];[e setScissorRect:(TeguScissorRect){1,2,6,7}];
  [e setCullMode:0];[e setFrontFacingWinding:1];
  [e drawPrimitives:3 vertexStart:0 vertexCount:3];
 } else [e drawPrimitives:3 vertexStart:3 vertexCount:3];
}
static void *api_par_thread(void *p){api_par_record(p);return 0;}
static unsigned api_draw_jobs(TeguCommand *c,struct Dispatch **out,unsigned cap){
 unsigned n=0;for(unsigned i=0;i<c->count&&n<cap;i++){struct Dispatch *j=tegu_command_job(c,i);if(j->genericDrawSnapshot)out[n++]=j;}return n;
}
static void api_effective_viewport(const struct tegu_generic_draw_request *r,float out[6],unsigned w,unsigned h){
 if(r->viewport_valid){memcpy(out,r->viewport,24);return;}
 out[0]=0;out[1]=0;out[2]=(float)w;out[3]=(float)h;out[4]=0;out[5]=1;
}
static void api_test_parallel(TeguComputeDevice *d){
 id error=0;TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];q->blockCopy=api_block_copy;q->blockRelease=api_block_release;
 TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0];
 TeguTexture *tex=new_sample(d,80,16,16,1,2,1);TeguSampler *smp=new_sampler(d,3);
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 api_block_copies=api_block_releases=0;
 struct Dispatch *par[8],*refA[8],*refB[8];
 /* two threads record sub-encoders concurrently; they end out of order */
 TeguCommand *c=[[q commandBuffer] retain];
 TeguParallelRenderEncoder *p=[c parallelRenderCommandEncoderWithDescriptor:(id)pass];assert(p&&!c->invalid);
 TeguRenderSubEncoder *s0=[p renderCommandEncoder],*s1=[p renderCommandEncoder],*s2=[p renderCommandEncoder];
 assert(s0&&s1&&s2&&s0!=s1&&[s0 device]==d);
 /* the host fixture's retain counts are not atomic (Foundation's are): each recording thread gets its own
  * resources so no object is retained from two threads at once */
 TeguRenderPipeline *pipeline1=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline1);
 TeguBuffer *vb1=[d newBufferWithLength:256 options:0];TeguTexture *tex1=new_sample(d,80,16,16,1,2,1);TeguSampler *smp1=new_sampler(d,3);
 struct ApiParArg a0={s0,pipeline,vb,tex,smp,0},a1={s1,pipeline1,vb1,tex1,smp1,1};
 pthread_t t0,t1;assert(!pthread_create(&t0,0,api_par_thread,&a0));assert(!pthread_create(&t1,0,api_par_thread,&a1));
 pthread_join(t1,0);pthread_join(t0,0);
 assert(s0->count+s1->count==18); /* every recorded call is one record */
 unsigned before=c->count;
 [s1 endEncoding];[s2 endEncoding];[s0 endEncoding];
 assert(c->count==before&&!c->invalid);
 [p endEncoding];if(c->invalid)fprintf(stderr,"PAR_INVALID %s:%u\n",c->invalidFunction,c->invalidLine);assert(!c->invalid);
 unsigned nPar=api_draw_jobs(c,par,8);assert(nPar==2);
 /* references: the same calls in an ordinary encoder, one per variant */
 TeguCommand *ca=[q commandBuffer];TeguRenderEncoder *ea=[ca renderCommandEncoderWithDescriptor:(id)pass];
 api_par_ref(ea,pipeline,vb,tex,smp,0);[ea endEncoding];assert(api_draw_jobs(ca,refA,8)==1&&!ca->invalid);
 TeguCommand *cb=[q commandBuffer];TeguRenderEncoder *eb=[cb renderCommandEncoderWithDescriptor:(id)pass];
 api_par_ref(eb,pipeline,vb,tex,smp,1);[eb endEncoding];assert(api_draw_jobs(cb,refB,8)==1&&!cb->invalid);
 {
  const struct tegu_generic_draw_request *x=&par[0]->genericDrawRequest,*y=&refA[0]->genericDrawRequest;
  assert(x->vertex_count==y->vertex_count&&x->primitive==y->primitive&&x->binding_count==y->binding_count);
  assert(!memcmp(x->scissor,y->scissor,sizeof(x->scissor))&&x->viewport_valid&&!memcmp(x->viewport,y->viewport,sizeof(x->viewport)));
  float ex[6],ey[6];
  x=&par[1]->genericDrawRequest;y=&refB[0]->genericDrawRequest;
  assert(x->vertex_count==y->vertex_count&&x->binding_count==y->binding_count);
  api_effective_viewport(x,ex,16,16);api_effective_viewport(y,ey,16,16);
  assert(!memcmp(ex,ey,sizeof(ex))); /* the second sub-encoder did NOT inherit the first one's viewport */
  assert(x->scissor[0]==y->scissor[0]&&x->scissor[1]==y->scissor[1]&&x->scissor[2]==y->scissor[2]&&x->scissor[3]==y->scissor[3]);
 }
 [c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
 [c release];
 /* block copies are balanced once the command and sub-encoders are gone */
 drain();assert(api_block_copies==api_block_releases); /* records, not blocks: the queue block hooks are never used */

 /* failure semantics: an unended sub-encoder, use after end, post-end creation, multi-viewport */
 c=[q commandBuffer];p=[c parallelRenderCommandEncoderWithDescriptor:(id)pass];
 s0=[p renderCommandEncoder];[p endEncoding];assert(c->invalid);
 c=[q commandBuffer];p=[c parallelRenderCommandEncoderWithDescriptor:(id)pass];
 s0=[p renderCommandEncoder];[s0 endEncoding];[s0 setCullMode:2];assert(c->invalid);[p endEncoding];
 c=[q commandBuffer];p=[c parallelRenderCommandEncoderWithDescriptor:(id)pass];[p endEncoding];assert(!c->invalid&&![p renderCommandEncoder]&&c->invalid);
 c=[q commandBuffer];p=[c parallelRenderCommandEncoderWithDescriptor:(id)pass];s0=[p renderCommandEncoder];
 { TeguViewport two[2]={{0,0,1,1,0,1},{0,0,1,1,0,1}};[s0 setViewports:two count:2]; }
 assert(c->invalid);[s0 endEncoding];[p endEncoding];
 c=[q commandBuffer];assert(![q commandBuffer]==0);
 /* a command with an open encoder cannot start a parallel one */
 c=[q commandBuffer];[c computeCommandEncoder];assert(![c parallelRenderCommandEncoderWithDescriptor:(id)pass]&&c->invalid);
 /* bytes are copied at record time */
 {
  TeguCommand *cc=[q commandBuffer];TeguParallelRenderEncoder *pp=[cc parallelRenderCommandEncoderWithDescriptor:(id)pass];
  TeguRenderSubEncoder *ss=[pp renderCommandEncoder];
  unsigned char bytes[8]={1,2,3,4,5,6,7,8};
  [ss setRenderPipelineState:pipeline];[ss setVertexBytes:bytes length:8 atIndex:5];memset(bytes,0,8);
  [ss setVertexBytes:0 length:8 atIndex:6];assert(cc->invalid);
  [ss endEncoding];[pp endEncoding];
 }
 [pipeline1 release];[vb1 release];[tex1 release];[smp1 release];
 [pass release];[smp release];[tex release];[vb release];[target release];[pipeline release];[pd release];[pl release];
 q->blockCopy=0;q->blockRelease=0;[q release];
}

/* ============================ indirect compute dispatch ============================ */
static unsigned api_spy_dispatches,api_spy_imports,api_spy_groups[3];
static int api_spy_method(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,const void *data,NSUInteger bytes,
 U64 *out,unsigned *no,void *output,NSUInteger *size){
 if(sel==5){out[0]=0x80000000u|++api_spy_imports;*no=1;return 0;}
 if(sel==7){
  const unsigned *w=data;api_spy_dispatches++;
  if(bytes==TEGU_PIPELINE_DISPATCH2_BYTES){api_spy_groups[0]=w[3];api_spy_groups[1]=w[4];api_spy_groups[2]=w[5];}
  else{api_spy_groups[0]=w[1];api_spy_groups[1]=w[2];api_spy_groups[2]=w[3];}
  out[0]=api_spy_dispatches;out[1]=7;*no=2;return 0;
 }
 return method_adapter(c,sel,in,ni,data,bytes,out,no,output,size);
}
static int api_spy_scalar(TeguConnection c,unsigned sel,const U64 *in,unsigned ni,U64 *out,unsigned *no){
 if(sel==6)return 0;
 return scalar_adapter(c,sel,in,ni,out,no);
}
static void api_test_indirect_dispatch(TeguComputeDevice *base,const char *computeAir){
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=base->connection;d->scalar=api_spy_scalar;d->method=api_spy_method;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=base->capabilities;
 FILE *in=fopen(computeAir,"rb");assert(in);fseek(in,0,SEEK_END);long n=ftell(in);rewind(in);
 void *air=malloc((size_t)n);assert(fread(air,1,(size_t)n,in)==(size_t)n);fclose(in);
 id error=0;TeguLibrary *lib=[d newTeguLibraryWithBytes:air length:(NSUInteger)n error:&error];free(air);assert(lib&&!error);
 TeguFunction *fn=[lib newFunctionWithName:[NSString stringWithUTF8String:"nord2_compute_probe"]];assert(fn);
 TeguPipeline *pipeline=[d newComputePipelineStateWithFunction:fn error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];
 TeguBuffer *out=[d newBufferWithLength:1024 options:0],*args=[d newBufferWithLength:64 options:0],*src=[d newBufferWithLength:64 options:0];
 uint32_t *a=[args contents],*s=[src contents];
 /* baseline: a direct dispatch shows the wire format the spy decodes */
 api_spy_dispatches=0;
 TeguCommand *c=[q commandBuffer];TeguEncoder *e=[c computeCommandEncoder];
 [e setComputePipelineState:pipeline];[e setBuffer:out offset:0 atIndex:0];
 [e dispatchThreadgroups:(MTLSize){4,1,1} threadsPerThreadgroup:(MTLSize){8,1,1}];[e endEncoding];
 [c commit];[c waitUntilCompleted];assert([c status]==4&&api_spy_dispatches>=1&&api_spy_groups[0]==4&&api_spy_groups[1]==1&&api_spy_groups[2]==1);
 /* a no-copy buffer bound to a compute dispatch is refreshed from the caller's memory at execution */
 {
  tegu_event_ops=(struct TeguEventOps){api_q_retain,api_q_release,api_q_schedule,api_block_copy,api_block_release,1};
  unsigned char *nm=aligned_alloc(16384,16384);memset(nm,0x10,16384);
  TeguBuffer *nbuf=[d newBufferWithBytesNoCopy:nm length:16384 options:0 deallocator:0];assert(nbuf);
  nm[0]=0xA1;nm[16383]=0xA2;
  c=[q commandBuffer];e=[c computeCommandEncoder];[e setComputePipelineState:pipeline];[e setBuffer:nbuf offset:0 atIndex:0];
  [e dispatchThreadgroups:(MTLSize){1,1,1} threadsPerThreadgroup:(MTLSize){8,1,1}];[e endEncoding];
  nm[1]=0xA3; /* after encode, before commit */
  [c commit];[c waitUntilCompleted];assert([c status]==4);
  assert(((unsigned char *)tegu_buffer_bytes(nbuf))[0]==0xA1&&((unsigned char *)tegu_buffer_bytes(nbuf))[1]==0xA3&&((unsigned char *)tegu_buffer_bytes(nbuf))[16383]==0xA2);
  [nbuf release];free(nm);tegu_event_ops=(struct TeguEventOps){0};
 }
 /* indirect: the grid is produced by an EARLIER job of the same command (copy), after the encode */
 a[0]=a[1]=a[2]=9;s[0]=3;s[1]=2;s[2]=5;
 c=[[q commandBuffer] retain];
 TeguBlitEncoder *b=[c blitCommandEncoder];[b copyFromBuffer:src sourceOffset:0 toBuffer:args destinationOffset:16 size:12];[b endEncoding];
 e=[c computeCommandEncoder];[e setComputePipelineState:pipeline];[e setBuffer:out offset:0 atIndex:0];
 [e dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:16 threadsPerThreadgroup:(MTLSize){8,1,1}];[e endEncoding];
 assert(!c->invalid&&c->count==2);
 unsigned before=api_spy_dispatches;
 /* the app drops every reference before commit: the snapshot owns its bindings */
 [out retain];[out release];
 [c commit];[c waitUntilCompleted];
 assert([c status]==4&&![c error]&&api_spy_dispatches>before);
 assert(api_spy_groups[0]==3&&api_spy_groups[1]==2&&api_spy_groups[2]==5);
 [c release];
 /* zero in any dimension: nothing is dispatched (Metal), command succeeds */
 a[0]=4;a[1]=0;a[2]=1;before=api_spy_dispatches;
 c=[q commandBuffer];e=[c computeCommandEncoder];[e setComputePipelineState:pipeline];[e setBuffer:out offset:0 atIndex:0];
 [e dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:0 threadsPerThreadgroup:(MTLSize){8,1,1}];[e endEncoding];
 [c commit];[c waitUntilCompleted];assert([c status]==4&&api_spy_dispatches==before);
 /* a grid beyond Metal's per-dimension bound fails the command, never a wrap */
 a[0]=70000;a[1]=1;a[2]=1;
 c=[q commandBuffer];e=[c computeCommandEncoder];[e setComputePipelineState:pipeline];[e setBuffer:out offset:0 atIndex:0];
 [e dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:0 threadsPerThreadgroup:(MTLSize){8,1,1}];[e endEncoding];
 [c commit];[c waitUntilCompleted];assert([c status]==5&&[c error]&&api_spy_dispatches==before);
 /* a later event signal still ran for the failed command only through the flush path */
 /* encode-time refusals */
#define API_BADI(...) do{TeguCommand *bc=[q commandBuffer];TeguEncoder *be=[bc computeCommandEncoder];[be setComputePipelineState:pipeline];\
  __VA_ARGS__;assert(bc->invalid);[be endEncoding];}while(0)
 API_BADI([be dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:2 threadsPerThreadgroup:(MTLSize){8,1,1}]);
 API_BADI([be dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:56 threadsPerThreadgroup:(MTLSize){8,1,1}]);
 API_BADI([be dispatchThreadgroupsWithIndirectBuffer:0 indirectBufferOffset:0 threadsPerThreadgroup:(MTLSize){8,1,1}]);
 API_BADI([be dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:0 threadsPerThreadgroup:(MTLSize){0,1,1}]);
 API_BADI([be dispatchThreadgroupsWithIndirectBuffer:(id)fn indirectBufferOffset:0 threadsPerThreadgroup:(MTLSize){8,1,1}]);
#undef API_BADI
 { TeguCommand *bc=[q commandBuffer];TeguEncoder *be=[bc computeCommandEncoder];
   [be dispatchThreadgroupsWithIndirectBuffer:args indirectBufferOffset:0 threadsPerThreadgroup:(MTLSize){8,1,1}];assert(bc->invalid);[be endEncoding]; } /* no pipeline */
 [src release];[args release];[out release];[pipeline release];[fn release];[lib release];[q release];
 d->connection=0;[d release];
}

/* ============================ indirect draws (CPU-written arguments) ============================ */
static void api_test_indirect_draw(TeguComputeDevice *d){
 id error=0;TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];
 TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0];
 TeguTexture *tex=new_sample(d,80,16,16,1,2,1);TeguSampler *smp=new_sampler(d,3);
 TeguBuffer *args=[d newBufferWithLength:64 options:0],*ib=[d newBufferWithLength:64 options:0];
 uint32_t *a=[args contents];uint16_t *idx=[ib contents];idx[0]=0;idx[1]=1;idx[2]=2;
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 struct Dispatch *ind[4],*dir[4];
 /* non-indexed: args {count,instances,start,baseInstance} -> identical request to the direct draw */
 a[0]=3;a[1]=1;a[2]=3;a[3]=0;
 TeguCommand *ci=[q commandBuffer];TeguRenderEncoder *ei=[ci renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ei,vb,pipeline,tex,tex,smp,smp);[ei drawPrimitives:3 indirectBuffer:args indirectBufferOffset:0];[ei endEncoding];
 TeguCommand *cd=[q commandBuffer];TeguRenderEncoder *ed=[cd renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ed,vb,pipeline,tex,tex,smp,smp);[ed drawPrimitives:3 vertexStart:3 vertexCount:3];[ed endEncoding];
 assert(!ci->invalid&&!cd->invalid&&api_draw_jobs(ci,ind,4)==1&&api_draw_jobs(cd,dir,4)==1);
 assert(ind[0]->genericDrawRequest.vertex_count==dir[0]->genericDrawRequest.vertex_count&&ind[0]->genericVertexBase==dir[0]->genericVertexBase&&ind[0]->genericVertexBase==3);
 [ci commit];[ci waitUntilCompleted];assert([ci status]==4&&![ci error]);
 /* instanced + base instance: same outcome as the direct call (valid or refused) */
 a[0]=3;a[1]=2;a[2]=0;a[3]=1;
 ci=[q commandBuffer];ei=[ci renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ei,vb,pipeline,tex,tex,smp,smp);[ei drawPrimitives:3 indirectBuffer:args indirectBufferOffset:0];[ei endEncoding];
 cd=[q commandBuffer];ed=[cd renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ed,vb,pipeline,tex,tex,smp,smp);[ed drawPrimitives:3 vertexStart:0 vertexCount:3 instanceCount:2 baseInstance:1];[ed endEncoding];
 assert(!!ci->invalid==!!cd->invalid&&ci->count==cd->count);
 if(!cd->invalid){unsigned ni=api_draw_jobs(ci,ind,4),nd=api_draw_jobs(cd,dir,4);assert(ni==nd&&ni==1&&ind[0]->genericBaseInstance==dir[0]->genericBaseInstance);}
 /* zero count or zero instances draw nothing and do not fail */
 a[0]=0;a[1]=1;a[2]=0;a[3]=0;
 ci=[q commandBuffer];ei=[ci renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ei,vb,pipeline,tex,tex,smp,smp);[ei drawPrimitives:3 indirectBuffer:args indirectBufferOffset:0];[ei endEncoding];
 assert(!ci->invalid&&api_draw_jobs(ci,ind,4)==0);[ci commit];[ci waitUntilCompleted];assert([ci status]==4);
 /* indexed: args {indexCount,instances,indexStart,baseVertex,baseInstance} */
 a[0]=3;a[1]=1;a[2]=0;a[3]=0;a[4]=0;
 ci=[q commandBuffer];ei=[ci renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ei,vb,pipeline,tex,tex,smp,smp);
 [ei drawIndexedPrimitives:3 indexType:0 indexBuffer:ib indexBufferOffset:0 indirectBuffer:args indirectBufferOffset:0];[ei endEncoding];
 cd=[q commandBuffer];ed=[cd renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ed,vb,pipeline,tex,tex,smp,smp);[ed drawIndexedPrimitives:3 indexCount:3 indexType:0 indexBuffer:ib indexBufferOffset:0];[ed endEncoding];
 assert(!!ci->invalid==!!cd->invalid&&ci->count==cd->count);
 /* a CPU rewrite between encode and commit fails the command instead of drawing something else */
 a[0]=3;a[1]=1;a[2]=0;a[3]=0;
 ci=[q commandBuffer];ei=[ci renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ei,vb,pipeline,tex,tex,smp,smp);[ei drawPrimitives:3 indirectBuffer:args indirectBufferOffset:0];[ei endEncoding];
 assert(!ci->invalid);a[0]=6;
 [ci commit];[ci waitUntilCompleted];assert([ci status]==5&&[ci error]);
 a[0]=3;
 /* unchanged bytes verify and run */
 ci=[q commandBuffer];ei=[ci renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(ei,vb,pipeline,tex,tex,smp,smp);[ei drawPrimitives:3 indirectBuffer:args indirectBufferOffset:0];[ei endEncoding];
 [ci commit];[ci waitUntilCompleted];assert([ci status]==4);
 /* arguments written by an earlier job of the same command (blit) are refused, not guessed */
 ci=[q commandBuffer];TeguBlitEncoder *bl=[ci blitCommandEncoder];[bl fillBuffer:args range:(TeguRange){0,16} value:1];[bl endEncoding];
 ei=[ci renderCommandEncoderWithDescriptor:(id)pass];bind_all(ei,vb,pipeline,tex,tex,smp,smp);
 [ei drawPrimitives:3 indirectBuffer:args indirectBufferOffset:0];[ei endEncoding];assert(ci->invalid);
 a[0]=3;a[1]=1;a[2]=3;a[3]=0;
 /* encode-time refusals */
#define API_BADD(...) do{TeguCommand *bc=[q commandBuffer];TeguRenderEncoder *be=[bc renderCommandEncoderWithDescriptor:(id)pass];assert(be);\
  bind_all(be,vb,pipeline,tex,tex,smp,smp);__VA_ARGS__;assert(bc->invalid);[be endEncoding];}while(0)
 API_BADD([be drawPrimitives:3 indirectBuffer:args indirectBufferOffset:2]);
 API_BADD([be drawPrimitives:3 indirectBuffer:args indirectBufferOffset:56]);
 API_BADD([be drawPrimitives:3 indirectBuffer:0 indirectBufferOffset:0]);
 API_BADD([be drawPrimitives:3 indirectBuffer:(TeguBuffer *)pipeline indirectBufferOffset:0]);
 API_BADD([be drawIndexedPrimitives:3 indexType:2 indexBuffer:ib indexBufferOffset:0 indirectBuffer:args indirectBufferOffset:0]);
 API_BADD([be drawIndexedPrimitives:3 indexType:0 indexBuffer:0 indexBufferOffset:0 indirectBuffer:args indirectBufferOffset:0]);
#undef API_BADD
 [pass release];[ib release];[args release];[smp release];[tex release];[vb release];[target release];
 [pipeline release];[pd release];[pl release];[q release];
}

/* ============================ buffer-backed textures ============================ */
static TextureDesc *api_buf_desc(NSUInteger format,NSUInteger w,NSUInteger h){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=format;desc->w=w;desc->h=h;desc->uses=1;desc->options=0;desc->levels=1;return desc;
}
static void api_test_buffer_texture(TeguComputeDevice *d){
 id error=0;TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0];
 TeguSampler *smp=new_sampler(d,3);
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 TeguBuffer *buf=[d newBufferWithLength:16384 options:0];unsigned char *mem=[buf contents];memset(mem,0,16384);
 TextureDesc *desc=api_buf_desc(80,16,16);
 TeguTexture *t=[buf newTextureWithDescriptor:(id)desc offset:256 bytesPerRow:128];
 assert(t&&[t buffer]==buf&&[t bufferOffset]==256&&[t bufferBytesPerRow]==128&&[t width]==16&&[t height]==16&&[t pixelFormat]==80&&[t device]==d);
 /* CPU -> texture lands in the buffer's own bytes at (offset, row pitch) */
 uint32_t px[16*16];for(unsigned i=0;i<256;i++)px[i]=0x01020304u*(i+1);
 [t replaceRegion:(TeguRegion){{0,0,0},{16,16,1}} mipmapLevel:0 withBytes:px bytesPerRow:64];
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){uint32_t v;memcpy(&v,mem+256+y*128+x*4,4);assert(v==px[y*16+x]);}
 assert(mem[255]==0&&mem[256+15*128+64]==0); /* nothing outside the described rows */
 /* buffer -> texture (getBytes sees the app's later writes) */
 mem[256+3*128+8]=0xAB;
 uint32_t back[16*16];[t getBytes:back bytesPerRow:64 fromRegion:(TeguRegion){{0,0,0},{16,16,1}} mipmapLevel:0];
 assert(((unsigned char *)back)[3*64+8]==0xAB);
 /* a draw samples it and the command completes */
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(e,vb,pipeline,t,t,smp,smp);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
 assert(!c->invalid);[c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
 /* views keep the answers; releasing the buffer first does not free the memory under the texture */
 TeguTexture *v=[t newTextureViewWithPixelFormat:80];assert(v&&[v buffer]==buf&&[v bufferOffset]==256);
 [buf release];drain();
 [t getBytes:back bytesPerRow:64 fromRegion:(TeguRegion){{0,0,0},{16,16,1}} mipmapLevel:0];assert(((unsigned char *)back)[3*64+8]==0xAB);
 [v release];[t release];
 /* an ordinary texture has no parent buffer */
 TeguTexture *own=new_sample(d,80,16,16,1,2,1);assert(![own buffer]&&![own bufferOffset]&&![own bufferBytesPerRow]);[own release];
 /* refusals: Metal's rules plus what the layout can address */
 TeguBuffer *b2=[d newBufferWithLength:16384 options:0];
 assert(![b2 newTextureWithDescriptor:(id)desc offset:32 bytesPerRow:128]);          /* offset alignment */
 assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:100]);           /* row pitch alignment */
 assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:0]);
 assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:32]);            /* narrower than a row */
 assert(![b2 newTextureWithDescriptor:(id)desc offset:16384 bytesPerRow:64]);        /* outside */
 assert(![b2 newTextureWithDescriptor:(id)desc offset:12288 bytesPerRow:1024]);      /* last row crosses the end */
 { TeguTexture *edge=[b2 newTextureWithDescriptor:(id)desc offset:16384-(15*1024+64) bytesPerRow:1024];assert(edge);[edge release]; } /* exact fit */
 desc->levels=2;assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:64]);desc->levels=1;
 desc->type=5;assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:64]);desc->type=2;
 desc->samples=4;assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:64]);desc->samples=1;
 desc->options=0x20;assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:64]);desc->options=0; /* storage mode differs */
 desc->format=204;assert(![b2 newTextureWithDescriptor:(id)desc offset:0 bytesPerRow:64]);desc->format=80;     /* block-compressed */
 assert(![b2 newTextureWithDescriptor:0 offset:0 bytesPerRow:64]);
 { TeguTexture *lin=[b2 newLinearTextureWithDescriptor:(id)desc offset:0 bytesPerRow:64 bytesPerImage:0];assert(lin&&[lin buffer]==b2);[lin release]; }
 [desc release];[b2 release];
 [pass release];[smp release];[vb release];[target release];[pipeline release];[pd release];[pl release];[q release];
}


/* ============================ randomized API misuse (never crash, never leak) ============================ */
static uint64_t api_rng_state=0x9e3779b97f4a7c15ull;
static unsigned api_rand(unsigned n){api_rng_state^=api_rng_state<<13;api_rng_state^=api_rng_state>>7;api_rng_state^=api_rng_state<<17;return (unsigned)((api_rng_state>>11)%n);}
static void api_test_fuzz(TeguComputeDevice *d,unsigned iterations){
 id error=0;TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];q->blockCopy=api_block_copy;q->blockRelease=api_block_release;
 TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0],*args=[d newBufferWithLength:64 options:0];
 TeguTexture *tex=new_sample(d,80,16,16,1,2,1);TeguSampler *smp=new_sampler(d,3);
 TeguFence *fence=[d newFence];TeguSharedEvent *ev=[d newSharedEvent];TeguEvent *pev=[d newEvent];
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 uint32_t *a=[args contents];
 { uint16_t *half=[vb contents];for(unsigned i=0;i<128;i++)half[i]=0x4000; } /* factor 2.0 */
 static struct TeguTessInfo fuzzInfo={TESS_QUAD,4,TESS_FRACTIONAL_EVEN,TESS_CLOCKWISE,1,0,30,8.0f};
 tegu_tess_test_info=&fuzzInfo;
 id objs[8]={vb,args,tex,smp,fence,ev,pev,(id)pipeline};
 unsigned done=0,failed=0;
 for(unsigned it=0;it<iterations;it++){
  a[0]=1+api_rand(4);a[1]=api_rand(3);a[2]=api_rand(6);a[3]=0;a[4]=0;
  TeguCommand *c=[q commandBuffer];
  unsigned steps=1+api_rand(6);
  for(unsigned s=0;s<steps;s++){
   switch(api_rand(8)){
    case 0:{[c encodeSignalEvent:objs[api_rand(8)] value:1+api_rand(5)];break;}
    case 1:{[c encodeWaitForEvent:api_rand(4)?(id)ev:objs[api_rand(8)] value:api_rand(3)];break;} /* ev starts at 0 or higher: waits <=2 only after signals */
    case 2:{TeguEncoder *e=[c computeCommandEncoder];if(!e)break;
      TeguBuffer *l[3]={vb,args,0};NSUInteger o[3]={4*api_rand(4),0,api_rand(3)*4};
      [e setBuffers:l offsets:o withRange:(struct TeguComputeRange){api_rand(33),api_rand(4)}];
      [e setSamplerState:smp lodMinClamp:(float)api_rand(3) lodMaxClamp:(float)api_rand(5) atIndex:api_rand(18)];
      [e useResource:objs[api_rand(8)] usage:api_rand(9)];[e memoryBarrierWithScope:api_rand(9)];
      [e updateFence:objs[api_rand(8)]];[e waitForFence:fence];
      if(api_rand(3)==0)[e dispatchThreadgroupsWithIndirectBuffer:objs[api_rand(8)] indirectBufferOffset:api_rand(80) threadsPerThreadgroup:(MTLSize){api_rand(3),1,1}];
      if(api_rand(8))[e endEncoding];break;}
    case 3:{TeguBlitEncoder *b=[c blitCommandEncoder];if(!b)break;
      [b fillBuffer:vb range:(TeguRange){api_rand(300),api_rand(100)} value:(unsigned char)api_rand(256)];
      [b updateFence:objs[api_rand(8)]];[b waitForFence:fence];
      [b copyFromBuffer:vb sourceOffset:api_rand(300) toBuffer:args destinationOffset:api_rand(80) size:api_rand(40)];
      if(api_rand(8))[b endEncoding];break;}
    case 4:{TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];if(!e)break;
      bind_all(e,vb,pipeline,tex,tex,smp,smp);
      TeguViewport vp={0,0,16,16,0,1};[e setViewports:&vp count:1+api_rand(2)];
      [e setFragmentSamplerState:smp lodMinClamp:(float)api_rand(3) lodMaxClamp:(float)api_rand(3) atIndex:api_rand(18)];
      [e updateFence:fence afterStages:api_rand(5)];[e useResources:objs count:api_rand(9) usage:1+api_rand(8)];
      [e setDepthStoreAction:api_rand(3)];[e setColorStoreActionOptions:api_rand(2) atIndex:api_rand(5)];
      switch(api_rand(5)){
       case 0:[e drawPrimitives:3 vertexStart:api_rand(4) vertexCount:3];break;
       case 1:[e drawPrimitives:3 indirectBuffer:objs[api_rand(8)] indirectBufferOffset:api_rand(80)];break;
       case 2:a[0]=api_rand(7);[e drawIndexedPrimitives:3 indexType:api_rand(3) indexBuffer:vb indexBufferOffset:api_rand(8)*2 indirectBuffer:args indirectBufferOffset:4*api_rand(4)];break;
       case 3:{ /* tessellation, argument-buffer residency and refused feature families with random arguments */
         [e setTessellationFactorBuffer:api_rand(4)?(id)vb:objs[api_rand(8)] offset:api_rand(40) instanceStride:4*api_rand(5)];
         [e setTessellationFactorScale:(float)api_rand(4)-0.5f];
         switch(api_rand(5)){
          case 0:[e drawPatches:1+api_rand(5) patchStart:api_rand(3) patchCount:api_rand(3) patchIndexBuffer:api_rand(2)?(id)0:objs[api_rand(8)] patchIndexBufferOffset:api_rand(10) instanceCount:api_rand(3) baseInstance:api_rand(3)];break;
          case 1:a[0]=api_rand(4);a[1]=api_rand(3);a[2]=api_rand(3);a[3]=api_rand(2);[e drawPatches:4 patchIndexBuffer:0 patchIndexBufferOffset:0 indirectBuffer:objs[api_rand(8)] indirectBufferOffset:api_rand(80)];break;
          case 2:[e drawIndexedPatches:4 patchStart:0 patchCount:api_rand(3) patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:objs[api_rand(8)] controlPointIndexBufferOffset:api_rand(20) instanceCount:1 baseInstance:0];break;
          case 3:[e setVertexAmplificationCount:2 viewMappings:0];[e setObjectBytes:0 length:0 atIndex:0];break;
          default:[e useHeap:objs[api_rand(8)]];[e useResource:objs[api_rand(8)] usage:api_rand(9)];break;
         }
         break;}
       default:break;}
      if(api_rand(8))[e endEncoding];break;}
    case 5:{TeguParallelRenderEncoder *p=[c parallelRenderCommandEncoderWithDescriptor:(id)pass];if(!p)break;
      unsigned n=api_rand(4);TeguRenderSubEncoder *subs[4]={0};
      for(unsigned k=0;k<n;k++){subs[k]=[p renderCommandEncoder];if(!subs[k])continue;
       [subs[k] setRenderPipelineState:pipeline];[subs[k] setVertexBuffer:vb offset:0 atIndex:0];
       [subs[k] setFragmentTexture:tex atIndex:0];[subs[k] setFragmentSamplerState:smp atIndex:0];
       if(api_rand(2))[subs[k] setViewport:(TeguViewport){1,1,8,8,0,1}];
       if(api_rand(2))[subs[k] setCullMode:api_rand(3)];
       [subs[k] drawPrimitives:3 vertexStart:0 vertexCount:3];
       if(api_rand(6))[subs[k] endEncoding];}
      if(api_rand(8))[p endEncoding];break;}
    case 6:{[ev setSignaledValue:api_rand(4)];break;}
    default:{[c pushDebugGroup:0];[c popDebugGroup];break;}
   }
  }
  if(api_rand(10)){
   /* only commit commands whose waits are satisfiable: pre-signal the shared event high */
   [ev setSignaledValue:100];[c commit];[c waitUntilCompleted];
   if([c status]==4)done++;else failed++;
   [ev setSignaledValue:0];
  }
  if(it%64==63)drain();
 }
 fprintf(stderr,"FUZZ iterations=%u completed=%u failed=%u\n",iterations,done,failed);
 assert(done>iterations/20&&failed>iterations/20); /* both outcomes exercised */
 drain();
 q->blockCopy=0;q->blockRelease=0;tegu_tess_test_info=0;
 [pass release];[ev release];[pev release];[fence release];[smp release];[tex release];[args release];[vb release];[target release];
 [pipeline release];[pd release];[pl release];[q release];
}

static void api_test_resource_options(TeguComputeDevice *d){
 static const NSUInteger good[]={0,0x1,0x20,0x21,0x100,0x200,0x101,0x120,0x220,0x121,0x221};
 static const NSUInteger bad[]={0x10,0x11,0x30,0x2,0x3,0x300,0x400,0x1000,0x40,0x15};
 id error=0;TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];TeguBuffer *vb=[d newBufferWithLength:256 options:0];TeguSampler *smp=new_sampler(d,3);
 for(unsigned i=0;i<sizeof(good)/sizeof(good[0]);i++){
  NSUInteger o=good[i];
  TeguBuffer *b=[d newBufferWithLength:256 options:o];
  assert(b&&[b resourceOptions]==o&&[b storageMode]==((o>>4)&15)&&[b cpuCacheMode]==(o&15)&&[b hazardTrackingMode]==((o>>8)&3));
  assert((((o>>4)&15)==2)==([b contents]==0)); /* private storage has no CPU pointer */
  if(((o>>4)&15)==0){memset([b contents],0x5c,256);assert(((unsigned char *)[b contents])[255]==0x5c);}
  /* the blit engine still reaches private storage */
  TeguCommand *c=[q commandBuffer];TeguBlitEncoder *bl=[c blitCommandEncoder];[bl fillBuffer:b range:(TeguRange){0,64} value:0x77];[bl endEncoding];
  [c commit];[c waitUntilCompleted];assert([c status]==4&&((unsigned char *)tegu_buffer_bytes(b))[63]==0x77);
  [b release];
  TeguBuffer *w=[d newBufferWithBytes:"abcdefgh" length:8 options:o];
  assert(!!w==(((o>>4)&15)==0));
  if(w){assert(!memcmp([w contents],"abcdefgh",8));[w release];}
  /* textures: sampled, and rendered into */
  TextureDesc *x=api_buf_desc(80,16,16);x->options=o;x->uses=1;
  TeguTexture *ts=[d newTextureWithDescriptor:(id)x];
  assert(ts&&[ts resourceOptions]==o&&[ts cpuCacheMode]==(o&15)&&[ts hazardTrackingMode]==((o>>8)&3)&&[ts storageMode]==((o>>4)&15));
  x->uses=4;TeguTexture *tr=[d newTextureWithDescriptor:(id)x];assert(tr&&[tr resourceOptions]==o);
  RenderPass *pass=[[RenderPass alloc] init];
  pass->color=[[PassColor alloc] init];pass->color->target=[tr retain];
  pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
  c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e);
  bind_all(e,vb,pipeline,ts,ts,smp,smp);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
  assert(!c->invalid);[c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
  [pass release];[tr release];[ts release];[x release];
 }
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
  NSUInteger o=bad[i];
  assert(![d newBufferWithLength:256 options:o]);
  TextureDesc *x=api_buf_desc(80,16,16);x->options=o;
  TeguTexture *t=[d newTextureWithDescriptor:(id)x];
  assert(!t||o==0x30); /* memoryless is a valid texture mode */
  [t release];[x release];
 }
 assert(![d newBufferWithLength:0 options:0x100]);
 /* IOSurface imports take the same cache/hazard bits (outcome identical to options 0, options echoed) */
 {
  test_surface.width=64;test_surface.height=64;test_surface.row=256;test_surface.alloc=256*64;test_surface.planes=0;
  test_surface.fourcc=0x42475241u;memset(test_surface.data,0x33,sizeof(test_surface.data));
  static const NSUInteger io[]={0x100,0x1,0x101,0x200};
  TextureDesc *x=api_buf_desc(80,64,64);x->options=0;
  TeguTexture *base=[d newTextureWithDescriptor:(id)x iosurface:(id)&test_surface plane:0];assert(base); /* sampled BGRA import works with options 0 */
  for(unsigned i=0;i<sizeof(io)/sizeof(io[0]);i++){
   x->options=io[i];TeguTexture *t=[d newTextureWithDescriptor:(id)x iosurface:(id)&test_surface plane:0];
   assert(!!t==!!base);
   if(t){assert([t resourceOptions]==io[i]&&[t iosurface]);[t release];}
  }
  x->options=0x300;assert(![d newTextureWithDescriptor:(id)x iosurface:(id)&test_surface plane:0]);
  x->options=0x2;assert(![d newTextureWithDescriptor:(id)x iosurface:(id)&test_surface plane:0]);
  [base release];[x release];
 }
 /* heaps: write-combined and untracked are accepted, resources inherit storage+cache from the heap */
 {
  ApiHeapDescriptor *hd=[[ApiHeapDescriptor alloc] init];hd->size=262144;hd->cpu=1;hd->hazard=1;
  TeguHeap *h=[d newHeapWithDescriptor:(id)hd];assert(h&&[h cpuCacheMode]==1&&[h hazardTrackingMode]==1&&[h resourceOptions]==0x101);
  TeguBuffer *hb=[h newBufferWithLength:100 options:0x101];assert(hb&&[hb resourceOptions]==0x101&&[hb heap]==h);
  TeguBuffer *hb2=[h newBufferWithLength:100 options:0x201];assert(hb2&&[hb2 hazardTrackingMode]==2); /* hazard mode may differ per resource */
  assert(![h newBufferWithLength:100 options:0x100]);   /* cache mode must match the heap */
  assert(![h newBufferWithLength:100 options:0x21]);    /* storage mode must match the heap */
  TextureDesc *x=api_buf_desc(80,16,16);x->options=0x101;
  TeguTexture *ht=[h newTextureWithDescriptor:(id)x];assert(ht&&[ht resourceOptions]==0x101&&[ht heap]==h);
  x->options=0x201;TeguTexture *ht2=[h newTextureWithDescriptor:(id)x];assert(ht2&&[ht2 hazardTrackingMode]==2);
  x->options=0x1;{TeguTexture *ht3=[h newTextureWithDescriptor:(id)x];assert(ht3&&[ht3 hazardTrackingMode]==0);[ht3 release];}
  x->options=0x100;assert(![h newTextureWithDescriptor:(id)x]); /* cache mode differs from the heap */
  [x release];[ht2 release];[ht release];[hb2 release];[hb release];[h release];
  hd->hazard=3;assert(![d newHeapWithDescriptor:(id)hd]);hd->hazard=0;hd->cpu=2;assert(![d newHeapWithDescriptor:(id)hd]);
  [hd release];
 }
 [smp release];[vb release];[pipeline release];[pd release];[pl release];[q release];
}

/* ============================ events with asynchronous kernel submission ============================ */
static struct fake *api_async_fake;static unsigned api_async_signals,api_async_pending_at_signal;
@interface ApiCheckEvent : NSObject { @public U64 value; }
- (U64)signaledValue;- (void)setSignaledValue:(U64)v;
@end
@implementation ApiCheckEvent
- (U64)signaledValue{return __atomic_load_n(&value,__ATOMIC_ACQUIRE);}
- (void)setSignaledValue:(U64)v{api_async_signals++;api_async_pending_at_signal+=api_async_fake->pending;__atomic_store_n(&value,v,__ATOMIC_RELEASE);}
@end
static void api_test_async_events(void){
 struct fake f={.job=1ull<<32,.seq_model=1};uintptr_t bridge=open_bridge(&f);
 assert(n2mb_async(bridge,1)==0&&n2mb_async(bridge,2)==1);
 TeguComputeDevice *d=api_device(&f,bridge);
 TeguLibrary *pl=api_probe_library(d);RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 id error=0;TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0];
 TeguTexture *tex=new_sample(d,80,16,16,1,2,1);TeguSampler *smp=new_sampler(d,3);
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 ApiCheckEvent *ev=[[ApiCheckEvent alloc] init];api_async_fake=&f;api_async_signals=api_async_pending_at_signal=0;
 for(unsigned k=0;k<3;k++){
  TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
  bind_all(e,vb,pipeline,tex,tex,smp,smp);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
  [c encodeSignalEvent:(id)ev value:10+k];
  TeguRenderEncoder *e2=[c renderCommandEncoderWithDescriptor:(id)pass];
  bind_all(e2,vb,pipeline,tex,tex,smp,smp);[e2 drawPrimitives:3 vertexStart:0 vertexCount:3];[e2 endEncoding];
  [c encodeSignalEvent:(id)ev value:20+k];
  assert(!c->invalid);[c commit];[c waitUntilCompleted];assert([c status]==4&&![c error]);
  assert(!f.pending);
 }
 /* with GPU jobs queued before each signal, the signal only runs after they were reaped */
 assert(api_async_signals==6&&api_async_pending_at_signal==0&&f.submitted>=6);
 [ev release];[pass release];[smp release];[tex release];[vb release];[target release];[q release];[pipeline release];[pd release];[pl release];
 d->connection=0;[d release];drain();close_bridge(bridge,&f);
}

/* ============================ newBufferWithBytesNoCopy ============================ */
static unsigned api_nocopy_freed;static void *api_nocopy_ptr;static NSUInteger api_nocopy_len;
static void api_test_nocopy(TeguComputeDevice *d){
 tegu_event_ops=(struct TeguEventOps){api_q_retain,api_q_release,api_q_schedule,api_block_copy,api_block_release,1};
 api_block_copies=api_block_releases=0;api_nocopy_freed=0;
 id error=0;TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];TeguTexture *target=new_target(d);
 TeguTexture *tex=new_sample(d,80,16,16,1,2,1);TeguSampler *smp=new_sampler(d,3);
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 unsigned char *mem=aligned_alloc(16384,32768);assert(mem);for(unsigned i=0;i<32768;i++)mem[i]=(unsigned char)(i*7);
 TeguBuffer *nb=[d newBufferWithBytesNoCopy:mem length:32768 options:0 deallocator:^(void *p,NSUInteger n){api_nocopy_freed++;api_nocopy_ptr=p;api_nocopy_len=n;}];
 assert(nb&&[nb contents]==mem&&[nb length]==32768&&nb->external==mem);
 assert(!memcmp(tegu_buffer_bytes(nb),mem,32768)&&tegu_buffer_bytes(nb)!=mem); /* the BO holds an initial copy */
 /* app writes after creation; a blit from the buffer sees them (copy-in at execution) */
 memset(mem,0x42,100);
 TeguBuffer *dst=[d newBufferWithLength:4096 options:0];memset([dst contents],0,4096);
 TeguCommand *c=[q commandBuffer];TeguBlitEncoder *b=[c blitCommandEncoder];
 [b copyFromBuffer:nb sourceOffset:0 toBuffer:dst destinationOffset:0 size:100];[b endEncoding];
 [c commit];[c waitUntilCompleted];assert([c status]==4&&((unsigned char *)[dst contents])[99]==0x42&&((unsigned char *)[dst contents])[100]!=0x42);
 /* blit writes come back to the caller's memory: exactly the written range, nothing else */
 mem[5000]=0x77;
 c=[q commandBuffer];b=[c blitCommandEncoder];
 [b fillBuffer:nb range:(TeguRange){200,64} value:0x99];
 memset([dst contents],0x5a,50);[b copyFromBuffer:dst sourceOffset:0 toBuffer:nb destinationOffset:1000 size:50];[b endEncoding];
 mem[5001]=0x78; /* app write between encode and commit: preserved (copy-in then copy-back of ranges only) */
 [c commit];[c waitUntilCompleted];assert([c status]==4);
 for(unsigned i=200;i<264;i++)assert(mem[i]==0x99);
 for(unsigned i=1000;i<1050;i++)assert(mem[i]==0x5a);
 assert(mem[5000]==0x77&&mem[5001]==0x78&&mem[99]==0x42&&mem[264]==(unsigned char)(264*7));
 /* a draw binding it as a vertex buffer: the GPU copy is refreshed from the caller's latest bytes */
 mem[0]=0xEE;mem[255]=0xDD;
 c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
 bind_all(e,nb,pipeline,tex,tex,smp,smp);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
 assert(!c->invalid);mem[1]=0xEF;
 [c commit];[c waitUntilCompleted];assert([c status]==4);
 /* (vt_vertex does not read the vertex buffer, so the planner does not bind it: nothing to refresh here;
  * a compute dispatch that does bind one is checked in api_test_indirect_dispatch) */
 /* a texture over the buffer samples the caller's pixels */
 { TextureDesc *desc=api_buf_desc(80,16,16);
   TeguTexture *bt=[nb newTextureWithDescriptor:(id)desc offset:8192 bytesPerRow:64];assert(bt&&[bt buffer]==nb);
   mem[8192+5]=0xC1;
   c=[q commandBuffer];e=[c renderCommandEncoderWithDescriptor:(id)pass];
   bind_all(e,dst,pipeline,bt,bt,smp,smp);[e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];
   [c commit];[c waitUntilCompleted];assert([c status]==4&&((unsigned char *)tegu_buffer_bytes(nb))[8192+5]==0xC1);
   [bt release];[desc release]; }
 /* creation refusals: unaligned, empty, private/managed/memoryless storage, undefined bits */
 assert(![d newBufferWithBytesNoCopy:mem+16 length:16384 options:0 deallocator:0]);
 assert(![d newBufferWithBytesNoCopy:mem length:0 options:0 deallocator:0]);
 assert(![d newBufferWithBytesNoCopy:0 length:16384 options:0 deallocator:0]);
 assert(![d newBufferWithBytesNoCopy:mem length:16384 options:0x20 deallocator:0]);
 assert(![d newBufferWithBytesNoCopy:mem length:16384 options:0x10 deallocator:0]);
 assert(![d newBufferWithBytesNoCopy:mem length:16384 options:0x400 deallocator:0]);
 assert(api_nocopy_freed==0); /* a refused creation never calls the deallocator */
 /* a second buffer without deallocator, write-combined + untracked bits accepted */
 { TeguBuffer *plain=[d newBufferWithBytesNoCopy:mem+16384 length:16384 options:0x101 deallocator:0];assert(plain&&[plain contents]==mem+16384&&[plain resourceOptions]==0x101);[plain release]; }
 assert(api_nocopy_freed==0);
 /* release the app reference while a committed command may still hold it: the deallocator runs once, after the last user */
 TeguCommand *held=[[q commandBuffer] retain];b=[held blitCommandEncoder];
 [b copyFromBuffer:nb sourceOffset:0 toBuffer:dst destinationOffset:0 size:16];[b endEncoding];
 [nb release];drain();assert(api_nocopy_freed==0); /* the command still retains it */
 [held commit];[held waitUntilCompleted];assert([held status]==4);
 [held release];drain();
 assert(api_nocopy_freed==1&&api_nocopy_ptr==mem&&api_nocopy_len==32768);
 assert(api_block_copies==api_block_releases);
 free(mem);[dst release];[pass release];[smp release];[tex release];[target release];[q release];[pipeline release];[pd release];[pl release];
 tegu_event_ops=(struct TeguEventOps){0};
 assert(!tegu_nocopy_live);
}

/* ============================ tessellation: generator bytes + encoder state + draw glue ============================ */
static void api_half_quad(uint16_t *dst,uint16_t e0,uint16_t e1,uint16_t e2,uint16_t e3,uint16_t i0,uint16_t i1){
 dst[0]=e0;dst[1]=e1;dst[2]=e2;dst[3]=e3;dst[4]=i0;dst[5]=i1;
}
static unsigned api_patch_vertices(const struct TeguTessInfo *info,float scale,const uint16_t *half){
 float edge[4],inside[2];tess_read_factors(info->domain,(const uint8_t *)half,scale,edge,inside);
 TessConfig cfg={info->domain,info->partition,info->winding,info->maxFactor};TessOut o={0,0,0,0,0,0,0,0};
 if(tess_patch(&cfg,edge,inside,&o))return 0;
 return o.vertex_count;
}
static void api_test_tessellation(TeguComputeDevice *d){
 const uint16_t H1=0x3c00,H2=0x4000,H3=0x4200,H4=0x4400,H0=0;
 struct TeguTessInfo quad={TESS_QUAD,4,TESS_INTEGER,TESS_COUNTER_CLOCKWISE,0,0,30,16.0f};
 unsigned char *v;uint32_t nv,*ix,ni;
 uint16_t f[4][6];
 api_half_quad(f[0],H2,H2,H2,H2,H3,H3);api_half_quad(f[1],H4,H4,H4,H4,H4,H4);
 api_half_quad(f[2],H0,H2,H2,H2,H2,H2);api_half_quad(f[3],H1,H1,H1,H1,H1,H1);
 unsigned c0=api_patch_vertices(&quad,1.0f,f[0]),c1=api_patch_vertices(&quad,1.0f,f[1]),c3=api_patch_vertices(&quad,1.0f,f[3]);
 assert(c0&&c1&&c3==4&&c0!=c1&&!api_patch_vertices(&quad,1.0f,f[2]));
 const uint32_t stride=tegu_tess_stride(4);assert(stride==36&&tegu_tess_groups(4)==1&&tegu_tess_groups(5)==2&&tegu_tess_groups(1)==1&&tegu_tess_stride(32)==148);
#define REC(i) ((const uint32_t *)(const void *)(v+(size_t)(i)*stride))
#define POS(i) ((const float *)(const void *)(v+(size_t)(i)*stride+8+16))
 /* stream layout the pipeline must append */
 { TeguAIRVertexAttribute at[16];TeguAIRVertexLayout lay;
   unsigned na=tegu_tess_stream_layout(&quad,at,&lay);
   assert(na==4&&at[0].index==0&&at[0].offset==0&&at[1].index==1&&at[1].offset==4&&at[2].index==2&&at[2].offset==8&&at[2].format==39);
   assert(at[3].index==30&&at[3].offset==24&&at[3].format==30&&lay.stride==36&&lay.index==30&&lay.step_function==1);
   struct TeguTessInfo big=quad;big.controlPoints=9;na=tegu_tess_stream_layout(&big,at,&lay);
   assert(na==6&&at[4].index==4&&at[4].offset==40&&at[5].index==30&&at[5].offset==56&&lay.stride==tegu_tess_stride(9)); }
 /* constant step: one factor struct for every patch and instance; control points patch*N+k; base instance in the instance word */
 assert(!tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),0,0,1.0f,0,3,0,2,5,0,0,0,&v,&nv,&ix,&ni));
 assert(nv==6*c0&&ni%3==0&&ni>0);
 for(uint32_t i=0;i<ni;i++)assert(ix[i]<nv);
 for(uint32_t k=0;k<6;k++)for(uint32_t j=0;j<c0;j++) {
  const uint32_t *r=REC(k*c0+j);
  assert(r[0]==k%3&&r[1]==5+k/3);
  for(uint32_t c=0;c<4;c++)assert(r[2+c]==(k%3)*4+c);
  assert(POS(k*c0+j)[2]==0.0f&&POS(k*c0+j)[0]>=0&&POS(k*c0+j)[0]<=1&&POS(k*c0+j)[1]>=0&&POS(k*c0+j)[1]<=1);
 }
 for(uint32_t t=0;t<ni/3;t++) { /* every triangle stays inside its own patch's vertex range */
  uint32_t blk=ix[3*t]/c0;assert(ix[3*t+1]/c0==blk&&ix[3*t+2]/c0==blk);
 }
 free(v);free(ix);
 /* perPatch: patch 1 has distinct factors, patch 2 is culled (edge 0), patch 3 is 1,1,1,1 */
 quad.stepFunction=1;
 assert(!tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),0,0,1.0f,0,4,0,1,0,0,0,0,&v,&nv,&ix,&ni));
 assert(nv==c0+c1+c3);
 assert(REC(0)[0]==0&&REC(c0)[0]==1&&REC(c0+c1)[0]==3);
 free(v);free(ix);
 /* patch index remap + factor offset */
 { uint32_t remap[4]={3,1,0,3};
   assert(!tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),0,0,1.0f,1,3,remap,1,0,0,0,0,&v,&nv,&ix,&ni));
   assert(nv==c1+c0+c3&&REC(0)[0]==1&&REC(c1)[0]==0&&REC(c1+c0)[0]==3&&REC(0)[2]==4);free(v);free(ix); }
 /* perInstance: instanceStride selects the struct per instance */
 quad.stepFunction=2;
 assert(!tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),0,12,1.0f,0,1,0,2,0,0,0,0,&v,&nv,&ix,&ni));
 assert(nv==c0+c1&&REC(0)[1]==0&&REC(c0)[1]==1);free(v);free(ix);
 /* perPatchAndPerInstance with a base offset */
 quad.stepFunction=3;
 assert(!tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),12,24,1.0f,0,1,0,2,0,0,0,0,&v,&nv,&ix,&ni));
 assert(nv==c1+c3);free(v);free(ix); /* instance0 reads f[1], instance1 reads f[3] */
 /* reads outside the factor buffer, overflow in the addressing, and a short buffer are refused */
 quad.stepFunction=1;
 assert(tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),0,0,1.0f,0,5,0,1,0,0,0,0,&v,&nv,&ix,&ni)==-1);
 assert(tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),sizeof(f)-8,0,1.0f,0,1,0,1,0,0,0,0,&v,&nv,&ix,&ni)==-1);
 quad.stepFunction=3;
 assert(tegu_tess_generate(&quad,(const unsigned char *)f,sizeof(f),0,~(NSUInteger)0,1.0f,0,1,0,3,0,0,0,0,&v,&nv,&ix,&ni)==-1);
 quad.stepFunction=1;
 /* no factor buffer: defaults of 1 (two triangles per patch); scale multiplies */
 assert(!tegu_tess_generate(&quad,0,0,0,0,1.0f,0,5,0,1,0,0,0,0,&v,&nv,&ix,&ni));assert(nv==20&&ni==30);free(v);free(ix);
 assert(!tegu_tess_generate(&quad,(const unsigned char *)f[3],12,0,0,4.0f,0,1,0,1,0,0,0,0,&v,&nv,&ix,&ni));
 assert(nv==25&&ni==96);free(v);free(ix); /* 4x4 segments, integer */
 /* all patches culled: success with no output; a scale of 0 culls everything */
 quad.stepFunction=0;
 assert(!tegu_tess_generate(&quad,(const unsigned char *)f[2],12,0,0,1.0f,0,3,0,1,0,0,0,0,&v,&nv,&ix,&ni)&&!v&&!ix&&!nv&&!ni);
 assert(!tegu_tess_generate(&quad,(const unsigned char *)f[0],12,0,0,0.0f,0,3,0,1,0,0,0,0,&v,&nv,&ix,&ni)&&!v&&!nv);
 /* control point index buffer: uint16 and uint32 values, offset, out-of-range reads refused */
 { struct TeguTessInfo ci=quad;ci.stepFunction=0;
   uint16_t c16[16];for(unsigned i=0;i<16;i++)c16[i]=(uint16_t)(100+i*3);
   ci.controlPointIndexType=1;
   assert(!tegu_tess_generate(&ci,(const unsigned char *)f[3],12,0,0,1.0f,1,2,0,1,0,(const unsigned char *)c16,sizeof(c16),4,&v,&nv,&ix,&ni));
   /* patch 1: elements 4..7 at byte offset 4 -> c16[(4+4)/... ] = c16[2+4..2+7] */
   assert(nv==8&&REC(0)[0]==1&&REC(0)[2]==c16[2+4]&&REC(0)[3]==c16[2+5]&&REC(0)[5]==c16[2+7]&&REC(4)[0]==2&&REC(4)[2]==c16[2+8]);free(v);free(ix);
   assert(tegu_tess_generate(&ci,(const unsigned char *)f[3],12,0,0,1.0f,1,3,0,1,0,(const unsigned char *)c16,sizeof(c16),4,&v,&nv,&ix,&ni)==-1);
   uint32_t c32[8]={7,8,9,10,11,12,13,0xdeadbeef};ci.controlPointIndexType=2;
   assert(!tegu_tess_generate(&ci,(const unsigned char *)f[3],12,0,0,1.0f,0,1,0,1,0,(const unsigned char *)c32,sizeof(c32),0,&v,&nv,&ix,&ni));
   assert(REC(0)[2]==7&&REC(0)[5]==10);free(v);free(ix);
   assert(tegu_tess_generate(&ci,(const unsigned char *)f[3],12,0,0,1.0f,1,1,0,1,0,(const unsigned char *)c32,sizeof(c32),0,&v,&nv,&ix,&ni)==0);
   free(v);free(ix);
   assert(tegu_tess_generate(&ci,(const unsigned char *)f[3],12,0,0,1.0f,2,1,0,1,0,(const unsigned char *)c32,sizeof(c32),0,&v,&nv,&ix,&ni)==-1);
   /* 9 control points: two uvec4 groups, lanes past N are zero */
   struct TeguTessInfo nine=quad;nine.controlPoints=9;nine.stepFunction=0;
   assert(!tegu_tess_generate(&nine,(const unsigned char *)f[3],12,0,0,1.0f,2,1,0,1,0,0,0,0,&v,&nv,&ix,&ni));
   { const uint32_t *r=(const uint32_t *)(const void *)v;
     for(uint32_t k=0;k<9;k++)assert(r[2+k]==2*9+k);
     for(uint32_t k=9;k<12;k++)assert(r[2+k]==0);
     assert(((const float *)(const void *)v)[2+12+2]==0.0f); }
   free(v);free(ix); }
 /* the output cap fails cleanly */
 quad.stepFunction=1;
 { uint16_t big[6];api_half_quad(big,0x5400,0x5400,0x5400,0x5400,0x5400,0x5400);
   struct TeguTessInfo wide=quad;wide.maxFactor=64.0f;wide.stepFunction=0;
   assert(tegu_tess_generate(&wide,(const unsigned char *)big,12,0,0,1.0f,0,2000,0,1,0,0,0,0,&v,&nv,&ix,&ni)==-2&&!v&&!ix); }
 /* triangle domain and a fractional partition run through the same path */
 { struct TeguTessInfo tri={TESS_TRIANGLE,3,TESS_FRACTIONAL_ODD,TESS_CLOCKWISE,0,0,30,16.0f};
   uint16_t tf[4]={0x4100,0x4100,0x4100,0x4100}; /* 2.5 */
   assert(!tegu_tess_generate(&tri,(const unsigned char *)tf,8,0,0,1.0f,0,2,0,1,0,0,0,0,&v,&nv,&ix,&ni)&&nv>0&&ni%3==0);
   for(uint32_t i=0;i<nv;i++) {
    const float *p=(const float *)(const void *)(v+(size_t)i*tegu_tess_stride(3)+8+16);
    assert(p[0]>=0&&p[0]<=1.0001f&&p[0]+p[1]+p[2]>0.999f&&p[0]+p[1]+p[2]<1.001f);
   }
   free(v);free(ix); }
#undef REC
#undef POS

/* ---- encoder: state validation and draw glue ---- */
 id error=0;TeguLibrary *pl=api_probe_library(d);
 RenderDescriptor *pd=api_render_descriptor(pl,"RenderDescriptor");
 TeguRenderPipeline *pipeline=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(pipeline&&!error);
 TeguQueue *q=[d newCommandQueue];
 TeguTexture *target=new_target(d);TeguBuffer *vb=[d newBufferWithLength:256 options:0];
 TeguTexture *tex=new_sample(d,80,16,16,1,2,1);TeguSampler *smp=new_sampler(d,3);
 TeguBuffer *fbuf=[d newBufferWithBytes:f length:sizeof(f) options:0];
 TeguBuffer *pib=[d newBufferWithLength:64 options:0],*args=[d newBufferWithLength:64 options:0];
 uint32_t *pidx=[pib contents],*a=[args contents];pidx[0]=0;pidx[1]=1;pidx[2]=3;pidx[3]=1;
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[target retain];
 pass->color->load=2;pass->color->store=1;pass->passWidth=pass->passHeight=16;pass->arrayLength=1;pass->sampleCount=1;
 struct Dispatch *jobs[4];
 /* no tessellation pipeline: every draw refuses (logged), none crashes, none is silently dropped */
 tegu_tess_test_info=0;
 { TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   bind_all(e,vb,pipeline,tex,tex,smp,smp);
   [e setTessellationFactorBuffer:fbuf offset:0 instanceStride:12];[e setTessellationFactorScale:2.0f];assert(!c->invalid);
   [e drawPatches:4 patchStart:0 patchCount:2 patchIndexBuffer:0 patchIndexBufferOffset:0 instanceCount:1 baseInstance:0];
   assert(c->invalid);[e endEncoding]; }
 quad.stepFunction=1;tegu_tess_test_info=&quad;struct TeguTessInfo indexedInfo=quad;indexedInfo.controlPointIndexType=2;
 /* state validation */
#define API_BADT(...) do{TeguCommand *bc=[q commandBuffer];TeguRenderEncoder *be=[bc renderCommandEncoderWithDescriptor:(id)pass];assert(be);\
  bind_all(be,vb,pipeline,tex,tex,smp,smp);__VA_ARGS__;assert(bc->invalid);[be endEncoding];}while(0)
 API_BADT([be setTessellationFactorBuffer:fbuf offset:2 instanceStride:0]);
 API_BADT([be setTessellationFactorBuffer:fbuf offset:0 instanceStride:6]);
 API_BADT([be setTessellationFactorBuffer:fbuf offset:4096 instanceStride:0]);
 API_BADT([be setTessellationFactorBuffer:(id)pipeline offset:0 instanceStride:0]);
 API_BADT([be setTessellationFactorScale:-1.0f]);
 API_BADT([be setTessellationFactorScale:__builtin_nanf("")]);
 API_BADT([be drawPatches:3 patchStart:0 patchCount:1 patchIndexBuffer:0 patchIndexBufferOffset:0 instanceCount:1 baseInstance:0]); /* control points differ */
 API_BADT([be drawPatches:4 patchStart:0 patchCount:4 patchIndexBuffer:pib patchIndexBufferOffset:56 instanceCount:1 baseInstance:0]);
 API_BADT([be drawPatches:4 patchStart:0 patchCount:1 patchIndexBuffer:pib patchIndexBufferOffset:2 instanceCount:1 baseInstance:0]);
 API_BADT([be drawPatches:4 patchStart:0 patchCount:1 patchIndexBuffer:(id)pipeline patchIndexBufferOffset:0 instanceCount:1 baseInstance:0]);
 API_BADT([be setTessellationFactorBuffer:fbuf offset:0 instanceStride:0];
          [be drawPatches:4 patchStart:0 patchCount:9 patchIndexBuffer:0 patchIndexBufferOffset:0 instanceCount:1 baseInstance:0]); /* factors out of range */
 API_BADT([be drawIndexedPatches:4 patchStart:0 patchCount:1 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:vb controlPointIndexBufferOffset:0 instanceCount:1 baseInstance:0]); /* pipeline has no control point indices */
 API_BADT([be drawIndexedPatches:4 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:vb controlPointIndexBufferOffset:0 indirectBuffer:args indirectBufferOffset:0]);
 API_BADT([be drawPatches:4 patchIndexBuffer:0 patchIndexBufferOffset:0 indirectBuffer:args indirectBufferOffset:2]);
 API_BADT([be drawPatches:4 patchIndexBuffer:0 patchIndexBufferOffset:0 indirectBuffer:0 indirectBufferOffset:0]);
#undef API_BADT
 /* valid direct draw: one generated indexed draw, slot 30 restored */
 TeguBuffer *old30=[d newBufferWithLength:64 options:0];
 { TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   bind_all(e,vb,pipeline,tex,tex,smp,smp);[e setVertexBuffer:old30 offset:16 atIndex:30];
   [e setTessellationFactorBuffer:fbuf offset:0 instanceStride:12];
   unsigned before=e->draws;
   [e drawPatches:4 patchStart:0 patchCount:4 patchIndexBuffer:0 patchIndexBufferOffset:0 instanceCount:2 baseInstance:0];
   assert(!c->invalid&&e->draws==before+1&&e->vertexBuffers[30]==old30&&e->vertexOffsets[30]==16&&e->vertexBytesLength[30]==0);
   [e endEncoding];assert(api_draw_jobs(c,jobs,4)<=1);
   [c commit];[c waitUntilCompleted];assert([c status]==4||[c status]==5); }
 /* drawIndexedPatches: needs a pipeline with control point indices (uint32 here) and a stable index buffer */
 { TeguBuffer *cpb=[d newBufferWithLength:256 options:0];uint32_t *cp=[cpb contents];for(unsigned i=0;i<64;i++)cp[i]=i*2;
   tegu_tess_test_info=&indexedInfo;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   bind_all(e,vb,pipeline,tex,tex,smp,smp);[e setTessellationFactorBuffer:fbuf offset:0 instanceStride:12];
   unsigned before=e->draws;
   [e drawIndexedPatches:4 patchStart:0 patchCount:4 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:cpb controlPointIndexBufferOffset:0 instanceCount:1 baseInstance:2];
   assert(!c->invalid&&e->draws==before+1);
   a[0]=2;a[1]=1;a[2]=0;a[3]=0;
   [e drawIndexedPatches:4 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:cpb controlPointIndexBufferOffset:0 indirectBuffer:args indirectBufferOffset:0];
   assert(!c->invalid&&e->draws==before+2);[e endEncoding];
   /* wrong shape: a non-indexed draw on an indexed pipeline, a misaligned or short control point buffer */
#define API_BADI2(...) do{TeguCommand *bc=[q commandBuffer];TeguRenderEncoder *be=[bc renderCommandEncoderWithDescriptor:(id)pass];\
  bind_all(be,vb,pipeline,tex,tex,smp,smp);[be setTessellationFactorBuffer:fbuf offset:0 instanceStride:12];__VA_ARGS__;assert(bc->invalid);[be endEncoding];}while(0)
   API_BADI2([be drawPatches:4 patchStart:0 patchCount:1 patchIndexBuffer:0 patchIndexBufferOffset:0 instanceCount:1 baseInstance:0]);
   API_BADI2([be drawIndexedPatches:4 patchStart:0 patchCount:1 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:cpb controlPointIndexBufferOffset:2 instanceCount:1 baseInstance:0]);
   API_BADI2([be drawIndexedPatches:4 patchStart:0 patchCount:1 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:0 controlPointIndexBufferOffset:0 instanceCount:1 baseInstance:0]);
   API_BADI2([be drawIndexedPatches:4 patchStart:0 patchCount:1 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:cpb controlPointIndexBufferOffset:512 instanceCount:1 baseInstance:0]);
   API_BADI2([be drawIndexedPatches:4 patchStart:15 patchCount:1 patchIndexBuffer:0 patchIndexBufferOffset:0 controlPointIndexBuffer:cpb controlPointIndexBufferOffset:200 instanceCount:1 baseInstance:0]);
#undef API_BADI2
   a[0]=2;a[1]=1;a[2]=0;a[3]=0;
   tegu_tess_test_info=&quad;[cpb release]; }
 /* patch index buffer + scale + a different instance stride */
 { TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   bind_all(e,vb,pipeline,tex,tex,smp,smp);
   [e setTessellationFactorBuffer:fbuf offset:0 instanceStride:0];[e setTessellationFactorScale:0.5f];
   unsigned before=e->draws;
   [e drawPatches:4 patchStart:1 patchCount:3 patchIndexBuffer:pib patchIndexBufferOffset:0 instanceCount:1 baseInstance:3];
   assert(!c->invalid&&e->draws==before+1&&e->vertexBuffers[30]==0);[e endEncoding]; }
 /* render useResource forms validate and retain; stage masks beyond vertex|fragment fail */
 { TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   [e useResource:fbuf usage:1];[e useResource:fbuf usage:3 stages:3];[e useResources:(id const *)&fbuf count:1 usage:1];
   assert(!c->invalid);[e useResource:0 usage:1];assert(c->invalid);[e endEncoding]; }
 { TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   [e useResource:fbuf usage:1 stages:8];assert(c->invalid);[e endEncoding]; }
 /* every patch culled draws nothing and is not an error */
 { TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   bind_all(e,vb,pipeline,tex,tex,smp,smp);[e setTessellationFactorBuffer:fbuf offset:0 instanceStride:0];[e setTessellationFactorScale:0.0f];
   unsigned before=e->draws;[e drawPatches:4 patchStart:0 patchCount:2 patchIndexBuffer:0 patchIndexBufferOffset:0 instanceCount:1 baseInstance:0];
   assert(!c->invalid&&e->draws==before);[e endEncoding]; }
 /* indirect: {patchCount, instanceCount, patchStart, baseInstance}; CPU rewrite after encode fails the command */
 a[0]=2;a[1]=1;a[2]=0;a[3]=0;
 { TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];
   bind_all(e,vb,pipeline,tex,tex,smp,smp);[e setTessellationFactorBuffer:fbuf offset:0 instanceStride:12];
   unsigned before=e->draws;[e drawPatches:4 patchIndexBuffer:0 patchIndexBufferOffset:0 indirectBuffer:args indirectBufferOffset:0];
   assert(!c->invalid&&e->draws==before+1);[e endEncoding];a[0]=3;
   [c commit];[c waitUntilCompleted];assert([c status]==5&&[c error]);a[0]=2; }
 { TeguCommand *c=[q commandBuffer];TeguBlitEncoder *bl=[c blitCommandEncoder];[bl fillBuffer:args range:(TeguRange){0,16} value:1];[bl endEncoding];
   TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];bind_all(e,vb,pipeline,tex,tex,smp,smp);
   [e drawPatches:4 patchIndexBuffer:0 patchIndexBufferOffset:0 indirectBuffer:args indirectBufferOffset:0];assert(c->invalid);[e endEncoding]; }
 tegu_tess_test_info=0;
 [old30 release];[pass release];[args release];[pib release];[fbuf release];[smp release];[tex release];[vb release];[target release];
 [pipeline release];[pd release];[pl release];[q release];
}

/* ============================ compute descriptor: linked functions / stage input / call depth ============================ */
@interface ApiList : NSObject { @public id items[8];NSUInteger n;id fill; }
- (NSUInteger)count;- (id)objectAtIndex:(NSUInteger)i;- (id)objectAtIndexedSubscript:(NSUInteger)i;
@end
@implementation ApiList
- (NSUInteger)count{return n;}
- (id)objectAtIndex:(NSUInteger)i{return i<n?items[i]:0;}
- (id)objectAtIndexedSubscript:(NSUInteger)i{return i<n?items[i]:fill;}
@end
@interface ApiAttr : NSObject { @public NSUInteger format,offset,bufferIndex,stride,stepFunction,stepRate; }
- (NSUInteger)format;- (NSUInteger)offset;- (NSUInteger)bufferIndex;- (NSUInteger)stride;- (NSUInteger)stepFunction;- (NSUInteger)stepRate;
@end
@implementation ApiAttr
- (NSUInteger)format{return format;}- (NSUInteger)offset{return offset;}- (NSUInteger)bufferIndex{return bufferIndex;}
- (NSUInteger)stride{return stride;}- (NSUInteger)stepFunction{return stepFunction;}- (NSUInteger)stepRate{return stepRate;}
@end
@interface ApiStage : NSObject { @public id attributes,layouts;NSUInteger indexType,indexBufferIndex; }
- (id)attributes;- (id)layouts;- (NSUInteger)indexType;- (NSUInteger)indexBufferIndex;
@end
@implementation ApiStage
- (id)attributes{return attributes;}- (id)layouts{return layouts;}
- (NSUInteger)indexType{return indexType;}- (NSUInteger)indexBufferIndex{return indexBufferIndex;}
@end
@interface ApiLinked : NSObject { @public id functions,privateFunctions,binaryFunctions,groups; }
- (id)functions;- (id)privateFunctions;- (id)binaryFunctions;- (id)groups;
@end
@implementation ApiLinked
- (id)functions{return functions;}- (id)privateFunctions{return privateFunctions;}
- (id)binaryFunctions{return binaryFunctions;}- (id)groups{return groups;}
@end
@interface ApiCompDesc : NSObject { @public id function,linked,stage,insertLibs,preloaded;NSUInteger depth; }
- (id)computeFunction;- (id)linkedFunctions;- (id)stageInputDescriptor;- (NSUInteger)maxCallStackDepth;
- (id)insertLibraries;- (id)preloadedLibraries;
@end
@implementation ApiCompDesc
- (id)computeFunction{return function;}- (id)linkedFunctions{return linked;}- (id)stageInputDescriptor{return stage;}
- (NSUInteger)maxCallStackDepth{return depth;}- (id)insertLibraries{return insertLibs;}- (id)preloadedLibraries{return preloaded;}
@end
static id api_keep[64];static unsigned api_kept;
static id api_track(id o){assert(api_kept<64);api_keep[api_kept++]=o;return o;}
static ApiList *api_list(id a,id b,id fill){
 ApiList *l=[[ApiList alloc] init];api_track(l);l->fill=fill;if(a)l->items[l->n++]=a;if(b)l->items[l->n++]=b;return l;
}
static void api_test_compute_descriptor(TeguComputeDevice *d,const char *computeAir){
 FILE *in=fopen(computeAir,"rb");assert(in);fseek(in,0,SEEK_END);long n=ftell(in);rewind(in);
 void *air=malloc((size_t)n);assert(fread(air,1,(size_t)n,in)==(size_t)n);fclose(in);
 id error=0;TeguLibrary *lib=[d newTeguLibraryWithBytes:air length:(NSUInteger)n error:&error];free(air);assert(lib&&!error);
 TeguFunction *fn=[lib newFunctionWithName:[NSString stringWithUTF8String:"nord2_compute_probe"]];assert(fn);
 TeguComputeCreateState st;
 ApiCompDesc *desc=[[ApiCompDesc alloc] init];desc->function=fn;
 /* a plain descriptor takes the byte-identical plain create path */
 assert(!tegu_compute_create_state_init(d,desc,&st)&&!st.used&&!st.options.linked_count&&!st.options.has_stage_input);
 assert(!tegu_compute_create_state_init(d,0,&st)&&!st.used);
 /* full marshalling */
 ApiAttr *zero=[[ApiAttr alloc] init];
 ApiAttr *a0=[[ApiAttr alloc] init];a0->format=3;a0->offset=4;a0->bufferIndex=2;
 ApiAttr *a1=[[ApiAttr alloc] init];a1->format=28;a1->offset=0;a1->bufferIndex=5;
 ApiAttr *lay=[[ApiAttr alloc] init];lay->stride=16;lay->stepFunction=6;lay->stepRate=1;
 ApiAttr *lay2=[[ApiAttr alloc] init];lay2->stride=32;lay2->stepFunction=1;lay2->stepRate=1;
 ApiStage *stage=[[ApiStage alloc] init];
 ApiList *attrs=[[ApiList alloc] init];attrs->fill=zero;attrs->n=8;for(int i=0;i<8;i++)attrs->items[i]=zero;attrs->items[0]=a0;attrs->items[3]=a1;
 ApiList *lays=[[ApiList alloc] init];lays->fill=zero;lays->n=8;for(int i=0;i<8;i++)lays->items[i]=zero;lays->items[2]=lay;lays->items[5]=lay2;
 stage->attributes=attrs;stage->layouts=lays;stage->indexType=1;stage->indexBufferIndex=3;
 ApiLinked *linked=[[ApiLinked alloc] init];
 linked->functions=api_list(fn,0,0);linked->privateFunctions=api_list(fn,0,0);linked->binaryFunctions=api_list(fn,fn,0);
 desc->linked=linked;desc->stage=stage;desc->depth=4;
 const char *why=tegu_compute_create_state_init(d,desc,&st);assert(!why);
 assert(st.used&&st.options.version==1&&st.options.max_call_stack_depth==4&&st.options.linked_count==4);
 static const uint32_t lists[4]={0,1,2,2};
 for(unsigned i=0;i<4;i++) {
  assert(st.linked[i].list==lists[i]&&st.linked[i].library==fn->library->library&&st.linked[i].name_len==19);
  assert(!memcmp(st.linked[i].name,"nord2_compute_probe",19));
  assert(!st.linked[i].link_name&&!st.linked[i].link_name_len);
 }
 assert(st.options.linked==st.linked&&st.options.has_stage_input&&st.options.stage_attribute_count==2);
 assert(st.attributes[0].index==0&&st.attributes[0].format==3&&st.attributes[0].offset==4&&st.attributes[0].buffer_index==2);
 assert(st.attributes[1].index==3&&st.attributes[1].format==28&&st.attributes[1].buffer_index==5);
 assert(st.options.stage_layout_count==2&&st.layouts[0].index==2&&st.layouts[0].stride==16&&st.layouts[0].step_function==6);
 assert(st.layouts[1].index==5&&st.layouts[1].stride==32&&st.layouts[1].step_function==1&&st.layouts[1].step_rate==1);
 assert(st.options.stage_index_type==1&&st.options.stage_index_buffer_index==3);
 /* a function made under another name (specializedName) links by its specialized name */
 { id savedName=fn->displayName;fn->displayName=[NSString stringWithUTF8String:"prefixPrimary_f"];
   linked->binaryFunctions=api_list(fn,0,0);
   assert(!tegu_compute_create_state_init(d,desc,&st));
   assert(st.linked[2].list==2&&st.linked[2].link_name_len==15&&!memcmp(st.linked[2].link_name,"prefixPrimary_f",15)&&st.linked[2].name_len==19);
   assert(!st.linked[0].link_name==0); /* every entry of that function carries it */
   fn->displayName=[NSString stringWithUTF8String:"nord2_compute_probe"];
   assert(!tegu_compute_create_state_init(d,desc,&st)&&!st.linked[2].link_name); /* same as the library name: none */
   fn->displayName=savedName; }
 /* default call stack depth (0 or 1) forwards nothing */
 desc->linked=0;desc->stage=0;desc->depth=1;assert(!tegu_compute_create_state_init(d,desc,&st)&&!st.used);
 desc->depth=0;assert(!tegu_compute_create_state_init(d,desc,&st)&&!st.used);
 /* an empty-but-present stage input descriptor still enables stage-in */
 { ApiStage *empty=[[ApiStage alloc] init];desc->stage=empty;
   assert(!tegu_compute_create_state_init(d,desc,&st)&&st.used&&st.options.has_stage_input&&!st.options.stage_attribute_count);
   desc->stage=0;[empty release]; }
 /* refusals (each names a reason; nothing is silently dropped) */
 desc->linked=linked;linked->functions=api_list(api_track([[ApiPlainEvent alloc] init]),0,0);
 assert(tegu_compute_create_state_init(d,desc,&st));
 linked->functions=api_list(fn,0,0);
 { unsigned k=fn->kind;fn->kind=1;assert(tegu_compute_create_state_init(d,desc,&st));fn->kind=5;assert(!tegu_compute_create_state_init(d,desc,&st));fn->kind=k; }
 linked->groups=api_list((id)fn,0,0);assert(tegu_compute_create_state_init(d,desc,&st));linked->groups=0;
 desc->insertLibs=api_list((id)fn,0,0);assert(tegu_compute_create_state_init(d,desc,&st));desc->insertLibs=0;
 desc->preloaded=api_list((id)fn,0,0);assert(tegu_compute_create_state_init(d,desc,&st));desc->preloaded=0;
 desc->linked=0;desc->depth=(NSUInteger)1<<33;assert(tegu_compute_create_state_init(d,desc,&st));desc->depth=0;
 a0->bufferIndex=31;desc->stage=stage;assert(tegu_compute_create_state_init(d,desc,&st));a0->bufferIndex=2;
 stage->indexType=2;assert(tegu_compute_create_state_init(d,desc,&st));stage->indexType=1;
 stage->indexBufferIndex=31;assert(tegu_compute_create_state_init(d,desc,&st));stage->indexBufferIndex=3;
 lay->stride=(NSUInteger)1<<33;assert(tegu_compute_create_state_init(d,desc,&st));lay->stride=16;
 assert(!tegu_compute_create_state_init(d,desc,&st));
 /* a function of another device is refused */
 { TeguComputeDevice *other=[TeguComputeDevice alloc];other->connection=d->connection;other->scalar=d->scalar;other->method=d->method;other->map=d->map;other->unmap=d->unmap;other->capabilities=d->capabilities;
   linked->functions=api_list(fn,0,0);desc->linked=linked;desc->stage=0;
   TeguLibrary *save=fn->library;fn->library=[[TeguLibrary alloc] init];fn->library->owner=[other retain];
   assert(tegu_compute_create_state_init(d,desc,&st));
   other->connection=0;[fn->library release];fn->library=save;[other release]; }
 /* end to end through the descriptor entry point */
 desc->linked=0;desc->stage=0;desc->depth=0;
 id reflection=0;error=0;
 TeguPipeline *plain=[d newComputePipelineStateWithDescriptor:desc options:0 reflection:&reflection error:&error];assert(plain&&!error);
 assert(tegu_air_pipeline_dispatchable(plain->compiled)>=0);
 desc->depth=3; /* call stack depth: create_ex path, same kernel */
 error=0;TeguPipeline *deep=[d newComputePipelineStateWithDescriptor:desc options:0 reflection:&reflection error:&error];
 assert(tegu_air_pipeline_create_ex);
 assert(deep&&!error);
 assert(plain->limits[0]==deep->limits[0]);
 [deep release];desc->depth=0;
 { ApiStage *empty=[[ApiStage alloc] init];desc->stage=empty;
   error=0;TeguPipeline *st1=[d newComputePipelineStateWithDescriptor:desc options:0 reflection:&reflection error:&error];
   fprintf(stderr,"API compute descriptor with empty stage input: %s\n",st1?"created":"refused");
   assert(st1&&!error);[st1 release];desc->stage=0;[empty release]; }
 /* refused descriptors are nil with an error, never an inert or half-built state */
 desc->linked=linked;linked->groups=api_list((id)fn,0,0);
 error=0;assert(![d newComputePipelineStateWithDescriptor:desc options:0 reflection:&reflection error:&error]&&error);
 linked->groups=0;
 /* a linked list naming a function the compiler cannot resolve fails with the compiler's reason */
 error=0;TeguPipeline *lk=[d newComputePipelineStateWithDescriptor:desc options:0 reflection:&reflection error:&error];
 fprintf(stderr,"API compute descriptor linking the kernel itself: %s\n",lk?"created":"refused");
 [lk release];
 [plain release];[desc release];[stage release];[linked release];[attrs release];[lays release];
 while(api_kept)[api_keep[--api_kept] release];
 [zero release];[a0 release];[a1 release];[lay release];[lay2 release];[fn release];[lib release];
}

/* ============================ argument buffers: gpuAddress + MTLArgumentEncoder ============================ */
@interface ApiArgDesc : NSObject { @public NSUInteger type,index,length,block; }
- (NSUInteger)dataType;- (NSUInteger)index;- (NSUInteger)arrayLength;- (NSUInteger)constantBlockAlignment;
@end
@implementation ApiArgDesc
- (NSUInteger)dataType{return type;}- (NSUInteger)index{return index;}
- (NSUInteger)arrayLength{return length;}- (NSUInteger)constantBlockAlignment{return block;}
@end
static ApiArgDesc *api_arg(NSUInteger type,NSUInteger index,NSUInteger length,NSUInteger block){
 ApiArgDesc *a=[[ApiArgDesc alloc] init];a->type=type;a->index=index;a->length=length;a->block=block;return a;
}
static void api_test_argument_buffers(TeguComputeDevice *d){
 TeguBuffer *b0=[d newBufferWithLength:4096 options:0],*b1=[d newBufferWithLength:512 options:0];
 U64 v0=[b0 gpuAddress],v1=[b1 gpuAddress];
 assert(v0&&v1&&v0!=v1&&[b0 gpuAddress]==v0&&b0->gpuVA==v0); /* cached, stable */
 assert(v0+4096<=v1||v1+512<=v0);
 assert([[b0 retain] gpuAddress]==v0);[b0 release];
 assert(![(TeguBuffer *)0 gpuAddress]);
 /* layout of explicit descriptors, hand computed (MSL alignment: float3 occupies 16) */
 ApiArgDesc *ad[6]={api_arg(60,0,1,0),api_arg(33,1,1,0),api_arg(6,2,1,0),api_arg(5,3,1,0),api_arg(49,4,1,0),api_arg(60,5,3,0)};
 id list[6];for(int i=0;i<6;i++)list[i]=ad[i];
 ApiList *arr=[[ApiList alloc] init];for(int i=0;i<6;i++)arr->items[arr->n++]=list[i];
 TeguArgumentEncoder *enc=[d newArgumentEncoderWithArguments:arr];assert(enc);
 assert(enc->members[0].offset==0&&enc->members[1].offset==8&&enc->members[2].offset==16&&enc->members[3].offset==32&&
        enc->members[4].offset==48&&enc->members[5].offset==56&&enc->members[5].stride==8&&enc->members[5].count==3);
 assert([enc encodedLength]==80&&[enc alignment]==16&&[enc device]==d);
 /* encoding: pointers are gpuAddress+offset, nil is 0, constants are in place */
 TeguBuffer *arg=[d newBufferWithLength:4096 options:0];unsigned char *mem=[arg contents];memset(mem,0xEE,4096);
 [enc setArgumentBuffer:arg offset:256];
 [enc setBuffer:b0 offset:0 atIndex:0];
 [enc setBuffer:b1 offset:16 atIndex:5];[enc setBuffer:0 offset:0 atIndex:6];[enc setBuffer:b0 offset:4096 atIndex:7];
 uint64_t w;memcpy(&w,mem+256,8);assert(w==v0);
 memcpy(&w,mem+256+56,8);assert(w==v1+16);
 memcpy(&w,mem+256+64,8);assert(w==0);
 memcpy(&w,mem+256+72,8);assert(w==v0+4096); /* one past the end is a valid pointer value */
 { uint32_t *cd=[enc constantDataAtIndex:1];assert((unsigned char *)cd==mem+256+8);*cd=0x11223344u;
   float *f4=[enc constantDataAtIndex:2];assert((unsigned char *)f4==mem+256+16);
   assert([enc constantDataAtIndex:0]==0&&[enc constantDataAtIndex:99]==0); /* pointers are not constants */ }
 assert(mem[255]==0xEE&&mem[256+80]==0xEE); /* nothing outside the encoded range */
 { id bs[2]={b1,b0};NSUInteger os[2]={4,8};[enc setBuffers:bs offsets:os withRange:(TeguRange){5,2}];
   memcpy(&w,mem+256+56,8);assert(w==v1+4);memcpy(&w,mem+256+64,8);assert(w==v0+8); }
 /* array element selection: the second argument buffer lives one aligned stride further */
 [enc setArgumentBuffer:arg startOffset:512 arrayElement:2];
 [enc setBuffer:b1 offset:0 atIndex:0];memcpy(&w,mem+512+2*80,8);assert(w==v1);
 /* refusals leave the memory untouched */
 memset(mem+1024,0xAB,64);[enc setArgumentBuffer:arg offset:1024];
 [enc setBuffer:b0 offset:8192 atIndex:0]; /* offset beyond the buffer */
 [enc setBuffer:b0 offset:0 atIndex:1];    /* a constant member */
 [enc setBuffer:b0 offset:0 atIndex:8];    /* no such member */
 [enc setTexture:0 atIndex:1];[enc setSamplerState:0 atIndex:1];
 { unsigned char same[64];memset(same,0xAB,64);assert(!memcmp(mem+1024,same,64)); }
 [enc setArgumentBuffer:arg offset:8];assert(enc->bufferOffset==1024); /* misaligned offset refused */
 [enc setArgumentBuffer:arg offset:4090];assert(enc->bufferOffset==1024); /* extent refused */
 [enc setArgumentBuffer:0 offset:0];[enc setBuffer:b0 offset:0 atIndex:0]; /* no buffer set: refused, no crash */
 assert([enc newArgumentEncoderForBufferAtIndex:0]==0);
 [enc release];
 /* constantBlockAlignment, matrices, vectors, bools, half */
 { ApiArgDesc *m0=api_arg(15,0,1,0),*m1=api_arg(1+2,1,1,64),*m2=api_arg(11,2,1,0),*m3=api_arg(19,3,1,0),*m4=api_arg(55,4,2,0);
   ApiList *l2=[[ApiList alloc] init];l2->items[0]=m0;l2->items[1]=m1;l2->items[2]=m2;l2->items[3]=m3;l2->items[4]=m4;l2->n=5;
   TeguArgumentEncoder *e2=[d newArgumentEncoderWithArguments:l2];assert(e2);
   /* float4x4 64@0; float 4 bytes at 64 (block alignment 64); float3x3 3x16=48 @80 align16; half4 8@128; bool3 x2: size4 align4 @136,140 */
   assert(e2->members[0].offset==0&&e2->members[1].offset==64&&e2->members[2].offset==80&&e2->members[3].offset==128&&e2->members[4].offset==136);
   assert(e2->members[4].stride==4&&[e2 encodedLength]==192&&[e2 alignment]==64); /* block alignment raises the encoder alignment, the length rounds to it */
   [e2 release];[l2 release];[m0 release];[m1 release];[m2 release];[m3 release];[m4 release]; }
 /* textures and samplers are 8-byte members (layout only; encoding them is refused) */
 { ApiArgDesc *t=api_arg(58,0,2,0),*sm=api_arg(59,2,1,0),*pt=api_arg(60,3,1,0);
   ApiList *l3=[[ApiList alloc] init];l3->items[0]=t;l3->items[1]=sm;l3->items[2]=pt;l3->n=3;
   TeguArgumentEncoder *e3=[d newArgumentEncoderWithArguments:l3];assert(e3&&[e3 encodedLength]==32);
   assert(e3->members[2].offset==16&&e3->members[3].offset==24);[e3 release];[l3 release];[t release];[sm release];[pt release]; }
 /* invalid descriptor sets are nil */
 { ApiArgDesc *dup0=api_arg(33,0,1,0),*dup1=api_arg(33,0,1,0),*st=api_arg(1,0,1,0),*big=api_arg(33,300,1,0),*badblk=api_arg(33,0,1,24);
   ApiList *bad=[[ApiList alloc] init];
   bad->items[0]=dup0;bad->items[1]=dup1;bad->n=2;assert(![d newArgumentEncoderWithArguments:bad]);
   bad->items[0]=st;bad->n=1;assert(![d newArgumentEncoderWithArguments:bad]);
   bad->items[0]=big;assert(![d newArgumentEncoderWithArguments:bad]);
   bad->items[0]=badblk;assert(![d newArgumentEncoderWithArguments:bad]);
   bad->n=0;assert(![d newArgumentEncoderWithArguments:bad]);assert(![d newArgumentEncoderWithArguments:0]);
   [bad release];[dup0 release];[dup1 release];[st release];[big release];[badblk release]; }
 for(int i=0;i<6;i++)[ad[i] release];
 [arr release];[arg release];[b0 release];[b1 release];
 /* device entry points of unsupported feature families answer nil / zero with a trace, never an exception */
 { id e=0,r=0;assert(![d newRenderPipelineStateWithMeshDescriptor:0 options:0 reflection:&r error:&e]&&e&&!r);
   MTLSize z=[d sparseTileSizeWithTextureType:2 pixelFormat:80 sampleCount:1];assert(!z.width&&!z.height&&!z.depth);
   z=[d sparseTileSizeWithTextureType:2 pixelFormat:80 sampleCount:1 sparsePageSize:16];assert(!z.width);
   assert(![d sparseTileSizeInBytesForSparsePageSize:16]);
   [d convertSparsePixelRegions:0 toTileRegions:0 withTileSize:z alignmentMode:0 numRegions:0];
   [d convertSparseTileRegions:0 toPixelRegions:0 withTileSize:z numRegions:0];
   TeguAccelerationStructureSizes sz=[d accelerationStructureSizesWithDescriptor:0];assert(!sz.accelerationStructureSize&&!sz.buildScratchBufferSize);
   assert(![d newAccelerationStructureWithSize:64]&&![d newAccelerationStructureWithDescriptor:0]);
   TeguSizeAndAlign sa=[d heapAccelerationStructureSizeAndAlignWithSize:64];assert(!sa.size&&!sa.align);
   sa=[d heapAccelerationStructureSizeAndAlignWithDescriptor:0];assert(!sa.size); }
}

int main(int argc,char **argv){
 unsetenv("NORD2_METAL_TRACE"); /* traces force ordered submission */
 assert(argc>=2);api_air_path=argv[1];
 tegu_host_clock=1;
 struct fake f={0};uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=api_device(&f,bridge);
 unsigned base=live;
 { /* the diagnostic hook only logs and answers NO; repeated selectors are suppressed */
   unsigned before=tegu_unrecognized_count;
   assert(![TeguRenderEncoder resolveInstanceMethod:@selector(apiNoSuchSelectorA:)]);
   assert(![TeguRenderEncoder resolveInstanceMethod:@selector(apiNoSuchSelectorA:)]);
   assert(![TeguComputeDevice resolveInstanceMethod:@selector(apiNoSuchSelectorB)]);
   assert(![TeguRenderSubEncoder resolveInstanceMethod:@selector(apiNoSuchSelectorC)]);
   assert(tegu_unrecognized_count==before+3); }
 api_test_events(d);drain();fprintf(stderr,"LIVE events %d\n",(int)(live-base));
 api_test_device_queue_command(d);drain();fprintf(stderr,"LIVE queue %d\n",(int)(live-base));
 api_test_source_and_archive(d);drain();fprintf(stderr,"LIVE source %d\n",(int)(live-base));
 api_test_compute_encoder(d);drain();fprintf(stderr,"LIVE compute %d\n",(int)(live-base));
 api_test_render_blit_texture(d);drain();fprintf(stderr,"LIVE render %d\n",(int)(live-base));
 api_test_parallel(d);drain();fprintf(stderr,"LIVE parallel %d\n",(int)(live-base));
 api_test_async_events();drain();fprintf(stderr,"LIVE async-events %d\n",(int)(live-base));
 api_test_fuzz(d,getenv("API_FUZZ")?(unsigned)atoi(getenv("API_FUZZ")):600);drain();fprintf(stderr,"LIVE fuzz %d\n",(int)(live-base));
 api_test_resource_options(d);drain();fprintf(stderr,"LIVE options %d\n",(int)(live-base));
 api_test_buffer_texture(d);drain();fprintf(stderr,"LIVE buffer-texture %d\n",(int)(live-base));
 api_test_nocopy(d);drain();fprintf(stderr,"LIVE nocopy %d\n",(int)(live-base));
 api_test_indirect_draw(d);drain();fprintf(stderr,"LIVE indirect-draw %d\n",(int)(live-base));
 api_test_indirect_dispatch(d,argc>=3?argv[2]:0);drain();fprintf(stderr,"LIVE indirect %d\n",(int)(live-base));
 api_test_argument_buffers(d);drain();fprintf(stderr,"LIVE argument-buffers %d\n",(int)(live-base));
 api_test_tessellation(d);drain();fprintf(stderr,"LIVE tessellation %d\n",(int)(live-base));
 if(argc>=3){api_test_compute_descriptor(d,argv[2]);drain();fprintf(stderr,"LIVE compute-descriptor %d\n",(int)(live-base));}
 d->connection=0;[d release];drain();close_bridge(bridge,&f);
 assert(!live);
 puts("api_surface_frontend_test: PASS");
 return 0;
}
NSUInteger tegu_host_constant_data_size(NSUInteger type) {
 switch(type){case 53:case 45:case 49:return 1;case 16:case 37:case 41:return 2;
 case 3:case 29:case 33:return 4;default:return 0;}
}
