#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tegu_gpu_render_job.h"

#define TEGU_GPU_QUEUE_ROOT 0x880100000ULL
#define TEGU_GPU_QUEUE_VA 0x05000000ULL
#define GPU_IF_BARRIER() __sync_synchronize()
static unsigned char queue_arena[0x54000];
static unsigned invl_count;
static void *gpu_cpu_pointer(uint64_t pa,size_t bytes) {
 assert(pa>=TEGU_GPU_QUEUE_ROOT&&pa-TEGU_GPU_QUEUE_ROOT+bytes<=sizeof(queue_arena));
 return queue_arena+(pa-TEGU_GPU_QUEUE_ROOT);
}
#define GPU_CPU_POINTER(pa,n) gpu_cpu_pointer((pa),(n))
static int gpu_boot_wait(uint32_t reg,uint32_t mask,uint32_t want,unsigned timeout) {
 (void)reg;(void)mask;(void)want;(void)timeout;return 0;
}
static void gpu_wr(uint32_t reg,uint32_t value) {(void)value;if(reg==0x2458)invl_count++;}
static void gpu_wr64(uint32_t reg,uint64_t value) {(void)reg;(void)value;}
static uint64_t tegu_gpu_ring_append(volatile uint32_t *r,uint64_t i,uint64_t e,
 const uint64_t *words,unsigned count) {(void)r;(void)i;(void)e;(void)words;(void)count;return 1;}
#include "render_submit.inc"

static uint64_t *pte_table(void){return (uint64_t *)(queue_arena+0x3000);}
int main(void) {
 uint64_t dummy_command=0;struct tegu_render_job dummy={0};
 dummy.commands=&dummy_command;dummy.command_count=1;
 assert(tg_render_append(NULL,0,0,1,&dummy)==1);
 struct tegu_render_job job={0};job.page_count=TEGU_RENDER_PAGES;
 job.target_page_start=64;job.target_page_count=64;
 for(unsigned i=0;i<job.page_count;i++)job.pages[i]=0x883000000ULL+(uint64_t)i*4096;
 assert(tg_render_map(&job)==0);
 for(unsigned i=0;i<TEGU_RENDER_PAGES;i++) {
  unsigned slot=256+i;
  uint64_t expected=job.pages[i]|((i==2||i==3)?0x4d7ULL:
    i>=job.target_page_start+job.target_page_count?0x400000000004d7ULL:0x40000000000457ULL);
  assert(pte_table()[slot]==expected);
 }
 assert(invl_count==2);
 assert(tg_render_map(0)==0);
 for(unsigned slot=256;slot<512;slot++)assert(pte_table()[slot]==0);
 assert(invl_count==4);
 memset(&job,0,sizeof(job));job.page_count=68;job.target_page_start=64;job.target_page_count=1;
 job.load_page_start=65;job.load_page_count=3;job.readonly_load_mask=3;job.executable_load_mask=4;
 for(unsigned i=0;i<job.page_count;i++)job.pages[i]=0x883100000ULL+(uint64_t)i*4096;
 assert(tg_render_map(&job)==0);
 assert(pte_table()[256+64]==(job.pages[64]|0x40000000000457ULL));
 assert(pte_table()[256+65]==(job.pages[65]|0x400000000004d7ULL));
 assert(pte_table()[256+66]==(job.pages[66]|0x400000000004d7ULL));
 assert(pte_table()[256+67]==(job.pages[67]|0x4d7ULL));
 assert(tg_render_map(0)==0&&invl_count==8);
 job.executable_load_mask=0;assert(tg_render_map(&job)==-82&&invl_count==8);
 job.executable_load_mask=4;job.page_count=TEGU_RENDER_PAGES+1;
 assert(tg_render_map(&job)==-82&&invl_count==8);
 job.page_count=TEGU_RENDER_PAGES;job.target_page_start=65;
 assert(tg_render_map(&job)==-82&&invl_count==8);
 job.target_page_start=64;job.pages[65]=job.pages[64];
 assert(tg_render_map(&job)==-82&&invl_count==8);
 job.pages[65]=0x883000000ULL+65u*4096u;
 pte_table()[256]=1;
 assert(tg_render_map(&job)==-81&&invl_count==8);pte_table()[256]=0;
 puts("isolated 256-page aperture mapping, LOAD RO/NX + RO/X permissions, teardown, and bounds PASS");
}
