/* Native IDVS builder: one 16x16 RGBA8 clear+triangle pass; TGR1/2 have no resources
 * or varyings, TGR3 adds one paired IDVS varying program. Caller maps/retains every range and zeroes geometry/chunk memory
 * before a new pass. Does not submit, map, publish caches or own allocations. */
#ifndef TEGU_RENDER_STATE_H
#define TEGU_RENDER_STATE_H
#include "render_pipeline_format.h"
struct tegu_render_addresses {
 uint64_t descriptors,tiler,geometry,chunk,target,vertex_code,fragment_code;
 uint32_t clear_rgba;
 uint32_t chunk_bytes; /* 0 preserves the exact legacy 128 KiB descriptor */
 uint32_t hierarchy_mask; /* 0=>legacy1; fixed 2 MiB path requires 0x82 */
};
struct tegu_render_state {
 unsigned char descriptors[4096],tiler[256];
 uint64_t commands[128];
 uint32_t command_count;
};
/* Selector 15 dimension contract. Surface bytes are derived from stride*height;
 * requesters provide no GPU address and the target byte length is checked by
 * the kernel against its owned allocation before state construction. */
struct tegu_render_target_layout {
 uint32_t version,width,height,row_stride;
 uint64_t target_length;
};
#define TEGU_RENDER_TARGET_LAYOUT_VERSION 1u
#define TEGU_RENDER_TARGET_MAX_DIMENSION 256u
#define TEGU_RENDER_TARGET_MAX_BYTES (1u<<20)
int tegu_render_target_layout_validate(const struct tegu_render_target_layout *,uint64_t *surface_bytes);
/* Logical RGBA tile/clear channels; writeback swizzle selects RGBA or BGRA
 * memory byte order. */
#define TEGU_RENDER_CLEAR_LOGICAL_RGBA8 1u
struct tegu_render_target_format {
 uint32_t metal_format,internal_format,writeback_format,swizzle,word0,word1,clear_layout;
 unsigned char storage_channel[4];
};
int tegu_render_target_format(uint32_t metal_format,struct tegu_render_target_format *);
/* GPU addresses are kernel-resolved aliases, never raw user input. Shader code
 * at each alias must equal the corresponding package stage. Exact resources:
 * descriptors4K, tiler4K, geometry64K, chunk128K (zero64-byte header), target1K.
 * Input spans and output must be valid and nonoverlapping.
 * Output unchanged on refusal. RGBA8Unorm(70)/BGRA8Unorm(80) use
 * tegu_render_target_format; other formats/passes need separate handling. */
int tegu_render_build(const void *package,size_t size,
 const struct tegu_render_addresses *,struct tegu_render_state *);
/* Same qualified draw shape, with an owned vertex descriptor page generated
 * by tegu_vertex_fetch_build. The resource table must match a TGR2/TGR3 pipeline.
 * TGR3 vertex_code is the full paired VS binary: the position program starts at
 * position_offset and the IDVS varying program at secondary_offset, both bound
 * through the same alias. Varying memory comes from the tiler heap. */
int tegu_render_build_vertex(const void *package,size_t size,
 const struct tegu_render_addresses *,uint64_t vertex_resource_table,struct tegu_render_state *);
/* Fragment resource table is mandatory ONLY for TGR5, a distinct descriptor
 * page from every other consumed range. Caller retains the sampled bytes and
 * descriptor page and maps both read-only/non-executable through completion. */
int tegu_render_build_resources(const void *package,size_t size,
 const struct tegu_render_addresses *,uint64_t vertex_resource_table,
 uint64_t fragment_resource_table,struct tegu_render_state *);
/* Research-only variable-size sibling. Existing selectors keep the fixed
 * 16x16 ABI above; no production or hardware qualification is implied. */
int tegu_render_build_target_resources(const void *package,size_t size,
 const struct tegu_render_addresses *,uint64_t vertex_resource_table,
 uint64_t fragment_resource_table,const struct tegu_render_target_layout *,
 struct tegu_render_state *);
#endif
