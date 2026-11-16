#ifndef TEGU_FIXED_UI_NATIVE_PACKAGE_UI1_H
#define TEGU_FIXED_UI_NATIVE_PACKAGE_UI1_H
#include <stddef.h>
#include <stdint.h>

/* Additive package class for fixed_vert_lph_spc + fixed_frag_lph_cph.  TFU1
 * is deliberately distinct from TGR1..8 and TAC1 so an older parser cannot
 * interpret its two stage-owned UBOs as a legacy compositor table. */
#define TEGU_UI1_VERSION 1u
#define TEGU_UI1_HEADER_BYTES 768u
#define TEGU_UI1_STAGE_MAX 65536u
#define TEGU_UI1_ATTRIBUTE_COUNT 2u
#define TEGU_UI1_RESOURCE_COUNT 3u
#define TEGU_UI1_PUSH_COUNT 2u
#define TEGU_UI1_CONSTANT_BYTES 8u
#define TEGU_UI1_FORMAT_BGRA8 80u
#define TEGU_UI1_TOPOLOGY_TRIANGLE 3u
#define TEGU_UI1_LOAD_DONTCARE 0u
#define TEGU_UI1_LOAD 1u
#define TEGU_UI1_LOAD_CLEAR 2u
#define TEGU_UI1_STORE_DONTCARE 0u
#define TEGU_UI1_STORE 1u
#define TEGU_UI1_OK 0
#define TEGU_UI1_EINVAL -1
#define TEGU_UI1_EFORMAT -2
#define TEGU_UI1_ECONTRACT -3
#define TEGU_UI1_ERANGE -4
#define TEGU_UI1_EALIAS -5

struct tegu_ui1_stage {
 uint32_t bytes,offset,stage,registers,tls,fau_words,sysval_mask,push_bytes;
 uint64_t preload;
};
struct tegu_ui1_varying {uint32_t location,offset,components,alu_type;};
struct tegu_ui1_pair {
 uint32_t version,flags,position_offset,position_size,point_position_offset,point_position_size;
 uint32_t secondary_offset,secondary_size,secondary_registers,generic_size_bytes;
 uint64_t secondary_preload;
 uint32_t vertex_attribute_mask,vertex_varying_mask,fragment_varying_mask,fragment_uses_ld_var;
 uint32_t noperspective_mask,needs_extended_fifo,point_size_section,point_size_offset,point_size_alu_type,varying_count;
 struct tegu_ui1_varying varyings[4];
};
struct tegu_ui1_attribute {
 uint32_t location,format,offset,buffer_index,stride,input_rate,divisor;
};
struct tegu_ui1_resource {
 uint32_t backend_stage,metal_stage,metal_slot,descriptor_set,binding,table,index,declared_bytes,window_bytes,push_offset;
};
struct tegu_ui1_push_span {uint32_t stage,offset,length,reserved;};
struct tegu_ui1_info {
 uint32_t version,total_bytes,target_format,target_write_mask,target_blend_enabled,sample_count,topology;
 uint32_t load_action_mask,store_action_mask,constant_stage,constant_length;
 struct tegu_ui1_stage stages[2];
 struct tegu_ui1_pair pair;
 struct tegu_ui1_attribute attributes[TEGU_UI1_ATTRIBUTE_COUNT];
 struct tegu_ui1_resource resources[TEGU_UI1_RESOURCE_COUNT];
 struct tegu_ui1_push_span pushes[TEGU_UI1_PUSH_COUNT];
 uint8_t constant[TEGU_UI1_CONSTANT_BYTES];
};
struct tegu_ui1_allocation {
 uint64_t token,object_id,length,offset;
};
struct tegu_ui1_target {
 struct tegu_ui1_allocation allocation;
 uint32_t format,width,height,row_bytes;
};
struct tegu_ui1_buffer {
 struct tegu_ui1_allocation allocation;
 uint32_t metal_stage,metal_slot;
 /* Exact allocation-relative range returned by AIR instantiate_draw.  It can
  * be wider than the shader window because the descriptor base is aligned. */
 uint64_t descriptor_offset,descriptor_length;
};
struct tegu_ui1_pass {
 uint32_t render_x,render_y,render_width,render_height;
 uint32_t viewport_x,viewport_y,viewport_width,viewport_height;
 uint32_t scissor_x,scissor_y,scissor_width,scissor_height;
 uint32_t load_action,store_action,clear_color[4];
};
struct tegu_ui1_bind_request {
 uint32_t version,width,height,topology,indexed,vertex_start,vertex_count,instance_count,base_instance;
 struct tegu_ui1_target target;
 struct tegu_ui1_allocation vertex;
 uint32_t vertex_slot,vertex_stride;
 struct tegu_ui1_buffer resources[2];
 uint8_t vertex_push[4],fragment_push[8];
 struct tegu_ui1_pass pass;
};
struct tegu_ui1_bound_resource {
 uint64_t token,object_id,descriptor_offset;
 uint32_t descriptor_bytes,window_bytes,metal_stage,metal_slot,table,index,push_offset,push_residual;
};
struct tegu_ui1_binding {
 uint32_t width,height,target_format,target_row_bytes,load_action,store_action,clear_color[4];
 uint64_t target_token,target_object_id,target_offset,target_bytes;
 uint64_t vertex_token,vertex_object_id,vertex_offset,vertex_bytes;
 uint32_t vertex_stride,topology,vertex_start,vertex_count,instance_count,base_instance;
 uint32_t render[4],viewport[4],scissor[4];
 struct tegu_ui1_bound_resource resources[2];
 uint8_t vertex_push[4],fragment_push[8],constant[8];
 uint32_t constant_stage;
};

#if defined(__cplusplus)
static_assert(sizeof(struct tegu_ui1_stage)==40,"UI1 stage ABI");
static_assert(sizeof(struct tegu_ui1_pair)==152,"UI1 pair ABI");
static_assert(sizeof(struct tegu_ui1_attribute)==28,"UI1 attribute ABI");
static_assert(sizeof(struct tegu_ui1_resource)==40,"UI1 resource ABI");
static_assert(sizeof(struct tegu_ui1_info)==496,"UI1 parsed ABI");
static_assert(sizeof(struct tegu_ui1_bind_request)==328,"UI1 bind request ABI");
static_assert(sizeof(struct tegu_ui1_binding)==312,"UI1 binding ABI");
#else
_Static_assert(sizeof(struct tegu_ui1_stage)==40,"UI1 stage ABI");
_Static_assert(sizeof(struct tegu_ui1_pair)==152,"UI1 pair ABI");
_Static_assert(sizeof(struct tegu_ui1_attribute)==28,"UI1 attribute ABI");
_Static_assert(sizeof(struct tegu_ui1_resource)==40,"UI1 resource ABI");
_Static_assert(sizeof(struct tegu_ui1_info)==496,"UI1 parsed ABI");
_Static_assert(sizeof(struct tegu_ui1_bind_request)==328,"UI1 bind request ABI");
_Static_assert(sizeof(struct tegu_ui1_binding)==312,"UI1 binding ABI");
#endif

#if defined(__cplusplus)
extern "C" {
#endif
int tegu_ui1_parse(const void *,size_t,struct tegu_ui1_info *);
int tegu_ui1_copy_code(const struct tegu_ui1_info *,const void *,size_t,
 void *,size_t,void *,size_t);
int tegu_ui1_bind(const struct tegu_ui1_info *,const struct tegu_ui1_bind_request *,
 struct tegu_ui1_binding *);
#if defined(__cplusplus)
}
#endif
#endif
