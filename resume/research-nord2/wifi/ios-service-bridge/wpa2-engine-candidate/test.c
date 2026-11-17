#include <assert.h>
#include <stdio.h>
#include "wifi_wpa2.c" /* exercise actual compiled private arithmetic, no mirror */
#include "vectors.h"
static unsigned checks,rng_calls;
static int rng(void*x,uint8_t*p,size_t n){(void)x;assert(n==32);memcpy(p,sn,n);rng_calls++;return 0;}
static int badrng(void*x,uint8_t*p,size_t n){(void)x;memset(p,0xa5,n);return -1;}
static void gone(struct nwp*w){struct nwp z={0};assert(!memcmp(w,&z,sizeof z));checks++;}
static void begin(struct nwp*w){assert(!nwp_begin(w,42,pmk_vector,own,peer,rsn,rsn,rng,0));checks++;}
static void wait3(struct nwp*w){struct nwp_result r;begin(w);assert(nwp_receive(w,m1,sizeof m1,&r)==NWP_TX&&r.bytes==121&&equal(r.frame,m2_oracle,121));assert(equal(w->kck,ptk,16)&&equal(w->kek,ptk+16,16)&&equal(w->tk,ptk+32,16));assert(nwp_tx_done(w,42,r.ticket,1)==NWP_DROP);checks+=3;}
static void signed_bad(struct nwp*w,uint8_t*p,unsigned n){struct nwp_result r;mic(w,p,n,p+81);assert(nwp_receive(w,p,n,&r)==NWP_FAIL);gone(w);checks++;}
int main(void){struct nwp w;struct nwp_result r;uint8_t p[NWP_FRAME],key[32],plain[32];
 assert(!nwp_pmk((const uint8_t*)"password",8,(const uint8_t*)"IEEE",4,key)&&equal(key,pmk_vector,32));checks++;
 assert(!aes_unwrap(rfc_kek,rfc_wrapped,sizeof rfc_wrapped,plain)&&equal(plain,rfc_plain,16));checks++;
 begin(&w);assert(nwp_keys_ready(&w,42,w.install_ticket,7,1)==NWP_DROP);checks++;
 assert(nwp_receive(&w,m1,sizeof m1,&r)==NWP_TX);uint64_t ticket=r.ticket;uint8_t m2[256];memcpy(m2,r.frame,r.bytes);unsigned m2n=r.bytes;checks++;
 assert(nwp_receive(&w,m1,sizeof m1,&r)==NWP_BUSY);assert(nwp_tx_done(&w,43,ticket,1)==NWP_DROP&&w.pending==ticket);checks+=2;
 assert(nwp_tx_done(&w,42,ticket,1)==NWP_DROP);assert(nwp_receive(&w,m1,sizeof m1,&r)==NWP_TX&&r.bytes==m2n&&equal(r.frame,m2,m2n));assert(rng_calls==1);checks+=3;
 assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_DROP);assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_TX&&r.bytes==99&&equal(r.frame,m4_oracle,99)&&equal(w.gtk,gtk,16));ticket=r.ticket;checks+=2;
 assert(w.state==NWP_M4_TX&&!w.installed);assert(nwp_keys_ready(&w,42,w.install_ticket,7,1)==NWP_DROP);assert(nwp_tx_done(&w,42,ticket,1)==NWP_INSTALL);assert(nwp_tx_done(&w,42,ticket,1)==NWP_DROP);checks+=4;
 assert(nwp_keys_ready(&w,41,w.install_ticket,7,1)==NWP_DROP);assert(nwp_keys_ready(&w,42,w.install_ticket+1,7,1)==NWP_DROP);assert(nwp_keys_ready(&w,42,w.install_ticket,1,1)==NWP_DROP);assert(nwp_keys_ready(&w,42,w.install_ticket,2,1)==NWP_DROP);assert(nwp_keys_ready(&w,42,w.install_ticket,4,1)==NWP_AUTH);checks+=3;checks+=2;
 assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_TX);assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_DROP&&w.state==NWP_AUTHORIZED&&w.installed==1);checks+=2;
 nwp_cancel(&w);gone(&w);
 /* Every truncation, trailing byte, and descriptor bit malformed. */
 for(unsigned n=0;n<sizeof m3;n++){wait3(&w);assert(nwp_receive(&w,m3,n,&r)==NWP_FAIL);gone(&w);checks++;}
 for(unsigned i=0;i<sizeof m3;i++){wait3(&w);memcpy(p,m3,sizeof m3);p[i]^=0x80;assert(nwp_receive(&w,p,sizeof m3,&r)==NWP_FAIL);gone(&w);checks++;}
 wait3(&w);memcpy(p,m3,sizeof m3);p[sizeof m3]=0;assert(nwp_receive(&w,p,sizeof m3+1,&r)==NWP_FAIL);gone(&w);checks++;
 const uint8_t*bad[]={bad_rsn,duplicate_gtk,bad_kde_length};const unsigned lens[]={sizeof bad_rsn,sizeof duplicate_gtk,sizeof bad_kde_length};
 for(unsigned i=0;i<3;i++){wait3(&w);assert(nwp_receive(&w,bad[i],lens[i],&r)==NWP_FAIL);gone(&w);checks++;}
 wait3(&w);memcpy(p,m3,sizeof m3);p[16]=1;signed_bad(&w,p,sizeof m3); /* replay equal to M1 */
 wait3(&w);memcpy(p,m3,sizeof m3);p[17]^=1;signed_bad(&w,p,sizeof m3); /* valid MIC, wrong ANonce */
 wait3(&w);assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_TX);assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_INSTALL);memcpy(p,m3,sizeof m3);p[16]=3;mic(&w,p,sizeof m3,p+81);assert(nwp_receive(&w,p,sizeof m3,&r)==NWP_TX);assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_DROP&&w.state==NWP_KEYS&&w.installed==1);checks+=2; /* higher counter retry does not reinstall */
 wait3(&w);memcpy(p,m1,sizeof m1);p[16]=0;assert(nwp_receive(&w,p,sizeof m1,&r)==NWP_FAIL);gone(&w);checks++;
 wait3(&w);assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_TX);assert(nwp_tx_done(&w,42,r.ticket,0)==NWP_FAIL);gone(&w);checks++;
 for(unsigned mask=0;mask<7;mask++){wait3(&w);assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_TX);assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_INSTALL);assert(nwp_keys_ready(&w,42,w.install_ticket,7,0)==NWP_FAIL);gone(&w);checks++;}
 assert(nwp_begin(&w,42,pmk_vector,own,peer,rsn,rsn,badrng,0)<0);gone(&w);checks++;
 for(unsigned i=0;i<22;i++){memcpy(p,rsn,22);p[i]^=1;assert(nwp_begin(&w,42,pmk_vector,own,peer,p,rsn,rng,0)<0);gone(&w);checks++;}
 assert(nwp_pmk((const uint8_t*)"short",5,(const uint8_t*)"IEEE",4,key)<0&&!nonzero(key,32));checks++;
 /* M1 retransmit with higher counter retains keys/nonces; stale callback
 * tickets never retire a newer transmission. Cancel wipes every state. */
 wait3(&w);memcpy(p,m1,sizeof m1);p[16]=2;assert(nwp_receive(&w,p,sizeof m1,&r)==NWP_TX);uint64_t newer=r.ticket;assert(equal(w.tk,ptk+32,16)&&equal(w.snonce,sn,32));assert(nwp_tx_done(&w,42,newer-1,0)==NWP_DROP&&w.pending==newer);assert(nwp_tx_done(&w,42,newer,1)==NWP_DROP);checks+=4;nwp_cancel(&w);gone(&w);
 for(unsigned phase=0;phase<4;phase++){begin(&w);if(phase){assert(nwp_receive(&w,m1,sizeof m1,&r)==NWP_TX);assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_DROP);}if(phase>=2){assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_TX);}if(phase>=3)assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_INSTALL);nwp_cancel(&w);gone(&w);assert(nwp_receive(&w,m1,sizeof m1,&r)==NWP_FAIL);checks++;}
 wait3(&w);assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_TX);assert(nwp_tx_done(&w,42,r.ticket,1)==NWP_INSTALL);memcpy(p,m3,sizeof m3);p[16]=3;p[65]=1;signed_bad(&w,p,sizeof m3); /* signed changed RSC is NOT a safe retry */
 for(unsigned n=0;n<sizeof m1;n++){begin(&w);assert(nwp_receive(&w,m1,n,&r)==NWP_FAIL);gone(&w);checks++;}
 wait3(&w);memcpy(p,m3,sizeof m3);p[sizeof m3-1]^=1;signed_bad(&w,p,sizeof m3); /* valid MIC, wrap integrity failure */
 wait3(&w);w.serial=UINT64_MAX;assert(nwp_receive(&w,m3,sizeof m3,&r)==NWP_FAIL);gone(&w);struct nwp_result zr={0};assert(!memcmp(&r,&zr,sizeof r));checks+=2;
 begin(&w);assert(nwp_begin(&w,0,pmk_vector,own,peer,rsn,rsn,rng,0)<0);gone(&w);checks++;
 printf("PASS %u WPA2 crypto/state/async/zeroization checks; synthetic/public vectors only\n",checks);return 0;
}
