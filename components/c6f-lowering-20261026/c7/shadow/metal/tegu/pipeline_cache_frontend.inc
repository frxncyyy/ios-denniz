/* Render pipeline build cache.  Pipeline builds spend their CPU time in two
 * pure functions: the AIR->SPIR-V translation (tegu_air_render_pipeline_create*)
 * and the Mali compile (tegu_mali_compile_*_pair*).  Both are cached by
 * content: the AIR key covers the build (TEGU_PIPELINE_CACHE_BUILD: bridge and
 * Mali archive hashes), the variant entry, both library identities (metallib
 * SHA-256 plus specialization constants), function names, colour format,
 * attachments, blends, vertex descriptor, translator profile bytes and the
 * caller's message capacity; the Mali key covers the build, entry, output
 * struct sizes, render target and both SPIR-V modules.  A hit returns the same
 * bytes a compile returns (host-tested), so every gate, package, import and
 * fallback in teguBuildRenderPipeline runs unchanged.  Refusals are cached
 * too (their message is the payload): fallback chains retry them per build.
 *
 * Memory always; disk when the bundle carries a build id and a writable
 * directory exists: $TEGU_PIPELINE_CACHE_DIR, $HOME/Library/Caches,
 * /private/var/mobile/Library/Caches, $TMPDIR, /private/var/tmp (first
 * usable), subdirectory com.tegu.metal.pipelines.  TEGU_PIPELINE_CACHE=0
 * turns the cache off, =memory keeps it in memory only. */
extern char *getenv(const char *);
extern int getpid(void);
extern unsigned getuid(void);
extern unsigned geteuid(void);
extern int setenv(const char *,const char *,int);
#ifndef TEGU_PIPELINE_CACHE_BUILD
#define TEGU_PIPELINE_CACHE_BUILD ""
#endif
/* Host replay (gpu/tools/build_pipeline_pack.py) makes a pack for the build id
 * of a device bundle with a frontend compiled once: the host test reads it from
 * TEGU_PIPELINE_CACHE_BUILD_OVERRIDE.  The device build never reads the
 * environment for this. */
#ifdef TEGU_FRONTEND_HOST
static const char *tegu_pc_build_id(void) {
 const char *o=getenv("TEGU_PIPELINE_CACHE_BUILD_OVERRIDE");return o&&o[0]?o:TEGU_PIPELINE_CACHE_BUILD;
}
#else
#define tegu_pc_build_id() TEGU_PIPELINE_CACHE_BUILD
#endif
/* The bundled Mesa is a debugoptimized build: without NDEBUG it validates the
 * whole NIR shader after every pass, about half of each Mali compile.  The
 * device skips that (as release Mesa does); host tests keep it. */
#if !defined(TEGU_FRONTEND_HOST) && !defined(TEGU_NIR_VALIDATE)
__attribute__((constructor)) static void tegu_nir_skip_validation(void) {
 if(!getenv("NIR_DEBUG"))setenv("NIR_DEBUG","novalidate",0);
}
#endif
static struct tegu_pc tegu_render_cache;
static unsigned tegu_render_cache_state; /* 0 untried, 1 on, 2 off */
static unsigned tegu_render_cache_init_lock;
static const struct tegu_pc_kind_config tegu_render_cache_kinds[TEGU_PC_KINDS]={
 {0,0,0},
 /* AIR: ~0.65 MB per QuartzCore pipeline (its fragment SPIR-V). Slots are
  * the first 16 key bits: R199's 192 slots collided for ~100 pipelines. */
 {24u<<20,2u<<20,65536},
 /* Mali: 5-40 KB per pair, but QuartzCore blur/material fragment binaries
  * are 730-810 KB (R199: 16 of 35), which a 256 KiB cap kept off disk. */
 {32u<<20,2u<<20,65536},
};
static int tegu_render_cache_dir_candidate(char *out,size_t size,const char *base,const char *suffix) {
 if(!base||base[0]!='/')return 0;
 int n=snprintf(out,size,"%s%s/com.tegu.metal.pipelines",base,suffix);
 return n>0&&(size_t)n<size&&tegu_pc_dir_usable(out);
}
/* The bundle's own pack: <bundle>/PipelinePack.tpcpack (or $TEGU_PIPELINE_PACK). */
static int tegu_render_cache_pack_path(char *out,size_t size) {
 const char *env=getenv("TEGU_PIPELINE_PACK");
 if(env&&env[0]=='/'){int n=snprintf(out,size,"%s",env);return n>0&&(size_t)n<size;}
#if defined(__APPLE__) && defined(TEGU_GRAPHICS_BRIDGE)
 struct tegu_dl_info self;
 if(tegu_dladdr((const void *)&tegu_bundle_base_marker,&self)&&self.fname) {
  size_t n=0,slash=0;while(self.fname[n]){if(self.fname[n]=='/')slash=n;n++;}
  if(slash&&slash+32<size){memcpy(out,self.fname,slash);snprintf(out+slash,size-slash,"/PipelinePack.tpcpack");return 1;}
 }
#endif
 return 0;
}
static void tegu_render_cache_open(const char *forcedDir,int enable,int disk,const char *forcedPack) {
 char dir[512],diag[1400];dir[0]=0;diag[0]=0;
 const char *build=tegu_pc_build_id();
 if(enable&&disk&&*build) {
  if(forcedDir) {if(!tegu_render_cache_dir_candidate(dir,sizeof(dir),forcedDir,""))dir[0]=0;}
  else {
   /* The installer-created shared directory comes first: it belongs to
    * mobile (every UI process runs as mobile, the probe as root) and is 0755,
    * so both can use it.  Before it existed the first candidate was a
    * root-owned 0700 directory left by the probe, unusable to mobile, and the
    * UI processes fell through to /private/var/tmp.  Every candidate's
    * failure (errno of mkdir/open/write) goes into the per-process log line. */
   const char *env=getenv("TEGU_PIPELINE_CACHE_DIR");
   if(env&&env[0]=='/'&&(snprintf(dir,sizeof(dir),"%s",env),tegu_pc_dir_usable(dir))) {
    snprintf(diag,sizeof(diag),"%s=env",dir);
   } else {
    dir[0]=0;
    struct tegu_pc_candidate list[4]={{"/private/var/mobile","/Library/Caches"},{getenv("HOME"),"/Library/Caches"},
     {getenv("TMPDIR"),""},{"/private/var/tmp",""}};
#ifdef __APPLE__
    tegu_pc_choose_dir(list,4,"com.tegu.metal.pipelines",dir,sizeof(dir),diag,sizeof(diag));
#else
    (void)list;
#endif
   }
  }
 }
 tegu_pc_close(&tegu_render_cache);
 tegu_pc_init(&tegu_render_cache,dir,build,tegu_render_cache_kinds);
 /* A pack only ever serves its own build (build tag in its header). */
 char pack[600];int packState=0;
 if(enable&&*build&&(forcedPack?snprintf(pack,sizeof(pack),"%s",forcedPack)>0:tegu_render_cache_pack_path(pack,sizeof(pack))))
  {int rc=tegu_pc_attach_pack(&tegu_render_cache,pack);packState=!rc?1:rc==-2?-2:-1;}
 char line[800];int n=snprintf(line,sizeof(line),"TEGU_PIPELINE_CACHE enabled=%d disk=%s build=%.28s pack=%s entries=%u\n",
  enable,dir[0]?dir:"-",*build?build:"-",packState>0?"loaded":packState==-2?"absent":packState<0?"refused":"-",tegu_render_cache.pack_count);
 if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
 /* Why each directory candidate was taken or skipped (errno of mkdir, probe
  * open, probe write), per process; pid/uid tell the processes apart. */
 if(diag[0]) {
  char dl[1600];int m=snprintf(dl,sizeof(dl),"TEGU_PIPELINE_CACHE_DIRS pid=%d uid=%d euid=%d %s\n",getpid(),(int)getuid(),(int)geteuid(),diag);
  if(m>0)write(2,dl,(size_t)m<sizeof(dl)?(size_t)m:sizeof(dl)-1);
 }
}
/* Cumulative disk traffic of this process for the TEGU_PERF_PSO window: reads
 * attempted/absent/rejected, writes done/failed (last errno), pack state. */
static int tegu_render_cache_disk_line(char *out,size_t size) {
 if(__atomic_load_n(&tegu_render_cache_state,__ATOMIC_ACQUIRE)!=1)return 0;
 struct tegu_pc_stats st;tegu_pc_snapshot(&tegu_render_cache,&st);
 return snprintf(out,size,"TEGU_PERF_DISK state=%d reads=%llu absent=%llu rejects=%llu hits=%llu writes=%llu write_fail=%llu errno=%llu rbytes=%llu wbytes=%llu\n",
  (int)__atomic_load_n(&tegu_render_cache.disk,__ATOMIC_RELAXED),(unsigned long long)st.disk_reads,(unsigned long long)st.disk_read_absent,
  (unsigned long long)st.disk_rejects,(unsigned long long)st.disk_hits,(unsigned long long)st.disk_writes,
  (unsigned long long)st.disk_write_failures,(unsigned long long)st.last_write_errno,
  (unsigned long long)st.disk_bytes_read,(unsigned long long)st.disk_bytes_written);
}
static struct tegu_pc *tegu_render_cache_get(void) {
 unsigned state=__atomic_load_n(&tegu_render_cache_state,__ATOMIC_ACQUIRE);
 if(!state) {
  tegu_lock(&tegu_render_cache_init_lock);
  state=tegu_render_cache_state;
  if(!state) {
   const char *mode=getenv("TEGU_PIPELINE_CACHE");
   int off=mode&&mode[0]=='0'&&!mode[1],memoryOnly=mode&&!__builtin_strcmp(mode,"memory");
   tegu_render_cache_open(0,!off,!memoryOnly,0);
   state=off?2:1;__atomic_store_n(&tegu_render_cache_state,state,__ATOMIC_RELEASE);
  }
  tegu_unlock(&tegu_render_cache_init_lock);
 }
 return state==1?&tegu_render_cache:0;
}
#ifdef TEGU_FRONTEND_HOST
/* Tests: reopen with a directory (NULL = memory only) or turn it off. */
static void tegu_render_cache_test_reset(const char *dir,int enable,const char *pack) {
 tegu_render_cache_open(dir,enable,dir!=0,pack);
 __atomic_store_n(&tegu_render_cache_state,enable?1u:2u,__ATOMIC_RELEASE);
}
#endif
static void tegu_render_cache_count(int hit) {
 __atomic_fetch_add(hit==1?&tegu_perf_pc_memory:hit==2?&tegu_perf_pc_disk:hit==3?&tegu_perf_pc_pack:&tegu_perf_pc_miss,1,__ATOMIC_RELAXED);
}
/* Every miss is recorded so one boot names the pipelines a pack needs and
 * shows their cost: a short console line (first 256 per process) and a full
 * replay record in <cache dir>/misses.txt (bounded 4 MB). */
static unsigned tegu_render_cache_miss_lines;
static void tegu_pc_hex(const unsigned char *p,size_t n,char *out) {
 static const char hex[]="0123456789abcdef";for(size_t i=0;i<n;i++){out[2*i]=hex[p[i]>>4];out[2*i+1]=hex[p[i]&15];}out[2*n]=0;
}
static void tegu_render_cache_miss_line(const char *text) {
 if(__atomic_fetch_add(&tegu_render_cache_miss_lines,1,__ATOMIC_RELAXED)>=256)return;
 size_t n=0;while(text[n])n++;write(2,text,n);
}

/* ---- AIR translation ---- */
enum {TEGU_PC_AIR_VERTEX=1,TEGU_PC_AIR_BLEND=2,TEGU_PC_AIR_ATTACHMENTS=3,TEGU_PC_AIR_ATTACHMENT_BLENDS=4};
static TeguAIRRenderPipeline *tegu_pc_air_compile(unsigned variant,TeguFunction *vertex,const char *vn,NSUInteger vl,
 TeguFunction *fragment,const char *fn,NSUInteger fl,uint32_t format,const TeguAIRRenderColorAttachment *targets,NSUInteger targetCount,
 const TeguAIRRenderBlend *blends,const TeguAIRVertexAttribute *attributes,NSUInteger attributeCount,
 const TeguAIRVertexLayout *layouts,NSUInteger layoutCount,const unsigned char *profile,NSUInteger profileBytes,
 TeguAIRBlob *vs,TeguAIRBlob *fs,TeguAIRBlob *meta,char *message,NSUInteger size) {
 const TeguAIRLibrary *vlib=vertex->library->library,*flib=fragment->library->library;
 switch(variant) {
 case TEGU_PC_AIR_BLEND:
  return tegu_air_render_pipeline_create_with_blend(vlib,(const uint8_t *)vn,vl,flib,(const uint8_t *)fn,fl,format,
   attributes,attributeCount,layouts,layoutCount,blends,profile,profileBytes,vs,fs,meta,message,size);
 case TEGU_PC_AIR_ATTACHMENTS:
  return tegu_air_render_pipeline_create_with_attachments(vlib,(const uint8_t *)vn,vl,flib,(const uint8_t *)fn,fl,
   targets,targetCount,attributes,attributeCount,layouts,layoutCount,profile,profileBytes,vs,fs,meta,message,size);
 case TEGU_PC_AIR_ATTACHMENT_BLENDS:
  return tegu_air_render_pipeline_create_with_attachment_blends(vlib,(const uint8_t *)vn,vl,flib,(const uint8_t *)fn,fl,
   targets,targetCount,blends,attributes,attributeCount,layouts,layoutCount,profile,profileBytes,vs,fs,meta,message,size);
 default:
  return tegu_air_render_pipeline_create_with_vertex_descriptor(vlib,(const uint8_t *)vn,vl,flib,(const uint8_t *)fn,fl,format,
   attributes,attributeCount,layouts,layoutCount,profile,profileBytes,vs,fs,meta,message,size);
 }
}
static void tegu_pc_key_blend(struct tegu_pc_sha256 *s,const TeguAIRRenderBlend *b) {
 const uint32_t v[9]={b->version,b->enabled,b->rgb_op,b->rgb_src,b->rgb_dst,b->alpha_op,b->alpha_src,b->alpha_dst,b->write_mask};
 for(unsigned i=0;i<9;i++)tegu_pc_key_u32(s,v[i]);
}
/* Replay record of one AIR pipeline: every input the key covers, in text (the
 * "TPC_MISS v1 air" grammar make_pipeline_pack's host replay,
 * gpu/metal/tegu/pipeline_corpus.py, reads).  *at is the length written; the
 * caller appends " ms=.. status=..\n" or nothing.  NULL when out of memory. */
static char *tegu_pc_air_record(size_t *at_out,const char *keyHex,unsigned variant,uint32_t format,TeguFunction *vertex,
 const char *vn,TeguFunction *fragment,const char *fn,const TeguAIRRenderColorAttachment *targets,NSUInteger targetCount,
 const TeguAIRRenderBlend *blends,const TeguAIRVertexAttribute *attributes,NSUInteger attributeCount,
 const TeguAIRVertexLayout *layouts,NSUInteger layoutCount,const unsigned char *profile,NSUInteger profileBytes,NSUInteger size) {
 const char *vr=vertex->library->cacheReplay,*fr=fragment->library->cacheReplay;
 size_t cap=4096+(vr?__builtin_strlen(vr):0)+(fr?__builtin_strlen(fr):0);char *record=malloc(cap);
 if(!record)return 0;
 unsigned char profileSum[32];struct tegu_pc_sha256 ps;tegu_pc_sha256_init(&ps);tegu_pc_sha256_update(&ps,profile,profileBytes);
 tegu_pc_sha256_final(&ps,profileSum);char profileHex[65];tegu_pc_hex(profileSum,32,profileHex);
 size_t at=(size_t)snprintf(record,cap,"TPC_MISS v1 air key=%s variant=%u format=%u vertex=%s vlib=%s vspec=%s fragment=%s flib=%s fspec=%s targets=",
  keyHex,variant,format,vn,vertex->library->sourceSHA256,vr?vr:"-",fn,fragment->library->sourceSHA256,fr?fr:"-");
 for(NSUInteger i=0;targets&&i<targetCount&&at<cap;i++)at+=(size_t)snprintf(record+at,cap-at,"%u/%u,",targets[i].format,targets[i].write_mask);
 NSUInteger blendCount=!blends?0:variant==TEGU_PC_AIR_BLEND?1:variant==TEGU_PC_AIR_ATTACHMENT_BLENDS?targetCount:0;
 if(at<cap)at+=(size_t)snprintf(record+at,cap-at," blends=");
 for(NSUInteger i=0;i<blendCount&&at<cap;i++){const TeguAIRRenderBlend *b=&blends[i];
  at+=(size_t)snprintf(record+at,cap-at,"%u/%u/%u/%u/%u/%u/%u/%u/%u,",b->version,b->enabled,b->rgb_op,b->rgb_src,b->rgb_dst,
   b->alpha_op,b->alpha_src,b->alpha_dst,b->write_mask);}
 if(at<cap)at+=(size_t)snprintf(record+at,cap-at," attrs=");
 for(NSUInteger i=0;attributes&&i<attributeCount&&at<cap;i++)at+=(size_t)snprintf(record+at,cap-at,"%u/%u/%u/%u,",
  attributes[i].index,attributes[i].format,attributes[i].offset,attributes[i].buffer_index);
 if(at<cap)at+=(size_t)snprintf(record+at,cap-at," layouts=");
 for(NSUInteger i=0;layouts&&i<layoutCount&&at<cap;i++)at+=(size_t)snprintf(record+at,cap-at,"%u/%u/%u/%u,",
  layouts[i].index,layouts[i].stride,layouts[i].step_function,layouts[i].step_rate);
 if(at<cap)at+=(size_t)snprintf(record+at,cap-at," profile=%s msgcap=%lu",profileHex,(unsigned long)size);
 if(at>=cap){free(record);return 0;}
 *at_out=at;return record;
}
/* Console copy of a replay record, so one boot's log names every pipeline (with
 * its function-constant values) without pulling misses.txt from the device.
 * Long lines do not survive the USB log ring (cut at ~256 bytes): the record goes out in
 * numbered chunks "TEGU_PSO_REC k=<key16> src=<miss|mem|disk|pack> i=<n>/<N> <text>"
 * (spaces of the record written as '#'), rejoined by (process, key, i).  Each distinct key is emitted once per process
 * (hits included: a warm boot still lists the whole pipeline set), bounded to
 * TEGU_PC_REC_MAX records per process. */
#define TEGU_PC_REC_MAX 640u
/* The USB console cuts a line at ~256 bytes including its "|process| " prefix (c6k: 200-character
 * chunks lost 5-7 characters each): prefix <=24 + header <=50 + chunk must stay well under that. */
#define TEGU_PC_REC_CHUNK 120u
static uint64_t tegu_pc_rec_seen[TEGU_PC_REC_MAX];static unsigned tegu_pc_rec_count;
static int tegu_pc_rec_claim(const unsigned char key[32]) {
 uint64_t id=0;for(unsigned i=0;i<8;i++)id=(id<<8)|key[i];
 unsigned n=__atomic_load_n(&tegu_pc_rec_count,__ATOMIC_ACQUIRE);if(n>TEGU_PC_REC_MAX)n=TEGU_PC_REC_MAX;
 for(unsigned i=0;i<n;i++)if(__atomic_load_n(&tegu_pc_rec_seen[i],__ATOMIC_RELAXED)==id)return 0;
 unsigned slot=__atomic_fetch_add(&tegu_pc_rec_count,1,__ATOMIC_ACQ_REL);
 if(slot>=TEGU_PC_REC_MAX)return 0;
 __atomic_store_n(&tegu_pc_rec_seen[slot],id,__ATOMIC_RELEASE);return 1;
}
static void tegu_pc_rec_console(const char *keyHex,const char *src,const char *text,size_t bytes) {
 unsigned chunks=(unsigned)((bytes+TEGU_PC_REC_CHUNK-1)/TEGU_PC_REC_CHUNK);
 for(unsigned i=0;i<chunks;i++) {
  size_t from=(size_t)i*TEGU_PC_REC_CHUNK,len=bytes-from<TEGU_PC_REC_CHUNK?bytes-from:TEGU_PC_REC_CHUNK;
  /* Spaces inside the record go out as '#' (no record field contains one), so a
   * chunk ending in a space cannot lose it to trailing-whitespace trimming. */
  char line[TEGU_PC_REC_CHUNK+96],body[TEGU_PC_REC_CHUNK+1];
  for(size_t j=0;j<len;j++)body[j]=text[from+j]==' '?'#':text[from+j];
  body[len]=0;
  int n=snprintf(line,sizeof(line),"TEGU_PSO_REC k=%.16s src=%s i=%u/%u %s\n",keyHex,src,i+1,chunks,body);
  if(n>0&&(size_t)n<sizeof(line))write(2,line,(size_t)n);
 }
}
/* Console record of a cache hit (first sight of the key in this process). */
#define TEGU_PC_REC_EMIT_HIT(HIT) do { \
 if(tegu_pc_rec_claim(key)) { \
  char kh[65];tegu_pc_hex(key,32,kh);size_t rn=0; \
  char *r=tegu_pc_air_record(&rn,kh,variant,format,vertex,vn,fragment,fn,targets,targetCount,blends,attributes,attributeCount, \
   layouts,layoutCount,profile,profileBytes,size); \
  if(r){tegu_pc_rec_console(kh,(HIT)==1?"mem":(HIT)==2?"disk":"pack",r,rn);free(r);} \
 } \
} while(0)
/* Same contract as the tegu_air_render_pipeline_create* entry the variant
 * names; the names are the functions' own. */
/* Translator variants (texture-opus): the AIR cache key of each pipeline
 * handle this process created or loaded, so a pipeline that compiled new
 * translator variants is stored again (caches and packs then carry them),
 * and the creation inputs given back to a cache-loaded handle
 * (tegu_air_render_pipeline_attach_source) so it can compile variants it has
 * not cached. */
struct tegu_pc_render_key {const TeguAIRRenderPipeline *handle;unsigned char key[32];uint32_t variants;};
static struct tegu_pc_render_key tegu_pc_render_keys[512];static unsigned tegu_pc_render_key_next,tegu_pc_render_key_lock;
static void tegu_pc_render_key_note(const TeguAIRRenderPipeline *handle,const unsigned char key[32]) {
 if(!handle)return;
 tegu_lock(&tegu_pc_render_key_lock);
 /* A released handle's address can be reused by a later pipeline: drop any
  * older record of this address so a variant store never lands under a
  * previous pipeline's key. */
 for(unsigned i=0;i<512;i++)if(tegu_pc_render_keys[i].handle==handle)tegu_pc_render_keys[i].handle=0;
 struct tegu_pc_render_key *e=&tegu_pc_render_keys[tegu_pc_render_key_next++%512u];
 e->handle=handle;memcpy(e->key,key,32);e->variants=tegu_air_render_pipeline_variant_count(handle);
 tegu_unlock(&tegu_pc_render_key_lock);
}
/* After a translator variant compiled: store the serialized pipeline again
 * under its creation key when its variant count grew. */
static void tegu_pc_render_variants_store(const TeguAIRRenderPipeline *handle) {
 struct tegu_pc *cache=tegu_render_cache_get();
 if(!cache||!handle)return;
 unsigned char key[32];int found=0;uint32_t count=tegu_air_render_pipeline_variant_count(handle);
 tegu_lock(&tegu_pc_render_key_lock);
 for(unsigned i=0;i<512;i++)if(tegu_pc_render_keys[i].handle==handle&&tegu_pc_render_keys[i].variants<count){
  memcpy(key,tegu_pc_render_keys[i].key,32);tegu_pc_render_keys[i].variants=count;found=1;break;}
 tegu_unlock(&tegu_pc_render_key_lock);
 if(!found)return;
 TeguAIRBlob serialized={0};char serializeError[128];
 if(!tegu_air_render_pipeline_serialize(handle,&serialized,serializeError,sizeof(serializeError))&&serialized.bytes)
  tegu_pc_store(cache,TEGU_PC_KIND_AIR,key,serialized.bytes,serialized.length);
 tegu_air_blob_free(&serialized);
}
static void tegu_pc_render_attach_source(const TeguAIRRenderPipeline *p,unsigned variant,TeguFunction *vertex,const char *vn,NSUInteger vl,
 TeguFunction *fragment,const char *fn,NSUInteger fl,uint32_t format,const TeguAIRRenderColorAttachment *targets,NSUInteger targetCount,
 const TeguAIRRenderBlend *blends,const TeguAIRVertexAttribute *attributes,NSUInteger attributeCount,
 const TeguAIRVertexLayout *layouts,NSUInteger layoutCount) {
 const TeguAIRLibrary *vlib=vertex->library->library,*flib=fragment->library->library;char error[160];
 int blendOne=variant==TEGU_PC_AIR_BLEND,explicitTargets=variant==TEGU_PC_AIR_ATTACHMENTS||variant==TEGU_PC_AIR_ATTACHMENT_BLENDS;
 (void)tegu_air_render_pipeline_attach_source(p,vlib,(const uint8_t *)vn,vl,flib,(const uint8_t *)fn,fl,
  explicitTargets?0:format,attributes,attributeCount,layouts,layoutCount,blendOne?blends:0,
  explicitTargets?targets:0,explicitTargets?targetCount:0,variant==TEGU_PC_AIR_ATTACHMENT_BLENDS?blends:0,error,sizeof(error));
}
static TeguAIRRenderPipeline *tegu_pc_render_create(unsigned variant,TeguFunction *vertex,TeguFunction *fragment,
 uint32_t format,const TeguAIRRenderColorAttachment *targets,NSUInteger targetCount,const TeguAIRRenderBlend *blends,
 const TeguAIRVertexAttribute *attributes,NSUInteger attributeCount,const TeguAIRVertexLayout *layouts,NSUInteger layoutCount,
 const unsigned char *profile,NSUInteger profileBytes,TeguAIRBlob *vs,TeguAIRBlob *fs,TeguAIRBlob *meta,char *message,NSUInteger size) {
 const char *vn=[vertex->functionName UTF8String],*fn=[fragment->functionName UTF8String];
 NSUInteger vl=0,fl=0;if(vn)while(vl<4096&&vn[vl])vl++;if(fn)while(fl<4096&&fn[fl])fl++;
 struct tegu_pc *cache=tegu_render_cache_get();
 int cacheable=cache&&vertex->library->cacheIdentityValid&&fragment->library->cacheIdentityValid&&vn&&fn&&
  vs&&fs&&meta&&!vs->bytes&&!fs->bytes&&!meta->bytes&&profile&&profileBytes&&targetCount<=8&&attributeCount<=31&&layoutCount<=31;
 unsigned char key[32];
 if(cacheable) {
  struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);
  tegu_pc_key_str(&s,"TPC-AIR-1");tegu_pc_key_str(&s,tegu_pc_build_id());tegu_pc_key_u32(&s,variant);
  tegu_pc_key_bytes(&s,vertex->library->cacheIdentity,32);tegu_pc_key_bytes(&s,vn,vl);
  tegu_pc_key_bytes(&s,fragment->library->cacheIdentity,32);tegu_pc_key_bytes(&s,fn,fl);
  tegu_pc_key_u32(&s,format);tegu_pc_key_u32(&s,(uint32_t)targetCount);
  for(NSUInteger i=0;targets&&i<targetCount;i++){tegu_pc_key_u32(&s,targets[i].format);tegu_pc_key_u32(&s,targets[i].write_mask);}
  NSUInteger blendCount=!blends?0:variant==TEGU_PC_AIR_BLEND?1:variant==TEGU_PC_AIR_ATTACHMENT_BLENDS?targetCount:0;
  tegu_pc_key_u32(&s,(uint32_t)blendCount);for(NSUInteger i=0;i<blendCount;i++)tegu_pc_key_blend(&s,&blends[i]);
  tegu_pc_key_u32(&s,(uint32_t)attributeCount);
  for(NSUInteger i=0;attributes&&i<attributeCount;i++){const TeguAIRVertexAttribute *a=&attributes[i];
   tegu_pc_key_u32(&s,a->index);tegu_pc_key_u32(&s,a->format);tegu_pc_key_u32(&s,a->offset);tegu_pc_key_u32(&s,a->buffer_index);}
  tegu_pc_key_u32(&s,(uint32_t)layoutCount);
  for(NSUInteger i=0;layouts&&i<layoutCount;i++){const TeguAIRVertexLayout *l=&layouts[i];
   tegu_pc_key_u32(&s,l->index);tegu_pc_key_u32(&s,l->stride);tegu_pc_key_u32(&s,l->step_function);tegu_pc_key_u32(&s,l->step_rate);}
  tegu_pc_key_bytes(&s,profile,profileBytes);tegu_pc_key_u32(&s,(uint32_t)size);
  tegu_pc_sha256_final(&s,key);
  unsigned char *payload=0;size_t bytes=0;int hit=tegu_pc_lookup(cache,TEGU_PC_KIND_AIR,key,&payload,&bytes);
  /* A refused translation is stored as "TGRF" + the refusal message. */
  if(hit&&bytes>=4&&!memcmp(payload,"TGRF",4)) {
   size_t text=bytes-4;
   if(message&&text<size) {
    memcpy(message,payload+4,text);message[text]=0;free(payload);
    tegu_render_cache_count(hit);TEGU_PC_REC_EMIT_HIT(hit);return 0;
   }
   free(payload);tegu_pc_reject(cache,TEGU_PC_KIND_AIR,key);hit=0;
  }
  if(hit) {
   char decodeError[256]={0};
   TeguAIRRenderPipeline *p=tegu_air_render_pipeline_deserialize(payload,bytes,profile,profileBytes,vs,fs,meta,decodeError,sizeof(decodeError));
   free(payload);
   if(p) {
    tegu_pc_render_attach_source(p,variant,vertex,vn,vl,fragment,fn,fl,format,targets,targetCount,blends,
     attributes,attributeCount,layouts,layoutCount);
    tegu_pc_render_key_note(p,key);
    tegu_render_cache_count(hit);TEGU_PC_REC_EMIT_HIT(hit);
    if(message&&size)message[0]=0; /* what a successful create leaves */
    return p;
   }
   tegu_air_blob_free(vs);tegu_air_blob_free(fs);tegu_air_blob_free(meta);
   tegu_pc_reject(cache,TEGU_PC_KIND_AIR,key);
   char line[320];int n=snprintf(line,sizeof(line),"TEGU_PIPELINE_CACHE air_reject %.200s\n",decodeError);
   if(n>0)write(2,line,(size_t)n<sizeof(line)?(size_t)n:sizeof(line)-1);
  }
  tegu_render_cache_count(0);
 }
 double start=tegu_host_seconds();
 TeguAIRRenderPipeline *p=tegu_pc_air_compile(variant,vertex,vn,vl,fragment,fn,fl,format,targets,targetCount,blends,
  attributes,attributeCount,layouts,layoutCount,profile,profileBytes,vs,fs,meta,message,size);
 double spent=tegu_host_seconds()-start;
 if(spent>0)__atomic_fetch_add(&tegu_perf_air_ns,(U64)(spent*1e9),__ATOMIC_RELAXED);
 if(cacheable) {
  char keyHex[65];tegu_pc_hex(key,32,keyHex);
  char line[400];int n=snprintf(line,sizeof(line),"TEGU_PSO_MISS air key=%.16s v=%.60s f=%.60s variant=%u fmt=%u targets=%lu ms=%u %s\n",
   keyHex,vn,fn,variant,format,(unsigned long)targetCount,(unsigned)(spent*1e3),p?"ok":"refused");
  if(n>0&&(size_t)n<sizeof(line))tegu_render_cache_miss_line(line);
  /* Full replay record: everything the key covers, in text. */
  size_t at=0;char *record=tegu_pc_air_record(&at,keyHex,variant,format,vertex,vn,fragment,fn,targets,targetCount,blends,
   attributes,attributeCount,layouts,layoutCount,profile,profileBytes,size);
  if(record) {
   size_t cap=at+64;char *full=malloc(cap);
   if(full) {
    size_t used=at;memcpy(full,record,at);
    used+=(size_t)snprintf(full+used,cap-used," ms=%u status=%s\n",(unsigned)(spent*1e3),p?"ok":"refused");
    if(used<cap)tegu_pc_append_record(cache,"misses.txt",full,used,4u<<20);
    free(full);
   }
   if(tegu_pc_rec_claim(key))tegu_pc_rec_console(keyHex,"miss",record,at);
   free(record);
  }
 }
 if(cacheable&&!p&&message&&message[0]&&!vs->bytes&&!fs->bytes&&!meta->bytes) {
  size_t text=0;while(text<size&&message[text])text++;
  unsigned char *refusal=text<size?malloc(text+4):0;
  if(refusal){memcpy(refusal,"TGRF",4);memcpy(refusal+4,message,text);tegu_pc_store(cache,TEGU_PC_KIND_AIR,key,refusal,text+4);free(refusal);}
 }
 if(cacheable&&p) {
  TeguAIRBlob serialized={0};char serializeError[128];
  if(!tegu_air_render_pipeline_serialize(p,&serialized,serializeError,sizeof(serializeError))&&serialized.bytes)
   tegu_pc_store(cache,TEGU_PC_KIND_AIR,key,serialized.bytes,serialized.length);
  tegu_air_blob_free(&serialized);
  tegu_pc_render_key_note(p,key);
 }
 return p;
}

/* ---- Mali compile ---- */
enum {TEGU_PC_MALI_GENERIC=1,TEGU_PC_MALI_GENERIC_RT=2,TEGU_PC_MALI_ATTACHMENT_CLEAR_V2=3,TEGU_PC_MALI_RESOURCES=4,
 TEGU_PC_MALI_RESOURCES_V7=5,TEGU_PC_MALI_FIXED_UI_R80=6,TEGU_PC_MALI_FIXED_UI_R85=7,TEGU_PC_MALI_BLUR3=8,TEGU_PC_MALI_TEXTURE=9,TEGU_PC_MALI_GENERIC_TILE_RT=10,
 TEGU_PC_MALI_GENERIC_OPTIONS=11};
/* tegu_mali_compile_generic_pair_options: the render path's generic compile (specialization
 * tables, options struct, varyings).  C2 (4462135) moved every generic compile from the
 * cached _pair/_rt/_tile_rt entries to this one without a cache, so UI boots recompiled all
 * generic Mali pairs each time (TEGU_PERF_PSO mali_ms=0, compile_ms >> air_ms).  The extra
 * inputs are keyed here; the varyings output is stored behind the plan. */
struct tegu_pc_generic_ctx {
 const struct tegu_mali_specialization *vc,*fc;size_t vcn,fcn;const void *options;size_t optionsBytes;
};
struct tegu_pc_generic_blob {struct tegu_mali_generic_plan plan;struct tegu_mali_varying_info varying;};
static int tegu_pc_mali_compile(unsigned entry,unsigned rt,const void *vs,size_t vn,const void *fs,size_t fn,
 struct tegu_mali_graphics *vo,struct tegu_mali_graphics *fo,struct tegu_mali_graphics_pair2 *pair,void *plan,char *message,size_t size,
 const struct tegu_pc_generic_ctx *ctx) {
 switch(entry) {
 case TEGU_PC_MALI_GENERIC_OPTIONS:
  if(!ctx)return -1;
  return tegu_mali_compile_generic_pair_options(vs,vn,fs,fn,ctx->vc,ctx->vcn,ctx->fc,ctx->fcn,
   (const struct tegu_mali_generic_options *)ctx->options,vo,fo,pair,
   &((struct tegu_pc_generic_blob *)plan)->plan,&((struct tegu_pc_generic_blob *)plan)->varying,message,size);
#ifdef TEGU_TILE_FETCH_DEFAULT
 case TEGU_PC_MALI_GENERIC_TILE_RT:return tegu_mali_compile_generic_pair_tile_rt(vs,vn,fs,fn,vo,fo,pair,plan,rt,message,size);
#endif
 case TEGU_PC_MALI_GENERIC:return tegu_mali_compile_generic_pair(vs,vn,fs,fn,vo,fo,pair,plan,message,size);
 case TEGU_PC_MALI_GENERIC_RT:return tegu_mali_compile_generic_pair_rt(vs,vn,fs,fn,vo,fo,pair,plan,rt,message,size);
 case TEGU_PC_MALI_ATTACHMENT_CLEAR_V2:return tegu_mali_compile_attachment_clear_pair_v2(vs,vn,fs,fn,vo,fo,plan,message,size);
 case TEGU_PC_MALI_RESOURCES:return tegu_mali_compile_graphics_pair_resources(vs,vn,fs,fn,vo,fo,pair,plan,message,size);
 case TEGU_PC_MALI_RESOURCES_V7:return tegu_mali_compile_graphics_pair_resources_v7(vs,vn,fs,fn,vo,fo,pair,plan,message,size);
 case TEGU_PC_MALI_FIXED_UI_R80:return tegu_mali_compile_fixed_ui_pair_r80(vs,vn,fs,fn,vo,fo,pair,plan,message,size);
 case TEGU_PC_MALI_FIXED_UI_R85:return tegu_mali_compile_fixed_ui_texture_pair_r85(vs,vn,fs,fn,vo,fo,pair,plan,message,size);
 case TEGU_PC_MALI_BLUR3:return tegu_mali_compile_single_pass_blur3_pair(vs,vn,fs,fn,vo,fo,pair,plan,message,size);
 case TEGU_PC_MALI_TEXTURE:return tegu_mali_compile_graphics_pair_texture(vs,vn,fs,fn,vo,fo,pair,plan,message,size);
 }
 return -1;
}
static int tegu_pc_all_zero(const void *p,size_t n) {
 const unsigned char *b=(const unsigned char *)p;for(size_t i=0;i<n;i++)if(b[i])return 0;
 return 1;
}
#define TEGU_PC_MALI_MAGIC 0x4d435054u /* "TPCM" */
#define TEGU_PC_MALI_VERSION 1u
struct tegu_pc_writer {unsigned char *p;size_t n,cap;int failed;};
static void tegu_pc_put(struct tegu_pc_writer *w,const void *data,size_t n) {
 if(w->failed||!n)return;
 if(w->n+n>w->cap) {
  size_t cap=w->cap?w->cap:4096;while(cap<w->n+n)cap*=2;
  unsigned char *grown=malloc(cap);if(!grown){w->failed=1;return;}
  if(w->n)memcpy(grown,w->p,w->n);free(w->p);w->p=grown;w->cap=cap;
 }
 memcpy(w->p+w->n,data,n);w->n+=n;
}
static void tegu_pc_put_u32v(struct tegu_pc_writer *w,uint32_t v){tegu_pc_put(w,&v,4);}
static void tegu_pc_put_u64v(struct tegu_pc_writer *w,uint64_t v){tegu_pc_put(w,&v,8);}
static void tegu_pc_put_graphics(struct tegu_pc_writer *w,const struct tegu_mali_graphics *g) {
 tegu_pc_put_u32v(w,g->code!=0);tegu_pc_put_u64v(w,g->size);
 const uint32_t fields[6]={g->stage,g->registers,g->tls,g->fau_words,g->sysval_mask,g->push_bytes};
 tegu_pc_put(w,fields,sizeof(fields));tegu_pc_put_u64v(w,g->preload);
 if(g->code)tegu_pc_put(w,g->code,g->size);
}
struct tegu_pc_reader {const unsigned char *p;size_t n,at;int failed;};
static const unsigned char *tegu_pc_take(struct tegu_pc_reader *r,size_t n) {
 if(r->failed||n>r->n-r->at){r->failed=1;return 0;}
 const unsigned char *v=r->p+r->at;r->at+=n;return v;
}
static uint32_t tegu_pc_take_u32(struct tegu_pc_reader *r){const unsigned char *v=tegu_pc_take(r,4);uint32_t x=0;if(v)memcpy(&x,v,4);return x;}
static uint64_t tegu_pc_take_u64(struct tegu_pc_reader *r){const unsigned char *v=tegu_pc_take(r,8);uint64_t x=0;if(v)memcpy(&x,v,8);return x;}
static int tegu_pc_take_graphics(struct tegu_pc_reader *r,struct tegu_mali_graphics *g) {
 memset(g,0,sizeof(*g));
 uint32_t present=tegu_pc_take_u32(r);uint64_t bytes=tegu_pc_take_u64(r);
 const unsigned char *fields=tegu_pc_take(r,24);uint64_t preload=tegu_pc_take_u64(r);
 if(r->failed||present>1||(!present&&bytes)||bytes>(2u<<20))return -1;
 const unsigned char *code=present?tegu_pc_take(r,(size_t)bytes):0;
 if(r->failed)return -1;
 uint32_t f[6];memcpy(f,fields,sizeof(f));
 if(present) {g->code=malloc(bytes?(size_t)bytes:1);if(!g->code)return -1;if(bytes)memcpy(g->code,code,(size_t)bytes);}
 g->size=(size_t)bytes;g->stage=f[0];g->registers=f[1];g->tls=f[2];g->fau_words=f[3];g->sysval_mask=f[4];g->push_bytes=f[5];g->preload=preload;
 return 0;
}
/* Draw-time Mali compiles (entry 11) depend on the draw's specialization tables, not on the
 * pipeline descriptor alone: log them too ("TPC_MISS v1 mali"), so the host replay can compile
 * the same variants.  The SPIR-V is named by SHA-256 (the replay owns the same bytes from the
 * AIR record); tables and options are hex. */
#ifdef TEGU_FRONTEND_HOST
/* Replay registry of every SPIR-V pair the frontend compiled (see pipeline_replay_host_test.inc). */
static void (*tegu_pc_spirv_hook)(const void *,size_t,const void *,size_t);
#endif
static void tegu_pc_sha_hex(const void *p,size_t n,char out[65]) {
 struct tegu_pc_sha256 s;unsigned char d[32];tegu_pc_sha256_init(&s);tegu_pc_sha256_update(&s,p,n);tegu_pc_sha256_final(&s,d);tegu_pc_hex(d,32,out);
}
static void tegu_pc_mali_record(const unsigned char key[32],const char *src,unsigned entry,unsigned rt,const void *vs,size_t vn,
 const void *fs,size_t fn,size_t msgcap,const struct tegu_pc_generic_ctx *ctx) {
 if(!ctx||!tegu_pc_rec_claim(key))return;
 char kh[65],vh[65],fh[65];tegu_pc_hex(key,32,kh);tegu_pc_sha_hex(vs,vn,vh);tegu_pc_sha_hex(fs,fn,fh);
 size_t vb=ctx->vcn*sizeof(*ctx->vc),fb=ctx->fcn*sizeof(*ctx->fc),cap=512+2*(vb+fb+ctx->optionsBytes);
 char *text=malloc(cap);if(!text)return;
 size_t at=(size_t)snprintf(text,cap,"TPC_MISS v1 mali key=%s entry=%u rt=%u msgcap=%lu vn=%lu fn=%lu vsha=%s fsha=%s vc=",kh,entry,rt,
  (unsigned long)msgcap,(unsigned long)vn,(unsigned long)fn,vh,fh);
 const struct {const void *p;size_t n;const char *tag;} parts[3]={{ctx->vc,vb,""},{ctx->fc,fb," fc="},{ctx->options,ctx->optionsBytes," opt="}};
 for(unsigned i=0;i<3&&at<cap;i++) {
  if(parts[i].tag[0])at+=(size_t)snprintf(text+at,cap-at,"%s",parts[i].tag);
  for(size_t j=0;j<parts[i].n&&at+2<cap;j++)at+=(size_t)snprintf(text+at,cap-at,"%02x",((const unsigned char *)parts[i].p)[j]);
  if(!parts[i].n&&at+1<cap)text[at++]='-';
 }
 if(at<cap)tegu_pc_rec_console(kh,src,text,at);
 free(text);
}
/* Same contract as the compiler entry it names (status, outputs and the
 * message as the compiler leaves them, failures included).  pair may be
 * NULL (attachment clear); plan is planBytes of caller storage. */
static int tegu_pc_mali_pair_ctx(unsigned entry,unsigned rt,const void *vs,size_t vn,const void *fs,size_t fn,
 struct tegu_mali_graphics *vo,struct tegu_mali_graphics *fo,struct tegu_mali_graphics_pair2 *pair,void *plan,size_t planBytes,
 char *message,size_t size,const struct tegu_pc_generic_ctx *ctx) {
#ifdef TEGU_FRONTEND_HOST
 if(ctx&&tegu_pc_spirv_hook)tegu_pc_spirv_hook(vs,vn,fs,fn);
#endif
 struct tegu_pc *cache=tegu_render_cache_get();
 size_t pairBytes=pair?sizeof(*pair):0;
 /* Only the fresh-output calls the compiler defines are cached. */
 int cacheable=cache&&vs&&fs&&vn&&fn&&vo&&fo&&plan&&message&&size&&tegu_pc_all_zero(vo,sizeof(*vo))&&tegu_pc_all_zero(fo,sizeof(*fo))&&
  (!pair||tegu_pc_all_zero(pair,pairBytes))&&tegu_pc_all_zero(plan,planBytes);
 unsigned char key[32];
 if(cacheable) {
  struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);
  tegu_pc_key_str(&s,"TPC-MALI-1");tegu_pc_key_str(&s,tegu_pc_build_id());
  tegu_pc_key_u32(&s,entry);tegu_pc_key_u32(&s,rt);tegu_pc_key_u32(&s,(uint32_t)sizeof(*vo));
  tegu_pc_key_u32(&s,(uint32_t)pairBytes);tegu_pc_key_u32(&s,(uint32_t)planBytes);tegu_pc_key_u32(&s,(uint32_t)size);
  tegu_pc_key_bytes(&s,vs,vn);tegu_pc_key_bytes(&s,fs,fn);
  if(ctx) {
   tegu_pc_key_u32(&s,(uint32_t)ctx->vcn);tegu_pc_key_bytes(&s,ctx->vc,ctx->vcn*sizeof(*ctx->vc));
   tegu_pc_key_u32(&s,(uint32_t)ctx->fcn);tegu_pc_key_bytes(&s,ctx->fc,ctx->fcn*sizeof(*ctx->fc));
   tegu_pc_key_bytes(&s,ctx->options,ctx->optionsBytes);
  }
  tegu_pc_sha256_final(&s,key);
  unsigned char *payload=0;size_t bytes=0;int hit=tegu_pc_lookup(cache,TEGU_PC_KIND_MALI,key,&payload,&bytes);
  if(hit) {
   struct tegu_pc_reader r={payload,bytes,0,0};
   uint32_t magic=tegu_pc_take_u32(&r),version=tegu_pc_take_u32(&r),status=tegu_pc_take_u32(&r),messageBytes=tegu_pc_take_u32(&r);
   uint32_t storedPair=tegu_pc_take_u32(&r),storedPlan=tegu_pc_take_u32(&r);
   struct tegu_mali_graphics v,f;memset(&v,0,sizeof(v));memset(&f,0,sizeof(f));
   int ok=!r.failed&&magic==TEGU_PC_MALI_MAGIC&&version==TEGU_PC_MALI_VERSION&&storedPair==pairBytes&&storedPlan==planBytes&&
    messageBytes<size&&!tegu_pc_take_graphics(&r,&v)&&!tegu_pc_take_graphics(&r,&f);
   const unsigned char *pairData=ok&&pairBytes?tegu_pc_take(&r,pairBytes):0,*planData=ok?tegu_pc_take(&r,planBytes):0;
   const unsigned char *text=ok?tegu_pc_take(&r,messageBytes):0;
   ok=ok&&!r.failed&&r.at==r.n;
   if(ok) {
    *vo=v;*fo=f;if(pairBytes)memcpy(pair,pairData,pairBytes);memcpy(plan,planData,planBytes);
    if(messageBytes)memcpy(message,text,messageBytes);message[messageBytes]=0;
    free(payload);tegu_render_cache_count(hit);
    tegu_pc_mali_record(key,hit==1?"mem":hit==2?"disk":"pack",entry,rt,vs,vn,fs,fn,size,ctx);
    return (int)status;
   }
   tegu_mali_graphics_free(&v);tegu_mali_graphics_free(&f);free(payload);
   tegu_pc_reject(cache,TEGU_PC_KIND_MALI,key);
   write(2,"TEGU_PIPELINE_CACHE mali_reject\n",32);
  }
  tegu_render_cache_count(0);
 }
 double start=tegu_host_seconds();
 int status=tegu_pc_mali_compile(entry,rt,vs,vn,fs,fn,vo,fo,pair,plan,message,size,ctx);
 double spent=tegu_host_seconds()-start;
 if(spent>0)__atomic_fetch_add(&tegu_perf_mali_ns,(U64)(spent*1e9),__ATOMIC_RELAXED);
 if(cacheable) {
  tegu_pc_mali_record(key,"miss",entry,rt,vs,vn,fs,fn,size,ctx);
  char keyHex[65];tegu_pc_hex(key,32,keyHex);
  char line[256];int n=snprintf(line,sizeof(line),"TEGU_PSO_MISS mali key=%.16s entry=%u rt=%u vs=%lu fs=%lu ms=%u status=%d\n",
   keyHex,entry,rt,(unsigned long)vn,(unsigned long)fn,(unsigned)(spent*1e3),status);
  if(n>0&&(size_t)n<sizeof(line)) {
   tegu_render_cache_miss_line(line);
   char record[300];int m=snprintf(record,sizeof(record),"TPC_MISS v1 mali key=%s entry=%u rt=%u vs=%lu fs=%lu ms=%u status=%d\n",
    keyHex,entry,rt,(unsigned long)vn,(unsigned long)fn,(unsigned)(spent*1e3),status);
   if(m>0&&(size_t)m<sizeof(record))tegu_pc_append_record(cache,"misses.txt",record,(size_t)m,4u<<20);
  }
 }
 if(cacheable) {
  size_t messageBytes=0;while(messageBytes<size&&message[messageBytes])messageBytes++;
  if(messageBytes<size) {
   struct tegu_pc_writer w={0,0,0,0};
   tegu_pc_put_u32v(&w,TEGU_PC_MALI_MAGIC);tegu_pc_put_u32v(&w,TEGU_PC_MALI_VERSION);tegu_pc_put_u32v(&w,(uint32_t)status);
   tegu_pc_put_u32v(&w,(uint32_t)messageBytes);tegu_pc_put_u32v(&w,(uint32_t)pairBytes);tegu_pc_put_u32v(&w,(uint32_t)planBytes);
   tegu_pc_put_graphics(&w,vo);tegu_pc_put_graphics(&w,fo);
   if(pairBytes)tegu_pc_put(&w,pair,pairBytes);
   tegu_pc_put(&w,plan,planBytes);tegu_pc_put(&w,message,messageBytes);
   if(!w.failed)tegu_pc_store(cache,TEGU_PC_KIND_MALI,key,w.p,w.n);
   free(w.p);
  }
 }
 return status;
}
static int tegu_pc_mali_pair(unsigned entry,unsigned rt,const void *vs,size_t vn,const void *fs,size_t fn,
 struct tegu_mali_graphics *vo,struct tegu_mali_graphics *fo,struct tegu_mali_graphics_pair2 *pair,void *plan,size_t planBytes,
 char *message,size_t size) {
 return tegu_pc_mali_pair_ctx(entry,rt,vs,vn,fs,fn,vo,fo,pair,plan,planBytes,message,size,0);
}
/* Cached tegu_mali_compile_generic_pair_options: same arguments, outputs and
 * message as the compiler.  Non-fresh outputs, unknown option versions and the
 * disabled cache all call the compiler directly. */
static int tegu_pc_mali_generic_options(const void *vs,size_t vn,const void *fs,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vcn,const struct tegu_mali_specialization *fc,size_t fcn,
 const struct tegu_mali_generic_options *options,struct tegu_mali_graphics *vo,struct tegu_mali_graphics *fo,
 struct tegu_mali_graphics_pair2 *pair,struct tegu_mali_generic_plan *plan,struct tegu_mali_varying_info *varying,
 char *message,size_t size) {
 size_t optionsBytes=!options?0:options->version==TEGU_MALI_GENERIC_OPTIONS_VERSION?sizeof(struct tegu_mali_generic_options):
  options->version==TEGU_MALI_GENERIC_OPTIONS2_VERSION?sizeof(struct tegu_mali_generic_options2):0;
 if(!optionsBytes||!varying||!plan||!message||!size||vcn>64||fcn>64||(vcn&&!vc)||(fcn&&!fc)||
    !tegu_pc_all_zero(plan,sizeof(*plan))||!tegu_pc_all_zero(varying,sizeof(*varying)))
  return tegu_mali_compile_generic_pair_options(vs,vn,fs,fn,vc,vcn,fc,fcn,options,vo,fo,pair,plan,varying,message,size);
 struct tegu_pc_generic_blob blob;memset(&blob,0,sizeof(blob));
 const struct tegu_pc_generic_ctx ctx={vc,fc,vcn,fcn,options,optionsBytes};
 message[0]=0; /* invalid options leave the message untouched; never cache stale bytes */
 int status=tegu_pc_mali_pair_ctx(TEGU_PC_MALI_GENERIC_OPTIONS,0,vs,vn,fs,fn,vo,fo,pair,&blob,sizeof(blob),message,size,&ctx);
 *plan=blob.plan;*varying=blob.varying;
 return status;
}
