#define SUBMISSION_MAIN existing_submission_main
#include "test-security-submission.c"
#undef SUBMISSION_MAIN
#include "packets.h"
static void actual_ether(struct test*h,const uint8_t*eth,size_t n,unsigned group,unsigned pn,unsigned bad){
 uint8_t p[56+1518]={0};assert(n<=1518);put16(p,(unsigned)n+56);put16(p+2,2u<<11);put32(p+4,(group?h->s.peer.group_wlan:h->s.peer.wlan)|(9u<<11)|(4u<<16)|((group?h->s.gtk.key_id:h->s.ptk.key_id)<<21));put32(p+8,1u<<13);put32(p+12,6u<<8);put16(p+24,0x4208);memcpy(p+26,h->s.peer.ap,6);p[40]=(uint8_t)pn;memcpy(p+56,eth,n);if(bad)p[26]^=2;deliver(h,0,p,(unsigned)n+56);
}
