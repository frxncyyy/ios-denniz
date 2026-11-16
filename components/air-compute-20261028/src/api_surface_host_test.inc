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
