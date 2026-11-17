#include <assert.h>
#include <string.h>
#include "wifi_basic_config.c"
static struct nbc_state s;static struct nwi_state i;static struct ncd_state cap;static struct nfp_native fw;static unsigned calls,writes,consumed;static uint8_t frame[48];static unsigned frame_bytes;
static int own(void*c){(void)c;calls++;return 0;}
static int now(void*c,uint64_t*t){(void)c;calls++;*t=1;return 0;}
static int readreg(void*c,uint32_t a,uint32_t*v){(void)c;calls++;*v=a==0x1802550c?4:0;return 0;}
static int writereg(void*c,uint32_t a,uint32_t v){(void)c;calls++;assert(a==0x18025508&&v==3);writes++;consumed++;return 0;}
static int readmem(void*c,uint64_t a,void*p,uint32_t n){(void)c;calls++;if(n==16){assert(a==i.transport.rx_ring+48);memset(p,0,16);uint8_t*d=p;uint32_t b=(uint32_t)(i.transport.rx_buffers+12288);for(unsigned k=0;k<4;k++)d[k]=(uint8_t)(b>>(8*k));d[6]=(uint8_t)frame_bytes;d[7]=0xc0;}else{assert(n==frame_bytes);memcpy(p,frame,n);}return 0;}
static int writemem(void*c,uint64_t a,const void*p,uint32_t n){(void)c;(void)a;(void)p;calls++;assert(n==16);writes++;return 0;}
static int syncm(void*c,uint64_t a,uint32_t n,unsigned way){(void)c;(void)a;(void)n;(void)way;calls++;return 0;}
static int delaym(void*c,unsigned us){(void)c;(void)us;calls++;return 0;}
static const struct nwt_io bus={own,readreg,writereg,readmem,writemem,syncm,delaym};static const struct ncd_io io={&bus,now};
static void setup(unsigned type){memset(&s,0,sizeof s);memset(&i,0,sizeof i);memset(&cap,0,sizeof cap);memset(&fw,0,sizeof fw);memset(frame,0,48);calls=writes=consumed=0;frame_bytes=48;
 i.initialized=1;i.transport.phase=2;i.transport.finish_phase=2;i.transport.completed=4;i.transport.tx_index=i.transport.rx_index=3;i.transport.next_sequence=5;i.transport.rx_ring=0x90004000;i.transport.rx_buffers=0x90006000;
 cap.init=&i;cap.firmware=&fw;cap.entered=1;cap.phase=5;cap.completed=cap.matched=cap.submit_attempted=cap.tx_done=cap.events=cap.rearmed=1;cap.history[0].kind=1;cap.history[0].eid=0xec;cap.capability.decoded=1;
 fw.ready=fw.finish_done=fw.ready_polls=1;fw.ready_raw=3;fw.state.completed=127;
 s.cap=&cap;s.init=&i;s.firmware=&fw;s.attempted=s.prepared=s.entered=1;s.phase=NBC_DRAINING;s.deadline=100;s.tx_index=s.rx_index=3;s.sequence=5;s.command[36]=2;s.command[37]=0xa0;s.command[38]=1;s.command[39]=5;
 frame[3]=0x38;frame[26]=(uint8_t)type;frame[27]=(uint8_t)(type>>8);frame[28]=0x27;frame[38]=3;
}
int main(void){const unsigned types[]={0x3800,0xe000,0xffff,7};unsigned reference=0;
 for(unsigned k=0;k<65536;k++){setup(k);assert(!drain(&io,0,&s)&&s.debug_events==1&&s.rearmed==1&&consumed==1&&i.transport.rx_index==4&&!s.halt&&s.last_debug.inner_type==k);if(!k)reference=calls;else assert(calls==reference);}
 for(unsigned k=0;k<4;k++)for(unsigned bad=0;bad<6;bad++){setup(types[k]);if(bad==0)frame[3]=0;if(bad==1)frame[28]=0x28;if(bad==2)frame[38]=2;if(bad==3)frame[44]=1;if(bad==4)frame_bytes=47;if(bad==5){frame[44]=0xb1;frame[45]=4;}assert(drain(&io,0,&s)<0&&s.halt&&!s.rearmed&&!writes&&!consumed);unsigned old=calls;assert(drain(&io,0,&s)<0&&calls==old);}
 return 0;}
