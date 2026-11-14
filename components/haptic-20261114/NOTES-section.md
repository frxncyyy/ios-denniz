## 2026-11-14 haptic motor (AW8697) for iOS: 0610 + guest haptic1, image q10jcu (host build only, NOT on the phone) -- gpu/build/haptic-20261114
- Chip/bus [V live DT ios-sweet/dtb/nord2/live.dts; Android dmesg baseband-recon-20261104/dmesg_vendor_debug.log + wifi/rescue-dmesg.txt]:
  Awinic AW8697 (id reg 0x00 = 0x97, "aw8697 detected"), i2c9@11CB1000 addr 0x5a (SCL GPIO120, SDA GPIO121: the bus nord2_slider.inc
  already bit-bangs on the input owner thread), reset GPIO110 (high = run), irq GPIO135 (pull-up, only read), qcom,device_id 815,
  f0_pre 170.0 Hz. The RAM waveform file aw8697_haptic_170.bin is NOT on the phone (Android: "failed to read") nor in the vendor dump
  -> CONT mode only (chip drives the LRA at resonance, closed loop on back-EMF), boost bypassed (never enabled, BSTDBG4 never written).
  Registers/sequences from nord2-current-kernel drivers/misc/aw8697_haptic/aw8697.c v1.3.6 (haptic_init, haptic_cont with the
  AW8697_0815_* constants: DRV_LVL 60, OV 125, TD 0x9a, ZC 0xff1, BRK 3; stop = GO 0, wait GLB_STATE idle, standby).
- iOS contract [V disasm 20H392, haptic-20261114/re; RedwoodAudio.driver source halplug/d/audio_plugin.c actuator_write]: with
  MobileGestalt DeviceSupportsClosedLoopHaptics, iOS (FigVibrator/CoreHaptics/UIKit level 2/keyboard/System Haptics) plays every haptic
  as a waveform on the HAL plug-in's "Actuator" device; the installed RedwoodAudio.driver opens /dev/redwood-haptic once
  (O_WRONLY|O_NONBLOCK) and writes int16 mono 48 kHz each I/O cycle (same contract as HackiOS redwood_haptic.c haptic=5).
  MG keys: DeviceSupportsHaptics slot 0xbd (/product/haptics exists), DeviceSupportsClosedLoopHaptics 0xa7 ("closed-loop"),
  haptics 0x84 ("supports-3rd-party-haptics"), VibratorCapability 0x22e; all cached valid=1 value=0 in the mirror (mgslots.py checked).
  Settings System Haptics = CFPreferences com.apple.preferences.sounds "effects-haptic" (file absent in the mirror).
- 0610 (sweet_touch.c + nord2_haptic.h/.inc + SweetFramebuffer.cpp; patch sha 08dbb74d...): /dev/redwood-haptic 0666 (the write only
  records the envelope); input owner thread: reset pulse, id, init, LRA resistance + VBAT read, one 60 ms boot pulse; then the motor
  runs while iOS's waveform is non-silent (>256/32768), >= 12 ms per onset, drive follows peak (full from 1/3 FS, 5/8 for the softest,
  HackiOS gain_for), stops on silence / no write for 25 ms / last close. Limits: DRV_LVL <= 60 (Android), one run <= kern.n2hap_max_ms
  1000 then a rest as long, on-time bucket 8 s refilled at 50 %, battery >= 43 C half drive, >= 46 C off (< 44 C back), watchdog stop 50 ms
  past the limit, stop not idle in 100 ms -> soft reset + re-init, 3 I2C failures or a chip fault (SYSST boost/OV/OC/OT) -> reset pin low
  (motor off without I2C) for the boot. SweetFramebuffer: IODeviceTree:/product/haptics {closed-loop, supports-3rd-party-haptics}.
  Kill: boot-arg n2hap=0 or n2gdv +1024 (n2gdv=1033 fits: 3 chars) = no node, no DT node, chip and pins untouched; sysctl kern.n2hap /
  n2hap_drvmax / n2hap_max_ms / n2hap_test / n2hapstat. No ml_io_map added (GPIO page = ni_gpio).
- Host: test/run.sh HAPTIC_HOST_TESTS_OK (simulated AW8697: register end states, allow-list, ceilings, boost never set, NACK step;
  stream scenarios; 3 x 200000-tick random runs: duty <= 50 % + bucket, run cap). sweet_touch.c -Werror clean; SweetFramebuffer.cpp
  1438 warnings = q10jcp, same messages.
- Image q10jcu = q10jcq recipe + 0610 (MORE_PATCHES '... 0540 0550 0551 0610', MORE_DIRS + $B/haptic-20261114, EXTRA_ARGS n2gdv=9):
  device-mmu-q10jcu-candidate/ui-sched/nord2-ios-metal-q10jcu-ui654fb60dvfs-cap3500-ram5088-sched.img 15155200 B
  sha256 47d4ac30264073bbd08d2e72e5718b78c63f982e35f5853475f7f917f7581181; KERNEL OK, FAILURES none, known 0399 IMAGECHECK stop,
  WIFI_MEMBERS_OK, SCANPATH_OK after adding the 11 _n2h*/_n2hd* functions to SCANPATH_EXTRA_ALLOWED (scanpath_rerun.log); ADT == q10jcq.
- Guest haptic1 (block-haptic1.py: the 4 MG cache bytes 0 -> 1, receipt haptic1.json; block-hapticpref.py: effects-haptic = true only
  if absent, receipt hapticpref.json); owned-haptic1.json; txn_haptic.sh KERNEL_IMG TAG (UNDO=1 for the undo). The kernel alone gives
  nothing visible in iOS (cached "no"); the payload alone with an old kernel: iOS plays to a missing node ("not felt").
- NOT verified: anything on hardware.
- Rehearsals (VM on the mirror of 01:19, root 782b0ccb...): offline haptic1 HAPTIC1_INSTALL (4 slots 0 -> 1) + HAPTICPREF_INSTALL (file created),
  catalog check changed 4, outside_owned_paths []; round trip RT_INSTALL_MG other bytes unchanged True, RT_SECOND_INSTALL_NOOP True,
  RT_UNDO_DIFF 0, RT_SECOND_UNDO_NOOP True, byte_identical_to_before True.
- Log lines to read after the flash (USB console): `NORD2 HAPTIC: /dev/redwood-haptic made`, `NORD2 HAPTIC CAP: ... made, closed-loop = 1`,
  `NORD2 HAPTIC: pins as found ...`, `NORD2 HAPTIC: AW8697 id 0x97 configured ... LRA x ohm, VBAT y mV` (LRA 4..60 ohm = motor wired),
  boot pulse `on (raw), drive 49 ...` / `stop (end of pattern) after ~60 ms` / `standby N ms after the stop; tracked f0 ~165-175 Hz`
  (a plausible f0 = the chip saw back-EMF zero crossings = the motor moved), then with haptic1 applied: RedwoodAudio's
  `redwood: audio: the actuator plays: ...` (no "not felt"), `NORD2 HAPTIC: first waveform from iOS's Actuator`, `on (iOS)` lines and
  `NORD2 HAPTIC SUM:` each minute (writes/onsets/starts/stops; io_fails, watchdog, stop_timeouts, resets, faults should be 0).
  Failure lines: `no AW8697 at I2C9 0x5a`, `init step N failed`, `held in reset`, `still busy`, `watchdog`.
