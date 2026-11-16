/* ab_schedule_test.py main: in-boot A/B toggles (ab_schedule.inc) driven by a
 * test clock. The schedule file is $TEGU_AB_SCHEDULE (written by the .py). */
#include <pthread.h>
static unsigned ab_checks;
static pthread_mutex_t ab_mu=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t ab_cv=PTHREAD_COND_INITIALIZER;
static struct {void *ctx;void (*fn)(void *);} ab_work[64];static unsigned ab_wh,ab_wt,ab_started;static int ab_target;
static void *ab_worker(void *arg){(void)arg;
 for(;;){pthread_mutex_lock(&ab_mu);while(ab_wh==ab_wt)pthread_cond_wait(&ab_cv,&ab_mu);
  void *ctx=ab_work[ab_wh%64].ctx;void (*fn)(void *)=ab_work[ab_wh%64].fn;ab_wh++;pthread_mutex_unlock(&ab_mu);fn(ctx);}
 return 0;}
static void ab_retain(void *p){(void)p;}
static void ab_release(void *p){(void)p;}
static void ab_schedule(void *target,void *ctx,void (*fn)(void *)){assert(target==&ab_target);
 pthread_mutex_lock(&ab_mu);
 if(!ab_started){pthread_t t;assert(!pthread_create(&t,0,ab_worker,0));pthread_detach(t);ab_started=1;}
 ab_work[ab_wt%64].ctx=ctx;ab_work[ab_wt%64].fn=fn;ab_wt++;pthread_cond_signal(&ab_cv);pthread_mutex_unlock(&ab_mu);}
/* One pass of `draws` indexed quads (index buffer, attribute-fetch shader). */
static TeguCommand *ab_command(TeguQueue *q,TeguRenderPipeline *p,TeguTexture *t,TeguBuffer *vb,TeguBuffer *ib,
 TeguTexture *src,TeguSampler *samp,unsigned draws){
 RenderPass *pass=[[RenderPass alloc] init];
 pass->color=[[PassColor alloc] init];pass->color->target=[t retain];pass->color->load=2;pass->color->store=1;
 pass->passWidth=t->textureWidth;pass->passHeight=t->textureHeight;pass->arrayLength=1;pass->sampleCount=1;
 TeguCommand *c=[q commandBuffer];TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)pass];assert(e&&!e->inert);
 [e setRenderPipelineState:p];[e setVertexBuffer:vb offset:0 atIndex:1];
 for(unsigned k=0;k<4;k++){[e setFragmentTexture:src atIndex:k];[e setFragmentSamplerState:samp atIndex:k];}
 for(unsigned k=0;k<draws;k++)[e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:ib indexBufferOffset:0];
 [e endEncoding];[pass release];
 return c;
}
static void ab_jobs(TeguCommand *c,unsigned *native,unsigned *expanded){
 *native=*expanded=0;
 for(unsigned i=0;i<c->count;i++){struct Dispatch *j=tegu_command_job(c,i);if(!j->genericDrawSnapshot)continue;
  if(j->genericDrawRequest.index_size)(*native)++;if(j->expandOut)(*expanded)++;}
}
int main(int argc,char **argv){
 assert(argc>=3);
 tegu_host_process_backboardd=1;tegu_host_clock=1;
 struct fake f={0};f.render_model=1;uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 TeguFunction *dsv=c6f_function(d,argv[1],"downsample_blur_vert_lph"),*ds4=c6f_function(d,argv[2],"downsample_4_frag_lph");
 id error=0;RenderDescriptor *pd=c6f_ca_descriptor(dsv,ds4,80,15,0,0);
 TeguRenderPipeline *p=[d newRenderPipelineStateWithDescriptor:(id)pd error:&error];assert(p&&p->genericDraw&&p->genericShader.attribute_count);
 TeguQueue *q=[d newCommandQueue];TeguBuffer *vb=[d newBufferWithLength:4096 options:0];
 TeguBuffer *ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([ib contents],idx,sizeof(idx));
 TeguTexture *src=new_sample(d,80,64,64,1,2,1);fill_texture(src,0x80402010u);TeguSampler *samp=new_sampler(d,3);
 TeguTexture *t=c6f_texture(d,80,64,64,1,5,0x20);assert(t);
 tegu_jm_batch_force=-1; /* the toggles decide */
 unsigned nat=0,exp=0;
 /* phase 0 (baseline, t=5): defaults - native indexed, no batching */
 tegu_ab_test_now=5;
 {TeguCommand *c=ab_command(q,p,t,vb,ib,src,samp,4);unsigned s0=f.submitted;[c commit];[c waitUntilCompleted];ab_jobs(c,&nat,&exp);
  char tag[64];tegu_ab_tag(c->abWord,tag,sizeof(tag));
  fprintf(stderr,"AB_PHASE0 %s native=%u expanded=%u submits=%u status=%lu\n",tag,nat,exp,f.submitted-s0,(unsigned long)[c status]);
  assert([c status]==4&&nat==4&&!exp&&f.submitted-s0==5);ab_checks++;}
 /* phase 1 (t=15): TEGU_NATIVE_INDEXED=0 -> CPU index expansion */
 tegu_ab_test_now=15;
 {TeguCommand *c=ab_command(q,p,t,vb,ib,src,samp,4);[c commit];[c waitUntilCompleted];ab_jobs(c,&nat,&exp);
  fprintf(stderr,"AB_PHASE1 native=%u expanded=%u\n",nat,exp);assert([c status]==4&&!nat&&exp==4);ab_checks++;}
 /* A command created in phase 1 but executed in phase 2 keeps phase 1. */
 {TeguCommand *c=ab_command(q,p,t,vb,ib,src,samp,2);tegu_ab_test_now=25;[c commit];[c waitUntilCompleted];ab_jobs(c,&nat,&exp);
  fprintf(stderr,"AB_BOUNDARY native=%u expanded=%u\n",nat,exp);assert([c status]==4&&!nat&&exp==2);ab_checks++;}
 /* phase 2 (t=25): TEGU_JM_BATCH=1 -> one kernel job for the pass */
 {TeguCommand *c=ab_command(q,p,t,vb,ib,src,samp,4);unsigned s0=f.submitted;[c commit];[c waitUntilCompleted];
  fprintf(stderr,"AB_PHASE2 submits=%u status=%lu\n",f.submitted-s0,(unsigned long)[c status]);
  assert([c status]==4&&f.submitted-s0==1);ab_checks++;}
 /* phase 3 (t=35): NORD2_METAL_PREFRAME=1 applied to the bridge per command */
 tegu_ab_test_now=35;
 {TeguCommand *c=ab_command(q,p,t,vb,ib,src,samp,1);[c commit];[c waitUntilCompleted];
  fprintf(stderr,"AB_PHASE3 preframe=%u\n",n2mb_preframe(bridge,2));assert([c status]==4&&n2mb_preframe(bridge,2)==1);ab_checks++;}
 /* phase 4 (t=45): TEGU_ASYNC_PIPELINE=1 -> completion through the finish FIFO
  * (async WAIT_SEQ fake); the pre-frame mode is back to its default. */
 tegu_ab_test_now=45;f.seq_model=1;f.window=96;fa_mode=1;assert(n2mb_async(bridge,1)==0&&n2mb_async(bridge,2)==1);
 {TeguQueue *pq=[d newCommandQueue];
  pq->finishes.ops=(struct tegu_submission_ops){ab_retain,ab_release,ab_schedule};
  assert(tegu_submission_set_target(&pq->finishes,&ab_target));pq->asyncPipeline=1;
  unsigned w0=ab_wt;TeguCommand *c=[ab_command(pq,p,t,vb,ib,src,samp,2) retain];[c commit];[c waitUntilCompleted];
  fprintf(stderr,"AB_PHASE4 status=%lu fifo=%u preframe=%u\n",(unsigned long)[c status],ab_wt-w0,n2mb_preframe(bridge,2));
  assert([c status]==4&&ab_wt-w0==1&&n2mb_preframe(bridge,2)==0&&!fa_count);[c release];
  /* phase 5 (t=55): NORD2_METAL_BATCH_MAX=128 -> the bridge cap per command */
  tegu_ab_test_now=55;
  {TeguCommand *b5=[ab_command(q,p,t,vb,ib,src,samp,1) retain];[b5 commit];[b5 waitUntilCompleted];
   fprintf(stderr,"AB_PHASE5 cap=%u\n",n2mb_batch_capacity(bridge,0));assert([b5 status]==4&&n2mb_batch_capacity(bridge,0)==128);[b5 release];}
  /* phase 1 of the next cycle (not pipelined): the same queue completes inline again */
  tegu_ab_test_now=15+60;w0=ab_wt;c=[ab_command(pq,p,t,vb,ib,src,samp,1) retain];[c commit];[c waitUntilCompleted];
  fprintf(stderr,"AB_CYCLE1 status=%lu fifo=%u cap=%u\n",(unsigned long)[c status],ab_wt-w0,n2mb_batch_capacity(bridge,0));
  assert([c status]==4&&ab_wt-w0==0&&((c->abWord>>16)&0xffffu)==1&&n2mb_batch_capacity(bridge,0)==N2MB_BATCH_CAPACITY) /* default cap 128 */;[c release];ab_checks++;}
 /* phase 6 (t=65): NI=0 + JM_BATCH=1; a GPU failure disables both for good */
 tegu_ab_test_now=65;
 {TeguCommand *c=ab_command(q,p,t,vb,ib,src,samp,2);f.fail_seq=1;[c commit];[c waitUntilCompleted];
  fprintf(stderr,"AB_PHASE6 status=%lu\n",(unsigned long)[c status]);assert([c status]==5);
  /* The bridge is broken after a GPU failure: a new bridge for the rest. */
  TeguCommand *n=[q commandBuffer];
  fprintf(stderr,"AB_DISABLED word=0x%x ni=%u jb=%u\n",n->abWord,tegu_ab_value(n->abWord,TEGU_AB_NATIVE_INDEXED),tegu_ab_value(n->abWord,TEGU_AB_JM_BATCH));
  assert(tegu_ab_value(n->abWord,TEGU_AB_NATIVE_INDEXED)==1&&tegu_ab_value(n->abWord,TEGU_AB_JM_BATCH)==0);
  tegu_ab_test_now=15+60; /* phase 1 of the next cycle: NI stays at its default */
  TeguCommand *m=[q commandBuffer];assert(tegu_ab_value(m->abWord,TEGU_AB_NATIVE_INDEXED)==1);
  /* Commands are dropped without commit: valid unencoded command buffers. */
  ab_checks++;}
 printf("PASS ab schedule: %u checks (baseline, native-indexed off, command-boundary snapshot, JM batching on, pre-frame on/off, async pipelining via FIFO and back, batch cap 128 and back, failure disables toggles); no GPU claim\n",ab_checks);
 return 0;
}
