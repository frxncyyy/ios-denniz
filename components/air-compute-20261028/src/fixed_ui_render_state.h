#ifndef TEGU_FIXED_UI_RENDER_STATE_H
#define TEGU_FIXED_UI_RENDER_STATE_H
#include "render_state.h"
#include "native_package_ui1.h"
int tegu_fixed_ui_render_build(const struct tegu_ui1_info *,
 const struct tegu_ui1_binding *,const struct tegu_render_addresses *,uint64_t,
 const struct tegu_render_target_layout *,struct tegu_render_state *);
#endif
