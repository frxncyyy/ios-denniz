#ifndef TEGU_FIXED_UI_RESOURCES_H
#define TEGU_FIXED_UI_RESOURCES_H
#include <stdint.h>
#include "native_package_ui1.h"

#define TEGU_FIXED_UI_STATE_VERSION 2u
#define TEGU_FIXED_UI_TABLE_BYTES 64u
#define TEGU_FIXED_UI_DESCRIPTOR_BYTES 128u
#define TEGU_FIXED_UI_PUSH_BYTES 12u
#define TEGU_FIXED_UI_CONSTANT_BYTES 8u

/* RenderResource stages name frontend resource namespaces. */
#define TEGU_FIXED_UI_RESOURCE_STAGE_VERTEX 1u
#define TEGU_FIXED_UI_RESOURCE_STAGE_FRAGMENT 2u
/* Push stages are Vulkan shader-stage masks and are a distinct namespace. */
#define TEGU_FIXED_UI_PUSH_STAGE_VERTEX 1u
#define TEGU_FIXED_UI_PUSH_STAGE_FRAGMENT 16u

struct tegu_fixed_ui_span { uint64_t address, bytes; };
struct tegu_fixed_ui_allocation_span {
 uint64_t token, object_id, address, bytes;
};
struct tegu_fixed_ui_resource_input {
 uint32_t version;
 const struct tegu_ui1_binding *binding;
 struct tegu_fixed_ui_span table0, resource_table, descriptors;
 /* Kernel-resolved GPU allocations corresponding to binding.resources[0..1]. */
 struct tegu_fixed_ui_allocation_span vertex_buffer, fragment_buffer;
};
struct tegu_fixed_ui_resource_state {
 uint64_t resource_table_handle;
 uint8_t resource_table[TEGU_FIXED_UI_TABLE_BYTES];
 uint8_t descriptors[TEGU_FIXED_UI_DESCRIPTOR_BYTES];
 uint8_t push_bytes[TEGU_FIXED_UI_PUSH_BYTES];
 uint8_t constant_blob[TEGU_FIXED_UI_CONSTANT_BYTES];
 uint32_t vertex_resource_stage, fragment_resource_stage;
 uint32_t vertex_push_stage, fragment_push_stage;
 uint32_t vertex_binding, fragment_binding;
 uint32_t vertex_push_offset, vertex_push_length;
 uint32_t fragment_push_offset, fragment_push_length;
};

/* Packs a validated UI1 binding: table1/index0=VS Metal buffer2,
 * index1=FS Metal buffer1 and index2=the internal padded constant blob.
 * Descriptor
 * offsets, lengths and residual push bytes come from tegu_ui1_bind. Buffer
 * spans are kernel-resolved allocation bases and may name the same allocation.
 * The 128-byte descriptor allocation holds three 32-byte descriptors followed
 * by the 16-byte padded constant payload at offset96.
 * Output allocations must remain disjoint from every input allocation and one
 * another. Failure leaves out untouched. */
int tegu_fixed_ui_resource_state_build(const struct tegu_fixed_ui_resource_input *,
 struct tegu_fixed_ui_resource_state *);
#endif
