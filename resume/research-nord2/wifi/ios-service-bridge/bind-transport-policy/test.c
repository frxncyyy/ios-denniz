#include "binding_contract.h"
#include "control_contract.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned checks;
static void w32(unsigned char*p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
int main(void){
 uint8_t out;size_t used;struct niwb_dispatch_view v={NIWB_OPEN,0,0};
 for(unsigned c=0;c<4;c++)for(unsigned p=0;p<64;p++)for(unsigned sel=1;sel<=5;sel+=4){
  v.control_handlers=c;v.pipe_handlers=p;out=0xaa;used=99;
  assert(!niwb_selector(&v,sel,0,1,0,0,0,&out,&used));
  assert(used==1&&out==(sel==1?(c==3):(c==3&&p==63)));checks++;
 }
 for(unsigned phase=NIWB_DRAINING;phase<=NIWB_CLOSED;phase++){
  v=(struct niwb_dispatch_view){phase,3,63};out=0xaa;used=99;
  assert(niwb_selector(&v,1,0,1,0,0,0,&out,&used)<0&&out==0xaa&&used==99);checks++;
 }
 v=(struct niwb_dispatch_view){NIWB_OPEN,3,63};
 for(unsigned bad=0;bad<8;bad++){
  out=0xaa;used=99;struct niwb_dispatch_view x=v;if(bad==6)x.control_handlers=4;if(bad==7)x.pipe_handlers=64;
  assert(niwb_selector(&x,bad==0?2:5,bad==1, bad==2?2:1,bad==3,bad==4,bad==5,&out,&used)<0&&out==0xaa&&used==99);checks++;
 }
 unsigned char request[NIW_SMALL_BYTES]={0},response[256],before[256];
 w32(request,NIW_GET);memcpy(request+15368,"en-test",8);w32(request+15384,43);
 const char identity[]="Synthetic driver identity";used=99;memset(response,0xaa,sizeof(response));
 assert(!niwb_driver_version(request,sizeof(request),identity,sizeof(identity),response,sizeof(response),&used));
 assert(used==sizeof(identity)&&!memcmp(response,identity,used)&&response[used]==0xaa);checks++;
 for(size_t cap=0;cap<sizeof(identity);cap++){
  memset(response,0xaa,sizeof(response));memcpy(before,response,sizeof(response));used=99;
  assert(niwb_driver_version(request,sizeof(request),identity,sizeof(identity),response,cap,&used)<0&&used==99&&!memcmp(before,response,sizeof(response)));checks++;
 }
 for(unsigned bad=0;bad<9;bad++){
  unsigned char q[NIW_SMALL_BYTES];memcpy(q,request,sizeof(q));char id[257];memset(id,'X',sizeof(id));id[255]=0;size_t len=256,n=sizeof(q);
  if(bad==0)w32(q,0);if(bad==1)w32(q+4,1);if(bad==2)w32(q+15384,12);if(bad==3)w32(q+15388,1);if(bad==4)n--;if(bad==5)memset(q+15368,'X',16);if(bad==6)len=257;if(bad==7)id[5]=0;if(bad==8)id[5]='\n';
  memset(response,0xaa,sizeof(response));memcpy(before,response,sizeof(response));used=99;
  assert(niwb_driver_version(q,n,id,len,response,sizeof(response),&used)<0&&used==99&&!memcmp(before,response,sizeof(response)));checks++;
 }
 char id[256];memset(id,'X',sizeof(id));id[255]=0;assert(!niwb_driver_version(request,sizeof(request),id,256,response,256,&used)&&used==256);checks++;
 printf("PASS %u exact bind/selector boundary checks, synthetic only; no native service readiness.\n",checks);return 0;
}
