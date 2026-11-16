/* Selector 16 implementation. The userclient lock is held by externalMethod.
 * This method owns every backing through map, submit, completion and unmap.
 */
#include "TeguGPUCompositorRequest.h"
#include "native_package_v6.h"
#include "native_package_v7.h"
#include "native_package_b3.h"
#include "compositor_v7_scratch_layout.h"
#include "compositor_v7_scratch_layout.c"
#include "compositor_vertex_state.h"
#include "compositor_resource_state.h"
#include "compositor_render_state_v6.h"
#include "compositor_load_bundle_v6.h"
#include "compositor_load_assets_v6.h"
#include "tegu_memory.h"
#include "native_codecheck_diag.h"
#include <machine/machine_routines.h>
#ifndef TEGU_GPU_COMPOSITOR_V7
#define TEGU_GPU_COMPOSITOR_V7 0
#endif
#ifndef TEGU_GPU_COMPOSITOR_IOSURFACE
#define TEGU_GPU_COMPOSITOR_IOSURFACE 0
#endif

#include "compositor_resource_state.c"
#include "compositor_vertex_state.c"
#include "compositor_render_state_v6.c"
#include "compositor_load_mapping.c"
#include "compositor_load_assets_v6.c"
#include "compositor_load_bundle_v6.c"

static int tegu_compositor_surface_page(IOMemoryDescriptor *backing,
    uint64_t offset,uint64_t *physical)
{
    return backing && tegu_surface_gpu_page(backing,backing->getLength(),offset,physical);
}

static int tegu_compositor_all_zero(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; ++i) if (b[i]) return 0;
    return 1;
}
static uint64_t tegu_compositor_page_count(uint64_t offset, uint64_t bytes)
{
    if (!bytes || bytes > UINT64_MAX - (offset & 4095u) - 4095u) return UINT64_MAX;
    return ((offset & 4095u) + bytes + 4095u) / 4096u;
}

/* Keep v7's distinct types and checks separate without aliasing C structs. */
static __attribute__((noinline)) int tegu_compositor_bind_v7(const void *package,
 size_t bytes,const tegu_native_v6_binding *input,tegu_native_v6_checked *output) {
 static_assert(sizeof(tegu_native_v7_binding)==sizeof(tegu_native_v6_binding),"binding POD layout");
 static_assert(sizeof(tegu_native_v7_checked)==sizeof(tegu_native_v6_checked),"checked POD layout");
 tegu_native_v7_binding in={};tegu_native_v7_checked out={};
 memcpy(&in,input,sizeof(in));
 if(tegu_native_v7_bind(package,bytes,&in,&out))return -1;
 memcpy(output,&out,sizeof(out));return 0;
}
static __attribute__((noinline)) int tegu_compositor_stage_v7(const void *package,
 size_t bytes,void *memory,size_t scratch_bytes,tegu_native_v6_layout *output) {
 tegu_native_v7_layout layout={};
 if(tegu_native_v7_stage_copy(package,bytes,memory,scratch_bytes,&layout))return -1;
 *output={layout.vs_offset,layout.vs_bytes,layout.fs_offset,layout.fs_bytes,layout.vs_rx,layout.fs_rx};
 return 0;
}
static __attribute__((noinline)) int tegu_compositor_bind_b3(const void *package,
 size_t bytes,const tegu_native_v6_binding *input,tegu_native_v6_checked *output) {
 static_assert(sizeof(tegu_native_b3_binding)==sizeof(tegu_native_v6_binding),"binding POD layout");
 static_assert(sizeof(tegu_native_b3_checked)==sizeof(tegu_native_v6_checked),"checked POD layout");
 tegu_native_b3_binding in={};tegu_native_b3_checked out={};
 memcpy(&in,input,sizeof(in));
 if(tegu_native_b3_bind(package,bytes,&in,&out))return -1;
 memcpy(output,&out,sizeof(out));return 0;
}
static __attribute__((noinline)) int tegu_compositor_stage_b3(const void *package,
 size_t bytes,void *memory,size_t scratch_bytes,tegu_native_v6_layout *output) {
 tegu_native_b3_layout layout={};
 if(tegu_native_b3_stage_copy(package,bytes,memory,scratch_bytes,&layout))return -1;
 *output={layout.vs_offset,layout.vs_bytes,layout.fs_offset,layout.fs_bytes,layout.vs_rx,layout.fs_rx};
 return 0;
}
static unsigned tegu_native_codecheck_seen;
static int tegu_native_codecheck_enabled(void)
{
 int enabled=0;
 return PE_parse_boot_argn("tegu_gpu_codecheck",&enabled,sizeof(enabled))&&enabled==1;
}
static int tegu_native_codecheck_take(void)
{
 if(!tegu_native_codecheck_enabled())return 0;
 return __atomic_fetch_add(&tegu_native_codecheck_seen,1u,__ATOMIC_RELAXED)<16u;
}
static void tegu_native_codecheck_emit(uint32_t token,const void *package,size_t package_bytes,
 const struct tegu_render_info *info,const void *memory,size_t memory_bytes,
 const struct tegu_native_v6_layout *layout,TeguGPUBuffer *scratch,
 const struct tegu_full_render_job *job)
{
 if(!package||!info||!memory||!layout||memory_bytes<0x490){
  IOLog("TEGU_CODECHECK token=%u rc=invalid-input\n",token);return;
 }
 struct tegu_codecheck_result r={0};
 uint32_t po[2]={info->stages[0].code_offset,info->stages[1].code_offset};
 uint32_t cb[2]={info->stages[0].code_bytes,info->stages[1].code_bytes};
 uint32_t so[2]={layout->vs_offset,layout->fs_offset};
 int rc=tegu_codecheck_capture(package,package_bytes,po,cb,memory,memory_bytes,so,&r);
 IOLog("TEGU_CODECHECK token=%u rc=%d pkg_bytes=%lu vs_off=%u fs_off=%u vs_bytes=%u fs_bytes=%u\n",
       token,rc,(unsigned long)package_bytes,so[0],so[1],cb[0],cb[1]);
 if(rc)return;
 if(job&&job->commands&&job->command_count&&job->command_count<=128){
  char command_sha256[65];size_t command_bytes=(size_t)job->command_count*sizeof(job->commands[0]);
  if(!tegu_codecheck_sha256(job->commands,command_bytes,command_sha256))
   IOLog("TEGU_CODECHECK token=%u render_commands=%u bytes=%lu sha256=%s\n",
         token,(unsigned)job->command_count,(unsigned long)command_bytes,command_sha256);
  else IOLog("TEGU_CODECHECK token=%u render_commands_hash_rc=failed\n",token);
 }else IOLog("TEGU_CODECHECK token=%u render_commands=invalid\n",token);
 IOLog("TEGU_CODECHECK token=%u package_sha256=%s\n",token,r.package_sha256);
 IOLog("TEGU_CODECHECK token=%u vs_pkg=%s vs_stage=%s equal=%d first_diff=%lu\n",
       token,r.package_vs_sha256,r.staged_vs_sha256,r.vs_equal,(unsigned long)r.vs_first_diff);
 IOLog("TEGU_CODECHECK token=%u fs_pkg=%s fs_stage=%s equal=%d first_diff=%lu\n",
       token,r.package_fs_sha256,r.staged_fs_sha256,r.fs_equal,(unsigned long)r.fs_first_diff);
 if(!scratch||!job||!job->request||!job->request->scratch){
  IOLog("TEGU_CODECHECK token=%u pa_map=invalid\n",token);return;
 }
 for(unsigned stage=0;stage<2;stage++)for(uint32_t n=0;n<cb[stage];n+=FS_PAGE_SIZE){
  uint32_t off=so[stage]+n;size_t page=off/FS_PAGE_SIZE;uint64_t gpu=0;
  uint64_t cpu=(uint64_t)ml_vtophys((vm_offset_t)memory+off);
  int gpu_ok=scratch->gpuPage((uint64_t)page*FS_PAGE_SIZE,&gpu);
  int req_ok=page<job->request->scratch_count;
  uint64_t req=req_ok?job->request->scratch[page].pa:UINT64_MAX;
  IOLog("TEGU_CODECHECK token=%u stage=%s page=%lu gpu_ok=%d gpu_pa=%llx cpu_pa=%llx req_pa=%llx match=%d\n",
        token,stage?"fs":"vs",(unsigned long)page,gpu_ok,(unsigned long long)gpu,
        (unsigned long long)cpu,(unsigned long long)req,
        gpu_ok&&req_ok&&cpu&&gpu==req&&cpu==gpu);
 }
 const uint8_t *spd=(const uint8_t *)memory+0x480;
 IOLog("TEGU_CODECHECK token=%u fs_spd16=%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
       token,spd[0],spd[1],spd[2],spd[3],spd[4],spd[5],spd[6],spd[7],
       spd[8],spd[9],spd[10],spd[11],spd[12],spd[13],spd[14],spd[15]);
}

IOReturn TeguGPUUserClient::renderCompositor(IOExternalMethodArguments *a)
{
    static_assert(sizeof(struct tegu_compositor_request) == TEGU_COMPOSITOR_REQUEST_BYTES,
        "compositor request ABI");
    if (!a || !a->scalarInput || !a->scalarOutput || !a->structureInput ||
        a->scalarInputCount != 4 || a->scalarOutputCount != 2)
        return kIOReturnBadArgument;
    if(a->structureInputSize==sizeof(struct tegu_ui2_bind_request)){
        RenderPipeline *ui=NULL;
        for(unsigned i=0;i<16;i++)if(renderPipelines[i].package&&
           renderPipelines[i].handle==a->scalarInput[0])ui=&renderPipelines[i];
        if(!ui)return kIOReturnNotFound;
        return renderFixedUITexture(a,ui);
    }
    if(a->structureInputSize==sizeof(struct tegu_ui1_bind_request)){
        RenderPipeline *ui=NULL;
        for(unsigned i=0;i<16;i++)if(renderPipelines[i].package&&
           renderPipelines[i].handle==a->scalarInput[0])ui=&renderPipelines[i];
        if(!ui)return kIOReturnNotFound;
        return renderFixedUI(a,ui);
    }
    if(a->structureInputSize!=TEGU_COMPOSITOR_REQUEST_BYTES)return kIOReturnBadArgument;
    if (!TEGU_GPU_COMPOSITOR_EXEC || !TEGU_GPU_COMPOSITOR_V6 || !tegu_gpu_compute_ready())
        return kIOReturnUnsupported;
    if (!nextSerial || nextSerial > UINT32_MAX) return kIOReturnNoResources;

    auto getBuffer = [this](uint64_t handle) -> TeguGPUBuffer * {
        if (!handle || handle > UINT32_MAX) return NULL;
        for (unsigned i=0;i<tgSlots;i++)
            if (slots[i].buffer && slots[i].handle==(uint32_t)handle) return slots[i].buffer;
        return NULL;
    };
    struct tegu_compositor_request request;
    memcpy(&request, a->structureInput, sizeof(request));
    bool loadPass=request.version==TEGU_COMPOSITOR_REQUEST_VERSION_LOAD&&
        request.load_action==TEGU_COMPOSITOR_LOAD_LOAD;
    bool clearPass=request.version==TEGU_COMPOSITOR_REQUEST_VERSION&&
        request.load_action==TEGU_COMPOSITOR_LOAD_CLEAR;
    if ((!clearPass&&!loadPass) || request.reserved ||
        request.store_action != TEGU_COMPOSITOR_STORE_STORE ||
        request.attachment_mask != 1 ||
        request.sampler_class != TEGU_COMPOSITOR_SAMPLER_NEAREST_CLAMP_LOD0 ||
        !tegu_compositor_all_zero(request.texture_reserved,sizeof(request.texture_reserved)) ||
        request.vertex.version != 1 || (request.vertex.buffer_mask != 1 && request.vertex.buffer_mask != 2) ||
        request.vertex.reserved[0] || request.vertex.reserved[1] ||
        request.vertex.draw.first_vertex || request.vertex.draw.vertex_count != 3 ||
        request.vertex.draw.instance_count != 1 || request.vertex.draw.base_instance)
        return kIOReturnBadArgument;
    if(loadPass&&!TEGU_GPU_COMPOSITOR_LOAD)return kIOReturnUnsupported;

    struct RenderPipeline *pipeline = NULL;
    for (unsigned i = 0; i < 16; ++i)
        if (renderPipelines[i].package && renderPipelines[i].handle == a->scalarInput[0])
            pipeline = &renderPipelines[i];
    if (!pipeline || !pipeline->package) return kIOReturnNotFound;
    if(pipeline->kind!=TEGU_RENDER_PIPELINE_TGR||!pipeline->info||pipeline->ac1)
        return kIOReturnUnsupported;
    const struct tegu_render_info *info = pipeline->info;
    const bool v7=info->version==7;
    const bool b3=info->version==TEGU_RENDER_B3_VERSION;
    const bool expanded=v7||b3;
    if(expanded&&!TEGU_GPU_COMPOSITOR_V7)return kIOReturnUnsupported;
    if(request.texture_format!=(b3?80u:70u))return kIOReturnUnsupported;
    if(b3&&(request.width!=776u||request.height!=24u||request.row_stride!=3104u||
       request.texture_width!=776u||request.texture_height!=24u||
       request.texture_stride!=3104u||a->scalarInput[2]))return kIOReturnUnsupported;
    if(v7) {
        const int blendClass=tegu_native_v7_blend_class(&info->blend);
        if(blendClass!=TEGU_NATIVE_V7_BLEND_SOURCE_OVER&&blendClass!=TEGU_NATIVE_V7_BLEND_DISABLED)
            return kIOReturnUnsupported;
        /* R44's unblended descriptor is supported for clear/store only. */
        if(blendClass==TEGU_NATIVE_V7_BLEND_DISABLED&&loadPass)return kIOReturnUnsupported;
    }
    if(b3&&loadPass)return kIOReturnUnsupported;
    if ((info->version != 6 && !expanded) || info->compositor.version != 1 ||
        info->color_count != 2 || info->color_formats[0] != 80 ||
        info->color_write_masks[0] != 15 || info->color_formats[1] != 80 ||
        info->color_write_masks[1] != 0 ||
        !tegu_render_compositor_pass_supported(info,request.attachment_mask) ||
        info->vertex_mask != 3 || info->vertex_count != 2 ||
        info->texture.version != 1 || info->texture.stage != 16 ||
        info->texture.texture_index != 3 || info->texture.sampler_index != 0 ||
        info->texture.descriptor_set != 0 || info->texture.binding != 4 ||
        info->texture.normalized_coordinates != 1)
        return kIOReturnUnsupported;
    if(loadPass&&(!info->blend.enabled||info->blend.rgb_op!=0||
       info->blend.rgb_src!=1||info->blend.rgb_dst!=5||info->blend.alpha_op!=0||
       info->blend.alpha_src!=1||info->blend.alpha_dst!=5||info->blend.write_mask!=15))
        return kIOReturnUnsupported;
    if (info->compositor.ubo_count != 4 || info->compositor.texture_count != 1 ||
        info->compositor.sampler_count != 1 || info->compositor.sample_count != (v7?16u:4u) ||
        info->compositor.ubo_stage[0] != 1 || info->compositor.ubo_stage[1] != 1 ||
        info->compositor.ubo_stage[2] != 2 || info->compositor.ubo_stage[3] != 2 ||
        info->compositor.ubo_index[0] != 2 || info->compositor.ubo_index[1] != 3 ||
        info->compositor.ubo_index[2] != 1 || info->compositor.ubo_index[3] != 6 ||
        info->compositor.ubo_window[0] != 64 || info->compositor.ubo_window[1] != 16 ||
        info->compositor.ubo_window[2] != (v7?32u:b3?82u:72u) || info->compositor.ubo_window[3] != 2 ||
        info->compositor.texture_index != 3 || info->compositor.sampler_index != 0 ||
        info->compositor.descriptor_binding != 4 || info->compositor.texel_format != 0xd241f2)
        return kIOReturnUnsupported;

    if(info->vertex[0].buffer_index>1 || info->vertex[1].buffer_index!=info->vertex[0].buffer_index)
        return kIOReturnUnsupported;
    const unsigned vertexSlot=info->vertex[0].buffer_index;
    if(request.vertex.buffer_mask!=(1u<<vertexSlot))return kIOReturnBadArgument;
    for(unsigned i=0;i<TEGU_VERTEX_SLOTS;i++)if(i!=vertexSlot&&
        (request.vertex.buffers[i].handle||request.vertex.buffers[i].offset||request.vertex.buffers[i].length))
        return kIOReturnBadArgument;
    struct tegu_render_vertex_binding *vb = &request.vertex.buffers[vertexSlot];
    TeguGPUBuffer *target = getBuffer(a->scalarInput[1]);
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    TeguGPUImportedTarget *surfaceTarget = NULL;
    for(unsigned i=0;i<tgImportedTargetSlots;i++)
        if(importedTargets[i].surface&&importedTargets[i].handle==a->scalarInput[1])
            surfaceTarget=&importedTargets[i];
    if(target&&surfaceTarget)return kIOReturnBadArgument;
    if(surfaceTarget && (surfaceTarget->metadataVersion!=TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION ||
        surfaceTarget->metalFormat!=80 || !surfaceTarget->backing || !surfaceTarget->surface))
        return kIOReturnUnsupported;
    IOMemoryDescriptor *targetBacking=surfaceTarget?surfaceTarget->backing:
        (IOMemoryDescriptor *)target;
    void *targetCPU=surfaceTarget?(void *)surfaceTarget->surface->kernelAddress():
        (target?target->getBytesNoCopy():NULL);
#else
    IOMemoryDescriptor *targetBacking=(IOMemoryDescriptor *)target;
    void *targetCPU=target?target->getBytesNoCopy():NULL;
#endif
    uint64_t targetLength=targetBacking?targetBacking->getLength():0;
    TeguGPUBuffer *vertex = getBuffer(vb->handle);
    if (!targetBacking || !vertex || !targetCPU) return kIOReturnNotFound;
    if(b3&&(!target||target->logicalLength()!=99524u))return kIOReturnUnsupported;
    if ((target && target == vertex) || !vb->length || vb->length > UINT32_MAX ||
        vb->offset > vertex->logicalLength() ||
        vb->length > vertex->logicalLength()-vb->offset ||
        vb->offset > UINT32_MAX || vb->length > UINT32_MAX-vb->offset)
        return kIOReturnBadArgument;

    struct tegu_fs_resource_span spans[7] = {};
    uint64_t original_offsets[5] = {};
    uint32_t descriptor_bytes[4] = {};
    uint64_t resource_pages = 0;
    spans[0] = (struct tegu_fs_resource_span) {
        vertex, vb->offset & ~UINT64_C(4095),
        (uint32_t)((vb->offset & 4095u)+vb->length)
    };
    if ((uint64_t)spans[0].length != (vb->offset&4095u)+vb->length)
        return kIOReturnBadArgument;

    resource_pages=tegu_compositor_page_count(spans[0].offset,spans[0].length);
    if(resource_pages==UINT64_MAX)return kIOReturnBadArgument;

    struct tegu_native_v6_binding dynamic = {};
    dynamic.table1_handle = FS_APERTURE_VA+0x7004;
    const uint32_t windows[4] = {64,16,v7?32u:b3?82u:72u,2};
    for(unsigned i=0;i<4;i++) {
        const struct tegu_render_vertex_binding *binding=&request.ubos[i];
        TeguGPUBuffer *b=getBuffer(binding->handle);
        if(!b)return kIOReturnNotFound;
        if((target && b==target)||!binding->length||binding->offset>UINT32_MAX||
           binding->length>UINT32_MAX-binding->offset||
           binding->offset>b->logicalLength()||binding->length>b->logicalLength()-binding->offset)
            return kIOReturnBadArgument;
        uint32_t residual=(uint32_t)(binding->offset&15u);
        uint32_t need=(residual+windows[i]+15u)&~15u;
        if(binding->length<windows[i])return kIOReturnBadArgument;
        descriptor_bytes[i]=need;
        uint64_t aligned=binding->offset&~UINT64_C(15);
        uint64_t page=aligned&~UINT64_C(4095);
        uint64_t spanbytes=(aligned-page)+(uint64_t)need;
        if(aligned>b->getLength()||need>b->getLength()-aligned||spanbytes>UINT32_MAX)return kIOReturnBadArgument;
        spans[i+1]=(struct tegu_fs_resource_span){b,page,(uint32_t)spanbytes};

        uint64_t np=tegu_compositor_page_count(page,spanbytes);
        if(np==UINT64_MAX||np>FS_MAX_RESOURCE_PAGES-resource_pages)return kIOReturnNoResources;
        resource_pages+=np;
        original_offsets[i]=binding->offset;
        dynamic.ubo_offset[i]=(uint32_t)binding->offset;
        dynamic.ubo_length[i]=(uint32_t)(binding->offset+binding->length);
    }
    const struct tegu_render_vertex_binding *tb=&request.texture;
    TeguGPUBuffer *texture=getBuffer(tb->handle);
    if(!texture)return kIOReturnNotFound;
    /* The admitted sampler is nearest/clamp with min/max LOD zero. Map the
     * request's exact level-0 span; a whole multi-level backing is refused. */
    uint64_t textureBytes=(uint64_t)request.texture_stride*request.texture_height;
    if(!request.texture_width||!request.texture_height||request.texture_width>4096u||
       request.texture_height>4096u||(!b3&&(request.texture_stride&63u))||
       request.texture_stride<(uint64_t)request.texture_width*4u||
       request.texture_stride>4096u*16u||!textureBytes||textureBytes>(16u*1024u*1024u)||
       (!b3&&(request.texture_width!=16||request.texture_height!=16||request.texture_stride!=64))||
       (b3&&(texture->logicalLength()!=99524u||tb->offset))||
       (target && texture==target)||!tb->length||tb->offset&4095u||tb->length!=textureBytes||
       tb->offset>texture->logicalLength()||tb->length>texture->logicalLength()-tb->offset)
        return kIOReturnBadArgument;
    spans[5]=(struct tegu_fs_resource_span){texture,tb->offset,(uint32_t)textureBytes};
    uint64_t texture_pages=tegu_compositor_page_count(tb->offset,textureBytes);
    if(texture_pages==UINT64_MAX||texture_pages>FS_MAX_RESOURCE_PAGES-resource_pages)
        return kIOReturnNoResources;
    resource_pages+=texture_pages;

    if(loadPass)resource_pages+=2;
    if(resource_pages>FS_MAX_RESOURCE_PAGES)return kIOReturnNoResources;
    struct tegu_fs_surface_layout layout;
    uint64_t target_offset=a->scalarInput[2];
    const bool use_heap2m=expanded&&(request.width>16||request.height>16);
    if((b3?tegu_fs_surface_layout_b3_with_heap(request.width,request.height,request.row_stride,
          target_offset,(uint32_t)resource_pages,FS_V7_SCRATCH_PAGES,FS_HEAP_PAGES,&layout):
       tegu_fs_surface_layout_with_heap(request.width,request.height,request.row_stride,
          target_offset,(uint32_t)resource_pages,expanded?FS_V7_SCRATCH_PAGES:FS_SCRATCH_PAGES,
          use_heap2m?FS_HEAP_PAGES:0,&layout)) ||
       target_offset>targetLength || layout.target_bytes>targetLength-target_offset ||
       target_offset>UINT32_MAX || layout.target_bytes>UINT32_MAX)
        return kIOReturnBadArgument;
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    if(surfaceTarget) {
        if(surfaceTarget->metadataVersion!=TEGU_GPU_COMPOSITOR_IOSURFACE_VERSION||
           surfaceTarget->metalFormat!=80||!surfaceTarget->surface||!surfaceTarget->backing)
            return kIOReturnUnsupported;
        const struct sweet_surface_gpu_layout *m=&surfaceTarget->layout;
        const struct sweet_surface_plane *p=&m->plane0;
        struct tegu_compositor_iosurface_layout surface={
            m->width,m->height,m->bytesPerRow,m->offset,m->pixelFormat,m->allocSize,
            m->planeCount,p->width,p->height,p->bytesPerRow,p->offset,p->size,
            m->bytesPerElement,p->bytesPerElement,m->elementWidth,m->elementHeight,
            p->elementWidth,p->elementHeight
        };
        struct tegu_fs_surface_layout importedLayout={};
        if(target_offset!=m->offset||request.width!=m->width||request.height!=m->height||
           request.row_stride!=m->bytesPerRow||
           !tegu_compositor_iosurface_layout_validate(&surface,targetLength,&importedLayout)||
           importedLayout.target_bytes!=layout.target_bytes||
           importedLayout.target_pages!=layout.target_pages)
            return kIOReturnUnsupported;
    }
#endif
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    if(surfaceTarget) {
        const struct sweet_surface_gpu_layout *m=&surfaceTarget->layout;
        if(request.width!=m->width||request.height!=m->height||
           request.row_stride!=m->bytesPerRow||target_offset!=m->offset||
           m->pixelFormat!=TEGU_IOSURFACE_PIXELFORMAT_BGRA||m->allocSize!=targetLength)
            return kIOReturnBadArgument;
    }
#endif
    if(target && (target==vertex||target==texture))return kIOReturnBadArgument;

    struct tegu_fs_queue_context queue={};
    if(tegu_gpu_fullscreen_context(&queue)||!queue.l2_entries||
       queue.l2_page_pa!=TEGU_FS_QUEUE_L2_PA||queue.protected_begin>=queue.protected_end)
        return kIOReturnNotReady;
    struct tegu_fs_reserved_range excluded[2] = {
        {queue.protected_begin,queue.protected_end},
        {queue.l2_page_pa,queue.l2_page_pa+FS_PAGE_SIZE}
    };
    struct tegu_fs_reserved_context reserved={excluded,2};
    TeguGPUBuffer *loadBacking=NULL;
    if(loadPass) {
        loadBacking=TeguGPUBuffer::allocate(8192);
        if(!loadBacking)return kIOReturnNoMemory;
        loadBacking->setLogicalLength(8192);
        spans[6]=(struct tegu_fs_resource_span){loadBacking,0,8192};
    }
    tegu_v7_scratch_layout v7Layout={};
    if(expanded&&!(b3?tegu_b3_scratch_layout_build(info->stages[0].code_bytes,
        info->stages[1].code_bytes,loadPass?1:0,&v7Layout):
        tegu_v7_scratch_layout_build(info->stages[0].code_bytes,
        info->stages[1].code_bytes,loadPass?1:0,&v7Layout))) {
        if(loadBacking)loadBacking->release();return kIOReturnUnsupported;
    }
    const uint32_t scratchBytes=expanded?v7Layout.scratch_bytes:0x40000u;
    const uint64_t execMask=expanded?v7Layout.exec_mask:
        (loadPass?FS_SCRATCH_EXEC_LOAD:FS_SCRATCH_EXEC_ALLOWED);
    TeguGPUBuffer *scratch=TeguGPUBuffer::allocate(scratchBytes);
    if(!scratch){if(loadBacking)loadBacking->release();return kIOReturnNoMemory;}
    scratch->setLogicalLength(scratchBytes);
    struct tegu_fullscreen_backing *backing=NULL;
#if TEGU_GPU_COMPOSITOR_IOSURFACE
    if(surfaceTarget) {
        auto create=use_heap2m?tegu_fullscreen_backing_create_surface_v7_heap2m:
            expanded?tegu_fullscreen_backing_create_surface_v7:tegu_fullscreen_backing_create_surface;
        backing=create(scratch,surfaceTarget->backing,surfaceTarget->gpuLease,
            tegu_fullscreen_surface_page_resolve,target_offset,&layout,spans,
            loadPass?7:6,execMask,queue.l2_entries,queue.l2_page_pa,&reserved);
    } else
#endif
    {
        auto create=b3?tegu_fullscreen_backing_create_b3_heap2m:
            use_heap2m?tegu_fullscreen_backing_create_v7_heap2m:
            expanded?tegu_fullscreen_backing_create_v7:tegu_fullscreen_backing_create;
        backing=create(scratch,target,target_offset,&layout,spans,loadPass?7:6,
            execMask,queue.l2_entries,queue.l2_page_pa,&reserved);
    }
    if(!backing){if(loadBacking)loadBacking->release();scratch->release();return kIOReturnNoMemory;}

    IOReturn result=kIOReturnBadArgument;
    struct tegu_compositor_load_bundle_v6 *loadBundle=NULL;
    auto cleanup = [&]() -> IOReturn {
        if(loadBundle)IOFree(loadBundle,sizeof(*loadBundle));
        tegu_fullscreen_backing_finish(backing,1);
        if(loadBacking)loadBacking->release();
        scratch->release();
        return result;
    };
    unsigned char *memory=(unsigned char *)scratch->getBytesNoCopy();
    struct tegu_native_v6_checked checked={};
    for(unsigned i=0;i<4;i++) {
        uint32_t residual=(uint32_t)(original_offsets[i]&15u);
        unsigned char *push=i<2?dynamic.vertex_push:dynamic.fragment_push;
        tegu_render_put(push+info->compositor.push_residual_offset[i],residual);
    }
    tegu_render_put(dynamic.fragment_push+info->compositor.texel_format_push_offset,0xd241f2);
    uint64_t package_size=(uint64_t)info->stages[1].code_offset+info->stages[1].code_bytes;
    if(package_size>TEGU_RENDER_STORAGE_MAX||
       (b3?tegu_compositor_bind_b3(pipeline->package->getBytesNoCopy(),package_size,&dynamic,&checked):
        v7?tegu_compositor_bind_v7(pipeline->package->getBytesNoCopy(),package_size,&dynamic,&checked):
        tegu_native_v6_bind(pipeline->package->getBytesNoCopy(),package_size,&dynamic,&checked)))return cleanup();
    struct tegu_native_v6_layout code_layout={};
    if(b3?tegu_compositor_stage_b3(pipeline->package->getBytesNoCopy(),package_size,
        memory,scratchBytes,&code_layout):
       v7?tegu_compositor_stage_v7(pipeline->package->getBytesNoCopy(),package_size,
        memory,scratchBytes,&code_layout):
        tegu_native_v6_stage_copy(pipeline->package->getBytesNoCopy(),package_size,
        memory,scratchBytes,&code_layout))return cleanup();
    if(loadPass&&!v7) {
        if(code_layout.fs_bytes>2u*FS_PAGE_SIZE)return cleanup();
        memmove(memory+0xc000,memory+code_layout.fs_offset,code_layout.fs_bytes);
        memset(memory+0xe000,0,2u*FS_PAGE_SIZE);
        code_layout.fs_offset=0xc000;
    }
    (void)code_layout;
    if(expanded) {
        if(!tegu_v7_scratch_prepare(memory,scratchBytes))return cleanup();
    } else memset(memory+0x10000,0,0x30000); /* v6 geometry + chunk */

    uint64_t vertex_va=0;
    if(!tegu_fullscreen_backing_resolve_resource(backing,0,vb->offset,vb->length,&vertex_va))
        return cleanup();
    struct tegu_vertex_buffer vertex_buffer={vertex_va,vb->length};
    struct tegu_compositor_vertex_state *vertex_state=
        (struct tegu_compositor_vertex_state *)(memory+0x8000);
    if(sizeof(*vertex_state)>0x4000 || tegu_compositor_vertex_build(info->vertex,info->vertex_count,&vertex_buffer,
          &request.vertex.draw,FS_APERTURE_VA+0x6000,vertex_state))return cleanup();
    memcpy(memory+0x6000,vertex_state->driver,sizeof(vertex_state->driver));

    struct tegu_compositor_resource_input resource_input={};
    resource_input.version=TEGU_RESOURCE_STATE_VERSION;
    resource_input.table0=(struct tegu_gpu_span){FS_APERTURE_VA+0x6000,TEGU_COMPOSITOR_DRIVER_BYTES};
    resource_input.resource_table=(struct tegu_gpu_span){FS_APERTURE_VA+0x7000,64};
    resource_input.descriptors=(struct tegu_gpu_span){FS_APERTURE_VA+0x7100,192};
    resource_input.plane=(struct tegu_gpu_span){FS_APERTURE_VA+0x7200,32};
    for(unsigned i=0;i<4;i++) {
        uint64_t va=0;uint64_t aligned=original_offsets[i]&~UINT64_C(15);
        if(!tegu_fullscreen_backing_resolve_resource(backing,i+1,aligned,
             descriptor_bytes[i],&va))return cleanup();
        resource_input.ubos[i]=(struct tegu_gpu_span){va,descriptor_bytes[i]};
    }
    uint64_t texel_va=0;
    if(!tegu_fullscreen_backing_resolve_resource(backing,5,tb->offset,textureBytes,&texel_va))
        return cleanup();
    resource_input.texels=(struct tegu_gpu_span){texel_va,textureBytes};
    struct tegu_compositor_resource_state resource_state={};
    if(b3?tegu_compositor_resource_state_build_b3(&resource_input,request.texture_width,
         request.texture_height,request.texture_stride,&resource_state):
       v7?tegu_compositor_resource_state_build_v7(&resource_input,&resource_state):
        tegu_compositor_resource_state_build(&resource_input,&resource_state))return cleanup();
    memcpy(memory+0x7000,resource_state.resource_table,sizeof(resource_state.resource_table));
    memcpy(memory+0x7100,resource_state.descriptors,sizeof(resource_state.descriptors));
    memcpy(memory+0x7200,resource_state.plane,sizeof(resource_state.plane));

    struct tegu_render_addresses addresses={
        .descriptors=FS_APERTURE_VA,.tiler=FS_APERTURE_VA+0x1000,
        .geometry=FS_APERTURE_VA+0x10000,
        .chunk=use_heap2m?tegu_fullscreen_backing_job(backing)->heap_gpu_va:
            FS_APERTURE_VA+(expanded?v7Layout.chunk_offset:0x20000u),
        .target=tegu_fullscreen_backing_job(backing)->target_gpu_va,
        .vertex_code=FS_APERTURE_VA+0x2000,.fragment_code=FS_APERTURE_VA+code_layout.fs_offset,
        .clear_rgba=(uint32_t)a->scalarInput[3],
        .chunk_bytes=tegu_fullscreen_backing_job(backing)->heap_bytes,
        .hierarchy_mask=use_heap2m?0x82u:1u
    };
    struct tegu_render_target_layout target_layout={
        TEGU_RENDER_TARGET_LAYOUT_VERSION,request.width,request.height,
        request.row_stride,layout.target_bytes
    };
    struct tegu_render_state *state=(struct tegu_render_state *)(memory+0x8000);
    auto buildState=b3?tegu_compositor_render_build_b3:
        v7?tegu_compositor_render_build_v7:tegu_compositor_render_build_v6;
    if(sizeof(*state)>0x4000 || buildState(
         pipeline->package->getBytesNoCopy(),package_size,&addresses,
         resource_state.resource_table_handle,&target_layout,request.attachment_mask,
         checked.vertex_push,checked.fragment_push,state))return cleanup();
    const struct tegu_render_state *finalState=state;
    if(loadPass) {
        loadBundle=(struct tegu_compositor_load_bundle_v6 *)IOMallocZero(sizeof(*loadBundle));
        if(!loadBundle)return cleanup();
        struct tegu_load_mapping map={};
        if(!tegu_fullscreen_backing_resolve_resource(backing,6,0,8192,&map.resource_va))return cleanup();
        map.dcd_va=map.resource_va+FS_PAGE_SIZE;map.shader_va=FS_APERTURE_VA+14u*FS_PAGE_SIZE;
        map.target_pages=layout.target_pages;map.total_pages=layout.total_pages;
        map.page_role[0]=TEGU_LOAD_MAP_RO_NX;map.page_role[1]=TEGU_LOAD_MAP_RO_NX;map.page_role[2]=TEGU_LOAD_MAP_RO_X;
        struct tegu_render_target_layout loadLayout={TEGU_RENDER_TARGET_LAYOUT_VERSION,
            request.width,request.height,request.row_stride,layout.target_bytes};
        struct tegu_compositor_load_asset_scratch_v6 loadScratch={};
        struct tegu_compositor_load_assets_v6 loadAssets={};
        uint8_t *loadBytes=(uint8_t *)loadBacking->getBytesNoCopy();
        if(!loadBytes||tegu_compositor_load_assets_build_v6(&loadLayout,
             tegu_fullscreen_backing_job(backing)->target_gpu_va,loadBytes,loadBytes+FS_PAGE_SIZE,
             memory+0xe000,&loadScratch,&loadAssets))return cleanup();
        if(tegu_compositor_load_bundle_build_v6(state,&addresses,&loadLayout,&map,&loadAssets,
             code_layout.vs_bytes,code_layout.fs_bytes,loadBundle))return cleanup();
        memcpy(loadBytes,loadBundle->resource_page,FS_PAGE_SIZE);
        memcpy(loadBytes+FS_PAGE_SIZE,loadBundle->dcd_page,FS_PAGE_SIZE);
        memcpy(memory+0xe000,loadBundle->shader_page,FS_PAGE_SIZE);
        memcpy(state,&loadBundle->draw,sizeof(*state));
        IOFree(loadBundle,sizeof(*loadBundle));loadBundle=NULL;
        finalState=state;
    }
    memcpy(memory,finalState->descriptors,sizeof(finalState->descriptors));
    memcpy(memory+0x1000,finalState->tiler,sizeof(finalState->tiler));
    struct tegu_full_render_job *job=tegu_fullscreen_backing_job(backing);
    if(finalState->command_count>128)return cleanup();
    job->commands=finalState->commands;job->command_count=finalState->command_count;
    job->width=request.width;job->height=request.height;job->row_stride=request.row_stride;
    if(use_heap2m) {
        job->timeout_ticks=tegu_gpu_fullscreen_timeout_ticks();
        if(!job->timeout_ticks)return cleanup();
    }
    for(size_t i=0;i<tegu_fullscreen_backing_resource_count(backing);i++) {
        const struct tegu_fs_resource_binding *binding=tegu_fullscreen_backing_resource(backing,i);
        if(!binding||!binding->buffer)return cleanup();
        for(uint32_t p=0;p<binding->page_count;p++)
            FlushPoC_DcacheRegion((vm_offset_t)binding->buffer->getBytesNoCopy()+
                binding->buffer_offset+(uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);
    }
    int codecheck_capture=expanded&&tegu_native_codecheck_take();
    int codecheck_readback_seeded=0;
    if(codecheck_capture&&memory&&scratchBytes>=0x50000u){
        volatile uint32_t *dst=(volatile uint32_t *)(memory+0x4f000u);
        for(unsigned i=0;i<1024;i++)dst[i]=TEGU_CODECHECK_READBACK_SENTINEL;
        codecheck_readback_seeded=1;
    }
    for(size_t i=0;i<tegu_fullscreen_backing_heap_buffer_count(backing);i++) {
        TeguGPUBuffer *heap=tegu_fullscreen_backing_heap_buffer(backing,i);
        if(!heap||!heap->getBytesNoCopy()||heap->logicalLength()<4u*FS_PAGE_SIZE)
            return cleanup();
        FlushPoC_DcacheRegion((vm_offset_t)heap->getBytesNoCopy(),4u*FS_PAGE_SIZE);
    }
    FlushPoC_DcacheRegion((vm_offset_t)memory,scratchBytes);
    /* Diagnostic reads only after the complete staged scratch is published. */
    if(codecheck_capture)
        tegu_native_codecheck_emit((uint32_t)nextSerial,pipeline->package->getBytesNoCopy(),
            package_size,info,memory,scratchBytes,&code_layout,scratch,
            tegu_fullscreen_backing_job(backing));
    /* Publish the full mapped target before GPU ownership begins. */
    for(uint32_t p=0;p<layout.target_pages;p++)
        FlushPoC_DcacheRegion((vm_offset_t)targetCPU+target_offset+
            (uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);
    uint32_t fence=0;
    {
        int status=tegu_gpu_render_fullscreen(job,(uint32_t)nextSerial,&fence);
        if(codecheck_capture){
            if(!codecheck_readback_seeded){
                IOLog("TEGU_CODECHECK token=%u readback=not-armed rc=seed-range\n",(uint32_t)nextSerial);
            }else if(status==-100){
                IOLog("TEGU_CODECHECK token=%u readback=skipped rc=queue-stop-uncertain\n",(uint32_t)nextSerial);
            }else if(status==0||status==-44){
                FlushPoC_DcacheRegion((vm_offset_t)(memory+0x4f000u),FS_PAGE_SIZE);
                int expected_ok=info->stages[1].code_offset<=package_size&&
                    package_size-info->stages[1].code_offset>=0x1b00u&&
                    info->stages[1].code_bytes>=0x1b00u;
                if(!expected_ok){
                    IOLog("TEGU_CODECHECK token=%u readback=not-compared rc=package-range\n",(uint32_t)nextSerial);
                }else{
                    struct tegu_codecheck_readback_result rb={0};
                    int rb_rc=tegu_codecheck_readback_verify(memory+0x4f000u,FS_PAGE_SIZE,
                        (const uint8_t *)pipeline->package->getBytesNoCopy()+
                            info->stages[1].code_offset+0x1a00u,256u,&rb);
                    const char *qualification=rb_rc?"unqualified":
                        (rb.mismatches||rb.guard_errors?"mismatch":"match");
                    IOLog("TEGU_CODECHECK token=%u readback_qualification=%s rc=%d marker=%u bytes=256 mismatches=%u first_diff=%lu guard_errors=%u submit_rc=%d\n",
                        (uint32_t)nextSerial,qualification,rb_rc,rb.marker_ok,rb.mismatches,
                        (unsigned long)rb.first_diff,rb.guard_errors,status);
                }
            }else{
                IOLog("TEGU_CODECHECK token=%u readback=not-executed submit_rc=%d\n",(uint32_t)nextSerial,status);
            }
        }
        struct fs_mapping *mapping=tegu_fullscreen_backing_mapping(backing);
        bool complete=status==0&&mapping&&!mapping->active&&mapping->disposition==FS_RELEASED;
        bool safe=mapping&&!mapping->active&&
            (mapping->disposition==FS_RELEASE_STAGED||mapping->disposition==FS_RELEASED);
        /* Only invalidate/clean CPU target aliases after known GPU completion. */
        if(complete)for(uint32_t p=0;p<layout.target_pages;p++)
            FlushPoC_DcacheRegion((vm_offset_t)targetCPU+target_offset+
                (uint64_t)p*FS_PAGE_SIZE,FS_PAGE_SIZE);
        tegu_fullscreen_backing_finish(backing,safe);
        backing=NULL;
        if(status)result=status==-41?kIOReturnNotReady:
            status==-40?kIOReturnBadArgument:kIOReturnIOError;
        else if(!complete)result=kIOReturnIOError;
        else {
            a->scalarOutput[0]=nextSerial++;
            a->scalarOutput[1]=fence;
            result=kIOReturnSuccess;
        }
    }
    if(loadBacking)loadBacking->release();
    scratch->release();
    return result;
}
