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
static unsigned allocated,freed,dispatched,serial,failDispatch,failAlloc,failMap,failFree;
static NSUInteger allocationExtra;
static void *memory[4096];
static size_t memoryBytes[4096];
static int call(unsigned c,unsigned selector,const U64 *args,unsigned argc,U64 *out,unsigned *n) {
 (void)c;(void)argc;
 if(selector==1) { if(failAlloc)return -1; assert(*n==2); unsigned h=++allocated; assert(h<4096);
  memoryBytes[h]=((args[0]+16383)&~(U64)16383)+allocationExtra;memory[h]=calloc(1,memoryBytes[h]);
  out[0]=h;out[1]=memoryBytes[h];return 0; }
 if(selector==2) { assert(memory[*args]);if(failFree)return -9;free(memory[*args]);memory[*args]=0;memoryBytes[*args]=0;freed++;return 0; }
 if(selector==6) return 0;
 abort();
}
static int map(unsigned c,unsigned h,unsigned task,U64 *addr,U64 *size,unsigned options) { (void)c;(void)task;(void)options;if(failMap||h>=4096||!memory[h])return -1;*addr=(NSUInteger)memory[h];*size=memoryBytes[h];return 0; }
static int unmap(unsigned c,unsigned h,unsigned task,U64 addr) { (void)c;(void)h;(void)task;(void)addr;return 0; }
static int method(unsigned c,unsigned selector,const U64 *args,unsigned argc,const void *data,NSUInteger size,U64 *out,unsigned *n,void *unused,NSUInteger *unusedSize) {
 (void)c;(void)unused;(void)unusedSize;
 if(selector==5) { assert(size==72&&*n==1);out[0]=0x80000001;return 0; }
 assert(selector==7&&argc==1&&args[0]==0x80000001&&size==272&&*n==2);
 const unsigned *wire=data;
 assert(wire[0]==1&&wire[1]==256&&wire[2]==1&&wire[3]==1);
 for(unsigned i=0;i<3;i++) assert(memory[wire[4+4*i]]);
 for(unsigned i=16;i<68;i++) assert(wire[i]==0);
 assert(wire[5]==128&&wire[9]==128&&wire[13]==0);
 assert(wire[6]==1024&&wire[10]==1024&&wire[14]==4);
 dispatched++;
 if(failDispatch) return -17;
 out[0]=++serial;out[1]=42;return 0;
}
static void drain(void) { while(poolCount) [pool[--poolCount] release]; }
#include "depth_stencil_host_test.inc"
#include "drawable_frontend_host_test.inc"
int main(void) {
 test_depth_stencil();
 test_drawable_presentation();
 TeguComputeDevice *identityDevice=[TeguComputeDevice alloc];
 const struct MTLTargetDeviceArch *identity=[identityDevice targetDeviceInfo];
 assert(identity&&identity==[identityDevice targetDeviceInfo]);
 assert(!identity->identifier&&!identity->version&&!strcmp(identity->name,TEGU_TARGET_CACHE_NAME));
 [identityDevice release];
 assert(!strcmp(identity->name,TEGU_TARGET_CACHE_NAME)); /* static lifetime */
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->scalar=call;d->method=method;d->map=map;d->unmap=unmap;
 assert([d currentAllocatedSize]==0);
 unsigned char source[33];for(unsigned i=0;i<33;i++)source[i]=(unsigned char)(i+1);
 TeguBuffer *copy=[d newBufferWithBytes:source length:33 options:0];
 assert(copy&&[copy length]==33&&[copy device]==d);
 assert(!memcmp([copy contents],source,33));
 source[0]=99;assert(((unsigned char *)[copy contents])[0]==1);
 assert(![copy label]);
 TestLabel *bufferLabel=[[TestLabel alloc] init];bufferLabel->value=23;
 [copy setLabel:bufferLabel];bufferLabel->value=91;[bufferLabel release];
 assert(((TestLabel *)[copy label])->value==23);
 [copy setLabel:[copy label]];assert(((TestLabel *)[copy label])->value==23);
 [copy setLabel:0];assert(![copy label]);
 bufferLabel=[[TestLabel alloc] init];bufferLabel->value=47;[copy setLabel:bufferLabel];[bufferLabel release];
 assert([d currentAllocatedSize]==16384);
 [copy release];assert([d currentAllocatedSize]==0);
 assert(![d newBufferWithBytes:0 length:33 options:0]);
 assert(![d newBufferWithBytes:source length:0 options:0]);
 assert(![d newBufferWithBytes:source length:33 options:16]);
 failAlloc=1;assert(![d newBufferWithBytes:source length:33 options:0]);failAlloc=0;
 failMap=1;assert(![d newBufferWithBytes:source length:33 options:0]);failMap=0;
 assert([d currentAllocatedSize]==0&&allocated==freed);
 MTLSize max=[d maxThreadsPerThreadgroup];assert(max.width==256&&max.height==256&&max.depth==256);
 assert(![d maxThreadgroupMemoryLength]&&[d hasUnifiedMemory]&&![d isRemovable]);
 assert([d isHeadless]&&![d supportsSampleCount:1]);
 unsigned baselineFreed=freed;
 unsigned char package[72]={0};unsigned words[16]={TEGU_PIPELINE_MAGIC,1,8,TEGU_PIPELINE_GPU,32,43,0,0x02000000,7,1,1,1,0,0,1,0};
 for(unsigned i=0;i<16;i++) tegu_pipeline_put(package+4*i,words[i]);
 assert(![d newTeguPipelineWithPackage:package length:71]);
 TeguPipeline *p=[d newTeguPipelineWithPackage:package length:72];assert(p);
 TeguBuffer *a=[d newBufferWithLength:1152 options:0],*b=[d newBufferWithLength:1152 options:0],*s=[d newBufferWithLength:4 options:0];
 assert(a&&b&&s&&![d newBufferWithLength:4 options:16]);
 assert([d currentAllocatedSize]==4*16384);
 TeguQueue *q=[d newCommandQueue];
 assert(![q label]);
 TestLabel *label=[[TestLabel alloc] init];label->value=42;
 [q setLabel:label];label->value=99;[label release];
 assert(((TestLabel *)[q label])->value==42);
 [q setLabel:[q label]];assert(((TestLabel *)[q label])->value==42);
 [q setLabel:0];assert(![q label]);
 label=[[TestLabel alloc] init];label->value=17;[q setLabel:label];[label release];

 TeguCommand *labeled=[q commandBuffer];assert(![labeled label]);
 label=[[TestLabel alloc] init];label->value=73;
 [labeled setLabel:label];label->value=99;[label release];
 assert(((TestLabel *)[labeled label])->value==73);
 [labeled setLabel:[labeled label]];assert(((TestLabel *)[labeled label])->value==73);
 [labeled setLabel:0];assert(![labeled label]);
 assert(![labeled protectionOptions]);[labeled setProtectionOptions:0];
 assert(![labeled protectionOptions]&&!labeled->invalid);
 TeguCommand *protected=[q commandBuffer];[protected setProtectionOptions:1];
 assert(protected->invalid&&![protected protectionOptions]);
 TeguCommand *submitted=[q commandBuffer];
 assert([submitted commitAndWaitUntilSubmitted]&&[submitted status]==4&&![submitted error]);
 TeguCommand *submissionError=[q commandBuffer];[submissionError computeCommandEncoder];
 assert(![submissionError commitAndWaitUntilSubmitted]&&[submissionError status]==5&&[submissionError error]);
 TeguCommand *deadlineSubmitted=[q commandBuffer];
 assert([deadlineSubmitted commitAndWaitUntilSubmittedWithDeadline:123]&&[deadlineSubmitted status]==4);
 TeguCommand *deadlineCommit=[q commandBuffer];[deadlineCommit commitWithDeadline:456];
 assert([deadlineCommit status]==4);[deadlineCommit waitUntilScheduled];

 for(unsigned scenario=0;scenario<4;scenario++) {
  TeguCommand *c=[q commandBuffer];
  if(!scenario){[c enqueue];assert([c status]==1);}
  TeguEncoder *e=[c computeCommandEncoder];
  [e setComputePipelineState:p];[e setBuffer:a offset:128 atIndex:0];[e setBuffer:b offset:128 atIndex:1];[e setBuffer:s offset:0 atIndex:2];
  if(scenario==1) [e setBuffer:s offset:0 atIndex:31];
  if(scenario==0) [e setBuffer:s offset:0 atIndex:30];
  [e dispatchThreadgroups:(MTLSize){256,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];
  if(scenario!=2) [e endEncoding];
  failDispatch=scenario==3;
  unsigned before=dispatched;
  [c commit];[c waitUntilCompleted];
  assert([c status]==(scenario?5:4));assert(!![c error]==!!scenario);
  assert(dispatched==before+(scenario==0||scenario==3));
  [c commit]; assert(dispatched==before+(scenario==0||scenario==3));
  if(scenario==2) [e endEncoding];
  drain();
 }
 /* Real Metal command buffers commonly contain more than sixteen operations.
  * Preserve a bounded allocation, but prove that the old compositor startup
  * ceiling is gone and that the new boundary refuses without overrunning. */
 failDispatch=0;
 unsigned beforeMany=dispatched;
 TeguCommand *many=[q commandBuffer];TeguEncoder *manyEncoder=[many computeCommandEncoder];
 [manyEncoder setComputePipelineState:p];[manyEncoder setBuffer:a offset:128 atIndex:0];
 [manyEncoder setBuffer:b offset:128 atIndex:1];[manyEncoder setBuffer:s offset:0 atIndex:2];
 for(unsigned i=0;i<TEGU_COMMAND_MAX_JOBS;i++)
  [manyEncoder dispatchThreadgroups:(MTLSize){256,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];
 assert(many->count==TEGU_COMMAND_MAX_JOBS&&!many->invalid);[manyEncoder endEncoding];
 [many commit];[many waitUntilCompleted];assert([many status]==4&&dispatched==beforeMany+TEGU_COMMAND_MAX_JOBS);drain();
 beforeMany=dispatched;many=[q commandBuffer];manyEncoder=[many computeCommandEncoder];
 [manyEncoder setComputePipelineState:p];[manyEncoder setBuffer:a offset:128 atIndex:0];
 [manyEncoder setBuffer:b offset:128 atIndex:1];[manyEncoder setBuffer:s offset:0 atIndex:2];
 for(unsigned i=0;i<=TEGU_COMMAND_MAX_JOBS;i++)
  [manyEncoder dispatchThreadgroups:(MTLSize){256,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];
 assert(many->count==TEGU_COMMAND_MAX_JOBS&&many->invalid&&many->invalidFunction&&many->invalidLine);
 const char *firstInvalidFunction=many->invalidFunction;unsigned firstInvalidLine=many->invalidLine;
 [manyEncoder endEncoding];[manyEncoder endEncoding];
 assert(many->invalidFunction==firstInvalidFunction&&many->invalidLine==firstInvalidLine);
 [many commit];[many waitUntilCompleted];assert([many status]==5&&dispatched==beforeMany);drain();
 /* Resource snapshots retain objects even if callers release before commit. */
 failDispatch=0;TeguCommand *c=[q commandBuffer];TeguEncoder *e=[c computeCommandEncoder];
 [e setComputePipelineState:p];[e setBuffer:a offset:128 atIndex:0];[e setBuffer:b offset:128 atIndex:1];[e setBuffer:s offset:0 atIndex:2];
 [e dispatchThreadgroups:(MTLSize){256,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];[e endEncoding];
 [d retain];
 [p release];[a release];[b release];[s release];[q release];[d release];assert(freed==baselineFreed);
 assert([d currentAllocatedSize]==4*16384);
 [c commit];assert([c status]==4);drain();assert(allocated==freed&&[d currentAllocatedSize]==0);[d release];assert(live==0);
 puts("PASS frontend recording, errors, ownership, pipeline validation (simulated transport)");
 return 0;
}
