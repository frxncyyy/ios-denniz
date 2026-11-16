#ifndef TEGU_FIXED_UI_NATIVE_PACKAGE_UI1_WRITER_H
#define TEGU_FIXED_UI_NATIVE_PACKAGE_UI1_WRITER_H
#include "air_bridge.h"
#include "mali_graphics_compiler.h"
#if defined(__cplusplus)
extern "C" {
#endif
/* The AIR plan is a draw-resolved attestation.  Its allocation offsets and
 * push values are validated but remain draw state; only its exact span layout
 * and eight-byte stage constant enter the reusable package. */
int tegu_ui1_build(const struct tegu_mali_graphics *,const struct tegu_mali_graphics *,
 const struct tegu_mali_graphics_pair2 *,const struct tegu_mali_fixed_ui_plan *,
 const TeguAIRFixedUiR80Plan *,uint32_t,uint8_t **,size_t *);
void tegu_ui1_free(uint8_t **,size_t *);
#if defined(__cplusplus)
}
#endif
#endif
