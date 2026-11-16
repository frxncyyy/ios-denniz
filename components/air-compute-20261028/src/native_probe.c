/* Native iOS Objective-C Metal discovery probe, with no SDK header dependency.
 * Success covers object creation and CPU-visible storage, not GPU execution. */
#include "target_device_info.h"
typedef unsigned long usize;
typedef void *object;
typedef void *selector;
extern int printf(const char *, ...);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern char *dlerror(void);
extern long write(int, const void *, usize);
#define MARK(s) ((void)write(2, "TEGU_METAL " s "\n", sizeof("TEGU_METAL " s "\n")-1))

/* /dev/console can stop emitting when the graphics console is acquired. Keep
 * phase results on UFS as well, so absence of a live marker is not called a hang
 * or a pass. Resolve libc here to retain the SDK-free baseline link. */
static int journal_fd=-1;
static int (*journal_sync)(int);
static int (*journal_open_file)(const char *,int,...);
static int (*journal_close_file)(int);
static int (*journal_dup2)(int,int);
static void journal_refresh_console(void)
{
    /* A console session leader can revoke descriptors held by other processes.
     * Reopen before each durable phase report instead of trusting launchd's fd2. */
    if(!journal_open_file||!journal_close_file||!journal_dup2) return;
    int fd=journal_open_file("/dev/console",2);
    if(fd>=0) {
        if(fd!=2) {journal_dup2(fd,2);journal_close_file(fd);}
    }
}
static void journal(const char *phase, int result)
{
    usize length=0;
    while(phase[length]) length++;
    const char suffix[3]={' ',result<0?'?':(char)('0'+result),'\n'};
    if(journal_fd>=0) {
        write(journal_fd,phase,length);
        write(journal_fd,suffix,sizeof(suffix));
        journal_sync(journal_fd);
    }
    journal_refresh_console();
    write(2,phase,length);write(2,suffix,sizeof(suffix));
}
static void journal_open(void)
{
    void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
    int (*open_file)(const char *,int,...)=system?dlsym(system,"open"):0;
    long (*read_file)(int,void *,usize)=system?dlsym(system,"read"):0;
    int (*close_file)(int)=system?dlsym(system,"close"):0;
    journal_open_file=open_file;journal_close_file=close_file;
    journal_dup2=system?dlsym(system,"dup2"):0;
    journal_sync=system?dlsym(system,"fsync"):0;
    journal_refresh_console();
    /* Replay the previous boot's small journal before graphics startup. This
     * provides readback on a second temporary boot without reflashing userdata.
     * Delimit it clearly: prior results must never count as this boot's pass. */
    if(open_file&&read_file&&close_file) {
        int previous=open_file("/var/tmp/tegu-metal-probe.result",0);
        if(previous>=0) {
            MARK("previous_boot_journal_begin");
            char bytes[512];
            usize total=0;
            long n;
            while(total<4096 && (n=read_file(previous,bytes,sizeof(bytes)))>0) {
                write(2,bytes,(usize)n);total+=(usize)n;
            }
            close_file(previous);
            MARK("previous_boot_journal_end");
        }
    }
    /* Darwin O_WRONLY | O_CREAT | O_TRUNC, root-private diagnostic evidence. */
    if(open_file&&journal_sync)
        journal_fd=open_file("/var/tmp/tegu-metal-probe.result",0x601,0600);
    if(journal_fd<0) MARK("journal_unavailable");
}

extern int tegu_transport_probe(void);
#ifdef TEGU_METAL_BASE
extern int tegu_framework_device_probe(void);
#endif
#ifdef TEGU_AIR_BRIDGE
extern int tegu_air_native_probe(void);
#ifdef TEGU_MALI_BRIDGE
extern int tegu_runtime_frontend_probe(void);
#endif
extern int tegu_library_frontend_probe(void);
#endif
#ifdef TEGU_PIPELINE_FIXTURE
extern int tegu_frontend_probe(void);
#endif

#ifdef TEGU_SYSTEM_DEVICE
/* Stack block literals escape this helper; the driver must copy them. */
__attribute__((noinline)) static void tegu_probe_completed(void *send,selector add,object command,unsigned *calls) {
 unsigned cookie=0x54454755;
 ((void (*)(object,selector,void (^)(object)))send)(command,add,^(object value){
  if(value!=command||cookie!=0x54454755||__atomic_fetch_add(calls,1,__ATOMIC_SEQ_CST)!=0)
   __atomic_store_n(calls,100,__ATOMIC_SEQ_CST);
 });
 ((void (*)(object,selector,void (^)(object)))send)(command,add,^(object value){
  if(value!=command||__atomic_fetch_add(calls,1,__ATOMIC_SEQ_CST)!=1)
   __atomic_store_n(calls,100,__ATOMIC_SEQ_CST);
 });
}
struct tegu_probe_gate { void *semaphore; long (*wait)(void *,unsigned long long); };
static void tegu_probe_gate_wait(void *context) {
 struct tegu_probe_gate *gate=context;gate->wait(gate->semaphore,~0ULL);
}
static void tegu_probe_barrier(void *context) { (void)context; }
#endif
#ifdef TEGU_RENDER_PROBE
#include "native_blit_probe.inc"
#include "native_texture_blit_probe.inc"
#include "native_render_probe.inc"
#ifdef TEGU_VERTEX_RENDER_PROBE
#include "native_vertex_render_probe.inc"
#endif
#ifdef TEGU_VARYING_RENDER_PROBE
#include "native_varying_render_probe.inc"
#endif
#ifdef TEGU_BLEND_RENDER_PROBE
#include "native_blend_render_probe.inc"
#endif
#ifdef TEGU_TEXTURE_RENDER_PROBE
#include "tegu-texture-air.h"
#include "native_texture_render_probe.inc"
#endif
#endif
int main(void)
{
    MARK("entered_main");
    journal_open();
    journal("TEGU_RESULT transport_begin",-1);
    int transport=tegu_transport_probe();
    journal("TEGU_RESULT transport",transport?1:0);
    void *objc=dlopen("/usr/lib/libobjc.A.dylib",2);
    MARK("objc_load_returned");
    void *metal=dlopen("/System/Library/Frameworks/Metal.framework/Metal",2);
    MARK("metal_load_returned");
    if (!objc || !metal) {
        printf("TEGU_METAL load_error %s\n",dlerror());
        return 1;
    }
    selector (*sel)(const char *)=dlsym(objc,"sel_registerName");
    void *send=dlsym(objc,"objc_msgSend");
    void *(*push)(void)=dlsym(objc,"objc_autoreleasePoolPush");
    void (*pop)(void *)=dlsym(objc,"objc_autoreleasePoolPop");
    object (*create)(void)=dlsym(metal,"MTLCreateSystemDefaultDevice");
    if (!sel || !send || !push || !pop || !create) {
        printf("TEGU_METAL missing_runtime_symbol\n");
        return 1;
    }
    object (*get)(object,selector)=(object (*)(object,selector))send;
    void (*release)(object,selector)=(void (*)(object,selector))send;
    void *pool=push();
#ifdef TEGU_PIPELINE_FIXTURE
    /* Run bounded experimental frontend before potentially blocking global
     * device discovery. Offline compilation is explicitly distinct. */
#ifdef TEGU_METAL_BASE
    journal("TEGU_RESULT framework_begin",-1);
    int framework=tegu_framework_device_probe();
    journal("TEGU_RESULT framework",framework?1:0);
    if(framework) {journal("TEGU_RESULT complete",1);pop(pool);return 1;}
#endif
    journal("TEGU_RESULT offline_begin",-1);
    int frontend=tegu_frontend_probe();
    journal("TEGU_RESULT offline",frontend?1:0);
    if(transport) frontend=1;
#ifdef TEGU_AIR_BRIDGE
    journal("TEGU_RESULT library_begin",-1);
    int library=tegu_library_frontend_probe();
    journal("TEGU_RESULT library",library?1:0);
    journal("TEGU_RESULT compiler_begin",-1);
    int compiler=tegu_air_native_probe();
    journal("TEGU_RESULT compiler",compiler?1:0);
    if(library||compiler) frontend=1;
#ifdef TEGU_MALI_BRIDGE
    journal("TEGU_RESULT runtime_begin",-1);
    int runtime=tegu_runtime_frontend_probe();
    journal("TEGU_RESULT runtime",runtime?1:0);
    if(runtime) frontend=1;
#endif
#endif
    pop(pool);
    journal("TEGU_RESULT complete",frontend?1:0);
    return frontend;
#endif
#ifdef TEGU_BUNDLE_LOADER
    journal("TEGU_RESULT bundle_load_begin",-1);
    void *foundation=dlopen("/System/Library/Frameworks/Foundation.framework/Foundation",2);
    object (*getClass)(const char *)=dlsym(objc,"objc_getClass");
    object (*superclass)(object)=dlsym(objc,"class_getSuperclass");
    if(!foundation||!getClass||!superclass) {journal("TEGU_RESULT complete",1);pop(pool);return 1;}
    object path=((object (*)(object,selector,const char *))send)(getClass("NSString"),
        sel("stringWithUTF8String:"),"/System/Library/Extensions/TeguMetal.bundle");
    object bundle=((object (*)(object,selector,object))send)(getClass("NSBundle"),sel("bundleWithPath:"),path);
    object error=0;
    signed char loaded=((signed char (*)(object,selector,object *))send)(bundle,sel("loadAndReturnError:"),&error);
    object cls=loaded?get(bundle,sel("principalClass")):0;
    if(!cls||cls!=getClass("TeguComputeDevice")||superclass(cls)!=getClass("_MTLDevice")) {
        journal("TEGU_RESULT bundle_load",1);journal("TEGU_RESULT complete",1);pop(pool);return 1;
    }
    journal("TEGU_RESULT bundle_load",0);
    object device=get(get(cls,sel("alloc")),sel("init"));
    journal("TEGU_RESULT bundle_device",device?0:1);
#else
    journal("TEGU_RESULT system_device_begin",-1);
    MARK("create_device_begin");
    object device=create();
    MARK("create_device_returned");
#endif
    if (!device) {
        journal("TEGU_RESULT complete",1);
        printf("TEGU_METAL no_device\n");
        pop(pool);
        return 2;
    }
#ifdef TEGU_SYSTEM_DEVICE
    object (*getClass)(const char *)=dlsym(objc,"objc_getClass");
    object (*objectClass)(object)=dlsym(objc,"object_getClass");
    object (*getProtocol)(const char *)=dlsym(objc,"objc_getProtocol");
    signed char (*conforms)(object,selector,object)=(void *)send;
    object publicProtocol=getProtocol?getProtocol("MTLDevice"):0;
    object privateProtocol=getProtocol?getProtocol("MTLDeviceSPI"):0;
    int valid=getClass&&objectClass&&publicProtocol&&privateProtocol&&
        objectClass(device)==getClass("TeguComputeDevice")&&
        conforms(device,sel("conformsToProtocol:"),publicProtocol)&&
        conforms(device,sel("conformsToProtocol:"),privateProtocol)&&
        ((unsigned long long (*)(object,selector))send)(device,sel("registryID"));
    object second=create();
    valid=valid && second==device;
    release(second,sel("release"));
    journal("TEGU_RESULT system_device",valid?0:1);
    if(!valid) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#endif
#ifdef TEGU_RENDER_PROBE
    if(tegu_native_blit_probe(device,objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
    if(tegu_native_render_probe(device,objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#ifdef TEGU_VERTEX_RENDER_PROBE
    if(tegu_native_vertex_render_probe(device,objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#endif
#ifdef TEGU_VARYING_RENDER_PROBE
    if(tegu_native_varying_render_probe(device,objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#endif
#ifdef TEGU_BLEND_RENDER_PROBE
    if(tegu_native_blend_render_probe(device,objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#endif
#ifdef TEGU_BGRA_BLEND_RENDER_PROBE
    if(tegu_native_bgra_blend_render_probe(device,objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#endif
#ifdef TEGU_TEXTURE_RENDER_PROBE
    if(tegu_native_texture_render_probe(device,tegu_texture_air,sizeof(tegu_texture_air),objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#endif
#ifdef TEGU_IOSURFACE_RENDER_PROBE
    if(tegu_native_iosurface_render_probe(device,objc,send,sel)) {
        release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;
    }
#endif
#endif
    object name=get(device,sel("name"));
    const char *label=(const char *)get(name,sel("UTF8String"));
    printf("TEGU_METAL device %s\n",label?label:"(unnamed)");
#ifdef TEGU_SYSTEM_DEVICE
    const struct MTLTargetDeviceArch *targetInfo=((const struct MTLTargetDeviceArch *(*)(object,selector))send)
        (device,sel("targetDeviceInfo"));
    const struct MTLTargetDeviceArch *again=((const struct MTLTargetDeviceArch *(*)(object,selector))send)
        (device,sel("targetDeviceInfo"));
    int target_ok=targetInfo&&targetInfo==again&&!targetInfo->identifier&&!targetInfo->version&&targetInfo->name;
    if(target_ok) {
        const char expected[]=TEGU_TARGET_CACHE_NAME;
        for(usize i=0;i<sizeof(expected);i++)if(targetInfo->name[i]!=expected[i]){target_ok=0;break;}
    }
    journal("TEGU_RESULT system_target_info",target_ok?0:1);
    if(!target_ok){release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;}
    journal("TEGU_RESULT system_depth_stencil_begin",-1);
    object descriptor=get(getClass("MTLDepthStencilDescriptor"),sel("new"));
    object depthLabel=((object (*)(object,selector,const char *))send)
        (getClass("NSString"),sel("stringWithUTF8String:"),"Tegu depth stencil state");
    ((void (*)(object,selector,object))send)(descriptor,sel("setLabel:"),depthLabel);
    object depthState=((object (*)(object,selector,object))send)
        (device,sel("newDepthStencilStateWithDescriptor:"),descriptor);
    /* This donor asserts on nil setLabel:, so mutate to a distinct valid label. */
    object changedDepthLabel=((object (*)(object,selector,const char *))send)
        (getClass("NSString"),sel("stringWithUTF8String:"),"Changed descriptor label");
    ((void (*)(object,selector,object))send)(descriptor,sel("setLabel:"),changedDepthLabel);
    release(descriptor,sel("release"));
    object depthProtocol=getProtocol("MTLDepthStencilState");
    int depth_ok=depthState&&depthProtocol&&get(depthState,sel("device"))==device&&
        conforms(depthState,sel("conformsToProtocol:"),depthProtocol)&&
        ((signed char (*)(object,selector,object))send)(get(depthState,sel("label")),sel("isEqualToString:"),depthLabel);
    release(depthState,sel("release"));
    journal("TEGU_RESULT system_depth_stencil",depth_ok?0:1);
    if(!depth_ok){release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;}
    usize baseline=((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"));
    unsigned source[16];for(unsigned i=0;i<16;i++)source[i]=0x54454755u^i;
    object copied=((object (*)(object,selector,const void *,usize,usize))send)
        (device,sel("newBufferWithBytes:length:options:"),source,sizeof(source),0);
    const unsigned *copy=copied?(const unsigned *)get(copied,sel("contents")):0;
    int copy_ok=copy&&((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"))==baseline+16384;
    if(copy) {
        for(unsigned i=0;i<16;i++)if(copy[i]!=source[i])copy_ok=0;
        source[0]=0;if(copy[0]!=0x54454755u)copy_ok=0;
    }
    release(copied,sel("release"));
    if(((usize (*)(object,selector))send)(device,sel("currentAllocatedSize"))!=baseline)copy_ok=0;
    journal("TEGU_RESULT system_buffer_copy",copy_ok?0:1);
    if(!copy_ok) {release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;}
#endif
    object queue=get(device,sel("newCommandQueue"));
#ifdef TEGU_SYSTEM_DEVICE
    object queueLabel=((object (*)(object,selector,const char *))send)
        (getClass("NSString"),sel("stringWithUTF8String:"),"Tegu system Metal probe");
    ((void (*)(object,selector,object))send)(queue,sel("setLabel:"),queueLabel);
    object readLabel=get(queue,sel("label"));
    int label_ok=queue&&queueLabel&&((signed char (*)(object,selector,object))send)
        (readLabel,sel("isEqualToString:"),queueLabel);
    journal("TEGU_RESULT system_queue_label",label_ok?0:1);
    if(!label_ok) {release(queue,sel("release"));release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;}
    /* Current kernel has no priority API: explicit refusal, then verify normal
     * async commands still execute. This is NOT priority scheduling support. */
    int priority_ok=!((signed char (*)(object,selector,usize))send)(queue,sel("setGPUPriority:"),1)&&
        !((signed char (*)(object,selector,usize))send)(queue,sel("setBackgroundGPUPriority:"),3);
    journal("TEGU_RESULT system_priority_refusal",priority_ok?0:1);
    if(!priority_ok){release(queue,sel("release"));release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;}
    journal("TEGU_RESULT system_async_begin",-1);
    void *system=dlopen("/usr/lib/libSystem.B.dylib",2);
    void *(*queueCreate)(const char *,void *)=dlsym(system,"dispatch_queue_create");
    void *(*semaphoreCreate)(long)=dlsym(system,"dispatch_semaphore_create");
    long (*semaphoreWait)(void *,unsigned long long)=dlsym(system,"dispatch_semaphore_wait");
    long (*semaphoreSignal)(void *)=dlsym(system,"dispatch_semaphore_signal");
    void (*async)(void *,void *,void (*)(void *))=dlsym(system,"dispatch_async_f");
    void (*sync)(void *,void *,void (*)(void *))=dlsym(system,"dispatch_sync_f");
    void (*dispatchRelease)(void *)=dlsym(system,"dispatch_release");
    int async_ok=queueCreate&&semaphoreCreate&&semaphoreWait&&semaphoreSignal&&async&&sync&&dispatchRelease;
    if(async_ok) {
        void *target=queueCreate("org.tegu.probe.submission",0),*gate=semaphoreCreate(0);
        async_ok=target&&gate;
        if(async_ok) {
            struct tegu_probe_gate context={gate,semaphoreWait};
            async(target,&context,tegu_probe_gate_wait);
            ((void (*)(object,selector,void *))send)(queue,sel("setSubmissionQueue:"),target);
            object command=get(queue,sel("commandBuffer"));
            release(command,sel("retain"));
            get(command,sel("computeCommandEncoder")); /* Deliberately unfinished. */
            release(command,sel("commit"));
            usize pending=((usize (*)(object,selector))send)(command,sel("status"));
            semaphoreSignal(gate);
            release(command,sel("waitUntilCompleted"));
            usize finished=((usize (*)(object,selector))send)(command,sel("status"));
            /* Flush target before stack gate context goes out of scope, even if
             * command wait was broken. Unfinished encoding deliberately reports error. */
            sync(target,0,tegu_probe_barrier);
            async_ok=command&&pending==2&&finished==5&&get(command,sel("error"));
            release(command,sel("commit"));
            if(((usize (*)(object,selector))send)(command,sel("status"))!=5)async_ok=0;
            release(command,sel("release"));
            ((void (*)(object,selector,void *))send)(queue,sel("setSubmissionQueue:"),0);
        }
        if(gate)dispatchRelease(gate);
        if(target)dispatchRelease(target);
    }
    journal("TEGU_RESULT system_completion_begin",-1);
    int completion_ok=0;
    if(async_ok) {
        void *target=queueCreate("org.tegu.probe.completion",0),*gate=semaphoreCreate(0);
        if(target&&gate) {
            unsigned calls=0;
            struct tegu_probe_gate context={gate,semaphoreWait};
            async(target,&context,tegu_probe_gate_wait);
            ((void (*)(object,selector,void *))send)(queue,sel("setCompletionQueue:"),target);
            object command=get(queue,sel("commandBuffer"));
            release(command,sel("retain"));
            tegu_probe_completed(send,sel("addCompletedHandler:"),command,&calls);
            release(command,sel("commit"));
            /* Empty command succeeds inline, but callbacks stay on their target. */
            completion_ok=command&&((usize (*)(object,selector))send)(command,sel("status"))==4&&!get(command,sel("error"))&&
                __atomic_load_n(&calls,__ATOMIC_SEQ_CST)==0;
            ((void (*)(object,selector,void *))send)(queue,sel("setCompletionQueue:"),0);
            semaphoreSignal(gate);
            release(command,sel("waitUntilCompleted"));
            completion_ok=completion_ok&&__atomic_load_n(&calls,__ATOMIC_SEQ_CST)==2;
            release(command,sel("commit"));release(command,sel("waitUntilCompleted"));
            completion_ok=completion_ok&&__atomic_load_n(&calls,__ATOMIC_SEQ_CST)==2;
            sync(target,0,tegu_probe_barrier);
            release(command,sel("release"));
        }
        if(gate)dispatchRelease(gate);
        if(target)dispatchRelease(target);
    }
    journal("TEGU_RESULT system_completion",completion_ok?0:1);
    async_ok=async_ok&&completion_ok;
    journal("TEGU_RESULT system_async",async_ok?0:1);
    if(!async_ok) {release(queue,sel("release"));release(device,sel("release"));pop(pool);journal("TEGU_RESULT complete",1);return 1;}
#endif
    object buffer=((object (*)(object,selector,usize,usize))send)
        (device,sel("newBufferWithLength:options:"),4096,0);
    int result=3;
    if (queue && buffer) {
        usize length=((usize (*)(object,selector))send)(buffer,sel("length"));
        volatile unsigned *words=(volatile unsigned *)get(buffer,sel("contents"));
        if (length>=4096 && words) {
            for (unsigned i=0;i<1024;i++) words[i]=0x54454755u^i;
            result=0;
            for (unsigned i=0;i<1024;i++) if(words[i]!=(0x54454755u^i)) result=4;
        }
        printf("TEGU_METAL buffer_length %lu cpu_storage_result %d\n",length,result);
    } else printf("TEGU_METAL object_creation_failed queue=%d buffer=%d\n",queue!=0,buffer!=0);
    release(buffer,sel("release"));
    release(queue,sel("release"));
    release(device,sel("release"));
    pop(pool);
    if(transport) result=1;
    journal("TEGU_RESULT complete",result?1:0);
#ifdef TEGU_RENDER_PROBE
    printf("TEGU_METAL complete %d render_gpu_execution_tested=1\n",result);
#else
    printf("TEGU_METAL complete %d gpu_execution_tested=0\n",result);
#endif
    return result;
}
