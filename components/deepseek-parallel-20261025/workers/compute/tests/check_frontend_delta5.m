/* Host-only delta5 frontend tests: bounded NOTREADY retry, compositor gate,
 * and explicit newPipelineLibrary refusal.  No GPU or phone. */
#define TEGU_RUNTIME_MAIN runtime_frontend_original_main
#include "runtime_frontend_host_test.m"
#undef TEGU_RUNTIME_MAIN

#ifdef TEGU_METAL_BASE
/* Host stub for the Apple superclass symbol; no Apple methods are used. */
@implementation _MTLDevice
- (id)initWithAcceleratorPort:(unsigned)port{(void)port;return [super init];}
- (void)initLimits{}
- (void)initFeatureQueries{}
- (signed char)supportsFamily:(long long)family{(void)family;return 0;}
#ifdef TEGU_METAL_FEATURE_INIT
- (const struct tegu_device_limits *)limits{return 0;}
#endif
@end
#endif
static unsigned retry_calls,retry_ready_after;
static int retry_scalar(TeguConnection c,unsigned selector,const U64 *args,
                        unsigned argc,U64 *out,unsigned *n) {
 (void)c;(void)selector;(void)args;(void)argc;
 retry_calls++;
 if(retry_ready_after&&retry_calls<retry_ready_after)return TEGU_CAP_NOTREADY;
 out[0]=1;out[1]=8|16|64;*n=4;return 0;
}
static int never_ready_scalar(TeguConnection c,unsigned selector,const U64 *args,
                              unsigned argc,U64 *out,unsigned *n) {
 (void)c;(void)selector;(void)args;(void)argc;(void)out;(void)n;
 retry_calls++;return TEGU_CAP_NOTREADY;
}
static int hard_error_scalar(TeguConnection c,unsigned selector,const U64 *args,
                             unsigned argc,U64 *out,unsigned *n) {
 (void)c;(void)selector;(void)args;(void)argc;(void)out;(void)n;
 retry_calls++;return -1;
}
int main(void) {
 assert(TEGU_CAP_RETRY_ATTEMPTS==250&&TEGU_CAP_RETRY_US==20000);
 /* MTLFeatureSet admission is the Apple1/Apple2 tier claimed by
  * supportsFamily: Family1/2 values true, Family3/4/5 and non-iOS false. */
 assert(tegu_supports_ios_feature_set(0,1)==1);  /* iOS Family1 v1 */
 assert(tegu_supports_ios_feature_set(2,1)==1);  /* Family1 v2 */
 assert(tegu_supports_ios_feature_set(5,1)==1);
 assert(tegu_supports_ios_feature_set(8,1)==1);
 assert(tegu_supports_ios_feature_set(12,1)==1);
 assert(tegu_supports_ios_feature_set(1,1)==1);  /* iOS Family2 v1 */
 assert(tegu_supports_ios_feature_set(3,1)==1);
 assert(tegu_supports_ios_feature_set(6,1)==1);
 assert(tegu_supports_ios_feature_set(9,1)==1);
 assert(tegu_supports_ios_feature_set(13,1)==1);
 for(unsigned f=0;f<=16;f++) {
  int expected=(f==0||f==1||f==2||f==3||f==5||f==6||f==8||f==9||f==12||f==13);
  assert(tegu_supports_ios_feature_set(f,1)==expected);
  assert(tegu_supports_ios_feature_set(f,0)==0);
 }
 assert(tegu_supports_ios_feature_set(10000,1)==0); /* macOS */
 assert(tegu_supports_ios_feature_set(30000,1)==0); /* tvOS */
 assert(tegu_supports_ios_feature_set(17,1)==0);    /* unknown/newer */

 /* Capability trace: every query emits a bounded TEGU_DEVQ line, and the
  * method results stay consistent with the non-graphics host backend. */
 {
  TeguComputeDevice *qd=[TeguComputeDevice alloc];
  tegu_host_devq_reset();
  assert(![qd supportsFeatureSet:0]);
  assert(![qd supportsFamily:1001]);
  assert(![qd supportsTextureSampleCount:4]);
  assert(![qd isDepth24Stencil8PixelFormatSupported]);
  assert(![qd supportsSampleCount:4]);
  for(unsigned i=0;i<80;i++)[qd supportsFeatureSet:i];
  [qd release];
 }

 /* Gate semantics: non-backboardd is always allowed; backboardd defaults off
  * and is enabled only by the explicit file fact or experiment macro. */
 assert(tegu_compositor_gate_decision(0,0)==1);
 assert(tegu_compositor_gate_decision(1,0)==0);
 assert(tegu_compositor_gate_decision(1,1)==1);
 tegu_host_process_backboardd=1;
 assert(tegu_compositor_process_is_backboardd()==1);
 tegu_host_process_backboardd=0;
 assert(tegu_compositor_process_is_backboardd()==0);
#ifdef TEGU_COMPOSITOR_DEFAULT_ON
 assert(tegu_compositor_opt_in_exists()==1);
#else
 assert(tegu_compositor_opt_in_exists()==0);
#endif
 tegu_host_compositor_gate_reset();tegu_log_compositor_gate(0);
 tegu_host_compositor_gate_reset();tegu_log_compositor_gate(1);

 /* Pure retry helper: two NOTREADY replies then a valid 4-word capability. */
 U64 info[4]={0,0,0,0};unsigned attempts=0;retry_calls=0;retry_ready_after=3;
 assert(!tegu_capabilities_with_retry(0,retry_scalar,info,&attempts));
 assert(retry_calls==3&&attempts==3&&info[0]==1&&(info[1]&8));

 /* A non-NOTREADY error is not retried. */
 retry_calls=0;
 assert(tegu_capabilities_with_retry(0,hard_error_scalar,info,&attempts));
 assert(retry_calls==1&&attempts==1);

 /* Never-ready is bounded at exactly 250 attempts. */
 retry_calls=0;attempts=0;
 assert(tegu_capabilities_with_retry(0,never_ready_scalar,info,&attempts));
 assert(retry_calls==TEGU_CAP_RETRY_ATTEMPTS&&attempts==TEGU_CAP_RETRY_ATTEMPTS);

 /* Actual initWithAcceleratorPort path with the host transport seam. */
 tegu_host_process_backboardd=0;tegu_host_compositor_file=0;
 tegu_host_init_test_mode=1;tegu_host_init_test_scalar=retry_scalar;
 tegu_host_init_test_connection=0;retry_calls=0;retry_ready_after=3;
 TeguComputeDevice *device=[[TeguComputeDevice alloc] init];
 assert(device&&retry_calls==3);
 tegu_host_compositor_gate_reset();
 [device release];

 /* backboardd defaults denied; the file or the experiment macro enables it. */
 tegu_host_process_backboardd=1;tegu_host_compositor_file=0;
 tegu_host_init_test_mode=1;tegu_host_init_test_scalar=retry_scalar;
 tegu_host_init_test_connection=0;retry_calls=0;retry_ready_after=0;
#ifdef TEGU_COMPOSITOR_DEFAULT_ON
 tegu_host_compositor_gate_reset();
 TeguComputeDevice *enabled=[[TeguComputeDevice alloc] init];
 assert(enabled&&retry_calls==1);
 tegu_host_compositor_gate_reset();
 [enabled release];
#else
 tegu_host_compositor_gate_reset();
 TeguComputeDevice *denied=[[TeguComputeDevice alloc] init];
 assert(!denied&&retry_calls==0);
 tegu_host_compositor_file=1;retry_calls=0;
 tegu_host_compositor_gate_reset();
 TeguComputeDevice *enabled=[[TeguComputeDevice alloc] init];
 assert(enabled&&retry_calls==1);
 tegu_host_compositor_gate_reset();
 [enabled release];
#endif

 /* Never-ready through the real init is nil within the bounded attempts. */
 tegu_host_process_backboardd=0;tegu_host_compositor_file=0;
 tegu_host_init_test_mode=1;tegu_host_init_test_scalar=never_ready_scalar;
 tegu_host_init_test_connection=0;retry_calls=0;
 TeguComputeDevice *never=[[TeguComputeDevice alloc] init];
 assert(!never&&retry_calls==TEGU_CAP_RETRY_ATTEMPTS);
 tegu_host_compositor_gate_reset();

 /* Apple's inherited pipeline-library path must never run. */
 TeguComputeDevice *libraryDevice=[TeguComputeDevice alloc];
 id error=0;
 assert(![libraryDevice newPipelineLibraryWithFilePath:0 error:&error]&&error);
 [libraryDevice release];

 /* C4 selector-gate completeness: all four selector names exist and the
  * unsupported ones refuse explicitly without copying or aborting. */
 {
  TeguComputeDevice *selectorDevice=[TeguComputeDevice alloc];
  TeguBuffer *selectorBuffer=[[TeguBuffer alloc] init];
  assert(![selectorDevice newBufferWithBytesNoCopy:0 length:0 options:0 deallocator:0]);
  assert(![selectorDevice newResourceGroupFromResources:0 count:0]);
  [selectorDevice encodeCacheHint:0];
  assert(![selectorBuffer newLinearTextureWithDescriptor:0 offset:0 bytesPerRow:0 bytesPerImage:0]);
  [selectorBuffer release];
  [selectorDevice release];
 }

 /* DELTA15 selector-gate completion owned by compute. */
 {
  TeguComputeDevice *selectorDevice=[TeguComputeDevice alloc];
  assert([selectorDevice name]);
  const char *deviceName=[(NSString *)[selectorDevice name] UTF8String];
  assert(deviceName&&(!strcmp(deviceName,"Tegu Mali GPU")||!strcmp(deviceName,"Nord2 Mali-G77")));
  assert([selectorDevice minimumLinearTextureAlignmentForPixelFormat:80]==64);
  assert([selectorDevice minimumLinearTextureAlignmentForPixelFormat:10]==64);
  assert([selectorDevice minimumLinearTextureAlignmentForPixelFormat:999]==0);
  TeguQueue *selectorQueue=[selectorDevice newCommandQueue];
  assert(selectorQueue);
  TeguCommand *descriptorCommand=[selectorQueue commandBufferWithDescriptor:0];
  assert(descriptorCommand);
  TeguCommand *eventCommand=[selectorQueue commandBuffer];
  assert(eventCommand);
  [eventCommand encodeSignalEvent:0 value:1];
  assert(eventCommand->invalid&&eventCommand->failure);
  /* A foreign event is never signaled, and the first NSError must survive
   * execution rather than being overwritten and leaked. */
  id firstEventError=eventCommand->failure;
  [eventCommand encodeSignalEvent:@"foreign-event" value:2];
  assert(eventCommand->failure==firstEventError);
  [eventCommand teguExecute];
  assert(eventCommand->failure==firstEventError);
  TeguBuffer *selectorBuffer=[[TeguBuffer alloc] init];
  assert(![selectorBuffer newTextureWithDescriptor:0 offset:0 bytesPerRow:0]);
  [selectorBuffer release];
  TeguLibrary *emptyLibrary=[[TeguLibrary alloc] init];
  __block unsigned completions=0;
  [emptyLibrary newFunctionWithName:@"missing" constantValues:0
   completionHandler:^(id function,NSError *error){assert(!function);assert(error);completions++;}];
  assert(completions==1);
  [emptyLibrary release];
  [selectorQueue release];
  [selectorDevice release];
 }

 /* DELTA11: device bound, per-index storage, Metal 16-byte/32KiB checks. */
 TeguComputeDevice *lengthDevice=[TeguComputeDevice alloc];
 assert([lengthDevice maxThreadgroupMemoryLength]==32768);
 TeguQueue *lengthQueue=[lengthDevice newCommandQueue];
 assert(lengthQueue);
 {
  TeguCommand *command=[lengthQueue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  assert(encoder);
  [encoder setThreadgroupMemoryLength:16 atIndex:0];
  [encoder setThreadgroupMemoryLength:4096 atIndex:30];
  assert(!command->invalid&&[encoder teguHostThreadgroupMemoryLengthAtIndex:0]==16&&[encoder teguHostThreadgroupMemoryLengthAtIndex:30]==4096);
  [encoder setThreadgroupMemoryLength:48 atIndex:1];
  assert(!command->invalid&&[encoder teguHostThreadgroupMemoryLengthAtIndex:1]==48);
  [encoder endEncoding];
 }
 {
  TeguCommand *command=[lengthQueue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  assert(encoder);
  [encoder setThreadgroupMemoryLength:17 atIndex:0];
  assert(command->invalid);
  [encoder endEncoding];
 }
 {
  TeguCommand *command=[lengthQueue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  assert(encoder);
  [encoder setThreadgroupMemoryLength:32784 atIndex:0];
  assert(command->invalid);
  [encoder endEncoding];
 }
 {
  TeguCommand *command=[lengthQueue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  assert(encoder);
  [encoder setThreadgroupMemoryLength:16 atIndex:31];
  assert(command->invalid);
  [encoder endEncoding];
 }

 /* A non-inert dispatch with no compiled package still reaches the frontend
  * boundary, where the host seam records the exact table before the planner
  * rejects the missing package. This proves the table is not dropped. */
 {
  TeguPipeline *pipeline=[[TeguPipeline alloc] init];
  pipeline->owner=[lengthDevice retain];
  pipeline->info.local[0]=pipeline->info.local[1]=pipeline->info.local[2]=1;
  TeguCommand *command=[lengthQueue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  assert(encoder);
  for(unsigned i=0;i<31;i++)tegu_host_forwarded_threadgroup_lengths[i]=0;
  [encoder setComputePipelineState:pipeline];
  [encoder setThreadgroupMemoryLength:256 atIndex:2];
  [encoder setThreadgroupMemoryLength:1024 atIndex:7];
  [encoder dispatchThreadgroups:(MTLSize){1,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];
  assert(tegu_host_forwarded_threadgroup_lengths[2]==256);
  assert(tegu_host_forwarded_threadgroup_lengths[7]==1024);
  assert(tegu_host_forwarded_threadgroup_lengths[0]==0);
  assert(command->invalid&&command->count==0);
  [encoder endEncoding];
  [pipeline release];
 }

 /* Inert states drop every dispatch without poisoning the command or adding a
  * job; both the host-created state and the backboardd fallback are covered. */
 {
  TeguPipeline *pipeline=[[TeguPipeline alloc] init];
  pipeline->owner=[lengthDevice retain];pipeline->inert=1;
  pipeline->info.local[0]=pipeline->info.local[1]=pipeline->info.local[2]=1;
  TeguCommand *command=[lengthQueue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  assert(encoder);
  [encoder setComputePipelineState:pipeline];
  [encoder setThreadgroupMemoryLength:256 atIndex:0];
  [encoder dispatchThreadgroups:(MTLSize){2,3,1} threadsPerThreadgroup:(MTLSize){1,1,1}];
  assert(!command->invalid&&command->count==0);
  [encoder endEncoding];
  [pipeline release];
 }
 tegu_host_process_backboardd=1;
 {
  id inertError=(id)1;
  TeguPipeline *pipeline=[lengthDevice newComputePipelineStateWithFunction:0 error:&inertError];
#ifdef TEGU_INERT_PIPELINES
  assert(pipeline&&pipeline->inert&&!inertError);
  TeguCommand *command=[lengthQueue commandBuffer];
  TeguEncoder *encoder=[command computeCommandEncoder];
  assert(encoder);
  [encoder setComputePipelineState:pipeline];
  [encoder setThreadgroupMemoryLength:64 atIndex:3];
  [encoder dispatchThreads:(MTLSize){4,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];
  assert(!command->invalid&&command->count==0);
  [encoder endEncoding];
  [pipeline release];
#else
  assert(!pipeline&&inertError);
#endif
 }
 tegu_host_process_backboardd=0;
 {
  id inertError=(id)1;
  TeguPipeline *pipeline=[lengthDevice newComputePipelineStateWithFunction:0 error:&inertError];
#ifdef TEGU_INERT_PIPELINES
  assert(!pipeline&&inertError);
#else
  assert(!pipeline&&inertError);
#endif
 }
 [lengthQueue release];
 [lengthDevice release];
 drain();
 puts("PASS frontend delta5: bounded NOTREADY retry, compositor gate states, pipeline-library refusal");
 return 0;
}
