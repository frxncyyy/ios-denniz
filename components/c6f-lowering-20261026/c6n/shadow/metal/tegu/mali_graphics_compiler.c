#include "nord2_target.h"
/* Callable graphics codegen with linked VS/FS generic varyings and, for the
 * textured pair only, one fragment combined image sampler.
 * See the header for native binding requirements and ownership. */
#include "compiler/glsl_types.h"
#include "compiler/spirv/nir_spirv.h"
#include "compiler/spirv/spirv.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/nir/nir_deref.h"
#include "panfrost/compiler/pan_compiler.h"
#include "panfrost/compiler/pan_nir.h"
#include "mali_graphics_compiler.h"
#include "mali_compute_compiler.h"
#include "compiler_trace_sha256.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const uint64_t gpu=NORD2_MALI_COMPILER_ID;
static bool overlaps(const void *,size_t,const void *,size_t);
static bool valid_span(const void *,size_t);
static int glsl_type_size(const struct glsl_type *type, bool bindless) {
 (void)bindless;return glsl_count_attribute_slots(type,false);
}
/* Shared native FAU byte layout: viewport scale0,
 * offset16, shader-output flags32, blend descriptor48; AIR push data128+. */
enum graphics_policy { GRAPHICS_POLICY_LEGACY=0, GRAPHICS_POLICY_ATTACHMENT_CLEAR=1, GRAPHICS_POLICY_MRT=2 };
struct graphics_context { bool failed, keep_output_flags, draw_params; enum graphics_policy policy; unsigned mask, push_bytes, targets_seen; };
static bool graphics_sysvals(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 struct graphics_context *ctx=unused;unsigned offset,bit;
 switch(i->intrinsic) {
 case nir_intrinsic_load_viewport_scale:offset=TEGU_GRAPHICS_VIEWPORT_SCALE;bit=0;break;
 case nir_intrinsic_load_viewport_offset:offset=TEGU_GRAPHICS_VIEWPORT_OFFSET;bit=1;break;
 case nir_intrinsic_load_shader_output_pan:
  // Paired IDVS: bi_compile_variant inlines POSITION|ATTRIB into the
  // position program and VARY into the varying program. Both programs share
  // FAU_0, so a single FAU word would make them take the same branch.
  if(ctx->keep_output_flags)return false;
  offset=TEGU_GRAPHICS_OUTPUT_FLAGS;bit=2;break;
 case nir_intrinsic_load_blend_descriptor_pan:
  if(ctx->policy==GRAPHICS_POLICY_MRT) {
   /* True MRT: one 64-bit internal blend word per target in FS FAU. */
   unsigned rt=nir_intrinsic_base(i);
   if(rt>=TEGU_MALI_GENERIC_MAX_TARGETS){ctx->failed=true;return false;}
   ctx->targets_seen|=1u<<rt;
   offset=TEGU_GRAPHICS_BLEND_DESCRIPTOR+8*rt;
  } else if(ctx->policy==GRAPHICS_POLICY_ATTACHMENT_CLEAR) {
   unsigned rt=nir_intrinsic_base(i);
   if(rt>1){ctx->failed=true;return false;}
   ctx->targets_seen|=1u<<rt;
   offset=TEGU_GRAPHICS_BLEND_DESCRIPTOR+8*rt;
  } else offset=TEGU_GRAPHICS_BLEND_DESCRIPTOR;
  bit=3;break;
 /* Opt-in draw parameters (tegu_mali_compile_generic_pair_options). Without
  * the option these stay unlowered and graphics_driver_sysval_error refuses. */
 case nir_intrinsic_load_first_vertex:
  if(!ctx->draw_params||b->shader->info.stage!=MESA_SHADER_VERTEX)return false;
  offset=TEGU_GRAPHICS_VERTEX_BASE;bit=4;break;
 case nir_intrinsic_load_base_instance:
  if(!ctx->draw_params||b->shader->info.stage!=MESA_SHADER_VERTEX)return false;
  offset=TEGU_GRAPHICS_BASE_INSTANCE;bit=5;break;
 default:return false;
 }
 ctx->mask|=1u<<bit;
 b->cursor=nir_before_instr(&i->instr);
 nir_def_replace(&i->def,nir_load_push_constant(b,i->def.num_components,i->def.bit_size,nir_imm_int(b,offset)));
 return true;
}
static bool graphics_push(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 struct graphics_context *ctx=unused;
 if(i->intrinsic!=nir_intrinsic_load_push_constant)return false;
 unsigned bytes=i->def.num_components*i->def.bit_size/8;
 if(!nir_src_is_const(i->src[0])){ctx->failed=true;return false;}
 uint64_t start=(uint64_t)nir_src_as_uint(i->src[0])+nir_intrinsic_base(i);
 if(bytes>TEGU_GRAPHICS_AIR_PUSH_LIMIT||start>TEGU_GRAPHICS_AIR_PUSH_LIMIT-bytes){ctx->failed=true;return false;}
 if(start+bytes>ctx->push_bytes)ctx->push_bytes=start+bytes;
 b->cursor=nir_before_instr(&i->instr);
 nir_src_rewrite(&i->src[0],nir_iadd_imm(b,i->src[0].ssa,nir_intrinsic_base(i)+TEGU_GRAPHICS_AIR_PUSH));
 nir_intrinsic_set_base(i,0);nir_intrinsic_set_range(i,0);return true;
}
static bool noperspective_left(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)b;(void)unused;return i->intrinsic==nir_intrinsic_load_noperspective_varyings_pan;
}

/* One stage of work. pair=false reproduces the production single-stage path. */
struct stage_job {
 uint32_t requested;mesa_shader_stage stage;
 bool resources;uint32_t resource_code_limit;
 bool vertex_inputs,pair,point_size; // point_size: pair2 VS may write PSIZ
 bool draw_params;                    // VS: VB/BI FAU sysvals (options entry)
 bool allow_tls;                      // options THREAD_STORAGE: caller binds LOCAL_STORAGE TLS
 enum graphics_policy policy;
 uint32_t targets_seen;
 const struct pan_varying_layout *vs_layout; // FS in pair: VS decides layout
 uint32_t noperspective;                     // VS in pair: from FS
 struct pan_shader_info info;uint32_t attribute_mask;
};
static nir_shader *translate_specialized(const void *data,size_t length,mesa_shader_stage stage,int *status,const char **reason,bool resources,bool storage_buffers,
 struct nir_spirv_specialization *specialization,bool prefold) {
 *status=2;*reason="Invalid SPIR-V size or graphics stage";
 if(!data||length<20||length%4||length>1024*1024)return NULL;
 uint32_t *words=malloc(length);
 if(!words){*status=3;*reason="Input allocation failed";return NULL;}
 memcpy(words,data,length);
 if(words[0]!=0x07230203){free(words);*reason="Invalid SPIR-V magic";return NULL;}
 struct spirv_to_nir_options options={.environment=NIR_SPIRV_VULKAN,.skip_os_break_in_debug_build=true};
 if(resources)options.ubo_addr_format=nir_address_format_vec2_index_32bit_offset;
 if(storage_buffers)options.ssbo_addr_format=nir_address_format_vec2_index_32bit_offset;
 nir_shader *s=spirv_to_nir(words,length/4,specialization,stage,"main",&options,pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false));
 free(words);
 *status=4;*reason="SPIR-V translation failed";
 if(!s)return NULL;
 NIR_PASS(_,s,nir_lower_variable_initializers,nir_var_function_temp);
 NIR_PASS(_,s,nir_lower_returns);NIR_PASS(_,s,nir_inline_functions);nir_remove_non_entrypoints(s);
 /* prefold (generic entries): uber shaders such as QuartzCore fixed_frag read
  * their switches from a constant state block; ~38K NIR instructions of which
  * a few hundred are live once it folds. Fold and drop the dead code here, so
  * every later pass (the generic fold loop included) walks only live code.
  * Standard Mesa folding passes that the generic path runs later anyway;
  * outputs are byte-identical on the QuartzCore corpus (compile-speed-20261026). */
 if(prefold)nir_lower_constant_to_temp(s);
 if(storage_buffers)NIR_PASS(_,s,nir_lower_variable_initializers,~0);
 NIR_PASS(_,s,nir_split_var_copies);NIR_PASS(_,s,nir_lower_var_copies);NIR_PASS(_,s,nir_lower_vars_to_ssa);
 NIR_PASS(_,s,nir_lower_system_values);
 if(prefold) {
  bool progress;
  do {
   progress=false;
   NIR_PASS(progress,s,nir_opt_constant_folding);NIR_PASS(progress,s,nir_opt_dead_cf);
   NIR_PASS(progress,s,nir_opt_copy_prop);NIR_PASS(progress,s,nir_opt_dce);
   NIR_PASS(progress,s,nir_opt_remove_phis);
  } while(progress);
 }
 /* Vulkan per-member builtins (e.g. gl_PerVertex Position + PointSize)
  * have independent locations. Split them before gathering/linking; a raw
  * struct offset would report PointSize at COL0 instead of PSIZ. vk_nir.c
  * performs this pass before shader-info collection for the same reason. */
 NIR_PASS(_,s,nir_split_per_member_structs);
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 return s;
}
static nir_shader *translate_ex(const void *data,size_t length,mesa_shader_stage stage,int *status,const char **reason,bool resources,bool storage_buffers) {
 return translate_specialized(data,length,stage,status,reason,resources,storage_buffers,NULL,false);
}
static nir_shader *translate(const void *data,size_t length,mesa_shader_stage stage,int *status,const char **reason) {
 return translate_ex(data,length,stage,status,reason,false,false);
}
/* Declared generic varyings of one interface. Refuses anything outside the
 * qualified smooth float32 subset instead of letting Mesa pick a format. */
/* ext (options entry EXTENDED_VARYINGS): also 32-bit int/uint, flat and
 * noperspective; types[] records 0 float/1 int/2 uint, interp[] the
 * tegu_mali_varying_interp of each declaration. */
static const char *collect_varyings_ex(nir_shader *s,nir_variable_mode mode,uint32_t *mask,uint8_t comps[32],
 bool ext,uint8_t types[32],uint8_t interp[32]) {
 nir_foreach_variable_with_modes(var,s,mode) {
  if(var->data.location<VARYING_SLOT_VAR0) {
   if(mode==nir_var_shader_in)return "Fragment builtin inputs are unsupported";
   continue; // VS builtins are bounded by the outputs_written check
  }
  const struct glsl_type *t=var->type;
  unsigned loc=var->data.location-VARYING_SLOT_VAR0;
  if(loc>=31)return "Varying location outside 0..30";
  enum glsl_base_type base=glsl_get_base_type(t);
  if(!ext&&(!glsl_type_is_vector_or_scalar(t)||base!=GLSL_TYPE_FLOAT))
   return "Varying must be a 32bit float scalar or vector";
  if(ext&&(!glsl_type_is_vector_or_scalar(t)||glsl_get_bit_size(t)!=32||
     (base!=GLSL_TYPE_FLOAT&&base!=GLSL_TYPE_INT&&base!=GLSL_TYPE_UINT)))
   return "Varying must be a 32bit float, int or uint scalar or vector";
  unsigned mode_interp=var->data.interpolation;
  if(!ext&&mode_interp!=INTERP_MODE_NONE&&mode_interp!=INTERP_MODE_SMOOTH)
   return "Only smooth perspective varyings are supported";
  if(ext&&mode_interp!=INTERP_MODE_NONE&&mode_interp!=INTERP_MODE_SMOOTH&&
     mode_interp!=INTERP_MODE_FLAT&&mode_interp!=INTERP_MODE_NOPERSPECTIVE)
   return "Varying interpolation mode is unsupported";
  if(ext&&mode==nir_var_shader_in&&base!=GLSL_TYPE_FLOAT&&mode_interp!=INTERP_MODE_FLAT)
   return "Integer fragment varyings must be flat";
  if(var->data.centroid||var->data.sample||var->data.patch||var->data.per_primitive||
     var->data.per_view||var->data.location_frac)
   return "Centroid/sample/patch/per-primitive/per-view/component varyings unsupported";
  if(var->data.precision==GLSL_PRECISION_MEDIUM||var->data.precision==GLSL_PRECISION_LOW)
   return "Relaxed-precision varyings unsupported";
  if(*mask&BITFIELD_BIT(loc))return "Aliased varying location";
  *mask|=BITFIELD_BIT(loc);comps[loc]=glsl_get_vector_elements(t);
  if(types)types[loc]=base==GLSL_TYPE_FLOAT?0:base==GLSL_TYPE_INT?1:2;
  if(interp)interp[loc]=mode_interp==INTERP_MODE_FLAT?TEGU_MALI_VARYING_FLAT:
   mode_interp==INTERP_MODE_NOPERSPECTIVE?TEGU_MALI_VARYING_NOPERSPECTIVE:TEGU_MALI_VARYING_SMOOTH;
 }
 return NULL;
}
static const char *collect_varyings(nir_shader *s,nir_variable_mode mode,uint32_t *mask,uint8_t comps[32]) {
 return collect_varyings_ex(s,mode,mask,comps,false,NULL,NULL);
}
/* Textured pair only: fragment table0 with sampler0/texture1, the layout the
 * texture-r1 hardware probe qualified. va_tex_handle packs it to 0x10000. */
enum { TEXTURE_TABLE=0, TEXTURE_SAMPLER=0, TEXTURE_INDEX=1 };
/* vtn emits the combined sampler2D var deref, optionally wrapped in a
 * deref_cast to texture2D (texture_deref) or sampler (sampler_deref). Any
 * other deref of a resource variable, or any non-sample use, is refused. */
static bool texture_deref_ok(nir_deref_instr *d,nir_variable *tv,nir_tex_src_type role) {
 if(d->deref_type==nir_deref_type_var)return d->var==tv;
 if(d->deref_type!=nir_deref_type_cast)return false;
 nir_deref_instr *parent=nir_deref_instr_parent(d);
 if(!parent||parent->deref_type!=nir_deref_type_var||parent->var!=tv)return false;
 const struct glsl_type *t=d->type;
 if(role==nir_tex_src_sampler_deref)return glsl_type_is_bare_sampler(t);
 return glsl_type_is_texture(t)&&glsl_get_sampler_dim(t)==GLSL_SAMPLER_DIM_2D&&
  !glsl_sampler_type_is_array(t)&&glsl_get_sampler_result_type(t)==GLSL_TYPE_FLOAT;
}
static bool texture_deref_use_ok(nir_deref_instr *d) {
 nir_foreach_use_including_if(use,&d->def) {
  if(nir_src_is_if(use))return false;
  nir_instr *user=nir_src_use_instr(use);
  if(user->type==nir_instr_type_deref&&d->deref_type==nir_deref_type_var&&
     nir_instr_as_deref(user)->deref_type==nir_deref_type_cast)continue;
  if(user->type!=nir_instr_type_tex)return false;
  nir_tex_src *ts=container_of(use,nir_tex_src,src);
  if(ts->src_type!=nir_tex_src_texture_deref&&ts->src_type!=nir_tex_src_sampler_deref)return false;
 }
 return true;
}
/* The legacy texture entry accepts binding0/one surviving sample. The new
 * resource entry accepts binding4/four surviving samples, with UBOs validated
 * and lowered separately. Both require explicit LOD0, and replace derefs
 * with immediate Valhall handles as panvk_vX_nir_lower_descriptors does. */
static bool plain_txl_src(nir_tex_src_type t) {
 return t==nir_tex_src_coord||t==nir_tex_src_lod||t==nir_tex_src_texture_deref||t==nir_tex_src_sampler_deref;
}
static bool plain_implicit_src(nir_tex_src_type t) {
 return t==nir_tex_src_coord||t==nir_tex_src_texture_deref||t==nir_tex_src_sampler_deref;
}
/* Checked on the vtn output BEFORE folding: nir_opt_constant_folding turns a
 * zero-gradient txd into txl and drops zero offsets/bias, which would hide
 * operations this contract refuses. Only txs (the AIR rescale query, which
 * must then fold away) may appear besides the plain txl. */
static const char *texture_ops_as_authored(nir_shader *s,bool implicit_lod) {
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(implicit_lod&&instr->type==nir_instr_type_intrinsic) {
   nir_intrinsic_op op=nir_instr_as_intrinsic(instr)->intrinsic;
   if(op==nir_intrinsic_ddx||op==nir_intrinsic_ddx_coarse||op==nir_intrinsic_ddx_fine||
      op==nir_intrinsic_ddy||op==nir_intrinsic_ddy_coarse||op==nir_intrinsic_ddy_fine)
    return "Explicit derivatives are unsupported by the blur3 class";
  }
  if(instr->type!=nir_instr_type_tex)continue;
  nir_tex_instr *tex=nir_instr_as_tex(instr);
  if(tex->op==nir_texop_txs)continue;
  if(tex->op==nir_texop_query_levels||tex->op==nir_texop_texture_samples)return "Texture query survived default specialization";
  if(tex->op!=(implicit_lod?nir_texop_tex:nir_texop_txl))
   return implicit_lod?"Only implicit-LOD texture sample is supported":"Only explicit-LOD texture sample is supported";
  for(unsigned i=0;i<tex->num_srcs;i++)
   if(!(implicit_lod?plain_implicit_src(tex->src[i].src_type):plain_txl_src(tex->src[i].src_type)))
    return "Unsupported texture source";
 }
 return NULL;
}
static const char *lower_texture(nir_shader *s,bool resources,unsigned expected_samples,bool implicit_lod) {
 const char *why=texture_ops_as_authored(s,implicit_lod);
 if(why)return why;
 // spirv_to_nir already gave SpecId0 its default (0: normalized); fold it so
 // the unnormalized-coordinate rescale and its size query die BEFORE the op
 // set is checked.
 bool progress;
 do {
  progress=false;
  NIR_PASS(progress,s,nir_opt_copy_prop);NIR_PASS(progress,s,nir_opt_constant_folding);
  NIR_PASS(progress,s,nir_opt_algebraic);NIR_PASS(progress,s,nir_opt_dce);
  NIR_PASS(progress,s,nir_opt_dead_cf);NIR_PASS(progress,s,nir_opt_cse);
  NIR_PASS(progress,s,nir_opt_remove_phis);
 } while(progress);
 if(implicit_lod)nir_divergence_analysis(s);
 nir_variable *tv=NULL;unsigned vars=0;
 nir_foreach_variable_with_modes(var,s,nir_var_uniform|nir_var_image|nir_var_mem_ubo|nir_var_mem_ssbo) {
  if(resources&&var->data.mode==nir_var_mem_ubo)continue;
  tv=var;vars++;
 }
 if(vars!=1||tv->data.mode!=nir_var_uniform)return "Fragment must declare exactly one combined image sampler";
 const struct glsl_type *t=tv->type;
 if(!glsl_type_is_sampler(t)||glsl_get_sampler_dim(t)!=GLSL_SAMPLER_DIM_2D||glsl_sampler_type_is_array(t)||
    glsl_sampler_type_is_shadow(t)||glsl_get_sampler_result_type(t)!=GLSL_TYPE_FLOAT)
  return "Texture must be a float 2D non-array non-depth sampler2D";
 if(tv->data.descriptor_set!=0||tv->data.binding!=(resources?4:0)||tv->data.bindless)return resources?"Texture must use descriptor set0 binding4":"Texture must use descriptor set0 binding0";
 nir_tex_instr *samples[16]={0};unsigned count=0;
 // Pass0 checks texture operations, pass1 every other resource access, so a
 // refusal names the operation rather than its deref.
 for(unsigned pass=0;pass<2;pass++)
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(pass==0&&instr->type!=nir_instr_type_tex)continue;
  if(instr->type==nir_instr_type_intrinsic) {
   const char *n=nir_intrinsic_infos[nir_instr_as_intrinsic(instr)->intrinsic].name;
   if(!strncmp(n,"image",5)||!strncmp(n,"bindless",8))return "Image operations are unsupported";
   continue;
  }
  if(instr->type==nir_instr_type_deref) {
   nir_deref_instr *d=nir_instr_as_deref(instr);
   if(!(d->modes&(nir_var_uniform|nir_var_image|nir_var_mem_ubo|nir_var_mem_ssbo))||
      (resources&&(d->modes&nir_var_mem_ubo)))continue;
   if(nir_deref_instr_get_variable(d)!=tv||(d->deref_type!=nir_deref_type_var&&d->deref_type!=nir_deref_type_cast)||
      !texture_deref_use_ok(d))return "Texture deref used outside the sample";
   continue;
  }
  if(pass==1||instr->type!=nir_instr_type_tex)continue;
  nir_tex_instr *tex=nir_instr_as_tex(instr);
  if(tex->op==nir_texop_txs||tex->op==nir_texop_query_levels||tex->op==nir_texop_texture_samples)
   return "Texture query survived default specialization";
  if(tex->op!=(implicit_lod?nir_texop_tex:nir_texop_txl))
   return implicit_lod?"Only implicit-LOD texture sample is supported":"Only explicit-LOD texture sample is supported";
  if(tex->sampler_dim!=GLSL_SAMPLER_DIM_2D||tex->is_array||tex->is_shadow||tex->is_new_style_shadow||
     tex->is_sparse||tex->component||tex->array_is_lowered_cube||tex->is_gather_implicit_lod||
     tex->texture_non_uniform||tex->sampler_non_uniform||tex->embedded_sampler||tex->offset_non_uniform||
     tex->texture_index||tex->sampler_index||tex->backend_flags||tex->coord_components!=2||
     tex->dest_type!=nir_type_float32||tex->def.num_components!=4||tex->def.bit_size!=32)
   return "Unsupported texture sample form";
  if(implicit_lod&&tex->instr.block->divergent)
   return "Implicit-LOD texture sample in divergent control flow is unsupported";
  unsigned seen=0;
  for(unsigned i=0;i<tex->num_srcs;i++) {
   nir_tex_src *src=&tex->src[i];
   if(src->src_type>=32||(seen&BITFIELD_BIT(src->src_type)))return "Unsupported texture source";
   seen|=BITFIELD_BIT(src->src_type);
   switch(src->src_type) {
   case nir_tex_src_coord:
    if(src->src.ssa->bit_size!=32||src->src.ssa->num_components!=2)return "Texture coordinate must be float2";
    break;
   case nir_tex_src_lod:
    if(!nir_src_is_const(src->src)||src->src.ssa->bit_size!=32||src->src.ssa->num_components!=1||
       nir_src_as_uint(src->src))return "Texture LOD must be constant 0.0";
    break;
   case nir_tex_src_texture_deref:case nir_tex_src_sampler_deref: {
    nir_deref_instr *d=nir_src_as_deref(src->src);
    if(!d||!texture_deref_ok(d,tv,src->src_type))return "Texture and sampler must be the declared binding";
    break;
   }
   default:return "Unsupported texture source";
   }
  }
  unsigned expected=BITFIELD_BIT(nir_tex_src_coord)|BITFIELD_BIT(nir_tex_src_texture_deref)|
                    BITFIELD_BIT(nir_tex_src_sampler_deref);
  if(!implicit_lod)expected|=BITFIELD_BIT(nir_tex_src_lod);
  if(seen!=expected)
   return "Unsupported texture source";
  if(count>=16)return resources?"More than sixteen texture samples":"Fragment must sample exactly one texture";
  samples[count++]=tex;
 }
 if(count!=(resources?expected_samples:1))return resources?
  "Resource fragment texture sample count differs from the selected exact class":"Fragment must sample exactly one texture";
 for(unsigned sample=0;sample<count;sample++) {
 nir_tex_instr *only=samples[sample];
 nir_builder b=nir_builder_at(nir_before_instr(&only->instr));
 for(int i=only->num_srcs-1;i>=0;i--)
  if(only->src[i].src_type==nir_tex_src_texture_deref||only->src[i].src_type==nir_tex_src_sampler_deref)
   nir_tex_instr_remove_src(only,i);
 nir_tex_instr_add_src(only,nir_tex_src_texture_handle,nir_imm_int(&b,pan_res_handle(resources?1:TEXTURE_TABLE,resources?4:TEXTURE_INDEX)));
 nir_tex_instr_add_src(only,nir_tex_src_sampler_handle,nir_imm_int(&b,pan_res_handle(resources?1:TEXTURE_TABLE,resources?5:TEXTURE_SAMPLER)));
 }
 NIR_PASS(_,s,nir_opt_dce);NIR_PASS(_,s,nir_remove_dead_derefs);
 NIR_PASS(_,s,nir_remove_dead_variables,nir_var_uniform,NULL);
 nir_foreach_variable_with_modes(var,s,nir_var_uniform|nir_var_image)return "Texture variable still referenced";
 nir_validate_shader(s,"tegu texture lowering");
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 return NULL;
}
#include "mali_graphics_resources.inc"

/* Exact R85 fixed-UI sampled class. Input identity is additionally pinned by
 * the public entry point, so these passes can fold the default sampler
 * specialization before rejecting every residual resource form. */
static void fold_fixed_ui_texture_r85(nir_shader *s) {
 nir_lower_constant_to_temp(s);
 NIR_PASS(_,s,nir_lower_variable_initializers,~0);
 NIR_PASS(_,s,nir_split_var_copies);NIR_PASS(_,s,nir_lower_var_copies);
 bool progress;
 do {
  progress=false;
  NIR_PASS(progress,s,nir_lower_vars_to_ssa);NIR_PASS(progress,s,nir_opt_deref);
  NIR_PASS(progress,s,nir_opt_if,0);
  NIR_PASS(progress,s,nir_opt_copy_prop);NIR_PASS(progress,s,nir_opt_constant_folding);
  NIR_PASS(progress,s,nir_opt_algebraic);NIR_PASS(progress,s,nir_opt_dce);
  NIR_PASS(progress,s,nir_opt_dead_cf);NIR_PASS(progress,s,nir_opt_cse);
  NIR_PASS(progress,s,nir_opt_remove_phis);
 } while(progress);
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
}
static bool fixed_ui_texture_r85_index(nir_intrinsic_instr *i,bool vertex) {
 if(i->intrinsic!=nir_intrinsic_vulkan_resource_index||nir_intrinsic_desc_set(i)||
    !nir_src_is_const(i->src[0])||nir_src_as_uint(i->src[0])||
    i->def.bit_size!=32||i->def.num_components!=3)return false;
 unsigned binding=nir_intrinsic_binding(i),type=nir_intrinsic_desc_type(i);
 if(vertex)return binding<2&&type==nir_descriptor_type_uniform_buffer;
 return (binding>=2&&binding<=3&&type==nir_descriptor_type_uniform_buffer)||
        (binding==4&&type==nir_descriptor_type_storage_buffer);
}
struct fixed_ui_texture_r85_lower_ctx {bool vertex,descriptors;};
static bool lower_fixed_ui_texture_r85_intrinsic(nir_builder *b,nir_intrinsic_instr *i,void *data) {
 const struct fixed_ui_texture_r85_lower_ctx *ctx=data;
 if(ctx->descriptors&&i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
  nir_instr *source=nir_def_instr(i->src[0].ssa);
  if(source->type!=nir_instr_type_intrinsic||
     !fixed_ui_texture_r85_index(nir_instr_as_intrinsic(source),ctx->vertex))return false;
  b->cursor=nir_before_instr(&i->instr);nir_def_replace(&i->def,i->src[0].ssa);return true;
 }
 if(!ctx->descriptors&&i->intrinsic==nir_intrinsic_vulkan_resource_index) {
  if(!fixed_ui_texture_r85_index(i,ctx->vertex))return false;
  b->cursor=nir_before_instr(&i->instr);
  nir_def_replace(&i->def,nir_vec3(b,nir_imm_int(b,pan_res_handle(1,nir_intrinsic_binding(i))),
   nir_imm_int(b,0),nir_imm_int(b,0)));return true;
 }
 return false;
}
static const char *lower_fixed_ui_texture_r85_buffers(nir_shader *s,
 struct tegu_mali_fixed_ui_texture_r85_plan *plan) {
 bool vertex=s->info.stage==MESA_SHADER_VERTEX;unsigned counts[5]={0},sizes[5]={0},accessed=0;
 unsigned constant_min=UINT_MAX,constant_max=0;
 nir_foreach_variable_with_modes(var,s,nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform) {
  if(!vertex&&var->data.mode==nir_var_uniform)continue; /* exact sampler is checked separately */
  unsigned binding=var->data.binding;
  if(var->data.descriptor_set||var->data.bindless)return "R85 fixed-UI resource set or binding form changed";
  if(var->data.mode==nir_var_mem_ubo) {
   if((vertex&&binding>1)||(!vertex&&(binding<2||binding>3))||!glsl_type_is_struct_or_ifc(var->type))
    return "R85 fixed-UI UBO declaration differs from exact bindings";
   unsigned bytes=glsl_get_explicit_size(var->type,true);
   if(!bytes||bytes>4096)return "R85 fixed-UI UBO declared extent is invalid";
   counts[binding]++;if(bytes>sizes[binding])sizes[binding]=bytes;continue;
  }
  if(!vertex&&var->data.mode==nir_var_mem_ssbo&&binding==4&&
     (var->data.access&ACCESS_NON_WRITEABLE)&&fixed_ui_constant_type(var->type)){
   counts[4]++;continue;
  }
  return "R85 fixed-UI resource declaration differs from exact buffers";
 }
 if(vertex&&(counts[0]!=1||counts[1]!=1))return "R85 fixed-UI vertex UBO declaration count changed";
 if(!vertex&&(counts[2]!=2||counts[3]!=1||counts[4]!=1))return "R85 fixed-UI fragment buffer declaration count changed";
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(i->intrinsic==nir_intrinsic_vulkan_resource_index) {
   if(!fixed_ui_texture_r85_index(i,vertex))return "R85 fixed-UI resource index differs from exact bindings";
   accessed|=BITFIELD_BIT(nir_intrinsic_binding(i));
  } else if(i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
   nir_instr *source=nir_def_instr(i->src[0].ssa);
   if(i->def.bit_size!=32||i->def.num_components!=3||source->type!=nir_instr_type_intrinsic||
      !fixed_ui_texture_r85_index(nir_instr_as_intrinsic(source),vertex)||
      nir_intrinsic_desc_type(i)!=nir_intrinsic_desc_type(nir_instr_as_intrinsic(source)))
    return "R85 fixed-UI descriptor load differs from direct index";
  } else if(i->intrinsic==nir_intrinsic_vulkan_resource_reindex)return "R85 fixed-UI descriptor reindexing unsupported";
  else if(i->intrinsic==nir_intrinsic_load_deref) {
   nir_deref_instr *d=nir_src_as_deref(i->src[0]);
   if(d&&(d->modes&nir_var_mem_ssbo)) {
    if(!(nir_intrinsic_access(i)&ACCESS_NON_WRITEABLE)||i->def.bit_size!=32||i->def.num_components!=1)
     return "R85 fixed-UI constant source has a non-readonly scalar access";
    nir_deref_path path;nir_deref_path_init(&path,d,NULL);
    for(nir_deref_instr **p=&path.path[1];*p;p++)if((*p)->deref_type==nir_deref_type_array&&
       !nir_src_is_const((*p)->arr.index)){nir_deref_path_finish(&path);return "R85 fixed-UI constant source has dynamic access";}
    nir_deref_path_finish(&path);
    unsigned offset=nir_deref_instr_get_const_offset(d,glsl_get_natural_size_align_bytes);
    if(offset>332)return "R85 fixed-UI constant source exceeds bounded padded span";
    constant_min=MIN2(constant_min,offset);constant_max=MAX2(constant_max,offset);
   }
  } else if(i->intrinsic==nir_intrinsic_store_deref||i->intrinsic==nir_intrinsic_copy_deref) {
   nir_deref_instr *d=nir_src_as_deref(i->src[0]);
   if(d&&(d->modes&(nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform)))
    return "R85 fixed-UI writable resource dereference unsupported";
  }
 }
 if(!accessed)return "R85 fixed-UI stage has no live buffer access";
 unsigned first=vertex?0:2,last=vertex?1:4;
 for(unsigned binding=first;binding<=last;binding++)plan->buffers[binding]=(struct tegu_mali_resource_ubo){
  vertex?TEGU_GRAPHICS_VERTEX:TEGU_GRAPHICS_FRAGMENT,0,binding,1,binding,binding==4?0:sizes[binding]};
 if(!vertex)plan->constant_access_bytes=constant_min==UINT_MAX?0:constant_max+4;
 struct fixed_ui_texture_r85_lower_ctx ctx={vertex,true};
 NIR_PASS(_,s,nir_shader_intrinsics_pass,lower_fixed_ui_texture_r85_intrinsic,nir_metadata_control_flow,&ctx);
 ctx.descriptors=false;
 NIR_PASS(_,s,nir_shader_intrinsics_pass,lower_fixed_ui_texture_r85_intrinsic,nir_metadata_control_flow,&ctx);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ubo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ssbo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_shader_intrinsics_pass,pack_ubo_index,nir_metadata_control_flow,NULL);
 NIR_PASS(_,s,nir_opt_constant_folding);nir_validate_shader(s,"tegu R85 fixed UI buffers");
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));return NULL;
}
static const char *lower_fixed_ui_texture_r85_sample(nir_shader *s) {
 nir_variable *tv=NULL;unsigned variables=0;
 nir_foreach_variable_with_modes(var,s,nir_var_uniform|nir_var_image) {tv=var;variables++;}
 if(variables!=1||!tv||tv->data.mode!=nir_var_uniform||tv->data.descriptor_set||tv->data.binding!=5||tv->data.bindless)
  return "R85 fixed-UI fragment must declare exact sampler binding5";
 const struct glsl_type *type=tv->type;
 if(!glsl_type_is_sampler(type)||glsl_get_sampler_dim(type)!=GLSL_SAMPLER_DIM_2D||
    glsl_sampler_type_is_array(type)||glsl_sampler_type_is_shadow(type)||glsl_get_sampler_result_type(type)!=GLSL_TYPE_FLOAT)
  return "R85 fixed-UI texture must be float non-array sampler2D";
 nir_tex_instr *sample=NULL;unsigned count=0;
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type==nir_instr_type_deref) {
   nir_deref_instr *d=nir_instr_as_deref(instr);
   if((d->modes&(nir_var_uniform|nir_var_image))&&
      (nir_deref_instr_get_variable(d)!=tv||(d->deref_type!=nir_deref_type_var&&d->deref_type!=nir_deref_type_cast)||!texture_deref_use_ok(d)))
    return "R85 fixed-UI texture deref escaped sample";
   continue;
  }
  if(instr->type!=nir_instr_type_tex)continue;
  nir_tex_instr *tex=nir_instr_as_tex(instr);
  if(tex->op!=nir_texop_txb)return "R85 fixed-UI requires one implicit-LOD sample with bias";
  if(tex->sampler_dim!=GLSL_SAMPLER_DIM_2D||tex->is_array||tex->is_shadow||
     tex->is_sparse||tex->texture_non_uniform||tex->sampler_non_uniform||tex->embedded_sampler||
     tex->texture_index||tex->sampler_index||tex->backend_flags||tex->coord_components!=2||
     tex->dest_type!=nir_type_float32||tex->def.num_components!=4||tex->def.bit_size!=32)
   return "R85 fixed-UI texture sample form changed";
  unsigned seen=0;
  for(unsigned n=0;n<tex->num_srcs;n++) {nir_tex_src *src=&tex->src[n];
   if(src->src_type>=32||(seen&BITFIELD_BIT(src->src_type)))return "R85 fixed-UI duplicate texture source";
   seen|=BITFIELD_BIT(src->src_type);
   if(src->src_type==nir_tex_src_coord) {if(src->src.ssa->bit_size!=32||src->src.ssa->num_components!=2)return "R85 fixed-UI coordinate changed";}
   else if(src->src_type==nir_tex_src_bias) {if(src->src.ssa->bit_size!=32||src->src.ssa->num_components!=1)return "R85 fixed-UI bias changed";}
   else if(src->src_type==nir_tex_src_texture_deref||src->src_type==nir_tex_src_sampler_deref) {
    nir_deref_instr *d=nir_src_as_deref(src->src);if(!d||!texture_deref_ok(d,tv,src->src_type))return "R85 fixed-UI texture binding changed";
   } else return "R85 fixed-UI texture source changed";
  }
  unsigned expected=BITFIELD_BIT(nir_tex_src_coord)|BITFIELD_BIT(nir_tex_src_bias)|
    BITFIELD_BIT(nir_tex_src_texture_deref)|BITFIELD_BIT(nir_tex_src_sampler_deref);
  if(seen!=expected||count++)return "R85 fixed-UI texture sample count changed";
  sample=tex;
 }
 if(count!=1||!sample)return "R85 fixed-UI texture sample count changed";
 nir_builder b=nir_builder_at(nir_before_instr(&sample->instr));
 for(int n=sample->num_srcs-1;n>=0;n--)if(sample->src[n].src_type==nir_tex_src_texture_deref||
    sample->src[n].src_type==nir_tex_src_sampler_deref)nir_tex_instr_remove_src(sample,n);
 nir_tex_instr_add_src(sample,nir_tex_src_texture_handle,nir_imm_int(&b,pan_res_handle(1,5)));
 nir_tex_instr_add_src(sample,nir_tex_src_sampler_handle,nir_imm_int(&b,pan_res_handle(1,6)));
 NIR_PASS(_,s,nir_opt_dce);NIR_PASS(_,s,nir_remove_dead_derefs);
 NIR_PASS(_,s,nir_remove_dead_variables,nir_var_uniform,NULL);
 nir_foreach_variable_with_modes(var,s,nir_var_uniform|nir_var_image)return "R85 fixed-UI texture variable survived lowering";
 nir_validate_shader(s,"tegu R85 fixed UI sample");nir_shader_gather_info(s,nir_shader_get_entrypoint(s));return NULL;
}

/* Read-only path-blit StorageBuffer descriptor lowering, scoped to the
 * attachment-clear policy. Descriptor set/binding are static and exact. */
static bool attachment_clear_resource_index(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)unused;
 if(i->intrinsic==nir_intrinsic_vulkan_resource_index) {
  unsigned set=i->const_index[0],binding=i->const_index[1];
  nir_descriptor_type type=(nir_descriptor_type)i->const_index[2];
  if(set||!nir_src_is_const(i->src[0])||nir_src_as_uint(i->src[0])||
     i->def.bit_size!=32||i->def.num_components!=3)return false;
  if(type==nir_descriptor_type_storage_buffer&&binding==0)
   nir_intrinsic_set_desc_type(i,nir_descriptor_type_uniform_buffer);
  else if(type!=nir_descriptor_type_uniform_buffer||binding!=1)return false;
  b->cursor=nir_before_instr(&i->instr);
  nir_def_replace(&i->def,nir_vec3(b,nir_imm_int(b,pan_res_handle(1,binding)),
                                   nir_imm_int(b,0),nir_imm_int(b,0)));
  return true;
 }
 if(i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
  nir_descriptor_type type=nir_intrinsic_desc_type(i);
  if(type==nir_descriptor_type_storage_buffer)
   nir_intrinsic_set_desc_type(i,nir_descriptor_type_uniform_buffer);
  else if(type!=nir_descriptor_type_uniform_buffer)return false;
  if(
     i->def.bit_size!=32||i->def.num_components!=3||
     i->src[0].ssa->bit_size!=32||i->src[0].ssa->num_components!=3)return false;
  b->cursor=nir_before_instr(&i->instr);
  nir_def_replace(&i->def,i->src[0].ssa);return true;
 }
 return false;
}
static const char *lower_attachment_clear_resources(nir_shader *s,
 struct tegu_mali_attachment_clear_plan_v2 *plan) {
 unsigned ssbo=0,ubo=0;
 nir_foreach_variable_with_modes(var,s,nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform) {
  if(var->data.mode==nir_var_mem_ssbo) {
   if(var->data.descriptor_set||var->data.binding||var->data.bindless||
      !glsl_type_is_struct_or_ifc(var->type)||!(var->data.access&ACCESS_NON_WRITEABLE)||
      (var->data.access&(ACCESS_NON_READABLE|ACCESS_VOLATILE|ACCESS_COHERENT)))
    return "attachment-clear storage buffer is outside exact readonly binding0";
   var->data.mode=nir_var_mem_ubo;ssbo++;
  } else if(var->data.mode==nir_var_mem_ubo) {
   if(var->data.descriptor_set||var->data.binding!=1||var->data.bindless||
      !glsl_type_is_struct_or_ifc(var->type)||glsl_get_explicit_size(var->type,true)!=320)
    return "attachment-clear UBO differs from binding1 320-byte matrix";
   ubo++;
   plan->resources[1]=(struct tegu_mali_attachment_clear_resource_v2){
    TEGU_MALI_AC_ROLE_MATRIX_UBO,TEGU_GRAPHICS_VERTEX,0,1,
    TEGU_MALI_AC_SOURCE_UNIFORM_BUFFER,TEGU_MALI_AC_BACKEND_UBO,1,1,
    320,4,16,16,320,TEGU_MALI_AC_ACCESS_READ};
  } else return "attachment-clear contains an unsupported resource class";
 }
 if(ssbo!=1||ubo!=1)return "attachment-clear requires one readonly SSBO and one matrix UBO";
 plan->resources[0]=(struct tegu_mali_attachment_clear_resource_v2){
  TEGU_MALI_AC_ROLE_RECT_SSBO,TEGU_GRAPHICS_VERTEX,0,0,
  TEGU_MALI_AC_SOURCE_STORAGE_BUFFER,TEGU_MALI_AC_BACKEND_UBO,1,0,
  0,0,4,16,16,TEGU_MALI_AC_ACCESS_READ};
 unsigned ssbo_index=0,ubo_index=0;
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(i->intrinsic==nir_intrinsic_vulkan_resource_index) {
   if(i->const_index[0]||!nir_src_is_const(i->src[0])||nir_src_as_uint(i->src[0])||
      i->def.bit_size!=32||i->def.num_components!=3)return "attachment-clear resource index must be static";
   nir_descriptor_type type=nir_intrinsic_desc_type(i);unsigned binding=nir_intrinsic_binding(i);
   if(type==nir_descriptor_type_storage_buffer&&binding==0)ssbo_index++;
   else if(type==nir_descriptor_type_uniform_buffer&&binding==1)ubo_index++;
   else return "attachment-clear resource index differs from exact two-buffer ABI";
  } else if(i->intrinsic==nir_intrinsic_vulkan_resource_reindex)
   return "attachment-clear descriptor reindexing unsupported";
  else if(i->intrinsic==nir_intrinsic_store_ssbo||
          i->intrinsic==nir_intrinsic_ssbo_atomic||i->intrinsic==nir_intrinsic_ssbo_atomic_swap)
   return "attachment-clear SSBO must be read-only";
  else if(i->intrinsic==nir_intrinsic_store_deref||i->intrinsic==nir_intrinsic_copy_deref||
          i->intrinsic==nir_intrinsic_deref_atomic||i->intrinsic==nir_intrinsic_deref_atomic_swap) {
   unsigned sources=i->intrinsic==nir_intrinsic_copy_deref?2:1;
   for(unsigned n=0;n<sources;n++) {
    nir_variable *var=nir_intrinsic_get_var(i,n);
    if(var&&(var->data.mode==nir_var_mem_ssbo||var->data.mode==nir_var_mem_ubo))
     return "attachment-clear buffers must be read-only";
   }
  }
 }
 if(!ssbo_index||!ubo_index)return "attachment-clear resource accesses differ from declarations";
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_deref)continue;
  nir_deref_instr *d=nir_instr_as_deref(instr);
  if(d->modes&nir_var_mem_ssbo)d->modes=(d->modes&~nir_var_mem_ssbo)|nir_var_mem_ubo;
 }
 NIR_PASS(_,s,nir_shader_intrinsics_pass,attachment_clear_resource_index,nir_metadata_control_flow,NULL);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ubo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_shader_intrinsics_pass,pack_ubo_index,nir_metadata_control_flow,NULL);
 NIR_PASS(_,s,nir_opt_constant_folding);
 nir_validate_shader(s,"tegu exact attachment-clear readonly buffers");
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 plan->resource_count=2;
 return NULL;
}

static void fail(char *error,size_t error_size,const char *reason) {
 if(error&&error_size)snprintf(error,error_size,"%s",reason);
}
/* Keep the API boundary in sync with pinned pan_nir_lower_tex.c rather than
 * letting its private-source assertions terminate the compositor. Authored
 * derefs are legal until resource lowering; projectors until nir_lower_tex.
 * Every source in the pinned enum is classified, with a closed default for
 * future Mesa additions. Unsupported sources retain an explicit error. */
enum graphics_texture_phase { GRAPHICS_TEX_AUTHORED, GRAPHICS_TEX_RESOURCES, GRAPHICS_TEX_BACKEND };
static const char *graphics_texture_source_error(nir_tex_src_type type,enum graphics_texture_phase phase) {
 switch(type) {
 case nir_tex_src_coord:case nir_tex_src_comparator:case nir_tex_src_offset:
 case nir_tex_src_bias:case nir_tex_src_lod:case nir_tex_src_min_lod:
 case nir_tex_src_ms_index:case nir_tex_src_ddx:case nir_tex_src_ddy:
 case nir_tex_src_texture_offset:case nir_tex_src_sampler_offset:
 case nir_tex_src_texture_handle:case nir_tex_src_sampler_handle:return NULL;
 case nir_tex_src_projector:
  return phase==GRAPHICS_TEX_BACKEND?"Texture projector survived NIR lowering":NULL;
 case nir_tex_src_texture_deref:case nir_tex_src_sampler_deref:
  return phase==GRAPHICS_TEX_AUTHORED?NULL:"Texture descriptor dereference survived resource lowering";
 case nir_tex_src_plane:return "Multi-plane texture source requires a plane descriptor layout";
 case nir_tex_src_texture_heap_offset:case nir_tex_src_sampler_heap_offset:
  return "Texture descriptor heap offsets are outside the descriptor table ABI";
 case nir_tex_src_sampler_deref_intrinsic:case nir_tex_src_texture_deref_intrinsic:
  return "Texture dereference intrinsic survived function inlining";
 case nir_tex_src_backend1:case nir_tex_src_backend2:
  return "Backend texture source supplied before Valhall lowering";
 case nir_tex_src_max_lod_kk:case nir_tex_src_lod_bias_min_agx:
  return "Foreign backend LOD source is unsupported by Valhall";
 case nir_tex_src_ms_mcs_intel:return "Intel multisample compression source is unsupported by Valhall";
 case nir_tex_src_ref_coord:case nir_tex_src_texture_2_deref:case nir_tex_src_sampler_2_deref:
 case nir_tex_src_texture_2_handle:case nir_tex_src_sampler_2_handle:
 case nir_tex_src_block_size:case nir_tex_src_box_size:
  return "QCOM image processing sources are unsupported by Valhall";
 default:return "Unknown texture source is unsupported by Valhall";
 }
}
static const char *graphics_texture_op_error(nir_texop op) {
 switch(op) {
 case nir_texop_tex:case nir_texop_txb:case nir_texop_txl:case nir_texop_txd:
 case nir_texop_txf:case nir_texop_txf_ms:case nir_texop_tg4:case nir_texop_lod:
 case nir_texop_txs:case nir_texop_query_levels:case nir_texop_texture_samples:return NULL;
 default:return "Texture operation is not implemented by the Valhall lowering";
 }
}
static const char *graphics_texture_check(nir_shader *s,enum graphics_texture_phase phase) {
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_tex)continue;
  nir_tex_instr *tex=nir_instr_as_tex(instr);
  const char *why=graphics_texture_op_error(tex->op);if(why)return why;
  if(tex->is_sparse)return "Sparse texture residency is outside the graphics descriptor ABI";
  if(tex->embedded_sampler)return "Embedded texture samplers are outside the graphics descriptor ABI";
  if(tex->backend_flags)return "Texture backend flags supplied before Valhall lowering";
  switch(tex->sampler_dim) {
  case GLSL_SAMPLER_DIM_1D:case GLSL_SAMPLER_DIM_2D:case GLSL_SAMPLER_DIM_3D:
  case GLSL_SAMPLER_DIM_CUBE:case GLSL_SAMPLER_DIM_RECT:case GLSL_SAMPLER_DIM_EXTERNAL:
  case GLSL_SAMPLER_DIM_MS:case GLSL_SAMPLER_DIM_SUBPASS:case GLSL_SAMPLER_DIM_SUBPASS_MS:break;
  case GLSL_SAMPLER_DIM_BUF:
   if(tex->op!=nir_texop_txf&&tex->op!=nir_texop_txs)return "Buffer textures only support texel fetch and size";
   break;
  default:return "Unknown texture dimension is unsupported by Valhall";
  }
  uint64_t seen=0;
  for(unsigned n=0;n<tex->num_srcs;n++) {
   nir_tex_src *src=&tex->src[n];
   if((why=graphics_texture_source_error(src->src_type,phase)))return why;
   if((unsigned)src->src_type>=64||(seen&BITFIELD64_BIT(src->src_type)))return "Duplicate texture source";
   seen|=BITFIELD64_BIT(src->src_type);
  }
  /* The pinned Valhall pass prioritizes min_lod over ddx/ddy and would drop
   * explicit gradients. Do not silently compile that combination as implicit
   * derivatives. It needs an actual combined gradient/clamp implementation. */
  if(tex->op==nir_texop_txd&&nir_tex_instr_src_index(tex,nir_tex_src_min_lod)>=0)
   return "Explicit texture gradients with minimum LOD require a combined Valhall lowering";
  if(phase!=GRAPHICS_TEX_BACKEND)continue;
  unsigned spatial=tex->coord_components-tex->is_array;
  int c=nir_tex_instr_src_index(tex,nir_tex_src_coord);
  if(!nir_tex_instr_is_query(tex)||tex->op==nir_texop_lod) {
   if(c<0||!spatial||spatial>3||tex->src[c].src.ssa->bit_size!=32||
      tex->src[c].src.ssa->num_components!=tex->coord_components)
    return "Texture coordinates must match the 32-bit spatial and layer dimensions";
   if(tex->sampler_dim==GLSL_SAMPLER_DIM_CUBE&&tex->op!=nir_texop_txf&&spatial!=3)
    return "Cube texture coordinates require three spatial components";
  }
  int x=nir_tex_instr_src_index(tex,nir_tex_src_ddx),y=nir_tex_instr_src_index(tex,nir_tex_src_ddy);
  if((x<0)!=(y<0))return "Texture gradients must provide both ddx and ddy";
  if(x>=0&&(tex->src[x].src.ssa->num_components!=spatial||
     tex->src[y].src.ssa->num_components!=spatial||tex->src[x].src.ssa->bit_size!=32||
     tex->src[y].src.ssa->bit_size!=32))return "Texture gradients must match the 32-bit spatial dimensions";
  int o=nir_tex_instr_src_index(tex,nir_tex_src_offset);
  if(o>=0&&tex->src[o].src.ssa->num_components!=spatial)return "Texture offset dimension differs from coordinates";
  if(nir_tex_instr_src_index(tex,nir_tex_src_ms_index)>=0&&spatial!=2)
   return "Multisample texture coordinates require two spatial components";
 }
 return NULL;
}
/* Driver sysvals that panvk lowers to its own FAU layout and the pinned
 * Bifrost backend does not emit (bi_emit_intrinsic: "Unhandled intrinsic",
 * assert(0)). Anything still live here would abort the caller, so refuse it
 * before pan_shader_compile. Values the stage lowered itself are already push
 * constants. The list mirrors panvk_vX_shader.c panvk_lower_sysvals minus the
 * intrinsics bi_emit_intrinsic handles natively (draw_id, layer_id,
 * view_index, blend descriptor). */
static const char *graphics_driver_sysval_reason(nir_intrinsic_op op) {
 switch(op) {
 case nir_intrinsic_load_first_vertex:case nir_intrinsic_load_base_vertex:
  return "Vertex shader reads BaseVertex/first vertex; requires the draw-parameter compiler entry";
 case nir_intrinsic_load_base_instance:
  return "Vertex shader reads BaseInstance; requires the draw-parameter compiler entry";
 case nir_intrinsic_load_is_indexed_draw:return "Indexed-draw sysval is outside the graphics FAU ABI";
 case nir_intrinsic_load_raw_vertex_offset_pan:return "Raw vertex offset sysval is outside the arch9 graphics ABI";
 case nir_intrinsic_load_num_workgroups:case nir_intrinsic_load_workgroup_size:
 case nir_intrinsic_load_base_workgroup_id:return "Compute sysval in a graphics stage";
 case nir_intrinsic_load_printf_buffer_address:return "Shader printf buffer is outside the graphics ABI";
 case nir_intrinsic_load_input_attachment_target_pan:case nir_intrinsic_load_input_attachment_conv_pan:
  return "Input attachment target sysval is outside the graphics FAU ABI";
 case nir_intrinsic_load_ro_sink_address_poly:return "Read-only sink address sysval is outside the graphics ABI";
 case nir_intrinsic_load_viewport_scale:case nir_intrinsic_load_viewport_offset:
  return "Viewport sysval survived FAU lowering";
 default:return NULL;
 }
}
static const char *graphics_driver_sysval_scan(nir_shader *s) {
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  const char *why=graphics_driver_sysval_reason(nir_instr_as_intrinsic(instr)->intrinsic);
  if(why)return why;
 }
 return NULL;
}
static const char *graphics_driver_sysval_error(nir_shader *s) {
 if(!graphics_driver_sysval_scan(s))return NULL;
 /* The backend DCEs before emission, so a dead load never asserted. Only a
  * load that survives DCE is refused; the compiled shader is not modified. */
 nir_shader *copy=nir_shader_clone(NULL,s);
 if(!copy)return "Driver sysval check allocation failed";
 bool progress;
 do {progress=false;NIR_PASS(progress,copy,nir_opt_dce);NIR_PASS(progress,copy,nir_opt_dead_cf);} while(progress);
 const char *why=graphics_driver_sysval_scan(copy);
 ralloc_free(copy);return why;
}
/* Lowers, compiles and copies one translated stage. Consumes nothing; caller
 * frees s. Returns 0 or a status; result untouched on refusal. */
static int compile_stage(nir_shader *s,struct stage_job *job,struct tegu_mali_graphics *result,const char **reason) {
 struct graphics_context ctx={.keep_output_flags=job->pair&&job->stage==MESA_SHADER_VERTEX,
  .draw_params=job->draw_params&&job->stage==MESA_SHADER_VERTEX,.policy=job->policy};
 struct util_dynarray binary=UTIL_DYNARRAY_INIT;
 mesa_shader_stage stage=job->stage;
 int status=4;
 bool fs_inputs_ok=job->pair&&stage==MESA_SHADER_FRAGMENT;
 bool attachment_fs=job->policy==GRAPHICS_POLICY_ATTACHMENT_CLEAR&&stage==MESA_SHADER_FRAGMENT;
 if((s->info.inputs_read&&!(stage==MESA_SHADER_VERTEX&&job->vertex_inputs)&&!fs_inputs_ok)||
    (s->info.num_ubos&&!job->resources&&!attachment_fs) ||
    (s->info.num_ssbos&&!job->resources&&
     !(job->policy==GRAPHICS_POLICY_ATTACHMENT_CLEAR&&stage==MESA_SHADER_VERTEX)&&!attachment_fs) ||
    (!attachment_fs&&(s->info.num_textures||s->info.num_images))) {
  *reason=job->pair?"Shader resources are unsupported":
   "Vertex inputs require extended compiler; shader resources and fragment varyings are unsupported";goto cleanup;
 }
 if(job->policy==GRAPHICS_POLICY_ATTACHMENT_CLEAR&&stage==MESA_SHADER_FRAGMENT) {
  nir_foreach_variable_with_modes(var,s,nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform) {
   (void)var;*reason="Attachment-clear fragment resources are unsupported";goto cleanup;
  }
  if(s->info.num_textures||s->info.num_images) {
   *reason="Attachment-clear fragment textures and images are unsupported";goto cleanup;
  }
 }
 if(stage==MESA_SHADER_VERTEX) {
  nir_foreach_variable_with_modes(var,s,nir_var_shader_in) {
   unsigned slots=glsl_count_attribute_slots(var->type,false);
   if(var->data.location<VERT_ATTRIB_GENERIC0||var->data.location>=VERT_ATTRIB_GENERIC0+31||
      !slots||slots>31-(var->data.location-VERT_ATTRIB_GENERIC0)) {
    *reason="Vertex input location outside hardware descriptor slots0..30";goto cleanup;
   }
   // Keep sparse Vulkan locations stable. Dense assignment would silently
   // fetch the wrong buffer when, for example, only location7 is live.
   var->data.driver_location=var->data.location-VERT_ATTRIB_GENERIC0;
  }
 }
 uint64_t generic=BITFIELD64_RANGE(VARYING_SLOT_VAR0,31);
 uint64_t allowed=stage==MESA_SHADER_VERTEX?
  (job->pair?BITFIELD64_BIT(VARYING_SLOT_POS)|generic|(job->point_size?BITFIELD64_BIT(VARYING_SLOT_PSIZ):0):BITFIELD64_BIT(VARYING_SLOT_POS)|BITFIELD64_BIT(VARYING_SLOT_PSIZ)):
  (job->policy==GRAPHICS_POLICY_ATTACHMENT_CLEAR ?
   BITFIELD64_BIT(FRAG_RESULT_DATA0)|BITFIELD64_BIT(FRAG_RESULT_DATA1) :
   job->policy==GRAPHICS_POLICY_MRT?BITFIELD64_RANGE(FRAG_RESULT_DATA0,TEGU_MALI_GENERIC_MAX_TARGETS):
   BITFIELD64_BIT(FRAG_RESULT_DATA0));
 if(s->info.outputs_written&~allowed){
  /* Same refusal; name the first disallowed slot for the coverage ledger. */
  uint64_t bad=s->info.outputs_written&~allowed;unsigned slot=(unsigned)u_bit_scan64(&bad);
  *reason=stage==MESA_SHADER_VERTEX?
   (slot==VARYING_SLOT_VIEWPORT?"Unsupported graphics output: VS ViewportIndex (viewport arrays)":
    slot==VARYING_SLOT_LAYER?"Unsupported graphics output: VS Layer (layered rendering)":
    slot==VARYING_SLOT_CLIP_DIST0||slot==VARYING_SLOT_CLIP_DIST1?"Unsupported graphics output: VS ClipDistance":
    slot==VARYING_SLOT_CULL_DIST0||slot==VARYING_SLOT_CULL_DIST1?"Unsupported graphics output: VS CullDistance":
    slot==VARYING_SLOT_PSIZ?"Unsupported graphics output: VS PointSize outside pair2":
    "Unsupported graphics output: VS builtin or varying slot"):
   (slot==FRAG_RESULT_DEPTH?"Unsupported graphics output: FS FragDepth":
    slot==FRAG_RESULT_STENCIL?"Unsupported graphics output: FS FragStencilRef":
    slot==FRAG_RESULT_SAMPLE_MASK?"Unsupported graphics output: FS SampleMask":
    slot>FRAG_RESULT_DATA0&&slot<=FRAG_RESULT_DATA7?"Unsupported graphics output: additional colour target (use the _rt entry)":
    "Unsupported graphics output: FS output slot");
  goto cleanup;
 }
 if(job->policy==GRAPHICS_POLICY_ATTACHMENT_CLEAR&&stage==MESA_SHADER_FRAGMENT) {
  unsigned count=0,seen=0;
  nir_foreach_variable_with_modes(var,s,nir_var_shader_out) {
   unsigned loc=var->data.location;
   if(loc<FRAG_RESULT_DATA0||loc>FRAG_RESULT_DATA1||var->data.index!=0||
      !glsl_type_is_vector(var->type)||glsl_get_base_type(var->type)!=GLSL_TYPE_FLOAT||
      glsl_get_vector_elements(var->type)!=4||glsl_get_bit_size(var->type)!=32||
      var->data.precision==GLSL_PRECISION_MEDIUM) {
    *reason="Attachment-clear outputs must be float32 vec4 at index0, locations0/1";goto cleanup;
   }
   unsigned slot=loc-FRAG_RESULT_DATA0;
   if(seen&(1u<<slot)){*reason="Duplicate attachment-clear output location";goto cleanup;}
   seen|=1u<<slot;count++;
  }
  if(count!=2||seen!=3){*reason="Attachment-clear requires float32 outputs at locations0 and1";goto cleanup;}
 }
 if(job->pair&&stage==MESA_SHADER_VERTEX&&!(s->info.outputs_written&BITFIELD64_BIT(VARYING_SLOT_POS))) {
  *reason="Paired vertex shader must write position";goto cleanup;
 }
 const char *texture_error=graphics_texture_check(s,GRAPHICS_TEX_RESOURCES);
 if(texture_error){*reason=texture_error;goto cleanup;}
 pan_preprocess_nir(s,gpu);
 texture_error=graphics_texture_check(s,GRAPHICS_TEX_BACKEND);
 if(texture_error){*reason=texture_error;goto cleanup;}
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_push_const,nir_address_format_32bit_offset);
 NIR_PASS(_,s,nir_opt_constant_folding);
 NIR_PASS(_,s,nir_shader_intrinsics_pass,graphics_push,nir_metadata_control_flow,&ctx);
 if(ctx.failed){*reason="AIR push access exceeds supported constant-offset range";goto cleanup;}
 if(fs_inputs_ok)nir_assign_io_var_locations(s,nir_var_shader_in);
 nir_assign_io_var_locations(s,nir_var_shader_out);
 NIR_PASS(_,s,nir_lower_io,nir_var_shader_in|nir_var_shader_out,glsl_type_size,nir_lower_io_use_interpolated_input_intrinsics);
 NIR_PASS(_,s,nir_opt_constant_folding);
 struct pan_compile_inputs inputs={.gpu_id=gpu,.trust_varying_flat_highp_types=true};
 struct pan_varying_layout layout={0};
 if(stage==MESA_SHADER_VERTEX) {
  pan_varying_collect_formats(&layout,s,gpu,true,true);
  pan_build_varying_layout_compact(&layout,s,gpu);inputs.varying_layout=&layout;
 } else if(fs_inputs_ok) {
  if(!job->vs_layout||!(job->vs_layout->known&PAN_VARYING_LAYOUT_KNOWN)){*reason="Vertex varying layout unavailable";goto cleanup;}
  inputs.varying_layout=job->vs_layout;
 }
 struct pan_shader_info *info=&job->info;
 pan_postprocess_nir(s,&inputs,info);
 /* Valhall TLS addressing is a 16-bit signed immediate and spills start
  * after scratch (bi_ra.c bi_store_tl asserts offset<0x8000). Graphics never
  * admits TLS (checked again after codegen), so refuse private memory here
  * instead of letting a large private array abort the caller. */
 if(s->scratch_size&&(!job->allow_tls||s->scratch_size>=0x7000u)){
  *reason=job->allow_tls?"Graphics private memory exceeds the Valhall TLS immediate range (scratch plus spills)":
   "Graphics private memory (scratch/TLS) is unsupported";goto cleanup;
 }
 if(job->pair&&stage==MESA_SHADER_VERTEX) {
  if(!info->vs.idvs){*reason="Paired vertex shader did not select IDVS";goto cleanup;}
  NIR_PASS(_,s,nir_inline_sysval,nir_intrinsic_load_noperspective_varyings_pan,job->noperspective);
 }
 if(fs_inputs_ok&&info->bifrost.uses_ld_var) {
  *reason="Fragment varying needs LD_VAR attribute descriptors (unsupported)";goto cleanup;
 }
 NIR_PASS(_,s,nir_shader_intrinsics_pass,graphics_sysvals,nir_metadata_control_flow,&ctx);
 job->targets_seen=ctx.targets_seen;
 if(ctx.failed){*reason=job->policy==GRAPHICS_POLICY_MRT?"MRT blend target index outside 0..3":
  "Attachment-clear blend target index outside exact RT0/RT1 map";goto cleanup;}
 if(nir_shader_intrinsics_pass(s,noperspective_left,nir_metadata_all,NULL)) {
  *reason="Unlowered noperspective sysval";goto cleanup;
 }
 if((texture_error=graphics_driver_sysval_error(s))){*reason=texture_error;goto cleanup;}
 nir_validate_shader(s,"tegu graphics precompile");

 pan_shader_compile(s,&inputs,&binary,info);
 uint32_t code_limit=job->resources?(job->resource_code_limit?job->resource_code_limit:TEGU_MALI_RESOURCE_CODE_LIMIT):4096u;
 if(!binary.size||binary.size>code_limit||(info->tls_size&&(!job->allow_tls||info->tls_size>=0x8000u))||info->push.count>64) {
  /* Same refusal, with the actual limiting quantity for coverage ledgers. */
  *reason=!binary.size?"Graphics code or resource footprint unsupported: no code":
   binary.size>code_limit?"Graphics code or resource footprint unsupported: code exceeds stage limit":
   info->tls_size?"Graphics code or resource footprint unsupported: register spilling needs TLS":
   "Graphics code or resource footprint unsupported: more than 64 FAU words";
  goto cleanup;
 }
 // Single-stage VS note: production binaries already carry an unreferenced
 // IDVS varying program (secondary_enable=1) because the output-flags sysval
 // is an FAU load there; behaviour is kept byte-identical on purpose.
 result->code=malloc(binary.size);
 if(!result->code){*reason="Output allocation failed";status=3;goto cleanup;}
 memcpy(result->code,binary.data,binary.size);result->size=binary.size;
 result->stage=job->requested;result->registers=info->work_reg_count;result->tls=info->tls_size;
 result->fau_words=info->push.count;result->preload=info->preload;
 result->sysval_mask=ctx.mask;result->push_bytes=ctx.push_bytes;status=0;
 // Mesa's info.attributes_read uses GL semantic bits (GENERIC0 starts at15)
 // and is only32bits wide. Read the full64bit NIR mask so sparse high Vulkan
 // locations are not truncated; driver locations mirror these generic slots.
 if(stage==MESA_SHADER_VERTEX)
  job->attribute_mask=(uint32_t)(s->info.inputs_read>>VERT_ATTRIB_GENERIC0);
cleanup:
 util_dynarray_fini(&binary);return status;
}

void tegu_mali_graphics_free(struct tegu_mali_graphics *result) {
 if(result){free(result->code);memset(result,0,sizeof(*result));}
}

static bool graphics_zero_base_instance(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)unused;
 if(i->intrinsic!=nir_intrinsic_load_base_instance)return false;
 b->cursor=nir_before_instr(&i->instr);
 nir_def_replace(&i->def,nir_imm_zero(b,1,i->def.bit_size));return true;
}

int tegu_mali_compile_attachment_clear_pair_v2(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,
 struct tegu_mali_attachment_clear_plan_v2 *plan,char *error,size_t error_size) {
 const void *out[4]={vs,fs,plan,error};
 const size_t out_size[4]={sizeof(*vs),sizeof(*fs),sizeof(*plan),error?error_size:0};
 if(!vs||!fs||!plan||vs==fs||!vd||!fd||!vn||!fn||
    !valid_span(vd,vn)||!valid_span(fd,fn)||
    !valid_span(vs,sizeof(*vs))||!valid_span(fs,sizeof(*fs))||
    !valid_span(plan,sizeof(*plan))||(error_size&&(!error||!valid_span(error,error_size))))return 1;
 for(unsigned i=0;i<4;i++) {
  if(out_size[i]&&!valid_span(out[i],out_size[i]))return 1;
  for(unsigned j=i+1;j<4;j++)if(overlaps(out[i],out_size[i],out[j],out_size[j]))return 1;
  if(overlaps(out[i],out_size[i],vd,vn)||overlaps(out[i],out_size[i],fd,fn))return 1;
 }
 /* No caller-visible memory is touched until every span and alias is safe. */
 if(error&&error_size)error[0]=0;
 if(vs->code||vs->size||fs->code||fs->size||plan->version) {
  fail(error,error_size,"Output must be empty");return 1;
 }
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(plan,0,sizeof(*plan));
 glsl_type_singleton_init_or_ref();
 int status=2;const char *reason="Invalid SPIR-V size or graphics stage";
 nir_shader *v=translate_ex(vd,vn,MESA_SHADER_VERTEX,&status,&reason,true,true),*f=NULL;
 if(v)f=translate_ex(fd,fn,MESA_SHADER_FRAGMENT,&status,&reason,false,false);
 if(!v||!f)goto done;
 if((reason=lower_attachment_clear_resources(v,plan))!=NULL){status=5;goto done;}
 NIR_PASS(_,v,nir_shader_intrinsics_pass,graphics_zero_base_instance,nir_metadata_control_flow,NULL);
 struct stage_job vj={.requested=TEGU_GRAPHICS_VERTEX,.stage=MESA_SHADER_VERTEX,
  .resources=true,.resource_code_limit=4096,.pair=true,.point_size=true,
  .policy=GRAPHICS_POLICY_ATTACHMENT_CLEAR};
 struct stage_job fj={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,
  .policy=GRAPHICS_POLICY_ATTACHMENT_CLEAR};
 if((status=compile_stage(v,&vj,vs,&reason)))goto done;
 if(v->info.inputs_read||v->info.outputs_written!=
    (BITFIELD64_BIT(VARYING_SLOT_POS)|BITFIELD64_BIT(VARYING_SLOT_PSIZ))||
    vj.info.vs.secondary_enable||vj.info.vs.needs_extended_fifo||vj.info.vs.secondary_offset!=vs->size||
    !vj.info.vs.no_psiz_offset||vj.info.vs.no_psiz_offset>=vs->size) {
  reason="Vertex stage differs from no-secondary position+PSIZ class";status=5;goto done;
 }
 if(vs->size!=1792||vs->registers!=32||vs->tls||vs->fau_words!=34||
    vs->sysval_mask!=3||vs->push_bytes!=8||vs->preload!=0x3800000000000000ULL||
    vj.info.vs.no_psiz_offset!=896) {
  reason="Vertex codegen differs from exact attachment-clear ABI";status=5;goto done;
 }
 if((status=compile_stage(f,&fj,fs,&reason)))goto done;
 if(f->info.inputs_read||f->info.outputs_written!=
    (BITFIELD64_BIT(FRAG_RESULT_DATA0)|BITFIELD64_BIT(FRAG_RESULT_DATA1))) {
  reason="Fragment stage must write exactly RT0 and RT1 without inputs";status=5;goto done;
 }
 if(fj.targets_seen!=3||fs->size!=256||fs->registers!=32||fs->tls||fs->fau_words!=16||
    fs->sysval_mask!=8||fs->push_bytes!=72||fs->preload!=0x3000000000000000ULL) {
  reason="Fragment codegen differs from exact two-target attachment-clear ABI";status=5;goto done;
 }
 char digest[65];
 if(tegu_compiler_trace_sha256(vs->code,vs->size,digest)||
    strcmp(digest,"f65b226eb26a30602897f2577b9743916e739df6200b2dab81f45b7e07b7038b")) {
  reason="Vertex code bytes differ from qualified attachment-clear identity";status=5;goto done;
 }
 if(tegu_compiler_trace_sha256(fs->code,fs->size,digest)||
    strcmp(digest,"03209c66f0d2181a8b540bcc7d7c3e5e89bc7012f6e856d5c6da426a36dbfab6")) {
  reason="Fragment code bytes differ from qualified attachment-clear identity";status=5;goto done;
 }
 plan->output_count=2;plan->rt0_write_mask=0;plan->rt1_write_mask=15;
 plan->position_offset=vj.info.vs.no_psiz_offset;
 plan->secondary_offset=(uint32_t)vs->size;plan->secondary_enabled=0;plan->generic_size_B=0;
 plan->draw_vertex_count=3;plan->draw_instance_count=1;plan->draw_base_instance=0;plan->draw_indexed=0;
 plan->target0_format=80;plan->target1_format=80;plan->target0_blend_enabled=0;plan->target1_blend_enabled=0;
 plan->target_descriptors_seen=fj.targets_seen;
 plan->version=TEGU_MALI_ATTACHMENT_CLEAR_VERSION;status=0;
done:
 if(status) {
  fail(error,error_size,reason);tegu_mali_graphics_free(vs);tegu_mali_graphics_free(fs);memset(plan,0,sizeof(*plan));
 }
 if(v)ralloc_free(v);
 if(f)ralloc_free(f);
 glsl_type_singleton_decref();return status;
}
int tegu_mali_compile_graphics(const void *data,size_t length,uint32_t requested,
 struct tegu_mali_graphics *result,char *error,size_t error_size) {
 return tegu_mali_compile_graphics_with_vertex_inputs(data,length,requested,result,NULL,error,error_size);
}
int tegu_mali_compile_graphics_with_vertex_inputs(const void *data,size_t length,uint32_t requested,
 struct tegu_mali_graphics *result,uint32_t *attribute_mask,char *error,size_t error_size) {
 if(error&&error_size)error[0]=0;
 if(attribute_mask)*attribute_mask=0;
 if(!result||result->code||result->size){fail(error,error_size,"Output must be empty");return 1;}
 memset(result,0,sizeof(*result));
 if(requested!=TEGU_GRAPHICS_VERTEX&&requested!=TEGU_GRAPHICS_FRAGMENT) {
  fail(error,error_size,"Invalid SPIR-V size or graphics stage");return 2;
 }
 struct stage_job job={.requested=requested,.stage=requested==TEGU_GRAPHICS_VERTEX?MESA_SHADER_VERTEX:MESA_SHADER_FRAGMENT,
  .vertex_inputs=attribute_mask!=NULL};
 const char *reason;int status;
 glsl_type_singleton_init_or_ref();
 nir_shader *s=translate(data,length,job.stage,&status,&reason);
 if(s)status=compile_stage(s,&job,result,&reason);
 if(status)fail(error,error_size,reason);
 else if(attribute_mask)*attribute_mask=job.attribute_mask;
 if(s)ralloc_free(s);
 glsl_type_singleton_decref();return status;
}

/* Shared by both pair versions. v2=false keeps every version1 refusal (PSIZ is
 * an unsupported output there); pair is written only on success. texture is
 * NULL except for the textured pair, which also needs v2. */
/* Generic reflection-driven resources (see tegu_mali_compile_generic_pair). */
static const char *generic_record(struct tegu_mali_generic_plan *p,unsigned binding,unsigned kind,
 unsigned stage,unsigned bytes,unsigned input_index) {
 if(binding>=TEGU_MALI_GENERIC_MAX_BINDINGS)return "Generic binding index above 15";
 for(unsigned i=0;i<p->count;i++) {
  struct tegu_mali_generic_binding *b=&p->bindings[i];
  if(b->binding!=binding)continue;
  /* A combined image sampler is a texture and a sampler at one binding. */
  if((b->kind==TEGU_MALI_GENERIC_TEXTURE&&kind==TEGU_MALI_GENERIC_SAMPLER)||
     (b->kind==TEGU_MALI_GENERIC_SAMPLER&&kind==TEGU_MALI_GENERIC_TEXTURE))b->kind=TEGU_MALI_GENERIC_COMBINED;
  else if(!(b->kind==TEGU_MALI_GENERIC_COMBINED&&(kind==TEGU_MALI_GENERIC_TEXTURE||kind==TEGU_MALI_GENERIC_SAMPLER))&&
     b->kind!=kind)return "Generic binding kind differs between uses";
  b->stage_mask|=stage;if(bytes>b->declared_bytes)b->declared_bytes=bytes;return NULL;
 }
 if(p->count>=TEGU_MALI_GENERIC_MAX_BINDINGS)return "Too many generic bindings";
 p->bindings[p->count++]=(struct tegu_mali_generic_binding){binding,kind,stage,bytes,input_index,{0,0,0}};
 return NULL;
}
static bool generic_buffer_index(nir_builder *b,nir_intrinsic_instr *i,void *data) {
 bool *descriptors=data;
 if(*descriptors&&i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
  b->cursor=nir_before_instr(&i->instr);nir_def_replace(&i->def,i->src[0].ssa);return true;
 }
 if(!*descriptors&&i->intrinsic==nir_intrinsic_vulkan_resource_index) {
  b->cursor=nir_before_instr(&i->instr);
  nir_def_replace(&i->def,nir_vec3(b,nir_imm_int(b,pan_res_handle(TEGU_MALI_GENERIC_TEXTURE_TABLE,
   nir_intrinsic_binding(i))),nir_imm_int(b,0),nir_imm_int(b,0)));return true;
 }
 return false;
}
static bool generic_zero_layer(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)unused;
 if(i->intrinsic!=nir_intrinsic_load_layer_id)return false;
 b->cursor=nir_before_instr(&i->instr);nir_def_replace(&i->def,nir_imm_int(b,0));return true;
}
/* Tile-local framebuffer fetch, deliberately separate from the production
 * texture-fetch ABI. Based on Mesa panvk_vX_nir_lower_input_attachment_loads.c
 * and pan_fb_nir.c (Copyright Collabora, SPDX-License-Identifier: MIT).
 * Only a single-sample active colour target is admitted. MRT's selected output
 * has already become DATA0; fetches of any OTHER attachment must use the old
 * memory path until the job actually carries both render targets. */
#define TEGU_GENERIC_COMPILE_TILE (1u << 8)
/* Internal select bit of tegu_mali_compile_generic_pair_options. */
#define TEGU_GENERIC_COMPILE_DRAW_PARAMS (1u << 9)
#define TEGU_GENERIC_COMPILE_TLS (1u << 10)
struct generic_tile_context { unsigned attachment,loads;const char *error; };
static bool generic_tile_load(nir_builder *b,nir_intrinsic_instr *i,void *data) {
 struct generic_tile_context *ctx=data;
 if(i->intrinsic!=nir_intrinsic_image_deref_load&&i->intrinsic!=nir_intrinsic_image_deref_sparse_load)return false;
 nir_deref_instr *deref=nir_src_as_deref(i->src[0]);
 enum glsl_sampler_dim dim=glsl_get_sampler_dim(deref->type);
 if(dim!=GLSL_SAMPLER_DIM_SUBPASS&&dim!=GLSL_SAMPLER_DIM_SUBPASS_MS)return false;
 if(i->intrinsic!=nir_intrinsic_image_deref_load||dim!=GLSL_SAMPLER_DIM_SUBPASS||
    deref->deref_type!=nir_deref_type_var||deref->var->data.index!=ctx->attachment||
    i->def.num_components!=4||i->def.bit_size!=32||nir_intrinsic_dest_type(i)!=nir_type_float32) {
  ctx->error="Tile fetch requires one active single-sample float4 colour attachment";return false;
 }
 nir_scalar x=nir_scalar_resolved(i->src[1].ssa,0),y=nir_scalar_resolved(i->src[1].ssa,1);
 if(!nir_scalar_is_const(x)||!nir_scalar_is_const(y)||nir_scalar_as_uint(x)||nir_scalar_as_uint(y)) {
  ctx->error="Tile fetch requires the current pixel without an offset";return false;
 }
 b->cursor=nir_before_instr(&i->instr);
 /* The same conversion used by the store/blend path, supplied in the FAU
  * blend descriptor. This avoids confusing tile RGBA with memory BGRA. */
 nir_def *conversion=nir_unpack_64_2x32_split_y(b,nir_load_blend_descriptor_pan(b,.base=0));
 nir_io_semantics sem={0};sem.location=FRAG_RESULT_DATA0;sem.num_slots=1;
 sem.fb_fetch_output=true;sem.fb_fetch_output_coherent=true;
 nir_def *value=nir_load_tile_pan(b,4,32,
  pan_nir_tile_rt_sample(b,nir_imm_int(b,0),nir_imm_int(b,0)),
  pan_nir_tile_default_coverage(b),conversion,.dest_type=nir_type_float32,
  .access=nir_intrinsic_access(i)|ACCESS_COHERENT,.io_semantics=sem);
 nir_def_replace(&i->def,value);ctx->loads++;return true;
}
/* Vulkan VertexIndex = hardware id + VB and BaseVertex = VB. The FAU word is
 * loaded through the first_vertex sysval, lowered by graphics_sysvals after
 * AIR push rebasing. Inserted after the original load so it is not revisited. */
static bool generic_draw_params(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)unused;
 if(i->intrinsic==nir_intrinsic_load_base_vertex) {
  b->cursor=nir_before_instr(&i->instr);
  nir_def_replace(&i->def,nir_load_first_vertex(b));return true;
 }
 if(i->intrinsic!=nir_intrinsic_load_vertex_id)return false;
 b->cursor=nir_after_instr(&i->instr);
 nir_def *sum=nir_iadd(b,&i->def,nir_load_first_vertex(b));
 nir_def_rewrite_uses_after(&i->def,sum);return true;
}
static const char *lower_generic_resources(nir_shader *s,struct tegu_mali_generic_plan *plan) {
 bool vertex=s->info.stage==MESA_SHADER_VERTEX;
 unsigned stage=vertex?TEGU_GRAPHICS_VERTEX:TEGU_GRAPHICS_FRAGMENT;
 fold_fixed_ui_texture_r85(s);
 /* TGJ1 submission enforces instance_count == 1 and base_instance == 0.
  * Vulkan InstanceIndex from AIR expands into InstanceId + BaseInstance;
  * the latter is a driver sysval, not implemented by the shader backend. */
 if(vertex&&(plan->reserved&TEGU_GENERIC_COMPILE_DRAW_PARAMS))
  NIR_PASS(_,s,nir_shader_intrinsics_pass,generic_draw_params,nir_metadata_control_flow,NULL);
 else if(vertex)NIR_PASS(_,s,nir_shader_intrinsics_pass,graphics_zero_base_instance,nir_metadata_control_flow,NULL);
 const char *texture_error=graphics_texture_check(s,GRAPHICS_TEX_AUTHORED);
 if(texture_error)return texture_error;
 if(!vertex) {
  /* Metal framebuffer fetch arrives as Vulkan input attachments: texel fetch
   * of the bound render target at the fragment coordinate, single layer. */
  bool fetch=false;
  nir_foreach_variable_with_modes(var,s,nir_var_image|nir_var_uniform)
   if(glsl_get_sampler_dim(glsl_without_array(var->type))==GLSL_SAMPLER_DIM_SUBPASS||
      glsl_get_sampler_dim(glsl_without_array(var->type))==GLSL_SAMPLER_DIM_SUBPASS_MS)fetch=true;
  if(fetch) {
   if(plan->reserved&TEGU_GENERIC_COMPILE_TILE) {
    unsigned selected=plan->reserved&0xffu;
    struct generic_tile_context ctx={.attachment=selected?selected-1:0};
    NIR_PASS(_,s,nir_lower_vars_to_ssa);NIR_PASS(_,s,nir_opt_copy_prop);NIR_PASS(_,s,nir_opt_constant_folding);
    NIR_PASS(_,s,nir_shader_intrinsics_pass,generic_tile_load,nir_metadata_none,&ctx);
    if(ctx.error)return ctx.error;
    if(!ctx.loads)return "Tile fetch did not lower an input attachment load";
    plan->reads_framebuffer=TEGU_MALI_GENERIC_FETCH_TILE;
   } else {
    nir_input_attachment_options options={0};
    NIR_PASS(_,s,nir_lower_input_attachments,&options);
    NIR_PASS(_,s,nir_shader_intrinsics_pass,generic_zero_layer,nir_metadata_control_flow,NULL);
    plan->reads_framebuffer=TEGU_MALI_GENERIC_FETCH_MEMORY;
   }
  }
 }
 if(!vertex&&(s->info.fs.pixel_interlock_ordered||s->info.fs.pixel_interlock_unordered||
    s->info.fs.sample_interlock_ordered||s->info.fs.sample_interlock_unordered))
  return "Fragment shader interlock (Metal raster order groups) is unsupported: Valhall has no pinned Mesa lowering";
 nir_foreach_variable_with_modes(var,s,nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform) {
  if(var->data.descriptor_set||var->data.bindless)return "Generic resources must use descriptor set 0";
  const struct glsl_type *t=var->type;const char *why=NULL;unsigned binding=var->data.binding;
  if(var->data.mode==nir_var_mem_ubo) {
   if(!glsl_type_is_struct_or_ifc(t))return "Generic UBO must be a block";
   why=generic_record(plan,binding,TEGU_MALI_GENERIC_UBO,stage,glsl_get_explicit_size(t,true),0);
  } else if(var->data.mode==nir_var_mem_ssbo) {
   if(!(var->data.access&ACCESS_NON_WRITEABLE))return "Generic storage buffers must be read-only";
   why=generic_record(plan,binding,TEGU_MALI_GENERIC_READONLY_STORAGE,stage,0,0);
  } else if(glsl_type_is_array(t)) {
   return "Generic texture or sampler arrays are unsupported";
  } else if(glsl_type_is_image(t)) {
   enum glsl_sampler_dim dim=glsl_get_sampler_dim(t);
   if(dim!=GLSL_SAMPLER_DIM_SUBPASS&&dim!=GLSL_SAMPLER_DIM_SUBPASS_MS)return "Generic storage images are unsupported";
   why=generic_record(plan,binding,TEGU_MALI_GENERIC_INPUT_ATTACHMENT,stage,0,var->data.index);
  } else if(glsl_type_is_texture(t)) {
   why=generic_record(plan,binding,TEGU_MALI_GENERIC_TEXTURE,stage,0,0);
  } else if(glsl_type_is_sampler(t)) {
   why=generic_record(plan,binding,glsl_get_sampler_dim(t)==GLSL_SAMPLER_DIM_SUBPASS?
    TEGU_MALI_GENERIC_INPUT_ATTACHMENT:(glsl_get_sampler_result_type(t)==GLSL_TYPE_VOID?
    TEGU_MALI_GENERIC_SAMPLER:TEGU_MALI_GENERIC_COMBINED),stage,0,0);
  } else return "Generic resource type unsupported";
  if(why)return why;
 }
 /* Textures and samplers: immediate Valhall handles, as panvk does. */
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr_safe(instr,block) {
  if(instr->type!=nir_instr_type_tex)continue;
  nir_tex_instr *tex=nir_instr_as_tex(instr);
  nir_builder b=nir_builder_at(nir_before_instr(&tex->instr));
  int texture_binding=-1,has_sampler=0;
  for(int n=tex->num_srcs-1;n>=0;n--) {
   nir_tex_src_type type=tex->src[n].src_type;
   if(type!=nir_tex_src_texture_deref&&type!=nir_tex_src_sampler_deref)continue;
   nir_deref_instr *d=nir_src_as_deref(tex->src[n].src);
   nir_variable *var=d?nir_deref_instr_get_variable(d):NULL;
   if(!var||d->deref_type!=nir_deref_type_var)return "Generic texture deref must name a variable";
   unsigned table=type==nir_tex_src_texture_deref?TEGU_MALI_GENERIC_TEXTURE_TABLE:TEGU_MALI_GENERIC_SAMPLER_TABLE;
   if(type==nir_tex_src_texture_deref)texture_binding=(int)var->data.binding;else has_sampler=1;
   nir_tex_instr_remove_src(tex,n);
   nir_tex_instr_add_src(tex,type==nir_tex_src_texture_deref?nir_tex_src_texture_handle:nir_tex_src_sampler_handle,
    nir_imm_int(&b,pan_res_handle(table,var->data.binding)));
  }
  /* Valhall texel fetches still name a sampler; Mesa otherwise uses handle
   * 0, which here is table 0 (vertex attributes).  Userspace places a
   * nearest sampler at table 2 of the fetched binding (framebuffer fetch). */
  if(!has_sampler&&texture_binding>=0&&!nir_tex_instr_need_sampler(tex))
   nir_tex_instr_add_src(tex,nir_tex_src_sampler_handle,
    nir_imm_int(&b,pan_res_handle(TEGU_MALI_GENERIC_SAMPLER_TABLE,(unsigned)texture_binding)));
  /* Keep array coordinates intact. Input attachments already carry layer 0
   * from generic_zero_layer; ordinary 2D-array fetches must retain their own
   * layer. Valhall lowers both forms without losing that coordinate. */
 }
 bool descriptors=true;
 NIR_PASS(_,s,nir_shader_intrinsics_pass,generic_buffer_index,nir_metadata_control_flow,&descriptors);
 descriptors=false;
 NIR_PASS(_,s,nir_shader_intrinsics_pass,generic_buffer_index,nir_metadata_control_flow,&descriptors);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ubo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ssbo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_shader_intrinsics_pass,pack_ubo_index,nir_metadata_control_flow,NULL);
 NIR_PASS(_,s,nir_opt_dce);NIR_PASS(_,s,nir_remove_dead_derefs);
 NIR_PASS(_,s,nir_remove_dead_variables,nir_var_uniform|nir_var_image,NULL);
 nir_foreach_variable_with_modes(var,s,nir_var_uniform|nir_var_image)return "Generic texture variable survived lowering";
 NIR_PASS(_,s,nir_opt_constant_folding);nir_validate_shader(s,"tegu generic resources");
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));return NULL;
}

/* Multiple-render-target emulation: one TGJ1 job per stored colour target.
 * Keep fragment output `rt` as DATA0 and turn every other colour output into
 * a dead temporary, so the job writes exactly the target it was built for. */
static const char *select_generic_output(nir_shader *s,unsigned rt) {
 bool found=false;
 nir_foreach_variable_with_modes(var,s,nir_var_shader_out) {
  unsigned loc=var->data.location;
  if(loc<FRAG_RESULT_DATA0||loc>FRAG_RESULT_DATA7)continue;
  if(loc-FRAG_RESULT_DATA0==rt&&var->data.index==0){var->data.location=FRAG_RESULT_DATA0;found=true;}
  else var->data.mode=nir_var_shader_temp;
 }
 if(!found)return "Selected colour output is not written";
 nir_fixup_deref_modes(s);
 NIR_PASS(_,s,nir_lower_global_vars_to_local);NIR_PASS(_,s,nir_lower_vars_to_ssa);
 NIR_PASS(_,s,nir_opt_dce);NIR_PASS(_,s,nir_remove_dead_variables,nir_var_shader_temp|nir_var_function_temp,NULL);
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 return NULL;
}

/* True multi-render-target: keep every colour output whose attachment is
 * bound (format nonzero) at its own location; outputs to unbound attachments
 * are discarded exactly as Metal does. Formats are the n2r-qualified float
 * classes; integer outputs, dual-source blending and outputs past the
 * attachment list are refused. *mask gets the written target set. */
static bool mrt_format_ok(uint32_t f){return f==10||f==70||f==80||f==115;}
static const char *select_mrt_outputs(nir_shader *s,const uint32_t *formats,unsigned count,unsigned *mask) {
 *mask=0;
 nir_foreach_variable_with_modes(var,s,nir_var_shader_out) {
  unsigned loc=var->data.location;
  if(loc<FRAG_RESULT_DATA0||loc>FRAG_RESULT_DATA7)continue;
  unsigned rt=loc-FRAG_RESULT_DATA0;
  if(var->data.index)return "MRT dual-source blending is unsupported";
  if(rt>=count)return "MRT colour output beyond the attachment list";
  if(!formats[rt]){var->data.mode=nir_var_shader_temp;continue;}
  const struct glsl_type *t=var->type;
  if(!glsl_type_is_vector_or_scalar(t)||(glsl_get_base_type(t)!=GLSL_TYPE_FLOAT&&glsl_get_base_type(t)!=GLSL_TYPE_FLOAT16))
   return "MRT colour outputs must be float for the qualified unorm/float attachment formats";
  if(*mask&(1u<<rt))return "Aliased MRT colour output";
  *mask|=1u<<rt;
 }
 if(!*mask)return "MRT fragment writes no bound colour attachment";
 nir_fixup_deref_modes(s);
 NIR_PASS(_,s,nir_lower_global_vars_to_local);NIR_PASS(_,s,nir_lower_vars_to_ssa);
 NIR_PASS(_,s,nir_opt_dce);NIR_PASS(_,s,nir_remove_dead_variables,nir_var_shader_temp|nir_var_function_temp,NULL);
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 return NULL;
}
/* Preserve the legacy default plan's conservative declarations. An effective
 * specialization may remove resources, so reflect its remaining operations.
 * Buffers already use binding-index intrinsics rather than variable derefs. */
static void prune_specialized_resources(nir_shader *s) {
 fold_fixed_ui_texture_r85(s);
 NIR_PASS(_,s,nir_remove_dead_variables,nir_var_uniform|nir_var_image,NULL);
 nir_foreach_variable_with_modes_safe(var,s,nir_var_mem_ubo|nir_var_mem_ssbo) {
  bool live=false;
  nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
   if(instr->type!=nir_instr_type_intrinsic)continue;
   nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
   if(i->intrinsic==nir_intrinsic_vulkan_resource_index&&
      nir_intrinsic_desc_set(i)==var->data.descriptor_set&&nir_intrinsic_binding(i)==var->data.binding)live=true;
  }
  if(!live)exec_node_remove(&var->node);
 }
}

static nir_alu_type varying_alu_type(uint8_t type) {
 return type==1?nir_type_int32:type==2?nir_type_uint32:nir_type_float32;
}
static int compile_pair_specialized(const void *vs_data,size_t vs_length,const void *fs_data,size_t fs_length,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 bool v2,struct tegu_mali_texture_plan *texture,struct tegu_mali_resource_plan *resources,
 struct tegu_mali_fixed_ui_plan *fixed_ui,struct tegu_mali_fixed_ui_texture_r85_plan *fixed_ui_texture,
 struct tegu_mali_generic_plan *generic,unsigned resource_samples,
 uint32_t resource_code_limit,bool implicit_lod,char *error,size_t error_size,
 struct nir_spirv_specialization *vertex_specialization,
 struct nir_spirv_specialization *fragment_specialization,bool prune_vertex,bool prune_fragment,
 struct tegu_mali_varying_info *ext,const uint32_t *mrt_formats,unsigned mrt_count) {
 const char *reason="Varying interface mismatch";int status=5;
 glsl_type_singleton_init_or_ref();
 bool has_buffers=resources||fixed_ui||fixed_ui_texture||generic;
 bool storage=fixed_ui||fixed_ui_texture||generic;
 nir_shader *vn=translate_specialized(vs_data,vs_length,MESA_SHADER_VERTEX,&status,&reason,has_buffers,storage,vertex_specialization,generic!=NULL),*fn=NULL;
 if(vn)fn=translate_specialized(fs_data,fs_length,MESA_SHADER_FRAGMENT,&status,&reason,has_buffers,storage,fragment_specialization,generic!=NULL);
 struct stage_job vj={.requested=TEGU_GRAPHICS_VERTEX,.stage=MESA_SHADER_VERTEX,.vertex_inputs=true,.pair=true,.point_size=v2,.resource_code_limit=resource_code_limit};
 struct stage_job fj={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,.pair=true,.resource_code_limit=resource_code_limit};
 uint32_t vmask=0,fmask=0;uint8_t vcomps[32]={0},fcomps[32]={0};
 uint8_t vtypes[32]={0},ftypes[32]={0},vinterp[32]={0},finterp[32]={0};
 unsigned mrt_mask=0;
 const bool extv=ext!=NULL;
 if(!fn)goto cleanup;
 status=5;
 const char *why;
 if(resources) {
  vj.resources=fj.resources=true;
  if((why=lower_ubos(vn,resources))||(why=lower_ubos(fn,resources))||
     (why=lower_texture(fn,true,resource_samples,implicit_lod))){reason=why;goto cleanup;}
  resources->sample_count=resource_samples;
 }
 if(fixed_ui) {
  vj.resources=fj.resources=true;
  if((why=lower_fixed_ui_ubo(vn,fixed_ui))||(why=lower_fixed_ui_ubo(fn,fixed_ui))) {
   reason=why;goto cleanup;
  }
 }
 if(fixed_ui_texture) {
  vj.resources=fj.resources=true;fold_fixed_ui_texture_r85(fn);
  if((why=lower_fixed_ui_texture_r85_buffers(vn,fixed_ui_texture))||
     (why=lower_fixed_ui_texture_r85_buffers(fn,fixed_ui_texture))||
     (why=lower_fixed_ui_texture_r85_sample(fn))){reason=why;goto cleanup;}
 }
 if(texture&&(why=lower_texture(fn,false,1,false))){reason=why;goto cleanup;}
 if(generic) {
  vj.resources=fj.resources=true;
  vj.draw_params=(generic->reserved&TEGU_GENERIC_COMPILE_DRAW_PARAMS)!=0;
  vj.allow_tls=fj.allow_tls=(generic->reserved&TEGU_GENERIC_COMPILE_TLS)!=0;
  /* reserved = 1 + selected colour output (tegu_mali_compile_generic_pair_rt). */
  if((generic->reserved&0xffu)&&(why=select_generic_output(fn,(generic->reserved&0xffu)-1))){reason=why;goto cleanup;}
  if(mrt_formats) {
   unsigned mask=0;
   if((generic->reserved&0xffu)||(why=select_mrt_outputs(fn,mrt_formats,mrt_count,&mask))) {
    reason=why?why:"MRT cannot be combined with a selected colour output";goto cleanup;
   }
   fj.policy=GRAPHICS_POLICY_MRT;mrt_mask=mask;
  }
  if(prune_vertex)prune_specialized_resources(vn);
  if(prune_fragment)prune_specialized_resources(fn);
  if((why=lower_generic_resources(vn,generic))||(why=lower_generic_resources(fn,generic))){reason=why;goto cleanup;}
 }
 /* AIR specialization can leave interface declarations whose values no longer
  * reach the fragment entry.  Link the live post-specialization interface
  * before collecting the strict masks and before either backend stage sees it.
  * The checks below still require every remaining declaration to be live and
  * every fragment input to have a matching vertex output of the same shape. */
 NIR_PASS(_,fn,nir_remove_dead_variables,nir_var_shader_in,NULL);
 /* Generic: the FS may read a subset (even none) of the VS varyings; keep
  * the VS outputs so IDVS still has a varying program to run. */
 if(!generic)nir_remove_unused_varyings(vn,fn);
 nir_shader_gather_info(vn,nir_shader_get_entrypoint(vn));
 /* Generic pair whose VS writes no varying (a flat-colour fragment, R161
  * chronod plane_color_fragment): IDVS still needs a varying program, so the
  * VS writes one unused vec4. */
 if(generic&&!(vn->info.outputs_written&BITFIELD64_RANGE(VARYING_SLOT_VAR0,31))) {
  nir_variable *pad=nir_variable_create(vn,nir_var_shader_out,glsl_vec4_type(),"tegu_varying_pad");
  pad->data.location=VARYING_SLOT_VAR0;pad->data.interpolation=INTERP_MODE_SMOOTH;
  nir_function_impl *entry=nir_shader_get_entrypoint(vn);
  nir_builder pb=nir_builder_at(nir_after_impl(entry));
  nir_store_var(&pb,pad,nir_imm_vec4(&pb,0,0,0,0),0xf);
  nir_progress(true,entry,nir_metadata_control_flow);
  nir_shader_gather_info(vn,entry);
 }
 nir_shader_gather_info(fn,nir_shader_get_entrypoint(fn));
 if((why=collect_varyings_ex(vn,nir_var_shader_out,&vmask,vcomps,extv,vtypes,vinterp))||
    (why=collect_varyings_ex(fn,nir_var_shader_in,&fmask,fcomps,extv,ftypes,finterp))){reason=why;goto cleanup;}
 // Declared and live sets must agree, so an unwritten output cannot leave an
 // FS reading undefined generic memory and no dead declaration hides a gap.
 uint32_t vlive=(uint32_t)((vn->info.outputs_written>>VARYING_SLOT_VAR0)&BITFIELD_MASK(31));
 uint32_t flive=(uint32_t)((fn->info.inputs_read>>VARYING_SLOT_VAR0)&BITFIELD_MASK(31));
 if(fn->info.inputs_read&~BITFIELD64_RANGE(VARYING_SLOT_VAR0,31)){reason="Fragment builtin inputs are unsupported";goto cleanup;}
 if(vlive!=vmask){reason="Vertex varying declared but not written";goto cleanup;}
 if(flive!=fmask){reason="Fragment varying declared but not read";goto cleanup;}
 if(generic?(fmask&~vmask)!=0:vmask!=fmask){reason="Varying interface mismatch: VS outputs and FS inputs differ";goto cleanup;}
 if(!vmask){reason="Pair requires at least one generic varying";goto cleanup;}
 if(util_bitcount(vmask)>(extv?TEGU_MALI_EXT_MAX_VARYINGS:TEGU_MALI_PAIR_POLICY_VARYINGS)){reason="More generic varyings than qualified policy";goto cleanup;}
 u_foreach_bit(loc,generic?fmask:vmask)if(vcomps[loc]!=fcomps[loc]){reason="Varying component count mismatch";goto cleanup;}
 u_foreach_bit(loc,generic?fmask:vmask)if(vtypes[loc]!=ftypes[loc]){reason="Varying scalar type differs between stages";goto cleanup;}
 fj.noperspective=vj.noperspective=pan_nir_collect_noperspective_varyings_fs(fn);
 if(vj.noperspective&&!extv){reason="Noperspective varyings unsupported";goto cleanup;}

 if((status=compile_stage(vn,&vj,vs,&reason)))goto cleanup;
 status=5;
 const struct pan_shader_info *vi=&vj.info;
 const struct pan_varying_layout *layout=&vi->varyings.formats;
 if(!vi->vs.secondary_enable||!vi->vs.secondary_offset||vi->vs.secondary_offset>=vs->size||
    vi->vs.secondary_offset%128){reason="Missing or misaligned IDVS varying program";goto cleanup;}
 if(!v2&&(vi->vs.no_psiz_offset||vi->vs.needs_extended_fifo)){reason="Point-size/extended-FIFO vertex variants unsupported";goto cleanup;}
 // needs_extended_fifo is computed with PSIZ already excluded (Mesa patches it
 // out of the no_psiz variant), so it is relevant to the triangle draw too.
 if(vi->vs.needs_extended_fifo){reason="Vertex shader needs extended FIFO beyond point size (unsupported)";goto cleanup;}
 bool psiz=vn->info.outputs_written&BITFIELD64_BIT(VARYING_SLOT_PSIZ);
 uint32_t sec=vi->vs.secondary_offset,np=vi->vs.no_psiz_offset;
 // Mesa: [PSIZ position @0][PSIZ store NOPed @no_psiz_offset][varying]. Any
 // other arrangement means the backend changed under us.
 if(psiz!=vi->vs.writes_point_size||psiz!=(np!=0)||(psiz&&(np%128||np>=sec))) {
  reason="Unexpected point-size position variant layout";goto cleanup;
 }
 // The SPD preload field only carries r48..r63.
 if((vi->preload|vi->vs.secondary_preload)&BITFIELD64_MASK(48)){reason="Vertex preload outside r48..r63";goto cleanup;}
 if(!(layout->known&PAN_VARYING_LAYOUT_KNOWN)||!layout->generic_size_B||layout->generic_size_B%4){reason="Vertex varying layout unknown";goto cleanup;}
 uint32_t seen=0,end=0,count=0;bool psiz_slot=false;
 for(unsigned i=0;i<layout->count;i++) {
  const struct pan_varying_slot *slot=pan_varying_layout_slot_at(layout,i);
  if(slot&&v2&&slot->location==VARYING_SLOT_PSIZ) {
   // Recorded for a future point draw; the triangle variant never stores it.
   if(!psiz||psiz_slot||slot->section!=PAN_VARYING_SECTION_ATTRIBS||slot->offset<0||
      slot->ncomps!=1||slot->alu_type!=nir_type_float16){reason="Unexpected point-size varying slot";goto cleanup;}
   psiz_slot=true;pair->point_size_section=slot->section;pair->point_size_offset=(uint32_t)slot->offset;
   pair->point_size_alu_type=slot->alu_type;continue;
  }
  if(!slot||slot->section!=PAN_VARYING_SECTION_GENERIC)continue;
  unsigned loc=slot->location-VARYING_SLOT_VAR0;
  if(slot->location<VARYING_SLOT_VAR0||loc>=31||!(vmask&BITFIELD_BIT(loc))||(seen&BITFIELD_BIT(loc))||
     slot->alu_type!=varying_alu_type(vtypes[loc])||slot->ncomps!=vcomps[loc]||slot->offset<0||slot->offset%4||
     count>=(extv?TEGU_MALI_EXT_MAX_VARYINGS:TEGU_MALI_PAIR_MAX_VARYINGS)){reason="Unexpected vertex varying slot format";goto cleanup;}
  seen|=BITFIELD_BIT(loc);
  if((uint32_t)slot->offset+slot->ncomps*4>end)end=slot->offset+slot->ncomps*4;
  if(extv)ext->slots[count++]=(struct tegu_mali_varying_slot2){loc,(uint32_t)slot->offset,slot->ncomps,slot->alu_type,
   (fmask&BITFIELD_BIT(loc))?finterp[loc]:0u,0};
  else pair->varyings[count++]=(struct tegu_mali_varying_slot){loc,(uint32_t)slot->offset,slot->ncomps,slot->alu_type};
 }
 /* Generic pairs: Mesa omits a VS output whose store folded away (an undefined
  * value). That is only acceptable for locations the FS never reads; every
  * FS-read location must still have a layout slot (checked again below). */
 if((generic?(seen&fmask)!=fmask:seen!=vmask)||end>layout->generic_size_B){reason="Vertex varying layout does not cover interface";goto cleanup;}
 if(generic)vmask=seen;
 if(psiz!=psiz_slot){reason="Point-size varying slot missing";goto cleanup;}

 fj.vs_layout=layout;
 if((status=compile_stage(fn,&fj,fs,&reason)))goto cleanup;
 status=5;
 const struct pan_shader_info *fi=&fj.info;
 if(mrt_formats&&fj.targets_seen!=mrt_mask){reason="MRT blend descriptors differ from the written colour outputs";goto cleanup;}
 if(mrt_formats)generic->reserved=TEGU_MALI_GENERIC_PLAN_MRT|mrt_mask;
 if(fi->bifrost.uses_ld_var||(!extv&&(fi->bifrost.uses_flat_shading||fi->varyings.noperspective))||
    (extv&&fi->varyings.noperspective!=vj.noperspective)){reason="Fragment varying access mode unsupported";goto cleanup;}
 /* The FS was compiled with the VS layout above.  Mesa retains only the
  * fragment formats needed for attribute emission in fi->varyings.formats;
  * it is not a second enumeration of the linked NIR interface.  Validate the
  * live fragment mask against the authoritative VS layout instead. */
 u_foreach_bit(loc,fmask) {
  const struct pan_varying_slot *v=pan_varying_layout_find_slot(layout,VARYING_SLOT_VAR0+loc);
  if(!v||v->section!=PAN_VARYING_SECTION_GENERIC||v->alu_type!=varying_alu_type(ftypes[loc])||
     v->ncomps<fcomps[loc]){reason="Fragment varying format differs from vertex layout";goto cleanup;}
 }
 pair->flags=psiz?TEGU_MALI_PAIR2_WRITES_POINT_SIZE:0;
 pair->position_offset=psiz?np:0;pair->position_size=sec-pair->position_offset;
 pair->point_position_offset=0;pair->point_position_size=psiz?np:0;
 pair->secondary_offset=sec;pair->secondary_size=vs->size-sec;
 pair->secondary_registers=vi->vs.secondary_work_reg_count;
 pair->secondary_preload=vi->vs.secondary_preload;
 pair->generic_size_B=layout->generic_size_B;
 pair->vs_attribute_mask=vj.attribute_mask;
 pair->vs_varying_mask=vmask;pair->fs_varying_mask=fmask;
 pair->varying_count=extv?0:count;pair->version=TEGU_MALI_GRAPHICS_PAIR2_VERSION;status=0;
 if(extv) {
  uint32_t flat=0;u_foreach_bit(loc,fmask)if(finterp[loc]==TEGU_MALI_VARYING_FLAT)flat|=BITFIELD_BIT(loc);
  pair->noperspective_mask=vj.noperspective;
  if(fi->bifrost.uses_flat_shading)pair->flags|=TEGU_MALI_PAIR2_FLAT_SHADING;
  ext->version=TEGU_MALI_VARYING_INFO_VERSION;ext->count=count;ext->vs_mask=vmask;ext->fs_mask=fmask;
  ext->flat_mask=flat;ext->noperspective_mask=vj.noperspective;ext->generic_size_B=layout->generic_size_B;
  ext->uses_flat_shading=fi->bifrost.uses_flat_shading?1u:0u;
 }
 if(texture)*texture=(struct tegu_mali_texture_plan){TEGU_MALI_TEXTURE_PLAN_VERSION,TEXTURE_TABLE,
  TEXTURE_INDEX,TEXTURE_SAMPLER,1,0,0};
cleanup:
 if(status) {
  fail(error,error_size,reason);
  tegu_mali_graphics_free(vs);tegu_mali_graphics_free(fs);memset(pair,0,sizeof(*pair));
  if(texture)memset(texture,0,sizeof(*texture));
  if(resources)memset(resources,0,sizeof(*resources));
  if(fixed_ui)memset(fixed_ui,0,sizeof(*fixed_ui));
  if(fixed_ui_texture)memset(fixed_ui_texture,0,sizeof(*fixed_ui_texture));
  if(ext)memset(ext,0,sizeof(*ext));
 }
 if(vn)ralloc_free(vn);
 if(fn)ralloc_free(fn);
 glsl_type_singleton_decref();return status;
}
static int compile_pair(const void *vs_data,size_t vs_length,const void *fs_data,size_t fs_length,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 bool v2,struct tegu_mali_texture_plan *texture,struct tegu_mali_resource_plan *resources,
 struct tegu_mali_fixed_ui_plan *fixed_ui,struct tegu_mali_fixed_ui_texture_r85_plan *fixed_ui_texture,
 struct tegu_mali_generic_plan *generic,unsigned resource_samples,
 uint32_t resource_code_limit,bool implicit_lod,char *error,size_t error_size) {
 return compile_pair_specialized(vs_data,vs_length,fs_data,fs_length,vs,fs,pair,v2,
  texture,resources,fixed_ui,fixed_ui_texture,generic,resource_samples,resource_code_limit,
  implicit_lod,error,error_size,NULL,NULL,false,false,NULL,NULL,0);
}
static bool pair_outputs_empty(const struct tegu_mali_graphics *vs,const struct tegu_mali_graphics *fs) {
 return vs&&fs&&vs!=fs&&!vs->code&&!vs->size&&!fs->code&&!fs->size;
}
int tegu_mali_compile_graphics_pair(const void *vs_data,size_t vs_length,const void *fs_data,size_t fs_length,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair *pair,
 char *error,size_t error_size) {
 if(error&&error_size)error[0]=0;
 if(!pair||pair->version||!pair_outputs_empty(vs,fs)){fail(error,error_size,"Output must be empty");return 1;}
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 struct tegu_mali_graphics_pair2 p={0};
 int status=compile_pair(vs_data,vs_length,fs_data,fs_length,vs,fs,&p,false,NULL,NULL,NULL,NULL,NULL,0,0,false,error,error_size);
 if(status)return status;
 pair->version=TEGU_MALI_GRAPHICS_PAIR_VERSION;
 pair->position_size=pair->secondary_offset=p.secondary_offset;
 pair->secondary_size=p.secondary_size;pair->secondary_registers=p.secondary_registers;
 pair->generic_size_B=p.generic_size_B;pair->secondary_preload=p.secondary_preload;
 pair->vs_attribute_mask=p.vs_attribute_mask;pair->vs_varying_mask=p.vs_varying_mask;
 pair->fs_varying_mask=p.fs_varying_mask;pair->fs_uses_ld_var=p.fs_uses_ld_var;
 pair->noperspective_mask=p.noperspective_mask;pair->varying_count=p.varying_count;
 memcpy(pair->varyings,p.varyings,sizeof(pair->varyings));
 return 0;
}
int tegu_mali_compile_graphics_pair2(const void *vs_data,size_t vs_length,const void *fs_data,size_t fs_length,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 char *error,size_t error_size) {
 if(error&&error_size)error[0]=0;
 if(!pair||pair->version||!pair_outputs_empty(vs,fs)){fail(error,error_size,"Output must be empty");return 1;}
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 return compile_pair(vs_data,vs_length,fs_data,fs_length,vs,fs,pair,true,NULL,NULL,NULL,NULL,NULL,0,0,false,error,error_size);
}
static bool valid_span(const void *p,size_t n) {
 uintptr_t x=(uintptr_t)p;
 return p&&n&&n-1<=UINTPTR_MAX-x;
}
static bool overlaps(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 if(!a||!b||!an||!bn)return false;
 return x<=y?y-x<an:x-y<bn;
}
int tegu_mali_compile_graphics_pair_texture(const void *vs_data,size_t vs_length,const void *fs_data,size_t fs_length,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_texture_plan *texture,char *error,size_t error_size) {
 const void *out[5]={vs,fs,pair,texture,error};
 size_t len[5]={sizeof(*vs),sizeof(*fs),sizeof(*pair),sizeof(*texture),error?error_size:0};
 bool alias=false;
 // Outputs are cleared before translation, so no output may overlap another
 // output or an input buffer.
 for(unsigned i=0;i<5;i++) {
  for(unsigned j=i+1;j<5;j++)alias|=overlaps(out[i],len[i],out[j],len[j]);
  alias|=overlaps(out[i],len[i],vs_data,vs_length)||overlaps(out[i],len[i],fs_data,fs_length);
 }
 if(alias){error=NULL;error_size=0;}
 if(error&&error_size)error[0]=0;
 if(alias||!pair||pair->version||!texture||texture->version||!pair_outputs_empty(vs,fs)){
  fail(error,error_size,"Output must be empty");return 1;
 }
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));memset(texture,0,sizeof(*texture));
 return compile_pair(vs_data,vs_length,fs_data,fs_length,vs,fs,pair,true,texture,NULL,NULL,NULL,NULL,0,0,false,error,error_size);
}

int tegu_mali_compile_graphics_pair_resources(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_resource_plan *plan,char *error,size_t error_size) {
 const void *out[5]={vs,fs,pair,plan,error};
 size_t len[5]={sizeof(*vs),sizeof(*fs),sizeof(*pair),sizeof(*plan),error?error_size:0};
 bool alias=false;
 for(unsigned i=0;i<5;i++) {
  for(unsigned j=i+1;j<5;j++)alias|=overlaps(out[i],len[i],out[j],len[j]);
  alias|=overlaps(out[i],len[i],vd,vn)||overlaps(out[i],len[i],fd,fn);
 }
 if(alias)return 1;
 if(error&&error_size)error[0]=0;
 if(!pair||pair->version||!plan||plan->version||!pair_outputs_empty(vs,fs)) {
  fail(error,error_size,"Output must be empty");return 1;
 }
 struct tegu_mali_resource_plan tmp={0};
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 int status=compile_pair(vd,vn,fd,fn,vs,fs,pair,true,NULL,&tmp,NULL,NULL,NULL,4,TEGU_MALI_RESOURCE_CODE_LIMIT,false,error,error_size);
 if(!status){tmp.version=1;tmp.table=1;tmp.texture_index=4;tmp.sampler_index=5;
  tmp.normalized_coordinates=1;tmp.sample_count=4;*plan=tmp;}
 return status;
}

int tegu_mali_compile_graphics_pair_resources_v7(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_resource_plan *plan,char *error,size_t error_size) {
 const void *out[5]={vs,fs,pair,plan,error};
 size_t len[5]={sizeof(*vs),sizeof(*fs),sizeof(*pair),sizeof(*plan),error?error_size:0};
 bool alias=false;
 for(unsigned i=0;i<5;i++) {
  for(unsigned j=i+1;j<5;j++)alias|=overlaps(out[i],len[i],out[j],len[j]);
  alias|=overlaps(out[i],len[i],vd,vn)||overlaps(out[i],len[i],fd,fn);
 }
 if(alias)return 1;
 if(error&&error_size)error[0]=0;
 if(!pair||pair->version||!plan||plan->version||!pair_outputs_empty(vs,fs)) {
  fail(error,error_size,"Output must be empty");return 1;
 }
 struct tegu_mali_resource_plan tmp={0};
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 int status=compile_pair(vd,vn,fd,fn,vs,fs,pair,true,NULL,&tmp,NULL,NULL,NULL,16,32768u,false,error,error_size);
 int class_mismatch=0;
 if(!status) {
  static const uint32_t expected_ubo[4]={80,32,48,32};
  if(vs->size!=1152||fs->size!=25600||tmp.sample_count!=16)class_mismatch=1;
  for(unsigned i=0;!class_mismatch&&i<4;i++)if(tmp.ubos[i].declared_bytes!=expected_ubo[i])class_mismatch=1;
 }
 if(status||class_mismatch) {
  tegu_mali_graphics_free(vs);tegu_mali_graphics_free(fs);
  memset(pair,0,sizeof(*pair));
  if(class_mismatch){fail(error,error_size,"8-tap code/resource output differs from exact class");return 5;}
  return status;
 }
 tmp.version=1;tmp.table=1;tmp.texture_index=4;tmp.sampler_index=5;tmp.normalized_coordinates=1;tmp.sample_count=16;
 *plan=tmp;return 0;
}

int tegu_mali_compile_fixed_ui_pair_r80(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_fixed_ui_plan *plan,char *error,size_t error_size) {
 const void *out[5]={vs,fs,pair,plan,error};
 const size_t len[5]={sizeof(*vs),sizeof(*fs),sizeof(*pair),sizeof(*plan),error?error_size:0};
 if(!vd||!fd||!vn||!fn||!valid_span(vd,vn)||!valid_span(fd,fn)||
    !vs||!fs||!pair||!plan||!valid_span(vs,sizeof(*vs))||!valid_span(fs,sizeof(*fs))||
    !valid_span(pair,sizeof(*pair))||!valid_span(plan,sizeof(*plan))||
    (error_size&&(!error||!valid_span(error,error_size))))return 1;
 for(unsigned i=0;i<5;i++) {
  if(len[i]&&!valid_span(out[i],len[i]))return 1;
  for(unsigned j=i+1;j<5;j++)if(overlaps(out[i],len[i],out[j],len[j]))return 1;
  if(overlaps(out[i],len[i],vd,vn)||overlaps(out[i],len[i],fd,fn))return 1;
 }
 if(error&&error_size)error[0]=0;
 if(pair->version||plan->version||!pair_outputs_empty(vs,fs)) {
  fail(error,error_size,"Output must be empty");return 1;
 }
 struct tegu_mali_fixed_ui_plan tmp={0};
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 int status=compile_pair(vd,vn,fd,fn,vs,fs,pair,true,NULL,NULL,&tmp,NULL,NULL,0,
  TEGU_MALI_FIXED_UI_CODE_LIMIT,false,error,error_size);
 if(status)return status;
 tmp.version=TEGU_MALI_FIXED_UI_PLAN_VERSION;tmp.table=1;
 tmp.resource_count=3;tmp.texture_count=0;tmp.constant_bytes=8;
 tmp.constant_access_bytes=84*4;*plan=tmp;return 0;
}

int tegu_mali_compile_fixed_ui_texture_pair_r85(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_fixed_ui_texture_r85_plan *plan,char *error,size_t error_size) {
 const void *out[5]={vs,fs,pair,plan,error};
 const size_t len[5]={sizeof(*vs),sizeof(*fs),sizeof(*pair),sizeof(*plan),error?error_size:0};
 if(!vd||!fd||!vn||!fn||!valid_span(vd,vn)||!valid_span(fd,fn)||!vs||!fs||!pair||!plan||
    !valid_span(vs,sizeof(*vs))||!valid_span(fs,sizeof(*fs))||!valid_span(pair,sizeof(*pair))||
    !valid_span(plan,sizeof(*plan))||(error_size&&(!error||!valid_span(error,error_size))))return 1;
 for(unsigned i=0;i<5;i++) {
  if(len[i]&&!valid_span(out[i],len[i]))return 1;
  for(unsigned j=i+1;j<5;j++)if(overlaps(out[i],len[i],out[j],len[j]))return 1;
  if(overlaps(out[i],len[i],vd,vn)||overlaps(out[i],len[i],fd,fn))return 1;
 }
 if(error&&error_size)error[0]=0;
 if(pair->version||plan->version||!pair_outputs_empty(vs,fs)){
  fail(error,error_size,"Output must be empty");return 1;
 }
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 struct tegu_mali_fixed_ui_texture_r85_plan tmp={0};
 int status=compile_pair(vd,vn,fd,fn,vs,fs,pair,true,NULL,NULL,NULL,&tmp,NULL,0,
  TEGU_MALI_FIXED_UI_CODE_LIMIT,false,error,error_size);
 if(status)return status;
 static const uint32_t expected_bytes[5]={320,272,432,272,0};
 /* compile_pair already proves the IDVS section bounds and the complete live
  * varying interface.  Specializations legitimately change code length,
  * register pressure and generic layout, so keep only the hardware and R85
  * resource contract here. */
 bool mismatch=!vs->size||vs->size>TEGU_MALI_FIXED_UI_CODE_LIMIT||(vs->size&127)||
  !vs->registers||vs->registers>64||vs->tls||!vs->fau_words||vs->fau_words>64||
  vs->sysval_mask!=3||vs->push_bytes!=8||(vs->preload&UINT64_C(0x0000ffffffffffff))||
  !fs->size||fs->size>TEGU_MALI_FIXED_UI_CODE_LIMIT||(fs->size&127)||
  !fs->registers||fs->registers>64||fs->tls||!fs->fau_words||fs->fau_words>64||
  fs->sysval_mask!=8||fs->push_bytes!=76||(fs->preload&UINT64_C(0x0000ffffffffffff))||
  pair->flags!=TEGU_MALI_PAIR2_WRITES_POINT_SIZE||pair->vs_attribute_mask!=7||
  !pair->vs_varying_mask||pair->vs_varying_mask!=pair->fs_varying_mask||
  !pair->varying_count||pair->varying_count>TEGU_MALI_PAIR_MAX_VARYINGS||
  tmp.constant_access_bytes;
 for(unsigned i=0;!mismatch&&i<5;i++)if(tmp.buffers[i].declared_bytes!=expected_bytes[i])mismatch=true;
 if(mismatch) {
  if(error&&error_size)snprintf(error,error_size,
   "R85 output outside bounded class: vs=%zu/r%u/f%u/s%u/p%u/pre%llx fs=%zu/r%u/f%u/s%u/p%u/pre%llx flags=%u attr=%x vary=%x/%x count=%u const=%u",
   vs->size,vs->registers,vs->fau_words,vs->sysval_mask,vs->push_bytes,(unsigned long long)vs->preload,
   fs->size,fs->registers,fs->fau_words,fs->sysval_mask,fs->push_bytes,(unsigned long long)fs->preload,
   pair->flags,pair->vs_attribute_mask,pair->vs_varying_mask,pair->fs_varying_mask,pair->varying_count,
   tmp.constant_access_bytes);
  tegu_mali_graphics_free(vs);tegu_mali_graphics_free(fs);memset(pair,0,sizeof(*pair));
  return 5;
 }
 tmp.version=TEGU_MALI_FIXED_UI_TEXTURE_R85_PLAN_VERSION;tmp.table=1;tmp.resource_count=5;
 tmp.texture_index=5;tmp.sampler_index=6;tmp.normalized_coordinates=1;
 tmp.sample_count=1;tmp.implicit_lod=1;tmp.has_bias=1;tmp.constant_bytes=8;*plan=tmp;return 0;
}

int tegu_mali_compile_single_pass_blur3_pair(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_single_pass_blur3_plan *plan,char *error,size_t error_size) {
 const void *out[5]={vs,fs,pair,plan,error};
 size_t len[5]={sizeof(*vs),sizeof(*fs),sizeof(*pair),sizeof(*plan),error?error_size:0};
 if(!vs||!fs||!pair||!plan||!vd||!fd||!vn||!fn||
    !valid_span(vd,vn)||!valid_span(fd,fn)||!valid_span(vs,sizeof(*vs))||
    !valid_span(fs,sizeof(*fs))||!valid_span(pair,sizeof(*pair))||
    !valid_span(plan,sizeof(*plan))||(error_size&&(!error||!valid_span(error,error_size))))return 1;
 for(unsigned i=0;i<5;i++) {
  if(len[i]&&!valid_span(out[i],len[i]))return 1;
  for(unsigned j=i+1;j<5;j++)if(overlaps(out[i],len[i],out[j],len[j]))return 1;
  if(overlaps(out[i],len[i],vd,vn)||overlaps(out[i],len[i],fd,fn))return 1;
 }
 if(error&&error_size)error[0]=0;
 if(pair->version||plan->version||!pair_outputs_empty(vs,fs)) {
  fail(error,error_size,"Output must be empty");return 1;
 }
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 char digest[65];
 if(tegu_compiler_trace_sha256(vd,vn,digest)||
    strcmp(digest,"ab0458e8ba4a7d2cdcca8f317a361411570528e9c0bc2e49fc3d730ebcf3c8de")||
    tegu_compiler_trace_sha256(fd,fn,digest)||
    strcmp(digest,"411d6dba34a9977c8205ddbab35a83ce4944d4e7e88edf7b4ecfd3144167c206")) {
  fail(error,error_size,"SPIR-V inputs differ from exact single_pass_blur_3 class");return 5;
 }
 struct tegu_mali_resource_plan tmp={0};
 int status=compile_pair(vd,vn,fd,fn,vs,fs,pair,true,NULL,&tmp,NULL,NULL,NULL,4,16384u,true,error,error_size);
 if(status)return status;
 static const uint32_t expected_ubo[4]={80,32,112,32};
 bool mismatch=vs->size!=1152||vs->registers!=32||vs->tls||vs->fau_words!=34||
  vs->sysval_mask!=3||vs->push_bytes!=8||vs->preload!=0x3800000000000000ULL||
  fs->size!=8704||fs->registers!=64||fs->tls||fs->fau_words!=51||
  fs->sysval_mask!=8||fs->push_bytes!=76||fs->preload!=0x3000000000000000ULL||
  pair->flags!=TEGU_MALI_PAIR2_WRITES_POINT_SIZE||pair->position_offset!=512||
  pair->position_size!=512||pair->point_position_size!=512||pair->secondary_offset!=1024||
  pair->secondary_size!=128||pair->secondary_registers!=32||pair->generic_size_B!=8||
  pair->vs_attribute_mask!=3||pair->vs_varying_mask!=1||pair->fs_varying_mask!=1||
  pair->varying_count!=1||tmp.sample_count!=4;
 for(unsigned i=0;!mismatch&&i<4;i++)if(tmp.ubos[i].declared_bytes!=expected_ubo[i])mismatch=true;
 if(!mismatch&&(tegu_compiler_trace_sha256(vs->code,vs->size,digest)||
    strcmp(digest,"40e8dca1a260e5527cb8979161b6e15493d85d0a1b993da207e87c89859a7e52")||
    tegu_compiler_trace_sha256(fs->code,fs->size,digest)||
    strcmp(digest,"7c100704ba60f35a69f469d71e2c59b00b2d7ec7da0488869b4caa0ced8c0fd8")))mismatch=true;
 if(mismatch) {
  tegu_mali_graphics_free(vs);tegu_mali_graphics_free(fs);memset(pair,0,sizeof(*pair));
  fail(error,error_size,"Compiler output differs from exact single_pass_blur_3 class");return 5;
 }
 struct tegu_mali_single_pass_blur3_plan finished={0};
 finished.version=TEGU_MALI_SINGLE_PASS_BLUR3_PLAN_VERSION;
 finished.table=1;finished.texture_index=4;finished.sampler_index=5;
 finished.normalized_coordinates=1;finished.sample_count=4;finished.implicit_lod=1;
 memcpy(finished.ubos,tmp.ubos,sizeof(finished.ubos));*plan=finished;
 return 0;
}

struct graphics_specialization {
 struct tegu_mali_specialization constants[64];
 struct nir_spirv_specialization_entry entries[64];
 struct nir_spirv_specialization info;
 bool changes_defaults;
};
/* SPIR-V input may be unaligned. Do not let a caller's scalar size reach
 * Mesa's memcpy-based specialization reader before checking its declaration.
 * This validates the requested specialization contract, not all SPIR-V IR. */
static uint32_t graphics_spirv_word(const void *data,size_t word) {
 const uint8_t *p=(const uint8_t *)data+word*4;
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static const char *graphics_specialization_check(const void *data,size_t length,
 const struct tegu_mali_specialization *constants,size_t count,
 struct graphics_specialization *out,uint32_t *bad_id) {
 memset(out,0,sizeof(*out));
 if(!count)return NULL;
 memcpy(out->constants,constants,count*sizeof(*constants));
 if(length<20||length%4||length>1024*1024||graphics_spirv_word(data,0)!=SpvMagicNumber)
  return "Invalid SPIR-V module size or magic";
 const size_t words=length/4;
 const uint32_t bound=graphics_spirv_word(data,3);
 for(size_t p=5;p<words;) {
  uint32_t n=graphics_spirv_word(data,p)>>16;
  if(!n||n>words-p)return "Malformed SPIR-V instruction length";
  p+=n;
 }
 for(size_t k=0;k<count;k++) {
  const struct tegu_mali_specialization *c=&out->constants[k];*bad_id=c->id;
  if(c->size!=1&&c->size!=2&&c->size!=4&&c->size!=8)
   return "Invalid specialization scalar size";
  for(size_t j=0;j<k;j++)if(c->id==out->constants[j].id)
   return "Duplicate specialization ID";
  uint32_t target=0,type=0,constant_op=0,constant_words=0;size_t constant_at=0;
  for(size_t p=5;p<words;p+=graphics_spirv_word(data,p)>>16) {
   uint32_t op=graphics_spirv_word(data,p)&0xffffu,n=graphics_spirv_word(data,p)>>16;
   if(op!=SpvOpDecorate||n<3||graphics_spirv_word(data,p+2)!=SpvDecorationSpecId)continue;
   if(n!=4)return "Malformed SpecId decoration";
   if(graphics_spirv_word(data,p+3)!=c->id)continue;
   if(target)return "Duplicate SpecId declaration";
   target=graphics_spirv_word(data,p+1);
   if(!target||target>=bound)return "Invalid specialization target ID";
  }
  if(!target)return "Unknown specialization ID in this stage";
  for(size_t p=5;p<words;p+=graphics_spirv_word(data,p)>>16) {
   uint32_t op=graphics_spirv_word(data,p)&0xffffu,n=graphics_spirv_word(data,p)>>16;
   if(op==SpvOpDecorate&&n==4&&graphics_spirv_word(data,p+1)==target&&
      graphics_spirv_word(data,p+2)==SpvDecorationSpecId&&graphics_spirv_word(data,p+3)!=c->id)
    return "Multiple SpecIds on one specialization constant";
   if(op!=SpvOpSpecConstant&&op!=SpvOpSpecConstantTrue&&op!=SpvOpSpecConstantFalse)continue;
   if(n<3)return "Malformed scalar specialization constant";
   if(graphics_spirv_word(data,p+2)!=target)continue;
   if(type)return "Duplicate specialization result ID";
   type=graphics_spirv_word(data,p+1);constant_op=op;constant_words=n;constant_at=p;
   if(!type||type>=bound)return "Invalid specialization type ID";
  }
  if(!type)return "SpecId must name a scalar OpSpecConstant";
  uint32_t scalar_size=0;bool seen_type=false;
  for(size_t p=5;p<words;p+=graphics_spirv_word(data,p)>>16) {
   uint32_t op=graphics_spirv_word(data,p)&0xffffu,n=graphics_spirv_word(data,p)>>16;
   if(op!=SpvOpTypeBool&&op!=SpvOpTypeInt&&op!=SpvOpTypeFloat)continue;
   if(n<2)return "Malformed scalar type";
   if(graphics_spirv_word(data,p+1)!=type)continue;
   if(seen_type)return "Duplicate specialization type declaration";
   seen_type=true;
   if(op==SpvOpTypeBool) {
    if(n!=2||constant_words!=3||constant_op==SpvOpSpecConstant)
     return "Boolean specialization declaration is malformed";
    scalar_size=4; /* Vulkan specializes booleans with VkBool32. */
   } else {
    if(n!=(op==SpvOpTypeInt?4u:3u)||constant_op!=SpvOpSpecConstant)
     return "Numeric specialization declaration is malformed";
    uint32_t bits=graphics_spirv_word(data,p+2);
    if((bits!=8&&bits!=16&&bits!=32&&bits!=64)||(op==SpvOpTypeFloat&&bits==8)||
       (op==SpvOpTypeInt&&graphics_spirv_word(data,p+3)>1))
     return "Unsupported specialization scalar type";
    if(constant_words!=3+(bits+31)/32)return "Specialization literal width differs from its type";
    scalar_size=bits/8;
   }
  }
  if(!seen_type)return "Specialization type is not a supported scalar";
  if(c->size!=scalar_size)return "Specialization byte size differs from its declared type";
  if(constant_op==SpvOpSpecConstant) {
   const uint8_t *literal=(const uint8_t *)data+(constant_at+3)*4;
   out->changes_defaults|=memcmp(c->data,literal,c->size)!=0;
  } else out->changes_defaults|=(graphics_spirv_word(c->data,0)!=0)!=(constant_op==SpvOpSpecConstantTrue);
  out->entries[k]=(struct nir_spirv_specialization_entry){.id=c->id,.size=c->size,.data=out->constants[k].data};
 }
 out->info=(struct nir_spirv_specialization){.num_entries=count,.entries=out->entries};
 return NULL;
}
static int compile_generic_pair(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,unsigned select,char *error,size_t error_size,
 const struct tegu_mali_specialization *vc,size_t vc_count,
 const struct tegu_mali_specialization *fc,size_t fc_count,struct tegu_mali_varying_info *ext,
 const uint32_t *mrt_formats,unsigned mrt_count) {
 const void *out[6]={vs,fs,pair,plan,error,ext};
 const size_t len[6]={sizeof(*vs),sizeof(*fs),sizeof(*pair),sizeof(*plan),error?error_size:0,ext?sizeof(*ext):0};
 if(!vd||!fd||!vn||!fn||!valid_span(vd,vn)||!valid_span(fd,fn)||!vs||!fs||!pair||!plan||
    !valid_span(vs,sizeof(*vs))||!valid_span(fs,sizeof(*fs))||!valid_span(pair,sizeof(*pair))||
    !valid_span(plan,sizeof(*plan))||(error_size&&(!error||!valid_span(error,error_size)))||
    vc_count>64||fc_count>64||(vc_count&&!valid_span(vc,vc_count*sizeof(*vc)))||
    (fc_count&&!valid_span(fc,fc_count*sizeof(*fc))))return 1;
 for(unsigned i=0;i<6;i++) {
  if(len[i]&&!valid_span(out[i],len[i]))return 1;
  for(unsigned j=i+1;j<6;j++)if(overlaps(out[i],len[i],out[j],len[j]))return 1;
  if(overlaps(out[i],len[i],vd,vn)||overlaps(out[i],len[i],fd,fn))return 1;
  if(overlaps(out[i],len[i],vc,vc_count*sizeof(*vc))||overlaps(out[i],len[i],fc,fc_count*sizeof(*fc)))return 1;
 }
 if(error&&error_size)error[0]=0;
 if(pair->version||plan->version||(ext&&ext->version)||!pair_outputs_empty(vs,fs)){fail(error,error_size,"Output must be empty");return 1;}
 memset(vs,0,sizeof(*vs));memset(fs,0,sizeof(*fs));memset(pair,0,sizeof(*pair));
 struct graphics_specialization v_spec,f_spec;uint32_t bad_id=0;
 const char *why=graphics_specialization_check(vd,vn,vc,vc_count,&v_spec,&bad_id);
 const char *stage="Vertex";
 if(!why){stage="Fragment";why=graphics_specialization_check(fd,fn,fc,fc_count,&f_spec,&bad_id);}
 if(why) {
  if(error&&error_size)snprintf(error,error_size,"%s specialization ID %u: %s",stage,bad_id,why);
  return 2;
 }
 struct tegu_mali_generic_plan tmp;memset(&tmp,0,sizeof(tmp));tmp.reserved=select;
 int status=compile_pair_specialized(vd,vn,fd,fn,vs,fs,pair,true,NULL,NULL,NULL,NULL,&tmp,0,
  TEGU_MALI_FIXED_UI_CODE_LIMIT,false,error,error_size,vc_count?&v_spec.info:NULL,fc_count?&f_spec.info:NULL,
  v_spec.changes_defaults,f_spec.changes_defaults,ext,mrt_formats,mrt_count);
 if(status)return status;
 if((select&TEGU_GENERIC_COMPILE_TILE)&&tmp.reads_framebuffer!=TEGU_MALI_GENERIC_FETCH_TILE) {
  tegu_mali_graphics_free(vs);tegu_mali_graphics_free(fs);memset(pair,0,sizeof(*pair));
  if(ext)memset(ext,0,sizeof(*ext));
  fail(error,error_size,"Tile fetch variant requires an actual attachment read");return 5;
 }
 tmp.reserved=mrt_formats?tmp.reserved:(select&0xffu);
 tmp.version=TEGU_MALI_GENERIC_PLAN_VERSION;*plan=tmp;return 0;
}
int tegu_mali_compile_generic_pair(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,char *error,size_t error_size) {
 return compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,0,error,error_size,NULL,0,NULL,0,NULL,NULL,0);
}
int tegu_mali_compile_generic_pair_rt(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,unsigned rt,char *error,size_t error_size) {
 if(rt>7){if(error&&error_size)fail(error,error_size,"Colour output outside 0..7");return 1;}
 return compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,rt+1,error,error_size,NULL,0,NULL,0,NULL,NULL,0);
}

int tegu_mali_compile_generic_pair_tile_rt(const void *vd,size_t vn,const void *fd,size_t fn,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,unsigned rt,char *error,size_t error_size) {
 if(rt>7){if(error&&error_size)fail(error,error_size,"Colour output outside 0..7");return 1;}
 return compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,(rt+1)|TEGU_GENERIC_COMPILE_TILE,error,error_size,NULL,0,NULL,0,NULL,NULL,0);
}

int tegu_mali_compile_generic_pair_specialized(const void *vd,size_t vn,const void *fd,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vc_count,const struct tegu_mali_specialization *fc,size_t fc_count,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,char *error,size_t error_size) {
 return compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,0,error,error_size,vc,vc_count,fc,fc_count,NULL,NULL,0);
}
int tegu_mali_compile_generic_pair_specialized_rt(const void *vd,size_t vn,const void *fd,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vc_count,const struct tegu_mali_specialization *fc,size_t fc_count,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,unsigned rt,char *error,size_t error_size) {
 if(rt>7){if(error&&error_size)fail(error,error_size,"Colour output outside 0..7");return 1;}
 return compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,rt+1,error,error_size,vc,vc_count,fc,fc_count,NULL,NULL,0);
}
int tegu_mali_compile_generic_pair_specialized_tile_rt(const void *vd,size_t vn,const void *fd,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vc_count,const struct tegu_mali_specialization *fc,size_t fc_count,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,unsigned rt,char *error,size_t error_size) {
 if(rt>7){if(error&&error_size)fail(error,error_size,"Colour output outside 0..7");return 1;}
 return compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,(rt+1)|TEGU_GENERIC_COMPILE_TILE,error,error_size,vc,vc_count,fc,fc_count,NULL,NULL,0);
}
int tegu_mali_compile_generic_pair_options(const void *vd,size_t vn,const void *fd,size_t fn,
 const struct tegu_mali_specialization *vc,size_t vc_count,const struct tegu_mali_specialization *fc,size_t fc_count,
 const struct tegu_mali_generic_options *options,
 struct tegu_mali_graphics *vs,struct tegu_mali_graphics *fs,struct tegu_mali_graphics_pair2 *pair,
 struct tegu_mali_generic_plan *plan,struct tegu_mali_varying_info *varyings,char *error,size_t error_size) {
 const uint32_t known=TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS|TEGU_MALI_GENERIC_OPTION_SELECT_RT|
  TEGU_MALI_GENERIC_OPTION_TILE_FETCH|TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS|TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE|
  TEGU_MALI_GENERIC_OPTION_MULTI_TARGET;
 /* Version2 carries the attachment format list for MULTI_TARGET. */
 size_t osize=sizeof(*options);
 if(options&&valid_span(options,sizeof(*options))&&options->version==TEGU_MALI_GENERIC_OPTIONS2_VERSION)
  osize=sizeof(struct tegu_mali_generic_options2);
 if(!options||!valid_span(options,osize)||
    overlaps(options,osize,vs,vs?sizeof(*vs):0)||overlaps(options,osize,fs,fs?sizeof(*fs):0)||
    overlaps(options,osize,pair,pair?sizeof(*pair):0)||overlaps(options,osize,plan,plan?sizeof(*plan):0)||
    overlaps(options,osize,varyings,varyings?sizeof(*varyings):0)||
    (error_size&&error&&overlaps(options,osize,error,error_size)))return 1;
 if(!options||!valid_span(options,sizeof(*options))||
    overlaps(options,sizeof(*options),vs,vs?sizeof(*vs):0)||overlaps(options,sizeof(*options),fs,fs?sizeof(*fs):0)||
    overlaps(options,sizeof(*options),pair,pair?sizeof(*pair):0)||overlaps(options,sizeof(*options),plan,plan?sizeof(*plan):0)||
    overlaps(options,sizeof(*options),varyings,varyings?sizeof(*varyings):0)||
    (error_size&&error&&overlaps(options,sizeof(*options),error,error_size)))return 1;
 struct tegu_mali_generic_options o=*options;
 bool select_rt=(o.flags&TEGU_MALI_GENERIC_OPTION_SELECT_RT)!=0;
 bool mrt=(o.flags&TEGU_MALI_GENERIC_OPTION_MULTI_TARGET)!=0;
 struct tegu_mali_generic_options2 o2;memset(&o2,0,sizeof(o2));
 if(o.version==TEGU_MALI_GENERIC_OPTIONS2_VERSION) {
  memcpy(&o2,options,sizeof(o2));
  if(!mrt||select_rt||(o.flags&TEGU_MALI_GENERIC_OPTION_TILE_FETCH)||!o2.target_count||
     o2.target_count>TEGU_MALI_GENERIC_MAX_TARGETS||o2.reserved2[0]||o2.reserved2[1]||o2.reserved2[2])return 1;
  for(unsigned i=0;i<TEGU_MALI_GENERIC_MAX_TARGETS;i++)
   if(i<o2.target_count?(o2.target_formats[i]&&!mrt_format_ok(o2.target_formats[i])):o2.target_formats[i]!=0)return 1;
  o.version=TEGU_MALI_GENERIC_OPTIONS_VERSION;
 } else if(mrt)return 1;
 if(o.version!=TEGU_MALI_GENERIC_OPTIONS_VERSION||(o.flags&~known)||o.reserved||(select_rt?o.rt>7:o.rt!=0)||
    ((o.flags&TEGU_MALI_GENERIC_OPTION_TILE_FETCH)&&!select_rt)||
    (((o.flags&TEGU_MALI_GENERIC_OPTION_EXTENDED_VARYINGS)!=0)!=(varyings!=NULL)))return 1; /* nothing written */
 unsigned select=(select_rt?o.rt+1:0)|
  ((o.flags&TEGU_MALI_GENERIC_OPTION_TILE_FETCH)?TEGU_GENERIC_COMPILE_TILE:0)|
  ((o.flags&TEGU_MALI_GENERIC_OPTION_DRAW_PARAMETERS)?TEGU_GENERIC_COMPILE_DRAW_PARAMS:0)|
  ((o.flags&TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE)?TEGU_GENERIC_COMPILE_TLS:0);
 return compile_generic_pair(vd,vn,fd,fn,vs,fs,pair,plan,select,error,error_size,vc,vc_count,fc,fc_count,varyings,
  mrt?o2.target_formats:NULL,mrt?o2.target_count:0);
}
