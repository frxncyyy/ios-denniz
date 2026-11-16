/* Execution-time draw batching: consecutive generic draws of one pass reach
 * selector 19 as ONE job (one fragment pass, every draw's RUN_IDVS); another
 * target, a blit or a CLEAR ends the run, and the run keeps the chain flags
 * of its first and last draw. */
static uint8_t genericBatchVertexCode[1152],genericBatchFragmentCode[1408];
static void generic_batch_job(TeguCommand *command,unsigned index,TeguRenderPipeline *p,TeguBuffer *target,TeguBuffer *uniform,
 unsigned load,unsigned x) {
 struct Dispatch *j=tegu_command_job(command,index);memset(j,0,sizeof(*j));
 struct tegu_generic_draw_request *q=&j->genericDrawRequest;
 q->width=64;q->height=32;q->row_bytes=256;q->load_action=load;q->clear_color=load==2?0xff000000u:0;q->vertex_count=6;
 q->scissor[0]=x;q->scissor[2]=8;q->scissor[3]=32;
 q->target_token=target->handle;q->target_length=target->logical;
 q->bindings[0]=(struct tegu_generic_draw_binding){0,TEGU_GENERIC_DRAW_BUFFER,{uniform->handle,64u*x,64},0,0,0,0,0,0};q->binding_count=1;
 q->fragment_push[0]=(uint8_t)x;q->fragment_push_bytes=4;
 j->genericDrawSnapshot=1;j->renderPipeline=[p retain];j->renderArgs[1]=target->handle;j->compositorLoadAction=load;
 memcpy(j->genericName,"batch_test",11);
 if(index>=command->count)command->count=index+1;
}
static void test_generic_draw_batch_frontend(TeguComputeDevice *device) {
 U64 oldCaps=device->capabilities;unsigned oldMode=fixedUiPsoMode;
 device->capabilities=(device->capabilities&~(TEGU_GPU_CAP_GENERIC_JOB2|TEGU_GPU_CAP_GENERIC_JOB_160))|
  TEGU_GPU_CAP_GENERIC_JOB|TEGU_GPU_CAP_GENERIC_JOB_BATCH;fixedUiPsoMode=1;
 TeguRenderPipeline *p=[[TeguRenderPipeline alloc] init];p->genericDraw=1;
 struct tegu_generic_draw_shader *sh=&p->genericShader;
 sh->vertex_code=genericBatchVertexCode;sh->vertex_bytes=sizeof(genericBatchVertexCode);
 sh->fragment_code=genericBatchFragmentCode;sh->fragment_bytes=sizeof(genericBatchFragmentCode);
 sh->vertex_registers=sh->secondary_registers=sh->fragment_registers=32;sh->vertex_fau_words=32;sh->fragment_fau_words=34;
 sh->secondary_offset=640;sh->varying_bytes=16;sh->blend=(struct tegu_blend_inputs){1,1,0,1,5,0,1,5,15};
 TeguBuffer *a=[device newBufferWithLength:256u*32u options:0],*b=[device newBufferWithLength:256u*32u options:0];
 TeguBuffer *uniform=[device newBufferWithLength:4096 options:0];
 TeguQueue *queue=[device newCommandQueue];TeguCommand *command=[[queue commandBuffer] retain];
 /* A: CLEAR + three LOAD draws; B: one draw; A again: two LOAD draws. */
 for(unsigned k=0;k<4;k++)generic_batch_job(command,k,p,a,uniform,k?1:2,8*k);
 generic_batch_job(command,4,p,b,uniform,2,0);
 generic_batch_job(command,5,p,a,uniform,1,40);generic_batch_job(command,6,p,a,uniform,1,48);
 unsigned before=genericJobs;genericBatchCalls=0;
 [command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&genericJobs==before+3&&genericBatchCalls==3);
 assert(genericBatchIdvs[0]==4&&genericBatchIdvs[1]==1&&genericBatchIdvs[2]==2);
 for(unsigned c=0;c<3;c++)assert(genericBatchFragments[c]==1);
 /* A's first run continues A later; B stands alone; A's second run is clean. */
 assert((genericBatchFlags[0]&6)==TEGU_GENERIC_JOB_TARGET_CONTINUES&&!(genericBatchFlags[1]&6)&&
  (genericBatchFlags[2]&6)==TEGU_GENERIC_JOB_TARGET_CLEAN);
 /* TEGU_PERF_BATCH counters: 7 draws in 3 jobs; A and B end on another
  * target (no encoder stamps: a new encoder), the last run on the end. */
 {const struct tegu_perf_batch *pb=&command->perfBatch;
  assert(pb->draws==7&&pb->genericJobs==3&&pb->offLoad==1&&pb->offClear==2&&!pb->fullLoad&&!pb->fullClear&&!pb->skipped);
  assert(pb->breaks[TEGU_BREAK_TARGET]==2&&pb->breaks[TEGU_BREAK_END]==1&&pb->splitNew==2&&!pb->splitSame);}
 assert(genericBatchCommands[0]<4*57&&genericBatchCommands[0]<=TEGU_GENERIC_JOB_SUBMIT_COMMANDS);
 /* No TGJ2 capability: every job is TGJ1 (selector 19). */
 for(unsigned c=0;c<3;c++)assert(genericBatchSelector[c]==TEGU_GENERIC_JOB_SELECTOR);
 [command release];
 /* 0204: one CLEAR and 39 LOAD draws of one pass.  A TGJ2 kernel takes them
  * as ONE selector-20 job; without the capability they split into TGJ1 jobs
  * under the 128-command cap (160 with CAP_GENERIC_JOB_160). */
 const unsigned bigRun=40;
 const U64 capVariants[3]={0,TEGU_GPU_CAP_GENERIC_JOB_160,TEGU_GPU_CAP_GENERIC_JOB2};
 unsigned bigJobs[3]={0};
 for(unsigned v=0;v<3;v++) {
  device->capabilities=(oldCaps&~(TEGU_GPU_CAP_GENERIC_JOB2|TEGU_GPU_CAP_GENERIC_JOB_160))|
   TEGU_GPU_CAP_GENERIC_JOB|TEGU_GPU_CAP_GENERIC_JOB_BATCH|capVariants[v];
  command=[[queue commandBuffer] retain];
  for(unsigned k=0;k<bigRun;k++)generic_batch_job(command,k,p,a,uniform,k?1:2,k%56);
  before=genericJobs;unsigned before2=genericJobs2;genericBatchCalls=0;
  [command commit];[command waitUntilCompleted];
  assert([command status]==4&&![command error]);
  bigJobs[v]=genericJobs-before;
  unsigned idvs=0,limit=v==2?TEGU_GENERIC_JOB2_MAX_COMMANDS:v==1?TEGU_GENERIC_JOB_MAX_COMMANDS:TEGU_GENERIC_JOB_SUBMIT_COMMANDS;
  for(unsigned c=0;c<genericBatchCalls;c++){idvs+=genericBatchIdvs[c];assert(genericBatchFragments[c]==1&&genericBatchCommands[c]<=limit);
   assert(genericBatchSelector[c]==(v==2?TEGU_GENERIC_JOB2_SELECTOR:TEGU_GENERIC_JOB_SELECTOR));}
  if(bigJobs[v]<=8)assert(idvs==bigRun);
  /* TGJ1 runs end on the command cap, the TGJ2 run on the command's end. */
  assert(command->perfBatch.draws==bigRun&&command->perfBatch.genericJobs==bigJobs[v]&&command->perfBatch.breaks[TEGU_BREAK_END]==1&&
   (v==2||command->perfBatch.breaks[TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_COMMANDS]==bigJobs[v]-1));
  assert(v==2?(genericJobs2-before2==1&&bigJobs[v]==1&&genericBatchCommands[0]>TEGU_GENERIC_JOB_MAX_COMMANDS):genericJobs2==before2);
  [command release];
 }
 assert(bigJobs[0]>=bigJobs[1]&&bigJobs[1]>bigJobs[2]&&bigJobs[0]>=3);
 /* A TGJ2 job that reports more tiler heap than estimated raises the
  * estimate scale, so the next batches are planned smaller. */
 genericFakeHeap=UINT64_C(64)<<20;
 command=[[queue commandBuffer] retain];
 for(unsigned k=0;k<bigRun;k++)generic_batch_job(command,k,p,a,uniform,k?1:2,k%56);
 [command commit];[command waitUntilCompleted];assert([command status]==4&&![command error]);[command release];
 /* (capped at 10x; this tiny target's batch still fits ten times over) */
 assert(tegu_batch_heap_scale==1000);
 genericFakeHeap=UINT64_MAX;tegu_batch_heap_scale=100;
 printf("batch frontend: %u-draw pass in %u TGJ1 jobs (128), %u (160), %u TGJ2\n",bigRun,bigJobs[0],bigJobs[1],bigJobs[2]);
 /* Batch merge heap slack (R205): small jobs that use a few KB more than
  * their few-KB estimate inflated the process-wide scale and cut later
  * full-screen batches.  With the slack they leave the scale alone, so a
  * 30-draw full-screen pass takes fewer jobs; a real overrun still scales. */
 {TeguBuffer *screen=[device newBufferWithLength:4544u*2436u options:0];assert(screen);
  unsigned screenJobs[2]={0};
  for(unsigned gate=0;gate<2;gate++) {
   tegu_batch_heap_scale=100;tegu_batch_heap_slack=gate?TEGU_BATCH_HEAP_SLACK_BYTES:0;
   command=[[queue commandBuffer] retain];
   generic_batch_job(command,0,p,a,uniform,2,0);generic_batch_job(command,1,p,a,uniform,1,8);
   genericFakeHeap=12u<<10;[command commit];[command waitUntilCompleted];assert([command status]==4&&![command error]);[command release];
   assert(gate?tegu_batch_heap_scale==100:tegu_batch_heap_scale>100);
   command=[[queue commandBuffer] retain];
   for(unsigned k=0;k<30;k++) {
    generic_batch_job(command,k,p,screen,uniform,k?1:2,k%56);
    struct tegu_generic_draw_request *q=&tegu_command_job(command,k)->genericDrawRequest;
    q->width=1125;q->height=2436;q->row_bytes=4544;q->target_length=screen->logical;
    q->scissor[0]=q->scissor[1]=0;q->scissor[2]=1125;q->scissor[3]=2436;
   }
   genericFakeHeap=400u<<10;before=genericJobs;
   [command commit];[command waitUntilCompleted];assert([command status]==4&&![command error]);
   screenJobs[gate]=genericJobs-before;
   assert(command->perfBatch.draws==30&&command->perfBatch.genericJobs==screenJobs[gate]&&
    command->perfBatch.breaks[TEGU_BREAK_BUILDER+TEGU_BATCH_STOP_HEAP]==screenJobs[gate]-1);
   [command release];
   if(gate) {
    /* A full-screen job far over its estimate still raises the scale. */
    assert(tegu_batch_heap_scale==100);
    command=[[queue commandBuffer] retain];
    for(unsigned k=0;k<2;k++){generic_batch_job(command,k,p,screen,uniform,k?1:2,k);
     struct tegu_generic_draw_request *q=&tegu_command_job(command,k)->genericDrawRequest;
     q->width=1125;q->height=2436;q->row_bytes=4544;q->target_length=screen->logical;q->scissor[0]=q->scissor[1]=0;q->scissor[2]=1125;q->scissor[3]=2436;}
    genericFakeHeap=2u<<20;[command commit];[command waitUntilCompleted];assert([command status]==4&&![command error]);[command release];
    assert(tegu_batch_heap_scale>100);
   }
  }
  assert(screenJobs[1]<screenJobs[0]&&screenJobs[1]<=3);
  printf("heap slack: 30 full-screen draws in %u jobs after a small-job overrun (%u without the slack)\n",screenJobs[1],screenJobs[0]);
  genericFakeHeap=UINT64_MAX;tegu_batch_heap_scale=100;tegu_batch_heap_slack=0;[screen release];}
 /* Qualified bound extends calibrated small draws, and an unexpected
  * successful-job extent disables it before subsequent submissions. */
 {TeguBuffer *screen=[device newBufferWithLength:4608u*2436u options:0];assert(screen);
  unsigned counts[3]={0};tegu_generic_set_tile_pixels(1024);
  for(unsigned trial=0;trial<3;trial++) {
   tegu_batch_heap_scale=226;tegu_batch_heap_slack=65536;tegu_batch_small_heap=trial!=0;
   genericFakeHeap=trial==0?UINT64_MAX:trial==1?1856896u:0x1ff000u;
   command=[[queue commandBuffer] retain];before=genericJobs;
   for(unsigned k=0;k<12;k++) {
    generic_batch_job(command,k,p,screen,uniform,k?1:2,k);
    struct tegu_generic_draw_request *q=&tegu_command_job(command,k)->genericDrawRequest;
    q->width=1125;q->height=2436;q->row_bytes=4608;q->target_length=screen->logical;
    q->scissor[0]=q->scissor[1]=0;q->scissor[2]=1125;q->scissor[3]=2436;
   }
   [command commit];[command waitUntilCompleted];assert([command status]==4&&![command error]);
   counts[trial]=genericJobs-before;[command release];
   if(trial==1)assert(tegu_batch_small_heap&&tegu_batch_heap_scale==226);
   if(trial==2)assert(!tegu_batch_small_heap);
  }
  assert(counts[0]>1&&counts[1]==1&&counts[2]==1);
  genericFakeHeap=UINT64_MAX;tegu_batch_small_heap=0;tegu_batch_heap_scale=100;tegu_batch_heap_slack=0;
  tegu_generic_set_tile_pixels(256);[screen release];
 }
 /* A CPU blit between two draws of one target splits them. */
 command=[[queue commandBuffer] retain];
 generic_batch_job(command,0,p,a,uniform,2,0);
 {struct Dispatch *j=tegu_command_job(command,1);memset(j,0,sizeof(*j));j->blit=2;j->buffers[1]=[uniform retain];j->byteCount=4;command->count=2;}
 generic_batch_job(command,2,p,a,uniform,1,8);generic_batch_job(command,3,p,a,uniform,1,16);
 before=genericJobs;genericBatchCalls=0;
 [command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&genericJobs==before+2&&genericBatchIdvs[0]==1&&genericBatchIdvs[1]==2);
 assert(command->perfBatch.breaks[TEGU_BREAK_BLIT]==1&&command->perfBatch.breaks[TEGU_BREAK_END]==1);
 [command release];
 /* One encoder alternating two targets (the MRT RT0/RT1 jobs of each draw):
  * every job ends on the other target inside the same encoder. */
 command=[[queue commandBuffer] retain];
 for(unsigned k=0;k<4;k++){generic_batch_job(command,k,p,k&1?b:a,uniform,k<2?2:1,8*k);tegu_command_job(command,k)->perfPass=1;}
 before=genericJobs;
 [command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&genericJobs==before+4);
 assert(command->perfBatch.breaks[TEGU_BREAK_MRT]==3&&command->perfBatch.splitSame==3&&command->perfBatch.breaks[TEGU_BREAK_END]==1);
 [command release];
 /* Independent MRT-style alternation becomes two ordered target runs. */
 tegu_draw_reorder_enabled=1;
 command=[[queue commandBuffer] retain];
 for(unsigned k=0;k<8;k++){generic_batch_job(command,k,p,k&1?b:a,uniform,k<2?2:1,4*k);tegu_command_job(command,k)->perfPass=1;}
 before=genericJobs;genericBatchCalls=0;
 [command commit];[command waitUntilCompleted];
 assert([command status]==4&&![command error]&&genericJobs==before+2);
 assert(genericBatchIdvs[0]==4&&genericBatchIdvs[1]==4);
 for(unsigned k=0;k<8;k++) {
  struct Dispatch *j=tegu_command_job(command,k);
  assert(j->renderArgs[1]==(k<4?a:b)->handle);
  assert(j->genericDrawRequest.fragment_push[0]==(k<4?8*k:4+8*(k-4)));
 }
 [command release];
 /* A0, B0, A1: RAW, WAR, WAW/alias, CPU work, fetch, unknown resources
  * and encoder boundaries must keep the original order. */
 for(unsigned hazard=0;hazard<11;hazard++) {
  command=[[queue commandBuffer] retain];
  generic_batch_job(command,0,p,a,uniform,2,0);
  generic_batch_job(command,1,p,b,uniform,2,8);
  generic_batch_job(command,2,p,a,uniform,1,16);
  struct Dispatch *j0=tegu_command_job(command,0),*j1=tegu_command_job(command,1),*j2=tegu_command_job(command,2);
  j0->perfPass=j1->perfPass=j2->perfPass=1;
  switch(hazard) {
   case 0:j2->genericDrawRequest.bindings[0].memory.token=b->handle;break; /* RAW */
   case 1:j1->genericDrawRequest.bindings[0].memory.token=a->handle;break; /* WAR */
   case 2:j1->genericDrawRequest.vertex.token=a->handle;break;
   case 3:j1->genericDrawRequest.index.token=a->handle;break;
   case 4:j1->genericPreclear=1;break;
   case 5:j1->blit=1;break;
   case 6:j1->nord2CopySources[0]=a->handle;break; /* MRT cross-target snapshot */
   case 7:j1->perfPass=2;break;
   case 8:j1->genericDrawRequest.bindings[0].kind=999;break;
   case 9:j1->expandOut=[uniform retain];break;
   case 10:j2->genericDrawRequest.load_action=2;break;
  }
  assert(tegu_reorder_draws(command->jobChunks,command->count)==0);
  assert(tegu_command_job(command,1)->renderArgs[1]==b->handle);
  j1->nord2CopySources[0]=0;
  [command release];
 }
 /* Own-target framebuffer fetch (memory or tile) on B0 or A1 moves. */
 for(unsigned fetch=0;fetch<4;fetch++) {
  command=[[queue commandBuffer] retain];
  generic_batch_job(command,0,p,a,uniform,2,0);
  generic_batch_job(command,1,p,b,uniform,2,8);
  generic_batch_job(command,2,p,a,uniform,1,16);
  struct Dispatch *j1=tegu_command_job(command,1),*j2=tegu_command_job(command,2);
  tegu_command_job(command,0)->perfPass=j1->perfPass=j2->perfPass=1;
  (fetch&1?j2:j1)->genericDrawRequest.bindings[0]=(struct tegu_generic_draw_binding){.binding=0,
   .kind=fetch&2?TEGU_GENERIC_DRAW_TILE_ATTACHMENT:TEGU_GENERIC_DRAW_ATTACHMENT};
  assert(tegu_reorder_draws(command->jobChunks,command->count)==1);
  assert(tegu_command_job(command,1)->renderArgs[1]==a->handle&&tegu_command_job(command,2)->renderArgs[1]==b->handle);
  [command release];
 }
 /* TEGU_DRAW_REORDER=2: A0 | B0 | A1(Load) passes merge A1 into A0's run
  * unless a crossed job is a barrier (blit, MRT sidecar) or depends on it. */
 for(unsigned c=0;c<5;c++) {
  command=[[queue commandBuffer] retain];
  generic_batch_job(command,0,p,a,uniform,2,0);
  generic_batch_job(command,1,p,b,uniform,2,8);
  generic_batch_job(command,2,p,a,uniform,1,16);
  struct Dispatch *j1=tegu_command_job(command,1),*j2=tegu_command_job(command,2);
  tegu_command_job(command,0)->perfPass=1;j1->perfPass=2;j2->perfPass=3;
  struct tegu_render_generic_sidecar sc={.magic=TEGU_RENDER_SIDECAR_MAGIC,.version=TEGU_RENDER_SIDECAR_VERSION,.count=2};
  if(c==1)j1->blit=1;
  if(c==2){j1->compositorPlan.bytes=(uint8_t *)&sc;j1->compositorPlan.length=sizeof(sc);}
  if(c==3)j2->genericDrawRequest.bindings[0].memory.token=b->handle;
  if(c==4){j1->nord2ClearOnly=1;j1->genericDrawSnapshot=0;j1->renderTexture=0;} /* B CLEAR job: crossable */
  tegu_draw_reorder_cross=1;
  unsigned moved=tegu_reorder_draws(command->jobChunks,command->count);
  tegu_draw_reorder_cross=0;
  assert(moved==(c==0||c==4));
  assert(tegu_command_job(command,1)->renderArgs[1]==(c==0||c==4?a:b)->handle);
  if(c==2){j1->compositorPlan.bytes=0;j1->compositorPlan.length=0;}
  if(c==1)j1->blit=0;
  [command release];
 }
 /* Distinct import handles with the same canonical IOSurface ID alias. */
 {struct tegu_reorder_access x={0},y={0};x.valid=y.valid=x.pass=y.pass=1;
  x.target=(struct tegu_reorder_key){11,123,0,0};y.target=(struct tegu_reorder_key){12,123,0,0};
  assert(!tegu_reorder_independent(&x,&y));
  y.target.surface=124;assert(tegu_reorder_independent(&x,&y));
  y.reads[y.count++]=(struct tegu_reorder_key){13,123,0,0};assert(!tegu_reorder_independent(&x,&y));
 }
 /* Index expansion: A1 can cross B0 only when their arena slices are
  * disjoint; a paired draw must stay after its expansion producer. */
 for(unsigned overlap=0;overlap<2;overlap++) {
  command=[[queue commandBuffer] retain];
  TeguBuffer *arena=[device newBufferWithLength:4096 options:0];
  for(unsigned k=0;k<4;k++) {
   generic_batch_job(command,k,p,k&1?b:a,uniform,k<2?2:1,8*k);
   struct Dispatch *j=tegu_command_job(command,k);j->perfPass=1;
   NSUInteger off=overlap?0:(k/2)*256;
   j->genericDrawRequest.vertex=(struct tegu_generic_draw_memory){arena->handle,off,192};
   if(!(k&1)) {
    j->expandOut=[arena retain];j->expandIndex=[uniform retain];j->expandSource=[uniform retain];
    j->expandOutOffset=off;j->expandCount=6;j->expandStride=32;j->expandElement=2;
   }
  }
  assert(tegu_reorder_draws(command->jobChunks,4)==(overlap?0:1));
  assert(tegu_command_job(command,1)->renderArgs[1]==(overlap?b:a)->handle);
  [command release];[arena release];
 }
 {struct tegu_reorder_key a0={1,0,0,192},a1={1,0,192,192},a2={1,0,191,2};
  assert(!tegu_reorder_alias(a0,a1)&&tegu_reorder_alias(a0,a2));}
 tegu_draw_reorder_enabled=0;
 [queue release];[a release];[b release];[uniform release];[p release];
 /* The cached job images belong to this test's submissions. */
 tegu_lock(&tegu_job_images_lock);[tegu_job_images_scratch release];[tegu_job_images_load release];
 tegu_job_images_scratch=tegu_job_images_load=0;tegu_job_images_owner=0;tegu_unlock(&tegu_job_images_lock);
 device->capabilities=oldCaps;fixedUiPsoMode=oldMode;drain();
 puts("PASS generic draw batching: one job per run of same-target LOAD draws (TGJ2 when the kernel has it, TGJ1 otherwise), chain flags kept, blits and other targets split runs, heap feedback");
}
