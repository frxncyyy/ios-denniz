#include "card.h"
#include "control_contract.h"
#include "query_contract.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void w32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static unsigned checks;
static void refused(const uint8_t*q,size_t n,const struct niwc_manifest*m,size_t capacity){uint8_t out[20],before[20];size_t used=99;memset(out,0xaa,sizeof out);memcpy(before,out,sizeof out);assert(niwc_card_reply(q,n,m,out,capacity,&used)<0&&used==99&&!memcmp(out,before,sizeof out));checks++;}
int main(void){uint8_t q[NIW_SMALL_BYTES]={0};w32(q,NIW_GET);w32(q+4,13);memcpy(q+15368,"en-test",8);w32(q+15384,12);struct niwc_manifest m={0};
 const unsigned bits[]={0,1,3,13,14,76};
 for(unsigned mask=1;mask<64;mask++){
  memset(&m,0,sizeof m);for(unsigned f=0;f<6;f++)if(mask&(1u<<f))m.entry[m.count++]=(struct niwc_entry){f,NIWC_END_TO_END};
  uint8_t out[20];memset(out,0xaa,sizeof out);size_t used=99;assert(!niwc_card_reply(q,sizeof q,&m,out,sizeof out,&used)&&used==13&&out[13]==0xaa);
  struct niwq_caps caps;assert(!niwq_caps_decode(out,13,&caps)&&caps.count==m.count&&!caps.unconsumed_bit103);
  for(unsigned bit=0;bit<104;bit++){unsigned expected=0;for(unsigned f=0;f<6;f++)if(bits[f]==bit)expected=!!(mask&(1u<<f));assert(((out[bit/8]>>(bit%8))&1)==expected);}
  checks++;
  for(unsigned n=0;n<m.count;n++)for(unsigned op=1;op<=32;op<<=1){m.entry[n].installed_operations^=op;refused(q,sizeof q,&m,13);m.entry[n].installed_operations^=op;}
 }
 m=(struct niwc_manifest){.count=1,.entry={{NIWC_RSN_IE,NIWC_END_TO_END}}};
 for(unsigned cap=0;cap<13;cap++)refused(q,sizeof q,&m,cap);
 for(unsigned bad=0;bad<9;bad++){uint8_t x[NIW_SMALL_BYTES];memcpy(x,q,sizeof x);size_t n=sizeof x;struct niwc_manifest b=m;
  if(bad==0)w32(x,0);if(bad==1)w32(x+4,17);if(bad==2)w32(x+15384,43);if(bad==3)w32(x+15388,1);if(bad==4)n--;if(bad==5)memset(x+15368,'X',16);if(bad==6)b.count=0;if(bad==7)b.entry[0].feature=6;if(bad==8)b.entry[0].installed_operations=127;refused(x,n,&b,13);
 }
 m.count=2;m.entry[1]=m.entry[0];refused(q,sizeof q,&m,13);m.count=7;refused(q,sizeof q,&m,13);
 puts("PASS exact raw13/no-version CARD encoder,63 feature subsets, all missing-operation refusals, unsupported/empty/duplicate/shape/capacity atomic guards; no native readiness.");printf("checks=%u\n",checks);return 0;
}
