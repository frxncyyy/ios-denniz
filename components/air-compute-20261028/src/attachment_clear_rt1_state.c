#include "attachment_clear_rt1_state.h"
#include <string.h>

struct tegu_ac1_rt1_word { uint32_t offset; uint64_t value; };

/* Invariant bytes from the device-qualified R8 state. Dynamic framebuffer,
 * viewport, writeback, table and heap fields are written below. */
static const struct tegu_ac1_rt1_word scratch_words[]={
 {0x8u,UINT64_C(0x0000000006000100)},
 {0x28u,UINT64_C(0x0108888000000000)},
 {0x38u,UINT64_C(0x0000000006001000)},
 {0x80u,UINT64_C(0x0000010004000400)},{0xc0u,UINT64_C(0x860a029904000000)},
 {0x100u,UINT64_C(0x0000000000800080)},{0x180u,UINT64_C(0x0000000000800080)},
 {0x400u,UINT64_C(0x0000000000070007)},{0x410u,UINT64_C(0x00000000e4c00000)},
 {0x448u,UINT64_C(0x0000000000000003)},{0x450u,UINT64_C(0xf012212200000a00)},
 {0x458u,UINT64_C(0x000ed00000010019)},{0x480u,UINT64_C(0x0000300080000128)},
 {0x488u,UINT64_C(0x0000000006030000)},{0x4c0u,UINT64_C(0x0000001f00000000)},
 {0x600u,UINT64_C(0x0000380080000038)},{0x608u,UINT64_C(0x0000000006002380)},
 {0xa08u,UINT64_C(0x000000003f800000)},{0xa20u,UINT64_C(0x0000000000000001)},
 {0xb30u,UINT64_C(0x0000000000000003)},{0xb38u,UINT64_C(0x000ed00000010019)},
 {0xbc0u,UINT64_C(0x0000222200002222)},
 {0x1018u,UINT64_C(0x0000000006001080)},{0x1020u,UINT64_C(0x0000000006010010)},
 {0x1080u,UINT64_C(0x0000000000000029)}
};

static const uint64_t command_words[TEGU_AC1_RT1_COMMAND_COUNT]={
 UINT64_C(0x1700000000000000),UINT64_C(0x01480000060010c0),UINT64_C(0x3000480000000000),
 UINT64_C(0x3100000000000000),UINT64_C(0x0300000000ff0000),UINT64_C(0x220000000000000e),
 UINT64_C(0x0200000000000000),UINT64_C(0x0201000000000000),UINT64_C(0x0104000000000000),
 UINT64_C(0x020c000006000b00),UINT64_C(0x020d000008000000),UINT64_C(0x0112000000000000),
 UINT64_C(0x011c000000000000),UINT64_C(0x012e000000000000),UINT64_C(0x0136000000000000),
 UINT64_C(0x0208000006000a00),UINT64_C(0x0209000011000000),UINT64_C(0x0110000006000600),
 UINT64_C(0x0114000006000480),UINT64_C(0x01180000060004c0),UINT64_C(0x0220000000000000),
 UINT64_C(0x0221000000000003),UINT64_C(0x0222000000000001),UINT64_C(0x0223000000000000),
 UINT64_C(0x0224000000000000),UINT64_C(0x0225000000000000),UINT64_C(0x0226000000000000),
 UINT64_C(0x0227000000000000),UINT64_C(0x0128000006001000),UINT64_C(0x022a000000000000),
 UINT64_C(0x022b000000000000),UINT64_C(0x022c000000000000),UINT64_C(0x022d00003f800000),
 UINT64_C(0x0230000000000000),UINT64_C(0x0132000006000442),UINT64_C(0x0134000006000400),
 UINT64_C(0x0238000000430000),UINT64_C(0x0239000000000003),UINT64_C(0x023a00000002ffff),
 UINT64_C(0x0226000000002000),UINT64_C(0x013c00003f800000),UINT64_C(0x0600004200000008),
 UINT64_C(0x0900000000000000),UINT64_C(0x0300000000ff0000),UINT64_C(0x3100000100000000),
 UINT64_C(0x0300000000ff0000),UINT64_C(0x0128000006000005),UINT64_C(0x022a000000000000),
 UINT64_C(0x022b000000000000),UINT64_C(0x012c000000000000),UINT64_C(0x022e000000000000),
 UINT64_C(0x0700000000000000),UINT64_C(0x0300000000ff0000),UINT64_C(0x3100000300000000),
 UINT64_C(0x0300000000ff0000),UINT64_C(0x024a000000000000),UINT64_C(0x24004a0000000211),
 UINT64_C(0x0300000000010000),UINT64_C(0x2200000000000000)
};

static int ac1_rt1_span(const void *p,size_t n){return p&&n&&(uintptr_t)p<=UINTPTR_MAX-n;}
static int ac1_rt1_overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+bn&&y<x+an;}
static void put32(void *base,uint32_t offset,uint32_t value){memcpy((uint8_t *)base+offset,&value,sizeof(value));}
static void put64(void *base,uint32_t offset,uint64_t value){memcpy((uint8_t *)base+offset,&value,sizeof(value));}

/* Exact IEEE-754 binary32 representation of n/2 for n in 16..4096. */
static uint32_t ac1_rt1_half_dimension_float(uint32_t n){
 unsigned e=31u-(unsigned)__builtin_clz(n);
 return ((126u+e)<<23)|((n-(1u<<e))<<(23u-e));
}

int tegu_ac1_rt1_map_build(const struct tegu_ac1_rt1_layout *l,
 struct tegu_ac1_rt1_map *out)
{
 if(!l||!out||l->version!=TEGU_AC1_RT1_LAYOUT_VERSION||l->width<16u||
    l->height<16u||l->width>TEGU_AC1_RT1_MAX_DIMENSION||
    l->height>TEGU_AC1_RT1_MAX_DIMENSION||(l->row_stride&63u)||
    (uint64_t)l->row_stride<(uint64_t)l->width*4u||
    l->row_stride>TEGU_AC1_RT1_MAX_DIMENSION*16u)return TEGU_AC1_EINVAL;
 uint64_t bytes=(uint64_t)l->row_stride*l->height;
 uint64_t pages=(bytes+4095u)/4096u;
 uint64_t mapped_bytes=pages*4096u;
 /* fullscreen_mapping resolves every 4 KiB leaf including the partial final
  * surface page. Its caller passes getLength(), which is allocation-rounded
  * for both TeguGPUBuffer and imported IOSurface backing. */
 if(!bytes||bytes>TEGU_AC1_RT1_MAX_TARGET_BYTES||pages>UINT32_MAX||
    (l->target_length&4095u)||l->target_length<mapped_bytes||
    l->target_length>TEGU_AC1_RT1_MAX_TARGET_BYTES)return TEGU_AC1_EINVAL;
 struct tegu_ac1_rt1_map value={
  .target_va=TEGU_AC1_RT1_SCRATCH_VA+TEGU_AC1_RT1_SCRATCH_BYTES,
  .resource_va=TEGU_AC1_RT1_SCRATCH_VA+TEGU_AC1_RT1_SCRATCH_BYTES+pages*4096u,
  .heap_va=TEGU_AC1_RT1_SCRATCH_VA+TEGU_AC1_RT1_SCRATCH_BYTES+(pages+1u)*4096u,
  .target_bytes=bytes,.target_pages=(uint32_t)pages,.resource_pages=1u,
  .heap_pages=TEGU_AC1_RT1_HEAP_BYTES/4096u
 };
 if(value.heap_va>=(UINT64_C(1)<<48)||
    value.heap_va>=(UINT64_C(1)<<48)-TEGU_AC1_RT1_HEAP_BYTES)return TEGU_AC1_EINVAL;
 *out=value;return TEGU_AC1_OK;
}

int tegu_ac1_rt1_state_build(const struct tegu_ac1_info *info,
 const void *package,size_t package_bytes,const struct tegu_ac1_rt1_layout *layout,
 uint32_t clear_rgba8,const void *rect,size_t rect_bytes,const void *matrix,size_t matrix_bytes,
 void *scratch,size_t scratch_bytes,void *resources,size_t resource_bytes,
 struct tegu_ac1_rt1_map *map,struct tegu_ac1_rt1_commands *commands)
{
 if(!ac1_rt1_span(info,sizeof(*info))||!ac1_rt1_span(package,package_bytes)||!ac1_rt1_span(layout,sizeof(*layout))||
    !ac1_rt1_span(rect,rect_bytes)||!ac1_rt1_span(matrix,matrix_bytes)||!ac1_rt1_span(scratch,scratch_bytes)||
    !ac1_rt1_span(resources,resource_bytes)||!ac1_rt1_span(map,sizeof(*map))||!ac1_rt1_span(commands,sizeof(*commands))||
    package_bytes!=TEGU_AC1_PACKAGE_BYTES||rect_bytes!=TEGU_AC1_RT1_RECT_BYTES||
    matrix_bytes!=TEGU_AC1_RT1_MATRIX_BYTES||scratch_bytes!=TEGU_AC1_RT1_SCRATCH_BYTES||
    resource_bytes!=TEGU_AC1_RT1_RESOURCE_BYTES)return TEGU_AC1_EINVAL;
 const void *p[]={info,package,layout,rect,matrix,scratch,resources,map,commands};
 const size_t n[]={sizeof(*info),package_bytes,sizeof(*layout),rect_bytes,matrix_bytes,
  scratch_bytes,resource_bytes,sizeof(*map),sizeof(*commands)};
 for(unsigned i=0;i<9;i++)for(unsigned j=0;j<i;j++)if(ac1_rt1_overlap(p[i],n[i],p[j],n[j]))return TEGU_AC1_EALIAS;
 struct tegu_ac1_rt1_map value;
 if(tegu_ac1_rt1_map_build(layout,&value))return TEGU_AC1_EINVAL;
 struct tegu_ac1_layout code_layout;
 if(tegu_ac1_layout_build(info,8192,&code_layout)||code_layout.vs_offset!=0x1000||
    code_layout.fs_offset!=0x1800||code_layout.total_bytes!=0x2000)return TEGU_AC1_EIDENTITY;
 uint8_t vs[TEGU_AC1_VS_BYTES],fs[TEGU_AC1_FS_BYTES];
 int rc=tegu_ac1_copy_code(info,package,package_bytes,vs,sizeof(vs),fs,sizeof(fs));
 if(rc)return rc;

 memset(scratch,0,scratch_bytes);memset(resources,0,resource_bytes);
 for(unsigned i=0;i<sizeof(scratch_words)/sizeof(scratch_words[0]);i++)
  put64(scratch,scratch_words[i].offset,scratch_words[i].value);
 uint64_t clear=(uint64_t)clear_rgba8|((uint64_t)clear_rgba8<<32);
 put64(scratch,0xf0,clear);put64(scratch,0xf8,clear);
 uint32_t extent=(layout->width-1u)|((layout->height-1u)<<16);
 /* MALI_FRAMEBUFFER_PARAMETERS words8/10 and v10 TILER_CONTEXT word3. */
 put32(scratch,0x20,extent);put32(scratch,0x28,extent);
 put32(scratch,0x1008,TEGU_AC1_RT1_HIERARCHY_MASK);put32(scratch,0x100c,extent);
 /* Viewport scale/offset are width/2 and height/2. */
 uint32_t half_w=ac1_rt1_half_dimension_float(layout->width),half_h=ac1_rt1_half_dimension_float(layout->height);
 put32(scratch,0xa00,half_w);put32(scratch,0xa04,half_h);
 put32(scratch,0xa10,half_w);put32(scratch,0xa14,half_h);
 /* MALI_RGB_RENDER_TARGET slot1 words8..11. RT0 at 0x80 remains disabled. */
 put64(scratch,0xe0,value.target_va);put32(scratch,0xe8,layout->row_stride);
 put32(scratch,0xec,(uint32_t)value.target_bytes);
 /* MALI_TILER_HEAP words1..7: fixed qualified 2 MiB initial heap. */
 put32(scratch,0x1084,TEGU_AC1_RT1_HEAP_BYTES);
 put64(scratch,0x1088,value.heap_va);put64(scratch,0x1090,value.heap_va+64u);
 put64(scratch,0x1098,value.heap_va+TEGU_AC1_RT1_HEAP_BYTES);
 memcpy((uint8_t *)scratch+0x2000,vs,sizeof(vs));
 memcpy((uint8_t *)scratch+0x30000,fs,sizeof(fs));

 /* v10 MALI_RESOURCE entry1 and two SIMPLE MALI_BUFFER descriptors. */
 put64(resources,0x10,(value.resource_va+0x100u)|(UINT64_C(1)<<56));
 put64(resources,0x18,64u);
 put64(resources,0x100,UINT64_C(0x0000001000000019));
 put64(resources,0x108,value.resource_va+0x200u);
 put64(resources,0x120,UINT64_C(0x0000014000000019));
 put64(resources,0x128,value.resource_va+0x300u);
 memcpy((uint8_t *)resources+0x200,rect,rect_bytes);
 memcpy((uint8_t *)resources+0x300,matrix,matrix_bytes);

 memcpy(commands->words,command_words,sizeof(command_words));
 commands->words[6]=(UINT64_C(2)<<56)|(value.resource_va+4u);
 commands->words[30]=(UINT64_C(2)<<56)|(UINT64_C(43)<<48)|extent;
 commands->words[48]=(UINT64_C(2)<<56)|(UINT64_C(43)<<48)|extent;
 commands->count=TEGU_AC1_RT1_COMMAND_COUNT;*map=value;
 return TEGU_AC1_OK;
}
