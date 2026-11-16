/* AIR-direct graphics pairs: NIR from the vendored ansuz-metal AIR frontend
 * (air/, MIT; see air/NOTICE.md), adapted to the m2v plan ABI that the
 * render encoder already binds, then the unchanged generic pair pipeline
 * (lower_generic_resources, varyings link, compile_stage). Included at the end
 * of mali_graphics_compiler.c. Design: gpu/build/air-nir-20261028/DESIGN.md.
 *
 * Contract (see the header): the m2v metadata JSON of the same translation is
 * the authority for bindings, push residuals and runtime facts; whatever it
 * states that this adapter does not reproduce exactly is refused, so the
 * caller compiles the m2v SPIR-V instead. Never a silent approximation of a
 * binding. Arithmetic is Metal's own (fast-math flags as authored), which is
 * the point: m2v's conformance emulation is what costs the cycles.
 * Only compiled with -DTEGU_MALI_AIR (and -I air); otherwise the .c carries
 * refusing stubs, so builds that copy a fixed file list need nothing new. */
#include "nir_air.h"
#include "util/mesa-blake3.h"
#include "util/simple_mtx.h"
static double air_now(void){struct timespec t;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t);return t.tv_sec+t.tv_nsec*1e-9;}

/* ---- minimal JSON reader (trusted input: the pinned translator's own metadata) ---- */
enum { AJ_NULL, AJ_BOOL, AJ_NUM, AJ_STR, AJ_ARR, AJ_OBJ };
struct aj { uint32_t t,n,len; bool b; const char *s; struct aj *v; const char **k; uint32_t *kl; };
struct ajp { const char *p,*e; void *mem; };
static void aj_ws(struct ajp *P){while(P->p<P->e&&(*P->p==' '||*P->p=='\n'||*P->p=='\r'||*P->p=='\t'))P->p++;}
static bool aj_str(struct ajp *P,const char **s,uint32_t *len) {
 if(P->p>=P->e||*P->p!='"')return false;
 const char *a=++P->p;
 while(P->p<P->e&&*P->p!='"'){if(*P->p=='\\'&&++P->p>=P->e)return false;P->p++;}
 if(P->p>=P->e)return false;
 *s=a;*len=(uint32_t)(P->p-a);P->p++;return true;
}
static bool aj_val(struct ajp *P,struct aj *o,unsigned depth) {
 memset(o,0,sizeof(*o));
 if(depth>48)return false;
 aj_ws(P);if(P->p>=P->e)return false;
 char c=*P->p;
 if(c=='{'||c=='[') {
  bool obj=c=='{';char close=obj?'}':']';unsigned cap=0;
  P->p++;o->t=obj?AJ_OBJ:AJ_ARR;aj_ws(P);
  if(P->p<P->e&&*P->p==close){P->p++;return true;}
  for(;;) {
   if(o->n==cap) {
    cap=cap?cap*2:8;o->v=reralloc(P->mem,o->v,struct aj,cap);if(!o->v)return false;
    if(obj){o->k=reralloc(P->mem,o->k,const char *,cap);o->kl=reralloc(P->mem,o->kl,uint32_t,cap);if(!o->k||!o->kl)return false;}
   }
   if(obj) {
    aj_ws(P);if(!aj_str(P,&o->k[o->n],&o->kl[o->n]))return false;
    aj_ws(P);if(P->p>=P->e||*P->p!=':')return false;P->p++;
   }
   if(!aj_val(P,&o->v[o->n],depth+1))return false;
   o->n++;aj_ws(P);if(P->p>=P->e)return false;
   if(*P->p==','){P->p++;continue;}
   if(*P->p==close){P->p++;return true;}
   return false;
  }
 }
 if(c=='"'){o->t=AJ_STR;return aj_str(P,&o->s,&o->len);}
 if(P->e-P->p>=4&&!memcmp(P->p,"true",4)){o->t=AJ_BOOL;o->b=true;P->p+=4;return true;}
 if(P->e-P->p>=5&&!memcmp(P->p,"false",5)){o->t=AJ_BOOL;P->p+=5;return true;}
 if(P->e-P->p>=4&&!memcmp(P->p,"null",4)){o->t=AJ_NULL;P->p+=4;return true;}
 const char *a=P->p;
 while(P->p<P->e&&(*P->p=='-'||*P->p=='+'||*P->p=='.'||*P->p=='e'||*P->p=='E'||(*P->p>='0'&&*P->p<='9')))P->p++;
 if(P->p==a)return false;
 o->t=AJ_NUM;o->s=a;o->len=(uint32_t)(P->p-a);return true;
}
static const struct aj *aj_get(const struct aj *o,const char *key) {
 if(!o||o->t!=AJ_OBJ)return NULL;
 size_t l=strlen(key);
 for(uint32_t i=0;i<o->n;i++)if(o->kl[i]==l&&!memcmp(o->k[i],key,l))return &o->v[i];
 return NULL;
}
static bool aj_is(const struct aj *o,const char *s){return o&&o->t==AJ_STR&&o->len==strlen(s)&&!memcmp(o->s,s,o->len);}
static bool aj_u64(const struct aj *o,uint64_t *v) {
 if(!o||o->t!=AJ_NUM||!o->len||o->len>20)return false;
 uint64_t x=0;
 for(uint32_t i=0;i<o->len;i++){char c=o->s[i];if(c<'0'||c>'9')return false;uint64_t y=x*10+(uint64_t)(c-'0');if(y/10!=x&&x)return false;x=y;}
 *v=x;return true;
}
static bool aj_u32(const struct aj *o,uint32_t *v){uint64_t x;if(!aj_u64(o,&x)||x>UINT32_MAX)return false;*v=(uint32_t)x;return true;}
/* The single key of a serde externally tagged enum value. */
static const struct aj *aj_tag(const struct aj *o,const char **name,uint32_t *len) {
 if(!o||o->t!=AJ_OBJ||o->n!=1)return NULL;
 *name=o->k[0];*len=o->kl[0];return &o->v[0];
}
static bool aj_tag_is(const char *name,uint32_t len,const char *want){return len==strlen(want)&&!memcmp(name,want,len);}

/* ---- the m2v interface the AIR NIR must reproduce ---- */
enum { AIRB_BUFFER=1, AIRB_COMBINED=2, AIRB_TEXTURE=3, AIRB_SAMPLER=4, AIRB_INPUT=5 };
enum { AIRS_SAMPLER_STATE=1, AIRS_CONSTANT_LANES=2, AIRS_PRESENCE=3 };
#define AIR_PIXEL_COORDINATES 1u /* metal2vulkan metal::sampler::SamplerWord */
struct air_if_binding {
 uint32_t binding,ty,kind,stage,index; /* stage TEGU_GRAPHICS_VERTEX/FRAGMENT; index = Metal slot or attachment */
 int32_t sampler;                      /* COMBINED/SAMPLER: Metal sampler slot, -1 constexpr */
 uint64_t word;                        /* constexpr sampler word 0 */
};
struct air_if_residual { uint32_t stage,index,offset; };
struct air_if_spec { uint32_t stage,id,kind,index,fields,size;uint64_t def; };
struct air_if_attr { uint32_t location,buffer_index,offset; };
struct air_iface {
 uint32_t nb,nr,ns,na;
 struct air_if_binding b[TEGU_MALI_GENERIC_MAX_BINDINGS];
 struct air_if_residual r[32];
 struct air_if_spec s[32];
 struct air_if_attr a[31];
 uint32_t ncolor,color_format[8]; /* graphics.color_attachments (VkFormat, 0 none) */
};
struct air_used { uint32_t binding,kind,stage; };
struct air_request {
 const struct tegu_mali_air_pair *pair;
 struct air_iface iface;
 const struct tegu_mali_specialization *vc,*fc;size_t vc_count,fc_count;
 struct air_used used[2*TEGU_MALI_GENERIC_MAX_BINDINGS];unsigned used_count;
 char reason[224],deferred[224];
 bool runtime_spec; /* TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC: values from push words */
};
static uint32_t air_stage_of(const struct aj *o) {
 return aj_is(o,"Vertex")?TEGU_GRAPHICS_VERTEX:aj_is(o,"Fragment")?TEGU_GRAPHICS_FRAGMENT:0;
}
/* {"class":C,"index":n,"stage":S} */
static bool air_slot(const struct aj *o,const char *cls,uint32_t *stage,uint32_t *index) {
 return o&&aj_is(aj_get(o,"class"),cls)&&aj_u32(aj_get(o,"index"),index)&&(*stage=air_stage_of(aj_get(o,"stage")))!=0;
}
static const char *air_why(struct air_request *rq,const char *fmt,const char *what,uint32_t len) {
 snprintf(rq->reason,sizeof(rq->reason),fmt,(int)len,what?what:"");return rq->reason;
}
static const char *air_iface_parse(struct air_request *rq,void *mem) {
 const struct tegu_mali_air_pair *pair=rq->pair;
 struct air_iface *f=&rq->iface;memset(f,0,sizeof(*f));
 if(!pair->metadata||!pair->metadata_size||pair->metadata_size>(16u<<20))return "air: m2v metadata missing";
 struct ajp P={pair->metadata,pair->metadata+pair->metadata_size,mem};struct aj root;
 if(!aj_val(&P,&root,0)||root.t!=AJ_OBJ)return "air: m2v metadata is not JSON";
 uint64_t u;
 if(!aj_u64(aj_get(&root,"schema"),&u)||u!=1)return "air: m2v metadata schema";
 if(!aj_u64(aj_get(&root,"variant_bits"),&u))return "air: m2v metadata variant bits";
 if(u)return "air: m2v variant compile (sRGB/YCbCr/cubic/cross-class facts) not reproduced";
 const struct aj *in=aj_get(&root,"interface"),*g=aj_get(&root,"graphics");
 if(!in||!g)return "air: m2v metadata lacks interface/graphics";
 const struct aj *x;
 if((x=aj_get(in,"needs_handle_translation"))&&x->b)return "air: m2v handle translation";
 if((x=aj_get(in,"needs_pointer_translation"))&&x->b)return "air: m2v pointer translation";
 if((x=aj_get(in,"reaches_command_buffers"))&&x->b)return "air: indirect command buffers";
 if((x=aj_get(in,"threadgroup"))&&x->t==AJ_ARR&&x->n)return "air: threadgroup memory in a render stage";
 if((x=aj_get(g,"fragment_imageblock"))&&x->t!=AJ_NULL)return "air: imageblocks";
 if((x=aj_get(g,"storage_attachments"))&&x->t==AJ_ARR&&x->n)return "air: storage attachments";
 if((x=aj_get(g,"tessellation"))&&x->t!=AJ_NULL)return "air: tessellation";
 if((x=aj_get(g,"mesh_records"))&&x->t!=AJ_NULL)return "air: mesh records";
 if((x=aj_get(g,"color_attachments"))&&x->t==AJ_ARR) {
  if(x->n>8)return "air: more than 8 colour attachments";
  f->ncolor=x->n;
  for(uint32_t i=0;i<x->n;i++){const struct aj *fmt=aj_get(&x->v[i],"format");if(fmt&&fmt->t!=AJ_NULL&&!aj_u32(fmt,&f->color_format[i]))return "air: colour attachment format";}
 }
 /* bindings */
 const struct aj *layout=aj_get(&root,"layout"),*sets=aj_get(layout,"sets");
 if(!sets||sets->t!=AJ_ARR)return "air: m2v layout sets";
 for(uint32_t si=0;si<sets->n;si++) {
  const struct aj *set=&sets->v[si],*bs=aj_get(set,"bindings");uint32_t sn;
  if(!aj_u32(aj_get(set,"set"),&sn)||!bs||bs->t!=AJ_ARR)return "air: m2v set";
  if(bs->n&&sn)return "air: m2v descriptor set other than 0 (heap/argument tables)";
  for(uint32_t bi=0;bi<bs->n;bi++) {
   const struct aj *b=&bs->v[bi];struct air_if_binding o={0};uint32_t count;const char *tn;uint32_t tl;
   if(f->nb>=TEGU_MALI_GENERIC_MAX_BINDINGS)return "air: more m2v bindings than the generic plan holds";
   if(!aj_u32(aj_get(b,"binding"),&o.binding)||!aj_u32(aj_get(b,"ty"),&o.ty)||!aj_u32(aj_get(b,"count"),&count)||count!=1)
    return "air: m2v binding shape (arrays)";
   const struct aj *src=aj_tag(aj_get(b,"source"),&tn,&tl),*imm=aj_get(b,"immutable_sampler");
   if(!src)return "air: m2v binding source";
   o.sampler=-1;
   if(aj_tag_is(tn,tl,"Buffer")) {
    if(!air_slot(src,"Buffer",&o.stage,&o.index)||(o.ty!=6&&o.ty!=7))return "air: m2v buffer binding type";
    o.kind=AIRB_BUFFER;
   } else if(aj_tag_is(tn,tl,"Texture")) {
    if(!air_slot(src,"Texture",&o.stage,&o.index)||o.ty!=2)return "air: m2v texture binding type (storage image/texel buffer)";
    o.kind=AIRB_TEXTURE;
   } else if(aj_tag_is(tn,tl,"Sampler")) {
    uint32_t idx;if(!air_slot(src,"Sampler",&o.stage,&idx)||o.ty!=0)return "air: m2v sampler binding";
    o.kind=AIRB_SAMPLER;o.sampler=(int32_t)idx;o.index=idx;
   } else if(aj_tag_is(tn,tl,"CombinedTextureSampler")) {
    const struct aj *cs=aj_get(src,"sampler"),*k;const char *sn2;uint32_t sl;
    if(!air_slot(aj_get(src,"texture"),"Texture",&o.stage,&o.index)||o.ty!=1||!cs)return "air: m2v combined binding";
    if(((k=aj_get(cs,"raw"))&&k->b)||((k=aj_get(cs,"cube_gradient"))&&k->b))return "air: m2v raw/cube-gradient sampler use";
    const struct aj *sv=aj_tag(aj_get(cs,"sampler"),&sn2,&sl);
    if(!sv)return "air: m2v combined sampler";
    if(aj_tag_is(sn2,sl,"Slot")){uint32_t s;if(!aj_u32(sv,&s)||s>15)return "air: m2v sampler slot";o.sampler=(int32_t)s;}
    else if(aj_tag_is(sn2,sl,"Constexpr")){if(sv->t!=AJ_ARR||sv->n<1||!aj_u64(&sv->v[0],&o.word))return "air: m2v constexpr sampler";}
    else return air_why(rq,"air: m2v combined sampler form %.*s",sn2,sl);
    if((o.sampler<0)!=(imm&&imm->t!=AJ_NULL))return "air: m2v immutable sampler mismatch";
    o.kind=AIRB_COMBINED;
   } else if(aj_tag_is(tn,tl,"InputAttachment")) {
    if(!aj_u32(src,&o.index)||o.index>7)return "air: m2v input attachment";
    o.kind=AIRB_INPUT;o.stage=TEGU_GRAPHICS_FRAGMENT;
   } else return air_why(rq,"air: m2v binding source %.*s",tn,tl);
   if(o.binding>=TEGU_MALI_GENERIC_MAX_BINDINGS)return "air: m2v binding above 15";
   if(o.kind!=AIRB_COMBINED&&imm&&imm->t!=AJ_NULL)return "air: m2v immutable sampler on a non-combined binding";
   f->b[f->nb++]=o;
  }
 }
 /* push fields */
 const struct aj *push=aj_get(in,"push");
 for(uint32_t pi=0;push&&push->t==AJ_ARR&&pi<push->n;pi++) {
  const struct aj *fields=aj_get(&push->v[pi],"fields");x=aj_get(&push->v[pi],"argument_table_size");
  if(x&&(!aj_u64(x,&u)||u))return "air: m2v argument table";
  for(uint32_t fi=0;fields&&fields->t==AJ_ARR&&fi<fields->n;fi++) {
   const struct aj *fd=&fields->v[fi],*pl;const char *tn,*ln;uint32_t tl,ll,size;
   const struct aj *field=aj_tag(aj_get(fd,"field"),&tn,&tl),*loc=aj_tag(aj_get(fd,"loc"),&ln,&ll);
   if(!field&&aj_get(fd,"field")&&aj_get(fd,"field")->t==AJ_OBJ)return "air: m2v push field";
   if(!field){tn="?";tl=1;}
   if(aj_tag_is(tn,tl,"ColorWriteRule"))continue; /* m2v's own colour rounding emulation: not read */
   if(!aj_tag_is(tn,tl,"SlotBindOffset"))return air_why(rq,"air: m2v push field %.*s",tn,tl);
   struct air_if_residual r;
   if(!air_slot(field,"Buffer",&r.stage,&r.index)||!loc||!aj_tag_is(ln,ll,"Push")||!(pl=aj_get(loc,"offset"))||
      !aj_u32(pl,&r.offset)||!aj_u32(aj_get(fd,"size"),&size)||size!=4||r.offset>TEGU_GRAPHICS_AIR_PUSH_LIMIT-4||r.offset%4)
    return "air: m2v SlotBindOffset placement";
   if(f->nr>=32)return "air: too many m2v push fields";
   f->r[f->nr++]=r;
  }
 }
 /* specialization keys */
 const struct aj *stages=aj_get(&root,"stages");
 if(!stages||stages->t!=AJ_ARR)return "air: m2v stages";
 for(uint32_t si=0;si<stages->n;si++) {
  const struct aj *st=&stages->v[si];uint32_t flag;
  if(!aj_u32(aj_get(st,"stage"),&flag)||(flag!=1&&flag!=16))return "air: m2v stage";
  if((x=aj_get(st,"constant_blob"))&&x->t!=AJ_NULL)return "air: m2v constant blob";
  const struct aj *cs=aj_get(aj_get(st,"specialization"),"constants");
  for(uint32_t ci=0;cs&&cs->t==AJ_ARR&&ci<cs->n;ci++) {
   const struct aj *c=&cs->v[ci];struct air_if_spec o={0};const char *kn;uint32_t kl;
   o.stage=flag==1?TEGU_GRAPHICS_VERTEX:TEGU_GRAPHICS_FRAGMENT;
   const struct aj *key=aj_tag(aj_get(c,"key"),&kn,&kl);
   if(!key||!aj_u32(aj_get(c,"id"),&o.id)||!aj_u32(aj_get(c,"size"),&o.size)||!aj_u64(aj_get(c,"default"),&o.def)||o.size!=4)
    return "air: m2v specialization constant";
   uint32_t stg;
   if(aj_tag_is(kn,kl,"SamplerState")) {
    if(!aj_u32(aj_get(key,"fields"),&o.fields)||!air_slot(aj_get(key,"slot"),"Sampler",&stg,&o.index)||stg!=o.stage)
     return "air: m2v SamplerState key";
    o.kind=AIRS_SAMPLER_STATE;
   } else if(aj_tag_is(kn,kl,"Presence")) {
    /* whether a slot is bound: AIR code reads the binding as authored, which
     * is Metal's behaviour for a bound slot only (checked per variant) */
    const char *cls=aj_is(aj_get(key,"class"),"Buffer")?"Buffer":aj_is(aj_get(key,"class"),"Texture")?"Texture":"Sampler";
    if(!air_slot(key,cls,&stg,&o.index)||stg!=o.stage)return "air: m2v Presence key";
    o.kind=AIRS_PRESENCE;
   } else if(aj_tag_is(kn,kl,"ConstantLanes")) {
    if(!air_slot(key,"Texture",&stg,&o.index)||stg!=o.stage)return "air: m2v ConstantLanes key";
    o.kind=AIRS_CONSTANT_LANES;
   } else return air_why(rq,"air: m2v specialization key %.*s",kn,kl);
   if(f->ns>=32)return "air: too many m2v specialization constants";
   f->s[f->ns++]=o;
  }
 }
 /* vertex input */
 const struct aj *vi=aj_get(&root,"vertex_input");
 if(vi) {
  const struct aj *at=aj_get(vi,"attributes"),*bd=aj_get(vi,"bindings"),*pu=aj_get(vi,"pulled"),*vp=aj_get(vi,"variant_pulled");
  if((pu&&pu->t==AJ_ARR&&pu->n)||(vp&&vp->t==AJ_ARR&&vp->n))return "air: m2v pulled vertex attributes";
  for(uint32_t ai=0;at&&at->t==AJ_ARR&&ai<at->n;ai++) {
   struct air_if_attr a;uint32_t binding,found=0;
   if(f->na>=31||!aj_u32(aj_get(&at->v[ai],"location"),&a.location)||!aj_u32(aj_get(&at->v[ai],"binding"),&binding)||
      !aj_u32(aj_get(&at->v[ai],"offset"),&a.offset)||a.location>30)return "air: m2v vertex attribute";
   for(uint32_t bi=0;bd&&bd->t==AJ_ARR&&bi<bd->n;bi++) {
    uint32_t b2;if(aj_u32(aj_get(&bd->v[bi],"binding"),&b2)&&b2==binding&&aj_u32(aj_get(&bd->v[bi],"buffer_index"),&a.buffer_index))found++;
   }
   if(found!=1)return "air: m2v vertex binding";
   f->a[f->na++]=a;
  }
 }
 return NULL;
}
static const struct air_if_binding *air_find(const struct air_iface *f,unsigned kind,unsigned stage,unsigned index,int sampler) {
 const struct air_if_binding *hit=NULL;
 for(unsigned i=0;i<f->nb;i++) {
  const struct air_if_binding *b=&f->b[i];
  if(b->kind!=kind||b->stage!=stage||b->index!=index)continue;
  if((kind==AIRB_COMBINED||kind==AIRB_SAMPLER)&&b->sampler!=sampler)continue;
  if(hit)return NULL; /* ambiguous */
  hit=b;
 }
 return hit;
}
static const char *air_use(struct air_request *rq,unsigned binding,unsigned kind,unsigned stage) {
 for(unsigned i=0;i<rq->used_count;i++)
  if(rq->used[i].binding==binding&&rq->used[i].stage==stage){
   if(rq->used[i].kind!=kind&&!((rq->used[i].kind==TEGU_MALI_GENERIC_COMBINED)&&(kind==TEGU_MALI_GENERIC_TEXTURE||kind==TEGU_MALI_GENERIC_SAMPLER)))
    return "air: one binding used as two kinds";
   return NULL;
  }
 if(rq->used_count>=ARRAY_SIZE(rq->used))return "air: too many bindings";
 rq->used[rq->used_count++]=(struct air_used){binding,kind,stage};return NULL;
}
/* Reflect the adapted bindings into the generic plan (the encoder reads
 * stage_mask and input attachments from it); input attachments are recorded
 * by lower_generic_resources itself. */
static const char *air_record_plan(const struct air_request *rq,struct tegu_mali_generic_plan *plan) {
 for(unsigned i=0;i<rq->used_count;i++) {
  const char *why=generic_record(plan,rq->used[i].binding,rq->used[i].kind,rq->used[i].stage,0,0);
  if(why)return why;
 }
 return NULL;
}
/* After output selection (compile_pair_specialized): what dies with the
 * discarded outputs goes, including framebuffer reads (ansuz CHLOROPHYLL-EVAL
 * 9.2: a dead [[color(n)]] read made every CA variant a fetch shader). A read
 * still live without an m2v input attachment is refused. */
static const char *air_finish_fragment(const struct air_request *rq,nir_shader *fs) {
 bool progress;
 do {
  progress=false;
  NIR_PASS(progress,fs,nir_opt_dce);NIR_PASS(progress,fs,nir_opt_dead_cf);NIR_PASS(progress,fs,nir_opt_constant_folding);
  NIR_PASS(progress,fs,nir_opt_copy_prop);NIR_PASS(progress,fs,nir_opt_algebraic);
 } while(progress);
 NIR_PASS(_,fs,nir_remove_dead_derefs);NIR_PASS(_,fs,nir_remove_dead_variables,nir_var_image,NULL);
 nir_foreach_function_impl(impl,fs)nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(i->intrinsic==nir_intrinsic_load_output&&nir_intrinsic_io_semantics(i).fb_fetch_output)
   return "air: framebuffer fetch without an m2v input attachment";
 }
 nir_foreach_function_impl(impl,fs)nir_foreach_block(block,impl)nir_foreach_instr(instr,block)
  if(instr->type==nir_instr_type_tex&&nir_tex_instr_src_index(nir_instr_as_tex(instr),nir_tex_src_texture_handle)<0)
   return rq->deferred[0]?rq->deferred:"air: texture use without an exact m2v binding";
 nir_foreach_function_impl(impl,fs)nir_foreach_block(block,impl)nir_foreach_instr_safe(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  /* air/ emits buffer loads without ACCESS_CAN_REORDER, which DCE keeps */
  if((i->intrinsic==nir_intrinsic_load_ubo||i->intrinsic==nir_intrinsic_load_ssbo)&&nir_def_is_unused(&i->def)){nir_instr_remove(instr);continue;}
  if((i->intrinsic==nir_intrinsic_load_ubo||i->intrinsic==nir_intrinsic_load_ssbo)&&
     (!nir_src_is_const(i->src[0])||nir_src_as_uint(i->src[0])<(1u<<24)))
  {if(getenv("TEGU_AIR_PRINT_REFUSED"))nir_print_shader(fs,stderr);
   return rq->deferred[0]?rq->deferred:"air: buffer slot without an m2v binding";}
 }
 nir_shader_gather_info(fs,nir_shader_get_entrypoint(fs));
 return NULL;
}
static bool air_spec_value(const struct air_request *rq,const struct air_if_spec *s,uint64_t *value) {
 const struct tegu_mali_specialization *t=s->stage==TEGU_GRAPHICS_VERTEX?rq->vc:rq->fc;
 size_t n=s->stage==TEGU_GRAPHICS_VERTEX?rq->vc_count:rq->fc_count;
 *value=s->def;
 for(size_t i=0;i<n;i++)if(t[i].id==s->id) {
  if(t[i].size!=s->size)return false;
  uint64_t v=0;for(unsigned k=0;k<t[i].size&&k<8;k++)v|=(uint64_t)t[i].data[k]<<(8*k);
  *value=v;
 }
 return true;
}

/* The caller's SpecId table of a stage against the plan's specialization layout
 * (compile_generic_pair's check on the AIR path): every entry a declared id of
 * that stage, of its declared size, once. */
static const char *air_spec_check(const struct air_request *rq,unsigned stage,const struct tegu_mali_specialization *c,size_t n,uint32_t *bad) {
 for(size_t k=0;k<n;k++) {
  *bad=c[k].id;
  if(c[k].size!=1&&c[k].size!=2&&c[k].size!=4&&c[k].size!=8)return "Invalid specialization scalar size";
  for(size_t j=0;j<k;j++)if(c[j].id==c[k].id)return "Duplicate specialization ID";
  const struct air_if_spec *s=NULL;
  for(unsigned i=0;i<rq->iface.ns;i++)if(rq->iface.s[i].stage==stage&&rq->iface.s[i].id==c[k].id)s=&rq->iface.s[i];
  if(!s)return "Unknown specialization ID in this stage";
  if(s->size!=c[k].size)return "Specialization size differs from its declaration";
 }
 return NULL;
}
/* RUNTIME_SPEC: the push word of a stage's specialization constant, at
 * TEGU_SPEC_PUSH_BASE + 4*k with k its position in ascending SpecId order
 * among the stage's constants (tegu_spec_push_scan of the stage's module,
 * which declares exactly the plan's constants of that stage). */
static nir_def *air_spec_push(nir_builder *b,const struct air_request *rq,const struct air_if_spec *sp) {
 unsigned k=0;
 for(unsigned i=0;i<rq->iface.ns;i++)
  if(rq->iface.s[i].stage==sp->stage&&rq->iface.s[i].id<sp->id)k++;
 return nir_load_push_constant(b,1,32,nir_imm_int(b,0),.base=TEGU_SPEC_PUSH_BASE+4u*k,.range=4);
}
/* ---- per-stage adaptation ---- */
struct air_adapt {
 struct air_request *rq;unsigned stage;const struct air_shader_info *info;
 const char *why;nir_variable *subpass[8];
};
static bool air_adapt_buffer(nir_builder *b,nir_intrinsic_instr *i,void *data) {
 struct air_adapt *a=data;
 switch(i->intrinsic) {
 case nir_intrinsic_load_ubo:case nir_intrinsic_load_ssbo:break;
 case nir_intrinsic_store_ssbo:case nir_intrinsic_ssbo_atomic:case nir_intrinsic_ssbo_atomic_swap:
  if(!a->why)a->why="air: buffer write (the m2v plan class is read-only)";
  return false;
 case nir_intrinsic_load_constant:
  if(!a->why)a->why="air: shader constant data (needs a constant-blob binding)";
  return false;
 case nir_intrinsic_load_scratch:case nir_intrinsic_store_scratch:
  return false; /* private memory: THREAD_STORAGE rules apply in compile_stage */
 case nir_intrinsic_load_global:case nir_intrinsic_store_global:case nir_intrinsic_load_global_constant:
  if(!a->why)a->why="air: global (device address) access";
  return false;
 default:return false;
 }
 if(!nir_src_is_const(i->src[0])){if(!a->why)a->why="air: dynamic buffer index";return false;}
 unsigned slot=nir_src_as_uint(i->src[0]);
 const struct air_if_binding *bb=air_find(&a->rq->iface,AIRB_BUFFER,a->stage,slot,-1);
 const struct air_if_residual *r=NULL;
 for(unsigned k=0;k<a->rq->iface.nr;k++)
  if(a->rq->iface.r[k].stage==a->stage&&a->rq->iface.r[k].index==slot)r=&a->rq->iface.r[k];
 if(!bb||!r) {
  char *msg=a->stage==TEGU_GRAPHICS_FRAGMENT?a->rq->deferred:a->rq->reason;
  snprintf(msg,sizeof(a->rq->reason),"air: %s (stage %u buffer %u)",bb?"buffer without an m2v SlotBindOffset residual":"buffer slot without an m2v binding",a->stage,slot);
  /* A fragment read may die with an unselected output (air_finish_fragment). */
  if(a->stage!=TEGU_GRAPHICS_FRAGMENT&&!a->why)a->why=msg;
  return false;
 }
 const char *why=air_use(a->rq,bb->binding,bb->ty==6?TEGU_MALI_GENERIC_UBO:TEGU_MALI_GENERIC_READONLY_STORAGE,a->stage);
 if(why){if(!a->why)a->why=why;return false;}
 b->cursor=nir_before_instr(&i->instr);
 nir_def *residual=nir_load_push_constant(b,1,32,nir_imm_int(b,0),.base=r->offset,.range=4);
 nir_def *offset=nir_iadd(b,i->src[1].ssa,residual);
 nir_def *handle=nir_imm_int(b,pan_res_handle(TEGU_MALI_GENERIC_TEXTURE_TABLE,bb->binding));
 /* AIR's alignment is the Metal compiler's claim about the absolute address
  * (bind offset + offset). The binder's descriptor bases are 16-aligned
  * (metal2vulkan memory/views.rs; its uvec4 views rely on the same fact), so
  * residual + offset keeps that alignment up to 16 bytes. */
 unsigned mul=nir_intrinsic_align_mul(i),off=nir_intrinsic_align_offset(i);
 if(mul>16){mul=16;}
 off%=mul;
 nir_def *v;
 if(bb->ty==6)
  v=nir_load_ubo(b,i->def.num_components,i->def.bit_size,handle,offset,.align_mul=mul,.align_offset=off,
   .range_base=0,.range=~0u,.access=ACCESS_NON_WRITEABLE|ACCESS_CAN_REORDER);
 else
  v=nir_load_ssbo(b,i->def.num_components,i->def.bit_size,handle,offset,.align_mul=mul,.align_offset=off,
   .access=ACCESS_NON_WRITEABLE|ACCESS_CAN_REORDER);
 nir_def_replace(&i->def,v);
 return true;
}
static nir_def *air_tex_size(nir_builder *b,nir_tex_instr *tex,unsigned binding,unsigned comps) {
 nir_tex_instr *q=nir_tex_instr_create(b->shader,3);
 q->op=nir_texop_txs;q->sampler_dim=tex->sampler_dim;q->is_array=tex->is_array;q->dest_type=nir_type_int32;
 q->coord_components=0;
 q->src[0]=nir_tex_src_for_ssa(nir_tex_src_texture_handle,nir_imm_int(b,pan_res_handle(TEGU_MALI_GENERIC_TEXTURE_TABLE,binding)));
 q->src[1]=nir_tex_src_for_ssa(nir_tex_src_sampler_handle,nir_imm_int(b,pan_res_handle(TEGU_MALI_GENERIC_SAMPLER_TABLE,binding)));
 q->src[2]=nir_tex_src_for_ssa(nir_tex_src_lod,nir_imm_int(b,0));
 nir_def_init(&q->instr,&q->def,nir_tex_instr_dest_size(q),32);
 nir_builder_instr_insert(b,&q->instr);
 return nir_i2f32(b,nir_trim_vector(b,&q->def,comps));
}
static bool air_adapt_tex(nir_builder *b,nir_tex_instr *tex,struct air_adapt *a) {
 if(nir_tex_instr_src_index(tex,nir_tex_src_texture_handle)>=0)return false; /* done */
 const struct air_shader_info *info=a->info;
 unsigned t=tex->texture_index,s=tex->sampler_index;
 /* air/ binds a null texture (a pointer the constants do not remove) at slot
  * 31; Metal reads zero from it, which is what m2v emits too. */
 bool real31=false;
 for(unsigned k=0;k<info->num_resources;k++)
  if((info->resources[k].kind==AIR_RESOURCE_TEXTURE||info->resources[k].kind==AIR_RESOURCE_IMAGE)&&info->resources[k].index==31)real31=true;
 if(t==31&&!real31) {
  b->cursor=nir_before_instr(&tex->instr);
  nir_def_replace(&tex->def,nir_imm_zero(b,tex->def.num_components,tex->def.bit_size));
  return true;
 }
 bool sampled=nir_tex_instr_need_sampler(tex);
 int api_sampler=-1;uint64_t word=0;bool inline_sampler=false;
 if(sampled) {
  const struct air_resource *res=NULL;
  for(unsigned k=0;k<info->num_resources;k++)
   if(info->resources[k].kind==AIR_RESOURCE_SAMPLER&&info->resources[k].index==s)res=&info->resources[k];
  if(!res){a->why="air: sampler without a resource";return false;}
  if(res->inline_sampler){inline_sampler=true;word=res->sampler_state;}
  else api_sampler=(int)s;
 }
 const struct air_if_binding *bt=NULL,*bs=NULL;
 if(!sampled) {
  bt=air_find(&a->rq->iface,AIRB_TEXTURE,a->stage,t,-1);
  if(!bt) {
   /* a fetch/query through the combined descriptor of a texture that is also
    * sampled: m2v gives the slot one combined binding when only one exists */
   for(unsigned k=0;k<a->rq->iface.nb;k++){const struct air_if_binding *c=&a->rq->iface.b[k];
    if(c->kind==AIRB_COMBINED&&c->stage==a->stage&&c->index==t){if(bt){bt=NULL;break;}bt=c;}}
  }
 } else if(inline_sampler) {
  /* metal2vulkan's Constexpr word is the AIR word without bit 63 */
  for(unsigned k=0;k<a->rq->iface.nb;k++){const struct air_if_binding *c=&a->rq->iface.b[k];
   if(c->kind==AIRB_COMBINED&&c->stage==a->stage&&c->index==t&&c->sampler<0&&(c->word&~(1ull<<63))==(word&~(1ull<<63))){if(bt){bt=NULL;break;}bt=c;}}
 } else {
  bt=air_find(&a->rq->iface,AIRB_COMBINED,a->stage,t,api_sampler);
  if(!bt){bt=air_find(&a->rq->iface,AIRB_TEXTURE,a->stage,t,-1);bs=air_find(&a->rq->iface,AIRB_SAMPLER,a->stage,api_sampler,api_sampler);if(!bs)bt=NULL;}
 }
 if(!bt) {
  char *msg=a->stage==TEGU_GRAPHICS_FRAGMENT?a->rq->deferred:a->rq->reason;
  if(t==31)snprintf(msg,sizeof(a->rq->reason),"air: null texture sampled");
  else snprintf(msg,sizeof(a->rq->reason),"air: texture use without an exact m2v binding (stage %u texture %u sampler %d op %d constexpr 0x%llx)",
   a->stage,t,sampled?(inline_sampler?-1:api_sampler):-2,(int)tex->op,(unsigned long long)word);
  /* A fragment use may die with an unselected output (air_finish_fragment). */
  if(a->stage!=TEGU_GRAPHICS_FRAGMENT)a->why=msg;
  return false;
 }
 const char *why=air_use(a->rq,bt->binding,bt->kind==AIRB_COMBINED?TEGU_MALI_GENERIC_COMBINED:TEGU_MALI_GENERIC_TEXTURE,a->stage);
 if(!why&&bs)why=air_use(a->rq,bs->binding,TEGU_MALI_GENERIC_SAMPLER,a->stage);
 if(why){a->why=why;return false;}
 /* ConstantLanes (sparse textures) must be zero for this texture slot. */
 /* (the runtime stand-in serves the value sets the exact compile accepts:
  * ConstantLanes 0, Presence 1; see tegu_mali_compile_air_pair_options) */
 for(unsigned k=0;k<a->rq->iface.ns&&!a->rq->runtime_spec;k++){const struct air_if_spec *sp=&a->rq->iface.s[k];uint64_t v;
  if(sp->kind==AIRS_CONSTANT_LANES&&sp->stage==a->stage&&sp->index==t&&(!air_spec_value(a->rq,sp,&v)||v)){a->why="air: sparse texture lanes";return false;}}
 /* pixel coordinates */
 bool pixel=false;nir_def *pixel_rt=NULL; /* pixel_rt: the runtime stand-in's word test */
 b->cursor=nir_before_instr(&tex->instr);
 if(sampled&&inline_sampler)pixel=air_decode_sampler_state(word).pixel_coord;
 else if(sampled) {
  const struct air_if_spec *sp=NULL;
  for(unsigned k=0;k<a->rq->iface.ns;k++)
   if(a->rq->iface.s[k].kind==AIRS_SAMPLER_STATE&&a->rq->iface.s[k].stage==a->stage&&a->rq->iface.s[k].index==(unsigned)api_sampler)sp=&a->rq->iface.s[k];
  uint64_t v=0;
  if(tex->op!=nir_texop_txs&&tex->op!=nir_texop_query_levels&&tex->op!=nir_texop_texture_samples) {
   if(!sp||!(sp->fields&AIR_PIXEL_COORDINATES)){a->why="air: sampler coordinate mode not stated by m2v";return false;}
   if(a->rq->runtime_spec) {
    /* the coordinate mode is the bound sampler's word, read at the draw */
    if(tex->sampler_dim!=GLSL_SAMPLER_DIM_CUBE) {
     pixel_rt=nir_i2b(b,nir_iand_imm(b,air_spec_push(b,a->rq,sp),AIR_PIXEL_COORDINATES));pixel=true;
    }
   } else {
    if(!air_spec_value(a->rq,sp,&v)){a->why="air: SamplerState value size";return false;}
    pixel=(v&AIR_PIXEL_COORDINATES)!=0;
   }
  }
 }
 unsigned tbinding=bt->binding,sbinding=bs?bs->binding:bt->binding;
 if(pixel&&tex->op!=nir_texop_txf&&tex->op!=nir_texop_txf_ms&&!nir_tex_instr_is_query(tex)) {
  if(tex->sampler_dim==GLSL_SAMPLER_DIM_CUBE){a->why="air: pixel-coordinate cube sample";return false;}
  unsigned spatial=tex->coord_components-tex->is_array;
  nir_def *size=air_tex_size(b,tex,tbinding,spatial);
  for(unsigned n=0;n<tex->num_srcs;n++) {
   nir_tex_src_type ty=tex->src[n].src_type;
   if(ty!=nir_tex_src_coord&&ty!=nir_tex_src_ddx&&ty!=nir_tex_src_ddy)continue;
   nir_def *c=tex->src[n].src.ssa;
   if(c->bit_size!=32){a->why="air: 16-bit pixel coordinates";return false;}
   nir_def *comp[4];
   for(unsigned k=0;k<c->num_components;k++)
    comp[k]=k<spatial?nir_fdiv(b,nir_channel(b,c,k),nir_channel(b,size,k)):nir_channel(b,c,k);
   nir_def *scaled=nir_vec(b,comp,c->num_components);
   nir_src_rewrite(&tex->src[n].src,pixel_rt?nir_bcsel(b,pixel_rt,scaled,c):scaled);
  }
 }
 nir_tex_instr_add_src(tex,nir_tex_src_texture_handle,nir_imm_int(b,pan_res_handle(TEGU_MALI_GENERIC_TEXTURE_TABLE,tbinding)));
 nir_tex_instr_add_src(tex,nir_tex_src_sampler_handle,nir_imm_int(b,pan_res_handle(TEGU_MALI_GENERIC_SAMPLER_TABLE,sbinding)));
 tex->texture_index=0;tex->sampler_index=0;
 return true;
}
static bool air_adapt_fetch(nir_builder *b,nir_intrinsic_instr *i,struct air_adapt *a) {
 if(i->intrinsic!=nir_intrinsic_load_output||!nir_intrinsic_io_semantics(i).fb_fetch_output)return false;
 unsigned rt=nir_intrinsic_io_semantics(i).location-FRAG_RESULT_DATA0;
 const struct air_if_binding *bb=rt<8?air_find(&a->rq->iface,AIRB_INPUT,TEGU_GRAPHICS_FRAGMENT,rt,-1):NULL;
 if(!bb)return false; /* may die with an unselected output: air_finish_fragment decides */
 if(i->def.num_components!=4||i->def.bit_size!=32){a->why="air: framebuffer fetch shape";return false;}
 nir_variable *var=a->subpass[rt];
 if(!var) {
  const struct glsl_type *t=glsl_image_type(GLSL_SAMPLER_DIM_SUBPASS,false,GLSL_TYPE_FLOAT);
  var=nir_variable_create(b->shader,nir_var_image,t,"air_color_input");
  var->data.descriptor_set=0;var->data.binding=bb->binding;var->data.index=rt;
  var->data.access=ACCESS_NON_WRITEABLE;var->data.image.format=PIPE_FORMAT_NONE;
  a->subpass[rt]=var;
 }
 b->cursor=nir_before_instr(&i->instr);
 nir_deref_instr *d=nir_build_deref_var(b,var);
 nir_def *v=nir_image_deref_load(b,4,32,&d->def,nir_imm_ivec4(b,0,0,0,0),nir_undef(b,1,32),nir_imm_int(b,0),
  .image_dim=GLSL_SAMPLER_DIM_SUBPASS,.dest_type=nir_type_float32,.access=ACCESS_NON_WRITEABLE);
 nir_def_replace(&i->def,v);
 return true;
}
/* 16-bit derivatives: Valhall CLPER moves 32-bit lanes, so a half derivative
 * costs packing (LSHIFT_XOR/MKVEC) per use; compute it in f32 and round, as
 * the m2v SPIR-V does (a half result at least as accurate). */
static bool air_widen_derivative(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)unused;
 switch(i->intrinsic){case nir_intrinsic_ddx:case nir_intrinsic_ddy:case nir_intrinsic_ddx_fine:case nir_intrinsic_ddy_fine:
  case nir_intrinsic_ddx_coarse:case nir_intrinsic_ddy_coarse:break;default:return false;}
 if(i->def.bit_size!=16)return false;
 b->cursor=nir_before_instr(&i->instr);
 nir_def *wide=nir_f2f32(b,i->src[0].ssa);
 nir_def *d;
 switch(i->intrinsic) {
 case nir_intrinsic_ddx:d=nir_ddx(b,wide);break;
 case nir_intrinsic_ddy:d=nir_ddy(b,wide);break;
 case nir_intrinsic_ddx_fine:d=nir_ddx_fine(b,wide);break;
 case nir_intrinsic_ddy_fine:d=nir_ddy_fine(b,wide);break;
 case nir_intrinsic_ddx_coarse:d=nir_ddx_coarse(b,wide);break;
 default:d=nir_ddy_coarse(b,wide);break;
 }
 nir_def_replace(&i->def,nir_f2f16(b,d));
 return true;
}
static bool air_adapt_instr(nir_builder *b,nir_instr *instr,void *data) {
 struct air_adapt *a=data;
 if(a->why)return false;
 if(instr->type==nir_instr_type_tex)return air_adapt_tex(b,nir_instr_as_tex(instr),a);
 if(instr->type!=nir_instr_type_intrinsic)return false;
 nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
 if(i->intrinsic==nir_intrinsic_load_output)return air_adapt_fetch(b,i,a);
 return air_adapt_buffer(b,i,a);
}
/* 16-bit colour outputs to f32: the blend conversion words the encoder builds
 * expect 32-bit float colour registers (as the m2v outputs are). */
static const char *air_widen_outputs(nir_shader *s) {
 nir_foreach_variable_with_modes(var,s,nir_var_shader_out) {
  if(glsl_get_bit_size(glsl_without_array(var->type))!=16)continue;
  if(var->data.location<FRAG_RESULT_DATA0||glsl_type_is_array(var->type))return "air: 16-bit non-colour output";
  if(glsl_get_base_type(glsl_without_array(var->type))!=GLSL_TYPE_FLOAT16)return "air: 16-bit integer colour output";
  var->type=glsl_vec_type(glsl_get_vector_elements(var->type));
 }
 nir_foreach_function_impl(impl,s) {
  nir_builder b=nir_builder_create(impl);
  nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
   if(instr->type==nir_instr_type_deref) {
    nir_deref_instr *d=nir_instr_as_deref(instr);
    if(d->deref_type==nir_deref_type_var&&d->var->data.mode==nir_var_shader_out)d->type=d->var->type;
    continue;
   }
   if(instr->type!=nir_instr_type_intrinsic)continue;
   nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
   if(i->intrinsic==nir_intrinsic_load_deref) {
    nir_variable *var=nir_deref_instr_get_variable(nir_src_as_deref(i->src[0]));
    if(var&&var->data.mode==nir_var_shader_out&&i->def.bit_size!=glsl_get_bit_size(var->type))return "air: colour output read back";
   }
   if(i->intrinsic!=nir_intrinsic_store_deref)continue;
   nir_variable *var=nir_deref_instr_get_variable(nir_src_as_deref(i->src[0]));
   if(!var||var->data.mode!=nir_var_shader_out||i->src[1].ssa->bit_size==glsl_get_bit_size(var->type))continue;
   b.cursor=nir_before_instr(instr);
   nir_src_rewrite(&i->src[1],nir_f2f32(&b,i->src[1].ssa));
  }
  nir_progress(true,impl,nir_metadata_control_flow);
 }
 return NULL;
}
/* air/ places allocas and program-scope variables (QuartzCore's uber-shader
 * state, filled from function constants by air.static_init) in scratch. NIR
 * does not forward scratch stores to loads, so that state would stay memory
 * (TLS) and its selections unfolded, where m2v's SSA form folds them. Forward
 * constant-offset loads from the straight-line entry block's stores when no
 * other store can reach those bytes, then drop scratch that nothing reads. */
static nir_def *air_scratch_bytes(nir_builder *b,nir_intrinsic_instr **own,const uint16_t *ob,unsigned o,unsigned n,
 unsigned comps,unsigned bits) {
 nir_def *bytes[64];
 if(n>64||bits>32||bits<8)return NULL;
 for(unsigned k=0;k<n;k++) {
  nir_intrinsic_instr *st=own[o+k];
  if(!st||st->src[0].ssa->bit_size>32||st->src[0].ssa->bit_size<8)return NULL;
  nir_def *v=st->src[0].ssa;
  bytes[k]=nir_extract_bits(b,&v,1,ob[o+k]*8u,1,8);
 }
 return nir_extract_bits(b,bytes,n,0,comps,bits);
}
static bool air_forward_scratch(nir_shader *s) {
 unsigned size=s->scratch_size;
 if(!size||size>8192)return false;
 nir_function_impl *impl=nir_shader_get_entrypoint(s);
 nir_block *start=nir_start_block(impl);
 uint8_t *outside=calloc(size,1);nir_intrinsic_instr **own=calloc(size,sizeof(*own));uint16_t *ob=calloc(size,sizeof(*ob));
 bool ok=outside&&own&&ob,progress=false;
 nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
  if(!ok||instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(i->intrinsic!=nir_intrinsic_store_scratch)continue;
  if(!nir_src_is_const(i->src[1])){ok=false;break;}
  unsigned o=nir_src_as_uint(i->src[1]),n=i->src[0].ssa->num_components*i->src[0].ssa->bit_size/8;
  if(o>size||n>size-o){ok=false;break;}
  if(block!=start)memset(outside+o,1,n);
 }
 nir_builder b=nir_builder_create(impl);
 for(int pass=0;ok&&pass<2;pass++) {
  /* pass 0: the start block in order (map as of each load); pass 1: every
   * later block against the start block's final map. */
  nir_foreach_block(block,impl) {
   if((pass==0)!=(block==start))continue;
   nir_foreach_instr_safe(instr,block) {
    if(instr->type!=nir_instr_type_intrinsic)continue;
    nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
    if(pass==0&&i->intrinsic==nir_intrinsic_store_scratch) {
     unsigned o=nir_src_as_uint(i->src[1]),bs=i->src[0].ssa->bit_size/8,wm=nir_intrinsic_write_mask(i);
     for(unsigned c=0;c<i->src[0].ssa->num_components;c++)if(wm&(1u<<c))
      for(unsigned k=0;k<bs;k++){own[o+c*bs+k]=i;ob[o+c*bs+k]=(uint16_t)(c*bs+k);}
     continue;
    }
    if(i->intrinsic!=nir_intrinsic_load_scratch||!nir_src_is_const(i->src[0]))continue;
    unsigned o=nir_src_as_uint(i->src[0]),n=i->def.num_components*i->def.bit_size/8;
    if(o>size||n>size-o)continue;
    bool clean=true;
    for(unsigned k=0;k<n;k++)if(!own[o+k]||(pass==1&&outside[o+k]))clean=false;
    if(!clean)continue;
    b.cursor=nir_before_instr(instr);
    nir_def *v=air_scratch_bytes(&b,own,ob,o,n,i->def.num_components,i->def.bit_size);
    if(!v)continue;
    nir_def_replace(&i->def,v);progress=true;
   }
  }
 }
 free(outside);free(own);free(ob);
 if(progress)nir_progress(true,impl,nir_metadata_control_flow);
 return progress;
}
static void air_drop_dead_scratch(nir_shader *s) {
 if(!s->scratch_size)return;
 nir_function_impl *impl=nir_shader_get_entrypoint(s);
 nir_foreach_block(block,impl)nir_foreach_instr(instr,block)
  if(instr->type==nir_instr_type_intrinsic&&nir_instr_as_intrinsic(instr)->intrinsic==nir_intrinsic_load_scratch)return;
 nir_foreach_block(block,impl)nir_foreach_instr_safe(instr,block)
  if(instr->type==nir_instr_type_intrinsic&&nir_instr_as_intrinsic(instr)->intrinsic==nir_intrinsic_store_scratch)nir_instr_remove(instr);
 s->scratch_size=0;nir_progress(true,impl,nir_metadata_control_flow);
}
static const void *air_function_bitcode(void *mem,const struct tegu_mali_air_function *f,size_t *size,const char **err) {
 if(!f->bitcode||!f->size){*err="no bitcode";return NULL;}
 if(f->size>=4&&!memcmp(f->bitcode,"MTLB",4)) {
  struct air_metallib lib;
  if(!f->entry||!air_metallib_parse(mem,f->bitcode,f->size,&lib,err))return NULL;
  for(unsigned i=0;i<lib.num_functions;i++)if(!strcmp(lib.functions[i].name,f->entry)) {
   *size=lib.functions[i].bitcode_size;return lib.functions[i].bitcode;
  }
  *err="no such function in the library";return NULL;
 }
 *size=f->size;return f->bitcode;
}
/* Module facts reused across variants (the uber shader is ~100 KB of bitcode
 * and every draw-time variant names the same module): its stage and its
 * function constants' indices and names, keyed by content hash + entry, so
 * only air_to_nir itself parses the module per compile. */
#define AIR_META_WAYS 16
struct air_meta_entry { blake3_hash key; int stage; unsigned count; uint32_t index[128]; char *names[128]; unsigned used; };
static struct air_meta_entry air_meta[AIR_META_WAYS];static unsigned air_meta_next;
static simple_mtx_t air_meta_lock=SIMPLE_MTX_INITIALIZER;
static bool air_module_facts(void *mem,const void *bc,size_t size,const char *entry,int *stage,unsigned *count,
 uint32_t *index,const char **names) {
 blake3_hash key;
 {struct mesa_blake3 h;_mesa_blake3_init(&h);_mesa_blake3_update(&h,bc,size);
  if(entry)_mesa_blake3_update(&h,entry,strlen(entry)+1);_mesa_blake3_final(&h,key);}
 simple_mtx_lock(&air_meta_lock);
 for(unsigned w=0;w<AIR_META_WAYS;w++) {
  struct air_meta_entry *e=&air_meta[w];
  if(!e->used||memcmp(e->key,key,sizeof(key)))continue;
  *stage=e->stage;*count=e->count;
  for(unsigned i=0;i<e->count;i++){index[i]=e->index[i];names[i]=e->names[i]?ralloc_strdup(mem,e->names[i]):NULL;}
  simple_mtx_unlock(&air_meta_lock);return true;
 }
 simple_mtx_unlock(&air_meta_lock);
 const char *err=NULL;struct air_constant_value *fc=NULL;const char **cn=NULL;
 int st=air_module_stage_for(mem,bc,size,entry,NULL,&err);
 unsigned n=air_module_function_constant_names(mem,bc,size,&fc,&cn);
 if(n>128)return false;
 *stage=st;*count=n;
 for(unsigned i=0;i<n;i++){index[i]=fc[i].index;names[i]=cn?cn[i]:NULL;}
 simple_mtx_lock(&air_meta_lock);
 struct air_meta_entry *e=&air_meta[air_meta_next++%AIR_META_WAYS];
 for(unsigned i=0;i<e->count;i++)free(e->names[i]);
 memcpy(e->key,key,sizeof(key));e->stage=st;e->count=n;e->used=1;
 for(unsigned i=0;i<n;i++){e->index[i]=index[i];e->names[i]=names[i]?strdup(names[i]):NULL;}
 simple_mtx_unlock(&air_meta_lock);
 return true;
}
/* Translated base NIR reused across variants: a pipeline's draw-time variants
 * (sampler facts, render target, tile mode) share the module, its function
 * constants and the varying order, so air_to_nir and the constant-fold walk
 * (~15 ms host for QuartzCore's 100 KB fixed_frag module) run once; each
 * variant clones the folded NIR and adapts it (per-variant facts only). */
#define AIR_BASE_WAYS 8
struct air_base_entry { blake3_hash key;nir_shader *s;void *ctx;struct air_shader_info info;bool used; };
static struct air_base_entry air_base[AIR_BASE_WAYS];static unsigned air_base_next;
static void air_info_keep(void *ctx,struct air_shader_info *dst,const struct air_shader_info *src) {
 *dst=*src;
 dst->resources=ralloc_array(ctx,struct air_resource,src->num_resources+1);
 if(src->num_resources)memcpy(dst->resources,src->resources,src->num_resources*sizeof(*src->resources));
 for(unsigned i=0;i<src->num_resources;i++){dst->resources[i].name=NULL;dst->resources[i].type_name=NULL;}
 dst->varyings=ralloc_array(ctx,struct air_varying,src->num_varyings+1);
 if(src->num_varyings)memcpy(dst->varyings,src->varyings,src->num_varyings*sizeof(*src->varyings));
 for(unsigned i=0;i<src->num_varyings;i++)dst->varyings[i].name=src->varyings[i].name?ralloc_strdup(ctx,src->varyings[i].name):NULL;
 dst->vertex_attributes=NULL;dst->num_vertex_attributes=0;dst->notes=NULL;dst->num_notes=0;dst->entry_point=NULL;
}
/* The folded base NIR of one stage (cached, cloned): air_to_nir with the
 * function constants, the early lowering and the fold/scratch-forward loop.
 * Shared by the per-variant compile (air_translate_stage) and the AIR-derived
 * render plan (mali_graphics_air_plan.inc). why[cap] holds a formatted reason. */
static nir_shader *air_stage_base(char *why,size_t cap,void *mem,const struct tegu_mali_air_function *f,
 mesa_shader_stage stage,const char *const *names,unsigned name_count,struct air_shader_info *info,const char **reason) {
 const char *err=NULL;size_t size=0;
 const void *bc=air_function_bitcode(mem,f,&size,&err);
 if(!bc){snprintf(why,cap,"air: reader: %s",err?err:"?");*reason=why;return NULL;}
 double tp0=air_now();
 int st=-1;unsigned nfc=0;uint32_t cidx[128];const char *cn[128];
 if(!air_module_facts(mem,bc,size,f->entry,&st,&nfc,cidx,cn)){*reason="air: more than 128 function constants";return NULL;}
 double tp1=air_now();
 if(st!=(int)stage){*reason=stage==MESA_SHADER_VERTEX?"air: not a vertex function":"air: not a fragment function";return NULL;}
 struct air_constant_value *fc=ralloc_array(mem,struct air_constant_value,nfc+1);
 for(unsigned i=0;i<nfc;i++)fc[i]=(struct air_constant_value){cidx[i],0};
 for(unsigned i=0;i<nfc;i++)for(uint32_t j=0;j<f->constant_count;j++) {
  const struct tegu_mali_air_constant *c=&f->constants[j];
  bool hit=c->name?(cn[i]&&!strcmp(c->name,cn[i])):c->index==fc[i].index;
  if(!hit)continue;
  uint64_t v=c->bits;if(c->size&&c->size<8)v&=(1ull<<(c->size*8))-1;
  fc[i].bits=v;break;
 }
 double tp2=air_now();
 blake3_hash key;
 {struct mesa_blake3 h;_mesa_blake3_init(&h);_mesa_blake3_update(&h,"AIRBASE1",8);_mesa_blake3_update(&h,bc,size);
  uint32_t st32=(uint32_t)stage;_mesa_blake3_update(&h,&st32,4);
  if(f->entry)_mesa_blake3_update(&h,f->entry,strlen(f->entry)+1);
  for(unsigned i=0;i<nfc;i++){_mesa_blake3_update(&h,&fc[i].index,sizeof(fc[i].index));_mesa_blake3_update(&h,&fc[i].bits,sizeof(fc[i].bits));}
  uint32_t nn=names?name_count:0xffffffffu;_mesa_blake3_update(&h,&nn,4);
  for(unsigned i=0;names&&i<name_count;i++)_mesa_blake3_update(&h,names[i]?names[i]:"",names[i]?strlen(names[i])+1:1);
  _mesa_blake3_final(&h,key);}
 nir_shader *s=NULL;
 simple_mtx_lock(&air_meta_lock);
 for(unsigned w=0;w<AIR_BASE_WAYS;w++)if(air_base[w].used&&!memcmp(air_base[w].key,key,sizeof(key))) {
  s=nir_shader_clone(NULL,air_base[w].s);air_info_keep(mem,info,&air_base[w].info);break;
 }
 simple_mtx_unlock(&air_meta_lock);
 if(s) {
  if(getenv("TEGU_AIR_TIMING"))fprintf(stderr,"TEGU_AIR_TIMING stage=%d base=hit clone_ms=%.2f\n",(int)stage,(air_now()-tp2)*1e3);
  return s;
 }
 {
 struct air_to_nir_options opts={.constants=fc,.num_constants=nfc,.varying_names=names,.num_varying_names=names?name_count:0};
 s=air_to_nir(mem,bc,size,f->entry,&opts,pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),info,&err);
 if(getenv("TEGU_AIR_TIMING"))fprintf(stderr,"TEGU_AIR_TIMING stage=%d bitcode=%zu stage_ms=%.2f constants_ms=%.2f air_to_nir_ms=%.2f\n",
  (int)stage,size,(tp1-tp0)*1e3,(tp2-tp1)*1e3,(air_now()-tp2)*1e3);
 if(!s){snprintf(why,cap,"air: %s",err?err:"translation failed");*reason=why;return NULL;}
 ralloc_steal(NULL,s);
 if(getenv("TEGU_AIR_RAW_PRINT"))nir_print_shader(s,stderr);
 if(info->raster_order_groups){*reason="air: raster order groups";ralloc_free(s);return NULL;}
 if(info->num_imageblock||info->threadgroup_bytes){*reason="air: imageblock/threadgroup memory";ralloc_free(s);return NULL;}
 for(unsigned i=0;i<info->num_resources;i++)
  if(info->resources[i].argbuf_set!=~0u){*reason="air: argument buffer member";ralloc_free(s);return NULL;}
 NIR_PASS(_,s,nir_lower_variable_initializers,nir_var_function_temp);
 NIR_PASS(_,s,nir_lower_returns);NIR_PASS(_,s,nir_inline_functions);nir_remove_non_entrypoints(s);
 /* Vertex outputs the fragment stage does not read become temporaries
  * (ansuz: their computations and the attributes only they need go away). */
 if(stage==MESA_SHADER_VERTEX&&names) {
  bool demoted=false;
  nir_foreach_shader_out_variable(var,s)
   if(var->data.location>=VARYING_SLOT_VAR0&&var->data.location>=VARYING_SLOT_VAR0+(int)name_count){var->data.mode=nir_var_shader_temp;demoted=true;}
  if(demoted){nir_fixup_deref_modes(s);NIR_PASS(_,s,nir_lower_global_vars_to_local);}
 }
 NIR_PASS(_,s,nir_lower_variable_initializers,~0);
 NIR_PASS(_,s,nir_split_var_copies);NIR_PASS(_,s,nir_lower_var_copies);
 NIR_PASS(_,s,nir_lower_global_vars_to_local);NIR_PASS(_,s,nir_lower_vars_to_ssa);
 NIR_PASS(_,s,nir_lower_system_values);
 bool progress;
 do {
  progress=false;
  NIR_PASS(progress,s,nir_opt_constant_folding);NIR_PASS(progress,s,nir_opt_dead_cf);
  NIR_PASS(progress,s,nir_opt_copy_prop);NIR_PASS(progress,s,nir_opt_dce);
  NIR_PASS(progress,s,nir_opt_remove_phis);NIR_PASS(progress,s,nir_opt_algebraic);
  NIR_PASS(progress,s,air_forward_scratch);
 } while(progress);
 air_drop_dead_scratch(s);
 NIR_PASS(_,s,nir_remove_dead_variables,nir_var_shader_in|nir_var_shader_out|nir_var_system_value|nir_var_function_temp,NULL);
 {
  static bool glsl_kept;
  simple_mtx_lock(&air_meta_lock);
  if(!glsl_kept){glsl_type_singleton_init_or_ref();glsl_kept=true;} /* cached NIR keeps glsl types alive */
  struct air_base_entry *e=&air_base[air_base_next++%AIR_BASE_WAYS];
  if(e->used){ralloc_free(e->s);ralloc_free(e->ctx);}
  memset(e,0,sizeof(*e));memcpy(e->key,key,sizeof(key));
  e->s=nir_shader_clone(NULL,s);e->ctx=ralloc_context(NULL);air_info_keep(e->ctx,&e->info,info);e->used=e->s&&e->ctx;
  simple_mtx_unlock(&air_meta_lock);
 }
 }
 return s;
}
static nir_shader *air_translate_stage(struct air_request *rq,void *mem,const struct tegu_mali_air_function *f,
 mesa_shader_stage stage,const char *const *names,unsigned name_count,struct air_shader_info *info,const char **reason) {
 nir_shader *s=air_stage_base(rq->reason,sizeof(rq->reason),mem,f,stage,names,name_count,info,reason);
 if(!s)return NULL;
 /* Vertex attributes: Metal attribute index -> the m2v location of the same
  * (buffer, offset) fetch, which the encoder's vertex fetch programs. */
 if(stage==MESA_SHADER_VERTEX) {
  nir_foreach_shader_in_variable(var,s) {
   unsigned idx=var->data.location-VERT_ATTRIB_GENERIC0;int loc=-1;
   const struct tegu_mali_air_pair *p=rq->pair;
   for(uint32_t k=0;k<p->attribute_count;k++)if(p->attributes[k].index==idx) {
    for(unsigned m=0;m<rq->iface.na;m++)
     if(rq->iface.a[m].buffer_index==p->attributes[k].buffer_index&&rq->iface.a[m].offset==p->attributes[k].offset){
      if(loc>=0&&loc!=(int)rq->iface.a[m].location){loc=-2;break;}
      loc=(int)rq->iface.a[m].location;
     }
   }
   if(loc<0){*reason=loc==-2?"air: ambiguous vertex attribute location":"air: vertex attribute without an m2v location";ralloc_free(s);return NULL;}
   var->data.location=VERT_ATTRIB_GENERIC0+loc;
  }
 }
 if(stage==MESA_SHADER_FRAGMENT) {
  /* Outputs to attachments the pipeline does not have are discarded (Metal;
   * m2v emits none for them). */
  bool demoted=false;
  nir_foreach_shader_out_variable(var,s) {
   if(var->data.location<FRAG_RESULT_DATA0||var->data.location>FRAG_RESULT_DATA7)continue;
   unsigned rt=var->data.location-FRAG_RESULT_DATA0;
   if(rt>=rq->iface.ncolor||!rq->iface.color_format[rt]){var->data.mode=nir_var_shader_temp;demoted=true;}
  }
  if(demoted) {
   nir_fixup_deref_modes(s);NIR_PASS(_,s,nir_lower_global_vars_to_local);NIR_PASS(_,s,nir_lower_vars_to_ssa);
   NIR_PASS(_,s,nir_opt_dce);NIR_PASS(_,s,nir_opt_dead_cf);
   NIR_PASS(_,s,nir_remove_dead_variables,nir_var_shader_temp|nir_var_function_temp,NULL);
  }
  if((*reason=air_widen_outputs(s))){ralloc_free(s);return NULL;}
 }
 struct air_adapt a={.rq=rq,.stage=stage==MESA_SHADER_VERTEX?TEGU_GRAPHICS_VERTEX:TEGU_GRAPHICS_FRAGMENT,.info=info};
 for(unsigned k=0;k<rq->iface.ns&&!rq->runtime_spec;k++){const struct air_if_spec *sp=&rq->iface.s[k];uint64_t v;
  if(sp->kind==AIRS_PRESENCE&&sp->stage==a.stage&&(!air_spec_value(rq,sp,&v)||v!=1)){*reason="air: unbound slot the shader tests (Presence=0)";ralloc_free(s);return NULL;}}
 NIR_PASS(_,s,nir_shader_instructions_pass,air_adapt_instr,nir_metadata_control_flow,&a);
 if(stage==MESA_SHADER_FRAGMENT)NIR_PASS(_,s,nir_shader_intrinsics_pass,air_widen_derivative,nir_metadata_control_flow,NULL);
 if(a.why){*reason=a.why;ralloc_free(s);return NULL;}
 NIR_PASS(_,s,nir_opt_constant_folding);NIR_PASS(_,s,nir_opt_dce);
 NIR_PASS(_,s,nir_split_per_member_structs);
 nir_validate_shader(s,"tegu air adapted");
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 return s;
}
static void air_translate_pair(const struct air_request *cair,nir_shader **vs,nir_shader **fs,int *status,const char **reason) {
 struct air_request *rq=(struct air_request *)cair;
 double t0=air_now();
 *vs=*fs=NULL;*status=2;
 void *mem=ralloc_context(NULL);
 struct air_shader_info vi,fi;memset(&vi,0,sizeof(vi));memset(&fi,0,sizeof(fi));
 *fs=air_translate_stage(rq,mem,&rq->pair->fragment,MESA_SHADER_FRAGMENT,NULL,0,&fi,reason);
 if(*fs) {
  /* Link by name (ansuz): the FS input order is the pipeline's, varying i at
   * VAR0+i in both stages. The FS is retranslated with that list only when
   * its own numbering differs (Bergamota packs past the 12th varying). */
  unsigned count=fi.num_varyings;bool own=true;
  const char **names=ralloc_array(mem,const char *,count+1);
  for(unsigned i=0;i<count;i++) {
   names[i]=fi.varyings[i].name;
   if(fi.varyings[i].slot!=VARYING_SLOT_VAR0+i||fi.varyings[i].component)own=false;
  }
  if(!own) {
   ralloc_free(*fs);rq->used_count=0;memset(&fi,0,sizeof(fi));
   *fs=air_translate_stage(rq,mem,&rq->pair->fragment,MESA_SHADER_FRAGMENT,names,count,&fi,reason);
  }
  if(*fs)*vs=air_translate_stage(rq,mem,&rq->pair->vertex,MESA_SHADER_VERTEX,names,count,&vi,reason);
 }
 /* A fragment input no vertex output names (a pairing Metal itself would
  * reject; the translator's equivalent reads zero): the VS writes zero. */
 if(*vs&&*fs) {
  nir_function_impl *impl=nir_shader_get_entrypoint(*vs);bool added=false;
  nir_foreach_shader_in_variable(fv,*fs) {
   if(fv->data.location<VARYING_SLOT_VAR0)continue;
   bool found=false;
   nir_foreach_shader_out_variable(vv,*vs)if(vv->data.location==fv->data.location)found=true;
   if(found||!glsl_type_is_vector_or_scalar(fv->type))continue;
   nir_variable *nv=nir_variable_create(*vs,nir_var_shader_out,fv->type,"air_unwritten_varying");
   nv->data.location=fv->data.location;nv->data.location_frac=fv->data.location_frac;nv->data.interpolation=fv->data.interpolation;
   nir_builder b=nir_builder_at(nir_after_impl(impl));
   unsigned n=glsl_get_vector_elements(fv->type);
   nir_store_var(&b,nv,nir_imm_zero(&b,n,glsl_get_bit_size(fv->type)),BITFIELD_MASK(n));
   added=true;
  }
  if(added){nir_progress(true,impl,nir_metadata_control_flow);nir_shader_gather_info(*vs,impl);}
 }
 if(!*vs||!*fs){if(*vs)ralloc_free(*vs);if(*fs)ralloc_free(*fs);*vs=*fs=NULL;}
 else *status=5;
 if(getenv("TEGU_AIR_TIMING"))fprintf(stderr,"TEGU_AIR_TIMING translate_cpu_ms=%.2f\n",(air_now()-t0)*1e3);
 /* reasons point into rq or static strings; mem holds only frontend scratch */
 ralloc_free(mem);
}
int tegu_mali_compile_air_pair_options(const struct tegu_mali_air_pair *air,
 const void *vd,size_t vn,const void *fd,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vc_count,const struct tegu_mali_specialization *fc,size_t fc_count,
 const struct tegu_mali_generic_options *options,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,struct tegu_mali_varying_info *varyings,char *error,size_t error_size) {
 if(!air||!valid_span(air,sizeof(*air))||air->version!=TEGU_MALI_AIR_PAIR_VERSION||!options||!valid_span(options,sizeof(*options))||
    (air->attribute_count&&(!air->attributes||air->attribute_count>31||!valid_span(air->attributes,air->attribute_count*sizeof(*air->attributes)))))
  return 1;
 const uint32_t known=TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|TEGU_MALI_GENERIC_OPTION_SELECT_RT|
  TEGU_MALI_GENERIC_OPTION_TILE_FETCH|TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS|TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE|
  TEGU_MALI_GENERIC_OPTION_MULTI_TARGET;
 struct tegu_mali_generic_options o=*options;
 bool select_rt=(o.flags&TEGU_MALI_GENERIC_OPTION_SELECT_RT)!=0,mrt=(o.flags&TEGU_MALI_GENERIC_OPTION_MULTI_TARGET)!=0;
 /* RUNTIME_SPEC (the frontend's draw-time stand-in): the plan's specialization
  * constants are read from push words (air_spec_push) instead of folded, so one
  * compile serves every value set. AIR folds SamplerState's coordinate mode only
  * (a runtime select here); a stand-in serves value sets with ConstantLanes 0 and
  * Presence 1 exactly (the only ones the exact AIR compile accepts too: the
  * frontend's background exact compile refuses the others). Each stage's own
  * push words must end below TEGU_SPEC_PUSH_BASE. */
 bool runtime_spec=(o.flags&TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC)!=0;
 if(runtime_spec&&(vc_count||fc_count))return 1;
 o.flags&=~TEGU_MALI_GENERIC_OPTION_RUNTIME_SPEC;
 struct tegu_mali_generic_options2 o2;memset(&o2,0,sizeof(o2));
 if(o.version==TEGU_MALI_GENERIC_OPTIONS2_VERSION) {
  if(!valid_span(options,sizeof(o2)))return 1;
  memcpy(&o2,options,sizeof(o2));
  if(!mrt||select_rt||!o2.target_count||o2.target_count>TEGU_MALI_GENERIC_MAX_TARGETS||o2.reserved2[0]||o2.reserved2[1]||o2.reserved2[2])return 1;
  for(unsigned i=0;i<TEGU_MALI_GENERIC_MAX_TARGETS;i++)
   if(i<o2.target_count?(o2.target_formats[i]&&!mrt_format_ok(o2.target_formats[i])):o2.target_formats[i]!=0)return 1;
  o.version=TEGU_MALI_GENERIC_OPTIONS_VERSION;
 } else if(mrt)return 1;
 if(o.version!=TEGU_MALI_GENERIC_OPTIONS_VERSION||(o.flags&~known)||o.reserved||(select_rt?o.rt>7:o.rt!=0)||
    ((o.flags&TEGU_MALI_GENERIC_OPTION_TILE_FETCH)&&!select_rt&&!mrt)||
    (((o.flags&TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS)!=0)!=(varyings!=NULL)))return 1;
 unsigned select=(select_rt?o.rt+1:0)|
  ((o.flags&TEGU_MALI_GENERIC_OPTION_TILE_FETCH)?TEGU_GENERIC_COMPILE_TILE:0)|
  ((o.flags&TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS)?TEGU_GENERIC_COMPILE_DRAW_PARAMS:0)|
  ((o.flags&TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE)?TEGU_GENERIC_COMPILE_TLS:0);
 struct air_request *rq=calloc(1,sizeof(*rq));
 if(!rq){fail(error,error_size,"air: allocation failed");return 3;}
 rq->pair=air;rq->vc=vc;rq->fc=fc;rq->vc_count=vc_count;rq->fc_count=fc_count;rq->runtime_spec=runtime_spec;
 void *mem=ralloc_context(NULL);
 const char *why=air_iface_parse(rq,mem);
 ralloc_free(mem);
 for(unsigned i=0;!why&&runtime_spec&&i<rq->iface.nr;i++)
  if(rq->iface.r[i].offset+4>TEGU_SPEC_PUSH_BASE)why="air: runtime specialization: push words reach the specialization base";
 if(why){fail(error,error_size,why);free(rq);return 2;}
 int status=compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,select,error,error_size,vc,vc_count,fc,fc_count,varyings,
  mrt?o2.target_formats:NULL,mrt?o2.target_count:0,rq);
 free(rq);return status;
}
#include "mali_graphics_air_plan.inc"
