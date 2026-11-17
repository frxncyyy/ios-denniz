# Phase10 retained common core — supersedes phase7 boundary, 2026-10-21

New `completion.c/h` preserve the frozen `retain.c/h`, phase7 tests and manifest. They implement the prior D-die/common/SRAM/A-die/AFE/PLL stages plus the selected **first-radio WiFi-only RC-mode** caller tail. This is private injected C, not native MMIO code or a currently runnable image. No active integration/CI/device operation.

Actual selected caller tail is smaller than its names suggest:

- WiFi3 subsystem status: take common-SYSRAM semaphore19, OR bit3 into18052834, release19.
- `_consys_hw_raise_voltage(WIFI,true,true)` calls selected MT6893 PMIC handler; that returns0 immediately unless drv_type0 (BT). Therefore no WiFi voltage write/delay/timer is emitted. Do not synthesize a VCN13 raise.
- `connsys_low_power_setting(0,1<<WIFI)` selects non-BT E2/E3 analog defaults and the RC branch. Entry callback must freshly prove RC source register10006504bit0 (as existing outer evidence) and26MHz; a legacy-mode implementation is not provided.
- Final `pmic_mng_common_power_low_power_mode(1)` checks an ops hook absent from `g_consys_platform_pmic_ops_mt6893` and returns0. No final PMIF/PMIC write is justified. This does not omit initial common rails; they remain the pre-entry persistent-power owner's job.

## Added exact sequence

Set host18060184 mask/valuee00; CFG18001860 maskf000/value1000. RC stable words GPS183c/BT1844/WF184c/TOP1854=02080706, clear corresponding ACK masksbit15 at1838/1840/1848/1850; set RC_CTL01834 bits4,5,6, set EMI_CTL01c00bit0, clear RC_CTLbit0, set EMI_CTLbit18 and timeout7ff0=3ff0.

Top-clock enable follows vendor shared ownership: semaphore19, read SRAM2830 mask7f, if zero set ADIE_CTL1900bit1 and poll WTSLP_STA18005008bit26 to0, then set HOST_CONNINFRA ownerbit4 and release19. Source polling macro initializes one read, uses count>100, and rereads/delays5us; complete stubborn-busy path performs101 delays (505us), then fails. Candidate retains19/common state on failure instead of source's permissive continuation.

Take RFSPI11, RMW E3-as-E2-default: a18 maskc180/valuec180, a1c maskf0ff0080/value50550080; release11. Take19, clear our SRAM2830bit4 if set, reread7f; only when lastowner drops, clear ADIE_CTLbit1 and same bit26 polling; release19. Thus no nested11/19 acquisition. Unknown take/release/command completion retains flagged owner and halts.

Set CFG1a00bit28, light security10f0mask1b/value1b; WiFi-only skips BGF-power workaround. Source config setup writes1800f000=32c8001c; debug mux1b00mask7->0; RGU18000090=6; set CFG1860bit9. Finally clear bus SW control1a00bit23, then source read/OR/write CFG1860bit0 toHW control. **No added CONN proof/read/log callback after HW handoff.** Final two writes intentionally do not run generic off-domain checks between them; only the exact source1860read/write remains after the last guarded1a00 operation. Failure returns halt, no rollback or follow-up MMIO. Successful result phase10 means source-programmed retained state, not proven bus accessibility after handoff, RF/network readiness or permission to release CPUs.

## Native adapter boundaries (still required)

Use a new exact persistent opt-in mode separate from current ID/common/snapshot gates. Do not infer it from nord2wifi=1 or turn the diagnostic into persistent initialization silently. Before any mutation latch unique software owner and request mode; make subsequent calls idempotently refuse reentry. Input/key PMIF serialization remains owned by the same execution context. Do not create an independent concurrent radio thread against existing input PMIF.

`ni_wifi_power_probe` boundary: enter after its original common PMIC/clock/pinmux/power acknowledgements, AP bus-protection/readability and ID20010101 checks, at the current `ncc_power_window` opportunity **before** outer restore. Repeat positive WFSYS/BGFSYS reset+off and all other radio/master idle predicates; map exact powered CFG/RGU/host/SPI/table/AFE/thermal/debug/semaphore and SYSRAM18050000+4000 apertures. Do not use the current ncc_run/nis_scope rollback callback: it will undo retained common/strap/reset. Instead select the private persistent engine only in explicit new mode; legacy branch unchanged. Ordered32-bit read/write callbacks require per-address/phase/value masks, fresh AP owner/readability checks until finalHW transition and no broad CONN aperture permission. Source semaphore acquisition is side-effecting read; never snapshot it or probe release by reacquiring.

Before `npc_run`: audit and perform selected EMI remap as separate owned step, preserving exact source ordering before D-die. This is a radio address remap, not permission SET; neither ownership nor AP/CONN effective MPU access is implied. Independently complete legacy-MPU evidence before firmware placement. Initial PMIC/clock state must satisfy source cold power sequence; existing observer's temporary setup must be compared field-by-field with persistent source requirements.

On first source mutation set persistent retained latch **before** calling write; it must be recognized by `nw_restore`, old common cleanup, saved power context, suspend and Linux-return/boot handoff. Successful phase10 keeps CONN prerequisites and original PMIF/rail ownership; do not restore the old snapshots, clear ownership, or power down because function returned. On any failure after mutation/acquisition uncertainty latch fatal and manual reset; panic path must not run old analog/reset/common/outer teardown. Pre-mutation guard failure may use unchanged existing safe diagnostic cleanup. The outer probe must branch on explicit retained/dirty state before its current unconditional restore. Do not overload `clean=1` (which currently means verified restored) to mean persistent success.

The existing callback check cannot simply call full `ncc_native_check` unchanged: old cold reset and SPIbusy predicates reject our deasserted A-die/owned command; its common-table expectations and SYSRAM words change as source proceeds. Preserve fresh AP power/readability and WFSYS/BGFSYS cold ownership while phase-aware checks allow only our documented analog/clock/common changes. After sourceHW handoff do not run those off-domain checks automatically. A later source-supported conninfra wakeup transaction is required before additional off-domain reads/firmware stages.

## Validation and limitations

C11 Wall/Wextra/Werror and ASan/UBSan pass. New test injects failure at all8725 callback positions (no further callback after first failure), verifies phase10 retained success, no CPU/power-release address, no semaphore nesting, complete efusebusy70 and topclockbusy101 timeout retention, invalid-efuse source continuation and strictdefaultoff. Mock tracks topclock owner word and marks HW handoff; subsequent new MMIO check/read is refused except exact source final1860read/write. Tests do not prove native mappings, electrical calibration, MPU access, initial rail correctness or successful HW-control transition. Existing frozen phase7 independent review excludes this extension; request a separate source review before native implementation.

No fullready claim: persistent native cold owner/transport/EMIremap, software thermal metadata lifetime and hardware acceptance remain explicit gaps. Phase10 completes the selected source common register sequence only under those entry contracts, with WFSYS/BGFSYS CPUs still reset and off.

Pinned source references:

- [caller tail]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/consys_hw.c:382)
- [E2/E3 default table]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:78)
- [lowpower first-radio branch]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1567)
- [last HW handoff]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1800)
- [topclock ownership/poll]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:2200)
- [radio status]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:2330)
- [poll macro]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/include/consys_reg_util.h:90)
- [PMIC ops table]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pmic.c:84)
- [WiFi voltage no-op]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pmic.c:629)
- [final PMIC wrapper no-op]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/pmic_mng.c:241)
- [semaphore indices]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/include/mt6893.h:30)
- [driver and owner indices]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/include/conninfra.h:47)
