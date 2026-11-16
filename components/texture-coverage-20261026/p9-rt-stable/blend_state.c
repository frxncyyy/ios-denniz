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
enum {BF_ONE,BF_SOURCE_COLOR,BF_SOURCE_ALPHA,BF_DEST_COLOR,BF_DEST_ALPHA};
static struct blend_factor blend_factor(unsigned metal,unsigned alpha) {
 static const struct blend_factor factors[10]={
  {BF_ONE,1},{BF_ONE,0},{BF_SOURCE_COLOR,0},{BF_SOURCE_COLOR,1},
  {BF_SOURCE_ALPHA,0},{BF_SOURCE_ALPHA,1},{BF_DEST_COLOR,0},{BF_DEST_COLOR,1},
  {BF_DEST_ALPHA,0},{BF_DEST_ALPHA,1}};
 struct blend_factor f=factors[metal];
 if(alpha&&f.base==BF_SOURCE_COLOR)f.base=BF_SOURCE_ALPHA;
 if(alpha&&f.base==BF_DEST_COLOR)f.base=BF_DEST_ALPHA;
 return f;
}
static int blend_function(unsigned op,struct blend_factor s,struct blend_factor d,unsigned alpha,uint32_t *out) {
 static const unsigned operand_c[]={1,2,5,3,6};
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
int tegu_blend_build(const struct tegu_blend_inputs *in,struct tegu_blend_state *out) {
 if(!in||!out||in->version!=1||in->enabled>1||in->write_mask>15||
    in->rgb_op>2||in->alpha_op>2||in->rgb_src>9||in->rgb_dst>9||in->alpha_src>9||in->alpha_dst>9)return -1;
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
int tegu_blend_build_format(uint32_t metal_format,const struct tegu_blend_inputs *in,struct tegu_blend_state *out) {
 if(metal_format!=10&&metal_format!=43&&metal_format!=70&&metal_format!=80&&metal_format!=115&&
    metal_format!=25&&metal_format!=65&&metal_format!=90&&metal_format!=94)return -1;
 int rc=tegu_blend_build(in,out);if(rc)return rc;
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
 return 0;
}
