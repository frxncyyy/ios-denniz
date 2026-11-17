#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "key_station.h"
int main(void){unsigned checks=0;uint8_t event[8]={0,0,2,0,0,0,0,1};struct nwpk_station s={.generation=9,.bss=0,.peer={2,0,0,0,0,1},.valid=1,.protected_bss=1};
 assert(!nwpk_tx_allowed(&s));checks++;
 for(unsigned n=0;n<8;n++){struct nwpk_station before=s;assert(nwpk_event24(&s,9,event,n)<0&&!memcmp(&s,&before,sizeof s));checks++;}
 assert(!nwpk_event24(&s,8,event,8)&&!s.tx_key_ready);checks++;
 for(unsigned i=0;i<6;i++){event[i+2]^=1;assert(!nwpk_event24(&s,9,event,8)&&!s.tx_key_ready);event[i+2]^=1;checks++;}
 event[0]=1;assert(!nwpk_event24(&s,9,event,8)&&!s.tx_key_ready);event[0]=0;checks++;
 for(unsigned reserved=0;reserved<256;reserved++){event[1]=reserved;s.tx_key_ready=0;assert(nwpk_event24(&s,9,event,8)==1&&nwpk_tx_allowed(&s));assert(nwpk_event24(&s,9,event,8)==1&&s.tx_key_ready==1);checks+=2;}
 s.valid=0;assert(!nwpk_tx_allowed(&s)&&!nwpk_event24(&s,9,event,8));checks++;
 for(unsigned valid=0;valid<=1;valid++)for(unsigned protected=0;protected<=1;protected++)for(unsigned wep=0;wep<=1;wep++)for(unsigned ready=0;ready<=1;ready++){s.valid=valid;s.protected_bss=protected;s.wep=wep;s.tx_key_ready=ready;assert(nwpk_tx_allowed(&s)==(int)(valid&&(!protected||wep||ready)));checks++;}
 printf("PASS %u source station-selector/predicate checks; no GTK ACK inferred\n",checks);return 0;}
