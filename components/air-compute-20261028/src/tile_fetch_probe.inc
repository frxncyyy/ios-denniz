/* One-shot explicit GPU qualification, compiled only into a probe bundle.
 * Compare ordered LD_TILE draws in ONE pass against the existing memory-fetch
 * shader in separate completed passes. Targets are private and never shown.
 * Same-draw duplicate triangles additionally exercise intra-draw ordering.
 * A passing result is necessary, not sufficient, for broad UI activation. */
#ifdef TEGU_TILE_FETCH_PROBE
#include "tegu-tile-fetch-assets.h"
static int tegu_tile_probe_submit(TeguComputeDevice *d,const struct tegu_generic_job_images *images,
 TeguBuffer *target,TeguBuffer *vertices,unsigned draws,unsigned duplicate,unsigned clear,unsigned half,unsigned width,unsigned height,unsigned row,int tile,unsigned plain,unsigned wide,double *phase_us) {
 if(phase_us)*phase_us=0;
 struct tegu_generic_draw_request q[24];const struct tegu_generic_draw_request *requests[24];
 const struct tegu_generic_draw_shader *shaders[24];
 const struct tegu_generic_draw_shader *shader=tile?&tegu_tile_tile_shader:&tegu_tile_memory_shader;
#ifdef TEGU_TILE_WIDE_CONTROL_ASSET
 if(wide==1)shader=tile?&tegu_tile_tile_wide_shader:&tegu_tile_memory_wide_shader;
#else
 if(wide)return -1;
#endif
#ifdef TEGU_TILE_MEDIUM_CONTROL_ASSET
 if(wide==2)shader=tile?&tegu_tile_tile_medium_shader:&tegu_tile_memory_medium_shader;
#else
 if(wide==2)return -1;
#endif
 struct tegu_generic_draw_shader control;
#ifdef TEGU_TILE_PLAIN_CONTROL_ASSET
 if(plain) {
  control=tegu_tile_plain_shader;
  /* Diagnostic only: retain tile-read ordering/FPK restrictions even though
   * this control shader does not issue LD_TILE. Never used by UI pipelines. */
  if(tile&&plain==1)control.flags|=TEGU_GENERIC_SHADER_TILE_FETCH;
  shader=&control;
 }
#else
 (void)control;if(plain)return -1;
#endif
 if(!draws||draws>24)return -1;
 for(unsigned i=0;i<draws;i++) {
  memset(&q[i],0,sizeof(q[i]));q[i].width=width;q[i].height=height;q[i].row_bytes=row;
  q[i].load_action=clear&&i==0?2:1;q[i].vertex_count=duplicate==2?(i?6:48):duplicate?6:3;
  q[i].scissor[2]=half?width/2:width;q[i].scissor[3]=height;
  q[i].target_token=target->handle;q[i].target_length=target->logical;
  q[i].vertex=(struct tegu_generic_draw_memory){vertices->handle,0,q[i].vertex_count*16u};
  q[i].binding_count=plain&&(!tile||plain>1)?0:1;q[i].bindings[0].kind=tile?TEGU_GENERIC_DRAW_TILE_ATTACHMENT:TEGU_GENERIC_DRAW_ATTACHMENT;
  requests[i]=&q[i];shaders[i]=shader;
 }
 if(tile&&draws>1) {
  const struct tegu_generic_batch_limits limits={TEGU_GENERIC_JOB2_VERSION,TEGU_GENERIC_JOB2_MAX_COMMANDS,
   TEGU_GENERIC_BATCH_MAX_DRAWS,TEGU_GENERIC_BATCH_HEAP_BUDGET,0};
  struct tegu_generic_job2_request *job=malloc(sizeof(*job));if(!job)return -2;
  struct tegu_generic_batch_info info={0};
  int built=tegu_generic_draw_batch_build2(shaders,requests,draws,images,&limits,job,&info);
  int error=-3;
  if(built==(int)draws){U64 out[3]={0};unsigned n=3;
   /* Mode2 removes destination-read tagging but still forbids forward kill.
    * Mode3 uses the ordinary plain shader's unmodified FPK flags. */
   if(plain==2)for(unsigned k=0;k<job->command_count;k++)
    if((job->commands[k]>>56)==2&&((job->commands[k]>>48)&255)==57)
     job->commands[k]&=~UINT64_C(0xffffffff);
   error=d->method(d->connection,TEGU_GENERIC_JOB2_SELECTOR,0,0,job,sizeof(*job),out,&n,0,0);
   if(!error&&(n!=3||!out[0]))error=-4;
   char line[224];int length=snprintf(line,sizeof(line),
    "%s size=%ux%u draws=%u vertices=%u half=%u clear=%u serial=%llu estimate=%llu used=%llu rc=%d\n",
    plain?"TEGU_PLAIN_HEAP":wide==2?"TEGU_MEDIUM_HEAP":wide?"TEGU_WIDE_HEAP":"TEGU_TILE_HEAP",width,height,draws,q[0].vertex_count,half,clear,out[0],(U64)info.heap_estimate,out[2],error);
   tegu_bench_log(line,length);
   if(!error&&width>=1000&&draws>=4) {
    double phase_begin=tegu_host_seconds();
    error=tegu_generic_bench_transform(job->commands,&job->command_count,
     TEGU_GENERIC_JOB2_MAX_COMMANDS,TEGU_BENCH_NOFRAG,0);
    double started=tegu_host_seconds();n=3;memset(out,0,sizeof(out));
    if(!error)error=d->method(d->connection,TEGU_GENERIC_JOB2_SELECTOR,0,0,job,sizeof(*job),out,&n,0,0);
    double elapsed=(tegu_host_seconds()-started)*1e6;
    if(!error&&(n!=3||!out[0]))error=-4;
    length=snprintf(line,sizeof(line),"TEGU_PROBE_PHASE mode=%u wide=%u draws=%u half=%u phase=nofrag us=%.0f serial=%llu heap=%llu rc=%d\n",
     plain,wide,draws,half,elapsed,out[0],out[2],error);tegu_bench_log(line,length);
    if(phase_us)*phase_us=(tegu_host_seconds()-phase_begin)*1e6;
   }
  }
  free(job);return error;
 }
 struct tegu_generic_job_request *job=malloc(sizeof(*job));if(!job)return -2;
 int error=0;
 for(unsigned i=0;i<draws&&!error;i++) {
  error=tegu_generic_draw_job_build(shader,&q[i],images,job);
  if(!error){U64 out[2]={0};unsigned n=2;
   error=d->method(d->connection,TEGU_GENERIC_JOB_SELECTOR,0,0,job,sizeof(*job),out,&n,0,0);
   if(!error&&(n!=2||!out[0]))error=-4;}
 }
 free(job);return error;
}
static void tegu_tile_fetch_probe_maybe(TeguComputeDevice *d) {
 static unsigned done;
 if(!tegu_bench_process_ok("proc=backboardd")||__atomic_exchange_n(&done,1,__ATOMIC_ACQ_REL))return;
 if(!d||!(d->capabilities&TEGU_GPU_CAP_GENERIC_JOB2))return;
 const unsigned length=4608u*2436u;
 TeguBuffer *reference=[d newBufferWithLength:length options:0],*target=[d newBufferWithLength:length options:0],
  *vertices=[d newBufferWithLength:768 options:0],*scratch=0,*load=0;
 int images=0,error=0;unsigned failed=0,ran=0,expected_cases=23;
#ifdef TEGU_TILE_PLAIN_CONTROL_ASSET
 expected_cases+=10;
#endif
#ifdef TEGU_TILE_WIDE_CONTROL_ASSET
 expected_cases+=8;
#endif
#ifdef TEGU_TILE_MEDIUM_CONTROL_ASSET
 expected_cases+=5;
#endif
 if(!reference||!target||!vertices){error=-10;goto done;}
 {const float triangle[12]={-1,-1,0,1,3,-1,0,1,-1,3,0,1};
  for(unsigned k=0;k<16;k++)memcpy((char *)tegu_buffer_bytes(vertices)+k*sizeof(triangle),triangle,sizeof(triangle));}
 images=tegu_job_images_acquire(d,&scratch,&load);
 if(!images){error=-11;goto done;}
 {const struct tegu_generic_job_images im={scratch->handle,load->handle,tegu_buffer_bytes(scratch),tegu_buffer_bytes(load),0};
  /* duplicate=0: one triangle; 1: two per draw; 2: first mesh has
   * 16 triangles, later draws two. The new cases cover <=24 primitives. */
  const unsigned cases[][9]={{1,0,0,0,64,64,320},{2,0,0,0,64,64,320},{4,0,0,0,64,64,320},
   {8,0,0,0,64,64,320},{2,0,1,0,64,64,320},{4,0,0,1,64,64,320},{1,1,0,0,64,64,320},
   {2,0,0,0,1125,2436,4608},{4,0,0,0,1125,2436,4608},
   {6,0,0,0,1125,2436,4608},{8,0,0,0,1125,2436,4608},
   {4,0,0,1,1125,2436,4608},{4,1,0,0,1125,2436,4608},
   {7,0,0,0,1125,2436,4608},{9,0,0,0,1125,2436,4608},
   {12,0,0,0,1125,2436,4608},{16,0,0,0,1125,2436,4608},
   {24,0,0,0,1125,2436,4608},{24,0,0,0,64,64,320},{12,1,0,0,1125,2436,4608},
   {2,2,0,0,1125,2436,4608},{4,2,0,0,1125,2436,4608},{5,2,0,1,1125,2436,4608},
#ifdef TEGU_TILE_PLAIN_CONTROL_ASSET
   {4,0,0,0,1125,2436,4608,1},{8,0,0,0,1125,2436,4608,1},
   {12,0,0,0,1125,2436,4608,1},{24,0,0,0,1125,2436,4608,1},
   {8,0,0,0,1125,2436,4608,2},{12,0,0,0,1125,2436,4608,2},{24,0,0,0,1125,2436,4608,2},
   {8,0,0,0,1125,2436,4608,3},{12,0,0,0,1125,2436,4608,3},{24,0,0,0,1125,2436,4608,3},
#endif
#ifdef TEGU_TILE_WIDE_CONTROL_ASSET
   {2,0,0,0,1125,2436,4608,0,1},{4,0,0,0,1125,2436,4608,0,1},
   {8,0,0,0,1125,2436,4608,0,1},{12,0,0,0,1125,2436,4608,0,1},
   {12,1,0,0,1125,2436,4608,0,1},
   {2,2,0,0,1125,2436,4608,0,1},{4,2,0,0,1125,2436,4608,0,1},{5,2,0,1,1125,2436,4608,0,1},
#endif
#ifdef TEGU_TILE_MEDIUM_CONTROL_ASSET
   {2,0,0,0,1125,2436,4608,0,2},{4,0,0,0,1125,2436,4608,0,2},
   {8,0,0,0,1125,2436,4608,0,2},{12,1,0,0,1125,2436,4608,0,2},{5,2,0,1,1125,2436,4608,0,2},
#endif
  };
  for(unsigned c=0;c<sizeof(cases)/sizeof(cases[0])&&!error;c++) {
   unsigned draws=cases[c][0],duplicate=cases[c][1],clear=cases[c][2],half=cases[c][3];
   unsigned width=cases[c][4],height=cases[c][5],row=cases[c][6],plain=cases[c][7],wide=cases[c][8];
   unsigned char *a=tegu_buffer_bytes(reference),*b=tegu_buffer_bytes(target);
   memset(a,0xa5,length);memset(b,0xa5,length);
   for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++)for(unsigned k=0;k<4;k++) {
    /* Asymmetric seed checks channel conversion, alpha and LOAD preservation. */
    unsigned char seed=clear?0:(unsigned char)(17+34*k);
    a[y*row+x*4+k]=b[y*row+x*4+k]=seed;
   }
   if(plain){char mode[96];int n=snprintf(mode,sizeof(mode),"TEGU_PROBE_MODE case=%u mode=%u\n",c,plain);tegu_bench_log(mode,n);}
   unsigned primitives=duplicate==2?16+2*(draws-1):draws*(duplicate?2:1);
   double phase_us=0,started=tegu_host_seconds();
   error=tegu_tile_probe_submit(d,&im,reference,vertices,primitives,0,0,half,width,height,row,0,plain,wide,0);
   double reference_us=(tegu_host_seconds()-started)*1e6;started=tegu_host_seconds();
   if(!error)error=tegu_tile_probe_submit(d,&im,target,vertices,draws,duplicate,clear,half,width,height,row,1,plain,wide,&phase_us);
   double tile_us=(tegu_host_seconds()-started)*1e6-phase_us;
   unsigned exact=0,bad=0,badref=0,maxdiff=0,guards=0,changed=0;
   unsigned expected[4];const double colors[4]={191.25,127.5,63.75,255.0};
   for(unsigned k=0;k<4;k++) {
    expected[k]=clear?0:17+34*k;
    if(plain)expected[k]=(unsigned)(colors[k]+0.5);
    else for(unsigned draw=0;draw<primitives;draw++)expected[k]=(unsigned)((expected[k]+colors[k])*0.5+0.5);
   }
   if(!error)for(unsigned y=0;y<height;y++)for(unsigned x=0;x<row;x++) {
    unsigned at=y*row+x,delta=a[at]>b[at]?a[at]-b[at]:b[at]-a[at];
    if(x>=width*4){guards+=a[at]!=0xa5||b[at]!=0xa5;continue;}
    if(half&&x>=width/2*4){unsigned char seed=clear?0:(unsigned char)(17+34*(x%4));
     guards+=a[at]!=seed||b[at]!=seed;continue;}
    unsigned refdiff=a[at]>expected[x%4]?a[at]-expected[x%4]:expected[x%4]-a[at];badref+=refdiff>1;
    exact+=delta!=0;bad+=delta>1;if(delta>maxdiff)maxdiff=delta;
    changed+=a[at]!=(clear?0:(unsigned char)(17+34*(x%4)));
   }
   failed+=error||bad||badref||guards||!changed;ran++;
   char line[240];int n=snprintf(line,sizeof(line),
    "%s case=%u size=%ux%u draws=%u duplicate=%u clear=%u half=%u rc=%d differing=%u bad=%u refbad=%u maxdiff=%u guards=%u changed=%u ref_us=%.0f tile_us=%.0f\n",
    plain?"TEGU_PLAIN_PROBE":wide==2?"TEGU_MEDIUM_PROBE":wide?"TEGU_WIDE_PROBE":"TEGU_TILE_PROBE",c,width,height,draws,duplicate,clear,half,error,exact,bad,badref,maxdiff,guards,changed,reference_us,tile_us);
   tegu_bench_log(line,n);
  }
 }
 done:
 if(images)tegu_job_images_release();
 [reference release];[target release];[vertices release];
 {char line[128];int n=snprintf(line,sizeof(line),"TEGU_TILE_PROBE end cases=%u failed=%u error=%d result=%s\n",
  ran,failed,error,ran==expected_cases&&!failed&&!error?"PASS":"FAIL");tegu_bench_log(line,n);}
}
#endif
