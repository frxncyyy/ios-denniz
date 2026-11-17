# EMI ABI evidence and next executable common-init stage — 2026-10-20

Hardware established: four exact firmware files authenticated/decompressed in
RAM, total1,326,158 bytes; no ROM was placed or executed. Lock screen restored
per user. Eight MPU observer calls returned x0=2. This note makes no Wi-Fi
networking claim, and does not authorize a new device call or hardware write.

## What the local source actually specifies

Selected Android config has ARM64=y, MEDIATEK_EMI=y, MTK_EMI=n, MTK_EMI_MPU=n.
`drivers/memory/mediatek/Makefile:11–16` selects emimpu_old.c.
`emimpu_old.c:43–50` calls arm_smccc_smc with:
 x0=c200050b, x1=READ2, x2=selector0SA/1EA/2APC,
 x3=region, x4=domain_group, x5=x6=x7=0.
It returns `(unsigned int)smc_res.a0` DIRECTLY as the register value. There
is no separate success/status word in this selected read caller. SA/EA are
then shifted16 and DRAM base added; APC is decoded in3-bit fields (:80–110).
`include/linux/soc/mediatek/mtk_sip_svc.h:17–31,159–168` produces this exact
SMC64 ID. Its errors are negative-1..-4; positive2 is not a defined error.
`arch/arm64/kernel/smccc-call.S:17–31` captures rawx0..3 after SMC; XNU native
wrapper and reviewed linked instructions implement the same inputs/results.

Only x0 is consumed by the Linux read caller. Preserved x1..3 are ordinary
SMCCC behavior when a handler changes only x0; they do NOT alone demonstrate
an argument-shift bug. The concern is constant x0=2 for BOTH regions and ALL
SA/EA/APC groups. Treat `status0/range_valid1` in current logs as structural
bitwidth/form checks, not semantic validation. 40020000..4002ffff is NOT a
validated CONN range or permission. Never use it to gate ROM placement.

Legacyc2000261 is a different offset-based API in
`drivers/misc/mediatek/emi_mpu/emi_reg_rw.c:55–64`, not the selected config.
No alternate FID, debug action, raw MPU read or protection change is justified
by this constant2 result. No such call was made during this investigation.

## Primary upstream comparison: applicability boundaries

Official TF-A v2.8 MT8192 platform dispatcher has VCORE/APUSYS/DFD handlers,
and otherwise returns SMC_UNK. That establishes only the behavior of that
public MT8192 version, not Nord2/MT6893 secure firmware:
https://github.com/ARM-software/arm-trusted-firmware/blob/v2.8/plat/mediatek/mt8192/plat_sip_calls.c
Its common dispatcher also falls back to a platform handler:
https://github.com/ARM-software/arm-trusted-firmware/blob/v2.8/plat/mediatek/common/mtk_sip_svc.c

The current official 2026 common EMI implementation uses a different service
organization: NS_EL1 function50b is EMIDBG, with READ conditional on a debug
build option; its READ selector coverage and error mapping differ from the
older Android caller. Bootloader service415 handles another EMI path. These
new sources cannot decode this phone's positive2 result:
https://github.com/ARM-software/arm-trusted-firmware/blob/master/plat/mediatek/drivers/emi/emi_ctrl.c
https://github.com/ARM-software/arm-trusted-firmware/blob/master/plat/mediatek/include/mtk_sip_def.h

A tempting upstream return-value change d07eee2 is explicitly MT8188 and
TEE MPU PERM SET, not this NS_EL1 READ2 service. It therefore supplies no fix
for this phone's x0=2:
https://github.com/ARM-software/arm-trusted-firmware/commit/d07eee245b3fcc6b276969df34dc63ded1d4c8a2

Conclusion: reviewed public sources do NOT identify the shipped MT6893 BL31
handler/build. They establish no verified interpretation of a0=2. Status-vs-
data ABI mismatch remains a possibility, not a demonstrated diagnosis. A
future existing-Android sysfs observation could independently establish what
its actual read caller sees, but would change boot context and cannot directly
prove current XNU effective permissions. Do not spend repeated boots sending
slightly different SMCs without a matching handler contract.

## Next hardware measurement: guarded WFSYS cold-state snapshot

This moves common-init bring-up forward without depending on ambiguous MPU
metadata or touching EMI. Implement a tiny snapshot ONLY inside the existing
successful cold CONN power/ID probe, after both AP power acknowledgements,
bus protections/readability and HW ID20010101 have been checked, BEFORE its
existing teardown. It must never run as an independent worker reading powered-
off CONN space. Use the same serialized input/probe execution and cleanup.

Exactly three new reads, all in selected `soc3_0.c:2164–2196`:
-0x18000010: WFSYS_CPU_SW_RST_B bit0,0 means CPU reset asserted.
-0x18000000: keyed WFSYS_ON_TOP_PWR_CTL bit7,0 means top power not requested.
-0x180602cc: TOP_DBG_DUMMY_3_CONNSYS_PWR_STATUS bit2,0 means WFSYS off.

These are CONN-side RGU/host state used before WFSYS power-up in Linux.
Do NOT read WFSYS-local version0x184b0010 until WFSYS power prerequisites are
satisfied. No reset writes, keyed power writes, version read, firmware copy,
SMC retry, A-die SPI transaction or DMA in this first extension.

New explicit opt-in branch (e.g. separately selected probe mode) logs all raw
values plus masks/validity once, bounded lines<255bytes. Require pre-existing
power probe restoration1. If CPU/top/status is not cold, report it and use
existing guarded restore; do not force it cold by writing. A register MMIO
stall cannot be recovered by a software deadline, so keep the same powered/
protected/identity access prerequisites. Host tests assert exact3read allowlist,
no read before prerequisite success, no writes beyond existing power probe,
every failed prerequisite/partial setup and retained restoration guards.
Hardware acceptance:3valid reads, reset0/top0/status0, ID20010101,
restored1, all8 SMP/input/lockscreen still working. No EMI permission claim.

## Next implementation after cold evidence: staged common-init transaction

Source selected common cold order (`consys_hw.c:311–405`) is power/ID, EMI
remap, D-die, SPI-master configuration, A-die, AFE calibration, PLL, subsystem
status/low-power. Do not release WFSYS CPU directly after the existing ID probe.
AP physical ROM-placement itself does not require D-die/SPI/AFE initialization,
but still requires independent reservation/AP-access/no-alias and restoration
proof. Keep the prepared ROM transaction's permission prerequisites unsatisfied
until real evidence exists; do not manufacture them from this snapshot.

Prepare a standalone injected, snapshot/write/readback/restore engine for the
D-die and SPI-master settings next; no active hook until its exact masks and
rollback are reviewed. D-die (`mt6893_pos.c:750–803`) reads efuse18001818 and
writes a SYSRAM handoff word plus hardware retention controls18000038=0,
1800003c=ff,18000050=0,18000054=1. Save all destination words and preserve
unowned fields; prove SYSRAM ownership before writing. Do not log efuse data.
SPI-master (`:805ff`) programs sleep-controller command/address tables for
Wi-Fi/BT/GPS; that is a substantial shared controller configuration. Audit its
complete write list, snapshot ALL fields before any write, select non-BT-only
branch, and refuse if another radio is active. This should not be hidden as a
single harmless SPI read. Restore every changed field/readback on failures.

Only after that transaction is proven should an A-die ID-only stage be scoped:
Linux `:1113ff` sets26MHz strap on GPIO180 (DIR/DOUT5 bit20, MODE22bits18:16),
deasserts A-die reset18001900bit0 and performs TOPSPI read0x02c. TOPSPI uses
18004000busybit5,18004050address=b000|002c,18004054trigger0,
18004058readback, with RFSPI semaphore11 and bounded polls. The Android
observed ID66358a11 is the acceptance reference, not a reason to skip ownership,
strap/reset/clock prerequisites. Snapshot+restore the strap/reset/controller
state, and stop before analog register writes, efuse programming/readout,
AFE calibration or firmware release. Existing GPIO155 volume-down must remain
untouched; it is not this GPIO180 strap.

The full Linux A-die path clears SYSRAM and writes analog calibration/LDO/
XOBUF controls, followed by AFE/PLL. Those are later separately reviewed
transactions. This staged path gives measurable cold-state, retention/SPI
and A-die milestones while MPU support is investigated from matching source;
it avoids treating an unverified permission read as authority to write memory.
