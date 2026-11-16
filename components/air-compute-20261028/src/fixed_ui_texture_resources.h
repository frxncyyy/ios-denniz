#ifndef TEGU_FIXED_UI_TEXTURE_RESOURCES_H
#define TEGU_FIXED_UI_TEXTURE_RESOURCES_H
#include <stdint.h>
#include "native_package_ui2.h"
#define TEGU_FIXED_UI_TEXTURE_STATE_VERSION 1u
#define TEGU_FIXED_UI_TEXTURE_TABLE_BYTES 64u
#define TEGU_FIXED_UI_TEXTURE_DESCRIPTOR_BYTES 256u
#define TEGU_FIXED_UI_TEXTURE_PLANE_BYTES 32u
#define TEGU_FIXED_UI_TEXTURE_PUSH_BYTES 20u
struct tegu_fixed_ui_texture_span {uint64_t address,bytes;};
struct tegu_fixed_ui_texture_allocation_span {uint64_t token,object_id,address,bytes;};
struct tegu_fixed_ui_texture_resource_input {
 uint32_t version;const struct tegu_ui2_binding *binding;
 struct tegu_fixed_ui_texture_span table0,resource_table,descriptors,plane;
 struct tegu_fixed_ui_texture_allocation_span buffers[4],texture;
};
struct tegu_fixed_ui_texture_resource_state {
 uint64_t resource_table_handle;
 uint8_t resource_table[TEGU_FIXED_UI_TEXTURE_TABLE_BYTES];
 uint8_t descriptors[TEGU_FIXED_UI_TEXTURE_DESCRIPTOR_BYTES];
 uint8_t plane[TEGU_FIXED_UI_TEXTURE_PLANE_BYTES];
 uint8_t push_bytes[TEGU_FIXED_UI_TEXTURE_PUSH_BYTES],constant_blob[8],specialization[4];
 uint32_t vertex_push_offset,vertex_push_length,fragment_push_offset,fragment_push_length;
};
/* Packs table1 entries 0..3 as dynamic UBOs, 4 as the bounded synthetic
 * constant, 5 as the private linear BGRA texture and 6 as its constexpr
 * nearest/clamp sampler. All addresses are kernel-resolved retained spans. */
int tegu_fixed_ui_texture_resource_state_build(const struct tegu_fixed_ui_texture_resource_input *,struct tegu_fixed_ui_texture_resource_state *);
#endif
