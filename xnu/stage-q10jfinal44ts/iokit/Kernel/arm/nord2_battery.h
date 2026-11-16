#ifndef NORD2_BATTERY_H
#define NORD2_BATTERY_H
#include <stdint.h>
/* Sample-only transport: SMBus command pointer+read, no data writes/control,
 * MAC, dataflash, unseal, learning, charger enable, GPIO or PMIF operations.
 * Caller serializes against controller users/VOOC gauge master and proves
 * the selected pre-VOOC default policy, correct bus/pinmux/clock ownership.
 * If a VOOC protocol driver is later added it must supply its real dynamic
 * allow_reading gate; this observer implements no fastcharge negotiation.
 * (0702 adds the only writes, in nord2_charger.h, through a separate entry point.) */
struct nbg_ops {
 int (*owned)(void *);
 int (*read)(void *,unsigned bus,unsigned slave,unsigned command,unsigned bytes,uint16_t *);
 int (*delay_ms)(void *,unsigned);
};
/* current_register_identified must prove BQ28Z610/ZY0603 source identity;
 * no arbitrary device-ID constants or automatic control writes here.
 * 0701: the Nord 2 (20827) DT declares the gauge `qcom,batt_bq28z610` (cust_mt6893_oplus_charge_20827.dtsi
 * bq27541-battery@55) and Oplus selects Current = 0x0c for it (oplus_bq27541.c gauge_set_device_type), so the platform
 * passes identified = 1.  AverageCurrent 0x14, RemainingCapacity 0x10, FullChargeCapacity 0x12 and BatteryStatus 0x0a are
 * standard commands of both bq27541 and bq28z610. */
#define NBG_CHG_REGS  0x16u    /* MP2650 REG00..REG15 (1 byte each) */
#define NBG_CHG_WORDS 6u       /* MP2650 ADC words 0x16 VBAT, 0x18 VSYS, 0x1a ICHG, 0x1c VIN, 0x1e IIN, 0x28 IDIS */
struct nbg_sample {
 uint16_t raw_soc[2],raw_voltage,raw_temperature,raw_current,raw_status[2],raw_fault;
 unsigned raw_valid; /*bits0/1SOC1/2,2voltage,3temp,4current,5status0,6fault,7status1,8avgcur,9rm,10fcc,11flags*/
 unsigned soc_valid,voltage_valid,temperature_valid,current_valid,charger_valid;
 unsigned percent,pack_mv;int temperature_centi_c,current_ma;
 unsigned external,charging,full;int error;
 /* 0701 */
 uint16_t raw_avg_current,raw_rm,raw_fcc,raw_flags;
 unsigned avg_valid,capacity_valid;int avg_current_ma;unsigned rm_mah,fcc_mah;
 uint8_t chg_reg[NBG_CHG_REGS];unsigned chg_regs_valid;      /* bit n = REGn read */
 uint16_t chg_word[NBG_CHG_WORDS];unsigned chg_words_valid;
 unsigned vin_mv,iin_ma,ichg_adc_ma,vbat_chg_mv,idis_ma;
 unsigned power_good,chg_state,ppm,aicl,ntc_fault,chg_fault,bat_ovp,chg_en,learn,otg,wdt;
 unsigned ilim_ma,ichg_set_ma,vindpm_mv,vfloat_cell_mv;
};
int nbg_collect(const struct nbg_ops *,void *,unsigned enabled,unsigned current_register_identified,struct nbg_sample *);
/* 0702: MP2650 charge policy, called with the bus owned after nbg_collect (nord2_battery.c). */
struct nbg_wops { int (*write8)(void *,unsigned bus,unsigned slave,unsigned reg,unsigned value); };
int nbg_charger_control(const struct nbg_ops *,const struct nbg_wops *,void *,struct nbg_sample *,
 unsigned host_events,unsigned configured,unsigned console_up,uint64_t now_ms);
#endif
