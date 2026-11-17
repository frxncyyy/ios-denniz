#include "wifi_ios_scalar.h"
int niws_reply(const struct niw_request *q,const char name[16],
 enum niws_native_state state,void *output,size_t capacity,size_t *used){
 uint32_t value;uint8_t *p=output;unsigned terminated=0;
 if(!q||!name||!p||!used||capacity<4||q->operation!=NIW_GET||q->payload_bytes||q->value||
    (state!=NIWS_IDLE&&state!=NIWS_SCANNING))return -1;
 for(unsigned i=0;i<16;i++){
  if(q->interface_name[i]!=name[i])return -1;
  if(!name[i])terminated=1;
 }
 if(!terminated||!name[0])return -1;
 if(q->request==13)value=state==NIWS_SCANNING?1u:0u;
 else if(q->request==15)value=1;
 else return -1;
 p[0]=(uint8_t)value;p[1]=0;p[2]=0;p[3]=0;*used=4;return 0;
}
