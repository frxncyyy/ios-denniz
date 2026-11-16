#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* Production bridge tested against an independent transport model. It checks
 * packet/metadata/lifetime contracts; it does NOT emulate shader execution. */
#include "nord2_metal_bridge.h"
#include "tegu/pipeline_format.h"
#include "nord2_gpu_transport.h"
#include "nord2_metal_render.h"
#include "nord2_preload.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define COUNT 600
struct buffer {uint64_t va,size;unsigned char *data;unsigned mapped,active,released,exec,gpu_only,surface;};
struct fake {
 unsigned last_chain,last_preframe,last_preframe1,last_dcd_rt[2];
 uint32_t last_fmin,last_fmax; /* FRAGMENT_JOB bound words (tiles) */
 pthread_mutex_t mutex;struct buffer buffers[COUNT];uint32_t next;
 unsigned live,maps,closed,submitted,waited,bad_state,fail_alloc,fail_map,fail_submit,fail_wait,fail_release;
 unsigned geometry_fixture,heap_logs,before_logs,after_logs,log_bytes,nonzero_before_logs;
 unsigned sync_calls,fail_sync_at;
 /* kernel 0112 model (selectors 13/14); off = a pre-0112 kernel (Unsupported) */
 unsigned resv_model,resv_calls,resv_last_n,retire_calls,resv_order_ok;uint64_t resv_next,retire_last[2];uint32_t resv_handles[8];
 /* chained submission model: verify the multi-frame chains of one job */
 unsigned chain_model,last_frames,last_tjobs;uint64_t last_heaps[8];
 unsigned handle_limit; /* per-context handle cap (P8 256, P9 1024); 0 = unlimited */
 /* tiler matrix model */
 unsigned render_model,tm_logs,adopt_logs,draw_submits,heap_allocs,const_fs_seen,nosec_seen,mask_seen[4];
 uint64_t heap_va,last_cmd_va;uint32_t last_mask,last_chunk,last_heap_size;char adopt_text[240];
 uint64_t job;unsigned pending;const unsigned char *pipeline;size_t pipeline_size;
 /* kernel 0090 model: WAIT_SEQ, busy-consumed release, in-flight window */
 unsigned seq_model,seq_waits,inflight,window,fail_seq,busy_releases,max_inflight;
 /* Optional device time model (frame_bench FB_SUBMIT_US/FB_GPU_US; 0 = off):
  * every SUBMIT_JM takes submit_us of wall time (job preparation) and queues
  * gpu_us of GPU time behind the previous job; WAIT and WAIT_SEQ sleep
  * until the waited job's simulated completion. */
 unsigned submit_us,gpu_us;double gpu_free,done_at[256];
 unsigned seq0_notfound; /* pre-0108 kernel: WAIT_SEQ(0) = kIOReturnNotFound */
 /* kernel 0096 model: ALLOC bit1 GPU-only (no_gpu_only = older kernel that
  * refuses it), and imported surfaces with a separate CPU view that only an
  * acquire (SYNC to CPU) refreshes: data = what the GPU sees/wrote,
  * surf_cpu[sid] = the CPU's (possibly stale) lines. */
 unsigned no_gpu_only,gpu_only_allocs,refused_maps,surf_model,surf_acquires;
 unsigned char *surf_cpu[4];size_t surf_bytes[4];
 /* fault diagnostics: SEL_FAULT model (ctx MMU fault) and the bridge's NORD2_METAL fault lines */
 unsigned fault_queries,fault_lines;uint32_t mmu_status,js_status;uint64_t mmu_address;char fault_text[240];
 unsigned lost_calls,fault_wait; /* n2mb_ops.lost calls; WAIT answers FAULTED (5) for the next job */
};
#define KBUSY_TEST ((int)0xe00002d5u)
/* Chained-submission model: the listed BO holding a GPU address. */
static const unsigned char *fake_at(struct fake *f,const struct nord2_gpu_submit_request *r,uint64_t va){
 for(unsigned k=0;k<r->nr_handles;k++){struct buffer *b=&f->buffers[r->handles[k]];
  if(va>=b->va&&va+64<=b->va+b->size)return b->data+(va-b->va);}
 assert(0);return NULL;
}

static double fake_now(void) {struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (double)t.tv_sec+(double)t.tv_nsec*1e-9;}
static void fake_timed_submit(struct fake *f) {
 /* Wall time, not a spin: the host runs in a CPU-quota slice beside other jobs. */
 if(f->submit_us){struct timespec d={0,(long)f->submit_us*1000L};nanosleep(&d,0);}
 if(!f->gpu_us)return;
 double now=fake_now(),start=f->gpu_free>now?f->gpu_free:now;
 f->gpu_free=start+f->gpu_us*1e-6;f->done_at[f->job&255u]=f->gpu_free;
}
static void fake_wait_job(struct fake *f,uint64_t job) {
 if(!f->gpu_us||!job)return;
 double until=f->done_at[job&255u],now;
 while((now=fake_now())<until){struct timespec d={0,(long)((until-now)*1e9)+1};nanosleep(&d,0);}
}
static uint64_t get64(const void *v) {const unsigned char *p=v;return tegu_pipeline_word(p)|((uint64_t)tegu_pipeline_word(p+4)<<32);}
static void collect(struct fake *f,unsigned h) {struct buffer *b=&f->buffers[h];assert(b->active);free(b->data);b->data=NULL;b->active=0;f->live--;}
/* MALI_FRAMEBUFFER_POINTER: bit0 set, render_target_count-1 in bits 2..4
 * (arch9 FRAMEBUFFER_POINTER); single-target jobs are exactly va|1. */
static int rtc_ok(uint64_t p){return (p&1)&&((p>>2)&7)<4;}
static int call(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os) {
 struct fake *f=p;
 switch(s) {
 case NORD2_GPU_SEL_LOG:
  /* Plain log writes remain valid when a wait failed; no GPU memory read. */
  assert(!ni&&bytes&&size>0&&size<240);
  if(size>=17&&!memcmp(bytes,"NORD2_METAL heap+",17))f->heap_logs++;
  f->log_bytes+=(unsigned)size;assert(f->log_bytes<=16384);
  if(size>=18&&!memcmp(bytes,"NORD2_METAL fault ",18)){f->fault_lines++;memcpy(f->fault_text,bytes,size);f->fault_text[size-1]=0;}
  if(size>=27&&!memcmp(bytes,"NORD2_METAL geometry-before ",27)){
   f->before_logs++;
   char text[240];memcpy(text,bytes,size);text[size]=0;
   unsigned ordinal=0,nonzero=0;
   assert(sscanf(text,"NORD2_METAL geometry-before ordinal=%u sampled=65792 nonzero=%u",&ordinal,&nonzero)==2);
   assert(ordinal>=1&&ordinal<=8&&nonzero<=65792);
   f->nonzero_before_logs+=nonzero!=0;
  }
  if(size>=26&&!memcmp(bytes,"NORD2_METAL geometry-after ",26))f->after_logs++;
  if(size>=15&&!memcmp(bytes,"NORD2_METAL tm ",15)){f->tm_logs++;if(getenv("TM_DUMP")&&f->render_model)fwrite(bytes,1,size,stderr);}
  if(size>=21&&!memcmp(bytes,"NORD2_METAL tm adopt=",21)){f->adopt_logs++;memcpy(f->adopt_text,bytes,size);f->adopt_text[size]=0;}
  return 0;
 case NORD2_GPU_SEL_STATUS: {
  assert(!ni&&!bytes&&!size&&ob&&*os==sizeof(struct nord2_gpu_status));
  struct nord2_gpu_status status={.abi_version=2,.enabled=1,.powered=1,.arch_ok=1,.gpu_id=0x90800011,.tiler_features=0x809};
  if(f->bad_state)status.powered=0;memcpy(ob,&status,sizeof(status));return 0;
 }
 case NORD2_GPU_SEL_ALLOC: {
  assert(ni==2&&*no==3&&!size);if(f->fail_alloc){f->fail_alloc--;return -10;}
  if(in[1]>(f->no_gpu_only?1u:3u))return (int)0xe00002c2u; /* kIOReturnBadArgument */
  if(f->handle_limit&&f->live>=f->handle_limit)return (int)0xe00002beu; /* N2C_E_NO_HANDLE -> kIOReturnNoResources */
  unsigned h=++f->next;assert(h<COUNT);struct buffer *b=&f->buffers[h];
  b->size=(in[0]+16383)&~16383ull;b->va=0x1000000000ull+h*0x2000000ull; /* 32 MiB apart: room for a 16 MiB heap */
  if(in[0]>=(2u<<20))f->heap_allocs++;
  b->data=calloc(1,b->size);assert(b->data);b->active=1;b->exec=(unsigned)in[1];f->live++;
  b->gpu_only=(in[1]&2)!=0;b->surface=0;f->gpu_only_allocs+=b->gpu_only;
  out[0]=h;out[1]=b->va;out[2]=b->size;*no=3;return 0;
 }
 case NORD2_GPU_SEL_SYNC:
  assert(ni==2&&in[0]<COUNT&&in[1]==0&&!f->pending);
  if(f->surf_model&&f->buffers[in[0]].surface){unsigned sid=f->buffers[in[0]].surface;
   memcpy(f->surf_cpu[sid],f->buffers[in[0]].data,f->surf_bytes[sid]);f->surf_acquires++;} /* acquire: CPU sees GPU writes */
  return ++f->sync_calls==f->fail_sync_at?-16:0;
 case NORD2_GPU_SEL_RELEASE: {
  assert(ni==1&&in[0]<COUNT&&!size);if(f->fail_release){f->fail_release--;return -15;}
  struct buffer *b=&f->buffers[in[0]];assert(b->active&&!b->mapped);
  if(f->pending){b->released=1;if(f->seq_model){f->busy_releases++;return KBUSY_TEST;}}else collect(f,(unsigned)in[0]);return 0;
 }
 case NORD2_GPU_SEL_SUBMIT_JM: {
  assert(!ni&&size==160&&bytes&&*no==1);
  const struct nord2_gpu_submit_request *r=bytes;
  assert(r->version==2);
  if(r->kind==2){
   assert(r->nr_handles>=5&&r->heads[0]&&r->heads[1]);
   if(f->chain_model){
    /* JS1: tiler jobs 1..n, each depending on its predecessor; JS0: one
     * fragment job per frame, 1..F, barrier and dependency on the previous;
     * every frame's tiler heap (FBD -> its frame block -> TILER_HEAP base)
     * distinct. All addresses inside listed BOs. */
    unsigned steps=0;
    for(uint64_t h=r->heads[0];h;steps++){const unsigned char *jh=fake_at(f,r,h);uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
     assert((w4>>16)==steps+1&&(w5&0xffff)==steps&&steps<4096);h=get64(jh+24);}
    unsigned k=0;
    for(uint64_t h=r->heads[1];h;k++){const unsigned char *jh=fake_at(f,r,h);uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
     assert((w4>>16)==k+1&&((w4>>8)&1)==(k>0)&&(w5&0xffff)==k&&((w4>>1)&0x7f)==9&&k<8);
     uint64_t fbp=get64(jh+40)&~63ull;const unsigned char *fr=fake_at(f,r,fbp);uint64_t base=get64(fr+768+8);
     for(unsigned j=0;j<k;j++)assert(f->last_heaps[j]!=base);f->last_heaps[k]=base;h=get64(jh+24);}
    f->last_tjobs=steps;f->last_frames=k;f->draw_submits++;
    goto accept_render;
   }
   unsigned command=0;
   for(unsigned k=0;k<r->nr_handles;k++){
    unsigned h=r->handles[k];assert(h<COUNT&&f->buffers[h].active);
    if(r->heads[1]>=f->buffers[h].va&&r->heads[1]<f->buffers[h].va+f->buffers[h].size)command=h;
   }
   assert(command);struct buffer *c=&f->buffers[command];
   if(f->geometry_fixture){
    uint64_t base=get64(c->data+768+8),bottom=base+(f->geometry_fixture==2?UINT64_C(0x400000):65536);
    memcpy(c->data+768+16,&bottom,8);
    for(unsigned k=0;k<r->nr_handles;k++){
     struct buffer *heap=&f->buffers[r->handles[k]];
     if(heap->va==base){assert(heap->size==2u<<20);if(f->geometry_fixture!=3)memset(heap->data,0x37,65536);}
    }
   }
   assert(r->heads[1]==c->va+N2R_FRAGMENT);
   {/* Capture tiler configuration from the packet (v9.xml word offsets). */
    uint32_t w;memcpy(&w,c->data+512+8,4);f->last_mask=w&0x1fff;memcpy(&w,c->data+768,4);f->last_chunk=(w>>8)&3;
    assert((w&0xff)==0x29);memcpy(&f->last_heap_size,c->data+768+4,4);f->last_cmd_va=c->va;
    uint64_t base=get64(c->data+768+8);assert((f->geometry_fixture||get64(c->data+768+16)==base)&&get64(c->data+768+24)==base+f->last_heap_size);
    /* One persistent heap: it changes only after the previous one was
     * released (the tiler matrix's own mapped heap, kernel 0096). */
    if(f->heap_va&&base!=f->heap_va){unsigned live=0;for(unsigned k=1;k<=f->next;k++)live|=f->buffers[k].active&&f->buffers[k].va==f->heap_va;
     assert(!live);f->heap_va=0;}
    if(!f->heap_va)f->heap_va=base;assert(base==f->heap_va);
    unsigned found=0;for(unsigned k=0;k<r->nr_handles;k++){struct buffer *hb=&f->buffers[r->handles[k]];
     if(hb->va==base){found=1;assert(r->write_mask&(1u<<k));assert(f->last_heap_size<=hb->size);}}
    assert(found);
    if(f->last_mask==0x01)f->mask_seen[0]++;if(f->last_mask==0x82)f->mask_seen[1]++;if(f->last_mask==0xff)f->mask_seen[2]++;
    memcpy(&w,c->data+N2R_JOB+32,4);unsigned sec=(w>>18)&1;memcpy(&w,c->data+N2R_JOB+52,4);
    if(!sec){f->nosec_seen++;assert(w==16);for(unsigned k=0;k<64;k++)assert(!c->data[N2R_JOB+320+k]);}
    else assert((w&0xffff)==(w>>16)+16);
    uint64_t fs_code=get64(c->data+1600+8);
    for(unsigned k=0;k<r->nr_handles;k++){struct buffer *hb=&f->buffers[r->handles[k]];
     if(hb->va==fs_code&&hb->data[0]==192&&hb->data[5]==192&&hb->data[32]==124)f->const_fs_seen++;}
    f->draw_submits++;
    if(f->render_model){
     /* Model: only 2 MiB heap with 256 KiB chunks "rasterises" (green). */
     uint64_t rt=get64(c->data+128+32);int green=f->last_chunk==0&&f->last_heap_size==(2u<<20)&&f->last_mask==0x82;
     for(unsigned k=0;k<r->nr_handles;k++){struct buffer *hb=&f->buffers[r->handles[k]];
      if(hb->va==rt&&hb->size>=16384)for(unsigned i=0;i<4096;i++){uint32_t v=green?0xff00ff00u:0xffff0000u;memcpy(hb->data+4*i,&v,4);}}
    }
   }
   {uint64_t fbp=get64(c->data+N2R_FRAGMENT+40); /* tagged pointer: low bits carry the RT count */
    assert((fbp&~63ull)==c->va&&(fbp&63u)&&((fbp&63u)==1||rtc_ok(fbp)));}
   memcpy(&f->last_fmin,c->data+N2R_FRAGMENT+32,4);memcpy(&f->last_fmax,c->data+N2R_FRAGMENT+36,4);
   {/* Tiler chain inside the command buffer: every job at a block's JOB
     * offset, indices 1.. ascending along JOB_HEADER.next, each depending on
     * its predecessor, block 0 (the frame owner) included, ending at next=0. */
    uint64_t h=r->heads[0];unsigned steps=0,seen0=0;
    while(h){
     assert(h>=c->va&&h<c->va+c->size&&((h-c->va)%N2R_BLOCK)==N2R_JOB&&steps<=N2MB_BATCH_CAPACITY+1);
     const uint8_t *jh=c->data+(h-c->va);uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
     steps++;assert((w4>>16)==steps&&(w5&0xffff)==(steps>1?steps-1:0));
     if(h==c->va+N2R_JOB)seen0=1;
     h=get64(jh+24);
    }
    assert(seen0&&steps>=1);f->last_chain=steps;
   }
   {/* Pre-frame preload: FRAMEBUFFER_PARAMETERS word0 pre_frame_0 (bits 0-2,
     * pre_frame_1/post_frame NEVER) and frame_shader_dcds (word 6) at
     * another block's DCD array, whose job slot is not a job. */
    uint32_t w0;memcpy(&w0,c->data,4);uint64_t d=get64(c->data+24);f->last_preframe=w0&7;f->last_preframe1=(w0>>3)&7;
    f->last_dcd_rt[0]=f->last_dcd_rt[1]=0;
    if(w0&0x1ff){
     /* post_frame NEVER; pre_frame_1 only with pre_frame_0 (true-MRT LOAD of
      * a second target, DRAW 1 of the same array, mode equal to DRAW 0). */
     assert(!(w0&0x1c0)&&(w0&7)&&(!((w0>>3)&7)||((w0>>3)&7)==(w0&7)));
     assert(d>=c->va+N2R_BLOCK&&d<c->va+c->size&&((d-c->va)%N2R_BLOCK)==N2R_FRAME_DCDS);
     const uint8_t *dcd=c->data+(d-c->va);
     for(unsigned k=0;k<128;k++)assert(!(dcd-128)[k]&&!dcd[256+k]); /* no job header, DCD2 zero */
     for(unsigned slot=0;slot<2;slot++){
      const uint8_t *x=dcd+128*slot;
      if(!(slot?(w0>>3)&7:w0&7)){for(unsigned k=0;k<128;k++)assert(!x[k]);continue;}
      uint32_t f0,f1;memcpy(&f0,x,4);memcpy(&f1,x+4,4);unsigned mask=(f1>>16)&0xff;
      assert((f0&(1u<<9))&&mask&&!(mask&(mask-1))&&!(get64(x+8)&1)); /* clean fragment write, one RT, no vertex packet */
      f->last_dcd_rt[slot]=mask;
      uint64_t spd=get64(x+64+40); /* Shader Environment word 10 */
      assert(spd>=c->va+N2R_BLOCK&&spd<c->va+c->size&&((spd-c->va)%N2R_BLOCK)==1600);
      if(!slot)assert(spd-d==1600-N2R_FRAME_DCDS);
     }
    } else assert(!d);
   }
   accept_render:
   if(f->fail_submit){f->fail_submit--;return -12;}
   if(f->window&&f->inflight>=f->window)return KBUSY_TEST;
   f->inflight++;if(f->inflight>f->max_inflight)f->max_inflight=f->inflight;
   f->submitted++;f->pending=1;out[0]=++f->job;*no=1;fake_timed_submit(f);return 0;
  }
  assert(r->version==2&&r->kind==1&&r->nr_handles==3&&r->write_mask==4&&!r->heads[1]);
  unsigned hc=r->handles[0],hs=r->handles[1],hr=r->handles[2];
  assert(hc<COUNT&&hs<COUNT&&hr<COUNT);struct buffer *c=&f->buffers[hc],*shader=&f->buffers[hs],*res=&f->buffers[hr];
  assert(c->active&&shader->active&&res->active&&shader->exec&&!c->exec&&!res->exec);
  assert(r->heads[0]==c->va);
  /* SHADER_ENV at COMPUTE_JOB+64, offset32 onward: resource, program,
   * TLS, FAU. Test against independent word offsets from v9.xml. */
  assert(get64(c->data+104)==c->va+128);assert(get64(c->data+112)==c->va+192);
  uint64_t fau=get64(c->data+120);assert(fau==c->va+256);
  assert(tegu_pipeline_word(c->data+68)==17); /* real compiler reports33 FAU words */
  assert(tegu_pipeline_word(c->data+128)==0x80000018u);
  assert(tegu_pipeline_word(c->data+132)==0x1000);assert(get64(c->data+136)==shader->va);
  assert(get64(c->data+256)==res->va+32);assert(tegu_pipeline_word(c->data+264)==1024);assert(!tegu_pipeline_word(c->data+268));
  assert(tegu_pipeline_word(c->data+384)==0x12345678);assert(tegu_pipeline_word(c->data+512)==3);
  assert(!memcmp(shader->data,f->pipeline+64,f->pipeline_size-64));
  if(f->fail_submit){f->fail_submit--;return -12;}
  f->submitted++;f->pending=1;out[0]=++f->job;*no=1;return 0;
 }
 case 12: { /* WAIT_SEQ (kernel 0090) */
  if(!f->seq_model)return (int)0xe00002c7u;
  if(!in[0]){ /* the availability probe: 0108+ answers {4,0,0}; 0090-0107 refused seq 0 (N2C_E_NO_JOB) */
   assert(ni==2&&*no==3);
   if(f->seq0_notfound)return (int)0xe00002f0u;
   out[0]=4;out[1]=0;out[2]=0;return 0;
  }
  fake_wait_job(f,in[0]);
  if(f->gpu_us&&in[0]<f->job){ /* time model only: a pipelined finish reaps jobs <= seq (frame_bench CA loop) */
   assert(ni==2&&*no==3&&in[1]<=5000);
   uint64_t first=f->job-f->inflight;unsigned reaped=in[0]>first?(unsigned)(in[0]-first):0u;
   if(reaped>f->inflight)reaped=f->inflight;
   f->seq_waits++;f->inflight-=reaped;out[0]=4;out[1]=reaped;out[2]=0;return 0;
  }
  assert(ni==2&&*no==3&&in[1]<=5000&&in[0]==f->job);
  f->seq_waits++;out[1]=f->inflight;
  if(f->fail_seq){f->fail_seq--;out[0]=5;out[2]=f->job-f->inflight+1;
   if(ob&&os&&*os>=sizeof(struct nord2_gpu_fault_info)){struct nord2_gpu_fault_info fi={0};fi.job_id_lo=(uint32_t)out[2];fi.job_id_hi=(uint32_t)(out[2]>>32);
    fi.state=5;fi.status=f->js_status;fi.fault_status=f->mmu_status;fi.fault_address=f->mmu_address;memcpy(ob,&fi,sizeof(fi));*os=sizeof(fi);}}
  else{out[0]=4;out[2]=0;}
  f->pending=0;f->inflight=0;
  for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active&&f->buffers[h].released){f->buffers[h].released=0;collect(f,h);}
  return 0;
 }
 case NORD2_GPU_SEL_WAIT: {
  assert(ni==2&&in[0]==f->job&&in[1]==5000&&*no==1);
  f->waited++;fake_wait_job(f,in[0]);if(f->fail_wait){f->fail_wait--;return -13;}
  if(f->bad_state){out[0]=2;return 0;}f->pending=0;f->inflight=0;
  if(f->fault_wait){f->fault_wait--;out[0]=5; /* terminal and reaped, as the kernel answers a faulted job */
   if(ob&&os&&*os>=sizeof(struct nord2_gpu_fault_info)){struct nord2_gpu_fault_info fi={0};fi.job_id_lo=(uint32_t)in[0];fi.job_id_hi=(uint32_t)(in[0]>>32);
    fi.state=5;fi.status=f->js_status;fi.fault_status=f->mmu_status;fi.fault_address=f->mmu_address;memcpy(ob,&fi,sizeof(fi));*os=sizeof(fi);}
   for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active&&f->buffers[h].released){f->buffers[h].released=0;collect(f,h);}
   return 0;}
  out[0]=4;return 0;
 }
 case NORD2_GPU_SEL_SURFACE_RESERVE: {
  if(!f->resv_model)return (int)0xe00002c7u;
  assert(ni==1&&*no==1&&in[0]<=8&&size==in[0]*4&&(!in[0]||bytes));
  f->resv_calls++;f->resv_last_n=(unsigned)in[0];if(in[0])memcpy(f->resv_handles,bytes,size);
  for(unsigned k=0;k<in[0];k++)assert(f->resv_handles[k]<COUNT&&f->buffers[f->resv_handles[k]].active);
  out[0]=in[0]?++f->resv_next:0;return 0;
 }
 case NORD2_GPU_SEL_FAULT: { /* the context's MMU fault (kernel ctxs[].fault_status/address) */
  assert(!ni&&!size&&ob&&os&&*os>=sizeof(struct nord2_gpu_fault_info));
  struct nord2_gpu_fault_info fi={0};fi.fault_status=f->mmu_status;fi.fault_address=f->mmu_address;
  memcpy(ob,&fi,sizeof(fi));*os=sizeof(fi);f->fault_queries++;return 0;
 }
 case NORD2_GPU_SEL_SURFACE_RETIRE:
  if(!f->resv_model)return (int)0xe00002c7u;
  assert(ni==2&&in[0]&&in[0]<=f->resv_next);f->retire_calls++;f->retire_last[0]=in[0];f->retire_last[1]=in[1];
  f->resv_order_ok=!in[1]||in[1]==f->job;return 0;
 default:
  if(s==NORD2_GPU_SEL_IMPORT_SURFACE&&f->surf_model){
   assert(ni==4&&*no==3&&in[0]>0&&in[0]<4&&f->surf_cpu[in[0]]&&in[2]==f->surf_bytes[in[0]]);
   unsigned h=++f->next;assert(h<COUNT);struct buffer *b=&f->buffers[h];
   b->size=(in[2]+16383)&~16383ull;b->va=0x1000000000ull+h*0x2000000ull;
   b->data=calloc(1,b->size);assert(b->data);memcpy(b->data,f->surf_cpu[in[0]],in[2]);
   b->active=1;b->exec=0;b->gpu_only=0;b->surface=(unsigned)in[0];f->live++;
   out[0]=h;out[1]=b->va;out[2]=in[2];*no=3;return 0;
  }
  assert(!"unexpected kernel selector");return -99;
 }
}
static int map(void *p,unsigned h,uint64_t *address,uint64_t *size) {
 struct fake *f=p;if(f->fail_map){f->fail_map--;return -11;}
 assert(h<COUNT);struct buffer *b=&f->buffers[h];assert(b->active&&!b->mapped);
 if(b->gpu_only){f->refused_maps++;return (int)0xe00002f0u;} /* kIOReturnNotFound: no CPU mapping */
 b->mapped=1;f->maps++;*address=(uintptr_t)b->data;*size=b->size;return 0;
}
static int unmap(void *p,unsigned h,uint64_t address) {
 struct fake *f=p;assert(h<COUNT);struct buffer *b=&f->buffers[h];
 assert(b->active&&b->mapped&&address==(uintptr_t)b->data);b->mapped=0;f->maps--;return 0;
}
static int close_raw(void *p) {
 struct fake *f=p;assert(!f->maps);f->closed=1;
 for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active)collect(f,h);
 return 0;
}
static void fake_lost(void *p){((struct fake *)p)->lost_calls++;}
static void lock(void *p){assert(!pthread_mutex_lock(&((struct fake *)p)->mutex));}
static void unlock(void *p){assert(!pthread_mutex_unlock(&((struct fake *)p)->mutex));}
static uintptr_t open_bridge(struct fake *f) {
 assert(!pthread_mutex_init(&f->mutex,NULL));struct n2mb_ops ops={.opaque=f,.call=call,.map=map,.unmap=unmap,.close=close_raw,.lock=lock,.unlock=unlock,.lost=fake_lost};
 uintptr_t b=n2mb_open(&ops);assert(b);uint64_t caps[4];unsigned n=4;
 assert(!n2mb_scalar(b,0,NULL,0,caps,&n)&&n==4&&(caps[1]&8));return b;
}
static void close_bridge(uintptr_t b,struct fake *f){assert(!n2mb_close(b)&&f->closed&&!f->live&&!f->maps);assert(!pthread_mutex_destroy(&f->mutex));}
static uint64_t pipeline(uintptr_t b,struct fake *f){uint64_t h=0;unsigned n=1;assert(!n2mb_method(b,5,NULL,0,f->pipeline,f->pipeline_size,&h,&n,NULL,NULL)&&n==1&&h>>63);return h;}
static uint64_t resource(uintptr_t b){uint64_t bytes=2048,out[2];unsigned n=2;assert(!n2mb_scalar(b,1,&bytes,1,out,&n)&&n==2&&out[1]==16384);return out[0];}
static int dispatch(uintptr_t b,uint64_t pipe,uint64_t resource,unsigned invalid) {
 unsigned char wire[272]={0};tegu_pipeline_put(wire,1);tegu_pipeline_put(wire+4,3);tegu_pipeline_put(wire+8,1);tegu_pipeline_put(wire+12,1);
 tegu_pipeline_put(wire+16,(uint32_t)resource);tegu_pipeline_put(wire+20,32);tegu_pipeline_put(wire+24,invalid?20000:1024);tegu_pipeline_put(wire+144,0x12345678);
 uint64_t out[2]={0};unsigned n=2;int e=n2mb_method(b,7,&pipe,1,wire,sizeof(wire),out,&n,NULL,NULL);
 if(!e)assert(n==2&&out[0]&&out[0]==out[1]);return e;
}
struct thread_arg{uintptr_t b;uint64_t pipe,resource;};
static void *thread(void *v){struct thread_arg *a=v;for(unsigned i=0;i<20;i++)assert(!dispatch(a->b,a->pipe,a->resource,0));return NULL;}
int main(int argc,char **argv) {
 assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);unsigned char blob[8192];size_t size=fread(blob,1,sizeof(blob),file);assert(!ferror(file));fclose(file);
 struct tegu_pipeline_info pi;assert(!tegu_pipeline_parse(blob,size,&pi)&&pi.push_words==33&&pi.preload==0x1000000000000000ull);
 for(unsigned scenario=0;scenario<8;scenario++) {
  struct fake f={.pipeline=blob,.pipeline_size=size,.job=1ull<<32};uintptr_t b=open_bridge(&f);
  uint64_t p=pipeline(b,&f),r=resource(b);assert(f.live==2&&f.maps==1);
  unsigned n=0;assert(n2mb_scalar(b,2,&p,1,NULL,&n)); /* cannot release pipeline as buffer */
  assert(dispatch(b,p,r,1)&&!f.submitted); /* bounded span validation before DMA */
  uint64_t cpu=0,mapped=0;assert(!n2mb_map(b,(unsigned)r,0,&cpu,&mapped,1)&&mapped==16384);
  {uint64_t again=0,size2=0;assert(!n2mb_map(b,(unsigned)r,0,&again,&size2,1)&&again==cpu&&size2==mapped);} /* idempotent: one shared mapping */
  assert(f.maps==2);assert(!n2mb_unmap(b,(unsigned)r,0,cpu));
  if(scenario==1)f.fail_alloc=1;if(scenario==2)f.fail_map=1;if(scenario==3)f.fail_submit=1;
  if(scenario==4)f.fail_wait=1;if(scenario==5)f.bad_state=1;if(scenario==6)f.fail_release=1;
  int e=dispatch(b,p,r,0);
  if(scenario==0||scenario==7)assert(!e);else assert(e);
  if(scenario==4||scenario==5) {
   unsigned previous=f.submitted;assert(dispatch(b,p,r,0)&&f.submitted==previous);
   assert(f.live==3); /* uncertain command backing stays retained until close */
  }
  if(scenario==7) {
   struct thread_arg a={b,p,r};pthread_t threads[4];
   for(unsigned i=0;i<4;i++)assert(!pthread_create(&threads[i],NULL,thread,&a));
   for(unsigned i=0;i<4;i++)assert(!pthread_join(threads[i],NULL));assert(f.submitted==81);
  }
  /* None of donor's CSF or surface selectors may alias kernel raw JM/sync. */
  for(unsigned s=9;s<=20;s++){uint64_t in[4]={0},out[2]={0};n=2;assert(n2mb_method(b,s,in,4,blob,size,out,&n,NULL,NULL));}
  if(scenario==0) {
   n=0;assert(!n2mb_scalar(b,6,&p,1,NULL,&n));assert(n2mb_scalar(b,6,&p,1,NULL,&n));
   assert(!n2mb_scalar(b,2,&r,1,NULL,&n));assert(n2mb_scalar(b,2,&r,1,NULL,&n));
   /* C6s: the released client BO was CPU-mapped, so it is pooled (kept
    * allocated and mapped) for the next ALLOC of its size class. */
   {uint64_t cp[4];n2mb_client_pool_stats(b,cp);assert(f.live==1&&cp[3]==1&&cp[2]==1);}
  }
  close_bridge(b,&f);
 }

 for(unsigned fault=0;fault<2;fault++){
  struct fake f={0};uintptr_t b=open_bridge(&f);uint64_t src=resource(b),dst=resource(b);
  for(unsigned i=0;i<2048;i++)f.buffers[src].data[i]=(unsigned char)(i*7);
  if(fault)f.fail_map=1;
  int rc=n2mb_copy(b,src,16,dst,1024);
  if(fault)assert(rc);else{
   assert(!rc&&!memcmp(f.buffers[dst].data,f.buffers[src].data+16,1024));
   memset(f.buffers[src].data,0,2048);assert(f.buffers[dst].data[1]==119);
  }
  assert(n2mb_copy(b,src,0,src,1));assert(n2mb_copy(b,src,UINT64_MAX,dst,1));
  assert(n2mb_copy(b,src,0,dst,16385));assert(n2mb_copy(b,src,0,dst,0));
  assert(n2mb_copy(b,999,0,dst,1));close_bridge(b,&f);
 }
 puts("PASS MRT pre-draw transfer: independent content, bounds, mapping failure, retained lifetime");
 for(unsigned load=0;load<2;load++)for(unsigned fault=0;fault<5;fault++){
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=load?1:2,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=r,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  if(fault==1)f.fail_alloc=1;if(fault==2)f.fail_map=1;if(fault==3)f.fail_submit=1;if(fault==4)f.fail_wait=1;
  uint64_t out[2];unsigned n=2;int e=n2mb_draw(b,&n2_preload_shader,&q,out,&n);
  /* target + the context's persistent tiler heap */
  /* target + persistent tiler heap + the retained shader-code cache */
  if(!fault){assert(!e&&f.submitted==1&&f.waited==1&&f.live==2+n2mb_code_cache_count(b)+n2mb_cmdpool_count(b)&&n2mb_code_cache_count(b)==2&&f.heap_allocs==1);n=0;assert(!n2mb_scalar(b,2,&r,1,NULL,&n));}
  else assert(e);
  close_bridge(b,&f);
 }
 {/* LOAD without feedback: the preload samples the target in place (no
    * per-draw snapshot copy); shader code is uploaded once and reused. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  uint64_t out[2];unsigned n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  unsigned allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  assert(f.next==allocs); /* no snapshot, no code upload, command block reused from the pool */
  assert(f.live==3+n2mb_code_cache_count(b)+n2mb_cmdpool_count(b)&&n2mb_code_cache_count(b)==2&&f.submitted==2);
  /* Framebuffer fetch (own pixel) under LOAD: also in place, no snapshot. */
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_ATTACHMENT};
  allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));assert(f.next==allocs);
  /* Sampling the target itself still takes the bounded snapshot. */
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=r,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  allocs=f.next;uint64_t snaps=n2mb_snapshot_copies(b);n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));assert(f.next==allocs+1&&n2mb_snapshot_copies(b)==snaps+1); /* the snapshot */
  /* c6o blur pyramid: rendering level L (here bytes 8192..) while sampling
   * another byte range of the same BO (level L-1) needs no snapshot; a
   * window touching the target's rows does. */
  q.target_offset=8192;q.bindings[0].memory.offset=0; /* 16x16 BGRA8: 1024 bytes each */
  allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));assert(f.next==allocs);
  q.bindings[0].memory.offset=8192-512;allocs=f.next;snaps=n2mb_snapshot_copies(b);n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  assert(f.next==allocs&&n2mb_snapshot_copies(b)==snaps+1); /* C6s: the snapshot BO is reused */
  q.bindings[0].memory.offset=8192+1024; /* just past the target rows */
  allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));assert(f.next==allocs);
  q.target_offset=8192+32;n=2;assert(n2mb_draw(b,&n2_preload_shader,&q,out,&n)); /* n2r: 64-byte target base only */
  close_bridge(b,&f);
 }
 puts("PASS LOAD preload and framebuffer fetch read the target in place; shader code cached across draws");
 {/* Render-pass batching: N draws = one job (one fragment job, chained tiler
    * jobs, preload once). Refusals happen before submission. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q[4];
  for(unsigned k=0;k<4;k++){
   q[k]=(struct tegu_generic_draw_request){.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
   q[k].bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
   q[k].scissor[0]=k;q[k].scissor[2]=16-k;
  }
  struct n2mb_batch_draw dr[4];for(unsigned k=0;k<4;k++)dr[k]=(struct n2mb_batch_draw){&n2_preload_shader,&q[k],NULL,NULL};
  uint64_t out[2];unsigned n=2;unsigned allocs=f.next;
  assert(!n2mb_draw_batch(b,dr,4,out,&n)&&f.submitted==1&&f.waited==1&&f.last_chain==5); /* preload + 4 draws */
  assert(f.next-allocs==4); /* heap (first use) + one command block + VS/FS code, shared by all 4 draws */
  allocs=f.next;
  q[0].load_action=2;n=2;assert(!n2mb_draw_batch(b,dr,3,out,&n)&&f.submitted==2&&f.last_chain==3); /* CLEAR: no preload */
  assert(f.next-allocs==0); /* the command block is reused from the pool */
  q[0].load_action=1;
  q[2].load_action=2;n=2;assert(n2mb_draw_batch(b,dr,3,out,&n)==(int)0xe00002c2u&&f.submitted==2);q[2].load_action=1; /* mid-pass clear */
  q[1].bindings[0].kind=TEGU_GENERIC_DRAW_ATTACHMENT;q[1].bindings[0].memory=(struct tegu_generic_draw_memory){0};
  n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)==(int)0xe00002c2u&&f.submitted==2); /* framebuffer fetch */
  {/* Tile-buffer fetch (LD_TILE) sees earlier draws of the job: batches. */
   struct tegu_generic_draw_shader tile=n2_preload_shader;tile.flags=TEGU_GENERIC_SHADER_TILE_FETCH;
   struct tegu_generic_draw_request tq=q[0];tq.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_TILE_ATTACHMENT};
   struct n2mb_batch_draw td[3]={dr[0],{&tile,&tq,NULL,NULL},{&tile,&tq,NULL,NULL}};
   n=2;assert(!n2mb_draw_batch(b,td,3,out,&n)&&f.submitted==3&&f.last_chain==4&&n2mb_batch_last_count(b)==3);
   struct n2mb_batch_draw bad[2]={dr[0],{&n2_preload_shader,&tq,NULL,NULL}}; /* tile binding without LD_TILE program */
   n=2;assert(n2mb_draw_batch(b,bad,2,out,&n)==(int)0xe00002c2u&&f.submitted==3);
   f.submitted=2; /* later checks count submissions absolutely */
  }
  q[1].bindings[0]=q[0].bindings[0];q[1].bindings[0].memory.token=r;
  n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)==(int)0xe00002c2u&&f.submitted==2); /* target sampled */
  q[1].bindings[0]=q[0].bindings[0];q[1].row_bytes=128;
  n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)==(int)0xe00002c2u&&f.submitted==2); /* different target layout */
  q[1].row_bytes=64;f.fail_submit=1;n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)&&f.submitted==2);
  n=2;assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.submitted==3); /* failure did not break the bridge */
  static struct n2mb_batch_draw many[N2MB_BATCH_CAPACITY+1];for(unsigned k=0;k<=N2MB_BATCH_CAPACITY;k++)many[k]=dr[1];many[0]=dr[0];
  n=2;assert(n2mb_draw_batch(b,many,N2MB_BATCH_CAPACITY+1,out,&n)==(int)0xe00002c2u);
  /* Default runtime cap: a longer request submits its first N2MB_BATCH_MAX draws. */
  assert(n2mb_batch_capacity(b,0)==N2MB_BATCH_MAX);
  n=2;assert(!n2mb_draw_batch(b,many,N2MB_BATCH_MAX+1,out,&n)&&f.last_chain==N2MB_BATCH_MAX+1&&n2mb_batch_last_count(b)==N2MB_BATCH_MAX);
  n=2;assert(!n2mb_draw_batch(b,many,N2MB_BATCH_MAX,out,&n)&&f.last_chain==N2MB_BATCH_MAX+1);
  /* Raised cap (device A/B): 100 small draws in one job. */
  assert(n2mb_batch_capacity(b,100)==N2MB_BATCH_MAX&&n2mb_batch_capacity(b,1)==100&&n2mb_batch_capacity(b,N2MB_BATCH_CAPACITY+1)==100);
  n=2;assert(!n2mb_draw_batch(b,many,N2MB_BATCH_CAPACITY,out,&n)&&f.last_chain==101&&n2mb_batch_last_count(b)==100);
  /* Summed geometry: 48 KiB draws stop at 32 per job whatever the cap. */
  {struct tegu_generic_draw_shader big=n2_preload_shader;big.varying_bytes=48;
   static struct tegu_generic_draw_request gq[40];static struct n2mb_batch_draw gd[40];
   for(unsigned k=0;k<40;k++){gq[k]=q[k?1:0];gq[k].vertex_count=768;gd[k]=(struct n2mb_batch_draw){&big,&gq[k],NULL,NULL};}
   assert((16u+48u)*768u==TEGU_GENERIC_DRAW_GEOMETRY_BUDGET);
   n=2;assert(!n2mb_draw_batch(b,gd,40,out,&n)&&n2mb_batch_last_count(b)==32&&f.last_chain==33);}
  {/* C6t (device 0xE00002C2 at n2mb_draw, n2r site 265): AIR compiles the CA
    * fixed_vert_lph_spc pair with 48 varying bytes (m2v: 16/32), and the
    * stale 0xc000 per-draw budget of the old 64 KiB geometry heap refused
    * every draw over 768 vertices. Varyings come from the >=2 MiB tiler heap;
    * one draw is bounded by MAX_VERTICES and 64 varying bytes, a batch still
    * by the summed N2MB_BATCH_GEOMETRY. The refusal names its inner check. */
   struct tegu_generic_draw_shader air=n2_preload_shader;air.varying_bytes=48;
   struct tegu_generic_draw_request big=q[0];big.vertex_count=900;
   n=2;int rc=n2mb_draw(b,&air,&big,out,&n);int site=0,line=0;site=n2mb_last_failure(b,&line);
   if(rc)fprintf(stderr,"AIR-varying 900-vertex draw refused rc=%x site=%d n2r=%d\n",(unsigned)rc,site,line);
   assert(!rc);
   air.varying_bytes=64;big.vertex_count=TEGU_GENERIC_DRAW_MAX_VERTICES/3*3;n=2;assert(!n2mb_draw(b,&air,&big,out,&n)); /* the largest single triangle list */
   big.vertex_count=(TEGU_GENERIC_DRAW_MAX_VERTICES/3+1)*3;n=2;assert(n2mb_draw(b,&air,&big,out,&n)==(int)0xe00002c2u);
   (void)n2mb_last_failure(b,&line);assert(line>100000); /* the inner gd_request_ok site, not 265 */
   static struct tegu_generic_draw_request hq[3];static struct n2mb_batch_draw hd[3];
   for(unsigned k=0;k<3;k++){hq[k]=q[k?1:0];hq[k].vertex_count=900;hd[k]=(struct n2mb_batch_draw){&air,&hq[k],NULL,NULL};}
   air.varying_bytes=48;n=2;assert(!n2mb_draw_batch(b,hd,3,out,&n)&&n2mb_batch_last_count(b)==3);}
  assert(n2mb_batch_capacity(b,N2MB_BATCH_MAX)==100);
  assert(f.live==3+n2mb_code_cache_count(b)+n2mb_cmdpool_count(b)); /* two resources + heap + cached code */
  close_bridge(b,&f);
 }
 puts("PASS render-pass batching: one job per segment, preload once, exact refusals before submission");
 {/* Pre-frame preload (n2mb_preframe 1): LOAD passes carry the colour
    * preload as FRAMEBUFFER_PARAMETERS pre_frame_0 INTERSECT DCD; no preload
    * job is chained, single draws and batches alike. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  assert(n2mb_preframe(b,1)==0&&n2mb_preframe(b,2)==1);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  uint64_t out[2];unsigned n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  assert(f.last_chain==1&&f.last_preframe==2&&n2mb_code_cache_count(b)==3); /* draw VS/FS + frame FS */
  unsigned allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));assert(f.next==allocs); /* pooled command block */
  /* Feedback snapshot: the frame preload reads the snapshot copy. */
  q.bindings[0].memory.token=r;allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  assert(f.next==allocs+1&&f.last_chain==1&&f.last_preframe==2);q.bindings[0].memory.token=t; /* the snapshot */
  q.load_action=2;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&f.last_chain==1&&f.last_preframe==0);q.load_action=1;
  struct tegu_generic_draw_request bq[4];struct n2mb_batch_draw dr[4];
  for(unsigned k=0;k<4;k++){bq[k]=q;bq[k].scissor[0]=k;bq[k].scissor[2]=16-k;dr[k]=(struct n2mb_batch_draw){&n2_preload_shader,&bq[k],NULL,NULL};}
  n=2;assert(!n2mb_draw_batch(b,dr,4,out,&n)&&f.last_chain==4&&f.last_preframe==2);
  bq[0].load_action=2;n=2;assert(!n2mb_draw_batch(b,dr,3,out,&n)&&f.last_chain==3&&f.last_preframe==0);bq[0].load_action=1;
  struct n2mb_batch_draw many[N2MB_BATCH_MAX];for(unsigned k=0;k<N2MB_BATCH_MAX;k++)many[k]=dr[k?1:0];
  n=2;assert(!n2mb_draw_batch(b,many,N2MB_BATCH_MAX,out,&n)&&f.last_chain==N2MB_BATCH_MAX&&f.last_preframe==2);
  /* Mode 0 restores the chained preload job. */
  assert(n2mb_preframe(b,0)==1);n=2;assert(!n2mb_draw_batch(b,dr,4,out,&n)&&f.last_chain==5&&f.last_preframe==0);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&f.last_chain==2&&f.last_preframe==0);
  close_bridge(b,&f);
 }
 puts("PASS pre-frame preload: INTERSECT DCD array, no preload job (single, snapshot, batch, max batch), mode switch");
 {/* Fragment-job tile bound (n2mb_extent): Load/DontCare jobs cover the
    * union of their draws' scissor boxes in 16-pixel tiles; Clear keeps the
    * whole target; mode 0 restores the full bound. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  unsigned def=n2mb_extent(b,2);
  struct tegu_generic_draw_request q[2];struct n2mb_batch_draw dr[2];
  for(unsigned k=0;k<2;k++){
   q[k]=(struct tegu_generic_draw_request){.width=64,.height=64,.row_bytes=256,.load_action=1,.vertex_count=3,.binding_count=1,.target_token=r,.target_length=16384};
   q[k].bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
   dr[k]=(struct n2mb_batch_draw){&n2_preload_shader,&q[k],NULL,NULL};
  }
  q[0].scissor[0]=4;q[0].scissor[1]=20;q[0].scissor[2]=8;q[0].scissor[3]=8;
  q[1].scissor[0]=40;q[1].scissor[1]=2;q[1].scissor[2]=10;q[1].scissor[3]=30;
  uint64_t out[2];unsigned n=2;
  assert(n2mb_extent(b,1)==def);
  assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.last_fmin==0&&f.last_fmax==(3u|(1u<<16)));
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q[0],out,&n)&&f.last_fmin==(0u|(1u<<16))&&f.last_fmax==(0u|(1u<<16)));
  q[1].scissor[0]=20;q[1].scissor[1]=36;q[1].scissor[2]=8;q[1].scissor[3]=8;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q[1],out,&n)&&f.last_fmin==(1u|(2u<<16))&&f.last_fmax==(1u|(2u<<16)));
  q[1].load_action=0;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q[1],out,&n)&&f.last_fmin==(1u|(2u<<16)));
  q[1].load_action=2;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q[1],out,&n)&&f.last_fmin==0&&f.last_fmax==(3u|(3u<<16)));
  q[1].load_action=1;q[0].load_action=2;
  n=2;assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.last_fmin==0&&f.last_fmax==(3u|(3u<<16))); /* Clear owner: full */
  q[0].load_action=1; /* a draw clipped by its viewport adds nothing */
  q[1].viewport_valid=1;q[1].viewport[0]=100;q[1].viewport[1]=0;q[1].viewport[2]=10;q[1].viewport[3]=10;q[1].viewport[4]=0;q[1].viewport[5]=1;
  n=2;assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.last_fmin==(0u|(1u<<16))&&f.last_fmax==(0u|(1u<<16)));
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q[1],out,&n)&&f.last_fmin==0&&f.last_fmax==(3u|(3u<<16)));
  assert(n2mb_preframe(b,1)==0); /* pre-frame INTERSECT preload inside the bound */
  n=2;assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.last_preframe==2&&f.last_fmin==(0u|(1u<<16)));
  {uint64_t ts[3];n2mb_tile_stats(b,ts);assert(ts[0]==9&&ts[1]==9*16&&ts[2]==8+1+1+1+16+16+1+16+1);} /* 64x64 = 16 tiles */
  assert(n2mb_extent(b,0)==1);
  n=2;assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.last_fmin==0&&f.last_fmax==(3u|(3u<<16)));
  {uint64_t ts[3];n2mb_tile_stats(b,ts);assert(ts[0]==10&&ts[1]==160&&ts[2]==61+16);}
  close_bridge(b,&f);
 }
 {/* True-MRT LOAD (pre-frame only): one DCD per loaded target, DRAW 0/1,
    * each writing exactly its target; Clear targets need no DCD; more than
    * two loads, MSAA or preframe 0 refuse before submission. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);
  uint64_t r0=resource(b),r1=resource(b),r2=resource(b),t=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r0,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  struct n2mb_render_target rt[3];uint64_t tok[3]={r0,r1,r2};
  for(unsigned i=0;i<3;i++)rt[i]=(struct n2mb_render_target){.token=tok[i],.offset=0,.length=16384,.format=i==1?70:80,.load_action=1,.write_mask=15,.row_bytes=64,.width=16,.height=16};
  struct n2mb_render_targets ts={.version=N2MB_RENDER_TARGETS_VERSION,.count=2,.targets=rt};
  struct n2r_texture_resources tr0={N2R_TEXTURE_RESOURCES_VERSION,0,NULL};
  uint64_t out[2];unsigned n=2;unsigned subs=f.submitted;
  assert(n2mb_preframe(b,0)==0);
  assert(n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n)&&f.submitted==subs); /* preframe 0: refused */
  assert(n2mb_preframe(b,1)==0);
  n=2;assert(!n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n)&&f.submitted==subs+1);
  assert(f.last_chain==1&&f.last_preframe==2&&f.last_preframe1==2&&f.last_dcd_rt[0]==1&&f.last_dcd_rt[1]==2);
  rt[0].load_action=2;n=2;assert(!n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n));
  assert(f.last_chain==1&&f.last_preframe==1&&f.last_preframe1==0&&f.last_dcd_rt[0]==2); /* Clear forces ALWAYS */
  rt[0].load_action=0;n=2;assert(!n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n));
  assert(f.last_preframe==2&&f.last_dcd_rt[0]==2&&f.last_preframe1==0);
  rt[1].load_action=0;n=2;assert(!n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n)&&f.last_preframe==0);
  rt[0].load_action=rt[1].load_action=1;ts.count=3;subs=f.submitted;
  n=2;assert(n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n)&&f.submitted==subs); /* three loads */
  rt[1].load_action=2;n=2;assert(!n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n));
  assert(f.last_dcd_rt[0]==1&&f.last_dcd_rt[1]==4&&f.last_preframe==1&&f.last_preframe1==1);
  /* Discard (store DontCare) target flag: accepted (bit0), others refused. */
  rt[1].reserved=N2MB_TARGET_DISCARD;n=2;assert(!n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n));
  rt[1].reserved=2;n=2;subs=f.submitted;assert(n2mb_draw_v4(b,&n2_preload_shader,&q,&tr0,NULL,&ts,out,&n)&&f.submitted==subs);
  rt[1].reserved=0;
  {/* True-MRT batch: one job for draws sharing the target set. */
   rt[0].load_action=1;rt[1].load_action=2;ts.count=2;
   struct n2mb_render_target mrt[2][2];struct n2mb_render_targets mts[3];struct n2mb_batch_draw md[3];
   struct tegu_generic_draw_request mq[3];const struct n2mb_render_targets *mtp[3];
   for(unsigned k=0;k<3;k++){
    mq[k]=q;mq[k].scissor[0]=k;mq[k].scissor[2]=16-k;
    if(k){memcpy(mrt[k-1],rt,sizeof(mrt[k-1]));mrt[k-1][0].load_action=mrt[k-1][1].load_action=1;mrt[k-1][k-1].write_mask=0;
     mts[k]=(struct n2mb_render_targets){.version=N2MB_RENDER_TARGETS_VERSION,.count=2,.targets=mrt[k-1]};}
    else mts[0]=ts;
    md[k]=(struct n2mb_batch_draw){&n2_preload_shader,&mq[k],&tr0,NULL};mtp[k]=&mts[k];
   }
   subs=f.submitted;n=2;assert(!n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs+1&&n2mb_batch_last_count(b)==3);
   assert(f.last_chain==3&&f.last_preframe==1&&f.last_preframe1==0&&f.last_dcd_rt[0]==1);
   rt[1].load_action=1;n=2;assert(!n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.last_chain==3&&f.last_preframe==2&&f.last_preframe1==2&&
    f.last_dcd_rt[0]==1&&f.last_dcd_rt[1]==2);
   rt[0].load_action=rt[1].load_action=2;n=2;assert(!n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.last_chain==3&&f.last_preframe==0);
   subs=f.submitted;
   mrt[1][1].load_action=2;n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs);mrt[1][1].load_action=1; /* mid-job clear */
   mrt[1][1].token=r2;n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs);mrt[1][1].token=r1; /* other set */
   /* Discard: a DontCare member of a discarded target may carry load 0; a
    * member storing a target the frame (draw 0) discards may not join. */
   mrt[1][1].load_action=0;n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs);
   mrt[1][1].reserved=N2MB_TARGET_DISCARD; /* the frame storing rt1 is conservative */
   n=2;assert(!n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs+1&&n2mb_batch_last_count(b)==3);subs=f.submitted;
   rt[1].reserved=N2MB_TARGET_DISCARD;mrt[0][1].reserved=N2MB_TARGET_DISCARD;
   n=2;assert(!n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs+1&&n2mb_batch_last_count(b)==3);subs=f.submitted;
   mrt[1][1].load_action=1;mrt[1][1].reserved=0;n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs); /* stores, frame discards */
   rt[1].reserved=mrt[0][1].reserved=0;
   mtp[2]=NULL;n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs);mtp[2]=&mts[2];
   md[1].resources=NULL;n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs);md[1].resources=&tr0;
   mq[1].bindings[0].memory.token=r1;n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs);mq[1].bindings[0].memory.token=t;
   {struct tegu_generic_draw_shader tile=n2_preload_shader;tile.flags=TEGU_GENERIC_SHADER_TILE_FETCH;md[1].shader=&tile;
    struct tegu_generic_draw_request tq=mq[1];tq.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_TILE_ATTACHMENT};
    md[1].request=&tq;
    n=2;int trc=n2mb_draw_batch_mrt(b,md,mtp,3,out,&n);int line=0;
    if(trc)fprintf(stderr,"MRT tile batch rc=%x site=%d n2r=%d\n",trc,n2mb_last_failure(b,&line),line);
    assert(!trc&&f.submitted==subs+1);md[1].shader=&n2_preload_shader;md[1].request=&mq[1]; /* MRT tile fetch batches */
    subs=f.submitted;}
   rt[0].load_action=1;assert(n2mb_preframe(b,0)==1);
   n=2;assert(n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs); /* Load needs pre-frame */
   assert(n2mb_preframe(b,1)==0);n=2;assert(!n2mb_draw_batch_mrt(b,md,mtp,3,out,&n)&&f.submitted==subs+1);
  {/* 4x MSAA pass: Clear + continuing draws in ONE job with the resolve;
     * a 4x Load (standalone or as draw 0) is refused before submission. */
   struct n2mb_render_target st[3];struct n2mb_resolve_target sr={.token=r2,.offset=0,.length=16384,.format=80,.row_bytes=64,.width=16,.height=16};
   uint32_t sc4[1]={4};struct n2mb_render_targets sts[3];const struct n2mb_render_targets *stp[3];
   for(unsigned k=0;k<3;k++){
    st[k]=(struct n2mb_render_target){.token=r0,.offset=0,.length=16384,.format=80,.load_action=k?1:2,.write_mask=15,.row_bytes=64,.width=16,.height=16};
    sts[k]=(struct n2mb_render_targets){.version=N2MB_RENDER_TARGETS_MSAA_VERSION,.count=1,.targets=&st[k],.sample_counts=sc4,.resolves=&sr};
    stp[k]=&sts[k];
   }
   subs=f.submitted;n=2;assert(!n2mb_draw_batch_mrt(b,md,stp,3,out,&n)&&f.submitted==subs+1&&f.last_chain==3&&f.last_preframe==0);
   n=2;assert(n2mb_draw_v4(b,&n2_preload_shader,&mq[1],&tr0,NULL,&sts[1],out,&n)&&f.submitted==subs+1); /* lone 4x Load */
   st[0].load_action=1;n=2;assert(n2mb_draw_batch_mrt(b,md,stp,3,out,&n)&&f.submitted==subs+1);st[0].load_action=2;
   struct n2mb_resolve_target other=sr;other.token=r1;sts[2].resolves=&other;
   n=2;assert(n2mb_draw_batch_mrt(b,md,stp,3,out,&n)&&f.submitted==subs+1);sts[2].resolves=&sr; /* other resolve */
   sts[2].version=N2MB_RENDER_TARGETS_VERSION;n=2;assert(n2mb_draw_batch_mrt(b,md,stp,3,out,&n)&&f.submitted==subs+1);
   sts[2].version=N2MB_RENDER_TARGETS_MSAA_VERSION;
   st[0].load_action=0;n=2;assert(!n2mb_draw_batch_mrt(b,md,stp,3,out,&n)&&f.submitted==subs+2); /* DontCare start */
  }
  }
  close_bridge(b,&f);
 }
 puts("PASS true-MRT LOAD: per-target pre-frame DCDs (DRAW 0/1), Clear/DontCare mix, MRT batches, refusals before submission");
 {/* Tile-buffer depth/stencil continuity: a job continuing a pass's depth
    * (load 1) never owns a frame; batch members continue draw 0's pass. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  struct tegu_generic_draw_request q1=q;q1.load_action=1;
  struct n2r_texture_resources tr={N2R_TEXTURE_RESOURCES_VERSION,0,NULL};
  struct n2r_depth_stencil z0={.version=N2R_DEPTH_STENCIL_VERSION,.format=252,.load=2,.depth_compare=1,.depth_write=1,.clear_depth=1},z1=z0;z1.load=1;
  struct n2r_draw_options o0={.version=N2R_DRAW_OPTIONS_VERSION,.flags=N2R_DRAW_DEPTH_STENCIL,.zs=&z0},o1=o0;o1.zs=&z1;
  uint64_t out[2];unsigned n=2,subs=f.submitted;
  assert(!n2mb_draw_v3(b,&n2_preload_shader,&q,&tr,&o0,out,&n)&&f.submitted==subs+1);
  n=2;assert(n2mb_draw_v3(b,&n2_preload_shader,&q1,&tr,&o1,out,&n)&&f.submitted==subs+1); /* lone continue */
  struct n2mb_batch_draw zd[2]={{&n2_preload_shader,&q,&tr,&o0},{&n2_preload_shader,&q1,&tr,&o1}};
  n=2;assert(!n2mb_draw_batch(b,zd,2,out,&n)&&f.submitted==subs+2&&f.last_chain==2);
  zd[1].options=&o0;n=2;assert(n2mb_draw_batch(b,zd,2,out,&n)&&f.submitted==subs+2);      /* member restarts depth */
  zd[1].options=&o1;z1.clear_depth=.5f;n=2;assert(n2mb_draw_batch(b,zd,2,out,&n)&&f.submitted==subs+2); /* other pass */
  z1.clear_depth=1;zd[0].options=&o1;n=2;assert(n2mb_draw_batch(b,zd,2,out,&n)&&f.submitted==subs+2);   /* continue owns frame */
  close_bridge(b,&f);
 }
 {/* Handle table: 1024 slots; a P8 kernel (256 handles) refuses the 257th
    * and the bridge then caps itself at 256; a P9 kernel keeps going. */
  for(unsigned lim=0;lim<2;lim++){
   struct fake f={.job=1ull<<32,.handle_limit=lim?0:256};uintptr_t b=open_bridge(&f);
   static uint64_t h[400];unsigned got=0;
   for(unsigned i=0;i<300;i++){uint64_t bytes=2048,out[2];unsigned n=2;if(n2mb_scalar(b,1,&bytes,1,out,&n))break;h[got++]=out[0];}
   if(lim){assert(got==300);}
   else {
    assert(got==256);
    uint64_t bytes=2048,out[2];unsigned n=2;assert(n2mb_scalar(b,1,&bytes,1,out,&n)); /* cap reached, no kernel call */
    n=0;assert(!n2mb_scalar(b,2,&h[--got],1,NULL,&n));
    n=2;assert(!n2mb_scalar(b,1,&bytes,1,out,&n));h[got++]=out[0];                  /* a freed slot is reused */
   }
   for(unsigned i=0;i<got;i++){unsigned n=0;assert(!n2mb_scalar(b,2,&h[i],1,NULL,&n));}
   assert(!f.live);close_bridge(b,&f);
  }
 }
 puts("PASS handle table: 1024 slots, P8 256-handle cap learned on the first refusal, freed slots reused");
 puts("PASS tile-buffer depth/stencil: lone continue refused, batch continuity, restart/other-pass refusals");
 puts("PASS fragment-job tile bound: draw/batch scissor union for Load/DontCare, full for Clear/clipped, pre-frame, mode switch");
 {/* Asynchronous submission (kernel 0090 WAIT_SEQ). Traces force ordered
    * submission, so these bridges open without NORD2_METAL_TRACE. */
  const char *trace_env=getenv("NORD2_METAL_TRACE");char *trace_saved=trace_env?strdup(trace_env):NULL;unsetenv("NORD2_METAL_TRACE");
  struct fake old={.job=1ull<<32};uintptr_t ob=open_bridge(&old);
  assert(n2mb_async(ob,1)==0&&n2mb_async(ob,2)==0); /* no WAIT_SEQ: stays synchronous */
  close_bridge(ob,&old);
  {/* Kernels 0090-0107 (P8-P10) have WAIT_SEQ but refuse its seq-0 probe:
    * the bridge stays synchronous there - one WAIT per job, no WAIT_SEQ. */
   struct fake pre={.job=1ull<<32,.seq_model=1,.seq0_notfound=1};uintptr_t pb=open_bridge(&pre);uint64_t pr=resource(pb),pt=resource(pb);
   assert(n2mb_async(pb,1)==0&&n2mb_async(pb,2)==0);
   struct tegu_generic_draw_request pq={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=pr,.target_length=16384};
   pq.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=pt,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
   uint64_t po[2];unsigned pn;
   for(unsigned k=0;k<3;k++){pn=2;assert(!n2mb_draw(pb,&n2_preload_shader,&pq,po,&pn)&&po[0]==pre.job);}
   assert(pre.submitted==3&&pre.waited==3&&pre.seq_waits==0&&!pre.pending);
   assert(n2mb_wait_pending(pb,0)==0&&pre.seq_waits==0);
   close_bridge(pb,&pre);}
  struct fake f={.job=1ull<<32,.seq_model=1};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b),c=resource(b);
  assert(n2mb_async(b,1)==0&&n2mb_async(b,2)==1);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  uint64_t out[2];unsigned n;
  for(unsigned k=0;k<3;k++){n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&out[0]==f.job);}
  /* command blocks stay in the bridge pool while their jobs run: no release, none reused */
  assert(f.submitted==3&&f.waited==0&&f.seq_waits==0&&f.inflight==3&&f.busy_releases==0&&n2mb_cmdpool_count(b)==3);
  uint64_t failed=7;assert(!n2mb_wait_pending(b,&failed)&&!failed&&f.seq_waits==1&&!f.pending);
  assert(f.live==4+n2mb_code_cache_count(b)+n2mb_cmdpool_count(b)); /* deferred blocks freed once idle */
  assert(!n2mb_wait_pending(b,NULL)&&f.seq_waits==1); /* nothing pending: no call */
  /* A clear and a batch stay asynchronous; a snapshot draw reaps first (SYNC
   * asserts no pending job in the model); n2mb_copy reaps before the CPU copy. */
  n=2;assert(!n2mb_clear(b,&q,out,&n)&&f.pending);
  struct n2mb_batch_draw dr[2]={{&n2_preload_shader,&q,NULL,NULL},{&n2_preload_shader,&q,NULL,NULL}};
  n=2;assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.inflight==2&&f.seq_waits==1);
  q.bindings[0].memory.token=r;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&f.seq_waits==2&&f.inflight==1);
  q.bindings[0].memory.token=t;
  /* C6s: a CPU READ of a buffer waits only for GPU jobs that write it: t is
   * only sampled by the queued draw (no wait), its target r is written. */
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&f.pending);
  {unsigned sw=f.seq_waits,waited=9;uint64_t h=t;assert(!n2mb_wait_handles_written(b,&h,1,NULL,&waited)&&!waited&&f.seq_waits==sw&&f.pending);
   h=r;assert(!n2mb_wait_handles_written(b,&h,1,NULL,&waited)&&waited&&f.seq_waits==sw+1&&!f.pending);}
  assert(!n2mb_copy(b,t,0,c,64)&&f.seq_waits==3&&!f.pending);
  /* The kernel in-flight window: reap and resubmit. */
  f.window=2;f.max_inflight=0;for(unsigned k=0;k<5;k++){n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));}
  assert(f.max_inflight==2&&f.seq_waits==5&&f.inflight==1);
  /* A GPU failure surfaces at the next reap with the failing job id and
   * breaks the bridge like a failed synchronous wait. */
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));f.fail_seq=1;
  f.js_status=0x58;f.mmu_status=0x10c3;f.mmu_address=f.buffers[t].va+0x40; /* the failed job samples t */
  assert(n2mb_wait_pending(b,&failed)&&failed==f.job-1);
  /* The break is named once: job, JS status, MMU fault, and the job's buffer holding the address. */
  {char want[64];snprintf(want,sizeof(want),"job=%llu state=5 js=0x58 mmu=0x10c3",(unsigned long long)failed);
   char hit[32];snprintf(hit,sizeof(hit),"%u*@",(unsigned)t);
   assert(f.fault_lines==1&&!f.fault_queries&&strstr(f.fault_text,want)&&strstr(f.fault_text,hit)&&strstr(f.fault_text,"recover=0"));}
  assert(f.lost_calls==1&&n2mb_lost(b)&&!n2mb_fault_recoveries(b)); /* MMU fault: lost, the transport is told once */
  n=2;assert(n2mb_draw(b,&n2_preload_shader,&q,out,&n)==(int)0xe00002d8u);
  assert(f.fault_lines==1&&f.lost_calls==1); /* later refusals of the broken bridge log nothing more */
  close_bridge(b,&f);
  struct fake g={.job=1ull<<32,.seq_model=1};uintptr_t gb=open_bridge(&g);uint64_t gr=resource(gb),gt=resource(gb);
  assert(n2mb_async(gb,1)==0);q.target_token=gr;q.bindings[0].memory.token=gt;
  n=2;assert(!n2mb_draw(gb,&n2_preload_shader,&q,out,&n)&&g.pending);
  assert(n2mb_async(gb,0)==1&&!g.pending&&n2mb_async(gb,2)==0); /* mode 0 reaps first */
  n=2;assert(!n2mb_draw(gb,&n2_preload_shader,&q,out,&n)&&!g.pending&&g.waited==1);
  n=2;assert(!n2mb_draw(gb,&n2_preload_shader,&q,out,&n));
  close_bridge(gb,&g); /* close reaps */
  {/* GPU fault recovery, injected mid-stream. A job fault with a clean
    * context (no MMU fault) fails only the command whose wait covers it;
    * later draws submit and complete again. An MMU fault loses the bridge
    * and calls the transport's lost hook exactly once. */
   struct fake h={.job=1ull<<32,.seq_model=1};uintptr_t hb=open_bridge(&h);uint64_t hr=resource(hb),ht=resource(hb),hc=resource(hb),hf=0;
   assert(n2mb_async(hb,1)==0);q.target_token=hr;q.bindings[0].memory.token=ht;
   for(unsigned k=0;k<3;k++){n=2;assert(!n2mb_draw(hb,&n2_preload_shader,&q,out,&n));}
   h.fail_seq=1;h.js_status=0x58;h.mmu_status=0;
   assert(n2mb_wait_pending(hb,&hf)==(int)0xe00002bcu&&hf==h.job-2); /* async: the first of the 3 jobs failed */
   assert(!n2mb_lost(hb)&&n2mb_fault_recoveries(hb)==1&&!h.lost_calls&&h.fault_lines==1&&strstr(h.fault_text,"recover=1"));
   unsigned sub=h.submitted;
   for(unsigned k=0;k<3;k++){n=2;assert(!n2mb_draw(hb,&n2_preload_shader,&q,out,&n));}
   hf=7;assert(!n2mb_wait_pending(hb,&hf)&&h.submitted==sub+3&&!h.pending); /* the next frames complete */
   /* Seen first by an internal reap (n2mb_copy waits for the GPU): the
    * public wait of the owning command still reports it, once. */
   n=2;assert(!n2mb_draw(hb,&n2_preload_shader,&q,out,&n));uint64_t owner=out[0];h.fail_seq=1;
   assert(n2mb_copy(hb,ht,0,hc,64)&&!n2mb_lost(hb));
   hf=0;assert(n2mb_wait_seq(hb,owner,&hf)==(int)0xe00002bcu&&hf==owner);
   hf=7;assert(!n2mb_wait_seq(hb,owner,&hf)&&!n2mb_wait_pending(hb,&hf));
   n=2;assert(!n2mb_draw(hb,&n2_preload_shader,&q,out,&n)&&!n2mb_wait_pending(hb,NULL));
   assert(n2mb_fault_recoveries(hb)==2&&!h.lost_calls);
   /* MMU fault: the context is gone; lost once, nothing more submitted. */
   n=2;assert(!n2mb_draw(hb,&n2_preload_shader,&q,out,&n));h.fail_seq=1;h.mmu_status=0x10c3;h.mmu_address=h.buffers[ht].va;
   assert(n2mb_wait_pending(hb,&hf)&&n2mb_lost(hb)&&h.lost_calls==1);
   sub=h.submitted;n=2;assert(n2mb_draw(hb,&n2_preload_shader,&q,out,&n)==(int)0xe00002d8u&&h.submitted==sub&&h.lost_calls==1);
   close_bridge(hb,&h);
   /* Synchronous submission (pre-0108 kernels, NORD2_METAL_ASYNC=0): the
    * faulted draw itself fails; with a clean context the next one runs. */
   struct fake y={.job=1ull<<32};uintptr_t yb=open_bridge(&y);uint64_t yr=resource(yb),yt=resource(yb);
   q.target_token=yr;q.bindings[0].memory.token=yt;
   n=2;assert(!n2mb_draw(yb,&n2_preload_shader,&q,out,&n));
   y.fault_wait=1;n=2;assert(n2mb_draw(yb,&n2_preload_shader,&q,out,&n)==(int)0xe00002bcu&&!n2mb_lost(yb)&&n2mb_fault_recoveries(yb)==1);
   n=2;assert(!n2mb_draw(yb,&n2_preload_shader,&q,out,&n)&&!y.lost_calls);
   y.fault_wait=1;y.mmu_status=0x2c3;n=2;assert(n2mb_draw(yb,&n2_preload_shader,&q,out,&n)&&n2mb_lost(yb)&&y.lost_calls==1);
   sub=y.submitted;n=2;assert(n2mb_draw(yb,&n2_preload_shader,&q,out,&n)==(int)0xe00002d8u&&y.submitted==sub);
   close_bridge(yb,&y);
   puts("PASS GPU fault recovery: job fault (clean context) fails only its command and later frames complete; internal reap still reported once; MMU fault loses the bridge with one lost call (async and sync)");}
  if(trace_saved){setenv("NORD2_METAL_TRACE",trace_saved,1);free(trace_saved);}
 }
 if(!(getenv("NORD2_METAL_TRACE")&&getenv("NORD2_METAL_TRACE")[0]=='1')){
  /* B2 n2mb_wait_handles: a CPU access waits only for the queued jobs that
    * list one of its buffers (handle ring of this context's newest jobs).
    * gpu_us: the fake's partial WAIT_SEQ model (reaps jobs <= seq). */
  struct fake f={.job=1ull<<32,.seq_model=1,.gpu_us=1};uintptr_t b=open_bridge(&f);
  uint64_t r=resource(b),t=resource(b),u=resource(b),v=resource(b),w=resource(b),out[2],failed=9;unsigned n,waited=9;
  assert(n2mb_async(b,1)==0&&n2mb_async(b,2)==1);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));uint64_t j1=out[0];
  q.target_token=u;q.bindings[0].memory.token=v;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));uint64_t j2=out[0];assert(j2>j1&&f.inflight==2);
  unsigned waits=f.seq_waits;
  assert(!n2mb_wait_handles(b,&w,1,&failed,&waited)&&!waited&&!failed&&f.seq_waits==waits); /* not in flight */
  assert(!n2mb_wait_handles(b,&t,1,&failed,&waited)&&waited&&f.seq_waits==waits+1);        /* job 1 lists t */
  assert(!n2mb_wait_handles(b,&t,1,&failed,&waited)&&!waited);                               /* reaped since */
  uint64_t both[2]={w,u};
  assert(!n2mb_wait_handles(b,both,2,&failed,&waited)&&waited&&!f.pending);                  /* job 2 lists u */
  assert(!n2mb_wait_handles(b,NULL,0,&failed,&waited)&&!waited);
  close_bridge(b,&f);
  /* C6r regression: the P15 kernel keeps up to 96 unreaped jobs per context
   * (N2C_CTX_JOBS), more than the 64-entry ring held. A job that listed t and
   * then left the ring must still be waited for before a CPU access to t
   * (stale/overwritten textures on the device otherwise). */
  struct fake g={.job=1ull<<32,.seq_model=1,.gpu_us=1,.window=96};uintptr_t gb=open_bridge(&g);
  uint64_t gr=resource(gb),gt=resource(gb),gu=resource(gb),gv=resource(gb);
  assert(n2mb_async(gb,1)==0&&n2mb_async(gb,2)==1);
  q.target_token=gr;q.bindings[0].memory.token=gt;
  n=2;assert(!n2mb_draw(gb,&n2_preload_shader,&q,out,&n));uint64_t gj1=out[0];
  q.target_token=gu;q.bindings[0].memory.token=gv;
  for(unsigned k=0;k<80;k++){n=2;assert(!n2mb_draw(gb,&n2_preload_shader,&q,out,&n));}
  assert(g.inflight==81&&g.seq_waits==0);
  waited=0;assert(!n2mb_wait_handles(gb,&gt,1,&failed,&waited)&&waited&&g.seq_waits==1);
  assert(g.inflight<=80&&g.job-g.inflight>=gj1); /* gj1 reaped */
  close_bridge(gb,&g);
  /* Bisect switch NORD2_METAL_WAIT_HANDLES=0: every targeted wait reaps all. */
  setenv("NORD2_METAL_WAIT_HANDLES","0",1);
  struct fake h={.job=1ull<<32,.seq_model=1,.gpu_us=1};uintptr_t hb=open_bridge(&h);
  uint64_t hr=resource(hb),ht=resource(hb),hw=resource(hb);
  assert(n2mb_async(hb,1)==0&&n2mb_async(hb,2)==1);
  q.target_token=hr;q.bindings[0].memory.token=ht;
  n=2;assert(!n2mb_draw(hb,&n2_preload_shader,&q,out,&n));
  waited=0;assert(!n2mb_wait_handles(hb,&hw,1,&failed,&waited)&&waited&&!h.inflight); /* hw in no job: still a full reap */
  close_bridge(hb,&h);
  unsetenv("NORD2_METAL_WAIT_HANDLES");
 }
 puts("PASS targeted waits: only the queued jobs listing a CPU access's buffers are waited for");
 {/* B1 kernel 0112 surface reservation: only this context's imports are
    * reserved (others filtered), none -> id 0; a pre-0112 kernel answers
    * Unsupported once (probe) and the bridge never calls it again. */
  static unsigned char surf[16384];memset(surf,0x22,sizeof(surf));
  struct fake f={.job=1ull<<32,.surf_model=1,.resv_model=1};f.surf_cpu[1]=surf;f.surf_bytes[1]=sizeof(surf);
  uintptr_t b=open_bridge(&f);uint64_t r=resource(b),sh=0,id=9,st[4];
  assert(!n2mb_import_surface(b,1,sizeof(surf),(uint64_t)(uintptr_t)surf,&sh));
  uint64_t hs[3]={r,sh,sh};
  assert(!n2mb_surface_reserve(b,hs,1,&id)&&!id&&f.resv_calls==1);              /* probe only; r is no import */
  assert(!n2mb_surface_reserve(b,hs,3,&id)&&id==1&&f.resv_calls==2&&f.resv_last_n==1&&f.resv_handles[0]==sh);
  assert(!n2mb_surface_retire(b,id,0)&&f.retire_calls==1&&f.retire_last[1]==0); /* cancel */
  assert(!n2mb_surface_retire(b,0,5)&&f.retire_calls==1);                       /* id 0: no call */
  n2mb_surface_stats(b,st);assert(st[0]==1&&st[1]==1&&st[2]==1);
  close_bridge(b,&f);
  struct fake g={.job=1ull<<32,.surf_model=1};g.surf_cpu[1]=surf;g.surf_bytes[1]=sizeof(surf);
  uintptr_t gb=open_bridge(&g);
  assert(!n2mb_import_surface(gb,1,sizeof(surf),(uint64_t)(uintptr_t)surf,&sh));hs[1]=sh;
  assert(n2mb_surface_reserve(gb,hs,2,&id)==(int)0xe00002c7u&&!id);
  assert(n2mb_surface_reserve(gb,hs,2,&id)==(int)0xe00002c7u);
  close_bridge(gb,&g);
 }
 puts("PASS surface reservation (0112): imports only, probe once, pre-0112 Unsupported fallback");
 puts("PASS async submission: WAIT_SEQ reaping, busy-consumed releases, snapshot/copy ordering, in-flight window, failure, mode switch, old kernel");
 {/* Kernel 0096: GPU-only allocations, old-kernel fallback, and imported
    * IOSurface acquire before a bridge CPU read (stale-line model). */
  const char *trace_env=getenv("NORD2_METAL_TRACE");char *trace_saved=trace_env?strdup(trace_env):NULL;unsetenv("NORD2_METAL_TRACE");
  static unsigned char surf[16384];memset(surf,0x11,sizeof(surf));
  struct fake f={.job=1ull<<32,.surf_model=1};f.surf_cpu[1]=surf;f.surf_bytes[1]=sizeof(surf);
  uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b),dst=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  uint64_t out[2];unsigned n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  {unsigned heaps=0;for(unsigned h=1;h<=f.next;h++)if(f.buffers[h].active&&f.buffers[h].size>=(2u<<20)){heaps++;assert(f.buffers[h].gpu_only);}
   assert(heaps==1&&n2mb_gpu_only_state(b)==1);}
  /* A GPU-only client buffer cannot be mapped; other flag bits are refused. */
  uint64_t in2[2]={4096,2},o2[2];n=2;assert(!n2mb_scalar(b,1,in2,2,o2,&n)&&f.buffers[o2[0]].gpu_only);
  uint64_t a=0,sz=0;assert(n2mb_map(b,(unsigned)o2[0],0,&a,&sz,1)==(int)0xe00002c2u);
  n=0;assert(!n2mb_scalar(b,2,&o2[0],1,NULL,&n));
  in2[1]=1;n=2;assert(n2mb_scalar(b,1,in2,2,o2,&n)==(int)0xe00002c2u);
  /* Imported surface: a GPU job wrote 0x77; the CPU still holds 0x11 lines
   * until an acquire. n2mb_copy must acquire before reading. */
  uint64_t sh=0;assert(!n2mb_import_surface(b,1,sizeof(surf),(uint64_t)(uintptr_t)surf,&sh));
  memset(f.buffers[sh].data,0x77,sizeof(surf));
  unsigned acq=f.surf_acquires;assert(!n2mb_copy(b,sh,0,dst,1024));
  assert(f.surf_acquires==acq+1&&f.buffers[dst].data[0]==0x77&&f.buffers[dst].data[1023]==0x77);
  /* The feedback snapshot of an imported target acquires it too. */
  memset(f.buffers[sh].data,0x33,sizeof(surf));acq=f.surf_acquires;
  struct tegu_generic_draw_request fq=q;fq.target_token=sh;fq.target_length=sizeof(surf);fq.bindings[0].memory.token=sh;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&fq,out,&n)&&f.surf_acquires==acq+1&&surf[0]==0x33);
  close_bridge(b,&f);
  /* A kernel before 0096 refuses bit1: one refused attempt, then plain allocations. */
  struct fake g={.job=1ull<<32,.no_gpu_only=1};uintptr_t gb=open_bridge(&g);uint64_t gr=resource(gb),gt=resource(gb);
  q.target_token=gr;q.bindings[0].memory.token=gt;n=2;assert(!n2mb_draw(gb,&n2_preload_shader,&q,out,&n));
  assert(n2mb_gpu_only_state(gb)==2&&g.gpu_only_allocs==0);
  n=2;assert(!n2mb_draw(gb,&n2_preload_shader,&q,out,&n));
  close_bridge(gb,&g);
  if(trace_saved){setenv("NORD2_METAL_TRACE",trace_saved,1);free(trace_saved);}
 }
 puts("PASS kernel 0096: GPU-only heap/TLS/client buffers (map refused), old-kernel fallback, imported-surface acquire before CPU reads (stale-line model)");
 {/* Command-block pool: a block is handed out again only after the job that
    * used it is reaped (async WAIT_SEQ / n2mb_wait_seq, or a synchronous WAIT). */
  const char *trace_env=getenv("NORD2_METAL_TRACE");char *trace_saved=trace_env?strdup(trace_env):NULL;unsetenv("NORD2_METAL_TRACE");
  struct fake f={.job=1ull<<32,.seq_model=1};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  assert(n2mb_async(b,1)==0&&n2mb_async(b,2)==1);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  uint64_t out[2];unsigned n;
  for(unsigned k=0;k<3;k++){n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));}
  unsigned a0=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  assert(f.next==a0+1&&n2mb_cmdpool_count(b)==4); /* all blocks in flight: a new one */
  uint64_t failed=0;assert(!n2mb_wait_pending(b,&failed)&&!failed);
  a0=f.next;for(unsigned k=0;k<4;k++){n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));}
  assert(f.next==a0&&n2mb_cmdpool_count(b)==4&&!f.busy_releases); /* four reaped blocks reused, none released */
  /* Synchronous mode reuses the same block job after job. */
  assert(n2mb_async(b,0)==1);a0=f.next;
  for(unsigned k=0;k<5;k++){n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));}
  assert(f.next==a0&&f.waited==5);
  /* Clears take a slot of one colour slab (allocated once) instead of a BO each. */
  q.target_format=80;a0=f.next;
  for(unsigned k=0;k<6;k++){n=2;assert(!n2mb_clear(b,&q,out,&n));}
  assert(f.next==a0+1&&f.waited==11);
  close_bridge(b,&f);
  if(trace_saved){setenv("NORD2_METAL_TRACE",trace_saved,1);free(trace_saved);}
 }
 puts("PASS command-block pool: reuse only after the job is reaped (async and synchronous), in-flight blocks never handed out, clear-colour slab");
 {/* Budget pressure: a refused client allocation first reclaims the shader
    * code cache and retries; nothing is reclaimed while it is in use. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  uint64_t out[2];unsigned n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&n2mb_code_cache_count(b)==2);
  uint64_t priv=0,imp=0,in=0,code=0;unsigned bos=0;n2mb_memory_stats(b,&priv,&imp,&in,&code,&bos);
  assert(code==2*16384&&in>=code+(2u<<20)&&!imp&&bos==f.live);
  /* First the idle pooled command block, then the code cache, then nothing. */
  assert(n2mb_cmdpool_count(b)==1);
  f.fail_alloc=1;uint64_t bytes=4096,o[2];n=2;assert(!n2mb_scalar(b,1,&bytes,1,o,&n)&&!n2mb_cmdpool_count(b)&&n2mb_code_cache_count(b)==2);
  f.fail_alloc=1;bytes=4096;n=2;assert(!n2mb_scalar(b,1,&bytes,1,o,&n)&&n2mb_code_cache_count(b)==0);
  f.fail_alloc=1;bytes=4096;n=2;assert(n2mb_scalar(b,1,&bytes,1,o,&n)&&!f.fail_alloc); /* nothing left to reclaim */
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&n2mb_code_cache_count(b)==2);
  close_bridge(b,&f);
 }
 puts("PASS budget pressure: idle command-block pool, then code cache, reclaimed before a client allocation fails; memory stats");
 for(unsigned fault=0;fault<5;fault++){
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=128,.target_format=115,.target_token=r,.target_length=16384};
  const double rgba[4]={.25,.5,.75,1};assert(!n2r_clear(115,rgba,q.clear_words));
  /* 2 failures: the pooled colour slab, then the per-clear fallback BO. */
  if(fault==1)f.fail_alloc=2;if(fault==2)f.fail_map=2;if(fault==3)f.fail_submit=1;if(fault==4)f.fail_wait=1;
  uint64_t out[2];unsigned n=2;int e=n2mb_clear(b,&q,out,&n);
  if(!fault)assert(!e&&f.submitted==1&&f.waited==1&&f.live==2+n2mb_code_cache_count(b)+n2mb_cmdpool_count(b)&&f.heap_allocs==1);else assert(e);
  close_bridge(b,&f);
 }
 puts("PASS GPU clear-only pass: real submission, RGBA16F, five lifetime/error cases");
 for(unsigned fixture=1;fixture<=3;fixture++){
  struct fake f={.geometry_fixture=fixture};uintptr_t b=open_bridge(&f);uint64_t r=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.target_format=80,.target_token=r,.target_length=16384};
  uint64_t out[2];for(unsigned draw=0;draw<70;draw++){unsigned n=2;assert(!n2mb_clear(b,&q,out,&n));}
  const char *trace=getenv("NORD2_METAL_TRACE");unsigned enabled=trace&&!strcmp(trace,"1");
  assert(f.heap_logs==(enabled&&fixture==1?16u:0u));
  assert(f.before_logs==(enabled?8u:0u));
  assert(f.after_logs==(enabled&&fixture!=2?8u:0u));
  close_bridge(b,&f);
 }
 puts("PASS geometry diagnostics: bounded readback, invalid hardware bottom refusal, lifetime unchanged");
 {/* Persistent heap: one allocation reused by every draw, released at close. */
  struct fake f={0};uintptr_t b=open_bridge(&f);uint64_t r=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.target_format=80,.target_token=r,.target_length=16384};
  unsigned char *heap_byte=NULL;
  uint64_t out[2];for(unsigned draw=0;draw<12;draw++){
   unsigned n=2;assert(!n2mb_clear(b,&q,out,&n));
   if(!draw){for(unsigned h=1;h<=f.next;h++)if(f.buffers[h].active&&f.buffers[h].va==f.heap_va)heap_byte=f.buffers[h].data;
    assert(heap_byte);*heap_byte=0x6a; /* model a retained GPU allocation */
   }else assert(*heap_byte==0x6a); /* diagnostics must not zero live heap state */
  }
  if(getenv("NORD2_METAL_TRACE")&&!strcmp(getenv("NORD2_METAL_TRACE"),"1"))assert(f.nonzero_before_logs==7);
  assert(f.heap_allocs==1&&f.draw_submits==12&&f.live==2+n2mb_code_cache_count(b)+n2mb_cmdpool_count(b)&&f.last_mask==0x82&&f.last_chunk==3&&f.last_heap_size==(2u<<20));
  close_bridge(b,&f);
 }
 puts("PASS persistent tiler heap: one 2 MiB allocation for 12 draws, identical default packet, released at close");
 for(unsigned model=0;model<3;model++){
  /* model0: nothing renders; model1: chunk256k renders; model2: as 1 with an injected submit failure. */
  setenv("NORD2_METAL_TILER_MATRIX","1",1);
  struct fake f={.render_model=model!=0};uintptr_t b=open_bridge(&f);unsetenv("NORD2_METAL_TILER_MATRIX");
  uint64_t r=resource(b);if(model==2)f.fail_submit=1;
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.target_format=80,.target_token=r,.target_length=16384};
  uint64_t out[2];unsigned n=2;assert(!n2mb_clear(b,&q,out,&n));
  /* The matrix's own mapped 16 MiB heap is dropped; the persistent heap that
   * follows is a separate GPU-only allocation (kernel 0096). */
  unsigned matrix_draws=model==2?10:11;  assert(f.draw_submits==12&&f.heap_allocs==2&&f.adopt_logs==1); /* attempts, incl. an injected refusal */
  {unsigned live_heaps=0,gpu_only=0;for(unsigned h=1;h<=f.next;h++)if(f.buffers[h].active&&f.buffers[h].size>=(2u<<20)){live_heaps++;gpu_only+=f.buffers[h].gpu_only;}
   assert(live_heaps==1&&gpu_only==1&&f.buffers[0].active==0);}
  assert(f.mask_seen[0]==1&&f.mask_seen[2]==3&&f.const_fs_seen==3&&f.nosec_seen==2);
  assert(f.tm_logs==1+3*matrix_draws+(model==2)+1);
  if(model==0)assert(strstr(f.adopt_text,"adopt=-1 heap=2048K chunk=3 mask=082")&&f.last_chunk==3);
  else assert(strstr(f.adopt_text,"adopt=1 heap=2048K chunk=0 mask=082")&&f.last_chunk==0&&f.last_heap_size==(2u<<20));
  n=2;assert(!n2mb_clear(b,&q,out,&n));assert(f.draw_submits==13&&f.adopt_logs==1); /* runs once */
  assert(f.live==2+n2mb_code_cache_count(b)+n2mb_cmdpool_count(b)); /* matrix buffers released; heap and shader code retained */
  close_bridge(b,&f);
 }
 puts("PASS tiler matrix: 11 variants once per context, Mesa mask from TILER_FEATURES, const-FS/no-secondary packets, adoption, failure isolation, log quota");
 for(unsigned fail_sync=1;fail_sync<=2;fail_sync++){
  setenv("NORD2_METAL_TILER_MATRIX","1",1);
  struct fake f={.render_model=1,.fail_sync_at=fail_sync};
  uintptr_t b=open_bridge(&f);unsetenv("NORD2_METAL_TILER_MATRIX");
  uint64_t r=resource(b),out[2];unsigned n=2;
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.target_format=80,.target_token=r,.target_length=16384};
  assert(n2mb_clear(b,&q,out,&n));
  assert(f.draw_submits==1&&f.sync_calls==fail_sync&&f.adopt_logs==1);
  assert(strstr(f.adopt_text,"adopt=-1"));
  n=2;assert(n2mb_clear(b,&q,out,&n));assert(f.draw_submits==1);
  close_bridge(b,&f);
 }
 puts("PASS matrix readback failure: target/heap sync each abort before pixel inspection and further jobs");
 {
  setenv("NORD2_METAL_TILER_MATRIX","1",1);
  struct fake f={.fail_wait=1};uintptr_t b=open_bridge(&f);
  unsetenv("NORD2_METAL_TILER_MATRIX");
  uint64_t r=resource(b),out[2];unsigned n=2;
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.target_format=80,.target_token=r,.target_length=16384};
  assert(n2mb_clear(b,&q,out,&n));
  assert(f.pending&&f.draw_submits==1&&f.waited==1&&!f.sync_calls);
  assert(f.adopt_logs==1&&strstr(f.adopt_text,"adopt=-1"));
  n=2;assert(n2mb_clear(b,&q,out,&n));assert(f.draw_submits==1);
  close_bridge(b,&f);
 }
 if(!(getenv("NORD2_METAL_TRACE")&&getenv("NORD2_METAL_TRACE")[0]=='1')){
  /* Chained submission (default off; NORD2_METAL_CHAIN / n2mb_chain_mode):
   * one kernel job per run of frames, every frame on its own heap slice.
   * (Traced draws are synchronous and never chain.) */
  struct fake f={.job=1ull<<32,.chain_model=1};uintptr_t b=open_bridge(&f);
  uint64_t r=resource(b),t=resource(b),u=resource(b),v=resource(b),out[2],job=7,st[N2MB_CHAIN_STATS];unsigned n=2;
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,.scissor={0,0,16,16},
   .binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},
   .width=16,.height=16,.row_bytes=64,.format=80,.reserved=N2MB_BINDING_STAGED};
  /* Off: begin reports 0 and draws submit one job each. */
  unsigned subs=f.submitted;assert(n2mb_chain_begin(b)==0);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&out[0]&&f.submitted==subs+1);
  assert(!n2mb_chain_end(b,&job)&&!job);
  assert(n2mb_chain_mode(b,1,4)==0&&n2mb_chain_begin(b)==1);
  /* r <- t, u <- r, v <- u (a blur pyramid): one job, three frames. */
  struct tegu_generic_draw_request q1=q,q2=q;q1.target_token=u;q1.bindings[0].memory.token=r;
  q2.target_token=v;q2.bindings[0].memory.token=u;
  subs=f.submitted;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&!out[0]);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q1,out,&n)&&!out[0]);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q2,out,&n)&&!out[0]&&f.submitted==subs);
  assert(!n2mb_chain_end(b,&job)&&job==f.job&&f.submitted==subs+1&&f.last_frames==3&&f.last_tjobs==3);
  n2mb_chain_stats(b,st);assert(st[0]==1&&st[1]==3&&st[2]==2&&st[7]==1&&st[8]==3&&st[10]==1);
  /* A LOAD frame (preload job + draw) chains with its two tiler jobs. */
  assert(n2mb_chain_begin(b)==1);subs=f.submitted;
  struct tegu_generic_draw_request ql=q1;ql.load_action=1;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&!out[0]);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&ql,out,&n)&&!out[0]);
  assert(!n2mb_chain_end(b,&job)&&f.submitted==subs+1&&f.last_frames==2&&f.last_tjobs==3);
  /* The vertex stage may read the earlier frame's target (unstaged binding):
   * the chain is submitted first, the frame starts the next one. */
  assert(n2mb_chain_begin(b)==1);subs=f.submitted;q1.bindings[0].reserved=0;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&!out[0]);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q1,out,&n)&&!out[0]&&f.submitted==subs+1&&f.last_frames==1);
  assert(!n2mb_chain_end(b,&job)&&f.submitted==subs+2&&f.last_frames==1);
  q1.bindings[0].reserved=N2MB_BINDING_STAGED|N2MB_BINDING_VERTEX;
  assert(n2mb_chain_begin(b)==1);subs=f.submitted;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q1,out,&n)&&f.submitted==subs+1);
  assert(!n2mb_chain_end(b,&job)&&f.submitted==subs+2);q1.bindings[0].reserved=N2MB_BINDING_STAGED;
  n2mb_chain_stats(b,st);assert(st[3]==2);
  /* Mode 2: only independent frames. */
  assert(n2mb_chain_mode(b,2,0)==1&&n2mb_chain_begin(b)==2);subs=f.submitted;
  struct tegu_generic_draw_request qi=q;qi.target_token=v; /* t -> v: independent */
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&qi,out,&n)&&f.submitted==subs);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q1,out,&n)&&f.submitted==subs+1&&f.last_frames==2); /* reads r */
  assert(!n2mb_chain_end(b,&job)&&f.submitted==subs+2&&f.last_frames==1);
  /* Full: two frames per job. */
  assert(n2mb_chain_mode(b,1,2)==2&&n2mb_chain_begin(b)==1);subs=f.submitted;
  for(unsigned k=0;k<3;k++){n=2;assert(!n2mb_draw(b,&n2_preload_shader,k==1?&qi:&q,out,&n));}
  assert(f.submitted==subs+1&&f.last_frames==2);
  assert(!n2mb_chain_end(b,&job)&&f.submitted==subs+2&&f.last_frames==1);
  n2mb_chain_stats(b,st);assert(st[5]>=1);
  /* Ordering calls submit the open chain: a wait, a map of a chained buffer. */
  assert(n2mb_chain_mode(b,1,4)==1&&n2mb_chain_begin(b)==1);subs=f.submitted;
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&f.submitted==subs);
  assert(!n2mb_wait_pending(b,NULL)&&f.submitted==subs+1);
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&f.submitted==subs+1);
  {uint64_t addr=0,size=0;assert(!n2mb_map(b,(unsigned)r,0,&addr,&size,1)&&f.submitted==subs+2);}
  assert(!n2mb_chain_end(b,&job)&&!job);
  /* Clears and a batch in one chain; clear-colour slots wait for its id. */
  assert(n2mb_chain_begin(b)==1);subs=f.submitted;
  struct tegu_generic_draw_request qc=q;qc.binding_count=0;
  n=2;assert(!n2mb_clear(b,&qc,out,&n)&&!out[0]);
  struct tegu_generic_draw_request qb=qi;qb.load_action=1;
  struct n2mb_batch_draw bd[2]={{&n2_preload_shader,&qb,NULL,NULL},{&n2_preload_shader,&qb,NULL,NULL}};
  n=2;assert(!n2mb_draw_batch(b,bd,2,out,&n)&&!out[0]);
  n=2;assert(!n2mb_clear(b,&qc,out,&n)&&!out[0]&&f.submitted==subs);
  assert(!n2mb_chain_end(b,&job)&&job&&f.submitted==subs+1&&f.last_frames==3);
  /* Outside begin/end nothing chains. */
  subs=f.submitted;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&out[0]&&f.submitted==subs+1);
  n2mb_chain_stats(b,st);
  fprintf(stderr,"CHAIN chains=%llu frames=%llu dep=%llu flush_dep=%llu handles=%llu full=%llu other=%llu end=%llu most=%llu\n",
   (unsigned long long)st[0],(unsigned long long)st[1],(unsigned long long)st[2],(unsigned long long)st[3],(unsigned long long)st[4],
   (unsigned long long)st[5],(unsigned long long)st[6],(unsigned long long)st[7],(unsigned long long)st[8]);
  close_bridge(b,&f);
  /* NORD2_METAL_CHAIN=1: the first begin runs the 4-frame visibility probe
   * as one job; the fake GPU writes nothing, so it reads 'stale' and mode 1
   * falls back to 2 (independent frames only). */
  setenv("NORD2_METAL_CHAIN","1",1);
  struct fake g={.job=1ull<<32,.chain_model=1};uintptr_t gb=open_bridge(&g);unsetenv("NORD2_METAL_CHAIN");
  unsigned before=g.submitted;assert(n2mb_chain_begin(gb)==2&&g.submitted==before+1&&g.last_frames==4);
  n2mb_chain_stats(gb,st);assert(st[9]==2&&st[10]==2&&st[0]==1&&st[1]==4);
  assert(!n2mb_chain_end(gb,&job)&&!job);
  close_bridge(gb,&g);
 }
 {/* C6s kernel-call churn (device C6r: ~2.9 mem + ~2.9 map kernel calls
    * per job). Steady state must make no ALLOC/RELEASE/map/unmap call:
    * (1) 40 distinct fragment binaries cycled (the old 16-slot code cache
    * thrashed), (2) client buffers allocated, mapped, written, unmapped and
    * released every frame (pooled, kept mapped, zeroed on reuse), (3)
    * feedback-snapshot draws (one reused snapshot BO). */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=2,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  enum {NS=40};static uint8_t fs[NS][sizeof(n2_preload_fs)+64];struct tegu_generic_draw_shader sh[NS];
  for(unsigned i=0;i<NS;i++){memcpy(fs[i],n2_preload_fs,sizeof(n2_preload_fs));memset(fs[i]+sizeof(n2_preload_fs),0,64);
   fs[i][sizeof(n2_preload_fs)+8]=(uint8_t)(i+1);sh[i]=n2_preload_shader;sh[i].fragment_code=fs[i];sh[i].fragment_bytes=sizeof(n2_preload_fs)+64;}
  uint64_t st0[N2MB_SITES][2],st1[N2MB_SITES][2],code[3];uint64_t out[2];unsigned n;
  for(unsigned i=0;i<NS;i++){n=2;assert(!n2mb_draw(b,&sh[i],&q,out,&n));}
  n2mb_kcall_site_stats(b,st0,code);unsigned allocs=f.next;
  for(unsigned round=0;round<3;round++)for(unsigned i=0;i<NS;i++){n=2;assert(!n2mb_draw(b,&sh[i],&q,out,&n));}
  n2mb_kcall_site_stats(b,st1,code);
  unsigned long long mem=0,map=0;for(unsigned k=0;k<N2MB_SITES;k++){mem+=st1[k][0]-st0[k][0];map+=st1[k][1]-st0[k][1];}
  fprintf(stderr,"CHURN code draws=%u mem=%llu map=%llu code_hits=%llu misses=%llu evictions=%llu cache=%u\n",3*NS,mem,map,
   (unsigned long long)code[0],(unsigned long long)code[1],(unsigned long long)code[2],n2mb_code_cache_count(b));
  assert(!mem&&!map&&f.next==allocs&&!code[2]&&n2mb_code_cache_count(b)==NS+1);
  /* (2) client buffers per frame */
  uint64_t cp[4];unsigned fresh=0;
  for(unsigned frame=0;frame<50;frame++){
   if(frame==1){n2mb_kcall_site_stats(b,st0,0);allocs=f.next;}
   uint64_t bytes=2048+(frame%3)*8192,o2[2];n=2;assert(!n2mb_scalar(b,1,&bytes,1,o2,&n)&&n==2&&o2[1]>=bytes);
   uint64_t a=0,sz=0;assert(!n2mb_map(b,(unsigned)o2[0],0,&a,&sz,1)&&a);
   for(uint64_t k=0;k<bytes;k++)assert(!((uint8_t *)(uintptr_t)a)[k]); /* zero, as a fresh allocation */
   memset((void *)(uintptr_t)a,0x5a,(size_t)bytes);
   assert(!n2mb_unmap(b,(unsigned)o2[0],0,a));n=0;assert(!n2mb_scalar(b,2,o2,1,0,&n));
   if(frame==0)fresh=f.next;
  }
  n2mb_kcall_site_stats(b,st1,0);mem=map=0;for(unsigned k=0;k<N2MB_SITES;k++){mem+=st1[k][0]-st0[k][0];map+=st1[k][1]-st0[k][1];}
  n2mb_client_pool_stats(b,cp);
  fprintf(stderr,"CHURN client frames=49 mem=%llu map=%llu pool_hits=%llu misses=%llu puts=%llu entries=%llu\n",mem,map,
   (unsigned long long)cp[0],(unsigned long long)cp[1],(unsigned long long)cp[2],(unsigned long long)cp[3]);
  assert(fresh&&mem<=4&&map<=4&&f.next<=allocs+2); /* only the first use of each of the 3 size classes */
  /* (3) feedback snapshot draws: one snapshot BO, reused */
  q.load_action=1;q.bindings[0].memory.token=r;
  n=2;assert(!n2mb_draw(b,&sh[0],&q,out,&n));n2mb_kcall_site_stats(b,st0,0);allocs=f.next;
  for(unsigned i=0;i<20;i++){n=2;assert(!n2mb_draw(b,&sh[i%NS],&q,out,&n));}
  n2mb_kcall_site_stats(b,st1,0);mem=map=0;for(unsigned k=0;k<N2MB_SITES;k++){mem+=st1[k][0]-st0[k][0];map+=st1[k][1]-st0[k][1];}
  fprintf(stderr,"CHURN snapshot draws=20 mem=%llu map=%llu\n",mem,map);
  assert(!mem&&!map&&f.next==allocs);
  close_bridge(b,&f);
 }
 {/* GPU-only client BOs (private render targets / textures, ~10.6 MiB on the phone) are pooled: the second frame's texture of the same size class reuses the first one,
   * the kernel sees no steady ALLOC/RELEASE, and the contents are not cleared (undefined in Metal). */
  struct fake f={0};uintptr_t b=open_bridge(&f);
  uint64_t bytes=11075584,in[2]={bytes,2},o1[3]={0},o2[3]={0},o3[3]={0};unsigned n=3,n1=1;
  assert(!n2mb_scalar(b,1,in,2,o1,&n));assert(o1[0]&&o1[1]>=bytes);
  uint64_t first=o1[0],cls=o1[1];unsigned allocs=f.next;
  assert(!n2mb_scalar(b,2,&first,1,NULL,&n1)); /* released by the frontend: pooled, not freed */
  {uint64_t cp[4];n2mb_client_pool_stats(b,cp);assert(cp[2]==1&&cp[3]==1);}
  n=3;assert(!n2mb_scalar(b,1,in,2,o2,&n));
  assert(o2[0]==first&&o2[1]==cls&&f.next==allocs); /* same BO, no new kernel allocation */
  {uint64_t cp[4];n2mb_client_pool_stats(b,cp);assert(cp[0]==1&&cp[3]==0);}
  /* A CPU-mappable request of the same size must not take (or be handed) a GPU-only BO. */
  {uint64_t cpuin[1]={bytes};n=3;assert(n2mb_scalar(b,1,cpuin,1,o3,&n)||o3[0]!=first);}
  n1=1;assert(!n2mb_scalar(b,2,&first,1,NULL,&n1));
  for(unsigned i=0;i<32;i++){n=3;assert(!n2mb_scalar(b,1,in,2,o2,&n));assert(o2[0]==first);n1=1;assert(!n2mb_scalar(b,2,&first,1,NULL,&n1));}
  assert(f.next==allocs+(o3[0]?1u:0u)||f.next<=allocs+1);
  close_bridge(b,&f);
 }
 {/* Texture backing (ALLOC flag bit2): an idle, never-CPU-mapped buffer of the same size class is reused (CA creates ~10.6 MiB intermediates every frame); a plain
   * request of that size never receives it (it would expect zeroed memory), and a texture buffer the CPU mapped is not pooled. */
  struct fake f={0};uintptr_t b=open_bridge(&f);
  uint64_t bytes=11075584,tin[2]={bytes,4},plain[1]={bytes},o1[3]={0},o2[3]={0},o3[3]={0};unsigned n=3,n1=1;
  assert(!n2mb_scalar(b,1,tin,2,o1,&n));assert(o1[0]&&o1[1]>=bytes);
  uint64_t first=o1[0];unsigned allocs=f.next;
  assert(!n2mb_scalar(b,2,&first,1,NULL,&n1));
  {uint64_t cp[4];n2mb_client_pool_stats(b,cp);assert(cp[2]==1&&cp[3]==1);}
  n=3;assert(!n2mb_scalar(b,1,plain,1,o3,&n));assert(o3[0]!=first); /* plain request: not served from the texture pool */
  n=3;assert(!n2mb_scalar(b,1,tin,2,o2,&n));assert(o2[0]==first&&f.next==allocs+1); /* only the plain request allocated */
  n1=1;assert(!n2mb_scalar(b,2,&o3[0],1,NULL,&n1));
  for(unsigned i=0;i<16;i++){n1=1;assert(!n2mb_scalar(b,2,&first,1,NULL,&n1));n=3;assert(!n2mb_scalar(b,1,tin,2,o2,&n));assert(o2[0]==first);}
  {uint64_t cpu=0,mapped=0;assert(!n2mb_map(b,(unsigned)first,0,&cpu,&mapped,1)&&cpu);assert(!n2mb_unmap(b,(unsigned)first,0,cpu));
   uint64_t before=f.live;n1=1;assert(!n2mb_scalar(b,2,&first,1,NULL,&n1));assert(f.live==before-1);} /* CPU-mapped texture buffer: released for real */
  close_bridge(b,&f);
 }
 puts("PASS texture-backing BO pool: reused by the next texture of the same size, never handed to a plain buffer, a CPU-mapped one is freed");
 puts("PASS GPU-only client BO pool: a released private texture is reused by the next allocation of its size class, no kernel ALLOC/RELEASE per frame");
 puts("PASS kernel-call churn: 40 cycled shaders, per-frame client buffers and feedback snapshots make no steady ALLOC/RELEASE/map/unmap call");
 puts("PASS chained submission: frames linked in one job (renumbered tiler chain, barriered fragment chain, distinct heap slices), dependent/vertex/mode-2/full/handle flushes, ordering calls, clears and batches");
 puts("PASS matrix wait failure: no pixel readback or later jobs; pending buffers retained until context close");
 puts("PASS graphics CLEAR/LOAD,feedback snapshot,ordered2-stage submission,allocation/map/submit/wait failures");
 puts("PASS real TGP -> arch9 JM transport: eight lifetime/failure cases,80 concurrent dispatches,CSF rejection; no hardware execution");
}
