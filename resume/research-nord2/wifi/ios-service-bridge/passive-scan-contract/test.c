#include "request.h"
#include <assert.h>
#include <string.h>
static void p32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
int main(void){uint8_t p[NIWP_BYTES]={0};struct niwp_request r,old;
 p32(p,1);p32(p+4,3);p32(p+52,2);p32(p+56,1);p32(p+68,400);
 for(unsigned i=0;i<400;i++){p32(p+72+i*12,1);p32(p+76+i*12,i+1);}
 assert(!niwp_request_decode(p,sizeof p,&r));assert(r.bss_type==3&&r.scan_type==2&&r.channel_count==400&&r.channels[399].number==400);
 old=r;for(unsigned i=0;i<sizeof p;i++)assert(niwp_request_decode(p,i,&r)==-1&&!memcmp(&r,&old,sizeof r));
 p32(p+68,401);assert(niwp_request_decode(p,sizeof p,&r)==-1);p32(p+68,400);
 p32(p+72+399*12,0);assert(niwp_request_decode(p,sizeof p,&r)==-1&&!memcmp(&r,&old,sizeof r));
 p32(p+68,0);p32(p+16,33);assert(niwp_request_decode(p,sizeof p,&r)==-1);
 p32(p+16,32);assert(!niwp_request_decode(p,sizeof p,&r));assert(r.scan_type==2&&r.bss_type==3);
 return 0;}
