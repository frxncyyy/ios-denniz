#include "regulatory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void p32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t u32(const uint8_t*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(int argc,char**argv){
 assert(argc==2);uint8_t board[6146];FILE*f=fopen(argv[1],"rb");assert(f&&fread(board,1,sizeof(board),f)==sizeof(board));assert(fgetc(f)==EOF);fclose(f);
 struct nrp_board b;assert(!nrp_board_decode(board,sizeof(board),&b));assert(!memcmp(b.country,board+10,2)&&b.support5==board[197]&&b.enable5==board[240]);
 for(unsigned n=0;n<256;n++){struct nrp_board x,before;memset(&x,0xa5,sizeof(x));before=x;assert(nrp_board_decode(board,n,&x)<0&&!memcmp(&x,&before,sizeof(x)));}
 uint8_t frame[80]={0};frame[3]=0x38;frame[28]=0xec;frame[29]=5;frame[36]=2;p32(frame+40,3);p32(frame+44,12);p32(frame+48,0x5958);frame[52]=1;p32(frame+60,8);p32(frame+64,12);frame[70]=1;frame[72]=2;frame[78]=0xf;
 struct nrp_caps c;assert(!nrp_caps_decode(frame,sizeof(frame),&c)&&c.sku_present&&c.phy_present&&c.band5&&c.nss==2);uint32_t country;assert(!nrp_country(&c,&country)&&country==0x5958);c.efuse_valid=0;assert(!nrp_country(&c,&country)&&country==0x3030);
 for(unsigned n=0;n<80;n++){struct nrp_caps x,before;memset(&x,0xa5,sizeof(x));before=x;assert(nrp_caps_decode(frame,n,&x)<0&&!memcmp(&x,&before,sizeof(x)));}
 for(unsigned type=0;type<65536;type++){frame[26]=(uint8_t)type;frame[27]=(uint8_t)(type>>8);assert(!nrp_caps_decode(frame,80,&c));}
 frame[28]=1;assert(nrp_caps_decode(frame,80,&c)<0);frame[28]=0xec;frame[2]=1;assert(nrp_caps_decode(frame,80,&c)<0);frame[2]=0;
 p32(frame+60,3);assert(nrp_caps_decode(frame,80,&c)<0);p32(frame+60,8);p32(frame+64,0xffffffffu);assert(nrp_caps_decode(frame,80,&c)<0);p32(frame+64,12);assert(!nrp_caps_decode(frame,80,&c));
 b.support5=b.enable5=1;
 struct nrp_channel ch[4]={{1,0,2},{6,0,0},{36,1,8},{149,1,512}};
 uint8_t output[400],before[400];size_t used=0;
 for(unsigned seq=0;seq<256;seq++){assert(!nrp_domain_frame(&c,&b,ch,4,(uint8_t)seq,output,sizeof(output),&used)&&used==108);assert(output[36]==15&&output[37]==0xa0&&output[38]==1&&output[39]==seq&&u32(output+64)==0x5958&&output[72]==2&&output[73]==2&&!output[74]);assert(output[68]==1&&output[69]==1&&output[70]==1&&u32(output+80)==2);}
 for(unsigned n=0;n<108;n++){memset(output,0xa5,sizeof(output));memcpy(before,output,sizeof(output));used=123;assert(nrp_domain_frame(&c,&b,ch,4,0,output,n,&used)<0&&used==123&&!memcmp(output,before,sizeof(output)));}
 struct nrp_passive plan;assert(!nrp_passive_plan(&c,&b,ch,4,&plan)&&plan.count==2&&plan.channel[0].flags==2);
 ch[0].flags=1;assert(nrp_domain_frame(&c,&b,ch,4,0,output,sizeof(output),&used)<0);assert(!nrp_passive_plan(&c,&b,ch,4,&plan)&&plan.count==1);ch[1].flags=2048;assert(nrp_passive_plan(&c,&b,ch,4,&plan)<0);
 ch[0].flags=2;ch[1].flags=0;ch[1].channel=1;assert(nrp_passive_plan(&c,&b,ch,4,&plan)<0);ch[1].channel=6;
 ch[2].channel=35;assert(nrp_passive_plan(&c,&b,ch,4,&plan)<0);ch[2].channel=36;
 ch[0].flags=4;assert(nrp_passive_plan(&c,&b,ch,4,&plan)<0);ch[0].flags=2;
 c.sku_present=0;assert(nrp_domain_frame(&c,&b,ch,4,0,output,sizeof(output),&used)<0);c.sku_present=1;c.phy_present=0;assert(nrp_passive_plan(&c,&b,ch,4,&plan)<0);c.phy_present=1;c.band5=0;assert(nrp_passive_plan(&c,&b,ch,4,&plan)<0);c.band5=1;b.enable5=0;assert(nrp_passive_plan(&c,&b,ch,4,&plan)<0);
 puts("PASS pure regulatory inputs/tag3+PHY decoder, exact V2 CID0f framing, atomic refusals, source channel membership and passive-only filtering. No actual country/MAC/calibration output or regulatory/runtime authority.");
}
