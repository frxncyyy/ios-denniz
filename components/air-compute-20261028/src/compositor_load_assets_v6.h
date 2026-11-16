#ifndef TEGU_COMPOSITOR_LOAD_ASSETS_V6_H
#define TEGU_COMPOSITOR_LOAD_ASSETS_V6_H
#include "compositor_load_bundle_v6.h"

struct tegu_compositor_load_asset_scratch_v6 {
 unsigned char fbd[128], texture_plane[64], rt[64], visibility[16];
};

/* Materialize only the dimension/stride/target fields which Mesa's pinned
 * PAN_ARCH=10 emitters vary. `resource_page`, `dcd_page`, and `shader_page`
 * are caller-owned backing (kernel integration supplies retained buffers;
 * host tests use heap storage), never large stack arrays. */
int tegu_compositor_load_assets_build_v6(
 const struct tegu_render_target_layout *, uint64_t target_va,
 unsigned char resource_page[4096], unsigned char dcd_page[4096],
 unsigned char shader_page[4096],
 struct tegu_compositor_load_asset_scratch_v6 *,
 struct tegu_compositor_load_assets_v6 *);
#endif
