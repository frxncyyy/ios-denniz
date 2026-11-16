/* Trusted native render package, versions1/2/3/4/5. No user pointers/GPU addresses. Layout
 * validation is not shader instruction validation or a security boundary. */
#ifndef TEGU_RENDER_PIPELINE_FORMAT_H
#define TEGU_RENDER_PIPELINE_FORMAT_H
#include <stdint.h>
#include <stddef.h>
#include "tegu_memory.h"
#include "vertex_fetch.h"
#include "blend_state.h"
#define TEGU_RENDER_MAGIC 0x31524754u
#define TEGU_RENDER_HEADER 128u
#define TEGU_RENDER_CODE_OFFSET 384u
#define TEGU_RENDER_VERTEX_CODE_OFFSET 1280u
#define TEGU_RENDER_MAX_CODE 4096u
#define TEGU_RENDER_BLEND_HEADER 192u
#define TEGU_RENDER_BLEND_ATTRIBUTE_OFFSET 448u
#define TEGU_RENDER_BLEND_CODE_OFFSET 1344u
#define TEGU_RENDER_LEGACY_MAX_PACKAGE (TEGU_RENDER_VERTEX_CODE_OFFSET+2*TEGU_RENDER_MAX_CODE)
#define TEGU_RENDER_BLEND_MAX_PACKAGE (TEGU_RENDER_BLEND_CODE_OFFSET+2*TEGU_RENDER_MAX_CODE)
#define TEGU_RENDER_TEXTURE_HEADER 256u
#define TEGU_RENDER_TEXTURE_ATTRIBUTE_OFFSET 512u
#define TEGU_RENDER_TEXTURE_CODE_OFFSET 1408u
#define TEGU_RENDER_MAX_PACKAGE (TEGU_RENDER_TEXTURE_CODE_OFFSET+2*TEGU_RENDER_MAX_CODE)
#define TEGU_RENDER_MAX_VARYING 64u
#define TEGU_RENDER_V6_HEADER 512u
#define TEGU_RENDER_V6_PUSH 512u
#define TEGU_RENDER_V6_ATTRIBUTE 768u
#define TEGU_RENDER_V6_CODE 1664u
#define TEGU_RENDER_V6_MAX_STAGE 8192u
#define TEGU_RENDER_V6_MAX_PACKAGE (TEGU_RENDER_V6_CODE+2u*TEGU_RENDER_V6_MAX_STAGE)
#define TEGU_RENDER_V7_PUSH 512u
#define TEGU_RENDER_V7_MAX_PACKAGE (1664u+1152u+25600u)
#define TEGU_RENDER_B3_VERSION 8u
#define TEGU_RENDER_STORAGE_MAX TEGU_RENDER_V7_MAX_PACKAGE
#define TEGU_RENDER_IMPORT_UNSUPPORTED (-8)
struct tegu_render_stage_info {
 uint32_t code_bytes,code_offset,registers,fau_words,sysval_mask,push_bytes;
 uint64_t preload;
};
/* Metadata describes Metal slots, never GPU addresses or hardware indices. */
struct tegu_render_texture_plan {
 uint32_t version,stage,texture_index,sampler_index,descriptor_set,binding,normalized_coordinates;
};
struct tegu_render_compositor_plan {
 uint32_t version,table,descriptor_set,ubo_count,texture_count,sampler_count,sample_count;
 uint32_t texture_stage,texture_index,sampler_stage,sampler_index,descriptor_binding,normalized_coordinates;
 uint32_t ubo_stage[4],ubo_index[4],ubo_window[4],push_residual_offset[4];
 uint32_t texel_format_push_offset,texel_format;
};
struct tegu_render_info {
 uint32_t color_format,push_span[2],version,vertex_mask,vertex_count;
 struct tegu_render_stage_info stages[2];
 struct tegu_vertex_attribute vertex[TEGU_VERTEX_SLOTS];
 /* Version3 paired IDVS vertex binary; all zero for versions1/2. Offsets are
  * relative to the vertex code start, never GPU addresses. */
 uint32_t position_offset,secondary_offset,secondary_registers,varying_bytes;
 uint64_t secondary_preload;
 uint32_t push_offset;
 struct tegu_blend_inputs blend;
 struct tegu_render_texture_plan texture;
 uint32_t color_count,color_formats[8],color_write_masks[8];
 struct tegu_render_compositor_plan compositor;
};
static inline uint32_t tegu_render_word(const unsigned char *p) {
 return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline void tegu_render_put(unsigned char *p,uint32_t v) {
 p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);p[2]=(unsigned char)(v>>16);p[3]=(unsigned char)(v>>24);
}
/* Header words: magic/version/GPU/format/VSbytes/FSbytes/VSpushspan/FSpushspan;
 * 8 words per stage: registers/FAUwords/sysvals/TLS/preloadLo/Hi/pushbytes/reserved;
 * Version1 has8 reserved words; version2 uses mask/count then6 reserved words.
 * Version3 uses mask/count, position offset, secondary offset/registers,
 * secondary preloadLo/Hi and varying bytes; it has the version2 static layout.
 * Version4 appends a64-byte blend block at128: nine little-endian words
 * version/enabled/RGBop/src/dst/alphaop/src/dst/writeMask, then28zero bytes.
 * Push arrays start192, attributes448, code1344. Empty vertex metadata is
 * allowed when no fetch/pair is needed; words26..31 zero means no secondary.
 * Version5 adds a64-byte texture block at192: seven little-endian words
 * version/stage/Metal texture slot/sampler slot/set/binding/normalized, then
 * 36zero bytes. Qualified contract is1/16/0/0/0/0/1, RGBA8, unblended, paired
 * float4 varying. Push arrays256/384, attributes512, code1408.
 * Two128-byte AIR push arrays follow. Versions2/3 add31 padded attribute rows
 * (7 little-endian words each) through offset1280. Then VS code, zero padding to
 * 64bytes, FS code. FAU addresses and viewport/blend state are bound per draw.
 * Unaligned input allowed. Exact length required. Output unchanged on failure. */
static inline int tegu_render_parse_legacy(const void *bytes,size_t size,struct tegu_render_info *out) {
 if(!bytes||!out||size<TEGU_RENDER_CODE_OFFSET||size>TEGU_RENDER_MAX_PACKAGE)return -1;
 const unsigned char *p=(const unsigned char *)bytes;
 uint32_t w[32];for(unsigned i=0;i<32;i++)w[i]=tegu_render_word(p+4*i);
 if((w[1]<4&&size>TEGU_RENDER_LEGACY_MAX_PACKAGE)||(w[1]==4&&size>TEGU_RENDER_BLEND_MAX_PACKAGE))return -1;
 if(w[0]!=TEGU_RENDER_MAGIC||(w[1]!=1&&w[1]!=2&&w[1]!=3&&w[1]!=4&&w[1]!=5)||w[2]!=0xb8a21030u||(w[3]!=70&&w[3]!=80))return -2;
 for(unsigned i=w[1]==1?24:w[1]==2?26:32;i<32;i++)if(w[i])return -2;
 for(unsigned i=4;i<6;i++)if(!w[i]||(w[i]&7)||w[i]>TEGU_RENDER_MAX_CODE)return -2;
 uint32_t code=w[1]==1?TEGU_RENDER_CODE_OFFSET:w[1]==5?TEGU_RENDER_TEXTURE_CODE_OFFSET:w[1]==4?TEGU_RENDER_BLEND_CODE_OFFSET:TEGU_RENDER_VERTEX_CODE_OFFSET;
 uint32_t attributes=w[1]==5?TEGU_RENDER_TEXTURE_ATTRIBUTE_OFFSET:w[1]==4?TEGU_RENDER_BLEND_ATTRIBUTE_OFFSET:384u;
 uint32_t push=w[1]==5?TEGU_RENDER_TEXTURE_HEADER:w[1]==4?TEGU_RENDER_BLEND_HEADER:128u;
 uint32_t fs=code+((w[4]+63u)&~63u);
 if(size!=(size_t)fs+w[5])return -2;
 for(unsigned i=code+w[4];i<fs;i++)if(p[i])return -2;
 struct tegu_render_info info;memset(&info,0,sizeof(info));info.color_format=w[3];info.version=w[1];info.push_offset=push;
 info.blend.version=1;info.blend.rgb_src=1;info.blend.alpha_src=1;info.blend.write_mask=15;
 if(w[1]>=2) {
  if((w[24]&0x80000000u)||w[25]>31||(!w[24])!=(!w[25])||(!w[24]&&w[1]!=4))return -4;
  info.vertex_mask=w[24];info.vertex_count=w[25];uint32_t mask=0;
  for(unsigned i=0;i<w[25];i++) {
   struct tegu_vertex_attribute *a=&info.vertex[i];uint32_t fields[7];
   for(unsigned j=0;j<7;j++)fields[j]=tegu_render_word(p+attributes+i*28+j*4);
   a->location=fields[0];a->buffer_index=fields[1];a->format=fields[2];a->offset=fields[3];
   a->stride=fields[4];a->input_rate=fields[5];a->divisor=fields[6];
   if(a->location>=31||a->buffer_index>=31||(mask&(1u<<a->location))||
      (a->format!=97&&a->format!=100&&a->format!=103&&a->format!=106&&a->format!=109)||
      a->offset>0x7fffffffu||(a->offset&3)||(a->stride&3)||a->input_rate>1||a->divisor>1||
      (!a->input_rate&&a->divisor!=1))return -4;
   for(unsigned j=0;j<i;j++)if(info.vertex[j].buffer_index==a->buffer_index&&
      (info.vertex[j].stride!=a->stride||info.vertex[j].input_rate!=a->input_rate||info.vertex[j].divisor!=a->divisor))return -4;
   mask|=1u<<a->location;
  }
  if(mask!=info.vertex_mask)return -4;
  for(unsigned i=attributes+w[25]*28;i<code;i++)if(p[i])return -4;
 }
 if(w[1]==3||w[1]==5||(w[1]==4&&(w[26]||w[27]||w[28]||w[29]||w[30]||w[31]))) {
  /* Selected position program precedes the IDVS varying program inside VS code. */
  uint64_t preload=(uint64_t)w[29]|(uint64_t)w[30]<<32;
  if(!info.vertex_count||(w[26]&127)||(w[27]&127)||w[26]>=w[27]||w[27]>=w[4]||!w[28]||w[28]>32||
     (preload&~0x7f80000000000000ULL)||!w[31]||(w[31]&3)||w[31]>TEGU_RENDER_MAX_VARYING)return -5;
  info.position_offset=w[26];info.secondary_offset=w[27];info.secondary_registers=w[28];
  info.secondary_preload=preload;info.varying_bytes=w[31];
 }
 if(w[1]>=4) {
  struct tegu_blend_inputs *b=&info.blend;
  b->version=tegu_render_word(p+128);b->enabled=tegu_render_word(p+132);
  b->rgb_op=tegu_render_word(p+136);b->rgb_src=tegu_render_word(p+140);b->rgb_dst=tegu_render_word(p+144);
  b->alpha_op=tegu_render_word(p+148);b->alpha_src=tegu_render_word(p+152);b->alpha_dst=tegu_render_word(p+156);
  b->write_mask=tegu_render_word(p+160);
  for(unsigned i=164;i<192;i++)if(p[i])return -6;
  struct tegu_blend_state checked;
  if(tegu_blend_build_format(info.color_format,b,&checked))return -6;
 }
 if(w[1]==5) {
  struct tegu_render_texture_plan *t=&info.texture;
  t->version=tegu_render_word(p+192);t->stage=tegu_render_word(p+196);
  t->texture_index=tegu_render_word(p+200);t->sampler_index=tegu_render_word(p+204);
  t->descriptor_set=tegu_render_word(p+208);t->binding=tegu_render_word(p+212);
  t->normalized_coordinates=tegu_render_word(p+216);
  if(t->version!=1||t->stage!=16||t->texture_index||t->sampler_index||
     t->descriptor_set||t->binding||t->normalized_coordinates!=1||
     (info.color_format!=70&&info.color_format!=80)||
     info.blend.enabled||!info.secondary_offset||info.varying_bytes!=16)return -7;
  for(unsigned i=220;i<256;i++)if(p[i])return -7;
 }
 for(unsigned i=0;i<2;i++) {
  const uint32_t *s=w+8+8*i;
  struct tegu_render_stage_info *stage=&info.stages[i];
  uint64_t preload=(uint64_t)s[4]|(uint64_t)s[5]<<32;
  if(!s[0]||s[0]>32||s[1]>64||(s[2]&~(i?8u:7u))||s[3]||s[7]||
     (preload&~0x7f80000000000000ULL)||w[6+i]>128||s[6]>w[6+i]||
     (s[6]&&s[1]*4<128+s[6]))return -3;
  /* Each requested backend system value must lie inside the FAU footprint. */
  if(((s[2]&1)&&s[1]<3)||((s[2]&2)&&s[1]<7)||((s[2]&4)&&s[1]<9)||
     ((s[2]&8)&&s[1]<14))return -3;
  for(unsigned j=w[6+i];j<128;j++)if(p[push+128*i+j])return -3;
  info.push_span[i]=w[6+i];stage->code_bytes=w[4+i];stage->code_offset=i?fs:code;
  stage->registers=s[0];stage->fau_words=s[1];stage->sysval_mask=s[2];
  stage->push_bytes=s[6];stage->preload=preload;
 }
 info.color_count=1;info.color_formats[0]=info.color_format;
 info.color_write_masks[0]=info.blend.write_mask;
 *out=info;return 0;
}
/* The new version is dispatched before applying any legacy package-size bound.
 * TGR1..TGR5 continue through their original parser body byte-for-byte. */
#ifdef __cplusplus
extern "C" {
#endif
int tegu_render_parse_v6(const void *,size_t,struct tegu_render_info *);
int tegu_render_parse_v7(const void *,size_t,struct tegu_render_info *);
int tegu_render_parse_b3(const void *,size_t,struct tegu_render_info *);
#ifdef __cplusplus
}
#endif
static inline int tegu_render_parse(const void *bytes,size_t size,struct tegu_render_info *out) {
 if(!bytes||!out||size<8)return -1;
 const unsigned char *p=(const unsigned char *)bytes;
 if(tegu_render_word(p+4)==6)return tegu_render_parse_v6(bytes,size,out);
 if(tegu_render_word(p+4)==7)return tegu_render_parse_v7(bytes,size,out);
 if(tegu_render_word(p+4)==TEGU_RENDER_B3_VERSION)return tegu_render_parse_b3(bytes,size,out);
 return tegu_render_parse_legacy(bytes,size,out);
}
static inline int tegu_render_legacy_target_supported(const struct tegu_render_info *info) {
 return info && info->color_count==1;
}
/* The first compositor submitter may bind only slot0. Pipeline metadata still
 * retains slot1; a pass that binds it is refused until MRT load/store is wired. */
static inline int tegu_render_compositor_pass_supported(
 const struct tegu_render_info *info,uint32_t attachment_mask) {
 if(!info||(info->version!=6&&info->version!=7&&info->version!=TEGU_RENDER_B3_VERSION)||info->color_count!=2||attachment_mask!=1u||
    info->color_formats[0]!=80||info->color_write_masks[0]!=15||
    info->color_formats[1]!=80||info->color_write_masks[1]!=0)return 0;
 if(!info->blend.enabled)return info->blend.version==1&&info->blend.enabled==0&&
    info->blend.rgb_op==(info->version==TEGU_RENDER_B3_VERSION?0u:1u)&&
    info->blend.rgb_src==1&&info->blend.rgb_dst==0&&
    info->blend.alpha_op==(info->version==TEGU_RENDER_B3_VERSION?0u:1u)&&
    info->blend.alpha_src==1&&info->blend.alpha_dst==0&&info->blend.write_mask==15;
 return info->blend.version==1&&info->blend.enabled==1&&info->blend.rgb_op==0&&
    info->blend.rgb_src==1&&info->blend.rgb_dst==5&&info->blend.alpha_op==0&&
    info->blend.alpha_src==1&&info->blend.alpha_dst==5&&info->blend.write_mask==15;
}
static inline int tegu_render_parse_for_import_classes(const void *bytes,size_t size,
 uint32_t v6_enabled,uint32_t v7_enabled,struct tegu_render_info *out) {
 if(!bytes||!out||size<8)return tegu_render_parse(bytes,size,out);
 const uint32_t version=tegu_render_word((const unsigned char *)bytes+4);
 if((version==6&&!v6_enabled)||((version==7||version==TEGU_RENDER_B3_VERSION)&&!v7_enabled))return TEGU_RENDER_IMPORT_UNSUPPORTED;
 return tegu_render_parse(bytes,size,out);
}
/* Existing callers only opt into v6; they never implicitly enable v7. */
static inline int tegu_render_parse_for_import(const void *bytes,size_t size,
 uint32_t v6_enabled,struct tegu_render_info *out) {
 return tegu_render_parse_for_import_classes(bytes,size,v6_enabled,0,out);
}
#endif
