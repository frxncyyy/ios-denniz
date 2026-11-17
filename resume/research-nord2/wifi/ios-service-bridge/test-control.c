#include "control_contract.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static void put(uint8_t*p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=x>>(i*8);}
int main(void){
 uint8_t *p=calloc(1,NIW_LARGE_BYTES);assert(p);struct niw_request q,old;
 for(unsigned large=0;large<2;large++){
  size_t n=large?NIW_LARGE_BYTES:NIW_SMALL_BYTES,cap=large?NIW_LARGE_PAYLOAD:NIW_SMALL_PAYLOAD;unsigned sel=large?6:0;
  memset(p,0,n);put(p,NIW_GET);memcpy(p+8+cap,"en0",4);put(p+8+cap+16,43);
  assert(!niw_decode(sel,p,n,&q)&&q.request==43&&q.payload_capacity==cap);
  assert(niw_unbound_request(&q)<0);old=q;
  for(size_t i=0;i<n;i++)assert(niw_decode(sel,p,i,&q)<0&&!memcmp(&q,&old,sizeof q));
  put(p+4,(uint32_t)cap);assert(!niw_decode(sel,p,n,&q));old=q;
  put(p+4,(uint32_t)cap+1);assert(niw_decode(sel,p,n,&q)<0&&!memcmp(&q,&old,sizeof q));
  put(p+4,0);memset(p+8+cap,'x',16);assert(niw_decode(sel,p,n,&q)<0);p[8+cap]=0;assert(niw_decode(sel,p,n,&q)<0);
 }
 assert(niw_call_shape(0,NIW_SMALL_BYTES,256,0));assert(niw_call_shape(6,NIW_LARGE_BYTES,4,0));
 assert(niw_call_shape(1,0,1,0)&&niw_call_shape(5,0,1,0)&&niw_call_shape(2,0,0,8)&&niw_call_shape(3,0,0,0)&&niw_call_shape(4,0,0,0));
 assert(!niw_call_shape(2,0,0,7)&&!niw_call_shape(2,1,0,8)&&!niw_call_shape(1,0,0,0)&&!niw_call_shape(7,0,0,0));
 free(p);puts("PASS exact carrier bounds, truncations, atomic refusal, call shapes; unbound always unavailable");
}
