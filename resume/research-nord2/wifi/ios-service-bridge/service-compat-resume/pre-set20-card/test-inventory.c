#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_ios_card.h"
#include "wifi_ios_control.h"
typedef struct {unsigned held;}IOLock;
static IOLock gate,notice;
static struct {IOLock*lock;unsigned failed;}nss_native={&gate,0};
static struct {IOLock*notice_lock;}njs_native;
static unsigned available,locks,unlocks,checks;
static int nss_available(void){return available;}
static void IOLockLock(IOLock*p){assert(p==&gate&&!p->held);p->held=1;locks++;}
static void IOLockUnlock(IOLock*p){assert(p==&gate&&p->held);p->held=0;unlocks++;}
#include "inventory-function.inc"
static void w32(uint8_t*p,uint32_t v){for(unsigned n=0;n<4;n++)p[n]=(uint8_t)(v>>(8*n));}
int main(void){
 struct niwc_manifest out,before;memset(&out,0xa5,sizeof out);before=out;
 assert(nord2_wifi_join_card_inventory(NULL)==-1&&!locks);checks++;
 assert(nord2_wifi_join_card_inventory(&out)==1&&!memcmp(&out,&before,sizeof out)&&!locks);checks++;
 available=1;assert(nord2_wifi_join_card_inventory(&out)==1&&!memcmp(&out,&before,sizeof out)&&locks==unlocks&&!gate.held);checks++;
 njs_native.notice_lock=&notice;nss_native.failed=1;assert(nord2_wifi_join_card_inventory(&out)==1&&!memcmp(&out,&before,sizeof out)&&locks==unlocks&&!gate.held);checks++;
 nss_native.failed=0;assert(!nord2_wifi_join_card_inventory(&out)&&locks==unlocks&&!gate.held&&out.count==2);checks++;
 assert(out.entry[0].feature==NIWC_CCMP&&out.entry[1].feature==NIWC_RSN_IE&&out.entry[0].installed_operations==NIWC_END_TO_END&&out.entry[1].installed_operations==NIWC_END_TO_END);checks++;
 uint8_t q[15408]={0};w32(q,0xc02869c9);w32(q+4,13);memcpy(q+15368,"en-test",8);w32(q+15384,12);
 uint8_t raw[20];size_t used=99;memset(raw,0xa5,sizeof raw);assert(!niwc_card_reply(q,sizeof q,&out,raw,sizeof raw,&used)&&used==13);checks++;
 for(unsigned bit=0;bit<104;bit++){assert(((raw[bit/8]>>(bit%8))&1)==(bit==3||bit==14));checks++;}
 for(unsigned n=13;n<20;n++){assert(raw[n]==0xa5);checks++;}
 for(unsigned cap=0;cap<13;cap++){memset(raw,0xa5,sizeof raw);used=99;assert(niwc_card_reply(q,sizeof q,&out,raw,cap,&used)<0&&used==99);for(unsigned n=0;n<20;n++)assert(raw[n]==0xa5);checks++;}
 for(unsigned field=0;field<4;field++){uint8_t b[15408];memcpy(b,q,sizeof b);if(field==0)w32(b,0x802869c8);if(field==1)w32(b+4,17);if(field==2)w32(b+15384,43);if(field==3)w32(b+15388,1);memset(raw,0xa5,sizeof raw);used=99;assert(niwc_card_reply(b,sizeof b,&out,raw,sizeof raw,&used)<0&&used==99);for(unsigned n=0;n<20;n++)assert(raw[n]==0xa5);checks++;}
 /* Pairwise/group CCMP and RSN are the only implementation entries; no live
  * connection boolean is consulted or supplied as a feature readiness token. */
 puts("PASS installed native-owned CCMP/RSN inventory, unavailable/failed atomic refusal, exact raw13 onlybits3/14, old malformed/capacity guards; no hardware readiness");printf("checks=%u\n",checks);return 0;
}
