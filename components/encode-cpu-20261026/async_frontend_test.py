#!/usr/bin/env python3
"""Host test: frontend side of asynchronous submission (kernel 0090 WAIT_SEQ).

Builds the real frontend + real bridge + the shared fake kernel (bridge test
fake with its 0090 model: WAIT_SEQ, in-flight window -> kIOReturnBusy,
busy-consumed release, SYNC refused while jobs are pending) and wraps the fake
so that GPU writes of a JM job become visible only when the job is reaped
(WAIT / WAIT_SEQ), in submission order.  Checks:
  old  - a kernel without selector 12 keeps synchronous submission;
  blit - draw into a texture, CPU blit texture->buffer in the same command:
         the blit sees the draw only if the frontend reaped before it;
  many - 150 per-draw jobs in one command cross the 96-job window (busy ->
         reap -> resubmit), all reaped at the command end, one status;
  fail - an injected GPU failure is reported as the command error.
No GPU, phone or publish. Generated sources and binaries stay in this folder.
"""
import os, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
G = HERE.parents[1]
CA = G / 'build/c6f-lowering-20261026/ca_backboardd_host_test.py'
src = CA.read_text()
src = src.replace("HERE = pathlib.Path(__file__).resolve().parent", "HERE = pathlib.Path(%r)" % str(HERE))
src = src.replace("G = HERE.parents[1]", "G = pathlib.Path(%r)" % str(G))
# The shared harness inlines the working-tree compute_frontend.m (since
# 2026-10-26); FA_FRONTEND substitutes another copy (mutation checks).
if os.environ.get('FA_FRONTEND'):
    _old = "compute = (TEgu / 'compute_frontend.m').read_text()"
    _new = "compute = open(%r).read()" % os.environ['FA_FRONTEND']
    assert src.count("text = HARNESS.read_text()\n") == 1
    src = src.replace("text = HARNESS.read_text()\n",
                      "text = HARNESS.read_text()\nassert text.count(%r) == 1\ntext = text.replace(%r, %r)\n" % (_old, _old, _new), 1)
start = src.index("main = r'''")
end = src.index("\n'''\n", start)
ca_main = src[start + len("main = r'''"):end]
helpers = ca_main.split('int main(int argc,char **argv){', 1)[0]

wrapper = r'''
/* ---- deferred-execution wrapper around the shared fake kernel ---- */
static int call_base(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os);
#define FA_MAX 160
static int fa_mode,fa_old_kernel;static unsigned fa_count,fa_fail_nth,fa_submits,fa_seq_calls,fa_wait_calls,fa_busy;
static struct {uint64_t id;unsigned n;uint32_t h[24];unsigned char *after[24];uint64_t size[24];} fa_q[FA_MAX];
static unsigned fa_apply(struct fake *f,uint64_t seq){
 unsigned k=0;
 while(k<fa_count&&fa_q[k].id<=seq){
  for(unsigned m=0;m<fa_q[k].n;m++){struct buffer *bb=&f->buffers[fa_q[k].h[m]]; /* skipped: released meanwhile */
   if(bb->active&&bb->data&&bb->size>=fa_q[k].size[m])memcpy(bb->data,fa_q[k].after[m],fa_q[k].size[m]);free(fa_q[k].after[m]);}
  k++;
 }
 memmove(fa_q,fa_q+k,(fa_count-k)*sizeof(fa_q[0]));fa_count-=k;return k;
}
static uint64_t fa_seq_args[64];static unsigned fa_seq_n;
/* The fake kernel is single-threaded; with scheduled-at-submission the app
 * thread submits while the finish FIFO thread waits: serialize every call
 * (a real kernel call is atomic per selector the same way). */
#include <pthread.h>
static pthread_mutex_t fa_call_mu=PTHREAD_MUTEX_INITIALIZER;
static int call_unlocked(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os);
static int call(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os){
 pthread_mutex_lock(&fa_call_mu);int rc=call_unlocked(p,s,in,ni,bytes,size,out,no,ob,os);pthread_mutex_unlock(&fa_call_mu);return rc;
}
static int call_unlocked(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os){
 struct fake *f=p;
 if(s==12&&fa_old_kernel)return (int)0xe00002c2u; /* P1/P2 kernel: unknown selector */
 if(!fa_mode)return call_base(p,s,in,ni,bytes,size,out,no,ob,size?os:os);
 if(s==NORD2_GPU_SEL_SUBMIT_JM){
  const struct nord2_gpu_submit_request *r=bytes;
  unsigned char *before[24]={0};uint32_t hs[24];unsigned nw=0;
  for(unsigned k=0;k<r->nr_handles&&nw<24;k++)if(r->write_mask&(1u<<k)){
   unsigned h=r->handles[k];if(h>=COUNT||!f->buffers[h].active)continue;
   hs[nw]=h;before[nw]=malloc(f->buffers[h].size);assert(before[nw]);memcpy(before[nw],f->buffers[h].data,f->buffers[h].size);nw++;
  }
  int rc=call_base(p,s,in,ni,bytes,size,out,no,ob,os);
  if(rc){if(rc==(int)0xe00002d5u)fa_busy++;for(unsigned m=0;m<nw;m++)free(before[m]);return rc;}
  assert(fa_count<FA_MAX);
  fa_q[fa_count].id=out[0];fa_q[fa_count].n=nw;
  for(unsigned m=0;m<nw;m++){ /* the fake wrote now: keep the result, show the old bytes until reaped */
   struct buffer *b=&f->buffers[hs[m]];fa_q[fa_count].h[m]=hs[m];fa_q[fa_count].size[m]=b->size;
   fa_q[fa_count].after[m]=malloc(b->size);assert(fa_q[fa_count].after[m]);
   memcpy(fa_q[fa_count].after[m],b->data,b->size);memcpy(b->data,before[m],b->size);free(before[m]);
  }
  fa_count++;fa_submits++;
  if(fa_fail_nth&&fa_submits==fa_fail_nth)f->fail_seq=1;
  return 0;
 }
 if(s==12){
  fa_seq_calls++;if(fa_seq_n<64)fa_seq_args[fa_seq_n++]=in[0];
  unsigned reaped=fa_apply(f,in[0]); /* writes land before the reap returns */
  if(in[0]&&in[0]<f->job&&f->seq_model){ /* partial reap (pipelined finish): jobs <= seq only */
   assert(ni==2&&*no==3&&in[1]<=5000&&reaped<=f->inflight);
   f->inflight-=reaped;out[0]=4;out[1]=reaped;out[2]=0;return 0;
  }
  return call_base(p,s,in,ni,bytes,size,out,no,ob,os);
 }
 if(s==NORD2_GPU_SEL_WAIT){fa_wait_calls++;fa_apply(f,in[0]);return call_base(p,s,in,ni,bytes,size,out,no,ob,os);}
 return call_base(p,s,in,ni,bytes,size,out,no,ob,os);
}
'''

main = helpers + r'''
static unsigned fa_checks;
#include <pthread.h>
static pthread_mutex_t fa_mu=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t fa_cv=PTHREAD_COND_INITIALIZER;
static struct {void *ctx;void (*fn)(void *);} fa_work[64];static unsigned fa_wh,fa_wt,fa_gate,fa_worker_started;static int fa_target;
static void *fa_worker(void *arg){(void)arg;
 for(;;){pthread_mutex_lock(&fa_mu);while(fa_gate||fa_wh==fa_wt)pthread_cond_wait(&fa_cv,&fa_mu);
  void *ctx=fa_work[fa_wh%64].ctx;void (*fn)(void *)=fa_work[fa_wh%64].fn;fa_wh++;pthread_mutex_unlock(&fa_mu);fn(ctx);}
 return 0;}
static void fa_retain(void *p){(void)p;}
static void fa_release(void *p){(void)p;}
static void fa_schedule(void *target,void *ctx,void (*fn)(void *)){assert(target==&fa_target);
 pthread_mutex_lock(&fa_mu);
 if(!fa_worker_started){pthread_t t;assert(!pthread_create(&t,0,fa_worker,0));pthread_detach(t);fa_worker_started=1;}
 assert(fa_wt-fa_wh<64);fa_work[fa_wt%64].ctx=ctx;fa_work[fa_wt%64].fn=fn;fa_wt++;pthread_cond_signal(&fa_cv);pthread_mutex_unlock(&fa_mu);}
static void fa_set_gate(unsigned closed){pthread_mutex_lock(&fa_mu);fa_gate=closed;pthread_cond_broadcast(&fa_cv);pthread_mutex_unlock(&fa_mu);}
static TeguCommand *fa_draw_command(TeguComputeDevice *d,TeguQueue *q,TeguRenderPipeline *p,TeguTexture *t,TeguBuffer *vb,
 TeguTexture *src,TeguSampler *samp,unsigned draws,TeguBuffer *readback){
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[t retain];pass->color->load=2;pass->color->store=1;
 pass->passWidth=t->textureWidth;pass->passHeight=t->textureHeight;pass->arrayLength=1;pass->sampleCount=1;
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e&&!e->inert);
 [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
 for(unsigned k=0;k<4;k++){[e setFragmentTexture:src atIndex:k];[e setFragmentSamplerState:samp atIndex:k];}
 for(unsigned k=0;k<draws;k++){[e setScissorRect:(TeguScissorRect){k%32,k%32,32,32}];[e drawPrimitives:3 vertexStart:0 vertexCount:3];}
 [e endEncoding];[pass release];
 if(readback){
  TeguBlitEncoder *be=[c blitCommandEncoder];
  [be copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:(TeguOrigin){0,0,0} sourceSize:(MTLSize){t->textureWidth,t->textureHeight,1}
   toBuffer:readback destinationOffset:0 destinationBytesPerRow:t->rowBytes destinationBytesPerImage:t->rowBytes*t->textureHeight];
  [be endEncoding];
 }
 return c;
}
int main(int argc,char **argv){
 assert(argc>=3);
 tegu_host_process_backboardd=1;
 /* Pipelining is an A/B toggle (ab_schedule.inc): on, so the pipelined queue below uses its FIFO. */
 setenv("TEGU_ASYNC_PIPELINE","1",1);
 struct fake f={0};f.render_model=1;uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 TeguFunction *dsv=c6f_function(d,argv[1],"downsample_blur_vert_lph"),*ds4=c6f_function(d,argv[2],"downsample_4_frag_lph");
 id error=0;RenderDescriptor *pd=c6f_ca_descriptor(dsv,ds4,80,15,0,0);
 TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(p&&p->genericDraw);
 TeguQueue *q=[d newCommandQueue];TeguBuffer *vb=[d newBufferWithLength:4096 options:0];
 TeguTexture *src=new_sample(d,80,64,64,1,2,1);fill_texture(src,0x80402010u);TeguSampler *samp=new_sampler(d,3);
 TeguTexture *t=c6f_texture(d,80,64,64,1,5,0x20);assert(t&&t->rowBytes==256);
 TeguBuffer *rb=[d newBufferWithLength:256*64 options:0];
 tegu_jm_batch_force=0; /* one kernel job per draw */
 /* old: a kernel without WAIT_SEQ stays synchronous */
 fa_old_kernel=1;(void)n2mb_async(bridge,1);assert(n2mb_async(bridge,2)==0);fa_old_kernel=0;
 {memset([rb contents],0,256*64);TeguCommand *c=fa_draw_command(d,q,p,t,vb,src,samp,2,rb);[c commit];[c waitUntilCompleted];
  assert([c status]==4&&f.waited>=2&&!f.seq_waits);
  const uint32_t *px=[rb contents];assert(px[0]&&!memcmp([rb contents],(unsigned char *)tegu_buffer_bytes(t->buffer)+t->mip[0].offset,256*64));
  fprintf(stderr,"FA_OLD sync waits=%u seq_waits=%u pixel=%08x\n",f.waited,f.seq_waits,px[0]);fa_checks++;}
 /* async on: probe once */
 f.seq_model=1;f.window=96;fa_mode=1;
 assert(n2mb_async(bridge,1)==0&&n2mb_async(bridge,2)==1);
 /* blit: CPU readback of a texture drawn earlier in the same command */
 {memset((unsigned char *)tegu_buffer_bytes(t->buffer)+t->mip[0].offset,0,256*64);memset([rb contents],0,256*64);
  unsigned waits0=f.waited,seq0=fa_seq_calls;
  TeguCommand *c=fa_draw_command(d,q,p,t,vb,src,samp,3,rb);[c commit];[c waitUntilCompleted];
  const uint32_t *px=[rb contents];
  fprintf(stderr,"FA_BLIT status=%lu pixel=%08x seq_calls=%u sync_waits=%u queued=%u\n",(unsigned long)[c status],px[0],fa_seq_calls-seq0,f.waited-waits0,fa_count);
  assert([c status]==4&&px[0]&&fa_count==0&&f.waited==waits0&&fa_seq_calls>seq0);
  assert(!memcmp([rb contents],(unsigned char *)tegu_buffer_bytes(t->buffer)+t->mip[0].offset,256*64));fa_checks++;}
 /* many: >96 jobs in one command buffer (150 draws + the pass CLEAR job) */
 {unsigned busy0=fa_busy,sub0=fa_submits;TeguCommand *c=fa_draw_command(d,q,p,t,vb,src,samp,150,0);[c commit];[c waitUntilCompleted];
  fprintf(stderr,"FA_MANY status=%lu jobs=%u busy=%u max_inflight=%u queued=%u\n",(unsigned long)[c status],fa_submits-sub0,fa_busy-busy0,f.max_inflight,fa_count);
  assert([c status]==4&&fa_submits-sub0==151&&fa_busy>busy0&&f.max_inflight<=96&&fa_count==0);fa_checks++;}
 /* seed: an unchanged IOSurface shadow is not re-copied (IOSurfaceGetSeed);
  * a changed one is; a shadow render target is written back and then not
  * refreshed again while its surface keeps our seed. */
 {d->capabilities|=(1ULL<<15);
  test_surface.width=60;test_surface.height=60;test_surface.row=240;test_surface.alloc=16384;test_surface.planes=0;test_surface.fourcc=0x42475241u;
  for(unsigned i=0;i<240*60;i++)test_surface.data[i]=(unsigned char)(i*7);
  TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=60;sd->h=60;sd->uses=1;
  TeguTexture *sh=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];
  assert(sh&&sh->shadowSurface);
  unsigned locks[4];
  for(unsigned k=0;k<4;k++){if(k==2)fa_seed+=5; /* another writer */
   unsigned l0=test_surface_locks;TeguCommand *c=fa_draw_command(d,q,p,t,vb,sh,samp,1,0);[c commit];[c waitUntilCompleted];
   assert([c status]==4);locks[k]=test_surface_locks-l0;
   const unsigned char *b=(const unsigned char *)tegu_buffer_bytes(sh->buffer)+sh->mip[0].offset;
   for(unsigned y=0;y<60;y++)assert(!memcmp(b+y*sh->rowBytes,test_surface.data+y*240,240));}
  fprintf(stderr,"FA_SEED sampled locks per command=%u,%u,%u,%u\n",locks[0],locks[1],locks[2],locks[3]);
  assert(locks[0]>=1&&locks[1]==0&&locks[2]>=1&&locks[3]==0);
  sd->uses=5;TeguTexture *rt=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];[sd release];
  assert(rt&&rt->shadowSurface);
  for(unsigned k=0;k<3;k++){unsigned l0=test_surface_locks;
   RenderPass *pass=[[RenderPass alloc] init];pass->color=[[PassColor alloc] init];pass->color->target=[rt retain];pass->color->load=1;pass->color->store=1;
   pass->passWidth=60;pass->passHeight=60;pass->arrayLength=1;pass->sampleCount=1;
   TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e&&!e->inert);
   [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
   for(unsigned m=0;m<4;m++){[e setFragmentTexture:src atIndex:m];[e setFragmentSamplerState:samp atIndex:m];}
   [e drawPrimitives:3 vertexStart:0 vertexCount:3];[e endEncoding];[pass release];[c commit];[c waitUntilCompleted];
   assert([c status]==4);locks[k]=test_surface_locks-l0;
   const unsigned char *b=(const unsigned char *)tegu_buffer_bytes(rt->buffer)+rt->mip[0].offset;
   for(unsigned y=0;y<60;y++)assert(!memcmp(b+y*rt->rowBytes,test_surface.data+y*240,240));}
  fprintf(stderr,"FA_SEED target locks per command=%u,%u,%u\n",locks[0],locks[1],locks[2]);
  assert(locks[0]==2&&locks[1]==1&&locks[2]==1); /* refresh+write-back, then write-back only */
  [sh release];[rt release];fa_checks++;}
 /* pipe: two commands in flight; the first one's finish (its own WAIT_SEQ,
  * outside the bridge lock) runs on the finish FIFO after the second one
  * has already submitted; completions stay in commit order. */
 {TeguTexture *t2=c6f_texture(d,80,64,64,1,5,0x20);
  TeguQueue *pq=[d newCommandQueue];
  pq->finishes.ops=(struct tegu_submission_ops){fa_retain,fa_release,fa_schedule};
  assert(tegu_submission_set_target(&pq->finishes,&fa_target));pq->asyncPipeline=1;
  fa_set_gate(1);unsigned seqn0=fa_seq_n;
  TeguCommand *c1=[fa_draw_command(d,pq,p,t,vb,src,samp,3,0) retain];[c1 commit];
  TeguCommand *c2=[fa_draw_command(d,pq,p,t2,vb,src,samp,3,0) retain];[c2 commit];
  unsigned queued=fa_count;uint64_t newest=fa_q[fa_count-1].id;
  fprintf(stderr,"FA_PIPE committed c1=%lu c2=%lu queued_jobs=%u seq_calls=%u\n",(unsigned long)[c1 status],(unsigned long)[c2 status],queued,fa_seq_n-seqn0);
  /* P2: both are reported scheduled at submission, before any reap. */
  assert([c1 status]==3&&[c2 status]==3&&queued==8&&fa_seq_n==seqn0);
  [c1 waitUntilScheduled];[c2 waitUntilScheduled];
  /* P1: a third pipelined command finds two unfinished ones (their finishes
   * are gated) and waits up to 100 ms before it is scheduled anyway. */
  TeguTexture *t3=c6f_texture(d,80,64,64,1,5,0x20);
  tegu_host_clock=1;double w0=tegu_host_seconds();
  TeguCommand *c3=[fa_draw_command(d,pq,p,t3,vb,src,samp,1,0) retain];[c3 commit];
  double waited=tegu_host_seconds()-w0;
  fprintf(stderr,"FA_EARLY c1=%lu c2=%lu c3=%lu throttle_ms=%.1f early_wait=%llu early_forced=%llu inflight=%u\n",(unsigned long)[c1 status],
   (unsigned long)[c2 status],(unsigned long)[c3 status],waited*1e3,(unsigned long long)tegu_early_throttled,(unsigned long long)tegu_early_forced,pq->earlyInflight);
  assert([c3 status]==3&&waited>=0.095&&tegu_early_throttled==1&&tegu_early_forced==1&&pq->earlyInflight==3);
  fa_set_gate(0);
  /* Completions stay in commit order (scheduled is now reported at submission). */
  while([c2 status]<4){sched_yield();}
  assert([c1 status]>=4);
  [c1 waitUntilCompleted];[c2 waitUntilCompleted];
  fprintf(stderr,"FA_PIPE done c1=%lu c2=%lu first_seq=%llu newest=%llu queued=%u\n",(unsigned long)[c1 status],(unsigned long)[c2 status],
   (unsigned long long)fa_seq_args[seqn0],(unsigned long long)newest,fa_count);
  [c3 waitUntilCompleted];
  assert([c1 status]==4&&[c2 status]==4&&[c3 status]==4&&fa_count==0&&fa_seq_n>=seqn0+2&&fa_seq_args[seqn0]<newest);
  assert(pq->earlyInflight==0);
  /* P1 again, finishes open: a third command waits only for the oldest. */
  {TeguCommand *e1=[fa_draw_command(d,pq,p,t,vb,src,samp,2,0) retain],*e2=[fa_draw_command(d,pq,p,t2,vb,src,samp,2,0) retain],*e3=[fa_draw_command(d,pq,p,t3,vb,src,samp,2,0) retain];
   [e1 commit];[e2 commit];[e3 commit];[e3 waitUntilCompleted];[e1 waitUntilCompleted];[e2 waitUntilCompleted];
   assert([e1 status]==4&&[e2 status]==4&&[e3 status]==4&&pq->earlyInflight==0&&tegu_early_forced>=1);
   [e1 release];[e2 release];[e3 release];}
  fa_checks++;[c1 release];[c2 release];[c3 release];}
 /* B1 (lowering-opus): with kernel 0112 a pipelined command is reported
  * scheduled AT COMMIT, before its execution ran (the submission queue is
  * gated here): its jobs are submitted later on the executor thread. */
 {f.resv_model=1;n2mb_surface_reprobe(bridge);
  TeguTexture *tb=c6f_texture(d,80,64,64,1,5,0x20);
  TeguQueue *bq=[d newCommandQueue];
  bq->finishes.ops=(struct tegu_submission_ops){fa_retain,fa_release,fa_schedule};
  bq->submissions.ops=(struct tegu_submission_ops){fa_retain,fa_release,fa_schedule};
  assert(tegu_submission_set_target(&bq->finishes,&fa_target)&&tegu_submission_set_target(&bq->submissions,&fa_target));
  bq->asyncPipeline=1;fa_set_gate(1);unsigned subs0=fa_submits,resv0=f.resv_calls;
  TeguCommand *b1=[fa_draw_command(d,bq,p,tb,vb,src,samp,2,0) retain];[b1 commit];
  fprintf(stderr,"FA_B1 status=%lu submits=%u resv_calls=%u inflight=%u\n",(unsigned long)[b1 status],fa_submits-subs0,f.resv_calls-resv0,bq->earlyInflight);
  assert([b1 status]==3&&fa_submits==subs0&&bq->earlyInflight==1&&f.resv_calls==resv0+1); /* probe; no import to reserve */
  fa_set_gate(0);[b1 waitUntilCompleted];
  assert([b1 status]==4&&fa_submits>subs0&&bq->earlyInflight==0);
  f.resv_model=0;n2mb_surface_reprobe(bridge);
  [b1 release];[tb release];fa_checks++;}
 /* fail: the 3rd job of the next command fails on the GPU */
 {fa_submits=0;fa_fail_nth=3;TeguCommand *c=fa_draw_command(d,q,p,t,vb,src,samp,5,0);[c commit];[c waitUntilCompleted];
  fprintf(stderr,"FA_FAIL status=%lu error=%p queued=%u\n",(unsigned long)[c status],[c error],fa_count);
  assert([c status]==5&&[c error]&&fa_count==0);fa_checks++;}
 printf("PASS async frontend: %u checks (old-kernel sync fallback, reap before CPU blit, 96-job window, shadow refresh skip by IOSurface seed, cross-command pipelining in commit order, scheduled at submission + depth-2 throttle, B1 scheduled at commit under 0112, GPU failure -> command error); no GPU claim\n",fa_checks);
 return 0;
}
'''

# Reuse this harness for another frontend test (ab_schedule_test.py): its own
# main (helpers above stay available), output names and PASS marker.
NAME = os.environ.get('FA_NAME', 'async_frontend_test')
PASS_MARK = os.environ.get('FA_PASS', 'PASS async frontend')
if os.environ.get('FA_MAIN'):
    main = helpers + open(os.environ['FA_MAIN']).read()

prefix_marker = "static int call(void *p,unsigned s,"
gen = src[:start] + "main = r'''" + main + src[end:]
# Python-level edits on the generated prefix string happen inside the CA script;
# rename its fake `call` and add the wrapper right after the prefix is built.
hook = "fixed_ui = (TEgu / 'fixed_ui_frontend_host_test.inc')"
assert gen.count(hook) == 1
seed_model = [
 ("static int test_si_lock(void *p,uint32_t o,uint32_t *seed){(void)p;(void)o;(void)seed;test_surface_locks++;return 0;}",
  "static uint32_t fa_seed=1;static uint32_t fa_si_seed(void *p){(void)p;return fa_seed;}\n"
  "static int test_si_lock(void *p,uint32_t o,uint32_t *seed){(void)p;(void)o;if(seed)*seed=fa_seed;test_surface_locks++;return 0;}"),
 ("static int test_si_unlock(void *p,uint32_t o,uint32_t *seed){(void)p;(void)o;(void)seed;return 0;}",
  "static int test_si_unlock(void *p,uint32_t o,uint32_t *seed){(void)p;if(!(o&1))fa_seed++;if(seed)*seed=fa_seed;return 0;}"),
 ('if(!strcmp(n,"IOSurfaceLock"))return (void*)test_si_lock;',
  'if(!strcmp(n,"IOSurfaceGetSeed"))return (void*)fa_si_seed;\n if(!strcmp(n,"IOSurfaceLock"))return (void*)test_si_lock;'),
]
proto = 'static int call(void *p,unsigned s,const uint64_t *in,unsigned ni,const void *bytes,size_t size,uint64_t *out,unsigned *no,void *ob,size_t *os);\n'
seed_py = ''.join("assert prefix.count(%r) == 1\nprefix = prefix.replace(%r, %r, 1)\n" % (a, a, b) for a, b in seed_model)
gen = gen.replace(hook, seed_py + "prefix = prefix.replace(%r, %r, 1) + %r\n" % (prefix_marker, proto + 'static int call_base(void *p,unsigned s,', wrapper) + hook, 1)
assert "static int call(void *p,unsigned s,const uint64_t *in,unsigned ni" in gen
gen = gen.replace("out = HERE / 'ca_backboardd_host_test.m'", "out = HERE / '%s.m'" % NAME)
gen = gen.replace("for name, defines, asan in (('ca_backboardd_host_test_asan', ['TEGU_HOST_REAL_V2', 'TEGU_INERT_PIPELINES'], True),\n                            ('ca_backboardd_host_test_release', ['TEGU_HOST_REAL_V2', 'TEGU_INERT_PIPELINES'], False)):",
                  "for name, defines, asan in (('%s_asan', ['TEGU_HOST_REAL_V2', 'TEGU_INERT_PIPELINES'], True),\n                            ('%s_release', ['TEGU_HOST_REAL_V2', 'TEGU_INERT_PIPELINES'], False)):" % (NAME, NAME))
# result evaluation: only rc and PASS line matter here
tail_start = gen.index("    inert = [l for l in err.splitlines()")
tail_end = gen.index("    pending = err.count('C6F_PENDING')")
gen = gen[:tail_start] + "    good = r.returncode == 0 and %r in r.stdout.decode()\n" % PASS_MARK + gen[tail_end:]
gen = gen.replace("if line.startswith(('C6F_',", "if line.startswith(('FA_', 'AB_', 'TEGU_PERF ', 'TEGU_AB', 'C6F_',")
exec(compile(gen, str(HERE / (NAME + '_generated.py')), 'exec'), {'__name__': '__main__', '__file__': str(HERE / 'x.py')})
