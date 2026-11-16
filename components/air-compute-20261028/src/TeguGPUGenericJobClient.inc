/* Selector 19 (TGJ1) and selector 20 (TGJ2, 0204): userspace-built render
 * job.  The client resolves owned allocations, maps them into the per-job v7
 * backing, copies the scratch image, screens the command stream and submits
 * it.  It does not interpret draw state; tegu_generic_job_plan() and
 * tegu_generic_job2_plan() define every GPU VA.  TGJ1 is checked against its
 * own limits and then runs the same path as a TGJ2 request. */
#include "generic_job.h"
#include "generic_job.c"

static uint32_t tegu_generic_job_diagnostic_rows;
extern "C" int tegu_gpu_render_fullscreen_timed(const struct tegu_full_render_job *,uint32_t,uint32_t *,uint64_t[7]);
static_assert(TEGU_GENERIC_JOB_MAX_COMMANDS<=TEGU_FS_MAX_JOB_COMMANDS&&
    TEGU_GENERIC_JOB2_MAX_COMMANDS<=TEGU_FS_MAX_JOB_COMMANDS,"tg_submit takes every TGJ stream");
static_assert(TEGU_GENERIC_JOB_MAX_SPANS<=TEGU_GENERIC_JOB2_MAX_SPANS,"TGJ1 fits TGJ2");
/* Per-call state off the kernel stack: the request (its commands must
 * outlive submission) and the resolved spans. */
struct tegu_generic_job_work {
    struct tegu_generic_job2_request request;
    struct tegu_fs_resource_span spans[TEGU_GENERIC_JOB2_MAX_SPANS];
    struct tegu_generic_job_plan plan;
};
/* 0194: per-job wall-time breakdown (mach_absolute_time marks).  The first
 * 32 jobs and every job over 50 ms are logged, at most 512 lines. */
enum { TGJ_T_ENTRY, TGJ_T_PREP, TGJ_T_ALLOC, TGJ_T_FLUSH_RES, TGJ_T_FLUSH_PRE, TGJ_T_SUBMIT,
       TGJ_T_FLUSH_POST, TGJ_T_SUMMARY, TGJ_T_END, TGJ_T_COUNT };
static uint32_t tegu_generic_job_time_jobs, tegu_generic_job_time_rows;
static uint64_t tegu_generic_job_us(uint64_t from,uint64_t to)
{
    uint64_t ns=0;
    if(!from||!to||to<from)return 0;
    absolutetime_to_nanoseconds(to-from,&ns);
    return ns/1000u;
}
static void tegu_generic_job_time_log(unsigned abi,const struct tegu_generic_job2_request *r,const uint64_t t[TGJ_T_COUNT],
    const uint64_t st[7],uint64_t spanFlush,uint32_t targetPages,uint32_t resourcePages,uint64_t serial,IOReturn result,
    uint64_t heapUsed)
{
    uint32_t n=OSIncrementAtomic((volatile SInt32 *)&tegu_generic_job_time_jobs);
    uint64_t total=tegu_generic_job_us(t[TGJ_T_ENTRY],t[TGJ_T_END]);
    if((n>=32&&total<50000)||OSIncrementAtomic((volatile SInt32 *)&tegu_generic_job_time_rows)>=512)return;
    uint64_t spanUs=0;absolutetime_to_nanoseconds(spanFlush,&spanUs);spanUs/=1000u;
    /* 0203: two lines; one ~330-byte line lost its tail (flush_target_post,
     * summary, teardown) to the 256-byte log record limit (R190/R191). */
    IOLog("TEGU TGJ%u TIME: job %u serial %llu rc 0x%x target %ux%u row %u pages %u res_pages %u spans %u flags 0x%x cmds %u "
        "total %llu prep %llu span_flush %llu alloc %llu flush_res %llu flush_target_pre %llu us\n",
        abi,n,(unsigned long long)serial,(uint32_t)result,r->width,r->height,r->row_stride,targetPages,resourcePages,r->span_count,
        r->flags,r->command_count,(unsigned long long)total,
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_ENTRY],t[TGJ_T_PREP]),(unsigned long long)spanUs,
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_PREP],t[TGJ_T_ALLOC]),
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_ALLOC],t[TGJ_T_FLUSH_RES]),
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_FLUSH_RES],t[TGJ_T_FLUSH_PRE]));
    /* 0204: heap_kb = tiler heap bytes the job used / 1024 (-1 unknown). */
    IOLog("TEGU TGJ%u TIME2: job %u lock %llu idle %llu map %llu codecheck %llu gpu %llu unmap %llu submit %llu "
        "flush_target_post %llu summary %llu teardown %llu us heap_kb %lld\n",abi,n,
        (unsigned long long)tegu_generic_job_us(st[0],st[1]),(unsigned long long)tegu_generic_job_us(st[1],st[2]),
        (unsigned long long)tegu_generic_job_us(st[2],st[3]),(unsigned long long)tegu_generic_job_us(st[3],st[4]),
        (unsigned long long)tegu_generic_job_us(st[4],st[5]),(unsigned long long)tegu_generic_job_us(st[5],st[6]),
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_FLUSH_PRE],t[TGJ_T_SUBMIT]),
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_SUBMIT],t[TGJ_T_FLUSH_POST]),
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_FLUSH_POST],t[TGJ_T_SUMMARY]),
        (unsigned long long)tegu_generic_job_us(t[TGJ_T_SUMMARY],t[TGJ_T_END]),
        heapUsed==UINT64_MAX?-1LL:(long long)(heapUsed>>10));
}
static uint32_t tegu_generic_job_target_summaries;
static void tegu_generic_job_diagnostic(unsigned abi,uint32_t stage,int result,const struct tegu_generic_job2_request *r)
{
    if(tegu_generic_job_diagnostic_rows++>=64)return;
    IOLog("TEGU TGJ%u: stage %u result 0x%x target %llu size %ux%u row %u spans %u commands %u\n",
       abi,stage,(uint32_t)result,r?(unsigned long long)r->target_token:0ULL,r?r->width:0,r?r->height:0,
       r?r->row_stride:0,r?r->span_count:0,r?r->command_count:0);
}

/* 0204: tiler heap bytes a completed job used, read back from the job's own
 * scratch copy: the tiler context at scratch 0x1000 names its heap
 * descriptor at 0x1080 (the generic builders' layout; anything else reads as
 * unknown).  bottom grows up from base+64, top down from base+size.  Only
 * reported (scalar output 2 of TGJ2, TIME2 heap_kb); nothing depends on it. */
static uint64_t tegu_generic_job_heap_used(uint8_t *scratch,uint64_t heapVA,uint32_t heapBytes)
{
    if(!scratch||!heapVA||!heapBytes)return UINT64_MAX;
    FlushPoC_DcacheRegion((vm_offset_t)scratch+FS_PAGE_SIZE,FS_PAGE_SIZE);
    const volatile uint8_t *t=scratch+FS_PAGE_SIZE;
    auto get64=[&](unsigned o)->uint64_t{uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)t[o+i]<<(8*i);return v;};
    uint64_t size=get64(0x80)>>32,base=get64(0x88),bottom=get64(0x90),top=get64(0x98);
    if(get64(24)!=FS_APERTURE_VA+FS_PAGE_SIZE+0x80||size!=heapBytes||base!=heapVA||
       bottom<base+64||top>base+size||bottom>top)return UINT64_MAX;
    return (bottom-(base+64))+(base+size-top);
}

IOReturn TeguGPUUserClient::renderGenericJob(IOExternalMethodArguments *a)
{
    if(!a||!a->structureInput||a->structureInputSize!=sizeof(struct tegu_generic_job_request)||
       !a->scalarOutput||a->scalarOutputCount!=2)return kIOReturnBadArgument;
    struct tegu_generic_job_work *work=(struct tegu_generic_job_work *)IOMallocZero(sizeof(*work));
    if(!work)return kIOReturnNoMemory;
    /* TGJ1 keeps its own limits (8 spans, 160 commands); then the same
     * request as TGJ2 (identical header, VAs and refusals). */
    struct tegu_generic_job_request *v1=(struct tegu_generic_job_request *)IOMallocZero(sizeof(*v1));
    if(!v1){IOFree(work,sizeof(*work));return kIOReturnNoMemory;}
    memcpy(v1,a->structureInput,sizeof(*v1));
    struct tegu_generic_job2_request *r=&work->request;
    int planned=tegu_generic_job_plan(v1,&work->plan);
    r->version=v1->version;r->flags=v1->flags;r->width=v1->width;r->height=v1->height;r->row_stride=v1->row_stride;
    r->span_count=v1->span_count<=TEGU_GENERIC_JOB_MAX_SPANS?v1->span_count:0;
    r->command_count=v1->command_count<=TEGU_GENERIC_JOB_MAX_COMMANDS?v1->command_count:0;
    r->reserved=v1->reserved;r->target_token=v1->target_token;r->target_offset=v1->target_offset;
    r->target_length=v1->target_length;r->target_bytes=v1->target_bytes;r->scratch_token=v1->scratch_token;
    r->scratch_offset=v1->scratch_offset;r->exec_mask=v1->exec_mask;
    memcpy(r->spans,v1->spans,r->span_count*sizeof(r->spans[0]));
    memcpy(r->commands,v1->commands,r->command_count*sizeof(r->commands[0]));
    IOFree(v1,sizeof(*v1));
    if(!planned){r->version=TEGU_GENERIC_JOB2_VERSION;struct tegu_generic_job_plan check={};
        planned=tegu_generic_job2_plan(r,&check)||memcmp(&check,&work->plan,sizeof(check));}
    return renderGenericJobCore(a,work,1,planned==0);
}

IOReturn TeguGPUUserClient::renderGenericJob2(IOExternalMethodArguments *a)
{
    if(!a||!a->structureInput||a->structureInputSize!=sizeof(struct tegu_generic_job2_request)||
       !a->scalarOutput||a->scalarOutputCount!=TEGU_GENERIC_JOB2_OUTPUTS)return kIOReturnBadArgument;
    struct tegu_generic_job_work *work=(struct tegu_generic_job_work *)IOMallocZero(sizeof(*work));
    if(!work)return kIOReturnNoMemory;
    memcpy(&work->request,a->structureInput,sizeof(work->request));
    return renderGenericJobCore(a,work,2,tegu_generic_job2_plan(&work->request,&work->plan)==0);
}

/* Owns work.  planned: the request passed its ABI's plan into work->plan. */
IOReturn TeguGPUUserClient::renderGenericJobCore(IOExternalMethodArguments *a,struct tegu_generic_job_work *work,
    unsigned abi,bool planned)
{
    if(!nextSerial||nextSerial>UINT32_MAX){IOFree(work,sizeof(*work));return kIOReturnNoResources;}
    uint64_t t[TGJ_T_COUNT]={},st[7]={},spanFlush=0;uint32_t resourcePages=0;
    t[TGJ_T_ENTRY]=mach_absolute_time();
    struct tegu_generic_job2_request *request=&work->request;
    /* A refused job inside a batched chain (TARGET_CLEAN: an earlier job of
     * the command buffer skipped its flush and display notify for this
     * target) must still end the chain, or the frame never reaches the panel
     * (R140).  chainCPU is set once the target is resolved. */
    void *chainCPU=NULL;uint64_t chainOffset=0;uint32_t chainPages=0;
    auto refuse=[&](uint32_t stage,IOReturn r)->IOReturn{
        if(chainCPU&&(request->flags&TEGU_GENERIC_JOB_TARGET_CLEAN)){
            for(uint32_t p=0;p<chainPages;p++)FlushPoC_DcacheRegion((vm_offset_t)chainCPU+chainOffset+(uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);
            sweet_fb_note_gpu_write();
        }
        tegu_generic_job_diagnostic(abi,stage,r,request);IOFree(work,sizeof(*work));return r;};
    const struct tegu_generic_job_plan &plan=work->plan;
    if(!planned)return refuse(1,kIOReturnBadArgument);
    auto getBuffer=[&](uint64_t handle)->TeguGPUBuffer *{
        for(unsigned i=0;i<tgSlots;i++)if(slots[i].buffer&&slots[i].handle==(uint32_t)handle&&handle<=UINT32_MAX)
            return slots[i].buffer;
        return NULL;
    };

    TeguGPUBuffer *target=getBuffer(request->target_token);
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    TeguGPUImportedTarget *surfaceTarget=NULL;
    for(unsigned i=0;i<tgImportedTargetSlots;i++)if(importedTargets[i].surface&&
       importedTargets[i].handle==request->target_token)surfaceTarget=&importedTargets[i];
    if(target&&surfaceTarget)return refuse(2,kIOReturnBadArgument);
    if(surfaceTarget&&(surfaceTarget->metadataVersion!=TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION||
       surfaceTarget->metalFormat!=80||!surfaceTarget->backing||!surfaceTarget->surface))
        return refuse(2,kIOReturnUnsupported);
    IOMemoryDescriptor *targetBacking=surfaceTarget?surfaceTarget->backing:(IOMemoryDescriptor *)target;
    void *targetCPU=surfaceTarget?(void *)surfaceTarget->surface->kernelAddress():(target?target->getBytesNoCopy():NULL);
    uint64_t targetLength=surfaceTarget?surfaceTarget->backing->getLength():(target?target->logicalLength():0);
#else
    IOMemoryDescriptor *targetBacking=(IOMemoryDescriptor *)target;void *targetCPU=target?target->getBytesNoCopy():NULL;
    uint64_t targetLength=target?target->logicalLength():0;
#endif
    if(!targetBacking||!targetCPU||request->target_length!=targetLength)return refuse(3,kIOReturnBadArgument);
    chainCPU=targetCPU;chainOffset=request->target_offset;chainPages=plan.layout.target_pages;
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    if(surfaceTarget){
        const struct sweet_surface_gpu_layout *m=&surfaceTarget->layout;
        if(request->width!=m->width||request->height!=m->height||request->row_stride!=m->bytesPerRow||
           request->target_offset!=m->offset||m->pixelFormat!=TEGU_IOSURFACE_PIXELFORMAT_BGRA||m->allocSize!=targetLength)
            return refuse(3,kIOReturnUnsupported);
    }
#endif

    TeguGPUBuffer *source=getBuffer(request->scratch_token);
    if(!source||source==target||request->scratch_offset>source->logicalLength()||
       TEGU_GENERIC_JOB_SCRATCH_BYTES>source->logicalLength()-request->scratch_offset||!source->getBytesNoCopy())
        return refuse(4,kIOReturnBadArgument);
    struct tegu_fs_resource_span *spans=work->spans;
    for(uint32_t i=0;i<request->span_count;i++){
        const struct tegu_generic_job_span *s=&request->spans[i];
        TeguGPUBuffer *buffer=getBuffer(s->token);
#if TEGU_GPU_COMPOSITOR_IOSURFACE
        if(!buffer){
            /* An imported IOSurface sampled by the job (selector-17 handle):
             * clean the CPU's view of the span before the GPU reads it. */
            TeguGPUImportedTarget *surface=NULL;
            for(unsigned k=0;k<tgImportedTargetSlots;k++)if(importedTargets[k].surface&&
               importedTargets[k].handle==s->token)surface=&importedTargets[k];
            if(!surface||!surface->backing||!surface->gpuLease||!surface->surface||
               s->offset>surface->backing->getLength()||s->length>surface->backing->getLength()-s->offset)
                return refuse(5,kIOReturnNotFound);
            {uint64_t f0=mach_absolute_time();
            FlushPoC_DcacheRegion((vm_offset_t)surface->surface->kernelAddress()+s->offset,s->length);
            spanFlush+=mach_absolute_time()-f0;}
            spans[i]=(struct tegu_fs_resource_span){NULL,s->offset,(uint32_t)s->length,
                surface->backing,surface->gpuLease,tegu_fullscreen_surface_page_resolve};
            continue;
        }
#endif
        if(!buffer||buffer==target||s->offset>buffer->logicalLength()||s->length>buffer->logicalLength()-s->offset)
            return refuse(5,kIOReturnNotFound);
        spans[i]=(struct tegu_fs_resource_span){buffer,s->offset,(uint32_t)s->length,NULL,NULL,NULL};
    }

    struct tegu_fs_queue_context queue={};
    if(tegu_gpu_fullscreen_context(&queue)||!queue.l2_entries||queue.l2_page_pa!=TEGU_FS_QUEUE_L2_PA||
       queue.protected_begin>=queue.protected_end)return refuse(6,kIOReturnNotReady);
    struct tegu_fs_reserved_range excluded[2]={{queue.protected_begin,queue.protected_end},
       {queue.l2_page_pa,queue.l2_page_pa+FS_PAGE_SIZE}};struct tegu_fs_reserved_context reserved={excluded,2};
    t[TGJ_T_PREP]=mach_absolute_time();
    /* 0203: no bzero; the memcpy below overwrites all of it. */
    TeguGPUBuffer *scratch=TeguGPUBuffer::allocateRecycled(TEGU_GENERIC_JOB_SCRATCH_BYTES,false);
    if(!scratch)return refuse(7,kIOReturnNoMemory);
    scratch->setLogicalLength(TEGU_GENERIC_JOB_SCRATCH_BYTES);
    uint8_t *memory=(uint8_t *)scratch->getBytesNoCopy();
    if(!memory){scratch->recycle();return refuse(7,kIOReturnNoMemory);}
    memcpy(memory,(const uint8_t *)source->getBytesNoCopy()+request->scratch_offset,TEGU_GENERIC_JOB_SCRATCH_BYTES);
    const bool heap2m=(request->flags&TEGU_GENERIC_JOB_HEAP2M)!=0;
    struct tegu_fs_surface_layout layout=plan.layout;struct tegu_fullscreen_backing *backing=NULL;
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    if(surfaceTarget){auto create=heap2m?tegu_fullscreen_backing_create_surface_v7_heap2m:tegu_fullscreen_backing_create_surface_v7;
        backing=create(scratch,surfaceTarget->backing,surfaceTarget->gpuLease,tegu_fullscreen_surface_page_resolve,
          request->target_offset,&layout,spans,request->span_count,request->exec_mask,queue.l2_entries,queue.l2_page_pa,&reserved);
    }else
#endif
    {auto create=heap2m?tegu_fullscreen_backing_create_v7_heap2m:tegu_fullscreen_backing_create_v7;
     backing=create(scratch,target,request->target_offset,&layout,spans,request->span_count,request->exec_mask,
       queue.l2_entries,queue.l2_page_pa,&reserved);}
    if(!backing){scratch->recycle();return refuse(8,kIOReturnNoMemory);}
    struct tegu_full_render_job *job=tegu_fullscreen_backing_job(backing);
    /* Userspace encoded against plan VAs; the backing must agree exactly. */
    bool agree=job&&job->target_gpu_va==plan.target_va&&job->heap_gpu_va==plan.heap_va;
    for(uint32_t i=0;agree&&i<request->span_count;i++){uint64_t va=0;
        agree=tegu_fullscreen_backing_resolve_resource(backing,i,request->spans[i].offset,
           request->spans[i].length,&va)&&va==plan.span_va[i];}
    if(!agree||(heap2m&&!(job->timeout_ticks=tegu_gpu_fullscreen_timeout_ticks()))){
        tegu_fullscreen_backing_finish(backing,1);scratch->recycle();return refuse(9,kIOReturnBadArgument);}
    t[TGJ_T_ALLOC]=mach_absolute_time();
    job->commands=request->commands;job->command_count=request->command_count;
    job->width=request->width;job->height=request->height;job->row_stride=request->row_stride;
    for(size_t i=0;i<tegu_fullscreen_backing_resource_count(backing);i++){
        const struct tegu_fs_resource_binding *r=tegu_fullscreen_backing_resource(backing,i);
        if(!r){tegu_fullscreen_backing_finish(backing,1);scratch->recycle();return refuse(10,kIOReturnBadArgument);}
        resourcePages+=r->page_count;
        if(!r->buffer)continue; /* imported-surface span: cleaned when resolved above */
        /* 0203: one call over the span's pages (was one dmb+dsb per page). */
        FlushPoC_DcacheRegion((vm_offset_t)r->buffer->getBytesNoCopy()+
           (r->buffer_offset&~UINT64_C(4095)),(size_t)r->page_count*FS_PAGE_SIZE);
    }
    for(size_t i=0;i<tegu_fullscreen_backing_heap_buffer_count(backing);i++){
        TeguGPUBuffer *heap=tegu_fullscreen_backing_heap_buffer(backing,i);
        if(!heap||!heap->getBytesNoCopy()){tegu_fullscreen_backing_finish(backing,1);scratch->recycle();return refuse(11,kIOReturnBadArgument);}
        FlushPoC_DcacheRegion((vm_offset_t)heap->getBytesNoCopy(),4u*FS_PAGE_SIZE);
    }
    FlushPoC_DcacheRegion((vm_offset_t)memory,TEGU_GENERIC_JOB_SCRATCH_BYTES);
    t[TGJ_T_FLUSH_RES]=mach_absolute_time();
    /* 0203: same bytes as the former per-page loop in one call (one dmb/dsb
     * instead of one per 4 KiB page; the target is virtually contiguous). */
    if(!(request->flags&TEGU_GENERIC_JOB_TARGET_CLEAN))
        FlushPoC_DcacheRegion((vm_offset_t)targetCPU+request->target_offset,(size_t)layout.target_pages*FS_PAGE_SIZE);
    t[TGJ_T_FLUSH_PRE]=mach_absolute_time();
    uint32_t fence=0;int status=tegu_gpu_render_fullscreen_timed(job,(uint32_t)nextSerial,&fence,st);
    t[TGJ_T_SUBMIT]=mach_absolute_time();
    struct fs_mapping *mapping=tegu_fullscreen_backing_mapping(backing);
    bool complete=status==0&&mapping&&!mapping->active&&mapping->disposition==FS_RELEASED;
    bool safe=mapping&&!mapping->active&&(mapping->disposition==FS_RELEASE_STAGED||mapping->disposition==FS_RELEASED);
    /* A job that did not complete ends its chain too. */
    if(!complete||!(request->flags&TEGU_GENERIC_JOB_TARGET_CONTINUES)){for(uint32_t p=0;p<layout.target_pages;p++)FlushPoC_DcacheRegion((vm_offset_t)targetCPU+request->target_offset+(uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);sweet_fb_note_gpu_write();}
    t[TGJ_T_FLUSH_POST]=mach_absolute_time();
    uint64_t heapUsed=complete&&heap2m?tegu_generic_job_heap_used(memory,job->heap_gpu_va,job->heap_bytes):UINT64_MAX;
    /* Bounded R105-comparable summary: same FNV-1a hash over the mapped
     * target bytes and per-channel nonzero counts over visible pixels. */
    if(complete&&tegu_generic_job_target_summaries<8){
        uint32_t sample=++tegu_generic_job_target_summaries;
        const uint8_t *bytes=(const uint8_t *)targetCPU+request->target_offset;
        uint64_t nonzero=0,hash=UINT64_C(1469598103934665603),channels[4]={};
        for(uint64_t i=0;i<layout.target_bytes;i++){uint8_t v=bytes[i];hash=(hash^v)*UINT64_C(1099511628211);if(v)nonzero++;}
        for(uint32_t y=0;y<request->height;y++)for(uint32_t x=0;x<request->width;x++){
            const uint8_t *pixel=bytes+(uint64_t)y*request->row_stride+(uint64_t)x*4;
            for(unsigned ch=0;ch<4;ch++)if(pixel[ch])channels[ch]++;}
        IOLog("TEGU TGJ%u TARGET: sample %u serial %llu target %llu bytes %llu nonzero %llu hash 0x%llx bgra %llu/%llu/%llu/%llu commands %u spans %u\n",
           abi,sample,(unsigned long long)nextSerial,(unsigned long long)request->target_token,(unsigned long long)layout.target_bytes,
           (unsigned long long)nonzero,(unsigned long long)hash,(unsigned long long)channels[0],(unsigned long long)channels[1],
           (unsigned long long)channels[2],(unsigned long long)channels[3],request->command_count,request->span_count);
    }
    t[TGJ_T_SUMMARY]=mach_absolute_time();
    tegu_fullscreen_backing_finish(backing,safe);backing=NULL;
    IOReturn result;
    if(status)result=status==-41?kIOReturnNotReady:status==-40?kIOReturnBadArgument:kIOReturnIOError;
    else if(!complete)result=kIOReturnIOError;else{a->scalarOutput[0]=nextSerial++;a->scalarOutput[1]=fence;result=kIOReturnSuccess;}
    if(result!=kIOReturnSuccess)tegu_generic_job_diagnostic(abi,12,result,request);
    if(abi==2&&result==kIOReturnSuccess)a->scalarOutput[2]=heapUsed;
    uint64_t serial=result==kIOReturnSuccess?a->scalarOutput[0]:0;
    scratch->recycle();t[TGJ_T_END]=mach_absolute_time();
    tegu_generic_job_time_log(abi,request,t,st,spanFlush,layout.target_pages,resourcePages,serial,result,heapUsed);
    IOFree(work,sizeof(*work));return result;
}
