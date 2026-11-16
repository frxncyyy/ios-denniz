#include "attachment_clear_rt1_state.h"
#include "attachment_clear_r8_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PAN_ARCH 10
#include "genxml/v10_pack.h"

#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;} } while(0)

static uint8_t *read_file(const char *path,size_t expected)
{
 FILE *f=fopen(path,"rb");if(!f)return NULL;
 uint8_t *p=(uint8_t *)malloc(expected);if(!p){fclose(f);return NULL;}
 if(fread(p,1,expected,f)!=expected||fgetc(f)!=EOF){free(p);p=NULL;}
 fclose(f);return p;
}
static uint32_t get32(const void *p){uint32_t v;memcpy(&v,p,4);return v;}
static uint64_t get64(const void *p){uint64_t v;memcpy(&v,p,8);return v;}
static int heap_dynamic_byte(size_t i){return (i>=0x1008&&i<0x100c)||(i>=0x1084&&i<0x10a0);}

int main(int argc,char **argv)
{
 CHECK(argc==4);
 uint8_t *package=read_file(argv[1],TEGU_AC1_PACKAGE_BYTES);
 uint8_t *r8_aperture=read_file(argv[2],0x77000);
 uint8_t *r8_wire=read_file(argv[3],TEGU_AC1_R8_COMMAND_COUNT*8u);
 CHECK(package&&r8_aperture&&r8_wire);
 struct tegu_ac1_info info;CHECK(!tegu_ac1_parse(package,TEGU_AC1_PACKAGE_BYTES,&info));
 uint8_t *scratch=(uint8_t *)malloc(TEGU_AC1_RT1_SCRATCH_BYTES);
 uint8_t *resources=(uint8_t *)malloc(TEGU_AC1_RT1_RESOURCE_BYTES);
 CHECK(scratch&&resources);
 struct tegu_ac1_rt1_map map;
 struct tegu_ac1_rt1_commands commands;
 struct tegu_ac1_rt1_layout small={TEGU_AC1_RT1_LAYOUT_VERSION,16,16,64,4096};
 CHECK(!tegu_ac1_rt1_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,&small,0x11223344,
  r8_aperture+0x71200,16,r8_aperture+0x71300,320,scratch,TEGU_AC1_RT1_SCRATCH_BYTES,
  resources,TEGU_AC1_RT1_RESOURCE_BYTES,&map,&commands));
 CHECK(map.target_va==0x06070000&&map.resource_va==0x06071000&&map.heap_va==0x06072000);
 CHECK(map.target_bytes==1024&&map.target_pages==1&&map.resource_pages==1&&map.heap_pages==512);
 for(size_t i=0;i<TEGU_AC1_RT1_SCRATCH_BYTES;i++)
  if(!heap_dynamic_byte(i))CHECK(scratch[i]==r8_aperture[i]);
 CHECK(!memcmp(resources,r8_aperture+0x71000,TEGU_AC1_RT1_RESOURCE_BYTES));
 CHECK(commands.count==TEGU_AC1_RT1_COMMAND_COUNT&&!memcmp(commands.words,r8_wire,sizeof(commands.words)));

 /* The observed 776x24 texture class is a layout exercise only. The actual
  * attachment-clear pass dimensions remain unknown until command encoding. */
 struct tegu_ac1_rt1_layout wide={TEGU_AC1_RT1_LAYOUT_VERSION,776,24,3136,77824};
 CHECK(!tegu_ac1_rt1_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,&wide,0x44332211,
  r8_aperture+0x71200,16,r8_aperture+0x71300,320,scratch,TEGU_AC1_RT1_SCRATCH_BYTES,
  resources,TEGU_AC1_RT1_RESOURCE_BYTES,&map,&commands));
 CHECK(map.target_va==0x06070000&&map.target_bytes==75264&&map.target_pages==19);
 CHECK(map.resource_va==0x06083000&&map.heap_va==0x06084000);
 const uint32_t extent=775u|(23u<<16);
 CHECK(get32(scratch+0x20)==extent&&get32(scratch+0x28)==extent);
 CHECK(get32(scratch+0x1008)==0x82&&get32(scratch+0x100c)==extent);
 CHECK(get32(scratch+0xa00)==0x43c20000&&get32(scratch+0xa04)==0x41400000);
 CHECK(get32(scratch+0xa10)==0x43c20000&&get32(scratch+0xa14)==0x41400000);
 CHECK(get64(scratch+0xf0)==UINT64_C(0x4433221144332211)&&get64(scratch+0xf8)==UINT64_C(0x4433221144332211));
 CHECK(get64(scratch+0xe0)==map.target_va&&get32(scratch+0xe8)==3136&&get32(scratch+0xec)==75264);
 CHECK(get32(scratch+0x1084)==0x200000&&get64(scratch+0x1088)==map.heap_va);
 CHECK(get64(scratch+0x1090)==map.heap_va+64&&get64(scratch+0x1098)==map.heap_va+0x200000);
 CHECK(get64(resources+0x10)==((UINT64_C(1)<<56)|(map.resource_va+0x100)));
 CHECK(get64(resources+0x108)==map.resource_va+0x200&&get64(resources+0x128)==map.resource_va+0x300);
 CHECK(commands.words[6]==((UINT64_C(2)<<56)|(map.resource_va+4)));
 CHECK(commands.words[30]==((UINT64_C(2)<<56)|(UINT64_C(43)<<48)|extent));
 CHECK(commands.words[48]==commands.words[30]);

 /* Per-pass clear color reaches every MALI_RT_CLEAR lane. These two values
  * are both distinct from the R8 regression color above. */
 const uint32_t alternate_clears[]={0,UINT32_C(0xdeadbeef)};
 for(unsigned clear_case=0;clear_case<2;clear_case++) {
  uint32_t clear=alternate_clears[clear_case];
  CHECK(!tegu_ac1_rt1_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,&wide,clear,
   r8_aperture+0x71200,16,r8_aperture+0x71300,320,scratch,TEGU_AC1_RT1_SCRATCH_BYTES,
   resources,TEGU_AC1_RT1_RESOURCE_BYTES,&map,&commands));
  for(unsigned lane=0;lane<4;lane++)CHECK(get32(scratch+0xf0+lane*4)==clear);
 }

 /* Pixel-class portrait target: 1080 visible pixels, 64-byte-aligned 4352
  * row bytes, and a final partial surface page backed by a full mapped page. */
 struct tegu_ac1_rt1_layout pixel={TEGU_AC1_RT1_LAYOUT_VERSION,1080,2424,4352,10551296};
 CHECK(!tegu_ac1_rt1_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,&pixel,0x44332211,
  r8_aperture+0x71200,16,r8_aperture+0x71300,320,scratch,TEGU_AC1_RT1_SCRATCH_BYTES,
  resources,TEGU_AC1_RT1_RESOURCE_BYTES,&map,&commands));
 CHECK(map.target_bytes==10549248&&map.target_pages==2576);
 CHECK(map.resource_va==0x06a80000&&map.heap_va==0x06a81000);
 CHECK(get32(scratch+0xe8)==4352&&get32(scratch+0xec)==10549248);
 CHECK(get32(scratch+0x20)==(1079u|(2423u<<16)));

 /* The display scanout's packed 4320-byte stride is not in this 64-byte
  * state class, and a backing length that omits the rounded final page cannot
  * satisfy fullscreen_mapping's page resolver. */
 struct tegu_ac1_rt1_layout packed=pixel;packed.row_stride=4320;packed.target_length=10473472;
 CHECK(tegu_ac1_rt1_map_build(&packed,&map)==TEGU_AC1_EINVAL);
 struct tegu_ac1_rt1_layout short_backing=wide;short_backing.target_length=75264;
 CHECK(tegu_ac1_rt1_map_build(&short_backing,&map)==TEGU_AC1_EINVAL);

 /* Decode every changed descriptor through the pinned Mesa v10 packers. */
 CHECK(!tegu_ac1_rt1_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,&wide,0x44332211,
  r8_aperture+0x71200,16,r8_aperture+0x71300,320,scratch,TEGU_AC1_RT1_SCRATCH_BYTES,
  resources,TEGU_AC1_RT1_RESOURCE_BYTES,&map,&commands));
 struct mali_framebuffer_parameters_packed fp;memcpy(&fp,scratch,sizeof(fp));
 struct MALI_FRAMEBUFFER_PARAMETERS f={0};MALI_FRAMEBUFFER_PARAMETERS_unpack(&fp,&f);
 CHECK(f.width==776&&f.height==24&&f.bound_max_x==775&&f.bound_max_y==23);
 CHECK(f.render_target_count==2&&f.color_buffer_allocation==1024&&f.tiler==0x06001000);
 struct mali_rgb_render_target_packed r0p,r1p;
 memcpy(&r0p,scratch+0x80,sizeof(r0p));memcpy(&r1p,scratch+0xc0,sizeof(r1p));
 struct MALI_RGB_RENDER_TARGET r0={0},r1={0};
 MALI_RGB_RENDER_TARGET_unpack(&r0p,&r0);MALI_RGB_RENDER_TARGET_unpack(&r1p,&r1);
 CHECK(!r0.write_enable&&r0.internal_buffer_offset==1024&&r0.writeback_buffer.base==0);
 CHECK(r1.write_enable&&r1.internal_buffer_offset==0&&r1.writeback_format==MALI_COLOR_FORMAT_R8G8B8A8);
 CHECK(r1.writeback_block_format==MALI_BLOCK_FORMAT_LINEAR&&r1.swizzle==0x60a&&r1.clean_tile_write_enable);
 CHECK(r1.writeback_buffer.base==map.target_va&&r1.writeback_buffer.row_stride==3136&&
  r1.writeback_buffer.surface_stride==75264&&r1.clear.color_0==0x44332211&&
  r1.clear.color_1==0x44332211&&r1.clear.color_2==0x44332211&&r1.clear.color_3==0x44332211);
 struct mali_tiler_context_packed tcp;memcpy(&tcp,scratch+0x1000,sizeof(tcp));
 struct MALI_TILER_CONTEXT tc={0};MALI_TILER_CONTEXT_unpack(&tcp,&tc);
 CHECK(tc.hierarchy_mask==0x82&&tc.fb_width==776&&tc.fb_height==24&&tc.heap==0x06001080);
 struct mali_tiler_heap_packed thp;memcpy(&thp,scratch+0x1080,sizeof(thp));
 struct MALI_TILER_HEAP th={0};MALI_TILER_HEAP_unpack(&thp,&th);
 CHECK(th.size==0x200000&&th.base==map.heap_va&&th.bottom==map.heap_va+64&&th.top==map.heap_va+0x200000);
 struct mali_resource_packed rp;memcpy(&rp,resources+0x10,sizeof(rp));
 struct MALI_RESOURCE resource={0};MALI_RESOURCE_unpack(&rp,&resource);
 CHECK(resource.address==map.resource_va+0x100&&resource.contains_descriptors&&resource.size==64);
 struct mali_buffer_packed bp;memcpy(&bp,resources+0x100,sizeof(bp));
 struct MALI_BUFFER buffer={0};MALI_BUFFER_unpack(&bp,&buffer);
 CHECK(buffer.size==16&&buffer.address==map.resource_va+0x200);
 memcpy(&bp,resources+0x120,sizeof(bp));MALI_BUFFER_unpack(&bp,&buffer);
 CHECK(buffer.size==320&&buffer.address==map.resource_va+0x300);

 /* Invalid input is atomic across every caller-owned output. */
 memset(scratch,0xa5,TEGU_AC1_RT1_SCRATCH_BYTES);memset(resources,0x5a,TEGU_AC1_RT1_RESOURCE_BYTES);
 memset(&map,0x3c,sizeof(map));memset(&commands,0xc3,sizeof(commands));
 struct tegu_ac1_rt1_layout bad=wide;bad.row_stride=3104;
 CHECK(tegu_ac1_rt1_state_build(&info,package,TEGU_AC1_PACKAGE_BYTES,&bad,0x44332211,
  r8_aperture+0x71200,16,r8_aperture+0x71300,320,scratch,TEGU_AC1_RT1_SCRATCH_BYTES,
  resources,TEGU_AC1_RT1_RESOURCE_BYTES,&map,&commands)==TEGU_AC1_EINVAL);
 for(size_t i=0;i<TEGU_AC1_RT1_SCRATCH_BYTES;i++)CHECK(scratch[i]==0xa5);
 for(size_t i=0;i<TEGU_AC1_RT1_RESOURCE_BYTES;i++)CHECK(resources[i]==0x5a);
 const uint8_t *m=(const uint8_t *)&map;for(size_t i=0;i<sizeof(map);i++)CHECK(m[i]==0x3c);
 const uint8_t *c=(const uint8_t *)&commands;for(size_t i=0;i<sizeof(commands);i++)CHECK(c[i]==0xc3);

 free(resources);free(scratch);free(r8_wire);free(r8_aperture);free(package);
 puts("AC1_RT1_STATE_HOST_PASS R8 parity; 776x24 Mesa semantics; 1080x2424 rounded-map layout; atomic refusal");
 return 0;
}
