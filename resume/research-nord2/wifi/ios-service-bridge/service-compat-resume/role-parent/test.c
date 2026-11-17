#include "wifi_ios_scalar.h"
#include <assert.h>
#include <string.h>
int main(void){
 struct niw_request q={.operation=NIW_GET};char name[16]="en0";unsigned checks=0;
 memcpy(q.interface_name,name,16);
 for(unsigned id=0;id<512;id++)for(int state=-1;state<=3;state++)for(unsigned cap=0;cap<=5;cap++){
  uint8_t out[8];memset(out,0xa5,sizeof out);size_t used=99;q.request=id;
  int good=(id==13||id==15)&&(state==1||state==2)&&cap>=4;
  assert((niws_reply(&q,name,(enum niws_native_state)state,out,cap,&used)==0)==good);
  if(good){assert(used==4&&out[0]==(id==15||state==2)&&!out[1]&&!out[2]&&!out[3]&&out[4]==0xa5);}
  else{assert(used==99);for(unsigned j=0;j<8;j++)assert(out[j]==0xa5);}
  checks++;
 }
 q.request=13;uint8_t out[8];size_t used;
 for(unsigned mode=0;mode<5;mode++){
  struct niw_request bad=q;char wrong[16];memcpy(wrong,name,16);memset(out,0xa5,8);used=99;
  if(mode==0)bad.value=1;if(mode==1)bad.payload_bytes=4;if(mode==2)bad.operation=0;
  if(mode==3)wrong[2]='1';if(mode==4)memset(wrong,'X',16);
  assert(niws_reply(&bad,wrong,NIWS_IDLE,out,8,&used));assert(used==99&&out[0]==0xa5);checks++;
 }
 assert(niws_reply(0,name,NIWS_IDLE,out,8,&used));
 assert(niws_reply(&q,0,NIWS_IDLE,out,8,&used));
 assert(niws_reply(&q,name,NIWS_IDLE,0,8,&used));
 assert(niws_reply(&q,name,NIWS_IDLE,out,8,0));
 return checks==15365?0:1;
}
