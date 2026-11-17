#include "speed-stubs.h"
#include "android-speed.inc"
#include "audio_i2c8.h"
#include "test-i2c-model.h"
static unsigned programmed_timing,programmed_ltiming,programmed_timeout;
static int rd(void *c,uint32_t pa,unsigned w,uint32_t *v){*v=m_read(c,pa,w);return 0;}
static int wr(void *c,uint32_t pa,unsigned w,uint32_t v){struct i2c8_model *m=c;if(pa==0x11d04124){programmed_timing=m->ch[1][0x20/2];programmed_ltiming=m->ch[1][0x2c/2];programmed_timeout=m->ch[1][0x4c/2];}m_write(c,pa,w,v);return 0;}
static int us(void *c,unsigned n){(void)c;(void)n;return 0;}
int main(void){
 const struct na8_io io={rd,wr,us};struct comp flags={1,1,1};
 /* Actual26MHz, mainpll-derived68.25MHz, actualuniv124.8MHz, boundary parents. */
 const unsigned parents[]={26000000,68250000,124800000};
 for(unsigned k=0;k<3;k++){
  struct i2c8_model m;m_init(&m);struct na8_state s={0};uint16_t v;
  m.ch[1][0x24/2]=2;m.ch[1][0xec/2]=0x8800;
  if(k==1){m.mux=1u<<8;m.mainpll[1]=0x150000;}
  if(k==2){m.mux=2u<<8;m.univpll[1]=0x180000;}
  m.hclk=(parents[k]/5)/1000;
  struct mt_i2c a={.dev_comp=&flags,.speed_hz=100000};
  assert(!i2c_set_speed(&a,parents[k]/5));
  assert(!na8_read16(&io,&m,&s,0x34,3,&v));
  assert(s.parent_hz==parents[k]);
  assert(programmed_timing==(a.timing_reg|1) && programmed_ltiming==a.ltiming_reg && !a.high_speed_reg);
  /* Captured values are overwritten on restore; compare setup values from model. */
  unsigned tv2=((a.ltiming_reg&63)+1)*(((a.ltiming_reg>>6)&7)+1);
  assert(m.setup_hw_timeout==((m.hclk*2+tv2-1)/tv2) && programmed_timeout==m.setup_hw_timeout);
  /* savedword source is deliberately0x1106; intercept setup at launch below. */
  assert(m.ch[1][0x20/2]==m.saved[6] && m.ch[1][0x2c/2]==m.saved[8]);
  printf("sourceoracle parent%u timing%x LT%x HS%x timeout%u\n",parents[k],a.timing_reg|1,a.ltiming_reg,a.high_speed_reg,m.setup_hw_timeout);
 }
 puts("PASS compiled exact Android speed helper for all selected mux parents");
}
