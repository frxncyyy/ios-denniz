#include <assert.h>
#include <stdio.h>
#include "audio_hw.h"
#include "audio_amp.h"
static uint16_t regs[2][256];
static unsigned waits,closed,writes,fail_mute,fatal,reset_writes,stop_after=75;
static uint32_t reset_regs[4],current;
static int read_cb(void*p,uint32_t pa,unsigned w,uint32_t*v){(void)p;assert(w==4);if(pa==NAF_AFE_DL1_CUR){*v=current;return 0;}unsigned n=pa==0x10005130?0:pa==0x10005140?1:pa==0x10005030?2:3;assert(pa==0x10005130||pa==0x10005140||pa==0x10005030||pa==0x10005040);*v=reset_regs[n];return 0;}
static int write_cb(void*p,uint32_t pa,unsigned w,uint32_t v){(void)p;assert(w==4);unsigned n=pa==0x10005134?0:pa==0x10005144?1:pa==0x10005034?2:3;assert(pa==0x10005134||pa==0x10005144||pa==0x10005034||pa==0x10005044);reset_regs[n]|=v;reset_writes++;return 0;}
static uintptr_t who(void *p){(void)p;return 1;}
static int acquire(void*p,unsigned a){(void)p;(void)a;return 0;}
static int delay_cb(void*p,unsigned us){(void)p;assert(us==1000);assert(!(regs[0][0]&8));assert(!(regs[1][0]&8));waits++;return 0;}
int na8_read16(const struct na8_io*i,void*c,struct na8_state*s,unsigned a,unsigned r,uint16_t*v){(void)i;(void)c;(void)s;assert(a==0x34||a==0x37);unsigned n=a==0x37;if(r==0x14 && !n){assert(!(regs[1][0]&8));*v=waits<stop_after?8:0;}else *v=regs[n][r];return 0;}
int na8_write16(const struct na8_io*i,void*c,struct na8_state*s,unsigned a,unsigned r,uint16_t v){(void)i;(void)c;assert(a==0x34||a==0x37);assert(!fatal);writes++;if(fail_mute){s->offline=s->uncertain=1;return -2;}regs[a==0x37][r]=v;return 0;}
int nah_amp_lock(const struct na8_io*i,void*c,struct na8_state*s,unsigned a){(void)i;(void)c;(void)s;assert(a==0x34||a==0x37);return 0;}
int naf_stream_close(const struct naf_io*i,void*c,const struct nau_state*n,const struct naf_plan*p,struct naf_state*s){(void)i;(void)c;(void)n;(void)p;(void)s;if(fatal){assert(reset_writes==4);assert(reset_regs[0]&(1u<<15));assert(reset_regs[1]&(1u<<20));assert(reset_regs[2]&(1u<<15));assert(reset_regs[3]&(1u<<20));}closed++;return 0;}
/* Unused operations are discarded by function sections; real stop helpers remain. */
int main(void){struct nau_hw h={0};h.io.worker=who;h.io.lease=acquire;h.io.delay_us=delay_cb;h.owner=1;h.status.running=1;h.amps[0].addr=0x34;h.amps[1].addr=0x37;regs[0][0]=regs[1][0]=8;assert(nau_hw_stop(&h)==0);assert(waits==75&&closed==1&&writes==6);assert((regs[0][0]&9)==1&&(regs[1][0]&9)==1);assert(!h.status.uncertain);assert(nau_hw_stop(&h)==0&&closed==1);puts("PASS both amp outputs muted before slow first state wait; clean powerdown/idempotence");
 for(unsigned kind=0;kind<3;kind++){
  h=(struct nau_hw){0};h.io.worker=who;h.io.lease=acquire;h.io.delay_us=delay_cb;h.io.read=read_cb;h.io.write=write_cb;h.owner=1;h.status.running=1;h.reset_changed=1;
  reset_writes=0;for(unsigned j=0;j<4;j++)reset_regs[j]=0;fatal=1;unsigned before=writes;
  current=kind==1?NAF_SRAM+2:NAF_SRAM;h.last_progress_us=0;
  uint64_t now=kind==0?20000:kind==2?12000:1;
  int expected=kind==0?NAH_E_DEADLINE:kind==1?NAH_E_POINTER:NAH_E_CLOCK_STALLED;
  assert(nau_hw_tick(&h,now)==expected);assert(writes==before);assert(h.status.amp_reset_held&&h.status.uncertain&&h.status.stop_error);assert(!h.status.running);assert(h.reset_changed);assert(h.status.error==expected);
  assert(nau_hw_tick(&h,now+1)==NAH_E_ARG);
 }
 puts("PASS fatal deadline/pointer/stall assert local reset before AFE close with zero I2C, sticky unavailable");
 fatal=0;
 for(unsigned boundary=99;boundary<=101;boundary++){
  stop_after=boundary;waits=reset_writes=0;for(unsigned j=0;j<4;j++)reset_regs[j]=0;
  h=(struct nau_hw){0};h.io.worker=who;h.io.lease=acquire;h.io.delay_us=delay_cb;h.io.read=read_cb;h.io.write=write_cb;h.owner=1;h.status.running=1;h.amps[0].addr=0x34;h.amps[1].addr=0x37;h.reset_changed=boundary==101;regs[0][0]=regs[1][0]=8;
  int rc=nau_hw_stop(&h);assert(waits==100);assert((rc<0)==(boundary==101));if(boundary==101)assert(h.status.amp_reset_held&&h.status.uncertain&&reset_writes==4);
 }
 puts("PASS MANSTATE99/100ms finalread accepted, beyond100ms fails closed with localreset");
 fatal=0;fail_mute=1;reset_writes=waits=writes=0;for(unsigned j=0;j<4;j++)reset_regs[j]=0;
 h=(struct nau_hw){0};h.io.worker=who;h.io.lease=acquire;h.io.delay_us=delay_cb;h.io.read=read_cb;h.io.write=write_cb;h.owner=1;h.status.running=1;h.reset_changed=1;h.amps[0].addr=0x34;h.amps[1].addr=0x37;
 assert(nau_hw_stop(&h)<0);assert(writes==1&&!waits&&reset_writes==4);assert(h.status.amp_reset_held&&h.status.uncertain&&!h.status.running);
 puts("PASS first mute uncertainty performs no next-slave transaction or wait, forces both local resets");return 0;}
