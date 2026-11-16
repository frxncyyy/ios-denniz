#include "attachment_clear_r8_state.h"
#include <string.h>

struct tegu_ac1_r8_word { uint32_t offset; uint64_t value; };

static const struct tegu_ac1_r8_word scratch_words[]={
 {0x8u,UINT64_C(0x0000000006000100)},{0x20u,UINT64_C(0x00000000000f000f)},
 {0x28u,UINT64_C(0x01088880000f000f)},{0x38u,UINT64_C(0x0000000006001000)},
 {0x80u,UINT64_C(0x0000010004000400)},{0xc0u,UINT64_C(0x860a029904000000)},
 {0xe0u,UINT64_C(0x0000000006070000)},{0xe8u,UINT64_C(0x0000040000000040)},
 {0xf0u,UINT64_C(0x1122334411223344)},{0xf8u,UINT64_C(0x1122334411223344)},
 {0x100u,UINT64_C(0x0000000000800080)},{0x180u,UINT64_C(0x0000000000800080)},
 {0x400u,UINT64_C(0x0000000000070007)},{0x410u,UINT64_C(0x00000000e4c00000)},
 {0x448u,UINT64_C(0x0000000000000003)},{0x450u,UINT64_C(0xf012212200000a00)},
 {0x458u,UINT64_C(0x000ed00000010019)},{0x480u,UINT64_C(0x0000300080000128)},
 {0x488u,UINT64_C(0x0000000006030000)},{0x4c0u,UINT64_C(0x0000001f00000000)},
 {0x600u,UINT64_C(0x0000380080000038)},{0x608u,UINT64_C(0x0000000006002380)},
 {0xa00u,UINT64_C(0x4100000041000000)},{0xa08u,UINT64_C(0x000000003f800000)},
 {0xa10u,UINT64_C(0x4100000041000000)},{0xa20u,UINT64_C(0x0000000000000001)},
 {0xb30u,UINT64_C(0x0000000000000003)},{0xb38u,UINT64_C(0x000ed00000010019)},
 {0xbc0u,UINT64_C(0x0000222200002222)},{0x1008u,UINT64_C(0x000f000f00000001)},
 {0x1018u,UINT64_C(0x0000000006001080)},{0x1020u,UINT64_C(0x0000000006010010)},
 {0x1080u,UINT64_C(0x0002000000000029)},{0x1088u,UINT64_C(0x0000000006050000)},
 {0x1090u,UINT64_C(0x0000000006050040)},{0x1098u,UINT64_C(0x0000000006070000)}
};
static const struct tegu_ac1_r8_word resource_words[]={
 {0x10u,UINT64_C(0x0100000006071100)},{0x18u,UINT64_C(0x0000000000000040)},
 {0x100u,UINT64_C(0x0000001000000019)},{0x108u,UINT64_C(0x0000000006071200)},
 {0x120u,UINT64_C(0x0000014000000019)},{0x128u,UINT64_C(0x0000000006071300)}
};
static const uint64_t command_words[TEGU_AC1_R8_COMMAND_COUNT]={
 UINT64_C(0x1700000000000000),UINT64_C(0x01480000060010c0),UINT64_C(0x3000480000000000),
 UINT64_C(0x3100000000000000),UINT64_C(0x0300000000ff0000),UINT64_C(0x220000000000000e),
 UINT64_C(0x0200000006071004),UINT64_C(0x0201000000000000),UINT64_C(0x0104000000000000),
 UINT64_C(0x020c000006000b00),UINT64_C(0x020d000008000000),UINT64_C(0x0112000000000000),
 UINT64_C(0x011c000000000000),UINT64_C(0x012e000000000000),UINT64_C(0x0136000000000000),
 UINT64_C(0x0208000006000a00),UINT64_C(0x0209000011000000),UINT64_C(0x0110000006000600),
 UINT64_C(0x0114000006000480),UINT64_C(0x01180000060004c0),UINT64_C(0x0220000000000000),
 UINT64_C(0x0221000000000003),UINT64_C(0x0222000000000001),UINT64_C(0x0223000000000000),
 UINT64_C(0x0224000000000000),UINT64_C(0x0225000000000000),UINT64_C(0x0226000000000000),
 UINT64_C(0x0227000000000000),UINT64_C(0x0128000006001000),UINT64_C(0x022a000000000000),
 UINT64_C(0x022b0000000f000f),UINT64_C(0x022c000000000000),UINT64_C(0x022d00003f800000),
 UINT64_C(0x0230000000000000),UINT64_C(0x0132000006000442),UINT64_C(0x0134000006000400),
 UINT64_C(0x0238000000430000),UINT64_C(0x0239000000000003),UINT64_C(0x023a00000002ffff),
 UINT64_C(0x0226000000002000),UINT64_C(0x013c00003f800000),UINT64_C(0x0600004200000008),
 UINT64_C(0x0900000000000000),UINT64_C(0x0300000000ff0000),UINT64_C(0x3100000100000000),
 UINT64_C(0x0300000000ff0000),UINT64_C(0x0128000006000005),UINT64_C(0x022a000000000000),
 UINT64_C(0x022b0000000f000f),UINT64_C(0x012c000000000000),UINT64_C(0x022e000000000000),
 UINT64_C(0x0700000000000000),UINT64_C(0x0300000000ff0000),UINT64_C(0x3100000300000000),
 UINT64_C(0x0300000000ff0000),UINT64_C(0x024a000000000000),UINT64_C(0x24004a0000000211),
 UINT64_C(0x0300000000010000),UINT64_C(0x2200000000000000)
};

static int span(const void *p,size_t n){return p&&n&&(uintptr_t)p<=UINTPTR_MAX-n;}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<y+bn&&y<x+an;}
static void put64(void *base,uint32_t offset,uint64_t value){memcpy((uint8_t *)base+offset,&value,sizeof(value));}

int tegu_ac1_r8_state_build(const struct tegu_ac1_info *info,
 const void *package,size_t package_bytes,
 const void *rect,size_t rect_bytes,const void *matrix,size_t matrix_bytes,
 void *scratch,size_t scratch_bytes,void *resources,size_t resource_bytes,
 struct tegu_ac1_r8_commands *commands)
{
 if(!span(info,sizeof(*info))||!span(package,package_bytes)||
    !span(rect,rect_bytes)||!span(matrix,matrix_bytes)||
    !span(scratch,scratch_bytes)||!span(resources,resource_bytes)||
    !span(commands,sizeof(*commands))||package_bytes!=TEGU_AC1_PACKAGE_BYTES||
    rect_bytes!=TEGU_AC1_R8_RECT_BYTES||matrix_bytes!=TEGU_AC1_R8_MATRIX_BYTES||
    scratch_bytes!=TEGU_AC1_R8_SCRATCH_BYTES||resource_bytes!=TEGU_AC1_R8_RESOURCE_BYTES)
  return TEGU_AC1_EINVAL;
 const void *p[]={info,package,rect,matrix,scratch,resources,commands};
 const size_t n[]={sizeof(*info),package_bytes,rect_bytes,matrix_bytes,scratch_bytes,resource_bytes,sizeof(*commands)};
 for(unsigned i=0;i<7;i++)for(unsigned j=0;j<i;j++)if(overlap(p[i],n[i],p[j],n[j]))return TEGU_AC1_EALIAS;
 struct tegu_ac1_layout layout;
 if(tegu_ac1_layout_build(info,8192,&layout)||layout.vs_offset!=0x1000||
    layout.fs_offset!=0x1800||layout.total_bytes!=0x2000)return TEGU_AC1_EIDENTITY;
 uint8_t vs[TEGU_AC1_VS_BYTES],fs[TEGU_AC1_FS_BYTES];
 int rc=tegu_ac1_copy_code(info,package,package_bytes,vs,sizeof(vs),fs,sizeof(fs));
 if(rc)return rc;
 memset(scratch,0,scratch_bytes);memset(resources,0,resource_bytes);
 for(unsigned i=0;i<sizeof(scratch_words)/sizeof(scratch_words[0]);i++)
  put64(scratch,scratch_words[i].offset,scratch_words[i].value);
 /* The R8 capture uses its independently qualified fixed stage addresses. */
 memcpy((uint8_t *)scratch+0x2000,vs,sizeof(vs));
 memcpy((uint8_t *)scratch+0x30000,fs,sizeof(fs));
 for(unsigned i=0;i<sizeof(resource_words)/sizeof(resource_words[0]);i++)
  put64(resources,resource_words[i].offset,resource_words[i].value);
 memcpy((uint8_t *)resources+0x200,rect,rect_bytes);
 memcpy((uint8_t *)resources+0x300,matrix,matrix_bytes);
 memcpy(commands->words,command_words,sizeof(command_words));commands->count=TEGU_AC1_R8_COMMAND_COUNT;
 return TEGU_AC1_OK;
}
