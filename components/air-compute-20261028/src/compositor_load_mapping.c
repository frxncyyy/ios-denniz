#include "compositor_load_mapping.h"
#define PAGE 4096ull
int tegu_load_mapping_build(const struct tegu_load_mapping_input *in,struct tegu_load_mapping *out)
{
 if(!in||!out||!in->aperture_va||(in->aperture_va&(PAGE-1))||!in->target_bytes||in->target_bytes>(16ull<<20)||in->scratch_pages<=TEGU_LOAD_PRELOAD_SCRATCH_PAGE||!in->max_pages)return -1;
 uint64_t target_pages=(in->target_bytes+PAGE-1)/PAGE;
 uint64_t first=(uint64_t)in->scratch_pages+target_pages+in->ordinary_pages;
 /* Resource and DCD pages are additional RO/NX leaves. The executable
  * preload image is placed in scratch page 14, which the backing maps RO/X. */
 uint64_t total=first+2;
 if(target_pages>UINT32_MAX||total>in->max_pages||total>(1ull<<20)||in->aperture_va+total*PAGE<in->aperture_va||in->aperture_va+total*PAGE>=(1ull<<48))return -2;
 struct tegu_load_mapping tmp={0};tmp.resource_va=in->aperture_va+first*PAGE;tmp.dcd_va=tmp.resource_va+PAGE;tmp.shader_va=in->aperture_va+(uint64_t)TEGU_LOAD_PRELOAD_SCRATCH_PAGE*PAGE;tmp.target_pages=(uint32_t)target_pages;tmp.total_pages=(uint32_t)total;
 tmp.page_role[0]=TEGU_LOAD_MAP_RO_NX;tmp.page_role[1]=TEGU_LOAD_MAP_RO_NX;tmp.page_role[2]=TEGU_LOAD_MAP_RO_X;*out=tmp;return 0;
}
