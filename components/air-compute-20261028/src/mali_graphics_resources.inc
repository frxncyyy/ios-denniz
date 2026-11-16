/* Bounded opt-in read-only UBO lowering following pinned PanVK Arch>=9.
 * Set0 is table1; all four user descriptors remain separate from the driver
 * attribute table0. Dynamic BUFFER BYTE offsets from AIR push data are legal;
 * descriptor arrays/reindexing, SSBOs and writable resources are not. */
static bool resource_index_valid(nir_intrinsic_instr *i,unsigned first) {
 return i->intrinsic==nir_intrinsic_vulkan_resource_index&&
  nir_intrinsic_desc_type(i)==nir_descriptor_type_uniform_buffer&&
  nir_intrinsic_desc_set(i)==0&&nir_intrinsic_binding(i)>=first&&
  nir_intrinsic_binding(i)<first+2&&nir_src_is_const(i->src[0])&&
  nir_src_as_uint(i->src[0])==0&&i->def.bit_size==32&&i->def.num_components==3;
}
static bool lower_ubo_intrinsic(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)unused;b->cursor=nir_before_instr(&i->instr);
 if(i->intrinsic==nir_intrinsic_vulkan_resource_index) {
  /* PanVK build_res_index: (first descriptor handle, array index, array max).
   * This bounded plan has no descriptor arrays, so both latter values are0. */
  nir_def_replace(&i->def,nir_vec3(b,nir_imm_int(b,pan_res_handle(1,nir_intrinsic_binding(i))),
   nir_imm_int(b,0),nir_imm_int(b,0)));return true;
 }
 if(i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
  /* PanVK build_buffer_addr_for_res_index: (handle, array index, byte offset).
   * For the validated non-array descriptor this is the same vec3. */
  nir_def_replace(&i->def,i->src[0].ssa);return true;
 }
 return false;
}
/* Pinned PanVK panvk_vX_shader.c::valhall_pack_buf_idx: the backend
 * consumes scalar handle+array index after generic vec2-index IO lowering. */
static bool pack_ubo_index(nir_builder *b,nir_intrinsic_instr *i,void *unused) {
 (void)unused;
 if((i->intrinsic!=nir_intrinsic_load_ubo&&i->intrinsic!=nir_intrinsic_load_ssbo)||
    i->src[0].ssa->num_components==1)return false;
 b->cursor=nir_before_instr(&i->instr);
 nir_src_rewrite(&i->src[0],nir_iadd(b,nir_channel(b,i->src[0].ssa,0),nir_channel(b,i->src[0].ssa,1)));
 return true;
}
static const char *lower_ubos(nir_shader *s,struct tegu_mali_resource_plan *plan) {
 const unsigned first=s->info.stage==MESA_SHADER_VERTEX?0:2;
 unsigned mask=0;
 nir_foreach_variable_with_modes(var,s,nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform) {
  if(var->data.mode==nir_var_uniform) {
   if(first==0)return "Vertex sampled resources unsupported";
   continue; /* The separate strict texture pass validates the sole sampler. */
  }
  if(var->data.mode!=nir_var_mem_ubo)return "Only read-only UBO buffers are supported";
  unsigned binding=var->data.binding;
  if(var->data.descriptor_set||binding<first||binding>=first+2||var->data.bindless)
   return "Unexpected UBO descriptor set or binding";
  if(mask&BITFIELD_BIT(binding))return "Aliased UBO declarations";
  if(!glsl_type_is_struct_or_ifc(var->type))return "UBO descriptor arrays unsupported";
  unsigned bytes=glsl_get_explicit_size(var->type,true);
  if(!bytes||bytes>4096)return "UBO declared extent outside bounded span";
  mask|=BITFIELD_BIT(binding);
  plan->ubos[binding]=(struct tegu_mali_resource_ubo){
   first==0?TEGU_GRAPHICS_VERTEX:TEGU_GRAPHICS_FRAGMENT,0,binding,1,binding,bytes};
 }
 if(mask!=(BITFIELD_BIT(first)|BITFIELD_BIT(first+1)))return "Exactly two stage-owned UBOs required";
 unsigned accessed=0;
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(i->intrinsic==nir_intrinsic_vulkan_resource_index) {
   if(!resource_index_valid(i,first))return "UBO index must be one stage-owned non-array binding";
   accessed|=BITFIELD_BIT(nir_intrinsic_binding(i));
  } else if(i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
   if(nir_intrinsic_desc_type(i)!=nir_descriptor_type_uniform_buffer||
      i->def.bit_size!=32||i->def.num_components!=3||
      nir_def_instr(i->src[0].ssa)->type!=nir_instr_type_intrinsic||
      !resource_index_valid(nir_instr_as_intrinsic(nir_def_instr(i->src[0].ssa)),first))
    return "UBO descriptor load must use its direct validated index";
  } else if(i->intrinsic==nir_intrinsic_vulkan_resource_reindex) {
   return "Descriptor reindexing unsupported";
  } else if(i->intrinsic==nir_intrinsic_store_deref||i->intrinsic==nir_intrinsic_copy_deref) {
   nir_deref_instr *d=nir_src_as_deref(i->src[0]);
   if(d&&(d->modes&(nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform)))
    return "Writable resource dereference unsupported";
  }
 }
 if(accessed!=mask)return "UBO declaration and access mismatch";
 NIR_PASS(_,s,nir_shader_intrinsics_pass,lower_ubo_intrinsic,nir_metadata_control_flow,NULL);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ubo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_shader_intrinsics_pass,pack_ubo_index,nir_metadata_control_flow,NULL);
 NIR_PASS(_,s,nir_opt_constant_folding);
 nir_validate_shader(s,"tegu bounded user-table UBO lowering");
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 return NULL;
}

static bool fixed_ui_index_valid(nir_intrinsic_instr *i,bool vertex) {
 if(i->intrinsic!=nir_intrinsic_vulkan_resource_index||
    nir_intrinsic_desc_set(i)||!nir_src_is_const(i->src[0])||nir_src_as_uint(i->src[0])||
    i->def.bit_size!=32||i->def.num_components!=3)return false;
 unsigned binding=nir_intrinsic_binding(i),type=nir_intrinsic_desc_type(i);
 return vertex?(binding==0&&type==nir_descriptor_type_uniform_buffer):
  ((binding==1&&type==nir_descriptor_type_uniform_buffer)||
   (binding==2&&type==nir_descriptor_type_storage_buffer));
}
struct fixed_ui_lower_ctx {bool vertex,descriptors;};
static bool lower_fixed_ui_intrinsic(nir_builder *b,nir_intrinsic_instr *i,void *data) {
 const struct fixed_ui_lower_ctx *ctx=data;bool vertex=ctx->vertex;
 if(!ctx->descriptors&&i->intrinsic==nir_intrinsic_vulkan_resource_index) {
  if(!fixed_ui_index_valid(i,vertex))return false;
  b->cursor=nir_before_instr(&i->instr);
  nir_def_replace(&i->def,nir_vec3(b,nir_imm_int(b,pan_res_handle(1,nir_intrinsic_binding(i))),
   nir_imm_int(b,0),nir_imm_int(b,0)));return true;
 }
 if(ctx->descriptors&&i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
  nir_instr *source=nir_def_instr(i->src[0].ssa);
  if(source->type!=nir_instr_type_intrinsic||
     !fixed_ui_index_valid(nir_instr_as_intrinsic(source),vertex))return false;
  b->cursor=nir_before_instr(&i->instr);nir_def_replace(&i->def,i->src[0].ssa);return true;
 }
 return false;
}
static bool fixed_ui_constant_type(const struct glsl_type *type) {
 if(!glsl_type_is_struct_or_ifc(type)||glsl_get_length(type)!=1)return false;
 const struct glsl_type *array=glsl_get_struct_field(type,0);
 const struct glsl_type *element=glsl_get_array_element(array);
 return glsl_type_is_unsized_array(array)&&glsl_get_explicit_stride(array)==4&&
  element->base_type==GLSL_TYPE_UINT&&glsl_get_bit_size(element)==32&&
  glsl_get_vector_elements(element)==1;
}
/* R80 fixed UI pair after translation.  The caller-owned UBO descriptor sizes
 * are validated separately against AIR's 64/168-byte windows. */
static const char *lower_fixed_ui_ubo(nir_shader *s,
 struct tegu_mali_fixed_ui_plan *plan) {
 const bool vertex=s->info.stage==MESA_SHADER_VERTEX;
 const unsigned binding=vertex?0:1,declared_bytes=vertex?320:432;
 const unsigned expected_ubos=vertex?1:2;
 unsigned ubo_declarations=0,ssbo_declarations=0,ubo_accesses=0,ssbo_accesses=0;
 unsigned constant_min=UINT_MAX,constant_max=0;
 nir_foreach_variable_with_modes(var,s,nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform) {
  if(var->data.mode==nir_var_mem_ubo) {
   if(var->data.descriptor_set||var->data.binding!=binding||var->data.bindless||
      !glsl_type_is_struct_or_ifc(var->type))return "Fixed UI UBO descriptor differs from the exact stage binding";
   if(glsl_get_explicit_size(var->type,true)!=declared_bytes)return "Fixed UI UBO declaration has the wrong byte extent";
   ubo_declarations++;continue;
  }
  if(!vertex&&var->data.mode==nir_var_mem_ssbo&&var->data.descriptor_set==0&&
     var->data.binding==2&&!var->data.bindless&&(var->data.access&ACCESS_NON_WRITEABLE)&&
     fixed_ui_constant_type(var->type)){ssbo_declarations++;continue;}
  return "Fixed UI resource declaration differs from the exact UBO/constant shape";
 }
 if(ubo_declarations!=expected_ubos)return "Fixed UI has the wrong number of aliased UBO declarations";
 if(ssbo_declarations!=(vertex?0u:1u))return "Fixed UI has the wrong constant-buffer declaration count";
 nir_foreach_function_impl(impl,s) nir_foreach_block(block,impl) nir_foreach_instr(instr,block) {
  if(instr->type!=nir_instr_type_intrinsic)continue;
  nir_intrinsic_instr *i=nir_instr_as_intrinsic(instr);
  if(i->intrinsic==nir_intrinsic_vulkan_resource_index) {
   if(!fixed_ui_index_valid(i,vertex))return "Fixed UI resource index differs from the direct stage bindings";
   if(nir_intrinsic_desc_type(i)==nir_descriptor_type_uniform_buffer)ubo_accesses++;else ssbo_accesses++;
  } else if(i->intrinsic==nir_intrinsic_load_vulkan_descriptor) {
   nir_instr *source=nir_def_instr(i->src[0].ssa);
   if(i->def.bit_size!=32||i->def.num_components!=3||source->type!=nir_instr_type_intrinsic||
      !fixed_ui_index_valid(nir_instr_as_intrinsic(source),vertex)||
      nir_intrinsic_desc_type(i)!=nir_intrinsic_desc_type(nir_instr_as_intrinsic(source)))
    return "Fixed UI descriptor load must use its direct validated index";
  } else if(i->intrinsic==nir_intrinsic_vulkan_resource_reindex) {
   return "Fixed UI descriptor reindexing unsupported";
  } else if(i->intrinsic==nir_intrinsic_load_deref) {
   nir_deref_instr *d=nir_src_as_deref(i->src[0]);
   if(d&&(d->modes&nir_var_mem_ssbo)) {
    if(!(nir_intrinsic_access(i)&ACCESS_NON_WRITEABLE)||i->def.bit_size!=32||i->def.num_components!=1)
     return "Fixed UI constant buffer has a non-readonly scalar access";
    nir_deref_path path;nir_deref_path_init(&path,d,NULL);
    for(nir_deref_instr **p=&path.path[1];*p;p++)if((*p)->deref_type==nir_deref_type_array&&
       !nir_src_is_const((*p)->arr.index)){nir_deref_path_finish(&path);return "Fixed UI constant buffer has a dynamic access";}
    nir_deref_path_finish(&path);
    unsigned offset=nir_deref_instr_get_const_offset(d,glsl_get_natural_size_align_bytes);
    if(offset>332)return "Fixed UI constant buffer access exceeds the exact 84-word span";
    if(offset<constant_min)constant_min=offset;
    if(offset>constant_max)constant_max=offset;
   }
  } else if(i->intrinsic==nir_intrinsic_store_deref||i->intrinsic==nir_intrinsic_copy_deref) {
   nir_deref_instr *d=nir_src_as_deref(i->src[0]);
   if(d&&(d->modes&(nir_var_mem_ubo|nir_var_mem_ssbo|nir_var_image|nir_var_uniform)))
    return "Fixed UI writable resource dereference unsupported";
  }
 }
 if(!ubo_accesses||(!vertex&&(!ssbo_accesses||constant_min!=0||constant_max!=332)))
  return "Fixed UI resource declarations do not cover the exact live access span";
 plan->ubos[vertex?0:1]=(struct tegu_mali_resource_ubo){
  vertex?TEGU_GRAPHICS_VERTEX:TEGU_GRAPHICS_FRAGMENT,0,binding,1,binding,declared_bytes};
 if(!vertex)plan->ubos[2]=(struct tegu_mali_resource_ubo){TEGU_GRAPHICS_FRAGMENT,0,2,1,2,0};
 struct fixed_ui_lower_ctx ctx={vertex,true};
 NIR_PASS(_,s,nir_shader_intrinsics_pass,lower_fixed_ui_intrinsic,nir_metadata_control_flow,&ctx);
 ctx.descriptors=false;
 NIR_PASS(_,s,nir_shader_intrinsics_pass,lower_fixed_ui_intrinsic,nir_metadata_control_flow,&ctx);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ubo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_lower_explicit_io,nir_var_mem_ssbo,nir_address_format_vec2_index_32bit_offset);
 NIR_PASS(_,s,nir_shader_intrinsics_pass,pack_ubo_index,nir_metadata_control_flow,NULL);
 NIR_PASS(_,s,nir_opt_constant_folding);
 nir_validate_shader(s,"tegu fixed UI UBO and constant lowering");
 nir_shader_gather_info(s,nir_shader_get_entrypoint(s));
 return NULL;
}
