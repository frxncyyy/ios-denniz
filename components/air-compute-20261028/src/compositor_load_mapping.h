#ifndef TEGU_COMPOSITOR_LOAD_MAPPING_H
#define TEGU_COMPOSITOR_LOAD_MAPPING_H
#include <stdint.h>
enum { TEGU_LOAD_MAP_RO_NX=1, TEGU_LOAD_MAP_RO_X=2 };
#define TEGU_LOAD_APERTURE_VA UINT64_C(0x06000000)
#define TEGU_LOAD_PRELOAD_SCRATCH_PAGE 14u
struct tegu_load_mapping_input { uint64_t aperture_va,target_bytes; uint32_t scratch_pages,ordinary_pages,max_pages; };
struct tegu_load_mapping { uint64_t resource_va,dcd_va,shader_va; uint32_t target_pages,total_pages; uint8_t page_role[3]; };
int tegu_load_mapping_build(const struct tegu_load_mapping_input *,struct tegu_load_mapping *);
#endif
