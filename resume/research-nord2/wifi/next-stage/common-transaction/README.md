# Conditional MT6893 retention/SPI transaction — private, unlinked

`ncc_run(ops, context, 1, 2, result)` executes four D-die retention-control writes then all 36 selected Wi-Fi/non-BT SPI-table writes. Stage1 executes retention only. Every destination in the requested stage is snapshotted before ANY mutation, and every write is checked by a completed device barrier plus full-register readback. Success also restores every touched register in strict reverse order and verifies its full original value. Error paths attempt restoration of all touched destinations; a callback failure may already have written, so touched is set BEFORE invoking write. Failed restoration is reported dirty, with snapshots retained. The pure helper has no allocation, MMIO backend, maps, SMC, EMI, RF-SPI commands, CPU release, or calibration. The private adapter demonstration described below adds allowlisted MMIO only.

This is a reversible diagnostic of the selected table, NOT full D-die/common initialization. Efuse-to-SYSRAM and build-mode publication are deliberately omitted. No source CRC is invented. No table register is a SPI command launch register, but these tables govern autonomous sleep/wake behavior: all common-infra clients must be proven idle, not just Wi-Fi. Do not run under ordinary powered Android/radio coexistence.

## Exact source contract

Selected local Android commit `04d00c25`, platform MT6893 (not inferred from newer MediaTek chips):

- `drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893.c:107–108` binds the two functions.
- `mt6893_pos.c:750–803`: four full32 retention writes at18000038/3c/50/54, following efuse/build-mode publication.
- `mt6893_pos.c:805–990`: all36 SPI table writes in exact source order. The branch is non-BT, specifically Wi-Fi-only next_status; ON3/4 are74e0fff5/76e8fff5 and OFF3/4 are44e0fff5. Full32 and masked writes follow the source macros, including upper bits of GPS_CK_ADDR (the comment is misleading; executable code uses a full write).
- `include/mt6893_consys_reg_offset.h:174–214` fixes table base18005000 and offsets.
- `platform/include/consys_reg_util.h:57–70`: writel followed by mb, masked write performs preserving RMW. Adapter must supply equivalent device ordering; helper uses unchanged snapshot only after a fresh equality check under exclusive ownership.
- `platform/consys_hw.c:355–370`: actual sequence is EMI remapping, D-die, SPI tables, then A-die. This isolated subset neither sets remapping nor claims later A-die prerequisites completed. Retention/table registers are direct CONN MMIO, not EMI.

Primary source URLs (same tree/revision; no alternative SMC ABI assumed):
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893/mt6893_pos.c
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/consys_hw.c
https://github.com/<github-user>/android_kernel_oplus_mt6893/blob/04d00c25/drivers/misc/mediatek/connectivity/conninfra/platform/include/consys_reg_util.h

## Future integration and actual gaps

Inside the existing successful power+ID window, run the existing three-register cold snapshot FIRST. Only if all three cold bits are zero may the caller consider this helper. Fresh check() must prove ALL eight named flags before every operation: exact current probe ownership/armed outer restoration, chipID20010101, AP power/clock/protection acknowledgements, CPU reset asserted and WFSYS power/status off, ALL common clients/firmware idle, Wi-Fi-only intended branch, verified AP access to both apertures. A bootarg alone or three cold Wi-Fi bits alone cannot prove common client ownership. Existing power-probe ownership establishes only its own transaction; the adapter still needs defensible all-master idle/AP-access evidence. The concrete14-field proof and cold-origin marker are now implemented in a PRIVATE native adapter demonstration; no hardware approval is implied.

Future adapter must map RGU18000000 and WTSLP18005000 read/write, provide checked allowlisted IO and barriers, and leave original outer power cleanup unconditional. Require `clean==1` plus successful outer restoration before ordinary boot handoff; dirty means stop and retain evidence, never continue toward CPU release. Snapshot failure makes no writes. Permission ambiguity from SMC positive2 is not reinterpreted here. Reversible register restoration proves the observed register values, not reversal of any autonomous action if the ownership premise was false.

Fault tests cover every callback position transient/permanent failure, partial writes before failure, all possible injected single read corruptions, optout/invalidstage, full snapshot-before-write, table source equality, forward/reverse order, and whole-register restoration. Pure helper ARM64 compile uses the existing BSD compilation recipe. No source hooks/build list changed.


## Native demonstration ready for review

`adapter.inc` and `sweet_touch.candidate.c` implement the concrete contract in `adapter-contract.md`. `integration.patch` is an explanatory diff ONLY, never applied. New optin is `nord2wificommon=1`, in addition to outer `nord2wifi=2`. It does not depend on separate nord2wificold bootarg. Default/other values add no map/MMIO/log. Candidate sets a fresh-owned marker only after the existing original whole-CONN/PMIC cold checks pass, immediately before restoration is armed. It invokes cold snapshot then the14-field predicate twice and stage2 common transaction after cfg_id, before success/out/outer restore.

The native read/write callbacks only accept the40 exact table destinations; writes accept either their fully snapshotted original value or the source-masked programmed value, and only after all40 snapshots. Guard reads use the separate14-field list plus existing AP checks. Device barrier is `dsb sy`; existing ni_wr also provides its normal barrier. No native RF command access is exposed. Normal success means the table was TEMPORARILY programmed/read back then completely restored, not left initialized for execution.

Dirty restoration latches globally, retains `nw.saved`, `domain_touched`, fresh-owned and all snapshots. The adapter panics BEFORE returning to the caller's out/nw_restore. Candidate makes nw_restore return false without hardware access while latched, and makes nord2_input_prepare_return return-1 before any cleanup. Existing sweet_early.c:1158–1161 handles that error by flushing the console then WFE forever: manual physical reboot is required, rather than Linux rescue handoff. This is intentionally distinct from ordinary clean errors, which take normal outer power restoration. It cannot both preserve a dirty powered domain and boot Linux safely; no automatic recovery claim is made.

Native host tests use the actual adapter.inc, not a rewritten engine. They verify default zero additional accesses; every7 AP prerequisite,14 predicate and4 mapping failures;40 partially corrupted apply writes (clean reverse restoration); all40 corrupted restore writes (dirty panic, no outer restore); persistent AP guard loss after first write; precise address/write-value bounds, forward/reverse order and actual255-character line limit. Python ties the model to exact candidate insertion, original-cold marker placement and both early dirty-return guards. ASan/UBSan PASS; actual full private candidate and pure common helper BSD ARM64 compilation PASS. No active file/hook changes.

The implementation deliberately rechecks full14predicate before every helper operation and AP guards before each CONN read. Baseline is bounded but more expensive than the pure640callback model: approximately40,850 native reads including repeated AP checks for a successful40-register apply+restore, plus80 writes and ordering barriers, excluding the existing outer probe. This is an operation bound, NOT a hardware-stall timeout guarantee. A future optimization must preserve the same ownership proof and be separately reviewed; this demonstration prioritizes a directly auditable contract.
