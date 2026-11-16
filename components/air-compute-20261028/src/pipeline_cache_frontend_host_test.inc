#include <time.h>
/* Pipeline cache through the real frontend, AIR translator and Mali compiler:
 * the QuartzCore pipelines backboardd builds most (fixed_frag MRT fallback
 * with and without an RT1 job, fixed_frag RT0 fixed-UI class, luma_boost) are
 * built with the cache off, cold (miss), from memory, from disk and from disk
 * after a simulated restart, and after every cache file was corrupted.  Each
 * build's complete pipeline state (code, plans, packages, generic draw
 * shaders, flags and the serialized translator handle) must be byte
 * identical to the uncached build. */
static void tegu_pc_test_graphics(struct tegu_pc_sha256 *s,const struct tegu_mali_graphics *g) {
 tegu_pc_key_bytes(s,g->code,g->code?g->size:0);
 const uint32_t f[7]={g->code!=0,g->stage,g->registers,g->tls,g->fau_words,g->sysval_mask,g->push_bytes};
 tegu_pc_key_bytes(s,f,sizeof(f));tegu_pc_key_bytes(s,&g->preload,sizeof(g->preload));
}
static void tegu_pc_test_shader(struct tegu_pc_sha256 *s,const struct tegu_generic_draw_shader *shader,
 const struct tegu_mali_graphics *v,const struct tegu_mali_graphics *f) {
 struct tegu_generic_draw_shader copy=*shader;
 /* Code pointers must name the pipeline's own code; compare the rest. */
 assert((!copy.vertex_code||copy.vertex_code==v->code)&&(!copy.fragment_code||copy.fragment_code==f->code));
 copy.vertex_code=copy.fragment_code=0;tegu_pc_key_bytes(s,&copy,sizeof(copy));
}
static void tegu_pc_test_digest(TeguRenderPipeline *p,unsigned char out[32]) {
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);
 tegu_pc_test_graphics(&s,&p->vertexCode);tegu_pc_test_graphics(&s,&p->fragmentCode);
 tegu_pc_test_graphics(&s,&p->rt1Vertex);tegu_pc_test_graphics(&s,&p->rt1Fragment);
 tegu_pc_key_bytes(&s,&p->pair,sizeof(p->pair));tegu_pc_key_bytes(&s,&p->rt1Pair,sizeof(p->rt1Pair));
 tegu_pc_key_bytes(&s,&p->genericCompile,sizeof(p->genericCompile));tegu_pc_key_bytes(&s,&p->rt1Compile,sizeof(p->rt1Compile));
 tegu_pc_key_bytes(&s,&p->attachmentClearPlan,sizeof(p->attachmentClearPlan));
 tegu_pc_key_bytes(&s,&p->compositorResources,sizeof(p->compositorResources));
 tegu_pc_key_bytes(&s,&p->maliTexture,sizeof(p->maliTexture));tegu_pc_key_bytes(&s,&p->texture,sizeof(p->texture));
 tegu_pc_key_bytes(&s,&p->push,sizeof(p->push));tegu_pc_key_bytes(&s,&p->blend,sizeof(p->blend));
 tegu_pc_key_bytes(&s,&p->vertexFetch,sizeof(p->vertexFetch));tegu_pc_key_bytes(&s,&p->fixedUiTextureInfo,sizeof(p->fixedUiTextureInfo));
 tegu_pc_key_bytes(&s,p->genericLayouts,sizeof(p->genericLayouts));tegu_pc_key_bytes(&s,p->genericName,sizeof(p->genericName));
 tegu_pc_test_shader(&s,&p->genericShader,&p->vertexCode,&p->fragmentCode);
 tegu_pc_test_shader(&s,&p->rt1Shader,&p->rt1Vertex,&p->rt1Fragment);
 const uint64_t flags[]={p->compositorV6,p->compositorV7,p->compositorB3,p->compositorSnapshotOnly,p->attachmentClearSnapshotOnly,
  p->fixedUiSnapshotOnly,p->fixedUiTextureSnapshotOnly,p->fixedUiObserverOnly,p->genericObserverOnly,p->homeAffordanceObserverOnly,
  p->sphereObserverOnly,p->genericDraw,p->genericLayoutCount,p->genericMrt,p->rt1Draw,p->genericMainRt,p->compositorSampleClass,
  p->colorFormat,p->packageBytes,p->allocated,p->handle!=0,p->compiled!=0};
 tegu_pc_key_bytes(&s,flags,sizeof(flags));tegu_pc_key_bytes(&s,p->package,p->package?p->packageBytes:0);
 if(p->compiled) {
  TeguAIRBlob handle={0};char message[128];
  assert(!tegu_air_render_pipeline_serialize(p->compiled,&handle,message,sizeof(message))&&handle.bytes);
  tegu_pc_key_bytes(&s,handle.bytes,handle.length);tegu_air_blob_free(&handle);
 }
 tegu_pc_sha256_final(&s,out);
}
static TeguFunction *tegu_pc_test_function(TeguLibrary *base,const char *name,
 const struct TeguFixedUIConstantRow *rows,size_t count,const char *override,uint8_t value,unsigned kind) {
 TeguAIRFunctionConstant *constants=calloc(count,sizeof(*constants));assert(constants);
 for(size_t i=0;i<count;i++) {
  constants[i].kind=1;constants[i].type=rows[i].type;constants[i].size=rows[i].size;
  constants[i].name=(const uint8_t *)rows[i].name;constants[i].name_len=strlen(rows[i].name);
  memcpy(constants[i].data,rows[i].data,rows[i].size);
  if(override&&!strcmp(rows[i].name,override))constants[i].data[0]=value;
 }
 char message[512]={0};TeguAIRLibrary *specialized=tegu_air_library_specialize(base->library,
  (const uint8_t *)name,strlen(name),constants,count,message,sizeof(message));assert(specialized);
 TeguLibrary *snapshot=[[TeguLibrary alloc] init];assert(snapshot);snapshot->owner=[base->owner retain];
 snapshot->library=specialized;memcpy(snapshot->sourceSHA256,base->sourceSHA256,sizeof(snapshot->sourceSHA256));
 assert(base->cacheIdentityValid);
 tegu_pc_specialized_identity(base->cacheIdentity,name,strlen(name),constants,count,snapshot->cacheIdentity);
 snapshot->cacheIdentityValid=1;free(constants);
 TeguFunction *function=[[TeguFunction alloc] init];assert(function);function->library=snapshot;
 function->functionName=[[NSString stringWithUTF8String:name] retain];function->kind=kind;return function;
}
static RenderDescriptor *tegu_pc_test_descriptor(TeguFunction *vertex,TeguFunction *fragment,unsigned rt1Mask,int rt1,signed char blend) {
 RenderDescriptor *d=[[RenderDescriptor alloc] init];d->vertex=vertex;d->fragment=fragment;
 d->colors=[[RenderColors alloc] init];for(unsigned i=0;i<8;i++)d->colors->colors[i]=[[RenderColor alloc] init];
 d->colors->colors[0]->format=80;d->colors->colors[0]->mask=15;
 if(blend){RenderColor *c=d->colors->colors[0];c->blend=1;c->rgbSrc=c->alphaSrc=1;c->rgbDst=c->alphaDst=5;}
 if(rt1){d->colors->colors[1]->format=80;d->colors->colors[1]->mask=rt1Mask;}
 d->samples=1;d->raster=1;d->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;layout->buffers=[[RenderLayout alloc] init];
 const unsigned formats[4]={31,29,29,27},offsets[4]={0,16,24,32};
 for(unsigned i=0;i<4;i++){layout->entries[i]=[[RenderAttribute alloc] init];layout->entries[i]->value=formats[i];
  layout->entries[i]->byteOffset=offsets[i];layout->entries[i]->bufferSlot=1;}
 layout->buffers->entries[1]=[[RenderAttribute alloc] init];layout->buffers->entries[1]->layoutStride=48;
 layout->buffers->entries[1]->step=1;layout->buffers->entries[1]->rate=1;
 return d;
}
static double tegu_pc_test_build_ms;
static void tegu_pc_test_build(TeguComputeDevice *device,RenderDescriptor *d,unsigned char digest[32]) {
 struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
 id error=0;TeguRenderPipeline *p=[device newRenderPipelineStateWithDescriptor:d error:&error];
 clock_gettime(CLOCK_MONOTONIC,&b);tegu_pc_test_build_ms+=(b.tv_sec-a.tv_sec)*1e3+(b.tv_nsec-a.tv_nsec)/1e6;
 /* The RT1-written case is refused since the true-MRT policy (written-output
  * mask 1 vs bound mask 3): a refusal has no pipeline state to digest, and it
  * must stay identical (refused) with and without the cache. */
 if(!p){memset(digest,0xee,32);return;}
 assert(!error);tegu_pc_test_digest(p,digest);[p release];
}
static void tegu_pc_test_corrupt_dir(const char *dir) {
 unsigned corrupted=0;
 for(unsigned kind=1;kind<TEGU_PC_KINDS;kind++)for(unsigned slot=0;slot<tegu_render_cache_kinds[kind].slots;slot++) {
  char path[700];snprintf(path,sizeof(path),"%s/com.tegu.metal.pipelines/k%u-%03u.tpc",dir,kind,slot);
  FILE *f=fopen(path,"r+b");if(!f)continue;
  /* Flip a payload byte past the header: the checksum must catch it. */
  assert(!fseek(f,TEGU_PC_HEADER_BYTES+9,SEEK_SET));int c=fgetc(f);assert(c!=EOF);
  assert(!fseek(f,TEGU_PC_HEADER_BYTES+9,SEEK_SET));fputc(c^0x5a,f);fclose(f);corrupted++;
 }
 assert(corrupted>=4);
}
static void test_pipeline_cache_frontend(TeguComputeDevice *device,const char *path) {
 FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);
 assert(lib&&!error&&lib->cacheIdentityValid);
 U64 oldCaps=device->capabilities;device->capabilities=oldCaps|TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_GENERIC_JOB|256;
 unsigned oldMode=fixedUiPsoMode;fixedUiPsoMode=1;
 const char *packTest=getenv("TEGU_PIPELINE_PACK_TEST"),*packBad=getenv("TEGU_PIPELINE_PACK_BAD");
 const size_t vsCount=sizeof(tegu_fixed_ui_vs_constants)/sizeof(tegu_fixed_ui_vs_constants[0]);
 const size_t fsCount=sizeof(tegu_fixed_ui_fs_constants)/sizeof(tegu_fixed_ui_fs_constants[0]);
 enum {CASES=4};RenderDescriptor *cases[CASES];
 /* 0: fixed_frag MRT fallback, RT1 inactive (the R196 backboardd majority). */
 cases[0]=tegu_pc_test_descriptor(
  tegu_pc_test_function(lib,"fixed_vert_lph_spc",tegu_fixed_ui_vs_constants,vsCount,0,0,1),
  tegu_pc_test_function(lib,"fixed_frag_lph_cph",tegu_fixed_ui_fs_constants,fsCount,"fc_attachment_count",2,2),0,1,1);
 /* 1: the same with RT1 written: rt0 + rt1 generic compiles. */
 cases[1]=tegu_pc_test_descriptor(
  tegu_pc_test_function(lib,"fixed_vert_lph_spc",tegu_fixed_ui_vs_constants,vsCount,0,0,1),
  tegu_pc_test_function(lib,"fixed_frag_lph_cph",tegu_fixed_ui_fs_constants,fsCount,"fc_attachment_count",2,2),15,1,0);
 /* 2: RT0-only fixed UI class (fixed-UI gates plus generic draw). */
 cases[2]=tegu_pc_test_descriptor(
  tegu_pc_test_function(lib,"fixed_vert_lph_spc",tegu_fixed_ui_vs_constants,vsCount,0,0,1),
  tegu_pc_test_function(lib,"fixed_frag_lph_cph",tegu_fixed_ui_fs_constants,fsCount,0,0,2),0,0,1);
 /* 3: luma_boost (unspecialized library functions). */
 cases[3]=tegu_pc_test_descriptor([lib newFunctionWithName:[NSString stringWithUTF8String:"std_vert1_lph"]],
  [lib newFunctionWithName:[NSString stringWithUTF8String:"luma_boost_lph"]],0,1,0);
 char root[512];const char *env=getenv("TEGU_PIPELINE_CACHE_DIR");assert(env&&strlen(env)<400);
 snprintf(root,sizeof(root),"%s/frontend-test",env);mkdir(root,0700);
 unsigned char reference[CASES][32],digest[32];
 tegu_render_cache_test_reset(0,0,0);
 double uncached[CASES];
 for(unsigned c=0;c<CASES;c++){tegu_pc_test_build_ms=0;tegu_pc_test_build(device,cases[c],reference[c]);uncached[c]=tegu_pc_test_build_ms;}
 fprintf(stderr,"pipeline cache uncached build ms: %.1f %.1f %.1f %.1f\n",uncached[0],uncached[1],uncached[2],uncached[3]);
 for(unsigned c=1;c<CASES;c++)assert(memcmp(reference[c],reference[0],32)); /* distinct pipelines */
 struct tegu_pc_stats st;
 if(packTest) {
  /* Bundle pack made by make_pipeline_pack.py from an earlier run's cache
   * files: memory + pack only, then a pack with one corrupted payload. */
  const char *packs[2]={packTest,packBad};
  for(unsigned k=0;k<2&&packs[k];k++) {
   tegu_render_cache_test_reset(0,1,packs[k]);assert(tegu_render_cache.pack_count>=CASES+1);
   tegu_pc_test_build_ms=0;
   for(unsigned c=0;c<CASES;c++){tegu_pc_test_build(device,cases[c],digest);assert(!memcmp(digest,reference[c],32));}
   tegu_pc_snapshot(&tegu_render_cache,&st);
   fprintf(stderr,"pipeline cache %s pack: %.1f ms for %u builds pack=%llu memory=%llu miss=%llu pack_rejects=%llu\n",k?"corrupt":"good",
    tegu_pc_test_build_ms,CASES,(unsigned long long)st.pack_hits,(unsigned long long)st.memory_hits,(unsigned long long)st.misses,
    (unsigned long long)st.pack_rejects);
   if(!k)assert(st.pack_hits>=CASES+1&&!st.misses&&!st.disk_hits);
   else assert(st.pack_rejects>=1&&st.misses>=1);
  }
  tegu_render_cache_test_reset(0,0,0);
  for(unsigned c=0;c<CASES;c++)[cases[c] release];
  [lib release];device->capabilities=oldCaps;fixedUiPsoMode=oldMode;
  puts("PASS pipeline cache pack: bundle pack serves byte-identical pipelines; a corrupted pack entry falls back to compiling");
  return;
 }
 tegu_render_cache_test_reset(root,1,0);
 const char *phase[5]={"cold","memory","disk","restart","corrupt"};
 for(unsigned round=0;round<5;round++) {
  if(round==2)tegu_pc_clear_memory(&tegu_render_cache);
  if(round==3)tegu_render_cache_test_reset(root,1,0);
  if(round==4){tegu_pc_test_corrupt_dir(root);tegu_render_cache_test_reset(root,1,0);}
  struct tegu_pc_stats before;tegu_pc_snapshot(&tegu_render_cache,&before);tegu_pc_test_build_ms=0;
  for(unsigned c=0;c<CASES;c++) {
   tegu_pc_test_build(device,cases[c],digest);
   if(memcmp(digest,reference[c],32)){fprintf(stderr,"pipeline cache %s case %u differs\n",phase[round],c);abort();}
  }
  tegu_pc_snapshot(&tegu_render_cache,&st);
  uint64_t memory=st.memory_hits-before.memory_hits,disk=st.disk_hits-before.disk_hits,miss=st.misses-before.misses;
  fprintf(stderr,"pipeline cache %s: %.1f ms for %u builds memory=%llu disk=%llu miss=%llu rejects=%llu disk_bytes=%llu\n",phase[round],
   tegu_pc_test_build_ms,CASES,(unsigned long long)memory,(unsigned long long)disk,(unsigned long long)miss,(unsigned long long)st.disk_rejects,
   (unsigned long long)st.disk_bytes_written);
  /* At least one AIR and one Mali lookup per case; identical Mali inputs
   * across cases may hit memory within a round. */
  assert(memory+disk+miss>=CASES*2);
  if(round==0)assert(miss>=CASES+1&&!disk);
  /* Tile variants add enough code to evict a shared entry rebuilt earlier in
   * this round. Reading that newly repaired disk entry is legitimate. Every
   * result still has to match the uncached digest above. */
  if(round==4)assert(miss>=CASES+1);
  if(round==1)assert(!disk&&!miss);
  if(round==2||round==3)assert(disk>=CASES+1&&!miss);
  if(round==4)assert(st.disk_rejects>=4);
 }
 /* Memory-only and disabled modes give the same pipelines. */
 tegu_render_cache_test_reset(0,1,0);
 for(unsigned round=0;round<2;round++)for(unsigned c=0;c<CASES;c++){tegu_pc_test_build(device,cases[c],digest);assert(!memcmp(digest,reference[c],32));}
 tegu_pc_snapshot(&tegu_render_cache,&st);assert(st.memory_hits>=CASES*2&&!st.disk_hits&&!st.disk_writes);
 tegu_render_cache_test_reset(0,0,0);
 for(unsigned c=0;c<CASES;c++)[cases[c] release];
 [lib release];device->capabilities=oldCaps;fixedUiPsoMode=oldMode;
 /* Leave the process-wide cache as the suite configured it. */
 tegu_render_cache_test_reset(env,1,0);
 puts("PASS pipeline cache: fixed_frag MRT/RT1/RT0 and luma_boost pipelines byte-identical uncached, cold, memory, disk, restart and after corrupting every cache file");
}
