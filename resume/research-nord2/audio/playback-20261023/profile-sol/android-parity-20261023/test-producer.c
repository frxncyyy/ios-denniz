#define NAH_TEST_FULL 1
#include "test-stream.c"
#include "test-i2c-model.h"
#include "audio_board.h"
extern const struct nah_board *nah_board_profile(void);
static struct i2c8_model bm;
static int bus_address(uint32_t pa)
{ return (pa>=0x11d04000 && pa<0x11d04400) || (pa>=0x11000780 && pa<0x110007a0) || pa==0x11d05e00 || pa==0x100053f0 || pa==0x10005230 || pa==0x11d10070; }
static int allread(void *c,uint32_t pa,unsigned w,uint32_t *v)
{
 (void)c;
 if (bus_address(pa)) { *v=m_read(&bm,pa,w); return 0; }
 assert(w==4); return model_read(&cm,pa,v);
}
static int allwrite(void *c,uint32_t pa,unsigned w,uint32_t v)
{
 (void)c;
 if (pa>=0x11d04100 && pa<=0x11d0414c) { m_write(&bm,pa,w,v);
  /* Selected source cold-init I2CR literal2 self-clears toPWDN1.
   * This fixture adapter is unnecessary once sharedmodel learnsI2CR. */
  if(pa==0x11d04124 && v==1)for(unsigned a=0;a<2;a++)if(bm.slave[a].reg[0]==2)bm.slave[a].reg[0]=1;
  return 0; }
 if (pa==0x11d05e04) { assert(v==16);bm.gate&=~v;return 0; }
 if (pa==0x11d05e08) { assert(v==16);bm.gate|=v;return 0; }
 if (pa==0x100000b8) { assert(v==32768);*regp(&cm,0x100000b0)&=~v;return 0; }
 if (pa==0x100000b4) { assert(v==32768);*regp(&cm,0x100000b0)|=v;return 0; }
 if (pa==0x10005034 || pa==0x10005044 || pa==0x10005134 || pa==0x10005144) {
  assert(v==(pa==0x10005034 || pa==0x10005134 ? 1u<<15:1u<<20));
  *regp(&cm,pa-4)|=v; return 0;
 }
 if (pa==0x10005038 || pa==0x10005048 || pa==0x10005138 || pa==0x10005148) {
  assert(v==(pa==0x10005038 || pa==0x10005138 ? 1u<<15:1u<<20));
  *regp(&cm,pa-8)&=~v; return 0;
 }
 if ((hw.bus.uncertain || hw.bus.offline) && pa==NAF_AFE_DAC_CON0 && !(v&16)) {
  assert((*regp(&cm,0x10005130)&(1u<<15)) && (*regp(&cm,0x10005140)&(1u<<20)));
  assert((*regp(&cm,0x10005030)&(1u<<15)) && (*regp(&cm,0x10005040)&(1u<<20)));
 }
 assert(w==4); return model_write(&cm,pa,v);
}
static int alldelay(void *c,unsigned us) { (void)c;assert(us<=1000);return 0; }
static uintptr_t allworker(void *c) { (void)c;return cm.owner; }
static int alllease(void *c,unsigned acq) { (void)c;(void)acq;return 0; }
static const struct nah_io allio={allread,allwrite,alldelay,allworker,alllease};
static void full_prepare(uint16_t rev0,uint16_t rev1,unsigned sticky)
{
 setup(); /* Initializes independent cold AFE register model. */
 /* Legitimate inherited board pinmux: already I2S3, GPIO directionbits
  * may contain outputs while peripheral mux makes those GPIO bits inert. */
 *regp(&cm,NAF_GPIO_MODE_32)=0x1011;
 *regp(&cm,0x10005010)=0xb;
 memset(&hw,0,sizeof(hw));
 m_init(&bm);
 bm.slave[1].reg[0]=1;bm.slave[1].reg[0x14]=0;
 bm.slave[0].reg[0x11]=0x80;bm.slave[1].reg[0x11]=0x80;
 bm.slave[0].reg[3]=rev0;bm.slave[1].reg[3]=rev1;
 bm.slave[0].reg[0x10]=(uint16_t)sticky;bm.slave[1].reg[0x10]=(uint16_t)sticky; /* historical lost-clock sticky from cold startup */
 *regp(&cm,0x10001094)|=1u<<18;
 /* Truly cold clocks and asserted/reset input GPIOs, source setup needed. */
 bm.gate=16;*regp(&cm,0x100000b0)=32768;
 *regp(&cm,0x10005130)=1u<<15;*regp(&cm,0x10005140)=1u<<20;
 }
#include "audio_stream.h"
static struct nsa_stream producer;
static int16_t chunk[512];
static unsigned expected_old;
static void burst(uint64_t now) {
 for(unsigned n=0;n<16384;n+=256)nsa_enqueue(&producer,chunk,256,now);
}
int main(int argc,char **argv){
 assert(argc==3);expected_old=(unsigned)atoi(argv[2]);
 uint8_t blob[4163];FILE*f=fopen(argv[1],"rb");assert(f&&fread(blob,1,sizeof blob,f)==sizeof blob);fclose(f);
 const uint8_t digest[32]={0x63,0x53,0x00,0xfb,0xc9,0x6f,0x25,0x8f,0xf9,0xa3,0x5f,0x82,0x43,0x5f,0x2e,0x9d,0x3f,0xf6,0xf1,0x37,0x0c,0x15,0x60,0x67,0xb4,0x01,0x46,0xe0,0x11,0x03,0x86,0x92};
 assert(nah_board_load(blob,sizeof blob,digest)==0);
 for(unsigned i=0;i<512;i++)chunk[i]=(i&1)?-12000:12000;
 nsa_initialize(&producer,1);burst(1);
 if(expected_old)assert(producer.accepted==8192&&producer.dropped==8192);
 else assert(producer.accepted==16384&&!producer.dropped);
 full_prepare(0x0b74,0x0a74,0);assert(!nau_hw_init(&hw,&allio,0,nah_board_profile()));
 int start_rc=nau_hw_start(&hw,48000,2,16,0,256,0,200000);if(start_rc)fprintf(stderr,"STARTrc=%d afe=%u/%u bus=%u/%u gate=%u\n",start_rc,hw.afe.phase,hw.afe.failure,hw.bus.stage,hw.bus.failure,hw.status.gate);assert(!start_rc);
 cm.current=NAF_SRAM;assert(!nau_hw_tick(&hw,200000));
 unsigned next=1,consumed=0;
 for(uint64_t now=202000;now<=1700000;now+=2000){
  while(next<4&&now>=(uint64_t)next*16384*1000000/48000){burst(now);next++;}
  unsigned room=NAH_QUEUE_FRAMES-hw.status.queued_frames;if(room>256)room=256;
  int16_t pcm[512];unsigned n=nsa_dequeue(&producer,pcm,room);if(n)assert(nau_hw_feed(&hw,pcm,n)==(int)n);
  consumed+=96;cm.current=NAF_SRAM+4*(consumed%NAH_RING_FRAMES);int rc=nau_hw_tick(&hw,now);if(rc){fprintf(stderr,"tickfailed now=%llu rc=%d queued=%u fed=%u pcm=%u\n",(unsigned long long)now,rc,hw.status.queued_frames,hw.status.fed_frames,hw.status.pcm_frames);assert(!rc);}
 }
 assert(next==4&&producer.written==65536&&producer.count==0&&hw.status.queued_frames==0);
 assert(hw.status.pcm_frames==producer.accepted);
 if(expected_old)assert(producer.dropped>0);else assert(producer.accepted==65536&&!producer.dropped);
 assert(!nau_hw_stop(&hw));m_check_restored(&bm);
 /* Nonblocking backpressure remains explicit andbounded beyond capacity. */
 nsa_flush(&producer);unsigned cap=NSA_QUEUE_FRAMES;for(unsigned n=0;n<cap/256;n++)assert(nsa_enqueue(&producer,chunk,256,3000000)==256);
 assert(nsa_enqueue(&producer,chunk,256,3000001)==0&&producer.count==cap);
 printf("PASS queue=%u written65536 accepted=%llu dropped=%llu realhardwarePCM=%u\n",NSA_QUEUE_FRAMES,(unsigned long long)producer.accepted-cap,(unsigned long long)producer.dropped-256,hw.status.pcm_frames);
}
