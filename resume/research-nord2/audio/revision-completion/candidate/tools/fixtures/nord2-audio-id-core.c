/* Revision2 snapshot/PIO tests; simulated registers only. */
#include <assert.h>
#include "audio_id.h"
#include "audio_i2c8.h"
#include "nord2-audio-i2c8-model.h"
struct ctx {struct i2c8_model m;unsigned calls,writes,fail,failed,after,fail_delay;uint32_t snap[NAU_SNAP_COUNT];};
static int tick(struct ctx*c){if(c->failed)c->after++;if(++c->calls==c->fail){c->failed=1;return -1;}return 0;}
static int rd(void*p,uint32_t a,unsigned w,uint32_t*v){struct ctx*c=p;if(tick(c))return -1;*v=m_read(&c->m,a,w);return 0;}
static int wr(void*p,uint32_t a,unsigned w,uint32_t v){struct ctx*c=p;c->writes++;m_write(&c->m,a,w,v);return tick(c);}
static int us(void*p,unsigned n){struct ctx*c=p;assert(n==10);if(tick(c))return -1;if(c->fail_delay){c->failed=1;return -1;}return 0;}
static const struct na8_io io={rd,wr,us};
static int snap(void*p,uint32_t a,unsigned w,uint32_t*v){struct ctx*c=p;if(tick(c))return -1;assert(nau_read_allowed(a,w));
 if(w==4)for(unsigned i=0;i<NAU_SNAP_COUNT;i++)if(a==nau_snapshot_pa[i]){*v=c->snap[i];return 0;}
 if(w==4)for(unsigned i=0;i<NAU_AFE_COUNT;i++)if(a==nau_afe_pa[i]){assert(!nau_afe_block(c->snap,NAU_SNAP_COUNT));*v=0x12340000+i;return 0;}
 *v=m_read(&c->m,a,w);return 0;
}
static const struct nau_ops so={snap};
static void setup(struct ctx*c){memset(c,0,sizeof(*c));m_init(&c->m);c->snap[NAU_S_MODE_120]=c->m.pinmux;c->snap[NAU_S_IFR1_CG]=c->m.ifr1;c->snap[NAU_S_BM_IES122]=c->m.ies;c->snap[NAU_S_DIN_96]=c->m.pads;}
int main(void){struct ctx c;struct nau_state ss={0};struct na8_state s={0};struct na8_amp amp[2];uint16_t v;
 setup(&c);assert(nau_run(&so,&c,0,&ss)==-1&&!c.calls);assert(!nau_run(&so,&c,1,&ss)&&!c.writes&&ss.i2c8_read==NAU_I2C8_COUNT&&!ss.afe_read&&!ss.dma_read);assert(!nau_i2c8_ready(&ss));unsigned calls=c.calls;assert(nau_run(&so,&c,1,&ss)==-2&&c.calls==calls);
 for(unsigned f=1;f<=calls;f++){setup(&c);c.fail=f;ss=(struct nau_state){0};assert(nau_run(&so,&c,1,&ss)==-4&&ss.failed&&!c.after&&!c.writes);}
 setup(&c);c.snap[NAU_S_IMPS_CG]=1u<<4;ss=(struct nau_state){0};assert(!nau_run(&so,&c,1,&ss)&&!ss.i2c8_read&&nau_i2c8_ready(&ss));
 setup(&c);c.snap[NAU_S_CLK_CFG_10]=1u<<15;ss=(struct nau_state){0};assert(!nau_run(&so,&c,1,&ss)&&!ss.i2c8_read);
 setup(&c);c.snap[NAU_S_PWR_STATUS]=c.snap[NAU_S_PWR_STATUS_2ND]=1u<<21;c.snap[NAU_S_AUDIO_PWR_CON]=0xd;ss=(struct nau_state){0};assert(!nau_run(&so,&c,1,&ss)&&ss.afe_read==NAU_AFE_COUNT);
 setup(&c);s=(struct na8_state){0};assert(!na8_probe(&io,&c,&s,amp));assert(amp[0].rev==0xb74&&amp[0].reads==3&&amp[1].reads==3&&c.m.ntrans==6&&na8_return_ok(&s));m_check_trans(&c.m);
 setup(&c);s=(struct na8_state){0};assert(!na8_read16(&io,&c,&s,0x34,3,&v)&&v==0xb74);calls=c.calls;
 for(unsigned f=1;f<=calls;f++){setup(&c);c.fail=f;s=(struct na8_state){0};assert(na8_read16(&io,&c,&s,0x34,3,&v)&&s.offline&&!c.after);unsigned old=c.calls;assert(na8_read16(&io,&c,&s,0x34,3,&v)&&c.calls==old);assert(na8_return_ok(&s)==!s.uncertain);}
 for(unsigned i=0;i<3;i++){setup(&c);c.m.slave[0].reg[3]=i==0?0x1234:i==1?0x0174:0x0194;s=(struct na8_state){0};assert(!na8_probe(&io,&c,&s,amp)&&amp[0].reads==1&&amp[0].cls!=NA8_SUPPORTED&&amp[1].reads==3&&c.m.ntrans==4);m_check_trans(&c.m);}
 setup(&c);c.m.behaviour=M_HANG;s=(struct na8_state){0};assert(na8_probe(&io,&c,&s,amp)==-2&&s.polls==2000&&s.inflight&&s.uncertain&&c.m.starts==1&&!amp[1].reads&&!na8_return_ok(&s));
 setup(&c);c.m.behaviour=M_HANG;c.fail_delay=1;s=(struct na8_state){0};assert(na8_probe(&io,&c,&s,amp)==-2&&s.failure==12&&s.uncertain&&!c.after&&c.m.starts==1);
 setup(&c);c.m.slave[0].present=0;s=(struct na8_state){0};assert(na8_probe(&io,&c,&s,amp)==-2&&s.offline&&c.m.starts==1&&!amp[1].reads);
 setup(&c);c.m.ch[1][6]=1;s=(struct na8_state){0};assert(!na8_read16(&io,&c,&s,0x34,3,&v)&&s.prior_ack);
 setup(&c);c.m.ch[1][6]=1;c.m.irq_sticky=1;s=(struct na8_state){0};assert(na8_read16(&io,&c,&s,0x34,3,&v)==-2&&s.uncertain&&!c.m.starts);
 for(unsigned k=0;k<13;k++){setup(&c);s=(struct na8_state){0};if(k==0)c.m.gate=16;if(k==1)c.m.pwr=1u<<23;if(k==2)c.m.ch[0][0x24/2]=1;if(k==3)c.m.ch[2][0x24/2]=1;if(k==4)c.m.ch[1][0x10/2]|=4;if(k==5)c.m.ch[1][0xf4/2]=1;if(k==6)c.m.ch[1][6]=3;if(k==7)c.m.pads=0;if(k==8)c.m.ies=0;if(k==9)c.m.ch[0][0x10/2]=4;if(k==10)c.m.ch[2][0xf4/2]=1;if(k==11){c.m.ifr1=0;c.m.dma[2]=1;}if(k==12)c.m.ch[2][0x10/2]=4;assert(na8_read16(&io,&c,&s,0x34,3,&v)==-1&&!c.writes&&!c.m.starts&&na8_return_ok(&s));}
 setup(&c);s=(struct na8_state){0};assert(na8_read16(&io,&c,&s,0x51,3,&v)&&!c.calls&&!c.writes);
 puts("PASS revision2 snapshot zero-write/gates/faults; PIO source args/BE REV/variant-first/each callback failure/no replay/timeout/shared-bus refusal");return 0;
}
