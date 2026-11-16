/* Pipeline replay: rebuild pipelines from "TPC_MISS v1 air" records through the
 * real frontend, AIR translator and Mali compiler, writing the pipeline cache
 * entries a device would have written (gpu/tools/build_pipeline_pack.py turns
 * them into PipelinePack.tpcpack).  Included by the translation unit
 * gpu/tools/build_pipeline_replay.py assembles (the NORD2_JM_TRANSPORT frontend
 * of the device against the fake JM bridge, as gpu/tests/frontend_jm_test.py);
 * its main() calls test_pipeline_replay with TEGU_PIPELINE_CACHE_BUILD_OVERRIDE
 * = the device build id.
 *
 * A record is the description pipeline_cache_frontend.inc logs for every AIR
 * pipeline (variant, format, both functions with library SHA-256 and function
 * constants, targets, blends, vertex descriptor).  It is turned back into the
 * Metal descriptor QuartzCore would have passed: for variant 4/3 one colour
 * attachment per target (an unbound slot is format 80 mask 0, as the frontend
 * compiles it), for variant 1/2 colour attachment 0 of the record's format.
 * Raster state is the UI default (1 sample, rasterization on, no depth).
 *
 * Every record gets its own cache directory <out>/r<NNNN>/com.tegu.metal.pipelines
 * so a disk-slot collision between two pipelines cannot lose an entry.  Whether
 * a record reproduced is decided by the caller from the keys of the written
 * entries (never by this code): the AIR key is recomputed independently
 * (pipeline_keys.py) and compared with the entry's. */
#include <dirent.h>
#include <sys/stat.h>
struct tegu_replay_library {char sha[65];TeguLibrary *library;};
static struct tegu_replay_library tegu_replay_libraries[16];
static unsigned tegu_replay_library_count;
static TeguLibrary *tegu_replay_library(TeguComputeDevice *device,const char *dir,const char *sha) {
 for(unsigned i=0;i<tegu_replay_library_count;i++)if(!strcmp(tegu_replay_libraries[i].sha,sha))return tegu_replay_libraries[i].library;
 char path[1024];snprintf(path,sizeof(path),"%s/%s.metallib",dir,sha);
 FILE *f=fopen(path,"rb");if(!f){fprintf(stderr,"replay: library %s not found in %s\n",sha,dir);return 0;}
 assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);assert(n>0);
 void *bytes=malloc((size_t)n);assert(bytes&&fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
 id error=0;TeguLibrary *lib=[device newTeguLibraryWithBytes:bytes length:(NSUInteger)n error:&error];free(bytes);
 if(!lib||!lib->cacheIdentityValid){fprintf(stderr,"replay: library %s did not load\n",sha);return 0;}
 /* The record's library hash is the file's SHA-256: refuse a mislabelled file. */
 if(strcmp(lib->sourceSHA256,sha)){fprintf(stderr,"replay: %s hashes to %s\n",path,lib->sourceSHA256);return 0;}
 assert(tegu_replay_library_count<16);
 snprintf(tegu_replay_libraries[tegu_replay_library_count].sha,65,"%s",sha);
 tegu_replay_libraries[tegu_replay_library_count++].library=lib;return lib;
}
/* "entry|count|kind:type:index:name:hex;..." (tegu_pc_specialized_replay). */
static TeguFunction *tegu_replay_function(TeguLibrary *base,const char *name,const char *spec,unsigned kind) {
 if(!strcmp(spec,"-")) {
  TeguFunction *f=[base newFunctionWithName:[NSString stringWithUTF8String:name]];
  if(f)f->kind=kind;return f;
 }
 const char *bar=strchr(spec,'|');if(!bar||(size_t)(bar-spec)!=strlen(name)||strncmp(spec,name,strlen(name)))return 0;
 char *end;unsigned long count=strtoul(bar+1,&end,10);if(*end!='|'||count>256)return 0;
 const char *p=end+1;
 TeguAIRFunctionConstant *constants=calloc(count?count:1,sizeof(*constants));char (*names)[128]=calloc(count?count:1,sizeof(*names));
 assert(constants&&names);
 for(unsigned long i=0;i<count;i++) {
  unsigned k,t,idx;int used=0;
  if(sscanf(p,"%u:%u:%u:%n",&k,&t,&idx,&used)!=3||k>1){free(constants);free(names);return 0;}
  p+=used;const char *colon=strchr(p,':');if(!colon||(size_t)(colon-p)>=sizeof(names[i])){free(constants);free(names);return 0;}
  memcpy(names[i],p,(size_t)(colon-p));names[i][colon-p]=0;p=colon+1;
  size_t hex=0;while((p[hex]>='0'&&p[hex]<='9')||(p[hex]>='a'&&p[hex]<='f'))hex++;
  if(hex%2||hex/2>sizeof(constants[i].data)||p[hex]!=';'){free(constants);free(names);return 0;}
  constants[i].kind=k;constants[i].type=t;constants[i].size=(uint32_t)(hex/2);constants[i].index=k?0:idx;
  if(k){constants[i].name=(const uint8_t *)names[i];constants[i].name_len=strlen(names[i]);}
  for(size_t b=0;b<hex/2;b++){unsigned v;sscanf(p+2*b,"%2x",&v);constants[i].data[b]=(uint8_t)v;}
  p+=hex+1;
 }
 char message[512]={0};
 TeguAIRLibrary *specialized=tegu_air_library_specialize(base->library,(const uint8_t *)name,strlen(name),constants,count,message,sizeof(message));
 if(!specialized){fprintf(stderr,"replay: specialize %s: %s\n",name,message);free(constants);free(names);return 0;}
 TeguLibrary *snapshot=[[TeguLibrary alloc] init];assert(snapshot);snapshot->owner=[base->owner retain];snapshot->library=specialized;
 memcpy(snapshot->sourceSHA256,base->sourceSHA256,sizeof(snapshot->sourceSHA256));
 tegu_pc_specialized_identity(base->cacheIdentity,name,strlen(name),constants,count,snapshot->cacheIdentity);
 snapshot->cacheIdentityValid=1;snapshot->cacheReplay=tegu_pc_specialized_replay(name,strlen(name),constants,count);
 free(constants);free(names);
 TeguFunction *function=[[TeguFunction alloc] init];assert(function);function->library=snapshot;
 function->functionName=[[NSString stringWithUTF8String:name] retain];function->kind=kind;return function;
}
static const char *tegu_replay_field(const char *line,const char *name,char *out,size_t size) {
 size_t n=strlen(name);const char *p=line;
 while((p=strstr(p,name))){
  if((p==line||p[-1]==' ')&&p[n]=='='){p+=n+1;size_t len=strcspn(p,"\n ");if(len>=size)return 0;memcpy(out,p,len);out[len]=0;return out;}
  p+=n;
 }
 return 0;
}
/* "a/b/c," rows of `width` unsigned values; returns the row count or -1. */
static int tegu_replay_rows(const char *text,unsigned width,unsigned rows[][9],unsigned max) {
 unsigned n=0;
 while(*text) {
  if(n>=max)return -1;
  for(unsigned i=0;i<width;i++){char *e;rows[n][i]=(unsigned)strtoul(text,&e,10);if(e==text)return -1;text=e;if(i+1<width){if(*text!='/')return -1;text++;}}
  if(*text!=',')return -1;text++;n++;
 }
 return (int)n;
}
static RenderDescriptor *tegu_replay_descriptor(TeguComputeDevice *device,const char *libdir,const char *line) {
 static char vertex[256],fragment[256],vlib[80],flib[80],vspec[65536],fspec[65536],tmp[64],rows[3][2048];
 if(!tegu_replay_field(line,"vertex",vertex,sizeof(vertex))||!tegu_replay_field(line,"fragment",fragment,sizeof(fragment))||
    !tegu_replay_field(line,"vlib",vlib,sizeof(vlib))||!tegu_replay_field(line,"flib",flib,sizeof(flib))||
    !tegu_replay_field(line,"vspec",vspec,sizeof(vspec))||!tegu_replay_field(line,"fspec",fspec,sizeof(fspec)))return 0;
 if(!tegu_replay_field(line,"variant",tmp,sizeof(tmp)))return 0;unsigned variant=(unsigned)strtoul(tmp,0,10);
 if(!tegu_replay_field(line,"format",tmp,sizeof(tmp)))return 0;unsigned format=(unsigned)strtoul(tmp,0,10);
 if(!tegu_replay_field(line,"targets",rows[0],sizeof(rows[0])))rows[0][0]=0;
 if(!tegu_replay_field(line,"blends",rows[1],sizeof(rows[1])))rows[1][0]=0;
 static unsigned targets[8][9],blends[8][9],attrs[31][9],layouts[31][9];
 int tc=tegu_replay_rows(rows[0],2,targets,8),bc=tegu_replay_rows(rows[1],9,blends,8);
 static char attrText[4096],layoutText[4096];
 if(!tegu_replay_field(line,"attrs",attrText,sizeof(attrText)))attrText[0]=0;
 if(!tegu_replay_field(line,"layouts",layoutText,sizeof(layoutText)))layoutText[0]=0;
 int ac=tegu_replay_rows(attrText,4,attrs,31),lc=tegu_replay_rows(layoutText,4,layouts,31);
 if(tc<0||bc<0||ac<0||lc<0||(variant==TEGU_PC_AIR_ATTACHMENT_BLENDS&&bc!=tc))return 0;
 TeguLibrary *vl=tegu_replay_library(device,libdir,vlib),*fl=tegu_replay_library(device,libdir,flib);if(!vl||!fl)return 0;
 RenderDescriptor *d=[[RenderDescriptor alloc] init];
 d->vertex=tegu_replay_function(vl,vertex,vspec,1);d->fragment=tegu_replay_function(fl,fragment,fspec,2);
 if(!d->vertex||!d->fragment){[d release];return 0;}
 d->colors=[[RenderColors alloc] init];for(unsigned i=0;i<8;i++)d->colors->colors[i]=[[RenderColor alloc] init];
 if(variant==TEGU_PC_AIR_VERTEX||variant==TEGU_PC_AIR_BLEND) {
  RenderColor *c=d->colors->colors[0];c->format=format;c->mask=15;
  if(variant==TEGU_PC_AIR_BLEND&&bc==1) {
   c->mask=blends[0][8];c->blend=(signed char)blends[0][1];c->rgbOp=blends[0][2];c->rgbSrc=blends[0][3];c->rgbDst=blends[0][4];
   c->alphaOp=blends[0][5];c->alphaSrc=blends[0][6];c->alphaDst=blends[0][7];
  }
 } else for(int i=0;i<tc;i++) {
  RenderColor *c=d->colors->colors[i];c->format=targets[i][0];c->mask=targets[i][1];
  if(bc==tc) {
   c->blend=(signed char)blends[i][1];c->rgbOp=blends[i][2];c->rgbSrc=blends[i][3];c->rgbDst=blends[i][4];
   c->alphaOp=blends[i][5];c->alphaSrc=blends[i][6];c->alphaDst=blends[i][7];
  }
 }
 d->samples=1;d->raster=1;d->amplification=1;
 RenderLayout *layout=[[RenderLayout alloc] init];d->layout=layout;layout->buffers=[[RenderLayout alloc] init];
 for(int i=0;i<ac;i++) {
  RenderAttribute *a=[[RenderAttribute alloc] init];assert(attrs[i][0]<31);layout->entries[attrs[i][0]]=a;
  a->value=attrs[i][1];a->byteOffset=attrs[i][2];a->bufferSlot=attrs[i][3];
 }
 for(int i=0;i<lc;i++) {
  RenderAttribute *a=[[RenderAttribute alloc] init];assert(layouts[i][0]<31);layout->buffers->entries[layouts[i][0]]=a;
  a->layoutStride=layouts[i][1];a->step=layouts[i][2];a->rate=layouts[i][3];
 }
 return d;
}

/* ---- draw-time Mali variants ("TPC_MISS v1 mali"): same SPIR-V as an AIR pipeline replayed above, other tables ---- */
struct tegu_replay_spirv {char vsha[65],fsha[65];unsigned char *vs,*fs;size_t vn,fn;};
static struct tegu_replay_spirv *tegu_replay_spirvs;
static unsigned tegu_replay_spirv_count,tegu_replay_spirv_cap;
/* AIR-planned pipelines have no SPIR-V (interface-facts modules), so a mali
 * record (keyed by the m2v SPIR-V sha it was captured with) cannot find its
 * pipeline by sha. Map mode (TEGU_REPLAY_MAP_OUT=file, run with TEGU_AIR_PLAN=0)
 * writes "record vsha fsha" for every pipeline record; the normal run
 * (TEGU_REPLAY_MAP=file) keeps each record's pipeline and compiles the mali
 * records' variants through it, exactly as a draw does on the device (AIR
 * context, TPC-MALI-1 entry 12 keys). */
static FILE *tegu_replay_map_out;static int tegu_replay_current=-1,tegu_replay_mapped=-1;
struct tegu_replay_map_row {char vsha[65],fsha[65];unsigned idx;};   /* idx: the pipeline record; a map file may repeat an idx (m2v and AIR-facts shas) */
static struct tegu_replay_map_row *tegu_replay_map;static unsigned tegu_replay_map_count;
static TeguRenderPipeline **tegu_replay_pipes;static unsigned tegu_replay_pipe_count;
static void tegu_replay_spirv_note(const void *vs,size_t vn,const void *fs,size_t fn) {
 char vh[65],fh[65];tegu_pc_sha_hex(vs,vn,vh);tegu_pc_sha_hex(fs,fn,fh);
 if(tegu_replay_map_out&&tegu_replay_current>=0&&tegu_replay_mapped!=tegu_replay_current){
  fprintf(tegu_replay_map_out,"%d %s %s\n",tegu_replay_current,vh,fh);tegu_replay_mapped=tegu_replay_current;}
 for(unsigned i=0;i<tegu_replay_spirv_count;i++)if(!strcmp(tegu_replay_spirvs[i].vsha,vh)&&!strcmp(tegu_replay_spirvs[i].fsha,fh))return;
 if(tegu_replay_spirv_count==tegu_replay_spirv_cap) {
  tegu_replay_spirv_cap=tegu_replay_spirv_cap?tegu_replay_spirv_cap*2:64;
  tegu_replay_spirvs=realloc(tegu_replay_spirvs,tegu_replay_spirv_cap*sizeof(*tegu_replay_spirvs));assert(tegu_replay_spirvs);
 }
 struct tegu_replay_spirv *e=&tegu_replay_spirvs[tegu_replay_spirv_count++];
 memcpy(e->vsha,vh,65);memcpy(e->fsha,fh,65);e->vn=vn;e->fn=fn;
 e->vs=malloc(vn);e->fs=malloc(fn);assert(e->vs&&e->fs);memcpy(e->vs,vs,vn);memcpy(e->fs,fs,fn);
}
static size_t tegu_replay_unhex(const char *hex,unsigned char *out,size_t max) {
 size_t n=0;
 if(hex[0]=='-'&&!hex[1])return 0;
 for(;hex[2*n]&&hex[2*n+1];n++){unsigned v;if(n>=max||sscanf(hex+2*n,"%2x",&v)!=1)return (size_t)-1;out[n]=(unsigned char)v;}
 return n;
}
static int tegu_replay_mali(const char *line,const char *out,unsigned index) {
 static char vsha[80],fsha[80],vch[16384],fch[16384],och[1024],tmp[32];
 static unsigned char vc[16384/2],fc[16384/2],opt[512];
 if(!tegu_replay_field(line,"vsha",vsha,sizeof(vsha))||!tegu_replay_field(line,"fsha",fsha,sizeof(fsha))||!tegu_replay_field(line,"vc",vch,sizeof(vch))||
    !tegu_replay_field(line,"fc",fch,sizeof(fch))||!tegu_replay_field(line,"opt",och,sizeof(och))||!tegu_replay_field(line,"msgcap",tmp,sizeof(tmp)))return -1;
 size_t msgcap=strtoul(tmp,0,10),vcb=tegu_replay_unhex(vch,vc,sizeof(vc)),fcb=tegu_replay_unhex(fch,fc,sizeof(fc)),ob=tegu_replay_unhex(och,opt,sizeof(opt));
 if(vcb==(size_t)-1||fcb==(size_t)-1||ob==(size_t)-1||vcb%sizeof(struct tegu_mali_specialization)||fcb%sizeof(struct tegu_mali_specialization)||!ob||!msgcap)return -1;
 if(tegu_replay_map) {
  /* AIR-planned replay: the record's pipeline, its variant compiled as at a draw. */
  TeguRenderPipeline *rp=0;
  for(unsigned i=0;i<tegu_replay_map_count&&!rp;i++)
   if(!strcmp(tegu_replay_map[i].vsha,vsha)&&!strcmp(tegu_replay_map[i].fsha,fsha)&&tegu_replay_map[i].idx<tegu_replay_pipe_count&&
      tegu_replay_pipes[tegu_replay_map[i].idx]&&tegu_replay_pipes[tegu_replay_map[i].idx]->genericDraw)rp=tegu_replay_pipes[tegu_replay_map[i].idx];
  if(!rp)return -2;
  if(ob<16)return -1;
  uint32_t ov[4];memcpy(ov,opt,16);
  if(ov[1]&TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC)return 0; /* runtime-specialization bases: prewarmed at creation */
  if(vcb/sizeof(struct tegu_mali_specialization)>TEGU_AIR_GRAPHICS_SPEC_MAX||fcb/sizeof(struct tegu_mali_specialization)>TEGU_AIR_GRAPHICS_SPEC_MAX)return -1;
  static TeguAIRGraphicsSpecializationPlan spec;memset(&spec,0,sizeof(spec));spec.version=1;
  spec.vertex_count=(uint32_t)(vcb/sizeof(struct tegu_mali_specialization));spec.fragment_count=(uint32_t)(fcb/sizeof(struct tegu_mali_specialization));
  memcpy(spec.vertex_constants,vc,vcb);memcpy(spec.fragment_constants,fc,fcb);
  char dir[1100];snprintf(dir,sizeof(dir),"%s/m%04u",out,index);mkdir(dir,0755);tegu_render_cache_test_reset(dir,1,0);
  struct tegu_render_generic_variant v;char *message=calloc(msgcap,1);assert(message);
  int st=tegu_render_generic_variant_compile(rp,ov[2],&spec,&v,message,msgcap);
  if(!st){tegu_mali_graphics_free(&v.vertex);tegu_mali_graphics_free(&v.fragment);}
  free(message);return st?1:0;
 }
 struct tegu_replay_spirv *sp=0;
 for(unsigned i=0;i<tegu_replay_spirv_count;i++)if(!strcmp(tegu_replay_spirvs[i].vsha,vsha)&&!strcmp(tegu_replay_spirvs[i].fsha,fsha))sp=&tegu_replay_spirvs[i];
 if(!sp)return -2;   /* its pipeline is not in the corpus: nothing to compile */
 char dir[1100];snprintf(dir,sizeof(dir),"%s/m%04u",out,index);mkdir(dir,0755);tegu_render_cache_test_reset(dir,1,0);
 struct tegu_mali_graphics vo,fo;struct tegu_mali_graphics_pair2 pair;struct tegu_mali_generic_plan plan;struct tegu_mali_varying_info varying;
 memset(&vo,0,sizeof(vo));memset(&fo,0,sizeof(fo));memset(&pair,0,sizeof(pair));memset(&plan,0,sizeof(plan));memset(&varying,0,sizeof(varying));
 char *message=calloc(msgcap,1);assert(message);
 int st=tegu_pc_mali_generic_options(sp->vs,sp->vn,sp->fs,sp->fn,(const struct tegu_mali_specialization *)vc,vcb/sizeof(struct tegu_mali_specialization),
  (const struct tegu_mali_specialization *)fc,fcb/sizeof(struct tegu_mali_specialization),(const struct tegu_mali_generic_options *)opt,
  &vo,&fo,&pair,&plan,&varying,message,msgcap);
 tegu_mali_graphics_free(&vo);tegu_mali_graphics_free(&fo);free(message);
 return st;
}
static void test_pipeline_replay(TeguComputeDevice *device,const char *libdir,const char *records,const char *out) {
 FILE *f=fopen(records,"r");assert(f);mkdir(out,0755);
 /* The UI processes of the device run with the compositor and generic-job capabilities. */
 U64 oldCaps=device->capabilities;device->capabilities=oldCaps|TEGU_GPU_CAP_COMPOSITOR|TEGU_GPU_CAP_GENERIC_JOB;
 static char line[262144];unsigned index=0,built=0,refused=0,bad=0;
 tegu_pc_spirv_hook=tegu_replay_spirv_note;
 {const char *mo=getenv("TEGU_REPLAY_MAP_OUT"),*mi=getenv("TEGU_REPLAY_MAP");
  if(mo){tegu_replay_map_out=fopen(mo,"w");assert(tegu_replay_map_out);}
  if(mi) {
   FILE *m=fopen(mi,"r");assert(m);char a[80],bb[80];int idx;
   while(fscanf(m,"%d %79s %79s",&idx,a,bb)==3) {
    if(idx<0||idx>100000)continue;
    tegu_replay_map=realloc(tegu_replay_map,(tegu_replay_map_count+1)*sizeof(*tegu_replay_map));assert(tegu_replay_map);
    memset(tegu_replay_map+tegu_replay_map_count,0,sizeof(*tegu_replay_map));
    snprintf(tegu_replay_map[tegu_replay_map_count].vsha,65,"%s",a);snprintf(tegu_replay_map[tegu_replay_map_count].fsha,65,"%s",bb);
    tegu_replay_map[tegu_replay_map_count++].idx=(unsigned)idx;
   }
   fclose(m);
   if(!tegu_replay_map){tegu_replay_map=calloc(1,sizeof(*tegu_replay_map));assert(tegu_replay_map);}
  }}
 while(fgets(line,sizeof(line),f)) {
  if(strncmp(line,"TPC_MISS v1 air ",16))continue;
  char dir[1100];snprintf(dir,sizeof(dir),"%s/r%04u",out,index);mkdir(dir,0755);
  RenderDescriptor *d=tegu_replay_descriptor(device,libdir,line);
  if(!d){printf("REPLAY %04u bad-record\n",index++);bad++;continue;}
  tegu_render_cache_test_reset(dir,1,0);
  tegu_replay_current=(int)index;
  id error=0;TeguRenderPipeline *p=[device newRenderPipelineStateWithDescriptor:d error:&error];
  tegu_replay_current=-1;
  if(tegu_replay_map) {
   if(index>=tegu_replay_pipe_count){unsigned nc=index+64;tegu_replay_pipes=realloc(tegu_replay_pipes,nc*sizeof(*tegu_replay_pipes));assert(tegu_replay_pipes);
    memset(tegu_replay_pipes+tegu_replay_pipe_count,0,(nc-tegu_replay_pipe_count)*sizeof(*tegu_replay_pipes));tegu_replay_pipe_count=nc;}
   tegu_replay_pipes[index]=p?[p retain]:0;
  }
  struct tegu_pc_stats st;tegu_pc_snapshot(&tegu_render_cache,&st);
  printf("REPLAY %04u %s misses=%llu stores=%llu\n",index++,p?"built":"refused",(unsigned long long)st.misses,(unsigned long long)st.stores);
  if(p){built++;[p release];}else refused++;
  [d release];
 }
 if(tegu_replay_map_out) { /* map mode: the pipeline records only */
  fclose(tegu_replay_map_out);tegu_replay_map_out=0;fclose(f);tegu_render_cache_test_reset(0,0,0);tegu_pc_spirv_hook=0;
  device->capabilities=oldCaps;printf("REPLAY MAP records=%u built=%u refused=%u bad=%u\n",index,built,refused,bad);
  assert(!bad);return;
 }
 /* second pass: the draw-time Mali variants of the pipelines above */
 rewind(f);unsigned mali=0,malibad=0,maliskipped=0,maliok=0;
 while(fgets(line,sizeof(line),f)) {
  if(strncmp(line,"TPC_MISS v1 mali ",17))continue;
  int st=tegu_replay_mali(line,out,mali);
  printf("REPLAY MALI %04u %s\n",mali,st==-1?"bad-record":st==-2?"no-pipeline":st?"refused":"compiled");
  if(st==-1)malibad++;else if(st==-2)maliskipped++;else maliok++;
  mali++;
 }
 /* Background runtime-specialization variants (render_pipeline.inc tegu_vf_*)
  * prewarmed at pipeline creation land in the cache too: let them finish. */
 {extern int usleep(unsigned);unsigned waited=0;
  while(__atomic_load_n(&tegu_vf_draining,__ATOMIC_ACQUIRE)&&waited<120000u){(void)usleep(1000);waited++;}
  printf("REPLAY RUNTIME VARIANTS prewarmed=%llu refused=%llu wait_ms=%u\n",(unsigned long long)tegu_vf_prewarmed,
   (unsigned long long)tegu_vf_failed,waited);}
 fclose(f);tegu_render_cache_test_reset(0,0,0);tegu_pc_spirv_hook=0;
 printf("REPLAY MALI SUMMARY records=%u compiled_or_refused=%u no_pipeline=%u bad=%u\n",mali,maliok,maliskipped,malibad);
 device->capabilities=oldCaps;
 printf("REPLAY SUMMARY records=%u built=%u refused=%u bad=%u\n",index,built,refused,bad);
 assert(!bad);
}
