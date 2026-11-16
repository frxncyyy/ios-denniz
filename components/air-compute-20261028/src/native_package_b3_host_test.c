#include "native_package_b3.h"
#include "native_package_b3_writer.h"
#include "native_package_v7.h"
#include "native_package_v7_writer.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p,uint32_t x) {
 p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);p[2]=(uint8_t)(x>>16);p[3]=(uint8_t)(x>>24);
}
static void refused(const uint8_t *p,size_t n) {
 struct tegu_native_b3_info got,sentinel;
 memset(&sentinel,0xa5,sizeof(sentinel));got=sentinel;
 assert(tegu_native_b3_parse(p,n,&got)!=0);
 assert(!memcmp(&got,&sentinel,sizeof(got)));
}
int main(void) {
 uint8_t *vs=(uint8_t *)malloc(TEGU_NATIVE_B3_VS_BYTES);
 uint8_t *fs=(uint8_t *)malloc(TEGU_NATIVE_B3_FS_BYTES);
 assert(vs&&fs);
 for(size_t i=0;i<TEGU_NATIVE_B3_VS_BYTES;i++)vs[i]=(uint8_t)(i*17u+3u);
 for(size_t i=0;i<TEGU_NATIVE_B3_FS_BYTES;i++)fs[i]=(uint8_t)(i*29u+7u);
 uint8_t *package=NULL;size_t bytes=0;
 assert(!tegu_native_b3_build(vs,TEGU_NATIVE_B3_VS_BYTES,fs,TEGU_NATIVE_B3_FS_BYTES,&package,&bytes));
 assert(package&&bytes==TEGU_NATIVE_B3_PACKAGE_BYTES);
 struct tegu_native_b3_info info;
 assert(!tegu_native_b3_parse(package,bytes,&info));
 assert(info.version==TEGU_NATIVE_B3_VERSION&&info.sample_count==4&&info.stage[0].bytes==1152&&info.stage[1].bytes==8704);
 assert(info.ubo_window[0]==64&&info.ubo_window[1]==16&&info.ubo_window[2]==82&&info.ubo_window[3]==2);
 assert(info.ubo_declared_bytes[0]==80&&info.ubo_declared_bytes[1]==32&&info.ubo_declared_bytes[2]==112&&info.ubo_declared_bytes[3]==32);
 assert(info.blend.version==1&&!info.blend.enabled&&info.blend.rgb_op==0&&info.blend.rgb_src==1&&
  !info.blend.rgb_dst&&info.blend.alpha_op==0&&info.blend.alpha_src==1&&!info.blend.alpha_dst&&info.blend.write_mask==15);

 struct tegu_native_b3_binding binding={0};binding.table1_handle=0x12345004;
 const uint32_t length[4]={80,32,112,32},offset[4]={16,16,24,30};
 for(unsigned i=0;i<4;i++){binding.ubo_length[i]=length[i];binding.ubo_offset[i]=offset[i];}
 put32(binding.vertex_push,0);put32(binding.vertex_push+4,0);
 put32(binding.fragment_push+64,8);put32(binding.fragment_push+68,14);
 put32(binding.fragment_push+72,0xd241f2);
 struct tegu_native_b3_checked checked;
 assert(!tegu_native_b3_bind(package,bytes,&binding,&checked));
 assert(checked.ubo_window[2]==82&&checked.ubo_base_offset[2]==16);
 struct tegu_native_b3_checked keep=checked;binding.ubo_length[2]=105;
 assert(tegu_native_b3_bind(package,bytes,&binding,&checked)!=0&&!memcmp(&checked,&keep,sizeof(keep)));

 uint8_t *scratch=(uint8_t *)malloc(TEGU_NATIVE_B3_SCRATCH_BYTES+32);assert(scratch);
 memset(scratch,0xa5,TEGU_NATIVE_B3_SCRATCH_BYTES+32);
 struct tegu_native_b3_layout layout;
 assert(!tegu_native_b3_stage_copy(package,bytes,scratch,TEGU_NATIVE_B3_SCRATCH_BYTES,&layout));
 assert(layout.vs_offset==0x2000&&layout.vs_pages==2&&layout.fs_offset==0x30000&&layout.fs_pages==3);
 assert(!memcmp(scratch+layout.vs_offset,vs,TEGU_NATIVE_B3_VS_BYTES));
 assert(!memcmp(scratch+layout.fs_offset,fs,TEGU_NATIVE_B3_FS_BYTES));
 for(uint32_t i=layout.fs_offset+TEGU_NATIVE_B3_FS_BYTES;i<layout.fs_offset+3*4096u;i++)assert(!scratch[i]);
 for(uint32_t i=TEGU_NATIVE_B3_SCRATCH_BYTES;i<TEGU_NATIVE_B3_SCRATCH_BYTES+32;i++)assert(scratch[i]==0xa5);

 static const struct {uint32_t offset,value;} mixed[]={
  {4,7},{20,25600},{256+19*4,32},{256+26*4,16},{256+29*4,48}
 };
 for(unsigned i=0;i<sizeof(mixed)/sizeof(mixed[0]);i++) {
  uint8_t *bad=(uint8_t *)malloc(bytes);assert(bad);memcpy(bad,package,bytes);
  put32(bad+mixed[i].offset,mixed[i].value);refused(bad,bytes);free(bad);
 }

 uint8_t *v7fs=(uint8_t *)calloc(1,TEGU_NATIVE_V7_FS_BYTES),*v7=NULL;size_t v7n=0;
 assert(v7fs&&!tegu_native_v7_build_with_blend(vs,TEGU_NATIVE_V7_VS_BYTES,v7fs,TEGU_NATIVE_V7_FS_BYTES,
  TEGU_NATIVE_V7_BLEND_DISABLED,&v7,&v7n));
 refused(v7,v7n);
 struct tegu_native_v7_info v7info;
 assert(tegu_native_v7_parse(package,bytes,&v7info)!=0);
 tegu_native_v7_free(&v7,&v7n);free(v7fs);

 uint8_t *unchanged=(uint8_t *)(uintptr_t)0x12345678;size_t unchanged_bytes=99;
 assert(tegu_native_b3_build(vs,TEGU_NATIVE_B3_VS_BYTES-1,fs,TEGU_NATIVE_B3_FS_BYTES,
  &unchanged,&unchanged_bytes)==-1&&unchanged==(uint8_t *)(uintptr_t)0x12345678&&unchanged_bytes==99);
 free(scratch);tegu_native_b3_free(&package,&bytes);free(vs);free(fs);
 assert(!package&&!bytes);
 puts("PASS exact single-pass-blur-3 package, bind/staging, mixed-class and v7 cross-class refusals");
 return 0;
}
