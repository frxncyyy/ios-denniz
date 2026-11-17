#include "scan_bss.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint8_t frame[3840],done[NSB_SCAN_DONE_BYTES];
static struct nsb_scan scan;
static void w16(uint8_t *p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static void w32(uint8_t *p,unsigned x){w16(p,x);w16(p+2,x>>16);}
static size_t make(unsigned groups,unsigned offset,unsigned subtype)
{
 memset(frame,0,sizeof(frame));unsigned off=24;
 if(groups&8)off+=16;
 if(groups&1)off+=16;
 if(groups&2)off+=8;
 if(groups&4)off+=8;
 if(groups&16)off+=72;
 off+=offset*2;
 w16(frame+2,0x3801);w32(frame+4,groups<<11);w32(frame+8,(12<<8)|(offset<<14));w32(frame+12,6<<8);
 uint8_t *m=frame+off;m[0]=(uint8_t)(subtype<<4);m[10]=2;m[15]=3;m[16]=2;m[21]=4;
 w16(m+32,100);w16(m+34,0x11);
 size_t n=off+36;frame[n++]=0;frame[n++]=4;frame[n++]='A';frame[n++]=0;frame[n++]='B';frame[n++]=0xff;
 frame[n++]=3;frame[n++]=1;frame[n++]=6;frame[n++]=48;frame[n++]=2;frame[n++]=1;frame[n++]=0;
 frame[n++]=221;frame[n++]=4;frame[n++]=0;frame[n++]=0x50;frame[n++]=0xf2;frame[n++]=1;
 w16(frame,(unsigned)n);return n;
}
static void bad(size_t n)
{
 struct nsb_record r,copy;memset(&r,0xa5,sizeof(r));copy=r;
 assert(nsb_decode(frame,n,10,&r)==-1);assert(!memcmp(&r,&copy,sizeof(r)));
}
int main(void)
{
 struct nsb_record r;unsigned layouts=0,truncations=0;
 for(unsigned g=0;g<32;g++)for(unsigned o=0;o<4;o++)for(unsigned subtype=5;subtype<=8;subtype+=3){
  size_t n=make(g,o,subtype);assert(!nsb_decode(frame,n,55,&r));
  assert(r.ssid_len==4&&r.ssid[1]==0&&r.ssid[3]==255&&!r.hidden);
  assert(r.channel==6&&!r.channel_mismatch&&r.privacy&&r.rsn_present&&r.wpa_present);
  assert(r.group_valid==g&&r.rxv_valid==!!(g&4)&&r.observed_us==55);
  for(size_t k=0;k<n;k++){bad(k);truncations++;}layouts++;
 }
 size_t n=make(0,0,8);
 memset(frame+62,0,4);assert(!nsb_decode(frame,n,10,&r)&&r.hidden&&r.ssid_present&&r.ssid_len==4);
 make(0,0,8);w16(frame+24,0x8080);bad(n);

 for(unsigned bit=16;bit<=27;bit++){make(0,0,8);w32(frame+4,1u<<bit);bad(n);}
 const unsigned bits[]={13,23,24,25,27};for(unsigned i=0;i<5;i++){make(0,0,8);w32(frame+8,(12<<8)|(1u<<bits[i]));bad(n);}
 make(0,0,8);w32(frame+16,1<<11);bad(n);
 make(0,0,8);w16(frame+2,0x3800);bad(n);
 make(0,0,8);w16(frame+24,0x4080);bad(n);
 make(0,0,8);frame[24+22]=1;bad(n);
 make(0,0,8);frame[24+16]=1;bad(n);
 make(0,0,8);frame[60+1]=33;bad(n);
 make(0,0,8);frame[n++]=0;frame[n++]=0;w16(frame,(unsigned)n);bad(n);
 /* IE snapshot may truncate, validation never stops at snapshot boundary. */
 n=make(0,0,8);for(unsigned j=0;j<3;j++){frame[n++]=127;frame[n++]=250;memset(frame+n,0x77,250);n+=250;}
 w16(frame,(unsigned)n);assert(!nsb_decode(frame,n,10,&r));assert(r.ies_truncated&&r.ie_bytes==512&&r.ie_total>512);
 frame[n++]=99;w16(frame,(unsigned)n);bad(n);
 n=make(0,0,8);memset(&scan,0,sizeof(scan));assert(!nsb_begin(&scan,9,10,100));assert(nsb_begin(&scan,9,10,100)==-1);
 assert(!nsb_add(&scan,frame,n,11)&&scan.count==1);memset(frame+24+10,0x12,6);
 assert(!nsb_add(&scan,frame,n,12)&&scan.count==1&&scan.frames==2);
 /* Fill and report bounded loss, without substituting invented results. */
 for(unsigned i=1;i<16;i++){frame[24+21]=(uint8_t)(i+20);assert(!nsb_add(&scan,frame,n,12+i));}
 frame[24+21]=99;assert(nsb_add(&scan,frame,n,29)==1&&scan.dropped==1&&scan.partial);
 memset(done,0,sizeof(done));done[0]=8;done[5]=7;done[6]=4;done[16]=2;done[20]=1;done[21]=6;w16(done+84,0x1234);w32(done+340,7);
 assert(nsb_complete(&scan,done,sizeof(done),30)==1&&scan.active&&scan.count==16);
 done[0]=9;assert(!nsb_complete(&scan,done,sizeof(done),31)&&scan.done&&!scan.active&&scan.count==16);
 assert(scan.completion.idle[0]==0x1234&&scan.completion.beacons_2g==7);
 assert(nsb_add(&scan,frame,n,32)==-1&&nsb_complete(&scan,done,sizeof(done),32)==-1);
 struct nsb_done d,b;memset(&d,0xa5,sizeof(d));b=d;
 for(size_t i=0;i<sizeof(done);i++){assert(nsb_decode_done(done,i,&d)==-1);assert(!memcmp(&d,&b,sizeof(d)));}
 done[16]=65;assert(nsb_decode_done(done,sizeof(done),&d)==-1);done[16]=0;
 /* Completion is statistics only: an empty scan remains an empty scan. */
 memset(&scan,0,sizeof(scan));assert(!nsb_begin(&scan,9,10,100));assert(!nsb_complete(&scan,done,sizeof(done),11));assert(scan.count==0&&scan.done&&!scan.partial);
 memset(&scan,0,sizeof(scan));assert(!nsb_begin(&scan,9,10,100));done[5]=6;assert(!nsb_complete(&scan,done,sizeof(done),11));assert(scan.partial);
 memset(&scan,0,sizeof(scan));assert(!nsb_begin(&scan,9,10,100));assert(nsb_expire(&scan,110)==1&&scan.timed_out&&scan.partial);
 memset(&scan,0,sizeof(scan));assert(!nsb_begin(&scan,9,10,100));assert(nsb_expire(&scan,9)==-1&&!scan.active&&scan.partial);
 memset(&scan,0,sizeof(scan));assert(nsb_begin(&scan,1,UINT64_MAX,1)==-1);
 /* Normal completion wrapper classifies EID0d; never manufactures BSSes. */
 uint8_t event[36+NSB_SCAN_DONE_BYTES]={0};w16(event+2,0x3800);w16(event+26,0xe000);event[28]=0x0d;event[29]=55;
 done[0]=9;done[5]=7;memcpy(event+36,done,sizeof(done));
 memset(&scan,0,sizeof(scan));assert(!nsb_begin(&scan,9,10,100));
 event[28]=0xec;assert(nsb_complete_event(&scan,event,sizeof(event),11)==-1&&scan.active);
 event[28]=0x0d;assert(!nsb_complete_event(&scan,event,sizeof(event),12)&&scan.done&&!scan.count);
 assert(scan.completion.event_sequence==55&&scan.completion.rxd_bytes==0);
 printf("PASS pure scan BSS: %u selected RXD layouts, %u truncations, output atomicity, binary SSID/security IE retention, all source error bits, capacity/deadline/sequence/completion separation. Synthetic frames only.\n",layouts,truncations);
}
