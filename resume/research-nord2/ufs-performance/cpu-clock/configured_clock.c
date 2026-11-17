#include "configured_clock.h"
/* MT6893 platform.c:32–48,292–309. Mirror pairs are register observations,
 * not independent CPU-domain identities. B is pair4 (21c,a2b0), not pair2. */
const uint64_t n2clk_read_allowlist[10]={
 0x1000c20c,0x1000c22c,0x1000c23c,0x1000c24c,0x1000c21c,
 0x0c53a2a0,0x0c53a2a4,0x0c53a2a8,0x0c53a2ac,0x0c53a2b0
};
enum n2clk_status n2clk_decode(uint32_t con1,uint32_t div,struct n2clk_decoded *o)
{
 if(!o)return N2CLK_BAD_ARGUMENT;
 struct n2clk_decoded zero={0};*o=zero;
 o->con1=con1;o->divider_word=div;o->pcw=con1&0x3fffff;
 o->postdiv_code=(con1>>24)&7;o->divider_code=(div>>17)&31;o->mux=(div>>9)&3;
 unsigned num=1,den=1;
 if(o->mux!=1)return o->status=N2CLK_NON_ARMPLL;
 if(!o->pcw)return o->status=N2CLK_ZERO_PCW;
 /* Vendor default maps every code>=4 to /16; codes4..7 are not individually defined by that switch. Preserve them raw
  * and fail closed rather than turn its fallback into hardware knowledge. */
 if(o->postdiv_code>3)return o->status=N2CLK_UNKNOWN_POSTDIV;
 switch(o->divider_code){
 case 8:case 16:case 24:break;
 case 9:num=3;den=4;break;case 10:num=2;den=4;break;case 11:den=4;break;
 case 17:case 18:case 19:case 20:num=21-o->divider_code;den=5;break;
 case 25:case 26:case 27:case 28:case 29:num=30-o->divider_code;den=6;break;
 default:return o->status=N2CLK_UNKNOWN_DIVIDER;
 }
 /* Exact vendor rounding order; all intermediates promoted before multiply.
  * Maximum PCW*26 fits32bits but decoded arithmetic deliberately uses64bits. */
 uint64_t khz=(((uint64_t)o->pcw*26)>>14)*1000;
 khz/=UINT64_C(1)<<o->postdiv_code;
 o->configured_khz=khz*num/den;
 return o->status=N2CLK_OK;
}
enum n2clk_status n2clk_collect(n2clk_read_fn read,void *ctx,struct n2clk_capture *o)
{
 if(!o)return N2CLK_BAD_ARGUMENT;
 struct n2clk_capture zero={0};*o=zero;
 /* Zero storage for absent decoded pairs must never imply a valid rate. */
 for(unsigned i=0;i<5;i++)o->pairs[i].status=N2CLK_BAD_ARGUMENT;
 if(!read)return o->status=N2CLK_BAD_ARGUMENT;
 for(unsigned p=0;p<3;p++)for(unsigned i=0;i<10;i++){
  uint32_t value=0;o->attempted++;
  int rc=read(ctx,n2clk_read_allowlist[i],&value);o->read_result[p][i]=rc;
  if(rc)return o->status=N2CLK_READ_ERROR;
  o->words[p][i]=value;o->valid[p]|=1u<<i;
 }
 /* Three matching full raw snapshots reject A->B->A sample changes, but
  * cannot prove atomicity or detect DVFS changes between matching samples. */
 for(unsigned p=1;p<3;p++)for(unsigned i=0;i<10;i++)
  if(o->words[p][i]!=o->words[0][i])return o->status=N2CLK_UNSTABLE;
 o->status=N2CLK_OK;
 for(unsigned i=0;i<5;i++){
  enum n2clk_status s=n2clk_decode(o->words[0][i],o->words[0][5+i],&o->pairs[i]);
  if(s!=N2CLK_OK && o->status==N2CLK_OK)o->status=s;
 }
 return o->status;
}
