#ifndef TEGU_FIXED_UI_TEXTURE_NATIVE_PACKAGE_UI2_WRITER_H
#define TEGU_FIXED_UI_TEXTURE_NATIVE_PACKAGE_UI2_WRITER_H
#include "air_bridge.h"
#include "mali_graphics_compiler.h"
#if defined(__cplusplus)
extern "C" {
#endif
int tegu_ui2_build(const struct tegu_mali_graphics *,const struct tegu_mali_graphics *,
 const struct tegu_mali_graphics_pair2 *,const struct tegu_mali_fixed_ui_texture_r85_plan *,
 const TeguAIRFixedUiR85Plan *,uint8_t **,size_t *);
void tegu_ui2_free(uint8_t **,size_t *);
#if defined(__cplusplus)
}
#endif
#endif
