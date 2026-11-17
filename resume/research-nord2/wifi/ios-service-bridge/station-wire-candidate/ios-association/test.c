#include "association.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "reference-layout.h"
_Static_assert(sizeof(struct apple80211_assoc)==SWIA_BYTES,"original reference extent");
_Static_assert(offsetof(struct apple80211_assoc,ssid_length)==12,"original SSID length");
_Static_assert(offsetof(struct apple80211_assoc,bssid)==48,"original target");
_Static_assert(offsetof(struct apple80211_assoc,key_version)==56,"original key version");
_Static_assert(offsetof(struct apple80211_assoc,key)==72,"original key");
_Static_assert(offsetof(struct apple80211_assoc,rest)==136,"original rest ignored by reference");
static unsigned checks;
static void p16(uint8_t*p,uint16_t x){p[0]=x;p[1]=x>>8;}
static void p32(uint8_t*p,uint32_t x){p16(p,x);p16(p+2,x>>16);}
static void zeros(const void*v,size_t n){const uint8_t*p=v;for(size_t i=0;i<n;i++)assert(!p[i]);}
static void fixture(uint8_t p[592]){
 static const uint8_t rsn[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
 memset(p,0,592);p32(p,1);p16(p+4,2);p16(p+6,1);p16(p+8,8);p32(p+12,7);
 memcpy(p+16,"fixture",7);p32(p+56,1);p32(p+60,32);p32(p+64,6);
 for(unsigned i=0;i<32;i++)p[72+i]=(uint8_t)(i+1); /* public synthetic fixture */
 p16(p+204,22);memcpy(p+206,rsn,22);
}
static void no(const uint8_t*p,size_t n,enum swia_reason reason){struct swia_join out;struct swia_metadata meta;
 memset(&out,0xa5,sizeof out);assert(swia_decode_pmk(p,n,&out,sizeof out,&meta)<0);zeros(&out,sizeof out);
 assert(meta.reason==(unsigned)reason);checks++;}
static void yes(const uint8_t*p){struct swia_join out;struct swia_metadata meta;
 memset(&out,0xa5,sizeof out);assert(swia_decode_pmk(p,592,&out,sizeof out,&meta)==SWIA_OK);
 assert(!meta.reason&&out.ssid_length==7&&out.auth_lower==1&&out.auth_upper==8);
 assert(!memcmp(out.pmk,p+72,32));assert(!memcmp(out.ssid,"fixture",7));swia_clear(&out,sizeof out);zeros(&out,sizeof out);checks++;}
int main(void){uint8_t p[592],base[592],b[64];struct swia_join out;struct swia_metadata meta;size_t used;
 fixture(base);yes(base);assert(!memcmp(base+206,oracle_rsn,sizeof oracle_rsn));checks++;
 {struct apple80211_assoc original={0};original.version=1;original.mode=2;original.auth_lower=1;
  original.auth_upper=8;original.ssid_length=7;memcpy(original.ssid,"fixture",7);
  original.key_version=1;original.key_length=32;original.key_cipher=6;
  for(unsigned i=0;i<32;i++)original.key[i]=(uint8_t)(i+1);
  assert(!memcmp(base,&original,offsetof(struct apple80211_assoc,rest)));checks++;
  swia_clear(&original,sizeof original);}
 for(size_t n=0;n<592;n++)no(base,n,SWIA_R_SHAPE);
 no(base,593,SWIA_R_SHAPE);no(base,SIZE_MAX,SWIA_R_SHAPE);no(NULL,592,SWIA_R_SHAPE);
 for(size_t n=0;n<sizeof out;n++){memset(&out,0xa5,sizeof out);assert(swia_decode_pmk(base,592,&out,n,&meta)==SWIA_CAPACITY);
  zeros(&out,n);assert(((uint8_t*)&out)[n]==0xa5);checks++;}
 /* Inactive storage and alignment bytes must not recreate scan's zero-sweep bug. */
 for(unsigned i=0;i<592;i++){
  int inactive=(i>=10&&i<12)||(i>=23&&i<48)||(i>=54&&i<56)||(i>=104&&i<204)||
    (i>=228&&i<463)||(i>=466&&i<468)||(i>=472&&i<504)||(i>=508&&i<514)||
    (i>=515&&i<536)||(i>=540&&i<580);
  if(inactive){memcpy(p,base,sizeof p);p[i]=0xa5;yes(p);}
 }
 const unsigned auth[]={4,6,8};for(unsigned i=0;i<3;i++){memcpy(p,base,sizeof p);p16(p+auth[i],0x1234);no(p,592,SWIA_R_AUTH);}
 const unsigned cred[]={56,60,64,68,70};for(unsigned i=0;i<5;i++){memcpy(p,base,sizeof p);p16(p+cred[i],0x1234);no(p,592,SWIA_R_CREDENTIAL);}
 memcpy(p,base,sizeof p);p32(p+12,33);no(p,592,SWIA_R_SHAPE);p32(p+12,0);no(p,592,SWIA_R_SHAPE);
 memcpy(p,base,sizeof p);p[48]=255;no(p,592,SWIA_R_SHAPE);
 memcpy(p,base,sizeof p);p[48]=2;p[53]=1;p[463]=0x20;yes(p);
 memcpy(p,base,sizeof p);p[463]=0x20;no(p,592,SWIA_R_ASSOC_OPTIONS);
 for(unsigned i=204;i<228;i++){memcpy(p,base,sizeof p);p[i]^=1;no(p,592,SWIA_R_RSN);}
 for(unsigned i=463;i<=465;i++){memcpy(p,base,sizeof p);p[i]=1;no(p,592,SWIA_R_ASSOC_OPTIONS);}
 for(unsigned bit=0;bit<32;bit++){memcpy(p,base,sizeof p);p32(p+504,UINT32_C(1)<<bit);no(p,592,SWIA_R_NATIVE_OPTIONS);}
 memcpy(p,base,sizeof p);p32(p+468,32);no(p,592,SWIA_R_CACHE);
 memcpy(p,base,sizeof p);p[514]=1;no(p,592,SWIA_R_PRIVATE_OPTIONS);p[514]=0;p32(p+536,1);no(p,592,SWIA_R_PRIVATE_OPTIONS);
 const unsigned tail[]={580,584,588};for(unsigned i=0;i<3;i++){memcpy(p,base,sizeof p);p32(p+tail[i],1);no(p,592,SWIA_R_TRAILING_OPTIONS);}
 /* Passphrase profile carries another derived PMK; deliberately unsupported. */
 memcpy(p,base,sizeof p);p32(p+60,12);p32(p+64,10);p32(p+468,32);no(p,592,SWIA_R_CREDENTIAL);
 const unsigned codes[]={0,1,2,9,21};const uint8_t name[16]="en0";
 for(unsigned i=0;i<5;i++){
  memset(b,0xa5,sizeof b);assert(swia_result4(b,sizeof b,&used,7,7,codes[i])==0&&used==4&&b[0]==codes[i]);checks++;
  if(codes[i]){assert(swia_event9(b,sizeof b,&used,name,7,7,codes[i])==0&&used==48);assert(b[0]==87&&b[28]==9);zeros(b+24,4);checks++;}
 }
 for(unsigned n=0;n<48;n++){memset(b,0xa5,sizeof b);assert(swia_event9(b,n,&used,name,7,7,1)==SWIA_CAPACITY&&!used);
  for(unsigned i=0;i<sizeof b;i++)assert(b[i]==0xa5);checks++;}
 for(unsigned n=0;n<4;n++){memset(b,0xa5,sizeof b);assert(swia_result4(b,n,&used,7,7,1)==SWIA_CAPACITY&&!used);
  for(unsigned i=0;i<sizeof b;i++)assert(b[i]==0xa5);checks++;}
 assert(swia_result4(b,64,&used,7,8,1)==SWIA_STALE&&!used);assert(swia_event9(b,64,&used,name,7,8,1)==SWIA_STALE&&!used);checks+=2;
 assert(swia_result4(b,64,&used,0,0,1)==SWIA_STALE);assert(swia_result4(b,64,&used,7,7,99)==SWIA_INVALID);checks+=2;
 assert(swia_event9(b,64,&used,name,7,7,0)==SWIA_INVALID);checks++;
 {uint8_t tainted_name[16];memset(tainted_name,0xa5,sizeof tainted_name);memcpy(tainted_name,"en0",4);
  assert(swia_event9(b,64,&used,tainted_name,7,7,1)==0);zeros(b+35,13);checks++;
  memset(tainted_name,'x',16);assert(swia_event9(b,64,&used,tainted_name,7,7,1)==SWIA_INVALID);checks++;
  tainted_name[0]=0;assert(swia_event9(b,64,&used,tainted_name,7,7,1)==SWIA_INVALID);checks++;}
 assert(swia_decode_pmk(base,592,NULL,sizeof out,&meta)==SWIA_CAPACITY);checks++;
 assert(swia_result4(NULL,4,&used,7,7,1)==SWIA_INVALID&&!used);checks++;
 assert(swia_event9(NULL,48,&used,name,7,7,1)==SWIA_INVALID&&!used);checks++;
 assert(swia_disconnect22(22,0,0)==0);assert(swia_disconnect22(22,1,0)==SWIA_UNSUPPORTED);
 assert(swia_disconnect22(22,0,8)==SWIA_UNSUPPORTED);assert(swia_disconnect22(20,0,0)==SWIA_UNSUPPORTED);checks+=4;
 printf("PASS %u bounded association/result/event/disconnect/zeroization checks; synthetic only\n",checks);return 0;}
