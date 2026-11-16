#ifndef TEGU_NATIVE_PACKAGE_AC1_H
#define TEGU_NATIVE_PACKAGE_AC1_H
#include <stddef.h>
#include <stdint.h>
#define TEGU_AC1_HEADER_BYTES 448u
#define TEGU_AC1_VS_BYTES 1792u
#define TEGU_AC1_FS_BYTES 256u
#define TEGU_AC1_PACKAGE_BYTES (TEGU_AC1_HEADER_BYTES+TEGU_AC1_VS_BYTES+TEGU_AC1_FS_BYTES)
#define TEGU_AC1_MODE_PRODUCTION 1u
#define TEGU_AC1_MODE_RESEARCH_RT0_PLACEHOLDER 0x52543050u
#define TEGU_AC1_SURFACE_REAL 1u
#define TEGU_AC1_SURFACE_RESEARCH_PLACEHOLDER 2u
#define TEGU_AC1_TOPOLOGY_TRIANGLE_LIST 1u
#define TEGU_AC1_LOAD_DONTCARE 0u
#define TEGU_AC1_LOAD 1u
#define TEGU_AC1_LOAD_CLEAR 2u
#define TEGU_AC1_STORE_DONTCARE 0u
#define TEGU_AC1_STORE 1u
#define TEGU_AC1_OPERATION_DRAW 1u
#define TEGU_AC1_OPERATION_CLEAR 2u
#define TEGU_AC1_OPERATION_LOAD 3u
#define TEGU_AC1_OPERATION_STORE 4u
#define TEGU_AC1_OK 0
#define TEGU_AC1_EINVAL -1
#define TEGU_AC1_EFORMAT -2
#define TEGU_AC1_EIDENTITY -3
#define TEGU_AC1_ECONTRACT -4
#define TEGU_AC1_ERANGE -5
#define TEGU_AC1_EALIAS -6
struct tegu_ac1_resource {
 uint32_t role,stage,set,binding,source_class,backend_class,table,index;
 uint32_t declared_bytes,offset_push_byte,offset_alignment,base_alignment,window_bytes,access;
};
struct tegu_ac1_stage {
 uint32_t bytes,stage,registers,tls,fau_words,sysval_mask,push_bytes;
 uint32_t reserved;
 uint64_t preload;
};
struct tegu_ac1_info {
 uint32_t version, vs_bytes, fs_bytes;
 struct tegu_ac1_stage vs,fs;
 uint32_t resource_count,output_count,rt0_mask,rt1_mask;
 struct tegu_ac1_resource resources[2];
 uint8_t identity_sha256[6][32];
};
struct tegu_ac1_api_plan {
 uint32_t version,output_count,rt0_mask,rt1_mask;
 uint32_t position_offset,secondary_offset,secondary_enabled,generic_size_bytes;
 uint32_t draw_vertex_count,draw_instance_count,draw_base_instance,draw_indexed;
 uint32_t target0_format,target1_format,target0_blend_enabled,target1_blend_enabled;
 uint32_t target_descriptors_seen,resource_count;
 struct tegu_ac1_resource resources[2];
};
struct tegu_ac1_target_frontend {
 uint32_t format,write_mask,blend_enabled;
};
/* Fixed-layout, pointer-free frontend attestation. This object is an internal
 * kernel copy made after parsing the real pipeline; never dereference a user pointer. */
struct tegu_ac1_frontend {
 uint8_t metallib_sha256[32],profile_sha256[32],vs_spirv_sha256[32],fs_spirv_sha256[32];
 char vs_entry[32],fs_entry[32];
 struct tegu_ac1_stage vs,fs;
 struct tegu_ac1_api_plan plan;
 struct tegu_ac1_target_frontend targets[2];
 uint32_t target_count,sample_count,depth_enabled,metal_vertex_attribute_count,compiler_vertex_attribute_reads,fs_input_count,fs_resource_count;
 uint32_t fs_output_mask,fs_output0_float4,fs_output0_index,fs_output1_float4,fs_output1_index;
 uint32_t rect_slot,rect_push_byte,matrix_slot,matrix_push_byte;
 uint32_t fs_push_span,color_rule0_byte,color_rule0_value,color_rule1_byte,color_rule1_value;
 uint32_t topology,indexed,vertex_start,vertex_count,instance_count,base_instance;
};
struct tegu_ac1_surface {
 uint32_t kind,slot,format,width,height,row_bytes;
 uint64_t resolved_length_bytes,offset,resolved_object_id,token;
};
struct tegu_ac1_resource_binding {
 uint32_t slot,push_byte;
 uint64_t token,resolved_length_bytes,offset,resolved_object_id;
};
struct tegu_ac1_pass_target {
 uint32_t logical_index,internal_buffer_offset,write_enable,internal_format;
 uint32_t writeback_format,writeback_block_format,writeback_msaa,swizzle;
 uint32_t clean_tile_write_enable,row_stride,surface_stride;
 uint32_t load_action,store_action,clear_color[4];
};
struct tegu_ac1_pass_state {
 uint32_t render_x,render_y,render_width,render_height,sample_count,tile_size;
 uint32_t color_buffer_allocation,render_target_count,target_mask,draw_write_mask,draw_read_mask;
 struct tegu_ac1_pass_target targets[2];
 uint32_t viewport_x,viewport_y,viewport_width,viewport_height;
 uint32_t scissor_x,scissor_y,scissor_width,scissor_height;
 uint32_t sample_mask,cull_enabled,fill_mode,rasterization_enabled,alpha_to_coverage;
};
struct tegu_ac1_bind_request {
 uint32_t mode,operation,research_opt_in,width,height;
 struct tegu_ac1_frontend frontend;
 struct tegu_ac1_surface rt[2];
 struct tegu_ac1_resource_binding rect,matrix;
 struct tegu_ac1_pass_state pass;
};
struct tegu_ac1_bound_target {
 uint32_t logical_index,internal_buffer_offset,write_enable,internal_format;
 uint32_t writeback_format,writeback_block_format,writeback_msaa,swizzle;
 uint32_t clean_tile_write_enable,row_stride,surface_stride;
 uint32_t load_action,store_action,clear_color[4];
 uint64_t token,object_id,offset,length;
};
struct tegu_ac1_binding {
 uint32_t mode,width,height,render_x,render_y,render_width,render_height,sample_count,tile_size,color_buffer_allocation,rt_count,target_mask,draw_write_mask,draw_read_mask;
 uint64_t rt0_token,rt1_token,rect_token,matrix_token;
 uint64_t rect_descriptor_offset,matrix_descriptor_offset;
 uint32_t rect_descriptor_bytes,matrix_descriptor_bytes;
 uint32_t rect_push_residual,matrix_push_residual,offsets_r8_qualified;
 uint32_t rect_fau_byte,matrix_fau_byte;
 uint32_t rt0_format,rt1_format,rt0_mask,rt1_mask;
 uint64_t blend_descriptor[2];
 uint32_t vs_table,vs_table_entries,fs_table_entries,vs_position_offset;
 uint32_t vs_secondary_enabled,vary_size,fs_fau_words,fs_push_span;
 uint32_t viewport[4],scissor[4],sample_mask,cull_enabled,fill_mode,rasterization_enabled,alpha_to_coverage;
 uint32_t draw_topology,draw_vertex_start,draw_vertex_count,draw_instance_count,draw_indexed,draw_base_instance;
 struct tegu_ac1_bound_target pass_targets[2];
};
struct tegu_ac1_layout { uint32_t vs_offset,fs_offset,total_bytes; };

/* All structures crossing this prototype API are fixed-width, pointer-free. */
#if defined(__cplusplus)
extern "C" {
#endif
#if defined(__cplusplus)
static_assert(sizeof(struct tegu_ac1_resource)==56,"AC1 resource ABI");
static_assert(sizeof(struct tegu_ac1_stage)==40,"AC1 stage ABI");
static_assert(sizeof(struct tegu_ac1_api_plan)==184,"AC1 plan ABI");
static_assert(sizeof(struct tegu_ac1_target_frontend)==12,"AC1 target ABI");
static_assert(offsetof(struct tegu_ac1_frontend,vs_entry)==128,"AC1 frontend entry offset");
static_assert(offsetof(struct tegu_ac1_frontend,plan)==272,"AC1 frontend plan offset");
static_assert(offsetof(struct tegu_ac1_frontend,metal_vertex_attribute_count)==492,"AC1 Metal descriptor count offset");
static_assert(offsetof(struct tegu_ac1_frontend,compiler_vertex_attribute_reads)==496,"AC1 compiler fetch count offset");
static_assert(offsetof(struct tegu_ac1_frontend,fs_output_mask)==508,"AC1 fragment output mask offset");
static_assert(offsetof(struct tegu_ac1_frontend,rect_slot)==528,"AC1 resource slot map offset");
static_assert(offsetof(struct tegu_ac1_bind_request,pass)==808,"AC1 pass state offset");
static_assert(offsetof(struct tegu_ac1_binding,pass_targets)==280,"AC1 bound pass target offset");
static_assert(offsetof(struct tegu_ac1_bind_request,frontend)==24,"AC1 request frontend offset");
static_assert(sizeof(struct tegu_ac1_frontend)==592,"AC1 frontend ABI");
static_assert(sizeof(struct tegu_ac1_bind_request)==1040,"AC1 request ABI");
#else
_Static_assert(sizeof(struct tegu_ac1_resource)==56,"AC1 resource ABI");
_Static_assert(sizeof(struct tegu_ac1_stage)==40,"AC1 stage ABI");
_Static_assert(sizeof(struct tegu_ac1_api_plan)==184,"AC1 plan ABI");
_Static_assert(sizeof(struct tegu_ac1_target_frontend)==12,"AC1 target ABI");
_Static_assert(offsetof(struct tegu_ac1_frontend,vs_entry)==128,"AC1 frontend entry offset");
_Static_assert(offsetof(struct tegu_ac1_frontend,plan)==272,"AC1 frontend plan offset");
_Static_assert(offsetof(struct tegu_ac1_frontend,metal_vertex_attribute_count)==492,"AC1 Metal descriptor count offset");
_Static_assert(offsetof(struct tegu_ac1_frontend,compiler_vertex_attribute_reads)==496,"AC1 compiler fetch count offset");
_Static_assert(offsetof(struct tegu_ac1_frontend,fs_output_mask)==508,"AC1 fragment output mask offset");
_Static_assert(offsetof(struct tegu_ac1_frontend,rect_slot)==528,"AC1 resource slot map offset");
_Static_assert(offsetof(struct tegu_ac1_bind_request,pass)==808,"AC1 pass state offset");
_Static_assert(offsetof(struct tegu_ac1_binding,pass_targets)==280,"AC1 bound pass target offset");
_Static_assert(offsetof(struct tegu_ac1_bind_request,frontend)==24,"AC1 request frontend offset");
_Static_assert(sizeof(struct tegu_ac1_frontend)==592,"AC1 frontend ABI");
_Static_assert(sizeof(struct tegu_ac1_bind_request)==1040,"AC1 request ABI");
#endif
int tegu_ac1_parse(const void *package,size_t length,struct tegu_ac1_info *out);
/* package_copy must be stable kernel-owned storage. Hash the same snapshot before copying code. */
int tegu_ac1_copy_code(const struct tegu_ac1_info *info,const void *package_copy,size_t length,
 void *vs_destination,size_t vs_capacity,void *fs_destination,size_t fs_capacity);
int tegu_ac1_bind(const struct tegu_ac1_info *info,const struct tegu_ac1_bind_request *request,struct tegu_ac1_binding *out);
int tegu_ac1_layout_build(const struct tegu_ac1_info *info,uint32_t capacity,struct tegu_ac1_layout *out);
#if defined(__cplusplus)
}
#endif
#endif
