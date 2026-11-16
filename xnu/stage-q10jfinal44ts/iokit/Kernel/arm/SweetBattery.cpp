/*
 * Built-in iOS battery bring-up service. Default: 100%, AC, not charging.
 * sweetbattery=1: read-only PM6150 polling every 30 seconds; voltage-derived
 * SOC is an UNCALIBRATED estimate, not a replacement for the Linux QG algo.
 * See report.md for binary evidence, register provenance and integration.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <device/device_types.h> /* io_buf_ptr_t for private OSKext headers */
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/pwr_mgt/IOPMPowerSource.h>
#include <libkern/c++/OSCollectionIterator.h>
#if defined(NORD2)
extern "C" void nord2_stop_at(unsigned int point, const char *label);
#endif
#include <pexpert/pexpert.h>
#if defined(NORD2)
#include <kern/clock.h>   /* 0701: mach_absolute_time */
#endif
#if defined(NORD2)
extern "C" {
#include "nord2_battery.h"
int nord2_battery_platform_sample(struct nbg_sample *);
}
#endif
/* bsd/dev/sweet_spmi.c */
extern "C" int sweet_spmi_init(void);
extern "C" int sweet_spmi_read(unsigned sid, unsigned addr, uint8_t *out);

class SweetBattery : public IOPMPowerSource {
	OSDeclareDefaultStructors(SweetBattery);
public:
	bool init(OSDictionary *dictionary = NULL) APPLE_KEXT_OVERRIDE;
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
	void free(void) APPLE_KEXT_OVERRIDE;
private:
	IOWorkLoop *fLoop;
	IOTimerEventSource *fTimer;
	bool fTimerAdded, fRunning, fReal;
	int fLastMode;
	void putNumber(const char *key, UInt32 value);
	void putBool(const char *key, bool value);
	void publish(unsigned percent, unsigned mv, int ma, bool external,
	    bool capable, bool charging, bool full, bool measured, bool currentValid);
	void poll(void);
#if defined(NORD2)
	void pollNord2(void);
	unsigned tickNord2(void);   /* 0703 */
#endif
	static void timerFired(OSObject *owner, IOTimerEventSource *sender);
	static IOReturn stopPolling(OSObject *owner, void *, void *, void *, void *);
	static bool readWord(unsigned address, uint16_t *value);
	static unsigned estimatePercent(unsigned mv);
};
OSDefineMetaClassAndStructors(SweetBattery, IOPMPowerSource);

bool
SweetBattery::init(OSDictionary *dictionary)
{
	/* IOCatalogue calls init(dictionary). This XNU's IOPMPowerSource only
	 * overrides init(void): explicitly run it to allocate its PS dictionary
	 * and symbols, then copy the matching personality into the registry. */
	if (!IOPMPowerSource::init()) {
		return false;
	}
	fLoop = NULL;
	fTimer = NULL;
	fTimerAdded = fRunning = fReal = false;
	if (dictionary) {
		OSCollectionIterator *it = OSCollectionIterator::withCollection(dictionary);
		if (!it) {
			return false;
		}
		OSObject *object;
		bool ok = true;
		while ((object = it->getNextObject())) {
			const OSSymbol *key = OSDynamicCast(OSSymbol, object);
			if (!key || !setProperty(key, dictionary->getObject(key))) {
				ok = false;
				break;
			}
		}
		it->release();
		if (!ok) {
			return false;
		}
	}
	fLastMode = -1;
	return true;
}

void
SweetBattery::putNumber(const char *key, UInt32 value)
{
	const OSSymbol *symbol = OSSymbol::withCString(key);
	OSNumber *number = OSNumber::withNumber(value, 32);
	if (symbol && number) {
		setPSProperty(symbol, number);
	}
	OSSafeReleaseNULL(number);
	OSSafeReleaseNULL(symbol);
}

void
SweetBattery::putBool(const char *key, bool value)
{
	const OSSymbol *symbol = OSSymbol::withCString(key);
	if (symbol) {
		setPSProperty(symbol, value ? kOSBooleanTrue : kOSBooleanFalse);
		symbol->release();
	}
}

void
SweetBattery::publish(unsigned percent, unsigned mv, int ma, bool external,
    bool capable, bool charging, bool full, bool measured, bool currentValid)
{
	/* Use consistent mAh units (DT nominal=5020). powerd computes C*100/M. */
	const unsigned capacity = (5020u * percent + 50u) / 100u;
	setBatteryInstalled(true);
	setCurrentCapacity(capacity);
	setMaxCapacity(5020);
	setExternalConnected(external);
	setExternalChargeCapable(capable);
	setIsCharging(charging);
	setAtWarnLevel(false);
	/* An uncalibrated voltage curve must not assert a critical shutdown. */
	setAtCriticalLevel(false);
	setVoltage(mv);
	setAmperage(ma);
	setTimeRemaining(-1); /* Unknown; never publish zero minutes on error. */
	setCycleCount(0);     /* Placeholder: cycle learning is not implemented. */
	putBool("FullyCharged", full);
	putBool("AppleRawExternalConnected", external);
	putNumber("AppleRawCurrentCapacity", capacity);
	putNumber("AppleRawMaxCapacity", 5020);
	putNumber("DesignCapacity", 5020);
	putNumber("InstantAmperage", (UInt32)ma);
	/* iOS battery convention: centidegrees C; fixed 25 C, NOT measured. */
	putNumber("Temperature", 2500);
	putBool("SweetBatteryHardwareSampleValid", measured);
	putBool("SweetBatteryCurrentValid", currentValid);
	putBool("SweetBatterySOCEstimated", measured);
	putBool("SweetBatteryTemperatureEstimated", true);
	putBool("SweetBatteryFallback", !measured);
	putNumber("SweetBatteryPercent", percent);

	/* IOPMCopyBatteryInfo's per-source compatibility dictionary. */
	OSDictionary *legacy = OSDictionary::withCapacity(7);
	if (legacy) {
		const char *keys[] = {kIOBatteryCurrentChargeKey, kIOBatteryCapacityKey,
		    kIOBatteryVoltageKey, kIOBatteryAmperageKey, kIOBatteryCycleCountKey,
		    kIOBatteryFlagsKey};
		UInt32 values[] = {capacity, 5020, mv, (UInt32)ma, 0,
		    (UInt32)(kIOBatteryInstalled | (external ? kIOBatteryChargerConnect : 0) |
		    (charging ? kIOBatteryCharge : 0))};
		for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
			OSNumber *number = OSNumber::withNumber(values[i], 32);
			if (number) {
				legacy->setObject(keys[i], number);
				number->release();
			}
		}
		setLegacyIOBatteryInfo(legacy);
		legacy->release();
	}
	updateStatus(); /* Flush PS properties and send 0xe0024100 on change. */
	int mode = measured ? 1 : 0;
	if (mode != fLastMode) {
		IOLog("SWEET BATTERY: %s, %u%% %umV external=%d charging=%d\n",
		    measured ? "PM6150 / estimated SOC" : "static fallback",
		    percent, mv, external, charging);
		fLastMode = mode;
	}
}

/* Keep the proven single-byte observer command. Read two matching whole
 * words to reject most torn ADC samples without writing the QG HOLD bit.
 * This is a bounded consistency check, not a hardware snapshot guarantee. */
bool
SweetBattery::readWord(unsigned address, uint16_t *value)
{
	uint16_t previous = 0;
	for (unsigned attempt = 0; attempt < 4; ++attempt) {
		uint8_t lo, hi;
		if (sweet_spmi_read(0, address, &lo) ||
		    sweet_spmi_read(0, address + 1, &hi)) {
			return false;
		}
		uint16_t next = (uint16_t)((unsigned)lo | ((unsigned)hi << 8));
		if (attempt && next == previous) {
			*value = next;
			return true;
		}
		previous = next;
	}
	return false;
}

unsigned
SweetBattery::estimatePercent(unsigned mv)
{
	/* Generic single-cell curve, intentionally NOT advertised as QG SOC.
	 * Load sag, temperature, aging and charging introduce large errors. */
	static const unsigned volts[] = {3400, 3500, 3600, 3700, 3800, 3900,
	    4000, 4100, 4200, 4350};
	static const unsigned soc[] = {1, 5, 10, 20, 40, 60, 75, 85, 93, 100};
	if (mv <= volts[0]) {
		return soc[0];
	}
	for (unsigned i = 1; i < sizeof(volts) / sizeof(volts[0]); ++i) {
		if (mv < volts[i]) {
			return soc[i - 1] + (mv - volts[i - 1]) * (soc[i] - soc[i - 1]) /
			       (volts[i] - volts[i - 1]);
		}
	}
	return 100;
}

#if defined(NORD2)
/*
 * 0701 (usbchg-20261115): what iOS sees.
 *  - external / charging follow the MP2650 status with the corrected VIN power-good polarity (nord2_battery.c);
 *  - a failed sample no longer publishes 0 % / MaxCapacity 0: the last valid sample is held for kern.n2bat_hold_s (120 s), marked
 *    Nord2BatterySOCStale, and only then the source is invalidated (that 0/0 blink was one cause of the "odd" percentage);
 *  - displayed SOC: the gauge RSOC (0x2c), with a one-step direction filter (kern.n2bat_filter): a +-1 % flicker against the sign
 *    of the measured current is not shown (raw SOC stays in Nord2BatteryRawSOC and the log);
 *  - iPhone-style keys: CurrentCapacity/MaxCapacity in percent, AppleRawCurrentCapacity/AppleRawMaxCapacity/NominalChargeCapacity in
 *    mAh from the gauge (RM/FCC), Amperage = AverageCurrent, InstantAmperage = Current, time to empty/full, AdapterDetails;
 *  - poll every kern.n2bat_poll_ms (10 s, was 30 s); one NORD2 BAT14 line per change or every kern.n2bat_log_s.
 *  kern.n2bat_legacy=1 = the pre-0701 publishing (kill switch; the polarity fix stays).
 */
extern "C" {
extern unsigned nord2_bat_poll_ms, nord2_bat_hold_s, nord2_bat_filter, nord2_bat_log_s, nord2_bat_legacy;
extern unsigned nord2_bat_fast_ms;                 /* 0703 */
int nord2_battery_platform_pg(unsigned *pg);       /* 0703 nord2_battery_platform.cpp: MP2650 REG13 power-good only */
extern unsigned nord2_chg_policy_type(void);      /* 0702 (0 = no charger control in this kernel), nord2_battery.c */
extern const char *nord2_chg_policy_name(void);
}
static struct nbg_sample n2b_last;
static uint64_t n2b_last_ms, n2b_log_ms;
static unsigned n2b_shown = 101, n2b_last_pg = 9, n2b_last_state = 9, n2b_last_logged = 101, n2b_fail;
/* 0703: plug/unplug visible to iOS within ~0.5 s.  Before 0703 the VBUS state reached iOS only with the next full sample
 * (kern.n2bat_poll_ms = 10 s: 0..10 s, ~5 s average, the ~8 s seen on the phone).  Now the timer ticks every kern.n2bat_fast_ms
 * (500 ms; 0 = the 0701 behaviour) with ONE cheap read (MP2650 REG13 twice, power-good bit); on a change it runs the full sample
 * at once (gauge + charger + 0702 policy + IOPMPowerSource publish) and two follow-ups +1 s / +3 s (the MP2650 enters
 * pre/fast charge a moment after VIN good).  The full 10 s sample, the hold/stale logic and the 0702 ramp (now >= 5 s per step)
 * are unchanged.  Log: NORD2 BAT15 lines. */
static uint64_t n2b_next_full, n2b_edge_ms, n2b_fast_ms_at;
static unsigned n2b_pub_pg = 2, n2b_fast_pg = 2, n2b_followups, n2b_edges, n2b_fast_fail;
static uint64_t
n2b_now_ms(void)
{
 uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000000ull;
}
void
SweetBattery::pollNord2(void)
{
 struct nbg_sample s = {};
 int rc = fReal ? nord2_battery_platform_sample(&s) : -1;
 if(rc != 0){s = (struct nbg_sample){};}
 bool valid = rc == 0 && s.soc_valid && s.percent <= 100;
 if(nord2_bat_legacy){
  setBatteryInstalled(true);
  setCurrentCapacity(valid ? s.percent : 0);
  setMaxCapacity(valid ? 100 : 0);
  setTimeRemaining(-1);
  setExternalConnected(s.charger_valid && s.external);
  setExternalChargeCapable(s.charger_valid && s.external);
  setIsCharging(s.charger_valid && s.charging);
  setVoltage(s.voltage_valid ? s.pack_mv : 0);
  setAmperage(s.current_valid ? s.current_ma : 0);
  setAtWarnLevel(false); setAtCriticalLevel(false);
  putBool("FullyCharged", s.charger_valid && s.full);
  putBool("Nord2BatterySOCValid", valid);
  putBool("Nord2BatteryTemperatureValid", s.temperature_valid);
  putNumber("Temperature", s.temperature_valid ? (UInt32)s.temperature_centi_c : 0);
  const OSSymbol *e0 = OSSymbol::withCString(valid ? "" : "Battery sample unavailable");
  if(e0){setErrorCondition((OSSymbol *)e0);e0->release();}
  updateStatus();
  return;
 }
 uint64_t now = n2b_now_ms();
 bool stale = false;
 if(valid){
  /* ancillary fields missing in this sample: keep the previous ones (never fabricate on a first sample) */
  if(!s.temperature_valid && n2b_last.temperature_valid){s.temperature_valid = 1; s.temperature_centi_c = n2b_last.temperature_centi_c;}
  if(!s.voltage_valid && n2b_last.voltage_valid){s.voltage_valid = 1; s.pack_mv = n2b_last.pack_mv;}
  n2b_last = s; n2b_last_ms = now; n2b_fail = 0;
 } else if(n2b_last.soc_valid && now - n2b_last_ms <= (uint64_t)nord2_bat_hold_s * 1000ull){
  n2b_fail++; s = n2b_last; valid = true; stale = true;
  /* 0703: a fresh fast VBUS read beats the held sample for the plug state */
  if(n2b_fast_pg < 2 && now - n2b_fast_ms_at <= 2000){
   s.power_good = n2b_fast_pg; s.external = n2b_fast_pg; if(!n2b_fast_pg){s.charging = 0; s.full = 0;}
  }
 } else {
  n2b_fail++;
 }
 int amps = s.avg_valid ? s.avg_current_ma : (s.current_valid ? s.current_ma : 0);
 bool ampsValid = s.avg_valid || s.current_valid;
 unsigned shown = valid ? s.percent : 0;
 if(valid && nord2_bat_filter && n2b_shown <= 100 && ampsValid){
  /* one-step flicker against the current direction is not shown */
  if(shown == n2b_shown + 1 && amps < -50) shown = n2b_shown;
  else if(shown + 1 == n2b_shown && amps > 50) shown = n2b_shown;
 }
 if(valid) n2b_shown = shown;
 bool ext = s.charger_valid && s.external;
 if(s.charger_valid) n2b_pub_pg = s.power_good ? 1u : 0u;   /* 0703: what iOS was told */
 bool full = ext && s.full;
 bool chg = ext && s.charging && !full;
 unsigned fcc = s.capacity_valid ? s.fcc_mah : 4500u;
 unsigned rm = s.capacity_valid ? s.rm_mah : (4500u * shown + 50u) / 100u;
 int minutes = -1;
 if(valid && ampsValid && s.capacity_valid){
  if(amps < -30) minutes = (int)((uint64_t)rm * 60u / (unsigned)(-amps));
  else if(chg && amps > 30 && fcc > rm) minutes = (int)((uint64_t)(fcc - rm) * 60u / (unsigned)amps);
 }
 setBatteryInstalled(true);
 setCurrentCapacity(valid ? shown : 0);
 setMaxCapacity(valid ? 100 : 0);
 setTimeRemaining(minutes);
 setExternalConnected(ext);
 setExternalChargeCapable(ext);
 setIsCharging(chg);
 setVoltage(s.voltage_valid ? s.pack_mv : 0);
 setAmperage(ampsValid ? amps : 0);
 setAtWarnLevel(false);
 setAtCriticalLevel(false);
 putBool("FullyCharged", full);
 putBool("AppleRawExternalConnected", ext);
 putNumber("AppleRawCurrentCapacity", valid ? rm : 0);
 putNumber("AppleRawMaxCapacity", valid ? fcc : 0);
 putNumber("NominalChargeCapacity", fcc);
 putNumber("DesignCapacity", 4500);
 putNumber("AppleRawBatteryVoltage", s.voltage_valid ? s.pack_mv : 0);
 putNumber("InstantAmperage", (UInt32)(s.current_valid ? s.current_ma : amps));
 putNumber("AvgTimeToEmpty", (!chg && minutes > 0) ? (UInt32)minutes : 65535u);
 putNumber("AvgTimeToFull", (chg && minutes > 0) ? (UInt32)minutes : 65535u);
 putNumber("Nord2BatteryRawSOC", valid ? s.percent : 0);
 putBool("Nord2BatterySOCValid", valid);
 putBool("Nord2BatteryGaugeComputedSOC", valid);
 putBool("Nord2BatterySOCStale", !valid || stale);
 putBool("Nord2BatteryChargerValid", s.charger_valid);
 putBool("Nord2BatteryCurrentValid", ampsValid);
 putBool("Nord2BatteryVoltageValid", s.voltage_valid);
 putBool("Nord2BatteryTemperatureValid", s.temperature_valid && !stale);
 putNumber("Temperature", s.temperature_valid ? (UInt32)s.temperature_centi_c : 0);
 putBool("SweetBatterySOCEstimated", false);
 putBool("SweetBatteryTemperatureEstimated", false);
 putBool("SweetBatteryFallback", false);
 if(ext){
  OSDictionary *ad = OSDictionary::withCapacity(4);
  if(ad){
   unsigned vin = s.vin_mv ? s.vin_mv : 5000u, ilim = s.ilim_ma ? s.ilim_ma : 500u;
   OSNumber *w = OSNumber::withNumber((unsigned long long)((vin * ilim + 500000u) / 1000000u), 32);
   OSNumber *cu = OSNumber::withNumber((unsigned long long)ilim, 32);
   OSNumber *vo = OSNumber::withNumber((unsigned long long)vin, 32);
   OSString *de = OSString::withCString(nord2_chg_policy_type() ? nord2_chg_policy_name() : "usb");
   if(w){ad->setObject("Watts", w); w->release();}
   if(cu){ad->setObject("Current", cu); cu->release();}
   if(vo){ad->setObject("Voltage", vo); vo->release();}
   if(de){ad->setObject("Description", de); de->release();}
   const OSSymbol *k = OSSymbol::withCString("AdapterDetails");
   if(k){setPSProperty(k, ad); k->release();}
   ad->release();
  }
 } else {
  const OSSymbol *k = OSSymbol::withCString("AdapterDetails");
  OSDictionary *none = OSDictionary::withCapacity(1);
  if(k && none){setPSProperty(k, none);}
  OSSafeReleaseNULL(none); OSSafeReleaseNULL(k);
 }
 const OSSymbol *error = OSSymbol::withCString(valid ? "" : "Battery sample unavailable");
 if(error){setErrorCondition((OSSymbol *)error);error->release();}
 updateStatus();
 int mode = valid ? 2 : 3;
 if(mode != fLastMode){IOLog("NORD2 BATTERY: soc_valid=%d percent=%u charger_valid=%u external=%u charging=%u\n",valid,valid?s.percent:0,s.charger_valid,s.external,s.charging);fLastMode=mode;}
 bool change = shown != n2b_last_logged || s.power_good != n2b_last_pg || s.chg_state != n2b_last_state || stale;
 if(change || now - n2b_log_ms >= (uint64_t)nord2_bat_log_s * 1000ull){
  IOLog("NORD2 BAT14: t=%llus shown=%u raw=%u rm=%u fcc=%u mv=%u cur=%d avg=%d temp=%d.%02dC | chg valid=%u pg=%u state=%u ppm=%u aicl=%u"
   " ntc=%u fault=%u vin=%umV iin=%umA ichg_adc=%umA ilim=%u ichg_set=%u en=%u hiz=%u | ios ext=%u charging=%u full=%u mins=%d stale=%u fails=%u policy=%s\n",
   now / 1000ull, shown, valid ? s.percent : 0, s.rm_mah, s.fcc_mah, s.pack_mv, s.current_valid ? s.current_ma : 0, s.avg_valid ? s.avg_current_ma : 0,
   s.temperature_centi_c / 100, (s.temperature_centi_c < 0 ? -s.temperature_centi_c : s.temperature_centi_c) % 100,
   s.charger_valid, s.power_good, s.chg_state, s.ppm, s.aicl, s.ntc_fault, s.chg_fault, s.vin_mv, s.iin_ma, s.ichg_adc_ma, s.ilim_ma, s.ichg_set_ma,
   s.chg_en, s.learn, ext, chg, full, minutes, stale, n2b_fail, nord2_chg_policy_type() ? nord2_chg_policy_name() : "off");
  n2b_log_ms = now; n2b_last_logged = shown; n2b_last_pg = s.power_good; n2b_last_state = s.chg_state;
 }
}
unsigned
SweetBattery::tickNord2(void)
{
 unsigned full = nord2_bat_poll_ms < 2000 ? 2000 : nord2_bat_poll_ms > 60000 ? 60000 : nord2_bat_poll_ms;
 unsigned fast = nord2_bat_fast_ms;
 if(!fast || nord2_bat_legacy){ poll(); return full; }
 fast = fast < 100 ? 100 : fast > 2000 ? 2000 : fast;
 uint64_t now = n2b_now_ms();
 bool doFull = now >= n2b_next_full;
 if(!doFull){
  unsigned pg = 2;
  if(fReal && nord2_battery_platform_pg(&pg) == 0){
   n2b_fast_pg = pg; n2b_fast_ms_at = now;
   if(pg != n2b_pub_pg && n2b_pub_pg < 2 && now - n2b_edge_ms >= 1000){
    n2b_edges++; n2b_edge_ms = now; n2b_followups = 2; doFull = true;
    IOLog("NORD2 BAT15: VBUS %s at t=%llums (fast check, edge %u) -> immediate refresh\n", pg ? "ON" : "OFF", now, n2b_edges);
   }
  } else {
   n2b_fast_fail++;
  }
 }
 if(!doFull && n2b_followups && now - n2b_edge_ms >= (n2b_followups == 2 ? 1000ull : 3000ull)){ doFull = true; n2b_followups--; }
 if(doFull){
  poll();
  n2b_next_full = n2b_now_ms() + full;
  if(n2b_edge_ms && n2b_now_ms() - n2b_edge_ms < 4000)
   IOLog("NORD2 BAT15: published pg=%u %llums after the edge (followups left %u, fast fails %u)\n", n2b_pub_pg, n2b_now_ms() - n2b_edge_ms, n2b_followups, n2b_fast_fail);
 }
 return fast;
}
#endif

void
SweetBattery::poll(void)
{
#if defined(NORD2)
 pollNord2();return;
#endif
	uint8_t type, present, usb, path, charger, error, temperature;
	uint16_t rawVoltage;
	bool valid = fReal &&
	    !sweet_spmi_read(0, 0x4804, &type) && type == 0x0d &&
	    !sweet_spmi_read(0, 0x4808, &present) && (present & 1u) &&
	    readWord(0x48c0, &rawVoltage) && rawVoltage != 0x8000 &&
	    !sweet_spmi_read(0, 0x1310, &usb) &&
	    !sweet_spmi_read(0, 0x110b, &path) &&
	    !sweet_spmi_read(0, 0x1006, &charger) &&
	    !sweet_spmi_read(0, 0x1007, &error) &&
	    !sweet_spmi_read(0, 0x100d, &temperature);
	unsigned mv = valid ? (unsigned)(((uint64_t)rawVoltage * 194637u) / 1000000u) : 0;
	/* Zero, reset, floating, or implausible data cannot become 0% battery. */
	valid = valid && mv >= 2800 && mv <= 4600;
	if (!valid) {
		publish(100, 4200, 0, true, true, false, true, false, false);
		return;
	}
	bool external = (usb & 0x10u) != 0;
	bool capable = external && (path & 0x10u) && (path & 1u) && !(path & 0x40u);
	unsigned state = charger & 7u;
	bool charging = capable && state >= 1 && state <= 4 &&
	    !(error & 2u) && !(temperature & 0x0cu);
	/* Inhibit (0) does not by itself establish full capacity. */
	bool full = external && state == 5 && !(error & 2u);
	int ma = 0;
	uint8_t subtype;
	uint16_t rawCurrent;
	bool currentValid = !sweet_spmi_read(0, 0x4805, &subtype) &&
	    (subtype == 3 || subtype == 4) && readWord(0x48c2, &rawCurrent) &&
	    rawCurrent != 0x8000;
	if (currentValid) {
		int signedRaw = (rawCurrent & 0x8000u) ? (int)rawCurrent - 65536 : (int)rawCurrent;
		/* Linux: negative=charging; IOPMPowerSource: positive=charging, mA. */
		ma = (int)(-((int64_t)signedRaw * (subtype == 3 ? 152588 : 305176)) / 1000000);
	}
	publish(full ? 100 : estimatePercent(mv), mv, ma, external, capable,
	    charging, full, true, currentValid);
}

void
SweetBattery::timerFired(OSObject *owner, IOTimerEventSource *sender)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (self && self->fRunning) {
#if defined(NORD2)
		sender->setTimeoutMS(self->tickNord2());   /* 0703: fast VBUS check + full sample every kern.n2bat_poll_ms */
#else
		self->poll();
		sender->setTimeoutMS(30000);
#endif
	}
}

bool
SweetBattery::start(IOService *provider)
{
	if (!IOPMPowerSource::start(provider)) {
		return false;
	}
	int real = 0;
	(void)PE_parse_boot_argn("sweetbattery", &real, sizeof(real));
	#if defined(NORD2)
 real=0;(void)PE_parse_boot_argn("nord2battery", &real, sizeof(real));
 fReal = real == 1; /*Only read-only Nord2 provider; never Qualcomm SPMI.*/
#else
 fReal = real == 1 && sweet_spmi_init() == 0;
#endif
	/* Publish a complete source BEFORE registerService wakes powerd. */
	poll();
	if (fReal) {
		fLoop = IOWorkLoop::workLoop();
		fTimer = IOTimerEventSource::timerEventSource(this, &SweetBattery::timerFired);
		if (fLoop && fTimer && fLoop->addEventSource(fTimer) == kIOReturnSuccess) {
			fTimerAdded = true;
			fRunning = true;
#if defined(NORD2)
			fTimer->setTimeoutMS(5000);   /* 0701: first real sample 5 s after start */
#else
			fTimer->setTimeoutMS(30000);
#endif
		} else {
			fReal = false;
			poll();
			IOLog("SWEET BATTERY: timer unavailable; static fallback\n");
		}
	}
#if defined(NORD2)
	/* CI-only instrumentation (Apple clang): stop before/after the IOKit
	 * publish so the post-battery hang can be localised. */
	nord2_stop_at(2, "before SweetBattery registerService");
#endif
	registerService();
#if defined(NORD2)
	nord2_stop_at(3, "after SweetBattery registerService");
#endif
	return true;
}

void
SweetBattery::stop(IOService *provider)
{
	if (fLoop && fTimerAdded) {
		/* Serialize against a running timer callback, including rearming. */
		(void)fLoop->runAction(&SweetBattery::stopPolling, this);
		fLoop->removeEventSource(fTimer);
		fTimerAdded = false;
	}
	IOPMPowerSource::stop(provider);
}

IOReturn
SweetBattery::stopPolling(OSObject *owner, void *, void *, void *, void *)
{
	SweetBattery *self = OSDynamicCast(SweetBattery, owner);
	if (!self) {
		return kIOReturnBadArgument;
	}
	self->fRunning = false;
	self->fTimer->cancelTimeout();
	self->fTimer->disable();
	return kIOReturnSuccess;
}

void
SweetBattery::free(void)
{
	OSSafeReleaseNULL(fTimer);
	OSSafeReleaseNULL(fLoop);
	IOPMPowerSource::free();
}
#endif /* ARM64_BOARD_CONFIG_SWEET */
