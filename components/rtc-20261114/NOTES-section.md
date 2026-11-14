
## 2026-11-14 MT6359P hardware RTC as the time of day: 0540 + image q10jcn (host build only, NOT on the phone) -- gpu/build/rtc-20261114
- Reference ios-redfin xnu/patches/0099-flame-rtc.patch: SweetPlatformExpert overrides getGMTTimeOfDay/setGMTTimeOfDay and publishes "IORTC";
  the PM8150 RTC is read-only there (offset from boot-arg sweetrtcoff), setGMTTimeOfDay only logs. Taken: the idea + carry-safe read. Not
  possible here: SweetPlatformExpert.o is a prebuilt CI member nothing recompiles, so the hooks live in sweet_touch.o + the 0530 exec hook.
- Registers [V-source nord2-current-kernel drivers/rtc/rtc-mt6358.c, the driver mt6893_defconfig builds; mt6359p.dtsi base 0x580; mt6358-misc.c]:
  BBPU 0x588 (bit6 CBUSY), TC_SEC..YEA 0x592..0x59e (masks 3f 3f 1f 1f 7 f 7f, year = 1968 + field), PDN1 0x5b4 (bit13 fast boot),
  PDN2 0x5b6, PROT 0x5bc (0x586a/0x9136 unlock, 0 lock, each + trigger), WRTGR 0x5c2. [V-hw hf1 log] "NORD2 AUTOFB: RTC_PDN1 0000 -> 2000":
  the write interface is open at boot (bootloader), so writes are plain; unlock only if a readback proves the write was ignored, then relock.
- 0540 (sha e2f76706...): nord2_rtc.h (model/allow-list/sequences/policy, host-tested), nord2_rtc.inc (owner thread ni_thread: ni_pwrap
  mapped before its 2 s wait, RTC read first; nord2_rtc_boot_time() handshake with the first exec, 3 s max wait; late valid read applied by
  the owner thread; n2rtc_service every 15 s: write calendar -> RTC on step (NTP), invalid RTC, or >= 3 s drift at the hourly re-read; 1 write
  per minute max, 64/boot, 3 failures -> off; second readback 15 s later), kern_exec.c/nord2_clockfloor.h: calendar = max(RTC, 0530 floor).
  Writes only TC (RMW, DOW kept), WRTGR=1, PROT keys; never BBPU/IRQ/alarm/OSC32CON/POWERKEY/PDN/SPAR. Kill: n2rtc=0 or n2gdv +256 (n2gdv=265);
  n2rtc=2 read only; sysctl kern.n2rtc 0/1/2, kern.n2rtcstat. No ml_io_map added (net 0: the ni_pwrap line moved).
- Host: test/run.sh RTC_HOST_TESTS_OK (2 M conversions vs libc, simulated MT6359 RTC: carry, open/closed PROT, CBUSY stuck, allow-list,
  PDN/SPAR/alarm untouched, policy, 3-boot scenario) + 0530 CLOCKFLOOR_TEST_OK; both objects -Werror clean, kern_exec views identical (17);
  merge_check.sh MERGE_CHECK_OK (0540 regenerated identical after 0491..0531).
- q10jcn = q10jcm recipe + 0540: device-mmu-q10jcn-candidate/ui-sched/nord2-ios-metal-q10jcn-ui654fb60dvfs-cap3500-ram5088-sched.img
  15142912 B sha256 c94eb826343d60e804523304a72620208ee0d1907e9edaf1db8888bda0ddb92e; KERNEL OK, FAILURES none, SCANPATH_OK, WIFI_MEMBERS_OK,
  known 0399 IMAGECHECK stop (BUILD_EXIT 1); stage differs from q10jcm only in the 0540 files; ADT identical, n2gdv=9.
- Flash plan: heat-20261114/txn_heat.sh <q10jcn img> TAG (P=heat1): heat1 (dasd 1970 lastModulation removed) MUST go in the same transaction,
  else the correct clock still walks ~1.99 M slots once from the stored 1970 date. Needs the user's go.
- Next log: "NORD2 RTC: boot read VALID|INVALID", "NORD2 CLOCK: rtc ..." (source 16 = RTC), after NTP "NORD2 RTC: write ok (calendar stepped...)",
  next boot boot read VALID with the right date and no write; no 1970 dates; dasd "modulating for N slots" small; heatcheck.py quiet.
