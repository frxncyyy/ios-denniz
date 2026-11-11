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
 if (pa>=0x11d04100 && pa<=0x11d0414c) { m_write(&bm,pa,w,v); return 0; }
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
/*
 * 0330 (opus-i2c, 2026-11-11): the camera powers the CAM MTCMOS (SPM PWR_STATUS/_2ND bit 23) at its first session
 * (nord2_seninf.inc n2s_init, "NORD2 SENINF: CAM domain on") and never powers it off. The CI audio objects refused
 * every amplifier transfer while CAM was on (audio_i2c8.c stage 5 failure 16 "CAM power (CCU may run)") and failed
 * the I2C8 clock restore at stop (audio_hw.c bus_clock_restore -> NAH_E_IO -> status.uncertain, sticky). Phone
 * signature (user2..user6): NORD2 AUDIO ERROR: where=start reason=-3 ... i2c=5/16 ... uncertain=1 i2cpa=10006170.
 * This model: amps initialised with CAM off (boot), then CAM on, then three audible lifecycles + a silent one.
 */
static void cam_on(unsigned on)
{
 uint32_t *a=regp(&cm,NAF_SPM_PWR_STATUS),*b=regp(&cm,NAF_SPM_PWR_STATUS2);
 if (on) { *a|=1u<<23; *b|=1u<<23; } else { *a&=~(1u<<23); *b&=~(1u<<23); }
}
static int lifecycle(uint64_t t0)
{
 int16_t data[480];for(unsigned i=0;i<480;i++)data[i]=(int16_t)((i&1)?-1000:1000);
 int rc=nau_hw_start(&hw,48000,2,16,0,256,0,t0);
 if (rc) {
  printf("FAIL start=%d i2c=%u/%u i2cpa=%x uncertain=%u offline=%u\n",rc,hw.bus.stage,hw.bus.failure,
   hw.bus.last_addr,hw.status.uncertain,hw.bus.offline);
  return rc;
 }
 cm.current=NAF_SRAM; if ((rc=nau_hw_tick(&hw,t0))) return rc;
 for(unsigned i=1;i<=12;i++) {                       /* 12 ticks: three amp status monitors (every 4th) */
  assert(nau_hw_feed(&hw,data,240)==240);
  cm.current=NAF_SRAM+4*(240*i%1024);
  if ((rc=nau_hw_tick(&hw,t0+5000*i))) { printf("FAIL tick=%d i2c=%u/%u\n",rc,hw.bus.stage,hw.bus.failure); return rc; }
 }
 if ((rc=nau_hw_stop(&hw))) { printf("FAIL stop=%d i2c=%u/%u uncertain=%u\n",rc,hw.bus.stage,hw.bus.failure,hw.status.uncertain); return rc; }
 if (hw.status.uncertain) { printf("FAIL uncertain after stop\n"); return -100; }
 return 0;
}
int main(int argc,char **argv)
{
 setvbuf(stdout,0,_IONBF,0);
 assert(argc==2);
 static uint8_t blob[4163];
 static const uint8_t digest[32]={0x63,0x53,0x00,0xfb,0xc9,0x6f,0x25,0x8f,0xf9,0xa3,0x5f,0x82,0x43,0x5f,0x2e,0x9d,0x3f,0xf6,0xf1,0x37,0x0c,0x15,0x60,0x67,0xb4,0x01,0x46,0xe0,0x11,0x03,0x86,0x92};
 FILE *input=fopen(argv[1],"rb");assert(input);
 assert(fread(blob,1,sizeof(blob),input)==sizeof(blob));assert(fgetc(input)==EOF);fclose(input);
 assert(nah_board_load(blob,sizeof(blob),digest)==0);
 /* A (user3..user6): the camera's first session before the first sound: the first start after boot
  * (amps still held from init, reset_changed=1) went straight to the amplifiers -> i2c=5/16, then the stop's clock
  * restore failed -> uncertain=1 (sticky). */
 full_prepare(0x0c74,0x0c74,0x80);
 assert(nau_hw_init(&hw,&allio,0,nah_board_profile())==0);       /* boot: CAM off */
 cam_on(1);
 assert(lifecycle(0)==0);
 assert(lifecycle(1000000)==0);
 /* B (user2): sounds before the camera, then CAM on: the next start's amps_prepare -> bus_clock_prepare refused
  * (-3, i2c=16/0 i2cpa=11d04104, uncertain=0) on every later start. */
 full_prepare(0x0c74,0x0c74,0x80);
 assert(nau_hw_init(&hw,&allio,0,nah_board_profile())==0);       /* boot: CAM off */
 assert(lifecycle(0)==0);                                         /* a sound before the camera (user2 l.13223) */
 assert(bm.gate==16);
 cam_on(1);                                                       /* camera's first session: CAM domain on */
 for (unsigned k=0;k<3;k++) {
  assert(lifecycle(1000000ull*(k+1))==0);                        /* shutter sound, video start/stop chimes */
  assert(bm.gate==16 && !hw.reset_changed && !hw.bus.offline && !hw.bus.uncertain);
  assert((bm.slave[0].reg[0]&9)==1 && (bm.slave[1].reg[0]&9)==1); /* outputs off, powered down after each stop */
 }
 /* silent (microphone-path) start/stop with CAM on */
 assert(nau_hw_start(&hw,48000,2,16,1,256,0,5000000)==0);
 cm.current=NAF_SRAM; assert(nau_hw_tick(&hw,5000000)==0);
 assert(nau_hw_stop(&hw)==0 && !hw.status.uncertain);
 cam_on(0);
 assert(lifecycle(6000000)==0);                                  /* and with CAM off again */
 /* a cold init (amps_prepare: bus clock prepare) while CAM is already on */
 full_prepare(0x0c74,0x0c74,0x80); cam_on(1);
 assert(nau_hw_init(&hw,&allio,0,nah_board_profile())==0);
 assert(lifecycle(0)==0);
 m_check_restored(&bm);
 for(unsigned i=2;i<M_SLAVES;i++)assert(!bm.slave[i].accessed);
 printf("PASS 0330: amplifier start/monitor/stop and cold init with the CAM MTCMOS on (camera session) - no stage-5 refusal, no clock-restore error, nothing uncertain\n");
 return 0;
}
