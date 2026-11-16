/* Frame-cost benchmark main (host only, no GPU, no phone).
 *
 * Spliced by gpu/tools/frame_bench.py after the same real-frontend prelude the
 * C6f CoreAnimation host test uses (render worker harness + shared fake JM
 * kernel + the production Objective-C frontend and n2mb bridge).  Replays
 * CA-shaped command buffers on the imported 1125x2436 display and prints one
 *   FB_FRAME ... key=value ...
 * line per frame; frame_bench.py parses them.  Nothing here asserts product
 * behaviour: a failed encode is reported (invalid=1), never hidden.
 *
 * argv: 1..10 AIR/metallib paths exactly as ca_backboardd_host_test.py,
 *       11 steady frame count, 12 optional scenario-name filter.
 */
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>

static void *fb_read(const char *p,long *n){FILE *f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);*n=ftell(f);rewind(f);
 void *b=malloc((size_t)*n);assert(b&&fread(b,1,(size_t)*n,f)==(size_t)*n);fclose(f);return b;}
static TeguFunction *fb_function(TeguComputeDevice *d,const char *path,const char *name){
 long n=0;void *b=fb_read(path,&n);id error=0;
 TeguLibrary *l=[d newTeguLibraryWithBytes:b length:(NSUInteger)n error:&error];free(b);assert(l&&!error);
 TeguFunction *f=[l newFunctionWithName:[NSString stringWithUTF8String:name]];assert(f);return f;
}
/* Exact backboardd descriptor (c6ec TEGU_RENDER_* captures), as in the C6f test. */
static RenderDescriptor *fb_ca_descriptor(TeguFunction *v,TeguFunction *f,NSUInteger fmt,NSUInteger mask0,NSUInteger mask1,int blend){
 RenderDescriptor *pd=[[RenderDescriptor alloc] init];
 pd->vertex=[v retain];pd->fragment=[f retain];pd->samples=1;pd->raster=1;pd->amplification=1;pd->topology=0;
 pd->colors=[[RenderColors alloc] init];
 for(unsigned i=0;i<8;i++){pd->colors->colors[i]=[[RenderColor alloc] init];pd->colors->colors[i]->mask=15;}
 pd->colors->colors[0]->format=fmt;pd->colors->colors[0]->mask=mask0;
 pd->colors->colors[1]->format=fmt;pd->colors->colors[1]->mask=mask1;
 if(blend){RenderColor *c=pd->colors->colors[0];c->blend=1;c->rgbSrc=1;c->rgbDst=5;c->alphaSrc=1;c->alphaDst=5;}
 RenderLayout *l=[[RenderLayout alloc] init];l->buffers=[[RenderLayout alloc] init];
 const NSUInteger fmts[4]={31,29,29,27},offs[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){RenderAttribute *a=[[RenderAttribute alloc] init];a->value=fmts[i];a->byteOffset=offs[i];a->bufferSlot=1;l->entries[i]=a;}
 RenderAttribute *b=[[RenderAttribute alloc] init];b->layoutStride=48;b->step=1;b->rate=1;l->buffers->entries[1]=b;
 pd->layout=l;return pd;
}
static TeguTexture *fb_texture(TeguComputeDevice *d,NSUInteger fmt,NSUInteger w,NSUInteger h,NSUInteger levels,NSUInteger usage,NSUInteger options){
 TextureDesc *desc=[[TextureDesc alloc] init];desc->format=fmt;desc->w=w;desc->h=h;desc->levels=levels;desc->uses=usage;desc->options=options;
 TeguTexture *t=[d newTextureWithDescriptor:(id)desc];[desc release];return t;
}

/* ---- host measurement helpers ------------------------------------------ */
static double fb_thread_cpu(void){struct timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return (double)t.tv_sec+t.tv_nsec*1e-9;}
static double fb_proc_cpu(void){struct timespec t;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return (double)t.tv_sec+t.tv_nsec*1e-9;}
/* User-space retired instructions of the calling thread (perf_event_open with
 * exclude_kernel is allowed at perf_event_paranoid=2).  Independent of the
 * thermal/quota throttling of the host; 0 when the counter is unavailable. */
static int fb_ins_fd=-2;
static uint64_t fb_ins(void){
 if(fb_ins_fd==-2){
  struct perf_event_attr a;memset(&a,0,sizeof(a));a.type=PERF_TYPE_HARDWARE;a.size=sizeof(a);a.config=PERF_COUNT_HW_INSTRUCTIONS;
  a.exclude_kernel=1;a.exclude_hv=1;
  fb_ins_fd=(int)syscall(SYS_perf_event_open,&a,0,-1,-1,0);
 }
 if(fb_ins_fd<0)return 0;
 uint64_t v=0;if(read(fb_ins_fd,&v,sizeof(v))!=sizeof(v))return 0;return v;
}
/* n2mb_copy: shared-storage blits executed by the bridge (counted, not changed). */
extern int __real_n2mb_copy(uintptr_t,uint64_t,uint64_t,uint64_t,uint64_t);
unsigned long long fb_copy_calls,fb_copy_bytes;
int __wrap_n2mb_copy(uintptr_t b,uint64_t s,uint64_t o,uint64_t d,uint64_t n){fb_copy_calls++;fb_copy_bytes+=n;return __real_n2mb_copy(b,s,o,d,n);}

struct fb_scn {
 const char *name;
 unsigned disp_passes;      /* full-screen imported-display passes, each with disp_draws */
 unsigned disp_draws;
 const char *motif;         /* F fixed_frag (batchable), L luma_boost (framebuffer fetch), repeated to disp_draws */
 unsigned blur_passes;      /* offscreen blur passes (downsample then BGR5A1 narrow blur ping-pong) */
 unsigned blur_draws;
 unsigned small_passes;     /* small offscreen BGRA8 passes, fixed_frag draws */
 unsigned small_draws;
};
/* Shapes follow the c6jb device windows (per command): see frame_bench.py DEVICE_SHAPES. */
static const struct fb_scn fb_scns[]={
 {"tick",      1,  5,"FFLFL",           0,0, 0,0},
 {"small",     1, 18,"FFL",             0,0, 0,0},
 {"medium",    2, 18,"FFL",            1,3, 2,3},
 {"large",     2, 40,"FFFL",           3,4, 3,4},
 {"ca200",     2, 0, "",               10,4, 0,0},   /* disp_draws 0: the C6f 120+40 display draws (see below) */
 {"blur_heavy",1, 10,"FFFFFFFFFL",     12,4, 0,0},
};

static unsigned fb_pool_base;
static TeguComputeDevice *fb_d;static TeguQueue *fb_q;
static TeguRenderPipeline *fb_pf,*fb_pl,*fb_pd,*fb_pn;
static TeguTexture *fb_disp,*fb_t1,*fb_b43,*fb_c43,*fb_s0,*fb_img,*fb_lut,*fb_small[4],*fb_small2[4];
static TeguBuffer *fb_ib,*fb_fbuf,*fb_vb;static TeguSampler *fb_samp;
/* FB_OFFSET_RING=1: bind the per-draw buffers at advancing 256-byte offsets
 * of one 64 KiB buffer, as QuartzCore binds its per-draw data (c6m device:
 * 93% of plan-memo misses differ from an entry only in buffer offsets);
 * FB_OFFSET_RING=2 also varies the offset's low bits (4-byte steps). */
static unsigned fb_ring,fb_ring_next;
static NSUInteger fb_ring_offset(void){
 if(!fb_ring)return 0;
 unsigned k=fb_ring_next++;
 return (NSUInteger)(k%224u)*256u+(fb_ring>1u?(NSUInteger)(k%4u)*4u:0u);
}
/* The CA loop's finish FIFO: one serial worker thread standing in for the
 * device's dispatch queue com.tegu.metal.finish (the host has no libdispatch,
 * so a host queue has no asyncPipeline). */
#include <pthread.h>
static pthread_mutex_t fb_fin_mu=PTHREAD_MUTEX_INITIALIZER;static pthread_cond_t fb_fin_cv=PTHREAD_COND_INITIALIZER;
static struct {void *ctx;void (*fn)(void *);} fb_fin_work[256];static unsigned fb_fin_h,fb_fin_t,fb_fin_started;static int fb_fin_target;
static void *fb_fin_worker(void *arg){(void)arg;
 for(;;){pthread_mutex_lock(&fb_fin_mu);while(fb_fin_h==fb_fin_t)pthread_cond_wait(&fb_fin_cv,&fb_fin_mu);
  void *ctx=fb_fin_work[fb_fin_h%256].ctx;void (*fn)(void *)=fb_fin_work[fb_fin_h%256].fn;fb_fin_h++;pthread_mutex_unlock(&fb_fin_mu);fn(ctx);}
 return 0;}
static void fb_fin_retain(void *p){(void)p;}
static void fb_fin_release(void *p){(void)p;}
static void fb_fin_schedule(void *target,void *ctx,void (*fn)(void *)){(void)target;
 pthread_mutex_lock(&fb_fin_mu);
 if(!fb_fin_started){pthread_t t;if(!pthread_create(&t,0,fb_fin_worker,0)){pthread_detach(t);fb_fin_started=1;}}
 fb_fin_work[fb_fin_t%256].ctx=ctx;fb_fin_work[fb_fin_t%256].fn=fn;fb_fin_t++;pthread_cond_signal(&fb_fin_cv);pthread_mutex_unlock(&fb_fin_mu);}

/* FB_APSO: the host descriptor is immutable here, so its "copy" is itself. */
@interface RenderDescriptor (FBAsyncCopy)
- (id)copyWithZone:(void *)zone;
- (id)copy;
@end
@implementation RenderDescriptor (FBAsyncCopy)
- (id)copyWithZone:(void *)zone{(void)zone;return [self retain];}
- (id)copy{return [self retain];}
@end
static void fb_draw(TeguRenderEncoder *e,TeguRenderPipeline *p,unsigned k,int fetch,unsigned tw,unsigned th){
 [e setRenderPipelineState:p];[e setVertexBuffer:fb_vb offset:0 atIndex:1];
 {NSUInteger o=fb_ring_offset();
  for(unsigned bi=0;bi<4;bi++){[e setFragmentBuffer:fb_fbuf offset:o atIndex:bi];if(bi!=1)[e setVertexBuffer:fb_fbuf offset:o atIndex:bi];}}
 [e setFragmentTexture:(fetch?fb_lut:fb_img) atIndex:(fetch?0:3)];[e setFragmentSamplerState:fb_samp atIndex:0];
 {unsigned w=tw/2<150?tw/2:150,h=th/2<150?th/2:150; /* inside the target: out-of-target scissors invalidate the command */
  [e setScissorRect:(TeguScissorRect){(k*37)%(tw-w+1),(k*53)%(th-h+1),w,h}];}
 [e drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:fb_ib indexBufferOffset:0];
}

/* FB_PROF=<file>: SIGPROF sampling (process CPU time, every 100 us) of the
 * execution phase only (FB_PROF_ENCODE=1: the encoding phase instead) ([commit] .. [waitUntilCompleted]), each sample a
 * backtrace(); written at exit as hex return addresses plus the image base
 * (gpu/tools/fb_prof_report.py resolves them). Host CPU shape, not device time. */
#include <execinfo.h>
#include <signal.h>
#include <sys/time.h>
#define FB_PROF_MAX 200000u
#define FB_PROF_DEPTH 24
static volatile int fb_prof_on;static void *(*fb_prof_buf)[FB_PROF_DEPTH];static unsigned char *fb_prof_n;static volatile unsigned fb_prof_count;
static void fb_prof_handler(int sig){(void)sig;if(!fb_prof_on||!fb_prof_buf)return;unsigned i=__atomic_fetch_add(&fb_prof_count,1,__ATOMIC_RELAXED);
 if(i>=FB_PROF_MAX)return;fb_prof_n[i]=(unsigned char)backtrace(fb_prof_buf[i],FB_PROF_DEPTH);}
static void fb_prof_start(void){
 if(!getenv("FB_PROF")||fb_prof_buf)return;
 fb_prof_buf=calloc(FB_PROF_MAX,sizeof(*fb_prof_buf));fb_prof_n=calloc(FB_PROF_MAX,1);
 {void *warm[4];(void)backtrace(warm,4);} /* load the unwinder outside the handler */
 struct sigaction sa;memset(&sa,0,sizeof(sa));sa.sa_handler=fb_prof_handler;sa.sa_flags=SA_RESTART;sigaction(SIGPROF,&sa,0);
 struct itimerval it={{0,100},{0,100}};setitimer(ITIMER_PROF,&it,0);
}
static void fb_prof_dump(void){
 const char *path=getenv("FB_PROF");if(!path||!fb_prof_buf)return;
 struct itimerval off={{0,0},{0,0}};setitimer(ITIMER_PROF,&off,0);
 FILE *o=fopen(path,"w");if(!o)return;
 {FILE *m=fopen("/proc/self/maps","r");char line[512];while(m&&fgets(line,sizeof(line),m))fprintf(o,"map %s",line);if(m)fclose(m);}
 unsigned n=fb_prof_count<FB_PROF_MAX?fb_prof_count:FB_PROF_MAX;
 for(unsigned i=0;i<n;i++){for(unsigned k=0;k<fb_prof_n[i];k++)fprintf(o,"%s%p",k?" ":"",fb_prof_buf[i][k]);fputc('\n',o);}
 fclose(o);fprintf(stderr,"FB_PROF samples=%u file=%s\n",n,path);
}
struct fb_frame {unsigned draws,passes;};
static struct fb_frame fb_build(TeguCommand *c,const struct fb_scn *s){
 struct fb_frame fr={0,0};
 unsigned mlen=s->motif?(unsigned)strlen(s->motif):0;
 for(unsigned pass=0;pass<s->disp_passes;pass++){
  RenderPass *dp=[[RenderPass alloc] init];
  dp->color=[[PassColor alloc] init];dp->color->target=[fb_disp retain];dp->color->load=1;dp->color->store=1;
  dp->second=[[PassColor alloc] init];dp->second->target=[fb_t1 retain];dp->second->load=0;dp->second->store=0;
  dp->passWidth=1125;dp->passHeight=2436;dp->arrayLength=1;dp->sampleCount=1;
  TeguRenderEncoder *e=[c renderCommandEncoderWithDescriptor:(id)dp];
  unsigned n=s->disp_draws;
  if(!n&&!strcmp(s->name,"ca200"))n=pass?40u:120u;
  for(unsigned k=0;k<n;k++){
   int fetch=mlen?(s->motif[k%mlen]=='L'):(k%30==29);
   fb_draw(e,fetch?fb_pl:fb_pf,k,fetch,1125,2436);fr.draws++;
  }
  [e endEncoding];[dp release];fr.passes++;
 }
 for(unsigned bp=0;bp<s->blur_passes;bp++){
  TeguTexture *dst=bp==0?fb_s0:(bp&1)?fb_b43:fb_c43,*src=bp==0?fb_img:bp==1?fb_s0:(bp&1)?fb_c43:fb_b43;
  RenderPass *bpz=[[RenderPass alloc] init];
  bpz->color=[[PassColor alloc] init];bpz->color->target=[dst retain];bpz->color->load=2;bpz->color->store=1;
  bpz->passWidth=dst->textureWidth;bpz->passHeight=dst->textureHeight;bpz->arrayLength=1;bpz->sampleCount=1;
  TeguRenderEncoder *be=[c renderCommandEncoderWithDescriptor:(id)bpz];
  TeguRenderPipeline *p=bp==0?fb_pd:fb_pn;
  [be setRenderPipelineState:p];[be setVertexBuffer:fb_vb offset:0 atIndex:1];
  for(unsigned t=0;t<4;t++){[be setFragmentTexture:src atIndex:t];[be setFragmentSamplerState:fb_samp atIndex:t];}
  for(unsigned k=0;k<s->blur_draws;k++){[be drawIndexedPrimitives:3 indexCount:6 indexType:0 indexBuffer:fb_ib indexBufferOffset:0];fr.draws++;}
  [be endEncoding];[bpz release];fr.passes++;
 }
 for(unsigned sp=0;sp<s->small_passes;sp++){
  TeguTexture *dst=fb_small[sp&3];
  RenderPass *spz=[[RenderPass alloc] init];
  spz->color=[[PassColor alloc] init];spz->color->target=[dst retain];spz->color->load=2;spz->color->store=1;
  spz->passWidth=dst->textureWidth;spz->passHeight=dst->textureHeight;spz->arrayLength=1;spz->sampleCount=1;
  TeguRenderEncoder *se=[c renderCommandEncoderWithDescriptor:(id)spz];
  for(unsigned k=0;k<s->small_draws;k++){fb_draw(se,fb_pf,k,0,(unsigned)dst->textureWidth,(unsigned)dst->textureHeight);fr.draws++;}
  [se endEncoding];[spz release];fr.passes++;
 }
 return fr;
}

struct fb_snap {unsigned long long calls[16],alloc_bytes,map_calls,map_bytes,copy_calls,copy_bytes,submits,waits,live_peak;
 unsigned long long enc[TEGU_ENC_SLOTS];unsigned surface_locks;};
static void fb_snap_take(struct fb_snap *s,struct fake *f){
 for(unsigned i=0;i<16;i++)s->calls[i]=fb_calls[i];
 s->alloc_bytes=fb_alloc_bytes;s->map_calls=fb_map_calls;s->map_bytes=fb_map_bytes;s->copy_calls=fb_copy_calls;s->copy_bytes=fb_copy_bytes;
 s->submits=f->submitted;s->waits=f->waited;s->live_peak=fb_live_peak;
 for(unsigned i=0;i<TEGU_ENC_SLOTS;i++)s->enc[i]=tegu_enc_ns[i];
 s->surface_locks=test_surface_locks;
}

static unsigned fb_variants(void){return fb_pf->genericVariantCount+fb_pl->genericVariantCount+fb_pd->genericVariantCount+fb_pn->genericVariantCount;}

static void fb_run_frame(struct fake *f,const struct fb_scn *s,const char *mode,const char *phase,unsigned idx){
 struct fb_snap a,b;
 unsigned variants0=fb_variants();
 fb_live_peak=fb_live_bytes;
 fb_snap_take(&a,f);
 double wall0=tegu_enc_clock(),th0=fb_thread_cpu(),pr0=fb_proc_cpu();uint64_t in0=fb_ins();
 TeguCommand *c=[fb_q commandBuffer];
 static int profEncode=-1;if(profEncode<0){const char *pe=getenv("FB_PROF_ENCODE");profEncode=pe&&pe[0]=='1';}
 fb_prof_on=profEncode;struct fb_frame fr=fb_build(c,s);fb_prof_on=0;
 double wall1=tegu_enc_clock(),th1=fb_thread_cpu();uint64_t in1=fb_ins();
 unsigned invalid=c->invalid,jobs_enc=c->count;
 const char *at=c->invalidFunction?c->invalidFunction:"-";unsigned line=c->invalidLine;
 fb_prof_on=!profEncode;[c commit];[c waitUntilCompleted];fb_prof_on=0;
 double wall2=tegu_enc_clock();
 unsigned long fb_status=(unsigned long)[c status];struct tegu_perf_batch pbv=c->perfBatch;
 while(poolCount>fb_pool_base)[pool[--poolCount] release]; /* no run loop: drain only this frame's autoreleased objects */
 double pr2=fb_proc_cpu();
 fb_snap_take(&b,f);
 struct tegu_perf_batch *pb=&pbv;
 unsigned long long calls[16];for(unsigned i=0;i<16;i++)calls[i]=b.calls[i]-a.calls[i];
 printf("FB_FRAME scn=%s mode=%s phase=%s idx=%u draws=%u passes=%u jobs_enc=%u invalid=%u at=%s:%u status=%lu "
  "enc_us=%.1f enc_cpu_us=%.1f enc_ins=%llu exec_us=%.1f proc_cpu_us=%.1f "
  "draw_us=%.1f plan_us=%.1f variant_us=%.1f expand_us=%.1f pass_us=%.1f "
  "kjobs=%llu generic=%u other=%u fs_load=%u fs_clear=%u off_load=%u off_clear=%u enc=%u enc_fs=%u enc_off=%u skipped=%u "
  "k_alloc=%llu k_release=%llu k_sync=%llu k_submit=%llu k_wait=%llu k_import=%llu k_log=%llu k_status=%llu k_other=%llu "
  "alloc_kib=%llu map_calls=%llu map_kib=%llu copy_calls=%llu copy_kib=%llu surface_locks=%u peak_kib=%llu live_kib=%llu variants_new=%u\n",
  s->name,mode,phase,idx,fr.draws,fr.passes,jobs_enc,invalid,at,line,fb_status,
  (wall1-wall0)*1e6,(th1-th0)*1e6,(unsigned long long)(in1-in0),(wall2-wall1)*1e6,(pr2-pr0)*1e6,
  (b.enc[TEGU_ENC_DRAW]-a.enc[TEGU_ENC_DRAW])/1e3,(b.enc[TEGU_ENC_PLAN]-a.enc[TEGU_ENC_PLAN])/1e3,
  (b.enc[TEGU_ENC_VARIANT]-a.enc[TEGU_ENC_VARIANT])/1e3,(b.enc[TEGU_ENC_EXPAND]-a.enc[TEGU_ENC_EXPAND])/1e3,
  (b.enc[TEGU_ENC_PASS]-a.enc[TEGU_ENC_PASS])/1e3,
  (unsigned long long)(b.submits-a.submits),pb->genericJobs,pb->otherJobs,pb->fullLoad,pb->fullClear,pb->offLoad,pb->offClear,
  pb->encoders,pb->encFull,pb->encOff,pb->skipped,
  calls[NORD2_GPU_SEL_ALLOC],calls[NORD2_GPU_SEL_RELEASE],calls[NORD2_GPU_SEL_SYNC],calls[NORD2_GPU_SEL_SUBMIT_JM],calls[NORD2_GPU_SEL_WAIT],
  calls[NORD2_GPU_SEL_IMPORT_SURFACE],calls[NORD2_GPU_SEL_LOG],calls[NORD2_GPU_SEL_STATUS],
  (calls[3]+calls[5]+calls[6]+calls[7]+calls[12]+calls[13]+calls[14]+calls[15]),
  (b.alloc_bytes-a.alloc_bytes)/1024,b.map_calls-a.map_calls,(b.map_bytes-a.map_bytes)/1024,b.copy_calls-a.copy_calls,(b.copy_bytes-a.copy_bytes)/1024,
  b.surface_locks-a.surface_locks,fb_live_peak/1024,fb_live_bytes/1024,fb_variants()-variants0);
#ifdef FB_HAVE_ASYNC
 {static uint64_t h0,m0;uint64_t h=0,m=0;n2mb_cmdpool_stats(fb_d->connection,&h,&m);
  printf("FB_POOL scn=%s mode=%s phase=%s idx=%u pool_hits=%llu pool_misses=%llu pool_entries=%u\n",s->name,mode,phase,idx,
   (unsigned long long)(h-h0),(unsigned long long)(m-m0),n2mb_cmdpool_count(fb_d->connection));h0=h;m0=m;}
#endif
 fflush(stdout);
}

int main(int argc,char **argv){
 fb_prof_start();
 assert(argc>=11);
 unsigned steady=argc>11?(unsigned)atoi(argv[11]):4;if(!steady)steady=1;
 const char *only=argc>12&&argv[12][0]?argv[12]:0;
 tegu_host_process_backboardd=1;
 static struct fake f;uintptr_t bridge=open_bridge(&f);
 TeguComputeDevice *d=[TeguComputeDevice alloc];fb_d=d;
 d->connection=bridge;d->scalar=scalar_adapter;d->method=method_adapter;d->map=map_adapter;d->unmap=unmap_adapter;
 d->capabilities=(1ULL<<14)|(1ULL<<18)|256ULL|64ULL|32ULL|16ULL;
 /* FB_ASYNC=1: kernel 0090 model (WAIT_SEQ, 96-job window) and async bridge
  * submission, as on a P3+ kernel. The fake runs jobs at submit, so the host
  * shows only the CPU side (no per-job WAIT); GPU overlap is the model's. */
#ifdef FB_HAVE_ASYNC
 {const char *a=getenv("FB_ASYNC");if(a&&a[0]=='1'){f.seq_model=1;f.window=96;}
  unsigned mode=a&&a[0]=='1'?1u:0u;(void)n2mb_async(bridge,mode);
  printf("FB_ASYNC requested=%u active=%u\n",mode,n2mb_async(bridge,2)==1);}
#endif
#ifdef FB_HAVE_TIME
 /* Device time model (fake kernel): CPU preparation spun per SUBMIT_JM and
  * simulated GPU time per job, waits sleep until simulated completion. */
 {const char *su=getenv("FB_SUBMIT_US"),*gu=getenv("FB_GPU_US");
  f.submit_us=su?(unsigned)atoi(su):0u;f.gpu_us=gu?(unsigned)atoi(gu):0u;
  printf("FB_TIME submit_us=%u gpu_us=%u\n",f.submit_us,f.gpu_us);}
#endif
#ifndef FB_HAVE_ASYNC
 printf("FB_ASYNC requested=0 active=0\n");
#endif
 TeguFunction *dsv=fb_function(d,argv[1],"downsample_blur_vert_lph");
 TeguFunction *ds4=fb_function(d,argv[2],"downsample_4_frag_lph");
 TeguFunction *nb11=fb_function(d,argv[3],"narrow_blur_11_frag_lph");
 TeguFunction *hav=fb_function(d,argv[4],"std_vert1_lph");
 TeguFunction *luf=fb_function(d,argv[10],"luma_boost_lph");
 long n=0;void *qb=fb_read(argv[8],&n);id err=0;TeguLibrary *ql=[d newTeguLibraryWithBytes:qb length:(NSUInteger)n error:&err];free(qb);assert(ql);
 struct TeguFixedUIConstantRow fs[sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0])];
 struct TeguFixedUIConstantRow vs[sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0])];
 memcpy(fs,tegu_fixed_ui_fs_constants,sizeof(fs));memcpy(vs,tegu_fixed_ui_vs_constants,sizeof(vs));
 const char *enabled[]={"fc_has_image_0A","fc_has_lod_bias","fc_image_count","fc_plane_count0","fc_texcoord_count0","fc_texture_function"};
 for(unsigned i=0;i<sizeof(fs)/sizeof(fs[0]);i++){for(unsigned j=0;j<6;j++)if(!strcmp(fs[i].name,enabled[j]))fs[i].data[0]=1;
  if(!strcmp(fs[i].name,"fc_attachment_count"))fs[i].data[0]=2;}
 for(unsigned i=0;i<sizeof(vs)/sizeof(vs[0]);i++)if(!strcmp(vs[i].name,"fc_texcoord_count0"))vs[i].data[0]=1;
 RenderDescriptor *pdf=fb_ca_descriptor(tegu_fixed_ui_test_function(ql,"fixed_vert_lph_spc",vs,sizeof(vs)/sizeof(vs[0]),1),
  tegu_fixed_ui_test_function(ql,"fixed_frag_lph_cph",fs,sizeof(fs)/sizeof(fs[0]),2),80,15,0,1);
 RenderDescriptor *pdl=fb_ca_descriptor(hav,luf,80,15,0,0);
 RenderDescriptor *pdd=fb_ca_descriptor(dsv,ds4,80,15,0,1),*pdn=fb_ca_descriptor(dsv,nb11,43,15,0,0);
 id error=0;
 /* Pipeline creation = the device's PSO stall (first use compiles the variant). */
 double tp=tegu_enc_clock();
 fb_pf=[d newRenderPipelineStateWithDescriptor:(id)pdf error:&error];fb_pl=[d newRenderPipelineStateWithDescriptor:(id)pdl error:&error];
 fb_pd=[d newRenderPipelineStateWithDescriptor:(id)pdd error:&error];fb_pn=[d newRenderPipelineStateWithDescriptor:(id)pdn error:&error];
 printf("FB_PIPES create_us=%.0f ok=%d\n",(tegu_enc_clock()-tp)*1e6,fb_pf&&fb_pl&&fb_pd&&fb_pn&&fb_pf->genericDraw&&fb_pl->genericDraw&&fb_pd->genericDraw&&fb_pn->genericDraw);
 if(!(fb_pf&&fb_pl&&fb_pd&&fb_pn)){printf("FB_FATAL pipelines\n");return 3;}
 fb_q=[d newCommandQueue];fb_vb=[d newBufferWithLength:4096 options:0];
 test_surface.width=1125;test_surface.height=2436;test_surface.row=4608;test_surface.alloc=(4608u*2436u+4095u)&~4095u;test_surface.planes=0;
 test_surface.fourcc=0x42475241u;
 c6f_big_surface=calloc(1,4608u*2436u+4096u);assert(c6f_big_surface);
 d->capabilities|=(1ULL<<15);
 {TextureDesc *sd=[[TextureDesc alloc] init];sd->format=80;sd->w=1125;sd->h=2436;sd->uses=5;
  fb_disp=[d newTextureWithDescriptor:(id)sd iosurface:(id)&test_surface plane:0];[sd release];assert(fb_disp&&fb_disp->surfaceImported);}
 fb_t1=fb_texture(d,80,1125,2436,1,5,0x20);
 fb_b43=fb_texture(d,43,565,1220,1,5,0x20);fb_c43=fb_texture(d,43,565,1220,1,5,0x20);fb_s0=fb_texture(d,80,144,213,1,5,0x20);
 for(unsigned i=0;i<4;i++){fb_small[i]=fb_texture(d,80,300,200,1,5,0x20);}
 fb_img=new_sample(d,80,194,237,1,2,1);fb_lut=new_sample(d,10,128,1,1,2,1);fb_samp=new_sampler(d,3);
 assert(fb_t1&&fb_b43&&fb_c43&&fb_s0&&fb_img&&fb_lut&&fb_samp&&fb_small[0]);
 fb_ib=[d newBufferWithLength:64 options:0];uint16_t idx[6]={0,1,2,2,1,3};memcpy([fb_ib contents],idx,sizeof(idx));
 {const char *r=getenv("FB_OFFSET_RING");fb_ring=r?(unsigned)atoi(r):0u;}
 {NSUInteger n=fb_ring?65536u:1024u;fb_fbuf=[d newBufferWithLength:n options:0];memset([fb_fbuf contents],0,n);}
 printf("FB_ENV ins_counter=%d pid=%d live_kib=%llu\n",fb_ins()?1:0,(int)getpid(),fb_live_bytes/1024);
 fb_pool_base=poolCount; /* everything created above is persistent */
 /* Draw-time variant fallback: the runtime-specialization variants are
  * prewarmed in the background at pipeline creation; wait for them (as a
  * UI that creates its pipelines ahead of use would) unless FB_VF_WAIT=0. */
 {const char *vw=getenv("FB_VF_WAIT");double t0=tegu_enc_clock();
  if(!(vw&&vw[0]=='0'))while(__atomic_load_n(&tegu_vf_draining,__ATOMIC_ACQUIRE)&&tegu_enc_clock()-t0<60)usleep(1000);
  printf("FB_VF_PREWARM wait_ms=%.1f prewarmed=%llu failed=%llu enabled=%d\n",(tegu_enc_clock()-t0)*1e3,
   (unsigned long long)tegu_vf_prewarmed,(unsigned long long)tegu_vf_failed,tegu_vf_enabled());}
 /* Cold first use of each pipeline variant (host CPU; device cost is AIR+Mali compile, see model). */
 {struct fb_scn cold={"cold_variants",1,3,"FL",2,1,0,0};
  U64 d0=tegu_vf_fallback_draws,s0=tegu_vf_standins;
  tegu_jm_batch_force=1;fb_run_frame(&f,&cold,"batched","cold",0);
  {U64 sync=0;for(unsigned r=0;r<TEGU_VF_SYNC_REASONS;r++)sync+=tegu_vf_sync_n[r];
   printf("FB_VF cold fallback_draws=%llu standins=%llu sync=%llu base_pending=%llu stolen=%llu stolen_us=%llu sync_us=%llu base_waits=%llu\n",
    (unsigned long long)(tegu_vf_fallback_draws-d0),(unsigned long long)(tegu_vf_standins-s0),(unsigned long long)sync,
    (unsigned long long)tegu_vf_sync_n[TEGU_VF_SYNC_BASE_PENDING],(unsigned long long)tegu_vf_stolen,
    (unsigned long long)tegu_vf_stolen_us,(unsigned long long)tegu_vf_sync_us,(unsigned long long)tegu_vf_base_waits);}
  double t0=tegu_enc_clock();while(__atomic_load_n(&tegu_vf_draining,__ATOMIC_ACQUIRE)&&tegu_enc_clock()-t0<60)usleep(1000);
  printf("FB_VF swap wait_ms=%.1f compiled=%llu failed=%llu dropped=%llu stale=%llu swap_us_total=%llu swap_us_max=%llu\n",(tegu_enc_clock()-t0)*1e3,
   (unsigned long long)tegu_vf_done,(unsigned long long)tegu_vf_failed,(unsigned long long)tegu_vf_dropped,(unsigned long long)tegu_vf_stale,
   (unsigned long long)tegu_vf_swap_us,(unsigned long long)tegu_vf_swap_max_us);
  d0=tegu_vf_fallback_draws;fb_run_frame(&f,&cold,"batched","cold",1);
  printf("FB_VF cold_again fallback_draws=%llu\n",(unsigned long long)(tegu_vf_fallback_draws-d0));}
 /* FB_APSO=1: asynchronous pipeline creation. The four descriptors built
  * above are in the ledger now, so a second create returns placeholders;
  * FB_APSO_DRAIN=1 runs the (host: inline) worker queue first, otherwise the
  * first draw builds them. The built pipelines must equal the synchronous
  * ones; a frame encodes and executes with the placeholders bound. */
 {const char *ap=getenv("FB_APSO");
  if(ap&&ap[0]=='1') {
   RenderDescriptor *ds[4]={pdf,pdl,pdd,pdn};TeguRenderPipeline *sync[4]={fb_pf,fb_pl,fb_pd,fb_pn},*ph[4];unsigned placeholders=0,equal=0;
   double t0=tegu_enc_clock();
   for(unsigned i=0;i<4;i++){ph[i]=[d newRenderPipelineStateWithDescriptor:(id)ds[i] error:&error];placeholders+=ph[i]&&ph[i]->asyncState!=0;}
   double createUs=(tegu_enc_clock()-t0)*1e6;
   const char *dr=getenv("FB_APSO_DRAIN");if(dr&&dr[0]=='1')tegu_apso_drain_now();
   fb_pf=ph[0];fb_pl=ph[1];fb_pd=ph[2];fb_pn=ph[3];
   struct fb_scn tick={"apso_tick",1,5,"FFLFL",1,1,0,0};
   tegu_jm_batch_force=1;fb_run_frame(&f,&tick,"batched","apso",0);
   for(unsigned i=0;i<4;i++) {
    TeguRenderPipeline *r=ph[i]->asyncState==3?ph[i]->asyncReal:0;
    equal+=r&&r->genericVsLength==sync[i]->genericVsLength&&r->genericFsLength==sync[i]->genericFsLength&&
     !memcmp(r->genericModuleDigest,sync[i]->genericModuleDigest,32)&&
     !memcmp(&r->genericCompile,&sync[i]->genericCompile,sizeof(r->genericCompile))&&r->genericDraw==sync[i]->genericDraw;
   }
   printf("FB_APSO placeholders=%u create_us=%.0f drained=%d built=%llu at_draw=%llu inert=%llu equal=%u/4 known=%u\n",
    placeholders,createUs,dr&&dr[0]=='1',(unsigned long long)tegu_apso_built,(unsigned long long)tegu_apso_steals,
    (unsigned long long)tegu_apso_failed,equal,tegu_apso_known_count);
   fb_pf=sync[0];fb_pl=sync[1];fb_pd=sync[2];fb_pn=sync[3];
   for(unsigned i=0;i<4;i++)[ph[i] release];
  }}
 for(unsigned si=0;si<sizeof(fb_scns)/sizeof(fb_scns[0]);si++){
  const struct fb_scn *s=&fb_scns[si];
  if(only&&strcmp(only,s->name))continue;
  for(int mode=1;mode>=0;mode--){            /* batched = c6jb device config, perdraw = c6ic */
   tegu_jm_batch_force=mode;
   fb_run_frame(&f,s,mode?"batched":"perdraw","warm",0);          /* first frame of this shape */
   for(unsigned k=0;k<steady;k++)fb_run_frame(&f,s,mode?"batched":"perdraw","steady",k);
  }
 }
 /* FB_CA_LOOP=<frames>: CoreAnimation's commit loop - its own CPU work
  * (FB_CA_US of wall time on this thread), encode, commit, waitUntilScheduled (CA
  * swaps after commitAndWaitUntilSubmitted), next frame - on scenario
  * FB_CA_SCN (default large). With the fake kernel's FB_GPU_US/FB_SUBMIT_US
  * time model this shows whether CA's thread is released at submission or
  * at GPU completion, and the resulting frame period. */
 {const char *loop=getenv("FB_CA_LOOP");unsigned frames=loop?(unsigned)atoi(loop):0u;
  const char *cu=getenv("FB_CA_US"),*sn=getenv("FB_CA_SCN");double caUs=cu?atof(cu):0;
  const struct fb_scn *s=&fb_scns[0];
  for(unsigned si=0;si<sizeof(fb_scns)/sizeof(fb_scns[0]);si++)if(!strcmp(fb_scns[si].name,sn?sn:"large"))s=&fb_scns[si];
  if(frames) {
   tegu_jm_batch_force=1;tegu_host_clock=1; /* perf windows (TEGU_PERF_ASYNC) need the host clock */
#ifdef FB_HAVE_ASYNC
   f.window=0; /* the finish thread reaps concurrently with submits: no fake in-flight window */
#endif
   TeguQueue *cq=[fb_d newCommandQueue];
   {const char *ff=getenv("FB_FINISH_FIFO");
    if(!(ff&&ff[0]=='0')){cq->finishes.ops=(struct tegu_submission_ops){fb_fin_retain,fb_fin_release,fb_fin_schedule};
     cq->asyncPipeline=tegu_submission_set_target(&cq->finishes,&fb_fin_target);}}
   TeguCommand **cs=calloc(frames,sizeof(*cs));double buildUs=0,commitUs=0,sched=0,schedMax=0,t0=tegu_enc_clock();unsigned draws=0;
   struct fb_snap ka,kb;fb_snap_take(&ka,&f);uint64_t ph0=0,pm0=0;
#ifdef FB_HAVE_ASYNC
   n2mb_cmdpool_stats(fb_d->connection,&ph0,&pm0);
#endif
   for(unsigned k=0;k<frames&&cs;k++) {
    if(caUs>0){long ns=(long)(caUs*1e3);struct timespec d={ns/1000000000L,ns%1000000000L};nanosleep(&d,0);} /* CA's own work, as wall time */
    double b0=tegu_enc_clock();TeguCommand *c=[cq commandBuffer];struct fb_frame fr=fb_build(c,s);draws=fr.draws;
    double w0=tegu_enc_clock();buildUs+=(w0-b0)*1e6;[c commit];commitUs+=(tegu_enc_clock()-w0)*1e6;[c waitUntilScheduled];double w=(tegu_enc_clock()-w0)*1e6;
    sched+=w;if(w>schedMax)schedMax=w;cs[k]=[c retain];
   }
   double t1=tegu_enc_clock();
   for(unsigned k=0;k<frames&&cs;k++){[cs[k] waitUntilCompleted];[cs[k] release];}
   double t2=tegu_enc_clock();
   while(poolCount>fb_pool_base)[pool[--poolCount] release];
   fb_snap_take(&kb,&f);{uint64_t ph=0,pm=0;
#ifdef FB_HAVE_ASYNC
   n2mb_cmdpool_stats(fb_d->connection,&ph,&pm);
#endif
   /* Kernel calls per pipelined frame (device TEGU_PERF_KCALL mem = alloc+release+import, map = map+unmap). */
   double fr=frames?(double)frames:1;unsigned long long kj=kb.submits-ka.submits;
   printf("FB_CALOOP_KCALL scn=%s frames=%u kjobs_per_frame=%.1f alloc=%.2f release=%.2f import=%.2f map=%.2f mem_per_job=%.2f map_per_job=%.2f pool_hits=%llu pool_misses=%llu pool_entries=%u\n",
    s->name,frames,kj/fr,(kb.calls[NORD2_GPU_SEL_ALLOC]-ka.calls[NORD2_GPU_SEL_ALLOC])/fr,(kb.calls[NORD2_GPU_SEL_RELEASE]-ka.calls[NORD2_GPU_SEL_RELEASE])/fr,
    (kb.calls[NORD2_GPU_SEL_IMPORT_SURFACE]-ka.calls[NORD2_GPU_SEL_IMPORT_SURFACE])/fr,(kb.map_calls-ka.map_calls)/fr,
    kj?(double)(kb.calls[NORD2_GPU_SEL_ALLOC]-ka.calls[NORD2_GPU_SEL_ALLOC]+kb.calls[NORD2_GPU_SEL_RELEASE]-ka.calls[NORD2_GPU_SEL_RELEASE]+kb.calls[NORD2_GPU_SEL_IMPORT_SURFACE]-ka.calls[NORD2_GPU_SEL_IMPORT_SURFACE])/kj:0,
    kj?(double)(kb.map_calls-ka.map_calls)/kj:0,(unsigned long long)(ph-ph0),(unsigned long long)(pm-pm0),
#ifdef FB_HAVE_ASYNC
    n2mb_cmdpool_count(fb_d->connection)
#else
    0u
#endif
    );}
   printf("FB_CALOOP scn=%s frames=%u draws=%u ca_us=%.0f frame_us=%.1f sched_wait_us=%.1f/%.1f drain_us=%.1f build_us=%.1f commit_us=%.1f finish_fifo=%u async=%u\n",
    s->name,frames,draws,caUs,(t1-t0)*1e6/frames,sched/frames,schedMax,(t2-t1)*1e6,buildUs/frames,commitUs/frames,cq->asyncPipeline,
#ifdef FB_HAVE_ASYNC
    n2mb_async(fb_d->connection,2)
#else
    0u
#endif
    );free(cs);
  }}
 tegu_jm_batch_force=-1;
 printf("FB_PLAN ring=%u hits=%llu misses=%llu tmpl_made=%llu tmpl_refused=%llu tmpl_hits=%llu\n",fb_ring,
  (unsigned long long)tegu_plan_memo_hits,(unsigned long long)tegu_plan_memo_misses,
  (unsigned long long)tegu_plan_tmpl_made,(unsigned long long)tegu_plan_tmpl_refused,(unsigned long long)tegu_plan_tmpl_hits);
 fb_prof_dump();
 printf("FB_DONE\n");
 return 0;
}
