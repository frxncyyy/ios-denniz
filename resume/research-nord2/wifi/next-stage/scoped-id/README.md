# Private scoped cold/common -> A-die CHIP_ID diagnostic

UNLINKED candidate; no active/canonical source or current CI edits. Does not execute firmware, RF analog writes, calibration, CPU release or SYSRAM/EMI writes. A TOPSPI CHIP_ID READ command is issued (not a passive register observer), so hardware use requires separate approval after current common diagnostic evidence.

## Candidate/API

- `scope.c/.h`: private callback variant of canonical40-register common engine. Same all40snapshot/apply/verify/reverse cleanup. Calls inner ONLY after all40 programmed values verify, BEFORE restore. clean inner failure restores common; dirty inner failure retains all40 originals and leaves common applied for dirty halt. Canonical ncc_run unchanged.
- `strap.c/.h`: snapshots all4 strap/reset data registers BEFORE common mutation; refuses initially deasserted A-die reset. Applies exactsource sequence DIRoutput -> DOUThigh -> muxGPIO -> ADIEresetdeassert. Calls reviewed bounded `../adie-id/id.c`, then restores reset -> mux -> data -> direction, before common reversal. Partial mutations marked touched before callbacks. Unknown CHIP_ID cleans up and reports error without choosing analog table.
- `native.inc`: exact address/mask backend, semaphore11, no guessed securecalls. `common_adapter.candidate.inc` uses it only for `nord2wifiadieid=1`; requires existing `nord2wifi=2` AND `nord2wificommon=1`. Other values leave current common behavior; commonoptout adds zero maps/MMIO/logs. `sweet_touch.candidate.c` is the entire private example including pure modules. No generated patch applied.

## Exact native ownership and addresses

Original proven cold origin/rails-off, nw.saved/restore armed, sole power/input thread, measured chipID20010101 and existing AP power/clock/readability guards remain mandatory. No BT/GPS/FM drivers/CONN CPUs released. Before inner entry, canonical14predicate remains complete. During TOPSPI our own busybit may assert, so inner checks first13 cold client/request/power fields and AP guards, and separately checks actual strap/reset programmedvalues; it deliberately does NOT require SPIbusy0. TOPSPI helper itself polls busybit5 by phase.

All4 independent snapshots precede common apply (not merely before reset). Physical data registers/masks/source values:

|address|mask|programmed|
|---|---|---|
|10005050 GPIO DIR5|00100000|00100000|
|10005150 GPIO DOUT5|00100000|00100000|
|10005460 GPIO MODE22|00070000|00000000|
|18001900 ADIE_CTL|00000001|00000001|

GPIO180 DIR/DOUT writes use SET/CLR aliases10005054/58 and10005154/58, never an alias snapshot. MODE/ADIE use preserving RMW. Whole-register apply/readback and restoration verify source-owned fields plus unchanged unowned fields. Unexpected unowned changes refuse cleanup rather than overwrite a concurrent writer. Original ADIEresetbit0 must be0. No change to GPIO155/volume-down.

Selected mt6893.c:271–290 returns only26M externalXO or26M cotms (source52M branch unreachable in this selected clock function). Candidate relies on the existing powered clock, GALS/readability and6ms XOsettle; adds NO clock votes/muxes/TCXO pin. Source first A-die CHIP_ID occurs directly after strap/reset, before later analog efuse/calibration. Hardware must still establish valid oscillator response; source code and D-die ID do not prove a responsive A-die. A timeout is a failed diagnostic, never a reason to change frequency or skip prerequisites.

Semaphore source base18070000, M2 OWN_STA2000+11*4=1807202c; OWN_REL2200+11*4=1807222c. Candidate maps18072000/4KiB; acquire is a SIDE-EFFECTING read ofoffset2c bit0, max1000tries with1usdelay. No snapshot/release/reacquire of others' semaphores. Release writes1 tooffset22c with existing ordered ni_wr. A completed release write has no source-established nondestructive ownership readback in this helper; candidate does not invent one. Native MMIO read/write has no recoverable errno if the bus hangs. Barriers establish ordering, not a software timeout on an unreturned bus transaction.

Only TOPSPI addresses18004000status/50address/54triggerWD/58RD are exposed. Only allowed command addressb02c or original saved address; WD only0. No command format a000/analog write is emitted. Trigger is NEVER replayed during restoration. After busyclear completion, restore TOP_ADDR, release own semaphore, restore reset/strap, then common table, then original power/rail probe. Clean completion means transport/address+owned digital register restoration, not restoring the observed read-data register or undoing an analog read.

## Ambiguous completion/dirty contract

If triggerwrite/barrier/guard/completion becomes ambiguous, `nai_result.clean=0`: retain semaphore, applied strap/reset, all common registers, original snapshots and fresh-owned context. Do NOT assert reset, restore tables, power off or return Linux merely to make a cleanup log pass. Scoped resultclean0 reaches existing native common dirty latch/panic before normal outerout. Candidate retains exact `nw_restore` earlyfalse and `nord2_input_prepare_return` early-1 latch guards. Existing sweet_early path flushes and WFE-halts on that -1; physical reboot is required. Dirty reset/strap/common restoration follows the same rule. A retained powered domain cannot also be claimed safely handed to Linux rescue.

Busy polling is bounded100reads/phase+50usdelays (99betweenreads); semaphore polling1000tries. Each predicate repeatedly checks AP/readability andcold clients. Total software operation count bounded; a native MMIO hang is NOT interruptible/recoverable by these loops. No deadline claim exceeds that limitation.

## Validation

`python3 .../scoped-id/test.py`: ASan/UBSan actual native adapter and both engines. Verifies common remains programmed at the exact TOPSPI trigger, all4 strap snapshots precede it, no common restore until reset/strap/semaphore cleanup; exact reverse lifetime, optouts, initially-active reset refusal, known/unknown ID, semaphoretimeout,91write-corruption positions of92nativewrites (releasewrite has no modelled recoverable error channel), incomplete SPI busy/guardloss retains allownership and refuses outerreturn. Fault writes mutate before failure/readback, covering partial apply/restore. Existing pure ID test separately covers all28callback errno positions including ambiguous acquire/release. Test logs check actual255character cap.

Actual BSD ARM64 compile of whole private sweet_touch candidate plus separatelyscope/strap helpers PASS. No hardware, active integration or CI.

Source authoritative revision04d00c25:
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c#L1113
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c#L1857
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c#L1985
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893.c#L271
