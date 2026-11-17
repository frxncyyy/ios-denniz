#if defined(NORD2)
#include "audio_hw.h"
#include "sourced_profile.h"
static int supports(uint16_t revision,unsigned slave)
{
 return (slave==0x34 || slave==0x37) &&
  (revision==0x0a74 || revision==0x0b74 || revision==0x0c74);
}
static int apply(void *cookie,const struct na8_io *io,struct na8_state *bus,unsigned amp)
{
 struct nau_hw *h=cookie;
 if (!h || !io || !bus || amp>1 || h->amps[amp].addr!=(amp?0x37:0x34) ||
     !supports(h->amps[amp].rev,h->amps[amp].addr)) return NAH_E_BOARD_CONFIG;
 const struct nsp_op *ops=amp?nsp_amp1:nsp_amp0;
 for(unsigned i=0;i<52;i++) {
  uint16_t old=0,check=0;
  if(na8_read16(io,h,bus,h->amps[amp].addr,ops[i].reg,&old))return NAH_E_IO;
  uint16_t next=(old & ~ops[i].mask)|(ops[i].value & ops[i].mask);
  if(na8_write16(io,h,bus,h->amps[amp].addr,ops[i].reg,next) ||
     na8_read16(io,h,bus,h->amps[amp].addr,ops[i].reg,&check) ||
     ((check^next)&ops[i].mask))return NAH_E_IO;
 }
 return 0;
}
#define SHA {0x63,0x53,0x00,0xfb,0xc9,0x6f,0x25,0x8f,0xf9,0xa3,0x5f,0x82,0x43,0x5f,0x2e,0x9d,0x3f,0xf6,0xf1,0x37,0x0c,0x15,0x60,0x67,0xb4,0x01,0x46,0xe0,0x11,0x03,0x86,0x92}
#define B(a,b) {{a,b},SHA,apply,supports}
static const struct nah_board boards[3][3]={
 {B(0x0a74,0x0a74),B(0x0a74,0x0b74),B(0x0a74,0x0c74)},
 {B(0x0b74,0x0a74),B(0x0b74,0x0b74),B(0x0b74,0x0c74)},
 {B(0x0c74,0x0a74),B(0x0c74,0x0b74),B(0x0c74,0x0c74)}};
static const struct nah_board wildcard={{0,0},SHA,apply,supports};
const struct nah_board *nah_board_profile(void) { return &wildcard; }
const struct nah_board *nsp_select(uint16_t first,uint16_t second)
{
 if((first&255)!=0x74 || (second&255)!=0x74 || first<0x0a74 ||first>0x0c74 ||second<0x0a74 ||second>0x0c74)return 0;
 return &boards[(first>>8)-10][(second>>8)-10];
}
#endif
