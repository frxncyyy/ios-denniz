# E3 post-ID transaction design — 2026-10-21

Private source-only design; no active helper, hardware operation, CI/image change or UFS work. Source pin04d00c2565764c3922c9b4826b5acff1106e533f. Parent reports hardware8ca8ee39 CHIP_ID66358a11 complete/clean, clocks4006/IRQ1, common40 and outer power restored, input/display healthy. That advances observed transport identity to E3. It does not leave reset/common configuration applied after teardown.

Scope below inventories the complete **A-die function and nested efuse/thermal routine**, not later AFE/PLL/low-power controls. Those require separate transactions; selected mt6893.c:109–111 installs A-die,AFE,PLL directly, with no selected afe_sw_patch hook. There is no justified runnable write candidate yet: source documents intended values but not enough reversible semantics to call an arbitrary full analog-register snapshot a restoration guarantee.

## Source ordering and ownership boundary

Cold caller order is EMI remap -> D-die -> SPI master -> A-die -> AFE ->PLL -> subsystem/voltage/low-power. A-die itself clears16KiB SYSRAM **before** GPIO strap/reset/ID. Current ID diagnostic omits this clear. Do not append full A-die after ID, omit its clear silently, move clear after ID, or claim completed Android initialization. D-die/build words were written earlier by caller; source then clears the RAM. Preserve this observed order rather than repairing it by inference.

Full initialization requires exclusive ownership of this16KiB SYSRAM and proof no firmware/other radio/thermal reader can consume it during mutation; snapshot outside the input-thread stack, digest privately, restore and verify after a diagnostic. Current software exclusion plus cold/reset evidence is necessary but not evidence of complete SRAM ownership. Existing common40 touches limited SYSRAM words; that does not authorize16KiB. If exclusive ownership is not established, keep the SYSRAM clear and all handoff writes disabled.

An explicitly isolated pre-efuse diagnostic could eventually configure only the first two analog registers and the two digital controls, observe/readback and restore, without claiming common-init completion. It must be named a partial diagnostic and reviewed for independent analog side effects first. It does not require inventing a new full-init ordering or clearing SYSRAM, and gives a possible narrow milestone beyond ID. Its value is testing a reversible **TOPSPI write** path; ID proved only read transport. No such write is justified until the semantics gap below is closed.

## Complete A-die inventory

Rows list original source order. FFFFFFFF means source writes an entire32-bit register, **not** proof all bits are ordinary writable state. `S` means snapshot/readback required before any write; full-register snapshots must preserve reserved fields only once safe read/write behavior is known. All analog transfers share RFSPI semaphore11 and the source TOPSPI controller; take software ownership over the whole diagnostic and hardware semaphore over a complete owned group, never assume per-command release gives transaction exclusivity.

| Resource | Source mutation/observation | Candidate ownership and restoration |
|---|---|---|
| SYSRAM base +0..3fff | zero16KiB at1123–1127 | exclusive region ownership missing; no clear in partial diagnostic; full snapshot alone insufficient |
| GPIO180 DIR5/DOUT5 bit20; MODE22 bits18:16 | strap high/output/GPIO before ID; mode1 at1278 | reuse existing audited strap snapshot masks; restore original owned bits, no SET/CLEAR-register snapshot |
|18001900 bit0 | deassert A-die reset | reuse existing audited cold/reset ownership; ambiguous analog work blocks reset/power teardown |
| TOPSPI02c | ID read | no replay of command trigger; E3 identity gate66358a11 |
| SYSRAM+2800 | E3 ID write | no write without full SYSRAM ownership; not needed for isolated diagnostic |
| TOPSPI064 | FFFFFFFF <-00007700 | S; full-register write behavior/RMW/read-side effects not specified |
| TOPSPI0bc | FFFFFFFF <-00000021 | S; shared WF low-power command config, exclusive radio owner required |
|1800400c | clear80ff | S with mask80ff; partial diagnostic can restore only mask; shared FM settings require exclusive controller owner |
|18002018 | FFFFFFFF <-50305a00 | S; thermal engine owner/quiescence missing; restore exact original only after ownership |
| TOPSPI108 | clearbit30; clear43ff00c0 then OR40000000 for address0 | trigger/state-machine register; NEVER restore by replaying saved busy/kick/status bits; see below |
| TOPSPI130/134/138/13c | efuse RDATA reads | private data, no raw log; source shows no read-clear but lacks register specification to guarantee it |
| SYSRAM+2804/2808/280c/2810 | four efuse handoff writes | remain disabled until SRAM ownership; capture efuses privately outside UART |
| TOPSPI034 bits15:12 | if efuse1bit7, copy efuse1bits6:3 | S(maskf000), conditional thermal calibration |
| TOPSPI038 bits25:23 | if efuse1bit7, copybits2:0 | S(mask03800000) |
| TOPSPI038 bits27:26 | if efuse1bit15, copybits14:13 | S(mask0c000000); combined touchedmask0f800000 |
| CPU thermal software state | efuse-derived slope/b/offset or zero fallback | source update_thermal_data has global consumer lifetime; no blind use of Android helper in XNU; diagnostic need not publish |
| TOPSPI070 | FFFFFFFF <-80000000 | S; pad high-Z changes physical interface; restoring word is not established to undo electrical consequences |
| TOPSPI380/390 | FFFFFFFF <-000e8002 | S; disables calibration LDO; analog settling/restore evidence missing |
| TOPSPIa18 | FFFFFFFF <-f6e8fff5 | S; XOBUF supply voltage change, rollback settling unknown |
| TOPSPIa1c | E3 FFFFFFFF <-55555fff | S; E1 alternate d5555fff must never be selected for observedE3 |
| TOPSPI384/394 | FFFFFFFF <-00002008 | S; WF0/1 THADC IR initialization, analog behavior missing |
| GPIO180 MODE22 | source returns to CONN mode1 | inside full init; isolated diagnostic restores prediagnostic mode instead of leaving CONN |

Controller transport inventory is unchanged but TOPSPI **write** protocol and result ambiguity need their own audit: command/address/WD/RD/status, busybit5, owned semaphore11, clock-detector/timeout IRQ and barriers. Existing read-only adapter deliberately accepts onlyb02c command and WD0. Do not broaden its allowlist or reuse ID tests as write proof.

## Efuse kick: what source proves and does not

At983–1032 the routine takes semaphore11, reads108, clearsbit30, then RMW clears43ff00c0 (=kick30,address25:16,mode7:6) and setskick30, address0, mode00. Source comment explicitly calls mode00 a read of the first16bytes, then polls kickbusy30 clear with up to70*30us delays, checks valid29 and reads130..13c. This is **read sequencing**, not source evidence of a burn command. No high-voltage/burn enable write is present in this routine. Nevertheless mode00 safety and reset/retrigger/status semantics are not independently proven by a register manual or matching analog implementation; do not claim electrical non-programmability solely from a function name/comment.

Do not restore original108 as an ordinary saved word: savedkick1 could launch again, bit29 is status, and clearing then kicking mutates controller state. Require initialkick0; reject invalid/unknown mode; require known read-only mode semantics. Every TOPSPI operation must succeed independently. Source ORs some errors and still applies thermal values; candidate must refuse partial/invalid data instead. Busytimeout/transport-timeout after kick is ambiguous: hold semaphore and digital prerequisites, retain logs of phase only, halt further probe/cleanup until manual recovery. Do not power off/reset or release semaphore while an operation might still be active. Completedkick0 does not by itself prove safe restoration of the original internal efuse state. No efuse acquisition implementation until this lifecycle contract is known.

## Failure and acceptance design

Before any mutation: positive WFSYS/BGFSYS cold/reset and exclusive-radio owner gate; current source clocks/IRQ/readability; common40 verified; strap/reset prepared; E3 ID exact; no outstanding TOPSPI; snapshot every ordinary destination and verify all reads complete. Refuse partial snapshots. Budget every transport poll and total operation count, while acknowledging a stalled bus read itself is not software-interruptible.

After an unambiguous completed ordinary register write: readback only the proven owned fields; on mismatch reverse previously touched ordinary fields under retained semaphore, verify each restore, then release semaphore/restore reset+strap/common/outer power. A failed write callback may have partially committed: mark touched before issuing it. Any unknown completion, failed restore/readback or semaphore-lifetime uncertainty => retain ownership/common/reset/rails and enter explicit manual halt. Do not cascade teardown over a potentially active command. No CPUrelease/ROMload/AFE/PLL/calibration/radioactive-state claim.

A successful isolated first-write diagnostic would require correct ordinary readback and reverse restore, no efuse kick/SYSRAM/070/LDO/XOBUF changes, controller idle and owned semaphore clean, all common/outer restores, healthy input/display. It would prove only those two analog writes and digital controls. A full Android sequence remains blocked by all remaining inventory rows and EMI/current-domain policy.

## Concrete evidence gaps / next work

1. Obtain MT6635 E3 analog register semantics for064/0bc: reserved/W1C/self-clearing/readback and read side effects, plus required settling and write-command completion contract. Without these, no rollback implementation can honestly promise reversibility.
2. Audit source TOPSPI write adapter separately, including failures after trigger and no-replay cleanup; a mocked engine can enforce ordering but cannot supply missing analog semantics.
3. Establish16KiB SYSRAM ownership and consumers before implementing full clear/handoff. Keep partial diagnostic distinct while unresolved.
4. Confirm108 mode00 read-only operation and exact recovery/reset semantics; efuse validity and nested thermal writes require explicit staging, private values and restoration.
5. Later AFE/PLL are separate calibration transactions; do not hide them in this A-die milestone.

No executable candidate or invented behavior supplied: the exact blockers above prevent a safely reversible hardware-write implementation from the supplied source alone. Source-audited inventory and state machine make the next review concrete while parent completes the independent MPU observer.

Source links:

- [caller order]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/consys_hw.c:354)
- [selected callbacks]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893.c:109)
- [SYSRAM clear and strap]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1123)
- [post-ID controls]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1170)
- [efuse kick]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:983)
- [efuse handoff and thermal]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1034)
- [thermal masks]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1080)
- [RMW helper]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:2127)
- [register offsets and SYSRAM size]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/include/mt6893_consys_reg_offset.h:229)
- [later AFE]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1283)
- [later PLL]($HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c:1432)
