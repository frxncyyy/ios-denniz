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
  if(f->fail_seq){f->fail_seq--;out[0]=5;out[2]=f->job-f->inflight+1;}else{out[0]=4;out[2]=0;}
  f->pending=0;f->inflight=0;
  for(unsigned h=1;h<=f->next;h++)if(f->buffers[h].active&&f->buffers[h].released){f->buffers[h].released=0;collect(f,h);}
  return 0;
 }
 case NORD2_GPU_SEL_WAIT: {
  assert(ni==2&&in[0]==f->job&&in[1]==5000&&*no==1);
  f->waited++;fake_wait_job(f,in[0]);if(f->fail_wait){f->fail_wait--;return -13;}
  if(f->bad_state){out[0]=2;return 0;}f->pending=0;f->inflight=0;out[0]=4;return 0;
 }
 case NORD2_GPU_SEL_SURFACE_RESERVE: {
  if(!f->resv_model)return (int)0xe00002c7u;
  assert(ni==1&&*no==1&&in[0]<=8&&size==in[0]*4&&(!in[0]||bytes));
  f->resv_calls++;f->resv_last_n=(unsigned)in[0];if(in[0])memcpy(f->resv_handles,bytes,size);
  for(unsigned k=0;k<in[0];k++)assert(f->resv_handles[k]<COUNT&&f->buffers[f->resv_handles[k]].active);
  out[0]=in[0]?++f->resv_next:0;return 0;
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
static void lock(void *p){assert(!pthread_mutex_lock(&((struct fake *)p)->mutex));}
static void unlock(void *p){assert(!pthread_mutex_unlock(&((struct fake *)p)->mutex));}
static uintptr_t open_bridge(struct fake *f) {
 assert(!pthread_mutex_init(&f->mutex,NULL));struct n2mb_ops ops={.opaque=f,.call=call,.map=map,.unmap=unmap,.close=close_raw,.lock=lock,.unlock=unlock};
 uintptr_t b=n2mb_open(&ops);assert(b);uint64_t caps[4];unsigned n=4;
 assert(!n2mb_scalar(b,0,NULL,0,caps,&n)&&n==4&&(caps[1]&8));return b;
}
static void close_bridge(uintptr_t b,struct fake *f){assert(!n2mb_close(b)&&f->closed&&!f->live&&!f->maps);assert(!pthread_mutex_destroy(&f->mutex));}

static uint64_t resource(uintptr_t b){uint64_t bytes=2048,out[2];unsigned n=2;assert(!n2mb_scalar(b,1,&bytes,1,out,&n)&&n==2&&out[1]==16384);return out[0];}
static unsigned char vs_b[384],fs_b[128];
static const unsigned char *code_at(struct fake *f,uint64_t va){
 for(unsigned h=1;h<=f->next;h++){struct buffer *b=&f->buffers[h];
  if(b->active&&b->data&&va>=b->va&&va<b->va+b->size)return b->data+(va-b->va);}
 return NULL;
}
int main(void){
 memcpy(vs_b,n2_preload_vs,sizeof(vs_b));vs_b[sizeof(vs_b)-1]^=0x5a; /* tail padding: different bytes, same program */
 memcpy(fs_b,n2_preload_fs,sizeof(fs_b));fs_b[sizeof(fs_b)-1]^=0x5a;
 struct tegu_generic_draw_shader A=n2_preload_shader,B=n2_preload_shader;B.vertex_code=vs_b;B.fragment_code=fs_b;
 const struct tegu_generic_draw_shader *const P[][4]={
  {&A,&A,&B,0},{&B,&A,&A,0},{&A,&B,&A,0},{&A,&B,&B,0},{&B,&B,&A,0},{&A,&A,&B,&B},{&B,&A,&A,&B}};
 unsigned bad=0;
 for(unsigned p=0;p<sizeof(P)/sizeof(P[0]);p++){
  unsigned count=P[p][3]?4:3;
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q[4];struct n2mb_batch_draw dr[4];
  for(unsigned k=0;k<count;k++){
   q[k]=(struct tegu_generic_draw_request){.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
   q[k].bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
   dr[k]=(struct n2mb_batch_draw){P[p][k],&q[k],NULL,NULL};
  }
  uint64_t o[2];unsigned n=2;
  assert(!n2mb_draw_batch(b,dr,count,o,&n)&&f.submitted==1&&n2mb_batch_last_count(b)==count);
  /* The command BO: count draw blocks + one preload block, the only BO of that size. */
  const unsigned char *cmd=NULL;
  for(unsigned h=1;h<=f.next;h++)if(f.buffers[h].active&&f.buffers[h].size==(uint64_t)(count+1)*N2R_BLOCK){assert(!cmd);cmd=f.buffers[h].data;}
  assert(cmd);
  for(unsigned k=0;k<count;k++){
   const struct tegu_generic_draw_shader *s=P[p][k];const unsigned char *blk=cmd+(size_t)k*N2R_BLOCK;
   uint64_t vs=get64(blk+1536+8)-s->position_offset,fs=get64(blk+1536+64+8);
   const unsigned char *vc=code_at(&f,vs),*fc=code_at(&f,fs);
   int ok=vc&&fc&&!memcmp(vc,s->vertex_code,s->vertex_bytes)&&!memcmp(fc,s->fragment_code,s->fragment_bytes);
   if(!ok){bad++;printf("FAIL pattern %u draw %u runs another draw's %s code\n",p,k,
    vc&&!memcmp(vc,s->vertex_code,s->vertex_bytes)?"fragment":"vertex");}
  }
  close_bridge(b,&f);
 }
 if(bad){printf("FAIL %u draw(s) of mixed-shader batches run the wrong program\n",bad);return 1;}
 puts("PASS every draw of a mixed-shader batch runs its own vertex/fragment code");
 return 0;
}
