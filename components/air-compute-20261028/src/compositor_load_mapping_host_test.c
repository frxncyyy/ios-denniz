#include "compositor_load_mapping.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void test(uint32_t w,uint32_t h,uint32_t stride,uint32_t ordinary)
{
 assert(stride>=w*4);uint64_t bytes=(uint64_t)stride*h;struct tegu_load_mapping_input in={0x100000000ull,bytes,64,ordinary,8192};
 struct tegu_load_mapping m,before;memset(&m,0xa5,sizeof(m));assert(!tegu_load_mapping_build(&in,&m));
 assert(m.target_pages==(bytes+4095)/4096);assert(m.total_pages==64+m.target_pages+ordinary+2);
 assert(m.resource_va==in.aperture_va+(uint64_t)(64+m.target_pages+ordinary)*4096);
 assert(m.dcd_va==m.resource_va+4096&&m.shader_va==in.aperture_va+14*4096);
 assert((m.resource_va&4095)==0&&(m.dcd_va&4095)==0&&(m.shader_va&4095)==0);
 assert(m.page_role[0]==TEGU_LOAD_MAP_RO_NX&&m.page_role[1]==TEGU_LOAD_MAP_RO_NX&&m.page_role[2]==TEGU_LOAD_MAP_RO_X);
 /* Failure paths do not partially publish a mapping. */
 before=m;in.max_pages=m.total_pages-1;assert(tegu_load_mapping_build(&in,&m));assert(!memcmp(&m,&before,sizeof(m)));
}
int main(void)
{
 test(16,16,64,1);test(32,32,128,4);test(256,256,1024,32);test(1080,2424,4352,64);test(4096,1024,16384,128);
 struct tegu_load_mapping m,b;memset(&m,0x5a,sizeof(m));b=m;
 struct tegu_load_mapping_input bad={0x100000001ull,1024,64,0,8192};assert(tegu_load_mapping_build(&bad,&m));assert(!memcmp(&m,&b,sizeof(m)));
 struct tegu_load_mapping_input over={0x100000000ull,(16ull<<20)+4096,64,0,8192};assert(tegu_load_mapping_build(&over,&m));assert(!memcmp(&m,&b,sizeof(m)));
 puts("mapping-plan oracle: dynamic target pages, nonoverlap, RO/NX resource+DCD, RO/X shader, refusal preserves output");
}
