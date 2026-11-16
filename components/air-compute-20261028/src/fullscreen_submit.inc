/* Fullscreen MMU and queue adapter. All calls run under tg_submit_lock.
 * Pointers and physical metadata are kernel-owned, never caller GPU addresses. */
#include "fullscreen_job_contract.h"
/* Shared mapper lives in this BSD object; IOKit uses the C declarations. */
#include "fullscreen_map.c"

static int tg_fullscreen_lock_begin(void *ctx,uint64_t base,unsigned size_log2)
{
 (void)ctx;
 /* The new aperture spans at most 32 MiB, outside legacy queue mappings. */
 if(!tg_queue_live || size_log2<20 || size_log2>25 ||
    (base & ((UINT64_C(1)<<size_log2)-1)) ||
    base!=FS_APERTURE_VA ||
    (gpu_rd(0x2000)&0xffffu) || gpu_rd(0x245c) || gpu_rd(0x3c) || s2_rd(0x2000))return -1;
 volatile uint32_t *input=GPU_CPU_POINTER(TEGU_GPU_QUEUE_INPUT_PA,8);
 volatile uint32_t *output=GPU_CPU_POINTER(TEGU_GPU_QUEUE_OUTPUT_PA,8);
 if(!input||!output)return -1;
 uint64_t insert=tg_queue_position(input),extract=tg_queue_position(output);
 if(insert==UINT64_MAX||extract==UINT64_MAX||insert!=extract||(insert&7))return -1;
 if(gpu_boot_wait(0x2468,1,0,100))return -1;
 /* AS_LOCKADDR.SIZE encodes log2(bytes)-1, as in the qualified legacy path. */
 gpu_wr64(0x2450,base|(uint64_t)(size_log2-1));
 gpu_wr(0x2458,2);
 return gpu_boot_wait(0x2468,1,0,100)?-1:0;
}
static int tg_fullscreen_write_entry(void *ctx,volatile uint64_t *entry,uint64_t value)
{
 (void)ctx;
 if(!entry||((uintptr_t)entry&7))return -1;
 volatile uint32_t *words=(volatile uint32_t *)entry;
 /* Invalidate low word before changing address/high attributes, then publish. */
 words[0]=0;GPU_IF_BARRIER();
 words[1]=(uint32_t)(value>>32);GPU_IF_BARRIER();
 words[0]=(uint32_t)value;GPU_IF_BARRIER();
 return 0;
}
static int tg_fullscreen_flush_unlock(void *ctx)
{
 (void)ctx;
 GPU_IF_BARRIER();gpu_wr(0x2458,4);
 return gpu_boot_wait(0x2468,1,0,100)?-1:0;
}
extern void FlushPoC_DcacheRegion(vm_offset_t,size_t);
static int tg_fullscreen_sync_table(void *ctx,volatile uint64_t *entries,size_t bytes)
{
 (void)ctx;
 if(!entries||((uintptr_t)entries&7)||bytes!=FS_PAGE_SIZE)return -1;
 FlushPoC_DcacheRegion((vm_offset_t)entries,bytes);GPU_IF_BARRIER();return 0;
}
/* Static lifetime: fs_mapping keeps the callback table after submission and
 * quarantine must never retain a pointer into a dead submitter stack frame. */
static const struct fs_ops tg_fullscreen_ops={
 .ctx=0,.lock_begin=tg_fullscreen_lock_begin,
 .write_entry=tg_fullscreen_write_entry,.sync_table=tg_fullscreen_sync_table,
 .flush_unlock=tg_fullscreen_flush_unlock
};
static int tg_fullscreen_map(const struct tegu_full_render_job *job)
{
 size_t scratch_pages=job&&job->request?job->request->scratch_count:0;
 if(!job||!job->mapping||!job->request||!job->owner||
    job->mapping->magic!=FS_MAPPING_MAGIC||job->mapping->active||
    job->mapping->disposition!=FS_RELEASE_STAGED||
    job->request->owner!=job->owner||
    job->request->l2_page_pa!=TEGU_GPU_QUEUE_ROOT+0x2000||
    (const void *)job->request->l2_entries!=(const void *)GPU_CPU_POINTER(TEGU_GPU_QUEUE_ROOT+0x2000,4096)||
    (scratch_pages!=FS_SCRATCH_PAGES&&scratch_pages!=FS_V7_SCRATCH_PAGES)||
    job->target_gpu_va!=FS_APERTURE_VA+(uint64_t)scratch_pages*FS_PAGE_SIZE||
    job->target_page_start!=scratch_pages||
    job->target_page_count!=job->request->target_count||
    (job->request->heap_count!=0&&job->request->heap_count!=FS_HEAP_PAGES)||
    job->page_count!=job->request->scratch_count+job->request->target_count+
        job->request->resource_count+job->request->heap_count)return -120;
 uint64_t expected_heap_va=job->request->heap_count?FS_APERTURE_VA+
    (uint64_t)(scratch_pages+job->request->target_count+job->request->resource_count)*FS_PAGE_SIZE:0;
 if(job->heap_gpu_va!=expected_heap_va||
    job->heap_bytes!=(job->request->heap_count?0x200000u:0u))return -120;
 struct tegu_fs_surface_layout layout;
 if(tegu_fs_surface_layout_with_heap(job->width,job->height,job->row_stride,0,
       (uint32_t)job->request->resource_count,
       (uint32_t)scratch_pages,(uint32_t)job->request->heap_count,&layout)||
    layout.target_bytes!=job->request->target_bytes||layout.total_pages!=job->page_count||
    layout.target_pages!=job->target_page_count||layout.l3_pages!=job->request->l3_count)return -120;
 return fs_map(job->mapping,job->request,&tg_fullscreen_ops)==FS_KEEP_LIVE?0:-121;
}
static int tg_fullscreen_unmap(const struct tegu_full_render_job *job)
{
 return fs_unmap(job->mapping)==FS_RELEASED?0:-122;
}
#include "tegu_gpu_codecheck_map.h"
#include "tegu_gpu_codecheck_readback.h"
static uint64_t tg_fullscreen_append(volatile uint32_t *ring,uint64_t insert,
 uint64_t extract,uint32_t token,const struct tegu_full_render_job *job,int readback)
{
 if(!job->commands||!job->command_count||job->command_count>128)return 0;
 uint64_t commands[169];unsigned n=0;
 if(readback&&job->request&&job->request->scratch_count==FS_V7_SCRATCH_PAGES){
  struct tegu_codecheck_leaf src,dst;
  if(!tegu_codecheck_fs_leaf(job->request,49,&src)&&
     !tegu_codecheck_fs_leaf(job->request,79,&dst)&&src.match&&dst.match&&
     src.pa&&dst.pa&&src.expected==(src.pa|FS_PTE_RO_X)&&
     dst.expected==(dst.pa|FS_PTE_RW_NX)){
   uint64_t prefix[TEGU_CODECHECK_READBACK_WORDS];
   unsigned count=tegu_codecheck_readback_prefix(prefix);
   if(count!=TEGU_CODECHECK_READBACK_WORDS)return 0;
   for(unsigned i=0;i<count;i++)commands[n++]=prefix[i];
  }
 }
 commands[n++]=23ULL<<56;
 commands[n++]=(2ULL<<56)|(74ULL<<48);
 commands[n++]=(36ULL<<56)|(74ULL<<40)|0x233;
 commands[n++]=(3ULL<<56)|(1ULL<<16);
 for(unsigned i=0;i<job->command_count;i++)commands[n++]=job->commands[i];
 commands[n++]=(1ULL<<56)|(TEGU_GPU_QUEUE_VA+0x3800);
 commands[n++]=(2ULL<<56)|(2ULL<<48)|token;
 commands[n++]=(21ULL<<56)|(2ULL<<48)|(1ULL<<16);
 if(n>sizeof(commands)/sizeof(commands[0]))return 0;
 GPU_IF_BARRIER();
 return tegu_gpu_ring_append(ring,insert,extract,commands,n);
}
