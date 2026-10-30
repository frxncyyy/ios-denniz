# Kernel patch 0094 - opt-in MT6893 LVTS thermal sensing (host-only, not flashed; P5 images)

## What the system exposes (checked)
* The Nord2 ADT has **no** thermal node: LVTS is not described. Addresses are fixed MT6893 facts from the Android tree
  (`arch/arm64/boot/dts/mediatek/mt6893.dts`: `therm_ctrl@1100b000` reg 0x1100b000 + 0x26e200, SPI 169/170; `efuse@11c10000`),
  exactly like the GIC/MFG/clock banks the GPU driver already hard-codes. IRQs 169/170 are not used (no interrupt enable is ever written).
* Seven controllers: MCU (CPU) at 0x11278000 + {0, 0x100, 0x200} (LVTS1 big, LVTS2 big, LVTS3 little x4 sensors), AP at 0x1100b000 + {0, 0x100, 0x200, 0x300}
  (LVTS4 VPU/MDLA, **LVTS5 = GPU (T11, T12), controller 4, device id 0x85**, LVTS6 infra/cam, LVTS7 modem). Source: `drivers/misc/mediatek/thermal/mt6893/src/mtk_lvts_tc.c`.
* Access needs the infracfg_ao thermal gate open: `ifrao_therm` = infracfg_ao 0x10001000, CG0 bit 10 (STA 0x090, 1 = gated; CLR 0x084, SET 0x080). The driver reads the gate first and never touches a gated block.
* Temperature = `-252.5 * raw / 2^14 + golden/2 + 252.5` C (raw = MSR[15:0], valid = MSR[16]); EDATA registers get `rc_now * count_r >> 14` (manual RCK, the vendor config for this chip).

## The calibration problem (honest limits)
The vendor driver needs per-chip efuse values (`get_devinfo_with_index` 116-119, 190-207: golden temperature, count_r per sensor, count_rc per controller).
These cannot be obtained offline or from this port today:
* The devinfo array is handed over by the bootloader in `/chosen/atag,devinfo` of the live Android FDT; the Nord2 loader only reads that FDT for memory claims and does not forward it.
* The devinfo index -> MMIO address mapping is not linear (index 72 = 0x11C105D8 and 134 = 0x11C107B0 in the gpufreq sources; the LVTS header comments are placeholders), so a direct read of 0x11C10000 would be a guess.
So the default is the vendor's own "unfused sample" fallback (golden 25 C, count_r 35000, count_rc 2750), every reading is flagged **approximate** (a `~` suffix and `cal=default(approx)` in the log), and the guard applies a 10 C safety margin to it.
`n2lvts_cal_from_efuse()` already implements the vendor decode with plausibility checks (host-tested); the missing piece is a loader/ADT property carrying the 22 words (design below). Accuracy of default-calibrated values is unknown: use them for trends and for
cross-checking against the battery gauge (`bat_cC`), not as absolute truth. `nord2_lvts_golden=<2 x degC>` overrides the golden temperature when a reference is known (e.g. device cold-soaked at a measured ambient).

## Opt-in levels (boot-arg `nord2_lvts`, default 0 = nothing happens)
| level | does | writes |
|---|---|---|
| 1 | reads gate, then CLKEN/MONCTL0/ID/MSR of the selected controllers; reports temperatures only if a controller is already sensing (bootloader/firmware left it running) | none |
| 2 | vendor init of the selected controllers (default GPU only): clock enable, device reset + id check (0x81 + controller), device init, RC count, EDATA, speed/filter, sensing on. Refuses if the gate is closed | that controller's CONFIG/EDATA/TSSEL/CALSCALE/MSRCTL0/MONCTL0-2/CLKEN only |
| 3 | level 2 plus opens the thermal clock gate (CLR 0x084 bit 10) if closed; re-gates on failure/stop | + one infracfg_ao write |
Never written: MONINT/interrupt registers, protection (PROTCTL/PROT*), reset/debug/spare registers, INFRA/SPM state, other controllers. `nord2_lvts_mask=<bits>` selects controllers (bit 4 = GPU, bits 0-2 CPU, 3/5/6 others; default 0x10).
Fail-safe: id mismatch, access timeout, implausible calibration or closed gate abort that controller's init, restore CLKEN/MONCTL0 to what was found, and leave polling readers with `n/a` (-9999) values.
Implausible readings (invalid bit, raw 0, outside -40..150 C) are never reported or used.

## Where it is used
* **Stats line** (0093 format): `gpu_cC=<GPU temp, centi-C or -9999> soc_cC=<hottest valid sensor of the selected controllers> lvts=<state 0 off/1 reading/2 started/3 failed>[~ approximate]`. `bat_cC` stays alongside for cross-checking.
* **Cold-start guard** (`powerOn`, before the board preflight): if a boot-arg OPP above 350 MHz was requested (`nord2_gpu_khz`) and the GPU sensor is readable, a temperature above `nord2_lvts_max_c` (default 65 C; 55 C effective for approximate readings) makes the board use the minimum OPP instead, with a log line.
  An unreadable sensor never blocks (nothing is known; the stats line shows it). This bounds the effect of a reboot loop at a high OPP; there is still no in-run throttle.
* Setup (level 2/3) can sleep up to 200 ms waiting for the first sample, in the owner thread during power-on; level 1 never sleeps.

## Throttle hook for a future live OPP switch (designed, host-tested, NOT wired)
`n2lvts_throttle_step()` is a pure hysteresis policy: soft limit 70 C -> level 1 (cap 654 MHz), hard limit 80 C -> level 2 (350 MHz), 5 C hysteresis down, persistently unreadable sensor -> level 1 (never minimum on a sensor fault),
`n2lvts_throttle_khz_cap(level)` = 886000/654000/350000. Intended call site: the quiesce point of the live switch described in README-0092-0093 (driver lock held, no job running), sampled from the 1 ms tick with a 200 ms minimum interval between switches. No code calls it today.

## Adding real calibration later (design)
1. Loader: copy `/chosen/atag,devinfo` words 116-119 and 190-207 from the live FDT into an ADT property (`/arm-io/lvts` `efuse` = 22 x u32); this needs a loader change plus approval to run on the device (the loader is not part of this host-only series).
2. Driver: if the property exists call `n2lvts_cal_from_efuse()`; accept only when it returns true (non-zero and plausible), otherwise keep the defaults.

## Host tests (`tests/nord2_lvts_test.c`, register model)
Conversion (including the vendor's truncation), efuse decode (unfused, implausible rc/golden refused as a whole, fused sample accepted), gated probe touches nothing, an already-running controller is readable with zero writes, GPU-only init order and register values (EDATA from measured/refined RC), forbidden-register check, id mismatch / stuck device / bad
calibration leave the controller as found, level-3 gate open and re-gate (also on failure), guard margins, throttle hysteresis and unreadable-sensor behaviour. Mutation checks: skipping the gate check or writing MONINT makes the test fail.
The model is a protocol contract from the vendor driver, not evidence of silicon behaviour; the first device run must therefore be `nord2_lvts=1` on the probe image to see what the bootloader left running (`NORD2 GPU LVTS:` line shows clken/monctl0/id/msr0), then `nord2_lvts=2`.

## Risks
* Level 2/3 write to real thermal hardware that this port has never touched; the vendor SPM/SSPM normally pauses LVTS around 26 MHz-off states, which this port does not do (sensing runs continuously while on).
* If the controller or its clock misbehaves the bus access could hang (no external-abort recovery in this kernel): hence level 1 first, gate check before every access, and opt-in only.
* Approximate absolute temperature (default calibration); the guard margin is a heuristic.
* A level-3 gate open leaves `ifrao_therm` open until reboot unless the start fails; other drivers do not use it.
