/* AIR-direct compute kernels (air-compute-opus): NIR from the vendored
 * ansuz-metal AIR frontend (air/, MIT; see air/NOTICE.md), adapted to the
 * compute ABI the m2v planner already binds (TGP1/TGP2 packet, manifest, FAU
 * buffer records, push and region specialization), then the unchanged backend
 * tail compute_backend(). Included at the end of mali_compute_compiler.c with
 * -DTEGU_MALI_AIR. Contract: mali_compute_compiler.h tegu_mali_compile_compute_air.
 *
 * The m2v metadata JSON of the same translation is the authority for the
 * bindings (set0 binding N of a Metal slot), the SlotBindOffset residuals,
 * the DispatchRegion push field and the meaning of each specialization id;
 * whatever it states that this adapter does not reproduce exactly is refused.
 * Never a silent approximation of a binding.
 *
 * Kernel builtins follow m2v's dispatch contract (metal2vulkan lower/dispatch.rs):
 *  - the Vulkan/Mali workgroup is the region's exact local size (LocalSize X/Y/Z
 *    values), which is also Metal's threads_per_threadgroup for the region;
 *  - MultiRegion 0: the region base is 0, threadgroups_per_grid = NumWorkgroups,
 *    threads_per_grid = NumWorkgroups * local;
 *  - MultiRegion 1: DispatchRegion = {base.xyz, packed nominal L, grid.xyz, 0}:
 *    threadgroup_position_in_grid = base + WorkgroupId, thread_position_in_grid =
 *    (base + WorkgroupId) * L + local id, threads_per_grid = grid,
 *    threadgroups_per_grid = ceil(grid / L);
 *  - Metal simdgroups are 32 lanes of the linear local index (lane = lin & 31,
 *    simdgroup = lin >> 5, ceil(N/32) per threadgroup), as m2v derives them in
 *    every tier. Valhall subgroups are 16 lanes (pan_subgroup_size(9)), so a
 *    kernel with a cross-lane simdgroup operation over more than a quad is
 *    refused rather than run on 16-lane groups.
 */
#include <stdarg.h>
#include "nir_air.h"
#include "air_json.h"

#define CAIR_MAX_BINDINGS 64u
enum { CAIRB_BUFFER=1, CAIRB_COMBINED=2, CAIRB_TEXTURE=3, CAIRB_SAMPLER=4, CAIRB_STORAGE=5 };
enum { CAIRS_LOCAL=1, CAIRS_FLAT, CAIRS_MULTI, CAIRS_TGLEN, CAIRS_LANES, CAIRS_SPARSE, CAIRS_SAMPLER, CAIRS_PRESENCE };
struct cair_binding {
 uint32_t binding,ty,kind,index;
 int32_t sampler;      /* COMBINED/SAMPLER: Metal sampler slot, -1 constexpr */
 uint64_t word;        /* constexpr sampler word 0 */
};
struct cair_slot_tex { uint32_t index,dim,arrayed,ms,numeric,depth; bool valid; };
struct cair_spec { uint32_t id,kind,index,fields,size;uint64_t def; };
struct cair_req {
 struct context *ctx;
 const struct tegu_mali_air_compute *air;
 const struct tegu_mali_specialization *c;size_t cn;
 bool v2;
 uint32_t nb;struct cair_binding b[CAIR_MAX_BINDINGS];
 uint32_t nr;struct { uint32_t index,offset; } r[32];
 uint32_t ns;struct cair_spec s[64];
 struct cair_slot_tex tex[32];
 int region;                 /* DispatchRegion m2v push byte, -1 none */
 bool has_multi;uint32_t multi;
 uint32_t local[3];
 uint32_t tglen0;bool has_tglen0;
 uint32_t static_tg;
 const struct air_shader_info *info;
 nir_def *sg_defs[64];unsigned sg_count; /* Metal simdgroup index defs (lin >> 5): simdgroup-uniform */
 const char *why;
 char reason[256];
};
static const char *cair_fail(struct cair_req *rq,const char *fmt,...) {
 va_list ap;va_start(ap,fmt);vsnprintf(rq->reason,sizeof(rq->reason),fmt,ap);va_end(ap);return rq->reason;
}
static bool cair_slot(const struct aj *o,const char *cls,uint32_t *index) {
 return o&&aj_is(aj_get(o,"class"),cls)&&aj_is(aj_get(o,"stage"),"Kernel")&&aj_u32(aj_get(o,"index"),index);
}
static bool cair_value(const struct cair_req *rq,const struct cair_spec *s,uint64_t *value) {
 *value=s->def;
 for(size_t i=0;i<rq->cn;i++)if(rq->c[i].id==s->id) {
  if(rq->c[i].size!=s->size)return false;
  uint64_t v=0;for(unsigned k=0;k<rq->c[i].size&&k<8;k++)v|=(uint64_t)rq->c[i].data[k]<<(8*k);
  *value=v;
 }
 return true;
}
/* Metal texture type (metal2vulkan serde name) -> manifest dim/arrayed/ms */
static bool cair_texture_type(const struct aj *t,struct cair_slot_tex *o) {
 static const struct { const char *n;uint32_t dim,arr,ms; } map[]={
  {"Type1D",TEGU_PIPELINE2_DIM_1D,0,0},{"Type1DArray",TEGU_PIPELINE2_DIM_1D,1,0},
  {"Type2D",TEGU_PIPELINE2_DIM_2D,0,0},{"Type2DArray",TEGU_PIPELINE2_DIM_2D,1,0},
  {"Type2DMultisample",TEGU_PIPELINE2_DIM_2D,0,1},{"TypeCube",TEGU_PIPELINE2_DIM_CUBE,0,0},
  {"TypeCubeArray",TEGU_PIPELINE2_DIM_CUBE,1,0},{"Type3D",TEGU_PIPELINE2_DIM_3D,0,0},
  {"TypeTextureBuffer",TEGU_PIPELINE2_DIM_BUFFER,0,0},
 };
 for(unsigned i=0;i<ARRAY_SIZE(map);i++)if(aj_is(t,map[i].n)){o->dim=map[i].dim;o->arrayed=map[i].arr;o->ms=map[i].ms;return true;}
 return false;
}
static const char *cair_parse(struct cair_req *rq,void *mem) {
 const struct tegu_mali_air_compute *air=rq->air;
 if(!air->metadata||!air->metadata_size||air->metadata_size>(16u<<20))return "air: m2v compute metadata missing";
 struct ajp P={air->metadata,air->metadata+air->metadata_size,mem};struct aj root;
 if(!aj_val(&P,&root,0)||root.t!=AJ_OBJ)return "air: m2v metadata is not JSON";
 uint64_t u;const struct aj *x;
 if(!aj_u64(aj_get(&root,"schema"),&u)||u!=1)return "air: m2v metadata schema";
 if((x=aj_get(&root,"variant_bits"))&&(!aj_u64(x,&u)||u))return "air: m2v variant compile (sRGB/YCbCr/cubic/cross-class facts) not reproduced";
 if((x=aj_get(&root,"tile_pipeline"))&&x->t!=AJ_NULL)return "air: tile pipeline (imageblock tile dispatch) not adapted yet";
 if((x=aj_get(&root,"constant_blob"))&&x->t!=AJ_NULL)return "air: m2v constant blob";
 const struct aj *in=aj_get(&root,"interface");
 if(!in)return "air: m2v metadata lacks interface";
 if((x=aj_get(in,"needs_handle_translation"))&&x->b)return "air: m2v handle translation";
 if((x=aj_get(in,"needs_pointer_translation"))&&x->b)return "air: m2v pointer translation";
 if((x=aj_get(in,"reaches_command_buffers"))&&x->b)return "air: indirect command buffers";
 if((x=aj_get(in,"threadgroup"))&&x->t==AJ_ARR&&x->n)return "air: m2v threadgroup interface records";
 const struct aj *mi=aj_get(&root,"metal_info");
 if(!mi||!aj_u32(aj_get(mi,"static_threadgroup_memory_length"),&rq->static_tg))return "air: m2v metal_info";
 /* declared texture types (interface.slots) */
 const struct aj *slots=aj_get(in,"slots");
 for(uint32_t i=0;slots&&slots->t==AJ_ARR&&i<slots->n;i++) {
  const struct aj *e=&slots->v[i];uint32_t idx;const char *tn;uint32_t tl;
  if(e->t!=AJ_ARR||e->n!=2)return "air: m2v interface slot";
  const struct aj *body=aj_tag(&e->v[1],&tn,&tl);
  if(!body)return "air: m2v interface slot body";
  if(!aj_tag_is(tn,tl,"Texture"))continue;
  if(!cair_slot(&e->v[0],"Texture",&idx)||idx>=32)return "air: m2v texture slot";
  struct cair_slot_tex *t=&rq->tex[idx];
  if(!cair_texture_type(aj_get(body,"texture_type"),t))return "air: m2v texture type";
  const struct aj *st=aj_get(body,"sampled_type");
  if(aj_is(st,"Float"))t->numeric=TEGU_PIPELINE2_NUMERIC_FLOAT;
  else if(aj_is(st,"Int")||aj_is(st,"Sint"))t->numeric=TEGU_PIPELINE2_NUMERIC_SINT;
  else if(aj_is(st,"Uint"))t->numeric=TEGU_PIPELINE2_NUMERIC_UINT;
  else return "air: m2v texture sampled type";
  t->depth=(x=aj_get(body,"depth_compare"))&&x->t==AJ_BOOL&&x->b;
  t->index=idx;t->valid=true;
 }
 /* bindings */
 const struct aj *sets=aj_get(aj_get(&root,"layout"),"sets");
 if(!sets||sets->t!=AJ_ARR)return "air: m2v layout sets";
 for(uint32_t si=0;si<sets->n;si++) {
  const struct aj *set=&sets->v[si],*bs=aj_get(set,"bindings");uint32_t sn;
  if(!aj_u32(aj_get(set,"set"),&sn)||!bs||bs->t!=AJ_ARR)return "air: m2v set";
  if(bs->n&&sn)return "air: m2v descriptor set other than 0 (heap/argument tables)";
  for(uint32_t bi=0;bi<bs->n;bi++) {
   const struct aj *b=&bs->v[bi];struct cair_binding o={0};uint32_t count;const char *tn;uint32_t tl;
   if(rq->nb>=CAIR_MAX_BINDINGS)return "air: too many m2v bindings";
   if(!aj_u32(aj_get(b,"binding"),&o.binding)||!aj_u32(aj_get(b,"ty"),&o.ty)||!aj_u32(aj_get(b,"count"),&count))return "air: m2v binding";
   if(count!=1)return "air: m2v descriptor array binding";
   const struct aj *src=aj_tag(aj_get(b,"source"),&tn,&tl),*imm=aj_get(b,"immutable_sampler");
   if(!src)return "air: m2v binding source";
   o.sampler=-1;
   if(aj_tag_is(tn,tl,"Buffer")) {
    if(!cair_slot(src,"Buffer",&o.index)||(o.ty!=6&&o.ty!=7))return "air: m2v buffer binding type";
    o.kind=CAIRB_BUFFER;
   } else if(aj_tag_is(tn,tl,"Texture")) {
    if(!cair_slot(src,"Texture",&o.index)||(o.ty!=2&&o.ty!=3))return "air: m2v texture binding type (texel buffer)";
    o.kind=o.ty==3?CAIRB_STORAGE:CAIRB_TEXTURE;
   } else if(aj_tag_is(tn,tl,"Sampler")) {
    if(!cair_slot(src,"Sampler",&o.index)||o.ty!=0||o.index>15)return "air: m2v sampler binding";
    o.kind=CAIRB_SAMPLER;o.sampler=(int32_t)o.index;
   } else if(aj_tag_is(tn,tl,"CombinedTextureSampler")) {
    const struct aj *cs=aj_get(src,"sampler"),*k;const char *sn2;uint32_t sl;
    if(!cair_slot(aj_get(src,"texture"),"Texture",&o.index)||o.ty!=1||!cs)return "air: m2v combined binding";
    if(((k=aj_get(cs,"raw"))&&k->b)||((k=aj_get(cs,"cube_gradient"))&&k->b))return "air: m2v raw/cube-gradient sampler use";
    const struct aj *sv=aj_tag(aj_get(cs,"sampler"),&sn2,&sl);
    if(!sv)return "air: m2v combined sampler";
    if(aj_tag_is(sn2,sl,"Slot")){uint32_t s;if(!aj_u32(sv,&s)||s>15)return "air: m2v sampler slot";o.sampler=(int32_t)s;}
    else if(aj_tag_is(sn2,sl,"Constexpr")){if(sv->t!=AJ_ARR||sv->n<1||!aj_u64(&sv->v[0],&o.word))return "air: m2v constexpr sampler";}
    else return cair_fail(rq,"air: m2v combined sampler form %.*s",(int)sl,sn2);
    if((o.sampler<0)!=(imm&&imm->t!=AJ_NULL))return "air: m2v immutable sampler mismatch";
    o.kind=CAIRB_COMBINED;
   } else return cair_fail(rq,"air: m2v binding source %.*s",(int)tl,tn);
   if(o.binding>TEGU_PIPELINE2_MAX_BINDING)return "air: m2v binding above the TGP2 range";
   if(o.kind!=CAIRB_COMBINED&&imm&&imm->t!=AJ_NULL)return "air: m2v immutable sampler on a non-combined binding";
   rq->b[rq->nb++]=o;
  }
 }
 /* push fields */
 rq->region=-1;
 const struct aj *push=aj_get(in,"push");
 for(uint32_t pi=0;push&&push->t==AJ_ARR&&pi<push->n;pi++) {
  const struct aj *fields=aj_get(&push->v[pi],"fields");x=aj_get(&push->v[pi],"argument_table_size");
  if(x&&(!aj_u64(x,&u)||u))return "air: m2v argument table";
  if(!aj_is(aj_get(&push->v[pi],"stage"),"Kernel"))return "air: m2v push stage";
  for(uint32_t fi=0;fields&&fields->t==AJ_ARR&&fi<fields->n;fi++) {
   const struct aj *fd=&fields->v[fi],*pl,*fv=aj_get(fd,"field");const char *tn="?",*ln;uint32_t tl=1,ll,size,off;
   const struct aj *field=fv&&fv->t==AJ_OBJ?aj_tag(fv,&tn,&tl):NULL;
   if(fv&&fv->t==AJ_STR){tn=fv->s;tl=fv->len;}
   const struct aj *loc=aj_tag(aj_get(fd,"loc"),&ln,&ll);
   if(!loc||!aj_tag_is(ln,ll,"Push")||!(pl=aj_get(loc,"offset"))||!aj_u32(pl,&off)||!aj_u32(aj_get(fd,"size"),&size)||
      off>128||size>128-off||off%4)return "air: m2v push field placement";
   if(aj_tag_is(tn,tl,"DispatchRegion")) {
    if(size!=32||rq->region>=0)return "air: m2v DispatchRegion field";
    rq->region=(int)off;continue;
   }
   if(aj_tag_is(tn,tl,"SlotBindOffset")) {
    uint32_t idx;
    if(!field||!cair_slot(field,"Buffer",&idx)||size!=4)return "air: m2v SlotBindOffset field";
    if(rq->nr>=32)return "air: too many m2v push fields";
    rq->r[rq->nr].index=idx;rq->r[rq->nr].offset=off;rq->nr++;continue;
   }
   /* Planner-written texture facts the m2v code reads to emulate format,
    * level and channel queries. The AIR code asks the descriptor instead and
    * never reads them; the planner keeps writing them. */
   if(aj_tag_is(tn,tl,"Specialized")||aj_tag_is(tn,tl,"TextureHalfChannels")||aj_tag_is(tn,tl,"TextureLevels")||
      aj_tag_is(tn,tl,"TextureIntegerChannels"))continue;
   return cair_fail(rq,"air: m2v push field %.*s",(int)tl,tn);
  }
 }
 /* specialization keys */
 const struct aj *cs=aj_get(aj_get(&root,"specialization"),"constants");
 for(uint32_t ci=0;cs&&cs->t==AJ_ARR&&ci<cs->n;ci++) {
  const struct aj *c=&cs->v[ci],*kv=aj_get(c,"key");struct cair_spec o={0};const char *kn="?";uint32_t kl=1;
  const struct aj *key=kv&&kv->t==AJ_OBJ?aj_tag(kv,&kn,&kl):NULL;
  if(kv&&kv->t==AJ_STR){kn=kv->s;kl=kv->len;}
  if(!aj_u32(aj_get(c,"id"),&o.id)||!aj_u32(aj_get(c,"size"),&o.size)||!aj_u64(aj_get(c,"default"),&o.def)||o.size!=4)
   return "air: m2v specialization constant";
  if(aj_tag_is(kn,kl,"LocalSize")) {
   o.kind=CAIRS_LOCAL;
   if(aj_is(key,"X"))o.index=0;else if(aj_is(key,"Y"))o.index=1;else if(aj_is(key,"Z"))o.index=2;
   else return "air: m2v LocalSize axis";
  } else if(aj_tag_is(kn,kl,"FlatLocalSize"))o.kind=CAIRS_FLAT;
  else if(aj_tag_is(kn,kl,"MultiRegion"))o.kind=CAIRS_MULTI;
  else if(aj_tag_is(kn,kl,"TgLength")){o.kind=CAIRS_TGLEN;if(!aj_u32(key,&o.index)||o.index>30)return "air: m2v TgLength key";}
  else if(aj_tag_is(kn,kl,"ConstantLanes")){o.kind=CAIRS_LANES;if(!cair_slot(key,"Texture",&o.index))return "air: m2v ConstantLanes key";}
  else if(aj_tag_is(kn,kl,"SparseTexture")){o.kind=CAIRS_SPARSE;if(!cair_slot(key,"Texture",&o.index))return "air: m2v SparseTexture key";}
  else if(aj_tag_is(kn,kl,"SamplerState")) {
   if(!aj_u32(aj_get(key,"fields"),&o.fields)||!cair_slot(aj_get(key,"slot"),"Sampler",&o.index))return "air: m2v SamplerState key";
   o.kind=CAIRS_SAMPLER;
  } else if(aj_tag_is(kn,kl,"Presence")) {
   const char *cls=aj_is(aj_get(key,"class"),"Buffer")?"Buffer":aj_is(aj_get(key,"class"),"Texture")?"Texture":"Sampler";
   if(!cair_slot(key,cls,&o.index))return "air: m2v Presence key";
   o.kind=CAIRS_PRESENCE;
  } else return cair_fail(rq,"air: m2v specialization key %.*s",(int)kl,kn);
  if(rq->ns>=64)return "air: too many m2v specialization constants";
  rq->s[rq->ns++]=o;
 }
 /* every supplied value names a declared id of its size (the SPIR-V entry's
  * specialization check, against the metadata instead of the module) */
 for(size_t i=0;i<rq->cn;i++) {
  bool found=false;
  if(rq->c[i].size!=1&&rq->c[i].size!=2&&rq->c[i].size!=4&&rq->c[i].size!=8)return "air: invalid specialization scalar size";
  for(size_t j=0;j<i;j++)if(rq->c[j].id==rq->c[i].id)return "air: duplicate specialization id";
  for(uint32_t k=0;k<rq->ns;k++)if(rq->s[k].id==rq->c[i].id){found=true;if(rq->s[k].size!=rq->c[i].size)return "air: specialization byte size differs from its declared type";}
  if(!found)return cair_fail(rq,"air: specialization id %u is not declared by the m2v metadata",rq->c[i].id);
 }
 rq->local[0]=rq->local[1]=rq->local[2]=1;
 for(uint32_t k=0;k<rq->ns;k++) {
  const struct cair_spec *s=&rq->s[k];uint64_t v;
  if(!cair_value(rq,s,&v))return "air: specialization value size";
  switch(s->kind) {
  case CAIRS_LOCAL:if(!v||v>1024)return "air: region local size";rq->local[s->index]=(uint32_t)v;break;
  case CAIRS_MULTI:if(v>1)return "air: MultiRegion value";rq->has_multi=true;rq->multi=(uint32_t)v;break;
  case CAIRS_TGLEN:
   if(v>32768||v%16)return "air: threadgroup memory length";
   if(s->index==0){rq->tglen0=(uint32_t)v;rq->has_tglen0=true;}
   else if(v)return "air: threadgroup buffer at an index other than 0";
   break;
  case CAIRS_PRESENCE:if(v!=1)return "air: unbound slot the kernel tests (Presence=0)";break;
  case CAIRS_LANES:case CAIRS_SPARSE:if(v)return "air: sparse texture lanes";break;
  default:break;
  }
 }
 if(rq->has_multi&&rq->multi&&rq->region<0)return "air: MultiRegion without a DispatchRegion push field";
 return NULL;
}
static const struct cair_binding *cair_find(const struct cair_req *rq,unsigned kind,unsigned index,int sampler) {
 const struct cair_binding *hit=NULL;
 for(unsigned i=0;i<rq->nb;i++) {
  const struct cair_binding *b=&rq->b[i];
  if(b->kind!=kind||b->index!=index)continue;
  if((kind==CAIRB_COMBINED||kind==CAIRB_SAMPLER)&&b->sampler!=sampler)continue;
  if(hit)return NULL; /* ambiguous */
  hit=b;
 }
 return hit;
}
static int cair_residual(const struct cair_req *rq,unsigned slot) {
 for(unsigned k=0;k<rq->nr;k++)if(rq->r[k].index==slot)return (int)rq->r[k].offset;
 return -1;
}
/* m2v push byte p is FAU byte 128+p (compute_compiler app_push). */
static nir_def *cair_push(nir_builder *b,unsigned comps,unsigned byte) {
 return nir_load_push_constant(b,comps,32,nir_imm_int(b,128+byte));
}
static nir_def *cair_record(nir_builder *b,struct cair_req *rq,unsigned binding) {
 if(binding<8) {
  rq->ctx->bindings|=1u<<binding;
  return nir_load_push_constant(b,4,32,nir_imm_int(b,binding*16));
 }
#if COMPUTE_BUFFER_TABLE
 nir_def *t=nir_load_push_constant(b,4,32,nir_imm_int(b,TEGU_PIPELINE2_BUFFER_TABLE_FAU));
 nir_def *base=nir_pack_64_2x32(b,nir_channels(b,t,0x3));
 nir_def *v=nir_load_global_constant_bounded(b,4,32,base,nir_imm_int(b,binding*TEGU_PIPELINE2_BUFFER_TABLE_RECORD),
  nir_channel(b,t,2),.align_mul=16,.align_offset=0);
 rq->ctx->ext_bindings|=1u<<binding;
 return v;
#else
 return NULL;
#endif
}
/* ---- buffers: Metal slot -> m2v binding N record, residual + offset, guarded
 * global access (the 64bit_bounded_global semantics of the SPIR-V path) ---- */
static bool cair_buffer(nir_builder *b,nir_intrinsic_instr *i,struct cair_req *rq) {
 unsigned isrc,osrc,access;
 switch(i->intrinsic) {
 case nir_intrinsic_load_ubo:case nir_intrinsic_load_ssbo:isrc=0;osrc=1;access=TEGU_PIPELINE2_ACCESS_READ;break;
 case nir_intrinsic_store_ssbo:isrc=1;osrc=2;access=TEGU_PIPELINE2_ACCESS_WRITE;break;
 case nir_intrinsic_ssbo_atomic:case nir_intrinsic_ssbo_atomic_swap:
  isrc=0;osrc=1;access=TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC;break;
 case nir_intrinsic_get_ssbo_size:if(!rq->why){rq->why="air: buffer length query";}return false;
 case nir_intrinsic_load_constant:if(!rq->why){rq->why="air: shader constant data (needs a constant-blob binding)";}return false;
 case nir_intrinsic_load_global:case nir_intrinsic_store_global:case nir_intrinsic_load_global_constant:
 case nir_intrinsic_global_atomic:case nir_intrinsic_global_atomic_swap:
  if(!rq->why){rq->why="air: global (device address) access";}return false;
 default:return false;
 }
 if(!nir_src_is_const(i->src[isrc])){if(!rq->why)rq->why="air: dynamic buffer index";return false;}
 unsigned slot=nir_src_as_uint(i->src[isrc]);
 const struct cair_binding *bb=cair_find(rq,CAIRB_BUFFER,slot,-1);
 int res=cair_residual(rq,slot);
 if(!bb||res<0){if(!rq->why)rq->why=cair_fail(rq,"air: %s (buffer %u)",bb?"buffer without an m2v SlotBindOffset residual":"buffer slot without an m2v binding",slot);return false;}
 unsigned kind=bb->ty==6?TEGU_PIPELINE2_KIND_UBO:TEGU_PIPELINE2_KIND_SSBO;
 if(kind==TEGU_PIPELINE2_KIND_UBO&&access!=TEGU_PIPELINE2_ACCESS_READ){if(!rq->why)rq->why="air: write to a buffer the m2v plan binds as uniform";return false;}
 if(bb->binding>=COMPUTE_MAX_BUFFERS||(bb->binding>=8&&!rq->v2)){if(!rq->why)rq->why=rq->v2?"air: buffer binding beyond the extended table":"TGP1 buffers require set0 single descriptors0..7";return false;}
 if(access&TEGU_PIPELINE2_ACCESS_ATOMIC) {
  if(!compute_atomic_supported(rq->ctx,i)){if(!rq->why)rq->why=rq->ctx->error&&rq->ctx->error_size?rq->ctx->error:"air: atomic";return false;}
  if(!rq->v2&&!compute_native_atomic(&i->instr,NULL)){if(!rq->why)rq->why="TGP1 float atomic arithmetic requires the TGP2 CAS lowering";return false;}
 }
 unsigned table=bb->binding<8?TEGU_PIPELINE2_TABLE_FAU:TEGU_PIPELINE2_TABLE_BUFFER;
 if(!compute_record(rq->ctx,(struct tegu_pipeline_manifest){.binding=bb->binding,.kind=kind,.array_count=1,
   .access=access,.table=table,.entry=bb->binding})){if(!rq->why)rq->why=rq->ctx->error;return false;}
 b->cursor=nir_before_instr(&i->instr);
 nir_def *rec=cair_record(b,rq,bb->binding);
 if(!rec){if(!rq->why)rq->why="air: buffer table unavailable";return false;}
 nir_def *off=nir_iadd(b,i->src[osrc].ssa,cair_push(b,1,(unsigned)res));
 nir_def *val=i->intrinsic==nir_intrinsic_store_ssbo?i->src[0].ssa:&i->def;
 unsigned bytes=val->num_components*val->bit_size/8;
 if(i->intrinsic==nir_intrinsic_ssbo_atomic||i->intrinsic==nir_intrinsic_ssbo_atomic_swap)bytes=i->def.bit_size/8;
 nir_def *size=nir_channel(b,rec,2);
 nir_def *valid=nir_iand(b,nir_uge_imm(b,size,bytes),nir_uge(b,nir_iadd_imm(b,size,-(int64_t)bytes),off));
 /* Descriptor bases are 16-aligned and congruent to the Metal address
  * (metal2vulkan bind/instantiate.rs descriptor_range), so the authored
  * alignment holds up to 16 bytes. */
 unsigned mul=1,aoff=0;
 if(nir_intrinsic_has_align_mul(i)){mul=MIN2(nir_intrinsic_align_mul(i),16);aoff=nir_intrinsic_align_offset(i)%mul;}
 nir_push_if(b,valid);
 nir_def *addr=nir_iadd(b,nir_pack_64_2x32(b,nir_channels(b,rec,0x3)),nir_u2u64(b,off));
 nir_def *r=NULL;
 switch(i->intrinsic) {
 case nir_intrinsic_load_ubo:case nir_intrinsic_load_ssbo:
  r=nir_load_global(b,i->def.num_components,i->def.bit_size,addr,.align_mul=mul,.align_offset=aoff);break;
 case nir_intrinsic_store_ssbo:
  nir_store_global(b,i->src[0].ssa,addr,.align_mul=mul,.align_offset=aoff,.write_mask=nir_intrinsic_write_mask(i));break;
 case nir_intrinsic_ssbo_atomic:
  r=nir_global_atomic(b,i->def.bit_size,addr,i->src[2].ssa,.atomic_op=nir_intrinsic_atomic_op(i));break;
 default:
  r=nir_global_atomic_swap(b,i->def.bit_size,addr,i->src[2].ssa,i->src[3].ssa,.atomic_op=nir_intrinsic_atomic_op(i));break;
 }
 nir_push_else(b,NULL);
 nir_def *zero=r?nir_imm_zero(b,r->num_components,r->bit_size):NULL;
 nir_pop_if(b,NULL);
 if(r)nir_def_rewrite_uses(&i->def,nir_if_phi(b,r,zero));
 nir_instr_remove(&i->instr);
 return true;
}
/* ---- textures and samplers ---- */
/* shape from the m2v declaration of the slot (what the SPIR-V declares); the
 * depth bit from the use, as compute_opaque_record takes it */
static bool cair_shape(struct cair_req *rq,unsigned slot,bool shadow,uint32_t *shape) {
 if(slot>=32||!rq->tex[slot].valid){rq->why=cair_fail(rq,"air: texture %u without an m2v declaration",slot);return false;}
 const struct cair_slot_tex *t=&rq->tex[slot];
 *shape=t->dim|TEGU_PIPELINE2_SHAPE_NUMERIC(t->numeric)|(t->arrayed?TEGU_PIPELINE2_SHAPE_ARRAYED:0)|
  (t->ms?TEGU_PIPELINE2_SHAPE_MULTISAMPLE:0)|(shadow?TEGU_PIPELINE2_SHAPE_DEPTH_COMPARE:0);
 return true;
}
static bool cair_record_texture(struct cair_req *rq,const struct cair_binding *bt,bool shadow) {
 struct tegu_pipeline_manifest m={.binding=bt->binding,.kind=TEGU_PIPELINE2_KIND_TEXTURE,.array_count=1,
  .access=TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE,.table=TEGU_PIPELINE2_TABLE_TEXTURE,.entry=bt->binding};
 if(!cair_shape(rq,bt->index,shadow,&m.shape))return false;
 if(bt->kind==CAIRB_COMBINED)m.flags|=TEGU_PIPELINE2_FLAG_DECLARED_COMBINED;
 if(!tegu_pipeline2_shape_check(m.kind,m.shape,0)){rq->why="TGP2 image shape or declared format is incompatible with its scalar type";return false;}
 if(!compute_record(rq->ctx,m)){rq->why=rq->ctx->error;return false;}
 if(bt->kind==CAIRB_COMBINED) {
  struct tegu_pipeline_manifest s={.binding=bt->binding,.kind=TEGU_PIPELINE2_KIND_SAMPLER,.array_count=1,
   .table=TEGU_PIPELINE2_TABLE_SAMPLER,.entry=bt->binding,.flags=TEGU_PIPELINE2_FLAG_DECLARED_COMBINED};
  if(!compute_record(rq->ctx,s)){rq->why=rq->ctx->error;return false;}
 }
 return true;
}
static bool cair_record_sampler(struct cair_req *rq,const struct cair_binding *b,bool shadow) {
 struct tegu_pipeline_manifest m={.binding=b->binding,.kind=TEGU_PIPELINE2_KIND_SAMPLER,.array_count=1,
  .table=TEGU_PIPELINE2_TABLE_SAMPLER,.entry=b->binding};
 if(b->kind==CAIRB_COMBINED)m.flags|=TEGU_PIPELINE2_FLAG_DECLARED_COMBINED;
 if(shadow)m.flags|=TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE;
 unsigned mode=shadow?2:1;
 rq->ctx->sampler_modes[b->binding]|=mode;
 if(rq->ctx->sampler_modes[b->binding]==3){rq->why="TGP2 sampler binding is used both with and without depth comparison";return false;}
 if(!compute_record(rq->ctx,m)){rq->why=rq->ctx->error;return false;}
 return true;
}
static nir_def *cair_tex_size(nir_builder *b,nir_tex_instr *tex,unsigned binding,unsigned comps) {
 nir_tex_instr *q=nir_tex_instr_create(b->shader,3);
 q->op=nir_texop_txs;q->sampler_dim=tex->sampler_dim;q->is_array=tex->is_array;q->dest_type=nir_type_int32;
 q->coord_components=0;
 q->src[0]=nir_tex_src_for_ssa(nir_tex_src_texture_handle,nir_imm_int(b,pan_res_handle(TEGU_PIPELINE2_TABLE_TEXTURE,binding)));
 q->src[1]=nir_tex_src_for_ssa(nir_tex_src_sampler_handle,nir_imm_int(b,pan_res_handle(TEGU_PIPELINE2_TABLE_SAMPLER,TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY)));
 q->src[2]=nir_tex_src_for_ssa(nir_tex_src_lod,nir_imm_int(b,0));
 nir_def_init(&q->instr,&q->def,nir_tex_instr_dest_size(q),32);
 nir_builder_instr_insert(b,&q->instr);
 return nir_i2f32(b,nir_trim_vector(b,&q->def,comps));
}
static bool cair_tex(nir_builder *b,nir_tex_instr *tex,struct cair_req *rq) {
 if(nir_tex_instr_src_index(tex,nir_tex_src_texture_handle)>=0)return false;
 if(!rq->v2){rq->why="compute texture operations require a texture/sampler descriptor ABI; TGP1 supports buffers only";return false;}
 const struct air_shader_info *info=rq->info;
 unsigned t=tex->texture_index,s=tex->sampler_index;
 bool real31=false;
 for(unsigned k=0;k<info->num_resources;k++)
  if((info->resources[k].kind==AIR_RESOURCE_TEXTURE||info->resources[k].kind==AIR_RESOURCE_IMAGE)&&info->resources[k].index==31)real31=true;
 if(t==31&&!real31) { /* air/'s null texture: Metal reads zero, as m2v emits */
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
  if(!res){rq->why="air: sampler without a resource";return false;}
  if(res->inline_sampler){inline_sampler=true;word=res->sampler_state;}
  else api_sampler=(int)s;
 }
 const struct cair_binding *bt=NULL,*bs=NULL;
 if(!sampled) {
  bt=cair_find(rq,CAIRB_TEXTURE,t,-1);
  if(!bt)for(unsigned k=0;k<rq->nb;k++){const struct cair_binding *c=&rq->b[k];
   if(c->kind==CAIRB_COMBINED&&c->index==t){if(bt){bt=NULL;break;}bt=c;}}
 } else if(inline_sampler) {
  for(unsigned k=0;k<rq->nb;k++){const struct cair_binding *c=&rq->b[k];
   if(c->kind==CAIRB_COMBINED&&c->index==t&&c->sampler<0&&(c->word&~(1ull<<63))==(word&~(1ull<<63))){if(bt){bt=NULL;break;}bt=c;}}
 } else {
  bt=cair_find(rq,CAIRB_COMBINED,t,api_sampler);
  if(!bt){bt=cair_find(rq,CAIRB_TEXTURE,t,-1);bs=cair_find(rq,CAIRB_SAMPLER,api_sampler,api_sampler);if(!bs)bt=NULL;}
 }
 if(!bt){rq->why=cair_fail(rq,"air: texture use without an exact m2v binding (texture %u sampler %d op %d)",t,sampled?(inline_sampler?-1:api_sampler):-2,(int)tex->op);return false;}
 for(unsigned k=0;k<rq->ns;k++){const struct cair_spec *sp=&rq->s[k];uint64_t v;
  if((sp->kind==CAIRS_LANES||sp->kind==CAIRS_SPARSE)&&sp->index==t&&(!cair_value(rq,sp,&v)||v)){rq->why="air: sparse texture lanes";return false;}}
 if(!cair_record_texture(rq,bt,tex->is_shadow))return false;
 bool pixel=false;
 if(sampled) {
  if(!cair_record_sampler(rq,bs?bs:bt,tex->is_shadow))return false;
  if(inline_sampler)pixel=air_decode_sampler_state(word).pixel_coord;
  else if(tex->op!=nir_texop_txs&&tex->op!=nir_texop_query_levels&&tex->op!=nir_texop_texture_samples) {
   const struct cair_spec *sp=NULL;uint64_t v=0;
   for(unsigned k=0;k<rq->ns;k++)if(rq->s[k].kind==CAIRS_SAMPLER&&rq->s[k].index==(unsigned)api_sampler)sp=&rq->s[k];
   if(!sp||!(sp->fields&1u)){rq->why="air: sampler coordinate mode not stated by m2v";return false;}
   if(!cair_value(rq,sp,&v)){rq->why="air: SamplerState value size";return false;}
   pixel=(v&1u)!=0;
  }
 } else if(!compute_dummy_sampler(rq->ctx)){rq->why=rq->ctx->error;return false;}
 b->cursor=nir_before_instr(&tex->instr);
 if(pixel&&tex->op!=nir_texop_txf&&tex->op!=nir_texop_txf_ms&&!nir_tex_instr_is_query(tex)) {
  if(tex->sampler_dim==GLSL_SAMPLER_DIM_CUBE){rq->why="air: pixel-coordinate cube sample";return false;}
  if(!compute_dummy_sampler(rq->ctx)){rq->why=rq->ctx->error;return false;}
  unsigned spatial=tex->coord_components-tex->is_array;
  nir_def *size=cair_tex_size(b,tex,bt->binding,spatial);
  for(unsigned n=0;n<tex->num_srcs;n++) {
   nir_tex_src_type ty=tex->src[n].src_type;
   if(ty!=nir_tex_src_coord&&ty!=nir_tex_src_ddx&&ty!=nir_tex_src_ddy)continue;
   nir_def *c=tex->src[n].src.ssa;
   if(c->bit_size!=32){rq->why="air: 16-bit pixel coordinates";return false;}
   nir_def *comp[4];
   for(unsigned k=0;k<c->num_components;k++)
    comp[k]=k<spatial?nir_fdiv(b,nir_channel(b,c,k),nir_channel(b,size,k)):nir_channel(b,c,k);
   nir_src_rewrite(&tex->src[n].src,nir_vec(b,comp,c->num_components));
  }
 }
 unsigned sbinding=sampled?(bs?bs->binding:bt->binding):TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY;
 nir_tex_instr_add_src(tex,nir_tex_src_texture_handle,nir_imm_int(b,pan_res_handle(TEGU_PIPELINE2_TABLE_TEXTURE,bt->binding)));
 nir_tex_instr_add_src(tex,nir_tex_src_sampler_handle,nir_imm_int(b,pan_res_handle(TEGU_PIPELINE2_TABLE_SAMPLER,sbinding)));
 tex->texture_index=0;tex->sampler_index=0;
 return true;
}
/* ---- storage images (Metal access::write / read_write textures) ---- */
static bool cair_image(nir_builder *b,nir_intrinsic_instr *i,struct cair_req *rq) {
 unsigned access=TEGU_PIPELINE2_ACCESS_STORAGE;
 switch(i->intrinsic) {
 case nir_intrinsic_image_load:case nir_intrinsic_image_size:case nir_intrinsic_image_samples:
  access|=TEGU_PIPELINE2_ACCESS_READ;break;
 case nir_intrinsic_image_store:access|=TEGU_PIPELINE2_ACCESS_WRITE;break;
 case nir_intrinsic_image_atomic:case nir_intrinsic_image_atomic_swap:
  access|=TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC;break;
 case nir_intrinsic_image_levels:rq->why="air: storage image level count query";return false;
 default:
  if(nir_intrinsic_has_image_dim(i)){rq->why=cair_fail(rq,"air: image operation %s",nir_intrinsic_infos[i->intrinsic].name);return false;}
  return false;
 }
 if(!rq->v2){rq->why="compute image operations require an image descriptor ABI; TGP1 supports buffers only";return false;}
 if(!nir_src_is_const(i->src[0])){rq->why="air: dynamic image index";return false;}
 unsigned slot=nir_src_as_uint(i->src[0]);
 const struct cair_binding *bt=cair_find(rq,CAIRB_STORAGE,slot,-1);
 if(!bt){rq->why=cair_fail(rq,"air: image %u without an m2v storage-image binding",slot);return false;}
 struct tegu_pipeline_manifest m={.binding=bt->binding,.kind=TEGU_PIPELINE2_KIND_IMAGE,.array_count=1,
  .access=access,.table=TEGU_PIPELINE2_TABLE_TEXTURE,.entry=bt->binding};
 if(!cair_shape(rq,slot,false,&m.shape))return false;
 const struct cair_slot_tex *td=&rq->tex[slot];
 enum glsl_sampler_dim dim=nir_intrinsic_image_dim(i);
 unsigned want=dim==GLSL_SAMPLER_DIM_1D?TEGU_PIPELINE2_DIM_1D:dim==GLSL_SAMPLER_DIM_2D||dim==GLSL_SAMPLER_DIM_MS?TEGU_PIPELINE2_DIM_2D:
  dim==GLSL_SAMPLER_DIM_3D?TEGU_PIPELINE2_DIM_3D:dim==GLSL_SAMPLER_DIM_CUBE?TEGU_PIPELINE2_DIM_CUBE:dim==GLSL_SAMPLER_DIM_BUF?TEGU_PIPELINE2_DIM_BUFFER:99;
 if(want!=td->dim||nir_intrinsic_image_array(i)!=(bool)td->arrayed||(dim==GLSL_SAMPLER_DIM_MS)!=(bool)td->ms)
  {rq->why="TGP2 image operation shape differs from its declaration";return false;}
 if(!tegu_pipeline2_shape_check(m.kind,m.shape,0)){rq->why="TGP2 image shape or declared format is incompatible with its scalar type";return false;}
 if(!compute_record(rq->ctx,m)){rq->why=rq->ctx->error;return false;}
 if(i->intrinsic==nir_intrinsic_image_samples&&dim!=GLSL_SAMPLER_DIM_MS){rq->why="TGP2 image sample-count query requires a multisample declaration";return false;}
 int lod_src=i->intrinsic==nir_intrinsic_image_load?3:i->intrinsic==nir_intrinsic_image_store?4:i->intrinsic==nir_intrinsic_image_size?1:-1;
 if(lod_src>=0&&(!nir_src_is_const(i->src[lod_src])||nir_src_as_uint(i->src[lod_src])))
  {rq->why="TGP2 storage image nonzero or dynamic LOD requires a mip-aware image lowering";return false;}
 b->cursor=nir_before_instr(&i->instr);
 nir_src_rewrite(&i->src[0],nir_imm_int(b,pan_res_handle(TEGU_PIPELINE2_TABLE_TEXTURE,bt->binding)));
 /* Valhall image loads/stores move 32-bit components: widen half/short
  * values (the converted format is the descriptor's either way). */
 if(i->intrinsic==nir_intrinsic_image_store) {
  nir_alu_type t=nir_alu_type_get_base_type(nir_intrinsic_src_type(i));nir_def *v=i->src[3].ssa;
  if(v->bit_size!=32) {
   v=t==nir_type_float?nir_f2f32(b,v):t==nir_type_int?nir_i2i32(b,v):nir_u2u32(b,v);
   nir_src_rewrite(&i->src[3],v);
  }
  nir_intrinsic_set_src_type(i,t|32); /* air/ leaves the type unsized */
 }
 if(i->intrinsic==nir_intrinsic_image_load) {
  nir_alu_type t=nir_alu_type_get_base_type(nir_intrinsic_dest_type(i));unsigned bits=i->def.bit_size;
  nir_intrinsic_set_dest_type(i,t|32);
  if(bits!=32) {
   b->cursor=nir_after_instr(&i->instr);
   i->def.bit_size=32;
   nir_def *n=t==nir_type_float?nir_f2fN(b,&i->def,bits):t==nir_type_int?nir_i2iN(b,&i->def,bits):nir_u2uN(b,&i->def,bits);
   nir_def_rewrite_uses_after(&i->def,n);
  }
 }
 return true;
}
static bool cair_instr(nir_builder *b,nir_instr *instr,void *data) {
 struct cair_req *rq=data;
 if(rq->why)return false;
 if(instr->type==nir_instr_type_tex)return cair_tex(b,nir_instr_as_tex(instr),rq);
 if(instr->type!=nir_instr_type_intrinsic)return false;
 nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
 if(nir_intrinsic_has_image_dim(i))return cair_image(b,i,rq);
 return cair_buffer(b,i,rq);
}
/* ---- kernel builtins (m2v dispatch contract, see the header comment) ---- */
static bool cair_crosslane(nir_intrinsic_op op) {
 switch(op) {
 case nir_intrinsic_reduce:case nir_intrinsic_inclusive_scan:case nir_intrinsic_exclusive_scan:
 case nir_intrinsic_vote_all:case nir_intrinsic_vote_any:case nir_intrinsic_vote_feq:case nir_intrinsic_vote_ieq:
 case nir_intrinsic_ballot:case nir_intrinsic_read_invocation:case nir_intrinsic_read_first_invocation:
 case nir_intrinsic_shuffle:case nir_intrinsic_shuffle_xor:case nir_intrinsic_shuffle_up:case nir_intrinsic_shuffle_down:
 case nir_intrinsic_elect:case nir_intrinsic_first_invocation:case nir_intrinsic_last_invocation:
  return true;
 default:return false;
 }
}
struct cair_sv { struct cair_req *rq;bool changed; };
static bool cair_threads_per_grid(nir_builder *b,nir_alu_instr *alu,void *data) {
 /* air/ answers threads_per_grid as imul(num_workgroups, workgroup_size); a
  * multi-region dispatch reads the grid from the region instead */
 struct cair_req *rq=data;
 if(alu->op!=nir_op_imul||!rq->has_multi||!rq->multi)return false;
 nir_instr *a=nir_def_instr(alu->src[0].src.ssa),*c=nir_def_instr(alu->src[1].src.ssa);
 if(a->type!=nir_instr_type_intrinsic||c->type!=nir_instr_type_intrinsic)return false;
 nir_intrinsic_op x=nir_instr_as_intrinsic(a)->intrinsic,y=nir_instr_as_intrinsic(c)->intrinsic;
 if(!((x==nir_intrinsic_load_num_workgroups&&y==nir_intrinsic_load_workgroup_size)||
      (y==nir_intrinsic_load_num_workgroups&&x==nir_intrinsic_load_workgroup_size)))return false;
 for(unsigned k=0;k<2;k++)if(alu->src[k].swizzle[0]!=0||(alu->def.num_components>1&&alu->src[k].swizzle[1]!=1)||
    (alu->def.num_components>2&&alu->src[k].swizzle[2]!=2))return false;
 b->cursor=nir_before_instr(&alu->instr);
 nir_def *grid=nir_trim_vector(b,cair_push(b,3,(unsigned)rq->region+16),alu->def.num_components);
 nir_def_replace(&alu->def,grid);
 return true;
}
static nir_def *cair_nominal(nir_builder *b,struct cair_req *rq) {
 nir_def *w=cair_push(b,1,(unsigned)rq->region+12);
 return nir_vec3(b,nir_iadd_imm(b,nir_iand_imm(b,w,0x3ff),1),nir_iadd_imm(b,nir_iand_imm(b,nir_ushr_imm(b,w,10),0x3ff),1),
  nir_iadd_imm(b,nir_iand_imm(b,nir_ushr_imm(b,w,20),0x3ff),1));
}
static bool cair_sysval(nir_builder *b,nir_intrinsic_instr *i,void *data) {
 struct cair_sv *sv=data;struct cair_req *rq=sv->rq;
 bool multi=rq->has_multi&&rq->multi;
 b->cursor=nir_after_instr(&i->instr);
 nir_def *v=NULL;
 switch(i->intrinsic) {
 case nir_intrinsic_load_workgroup_id:
  if(!multi)return false;
  v=nir_iadd(b,cair_push(b,3,(unsigned)rq->region),&i->def);
  break;
 case nir_intrinsic_load_global_invocation_id: {
  b->cursor=nir_before_instr(&i->instr);
  nir_def *wg=nir_load_workgroup_id(b),*lid=nir_load_local_invocation_id(b);
  nir_def *l=multi?cair_nominal(b,rq):nir_imm_ivec3(b,rq->local[0],rq->local[1],rq->local[2]);
  if(multi)wg=nir_iadd(b,cair_push(b,3,(unsigned)rq->region),wg);
  v=nir_iadd(b,nir_imul(b,wg,l),lid);
  if(i->def.bit_size!=32)v=nir_u2uN(b,v,i->def.bit_size);
  nir_def_replace(&i->def,v);sv->changed=true;return true;
 }
 case nir_intrinsic_load_num_workgroups:
  if(!multi)return false;
  b->cursor=nir_before_instr(&i->instr);
  {nir_def *grid=cair_push(b,3,(unsigned)rq->region+16),*l=cair_nominal(b,rq);
   v=nir_udiv(b,nir_iadd(b,grid,nir_iadd_imm(b,l,-1)),l);}
  nir_def_replace(&i->def,v);sv->changed=true;return true;
 case nir_intrinsic_load_subgroup_invocation:case nir_intrinsic_load_subgroup_id:
 case nir_intrinsic_load_subgroup_size:case nir_intrinsic_load_num_subgroups: {
  b->cursor=nir_before_instr(&i->instr);
  unsigned n=rq->local[0]*rq->local[1]*rq->local[2];
  nir_def *lin=nir_load_local_invocation_index(b);
  switch(i->intrinsic) {
  case nir_intrinsic_load_subgroup_invocation:v=nir_iand_imm(b,lin,31);break;
  case nir_intrinsic_load_subgroup_id:v=nir_ushr_imm(b,lin,5);if(rq->sg_count<ARRAY_SIZE(rq->sg_defs))rq->sg_defs[rq->sg_count++]=v;break;
  case nir_intrinsic_load_subgroup_size:v=nir_imm_int(b,32);break;
  default:v=nir_imm_int(b,(n+31)/32);break;
  }
  if(i->def.bit_size!=32)v=nir_u2uN(b,v,i->def.bit_size);
  nir_def_replace(&i->def,v);sv->changed=true;return true;
 }
 default:return false;
 }
 nir_def_rewrite_uses_after(&i->def,v);
 sv->changed=true;
 return true;
}
/* ---- Metal 32-lane simdgroups on 16-lane Valhall subgroups ----------------
 * A Metal simdgroup is two hardware warps: lin 32s..32s+15 and 32s+16..32s+31
 * (Panfrost numbers warps by linear local index: bifrost_nir.c subgroup_id =
 * flat id / 16). A cross-lane operation is computed natively over the warp and
 * exchanged with the partner warp (w ^ 1) through threadgroup scratch: the
 * warp publishes its value in slot [w][k & 1] and its counter = k (k = the
 * operation's sequence number), then waits until the partner's counter reaches
 * k. Double buffering is enough: a warp writes slot k + 2 only after passing
 * operation k + 1's wait, i.e. after the partner finished operation k.
 * This needs both warps of the simdgroup to reach every operation, so it is
 * applied only where control flow is uniform across the simdgroup (every
 * enclosing condition and loop exit computed from simdgroup-uniform values:
 * constants, uniforms, threadgroup/grid ids, the simdgroup index, results of
 * the emulated operations, memory read at such addresses); anything else is
 * refused. All lanes are then active, so "first active lane" is lane 0. */
struct cair_simd {
 struct cair_req *rq;BITSET_WORD *sgu;unsigned ndefs;
 unsigned warps,slot_bytes,lanes;unsigned scratch;      /* scratch: byte base of counters */
 nir_variable *seq;unsigned emulated,refused;bool changed;
};
static bool cair_simd_op(nir_intrinsic_op op) {
 switch(op) {
 case nir_intrinsic_reduce:case nir_intrinsic_vote_all:case nir_intrinsic_vote_any:
 case nir_intrinsic_read_first_invocation:case nir_intrinsic_shuffle:return true;
 default:return false;
 }
}
static bool cair_sgu(struct cair_simd *m,nir_def *d) {return d->index<m->ndefs&&BITSET_TEST(m->sgu,d->index);}
static bool cair_loop_converges(struct cair_simd *m,nir_loop *loop);
/* every enclosing condition of `node` up to (excluding) `stop` is simdgroup-uniform */
static bool cair_cf_converges(struct cair_simd *m,nir_cf_node *node,nir_cf_node *stop) {
 for(nir_cf_node *n=node->parent;n&&n!=stop;n=n->parent) {
  if(n->type==nir_cf_node_if){if(!cair_sgu(m,nir_cf_node_as_if(n)->condition.ssa))return false;}
  else if(n->type==nir_cf_node_loop){if(!cair_loop_converges(m,nir_cf_node_as_loop(n)))return false;}
 }
 return true;
}
static bool cair_loop_converges(struct cair_simd *m,nir_loop *loop) {
 /* every break/continue of this loop sits under uniform conditions only */
 nir_foreach_block_in_cf_node(block,&loop->cf_node) {
  nir_instr *last=nir_block_last_instr(block);
  if(!last||last->type!=nir_instr_type_jump)continue;
  nir_jump_instr *j=nir_instr_as_jump(last);
  if(j->type!=nir_jump_break&&j->type!=nir_jump_continue)return false;
  /* the innermost loop of this jump */
  nir_cf_node *n=block->cf_node.parent;
  while(n&&n->type!=nir_cf_node_loop)n=n->parent;
  if(n!=&loop->cf_node)continue;
  for(nir_cf_node *c=block->cf_node.parent;c&&c!=&loop->cf_node;c=c->parent)
   if(c->type==nir_cf_node_if&&!cair_sgu(m,nir_cf_node_as_if(c)->condition.ssa))return false;
 }
 return true;
}
static bool cair_block_converges(struct cair_simd *m,nir_block *block) {return cair_cf_converges(m,&block->cf_node,NULL);}
static bool cair_src_sgu(nir_src *src,void *data) {
 struct cair_simd *m=data;return cair_sgu(m,src->ssa);
}
static bool cair_instr_sgu(struct cair_simd *m,nir_instr *instr) {
 switch(instr->type) {
 case nir_instr_type_load_const:case nir_instr_type_undef:return true;
 case nir_instr_type_alu:case nir_instr_type_tex:return nir_foreach_src(instr,cair_src_sgu,m);
 case nir_instr_type_phi: {
  nir_phi_instr *phi=nir_instr_as_phi(instr);
  nir_foreach_phi_src(s,phi)if(!cair_sgu(m,s->src.ssa))return false;
  nir_cf_node *prev=nir_cf_node_prev(&instr->block->cf_node);
  if(prev&&prev->type==nir_cf_node_if)return cair_sgu(m,nir_cf_node_as_if(prev)->condition.ssa);
  if(prev&&prev->type==nir_cf_node_loop)return cair_loop_converges(m,nir_cf_node_as_loop(prev));
  nir_cf_node *parent=instr->block->cf_node.parent;
  if(parent&&parent->type==nir_cf_node_loop&&nir_loop_first_block(nir_cf_node_as_loop(parent))==instr->block)
   return cair_loop_converges(m,nir_cf_node_as_loop(parent));
  return false;
 }
 case nir_instr_type_intrinsic: {
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  for(unsigned k=0;k<m->rq->sg_count;k++)if(m->rq->sg_defs[k]==&i->def)return true;
  switch(i->intrinsic) {
  case nir_intrinsic_load_workgroup_id:case nir_intrinsic_load_num_workgroups:case nir_intrinsic_load_workgroup_size:
  case nir_intrinsic_load_push_constant:case nir_intrinsic_load_ubo:case nir_intrinsic_load_ssbo:
  case nir_intrinsic_load_global:case nir_intrinsic_load_global_constant:case nir_intrinsic_load_shared:
  case nir_intrinsic_load_constant:case nir_intrinsic_image_load:case nir_intrinsic_image_size:
   return nir_foreach_src(instr,cair_src_sgu,m);
  case nir_intrinsic_reduce:case nir_intrinsic_vote_all:case nir_intrinsic_vote_any:case nir_intrinsic_read_first_invocation:
   return cair_block_converges(m,instr->block);
  default:return false;
  }
 }
 default:return false;
 }
}
static void cair_simd_analyze(struct cair_simd *m,nir_function_impl *impl) {
 nir_index_ssa_defs(impl);
 m->ndefs=impl->ssa_alloc;
 m->sgu=rzalloc_array(NULL,BITSET_WORD,BITSET_WORDS(m->ndefs+1));
 BITSET_SET_RANGE(m->sgu,0,m->ndefs?m->ndefs-1:0);
 bool changed;
 do {
  changed=false;
  nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
   nir_def *d=nir_instr_def(instr);
   if(!d||!cair_sgu(m,d))continue;
   if(!cair_instr_sgu(m,instr)){BITSET_CLEAR(m->sgu,d->index);changed=true;}
  }
 } while(changed);
}
static nir_def *cair_lin(nir_builder *b){return nir_load_local_invocation_index(b);}
/* publish `v` (this warp's value, or this lane's for a shuffle) and fetch the
 * partner warp's value of the same operation; NULL when there is no partner */
static nir_def *cair_exchange(nir_builder *b,struct cair_simd *m,nir_def *v,bool per_lane,nir_def *read_lane) {
 nir_def *lin=cair_lin(b),*w=nir_ushr_imm(b,lin,4),*lane=nir_iand_imm(b,lin,15),*p=nir_ixor(b,w,nir_imm_int(b,1));
 nir_def *k=nir_iadd_imm(b,nir_load_var(b,m->seq),1);nir_store_var(b,m->seq,k,1);
 nir_def *par=nir_iand_imm(b,k,1);
 unsigned data=ALIGN_POT(m->scratch+4*m->warps,16);
 nir_def *mine=nir_iadd_imm(b,nir_imul_imm(b,nir_iadd(b,nir_imul_imm(b,w,2),par),m->slot_bytes),data);
 if(per_lane)mine=nir_iadd(b,mine,nir_imul_imm(b,lane,16));
 nir_def *stored=v->bit_size==1?nir_b2i32(b,v):v;
 if(per_lane)nir_store_shared(b,stored,mine,.align_mul=16);
 else {nir_push_if(b,nir_elect(b,1));nir_store_shared(b,stored,mine,.align_mul=16);nir_pop_if(b,NULL);}
 nir_barrier(b,.memory_scope=SCOPE_WORKGROUP,.memory_semantics=NIR_MEMORY_ACQ_REL,.memory_modes=nir_var_mem_shared);
 nir_push_if(b,nir_elect(b,1));
 nir_shared_atomic(b,32,nir_iadd_imm(b,nir_imul_imm(b,w,4),m->scratch),k,.atomic_op=nir_atomic_op_xchg);
 nir_pop_if(b,NULL);
 nir_push_if(b,nir_ult_imm(b,p,m->warps));
 nir_loop *loop=nir_push_loop(b);
 {
  nir_def *c=nir_shared_atomic(b,32,nir_iadd_imm(b,nir_imul_imm(b,p,4),m->scratch),nir_imm_int(b,0),.atomic_op=nir_atomic_op_iadd);
  nir_break_if(b,nir_uge(b,c,k));
 }
 nir_pop_loop(b,loop);
 nir_barrier(b,.memory_scope=SCOPE_WORKGROUP,.memory_semantics=NIR_MEMORY_ACQ_REL,.memory_modes=nir_var_mem_shared);
 nir_def *theirs=nir_iadd_imm(b,nir_imul_imm(b,nir_iadd(b,nir_imul_imm(b,p,2),par),m->slot_bytes),data);
 if(per_lane)theirs=nir_iadd(b,theirs,nir_imul_imm(b,read_lane,16));
 nir_def *o=nir_load_shared(b,stored->num_components,stored->bit_size,theirs,.align_mul=per_lane?4:16);
 nir_pop_if(b,NULL);
 o=nir_if_phi(b,o,nir_undef(b,stored->num_components,stored->bit_size));
 return v->bit_size==1?nir_i2b(b,o):o;
}
static bool cair_simd_lower(nir_builder *b,nir_intrinsic_instr *i,void *data) {
 struct cair_simd *m=data;
 if(!cair_simd_op(i->intrinsic))return false;
 if(i->def.index>=m->ndefs)return false; /* created by this pass */
 if(i->intrinsic==nir_intrinsic_reduce&&nir_intrinsic_cluster_size(i)&&nir_intrinsic_cluster_size(i)<=16)return false;
 if(!cair_block_converges(m,i->instr.block)){m->refused++;return false;}
 nir_def *v=i->src[0].ssa;
 if(v->num_components*MAX2(v->bit_size,32)>128){m->refused++;return false;}
 /* the result's existing uses, rewritten once the exchange (which spans
  * blocks) has produced the 32-lane value */
 unsigned nuse=0;nir_foreach_use_including_if(src,&i->def)nuse++;
 nir_src **uses=ralloc_array(NULL,nir_src *,nuse+1);nuse=0;
 nir_foreach_use_including_if(src,&i->def)uses[nuse++]=src;
 b->cursor=nir_after_instr(&i->instr);
 nir_def *lin=cair_lin(b),*w=nir_ushr_imm(b,lin,4),*p=nir_ixor(b,w,nir_imm_int(b,1)),*has=nir_ult_imm(b,p,m->warps);
 nir_def *r;
 switch(i->intrinsic) {
 case nir_intrinsic_reduce: {
  nir_def *o=cair_exchange(b,m,&i->def,false,NULL);
  r=nir_bcsel(b,has,nir_build_alu2(b,nir_intrinsic_reduction_op(i),&i->def,o),&i->def);
  break;
 }
 case nir_intrinsic_vote_any:case nir_intrinsic_vote_all: {
  nir_def *o=cair_exchange(b,m,&i->def,false,NULL);
  r=nir_bcsel(b,has,i->intrinsic==nir_intrinsic_vote_any?nir_ior(b,&i->def,o):nir_iand(b,&i->def,o),&i->def);
  break;
 }
 case nir_intrinsic_read_first_invocation: {
  /* every lane is active (uniform flow): Metal's first lane is lane 0 of the
   * even warp of the pair */
  nir_def *o=cair_exchange(b,m,&i->def,false,NULL);
  r=nir_bcsel(b,nir_ieq_imm(b,nir_iand_imm(b,w,1),0),&i->def,o);
  break;
 }
 default: { /* shuffle(v, lane L of 32) */
  nir_def *L=nir_iand_imm(b,i->src[1].ssa,31);
  nir_def *own=nir_ieq(b,nir_ushr_imm(b,L,4),nir_iand_imm(b,w,1));
  nir_def *o=cair_exchange(b,m,v,true,nir_iand_imm(b,L,15));
  /* the native shuffle reads lane L & 15 of this warp */
  nir_cursor after=b->cursor;
  b->cursor=nir_before_instr(&i->instr);
  nir_src_rewrite(&i->src[1],nir_iand_imm(b,i->src[1].ssa,15));
  b->cursor=after;
  r=nir_bcsel(b,own,&i->def,o);
  break;
 }
 }
 for(unsigned u=0;u<nuse;u++)nir_src_rewrite(uses[u],r);
 ralloc_free(uses);
 m->emulated++;m->changed=true;
 return true;
}
/* returns NULL or a refusal; grows shared_size by the exchange scratch */
static const char *cair_simdgroups(struct cair_req *rq,nir_shader *s,unsigned *shared_size) {
 nir_function_impl *impl=nir_shader_get_entrypoint(s);
 bool any=false,shuffle=false;
 nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(cair_crosslane(i->intrinsic)&&!cair_simd_op(i->intrinsic))return "air: simdgroup operation without a 32-lane emulation";
  if(cair_simd_op(i->intrinsic)&&!(i->intrinsic==nir_intrinsic_reduce&&nir_intrinsic_cluster_size(i)&&nir_intrinsic_cluster_size(i)<=16))any=true;
  if(i->intrinsic==nir_intrinsic_shuffle)shuffle=true;
 }
 unsigned n=rq->local[0]*rq->local[1]*rq->local[2];
 if(!any||n<=16)return NULL; /* one warp per simdgroup: native is exact */
 struct cair_simd m={.rq=rq};
 m.warps=(n+15)/16;m.lanes=shuffle?16:1;m.slot_bytes=16*m.lanes;
 m.scratch=ALIGN_POT(*shared_size,16);
 unsigned end=ALIGN_POT(m.scratch+4*m.warps,16)+m.warps*2*m.slot_bytes;
 if(end>COMPUTE_WLS_LIMIT)return "air: simdgroup exchange scratch exceeds the 32 KiB workgroup memory";
 cair_simd_analyze(&m,impl);
 nir_builder b=nir_builder_at(nir_before_impl(impl));
 m.seq=nir_local_variable_create(impl,glsl_uint_type(),"air_simd_seq");
 nir_store_var(&b,m.seq,nir_imm_int(&b,0),1);
 /* counters start at 0 before any warp can publish */
 nir_def *lin=cair_lin(&b);
 nir_push_if(&b,nir_ieq_imm(&b,nir_iand_imm(&b,lin,15),0));
 nir_store_shared(&b,nir_imm_int(&b,0),nir_iadd_imm(&b,nir_imul_imm(&b,nir_ushr_imm(&b,lin,4),4),m.scratch),.align_mul=4);
 nir_pop_if(&b,NULL);
 nir_barrier(&b,.execution_scope=SCOPE_WORKGROUP,.memory_scope=SCOPE_WORKGROUP,.memory_semantics=NIR_MEMORY_ACQ_REL,.memory_modes=nir_var_mem_shared);
 nir_shader_intrinsics_pass(s,cair_simd_lower,nir_metadata_none,&m);
 ralloc_free(m.sgu);
 if(m.refused)return "air: 32-lane simdgroup operation where control flow may diverge within the simdgroup";
 NIR_PASS(_,s,nir_lower_vars_to_ssa);
 *shared_size=end;
 return NULL;
}
static const void *cair_bitcode(void *mem,const struct tegu_mali_air_compute *f,size_t *size,const char **err) {
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
static nir_shader *cair_translate(struct cair_req *rq,void *mem,struct air_shader_info *info) {
 const struct tegu_mali_air_compute *f=rq->air;
 const char *err=NULL;size_t size=0;
 const void *bc=cair_bitcode(mem,f,&size,&err);
 if(!bc){rq->why=cair_fail(rq,"air: reader: %s",err?err:"?");return NULL;}
 int st=air_module_stage_for(mem,bc,size,f->entry,NULL,&err);
 if(st!=MESA_SHADER_COMPUTE){rq->why="air: not a kernel function";return NULL;}
 struct air_constant_value *fc=NULL;const char **cn=NULL;
 unsigned nfc=air_module_function_constant_names(mem,bc,size,&fc,&cn);
 for(unsigned i=0;i<nfc;i++)for(uint32_t j=0;j<f->constant_count;j++) {
  const struct tegu_mali_air_kernel_constant *c=&f->constants[j];
  bool hit=c->name?(cn&&cn[i]&&!strcmp(c->name,cn[i])):c->index==fc[i].index;
  if(!hit)continue;
  uint64_t v=c->bits;if(c->size&&c->size<8)v&=(1ull<<(c->size*8))-1;
  fc[i].bits=v;break;
 }
 struct air_to_nir_options opts={.constants=fc,.num_constants=nfc};
 nir_shader *s=air_to_nir(mem,bc,size,f->entry,&opts,pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),info,&err);
 if(!s){rq->why=cair_fail(rq,"air: %s",err?err:"translation failed");return NULL;}
 ralloc_steal(NULL,s);
 if(info->raster_order_groups){rq->why="air: raster order groups";return s;}
 if(info->num_imageblock){rq->why="air: imageblock (tile function) not adapted yet";return s;}
 for(unsigned i=0;i<info->num_resources;i++)
  if(info->resources[i].argbuf_set!=~0u){rq->why="air: argument buffer member";return s;}
 return s;
}
int tegu_mali_compile_compute_air(const struct tegu_mali_air_compute *air,
                     const struct tegu_mali_specialization *constants,size_t count,
                     struct tegu_mali_blob *result,char *error,size_t error_size)
{
   if(!compute_span(result,sizeof(*result))||count>64||(count&&!compute_span(constants,count*sizeof(*constants)))||
      (error_size&&!compute_span(error,error_size))||!air||!compute_span(air,sizeof(*air)))return 2;
   if(compute_overlap(error,error_size,result,sizeof(*result))||
      compute_overlap(error,error_size,constants,count*sizeof(*constants)))return 2;
   if(error&&error_size)error[0]=0;
   if(result->data||result->size){if(error&&error_size)snprintf(error,error_size,"Output must be empty");return 2;}
   if(air->version!=TEGU_MALI_AIR_COMPUTE_VERSION||(air->flags&~TEGU_MALI_AIR_COMPUTE_V2)||air->reserved||
      (air->constant_count&&(!air->constants||air->constant_count>256))){
      if(error&&error_size)snprintf(error,error_size,"air: invalid compute request");
      return 2;
   }
   bool v2=(air->flags&TEGU_MALI_AIR_COMPUTE_V2)!=0;
   struct context ctx={.v2=v2,.error=error,.error_size=error_size};
   struct cair_req *rq=calloc(1,sizeof(*rq));
   if(!rq){refuse(&ctx,"air: allocation failed");return 6;}
   rq->ctx=&ctx;rq->air=air;rq->c=constants;rq->cn=count;rq->v2=v2;
   int status=2;nir_shader *s=NULL;
   void *mem=ralloc_context(NULL);
   glsl_type_singleton_init_or_ref();
   const char *why=cair_parse(rq,mem);
   if(why){refuse(&ctx,why);goto done;}
   struct air_shader_info info;memset(&info,0,sizeof(info));
   s=cair_translate(rq,mem,&info);
   if(!s||rq->why){refuse(&ctx,rq->why?rq->why:"air: translation failed");status=3;goto done;}
   rq->info=&info;
   /* the region's exact local size, before nir_lower_system_values folds
    * load_workgroup_size from it */
   s->info.workgroup_size[0]=rq->local[0];s->info.workgroup_size[1]=rq->local[1];s->info.workgroup_size[2]=rq->local[2];
   s->info.workgroup_size_variable=false;
   /* threadgroup memory: static (module) or one dynamic buffer at index 0 */
   bool dynamic_tg=false;
   /* m2v declares TgLength{0} only for a threadgroup argument the kernel reads;
    * one it proved unread (readnone) has no accesses here either */
   for(unsigned k=0;k<info.num_resources;k++)if(info.resources[k].kind==AIR_RESOURCE_THREADGROUP_BUFFER&&rq->has_tglen0)dynamic_tg=true;
   if(dynamic_tg&&info.threadgroup_bytes){refuse(&ctx,"air: static and dynamic threadgroup memory (the frontend bases both at 0)");status=4;goto done;}
   NIR_PASS(_,s,nir_lower_variable_initializers,nir_var_function_temp);
   NIR_PASS(_,s,nir_lower_returns);NIR_PASS(_,s,nir_inline_functions);nir_remove_non_entrypoints(s);
   NIR_PASS(_,s,nir_lower_variable_initializers,~0);
   NIR_PASS(_,s,nir_split_var_copies);NIR_PASS(_,s,nir_lower_var_copies);
   NIR_PASS(_,s,nir_lower_global_vars_to_local);NIR_PASS(_,s,nir_lower_vars_to_ssa);
   /* threads_per_grid before CSE can merge its operands */
   NIR_PASS(_,s,nir_shader_alu_pass,cair_threads_per_grid,nir_metadata_control_flow,rq);
   NIR_PASS(_,s,nir_lower_system_values);
   bool progress;
   do {
      progress=false;
      NIR_PASS(progress,s,nir_opt_constant_folding);NIR_PASS(progress,s,nir_opt_dead_cf);
      NIR_PASS(progress,s,nir_opt_copy_prop);NIR_PASS(progress,s,nir_opt_dce);
      NIR_PASS(progress,s,nir_opt_remove_phis);NIR_PASS(progress,s,nir_opt_algebraic);
   } while(progress);
   NIR_PASS(_,s,nir_remove_dead_variables,nir_var_function_temp|nir_var_shader_temp,NULL);
   nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
      if(instr->type!=nir_instr_type_intrinsic)continue;
      nir_intrinsic_op op=nir_instr_as_intrinsic(instr)->intrinsic;
      if(!dynamic_tg&&!info.threadgroup_bytes&&(op==nir_intrinsic_load_shared||op==nir_intrinsic_store_shared||
         op==nir_intrinsic_shared_atomic||op==nir_intrinsic_shared_atomic_swap)) {
         refuse(&ctx,"air: threadgroup memory access without an m2v TgLength or static size");status=4;goto done;
      }
      if((op==nir_intrinsic_load_workgroup_id||op==nir_intrinsic_load_num_workgroups||op==nir_intrinsic_load_global_invocation_id)&&
         !rq->has_multi&&rq->region<0) {
         /* m2v declares MultiRegion/DispatchRegion for every kernel that reads a
          * grid builtin; without them the planner may split the grid freely */
         refuse(&ctx,"air: grid builtin the m2v translation does not read (no MultiRegion)");status=4;goto done;
      }
   }
   s->info.workgroup_size[0]=rq->local[0];s->info.workgroup_size[1]=rq->local[1];s->info.workgroup_size[2]=rq->local[2];
   s->info.workgroup_size_variable=false;
   {
      struct cair_sv sv={.rq=rq};
      NIR_PASS(_,s,nir_shader_intrinsics_pass,cair_sysval,nir_metadata_control_flow,&sv);
   }
   NIR_PASS(_,s,nir_shader_instructions_pass,cair_instr,nir_metadata_none,rq);
   if(rq->why){if(rq->why!=error)refuse(&ctx,rq->why);ctx.failed=true;status=4;goto done;}
   if(ctx.failed){status=4;goto done;}
   nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
      if(instr->type!=nir_instr_type_intrinsic)continue;
      switch(nir_instr_as_intrinsic(instr)->intrinsic) {
      case nir_intrinsic_load_ubo:case nir_intrinsic_load_ssbo:case nir_intrinsic_store_ssbo:
      case nir_intrinsic_ssbo_atomic:case nir_intrinsic_ssbo_atomic_swap:case nir_intrinsic_load_constant:
         refuse(&ctx,"air: buffer access left unadapted");status=4;goto done;
      default:break;
      }
   }
   unsigned shared=dynamic_tg?rq->tglen0:info.threadgroup_bytes;
   if(rq->static_tg&&rq->static_tg<info.threadgroup_bytes){refuse(&ctx,"air: static threadgroup memory exceeds the m2v metal_info");status=4;goto done;}
   if((why=cair_simdgroups(rq,s,&shared))){refuse(&ctx,why);status=4;goto done;}
   s->info.shared_size=shared;
   nir_lower_compute_system_values_options cs={.global_id_is_32bit=true};
   NIR_PASS(_,s,nir_lower_compute_system_values,&cs);
   NIR_PASS(_,s,nir_opt_dce);
   nir_validate_shader(s,"tegu air compute adapted");
   nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
   s->info.workgroup_size[0]=rq->local[0];s->info.workgroup_size[1]=rq->local[1];s->info.workgroup_size[2]=rq->local[2];
   s->info.workgroup_size_variable=false;
   s->info.shared_size=shared;
   if(getenv("TEGU_AIR_COMPUTE_PRINT"))nir_print_shader(s,stderr);
   status=compute_backend(s,&ctx,v2,true,result);
done:
   if(s)ralloc_free(s);
   ralloc_free(mem);
   glsl_type_singleton_decref();
   free(rq);
   return status;
}
