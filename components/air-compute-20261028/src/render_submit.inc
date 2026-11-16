/* Single-flight render mappings share the existing submission mutex and queue
 * shutdown policy. Page ownership is held by the kernel caller, never a user. */
#include "tegu_gpu_render_job.h"
static uint64_t tg_render_ptes[TEGU_RENDER_PAGES];
static uint64_t tg_render_entries[TEGU_RENDER_PAGES];
static int tg_render_map(const struct tegu_render_job *job)
{
 volatile uint32_t *table=GPU_CPU_POINTER(TEGU_GPU_QUEUE_ROOT+0x3000,4096);
 if(!table)return -80;
 if(job&&(job->page_count<TEGU_RENDER_BASE_PAGES||job->page_count>TEGU_RENDER_PAGES||
    job->target_page_start!=64||!job->target_page_count||
    job->target_page_count>TEGU_RENDER_PAGES-job->target_page_start||
    job->page_count<job->target_page_start+job->target_page_count))return -82;
 unsigned target_end=job?job->target_page_start+job->target_page_count:64;
 if(job&&((!job->load_page_start)!=(!job->load_page_count)||
    (job->load_page_count&&(job->load_page_start!=target_end||job->load_page_count!=3||
      job->load_page_count>TEGU_RENDER_PAGES-job->load_page_start||
      job->page_count<job->load_page_start+job->load_page_count||
      job->readonly_load_mask!=3||job->executable_load_mask!=4))||
    (!job->load_page_count&&(job->readonly_load_mask||job->executable_load_mask))))return -82;
 for(unsigned i=0;i<TEGU_RENDER_PAGES;i++) {
  unsigned slot=256+i;
  uint64_t old=((uint64_t)table[slot*2+1]<<32)|table[slot*2];
  if(old!=tg_render_ptes[i]||(job&&old))return -81;
  uint64_t pa=job&&i<job->page_count?job->pages[i]:0;tg_render_entries[i]=0;
  if(job&&i<job->page_count) {
   if((pa&4095)||pa<0x882000000ULL||pa>0x8bffff000ULL)return -82;
   unsigned load_i=job->load_page_count&&i>=job->load_page_start&&i<job->load_page_start+job->load_page_count?
       i-job->load_page_start:TEGU_RENDER_PAGES;
   for(unsigned j=0;j<i;j++)if(pa==job->pages[j]&&(i<target_end||j<target_end||load_i<3||
       (job->load_page_count&&j>=job->load_page_start&&j<job->load_page_start+job->load_page_count)))return -82;
   int executable=(i==2||i==3)||(load_i<3&&(job->executable_load_mask&(1u<<load_i)));
   int readonly=load_i<3&&(job->readonly_load_mask&(1u<<load_i));
   tg_render_entries[i]=pa|(executable?0x4d7ULL:(readonly||i>=target_end?0x400000000004d7ULL:0x40000000000457ULL));
  }
 }
 if(gpu_boot_wait(0x2468,1,0,100))return -83;
 gpu_wr64(0x2450,TEGU_RENDER_SCRATCH_VA|19); /* existing 1MiB / 256-page aperture */
 gpu_wr(0x2458,2);
 if(gpu_boot_wait(0x2468,1,0,100))return -84;
 for(unsigned i=0;i<TEGU_RENDER_PAGES;i++) {
  unsigned slot=256+i;
  table[slot*2]=0;GPU_IF_BARRIER();
  table[slot*2+1]=(uint32_t)(tg_render_entries[i]>>32);GPU_IF_BARRIER();
  table[slot*2]=(uint32_t)tg_render_entries[i];tg_render_ptes[i]=tg_render_entries[i];
 }
 GPU_IF_BARRIER();gpu_wr(0x2458,4);
 if(gpu_boot_wait(0x2468,1,0,100))return -85;
 return 0;
}
static uint64_t tg_render_append(volatile uint32_t *ring,uint64_t insert,
 uint64_t extract,uint32_t token,const struct tegu_render_job *job)
{
 if(!job->commands||!job->command_count||job->command_count>128)return 0;
 // GPU aliases can hold different shader code on the next draw. Match the
 // qualified compute path's cache invalidation before consuming new programs.
 uint64_t commands[135];unsigned n=0;
 commands[n++]=23ULL<<56;
 commands[n++]=(2ULL<<56)|(74ULL<<48);
 commands[n++]=(36ULL<<56)|(74ULL<<40)|0x233;
 commands[n++]=(3ULL<<56)|(1ULL<<16);
 for(unsigned i=0;i<job->command_count;i++)commands[n++]=job->commands[i];
 commands[n++]=(1ULL<<56)|(TEGU_GPU_QUEUE_VA+0x3800);
 commands[n++]=(2ULL<<56)|(2ULL<<48)|token;
 commands[n++]=(21ULL<<56)|(2ULL<<48)|(1ULL<<16);
 GPU_IF_BARRIER();
 return tegu_gpu_ring_append(ring,insert,extract,commands,n);
}
