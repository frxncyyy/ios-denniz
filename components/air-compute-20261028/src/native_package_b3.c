#include "native_package_b3.h"
#include "tegu_memory.h"
#include <stdint.h>

#define TEGU_NATIVE_B3_MAGIC 0x31524754u
#define TEGU_NATIVE_B3_GPU 0xb8a21030u
#define TEGU_NATIVE_B3_VA_LIMIT (1ull<<48)
static uint32_t b3_u32(const uint8_t *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int b3_overlap(const void *a,size_t an,const void *b,size_t bn) {
 uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return an&&bn&&(x<=y?y-x<an:x-y<bn);
}
int tegu_native_b3_parse(const void *bytes,size_t n,struct tegu_native_b3_info *out) {
 if(!bytes||!out||n!=TEGU_NATIVE_B3_PACKAGE_BYTES||b3_overlap(bytes,n,out,sizeof(*out)))return -1;
 const uint8_t *p=(const uint8_t *)bytes;uint32_t w[32];
 for(unsigned i=0;i<32;i++)w[i]=b3_u32(p+4*i);
 if(w[0]!=TEGU_NATIVE_B3_MAGIC||w[1]!=TEGU_NATIVE_B3_VERSION||w[2]!=TEGU_NATIVE_B3_GPU||
    w[3]!=80||w[4]!=TEGU_NATIVE_B3_VS_BYTES||w[5]!=TEGU_NATIVE_B3_FS_BYTES||w[6]!=8||w[7]!=76)return -2;
 uint32_t fs=TEGU_NATIVE_B3_CODE+((w[4]+63u)&~63u);
 if(n!=(size_t)fs+w[5])return -2;
 for(uint32_t i=TEGU_NATIVE_B3_CODE+w[4];i<fs;i++)if(p[i])return -2;
 uint64_t secondary=(uint64_t)w[29]|((uint64_t)w[30]<<32);
 if(w[24]!=3||w[25]!=2||w[26]!=512||w[27]!=1024||w[28]!=32||
    secondary!=UINT64_C(0x3800000000000000)||w[31]!=8)return -3;
 static const uint32_t blend[9]={1,0,0,1,0,0,1,0,15};
 for(unsigned i=0;i<9;i++)if(b3_u32(p+128+4*i)!=blend[i])return -3;
 for(unsigned i=164;i<192;i++)if(p[i])return -3;
 static const uint32_t texture[7]={1,16,3,0,0,4,1};
 for(unsigned i=0;i<7;i++)if(b3_u32(p+192+4*i)!=texture[i])return -4;
 for(unsigned i=220;i<256;i++)if(p[i])return -4;
 static const uint32_t resources[30]={1,1,0,4,1,1,4,4,1,1,2,3,2,1,6,3,0,
  64,16,82,2,0,4,64,68,72,4,80,32,112};
 for(unsigned i=0;i<30;i++)if(b3_u32(p+256+4*i)!=resources[i])return -4;
 if(b3_u32(p+376)!=32||b3_u32(p+380))return -4;
 for(unsigned i=0;i<8;i++) {
  uint32_t format=b3_u32(p+384+i*8),mask=b3_u32(p+388+i*8);
  if(i<2) {if(format!=80||mask!=(i?0u:15u))return -5;}
  else if(format||mask)return -5;
 }
 for(unsigned i=448;i<512;i++)if(p[i])return -5;
 uint32_t attrs[2][7];
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<7;j++)attrs[i][j]=b3_u32(p+TEGU_NATIVE_B3_ATTR+i*28+j*4);
 if(attrs[0][0]!=0||attrs[0][1]!=1||attrs[0][2]!=109||attrs[0][3]!=0||attrs[0][4]!=48||attrs[0][5]||attrs[0][6]!=1||
    attrs[1][0]!=1||attrs[1][1]!=1||attrs[1][2]!=103||attrs[1][3]!=16||attrs[1][4]!=48||attrs[1][5]||attrs[1][6]!=1)return -5;
 for(unsigned i=TEGU_NATIVE_B3_ATTR+56;i<TEGU_NATIVE_B3_CODE;i++)if(p[i])return -5;
 struct tegu_native_b3_info x;memset(&x,0,sizeof(x));
 x.format=80;x.version=TEGU_NATIVE_B3_VERSION;x.attr_mask=3;x.attr_count=2;
 x.position_offset=512;x.secondary_offset=1024;x.secondary_registers=32;x.varying_bytes=8;x.secondary_preload=secondary;
 memcpy(x.attributes,attrs,sizeof(attrs));
 x.attachment_count=2;x.attachment[0]=(struct tegu_native_b3_attachment){80,15};
 x.attachment[1]=(struct tegu_native_b3_attachment){80,0};
 x.blend=(struct tegu_blend_inputs){1,0,0,1,0,0,1,0,15};
 x.ubo_stage[0]=x.ubo_stage[1]=1;x.ubo_stage[2]=x.ubo_stage[3]=2;
 x.ubo_metal_slot[0]=2;x.ubo_metal_slot[1]=3;x.ubo_metal_slot[2]=1;x.ubo_metal_slot[3]=6;
 x.texture_metal_slot=3;x.sampler_metal_slot=0;x.texel_format_push_offset=72;x.sample_count=4;
 static const uint32_t windows[4]={64,16,82,2},declared[4]={80,32,112,32},push[4]={0,4,64,68};
 for(unsigned i=0;i<4;i++) {
  x.ubo_window[i]=windows[i];x.ubo_declared_bytes[i]=declared[i];x.push_residual_offset[i]=push[i];
 }
 const uint32_t stage_offsets[2]={32,64},code_offsets[2]={TEGU_NATIVE_B3_CODE,fs};
 static const uint32_t bytes_exact[2]={TEGU_NATIVE_B3_VS_BYTES,TEGU_NATIVE_B3_FS_BYTES};
 static const uint32_t registers[2]={32,64},fau[2]={34,51},sysvals[2]={3,8},push_bytes[2]={8,76};
 static const uint64_t preload[2]={UINT64_C(0x3800000000000000),UINT64_C(0x3000000000000000)};
 for(unsigned i=0;i<2;i++) {
  const uint8_t *s=p+stage_offsets[i];struct tegu_native_b3_stage *d=&x.stage[i];
  d->bytes=w[4+i];d->offset=code_offsets[i];d->registers=b3_u32(s);d->fau_words=b3_u32(s+4);
  d->sysvals=b3_u32(s+8);d->preload=(uint64_t)b3_u32(s+16)|((uint64_t)b3_u32(s+20)<<32);
  d->push_bytes=b3_u32(s+24);
  if(d->bytes!=bytes_exact[i]||d->registers!=registers[i]||d->fau_words!=fau[i]||
     d->sysvals!=sysvals[i]||b3_u32(s+12)||d->preload!=preload[i]||
     d->push_bytes!=push_bytes[i]||b3_u32(s+28))return -6;
  for(unsigned k=push_bytes[i];k<128;k++)if(p[TEGU_NATIVE_B3_PUSH+i*128+k])return -6;
 }
 static const struct {unsigned stage,offset;} dynamic[5]={{0,0},{0,4},{1,64},{1,68},{1,72}};
 for(unsigned i=0;i<5;i++)if(b3_u32(p+TEGU_NATIVE_B3_PUSH+dynamic[i].stage*128+dynamic[i].offset))return -6;
 *out=x;return 0;
}
int tegu_native_b3_bind(const void *pkg,size_t n,const struct tegu_native_b3_binding *in,
 struct tegu_native_b3_checked *out) {
 if(!pkg||!in||!out||b3_overlap(in,sizeof(*in),out,sizeof(*out))||b3_overlap(pkg,n,out,sizeof(*out)))return -1;
 struct tegu_native_b3_info info;if(tegu_native_b3_parse(pkg,n,&info))return -2;
 uint64_t h=in->table1_handle;if(!h||(h&0xfff)!=4||h>=TEGU_NATIVE_B3_VA_LIMIT)return -3;
 uint32_t residual[4]={b3_u32(in->vertex_push),b3_u32(in->vertex_push+4),
  b3_u32(in->fragment_push+64),b3_u32(in->fragment_push+68)};
 struct tegu_native_b3_checked tmp;memset(&tmp,0,sizeof(tmp));tmp.table1_handle=h;
 for(unsigned i=0;i<4;i++) {
  if(in->ubo_offset[i]>in->ubo_length[i]||info.ubo_window[i]>in->ubo_length[i]-in->ubo_offset[i]||
     residual[i]!=(in->ubo_offset[i]&15))return -4;
  tmp.ubo_base_offset[i]=in->ubo_offset[i]&~15u;tmp.ubo_window[i]=info.ubo_window[i];
 }
 if(b3_u32(in->fragment_push+72)!=0xd241f2)return -4;
 for(unsigned i=8;i<128;i++)if(in->vertex_push[i])return -4;
 for(unsigned i=0;i<64;i++)if(in->fragment_push[i])return -4;
 for(unsigned i=76;i<128;i++)if(in->fragment_push[i])return -4;
 memcpy(tmp.vertex_push,in->vertex_push,128);memcpy(tmp.fragment_push,in->fragment_push,128);
 memcpy(out,&tmp,sizeof(tmp));return 0;
}
int tegu_native_b3_layout_build(const void *pkg,size_t n,uint32_t scratch_bytes,
 struct tegu_native_b3_layout *out) {
 if(!pkg||!out||b3_overlap(pkg,n,out,sizeof(*out)))return -1;
 struct tegu_native_b3_info info;if(tegu_native_b3_parse(pkg,n,&info))return -2;
 const uint32_t vp=2,fp=(info.stage[1].bytes+TEGU_NATIVE_B3_PAGE_BYTES-1)/TEGU_NATIVE_B3_PAGE_BYTES;
 if(scratch_bytes<TEGU_NATIVE_B3_SCRATCH_BYTES||fp!=3)return -3;
 *out=(struct tegu_native_b3_layout){TEGU_NATIVE_B3_VS_PAGE_OFFSET,info.stage[0].bytes,vp,
  TEGU_NATIVE_B3_FS_PAGE_OFFSET,info.stage[1].bytes,fp,TEGU_NATIVE_B3_SCRATCH_BYTES,1,1};
 return 0;
}
int tegu_native_b3_stage_copy(const void *pkg,size_t n,void *scratch,size_t scratch_bytes,
 struct tegu_native_b3_layout *out) {
 if(!pkg||!scratch||!out||b3_overlap(pkg,n,scratch,scratch_bytes)||
    b3_overlap(pkg,n,out,sizeof(*out))||b3_overlap(scratch,scratch_bytes,out,sizeof(*out)))return -1;
 struct tegu_native_b3_info info;struct tegu_native_b3_layout layout;
 if(tegu_native_b3_parse(pkg,n,&info)||tegu_native_b3_layout_build(pkg,n,
    (uint32_t)(scratch_bytes>UINT32_MAX?UINT32_MAX:scratch_bytes),&layout))return -2;
 uint8_t *dst=(uint8_t *)scratch;
 memset(dst+layout.vs_offset,0,(size_t)layout.vs_pages*TEGU_NATIVE_B3_PAGE_BYTES);
 memset(dst+layout.fs_offset,0,(size_t)layout.fs_pages*TEGU_NATIVE_B3_PAGE_BYTES);
 memcpy(dst+layout.vs_offset,(const uint8_t *)pkg+info.stage[0].offset,info.stage[0].bytes);
 memcpy(dst+layout.fs_offset,(const uint8_t *)pkg+info.stage[1].offset,info.stage[1].bytes);
 *out=layout;return 0;
}
