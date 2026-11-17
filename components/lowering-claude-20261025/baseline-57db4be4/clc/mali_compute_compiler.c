#include "nord2_target.h"
/* Callable compiler bridge for Vulkan compute SPIR-V -> Mali arch9.
 * Initial ABI: set0 bindings0..7 are bounded buffers, FAU bytes0..127 hold
 * {address_lo,address_hi,size,offset=0}. Application push constants start128;
 * num_workgroups starts256. No GPU addresses are baked into machine code.
 * Experimental trusted-input tool: selected unsupported layouts are refused;
 * other unsupported NIR may still hit backend assertions. Not a Metal device.
 */
#include "compiler/glsl_types.h"
#include "compiler/spirv/nir_spirv.h"
#include "compiler/spirv/spirv.h"
#include "compiler/nir/nir_deref.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "panfrost/compiler/pan_compiler.h"
#include "util/os_file.h"
#include "pipeline_format.h"
#include "mali_compute_compiler.h"
#include <stdio.h>
#include <stdlib.h>

struct context {
   bool failed, v2;
   unsigned bindings, manifest_count;
   struct tegu_pipeline_manifest manifest[TEGU_PIPELINE2_MAX_MANIFEST];
   unsigned sampler_modes[TEGU_PIPELINE2_MAX_BINDING+1];
   char *error;
   size_t error_size;
};
static bool refuse(struct context *ctx, const char *why)
{
   if(ctx->error && ctx->error_size) snprintf(ctx->error,ctx->error_size,"%s",why);
   ctx->failed = true; return false;
}
enum compute_texture_phase { COMPUTE_TEX_AUTHORED, COMPUTE_TEX_RESOURCES, COMPUTE_TEX_BACKEND };
static const char *compute_texture_source_error(nir_tex_src_type type,enum compute_texture_phase phase) {
 switch(type) {
 case nir_tex_src_coord:case nir_tex_src_comparator:case nir_tex_src_offset:
 case nir_tex_src_bias:case nir_tex_src_lod:case nir_tex_src_min_lod:
 case nir_tex_src_ms_index:case nir_tex_src_ddx:case nir_tex_src_ddy:
 case nir_tex_src_texture_offset:case nir_tex_src_sampler_offset:
 case nir_tex_src_texture_handle:case nir_tex_src_sampler_handle:return NULL;
 case nir_tex_src_projector:
  return phase==COMPUTE_TEX_BACKEND?"Texture projector survived NIR lowering":NULL;
 case nir_tex_src_texture_deref:case nir_tex_src_sampler_deref:
  return phase==COMPUTE_TEX_AUTHORED?NULL:"Texture descriptor dereference survived resource lowering";
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
static const char *compute_texture_op_error(nir_texop op) {
 switch(op) {
 case nir_texop_tex:case nir_texop_txb:case nir_texop_txl:case nir_texop_txd:
 case nir_texop_txf:case nir_texop_txf_ms:case nir_texop_tg4:case nir_texop_lod:
 case nir_texop_txs:case nir_texop_query_levels:case nir_texop_texture_samples:return NULL;
 default:return "Texture operation is not implemented by the Valhall lowering";
 }
}
static const char *compute_texture_check(nir_shader *s,enum compute_texture_phase phase) {
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_tex)continue;
  nir_tex_instr *tex=nir_instr_as_tex(instr);
  const char *why=compute_texture_op_error(tex->op);if(why)return why;
  if(tex->is_sparse)return "Sparse texture residency is outside the compute descriptor ABI";
  if(tex->embedded_sampler)return "Embedded texture samplers are outside the compute descriptor ABI";
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
   if((why=compute_texture_source_error(src->src_type,phase)))return why;
   if((unsigned)src->src_type>=64||(seen&BITFIELD64_BIT(src->src_type)))return "Duplicate texture source";
   seen|=BITFIELD64_BIT(src->src_type);
  }
  /* The pinned Valhall pass prioritizes min_lod over ddx/ddy and would drop
   * explicit gradients. Do not silently compile that combination as implicit
   * derivatives. It needs an actual combined gradient/clamp implementation. */
  if(tex->op==nir_texop_txd&&nir_tex_instr_src_index(tex,nir_tex_src_min_lod)>=0)
   return "Explicit texture gradients with minimum LOD require a combined Valhall lowering";
  if(phase!=COMPUTE_TEX_BACKEND)continue;
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

struct compute_specialization {
 struct tegu_mali_specialization constants[64];
 struct nir_spirv_specialization_entry entries[64];
 struct nir_spirv_specialization info;
 bool changes_defaults;
};
/* SPIR-V input may be unaligned. Do not let a caller's scalar size reach
 * Mesa's memcpy-based specialization reader before checking its declaration.
 * This validates the requested specialization contract, not all SPIR-V IR. */
static uint32_t compute_spirv_word(const void *data,size_t word) {
 const uint8_t *p=(const uint8_t *)data+word*4;
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static const char *compute_specialization_check(const void *data,size_t length,
 const struct tegu_mali_specialization *constants,size_t count,
 struct compute_specialization *out,uint32_t *bad_id) {
 memset(out,0,sizeof(*out));
 if(!count)return NULL;
 memcpy(out->constants,constants,count*sizeof(*constants));
 if(length<20||length%4||length>1024*1024||compute_spirv_word(data,0)!=SpvMagicNumber)
  return "Invalid SPIR-V module size or magic";
 const size_t words=length/4;
 const uint32_t bound=compute_spirv_word(data,3);
 for(size_t p=5;p<words;) {
  uint32_t n=compute_spirv_word(data,p)>>16;
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
  for(size_t p=5;p<words;p+=compute_spirv_word(data,p)>>16) {
   uint32_t op=compute_spirv_word(data,p)&0xffffu,n=compute_spirv_word(data,p)>>16;
   if(op!=SpvOpDecorate||n<3||compute_spirv_word(data,p+2)!=SpvDecorationSpecId)continue;
   if(n!=4)return "Malformed SpecId decoration";
   if(compute_spirv_word(data,p+3)!=c->id)continue;
   if(target)return "Duplicate SpecId declaration";
   target=compute_spirv_word(data,p+1);
   if(!target||target>=bound)return "Invalid specialization target ID";
  }
  if(!target)return "Unknown specialization ID in this stage";
  for(size_t p=5;p<words;p+=compute_spirv_word(data,p)>>16) {
   uint32_t op=compute_spirv_word(data,p)&0xffffu,n=compute_spirv_word(data,p)>>16;
   if(op==SpvOpDecorate&&n==4&&compute_spirv_word(data,p+1)==target&&
      compute_spirv_word(data,p+2)==SpvDecorationSpecId&&compute_spirv_word(data,p+3)!=c->id)
    return "Multiple SpecIds on one specialization constant";
   if(op!=SpvOpSpecConstant&&op!=SpvOpSpecConstantTrue&&op!=SpvOpSpecConstantFalse)continue;
   if(n<3)return "Malformed scalar specialization constant";
   if(compute_spirv_word(data,p+2)!=target)continue;
   if(type)return "Duplicate specialization result ID";
   type=compute_spirv_word(data,p+1);constant_op=op;constant_words=n;constant_at=p;
   if(!type||type>=bound)return "Invalid specialization type ID";
  }
  if(!type)return "SpecId must name a scalar OpSpecConstant";
  uint32_t scalar_size=0;bool seen_type=false;
  for(size_t p=5;p<words;p+=compute_spirv_word(data,p)>>16) {
   uint32_t op=compute_spirv_word(data,p)&0xffffu,n=compute_spirv_word(data,p)>>16;
   if(op!=SpvOpTypeBool&&op!=SpvOpTypeInt&&op!=SpvOpTypeFloat)continue;
   if(n<2)return "Malformed scalar type";
   if(compute_spirv_word(data,p+1)!=type)continue;
   if(seen_type)return "Duplicate specialization type declaration";
   seen_type=true;
   if(op==SpvOpTypeBool) {
    if(n!=2||constant_words!=3||constant_op==SpvOpSpecConstant)
     return "Boolean specialization declaration is malformed";
    scalar_size=4; /* Vulkan specializes booleans with VkBool32. */
   } else {
    if(n!=(op==SpvOpTypeInt?4u:3u)||constant_op!=SpvOpSpecConstant)
     return "Numeric specialization declaration is malformed";
    uint32_t bits=compute_spirv_word(data,p+2);
    if((bits!=8&&bits!=16&&bits!=32&&bits!=64)||(op==SpvOpTypeFloat&&bits==8)||
       (op==SpvOpTypeInt&&compute_spirv_word(data,p+3)>1))
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
  } else out->changes_defaults|=(compute_spirv_word(c->data,0)!=0)!=(constant_op==SpvOpSpecConstantTrue);
  out->entries[k]=(struct nir_spirv_specialization_entry){.id=c->id,.size=c->size,.data=out->constants[k].data};
 }
 out->info=(struct nir_spirv_specialization){.num_entries=count,.entries=out->entries};
 return NULL;
}

/* TGP2 follows the pinned PanVK lowering: immediate arch9 resource handles
 * for textures and samplers, default image intrinsics with a table handle.
 * The manifest is collected from live instructions before their dereferences
 * disappear. It never invents bound-view facts or sampler state. */
static bool compute_record(struct context *ctx, struct tegu_pipeline_manifest m)
{
   for(unsigned j=0;j<ctx->manifest_count;j++) {
      struct tegu_pipeline_manifest *q=&ctx->manifest[j];
      if(q->binding!=m.binding)continue;
      if(q->kind==m.kind) {
         if(q->table!=m.table||q->entry!=m.entry||q->storage_format!=m.storage_format||
            ((q->shape^m.shape)&~TEGU_PIPELINE2_SHAPE_DEPTH_COMPARE)||
            ((q->flags^m.flags)&~TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE))
            return refuse(ctx,"TGP2 resource binding has incompatible declarations or uses");
         q->access|=m.access;q->shape|=m.shape;q->flags|=m.flags;return true;
      }
      bool pair=(q->kind==TEGU_PIPELINE2_KIND_TEXTURE&&m.kind==TEGU_PIPELINE2_KIND_SAMPLER)||
                (q->kind==TEGU_PIPELINE2_KIND_SAMPLER&&m.kind==TEGU_PIPELINE2_KIND_TEXTURE);
      if(!pair||!(q->flags&m.flags&TEGU_PIPELINE2_FLAG_DECLARED_COMBINED))
         return refuse(ctx,"TGP2 binding collision is not a declared combined image sampler");
   }
   if(ctx->manifest_count==TEGU_PIPELINE2_MAX_MANIFEST)
      return refuse(ctx,"TGP2 live resource manifest exceeds capacity");
   ctx->manifest[ctx->manifest_count++]=m;return true;
}
static bool compute_shape(struct context *ctx,const struct glsl_type *type,
                          bool shadow,uint32_t *shape)
{
   unsigned dim;
   switch(glsl_get_sampler_dim(type)) {
   case GLSL_SAMPLER_DIM_1D:dim=TEGU_PIPELINE2_DIM_1D;break;
   case GLSL_SAMPLER_DIM_2D:case GLSL_SAMPLER_DIM_MS:dim=TEGU_PIPELINE2_DIM_2D;break;
   case GLSL_SAMPLER_DIM_3D:dim=TEGU_PIPELINE2_DIM_3D;break;
   case GLSL_SAMPLER_DIM_CUBE:dim=TEGU_PIPELINE2_DIM_CUBE;break;
   case GLSL_SAMPLER_DIM_BUF:dim=TEGU_PIPELINE2_DIM_BUFFER;break;
   default:return refuse(ctx,"TGP2 image dimension is outside 1D/2D/3D/cube/buffer");
   }
   unsigned numeric;
   switch(glsl_get_sampler_result_type(type)) {
   case GLSL_TYPE_FLOAT:numeric=TEGU_PIPELINE2_NUMERIC_FLOAT;break;
   case GLSL_TYPE_INT:numeric=TEGU_PIPELINE2_NUMERIC_SINT;break;
   case GLSL_TYPE_UINT:numeric=TEGU_PIPELINE2_NUMERIC_UINT;break;
   default:return refuse(ctx,"TGP2 image sampled scalar type is unsupported");
   }
   *shape=dim|TEGU_PIPELINE2_SHAPE_NUMERIC(numeric)|
      (glsl_sampler_type_is_array(type)?TEGU_PIPELINE2_SHAPE_ARRAYED:0)|
      (glsl_get_sampler_dim(type)==GLSL_SAMPLER_DIM_MS?TEGU_PIPELINE2_SHAPE_MULTISAMPLE:0)|
      (shadow?TEGU_PIPELINE2_SHAPE_DEPTH_COMPARE:0);
   return true;
}
/* Canonical VkFormat values, independently of runtime Metal view formats.
 * Source: pinned spirv_to_nir.c translate_image_format, ImageFormat 0..41. */
static uint32_t compute_storage_format(enum pipe_format format)
{
   switch(format) {
   case PIPE_FORMAT_NONE:return 0;
   case PIPE_FORMAT_R32G32B32A32_FLOAT:return 109;
   case PIPE_FORMAT_R16G16B16A16_FLOAT:return 97;
   case PIPE_FORMAT_R32_FLOAT:return 100;
   case PIPE_FORMAT_R8G8B8A8_UNORM:return 37;
   case PIPE_FORMAT_R8G8B8A8_SNORM:return 38;
   case PIPE_FORMAT_R32G32_FLOAT:return 103;
   case PIPE_FORMAT_R16G16_FLOAT:return 83;
   case PIPE_FORMAT_R11G11B10_FLOAT:return 122;
   case PIPE_FORMAT_R16_FLOAT:return 76;
   case PIPE_FORMAT_R16G16B16A16_UNORM:return 91;
   case PIPE_FORMAT_R10G10B10A2_UNORM:return 64;
   case PIPE_FORMAT_R16G16_UNORM:return 77;
   case PIPE_FORMAT_R8G8_UNORM:return 16;
   case PIPE_FORMAT_R16_UNORM:return 70;
   case PIPE_FORMAT_R8_UNORM:return 9;
   case PIPE_FORMAT_R16G16B16A16_SNORM:return 92;
   case PIPE_FORMAT_R16G16_SNORM:return 78;
   case PIPE_FORMAT_R8G8_SNORM:return 17;
   case PIPE_FORMAT_R16_SNORM:return 71;
   case PIPE_FORMAT_R8_SNORM:return 10;
   case PIPE_FORMAT_R32G32B32A32_SINT:return 108;
   case PIPE_FORMAT_R16G16B16A16_SINT:return 96;
   case PIPE_FORMAT_R8G8B8A8_SINT:return 42;
   case PIPE_FORMAT_R32_SINT:return 99;
   case PIPE_FORMAT_R32G32_SINT:return 102;
   case PIPE_FORMAT_R16G16_SINT:return 82;
   case PIPE_FORMAT_R8G8_SINT:return 21;
   case PIPE_FORMAT_R16_SINT:return 75;
   case PIPE_FORMAT_R8_SINT:return 14;
   case PIPE_FORMAT_R32G32B32A32_UINT:return 107;
   case PIPE_FORMAT_R16G16B16A16_UINT:return 95;
   case PIPE_FORMAT_R8G8B8A8_UINT:return 41;
   case PIPE_FORMAT_R32_UINT:return 98;
   case PIPE_FORMAT_R10G10B10A2_UINT:return 68;
   case PIPE_FORMAT_R32G32_UINT:return 101;
   case PIPE_FORMAT_R16G16_UINT:return 81;
   case PIPE_FORMAT_R8G8_UINT:return 20;
   case PIPE_FORMAT_R16_UINT:return 74;
   case PIPE_FORMAT_R8_UINT:return 13;
   case PIPE_FORMAT_R64_UINT:return 110;
   case PIPE_FORMAT_R64_SINT:return 111;
   default:return UINT32_MAX;
   }
}
static nir_variable *compute_resource_var(struct context *ctx,nir_src src)
{
   if(!src.ssa||nir_def_instr(src.ssa)->type!=nir_instr_type_deref) {
      refuse(ctx,"TGP2 resource must use a direct descriptor variable");return NULL;
   }
   nir_deref_instr *d=nir_src_as_deref(src);
   nir_variable *var=d->deref_type==nir_deref_type_var?d->var:NULL;
   if(!var||glsl_type_is_array(var->type)) {
      refuse(ctx,"TGP2 descriptor arrays or indirect resource pointers are unsupported");return NULL;
   }
   if(var->data.descriptor_set||var->data.binding>TEGU_PIPELINE2_MAX_BINDING||var->data.bindless) {
      refuse(ctx,"TGP2 resources require set0 bindings0..255 without bindless pointers");return NULL;
   }
   return var;
}
static bool compute_opaque_record(struct context *ctx,nir_variable *var,unsigned kind,
                                 unsigned access,bool shadow)
{
   struct tegu_pipeline_manifest m={.binding=var->data.binding,.kind=kind,.array_count=1,
      .access=access,.entry=var->data.binding};
   bool combined=glsl_type_is_sampler(var->type)&&glsl_get_sampler_result_type(var->type)!=GLSL_TYPE_VOID;
   if(kind==TEGU_PIPELINE2_KIND_SAMPLER) {
      if(!glsl_type_is_sampler(var->type))return refuse(ctx,"TGP2 sampler source is not a sampler declaration");
      m.table=TEGU_PIPELINE2_TABLE_SAMPLER;
      if(shadow)m.flags|=TEGU_PIPELINE2_FLAG_SAMPLER_COMPARE;
   } else {
      if(kind==TEGU_PIPELINE2_KIND_TEXTURE&&!(glsl_type_is_texture(var->type)||combined))
         return refuse(ctx,"TGP2 texture source is not a sampled-image declaration");
      if(kind==TEGU_PIPELINE2_KIND_IMAGE&&!glsl_type_is_image(var->type))
         return refuse(ctx,"TGP2 image source is not a storage-image declaration");
      m.table=TEGU_PIPELINE2_TABLE_TEXTURE;
      if(!compute_shape(ctx,var->type,shadow,&m.shape))return false;
      if(kind==TEGU_PIPELINE2_KIND_IMAGE) {
         m.storage_format=compute_storage_format(var->data.image.format);
         if(m.storage_format==UINT32_MAX)return refuse(ctx,"TGP2 storage image declared format has no canonical Vk mapping");
      }
   }
   if(combined)m.flags|=TEGU_PIPELINE2_FLAG_DECLARED_COMBINED;
   if(!tegu_pipeline2_shape_check(m.kind,m.shape,m.storage_format))
      return refuse(ctx,"TGP2 image shape or declared format is incompatible with its scalar type");
   if(!compute_record(ctx,m))return false;
   /* The manifest's combined descriptor identity includes both components,
    * even if this instruction only queries/fetches the image component. */
   if(combined&&kind==TEGU_PIPELINE2_KIND_TEXTURE) {
      struct tegu_pipeline_manifest sampler={.binding=m.binding,.kind=TEGU_PIPELINE2_KIND_SAMPLER,
         .array_count=1,.table=TEGU_PIPELINE2_TABLE_SAMPLER,.entry=m.binding,
         .flags=TEGU_PIPELINE2_FLAG_DECLARED_COMBINED};
      return compute_record(ctx,sampler);
   }
   return true;
}
static bool compute_dummy_sampler(struct context *ctx)
{
   return compute_record(ctx,(struct tegu_pipeline_manifest){
      .binding=TEGU_PIPELINE2_DUMMY_BINDING,.kind=TEGU_PIPELINE2_KIND_SAMPLER,.array_count=1,
      .table=TEGU_PIPELINE2_TABLE_SAMPLER,.entry=TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY,
      .flags=TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER});
}
static bool compute_lower_texture(nir_tex_instr *tex,struct context *ctx)
{
   nir_builder b=nir_builder_at(nir_before_instr(&tex->instr));
   bool texture=false,sampler=false;
   if(tex->texture_index||tex->sampler_index)
      return refuse(ctx,"TGP2 authored texture contains an unexplained descriptor index");
   for(int n=tex->num_srcs-1;n>=0;n--) {
      nir_tex_src_type type=tex->src[n].src_type;
      if(type==nir_tex_src_texture_handle||type==nir_tex_src_sampler_handle||
         type==nir_tex_src_texture_offset||type==nir_tex_src_sampler_offset)
         return refuse(ctx,"TGP2 authored texture handle bypasses resource reflection");
      if(type!=nir_tex_src_texture_deref&&type!=nir_tex_src_sampler_deref)continue;
      nir_variable *var=compute_resource_var(ctx,tex->src[n].src);if(!var)return false;
      bool is_sampler=type==nir_tex_src_sampler_deref;
      unsigned kind=is_sampler?TEGU_PIPELINE2_KIND_SAMPLER:TEGU_PIPELINE2_KIND_TEXTURE;
      unsigned access=is_sampler?0:TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_SAMPLE;
      if(!compute_opaque_record(ctx,var,kind,access,tex->is_shadow))return false;
      if(is_sampler) {
         unsigned mode=tex->is_shadow?2:1;
         ctx->sampler_modes[var->data.binding]|=mode;
         if(ctx->sampler_modes[var->data.binding]==3)
            return refuse(ctx,"TGP2 sampler binding is used both with and without depth comparison");
      }
      unsigned table=is_sampler?TEGU_PIPELINE2_TABLE_SAMPLER:TEGU_PIPELINE2_TABLE_TEXTURE;
      nir_tex_instr_remove_src(tex,n);
      nir_tex_instr_add_src(tex,is_sampler?nir_tex_src_sampler_handle:nir_tex_src_texture_handle,
                           nir_imm_int(&b,pan_res_handle(table,var->data.binding)));
      if(is_sampler)sampler=true;else texture=true;
   }
   if(!texture)return refuse(ctx,"TGP2 texture instruction has no reflected image source");
   if(!sampler) {
      if(nir_tex_instr_need_sampler(tex))return refuse(ctx,"TGP2 sampling instruction has no sampler source");
      if(!compute_dummy_sampler(ctx))return false;
      nir_tex_instr_add_src(tex,nir_tex_src_sampler_handle,
         nir_imm_int(&b,pan_res_handle(TEGU_PIPELINE2_TABLE_SAMPLER,TEGU_PIPELINE2_DUMMY_SAMPLER_ENTRY)));
   }
   return true;
}
/* Buffer pointers are deref chains rooted at load_vulkan_descriptor. Do not
 * infer permissions from NonWritable alone: actual loads/stores/atomics must
 * identify their native binding. Ambiguous pointer provenance is refused. */
static nir_intrinsic_instr *compute_buffer_root(nir_def *def,unsigned depth)
{
   if(!def||depth>64)return NULL;
   nir_instr *instr=nir_def_instr(def);
   if(instr->type==nir_instr_type_deref) {
      nir_deref_instr *d=nir_instr_as_deref(instr);
      if(d->deref_type==nir_deref_type_var)return NULL;
      return compute_buffer_root(d->parent.ssa,depth+1);
   }
   if(instr->type!=nir_instr_type_intrinsic)return NULL;
   nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
   if(i->intrinsic==nir_intrinsic_vulkan_resource_index)return i;
   if(i->intrinsic==nir_intrinsic_load_vulkan_descriptor)return compute_buffer_root(i->src[0].ssa,depth+1);
   return NULL;
}
static bool compute_buffer_use(struct context *ctx,nir_src src,unsigned access)
{
   nir_intrinsic_instr *root=compute_buffer_root(src.ssa,0);
   if(!root)return refuse(ctx,"TGP2 buffer pointer has unsupported or ambiguous descriptor provenance");
   unsigned binding=nir_intrinsic_binding(root);
   if(nir_intrinsic_desc_set(root)||binding>=TEGU_PIPELINE2_MAX_BUFFERS||
      !nir_src_is_const(root->src[0])||nir_src_as_uint(root->src[0]))
      return refuse(ctx,"TGP2 buffers require set0 single descriptors0..7");
   unsigned kind;
   switch(nir_intrinsic_desc_type(root)) {
   case nir_descriptor_type_uniform_buffer:kind=TEGU_PIPELINE2_KIND_UBO;break;
   case nir_descriptor_type_storage_buffer:kind=TEGU_PIPELINE2_KIND_SSBO;break;
   default:return refuse(ctx,"TGP2 buffer pointer has a non-buffer descriptor type");
   }
   if(kind==TEGU_PIPELINE2_KIND_UBO&&access!=TEGU_PIPELINE2_ACCESS_READ)
      return refuse(ctx,"TGP2 uniform buffer has a writable use");
   ctx->bindings|=1u<<binding;
   return compute_record(ctx,(struct tegu_pipeline_manifest){.binding=binding,.kind=kind,.array_count=1,
      .access=access,.table=TEGU_PIPELINE2_TABLE_FAU,.entry=binding});
}
/* Native Valhall atomics are integer operations. Mesa's generic CAS loop
 * preserves float32 add/min/max semantics, including the old value result,
 * instead of routing a float opcode into bi_atom_opc_for_nir's assertion. */
static bool compute_atomic_supported(struct context *ctx,nir_intrinsic_instr *i)
{
   if(i->def.bit_size!=32&&i->def.bit_size!=64)
      return refuse(ctx,"TGP2 atomics require 32-bit or 64-bit scalars");
   switch(nir_intrinsic_atomic_op(i)) {
   case nir_atomic_op_iadd:case nir_atomic_op_imin:case nir_atomic_op_umin:
   case nir_atomic_op_imax:case nir_atomic_op_umax:case nir_atomic_op_iand:
   case nir_atomic_op_ior:case nir_atomic_op_ixor:case nir_atomic_op_xchg:
   case nir_atomic_op_cmpxchg:return true;
   case nir_atomic_op_fadd:case nir_atomic_op_fmin:case nir_atomic_op_fmax:
      if(i->def.bit_size==32)return true;
      return refuse(ctx,"TGP2 float64 atomic arithmetic requires a float64 arithmetic lowering");
   default:return refuse(ctx,"TGP2 atomic opcode is unsupported by Valhall and the CAS lowering");
   }
}
static bool compute_native_atomic(const nir_instr *instr,const void *unused)
{
   (void)unused;
   nir_atomic_op op=nir_intrinsic_atomic_op(nir_instr_as_intrinsic(instr));
   return op!=nir_atomic_op_fadd&&op!=nir_atomic_op_fmin&&op!=nir_atomic_op_fmax;
}
static bool compute_lower_image(nir_intrinsic_instr *i,struct context *ctx)
{
   unsigned access=TEGU_PIPELINE2_ACCESS_STORAGE;
   switch(i->intrinsic) {
   case nir_intrinsic_image_deref_load:case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:access|=TEGU_PIPELINE2_ACCESS_READ;break;
   case nir_intrinsic_image_deref_store:access|=TEGU_PIPELINE2_ACCESS_WRITE;break;
   case nir_intrinsic_image_deref_atomic:case nir_intrinsic_image_deref_atomic_swap:
      access|=TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC;break;
   default:return refuse(ctx,"TGP2 image operation is not implemented by the pinned PanVK/Valhall lowering");
   }
   nir_variable *var=compute_resource_var(ctx,i->src[0]);if(!var)return false;
   if(!compute_opaque_record(ctx,var,TEGU_PIPELINE2_KIND_IMAGE,access,false))return false;
   if(nir_intrinsic_image_dim(i)!=glsl_get_sampler_dim(var->type)||
      nir_intrinsic_image_array(i)!=glsl_sampler_type_is_array(var->type))
      return refuse(ctx,"TGP2 image operation shape differs from its declaration");
   if(i->intrinsic==nir_intrinsic_image_deref_store&&nir_src_bit_size(i->src[3])!=32)
      return refuse(ctx,"Valhall image stores require 32-bit scalar components");
   if(i->intrinsic==nir_intrinsic_image_deref_load&&i->def.bit_size!=32)
      return refuse(ctx,"TGP2 image loads require 32-bit scalar components");
   if(i->intrinsic==nir_intrinsic_image_deref_samples&&nir_intrinsic_image_dim(i)!=GLSL_SAMPLER_DIM_MS)
      return refuse(ctx,"TGP2 image sample-count query requires a multisample declaration");
   /* The backend ignores nonzero image LOD in its load/store emitter. */
   int lod_src=i->intrinsic==nir_intrinsic_image_deref_load?3:
               i->intrinsic==nir_intrinsic_image_deref_store?4:
               i->intrinsic==nir_intrinsic_image_deref_size?1:-1;
   if(lod_src>=0&&(!nir_src_is_const(i->src[lod_src])||nir_src_as_uint(i->src[lod_src])))
      return refuse(ctx,"TGP2 storage image nonzero or dynamic LOD requires a mip-aware image lowering");
   nir_builder b=nir_builder_at(nir_before_instr(&i->instr));
   nir_rewrite_image_intrinsic(i,nir_imm_int(&b,pan_res_handle(TEGU_PIPELINE2_TABLE_TEXTURE,var->data.binding)),
                               nir_image_intrinsic_type_default);
   return true;
}
static bool compute_lower_v2_resources(nir_shader *s,struct context *ctx)
{
   const char *why=compute_texture_check(s,COMPUTE_TEX_AUTHORED);
   if(why)return refuse(ctx,why);
   nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr_safe(instr,block) {
      if(instr->type==nir_instr_type_tex) {
         if(!compute_lower_texture(nir_instr_as_tex(instr),ctx))return false;
         continue;
      }
      if(instr->type!=nir_instr_type_intrinsic)continue;
      nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
      if(nir_intrinsic_has_atomic_op(i)&&!compute_atomic_supported(ctx,i))return false;
      if(nir_intrinsic_has_image_dim(i)) {
         if(!compute_lower_image(i,ctx))return false;
         continue;
      }
      unsigned access;
      switch(i->intrinsic) {
      case nir_intrinsic_load_deref:case nir_intrinsic_deref_buffer_array_length:
         access=TEGU_PIPELINE2_ACCESS_READ;break;
      case nir_intrinsic_store_deref:access=TEGU_PIPELINE2_ACCESS_WRITE;break;
      case nir_intrinsic_deref_atomic:case nir_intrinsic_deref_atomic_swap:
         access=TEGU_PIPELINE2_ACCESS_READ|TEGU_PIPELINE2_ACCESS_WRITE|TEGU_PIPELINE2_ACCESS_ATOMIC;break;
      case nir_intrinsic_vulkan_resource_reindex:
         return refuse(ctx,"TGP2 buffer descriptor arrays are unsupported");
      default:continue;
      }
      nir_deref_instr *d=nir_src_as_deref(i->src[0]);
      if(d&&(d->modes&(nir_var_mem_ubo|nir_var_mem_ssbo)))
         if(!compute_buffer_use(ctx,i->src[0],access))return false;
   }
   NIR_PASS(_,s,nir_opt_dce);
   NIR_PASS(_,s,nir_remove_dead_derefs);
   NIR_PASS(_,s,nir_remove_dead_variables,nir_var_uniform|nir_var_image,NULL);
   why=compute_texture_check(s,COMPUTE_TEX_RESOURCES);
   if(why)return refuse(ctx,why);
   return true;
}

static bool resources(nir_builder *b, nir_intrinsic_instr *i, void *data)
{
   struct context *ctx = data;
   b->cursor = nir_before_instr(&i->instr);
   nir_def *v = NULL;
   switch (i->intrinsic) {
   case nir_intrinsic_vulkan_resource_index: {
      unsigned binding = nir_intrinsic_binding(i);
      if (nir_intrinsic_desc_set(i) || binding >= 8 ||
          !nir_src_is_const(i->src[0]) || nir_src_as_uint(i->src[0]))
         return refuse(ctx, "only set0 single buffer descriptors0..7 supported");
      if (i->def.num_components != 4 || i->def.bit_size != 32)
         return refuse(ctx, "expected bounded-global buffer address");
      if(nir_intrinsic_desc_type(i)!=nir_descriptor_type_uniform_buffer&&
         nir_intrinsic_desc_type(i)!=nir_descriptor_type_storage_buffer)
         return refuse(ctx,"TGP1 resource descriptors must be uniform or storage buffers");
      if(ctx->v2) {
         bool reflected=false;
         for(unsigned n=0;n<ctx->manifest_count;n++)
            reflected|=ctx->manifest[n].binding==binding&&ctx->manifest[n].table==TEGU_PIPELINE2_TABLE_FAU;
         if(!reflected)return refuse(ctx,"TGP2 buffer descriptor use escaped access reflection");
      }
      ctx->bindings |= 1u << binding;
      v = nir_load_push_constant(b, 4, 32, nir_imm_int(b, binding * 16));
      break;
   }
   case nir_intrinsic_load_vulkan_descriptor:
      v = i->src[0].ssa; break;
   case nir_intrinsic_vulkan_resource_reindex:
      return refuse(ctx, "descriptor arrays not implemented");
   default: return false;
   }
   nir_def_replace(&i->def, v);
   return true;
}
/* TGP1 only supplies bounded buffer addresses. A texture/image dereference
 * is not such an address: leaving it for pan_postprocess_nir reaches
 * steal_tex_srcs or image descriptor assertions before our late pointer check.
 * Check live instructions after inlining, before any backend preprocessing.
 * Do not erase these operations or pretend that a buffer slot is a texture. */
static bool compute_resources_supported(nir_shader *s,struct context *ctx)
{
   nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
      if(instr->type==nir_instr_type_tex)
         return refuse(ctx,"compute texture operations require a texture/sampler descriptor ABI; TGP1 supports buffers only");
      if(instr->type==nir_instr_type_intrinsic) {
         nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
         if(nir_intrinsic_has_image_dim(i))
            return refuse(ctx,"compute image operations require an image descriptor ABI; TGP1 supports buffers only");
         if(nir_intrinsic_has_atomic_op(i)) {
            if(!compute_atomic_supported(ctx,i))return false;
            if(!compute_native_atomic(instr,NULL))
               return refuse(ctx,"TGP1 float atomic arithmetic requires the TGP2 CAS lowering");
         }
      }
   }
   return true;
}
/* Shift application push constants before generating descriptor push loads. */
static bool app_push(nir_builder *b, nir_intrinsic_instr *i, void *data)
{
   if (i->intrinsic != nir_intrinsic_load_push_constant) return false;
   b->cursor = nir_before_instr(&i->instr);
   unsigned base = nir_intrinsic_base(i);
   if (base + nir_intrinsic_range(i) > 128)
      return refuse(data, "application push constants exceed128bytes");
   nir_src_rewrite(&i->src[0], nir_iadd_imm(b, i->src[0].ssa, base + 128));
   nir_intrinsic_set_base(i, 0); nir_intrinsic_set_range(i, 0);
   return true;
}
/* The generic explicit-IO pass leaves bounded UBO loads as a dedicated
 * intrinsic which this Mali backend does not implement. Lower to guarded
 * global loads; subtraction avoids offset+size overflow in the bounds check. */
static bool constant_memory(nir_builder *b, nir_intrinsic_instr *i, void *data)
{
   (void)data;
   if (i->intrinsic != nir_intrinsic_load_global_constant_bounded) return false;
   b->cursor=nir_before_instr(&i->instr);
   unsigned bytes=i->def.num_components*i->def.bit_size/8;
   nir_def *offset=i->src[1].ssa, *size=i->src[2].ssa;
   nir_def *valid=nir_iand(b,nir_uge(b,size,nir_imm_int(b,bytes)),
                            nir_uge(b,nir_isub(b,size,nir_imm_int(b,bytes)),offset));
   nir_push_if(b,valid);
   nir_def *address=nir_iadd(b,i->src[0].ssa,nir_u2u64(b,offset));
   nir_def *loaded=nir_load_global(b,i->def.num_components,i->def.bit_size,address,
       .align_mul=nir_intrinsic_align_mul(i),.align_offset=nir_intrinsic_align_offset(i));
   nir_push_else(b,NULL);
   nir_def *zero=nir_imm_zero(b,i->def.num_components,i->def.bit_size);
   nir_pop_if(b,NULL);
   nir_def_replace(&i->def,nir_if_phi(b,loaded,zero));
   return true;
}
static bool sysvals(nir_builder *b, nir_intrinsic_instr *i, void *data)
{
   b->cursor = nir_before_instr(&i->instr);
   nir_def *v;
   switch (i->intrinsic) {
   case nir_intrinsic_load_base_workgroup_id:
      v = nir_imm_zero(b, i->def.num_components, i->def.bit_size); break;
   case nir_intrinsic_load_workgroup_size:
      if (b->shader->info.workgroup_size_variable)
         return refuse(data, "unspecialized workgroup size");
      v = nir_imm_ivec3(b, b->shader->info.workgroup_size[0],
                       b->shader->info.workgroup_size[1], b->shader->info.workgroup_size[2]);
      break;
   case nir_intrinsic_load_num_workgroups:
      v = nir_load_push_constant(b, 3, 32, nir_imm_int(b, 256)); break;
   case nir_intrinsic_load_ro_sink_address_poly:
      v = nir_imm_int64(b, PAN_SHADER_OOB_ADDRESS); break;
   default: return false;
   }
   nir_def_replace(&i->def, v); return true;
}
void tegu_mali_free(struct tegu_mali_blob *result)
{
   if(result) { free(result->data); result->data=NULL; result->size=0; }
}
static int compile_compute(const void *spirv,size_t size,
                     const struct tegu_mali_specialization *constants,size_t count,
                     struct tegu_mali_blob *result,char *error,size_t error_size,bool v2)
{
   if(error && error_size) error[0]=0;
   if(!result)return 2;
   result->data=NULL;result->size=0;
   struct context ctx={.v2=v2,.error=error,.error_size=error_size};
   if(!spirv || size<20 || size%4 || size>(1<<20)) {
      refuse(&ctx,"invalid SPIR-V byte span");return 2;
   }
   if(count>64 || (count && !constants)) {
      refuse(&ctx,"invalid specialization table");return 2;
   }
   struct compute_specialization checked_spec;
   if(v2) {
      uint32_t bad_id=0;
      const char *why=compute_specialization_check(spirv,size,constants,count,&checked_spec,&bad_id);
      if(why) {
         if(error&&error_size)snprintf(error,error_size,"Compute specialization ID %u: %s",bad_id,why);
         return 2;
      }
      constants=checked_spec.constants;
   }
   struct nir_spirv_specialization_entry entries[64]={0};
   for(size_t i=0;i<count;i++) {
      unsigned n=constants[i].size;
      if(n!=1 && n!=2 && n!=4 && n!=8) {
         refuse(&ctx,"invalid specialization scalar size");return 2;
      }
      for(size_t j=0;j<i;j++) if(constants[j].id==constants[i].id) {
         refuse(&ctx,"duplicate specialization id");return 2;
      }
      entries[i]=(struct nir_spirv_specialization_entry){
         .id=constants[i].id,.size=n,.data=(uint8_t *)constants[i].data
      };
   }
   struct nir_spirv_specialization specialization={.num_entries=count,.entries=entries};
   uint32_t *words=malloc(size);
   if(!words) { refuse(&ctx,"SPIR-V allocation failed");return 6; }
   memcpy(words,spirv,size);
   int status=0;
   unsigned char *packet=NULL;
   struct util_dynarray binary=UTIL_DYNARRAY_INIT;
   glsl_type_singleton_init_or_ref();
   const uint64_t gpu=NORD2_MALI_COMPILER_ID;
   struct spirv_to_nir_options options={
      .environment=NIR_SPIRV_VULKAN,
      .ubo_addr_format=nir_address_format_64bit_bounded_global,
      .ssbo_addr_format=nir_address_format_64bit_bounded_global,
      .push_const_addr_format=nir_address_format_32bit_offset,
      .shared_addr_format=nir_address_format_32bit_offset,
      .temp_addr_format=nir_address_format_32bit_offset,
      .skip_os_break_in_debug_build=true,
   };
   nir_shader *s=spirv_to_nir(words,size/4,count?&specialization:NULL,MESA_SHADER_COMPUTE,"main",&options,
                            pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false));
   free(words);
   if (!s) { refuse(&ctx,"SPIR-V translation failed");status=3;goto cleanup; }
   NIR_PASS(_,s,nir_lower_variable_initializers,nir_var_function_temp);
   NIR_PASS(_,s,nir_lower_returns);
   NIR_PASS(_,s,nir_inline_functions);
   nir_remove_non_entrypoints(s);
   NIR_PASS(_,s,nir_lower_variable_initializers,~0);
   NIR_PASS(_,s,nir_split_var_copies);
   NIR_PASS(_,s,nir_lower_var_copies);
   NIR_PASS(_,s,nir_lower_vars_to_ssa);
   NIR_PASS(_,s,nir_lower_system_values);
   nir_lower_compute_system_values_options cs={.global_id_is_32bit=true};
   NIR_PASS(_,s,nir_lower_compute_system_values,&cs);
   NIR_PASS(_,s,nir_opt_dce);
   if(v2) {
      /* Fold specialization-controlled branches before reflecting live access. */
      bool progress;
      do {
         progress=false;
         NIR_PASS(progress,s,nir_lower_vars_to_ssa);
         NIR_PASS(progress,s,nir_opt_deref);
         NIR_PASS(progress,s,nir_opt_copy_prop);
         NIR_PASS(progress,s,nir_opt_constant_folding);
         NIR_PASS(progress,s,nir_opt_dce);
         NIR_PASS(progress,s,nir_opt_dead_cf);
      } while(progress);
      if(!compute_lower_v2_resources(s,&ctx)){status=4;goto cleanup;}
   } else if(!compute_resources_supported(s,&ctx)){status=4;goto cleanup;}
   pan_preprocess_nir(s,gpu);
   if(v2) {
      const char *why=compute_texture_check(s,COMPUTE_TEX_BACKEND);
      if(why){refuse(&ctx,why);status=4;goto cleanup;}
   }
   NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_push_const,nir_address_format_32bit_offset);
   NIR_PASS(_,s,nir_shader_intrinsics_pass,app_push,nir_metadata_control_flow,&ctx);
   NIR_PASS(_,s,nir_shader_intrinsics_pass,resources,nir_metadata_control_flow,&ctx);
   if(ctx.failed){status=4;goto cleanup;}
   NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ssbo|nir_var_mem_ubo,
            nir_address_format_64bit_bounded_global);
   NIR_PASS(_,s,nir_shader_intrinsics_pass,constant_memory,nir_metadata_none,&ctx);
   if(v2) {
      NIR_PASS(_,s,nir_lower_image_atomics_to_global,NULL,NULL);
      NIR_PASS(_,s,nir_lower_atomics,compute_native_atomic);
   }
   struct pan_compile_inputs inputs={.gpu_id=gpu,.robust_modes=nir_var_mem_ssbo|nir_var_mem_ubo};
   struct pan_shader_info info={0};
   pan_postprocess_nir(s,&inputs,&info);
   /* Valhall TLS offsets are signed 16-bit immediates and register spills
    * start after scratch: bi_store_tl/bi_load_tl assert offset<0x8000. TGP2
    * refuses any TLS after codegen anyway, so refuse private arrays before the
    * backend; TGP1 keeps small scratch but refuses the guaranteed abort. */
   if(s->scratch_size&&(v2||s->scratch_size>=0x8000)) {
      refuse(&ctx,v2?"TGP2 private or workgroup memory allocation is unsupported":
             "private memory exceeds the Valhall 32KiB TLS immediate range");
      status=4;goto cleanup;
   }
   NIR_PASS(_,s,nir_shader_intrinsics_pass,sysvals,nir_metadata_control_flow,&ctx);
   if(ctx.failed){status=4;goto cleanup;}
   /* ABI1 has bounded descriptors, not arbitrary physical pointers. Do not
    * pass unresolved dereferences to Bifrost, which asserts on this input. */
   nir_foreach_function_impl(impl,s) {
      nir_foreach_block(block,impl) {
         nir_foreach_instr(instr,block) {
            if(instr->type==nir_instr_type_deref) {
               refuse(&ctx,"unlowered pointer outside bounded resource ABI");
               status=4;goto cleanup;
            }
         }
      }
   }
   nir_validate_shader(s,"tegu precompile");
   pan_shader_compile(s,&inputs,&binary,&info);
   if(!binary.size) { refuse(&ctx,"Mali backend emitted no code");status=5;goto cleanup; }
   if(v2) {
      if(info.tls_size||info.wls_size) {
         refuse(&ctx,"TGP2 private or workgroup memory allocation is unsupported");status=4;goto cleanup;
      }
      size_t code_offset=TEGU_PIPELINE2_HEADER_BYTES+ctx.manifest_count*TEGU_PIPELINE2_MANIFEST_BYTES;
      packet=calloc(1,code_offset+binary.size);
      if(!packet){refuse(&ctx,"pipeline allocation failed");status=6;goto cleanup;}
      unsigned flags=0;
      for(unsigned i=0;i<ctx.manifest_count;i++) {
         const struct tegu_pipeline_manifest *m=&ctx.manifest[i];
         if(m->flags&TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER)flags|=TEGU_PIPELINE2_FLAG_DUMMY_SAMPLER;
         uint32_t r[16]={m->set,m->binding,m->kind,m->array_count,m->access,m->table,m->entry,m->flags,
                        m->storage_format,0,0,0,m->shape,0,0,0};
         for(unsigned j=0;j<16;j++)tegu_pipeline_put(packet+TEGU_PIPELINE2_HEADER_BYTES+i*64+j*4,r[j]);
      }
      uint32_t header[32]={TEGU_PIPELINE2_MAGIC,TEGU_PIPELINE2_VERSION,binary.size,TEGU_PIPELINE_GPU,
         info.work_reg_count,info.push.count,(uint32_t)info.preload,(uint32_t)(info.preload>>32),ctx.bindings,
         s->info.workgroup_size[0],s->info.workgroup_size[1],s->info.workgroup_size[2],
         TEGU_PIPELINE2_HEADER_BYTES,ctx.manifest_count,TEGU_PIPELINE2_MANIFEST_BYTES,code_offset,flags};
      for(unsigned i=0;i<32;i++)tegu_pipeline_put(packet+i*4,header[i]);
      memcpy(packet+code_offset,binary.data,binary.size);
      struct tegu_pipeline_info2 checked;
      if(tegu_pipeline_parse2(packet,code_offset+binary.size,&checked)) {
         refuse(&ctx,"unsupported TGP2 native pipeline or resource manifest requirements");status=4;goto cleanup;
      }
      result->data=packet;result->size=code_offset+binary.size;
   } else {
      packet=calloc(1,TEGU_PIPELINE_HEADER_BYTES+binary.size);
      if(!packet) { refuse(&ctx,"pipeline allocation failed");status=6;goto cleanup; }
      uint32_t header[16]={TEGU_PIPELINE_MAGIC,1,binary.size,TEGU_PIPELINE_GPU,
          info.work_reg_count,info.push.count,(uint32_t)info.preload,(uint32_t)(info.preload>>32),
          ctx.bindings,s->info.workgroup_size[0],s->info.workgroup_size[1],s->info.workgroup_size[2],
          info.tls_size,info.wls_size,1,0};
      for(unsigned i=0;i<16;i++)tegu_pipeline_put(packet+4*i,header[i]);
      memcpy(packet+TEGU_PIPELINE_HEADER_BYTES,binary.data,binary.size);
      struct tegu_pipeline_info checked;
      if(tegu_pipeline_parse(packet,TEGU_PIPELINE_HEADER_BYTES+binary.size,&checked)) {
         refuse(&ctx,"unsupported native pipeline resource requirements");
         status=4;goto cleanup;
      }
      result->data=packet;result->size=TEGU_PIPELINE_HEADER_BYTES+binary.size;
   }
   packet=NULL;
cleanup:
   free(packet);util_dynarray_fini(&binary);
   if(s)ralloc_free(s);
   glsl_type_singleton_decref();return status;
}
int tegu_mali_compile_specialized(const void *spirv,size_t size,
                     const struct tegu_mali_specialization *constants,size_t count,
                     struct tegu_mali_blob *result,char *error,size_t error_size)
{
   return compile_compute(spirv,size,constants,count,result,error,error_size,false);
}
static bool compute_span(const void *p,size_t n)
{
   return p&&n&&n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool compute_overlap(const void *a,size_t an,const void *b,size_t bn)
{
   return an&&bn&&(uintptr_t)a<(uintptr_t)b+bn&&(uintptr_t)b<(uintptr_t)a+an;
}
int tegu_mali_compile_compute_v2(const void *spirv,size_t size,
                     const struct tegu_mali_specialization *constants,size_t count,
                     struct tegu_mali_blob *result,char *error,size_t error_size)
{
   /* Validate aliasing before writing output/error. Inputs may be unaligned. */
   if(!compute_span(spirv,size)||!compute_span(result,sizeof(*result))||count>64||
      (count&&!compute_span(constants,count*sizeof(*constants)))||
      (error_size&&!compute_span(error,error_size)))return 2;
   if(compute_overlap(result,sizeof(*result),spirv,size)||
      compute_overlap(result,sizeof(*result),constants,count*sizeof(*constants))||
      compute_overlap(error,error_size,spirv,size)||
      compute_overlap(error,error_size,constants,count*sizeof(*constants))||
      compute_overlap(error,error_size,result,sizeof(*result)))return 2;
   if(error&&error_size)error[0]=0;
   if(result->data||result->size) {
      if(error&&error_size)snprintf(error,error_size,"Output must be empty");
      return 2;
   }
   if(size<20||size%4||size>(1<<20)||compute_spirv_word(spirv,0)!=SpvMagicNumber) {
      if(error&&error_size)snprintf(error,error_size,"Invalid SPIR-V byte span or magic");
      return 2;
   }
   for(size_t p=5;p<size/4;) {
      unsigned n=compute_spirv_word(spirv,p)>>16;
      if(!n||n>size/4-p) {
         if(error&&error_size)snprintf(error,error_size,"Malformed SPIR-V instruction length");
         return 2;
      }
      if((compute_spirv_word(spirv,p)&0xffffu)==SpvOpMemoryModel&&
         (n!=3||compute_spirv_word(spirv,p+1)!=SpvAddressingModelLogical)) {
         if(error&&error_size)snprintf(error,error_size,"TGP2 requires logical descriptor addressing; physical pointers have no manifest binding");
         return 4;
      }
      p+=n;
   }
   return compile_compute(spirv,size,constants,count,result,error,error_size,true);
}
int tegu_mali_compile(const void *spirv,size_t size,struct tegu_mali_blob *result,
                     char *error,size_t error_size)
{
   return tegu_mali_compile_specialized(spirv,size,NULL,0,result,error,error_size);
}
#ifndef TEGU_MALI_LIBRARY_ONLY
int main(int argc,char **argv)
{
   if(argc!=3 && argc!=4) { fprintf(stderr,"usage: tegu_mali_compute INPUT.spv OUTPUT.bin [OUTPUT.tgp]\n");return 2; }
   size_t size=0;void *bytes=os_read_file(argv[1],&size);
   struct tegu_mali_blob package={0};char error[512];
   int status=tegu_mali_compile(bytes,size,&package,error,sizeof(error));free(bytes);
   if(status) { fprintf(stderr,"REFUSED: %s\n",error);return status; }
   struct tegu_pipeline_info info;
   if(tegu_pipeline_parse(package.data,package.size,&info)) {tegu_mali_free(&package);return 4;}
   FILE *f=fopen(argv[2],"wb");
   if(!f){tegu_mali_free(&package);return 6;}
   bool written=fwrite(package.data+TEGU_PIPELINE_HEADER_BYTES,1,info.code_bytes,f)==info.code_bytes;
   int closed=fclose(f);
   printf("bytes=%u registers=%u preload=0x%llx push_words=%u bindings=0x%x local=%u,%u,%u\n",
       info.code_bytes,info.registers,(unsigned long long)info.preload,info.push_words,
       info.bindings,info.local[0],info.local[1],info.local[2]);
   if(argc==4 && written && !closed) {
      f=fopen(argv[3],"wb");
      if(!f){tegu_mali_free(&package);return 6;}
      written=fwrite(package.data,1,package.size,f)==package.size;closed=fclose(f);
   }
   tegu_mali_free(&package);return written && !closed ? 0:6;
}
#endif
