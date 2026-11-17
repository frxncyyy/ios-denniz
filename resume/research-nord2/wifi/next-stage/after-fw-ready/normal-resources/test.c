#include "resources.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void){
 struct ncp_result c={0};struct nnr_plan p,sentinel;struct nnr_budget b,old;
 memset(&sentinel,0xa5,sizeof sentinel);p=sentinel;
 assert(nnr_plan(&c,76,&p)<0&&!memcmp(&p,&sentinel,sizeof p));
 c.decoded=1;c.resources_supported=1;c.resource_version=0x80000001u;
 c.resources[0]=123;c.resources[1]=1;c.resources[2]=456;c.resources[3]=0;c.resources[4]=0;c.resources[5]=255;
 assert(!nnr_plan(&c,76,&p)&&p.tc==4&&p.pse_cost==1&&!p.ple_cost&&!p.pse_page_bytes&&!p.ple_page_bytes);
 assert(!nnr_begin(&p,&b));assert(!nnr_reserve_once(&b)&&b.reserved&&b.pse_remaining==0);old=b;
 assert(nnr_reserve_once(&b)<0&&!memcmp(&b,&old,sizeof b));
 c.resources[1]=UINT32_MAX;c.resources[4]=UINT32_MAX;
 assert(!nnr_plan(&c,76,&p)&&p.pse_page_bytes==65535&&p.ple_page_bytes==65535);
 assert(!nnr_begin(&p,&b)&&!nnr_reserve_once(&b)&&b.pse_remaining==UINT32_MAX-1);
 for(unsigned i=0;i<100;i++)if(i!=76){p=sentinel;assert(nnr_plan(&c,i,&p)<0&&!memcmp(&p,&sentinel,sizeof p));}
 c.resources[1]=0;assert(nnr_plan(&c,76,&p)<0);c.resources[1]=2;c.resources[5]=256;assert(nnr_plan(&c,76,&p)<0);
 c.resources[5]=0;c.resource_version=0;assert(nnr_plan(&c,76,&p)<0);c.resource_version=0x80000002;assert(nnr_plan(&c,76,&p)<0);
 c.resource_version=0x80000001;assert(!nnr_plan(&c,76,&p));assert(!nnr_begin(&p,&b));b.pse_remaining--;old=b;assert(nnr_reserve_once(&b)<0&&!memcmp(&b,&old,sizeof b));
 puts("PASS resource v1 atomic derivation, AXI unit debit, TC4 PLE exclusion, overflow boundaries, one-shot/no refund");
}
