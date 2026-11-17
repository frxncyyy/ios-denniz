#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "wifi_wpa2.h"
#include "vectors.h"
static int rng(void*c,uint8_t*p,size_t n){(void)c;assert(n==32);memcpy(p,sn,n);return 0;}
int main(void){struct nwp w;struct nwp_result out;assert(!nwp_begin(&w,42,pmk_vector,own,peer,rsn,rsn,rng,0));assert(nwp_receive(&w,m1,sizeof m1,&out)==NWP_TX);assert(!memcmp(w.kck,ptk,16));assert(!memcmp(w.kek,ptk+16,16));assert(!memcmp(w.tk,ptk+32,16));nwp_cancel(&w);puts("PASS current actual engine/provider independent full48-byte PTK oracle, including TK32..47; synthetic only");return 0;}
