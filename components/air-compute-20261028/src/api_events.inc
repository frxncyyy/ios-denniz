/* MTLFence, MTLEvent and MTLSharedEvent for the Tegu frontend.
 *
 * Semantics (host-tested in tests/api_surface_frontend_test.py):
 *  - A command buffer is the unit the queue executes, strictly in encode order
 *    on its queue's serial submission stream.  Fences order encoders inside one
 *    command queue, which this stream already guarantees, so TeguFence is an
 *    identity object: encoders validate it (class + device) and count updates,
 *    nothing waits.  A fence from another device invalidates the command.
 *  - Events cross queues and processes' threads.  -encodeSignalEvent:value: and
 *    -encodeWaitForEvent:value: append a PSEUDO-JOB (Dispatch.eventKind) at the
 *    encode position.  The job is a barrier for every reorder/batch pass
 *    because it is not a generic draw.  Execution (teguExecute):
 *      signal: all earlier GPU jobs of the command are reaped first, then the
 *              event value is stored (Metal: "signals when the prior work
 *              completes"); listeners whose threshold is reached are scheduled.
 *      wait  : the queue's submission thread polls with the shared backoff until
 *              event value >= value, bounded by TEGU_EVENT_WAIT_LIMIT_S; expiry
 *              fails the command (Metal's GPU watchdog also fails a stalled
 *              command buffer) with an NSError instead of hanging the queue.
 *    A signal that never ran because an earlier job failed is still delivered
 *    after the loop so other queues do not stall behind a failed producer.
 *  - MTLSharedEvent adds signaledValue/setSignaledValue:/notifyListener/
 *    waitUntilSignaledValue:timeoutMS:.  Cross-PROCESS sharing needs a Mach-port
 *    handle and is refused: -newSharedEventHandle returns nil and
 *    -newSharedEventWithHandle: returns nil (no silent fake). */

#ifndef TEGU_EVENT_WAIT_LIMIT_S
#define TEGU_EVENT_WAIT_LIMIT_S 30u
#endif
enum { TEGU_EVENT_NONE=0, TEGU_EVENT_WAIT=1, TEGU_EVENT_SIGNAL=2, TEGU_EVENT_INDIRECT=3, TEGU_EVENT_DONE=4 };
/* kind 3 (indirect compute dispatch, api_surface.inc) shares the pseudo-job path: the grid is read when the
 * job runs, after every earlier GPU job was reaped. */
static int tegu_indirect_dispatch_run(struct Dispatch *job);

/* Scheduling/blocks for listeners.  Device builds resolve libdispatch and the
 * block runtime lazily; host tests install fakes into tegu_event_ops. */
struct TeguEventOps {
 void (*retain)(void *);void (*release)(void *);
 void (*schedule)(void *queue,void *context,void (*work)(void *));
 void *(*blockCopy)(const void *);void (*blockRelease)(const void *);
 int ready;
};
static struct TeguEventOps tegu_event_ops;
static int tegu_event_ops_ready(void) {
 if(__atomic_load_n(&tegu_event_ops.ready,__ATOMIC_ACQUIRE))return 1;
#ifndef TEGU_FRONTEND_HOST
 void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
 if(!system)return 0;
 struct TeguEventOps ops={dlsym(system,"dispatch_retain"),dlsym(system,"dispatch_release"),
  dlsym(system,"dispatch_async_f"),dlsym(system,"_Block_copy"),dlsym(system,"_Block_release"),0};
 if(!ops.retain||!ops.release||!ops.schedule||!ops.blockCopy||!ops.blockRelease)return 0;
 tegu_event_ops.retain=ops.retain;tegu_event_ops.release=ops.release;tegu_event_ops.schedule=ops.schedule;
 tegu_event_ops.blockCopy=ops.blockCopy;tegu_event_ops.blockRelease=ops.blockRelease;
 __atomic_store_n(&tegu_event_ops.ready,1,__ATOMIC_RELEASE);
 return 1;
#else
 return 0;
#endif
}

struct TeguEventListener { struct TeguEventListener *next;U64 value;void *queue;void *block; };
typedef void (^TeguEventNotification)(id event,U64 value);

@interface NSObject (TeguEventListenerSelectors)
- (void *)dispatchQueue;
@end

@interface TeguFence : NSObject {
@public TeguComputeDevice *owner;id debugLabel;unsigned updates,waits;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
@end
@interface TeguEvent : NSObject {
@public TeguComputeDevice *owner;id debugLabel;U64 signaled;unsigned lock;struct TeguEventListener *listeners;
}
- (id)device;
- (id)label;
- (void)setLabel:(id)value;
@end
@interface TeguSharedEvent : TeguEvent
- (U64)signaledValue;
- (void)setSignaledValue:(U64)value;
- (signed char)waitUntilSignaledValue:(U64)value timeoutMS:(U64)timeout;
- (void)notifyListener:(id)listener atValue:(U64)value block:(TeguEventNotification)block;
- (id)newSharedEventHandle;
@end

@implementation TeguFence
- (id)device { return owner; }
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id replacement=[value copy];[debugLabel release];debugLabel=replacement; }
- (void)dealloc { [debugLabel release];[owner release];[super dealloc]; }
@end

struct TeguEventNotifyContext { void *block;void *queue;id event;U64 value; };
static void tegu_event_notify_run(void *context) {
 struct TeguEventNotifyContext *n=context;
 ((TeguEventNotification)n->block)(n->event,n->value);
 tegu_event_ops.blockRelease(n->block);
 tegu_event_ops.release(n->queue);
 [n->event release];free(n);
}

/* Store a value and schedule every listener whose threshold it reaches. */
static void tegu_event_set(TeguEvent *event,U64 value) {
 struct TeguEventListener *fire=0,**tail=&fire;
 while(__atomic_exchange_n(&event->lock,1u,__ATOMIC_ACQUIRE)){}
 __atomic_store_n(&event->signaled,value,__ATOMIC_RELEASE);
 struct TeguEventListener **link=&event->listeners;
 while(*link) {
  struct TeguEventListener *node=*link;
  if(value>=node->value){*link=node->next;node->next=0;*tail=node;tail=&node->next;}
  else link=&node->next;
 }
 __atomic_store_n(&event->lock,0u,__ATOMIC_RELEASE);
 while(fire) {
  struct TeguEventListener *node=fire;fire=node->next;
  struct TeguEventNotifyContext *context=malloc(sizeof(*context));
  if(!context||!tegu_event_ops_ready()) {
   if(context)free(context);
   if(tegu_event_ops.blockRelease)tegu_event_ops.blockRelease(node->block);
   if(tegu_event_ops.release)tegu_event_ops.release(node->queue);
   free(node);continue;
  }
  *context=(struct TeguEventNotifyContext){node->block,node->queue,[event retain],value};
  tegu_event_ops.schedule(node->queue,context,tegu_event_notify_run);
  free(node);
 }
}

@implementation TeguEvent
- (id)device { return owner; }
- (id)label { return debugLabel; }
- (void)setLabel:(id)value { id replacement=[value copy];[debugLabel release];debugLabel=replacement; }
- (void)dealloc {
 struct TeguEventListener *node=listeners;
 while(node) {
  struct TeguEventListener *next=node->next;
  if(tegu_event_ops.blockRelease)tegu_event_ops.blockRelease(node->block);
  if(tegu_event_ops.release)tegu_event_ops.release(node->queue);
  free(node);node=next;
 }
 [debugLabel release];[owner release];[super dealloc];
}
@end

/* Elapsed-time bound: a real clock where the build has one (device, tests that
 * enable it), else a bounded number of backoff steps (~20 us each at the tail). */
static int tegu_event_wait_expired(double began,double limitSeconds,unsigned spins) {
 if(began>0)return tegu_host_seconds()-began>=limitSeconds;
 return spins>=(unsigned)(limitSeconds*56000.0);
}

@implementation TeguSharedEvent
- (U64)signaledValue { return __atomic_load_n(&signaled,__ATOMIC_ACQUIRE); }
- (void)setSignaledValue:(U64)value { tegu_event_set(self,value); }
- (signed char)waitUntilSignaledValue:(U64)value timeoutMS:(U64)timeout {
 double began=tegu_host_seconds();unsigned spins=0;
 double limit=timeout>=~(U64)0/1000u?1.0e12:(double)timeout/1000.0;
 while(__atomic_load_n(&signaled,__ATOMIC_ACQUIRE)<value) {
  if(tegu_event_wait_expired(began,limit,spins))return 0;
  tegu_wait_backoff(&spins);
 }
 return 1;
}
- (void)notifyListener:(id)listener atValue:(U64)value block:(TeguEventNotification)block {
 if(!listener||!block)return;
 if(!tegu_event_ops_ready()||!tegu_event_ops.retain||!tegu_event_ops.release||!tegu_event_ops.schedule||!tegu_event_ops.blockCopy){
  MARK("FAIL event_listener_dispatch");return;
 }
 void *queue=[listener dispatchQueue];if(!queue)return;
 struct TeguEventListener *node=calloc(1,sizeof(*node));if(!node)return;
 node->block=tegu_event_ops.blockCopy(block);
 if(!node->block){free(node);return;}
 tegu_event_ops.retain(queue);node->queue=queue;node->value=value;
 /* Threshold already reached: deliver without waiting for another store. */
 while(__atomic_exchange_n(&lock,1u,__ATOMIC_ACQUIRE)){}
 U64 now=__atomic_load_n(&signaled,__ATOMIC_ACQUIRE);
 if(now<value){
  struct TeguEventListener **link=&listeners;while(*link)link=&(*link)->next;
  *link=node;__atomic_store_n(&lock,0u,__ATOMIC_RELEASE);return;
 }
 __atomic_store_n(&lock,0u,__ATOMIC_RELEASE);
 struct TeguEventNotifyContext *context=malloc(sizeof(*context));
 if(!context){tegu_event_ops.blockRelease(node->block);tegu_event_ops.release(queue);free(node);return;}
 *context=(struct TeguEventNotifyContext){node->block,queue,[self retain],now};free(node);
 tegu_event_ops.schedule(queue,context,tegu_event_notify_run);
}
/* A handle crosses processes through a Mach port; this frontend has none. */
- (id)newSharedEventHandle { return 0; }
@end

@interface TeguComputeDevice (APIEvents)
- (id)newFence;
- (id)newEvent;
- (id)newSharedEvent;
- (id)newSharedEventWithHandle:(id)handle;
@end
@implementation TeguComputeDevice (APIEvents)
- (id)newFence {
 TeguFence *fence=[[TeguFence alloc] init];if(!fence)return 0;
 fence->owner=[self retain];return fence;
}
- (id)newEvent {
 TeguEvent *event=[[TeguEvent alloc] init];if(!event)return 0;
 event->owner=[self retain];return event;
}
- (id)newSharedEvent {
 TeguSharedEvent *event=[[TeguSharedEvent alloc] init];if(!event)return 0;
 event->owner=[self retain];return event;
}
- (id)newSharedEventWithHandle:(id)handle {
 (void)handle;MARK("UNSUPPORTED shared_event_handle");return 0;
}
@end

/* Quiet class-name probe (tegu_owned_class logs every foreign class; plain and
 * shared events are both legitimate here).  Resolved once per process. */
static const char *tegu_api_class_name(id object) {
 static id (*objectClass)(id);static const char *(*className)(id);static unsigned resolved;
 if(!object)return 0;
 if(!__atomic_load_n(&resolved,__ATOMIC_ACQUIRE)) {
#ifdef TEGU_FRONTEND_HOST
  extern const char *class_getName(id);
  id (*oc)(id)=tegu_host_object_class;const char *(*cn)(id)=class_getName;
#else
  void *runtime=dlopen("/usr/lib/libobjc.A.dylib",2);
  id (*oc)(id)=runtime?dlsym(runtime,"object_getClass"):0;
  const char *(*cn)(id)=runtime?dlsym(runtime,"class_getName"):0;
#endif
  if(!oc||!cn)return 0;
  __atomic_store_n(&objectClass,oc,__ATOMIC_RELAXED);__atomic_store_n(&className,cn,__ATOMIC_RELAXED);
  __atomic_store_n(&resolved,1,__ATOMIC_RELEASE);
 }
 id (*oc)(id)=__atomic_load_n(&objectClass,__ATOMIC_RELAXED);const char *(*cn)(id)=__atomic_load_n(&className,__ATOMIC_RELAXED);
 return oc&&cn?cn(oc(object)):0;
}
static int tegu_api_class_is(id object,const char *a,const char *b) {
 const char *name=tegu_api_class_name(object);
 return name&&(!__builtin_strcmp(name,a)||(b&&!__builtin_strcmp(name,b)));
}
/* Encoder-side fence validation: a TeguFence of the command's device. */
static int tegu_fence_valid(TeguCommand *command,id fence) {
 return fence&&tegu_api_class_is(fence,"TeguFence",0)&&((TeguFence *)fence)->owner==command->queue->owner;
}
static void tegu_fence_update(TeguCommand *command,id fence) {
 if(!tegu_fence_valid(command,fence)){TEGU_INVALID(command);return;}
 __atomic_fetch_add(&((TeguFence *)fence)->updates,1u,__ATOMIC_RELAXED);
}
static void tegu_fence_wait(TeguCommand *command,id fence) {
 if(!tegu_fence_valid(command,fence)){TEGU_INVALID(command);return;}
 __atomic_fetch_add(&((TeguFence *)fence)->waits,1u,__ATOMIC_RELAXED);
}

/* Events from other implementations (an IOSurface-backed MTLSharedEvent a framework made itself) are
 * ordinary MTLSharedEvent objects: the public protocol is enough to honour both operations exactly.
 * A plain foreign MTLEvent has no readable value, so it stays refused. */
@interface NSObject (TeguForeignSharedEvent)
- (U64)signaledValue;
- (void)setSignaledValue:(U64)value;
- (signed char)respondsToSelector:(SEL)selector;
@end
static int tegu_event_is_native(id event) { return tegu_api_class_is(event,"TeguEvent","TeguSharedEvent"); }
static int tegu_event_is_foreign_shared(id event) {
 return event&&!tegu_event_is_native(event)&&[event respondsToSelector:@selector(signaledValue)]&&
  [event respondsToSelector:@selector(setSignaledValue:)];
}
static void tegu_event_signal(id event,U64 value) {
 if(tegu_event_is_native(event))tegu_event_set((TeguEvent *)event,value);
 else [event setSignaledValue:value];
}
static U64 tegu_event_value(id event) {
 return tegu_event_is_native(event)?__atomic_load_n(&((TeguEvent *)event)->signaled,__ATOMIC_ACQUIRE):[event signaledValue];
}
/* Command-level event encode (between encoders only). */
static void tegu_command_encode_event(TeguCommand *command,id event,U64 value,unsigned kind) {
 int native=event&&tegu_event_is_native(event);
 if([command teguCommitted]||command->encoding||command->count>=TEGU_COMMAND_MAX_JOBS||!event||
    (native?((TeguEvent *)event)->owner!=command->queue->owner:!tegu_event_is_foreign_shared(event))) {
  if(!command->failure)command->failure=[[NSError errorWithDomain:@"TeguEvent" code:1 userInfo:0] retain];
  TEGU_INVALID(command);return;
 }
 struct Dispatch *job=tegu_command_job(command,command->count++);
 job->eventKind=kind;job->eventObject=[event retain];job->eventValue=value;command->eventJobs++;
}

/* Execute one event pseudo-job on the queue's submission thread.  Returns 0 or
 * a negative command error.  Marks the job done so the post-loop flush skips it. */
static int tegu_event_job_run(struct Dispatch *job) {
 unsigned kind=job->eventKind&3u;
 if(kind==TEGU_EVENT_INDIRECT){job->eventKind|=TEGU_EVENT_DONE;return tegu_indirect_dispatch_run(job);}
 id event=job->eventObject;
 job->eventKind|=TEGU_EVENT_DONE;
 if(kind==TEGU_EVENT_SIGNAL){tegu_event_signal(event,job->eventValue);return 0;}
 double began=tegu_host_seconds();unsigned spins=0;
 while(tegu_event_value(event)<job->eventValue) {
  if(tegu_event_wait_expired(began,(double)TEGU_EVENT_WAIT_LIMIT_S,spins)) {
   MARK("FAIL event_wait_timeout");return -3;
  }
  tegu_wait_backoff(&spins);
 }
 return 0;
}
/* After the job loop: deliver signals that did not run (failure before them). */
static void tegu_command_events_flush(TeguCommand *command) {
 if(!command->eventJobs)return;
 for(unsigned i=0;i<command->count;i++) {
  if(!command->jobChunks[i/TEGU_JOB_CHUNK])continue;
  struct Dispatch *job=&TEGU_JOB(command->jobChunks,i);
  if((job->eventKind&3u)==TEGU_EVENT_SIGNAL&&!(job->eventKind&TEGU_EVENT_DONE)){
   job->eventKind|=TEGU_EVENT_DONE;tegu_event_signal(job->eventObject,job->eventValue);
  }
 }
}

@interface TeguCommand (APIEvents)
- (void)encodeWaitForEvent:(id)event value:(uint64_t)value;
@end
@implementation TeguCommand (APIEvents)
- (void)encodeWaitForEvent:(id)event value:(uint64_t)value {
 tegu_command_encode_event(self,event,value,TEGU_EVENT_WAIT);
}
@end

/* Indirect-draw argument verification (see api_surface.inc tegu_render_indirect_draw): the arguments were
 * read at encode time; before anything of the command runs they are compared again, and a change fails
 * the command (a CPU rewrite between encode and commit is visible to a real GPU draw, so a different
 * draw must never be submitted silently). */
struct TeguIndirectVerify { struct TeguIndirectVerify *next;TeguBuffer *buffer;NSUInteger offset;unsigned words;uint32_t expected[5]; };
static int tegu_command_indirect_verify(TeguCommand *command) {
 for(struct TeguIndirectVerify *n=(struct TeguIndirectVerify *)command->indirectVerifies;n;n=n->next) {
  if(!n->words)continue; /* retained resource (useResource/useHeap), nothing to compare */
  const unsigned char *bytes=(const unsigned char *)tegu_buffer_bytes(n->buffer);
  if(!bytes||n->offset>n->buffer->logical||n->buffer->logical-n->offset<n->words*4u)return -5;
  if(__builtin_memcmp(bytes+n->offset,n->expected,n->words*4u)){MARK("FAIL indirect_args_changed");return -5;}
 }
 return 0;
}
/* useResource / useHeap: the command retains the object until it is freed (Metal retains every resource an
 * encoder uses unless the buffer was created with unretained references), so an app that drops its reference
 * after commit cannot free memory a shader still reaches through a pointer in an argument buffer.  Stored in
 * the same list as the indirect verifications with words==0.  0 ok, -1 out of memory or too many. */
#define TEGU_MAX_USED_RESOURCES 65536u
static int tegu_command_use_resource(TeguCommand *command,id object) {
 unsigned seen=0,total=0;
 for(struct TeguIndirectVerify *n=(struct TeguIndirectVerify *)command->indirectVerifies;n;n=n->next) {
  total++;
  if(!n->words&&seen<8&&(id)n->buffer==object)return 0; /* same object used again right away */
  if(!n->words)seen++;
 }
 if(total>=TEGU_MAX_USED_RESOURCES)return -1;
 struct TeguIndirectVerify *node=calloc(1,sizeof(*node));
 if(!node)return -1;
 node->buffer=(TeguBuffer *)[object retain];
 node->next=(struct TeguIndirectVerify *)command->indirectVerifies;command->indirectVerifies=node;
 return 0;
}
static void tegu_command_indirect_free(TeguCommand *command) {
 struct TeguIndirectVerify *n=(struct TeguIndirectVerify *)command->indirectVerifies;command->indirectVerifies=0;
 while(n){struct TeguIndirectVerify *next=n->next;[n->buffer release];free(n);n=next;}
}
