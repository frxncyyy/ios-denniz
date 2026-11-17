/* Original bounded translation of the selected gen4m handler/predicate;
 * no copied Android source text, kernel types, event output or key material. */
#include "key_station.h"
int nwpk_event24(struct nwpk_station*s,uint64_t gen,const uint8_t*p,size_t n){
 if(!s||!p||n!=8)return -1;
 if(!s->generation||gen!=s->generation||!s->valid||p[0]!=s->bss)return 0;
 unsigned diff=0;for(unsigned i=0;i<6;i++)diff|=p[2+i]^s->peer[i];
 if(diff)return 0;
 /* p[1] is reserved/opaque, not key type, status, WLAN index or echo. */
 s->tx_key_ready=1;return 1;
}
int nwpk_tx_allowed(const struct nwpk_station*s){
 return s&&s->valid&&(!s->protected_bss||s->wep||s->tx_key_ready);
}
