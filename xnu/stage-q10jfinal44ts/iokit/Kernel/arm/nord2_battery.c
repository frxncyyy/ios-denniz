#if defined(KERNEL)
#include <pexpert/arm64/board_config.h>
#endif
#if defined(NORD2)
#include "nord2_battery.h"
/* 0701 knobs (sysctl in the kernel build; plain globals in host tests) */
unsigned nord2_bat_poll_ms = 10000, nord2_bat_hold_s = 120, nord2_bat_filter = 1, nord2_bat_log_s = 60, nord2_bat_legacy = 0;
unsigned nord2_chg_pg_active_low = 0;
unsigned nord2_bat_fast_ms = 500;       /* 0703: plug/unplug check period ms (100..2000), 0 = only the full sample */   /* 1 = the pre-0701 (wrong, v1 header) REG13 bit1 polarity */
#if defined(KERNEL)
#include <sys/types.h>
#include <sys/sysctl.h>
SYSCTL_UINT(_kern, OID_AUTO, n2bat_poll_ms, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_bat_poll_ms, 0, "0701: battery poll period ms (2000..60000)");
SYSCTL_UINT(_kern, OID_AUTO, n2bat_hold_s, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_bat_hold_s, 0, "0701: hold the last valid gauge sample s");
SYSCTL_UINT(_kern, OID_AUTO, n2bat_filter, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_bat_filter, 0, "0701: 1 = hide one-step SOC flicker against the current");
SYSCTL_UINT(_kern, OID_AUTO, n2bat_log_s, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_bat_log_s, 0, "0701: NORD2 BAT14 log period s");
SYSCTL_UINT(_kern, OID_AUTO, n2bat_legacy, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_bat_legacy, 0, "0701: 1 = pre-0701 IOPMPowerSource publishing");
SYSCTL_UINT(_kern, OID_AUTO, n2bat_fast_ms, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_bat_fast_ms, 0, "0703: VBUS fast check period ms, 0 = off");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_pg_active_low, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_chg_pg_active_low, 0, "0701: 1 = old MP2650 power-good polarity");
#endif
unsigned nord2_chg_policy_type(void);
const char *nord2_chg_policy_name(void);
static int get(const struct nbg_ops *o,void *c,unsigned bus,unsigned slave,unsigned reg,unsigned bytes,uint16_t *v){
 return o->owned(c)||o->read(c,bus,slave,reg,bytes,v);
}
static int capture(const struct nbg_ops *o,void *c,struct nbg_sample *s,unsigned bit,unsigned bus,unsigned slave,unsigned reg,unsigned bytes,uint16_t *v){
 int e=get(o,c,bus,slave,reg,bytes,v);if(!e)s->raw_valid|=1u<<bit;return e;
}
static int signed_ma(uint16_t raw){return (raw&0x8000)?(int)raw-65536:(int)raw;}
int nbg_collect(const struct nbg_ops *o,void *c,unsigned enabled,unsigned identified,struct nbg_sample *s){
 if(!s)return -1;
 *s=(struct nbg_sample){0};s->error=-1;
 if(enabled!=1||!o||!o->owned||!o->read||!o->delay_ms)return -1;
 if(capture(o,c,s,0,7,0x55,0x2c,2,&s->raw_soc[0]))return -1;
 if(o->owned(c)||o->delay_ms(c,10))return -1;
 if(capture(o,c,s,1,7,0x55,0x2c,2,&s->raw_soc[1]))return -1;
 unsigned a=s->raw_soc[0],b=s->raw_soc[1];
 if(a>100||b>100||(a>b?a-b:b-a)>3)return -1;
 s->percent=b;s->soc_valid=1;s->error=0;
 /* Optional ancillary observations never fabricate/erase a valid SOC. */
 if(!capture(o,c,s,2,7,0x55,8,2,&s->raw_voltage)&&s->raw_voltage>=5000&&s->raw_voltage<=10000){s->voltage_valid=1;s->pack_mv=s->raw_voltage;}
 if(!capture(o,c,s,3,7,0x55,6,2,&s->raw_temperature)&&s->raw_temperature>=2000&&s->raw_temperature<=4000){s->temperature_valid=1;s->temperature_centi_c=((int)s->raw_temperature-2731)*10;}
 /* Linux selects0c only after reading device type BQ28Z610/ZY0603.
  * Gauge current is positive charging; Linux getter negates for its API,
  * IOPMPowerSource uses positive charging so preserve signed word here. */
 if(identified==1&&!capture(o,c,s,4,7,0x55,0x0c,2,&s->raw_current)&&s->raw_current!=0x8000){
  s->current_ma=signed_ma(s->raw_current);
  if(s->current_ma>=-15000&&s->current_ma<=15000)s->current_valid=1;
 }
 /* 0701: AverageCurrent (standard 0x14, signed mA, + = charging), capacities (mAh) and BatteryStatus. */
 if(!capture(o,c,s,8,7,0x55,0x14,2,&s->raw_avg_current)&&s->raw_avg_current!=0x8000){
  s->avg_current_ma=signed_ma(s->raw_avg_current);
  if(s->avg_current_ma>=-15000&&s->avg_current_ma<=15000)s->avg_valid=1;
 }
 if(!capture(o,c,s,9,7,0x55,0x10,2,&s->raw_rm)&&!capture(o,c,s,10,7,0x55,0x12,2,&s->raw_fcc)&&
    s->raw_fcc>=1000&&s->raw_fcc<=6000&&s->raw_rm<=s->raw_fcc+200){s->capacity_valid=1;s->rm_mah=s->raw_rm;s->fcc_mah=s->raw_fcc;}
 (void)capture(o,c,s,11,7,0x55,0x0a,2,&s->raw_flags);
 /* MP2650 REG13 (v2 HAL oplus_hal_mp2650.h, the corrected polarity): bit1 VIN power good = 1, charging[3:2],
  * PPM bit4 (VINDPM/IINDPM active), AICL bit5.  REG14 faults.  0701 FIX: before, bit1 was read as active-low, so a
  * connected charger (status 0x3b = good, fast charge) was reported as external=0 charging=0.
  * Independent optional charger bus; do not infer cable solely from current. */
 if(!capture(o,c,s,5,11,0x5c,0x13,1,&s->raw_status[0])&&
    !capture(o,c,s,6,11,0x5c,0x14,1,&s->raw_fault)&&
    !capture(o,c,s,7,11,0x5c,0x13,1,&s->raw_status[1])&&
    s->raw_status[0]<=255&&s->raw_status[1]<=255&&s->raw_fault<=255&&
    ((s->raw_status[0]^s->raw_status[1])&0x0e)==0){
  s->charger_valid=1;
  unsigned st=s->raw_status[1];
  s->power_good=((st>>1)&1)^(nord2_chg_pg_active_low?1u:0u);s->external=s->power_good;
  s->chg_state=(st>>2)&3;s->ppm=(st>>4)&1;s->aicl=(st>>5)&1;
  s->ntc_fault=s->raw_fault&7;s->bat_ovp=(s->raw_fault>>3)&1;s->chg_fault=(s->raw_fault>>4)&3;
  s->charging=s->external&&(s->chg_state==1||s->chg_state==2)&&!s->bat_ovp&&!s->chg_fault;
  s->full=s->external&&s->chg_state==3&&!s->bat_ovp&&!s->chg_fault;
  /* 0701: configuration + ADC snapshot (read only), only while the charger answers. */
  for(unsigned r=0;r<NBG_CHG_REGS;r++){
   uint16_t v=0;
   if(r==0x13){s->chg_reg[r]=(uint8_t)st;s->chg_regs_valid|=1u<<r;continue;}
   if(r==0x14){s->chg_reg[r]=(uint8_t)s->raw_fault;s->chg_regs_valid|=1u<<r;continue;}
   if(get(o,c,11,0x5c,r,1,&v)||v>255)break;
   s->chg_reg[r]=(uint8_t)v;s->chg_regs_valid|=1u<<r;
  }
  static const uint8_t words[NBG_CHG_WORDS]={0x16,0x18,0x1a,0x1c,0x1e,0x28};
  for(unsigned i=0;i<NBG_CHG_WORDS&&s->chg_regs_valid==(1u<<NBG_CHG_REGS)-1;i++){
   uint16_t v=0;if(get(o,c,11,0x5c,words[i],2,&v))break;
   s->chg_word[i]=v;s->chg_words_valid|=1u<<i;
  }
  if(s->chg_regs_valid&1)s->ilim_ma=(s->chg_reg[0]&0x7f)*50u;
  if(s->chg_regs_valid&2)s->vindpm_mv=s->chg_reg[1]*100u;
  if(s->chg_regs_valid&4)s->ichg_set_ma=s->chg_reg[2]*50u;
  if(s->chg_regs_valid&0x10)s->vfloat_cell_mv=(37125u+125u*((s->chg_reg[4]>>1)&0x3f))/10u;
  if(s->chg_regs_valid&0x100){s->learn=(s->chg_reg[8]>>3)&1;s->chg_en=(s->chg_reg[8]>>4)&1;s->otg=(s->chg_reg[8]>>5)&1;}
  if(s->chg_regs_valid&0x200)s->wdt=(s->chg_reg[9]>>4)&3;
  if(s->chg_words_valid&1)s->vbat_chg_mv=(unsigned)((s->chg_word[0]>>6)*125u/10u);
  if(s->chg_words_valid&4)s->ichg_adc_ma=(unsigned)((s->chg_word[2]>>6)*125u/10u);
  if(s->chg_words_valid&8)s->vin_mv=(unsigned)((s->chg_word[3]>>6)*25u);
  if(s->chg_words_valid&16)s->iin_ma=(unsigned)((s->chg_word[4]>>6)*625u/100u);
  if(s->chg_words_valid&32)s->idis_ma=(unsigned)((s->chg_word[5]>>6)*125u/10u);
 }
 return 0;
}

/*
 * 0702 (usbchg-20261115): MP2650 charge control.  Root cause of "never charges with iOS": nothing on this port ever configured
 * the 2S buck-boost charger; it keeps whatever the bootloader / its own reset left (an LK USB input limit, 500 mA on plug in
 * Oplus' flow, or reset defaults), the MT6360 single-cell charger stays suspended (correct for the 2S pack), so with the display
 * and GPU on the input power was below the load: discharge while "connected".  This policy, once per battery poll (10 s):
 *   type   host (our USB console saw SET_CONFIGURATION / SETUPs after this plug-in)       -> kern.n2chg_sdp_ma   (500)
 *          no host traffic 4 s after plug-in while the console runs (wall charger/DCP)  -> kern.n2chg_dcp_ma   (2000, DT input_current_charger_ma)
 *          unknown (no USB console this boot)                                           -> kern.n2chg_unk_ma   (1500)
 *   input limit REG00/REG0F ramps 500 mA per poll to the target while VIN (MP2650 ADC) stays >= kern.n2chg_aicl_mv (4500 mV);
 *          a VIN below it steps back 500 mA and caps the ramp until the next plug-in (software AICL, Oplus mp2650 style);
 *          VINDPM REG01 is kept in 4.3..4.6 V (4.4 V written otherwise) as the hardware AICL behind it.
 *   charge current REG02 by gauge temperature (DT 20827 JEITA, rounded down): < 0 C off, 0..10 C 500 mA, 10..45 C kern.n2chg_ichg_ma
 *          (1100), 45..50 C 600 mA, >= 50 C off (back on below 47 C / above 2 C); unknown temperature 500 mA.
 *   REG08: LEARN(HiZ) bit 3 = 0, CHG_EN bit 4 per temperature/voltage guard (REG_RST/WTD_RST/OTG are never written as 1).
 *   REG09: the I2C watchdog field is cleared: the MP2650 reset defaults are 3 A input / 3 A charge, so a watchdog reset after this
 *          driver stops would be LESS safe than keeping the last (temperature-checked) setting.
 *   guards: pack > 8900 mV (4.45 V/cell) or float > 4460 mV/cell -> CHG_EN 0 until pack < 8800 mV; never touches the float voltage,
 *          OTG, MT6360, VOOC MCU or charge pump.  Each write is read back; 3 failures in a row -> control latched off for the boot.
 *   kern.n2chg_ctl 0 = observe only (no writes, kill switch), 1 = control (default).
 * Log: NORD2 CHG14 lines on every decision change and every write.
 */
#if defined(KERNEL)
extern void IOLog(const char *, ...) __attribute__((format(printf, 1, 2)));
#define N2C_LOG IOLog
#else
#include <stdio.h>
#define N2C_LOG printf
#endif
unsigned nord2_chg_ctl = 1, nord2_chg_sdp_ma = 500, nord2_chg_dcp_ma = 2000, nord2_chg_unk_ma = 1500, nord2_chg_ichg_ma = 1100,
         nord2_chg_aicl_mv = 4500;
unsigned nord2_chg_type, nord2_chg_target_ma, nord2_chg_writes, nord2_chg_errors, nord2_chg_latched;
#if defined(KERNEL)
SYSCTL_UINT(_kern, OID_AUTO, n2chg_ctl, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_chg_ctl, 0, "0702: 1 = MP2650 charge control, 0 = observe only");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_sdp_ma, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_chg_sdp_ma, 0, "0702: input limit mA with a USB host (<= 1500)");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_dcp_ma, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_chg_dcp_ma, 0, "0702: input limit mA for a non-host source (<= 2000)");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_unk_ma, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_chg_unk_ma, 0, "0702: input limit mA when the source type is unknown (<= 2000)");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_ichg_ma, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_chg_ichg_ma, 0, "0702: battery charge current mA at 10..45 C (<= 2000)");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_aicl_mv, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_chg_aicl_mv, 0, "0702: software AICL VIN floor mV (4300..4800)");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_type, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_chg_type, 0, "0702: 0 none, 1 host, 2 deciding, 3 non-host, 4 unknown");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_target_ma, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_chg_target_ma, 0, "0702: input limit target mA");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_writes, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_chg_writes, 0, "0702: MP2650 register writes");
SYSCTL_UINT(_kern, OID_AUTO, n2chg_errors, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_chg_errors, 0, "0702: failed/unverified writes");
#endif
enum { N2C_NONE = 0, N2C_HOST = 1, N2C_DECIDING = 2, N2C_NONHOST = 3, N2C_UNKNOWN = 4 };
static const char *const n2c_type_name[] = {"none", "usb host", "deciding", "charger", "unknown source"};
static struct {
	unsigned pg_prev, first_done, ev0, aicl_cap, temp_off, volt_off, fails, last_ichg, last_en, last_type, last_target, logs;
	uint64_t plug_ms, step_ms;   /* 0703: step_ms = last ramp-up step (>= 5 s apart: 0703 runs extra samples after a plug edge) */
} n2c;
unsigned nord2_chg_policy_type(void) { return nord2_chg_ctl && !nord2_chg_latched ? (nord2_chg_type ? nord2_chg_type : 5u) : 0u; }
const char *nord2_chg_policy_name(void) { return nord2_chg_type < 5 ? n2c_type_name[nord2_chg_type] : "?"; }
static unsigned n2c_clamp(unsigned v, unsigned lo, unsigned hi) { return v < lo ? lo : v > hi ? hi : v; }
static int n2c_write(const struct nbg_ops *o, const struct nbg_wops *w, void *c, unsigned reg, unsigned value, uint8_t *cache)
{
	uint16_t back = 0xffff;
	int e = w->write8(c, 11, 0x5c, reg, value & 0xff);
	if (!e) { e = o->read(c, 11, 0x5c, reg, 1, &back); }
	nord2_chg_writes++;
	if (e || back != (value & 0xff)) {
		nord2_chg_errors++; n2c.fails++;
		N2C_LOG("NORD2 CHG14: write REG%02x=%02x FAILED (rc=%d back=%#x, %u in a row)\n", reg, value & 0xff, e, back, n2c.fails);
		if (n2c.fails >= 3) { nord2_chg_latched = 1; N2C_LOG("NORD2 CHG14: control latched off for this boot\n"); }
		return -1;
	}
	n2c.fails = 0;
	if (cache) { *cache = (uint8_t)value; }
	N2C_LOG("NORD2 CHG14: write REG%02x=%02x ok\n", reg, value & 0xff);
	return 0;
}
int nbg_charger_control(const struct nbg_ops *o, const struct nbg_wops *w, void *c, struct nbg_sample *s,
    unsigned host_events, unsigned configured, unsigned console_up, uint64_t now_ms)
{
	if (!o || !w || !w->write8 || !s) { return -1; }
	if (!s->charger_valid || s->chg_regs_valid != (1u << NBG_CHG_REGS) - 1) { return 0; }
	unsigned pg = s->power_good;
	if (!pg) {
		nord2_chg_type = N2C_NONE; n2c.aicl_cap = 0;
	} else if (!n2c.pg_prev || !n2c.first_done) {
		n2c.plug_ms = now_ms; n2c.ev0 = host_events; n2c.aicl_cap = 0;
		/* first sample of the boot with VBUS already there: a configured console means the host enumerated us at boot */
		nord2_chg_type = (!n2c.first_done && configured) ? N2C_HOST : console_up ? N2C_DECIDING : N2C_UNKNOWN;
	} else if (nord2_chg_type == N2C_DECIDING || nord2_chg_type == N2C_UNKNOWN) {
		if (console_up && host_events != n2c.ev0) { nord2_chg_type = N2C_HOST; }
		else if (console_up && now_ms - n2c.plug_ms >= 4000) { nord2_chg_type = N2C_NONHOST; }
		else if (!console_up) { nord2_chg_type = N2C_UNKNOWN; }
	} else if (nord2_chg_type == N2C_NONHOST && console_up && host_events != n2c.ev0) {
		nord2_chg_type = N2C_HOST;      /* a late enumeration: back to the host limit */
	}
	n2c.pg_prev = pg; n2c.first_done = 1;
	unsigned target = 500;
	switch (nord2_chg_type) {
	case N2C_HOST: target = n2c_clamp(nord2_chg_sdp_ma, 100, 1500); break;
	case N2C_NONHOST: target = n2c_clamp(nord2_chg_dcp_ma, 100, 2000); break;
	case N2C_UNKNOWN: target = n2c_clamp(nord2_chg_unk_ma, 100, 2000); break;
	default: target = 500; break;
	}
	target = target / 50 * 50;
	if (n2c.aicl_cap && target > n2c.aicl_cap) { target = n2c.aicl_cap; }
	nord2_chg_target_ma = target;
	/* temperature / voltage guards -> charge current and CHG_EN */
	int t = s->temperature_centi_c; unsigned tv = s->temperature_valid;
	if (tv && (t < 0 || t >= 5000)) { n2c.temp_off = 1; }
	else if (tv && t >= 200 && t < 4700) { n2c.temp_off = 0; }
	if (s->voltage_valid && s->pack_mv > 8900) { n2c.volt_off = 1; }
	else if (s->voltage_valid && s->pack_mv < 8800) { n2c.volt_off = 0; }
	unsigned float_bad = s->vfloat_cell_mv > 4460;
	unsigned ichg = !tv ? 500 : t < 1000 ? 500 : t < 4500 ? n2c_clamp(nord2_chg_ichg_ma, 100, 2000) : 600;
	ichg = ichg / 50 * 50;
	unsigned en = !(n2c.temp_off || n2c.volt_off || float_bad);
	if (nord2_chg_type != n2c.last_type || target != n2c.last_target || ichg != n2c.last_ichg || en != n2c.last_en || (++n2c.logs % 30) == 0) {
		N2C_LOG("NORD2 CHG14: type=%s pg=%u target=%umA (aicl_cap %u) ichg=%umA en=%u (temp %d.%02dC valid=%u off=%u, pack %umV off=%u, float %umV/cell)"
		    " | now ilim=%u ichg_set=%u vindpm=%u reg08=%02x reg09=%02x vin=%umV iin=%umA ppm=%u state=%u | ctl=%u latched=%u host_ev=%u cfg=%u console=%u\n",
		    nord2_chg_type < 5 ? n2c_type_name[nord2_chg_type] : "?", pg, target, n2c.aicl_cap, ichg, en, t / 100, (t < 0 ? -t : t) % 100, tv,
		    n2c.temp_off, s->pack_mv, n2c.volt_off, s->vfloat_cell_mv, s->ilim_ma, s->ichg_set_ma, s->vindpm_mv, s->chg_reg[8], s->chg_reg[9],
		    s->vin_mv, s->iin_ma, s->ppm, s->chg_state, nord2_chg_ctl, nord2_chg_latched, host_events, configured, console_up);
		n2c.last_type = nord2_chg_type; n2c.last_target = target; n2c.last_ichg = ichg; n2c.last_en = en;
	}
	if (!nord2_chg_ctl || nord2_chg_latched) { return 0; }
	/* 1. REG08: CHG_EN per guard, LEARN(HiZ) off; bits 5..7 (OTG, WTD_RST, REG_RST) kept / never set */
	unsigned r8 = s->chg_reg[8];
	unsigned want8 = (r8 & ~0xd8u) | (en ? 0x10u : 0u);
	if (r8 & 0x20u) { want8 = r8 & ~0xc0u; }          /* OTG on (not ours): leave the register alone */
	if (want8 != r8 && n2c_write(o, w, c, 0x08, want8, &s->chg_reg[8])) { return -1; }
	/* 2. REG09: no I2C watchdog (see above) */
	if ((s->chg_reg[9] & 0x30u) && n2c_write(o, w, c, 0x09, s->chg_reg[9] & ~0x30u, &s->chg_reg[9])) { return -1; }
	/* 3. VINDPM 4.3..4.6 V */
	if ((s->vindpm_mv < 4300 || s->vindpm_mv > 4600) && n2c_write(o, w, c, 0x01, 44, &s->chg_reg[1])) { return -1; }
	/* 4. charge current */
	if (s->ichg_set_ma != ichg && n2c_write(o, w, c, 0x02, ichg / 50, &s->chg_reg[2])) { return -1; }
	/* 5. input current limit: ramp / AICL */
	unsigned cur = s->ilim_ma, next = cur;
	unsigned floor_mv = n2c_clamp(nord2_chg_aicl_mv, 4300, 4800);
	unsigned adc = s->vin_mv >= 3000;                     /* VIN ADC plausible */
	if (!pg) { next = 500; }
	else if (cur > target) { next = target; }
	else if (adc && s->vin_mv < floor_mv && cur > 500) {
		next = cur - 500 < 500 ? 500 : cur - 500; n2c.aicl_cap = next;
		N2C_LOG("NORD2 CHG14: VIN %umV < %umV at %umA: back to %umA (cap until re-plug)\n", s->vin_mv, floor_mv, cur, next);
	} else if (cur < target) {
		unsigned step_to = cur + 500 > target ? target : cur + 500;
		if (!adc && step_to > 1000) { step_to = cur < 1000 ? (target < 1000 ? target : 1000) : cur; }   /* no ADC: no ramp past 1 A */
		if ((!adc || s->vin_mv >= floor_mv + 100) && now_ms - n2c.step_ms >= 5000) { next = step_to; }
	}
	if (next < 100) { next = 100; }
	if (next != cur) {
		unsigned v = (next / 50) & 0x7f;
		if (n2c_write(o, w, c, 0x00, (s->chg_reg[0] & 0x80u) | v, &s->chg_reg[0])) { return -1; }
		if (n2c_write(o, w, c, 0x0f, (s->chg_reg[0x0f] & 0x80u) | v, &s->chg_reg[0x0f])) { return -1; }
		s->ilim_ma = next;
		if (next > cur) { n2c.step_ms = now_ms; }
	}
	return 0;
}

#endif /* NORD2 */
