#ifndef TEGU_COMPOSITOR_LOAD_BUNDLE_V6_H
#define TEGU_COMPOSITOR_LOAD_BUNDLE_V6_H
#include "compositor_load_mapping.h"
#include "compositor_render_state_v6.h"

struct tegu_compositor_load_assets_v6 {
 const unsigned char *fbd_parameters; size_t fbd_bytes; /* Mesa clear+load, 128 bytes */
 const unsigned char *resource_page; size_t resource_bytes; /* Mesa texture/resource page */
 const unsigned char *dcd_page; size_t dcd_bytes; /* Mesa PAN_ARCH=10 DCD page */
 const unsigned char *shader; size_t shader_bytes; /* qualified LOAD_IMAGE shader */
 const unsigned char *texture_plane; size_t texture_plane_bytes; /* Mesa texture+plane */
 const unsigned char *rt; size_t rt_bytes; /* Mesa BGRA8 linear RT */
 const unsigned char *visibility; size_t visibility_bytes; /* Mesa CS_FLUSH_CACHE2 + WAIT */
};
struct tegu_compositor_load_bundle_v6 {
 struct tegu_render_state draw;
 unsigned char resource_page[4096];
 unsigned char dcd_page[4096];
 unsigned char shader_page[4096];
 uint32_t page_count;
 uint8_t resource_readonly_nx,dcd_readonly_nx,shader_readonly_x;
};
/* Host/shared builder prototype. Inputs are emitted by pinned Mesa PAN_ARCH=10
 * packers and the raw r7-qualified shader. Relocations are explicit and all
 * span/alias/legacy-RT checks complete before the caller's output is modified. */
int tegu_compositor_load_bundle_build_v6(
 const struct tegu_render_state *clear_draw,
 const struct tegu_render_addresses *addresses,
 const struct tegu_render_target_layout *layout,
 const struct tegu_load_mapping *mapping,
 const struct tegu_compositor_load_assets_v6 *assets,
 size_t vertex_code_bytes,size_t fragment_code_bytes,
 struct tegu_compositor_load_bundle_v6 *out);
#endif
