#include <arm64/sweet_early.h>
#if defined(NORD2)
#include "wifi_ios_card.h"
#include "wifi_ios_control.h"
unsigned niwc_wpa3;
int niwc_card_reply(const void*input,size_t bytes,const struct niwc_manifest*m,
 void*out,size_t capacity,size_t*used){
 static const uint8_t bits[NIWC_FEATURES]={0,1,3,13,14,76};
 struct niw_request q;uint8_t raw[NIWC_CARD_BYTES]={0};unsigned seen=0;
 if(!m||!out||!used||capacity<sizeof(raw)||m->count>NIWC_FEATURES||
    niw_decode(0,input,bytes,&q)||q.request!=12||q.value||q.payload_bytes!=sizeof(raw))return -1;
 for(unsigned n=0;n<m->count;n++){
  unsigned f=m->entry[n].feature;
  if(f>=NIWC_FEATURES||m->entry[n].installed_operations!=NIWC_END_TO_END||
     (seen&(1u<<f)))return -1;
  seen|=1u<<f;raw[bits[f]/8]|=(uint8_t)(1u<<(bits[f]%8));
 }
 /* 0434: WPA3-Personal. IO80211/wifid (iOS 16.7) gate SAE on capability bit
  * 75 and 802.11w (also PSK-SHA256) on bit 52; SAE needs both. Reported with
  * the reviewed RSN path only; struct niwc_manifest keeps its CI layout. */
 if(niwc_wpa3&&(seen&(1u<<NIWC_RSN_IE))&&(seen&(1u<<NIWC_CCMP))){raw[75/8]|=(uint8_t)(1u<<(75%8));raw[52/8]|=(uint8_t)(1u<<(52%8));}
 for(unsigned n=0;n<sizeof(raw);n++)((uint8_t*)out)[n]=raw[n];
 *used=sizeof(raw);return 0;
}

#endif /* NORD2 */
