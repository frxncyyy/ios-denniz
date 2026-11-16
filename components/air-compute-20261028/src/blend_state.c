/* Fixed-function lowering adapted from Mesa pan_blend.c:
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "blend_state.h"
/* Mesa pan_blend.c to_mali_function and v10.xml Blend Function. v10/v12 agree;
 * source-alpha/premultiplied/separate-alpha descriptor paths are tested on v11.
 * Each channel evaluates (negateA ? -A : A) + (negateB ? -B : B)*C. */
struct blend_factor {unsigned base,invert;};
/* BF_CONSTANT: Metal BlendColor/OneMinusBlendColor/BlendAlpha/OneMinus-
 * BlendAlpha (11..14) -> Mesa to_c_factor CONST_COLOR/CONST_ALPHA ->
 * MALI_BLEND_OPERAND_C_CONSTANT: ONE scalar constant (BLEND.blend_constant),
 * so the components an equation uses must be equal (pan_blend_is_
 * homogenous_constant); the draw supplies the value. */
enum {BF_ONE,BF_SOURCE_COLOR,BF_SOURCE_ALPHA,BF_DEST_COLOR,BF_DEST_ALPHA,BF_CONSTANT};
static struct blend_factor blend_factor(unsigned metal,unsigned alpha) {
 static const struct blend_factor factors[15]={
  {BF_ONE,1},{BF_ONE,0},{BF_SOURCE_COLOR,0},{BF_SOURCE_COLOR,1},
  {BF_SOURCE_ALPHA,0},{BF_SOURCE_ALPHA,1},{BF_DEST_COLOR,0},{BF_DEST_COLOR,1},
  {BF_DEST_ALPHA,0},{BF_DEST_ALPHA,1},{BF_ONE,0}/* 10 SourceAlphaSaturated: refused by the caller */,
  {BF_CONSTANT,0},{BF_CONSTANT,1},{BF_CONSTANT,0},{BF_CONSTANT,1}};
 struct blend_factor f=factors[metal];
 if(alpha&&f.base==BF_SOURCE_COLOR)f.base=BF_SOURCE_ALPHA;
 if(alpha&&f.base==BF_DEST_COLOR)f.base=BF_DEST_ALPHA;
 return f;
}
static int blend_function(unsigned op,struct blend_factor s,struct blend_factor d,unsigned alpha,uint32_t *out) {
 static const unsigned operand_c[]={1,2,5,3,6,7};
 /* Bifrost's C=2*source encodes source*dest + dest*source. Alpha
  * factors count as channel colour only when packing the alpha equation. */
 if(op==0&&!s.invert&&!d.invert&&
    ((s.base==BF_DEST_COLOR&&d.base==BF_SOURCE_COLOR)||
     (alpha&&s.base==BF_DEST_ALPHA&&d.base==BF_SOURCE_ALPHA))) {
  *out=0x431;return 0;
 }
 if(s.base!=d.base&&s.base!=BF_ONE&&d.base!=BF_ONE)return -2;
 unsigned a,b,c,invert,neg_a=0,neg_b=0;
 unsigned si=s.invert^(s.base==BF_ONE),di=d.invert^(d.base==BF_ONE);
 if(s.base==BF_ONE&&s.invert) {
  a=1;b=3;c=operand_c[d.base];invert=di;neg_b=op==1;
 } else if(s.base==BF_ONE) {
  a=2;b=3;c=operand_c[d.base];invert=di;neg_b=op==1;neg_a=op==2;
 } else if(d.base==BF_ONE&&d.invert) {
  a=1;b=2;c=operand_c[s.base];invert=si;neg_b=op==2;
 } else if(d.base==BF_ONE) {
  a=3;b=2;c=operand_c[s.base];invert=si;neg_a=op==1;neg_b=op==2;
 } else if(s.invert==d.invert) {
  a=1;b=op==0?1:0;c=operand_c[s.base];invert=si;neg_b=op==2;
 } else {
  a=3;b=op==0?0:1;c=operand_c[s.base];invert=si;neg_b=op==2;neg_a=op==1;
 }
 *out=a|(neg_a<<3)|(b<<4)|(neg_b<<7)|(c<<8)|(invert<<11);
 return 0;
}
static unsigned blend_opaque(unsigned op,struct blend_factor s,struct blend_factor d) {
 return op<2&&s.base==BF_ONE&&!s.invert&&d.base==BF_ONE&&d.invert;
}
unsigned tegu_blend_constant_mask(const struct tegu_blend_inputs *in) {
 if(!in||in->version!=1||!in->enabled)return 0;
 unsigned m=0;
 if(in->rgb_src>=11&&in->rgb_src<=14)m|=in->rgb_src<=12?7u:8u;
 if(in->rgb_dst>=11&&in->rgb_dst<=14)m|=in->rgb_dst<=12?7u:8u;
 /* In the alpha equation BlendColor and BlendAlpha both read the alpha. */
 if((in->alpha_src>=11&&in->alpha_src<=14)||(in->alpha_dst>=11&&in->alpha_dst<=14))m|=8u;
 return m;
}
unsigned tegu_blend_constant_bits(uint32_t f) {
 switch(f){
 case 1:case 10:case 11:case 30:case 31:case 70:case 71:case 80:case 81:return 8;
 case 90:case 94:return 10;
 case 40:case 43:return 5;
 case 42:return 4;
 default:return 0;
 }
}
int tegu_blend_constant_word(uint32_t metal_format,const struct tegu_blend_inputs *in,const float rgba[4],uint32_t *word0) {
 unsigned mask=tegu_blend_constant_mask(in),bits=tegu_blend_constant_bits(metal_format);
 if(!mask)return 0;
 if(!bits||!rgba||!word0)return -1;
 float c=-1;
 for(unsigned i=0;i<4;i++)if(mask&(1u<<i)){if(c<0)c=rgba[i];else if(rgba[i]!=c)return -2;}
 if(!(c>=0))c=0;if(c>1)c=1; /* unorm targets clamp the blend colour */
 /* Mesa pan_pack_blend_constant: UNORM of the channel size, MSB-aligned. */
 uint32_t factor=((1u<<bits)-1u)<<(16u-bits);
 uint32_t v=(uint32_t)(c*(float)factor);
 *word0=(*word0&0xffffu)|(v<<16);
 return 0;
}
int tegu_blend_build(const struct tegu_blend_inputs *in,struct tegu_blend_state *out) {
 if(!in||!out||in->version!=1||in->enabled>1||in->write_mask>15||
    in->rgb_op>2||in->alpha_op>2||in->rgb_src>14||in->rgb_dst>14||in->alpha_src>14||in->alpha_dst>14||
    in->rgb_src==10||in->rgb_dst==10||in->alpha_src==10||in->alpha_dst==10)return -1;
 struct tegu_blend_state state={{0},0,1};
 uint32_t rgb=0x122,alpha=0x122;
 if(in->enabled) {
  struct blend_factor rs=blend_factor(in->rgb_src,0),rd=blend_factor(in->rgb_dst,0);
  struct blend_factor as=blend_factor(in->alpha_src,1),ad=blend_factor(in->alpha_dst,1);
  if(blend_function(in->rgb_op,rs,rd,0,&rgb)||blend_function(in->alpha_op,as,ad,1,&alpha))return -2;
  state.opaque=blend_opaque(in->rgb_op,rs,rd)&&blend_opaque(in->alpha_op,as,ad);
  if(state.opaque)rgb=alpha=0x122;
  else state.reads_destination=rd.base!=BF_ONE||!rd.invert||ad.base!=BF_ONE||!ad.invert||
   rs.base==BF_DEST_COLOR||rs.base==BF_DEST_ALPHA||as.base==BF_DEST_ALPHA;
 }
 state.words[0]=0xa00|state.reads_destination;
 /* Metal bits are A,B,G,R; Mali color_mask bits are R,G,B,A. */
 unsigned mask=((in->write_mask&8)>>3)|((in->write_mask&4)>>1)|((in->write_mask&2)<<1)|((in->write_mask&1)<<3);
 state.words[1]=(mask<<28)|rgb|(alpha<<12);
 state.words[2]=0x18|(state.opaque?1:2);state.words[3]=237u<<12;
 *out=state;return 0;
}
/* Mesa MALI_BLEND_PU_R8G8B8A8 uses the same logical RGBA tile format for
 * RGBA8Unorm and BGRA8Unorm. BGRA memory ordering is selected at writeback. */
/* Raw (non-blendable) colour targets: Mesa pan_dithered_format_from_pipe_
 * format falls back to pan_format_from_pipe_format(f)->hw, the format's own
 * pixel format (mali << 12 | component order; R001/RG01 are RGB1), which the
 * opaque blend conversion stores. Only opaque output: Mesa needs a blend
 * shader for anything else, which this path refuses. */
static uint32_t tegu_blend_raw_memory_format(uint32_t f) {
 switch(f) {
 case 12:return (99u<<12)|16u;   case 13:return (131u<<12)|16u;  case 14:return (195u<<12)|16u;
 case 20:return (164u<<12)|16u;  case 22:return (100u<<12)|16u;  case 23:return (132u<<12)|16u;
 case 24:return (196u<<12)|16u;  case 32:return (107u<<12)|16u;  case 33:return (139u<<12)|16u;
 case 34:return (203u<<12)|16u;  case 53:return (133u<<12)|16u;  case 54:return (197u<<12)|16u;
 case 55:return (167u<<12)|16u;  case 60:return (172u<<12)|16u;  case 62:return (108u<<12)|16u;
 case 63:return (140u<<12)|16u;  case 64:return (204u<<12)|16u;  case 72:return 123u<<12;
 case 73:return 155u<<12;        case 74:return 219u<<12;        case 91:return 71u<<12;
 case 103:return (141u<<12)|16u; case 104:return (205u<<12)|16u; case 105:return (175u<<12)|16u;
 case 110:return 188u<<12;       case 112:return 124u<<12;       case 113:return 156u<<12;
 case 114:return 220u<<12;       case 123:return 157u<<12;       case 124:return 221u<<12;
 case 125:return 191u<<12;
 default:return 0;
 }
}
int tegu_blend_build_format(uint32_t metal_format,const struct tegu_blend_inputs *in,struct tegu_blend_state *out) {
 uint32_t raw=tegu_blend_raw_memory_format(metal_format);
 if(raw) {
  if(!in||in->version!=1||in->enabled||in->write_mask!=15)return -2;
  struct tegu_blend_state built;int rc=tegu_blend_build(in,&built);if(rc)return rc;
  if(!built.opaque||(built.words[2]&3u)!=1u)return -2;
  built.words[3]=raw;*out=built;return 0;
 }
 if(metal_format!=10&&metal_format!=43&&metal_format!=70&&metal_format!=80&&metal_format!=115&&
    metal_format!=25&&metal_format!=65&&metal_format!=90&&metal_format!=94&&
    metal_format!=40&&metal_format!=42&&metal_format!=71&&metal_format!=81&&metal_format!=92&&
    metal_format!=1&&metal_format!=11&&metal_format!=30&&metal_format!=31)return -1;
 /* B5G6R5 and RG11B10Float have no alpha: Metal reads their destination
  * alpha as 1, the R5G6B5A0/RG11F_B10F tile has none to read. Refuse
  * destination-alpha factors there instead of blending with a wrong value. */
 if((metal_format==40||metal_format==92)&&in&&in->version==1&&in->enabled) {
  if(in->rgb_src==8||in->rgb_src==9||in->rgb_dst==8||in->rgb_dst==9||
     in->alpha_src==6||in->alpha_src==7||in->alpha_src==8||in->alpha_src==9||
     in->alpha_dst==6||in->alpha_dst==7||in->alpha_dst==8||in->alpha_dst==9)return -2;
 }
 /* Mali has no float blend constant (Mesa pan_pack_blend_constant asserts a
  * non-float format): constant factors only on unorm targets. */
 if(tegu_blend_constant_mask(in)&&tegu_blend_constant_bits(metal_format)==0)return -2;
 struct tegu_blend_state built;
 int rc=tegu_blend_build(in,&built);if(rc)return rc;
 *out=built;
 if(in->write_mask!=15){out->reads_destination=1;out->opaque=0;out->words[0]|=1;out->words[2]=(out->words[2]&~3u)|2;}
 if(metal_format==115)out->words[3]=223u<<12; /* Mesa MALI_BLEND_RGBA16F */
 /* BGR5A1Unorm: Mesa MALI_BLEND_PU_R5G5B5A1 (not dithered), RGBA order. */
 if(metal_format==43)out->words[3]=225u<<12;
 /* Mesa v9 pan_blendable_formats: BFMT_FLOAT(R16_FLOAT, R16F) and
  * (R16G16_FLOAT, RG16F) -> MALI_BLEND_R16F/RG16F; BFMT(R10G10B10A2_UNORM /
  * B10G10R10A2_UNORM, R10G10B10A2) -> MALI_BLEND_PU_R10G10B10A2 = RGB10_A2_TB,
  * RGBA order (BGR memory order is the RT swizzle, as for BGRA8). */
 if(metal_format==25)out->words[3]=199u<<12;
 if(metal_format==65)out->words[3]=207u<<12;
 if(metal_format==90||metal_format==94)out->words[3]=238u<<12;
 /* BFMT2(B5G6R5_UNORM, R5G6B5A0, R5G6B5): MALI_BLEND_PU_R5G6B5A0 =
  * R5G6B5_PU (227) with RGB1 order; fixed-function blending takes RGBA
  * order instead (pan_cmdstream "fixed function does not like RGB1"). */
 if(metal_format==40)out->words[3]=(227u<<12)|((out->words[2]&3u)==1u?16u:0u);
 /* BFMT(A4B4G4R4_UNORM, R4G4B4A4): MALI_BLEND_PU_R4G4B4A4 = RGBA4_PU (233). */
 if(metal_format==42)out->words[3]=233u<<12;
 /* BFMT_FLOAT(R11G11B10_FLOAT, RG11F_B10F): R11F_G11F_B10F (89). */
 if(metal_format==92)out->words[3]=89u<<12;
 /* BFMT_SRGB: R8G8B8A8 blend format with the sRGB flag (bit 20), and the
  * Blend descriptor's sRGB bit (word0 bit 10, pan_cmdstream cfg.srgb). */
 if(metal_format==71||metal_format==81||metal_format==11||metal_format==31){out->words[3]=(237u<<12)|(1u<<20);out->words[0]|=1u<<10;}
 /* A8 / RG8: BFMT2(A8_UNORM, R8G8B8A8, R8), BFMT_SRGB(R8G8, R8G8): the
  * R8G8B8A8 blend format (the default word3). */
 return 0;
}
