#include "native_package_v7.h"
#include "tegu_memory.h"
#include <stdint.h>
#define TEGU_NATIVE_V7_MAGIC 0x31524754u
#define TEGU_NATIVE_V7_GPU 0xb8a21030u
#define TEGU_NATIVE_V7_VA_LIMIT (1ull<<48)
static uint32_t tegu_native_v7_rd(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int tegu_native_v7_overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn);}
int tegu_native_v7_blend_class(const struct tegu_blend_inputs *b){
 if(!b)return 0;
 if(b->version==1&&b->enabled==1&&b->rgb_op==0&&b->rgb_src==1&&b->rgb_dst==5&&
    b->alpha_op==0&&b->alpha_src==1&&b->alpha_dst==5&&b->write_mask==15)return TEGU_NATIVE_V7_BLEND_SOURCE_OVER;
 if(b->version==1&&b->enabled==0&&b->rgb_op==0&&b->rgb_src==1&&b->rgb_dst==0&&
    b->alpha_op==0&&b->alpha_src==1&&b->alpha_dst==0&&b->write_mask==15)return TEGU_NATIVE_V7_BLEND_DISABLED;
 return 0;
}
int tegu_native_v7_parse(const void *bytes,size_t n,struct tegu_native_v7_info *out){
 if(!bytes||!out||n<TEGU_NATIVE_V7_CODE||n>TEGU_NATIVE_V7_PACKAGE_MAX||tegu_native_v7_overlap(bytes,n,out,sizeof(*out)))return -1;
 const uint8_t *p=(const uint8_t *)bytes;uint32_t w[32];for(unsigned i=0;i<32;i++)w[i]=tegu_native_v7_rd(p+4*i);
 if(w[0]!=TEGU_NATIVE_V7_MAGIC||w[1]!=7||w[2]!=TEGU_NATIVE_V7_GPU||w[3]!=80||!w[4]||!w[5]||(w[4]&7)||(w[5]&7)||w[4]!=TEGU_NATIVE_V7_VS_BYTES||w[5]!=TEGU_NATIVE_V7_FS_BYTES)return -2;
 uint32_t fs=TEGU_NATIVE_V7_CODE+((w[4]+63u)&~63u);if(n!=(size_t)fs+w[5])return -2;
 for(uint32_t i=TEGU_NATIVE_V7_CODE+w[4];i<fs;i++)if(p[i])return -2;
 /* V7 preserves paired-IDVS words in the established header positions. */
 uint64_t secondary_preload=(uint64_t)w[29]|((uint64_t)w[30]<<32);
 if(w[24]!=3||w[25]!=2||w[26]!=512||w[27]!=1024||w[28]!=32||secondary_preload!=0x3800000000000000ull||w[31]!=8)return -3;
 /* Preserve exact captured blend state: premultiplied BGRA source-over or
  * the R44 unblended 8-tap class. */
 uint32_t blend_words[9];for(unsigned i=0;i<9;i++)blend_words[i]=tegu_native_v7_rd(p+128+4*i);
 static const uint32_t sourceover[9]={1,1,0,1,5,0,1,5,15};
 static const uint32_t disabled[9]={1,0,0,1,0,0,1,0,15};
 int sourceover_class=1,disabled_class=1;
 for(unsigned i=0;i<9;i++){sourceover_class&=blend_words[i]==sourceover[i];disabled_class&=blend_words[i]==disabled[i];}
 if(!sourceover_class&&!disabled_class)return -3;
 for(unsigned i=164;i<192;i++)if(p[i])return -3;
 /* Reflection-bound Metal slots: texture3/sampler0, descriptor set0/binding4. */
 if(tegu_native_v7_rd(p+192)!=1||tegu_native_v7_rd(p+196)!=16||tegu_native_v7_rd(p+200)!=3||tegu_native_v7_rd(p+204)!=0||tegu_native_v7_rd(p+208)!=0||tegu_native_v7_rd(p+212)!=4||tegu_native_v7_rd(p+216)!=1)return -4;
 for(unsigned i=220;i<256;i++)if(p[i])return -4;
 /* Resource classes name Metal slots and active windows, never captured
  * buffer offsets. Four residual push offsets are per-draw field locations. */
 static const uint32_t ext[26]={1,1,0,4,1,1,4,4,1,1,2,3,2,1,6,3,0,64,16,32,2,0,4,64,68,72};
 for(unsigned i=0;i<26;i++)if(tegu_native_v7_rd(p+256+4*i)!=ext[i])return -4;
 if(tegu_native_v7_rd(p+360)!=16||tegu_native_v7_rd(p+364)!=80||tegu_native_v7_rd(p+368)!=32||tegu_native_v7_rd(p+372)!=48||tegu_native_v7_rd(p+376)!=32||tegu_native_v7_rd(p+380))return -4;
 /* Preserve all requested attachments; the native pass can refuse slot 1. */
 for(unsigned i=0;i<8;i++) {
  uint32_t format=tegu_native_v7_rd(p+384+i*8),mask=tegu_native_v7_rd(p+388+i*8);
  if(i<2) { if(format!=80 || mask!=(i?0u:15u))return -6; }
  else if(format||mask)return -6;
 }
 for(unsigned i=448;i<512;i++)if(p[i])return -6;
 uint32_t attrs[2][7];
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<7;j++)attrs[i][j]=tegu_native_v7_rd(p+TEGU_NATIVE_V7_ATTR+i*28+j*4);
 if(attrs[0][0]!=0||attrs[0][1]!=1||attrs[0][2]!=109||attrs[0][3]!=0||attrs[0][5]!=0||attrs[0][6]!=1||
    attrs[1][0]!=1||attrs[1][1]!=1||attrs[1][2]!=103||attrs[1][3]!=16||attrs[1][5]!=0||attrs[1][6]!=1||
    attrs[0][1]!=attrs[1][1]||attrs[0][4]!=attrs[1][4]||
    attrs[0][4]!=48)return -6;
 for(unsigned i=TEGU_NATIVE_V7_ATTR+56;i<TEGU_NATIVE_V7_CODE;i++)if(p[i])return -6;
 /* V7 stage metadata remains isolated from every v6 size/gate. */
 uint32_t push[2]={tegu_native_v7_rd(p+24),tegu_native_v7_rd(p+28)};
 if(push[0]!=8||push[1]!=76)return -5;
 struct tegu_native_v7_info x;memset(&x,0,sizeof(x));x.version=7;x.format=80;x.attr_mask=3;x.attr_count=2;
 x.position_offset=512;x.secondary_offset=1024;x.secondary_registers=32;x.varying_bytes=8;x.secondary_preload=secondary_preload;
 memcpy(x.attributes,attrs,sizeof(attrs));
 x.attachment_count=2;x.attachment[0]=(struct tegu_native_v7_attachment){80,15};
 x.attachment[1]=(struct tegu_native_v7_attachment){80,0};
 x.blend=(struct tegu_blend_inputs){blend_words[0],blend_words[1],blend_words[2],blend_words[3],blend_words[4],blend_words[5],blend_words[6],blend_words[7],blend_words[8]};
 x.ubo_stage[0]=x.ubo_stage[1]=1;x.ubo_stage[2]=x.ubo_stage[3]=2;
 x.ubo_metal_slot[0]=2;x.ubo_metal_slot[1]=3;x.ubo_metal_slot[2]=1;x.ubo_metal_slot[3]=6;
 x.texture_metal_slot=3;x.sampler_metal_slot=0;x.texel_format_push_offset=72;x.sample_count=16;
 static const uint32_t declared[4]={80,32,48,32};for(unsigned i=0;i<4;i++){x.ubo_window[i]=ext[17+i];x.push_residual_offset[i]=ext[21+i];x.ubo_declared_bytes[i]=declared[i];}
 uint32_t stageofs[2]={32,64},codeofs[2]={TEGU_NATIVE_V7_CODE,fs};
 for(unsigned i=0;i<2;i++){
  const uint8_t *s=p+stageofs[i];struct tegu_native_v7_stage *d=&x.stage[i];
  d->bytes=w[4+i];d->offset=codeofs[i];d->registers=tegu_native_v7_rd(s);d->fau_words=tegu_native_v7_rd(s+4);d->sysvals=tegu_native_v7_rd(s+8);
  d->preload=(uint64_t)tegu_native_v7_rd(s+16)|((uint64_t)tegu_native_v7_rd(s+20)<<32);d->push_bytes=tegu_native_v7_rd(s+24);
  if(d->registers!=(i?64u:32u)||!d->fau_words||d->fau_words>64||tegu_native_v7_rd(s+12)||tegu_native_v7_rd(s+28)||d->push_bytes>push[i]||d->preload!= (i?0x3000000000000000ull:0x3800000000000000ull))return -5;
  if((i==0&&(d->bytes!=1152||d->fau_words!=34||d->sysvals!=3||d->push_bytes!=8))||
     (i==1&&(d->bytes!=25600||d->fau_words!=51||d->sysvals!=8||d->push_bytes!=76)))return -5;
  for(unsigned k=push[i];k<128;k++)if(p[TEGU_NATIVE_V7_PUSH+i*128+k])return -5;
 }
 /* Dynamic UBO residuals and texture format are supplied for each draw.
  * Package-time snapshots must not contain values from the captured draw. */
 static const struct {unsigned stage,offset;} dynamic_fields[5]={
  {0,0},{0,4},{1,64},{1,68},{1,72}
 };
 for(unsigned i=0;i<5;i++)
  if(tegu_native_v7_rd(p+TEGU_NATIVE_V7_PUSH+dynamic_fields[i].stage*128u+
                         dynamic_fields[i].offset))return -5;
 *out=x;return 0;
}
int tegu_native_v7_bind(const void *pkg,size_t n,const struct tegu_native_v7_binding *in,struct tegu_native_v7_checked *out){
 if(!pkg||!in||!out||tegu_native_v7_overlap(in,sizeof(*in),out,sizeof(*out))||tegu_native_v7_overlap(pkg,n,out,sizeof(*out)))return -1;
 struct tegu_native_v7_info info;
 if(tegu_native_v7_parse(pkg,n,&info))return -2;
 uint64_t h=in->table1_handle;if(!h||(h&0xfff)!=4||h>=TEGU_NATIVE_V7_VA_LIMIT)return -3;
 uint32_t residual[4]={tegu_native_v7_rd(in->vertex_push),tegu_native_v7_rd(in->vertex_push+4),tegu_native_v7_rd(in->fragment_push+64),tegu_native_v7_rd(in->fragment_push+68),};
 uint32_t tex=tegu_native_v7_rd(in->fragment_push+72);
 struct tegu_native_v7_checked tmp;memset(&tmp,0,sizeof(tmp));tmp.table1_handle=h;
 for(unsigned i=0;i<4;i++){
  if(in->ubo_offset[i]>in->ubo_length[i]||
     info.ubo_window[i]>in->ubo_length[i]-in->ubo_offset[i]||
     residual[i]!=(in->ubo_offset[i]&15))return -4;
  tmp.ubo_base_offset[i]=in->ubo_offset[i]&~15u;tmp.ubo_window[i]=info.ubo_window[i];
 }
 if(tex!=0xd241f2)return -4;
 for(unsigned i=8;i<128;i++)if(in->vertex_push[i])return -4;
 for(unsigned i=0;i<64;i++)if(in->fragment_push[i])return -4;
 for(unsigned i=76;i<128;i++)if(in->fragment_push[i])return -4;
 memcpy(tmp.vertex_push,in->vertex_push,128);memcpy(tmp.fragment_push,in->fragment_push,128);memcpy(out,&tmp,sizeof(tmp));return 0;
}
int tegu_native_v7_layout_build(const void *pkg,size_t n,uint32_t scratch_bytes,struct tegu_native_v7_layout *out){
 if(!pkg||!out||tegu_native_v7_overlap(pkg,n,out,sizeof(*out)))return -1;
 struct tegu_native_v7_info i;if(tegu_native_v7_parse(pkg,n,&i))return -2;
 uint32_t vp=2,fp=(i.stage[1].bytes+TEGU_NATIVE_V7_PAGE_BYTES-1)/TEGU_NATIVE_V7_PAGE_BYTES;
 uint32_t vo=TEGU_NATIVE_V7_VS_PAGE_OFFSET,fo=0x30000u,need=TEGU_NATIVE_V7_SCRATCH_BYTES;
 if(need>scratch_bytes||need>TEGU_NATIVE_V7_SCRATCH_BYTES)return -3;
 struct tegu_native_v7_layout x={vo,i.stage[0].bytes,vp,fo,i.stage[1].bytes,fp,need,1,1};*out=x;return 0;
}
int tegu_native_v7_stage_copy(const void *pkg,size_t n,void *scratch,size_t scratch_bytes,struct tegu_native_v7_layout *out){
 if(!pkg||!scratch||!out||tegu_native_v7_overlap(pkg,n,scratch,scratch_bytes)||tegu_native_v7_overlap(pkg,n,out,sizeof(*out))||tegu_native_v7_overlap(scratch,scratch_bytes,out,sizeof(*out)))return -1;
 struct tegu_native_v7_info info;struct tegu_native_v7_layout layout;
 if(tegu_native_v7_parse(pkg,n,&info)||tegu_native_v7_layout_build(pkg,n,(uint32_t)(scratch_bytes>UINT32_MAX?UINT32_MAX:scratch_bytes),&layout))return -2;
 uint8_t *dst=(uint8_t *)scratch;
 memset(dst+layout.vs_offset,0,(size_t)layout.vs_pages*TEGU_NATIVE_V7_PAGE_BYTES);
 memset(dst+layout.fs_offset,0,(size_t)layout.fs_pages*TEGU_NATIVE_V7_PAGE_BYTES);
 memcpy(dst+layout.vs_offset,(const uint8_t *)pkg+info.stage[0].offset,info.stage[0].bytes);
 memcpy(dst+layout.fs_offset,(const uint8_t *)pkg+info.stage[1].offset,info.stage[1].bytes);
 *out=layout;return 0;
}
