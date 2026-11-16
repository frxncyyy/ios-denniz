#if defined(KERNEL)
#include <pexpert/arm64/board_config.h>
#endif
#if defined(NORD2)
/* PRIVATE native polling provider. No touch/PMIF/DMA/charger/gauge controls.
 * Explicit nord2battery=1 activates class polling, this file supplies I2C PIO.
 * Read-only SMBus commands; chip/charger configurations are never written. */
#include <stdint.h>
#include <mach/mach_types.h>
#include <kern/clock.h>
extern "C" {
#include "nord2_battery.h"
#include "mtk_i2c_read.h"
#include "nbg_telemetry.h"
extern void IOLog(const char *,...) __attribute__((format(printf,1,2)));
extern vm_offset_t ml_io_map(vm_offset_t,vm_size_t);
extern void delay(int);
extern volatile uint32_t nord2_chg_vbus_good;   /* 0700 sweet_usb.c: VBUS seen by the charger (late USB attach) */
extern uint32_t nord2_usb_host_events(void);     /* 0700 sweet_usb.c */
extern unsigned nord2_chg_pg_active_low;         /* 0701 nord2_battery.c */
extern uint32_t nord2_usb_configured(void);
extern uint32_t nord2_usb_console_state(void);
}
struct nbg_map {uint32_t pa;vm_size_t size;vm_offset_t va;};
static struct nbg_map maps[]={{0x10000000,0x1000,0},{0x1000c000,0x1000,0},{0x10005000,0x1000,0},
 {0x11d05000,0x1000,0},{0x11008000,0x1000,0},{0x11d03000,0x1000,0},{0x1101a000,0x1000,0}};
static unsigned busy;
static struct nbg_i2c_state buses[2];
static unsigned bus_logged[2],bus_last_stage[2],bus_last_failure[2];
static int bus_last_rc[2];
static unsigned sample_logged,sample_last_valid;
static int sample_last_rc;
/* 0701: charger configuration dump on change (ADC words excluded) or every 30th sample */
static uint8_t chg_last[NBG_CHG_REGS];
static unsigned chg_last_valid,chg_dumps,chg_samples;
static void report_charger(const struct nbg_sample *s){
 chg_samples++;
 bool same=s->chg_regs_valid==chg_last_valid;
 for(unsigned i=0;same&&i<NBG_CHG_REGS;i++)if(i!=0x13&&chg_last[i]!=s->chg_reg[i])same=false;
 if(same&&(chg_samples%30))return;
 for(unsigned i=0;i<NBG_CHG_REGS;i++)chg_last[i]=s->chg_reg[i];
 chg_last_valid=s->chg_regs_valid;chg_dumps++;
 const uint8_t *r=s->chg_reg;
 IOLog("NORD2 CHG REGS: valid=%#x 00=%02x 01=%02x 02=%02x 03=%02x 04=%02x 05=%02x 06=%02x 07=%02x 08=%02x 09=%02x 0a=%02x 0b=%02x"
  " 0c=%02x 0d=%02x 0e=%02x 0f=%02x 10=%02x 11=%02x 12=%02x 13=%02x 14=%02x 15=%02x\n",s->chg_regs_valid,
  r[0],r[1],r[2],r[3],r[4],r[5],r[6],r[7],r[8],r[9],r[10],r[11],r[12],r[13],r[14],r[15],r[16],r[17],r[18],r[19],r[20],r[21]);
 IOLog("NORD2 CHG CFG: dump=%u ilim=%umA vindpm=%umV ichg_set=%umA vfloat=%umV/cell chg_en=%u learn(hiz)=%u otg=%u wdt=%u words=%#x\n",chg_dumps,
  s->ilim_ma,s->vindpm_mv,s->ichg_set_ma,s->vfloat_cell_mv,s->chg_en,s->learn,s->otg,s->wdt,s->chg_words_valid);
}
static void report_bus(unsigned bus,int rc,const struct nbg_i2c_state *s){
 unsigned i=bus==7?0:1;
 if(bus_logged[i]&&bus_last_stage[i]==s->stage&&bus_last_failure[i]==s->failure&&bus_last_rc[i]==rc)return;
 bus_logged[i]=1;bus_last_stage[i]=s->stage;bus_last_failure[i]=s->failure;bus_last_rc[i]=rc;
 IOLog(NBG_I2C_LOG,bus,rc,s->stage,s->failure,s->observed,s->offline,s->inflight,
  s->last_addr,s->last_value,s->last_valid,s->last_write,s->last_width);
 IOLog(NBG_PREFLIGHT_LOG,bus,s->prior_irq,s->prior_ack);
 IOLog(NBG_CLOCK_LOG,bus,s->mux,s->parent_hz,s->pinmux,s->initial_gate,s->completion_irq,s->polls,s->observed);
}
static vm_offset_t address(uint32_t p,unsigned bytes){
 for(unsigned i=0;i<7;i++)if(p>=maps[i].pa&&(uint64_t)p+bytes<=(uint64_t)maps[i].pa+maps[i].size){
  if(!maps[i].va)maps[i].va=ml_io_map(maps[i].pa,maps[i].size);
  return maps[i].va?maps[i].va+p-maps[i].pa:0;
 }
 return 0;
}
static int rd(void *,uint32_t p,unsigned width,uint32_t *out){
 vm_offset_t v=address(p,width);if(!v||!out||(p&(width-1)))return -1;
 if(width==1)*out=*(volatile uint8_t *)v;
 else if(width==2)*out=*(volatile uint16_t *)v;
 else if(width==4)*out=*(volatile uint32_t *)v;else return -1;
 __asm__ volatile("dmb oshld" ::: "memory");return 0;
}
static int wr(void *,uint32_t p,unsigned width,uint32_t value){
 vm_offset_t v=address(p,width);if(!v||(p&(width-1)))return -1;
 __asm__ volatile("dmb oshst" ::: "memory");
 if(width==1)*(volatile uint8_t *)v=(uint8_t)value;
 else if(width==2)*(volatile uint16_t *)v=(uint16_t)value;
 else if(width==4)*(volatile uint32_t *)v=value;else return -1;
 __asm__ volatile("dsb osh" ::: "memory");return 0;
}
static int us(void *,unsigned n){if(n>10000)return -1;delay((int)n);return 0;}
static const struct nbg_i2c_io io={rd,wr,us};
static int owned(void *){return busy==1?0:-1;}
static int read(void *,unsigned bus,unsigned slave,unsigned cmd,unsigned bytes,uint16_t *out){
 if(bus!=7&&bus!=11)return -1;
 struct nbg_i2c_state *s=&buses[bus==7?0:1];
 int rc=nbg_i2c_read(&io,0,s,bus,slave,cmd,bytes,out);
 report_bus(bus,rc,s);return rc;
}
static int ms(void *,unsigned n){if(n!=10)return -1;delay(10000);return 0;}
static const struct nbg_ops ops={owned,read,ms};
/* 0702: byte writes, MP2650 bus only (the allow-list in mtk_i2c_read.c is the second fence) */
static int write8(void *,unsigned bus,unsigned slave,unsigned reg,unsigned value){
 if(bus!=11||busy!=1)return -1;
 struct nbg_i2c_state *s=&buses[1];
 int rc=nbg_i2c_write8(&io,nullptr,s,bus,slave,reg,value);
 report_bus(bus,rc,s);return rc;
}
static const struct nbg_wops wops={write8};
/* 0703: cheap plug check, MP2650 REG13 read twice (the same consistency rule as nbg_collect), power-good bit only. */
extern "C" int nord2_battery_platform_pg(unsigned *pg);
extern "C" int nord2_battery_platform_pg(unsigned *pg){
 if(!pg)return -1;
 unsigned expect=0;
 if(!__atomic_compare_exchange_n(&busy,&expect,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED))return -1;
 uint16_t a=0xffff,b=0xffff;
 int e=read(nullptr,11,0x5c,0x13,1,&a);
 if(!e)e=read(nullptr,11,0x5c,0x13,1,&b);
 __atomic_store_n(&busy,0u,__ATOMIC_RELEASE);
 if(e||a>255||b>255||((a^b)&2))return -1;
 *pg=((a>>1)&1u)^(nord2_chg_pg_active_low?1u:0u);
 nord2_chg_vbus_good=*pg;
 return 0;
}
extern "C" int nord2_battery_platform_sample(struct nbg_sample *s){
 if(!s)return -1;
 if(!__sync_bool_compare_and_swap(&busy,0u,1u)){*s=(struct nbg_sample){};s->error=-1;return -1;}
 /* Nocontrol-device-type query yet: SOC standardread available, current0c
  * deliberately unavailable rather than guessing BQ/ZY installed identity. */
 int e=nbg_collect(&ops,0,1,1,s);   /* 0701: identified = 1, DT qcom,batt_bq28z610 (see nord2_battery.h) */
 nord2_chg_vbus_good=s->charger_valid?s->power_good:(e?2u:nord2_chg_vbus_good);
 if(s->charger_valid)report_charger(s);
 if(s->charger_valid){   /* 0702 */
  uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
  uint32_t st=nord2_usb_console_state();
  (void)nbg_charger_control(&ops,&wops,nullptr,s,nord2_usb_host_events(),nord2_usb_configured(),st==1||st==3,ns/1000000ull);
 }
 unsigned valid=s->raw_valid|(s->soc_valid<<8)|(s->charger_valid<<9);
 if(!sample_logged||sample_last_valid!=valid||sample_last_rc!=e){
  sample_logged=1;sample_last_valid=valid;sample_last_rc=e;
  IOLog(NBG_RAW_LOG,e,s->raw_valid,(unsigned)s->raw_soc[0],(unsigned)s->raw_soc[1],
   (unsigned)s->raw_voltage,(unsigned)s->raw_temperature,(unsigned)s->raw_current,
   (unsigned)s->raw_status[0],(unsigned)s->raw_status[1],(unsigned)s->raw_fault,s->soc_valid,s->charger_valid);
 }
 __sync_lock_release(&busy);return e;
}

#endif /* NORD2 */
