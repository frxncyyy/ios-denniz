#include "$NORD2_WORK/gpu/metal/tegu/mali_graphics_compiler.c"
#include <assert.h>
static void rejected_at_compile_boundary(nir_tex_src_type type) {
 nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
  pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),"source boundary");
 nir_def *value=nir_build_tex(&b,nir_texop_tex,.dim=GLSL_SAMPLER_DIM_2D,
  .dest_type=nir_type_float32,.coord=nir_imm_vec2(&b,0.25,0.75),
  .texture_handle=nir_imm_int(&b,pan_res_handle(1,3)),
  .sampler_handle=nir_imm_int(&b,pan_res_handle(2,5)));
 nir_tex_instr *tex=nir_instr_as_tex(nir_def_instr(value));
 nir_tex_instr_add_src(tex,type,nir_imm_int(&b,0));
 struct stage_job job={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,.resources=true};
 struct tegu_mali_graphics result={0};const char *reason=NULL;
 int rc=compile_stage(b.shader,&job,&result,&reason);
 assert(rc&&reason&&!result.code&&!result.size);
 assert(!strcmp(reason,graphics_texture_source_error(type,GRAPHICS_TEX_RESOURCES)));
 ralloc_free(b.shader);
}
static void rejected_op_at_compile_boundary(nir_texop op) {
 nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
  pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),"operation boundary");
 nir_def *value=nir_build_tex(&b,nir_texop_tex,.dim=GLSL_SAMPLER_DIM_2D,
  .dest_type=nir_type_float32,.coord=nir_imm_vec2(&b,0.25,0.75),
  .texture_handle=nir_imm_int(&b,pan_res_handle(1,3)),
  .sampler_handle=nir_imm_int(&b,pan_res_handle(2,5)));
 nir_instr_as_tex(nir_def_instr(value))->op=op;
 struct stage_job job={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,.resources=true};
 struct tegu_mali_graphics result={0};const char *reason=NULL;
 assert(compile_stage(b.shader,&job,&result,&reason));
 assert(reason&&!result.code&&!strcmp(reason,graphics_texture_op_error(op)));
 ralloc_free(b.shader);
}
static void compile_dimension(enum glsl_sampler_dim dim) {
 nir_builder b=nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
  pan_get_nir_shader_compiler_options(NORD2_MALI_ARCH,false),"dimension boundary");
 bool ms=dim==GLSL_SAMPLER_DIM_MS||dim==GLSL_SAMPLER_DIM_SUBPASS_MS;
 bool fetch=ms||dim==GLSL_SAMPLER_DIM_BUF||dim==GLSL_SAMPLER_DIM_SUBPASS;
 bool array=dim==GLSL_SAMPLER_DIM_SUBPASS||dim==GLSL_SAMPLER_DIM_SUBPASS_MS;
 unsigned n=dim==GLSL_SAMPLER_DIM_1D||dim==GLSL_SAMPLER_DIM_BUF?1:
  dim==GLSL_SAMPLER_DIM_3D||dim==GLSL_SAMPLER_DIM_CUBE?3:2;
 nir_def *coord=fetch?nir_imm_zero(&b,n+array,32):nir_imm_floatN_t(&b,0.5,32);
 if(!fetch)coord=nir_replicate(&b,coord,n);
 nir_def *value=nir_build_tex(&b,ms?nir_texop_txf_ms:fetch?nir_texop_txf:nir_texop_txl,
  .dim=ms?GLSL_SAMPLER_DIM_MS:dim,.is_array=array,.dest_type=nir_type_float32,.coord=coord,
  .lod=ms?NULL:fetch?nir_imm_int(&b,0):nir_imm_float(&b,0),
  .ms_index=ms?nir_imm_int(&b,0):NULL,
  .texture_handle=nir_imm_int(&b,pan_res_handle(1,3)),
  .sampler_handle=nir_imm_int(&b,pan_res_handle(2,5)));
 nir_instr_as_tex(nir_def_instr(value))->sampler_dim=dim;
 nir_variable *out=nir_variable_create(b.shader,nir_var_shader_out,glsl_vec4_type(),"color");
 out->data.location=FRAG_RESULT_DATA0;nir_store_var(&b,out,value,15);
 nir_shader_gather_info(b.shader,nir_shader_get_entrypoint(b.shader));
 struct stage_job job={.requested=TEGU_GRAPHICS_FRAGMENT,.stage=MESA_SHADER_FRAGMENT,.resources=true};
 struct tegu_mali_graphics result={0};const char *reason=NULL;
 int rc=compile_stage(b.shader,&job,&result,&reason);
 if(rc)fprintf(stderr,"dimension %u refused: %s\n",dim,reason);
 assert(!rc&&result.code&&result.size&&!result.tls);
 tegu_mali_graphics_free(&result);ralloc_free(b.shader);
}
int main(void) {
 glsl_type_singleton_init_or_ref();
 assert((graphics_texture_source_error(nir_tex_src_coord,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_coord,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_coord,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_projector,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_projector,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_projector,GRAPHICS_TEX_BACKEND)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_comparator,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_comparator,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_comparator,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_offset,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_offset,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_offset,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_bias,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_bias,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_bias,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_lod,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_lod,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_lod,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_min_lod,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_min_lod,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_min_lod,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_max_lod_kk,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_max_lod_kk,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_max_lod_kk,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_max_lod_kk);
 assert((graphics_texture_source_error(nir_tex_src_lod_bias_min_agx,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_lod_bias_min_agx,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_lod_bias_min_agx,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_lod_bias_min_agx);
 assert((graphics_texture_source_error(nir_tex_src_ms_index,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ms_index,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ms_index,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ms_mcs_intel,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_ms_mcs_intel,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_ms_mcs_intel,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_ms_mcs_intel);
 assert((graphics_texture_source_error(nir_tex_src_ddx,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ddx,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ddx,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ddy,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ddy,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_ddy,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_deref,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_deref,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_deref,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_texture_deref);
 assert((graphics_texture_source_error(nir_tex_src_sampler_deref,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_sampler_deref,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_deref,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_sampler_deref);
 assert((graphics_texture_source_error(nir_tex_src_texture_offset,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_offset,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_offset,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_sampler_offset,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_sampler_offset,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_sampler_offset,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_handle,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_handle,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_handle,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_sampler_handle,GRAPHICS_TEX_AUTHORED)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_sampler_handle,GRAPHICS_TEX_RESOURCES)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_sampler_handle,GRAPHICS_TEX_BACKEND)==NULL)==1);
 assert((graphics_texture_source_error(nir_tex_src_texture_heap_offset,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_heap_offset,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_heap_offset,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_texture_heap_offset);
 assert((graphics_texture_source_error(nir_tex_src_sampler_heap_offset,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_heap_offset,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_heap_offset,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_sampler_heap_offset);
 assert((graphics_texture_source_error(nir_tex_src_sampler_deref_intrinsic,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_deref_intrinsic,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_deref_intrinsic,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_sampler_deref_intrinsic);
 assert((graphics_texture_source_error(nir_tex_src_texture_deref_intrinsic,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_deref_intrinsic,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_deref_intrinsic,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_texture_deref_intrinsic);
 assert((graphics_texture_source_error(nir_tex_src_plane,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_plane,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_plane,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_plane);
 assert((graphics_texture_source_error(nir_tex_src_backend1,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_backend1,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_backend1,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_backend1);
 assert((graphics_texture_source_error(nir_tex_src_backend2,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_backend2,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_backend2,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_backend2);
 assert((graphics_texture_source_error(nir_tex_src_ref_coord,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_ref_coord,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_ref_coord,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_ref_coord);
 assert((graphics_texture_source_error(nir_tex_src_texture_2_deref,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_2_deref,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_2_deref,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_texture_2_deref);
 assert((graphics_texture_source_error(nir_tex_src_sampler_2_deref,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_2_deref,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_2_deref,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_sampler_2_deref);
 assert((graphics_texture_source_error(nir_tex_src_texture_2_handle,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_2_handle,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_texture_2_handle,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_texture_2_handle);
 assert((graphics_texture_source_error(nir_tex_src_sampler_2_handle,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_2_handle,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_sampler_2_handle,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_sampler_2_handle);
 assert((graphics_texture_source_error(nir_tex_src_block_size,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_block_size,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_block_size,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_block_size);
 assert((graphics_texture_source_error(nir_tex_src_box_size,GRAPHICS_TEX_AUTHORED)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_box_size,GRAPHICS_TEX_RESOURCES)==NULL)==0);
 assert((graphics_texture_source_error(nir_tex_src_box_size,GRAPHICS_TEX_BACKEND)==NULL)==0);
 rejected_at_compile_boundary(nir_tex_src_box_size);
 assert((graphics_texture_op_error(nir_texop_tex)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_txb)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_txl)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_txd)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_txf)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_txf_ms)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_txf_ms_fb)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_txf_ms_fb);
 assert((graphics_texture_op_error(nir_texop_txf_ms_mcs_intel)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_txf_ms_mcs_intel);
 assert((graphics_texture_op_error(nir_texop_txs)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_lod)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_tg4)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_query_levels)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_texture_samples)==NULL)==1);
 assert((graphics_texture_op_error(nir_texop_samples_identical)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_samples_identical);
 assert((graphics_texture_op_error(nir_texop_tex_prefetch)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_tex_prefetch);
 assert((graphics_texture_op_error(nir_texop_lod_bias)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_lod_bias);
 assert((graphics_texture_op_error(nir_texop_fragment_fetch_amd)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_fragment_fetch_amd);
 assert((graphics_texture_op_error(nir_texop_fragment_mask_fetch_amd)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_fragment_mask_fetch_amd);
 assert((graphics_texture_op_error(nir_texop_descriptor_amd)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_descriptor_amd);
 assert((graphics_texture_op_error(nir_texop_sampler_descriptor_amd)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_sampler_descriptor_amd);
 assert((graphics_texture_op_error(nir_texop_image_min_lod_agx)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_image_min_lod_agx);
 assert((graphics_texture_op_error(nir_texop_has_custom_border_color_agx)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_has_custom_border_color_agx);
 assert((graphics_texture_op_error(nir_texop_custom_border_color_agx)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_custom_border_color_agx);
 assert((graphics_texture_op_error(nir_texop_hdr_dim_nv)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_hdr_dim_nv);
 assert((graphics_texture_op_error(nir_texop_tex_type_nv)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_tex_type_nv);
 assert((graphics_texture_op_error(nir_texop_sample_pos_nv)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_sample_pos_nv);
 assert((graphics_texture_op_error(nir_texop_gradient_pan)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_gradient_pan);
 assert((graphics_texture_op_error(nir_texop_sample_weighted_qcom)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_sample_weighted_qcom);
 assert((graphics_texture_op_error(nir_texop_box_filter_qcom)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_box_filter_qcom);
 assert((graphics_texture_op_error(nir_texop_block_match_sad_qcom)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_block_match_sad_qcom);
 assert((graphics_texture_op_error(nir_texop_block_match_ssd_qcom)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_block_match_ssd_qcom);
 assert((graphics_texture_op_error(nir_texop_resinfo_intel)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_resinfo_intel);
 assert((graphics_texture_op_error(nir_texop_sparse_residency_intel)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_sparse_residency_intel);
 assert((graphics_texture_op_error(nir_texop_sparse_residency_txf_intel)==NULL)==0);
 rejected_op_at_compile_boundary(nir_texop_sparse_residency_txf_intel);
 compile_dimension(GLSL_SAMPLER_DIM_1D);
 compile_dimension(GLSL_SAMPLER_DIM_2D);
 compile_dimension(GLSL_SAMPLER_DIM_3D);
 compile_dimension(GLSL_SAMPLER_DIM_CUBE);
 compile_dimension(GLSL_SAMPLER_DIM_RECT);
 compile_dimension(GLSL_SAMPLER_DIM_BUF);
 compile_dimension(GLSL_SAMPLER_DIM_EXTERNAL);
 compile_dimension(GLSL_SAMPLER_DIM_MS);
 compile_dimension(GLSL_SAMPLER_DIM_SUBPASS);
 compile_dimension(GLSL_SAMPLER_DIM_SUBPASS_MS);
assert(graphics_texture_source_error((nir_tex_src_type)999,GRAPHICS_TEX_BACKEND));
 assert(graphics_texture_op_error((nir_texop)999));
 glsl_type_singleton_decref();return 0;
}
