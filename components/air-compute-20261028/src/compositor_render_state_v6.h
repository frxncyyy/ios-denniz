#ifndef TEGU_COMPOSITOR_RENDER_STATE_V6_H
#define TEGU_COMPOSITOR_RENDER_STATE_V6_H
#include "render_state.h"
#include <stdint.h>
int tegu_compositor_render_build_v6(const void *,size_t,
 const struct tegu_render_addresses *,uint64_t,
 const struct tegu_render_target_layout *,uint32_t,
 const uint8_t[128],const uint8_t[128],struct tegu_render_state *);
int tegu_compositor_render_build_v7(const void *,size_t,
 const struct tegu_render_addresses *,uint64_t,
 const struct tegu_render_target_layout *,uint32_t,
 const uint8_t[128],const uint8_t[128],struct tegu_render_state *);
int tegu_compositor_render_build_b3(const void *,size_t,
 const struct tegu_render_addresses *,uint64_t,
 const struct tegu_render_target_layout *,uint32_t,
 const uint8_t[128],const uint8_t[128],struct tegu_render_state *);
#endif
