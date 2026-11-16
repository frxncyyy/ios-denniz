/* Real ObjC command frontend and FIFO, controlled async/completion backend. */
#define main synchronous_frontend_main
#include "frontend_host_test.m"
#undef main
struct AsyncObject { unsigned refs, pending, target; };
static unsigned asyncObjects, waits, runs, failGroup;
static void (*pendingWork)(void *);
static void *pendingContext;
static unsigned pendingTarget;
extern void *_Block_copy(const void *);
extern void _Block_release(const void *);
static unsigned copied,released,callbacks,scheduledCallbacks;
static void *copy_block(const void *p){copied++;return _Block_copy(p);}
static void release_block(const void *p){released++;_Block_release(p);}
__attribute__((noinline)) static void register_handlers(TeguCommand *c,unsigned expected) {
 unsigned cookie=0x12345678; void *identity=c;
 [c addCompletedHandler:^(id value){assert(value==identity);assert(cookie==0x12345678);assert([value status]==expected);assert(callbacks++==0);assert(!((TeguCommand *)value)->callbacksDone);}];
 [c addCompletedHandler:^(id value){assert(value==identity);assert(callbacks++==1);assert(!((TeguCommand *)value)->callbacksDone);}];
}
__attribute__((noinline)) static void register_scheduled_handler(TeguCommand *c) {
 void *identity=c;
 [c addScheduledHandler:^(id value){
  assert(value==identity);assert([value status]==3);assert(!callbacks);
  assert(!((TeguCommand *)value)->scheduledCallbacksDone);scheduledCallbacks++;
 }];
}
static struct AsyncObject *async_object(unsigned target) {
 struct AsyncObject *o=calloc(1,sizeof(*o));assert(o);o->refs=1;o->target=target;asyncObjects++;return o;
}
static void async_retain(void *p) {struct AsyncObject *o=p;assert(o->refs);o->refs++;}
static void async_release(void *p) {struct AsyncObject *o=p;assert(o->refs);if(!--o->refs){assert(!o->pending);asyncObjects--;free(o);}}
static void async_schedule(void *p,void *context,void (*work)(void *)) {
 assert(!pendingWork);pendingWork=work;pendingContext=context;pendingTarget=((struct AsyncObject *)p)->target;
}
static void *group_create(void) {return failGroup?0:async_object(0);}
static void group_enter(void *p) {struct AsyncObject *o=p;assert(!o->pending);o->pending++;}
static void group_leave(void *p) {struct AsyncObject *o=p;assert(o->pending==1);o->pending--;}
static void pump_async(void) {
 assert(pendingWork);void (*work)(void *)=pendingWork;void *context=pendingContext;
 assert(pendingTarget==(runs==1?8:7));pendingWork=0;runs++;work(context);
}
static long group_wait(void *p,U64 timeout) {
 struct AsyncObject *o=p;assert(timeout==~(U64)0);waits++;
 while(o->pending)pump_async();return 0;
}
int main(void) {
 TeguComputeDevice *d=[TeguComputeDevice alloc];d->scalar=call;d->method=method;d->map=map;d->unmap=unmap;
 TeguQueue *q=[d newCommandQueue];
 q->submissions.ops=(struct tegu_submission_ops){async_retain,async_release,async_schedule};
 q->blockCopy=copy_block;q->blockRelease=release_block;
 q->groupCreate=group_create;q->groupEnter=group_enter;q->groupLeave=group_leave;q->groupWait=group_wait;
 failGroup=1;assert(![q commandBuffer]);failGroup=0;
 unsigned char package[72]={0};unsigned words[16]={TEGU_PIPELINE_MAGIC,1,8,TEGU_PIPELINE_GPU,32,43,0,0x02000000,7,1,1,1,0,0,1,0};
 for(unsigned i=0;i<16;i++)tegu_pipeline_put(package+4*i,words[i]);
 TeguPipeline *p=[d newTeguPipelineWithPackage:package length:72];
 TeguBuffer *a=[d newBufferWithLength:1152 options:0],*b=[d newBufferWithLength:1152 options:0],*s=[d newBufferWithLength:4 options:0];
 struct AsyncObject *first=async_object(7),*second=async_object(8);
 [q setCompletionQueue:first];async_release(first);
 TeguCommand *c1=[[q commandBuffer] retain];TeguEncoder *e=[c1 computeCommandEncoder];
 [e setComputePipelineState:p];[e setBuffer:a offset:128 atIndex:0];[e setBuffer:b offset:128 atIndex:1];[e setBuffer:s offset:0 atIndex:2];
 [e dispatchThreadgroups:(MTLSize){256,1,1} threadsPerThreadgroup:(MTLSize){1,1,1}];[e endEncoding];
 register_handlers(c1,4);register_scheduled_handler(c1);
 [c1 commit];[c1 commit];assert([c1 status]==4&&dispatched==1&&!callbacks&&!c1->callbacksDone);
 assert(scheduledCallbacks==1&&c1->scheduledCallbacksDone);
 [q setCompletionQueue:second];async_release(second);
 /* Terminal GPU status must still enter groupWait and deliver both callbacks. */
 [c1 waitUntilCompleted];assert(callbacks==2&&c1->callbacksDone&&waits==1&&runs==1);
 callbacks=0;
 TeguCommand *c2=[[q commandBuffer] retain];register_handlers(c2,5);register_scheduled_handler(c2);
 [c2 computeCommandEncoder];[c2 commit]; /* unfinished encoding, not an empty command */
 assert([c2 status]==5&&[c2 error]&&!callbacks&&!c2->callbacksDone&&scheduledCallbacks==2&&c2->scheduledCallbacksDone);
 [q setCompletionQueue:0];
 [p release];[a release];[b release];[s release];[q release];[d release];drain();
 [c1 release];assert(allocated==freed);
 [c2 waitUntilCompleted];assert(callbacks==2&&runs==2&&waits==2);
 [c2 waitUntilCompleted];[c2 commit];assert(callbacks==2&&waits==2);
 [c2 release];drain();assert(!live&&!asyncObjects&&!pendingWork&&copied==released);
 /* Releasing an uncommitted command must dispose its copied blocks. */
 d=[TeguComputeDevice alloc];q=[d newCommandQueue];q->blockCopy=copy_block;q->blockRelease=release_block;
 TeguCommand *unused=[q commandBuffer];register_handlers(unused,4);register_scheduled_handler(unused);
 [q release];[d release];drain();assert(!live&&copied==released&&callbacks==2);
 callbacks=0;
 d=[TeguComputeDevice alloc];q=[d newCommandQueue];
 q->blockCopy=copy_block;q->blockRelease=release_block;
 q->submissions.ops=(struct tegu_submission_ops){async_retain,async_release,async_schedule};
 q->groupCreate=group_create;q->groupEnter=group_enter;q->groupLeave=group_leave;q->groupWait=group_wait;
 first=async_object(7);[q setCompletionQueue:first];async_release(first);
 TeguCommand *orphan=[q commandBuffer];register_handlers(orphan,4);register_scheduled_handler(orphan);[orphan commit];
 [q setCompletionQueue:0];[q release];[d release];drain();
 assert(live&&pendingWork&&!callbacks&&scheduledCallbacks==3);pump_async();drain();
 assert(!live&&!asyncObjects&&callbacks==2&&scheduledCallbacks==3&&copied==released);
 puts("PASS completion frontend: copied escaping blocks, ordered success/error callbacks, terminal-status wait, target replacement, cleanup");
}
