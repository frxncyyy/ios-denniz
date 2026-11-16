/* Golden page from pinned Mesa 81daea6 genxml/v10_pack.h pack macros only.
 * Field values mirror tools/tegu/gpu_texture_image.py populate() with the
 * sampler normalized_coordinates left at its v10 header default (true). */
#include <string.h>
#include "genxml/v10_pack.h"
#include "texture_state_mesa_golden.h"
void tegu_texture_mesa_golden_page(uint64_t d,uint64_t t,unsigned char page[4096]) {
 _Alignas(64) unsigned char p[4096];
 memset(p,0,sizeof(p));
 pan_cast_and_pack(p,RESOURCE,cfg) {cfg.address=d+0x100;cfg.size=64;}
 pan_cast_and_pack(p+0x100,SAMPLER,cfg) {cfg.minify_nearest=true;cfg.magnify_nearest=true;}
 pan_cast_and_pack(p+0x120,TEXTURE,cfg) {
  cfg.dimension=MALI_TEXTURE_DIMENSION_2D;
  cfg.format=(MALI_RGBA8_UNORM<<12)|MALI_RGB_COMPONENT_ORDER_RGBA;
  cfg.width=16;cfg.height=16;
  cfg.swizzle=MALI_CHANNEL_R|(MALI_CHANNEL_G<<3)|(MALI_CHANNEL_B<<6)|(MALI_CHANNEL_A<<9);
  cfg.surfaces=d+0x140;
 }
 pan_cast_and_pack(p+0x140,GENERIC_PLANE,cfg) {
  cfg.clump_ordering=MALI_CLUMP_ORDERING_LINEAR;cfg.clump_format=MALI_CLUMP_FORMAT_RAW32;
  cfg.size=1024;cfg.pointer=t;cfg.row_stride=64;cfg.slice_stride=1024;cfg.width=16;cfg.height=16;
 }
 memcpy(page,p,sizeof(p));
}
/* Decoded flags exposed so the test can assert the header defaults it relies on. */
int tegu_texture_mesa_sampler_header_normalized(void) {
 struct MALI_SAMPLER s={MALI_SAMPLER_header};return s.normalized_coordinates;
}
