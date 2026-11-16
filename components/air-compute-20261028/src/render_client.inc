#ifndef TEGU_GPU_LOAD_ACTION
#define TEGU_GPU_LOAD_ACTION 0
#endif
#ifndef TEGU_GPU_RGBA16F_SOURCE
#define TEGU_GPU_RGBA16F_SOURCE 0
#endif
/* Called under the client mutex. Only owned handles and bounded byte ranges
 * cross the user boundary. Scratch also owns all transient CPU draw state. */
#include "render_target_request.h"
#include "render_load.h"
IOReturn TeguGPUUserClient::render(IOExternalMethodArguments *a,unsigned kind)
{
    const bool targetSized=kind==3;
    bool extended=kind!=0,sampled=kind==2;
    RenderPipeline *pipeline=NULL;TeguGPUBuffer *ownedTarget=NULL;
    TeguGPUImportedTarget *importedTarget=NULL;
    for(unsigned i=0;i<16;i++)if(renderPipelines[i].package&&renderPipelines[i].handle==a->scalarInput[0])pipeline=&renderPipelines[i];
    for(unsigned i=0;i<tgSlots;i++)if(slots[i].buffer&&slots[i].handle==a->scalarInput[1])ownedTarget=slots[i].buffer;
    if(!ownedTarget)for(unsigned i=0;i<tgImportedTargetSlots;i++)
        if(importedTargets[i].surface&&importedTargets[i].handle==a->scalarInput[1])importedTarget=&importedTargets[i];
    if(!pipeline||(!ownedTarget&&!importedTarget))return kIOReturnNotFound;
    if(pipeline->kind!=TEGU_RENDER_PIPELINE_TGR||!pipeline->info||pipeline->ac1)return kIOReturnUnsupported;
    if(!tegu_render_legacy_target_supported(pipeline->info)||pipeline->info->version==6)return kIOReturnUnsupported;
    // IOSurface import remains fixed at 16x16 until its row/plane contract is
    // separately versioned and validated for larger layouts.
    if(kind==3&&importedTarget)return kIOReturnUnsupported;
    if(!nextSerial)return kIOReturnNoResources;
    IOMemoryDescriptor *targetBacking=ownedTarget ? (IOMemoryDescriptor *)ownedTarget : importedTarget->backing;
    SweetSurface *targetSurface=importedTarget ? importedTarget->surface : NULL;
    void *targetCPU=ownedTarget?ownedTarget->getBytesNoCopy():
        (targetSurface?(void *)targetSurface->kernelAddress():NULL);
    if(!targetBacking||!targetCPU)return kIOReturnNotFound;
    uint64_t targetLength=targetBacking->getLength();
    uint32_t targetFormat=importedTarget ? importedTarget->metalFormat : pipeline->info->color_format;
    uint64_t offset=a->scalarInput[2];
    struct tegu_render_target_layout targetLayout={TEGU_RENDER_TARGET_LAYOUT_VERSION,16,16,64,1024};
    uint64_t surfaceBytes=1024;
    struct tegu_render_target_request targetRequest={};bool loadPass=false;
    struct tegu_render_vertex_request request={};
    static_assert(sizeof(request)==TEGU_RENDER_VERTEX_REQUEST_BYTES,"vertex request ABI");
    struct tegu_render_texture_request textureRequest={};
    static_assert(sizeof(textureRequest)==TEGU_RENDER_TEXTURE_REQUEST_BYTES,"texture request ABI");
    if(targetSized) {
        if(!a->structureInput||a->structureInputSize!=TEGU_RENDER_TARGET_REQUEST_BYTES)return kIOReturnBadArgument;
        memcpy(&targetRequest,a->structureInput,sizeof(targetRequest));
        if(targetRequest.version==TEGU_RENDER_TARGET_REQUEST_VERSION) {
#if !TEGU_GPU_TARGET_DIMENSIONS
            return kIOReturnUnsupported;
#else
            if(targetRequest.reserved[0]||targetRequest.reserved[1]||targetRequest.reserved[2])return kIOReturnBadArgument;
#endif
        } else if(targetRequest.version==TEGU_RENDER_TARGET_REQUEST_VERSION_LOAD) {
#if TEGU_GPU_LOAD_ACTION
#if !TEGU_GPU_TARGET_DIMENSIONS
            return kIOReturnUnsupported;
#else
            if(targetRequest.reserved[0]!=TEGU_RENDER_PASS_LOAD||targetRequest.reserved[1]||targetRequest.reserved[2])return kIOReturnBadArgument;
            if(targetRequest.width!=16||targetRequest.height!=16||targetRequest.row_stride!=64||targetFormat!=70||importedTarget||
               targetRequest.kind!=TEGU_RENDER_TARGET_REQUEST_NONE)return kIOReturnUnsupported;
            for(unsigned i=0;i<sizeof(targetRequest.payload);i++)if(targetRequest.payload[i])return kIOReturnBadArgument;
            loadPass=true;
#endif
#else
            return kIOReturnUnsupported;
#endif
        } else return kIOReturnBadArgument;
        if(targetRequest.kind>TEGU_RENDER_TARGET_REQUEST_TEXTURE)return kIOReturnBadArgument;
        targetLayout.version=TEGU_RENDER_TARGET_LAYOUT_VERSION;targetLayout.width=targetRequest.width;
        targetLayout.height=targetRequest.height;targetLayout.row_stride=targetRequest.row_stride;
        targetLayout.target_length=TEGU_RENDER_TARGET_MAX_BYTES;
        if(tegu_render_target_layout_validate(&targetLayout,&surfaceBytes))return kIOReturnBadArgument;
        targetLayout.target_length=surfaceBytes;
        extended=targetRequest.kind!=TEGU_RENDER_TARGET_REQUEST_NONE;
        sampled=targetRequest.kind==TEGU_RENDER_TARGET_REQUEST_TEXTURE;
        if(targetRequest.kind==TEGU_RENDER_TARGET_REQUEST_VERTEX) {
            memcpy(&request,targetRequest.payload,sizeof(request));
            for(unsigned i=TEGU_RENDER_VERTEX_REQUEST_BYTES;i<sizeof(targetRequest.payload);i++)
                if(targetRequest.payload[i])return kIOReturnBadArgument;
        } else if(sampled) {
            memcpy(&textureRequest,targetRequest.payload,sizeof(textureRequest));request=textureRequest.vertex;
        } else for(unsigned i=0;i<sizeof(targetRequest.payload);i++)
            if(targetRequest.payload[i])return kIOReturnBadArgument;
    } else if(!tegu_gpu_target_range_valid(targetLength,offset))return kIOReturnBadArgument;
    if(targetSized&&(offset>targetLength||surfaceBytes>targetLength-offset||(offset&63)||
       ((offset&4095)+surfaceBytes+4095)/4096>TEGU_RENDER_PAGES-TEGU_RENDER_BASE_PAGES))return kIOReturnBadArgument;
    if(a->scalarInput[3]>0xffffffffULL)
        return kIOReturnBadArgument;
    if(!tegu_gpu_target_format_matches(pipeline->info->color_format,targetFormat)||
       extended!=(pipeline->info->vertex_mask!=0)||
       sampled!=(pipeline->info->texture.version!=0))return kIOReturnUnsupported;
    TeguGPUBuffer *vertices[TEGU_VERTEX_SLOTS]={};
    uint64_t pageOffsets[TEGU_VERTEX_SLOTS]={},pageBytes[TEGU_VERTEX_SLOTS]={};
    struct tegu_vertex_buffer bindings[TEGU_VERTEX_SLOTS]={};
    TeguGPUBuffer *texture=NULL;
    uint64_t texturePage=0,textureVA=0,textureDescriptorVA=0;
    uint32_t textureFormat=70,textureStride=64,textureBytes=1024;
    uint32_t used=0;unsigned targetPages=targetSized?(unsigned)(((offset&4095)+surfaceBytes+4095)/4096):1;
    unsigned targetPageStart=64;unsigned pages=targetPageStart+targetPages;
    if(loadPass) {
        if(pages!=TEGU_LOAD_PAGE_START||TEGU_LOAD_PAGE_COUNT>TEGU_RENDER_PAGES-pages)return kIOReturnNoResources;
        pages+=TEGU_LOAD_PAGE_COUNT;
    }
    if(extended) {
        if(sampled) {
            if(!targetSized) {
            memcpy(&textureRequest,a->structureInput,sizeof(textureRequest));
            request=textureRequest.vertex;
            }
            const struct tegu_render_vertex_binding *b=&textureRequest.texture;
            if(textureRequest.version==1) {
                if(textureRequest.reserved[0]||textureRequest.reserved[1]||textureRequest.reserved[2])return kIOReturnBadArgument;
            } else if(textureRequest.version==2) {
#if TEGU_GPU_RGBA16F_SOURCE
                if(textureRequest.reserved[0]!=115||textureRequest.reserved[1]!=128||textureRequest.reserved[2]!=2048)
                    return kIOReturnBadArgument;
                if(targetFormat!=70)return kIOReturnUnsupported;
                textureFormat=115;textureStride=128;textureBytes=2048;
#else
                return kIOReturnUnsupported;
#endif
            } else return kIOReturnBadArgument;
            if(!b->handle||b->handle>0xffffffffULL||(b->offset&4095)||b->length!=textureBytes)
                return kIOReturnBadArgument;
            for(unsigned i=0;i<tgSlots;i++)if(slots[i].buffer&&slots[i].handle==b->handle)texture=slots[i].buffer;
            if(!texture)return kIOReturnNotFound;
            if(texture==ownedTarget||b->offset>texture->logicalLength()||b->length>texture->logicalLength()-b->offset)return kIOReturnBadArgument;
            texturePage=b->offset;
        } else if(!targetSized)memcpy(&request,a->structureInput,sizeof(request));
        for(unsigned i=0;i<pipeline->info->vertex_count;i++)used|=1u<<pipeline->info->vertex[i].buffer_index;
        // The existing qualified render shape remains one nonindexed triangle.
        // Instance/large-target expansion must be qualified with matching state.
        if(request.version!=1||request.buffer_mask!=used||request.reserved[0]||request.reserved[1]||
           request.draw.first_vertex||request.draw.vertex_count!=3||request.draw.instance_count!=1||request.draw.base_instance)return kIOReturnBadArgument;
        for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++) {
            const struct tegu_render_vertex_binding *b=&request.buffers[i];
            if(!(used&(1u<<i))) {if(b->handle||b->offset||b->length)return kIOReturnBadArgument;continue;}
            if(!b->handle||b->handle>0xffffffffULL||!b->length||b->length>0xffffffffULL||(b->offset&3))return kIOReturnBadArgument;
            for(unsigned j=0;j<tgSlots;j++)if(slots[j].buffer&&slots[j].handle==b->handle)vertices[i]=slots[j].buffer;
            if(!vertices[i])return kIOReturnNotFound;
            if(vertices[i]==ownedTarget||b->offset>vertices[i]->getLength()||b->length>vertices[i]->getLength()-b->offset)return kIOReturnBadArgument;
            uint64_t n=((b->offset&4095)+b->length+4095)/4096;
            if(n>TEGU_RENDER_PAGES-pages)return kIOReturnNoResources;
            pageOffsets[i]=b->offset&~4095ULL;pageBytes[i]=n*4096;
            bindings[i].address=TEGU_RENDER_SCRATCH_VA+(uint64_t)pages*4096+(b->offset&4095);
            bindings[i].length=b->length;pages+=(unsigned)n;
        }
    }
    if(sampled) {
        if(pages>TEGU_RENDER_PAGES-2)return kIOReturnNoResources;
        textureVA=TEGU_RENDER_SCRATCH_VA+(uint64_t)pages*4096;
        textureDescriptorVA=textureVA+4096;pages+=2;
    }
    TeguGPUBuffer *scratch=TeguGPUBuffer::allocate(TEGU_RENDER_SCRATCH_BYTES);
    if(!scratch)return kIOReturnNoMemory;
    static_assert(sizeof(struct tegu_render_load_state)<=TEGU_LOAD_STAGE_CAPACITY,
        "LOAD state exceeds owned staging interval");
    static_assert(TEGU_LOAD_STAGE_OFFSET==TEGU_LOAD_PAGE_COUNT*4096u,
        "LOAD staging must follow its three mapped GPU pages");
    size_t loadBackingBytes=loadPass?(size_t)round_page(TEGU_LOAD_STAGE_OFFSET+TEGU_LOAD_STAGE_CAPACITY):0;
    TeguGPUBuffer *loadBacking=loadPass?TeguGPUBuffer::allocate(loadBackingBytes):NULL;
    if(loadPass&&!loadBacking){scratch->release();return kIOReturnNoMemory;}
    TeguGPUBuffer *textureDescriptors=sampled?TeguGPUBuffer::allocate(page_size):NULL;
    if(sampled&&!textureDescriptors){if(loadBacking)loadBacking->release();scratch->release();return kIOReturnNoMemory;}
    unsigned char *memory=(unsigned char *)scratch->getBytesNoCopy();
    struct tegu_render_state *state=(struct tegu_render_state *)(memory+0x4000);
    struct tegu_render_job *job=(struct tegu_render_job *)(memory+0xc000);
    struct tegu_vertex_state *vertexState=(struct tegu_vertex_state *)(memory+0x8000);
    static_assert(sizeof(*state)<=0x2000,"render state overlaps vertex descriptors");
    static_assert(sizeof(*job)<=0x4000,"render job exceeds reserved scratch interval");
    static_assert(sizeof(*vertexState)<=0x2000,"vertex state overlaps texture state");
    struct tegu_texture_state *textureState=(struct tegu_texture_state *)(memory+0xa000);
    static_assert(sizeof(*textureState)<=0x2000,"texture state overlaps reserved scratch");
    job->page_count=pages;job->target_page_start=targetPageStart;job->target_page_count=targetPages;
    job->load_page_start=loadPass?TEGU_LOAD_PAGE_START:0;job->load_page_count=loadPass?TEGU_LOAD_PAGE_COUNT:0;
    job->readonly_load_mask=loadPass?3:0;job->executable_load_mask=loadPass?4:0;bool valid=true;
    for(unsigned i=0;i<64;i++)if(!scratch->gpuPage(i*4096,&job->pages[i]))valid=false;
    uint64_t targetPage=offset&~4095ULL;
    for(unsigned i=0;i<targetPages;i++)if(!(ownedTarget ? ownedTarget->gpuPage(targetPage+(uint64_t)i*4096,&job->pages[targetPageStart+i]) :
        tegu_surface_gpu_page(targetBacking,targetLength,targetPage+(uint64_t)i*4096,&job->pages[targetPageStart+i])))valid=false;
    unsigned next=targetPageStart+targetPages;
    if(loadPass)for(unsigned i=0;i<TEGU_LOAD_PAGE_COUNT;i++)
        if(!loadBacking->gpuPage((uint64_t)i*4096,&job->pages[TEGU_LOAD_PAGE_START+i]))valid=false;
    if(loadPass)next+=TEGU_LOAD_PAGE_COUNT;
    for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(vertices[i])
        for(uint64_t p=0;p<pageBytes[i];p+=4096)if(!vertices[i]->gpuPage(pageOffsets[i]+p,&job->pages[next++]))valid=false;
    if(sampled) {
        if(!texture->gpuPage(texturePage,&job->pages[next++]))valid=false;
        if(!textureDescriptors->gpuPage(0,&job->pages[next++]))valid=false;
    }
    struct tegu_render_addresses addresses={TEGU_RENDER_SCRATCH_VA,TEGU_RENDER_SCRATCH_VA+0x1000,
        TEGU_RENDER_SCRATCH_VA+0x10000,TEGU_RENDER_SCRATCH_VA+0x20000,
        TEGU_RENDER_TARGET_VA+(offset&4095),TEGU_RENDER_SCRATCH_VA+0x2000,
        TEGU_RENDER_SCRATCH_VA+0x3000,(uint32_t)a->scalarInput[3],0,0};
    uint64_t resourceTable=0;
    if(extended) {
        if(tegu_vertex_fetch_build(pipeline->info->vertex_mask,pipeline->info->vertex,pipeline->info->vertex_count,
            bindings,&request.draw,TEGU_RENDER_SCRATCH_VA+0x6000,vertexState))valid=false;
        else {memcpy(memory+0x6000,vertexState->descriptors,4096);resourceTable=vertexState->resource_table;}
    }
    uint64_t fragmentTable=0;
    if(sampled) {
        struct tegu_texture_input input={};
        input.version=1;input.format=70;input.width=16;input.height=16;
        input.levels=1;input.layers=1;input.row_stride=64;input.normalized=1;input.max_anisotropy=1;
        input.descriptor_address=textureDescriptorVA;input.texel_address=textureVA;input.texel_length=1024;
        input.format=textureFormat;input.row_stride=textureStride;input.texel_length=textureBytes;
        if(tegu_texture_state_build(&input,textureState))valid=false;
        else {memcpy(textureDescriptors->getBytesNoCopy(),textureState->descriptors,4096);fragmentTable=textureState->resource_table;}
    }
    const unsigned char *package=(const unsigned char *)pipeline->package->getBytesNoCopy();
    size_t size=pipeline->info->stages[1].code_offset+pipeline->info->stages[1].code_bytes;
    if(!valid||(targetSized?tegu_render_build_target_resources(package,size,&addresses,resourceTable,fragmentTable,&targetLayout,state):
       tegu_render_build_resources(package,size,&addresses,resourceTable,fragmentTable,state))) {
        if(textureDescriptors)textureDescriptors->release();
        if(loadBacking)loadBacking->release();
        scratch->release();return kIOReturnBadArgument;
    }
    unsigned char *loadBytes=loadPass?(unsigned char *)loadBacking->getBytesNoCopy():NULL;
    struct tegu_render_load_state *loadState=loadPass?
        (struct tegu_render_load_state *)(loadBytes+TEGU_LOAD_STAGE_OFFSET):NULL;
    if(loadPass&&tegu_render_build_load_pass(state,package,size,&addresses,&targetLayout,loadState)) {
        if(textureDescriptors)textureDescriptors->release();if(loadBacking)loadBacking->release();scratch->release();return kIOReturnBadArgument;
    }
    if(loadPass) {
        if(loadState->page_start!=job->load_page_start||loadState->page_count!=job->load_page_count) {
            if(textureDescriptors)textureDescriptors->release();loadBacking->release();scratch->release();return kIOReturnBadArgument;
        }
        job->readonly_load_mask=loadState->readonly_page_mask;
        job->executable_load_mask=loadState->executable_page_mask;
        memcpy(loadBytes,loadState->resources,4096);
        memcpy(loadBytes+4096,loadState->dcd,4096);
        memcpy(loadBytes+8192,loadState->shader_page,4096);
    }
    memcpy(memory,state->descriptors,4096);memcpy(memory+0x1000,state->tiler,256);
    for(unsigned i=0;i<2;i++)memcpy(memory+0x2000+i*4096,
        package+pipeline->info->stages[i].code_offset,pipeline->info->stages[i].code_bytes);
    job->commands=state->commands;job->command_count=state->command_count;
    if(texture)texture->retain();
    if(ownedTarget)ownedTarget->retain();
    else {targetSurface->retain();targetBacking->retain();}
    for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(vertices[i])vertices[i]->retain();
    for(unsigned i=0;i<64;i++)FlushPoC_DcacheRegion((vm_offset_t)memory+i*4096,4096);
    if(loadBacking)FlushPoC_DcacheRegion((vm_offset_t)loadBacking->getBytesNoCopy(),TEGU_LOAD_PAGE_COUNT*4096);
    for(unsigned i=0;i<targetPages;i++)FlushPoC_DcacheRegion((vm_offset_t)targetCPU+targetPage+(uint64_t)i*4096,4096);
    for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(vertices[i])for(uint64_t p=0;p<pageBytes[i];p+=4096)
        FlushPoC_DcacheRegion((vm_offset_t)vertices[i]->getBytesNoCopy()+pageOffsets[i]+p,4096);
    if(texture) {
        FlushPoC_DcacheRegion((vm_offset_t)texture->getBytesNoCopy()+texturePage,4096);
        FlushPoC_DcacheRegion((vm_offset_t)textureDescriptors->getBytesNoCopy(),4096);
    }
    uint32_t fence=0;int status=tegu_gpu_render(job,(uint32_t)nextSerial,&fence);
    // Never release anything the GPU might still access after unsafe shutdown.
    if(status!=-100) {
        for(unsigned i=0;i<64;i++)FlushPoC_DcacheRegion((vm_offset_t)memory+i*4096,4096);
        if(loadBacking)FlushPoC_DcacheRegion((vm_offset_t)loadBacking->getBytesNoCopy(),TEGU_LOAD_PAGE_COUNT*4096);
        for(unsigned i=0;i<targetPages;i++)FlushPoC_DcacheRegion((vm_offset_t)targetCPU+targetPage+(uint64_t)i*4096,4096);
        for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(vertices[i])vertices[i]->release();
        if(texture)texture->release();
        if(textureDescriptors)textureDescriptors->release();
        if(loadBacking)loadBacking->release();
        if(ownedTarget)ownedTarget->release();
        else {targetBacking->release();targetSurface->release();}
        scratch->release();
    }
    if(status)return status==-41?kIOReturnNotReady:kIOReturnIOError;
    a->scalarOutput[0]=nextSerial++;a->scalarOutput[1]=fence;return kIOReturnSuccess;
}
