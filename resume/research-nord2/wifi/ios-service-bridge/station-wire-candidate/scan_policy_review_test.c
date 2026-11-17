/* Independent read-only review harness. Synthetic buffers only. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "wifi_ios_policy.h"
static unsigned checks;
static void put(uint8_t *p,uint32_t x){p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static void fixture(uint8_t p[5456],struct nss_info *i){
 memset(p,0,5456);memset(i,0,sizeof(*i));put(p,1);put(p+4,3);put(p+52,1);put(p+56,1);
 put(p+68,13);put(p+4880,UINT32_C(0x80000000));i->count=13;
 for(unsigned n=0;n<13;n++){put(p+72+12*n,1);put(p+76+12*n,n+1);put(p+80+12*n,138);
 i->channels[n].band=1;i->channels[n].channel=n+1;}
}
static void no(const uint8_t*p,size_t n,const struct nss_info*i){struct nss_request r,b;
 memset(&r,0xa5,sizeof r);b=r;assert(niwi_scan_admit(p,n,i,&r)<0);assert(!memcmp(&r,&b,sizeof r));checks++;}
static void yes(const uint8_t*p,const struct nss_info*i,unsigned count){struct nss_request r;
 memset(&r,0xa5,sizeof r);assert(niwi_scan_admit(p,5456,i,&r)==0);assert(r.count==count);
 for(unsigned n=count;n<NSS_CHANNELS;n++)assert(!r.channels[n].band&&!r.channels[n].channel);checks++;}
int main(void){uint8_t p[5456],base[5456],power[24];struct nss_info info,baseline;
 fixture(base,&baseline);info=baseline;memcpy(p,base,sizeof p);yes(p,&info,13);
 for(size_t n=0;n<5456;n++)no(p,n,&info);no(p,SIZE_MAX,&info);no(p,5457,&info);no(NULL,5456,&info);no(p,5456,NULL);
 assert(niwi_scan_admit(p,5456,&info,NULL)<0);checks++;
 put(p+68,0);memset(p+72,0xa5,4800);yes(p,&info,0);
 memcpy(p,base,sizeof p);put(p+4880,0);yes(p,&info,13);memset(p+8,255,6);yes(p,&info,13);
 for(unsigned x=0;x<6;x++){memcpy(p,base,sizeof p);p[8+x]=2;no(p,5456,&info);}
 info.count=0;no(base,5456,&info);info=baseline;info.count=NSS_CHANNELS+1;no(base,5456,&info);
 info=baseline;info.channels[1]=info.channels[0];no(base,5456,&info);
 info=baseline;info.channels[1].band=3;no(base,5456,&info);
 info=baseline;info.channels[1].channel=0;no(base,5456,&info);
 info=baseline;memcpy(p,base,sizeof p);put(p+76+12,1);no(p,5456,&info);
 put(p+68,NSS_CHANNELS+1);no(p,5456,&info);
 memcpy(p,base,sizeof p);put(p+80,UINT32_C(0x8000008a));no(p,5456,&info);
 memcpy(p,base,sizeof p);put(p+4876,1);no(p,5456,&info);
 memcpy(p,base,sizeof p);put(p+4884,1);no(p,5456,&info);
 memcpy(p,base,sizeof p);put(p+5448,1);no(p,5456,&info);
 memcpy(p,base,sizeof p);put(p+16,1);no(p,5456,&info);
 /* Fail in the final requested channel, after filling previous local rows. */
 memcpy(p,base,sizeof p);put(p+76+12*12,255);no(p,5456,&info);
 memset(power,0xa5,sizeof power);put(power,1);put(power+4,1);put(power+8,1);
 assert(niwi_power_on_admit(power,24)==0);checks++;
 for(unsigned k=0;k<3;k++){uint32_t v=k==0?2:k==1?0:0;uint8_t q[24];memcpy(q,power,24);put(q+4*k,v);assert(niwi_power_on_admit(q,24)<0);checks++;}
 assert(niwi_power_on_admit(power,25)<0);assert(niwi_power_on_admit(power,SIZE_MAX)<0);assert(niwi_power_on_admit(NULL,24)<0);checks+=3;
 printf("PASS independent scan-policy review: %u checks; synthetic only\n",checks);return 0;}
