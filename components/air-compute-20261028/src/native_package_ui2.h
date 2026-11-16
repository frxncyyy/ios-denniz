#ifndef TEGU_FIXED_UI_TEXTURE_NATIVE_PACKAGE_UI2_H
#define TEGU_FIXED_UI_TEXTURE_NATIVE_PACKAGE_UI2_H
#include <stddef.h>
#include <stdint.h>

/* TFU2 is the sampled fixed_vert_lph_spc/fixed_frag_lph_cph package.  It is
 * additive: no TFU1 parser may interpret this header or its seven-entry SRT. */
#define TEGU_UI2_VERSION 2u
#define TEGU_UI2_HEADER_BYTES 1024u
#define TEGU_UI2_STAGE_MAX 65536u
#define TEGU_UI2_ATTRIBUTE_COUNT 3u
#define TEGU_UI2_BUFFER_COUNT 4u
#define TEGU_UI2_RESOURCE_COUNT 5u
#define TEGU_UI2_PUSH_COUNT 2u
#define TEGU_UI2_FORMAT_BGRA8 80u
#define TEGU_UI2_TEXTURE_2D 2u
#define TEGU_UI2_TOPOLOGY_TRIANGLE 3u
#define TEGU_UI2_LOAD_DONTCARE 0u
#define TEGU_UI2_LOAD 1u
#define TEGU_UI2_LOAD_CLEAR 2u
#define TEGU_UI2_STORE_DONTCARE 0u
#define TEGU_UI2_STORE 1u
#define TEGU_UI2_OK 0
#define TEGU_UI2_EINVAL -1
#define TEGU_UI2_EFORMAT -2
#define TEGU_UI2_ECONTRACT -3
#define TEGU_UI2_ERANGE -4
#define TEGU_UI2_EALIAS -5

struct tegu_ui2_stage {uint32_t bytes,offset,stage,registers,tls,fau_words,sysval_mask,push_bytes;uint64_t preload;};
struct tegu_ui2_varying {uint32_t location,offset,components,alu_type;};
struct tegu_ui2_pair {
 uint32_t version,flags,position_offset,position_size,point_position_offset,point_position_size;
 uint32_t secondary_offset,secondary_size,secondary_registers,generic_size_bytes;uint64_t secondary_preload;
 uint32_t vertex_attribute_mask,vertex_varying_mask,fragment_varying_mask,fragment_uses_ld_var;
 uint32_t noperspective_mask,needs_extended_fifo,point_size_section,point_size_offset,point_size_alu_type,varying_count;
 struct tegu_ui2_varying varyings[4];
};
struct tegu_ui2_attribute {uint32_t location,format,offset,buffer_index,stride,input_rate,divisor;};
struct tegu_ui2_resource {uint32_t backend_stage,metal_stage,metal_slot,descriptor_set,binding,table,index,declared_bytes,window_bytes,push_offset;};
struct tegu_ui2_push_span {uint32_t stage,offset,length,reserved;};
struct tegu_ui2_texture_contract {
 uint32_t backend_stage,metal_stage,texture_slot,sampler_slot,descriptor_set,binding;
 uint32_t table,texture_index,sampler_index,format,texture_type,normalized_coordinates;
};
struct tegu_ui2_info {
 uint32_t version,total_bytes,target_format,target_write_mask,target_blend_enabled,sample_count,topology;
 uint32_t load_action_mask,store_action_mask,constant_stage,constant_length,specialization_id,specialization_length;
 struct tegu_ui2_stage stages[2];struct tegu_ui2_pair pair;
 struct tegu_ui2_attribute attributes[TEGU_UI2_ATTRIBUTE_COUNT];
 struct tegu_ui2_resource resources[TEGU_UI2_RESOURCE_COUNT];
 struct tegu_ui2_push_span pushes[TEGU_UI2_PUSH_COUNT];
 struct tegu_ui2_texture_contract texture;
 uint8_t constant[8],specialization[4];
};
struct tegu_ui2_allocation {uint64_t token,object_id,length,offset;};
struct tegu_ui2_target {struct tegu_ui2_allocation allocation;uint32_t format,width,height,row_bytes;};
struct tegu_ui2_buffer {struct tegu_ui2_allocation allocation;uint32_t metal_stage,metal_slot;uint64_t descriptor_offset,descriptor_length;};
struct tegu_ui2_texture {
 struct tegu_ui2_allocation allocation;uint64_t resource_id;
 uint32_t metal_stage,metal_slot,format,texture_type,width,height,row_bytes;
};
struct tegu_ui2_pass {
 uint32_t render_x,render_y,render_width,render_height,viewport_x,viewport_y,viewport_width,viewport_height;
 uint32_t scissor_x,scissor_y,scissor_width,scissor_height,load_action,store_action,clear_color[4];
};
struct tegu_ui2_bind_request {
 uint32_t version,width,height,topology,indexed,vertex_start,vertex_count,instance_count,base_instance;
 struct tegu_ui2_target target;struct tegu_ui2_allocation vertex;uint32_t vertex_slot,vertex_stride;
 struct tegu_ui2_buffer resources[TEGU_UI2_BUFFER_COUNT];struct tegu_ui2_texture texture;
 uint8_t vertex_push[8],fragment_push[12];struct tegu_ui2_pass pass;
};
struct tegu_ui2_bound_resource {
 uint64_t token,object_id,descriptor_offset;uint32_t descriptor_bytes,window_bytes,metal_stage,metal_slot,table,index,push_offset,push_residual;
};
struct tegu_ui2_bound_texture {
 uint64_t token,object_id,offset,bytes,resource_id;uint32_t format,texture_type,width,height,row_bytes,table,texture_index,sampler_index;
};
struct tegu_ui2_binding {
 uint32_t width,height,target_format,target_row_bytes,load_action,store_action,clear_color[4];
 uint64_t target_token,target_object_id,target_offset,target_bytes,vertex_token,vertex_object_id,vertex_offset,vertex_bytes;
 uint32_t vertex_stride,topology,vertex_start,vertex_count,instance_count,base_instance,render[4],viewport[4],scissor[4];
 struct tegu_ui2_bound_resource resources[TEGU_UI2_BUFFER_COUNT];struct tegu_ui2_bound_texture texture;
 uint8_t vertex_push[8],fragment_push[12],constant[8],specialization[4];uint32_t constant_stage,specialization_id;
};
#if defined(__cplusplus)
static_assert(sizeof(struct tegu_ui2_stage)==40,"UI2 stage ABI");
static_assert(sizeof(struct tegu_ui2_pair)==152,"UI2 pair ABI");
static_assert(sizeof(struct tegu_ui2_info)==664,"UI2 parsed ABI");
static_assert(sizeof(struct tegu_ui2_bind_request)==520,"UI2 request ABI");
static_assert(sizeof(struct tegu_ui2_binding)==512,"UI2 binding ABI");
#else
_Static_assert(sizeof(struct tegu_ui2_stage)==40,"UI2 stage ABI");
_Static_assert(sizeof(struct tegu_ui2_pair)==152,"UI2 pair ABI");
_Static_assert(sizeof(struct tegu_ui2_info)==664,"UI2 parsed ABI");
_Static_assert(sizeof(struct tegu_ui2_bind_request)==520,"UI2 request ABI");
_Static_assert(sizeof(struct tegu_ui2_binding)==512,"UI2 binding ABI");
#endif

#if defined(__cplusplus)
extern "C" {
#endif
int tegu_ui2_parse(const void *,size_t,struct tegu_ui2_info *);
int tegu_ui2_copy_code(const struct tegu_ui2_info *,const void *,size_t,void *,size_t,void *,size_t);
int tegu_ui2_bind(const struct tegu_ui2_info *,const struct tegu_ui2_bind_request *,struct tegu_ui2_binding *);
#if defined(__cplusplus)
}
#endif
#endif
