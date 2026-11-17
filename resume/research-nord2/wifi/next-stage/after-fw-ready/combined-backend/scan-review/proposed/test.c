#include "scan_plan.h"
#include <assert.h>
#include <string.h>
static void p32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
int main(void){uint8_t cap[52]={0},mac[6]={2,0,0,0,0,1};struct ncs_cap c,oldc;struct ncs_frames f,old;struct ncs_channel ch[64];
 cap[3]=0x38;cap[28]=0xec;cap[29]=4;
 assert(!ncs_cap_mac(cap,40,4,&c)&&c.bss==4&&c.wtbl==32&&c.wmm==4&&!c.tag9&&c.own_mac==1&&c.bmc==4);
 cap[36]=1;p32(cap+40,9);p32(cap+44,4);cap[48]=2;cap[49]=0;cap[50]=16;
 assert(!ncs_cap_mac(cap,52,4,&c)&&c.tag9&&c.bss==2&&c.wtbl==16&&c.wmm==1);
 cap[48]=1;assert(!ncs_cap_mac(cap,52,4,&c)&&c.own_mac==0);cap[48]=255;cap[50]=255;assert(!ncs_cap_mac(cap,52,4,&c)&&c.bss==4&&c.wtbl==32);
 cap[50]=5;oldc=c;assert(ncs_cap_mac(cap,52,4,&c)==-1&&!memcmp(&c,&oldc,sizeof c));cap[50]=32;cap[48]=4;
 for(unsigned i=0;i<64;i++)ch[i]=(struct ncs_channel){2,(uint8_t)(36+i)};
 assert(!ncs_build(cap,52,4,mac,ch,39,6,7,1,1,40,20,&f));
 assert(f.command[0].cid==0x11&&f.command[1].cid==3&&!f.command[0].need_response&&!f.command[1].need_response);
 assert(f.activate[64]==0&&f.activate[65]==1&&f.activate[66]==0&&f.activate[67]==1&&f.activate[74]==4&&!f.activate[75]);
 const uint8_t*s=f.scan+64;assert(s[0]==1&&!s[1]&&!s[2]&&!s[3]&&!s[4]&&!s[5]&&s[6]==2&&s[7]==1&&s[158]==4&&s[159]==32&&s[826]==7&&s[154]==40&&s[828]==20);
 assert(s[160]==2&&s[161]==36&&s[830]==2&&s[831]==68);for(unsigned i=0;i<6;i++){assert(s[1112+i]==255&&!s[1118+i]);}
 old=f;mac[0]=1;assert(ncs_build(cap,52,4,mac,ch,39,6,7,1,1,40,20,&f)==-1&&!memcmp(&f,&old,sizeof f));mac[0]=2;
 assert(ncs_build(cap,52,4,mac,ch,39,6,7,1,1,41,20,&f)==-1);ch[1]=ch[0];assert(ncs_build(cap,52,4,mac,ch,39,6,7,1,1,40,20,&f)==-1);
 uint8_t event[512]={0};struct ncs_event e;event[3]=0x38;event[28]=0x0d;event[29]=99;event[36]=1;event[41]=7;
 for(unsigned inner=0;inner<65536;inner++){event[26]=(uint8_t)inner;event[27]=(uint8_t)(inner>>8);assert(!ncs_done(event,512,1,&e)&&e.inner_type==inner&&e.event_sequence==99);}
 for(unsigned i=0;i<512;i++)assert(ncs_done(event,i,1,&e)==-1);assert(ncs_done(event,512,2,&e)==1);event[28]=0xec;assert(ncs_done(event,512,1,&e)==-1);event[28]=0x0d;event[3]=0;assert(ncs_done(event,512,1,&e)==-1);return 0;}
