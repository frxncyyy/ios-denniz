/* GPU floor microbenchmark: opt-in, one shot, never part of a frame's result.
 *
 * Every TGJ1/TGJ2 job costs ~590k GPU cycles (~650 us) from doorbell to
 * completion even for a 64x64 target (R197-R202 "TEGU GPU CLOCK"; ~340 us
 * before 0206 gave the CSG all seven fragment cores).  This submits variants
 * of one tiny generic draw job through the normal selector-19 path (kernel
 * allowlist opcodes only, see tegu_generic_bench_transform) on private
 * targets and logs one line per variant.
 *
 * Enable (checked once per process, at its first generic draw):
 *  - file /private/var/mobile/Library/Caches/com.tegu.metal.bench (or
 *    $HOME/Library/Caches/com.tegu.metal.bench).  The first process that
 *    renames it to <name>.taken runs the bench; so it runs once until the
 *    file is created again.  Its text holds the options.
 *  - or env TEGU_GPU_BENCH=<options> (that process only; "0" = off).
 * Options (space separated): "all" adds the tier-2 variants (stream shapes
 * the UI never submits: keepres, nodraw, nofrag, frag2), "reps=N" (1-32,
 * default 6), "big=0" skips the 1125x2436 target, "proc=NAME" runs it only
 * in that process (other processes leave the file in place).
 *
 * Timing: the kernel logs GPU CLOCK / TGJ1 TIME lines only for the first
 * 24/32 jobs of a boot and for jobs over 50 ms, so the bench measures the
 * selector call's wall time (map + GPU + unmap + target cache maintenance).
 * Variants of one target share every CPU-side cost, so over_nop_us (median
 * minus the same target's nop median) is the GPU-side difference.  serial=
 * is the kernel serial (GPU CLOCK token) of the variant's first sample.
 *
 * The bench draw is the process's first generic draw that builds as TGJ1:
 * its shader, bindings and vertices, retargeted to a private buffer with a
 * 16x16 scissor at the origin.  A kernel error stops the bench at once. */
extern long read(int,void *,size_t);
extern int open(const char *,int,...);
extern int close(int);
extern int rename(const char *,const char *);
extern int getpid(void);
extern int usleep(unsigned int);
#ifdef __APPLE__
extern const char *getprogname(void);
static const char *tegu_bench_process(void) {const char *p=getprogname();return p?p:"?";}
#else
static const char *tegu_bench_process(void) {return "host";}
#endif

#define TEGU_BENCH_TARGETS 3u
#define TEGU_BENCH_MAX_REPS 32u
struct tegu_bench_variant { const char *name; unsigned char target, load, transform, tier, idle_ms; };
/* Targets: 0 = 64x64, 1 = 16x16 (no 2 MiB heap), 2 = 1125x2436 (full screen). */
static const struct tegu_bench_variant tegu_bench_variants[]={
 {"nop",0,1,TEGU_BENCH_NOP,1,0},
 {"flush1",0,1,TEGU_BENCH_FLUSH1,1,0},
 {"flush8",0,1,TEGU_BENCH_FLUSH8,1,0},
 {"reqres",0,1,TEGU_BENCH_REQRES,1,0},
 {"full",0,1,TEGU_BENCH_AS_BUILT,1,0},
 {"full_idle20",0,1,TEGU_BENCH_AS_BUILT,1,20},
 {"clear",0,2,TEGU_BENCH_AS_BUILT,1,0},
 {"noflush",0,1,TEGU_BENCH_NOFLUSH,1,0},
 {"bbox",0,1,TEGU_BENCH_BBOX,1,0},
 {"t16_nop",1,1,TEGU_BENCH_NOP,1,0},
 {"t16_full",1,1,TEGU_BENCH_AS_BUILT,1,0},
 {"big_nop",2,1,TEGU_BENCH_NOP,1,0},
 {"big_full",2,1,TEGU_BENCH_AS_BUILT,1,0},
 {"big_bbox",2,1,TEGU_BENCH_BBOX,1,0},
 {"big_clear",2,2,TEGU_BENCH_AS_BUILT,1,0},
 {"keepres",0,1,TEGU_BENCH_KEEPRES,2,0},
 {"nodraw",0,1,TEGU_BENCH_NODRAW,2,0},
 {"nofrag",0,1,TEGU_BENCH_NOFRAG,2,0},
 {"frag2",0,1,TEGU_BENCH_FRAG2,2,0},
 {"big_frag2",2,1,TEGU_BENCH_FRAG2,2,0},
};
#define TEGU_BENCH_VARIANTS (sizeof(tegu_bench_variants)/sizeof(tegu_bench_variants[0]))
static const uint32_t tegu_bench_size[TEGU_BENCH_TARGETS][3]={{64,64,256},{16,16,64},{1125,2436,4608}};

static void tegu_bench_log(const char *line,int n) {
 if(n>0)write(2,line,(size_t)n<256?(size_t)n:255);
}
static size_t tegu_bench_len(const char *s) {size_t n=0;while(s[n])n++;return n;}
static int tegu_bench_has(const char *opts,const char *word) {
 size_t w=tegu_bench_len(word);
 for(const char *p=opts;*p;) {
  while(*p==' ')p++;
  const char *e=p;while(*e&&*e!=' ')e++;
  if((size_t)(e-p)==w&&!memcmp(p,word,w))return 1;
  p=e;
 }
 return 0;
}
/* "proc=NAME" in opts: only that process runs the bench. */
static int tegu_bench_process_ok(const char *opts) {
 const char *self=tegu_bench_process();
 for(const char *p=opts;*p;p++)
  if((p==opts||p[-1]==' ')&&p[0]=='p'&&p[1]=='r'&&p[2]=='o'&&p[3]=='c'&&p[4]=='=') {
   const char *e=p+5;while(*e&&*e!=' ')e++;
   size_t n=(size_t)(e-(p+5));
   return n==tegu_bench_len(self)&&!memcmp(p+5,self,n);
  }
 return 1;
}
/* Reads the env, or claims the file flag (rename); options into opts. */
static int tegu_bench_claim(char *opts,size_t size) {
 opts[0]=0;
 const char *env=getenv("TEGU_GPU_BENCH");
 if(env&&env[0]&&!(env[0]=='0'&&!env[1])){snprintf(opts,size,"%s",env);return tegu_bench_process_ok(opts);}
 const char *home=getenv("HOME");
 char path[2][256];unsigned paths=0;
 snprintf(path[paths++],sizeof(path[0]),"/private/var/mobile/Library/Caches/com.tegu.metal.bench");
 if(home&&home[0]=='/'&&snprintf(path[paths],sizeof(path[0]),"%s/Library/Caches/com.tegu.metal.bench",home)<(int)sizeof(path[0]))paths++;
 for(unsigned i=0;i<paths;i++) {
  int fd=open(path[i],0);
  if(fd<0)continue;
  long n=read(fd,opts,size-1);close(fd);opts[n>0?n:0]=0;
  for(char *c=opts;*c;c++)if(*c=='\n'||*c=='\r'||*c=='\t')*c=' ';
  /* Another process's bench: leave the flag in place for it. */
  if(!tegu_bench_process_ok(opts)){opts[0]=0;return 0;}
  char taken[272];snprintf(taken,sizeof(taken),"%s.taken",path[i]);
  return !rename(path[i],taken);
 }
#ifdef TEGU_GPU_BENCH_DEFAULT
 /* A bench-only bundle (build_compute_bundle.py --gpu-bench-default): no
  * file flag needed, once per process start. */
 snprintf(opts,size,"%s",TEGU_GPU_BENCH_DEFAULT);
 return tegu_bench_process_ok(opts);
#else
 return 0;
#endif
}
static int tegu_bench_prefix(const char *p,const char *key,size_t k) {
 for(size_t i=0;i<k;i++)if(p[i]!=key[i])return 0; /* stops at p's NUL: key has none */
 return 1;
}
static unsigned tegu_bench_number(const char *opts,const char *key,unsigned fallback) {
 size_t k=tegu_bench_len(key);
 for(const char *p=opts;*p;p++)
  if((p==opts||p[-1]==' ')&&tegu_bench_prefix(p,key,k)) {
   unsigned v=0;const char *d=p+k;if(*d<'0'||*d>'9')return fallback;
   while(*d>='0'&&*d<='9'&&v<100000u)v=v*10u+(unsigned)(*d++-'0');
   return v;
  }
 return fallback;
}
static void tegu_bench_sort(double *v,unsigned n) {
 for(unsigned i=1;i<n;i++){double x=v[i];unsigned k=i;while(k&&v[k-1]>x){v[k]=v[k-1];k--;}v[k]=x;}
}

/* One variant job: build the draw on target t, rewrite its stream.  0 or the
 * builder/transform error (nothing submitted). */
static int tegu_bench_build(const struct tegu_generic_draw_shader *sh,const struct tegu_generic_draw_request *base,
 TeguBuffer *target,unsigned t,const struct tegu_bench_variant *v,const struct tegu_generic_job_images *images,
 uint32_t cap,struct tegu_generic_draw_request *q,struct tegu_generic_job_request *r) {
 *q=*base;
 q->width=tegu_bench_size[t][0];q->height=tegu_bench_size[t][1];q->row_bytes=tegu_bench_size[t][2];
 q->target_token=target->handle;q->target_offset=0;q->target_length=target->logical;
 q->load_action=v->load;q->clear_color=0;
 q->scissor[0]=q->scissor[1]=0;q->scissor[2]=q->scissor[3]=16;
 memset(r,0,sizeof(*r));
 int built=tegu_generic_draw_job_build(sh,q,images,r);
 if(built)return built<0?built:-1;
 const uint32_t bbox[4]={0,0,15,15};
 if(tegu_generic_bench_transform(r->commands,&r->command_count,cap,v->transform,bbox))return -50;
 return 0;
}

static void tegu_gpu_floor_bench_run(const struct Dispatch *j,TeguComputeDevice *d,const char *opts,int *pending) {
 TeguRenderPipeline *pipeline=j->renderPipeline;
 const struct tegu_generic_draw_shader *sh=&pipeline->genericShader;
 const int all=tegu_bench_has(opts,"all"),big=!tegu_bench_has(opts,"big=0");
 unsigned reps=tegu_bench_number(opts,"reps=",6);if(reps<1)reps=1;if(reps>TEGU_BENCH_MAX_REPS)reps=TEGU_BENCH_MAX_REPS;
 const uint32_t cap=(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB_160)?TEGU_GENERIC_JOB_MAX_COMMANDS:TEGU_GENERIC_JOB_SUBMIT_COMMANDS;
 struct tegu_generic_draw_request *q=malloc(sizeof(*q));
 struct tegu_generic_job_request *r=malloc(sizeof(*r));
 double (*wall)[TEGU_BENCH_MAX_REPS]=calloc(TEGU_BENCH_VARIANTS,sizeof(*wall));
 unsigned ok[TEGU_BENCH_VARIANTS],cmds[TEGU_BENCH_VARIANTS];int rc[TEGU_BENCH_VARIANTS];U64 serial[TEGU_BENCH_VARIANTS];
 memset(ok,0,sizeof(ok));memset(cmds,0,sizeof(cmds));memset(rc,0,sizeof(rc));memset(serial,0,sizeof(serial));
 TeguBuffer *targets[TEGU_BENCH_TARGETS]={0};
 char line[256];int n;
 if(!q||!r||!wall){free(q);free(r);free(wall);*pending=0;return;}
 targets[0]=[d newBufferWithLength:(NSUInteger)tegu_bench_size[0][2]*tegu_bench_size[0][1] options:0];
 TeguBuffer *scratch=0,*load=0;
 int images=tegu_job_images_acquire(d,&scratch,&load);
 struct tegu_generic_job_images jobImages={images?scratch->handle:0,images?load->handle:0,
  images?(uint8_t *)tegu_buffer_bytes(scratch):0,images?(uint8_t *)tegu_buffer_bytes(load):0,0};
 /* Wait for a draw that builds as one TGJ1 job on the 64x64 target. */
 int probe=-1;
 if(images&&targets[0])probe=tegu_bench_build(sh,&j->genericDrawRequest,targets[0],0,&tegu_bench_variants[4],&jobImages,cap,q,r);
 if(probe) {
  tegu_job_images_release();
  for(unsigned t=0;t<TEGU_BENCH_TARGETS;t++)[targets[t] release];
  free(q);free(r);free(wall);
  return; /* still pending: try the next generic draw */
 }
 *pending=0;
 for(unsigned t=1;t<TEGU_BENCH_TARGETS;t++)
  if(t!=2||big)targets[t]=[d newBufferWithLength:(NSUInteger)tegu_bench_size[t][2]*tegu_bench_size[t][1] options:0];
 n=snprintf(line,sizeof(line),"TEGU_GPU_BENCH begin proc=%s pid=%d caps=0x%llx cap=%u reps=%u tier=%u big=%d fs=%s fragment_bbox=%d opts=\"%.60s\"\n",
  tegu_bench_process(),getpid(),(U64)d->capabilities,cap,reps,all?2u:1u,targets[2]!=0,pipeline->genericName,
  tegu_generic_fragment_bbox(),opts);
 tegu_bench_log(line,n);
 /* The bench measures the production stream: the bbox gate stays off. */
 int gate=tegu_generic_fragment_bbox();tegu_generic_set_fragment_bbox(0);
 double began=tegu_host_seconds();unsigned jobs=0;int aborted=0;
 for(unsigned rep=0;rep<reps&&!aborted;rep++)for(unsigned i=0;i<TEGU_BENCH_VARIANTS&&!aborted;i++) {
  const struct tegu_bench_variant *v=&tegu_bench_variants[i];
  if((v->tier>1&&!all)||!targets[v->target]||rc[i])continue;
  int built=tegu_bench_build(sh,&j->genericDrawRequest,targets[v->target],v->target,v,&jobImages,cap,q,r);
  if(built){rc[i]=built;continue;}
  cmds[i]=r->command_count;
  if(v->idle_ms)usleep(v->idle_ms*1000u);
  U64 reply[2]={0};unsigned outs=2;
  double t0=tegu_host_seconds();
  int error=d->method(d->connection,TEGU_GENERIC_JOB_SELECTOR,0,0,r,sizeof(*r),reply,&outs,0,0);
  double t1=tegu_host_seconds();jobs++;
  if(error||outs!=2||!reply[0]) {
   rc[i]=error?error:-2;aborted=1;
   n=snprintf(line,sizeof(line),"TEGU_GPU_BENCH abort variant=%s rep=%u error=0x%x outs=%u serial=%llu\n",v->name,rep,(unsigned)error,outs,(U64)reply[0]);
   tegu_bench_log(line,n);
   break;
  }
  if(!ok[i])serial[i]=reply[0];
  wall[i][ok[i]++]=(t1-t0)*1e6;
 }
 tegu_generic_set_fragment_bbox(gate);
 tegu_job_images_release();
 double nopMedian[TEGU_BENCH_TARGETS]={-1,-1,-1};
 for(unsigned i=0;i<TEGU_BENCH_VARIANTS;i++)if(ok[i]){tegu_bench_sort(wall[i],ok[i]);
  if(tegu_bench_variants[i].transform==TEGU_BENCH_NOP)nopMedian[tegu_bench_variants[i].target]=wall[i][ok[i]/2];}
 for(unsigned i=0;i<TEGU_BENCH_VARIANTS;i++) {
  const struct tegu_bench_variant *v=&tegu_bench_variants[i];
  if((v->tier>1&&!all)||!targets[v->target])continue;
  double med=ok[i]?wall[i][ok[i]/2]:-1,nop=nopMedian[v->target];
  n=snprintf(line,sizeof(line),"TEGU_GPU_BENCH variant=%s target=%ux%u load=%u cmds=%u reps=%u ok=%u wall_min_us=%.0f "
   "wall_med_us=%.0f wall_max_us=%.0f over_nop_us=%.0f serial=%llu rc=%d\n",v->name,tegu_bench_size[v->target][0],
   tegu_bench_size[v->target][1],v->load,cmds[i],reps,ok[i],ok[i]?wall[i][0]:-1.0,med,ok[i]?wall[i][ok[i]-1]:-1.0,
   ok[i]&&nop>=0?med-nop:-1.0,(U64)serial[i],rc[i]);
  tegu_bench_log(line,n);
 }
 n=snprintf(line,sizeof(line),"TEGU_GPU_BENCH end jobs=%u aborted=%d ms=%.0f\n",jobs,aborted,(tegu_host_seconds()-began)*1e3);
 tegu_bench_log(line,n);
 for(unsigned t=0;t<TEGU_BENCH_TARGETS;t++)[targets[t] release];
 free(q);free(r);free(wall);
}

#include "tile_fetch_probe.inc"

/* Called before each generic draw job is submitted; costs one load per call
 * once the bench is done or disabled. */
static unsigned tegu_bench_state; /* 0 unchecked, 1 claimed and waiting for a draw, 2 off/done */
static unsigned tegu_bench_lock,tegu_bench_attempts;
static char tegu_bench_opts[128];
static void tegu_gpu_floor_bench_maybe(const struct Dispatch *j,TeguComputeDevice *d) {
#ifdef TEGU_TILE_FETCH_PROBE
 tegu_tile_fetch_probe_maybe(d);
#endif
 if(__atomic_load_n(&tegu_bench_state,__ATOMIC_ACQUIRE)==2)return;
 tegu_lock(&tegu_bench_lock);
 if(tegu_bench_state==0)tegu_bench_state=tegu_bench_claim(tegu_bench_opts,sizeof(tegu_bench_opts))?1:2;
 if(tegu_bench_state==1) {
  int pending=1;
  TeguRenderPipeline *p=j?j->renderPipeline:0;
  if(d&&(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB)&&p&&p->genericDraw&&j->genericDrawSnapshot)
   tegu_gpu_floor_bench_run(j,d,tegu_bench_opts,&pending);
  if(pending&&++tegu_bench_attempts>=256) {
   char line[160];int n=snprintf(line,sizeof(line),"TEGU_GPU_BENCH skipped: no generic draw built as one TGJ1 job in %u tries\n",
    tegu_bench_attempts);
   tegu_bench_log(line,n);pending=0;
  }
  if(!pending)__atomic_store_n(&tegu_bench_state,2,__ATOMIC_RELEASE);
 }
 tegu_unlock(&tegu_bench_lock);
}

/* Production opt-in (default off): fragment bbox = union of the scissors of a
 * LOAD pass (see tegu_generic_set_fragment_bbox).  Env TEGU_FRAGMENT_BBOX=1
 * or the file /private/var/mobile/Library/Caches/com.tegu.metal.fragbbox. */
#if !defined(TEGU_FRONTEND_HOST)
__attribute__((constructor)) static void tegu_fragment_bbox_init(void) {
 const char *env=getenv("TEGU_FRAGMENT_BBOX");
#ifdef TEGU_FRAGMENT_BBOX_DEFAULT
 /* build_compute_bundle.py --fragment-bbox: on unless TEGU_FRAGMENT_BBOX=0. */
 int on=env?(env[0]=='1'):1;
 if(!env){tegu_generic_set_fragment_bbox(on);return;}
#else
 int on=env?(env[0]=='1'):0;
#endif
 if(!env){int fd=open("/private/var/mobile/Library/Caches/com.tegu.metal.fragbbox",0);if(fd>=0){on=1;close(fd);}}
 tegu_generic_set_fragment_bbox(on);
}
#endif

/* Batch merge gates (tegu_generic_set_code_share, tegu_batch_heap_slack): off
 * by default; build_compute_bundle.py --batch-merge turns both on.  Env
 * TEGU_BATCH_SHARE / TEGU_BATCH_HEAP (1 on, 0 off) override either. */
#if !defined(TEGU_FRONTEND_HOST)
static int tegu_batch_gate(const char *name) {
 const char *env=getenv(name);
#ifdef TEGU_BATCH_MERGE_DEFAULT
 return env?env[0]=='1':1;
#else
 return env?env[0]=='1':0;
#endif
}
__attribute__((constructor)) static void tegu_batch_merge_init(void) {
 const char *reorder=getenv("TEGU_DRAW_REORDER");
#ifdef TEGU_DRAW_REORDER_DEFAULT
 tegu_draw_reorder_enabled=reorder?reorder[0]=='1':1;
#else
 tegu_draw_reorder_enabled=reorder&&reorder[0]=='1';
#endif
 const char *small=getenv("TEGU_SMALL_HEAP");
#ifdef TEGU_SMALL_HEAP_DEFAULT
 tegu_batch_small_heap=small?small[0]=='1':1;
#else
 tegu_batch_small_heap=small&&small[0]=='1';
#endif
 {char line[64];int n=snprintf(line,sizeof(line),"TEGU_SMALL_HEAP enabled=%u\n",tegu_batch_small_heap);
  if(n>0)write(2,line,(size_t)n);}
 const char *tile32=getenv("TEGU_TILE32");
#ifdef TEGU_TILE32_DEFAULT
 tegu_generic_set_tile_pixels((tile32?tile32[0]=='1':1)?1024:256);
#else
 tegu_generic_set_tile_pixels(tile32&&tile32[0]=='1'?1024:256);
#endif
 {char size[64];int n=snprintf(size,sizeof(size),"TEGU_TILE_SIZE pixels=%u\n",tegu_generic_tile_pixels());
  if(n>0)write(2,size,(size_t)n);}
 const char *resources=getenv("TEGU_BATCH_RESOURCES");
#ifdef TEGU_RESOURCE_SHARE_DEFAULT
 tegu_generic_set_resource_share(resources?resources[0]=='1':1);
#else
 tegu_generic_set_resource_share(resources&&resources[0]=='1');
#endif
 tegu_generic_set_code_share(tegu_batch_gate("TEGU_BATCH_SHARE"));
 tegu_batch_heap_slack=tegu_batch_gate("TEGU_BATCH_HEAP")?TEGU_BATCH_HEAP_SLACK_BYTES:0;
 char line[96];int n=snprintf(line,sizeof(line),"TEGU_BATCH_MERGE share=%d heap_slack_kb=%u reorder=%u resources=%d\n",
  tegu_generic_code_share(),tegu_batch_heap_slack>>10,tegu_draw_reorder_enabled,tegu_generic_resource_share());
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
}
#endif
