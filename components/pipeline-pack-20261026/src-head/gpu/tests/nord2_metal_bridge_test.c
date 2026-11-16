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
#define COUNT 600
struct buffer {uint64_t va,size;unsigned char *data;unsigned mapped,active,released,exec;};
struct fake {
 unsigned last_chain;
 pthread_mutex_t mutex;struct buffer buffers[COUNT];uint32_t next;
 unsigned live,maps,closed,submitted,waited,bad_state,fail_alloc,fail_map,fail_submit,fail_wait,fail_release;
 unsigned geometry_fixture,heap_logs,before_logs,after_logs,log_bytes,nonzero_before_logs;
 unsigned sync_calls,fail_sync_at;
 /* tiler matrix model */
 unsigned render_model,tm_logs,adopt_logs,draw_submits,heap_allocs,const_fs_seen,nosec_seen,mask_seen[4];
 uint64_t heap_va,last_cmd_va;uint32_t last_mask,last_chunk,last_heap_size;char adopt_text[240];
 uint64_t job;unsigned pending;const unsigned char *pipeline;size_t pipeline_size;
};
static uint64_t get64(const void *v) {const unsigned char *p=v;return tegu_pipeline_word(p)|((uint64_t)tegu_pipeline_word(p+4)<<32);}
static void collect(struct fake *f,unsigned h) {struct buffer *b=&f->buffers[h];assert(b->active);free(b->data);b->data=NULL;b->active=0;f->live--;}
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
  unsigned h=++f->next;assert(h<COUNT);struct buffer *b=&f->buffers[h];
  b->size=(in[0]+16383)&~16383ull;b->va=0x1000000000ull+h*0x2000000ull; /* 32 MiB apart: room for a 16 MiB heap */
  if(in[0]>=(2u<<20))f->heap_allocs++;
  b->data=calloc(1,b->size);assert(b->data);b->active=1;b->exec=(unsigned)in[1];f->live++;
  out[0]=h;out[1]=b->va;out[2]=b->size;*no=3;return 0;
 }
 case NORD2_GPU_SEL_SYNC:
  assert(ni==2&&in[0]<COUNT&&in[1]==0&&!f->pending);
  return ++f->sync_calls==f->fail_sync_at?-16:0;
 case NORD2_GPU_SEL_RELEASE: {
  assert(ni==1&&in[0]<COUNT&&!size);if(f->fail_release){f->fail_release--;return -15;}
  struct buffer *b=&f->buffers[in[0]];assert(b->active&&!b->mapped);
  if(f->pending)b->released=1;else collect(f,(unsigned)in[0]);return 0;
 }
 case NORD2_GPU_SEL_SUBMIT_JM: {
  assert(!ni&&size==160&&bytes&&*no==1);
  const struct nord2_gpu_submit_request *r=bytes;
  assert(r->version==2);
  if(r->kind==2){
   assert(r->nr_handles>=5&&r->heads[0]&&r->heads[1]);
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
    if(!f->heap_va)f->heap_va=base;assert(base==f->heap_va); /* one persistent heap */
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
   assert(get64(c->data+N2R_FRAGMENT+40)==(c->va|1));
   {/* Tiler chain inside the command buffer: every job at a block's JOB
     * offset, indices 1.. ascending along JOB_HEADER.next, each depending on
     * its predecessor, block 0 (the frame owner) included, ending at next=0. */
    uint64_t h=r->heads[0];unsigned steps=0,seen0=0;
    while(h){
     assert(h>=c->va&&h<c->va+c->size&&((h-c->va)%N2R_BLOCK)==N2R_JOB&&steps<64);
     const uint8_t *jh=c->data+(h-c->va);uint32_t w4,w5;memcpy(&w4,jh+16,4);memcpy(&w5,jh+20,4);
     steps++;assert((w4>>16)==steps&&(w5&0xffff)==(steps>1?steps-1:0));
     if(h==c->va+N2R_JOB)seen0=1;
     h=get64(jh+24);
    }
    assert(seen0&&steps>=1);f->last_chain=steps;
   }
   if(f->fail_submit){f->fail_submit--;return -12;}
   f->submitted++;f->pending=1;out[0]=++f->job;*no=1;return 0;
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
 case NORD2_GPU_SEL_WAIT: {
  assert(ni==2&&in[0]==f->job&&in[1]==5000&&*no==1);
  f->waited++;if(f->fail_wait){f->fail_wait--;return -13;}
  if(f->bad_state){out[0]=2;return 0;}f->pending=0;out[0]=4;return 0;
 }
 default:assert(!"unexpected kernel selector");return -99;
 }
}
static int map(void *p,unsigned h,uint64_t *address,uint64_t *size) {
 struct fake *f=p;if(f->fail_map){f->fail_map--;return -11;}
 assert(h<COUNT);struct buffer *b=&f->buffers[h];assert(b->active&&!b->mapped);
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
  assert(n2mb_map(b,(unsigned)r,0,&cpu,&mapped,1));assert(!n2mb_unmap(b,(unsigned)r,0,cpu));
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
   assert(!n2mb_scalar(b,2,&r,1,NULL,&n));assert(n2mb_scalar(b,2,&r,1,NULL,&n));assert(!f.live);
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
  if(!fault){assert(!e&&f.submitted==1&&f.waited==1&&f.live==2+n2mb_code_cache_count(b)&&n2mb_code_cache_count(b)==2&&f.heap_allocs==1);n=0;assert(!n2mb_scalar(b,2,&r,1,NULL,&n));}
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
  assert(f.next==allocs+1); /* only the command block: no snapshot, no code upload */
  assert(f.live==3+n2mb_code_cache_count(b)&&n2mb_code_cache_count(b)==2&&f.submitted==2);
  /* Framebuffer fetch (own pixel) under LOAD: also in place, no snapshot. */
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_ATTACHMENT};
  allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));assert(f.next==allocs+1);
  /* Sampling the target itself still takes the bounded snapshot. */
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=r,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  allocs=f.next;n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n));assert(f.next==allocs+2);
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
  assert(f.next-allocs==1); /* only the command block */
  q[0].load_action=1;
  q[2].load_action=2;n=2;assert(n2mb_draw_batch(b,dr,3,out,&n)==(int)0xe00002c2u&&f.submitted==2);q[2].load_action=1; /* mid-pass clear */
  q[1].bindings[0].kind=TEGU_GENERIC_DRAW_ATTACHMENT;q[1].bindings[0].memory=(struct tegu_generic_draw_memory){0};
  n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)==(int)0xe00002c2u&&f.submitted==2); /* framebuffer fetch */
  q[1].bindings[0]=q[0].bindings[0];q[1].bindings[0].memory.token=r;
  n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)==(int)0xe00002c2u&&f.submitted==2); /* target sampled */
  q[1].bindings[0]=q[0].bindings[0];q[1].row_bytes=128;
  n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)==(int)0xe00002c2u&&f.submitted==2); /* different target layout */
  q[1].row_bytes=64;f.fail_submit=1;n=2;assert(n2mb_draw_batch(b,dr,2,out,&n)&&f.submitted==2);
  n=2;assert(!n2mb_draw_batch(b,dr,2,out,&n)&&f.submitted==3); /* failure did not break the bridge */
  struct n2mb_batch_draw many[N2MB_BATCH_MAX+1];for(unsigned k=0;k<=N2MB_BATCH_MAX;k++)many[k]=dr[1];many[0]=dr[0];
  n=2;assert(n2mb_draw_batch(b,many,N2MB_BATCH_MAX+1,out,&n)==(int)0xe00002c2u);
  n=2;assert(!n2mb_draw_batch(b,many,N2MB_BATCH_MAX,out,&n)&&f.last_chain==N2MB_BATCH_MAX+1);
  assert(f.live==3+n2mb_code_cache_count(b)); /* two resources + heap + cached code */
  close_bridge(b,&f);
 }
 puts("PASS render-pass batching: one job per segment, preload once, exact refusals before submission");
 {/* Budget pressure: a refused client allocation first reclaims the shader
    * code cache and retries; nothing is reclaimed while it is in use. */
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b),t=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=64,.load_action=1,.vertex_count=3,.scissor={0,0,16,16},.binding_count=1,.target_token=r,.target_length=16384};
  q.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,.kind=TEGU_GENERIC_DRAW_COMBINED,.memory={.token=t,.bytes=1024},.width=16,.height=16,.row_bytes=64,.format=80};
  uint64_t out[2];unsigned n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&n2mb_code_cache_count(b)==2);
  uint64_t priv=0,imp=0,in=0,code=0;unsigned bos=0;n2mb_memory_stats(b,&priv,&imp,&in,&code,&bos);
  assert(code==2*16384&&in>=code+(2u<<20)&&!imp&&bos==f.live);
  f.fail_alloc=1;uint64_t bytes=4096,o[2];n=2;assert(!n2mb_scalar(b,1,&bytes,1,o,&n)&&n2mb_code_cache_count(b)==0);
  f.fail_alloc=1;bytes=4096;n=2;assert(n2mb_scalar(b,1,&bytes,1,o,&n)&&!f.fail_alloc); /* nothing left to reclaim */
  n=2;assert(!n2mb_draw(b,&n2_preload_shader,&q,out,&n)&&n2mb_code_cache_count(b)==2);
  close_bridge(b,&f);
 }
 puts("PASS budget pressure: code cache reclaimed before a client allocation fails; memory stats");
 for(unsigned fault=0;fault<5;fault++){
  struct fake f={.job=1ull<<32};uintptr_t b=open_bridge(&f);uint64_t r=resource(b);
  struct tegu_generic_draw_request q={.width=16,.height=16,.row_bytes=128,.target_format=115,.target_token=r,.target_length=16384};
  const double rgba[4]={.25,.5,.75,1};assert(!n2r_clear(115,rgba,q.clear_words));
  if(fault==1)f.fail_alloc=1;if(fault==2)f.fail_map=1;if(fault==3)f.fail_submit=1;if(fault==4)f.fail_wait=1;
  uint64_t out[2];unsigned n=2;int e=n2mb_clear(b,&q,out,&n);
  if(!fault)assert(!e&&f.submitted==1&&f.waited==1&&f.live==2+n2mb_code_cache_count(b)&&f.heap_allocs==1);else assert(e);
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
  assert(f.heap_allocs==1&&f.draw_submits==12&&f.live==2+n2mb_code_cache_count(b)&&f.last_mask==0x82&&f.last_chunk==3&&f.last_heap_size==(2u<<20));
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
  unsigned matrix_draws=model==2?10:11;  assert(f.draw_submits==12&&f.heap_allocs==1&&f.adopt_logs==1); /* attempts, incl. an injected refusal */
  assert(f.mask_seen[0]==1&&f.mask_seen[2]==3&&f.const_fs_seen==3&&f.nosec_seen==2);
  assert(f.tm_logs==1+3*matrix_draws+(model==2)+1);
  if(model==0)assert(strstr(f.adopt_text,"adopt=-1 heap=2048K chunk=3 mask=082")&&f.last_chunk==3);
  else assert(strstr(f.adopt_text,"adopt=1 heap=2048K chunk=0 mask=082")&&f.last_chunk==0&&f.last_heap_size==(2u<<20));
  n=2;assert(!n2mb_clear(b,&q,out,&n));assert(f.draw_submits==13&&f.adopt_logs==1); /* runs once */
  assert(f.live==2+n2mb_code_cache_count(b)); /* matrix buffers released; heap and shader code retained */
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
 puts("PASS matrix wait failure: no pixel readback or later jobs; pending buffers retained until context close");
 puts("PASS graphics CLEAR/LOAD,feedback snapshot,ordered2-stage submission,allocation/map/submit/wait failures");
 puts("PASS real TGP -> arch9 JM transport: eight lifetime/failure cases,80 concurrent dispatches,CSF rejection; no hardware execution");
}
