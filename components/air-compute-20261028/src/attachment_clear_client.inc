/* Selector 18: the exact TAC1 shader/resource class with parameterized RT1.
 * externalMethod holds the client lock for resolution. The fullscreen backing
 * owns every GPU-visible page until completion or quarantine. */
#include "attachment_clear_request.h"
#include "attachment_clear_rt1_state.h"
#include "attachment_clear_rt1_state.c"

struct tegu_ac1_target_object {
 TeguGPUBuffer *buffer;
 TeguGPUImportedTarget *surface;
 IOMemoryDescriptor *backing;
 void *cpu;
 uint64_t length;
};

IOReturn TeguGPUUserClient::renderAttachmentClear(IOExternalMethodArguments *a)
{
    static_assert(sizeof(struct tegu_attachment_clear_request)==
        TEGU_ATTACHMENT_CLEAR_REQUEST_BYTES,"attachment-clear request ABI");
    if(!a||!a->scalarInput||!a->scalarOutput||!a->structureInput||
       a->scalarInputCount!=1||a->scalarOutputCount!=2||
       a->structureInputSize!=TEGU_ATTACHMENT_CLEAR_REQUEST_BYTES)
        return kIOReturnBadArgument;
    if(!tegu_gpu_compute_ready())return kIOReturnNotReady;
    if(!nextSerial||nextSerial>UINT32_MAX)return kIOReturnNoResources;

    RenderPipeline *pipeline=NULL;
    for(unsigned i=0;i<16;i++)if(renderPipelines[i].package&&
        renderPipelines[i].handle==a->scalarInput[0])pipeline=&renderPipelines[i];
    if(!pipeline)return kIOReturnNotFound;
    if(pipeline->kind!=TEGU_RENDER_PIPELINE_AC1||!pipeline->ac1||pipeline->info)
        return kIOReturnUnsupported;

    struct tegu_attachment_clear_request wire;
    memcpy(&wire,a->structureInput,sizeof(wire));
    if(wire.version!=TEGU_ATTACHMENT_CLEAR_REQUEST_VERSION||wire.width<16||
       wire.height<16||wire.width>TEGU_AC1_RT1_MAX_DIMENSION||
       wire.height>TEGU_AC1_RT1_MAX_DIMENSION||wire.sample_count!=1||
       wire.attachment_count!=TEGU_ATTACHMENT_CLEAR_TARGETS||
       wire.target_mask!=TEGU_ATTACHMENT_CLEAR_TARGET_MASK||
       wire.draw_write_mask!=TEGU_ATTACHMENT_CLEAR_TARGET_MASK||wire.draw_read_mask||
       wire.primitive_type!=3||wire.vertex_start||wire.vertex_count!=3||
       wire.instance_count!=1||wire.base_instance||wire.indexed||
       wire.reserved[0]||wire.reserved[1])return kIOReturnBadArgument;

    const uint32_t expected_mask[2]={TEGU_ATTACHMENT_CLEAR_RT0_MASK,
        TEGU_ATTACHMENT_CLEAR_RT1_MASK};
    struct tegu_ac1_target_object target[2]={};
    for(unsigned i=0;i<2;i++) {
        const struct tegu_attachment_clear_target_request *t=&wire.targets[i];
        uint64_t active=(uint64_t)t->row_stride*wire.height;
        if(t->format!=80||t->row_stride<wire.width*4u||(t->row_stride&63u)||
           !active||active>TEGU_AC1_RT1_MAX_TARGET_BYTES||
           t->memory.length!=((active+FS_PAGE_SIZE-1u)&~(uint64_t)(FS_PAGE_SIZE-1u))||!t->memory.handle||
           t->memory.handle>UINT32_MAX||(t->memory.offset&4095u)||
           t->write_mask!=expected_mask[i]||t->reserved[0]||t->reserved[1])
            return kIOReturnBadArgument;
        if(i==0) {
            if(!((t->load_action==TEGU_AC1_LOAD_DONTCARE&&
                  t->store_action==TEGU_AC1_STORE_DONTCARE)||
                 (t->load_action==TEGU_AC1_LOAD&&t->store_action==TEGU_AC1_STORE))||
               t->clear_rgba8)return kIOReturnUnsupported;
        } else if((t->load_action!=TEGU_AC1_LOAD_DONTCARE&&
                   t->load_action!=TEGU_AC1_LOAD_CLEAR)||
                  t->store_action!=TEGU_AC1_STORE||
                  (t->load_action==TEGU_AC1_LOAD_DONTCARE&&t->clear_rgba8))
            return kIOReturnUnsupported;
        for(unsigned j=0;j<tgSlots;j++)if(slots[j].buffer&&
            slots[j].handle==(uint32_t)t->memory.handle)target[i].buffer=slots[j].buffer;
        for(unsigned j=0;j<tgImportedTargetSlots;j++)if(importedTargets[j].surface&&
            importedTargets[j].handle==(uint32_t)t->memory.handle)target[i].surface=&importedTargets[j];
        if(!!target[i].buffer==!!target[i].surface)return kIOReturnNotFound;
        if(target[i].buffer) {
            if(t->memory.offset>target[i].buffer->logicalLength()||
               active>target[i].buffer->logicalLength()-t->memory.offset)
                return kIOReturnBadArgument;
            target[i].backing=target[i].buffer;
            target[i].cpu=target[i].buffer->getBytesNoCopy();
            target[i].length=target[i].buffer->getLength();
        } else {
#if TEGU_GPU_COMPOSITOR_IOSURFACE
            TeguGPUImportedTarget *s=target[i].surface;
            if(s->metadataVersion!=TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION||
               s->metalFormat!=80||!s->backing||!s->surface||!s->gpuLease||
               s->layout.width!=wire.width||s->layout.height!=wire.height||
               s->layout.bytesPerRow!=t->row_stride||s->layout.offset!=t->memory.offset)
                return kIOReturnUnsupported;
            target[i].backing=s->backing;target[i].cpu=(void *)s->surface->kernelAddress();
            target[i].length=s->backing->getLength();
#else
            return kIOReturnUnsupported;
#endif
        }
        uint64_t rounded=(active+FS_PAGE_SIZE-1u)&~(uint64_t)(FS_PAGE_SIZE-1u);
        if(!target[i].cpu||target[i].length&4095u||t->memory.offset>target[i].length||
           rounded>target[i].length-t->memory.offset)return kIOReturnBadArgument;
    }
    if(target[0].backing==target[1].backing)return kIOReturnBadArgument;

    TeguGPUBuffer *resource[2]={};
    const struct tegu_render_vertex_binding *resource_wire[2]={&wire.rect,&wire.matrix};
    const uint32_t resource_bytes[2]={TEGU_ATTACHMENT_CLEAR_RECT_BYTES,
        TEGU_ATTACHMENT_CLEAR_MATRIX_BYTES};
    for(unsigned i=0;i<2;i++) {
        const struct tegu_render_vertex_binding *r=resource_wire[i];
        if(!r->handle||r->handle>UINT32_MAX||r->length!=resource_bytes[i]||
           (r->offset&(i?15u:3u)))return kIOReturnBadArgument;
        for(unsigned j=0;j<tgSlots;j++)if(slots[j].buffer&&
            slots[j].handle==(uint32_t)r->handle)resource[i]=slots[j].buffer;
        if(!resource[i])return kIOReturnNotFound;
        if(r->offset>resource[i]->logicalLength()||
           r->length>resource[i]->logicalLength()-r->offset)return kIOReturnBadArgument;
        if((IOMemoryDescriptor *)resource[i]==target[0].backing||
           (IOMemoryDescriptor *)resource[i]==target[1].backing)return kIOReturnBadArgument;
    }
    if(resource[0]==resource[1])return kIOReturnBadArgument;

    uint64_t target_available=target[1].length-wire.targets[1].memory.offset;
    struct tegu_ac1_rt1_layout ac1_layout={TEGU_AC1_RT1_LAYOUT_VERSION,
        wire.width,wire.height,wire.targets[1].row_stride,target_available};
    struct tegu_ac1_rt1_map ac1_map={};
    if(tegu_ac1_rt1_map_build(&ac1_layout,&ac1_map))return kIOReturnBadArgument;
    struct tegu_fs_surface_layout layout={};
    if(tegu_fs_surface_layout_with_heap(wire.width,wire.height,
       wire.targets[1].row_stride,wire.targets[1].memory.offset,
       ac1_map.resource_pages,FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES,&layout)||
       layout.target_bytes!=ac1_map.target_bytes||
       layout.target_pages!=ac1_map.target_pages)return kIOReturnBadArgument;

    struct tegu_fs_queue_context queue={};
    if(tegu_gpu_fullscreen_context(&queue)||!queue.l2_entries||
       queue.l2_page_pa!=TEGU_FS_QUEUE_L2_PA||
       queue.protected_begin>=queue.protected_end)return kIOReturnNotReady;
    struct tegu_fs_reserved_range excluded[2]={{queue.protected_begin,queue.protected_end},
        {queue.l2_page_pa,queue.l2_page_pa+FS_PAGE_SIZE}};
    struct tegu_fs_reserved_context reserved={excluded,2};

    TeguGPUBuffer *scratch=TeguGPUBuffer::allocate(TEGU_AC1_RT1_SCRATCH_BYTES);
    TeguGPUBuffer *staging=TeguGPUBuffer::allocate(TEGU_AC1_RT1_RESOURCE_BYTES);
    if(!scratch||!staging){if(scratch)scratch->release();if(staging)staging->release();
        return kIOReturnNoMemory;}
    scratch->setLogicalLength(TEGU_AC1_RT1_SCRATCH_BYTES);
    staging->setLogicalLength(TEGU_AC1_RT1_RESOURCE_BYTES);
    struct tegu_ac1_rt1_commands commands={};struct tegu_ac1_rt1_map built_map={};
    int state_status=tegu_ac1_rt1_state_build(pipeline->ac1,
        pipeline->package->getBytesNoCopy(),TEGU_AC1_PACKAGE_BYTES,&ac1_layout,
        wire.targets[1].load_action==TEGU_AC1_LOAD_CLEAR?wire.targets[1].clear_rgba8:0,
        (const uint8_t *)resource[0]->getBytesNoCopy()+wire.rect.offset,wire.rect.length,
        (const uint8_t *)resource[1]->getBytesNoCopy()+wire.matrix.offset,wire.matrix.length,
        scratch->getBytesNoCopy(),TEGU_AC1_RT1_SCRATCH_BYTES,
        staging->getBytesNoCopy(),TEGU_AC1_RT1_RESOURCE_BYTES,&built_map,&commands);
    if(state_status||memcmp(&built_map,&ac1_map,sizeof(ac1_map))){
        staging->release();scratch->release();return kIOReturnBadArgument;}
    struct tegu_fs_resource_span span={staging,0,TEGU_AC1_RT1_RESOURCE_BYTES};
    struct tegu_fullscreen_backing *backing=NULL;
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    if(target[1].surface)backing=tegu_fullscreen_backing_create_surface_v7_heap2m(
        scratch,target[1].backing,target[1].surface->gpuLease,
        tegu_fullscreen_surface_page_resolve,wire.targets[1].memory.offset,&layout,&span,1,
        (UINT64_C(1)<<2)|(UINT64_C(1)<<48),queue.l2_entries,queue.l2_page_pa,&reserved);
    else
#endif
        backing=tegu_fullscreen_backing_create_v7_heap2m(scratch,target[1].buffer,
        wire.targets[1].memory.offset,&layout,&span,1,
        (UINT64_C(1)<<2)|(UINT64_C(1)<<48),queue.l2_entries,queue.l2_page_pa,&reserved);
    if(!backing){staging->release();scratch->release();return kIOReturnNoMemory;}
    struct tegu_full_render_job *job=tegu_fullscreen_backing_job(backing);
    const struct tegu_fs_resource_binding *mapped=tegu_fullscreen_backing_resource(backing,0);
    if(!job||!mapped||job->target_gpu_va!=built_map.target_va||
       mapped->gpu_va!=built_map.resource_va||job->heap_gpu_va!=built_map.heap_va||
       job->heap_bytes!=TEGU_AC1_RT1_HEAP_BYTES||
       job->target_page_count!=built_map.target_pages||
       job->request->resource_count!=built_map.resource_pages||
       job->request->heap_count!=built_map.heap_pages){
        tegu_fullscreen_backing_finish(backing,1);staging->release();scratch->release();
        return kIOReturnIOError;}
    uint64_t timeout_ticks=tegu_gpu_fullscreen_timeout_ticks();
    if(!timeout_ticks){tegu_fullscreen_backing_finish(backing,1);
        staging->release();scratch->release();return kIOReturnNotReady;}
    job->commands=commands.words;job->command_count=commands.count;
    job->width=wire.width;job->height=wire.height;
    job->row_stride=wire.targets[1].row_stride;job->kind=TEGU_FS_TRIANGLE;
    job->timeout_ticks=timeout_ticks;
    for(size_t i=0;i<tegu_fullscreen_backing_resource_count(backing);i++) {
        const struct tegu_fs_resource_binding *r=tegu_fullscreen_backing_resource(backing,i);
        if(!r||!r->buffer){tegu_fullscreen_backing_finish(backing,1);
            staging->release();scratch->release();return kIOReturnIOError;}
        for(uint32_t p=0;p<r->page_count;p++)FlushPoC_DcacheRegion(
            (vm_offset_t)r->buffer->getBytesNoCopy()+r->buffer_offset+
            (uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);
    }
    for(size_t i=0;i<tegu_fullscreen_backing_heap_buffer_count(backing);i++) {
        TeguGPUBuffer *heap=tegu_fullscreen_backing_heap_buffer(backing,i);
        if(!heap||!heap->getBytesNoCopy()){tegu_fullscreen_backing_finish(backing,1);
            staging->release();scratch->release();return kIOReturnIOError;}
        FlushPoC_DcacheRegion((vm_offset_t)heap->getBytesNoCopy(),heap->getLength());
    }
    FlushPoC_DcacheRegion((vm_offset_t)scratch->getBytesNoCopy(),TEGU_AC1_RT1_SCRATCH_BYTES);
    for(uint32_t p=0;p<layout.target_pages;p++)FlushPoC_DcacheRegion(
        (vm_offset_t)target[1].cpu+wire.targets[1].memory.offset+
        (uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);
    uint32_t fence=0;int status=tegu_gpu_render_fullscreen(job,(uint32_t)nextSerial,&fence);
    struct fs_mapping *mapping=tegu_fullscreen_backing_mapping(backing);
    bool complete=status==0&&mapping&&!mapping->active&&mapping->disposition==FS_RELEASED;
    bool safe=mapping&&!mapping->active&&
        (mapping->disposition==FS_RELEASE_STAGED||mapping->disposition==FS_RELEASED);
    if(complete)for(uint32_t p=0;p<layout.target_pages;p++)FlushPoC_DcacheRegion(
        (vm_offset_t)target[1].cpu+wire.targets[1].memory.offset+
        (uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);
    tegu_fullscreen_backing_finish(backing,safe);staging->release();scratch->release();
    if(status)return status==-41?kIOReturnNotReady:
        status==-40?kIOReturnBadArgument:kIOReturnIOError;
    if(!complete)return kIOReturnIOError;
    a->scalarOutput[0]=nextSerial++;a->scalarOutput[1]=fence;
    return kIOReturnSuccess;
}
