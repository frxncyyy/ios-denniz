/* AIR-derived render plan: the planner metadata of a generic render pipeline
 * (binding layout, push placement, specialization keys, runtime facts, vertex
 * input, graphics state) produced from the AIR module itself, in the
 * metal2vulkan schema-1 shape the render frontend, the Rust draw planner
 * (metal2vulkan bind::RenderPipeline::plan) and air_iface_parse already
 * consume. Replaces the m2v translation at render pipeline creation.
 * Included at the end of mali_graphics_air.inc (TEGU_MALI_AIR only).
 *
 * Rules are metal2vulkan's (pinned edb177a): link.rs layout/specialize/
 * pipeline_interface (dense set-0 bindings in ResourceSlot order, spec ids in
 * SpecKey order), lower/push.rs (stage windows [0,64) / [64,128), fields in
 * PushField order), memory/window.rs (hull of constant accesses, else
 * air.buffer_size, else Whole), lower/access view_kind (uniform view for a
 * read-only constant Bounded slot that fits maxUniformBufferRange), the
 * texture/sampler families' facts (BTreeSet<RuntimeFact> order),
 * link/graphics.rs and link/vertex.rs for the attachments and vertex input.
 * Everything outside the subset this file states exactly is refused with a
 * reason: the pipeline is then TEGU_AIR_REFUSED, never approximated.
 * Deliberately absent: m2v's own colour rounding emulation (ColorWriteRule
 * push fields, GraphicsState.color_write_rules), which AIR code does not
 * perform and the AIR adapter never read. */

/* ---- profile facts the rules read (tegu-air-profile.h, Mali-G77 MC9) ---- */
#define AIRP_MAX_UNIFORM_RANGE 65536u
#define AIRP_UNIFORM_ALIGN 64u /* max(minUniformBufferOffsetAlignment, 16) */
#define AIRP_MAX_PUSH_DESCRIPTORS 32u
#define AIRP_MAX_VERTEX_STRIDE 2048u

enum { AIRP_SLOT_BUFFER=0, AIRP_SLOT_TEXTURE=1, AIRP_SLOT_SAMPLER=2 };
/* metal2vulkan ResourceKind order: Buffer < Texture < Sampler < Heap < Synthetic <
 * InputAttachment < CombinedTextureSampler */
enum { AIRP_K_BUFFER=0, AIRP_K_TEXTURE=1, AIRP_K_INPUT=5, AIRP_K_COMBINED=6 };

struct airp_buffer { unsigned index; bool constant,read,write,unknown,used; int64_t lo,hi; unsigned size; };
struct airp_texture { unsigned index; int dim; bool array,ms,shadow,sample,read,query,used,ycbcr,constant_lanes,srgb,cross;
 int texel; /* 0 float 1 sint 2 uint */ };
struct airp_sampler { unsigned index; bool used,coords; };
struct airp_combined { unsigned texture; bool constexpr_; unsigned slot; uint64_t word; bool compare,integer; };
struct airp_stage {
 unsigned stage; /* TEGU_GRAPHICS_VERTEX / _FRAGMENT */
 struct airp_buffer b[31];unsigned nb;
 struct airp_texture t[32];unsigned nt;
 struct airp_sampler s[16];unsigned ns;
 struct airp_combined c[32];unsigned nc;
 uint64_t constexpr_words[16];unsigned nconstexpr; /* used constexpr samplers */
 uint64_t module_words[16];unsigned nmodule;       /* every constexpr sampler, module order */
 bool reads_texel,writes_texel;
 uint32_t fbfetch;     /* fragment: [[color(n)]] reads */
 uint32_t outputs;     /* fragment: colour outputs written (index 0) */
 bool flat_inputs,point_size,position_depth,early_tests;
 uint32_t attributes;  /* vertex: Metal attribute indices read */
 int attribute_type[31]; /* AIR_ATTRIB_* of each */
 bool vertex_id,instance_id;
 uint32_t presence; /* texture slots tested with air.is_null_texture (taken as bound) */
};
struct airp_desc_buf { char *p;size_t n,cap;bool oom; };
static void airp_put(struct airp_desc_buf *o,const char *fmt,...) {
 if(o->oom)return;
 for(;;) {
  va_list ap;va_start(ap,fmt);
  int k=vsnprintf(o->p?o->p+o->n:NULL,o->p?o->cap-o->n:0,fmt,ap);va_end(ap);
  if(k<0){o->oom=true;return;}
  if(o->p&&o->n+(size_t)k<o->cap){o->n+=(size_t)k;return;}
  size_t cap=o->cap?o->cap*2:8192;while(cap<o->n+(size_t)k+1)cap*=2;
  char *q=realloc(o->p,cap);if(!q){o->oom=true;return;}
  o->p=q;o->cap=cap;
 }
}
static const char *airp_stage_name(unsigned stage){return stage==TEGU_GRAPHICS_VERTEX?"Vertex":"Fragment";}
static void airp_slot(struct airp_desc_buf *o,unsigned stage,unsigned cls,unsigned index) {
 static const char *const names[]={"Buffer","Texture","Sampler"};
 airp_put(o,"{\"class\":\"%s\",\"index\":%u,\"stage\":\"%s\"}",names[cls],index,airp_stage_name(stage));
}
static struct airp_buffer *airp_buffer(struct airp_stage *st,unsigned index) {
 for(unsigned i=0;i<st->nb;i++)if(st->b[i].index==index)return &st->b[i];
 if(st->nb>=ARRAY_SIZE(st->b))return NULL;
 struct airp_buffer *b=&st->b[st->nb++];memset(b,0,sizeof(*b));b->index=index;b->lo=INT64_MAX;b->hi=INT64_MIN;return b;
}
static struct airp_texture *airp_texture(struct airp_stage *st,unsigned index) {
 for(unsigned i=0;i<st->nt;i++)if(st->t[i].index==index)return &st->t[i];
 if(st->nt>=ARRAY_SIZE(st->t))return NULL;
 struct airp_texture *t=&st->t[st->nt++];memset(t,0,sizeof(*t));t->index=index;t->dim=-1;t->texel=-1;return t;
}
static struct airp_sampler *airp_sampler(struct airp_stage *st,unsigned index) {
 for(unsigned i=0;i<st->ns;i++)if(st->s[i].index==index)return &st->s[i];
 if(st->ns>=ARRAY_SIZE(st->s))return NULL;
 struct airp_sampler *s=&st->s[st->ns++];memset(s,0,sizeof(*s));s->index=index;return s;
}
static bool airp_combined(struct airp_stage *st,const struct airp_combined *c) {
 for(unsigned i=0;i<st->nc;i++){const struct airp_combined *x=&st->c[i];
  if(x->texture==c->texture&&x->constexpr_==c->constexpr_&&x->slot==c->slot&&x->word==c->word&&x->compare==c->compare&&x->integer==c->integer)return true;}
 if(st->nc>=ARRAY_SIZE(st->c))return false;
 st->c[st->nc++]=*c;return true;
}
/* metal2vulkan's constexpr sampler word 0 is AIR's state word without bit 63 */
static uint64_t airp_word(uint64_t air){return air&~(1ull<<63);}

/* ---- the shader walk ---- */
static const char *airp_walk(struct airp_stage *st,nir_shader *s,const struct air_shader_info *info,char *why,size_t cap) {
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
  if(instr->type==nir_instr_type_intrinsic) {
   nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
   switch(i->intrinsic) {
   case nir_intrinsic_load_ubo:case nir_intrinsic_load_ssbo: {
    if(!nir_src_is_const(i->src[0]))return "air plan: dynamic buffer index";
    unsigned slot=nir_src_as_uint(i->src[0]);
    if(slot>30)return "air plan: buffer slot above 30";
    struct airp_buffer *b=airp_buffer(st,slot);if(!b)return "air plan: too many buffer slots";
    b->used=b->read=true;b->constant=i->intrinsic==nir_intrinsic_load_ubo;
    unsigned bytes=i->def.num_components*MAX2(i->def.bit_size,8)/8;
    if(nir_src_is_const(i->src[1])) {
     int64_t o=(int64_t)(int32_t)nir_src_as_uint(i->src[1]);
     b->lo=MIN2(b->lo,o);b->hi=MAX2(b->hi,o+(int64_t)bytes);
    } else b->unknown=true;
    break;
   }
   case nir_intrinsic_store_ssbo:case nir_intrinsic_ssbo_atomic:case nir_intrinsic_ssbo_atomic_swap:
    return "air plan: buffer write (writable slots are not in the AIR plan subset)";
   case nir_intrinsic_load_global:case nir_intrinsic_store_global:case nir_intrinsic_load_global_constant:
   case nir_intrinsic_global_atomic:case nir_intrinsic_global_atomic_swap:
    return "air plan: device address access";
   case nir_intrinsic_load_constant:return "air plan: shader constant data (constant blob binding)";
   case nir_intrinsic_load_shared:case nir_intrinsic_store_shared:return "air plan: threadgroup memory in a render stage";
   case nir_intrinsic_image_deref_load:case nir_intrinsic_image_deref_store:case nir_intrinsic_image_load:case nir_intrinsic_image_store:
   case nir_intrinsic_image_deref_atomic:case nir_intrinsic_image_atomic:case nir_intrinsic_image_deref_atomic_swap:
    return "air plan: storage texture access";
   case nir_intrinsic_load_output:
    if(nir_intrinsic_io_semantics(i).fb_fetch_output) {
     unsigned rt=nir_intrinsic_io_semantics(i).location-FRAG_RESULT_DATA0;
     if(rt>7)return "air plan: framebuffer fetch of a non-colour output";
     if(i->def.num_components!=4||i->def.bit_size!=32)return "air plan: framebuffer fetch shape";
     st->fbfetch|=1u<<rt;
    }
    break;
   case nir_intrinsic_load_vertex_id:case nir_intrinsic_load_vertex_id_zero_base:st->vertex_id=true;break;
   case nir_intrinsic_load_instance_id:st->instance_id=true;break;
   case nir_intrinsic_load_sample_id:case nir_intrinsic_load_sample_mask_in:case nir_intrinsic_load_sample_pos:
    return "air plan: per-sample fragment inputs";
   case nir_intrinsic_load_layer_id:case nir_intrinsic_load_view_index:return "air plan: layer/view inputs";
   case nir_intrinsic_load_barycentric_coord_pixel:case nir_intrinsic_load_barycentric_coord_centroid:
   case nir_intrinsic_load_barycentric_coord_sample:case nir_intrinsic_load_barycentric_coord_at_offset:
   case nir_intrinsic_load_barycentric_coord_at_sample:return "air plan: barycentric coordinates";
   case nir_intrinsic_terminate:case nir_intrinsic_terminate_if:case nir_intrinsic_demote:case nir_intrinsic_demote_if:break;
   default:break;
   }
   continue;
  }
  if(instr->type!=nir_instr_type_tex)continue;
  nir_tex_instr *tex=nir_instr_as_tex(instr);
  unsigned t=tex->texture_index,sidx=tex->sampler_index;
  bool real31=false;
  for(unsigned k=0;k<info->num_resources;k++)
   if((info->resources[k].kind==AIR_RESOURCE_TEXTURE||info->resources[k].kind==AIR_RESOURCE_IMAGE)&&info->resources[k].index==31)real31=true;
  if(t==31&&!real31)continue; /* air/'s null texture: Metal reads zero, no slot */
  if(t>30)return "air plan: texture slot above 30";
  if(nir_tex_instr_src_index(tex,nir_tex_src_texture_handle)>=0||nir_tex_instr_src_index(tex,nir_tex_src_texture_offset)>=0||
     nir_tex_instr_src_index(tex,nir_tex_src_sampler_offset)>=0)return "air plan: dynamic texture or sampler";
  for(unsigned k=0;k<info->num_resources;k++)
   if(info->resources[k].kind==AIR_RESOURCE_IMAGE&&info->resources[k].index==t)return "air plan: storage texture";
  struct airp_texture *tx=airp_texture(st,t);if(!tx)return "air plan: too many texture slots";
  int dim;
  switch(tex->sampler_dim){case GLSL_SAMPLER_DIM_1D:dim=1;break;case GLSL_SAMPLER_DIM_2D:dim=2;break;case GLSL_SAMPLER_DIM_3D:dim=3;break;
   case GLSL_SAMPLER_DIM_MS:dim=4;break; /* Type2DMultisample: reads only */
   case GLSL_SAMPLER_DIM_CUBE:dim=5;break; /* Cube */
   default:return "air plan: texture type outside 1D/2D/3D/2D-multisample (cube, buffer, external)";}
  if(tex->is_array)return "air plan: array texture";
  if(tex->is_shadow)return "air plan: depth comparison";
  if(tex->is_sparse)return "air plan: residency observed";
  int texel;
  switch(nir_alu_type_get_base_type(tex->dest_type)){case nir_type_float:texel=0;break;case nir_type_int:texel=1;break;case nir_type_uint:texel=2;break;
   default:return "air plan: texel type";}
  if(nir_alu_type_get_type_size(tex->dest_type)&&nir_alu_type_get_type_size(tex->dest_type)<16)return "air plan: 8-bit texel results";
  bool query=tex->op==nir_texop_txs||tex->op==nir_texop_query_levels||tex->op==nir_texop_texture_samples;
  if(query) {
   if(tex->op==nir_texop_query_levels&&dim==4)return "air plan: level query of a multisample texture";
  } else {
   if(tx->dim>=0&&tx->dim!=dim)return "air plan: one slot used with two texture types";
   if(tx->texel>=0&&tx->texel!=texel)return "air plan: one slot read in two numeric classes";
   tx->dim=dim;tx->texel=texel;
  }
  tx->used=true;
  if(query){tx->query=true;if(tx->dim<0)tx->dim=dim;continue;}
  switch(tex->op) {
  case nir_texop_txf:case nir_texop_txf_ms:
   if((tex->op==nir_texop_txf_ms)!=(dim==4))return "air plan: multisample read shape";
   tx->read=true;st->reads_texel=true;
   /* texture/mod.rs srgb_texels: a read with a float result of a non-depth, non-buffer slot;
    * the ConstantLanes arm: a texel op of a slot that is not depth (multisample too) */
   tx->constant_lanes=true;tx->srgb=!texel;
   continue;
  case nir_texop_tex:case nir_texop_txl:case nir_texop_txb:break;
  case nir_texop_tg4:if(getenv("TEGU_AIR_PLAN_EXPLORE"))break;
   snprintf(why,cap,"air plan: texture op %d outside the plan subset (sample, sample level, read, size)",(int)tex->op);return why;
  default:
   snprintf(why,cap,"air plan: texture op %d outside the plan subset (sample, sample level, read, size)",(int)tex->op);return why;
  }
  if(dim==4)return "air plan: multisample sample";
  /* a sample: through a runtime sampler slot or a constexpr sampler */
  const struct air_resource *res=NULL;
  for(unsigned k=0;k<info->num_resources;k++)
   if(info->resources[k].kind==AIR_RESOURCE_SAMPLER&&info->resources[k].index==sidx)res=&info->resources[k];
  if(!res)return "air plan: sampler without a resource";
  if(texel&&!res->inline_sampler)return "air plan: integer texture through a runtime sampler (SamplerState not restated)";
  int off=nir_tex_instr_src_index(tex,nir_tex_src_offset);
  if(off>=0&&!nir_src_is_const(tex->src[off].src))return "air plan: dynamic texel offset";
  if(off>=0&&!res->inline_sampler)return "air plan: texel offset through a runtime sampler (SamplerState fields not restated)";
  if(nir_tex_instr_src_index(tex,nir_tex_src_min_lod)>=0)return "air plan: min LOD clamp";
  struct airp_combined c={.texture=t,.integer=texel!=0};
  tx->sample=true;tx->constant_lanes=true;tx->srgb=!texel;
  if(res->inline_sampler) {
   const struct air_sampler_state *ss=&res->sampler;
   c.constexpr_=true;c.word=airp_word(res->sampler_state);
   /* the subset vk_sampler is restated for (airp_immutable) */
   if(ss->max_anisotropy>1)return "air plan: constexpr anisotropic sampler";
   if(ss->mag_filter==AIR_SAMPLER_BICUBIC||ss->min_filter==AIR_SAMPLER_BICUBIC)return "air plan: constexpr bicubic sampler";
   if(ss->compare_func!=0&&ss->compare_func!=8)return "air plan: constexpr compare sampler";
   if(ss->reduction)return "air plan: constexpr reduction sampler";
   if((res->sampler_state>>60)&7)return "air plan: constexpr sampler word";
   if(!texel&&(ss->mag_filter==AIR_SAMPLER_LINEAR||ss->min_filter==AIR_SAMPLER_LINEAR))tx->ycbcr=true;
   bool seen=false;
   for(unsigned k=0;k<st->nconstexpr;k++)if(st->constexpr_words[k]==c.word)seen=true;
   if(!seen){if(st->nconstexpr>=ARRAY_SIZE(st->constexpr_words))return "air plan: too many constexpr samplers";st->constexpr_words[st->nconstexpr++]=c.word;}
  } else {
   if(sidx>15)return "air plan: sampler slot above 15";
   struct airp_sampler *sm=airp_sampler(st,sidx);if(!sm)return "air plan: too many samplers";
   sm->used=sm->coords=true;tx->ycbcr=!texel;
   c.slot=sidx;
  }
  if(!airp_combined(st,&c))return "air plan: too many combined samplers";
 }
 return NULL;
}

/* ---- outputs, inputs ---- */
/* every component the framebuffer fetch of `loc`'s same component (through
 * movs and float width conversions) */
static bool airp_self_copy(nir_def *d,int loc) {
 for(unsigned c=0;c<d->num_components;c++) {
  nir_scalar x=nir_scalar_chase_movs(nir_get_scalar(d,c));
  for(;;) {
   if(!nir_scalar_is_alu(x))break;
   nir_op op=nir_scalar_alu_op(x);
   if(op!=nir_op_f2f16&&op!=nir_op_f2f32&&op!=nir_op_f2f16_rtne&&op!=nir_op_f2f16_rtz)break;
   x=nir_scalar_chase_movs(nir_scalar_chase_alu_src(x,0));
  }
  if(!nir_scalar_is_intrinsic(x))return false;
  nir_intrinsic_instr *in=nir_def_as_intrinsic(x.def);
  if(in->intrinsic!=nir_intrinsic_load_output||!nir_intrinsic_io_semantics(in).fb_fetch_output||
     (int)nir_intrinsic_io_semantics(in).location!=loc||x.comp+nir_intrinsic_component(in)!=c)return false;
 }
 return true;
}
static bool airp_poison(nir_def *d) {
 nir_scalar comps[NIR_MAX_VEC_COMPONENTS];
 for(unsigned c=0;c<d->num_components;c++) {
  nir_scalar x=nir_scalar_chase_movs(nir_get_scalar(d,c));
  if(nir_def_instr(x.def)->type==nir_instr_type_undef)continue;
  if(!nir_scalar_is_const(x))return false;
  uint64_t v=nir_scalar_as_uint(x);
  bool nan=x.def->bit_size==16?((v&0x7c00)==0x7c00&&(v&0x3ff)):x.def->bit_size==32?((v&0x7f800000)==0x7f800000&&(v&0x7fffff)):false;
  if(!nan)return false;
  comps[c]=x;
 }
 (void)comps;return true;
}
static const char *airp_io(struct airp_stage *st,nir_shader *s,const struct air_shader_info *info) {
 if(st->stage==TEGU_GRAPHICS_FRAGMENT)st->early_tests=s->info.fs.early_fragment_tests;
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(instr,block)
  if(instr->type==nir_instr_type_intrinsic&&nir_instr_as_intrinsic(instr)->intrinsic==nir_intrinsic_load_frag_coord&&
     (nir_def_components_read(&nir_instr_as_intrinsic(instr)->def)&0xc))st->position_depth=true;
 nir_foreach_function_impl(impl,s)nir_foreach_block(block,impl)nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(i->intrinsic!=nir_intrinsic_store_deref&&i->intrinsic!=nir_intrinsic_load_deref)continue;
  nir_variable *var=nir_deref_instr_get_variable(nir_src_as_deref(i->src[0]));
  if(!var)continue;
  if(st->stage==TEGU_GRAPHICS_FRAGMENT&&var->data.mode==nir_var_shader_out&&i->intrinsic==nir_intrinsic_store_deref) {
   int loc=var->data.location;
   if(loc==FRAG_RESULT_DEPTH)return "air plan: depth output";
   if(loc==FRAG_RESULT_STENCIL)return "air plan: stencil output";
   if(loc==FRAG_RESULT_SAMPLE_MASK)return "air plan: sample mask output";
   if(loc>=FRAG_RESULT_DATA0&&loc<=FRAG_RESULT_DATA7) {
    if(var->data.index)return "air plan: dual-source colour output";
    /* A store of NaN constants or undef is no write: every AIR function is
     * nnan, so such a value is poison, which metal2vulkan's entry does not
     * output (QuartzCore's fixed_frag returns half4(NaN) for an attachment its
     * constants leave out). */
    /* A store of the attachment's own fetched value is no write either (the
     * entry keeps what the attachment holds). */
    if(!airp_poison(i->src[1].ssa)&&!airp_self_copy(i->src[1].ssa,loc))st->outputs|=1u<<(loc-FRAG_RESULT_DATA0);
   }
  }
  /* link/graphics.rs reads_flat_inputs: a flat user varying the function reads
   * (metal2vulkan also passes varyings it proves uniform flat: an optimisation
   * of its own, not stated here) */
  if(st->stage==TEGU_GRAPHICS_FRAGMENT&&var->data.mode==nir_var_shader_in&&i->intrinsic==nir_intrinsic_load_deref&&
     var->data.interpolation==INTERP_MODE_FLAT)st->flat_inputs=true;
  if(st->stage==TEGU_GRAPHICS_FRAGMENT&&var->data.mode==nir_var_system_value&&i->intrinsic==nir_intrinsic_load_deref&&
     var->data.location==SYSTEM_VALUE_FRAG_COORD&&(nir_def_components_read(&i->def)&0xc))st->position_depth=true;
  if(st->stage==TEGU_GRAPHICS_VERTEX&&var->data.mode==nir_var_shader_out&&i->intrinsic==nir_intrinsic_store_deref&&
     var->data.location==VARYING_SLOT_PSIZ)st->point_size=true;
  if(st->stage==TEGU_GRAPHICS_FRAGMENT&&var->data.mode==nir_var_shader_in&&var->data.sample)return "air plan: sample-rate varying";
  if(st->stage==TEGU_GRAPHICS_VERTEX&&var->data.mode==nir_var_shader_in&&i->intrinsic==nir_intrinsic_load_deref) {
   int a=var->data.location-VERT_ATTRIB_GENERIC0;
   if(a<0||a>30)return "air plan: vertex input outside the generic attributes";
   st->attributes|=1u<<a;st->attribute_type[a]=AIR_ATTRIB_FLOAT;
   for(unsigned k=0;k<info->num_vertex_attributes;k++)
    if(info->vertex_attributes[k].index==(unsigned)a)st->attribute_type[a]=info->vertex_attributes[k].base_type;
  }
  if(st->stage==TEGU_GRAPHICS_VERTEX&&var->data.mode==nir_var_shader_out&&i->intrinsic==nir_intrinsic_store_deref) {
   int loc=var->data.location;
   if(loc==VARYING_SLOT_LAYER||loc==VARYING_SLOT_VIEWPORT)return "air plan: layer/viewport output";
   if(loc==VARYING_SLOT_CLIP_DIST0||loc==VARYING_SLOT_CLIP_DIST1)return "air plan: clip distances";
  }
 }
 return NULL;
}

/* ---- format tables (only the entries checked against metal2vulkan) ---- */
/* MTLPixelFormat -> VkFormat of a colour attachment (link/graphics.rs vk_format). */
static int airp_pixel_vk(uint32_t pf) {
 switch(pf) {
 case 10:return 9;     /* R8Unorm -> R8_UNORM */
 case 43:return 8;     /* BGR5A1Unorm -> A1R5G5B5_UNORM_PACK16 */
 case 55:return 100;   /* R32Float -> R32_SFLOAT */
 case 70:return 37;    /* RGBA8Unorm -> R8G8B8A8_UNORM */
 case 71:return 43;    /* RGBA8Unorm_sRGB -> R8G8B8A8_SRGB */
 case 80:return 44;    /* BGRA8Unorm -> B8G8R8A8_UNORM */
 case 81:return 50;    /* BGRA8Unorm_sRGB -> B8G8R8A8_SRGB */
 case 115:return 97;   /* RGBA16Float -> R16G16B16A16_SFLOAT */
 case 25:return 76;    /* R16Float -> R16_SFLOAT */
 case 65:return 83;    /* RG16Float -> R16G16_SFLOAT */
 case 30:return 16;    /* RG8Unorm -> R8G8_UNORM */
 case 105:return 103;  /* RG32Float -> R32G32_SFLOAT */
 case 125:return 109;  /* RGBA32Float -> R32G32B32A32_SFLOAT */
 /* integer classes (no blend, no Float input attachment) */
 case 13:return 13;case 14:return 14;case 33:return 20;case 34:return 21;case 73:return 41;case 74:return 42;
 case 23:return 74;case 24:return 75;case 63:return 81;case 64:return 82;case 113:return 95;case 114:return 96;
 case 53:return 98;case 54:return 99;case 103:return 101;case 104:return 102;case 123:return 107;case 124:return 108;
 default:return -1;
 }
}
static bool airp_float_format(uint32_t pf) {
 switch(pf){case 10:case 43:case 55:case 70:case 71:case 80:case 81:case 115:case 25:case 65:case 30:case 105:case 125:return true;default:return false;}
}
/* MTLVertexFormat -> VkFormat (metal/vertex.rs vk_vertex_format), float class only;
 * *align is metal2vulkan's vertex_alignment (the component size, at most 4). */
static int airp_vertex_vk(uint32_t vf,unsigned *align) {
 switch(vf) {
 case 9:*align=1;return 37;   /* UChar4Normalized -> R8G8B8A8_UNORM */
 case 42:*align=1;return 44;  /* UChar4Normalized_BGRA -> B8G8R8A8_UNORM */
 case 25:*align=2;return 83;  /* Half2 -> R16G16_SFLOAT */
 case 27:*align=2;return 97;  /* Half4 -> R16G16B16A16_SFLOAT */
 case 53:*align=2;return 76;  /* Half -> R16_SFLOAT */
 case 28:*align=4;return 100; /* Float -> R32_SFLOAT */
 case 29:*align=4;return 103; /* Float2 -> R32G32_SFLOAT */
 case 30:*align=4;return 106; /* Float3 -> R32G32B32_SFLOAT */
 case 31:*align=4;return 109; /* Float4 -> R32G32B32A32_SFLOAT */
 default:return -1;
 }
}
/* MTLBlendFactor -> VkBlendFactor (no constant colour, no dual source). */
static int airp_blend_factor(uint32_t f) {
 static const int8_t map[]={0,1,2,3,6,7,4,5,8,9,14};
 return f<ARRAY_SIZE(map)?map[f]:-1;
}

/* ---- the immutable sampler of a constexpr sampler (metal::sampler::vk_sampler) ----
 * Restated for normalized, isotropic, non-comparing, weighted-average samplers
 * whose LOD clamps are the defaults (0, 65504): the grids then give
 * NotMipmapped (0, 0.25), Nearest and Linear (0, 15). */
static const char *airp_immutable(struct airp_desc_buf *o,uint64_t word,bool integer) {
 unsigned addr[3];
 unsigned border=(word>>56)&3;
 for(unsigned k=0;k<3;k++) {
  switch((word>>(3*k))&7) {
  case 0:addr[k]=3;break;      /* ClampToZero / ClampToBorder -> CLAMP_TO_BORDER */
  case 1:addr[k]=2;break;      /* ClampToEdge */
  case 2:addr[k]=0;break;      /* Repeat */
  case 3:addr[k]=1;break;      /* MirroredRepeat */
  default:return "air plan: constexpr address mode";
  }
 }
 unsigned mag=(word>>9)&3,min=(word>>11)&3,mip=(word>>13)&3;
 unsigned lo=(word>>24)&0xffff,hi=(word>>40)&0xffff;
 if(lo!=0||hi!=0x7bff)return "air plan: constexpr LOD clamps outside the plan table";
 if((word>>16)&0xf&&((word>>16)&0xf)!=8)return "air plan: constexpr compare";
 if(border&&!(addr[0]==3||addr[1]==3||addr[2]==3))border=0;
 if(border)return "air plan: constexpr border colour other than transparent black";
 /* vk_sampler at the default clamps (0, 65504): NotMipmapped (0, 0.25); Nearest
  * (nearest_level: 0, 15); Linear (level_on_grid: 0, 65504); an integer texture's
  * Nearest/Linear keep the clamps, its filters and mip mode are nearest, its border
  * the integer transparent black (1). */
 const char *max_lod=mip==0?"0.25":(mip==1&&!integer)?"15.0":"65504.0";
 if(integer){mag=0;min=0;}
 airp_put(o,"{\"address_mode_u\":%u,\"address_mode_v\":%u,\"address_mode_w\":%u,\"border_color\":%u,\"border_components\":null,"
  "\"compare\":null,\"mag_filter\":%u,\"max_anisotropy\":null,\"max_lod\":%s,\"min_filter\":%u,\"min_lod\":0.0,\"mip_lod_bias\":0.0,"
  "\"mipmap_mode\":%u,\"reduction\":0}",addr[0],addr[1],addr[2],integer?1u:0u,mag,max_lod,min,(mip==2&&!integer)?1u:0u);
 return NULL;
}
static void airp_modes(struct airp_desc_buf *o,uint64_t word) {
 static const char *const names[]={"ClampToZero","ClampToEdge","Repeat","MirrorRepeat"};
 unsigned border=(word>>56)&3;
 airp_put(o,"[");
 for(unsigned k=0;k<3;k++){unsigned m=(word>>(3*k))&7;airp_put(o,"%s\"%s\"",k?",":"",m==0&&border?"ClampToBorderColor":names[m&3]);}
 airp_put(o,"]");
}

/* ---- one pipeline's metadata ---- */
struct airp_binding { unsigned stage,kind,index,ty; const struct airp_combined *c; };
static int airp_binding_cmp(const void *a,const void *b) {
 const struct airp_binding *x=a,*y=b;
#define AIRP_CMP(f) if(x->f!=y->f)return x->f<y->f?-1:1
 /* Stage (Vertex < Fragment), then kind, then for a combined binding its
  * CombinedSampler (SamplerRef Slot < Constexpr, slot / words, compare,
  * integer, cube_gradient, raw), then the index. */
 unsigned xs=x->stage==TEGU_GRAPHICS_VERTEX?0:1,ys=y->stage==TEGU_GRAPHICS_VERTEX?0:1;
 if(xs!=ys)return xs<ys?-1:1;
 AIRP_CMP(kind);
 if(x->kind==AIRP_K_COMBINED) {
  const struct airp_combined *p=x->c,*q=y->c;
  if(p->constexpr_!=q->constexpr_)return p->constexpr_?1:-1;
  if(!p->constexpr_&&p->slot!=q->slot)return p->slot<q->slot?-1:1;
  if(p->constexpr_&&p->word!=q->word)return p->word<q->word?-1:1;
  if(p->compare!=q->compare)return p->compare?1:-1;
  if(p->integer!=q->integer)return p->integer?1:-1;
 }
 AIRP_CMP(index);
#undef AIRP_CMP
 return 0;
}
/* window of a used buffer slot (memory/window.rs) */
static bool airp_window(const struct airp_buffer *b,int64_t *start,uint64_t *len) {
 if(!b->unknown){*start=b->lo;*len=b->hi>b->lo?(uint64_t)(b->hi-b->lo):0;return true;}
 if(b->size){*start=0;*len=b->size;return true;}
 return false;
}
static bool airp_uniform(const struct airp_buffer *b) {
 int64_t start;uint64_t len;
 if(!b->constant||b->write||!airp_window(b,&start,&len))return false;
 uint64_t range=(len+AIRP_UNIFORM_ALIGN-1+15)&~15ull;
 return range<=AIRP_MAX_UNIFORM_RANGE;
}
static void airp_fact(struct airp_desc_buf *o,bool *first,const char *name,unsigned stage,unsigned cls,unsigned index,const char *mech) {
 airp_put(o,"%s{\"fact\":{\"%s\":{\"slot\":",*first?"":",",name);airp_slot(o,stage,cls,index);airp_put(o,"}},\"mechanism\":\"%s\"}",mech);*first=false;
}
static int airp_tex_cmp(const void *a,const void *b){const struct airp_texture *x=a,*y=b;return x->index<y->index?-1:x->index>y->index;}
static int airp_buf_cmp(const void *a,const void *b){const struct airp_buffer *x=a,*y=b;return x->index<y->index?-1:x->index>y->index;}
static int airp_smp_cmp(const void *a,const void *b){const struct airp_sampler *x=a,*y=b;return x->index<y->index?-1:x->index>y->index;}

static const char *airp_emit(struct airp_desc_buf *o,struct airp_stage *st,const struct tegu_mali_air_render_desc *d) {
 const char *why;
 for(unsigned k=0;k<2;k++) {
  qsort(st[k].b,st[k].nb,sizeof(st[k].b[0]),airp_buf_cmp);
  qsort(st[k].t,st[k].nt,sizeof(st[k].t[0]),airp_tex_cmp);
  qsort(st[k].s,st[k].ns,sizeof(st[k].s[0]),airp_smp_cmp);
  unsigned storage=0;
  for(unsigned i=0;i<st[k].nb;i++)if(!airp_uniform(&st[k].b[i]))storage++;
  if(storage>4)return "air plan: more storage buffers than the plan restates (per-stage fallback)";
 }
 /* bindings */
 struct airp_binding bind[64];unsigned nbind=0;
 for(unsigned k=0;k<2;k++) {
  struct airp_stage *s=&st[k];
  for(unsigned i=0;i<s->nb;i++)bind[nbind++]=(struct airp_binding){s->stage,AIRP_K_BUFFER,s->b[i].index,airp_uniform(&s->b[i])?6:7,NULL};
  for(unsigned i=0;i<s->nt;i++)if(s->t[i].read||s->t[i].query)bind[nbind++]=(struct airp_binding){s->stage,AIRP_K_TEXTURE,s->t[i].index,2,NULL};
  for(unsigned i=0;i<8;i++)if(s->fbfetch&(1u<<i))bind[nbind++]=(struct airp_binding){s->stage,AIRP_K_INPUT,i,10,NULL};
  for(unsigned i=0;i<s->nc;i++)bind[nbind++]=(struct airp_binding){s->stage,AIRP_K_COMBINED,s->c[i].texture,1,&s->c[i]};
 }
 if(nbind>TEGU_MALI_GENERIC_MAX_BINDINGS)return "air plan: more bindings than the generic plan holds";
 qsort(bind,nbind,sizeof(bind[0]),airp_binding_cmp);
 airp_put(o,"\"layout\":{\"push_constants\":[");
 bool first=true;
 for(unsigned k=0;k<2;k++)if(st[k].nb) {
  airp_put(o,"%s{\"offset\":%u,\"size\":%u,\"stages\":%u}",first?"":",",k?64u:0u,4u*st[k].nb,k?16u:1u);first=false;
 }
 airp_put(o,"],\"push_descriptor_eligible\":%s,\"sets\":[{\"bindings\":[",nbind<=AIRP_MAX_PUSH_DESCRIPTORS?"true":"false");
 for(unsigned i=0;i<nbind;i++) {
  const struct airp_binding *b=&bind[i];
  airp_put(o,"%s{\"binding\":%u,\"count\":1,\"flags\":0,\"immutable_sampler\":",i?",":"",i);
  if(b->kind==AIRP_K_COMBINED&&b->c->constexpr_){if((why=airp_immutable(o,b->c->word,b->c->integer)))return why;}
  else airp_put(o,"null");
  airp_put(o,",\"source\":");
  unsigned cls=b->kind==AIRP_K_BUFFER?AIRP_SLOT_BUFFER:AIRP_SLOT_TEXTURE;
  switch(b->kind) {
  case AIRP_K_BUFFER:case AIRP_K_TEXTURE:
   airp_put(o,"{\"%s\":",b->kind==AIRP_K_BUFFER?"Buffer":"Texture");airp_slot(o,b->stage,cls,b->index);airp_put(o,"}");break;
  case AIRP_K_INPUT:airp_put(o,"{\"InputAttachment\":%u}",b->index);break;
  default:
   airp_put(o,"{\"CombinedTextureSampler\":{\"sampler\":{\"compare\":%s,\"cube_gradient\":false,\"integer\":%s,\"raw\":false,\"sampler\":",
    b->c->compare?"true":"false",b->c->integer?"true":"false");
   if(b->c->constexpr_)airp_put(o,"{\"Constexpr\":[%llu,0]}",(unsigned long long)b->c->word);
   else airp_put(o,"{\"Slot\":%u}",b->c->slot);
   airp_put(o,"},\"texture\":");airp_slot(o,b->stage,AIRP_SLOT_TEXTURE,b->index);airp_put(o,"}}");
  }
  airp_put(o,",\"stages\":%u,\"ty\":%u}",b->stage==TEGU_GRAPHICS_VERTEX?1u:16u,b->ty);
 }
 airp_put(o,"],\"flags\":0,\"set\":0}]}");
 /* interface */
 airp_put(o,",\"interface\":{\"needs_handle_translation\":false,\"needs_pointer_translation\":false,\"numbered_samplers\":[");
 for(unsigned k=0;k<2;k++) {
  airp_put(o,"%s[\"%s\",[",k?",":"",airp_stage_name(st[k].stage));
  bool f2=true;
  /* constexpr samplers in the reverse of the module's !air.sampler_states order
   * (air/ appends them to the resources in that order) */
  for(int i=(int)st[k].nmodule-1;i>=0;i--) {
   bool used=false;
   for(unsigned j=0;j<st[k].nconstexpr;j++)if(st[k].constexpr_words[j]==st[k].module_words[i])used=true;
   bool dup=false;
   for(int j=(int)st[k].nmodule-1;j>i;j--)if(st[k].module_words[j]==st[k].module_words[i])dup=true;
   if(!used||dup)continue;
   airp_put(o,"%s{\"Constexpr\":[%llu,0]}",f2?"":",",(unsigned long long)st[k].module_words[i]);f2=false;
  }
  if(st[k].reads_texel){airp_put(o,"%s\"Read\"",f2?"":",");f2=false;}
  airp_put(o,"]]");
 }
 airp_put(o,"],\"push\":[");
 for(unsigned k=0;k<2;k++) {
  unsigned base=k?64u:0u;
  if(st[k].nb>16)return "air plan: push window spill (argument table)";
  airp_put(o,"%s{\"argument_table_size\":0,\"fields\":[",k?",":"");
  for(unsigned i=0;i<st[k].nb;i++) {
   airp_put(o,"%s{\"field\":{\"SlotBindOffset\":",i?",":"");airp_slot(o,st[k].stage,AIRP_SLOT_BUFFER,st[k].b[i].index);
   airp_put(o,"},\"loc\":{\"Push\":{\"offset\":%u}},\"size\":4}",base+4u*i);
  }
  if(st[k].nb)airp_put(o,"],\"push_range\":[%u,%u],\"stage\":\"%s\"}",base,4u*st[k].nb,airp_stage_name(st[k].stage));
  else airp_put(o,"],\"push_range\":null,\"stage\":\"%s\"}",airp_stage_name(st[k].stage));
 }
 airp_put(o,"],\"reaches_command_buffers\":false,\"slots\":[");
 first=true;
 for(unsigned k=0;k<2;k++) {
  struct airp_stage *s=&st[k];
  /* SlotRef order: class Buffer < Texture < Sampler, then index */
  for(unsigned i=0;i<s->nb;i++) {
   const struct airp_buffer *b=&s->b[i];int64_t start;uint64_t len;bool bounded=airp_window(b,&start,&len);
   airp_put(o,"%s[",first?"":",");first=false;airp_slot(o,s->stage,AIRP_SLOT_BUFFER,b->index);
   airp_put(o,",{\"Buffer\":{\"access\":{\"read\":true,\"write\":false},\"form\":{\"Descriptor\":\"%s\"},\"kind\":\"Buffer\",\"window\":",
    airp_uniform(b)?"UniformWords":"Storage");
   if(bounded)airp_put(o,"{\"Bounded\":{\"len\":%llu,\"start\":%lld}}}}]",(unsigned long long)len,(long long)start);
   else airp_put(o,"\"Whole\"}}]");
  }
  for(unsigned i=0;i<s->nt;i++) {
   const struct airp_texture *t=&s->t[i];
   if(!(t->sample||t->read||t->query))continue;
   static const char *const types[]={"","Type1D","Type2D","Type3D","Type2DMultisample","Cube"};
   static const char *const texels[]={"Float","Sint","Uint"};
   airp_put(o,"%s[",first?"":",");first=false;airp_slot(o,s->stage,AIRP_SLOT_TEXTURE,t->index);
   airp_put(o,",{\"Texture\":{\"access\":{\"read\":%s,\"sample\":%s,\"write\":false},\"depth_compare\":false,\"sampled_type\":\"%s\",\"texture_type\":\"%s\"}}]",
    t->read?"true":"false",t->sample?"true":"false",texels[t->texel<0?0:t->texel],types[t->dim<0?2:t->dim]);
  }
  for(unsigned i=0;i<s->ns;i++) {
   airp_put(o,"%s[",first?"":",");first=false;airp_slot(o,s->stage,AIRP_SLOT_SAMPLER,s->s[i].index);
   airp_put(o,",{\"Sampler\":{\"compare\":false,\"coordinate_sensitive\":%s}}]",s->s[i].coords?"true":"false");
  }
 }
 airp_put(o,"],\"texture_writing_stages\":[],\"threadgroup\":[]}");
 /* dependencies: per stage, BTreeSet<RuntimeFact> order (facts.rs variant order:
  * SamplerCoordinates 10, SamplerReduction 11, BufferRangeLimit 17,
  * FramebufferFetch 19, SrgbTexture 45, ConstantLanes 63, YCbCrTexture 67,
  * ConstexprSampled 69, CubicSampler 70, CrossClassReads 78), slot order inside. */
 airp_put(o,",\"dependencies\":[");
 first=true;
 for(unsigned k=0;k<2;k++) {
  struct airp_stage *s=&st[k];
  /* ResourcePresence (facts.rs variant 7): the AIR code takes a tested texture as
   * bound; its Presence specialization lets the adapter refuse a draw where it is not. */
  for(unsigned i=0;i<32;i++)if(s->presence&(1u<<i))airp_fact(o,&first,"ResourcePresence",s->stage,AIRP_SLOT_TEXTURE,i,"Specialization");
  for(unsigned i=0;i<s->ns;i++)if(s->s[i].coords)airp_fact(o,&first,"SamplerCoordinates",s->stage,AIRP_SLOT_SAMPLER,s->s[i].index,"Specialization");
  for(unsigned i=0;i<s->ns;i++)airp_fact(o,&first,"SamplerReduction",s->stage,AIRP_SLOT_SAMPLER,s->s[i].index,"PlanTimeCheck");
  for(unsigned i=0;i<s->nb;i++)if(!airp_uniform(&s->b[i])){int64_t a;uint64_t l;if(!airp_window(&s->b[i],&a,&l))
   airp_fact(o,&first,"BufferRangeLimit",s->stage,AIRP_SLOT_BUFFER,s->b[i].index,"Variant");}
  for(unsigned i=0;i<8;i++)if(s->fbfetch&(1u<<i)){airp_put(o,"%s{\"fact\":{\"FramebufferFetch\":{\"color\":%u}},\"mechanism\":\"RenderPassContract\"}",first?"":",",i);first=false;}
  for(unsigned i=0;i<s->nt;i++)if(s->t[i].srgb)airp_fact(o,&first,"SrgbTexture",s->stage,AIRP_SLOT_TEXTURE,s->t[i].index,"Variant");
  for(unsigned i=0;i<s->nt;i++)if(s->t[i].constant_lanes)airp_fact(o,&first,"ConstantLanes",s->stage,AIRP_SLOT_TEXTURE,s->t[i].index,"Specialization");
  for(unsigned i=0;i<s->nt;i++)if(s->t[i].ycbcr)airp_fact(o,&first,"YCbCrTexture",s->stage,AIRP_SLOT_TEXTURE,s->t[i].index,"Variant");
  /* ConstexprSampled { slot, modes }: slot first */
  for(unsigned i=0;i<s->nt;i++)for(unsigned j=0;j<s->nc;j++)if(s->c[j].constexpr_&&s->c[j].texture==s->t[i].index) {
   airp_put(o,"%s{\"fact\":{\"ConstexprSampled\":{\"modes\":",first?"":",");airp_modes(o,s->c[j].word);
   airp_put(o,",\"slot\":");airp_slot(o,s->stage,AIRP_SLOT_TEXTURE,s->t[i].index);airp_put(o,"}},\"mechanism\":\"Variant\"}");first=false;
  }
  for(unsigned i=0;i<s->ns;i++)airp_fact(o,&first,"CubicSampler",s->stage,AIRP_SLOT_SAMPLER,s->s[i].index,"Variant");
  for(unsigned i=0;i<s->nt;i++)if(s->t[i].sample||s->t[i].read||s->t[i].query)
   airp_fact(o,&first,"CrossClassReads",s->stage,AIRP_SLOT_TEXTURE,s->t[i].index,"Variant");
 }
 airp_put(o,"]");
 /* stages: specialization constants, ids in SpecKey order over both stages:
  * SamplerState (16) before ConstantLanes (25), then the slot. */
 struct airp_key { unsigned stage,kind,index; } keys[160];unsigned nkeys=0;
 /* SpecKey order: Presence (5) < SamplerState (16) < ConstantLanes (25), then the slot */
 for(unsigned kind=0;kind<3;kind++)for(unsigned k=0;k<2;k++) {
  struct airp_stage *s=&st[k];
  if(kind==0){for(unsigned i=0;i<32;i++)if(s->presence&(1u<<i))keys[nkeys++]=(struct airp_key){s->stage,2,i};}
  else if(kind==1){for(unsigned i=0;i<s->ns;i++)if(s->s[i].coords)keys[nkeys++]=(struct airp_key){s->stage,0,s->s[i].index};}
  else for(unsigned i=0;i<s->nt;i++)if(s->t[i].constant_lanes)keys[nkeys++]=(struct airp_key){s->stage,1,s->t[i].index};
 }
 airp_put(o,",\"stages\":[");
 for(unsigned k=0;k<2;k++) {
  airp_put(o,"%s{\"constant_blob\":null,\"entry_point\":\"main\",\"specialization\":{\"constants\":[",k?",":"");
  bool f2=true;
  for(unsigned n=0;n<nkeys;n++) {
   if(keys[n].stage!=st[k].stage)continue;
   airp_put(o,"%s{\"default\":%u,\"id\":%u,\"key\":",f2?"":",",keys[n].kind==2?1u:0u,n);f2=false;
   if(keys[n].kind==2){airp_put(o,"{\"Presence\":");airp_slot(o,keys[n].stage,AIRP_SLOT_TEXTURE,keys[n].index);
    airp_put(o,"},\"size\":4,\"unread_bits\":0}");continue;}
   if(keys[n].kind==0){airp_put(o,"{\"SamplerState\":{\"fields\":2147483655,\"slot\":");airp_slot(o,keys[n].stage,AIRP_SLOT_SAMPLER,keys[n].index);
    airp_put(o,"}},\"size\":4,\"unread_bits\":4294967288}");}
   else {airp_put(o,"{\"ConstantLanes\":");airp_slot(o,keys[n].stage,AIRP_SLOT_TEXTURE,keys[n].index);airp_put(o,"},\"size\":4,\"unread_bits\":0}");}
  }
  airp_put(o,"]},\"stage\":%u}",k?16u:1u);
 }
 airp_put(o,"]");
 /* vertex input (link/vertex.rs: bindings dense by buffer index, attribute
  * locations dense in attribute order) */
 struct airp_stage *vs=&st[0];
 uint32_t buffers=0;
 airp_put(o,",\"vertex_input\":{\"attributes\":[");
 unsigned loc=0;first=true;
 for(unsigned a=0;a<31;a++)if(vs->attributes&(1u<<a)) {
  const struct tegu_mali_air_vertex_attribute *at=NULL;const struct tegu_mali_air_vertex_layout *ly=NULL;
  for(uint32_t i=0;i<d->attribute_count;i++)if(d->attributes[i].index==a)at=&d->attributes[i];
  if(!at)return "air plan: vertex attribute the descriptor lacks";
  for(uint32_t i=0;i<d->layout_count;i++)if(d->layouts[i].index==at->buffer_index)ly=&d->layouts[i];
  if(!ly)return "air plan: vertex layout the descriptor lacks";
  unsigned align=0;int vk=airp_vertex_vk(at->format,&align);
  if(vk<0)return "air plan: vertex format outside the plan table";
  if(vs->attribute_type[a]!=AIR_ATTRIB_FLOAT)return "air plan: integer vertex attribute";
  if(at->offset%align||ly->stride%align||!ly->stride||ly->stride>AIRP_MAX_VERTEX_STRIDE||ly->step_function!=1||ly->step_rate!=1)
   return "air plan: pulled vertex attribute (alignment, stride or step)";
  if(at->buffer_index>30)return "air plan: vertex buffer index";
  buffers|=1u<<at->buffer_index;
 }
 for(unsigned a=0;a<31;a++)if(vs->attributes&(1u<<a)) {
  const struct tegu_mali_air_vertex_attribute *at=NULL;
  for(uint32_t i=0;i<d->attribute_count;i++)if(d->attributes[i].index==a)at=&d->attributes[i];
  unsigned align=0;int vk=airp_vertex_vk(at->format,&align);
  unsigned binding=__builtin_popcount(buffers&((1u<<at->buffer_index)-1));
  airp_put(o,"%s{\"binding\":%u,\"format\":%d,\"location\":%u,\"offset\":%u}",first?"":",",binding,vk,loc++,at->offset);first=false;
 }
 airp_put(o,"],\"bindings\":[");
 first=true;
 for(unsigned bi=0,n=0;bi<31;bi++)if(buffers&(1u<<bi)) {
  const struct tegu_mali_air_vertex_layout *ly=NULL;
  for(uint32_t i=0;i<d->layout_count;i++)if(d->layouts[i].index==bi)ly=&d->layouts[i];
  airp_put(o,"%s{\"binding\":%u,\"buffer_index\":%u,\"divisor\":1,\"input_rate\":0,\"stride\":{\"Static\":%u}}",first?"":",",n++,bi,ly->stride);first=false;
 }
 airp_put(o,"],\"pulled\":[],\"variant_pulled\":[]}");
 /* graphics state (link/graphics.rs graphics_state, the subset above) */
 struct airp_stage *fs=&st[1];
 airp_put(o,",\"graphics\":{\"alpha_to_coverage\":false,\"alpha_to_one\":false,\"clip_distances\":0,\"color_attachments\":[");
 unsigned ntargets=d->target_count?d->target_count:1;uint32_t attached=0;
 for(unsigned i=0;i<ntargets;i++)if(d->target_count?d->targets[i].format!=0:d->color_format!=0)attached|=1u<<i;
 for(unsigned i=0;i<ntargets;i++) {
  uint32_t pf=d->target_count?d->targets[i].format:d->color_format,mask=d->target_count?d->targets[i].write_mask:(d->blends?d->blends[0].write_mask:15);
  const struct tegu_mali_air_blend *bl=d->blends?&d->blends[d->target_count?i:0]:NULL;
  if(!pf){airp_put(o,"%snull",i?",":"");continue;}
  int vk=airp_pixel_vk(pf);
  if(vk<0)return "air plan: colour attachment format outside the plan table";
  if(mask>15)return "air plan: write mask";
  uint32_t written=(fs->outputs&(1u<<i))?mask:0;
  airp_put(o,"%s{\"blend\":",i?",":"");
  if(bl&&bl->enabled&&!airp_float_format(pf))return "air plan: blend on an integer attachment";
  if(bl&&bl->enabled) {
   int f[4]={airp_blend_factor(bl->rgb_src),airp_blend_factor(bl->rgb_dst),airp_blend_factor(bl->alpha_src),airp_blend_factor(bl->alpha_dst)};
   if(f[0]<0||f[1]<0||f[2]<0||f[3]<0||bl->rgb_op>4||bl->alpha_op>4)return "air plan: blend factor outside the plan table (constant colour, dual source)";
   airp_put(o,"{\"alpha_op\":%u,\"color_op\":%u,\"dst_alpha\":%d,\"dst_color\":%d,\"src_alpha\":%d,\"src_color\":%d}",bl->alpha_op,bl->rgb_op,f[3],f[1],f[2],f[0]);
  } else airp_put(o,"null");
  airp_put(o,",\"format\":%d,\"write_mask\":%u}",vk,written);
 }
 airp_put(o,"],\"color_outputs\":%u,\"color_write_rules\":[],\"declares_layer\":false,\"depth_format\":null,"
  "\"dynamic_states\":[0,1,3,4,6,7,8,1000267002,1000377004,1000267000,1000267001,1000267006,1000267007,1000267008,1000267010,1000267011],"
  "\"early_fragment_tests\":%s,\"flat_fragment_inputs\":%s,\"fragment_imageblock\":null,\"fragment_writes_memory\":false,\"input_attachments\":[",
  fs->outputs&attached,fs->early_tests?"true":"false",fs->flat_inputs?"true":"false");
 first=true;
 for(unsigned i=0;i<8;i++)if(fs->fbfetch&(1u<<i)) {
  if(i>=ntargets)return "air plan: framebuffer fetch of an attachment the pipeline lacks";
  {uint32_t pf=d->target_count?d->targets[i].format:d->color_format;
   if(!airp_float_format(pf))return "air plan: framebuffer fetch of an integer attachment";}
  airp_put(o,"%s{\"color_index\":%u,\"format_class\":\"Float\",\"input_attachment_index\":%u}",first?"":",",i,i);first=false;
 }
 airp_put(o,"],\"max_vertex_amplification_count\":1,\"mesh_records\":null,\"rasterization_order_attachment_access\":%s,"
  "\"rasterization_samples\":1,\"rasterizer_discard\":false,\"reads_barycentric_coord\":false,\"reads_position_depth\":%s,"
  "\"sample_mask\":null,\"sample_shading\":false,\"shader_tests\":null,\"shadow_stencil\":null,\"stencil_format\":null,"
  "\"storage_attachments\":[],\"tessellation\":null,\"topology\":3,\"vertex_writes_memory\":false,\"writes_depth\":false,"
  "\"writes_point_size\":%s,\"writes_viewport_index\":false}",fs->fbfetch?"true":"false",fs->position_depth?"true":"false",
  st[0].point_size?"true":"false");
 return NULL;
}

int tegu_mali_air_render_metadata(const struct tegu_mali_air_render_desc *d,char **json,size_t *json_size,
 uint32_t *facts,char *error,size_t error_size) {
 if(json)*json=NULL;
 if(json_size)*json_size=0;
 if(facts)*facts=0;
 if(!d||!json||!json_size||!valid_span(d,sizeof(*d))||d->version!=TEGU_MALI_AIR_RENDER_DESC_VERSION||
    d->attribute_count>31||d->layout_count>31||d->target_count>8||
    (d->attribute_count&&!d->attributes)||(d->layout_count&&!d->layouts)||(d->target_count&&!d->targets)||
    (!d->target_count&&!d->color_format))return 1;
 double t0=air_now();
 void *mem=ralloc_context(NULL);
 char why[224]={0};const char *reason=NULL;
 struct airp_stage *st=calloc(2,sizeof(*st));
 if(!st||!mem){free(st);ralloc_free(mem);fail(error,error_size,"air plan: allocation failed");return 3;}
 st[0].stage=TEGU_GRAPHICS_VERTEX;st[1].stage=TEGU_GRAPHICS_FRAGMENT;
 struct air_shader_info vi,fi;memset(&vi,0,sizeof(vi));memset(&fi,0,sizeof(fi));
 /* Each function as metal2vulkan compiles it: the vertex function alone (its
  * outputs all live but those its constants remove), the fragment function
  * without the members its constants remove. An output to an attachment the
  * pipeline lacks stores nowhere (LoweringInputs::unattached_colors) but is
  * still computed there: its reads stay in the interface, and airp_emit
  * leaves it out of color_outputs. */
 nir_shader *fsn=air_stage_base(why,sizeof(why),mem,&d->fragment,MESA_SHADER_FRAGMENT,NULL,0,&fi,&reason);
 nir_shader *vsn=fsn?air_stage_base(why,sizeof(why),mem,&d->vertex,MESA_SHADER_VERTEX,NULL,0,&vi,&reason):NULL;
 if(fsn&&vsn) {
  bool demoted=false;
  nir_foreach_shader_out_variable(var,fsn)
   /* A member its function-constant predicate removes is no output (Metal;
    * metal2vulkan drops it at the entry, with what only it needed). */
   if(var->data.location>=0&&var->data.location<64&&(fi.gated_off_outputs&(1ull<<var->data.location))){var->data.mode=nir_var_shader_temp;demoted=true;}
  {bool vdem=false;
   nir_foreach_shader_out_variable(var,vsn)
    if(var->data.location>=0&&var->data.location<64&&(vi.gated_off_outputs&(1ull<<var->data.location))){var->data.mode=nir_var_shader_temp;vdem=true;}
   if(vdem) {
    nir_fixup_deref_modes(vsn);NIR_PASS(_,vsn,nir_lower_global_vars_to_local);NIR_PASS(_,vsn,nir_lower_vars_to_ssa);
    NIR_PASS(_,vsn,nir_opt_dce);NIR_PASS(_,vsn,nir_opt_dead_cf);
    NIR_PASS(_,vsn,nir_remove_dead_variables,nir_var_shader_temp|nir_var_function_temp,NULL);
   }}
  if(demoted) {
   nir_fixup_deref_modes(fsn);NIR_PASS(_,fsn,nir_lower_global_vars_to_local);NIR_PASS(_,fsn,nir_lower_vars_to_ssa);
   NIR_PASS(_,fsn,nir_opt_dce);NIR_PASS(_,fsn,nir_opt_dead_cf);
   NIR_PASS(_,fsn,nir_remove_dead_variables,nir_var_shader_temp|nir_var_function_temp,NULL);
  }
  st[0].presence=vi.presence_tested_textures;st[1].presence=fi.presence_tested_textures;
  for(unsigned k=0;k<2;k++){const struct air_shader_info *in=k?&fi:&vi;
   for(unsigned r=0;r<in->num_resources&&st[k].nmodule<ARRAY_SIZE(st[k].module_words);r++)
    if(in->resources[r].kind==AIR_RESOURCE_SAMPLER&&in->resources[r].inline_sampler)st[k].module_words[st[k].nmodule++]=airp_word(in->resources[r].sampler_state);}
  if(getenv("TEGU_AIR_PLAN_PRINT")){nir_print_shader(vsn,stderr);nir_print_shader(fsn,stderr);}
  /* buffer sizes from the AIR metadata (air.buffer_size of a reference parameter) */
  const struct air_shader_info *infos[2]={&vi,&fi};nir_shader *ns[2]={vsn,fsn};
  for(unsigned k=0;k<2&&!reason;k++) {
   if((reason=airp_walk(&st[k],ns[k],infos[k],why,sizeof(why))))break;
   if((reason=airp_io(&st[k],ns[k],infos[k])))break;
   for(unsigned i=0;i<st[k].nb;i++)for(unsigned r=0;r<infos[k]->num_resources;r++) {
    const struct air_resource *res=&infos[k]->resources[r];
    if((res->kind==AIR_RESOURCE_CONSTANT_BUFFER||res->kind==AIR_RESOURCE_DEVICE_BUFFER)&&res->index==st[k].b[i].index&&res->argbuf_set==~0u)
     st[k].b[i].size=res->size;
   }
  }
  if(!reason&&st[1].stage==TEGU_GRAPHICS_FRAGMENT&&(st[0].fbfetch))reason="air plan: framebuffer fetch in a vertex function";
 }
 struct airp_desc_buf o={0};
 if(!reason) {
  airp_put(&o,"{\"schema\":1,\"scope\":\"render_compilation_not_native_draw_plan\",\"source\":\"air\",\"variant_bits\":0,"
   "\"air_facts\":{\"vertex_index\":%s,\"instance_index\":%s,\"input_attachments\":%u},",
   st[0].vertex_id?"true":"false",st[0].instance_id?"true":"false",st[1].fbfetch);
  reason=airp_emit(&o,st,d);
  airp_put(&o,"}");
  if(!reason&&o.oom)reason="air plan: allocation failed";
 }
 if(vsn)ralloc_free(vsn);
 if(fsn)ralloc_free(fsn);
 ralloc_free(mem);
 if(facts&&!reason)*facts=(st[0].vertex_id?TEGU_MALI_AIR_FACT_VERTEX_ID:0)|(st[0].instance_id?TEGU_MALI_AIR_FACT_INSTANCE_ID:0)|
  (st[1].fbfetch?TEGU_MALI_AIR_FACT_FBFETCH:0);
 free(st);
 if(getenv("TEGU_AIR_TIMING"))fprintf(stderr,"TEGU_AIR_TIMING plan_cpu_ms=%.2f\n",(air_now()-t0)*1e3);
 if(reason){free(o.p);fail(error,error_size,reason);return 2;}
 *json=o.p;*json_size=o.n;
 return 0;
}
void tegu_mali_air_metadata_free(char *json){free(json);}
