#include "mapping.h"
#include <assert.h>
int main(void){struct niwp_channel_map m[]={{1,0xfeed,1,1},{36,0xbeef,2,36}};uint8_t b=99,k=99;uint32_t c=99,f=99,r[15]={99};size_t n=99;
 assert(!niwp_channel_to_mtk(m,2,36,0xbeef,&b,&k)&&b==2&&k==36);
 assert(niwp_channel_to_mtk(m,2,36,0xbeee,&b,&k)==-1&&b==2);
 assert(!niwp_channel_from_mtk(m,2,1,1,&c,&f)&&c==1&&f==0xfeed);
 m[1]=m[0];assert(niwp_channel_from_mtk(m,2,1,1,&c,&f)==-1);
 struct niwp_rate_map t[]={{2,1},{11,5}};uint8_t p[]={0x82,11,2};
 assert(!niwp_rates(t,2,p,3,r,&n)&&n==2&&r[0]==1&&r[1]==5);
 p[1]=12;n=99;r[0]=99;assert(niwp_rates(t,2,p,3,r,&n)==-1&&n==99&&r[0]==99);
 t[1]=t[0];assert(niwp_rates(t,2,p,1,r,&n)==-1);return 0;}
