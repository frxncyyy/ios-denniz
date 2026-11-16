# Power/clock gap: exact source facts and next implementation

The driver deliberately does not write MTCMOS/regulator values it cannot
verify.  `nord2_gpu=1` reaches `regulatorSet()` and is refused before any GPU
MMIO write.  `nord2_gpu_assume_powered=1` now performs a read-only snapshot only: it
does not program AS0 or enable submissions. See MORNING_REVIEW.md.

## Source facts already implemented

From `drivers/gpu/mediatek/gpufreq/v1/mt6893/mtk_gpufreq_plat.c`:

* power-on order: VSRAM then VGPU; MTCMOS `mfg_async`, `mfg`, then core stack
  domains; `subsys_mfg_cg`; external CG RMW on `0x13fbf020/0x2c/0xb0/0xb4/0x24`.
* power-off order: CG, MTCMOS reverse, VGPU then VSRAM.
* MFG base `0x13fbf000`; APMIXED `0x1000c000`; no voltage/frequency changes
  made by our driver.
* bus-idle diagnostic: `0x13fbf0b4=1`, `0x13fbf170=3`, poll
  `0x13fbf178 & 4`; bounded in our tests, not used on the power-on path.

From `drivers/clk/mediatek/clk-mt6885-pg.c` (MT6893-compatible table):

* SPM base `0x10006000`; `POWERON_CONFIG_EN=0x0000`,
  `PWR_STATUS=0x016c`, `PWR_STATUS_2ND=0x0170`.
* MFG0..MFG6 `PWR_CON` at `0x308..0x320`.
* status bits: MFG0 bit2, MFG1 bit3, ... MFG6 bit8.
* MFG SRAM PDN bit8; ACK bit12.
* MFG0 on sequence: `PWR_ON(1<<2)`, `PWR_ON_2ND(1<<3)`, poll both status
  views, clear `PWR_CLK_DIS(1<<4)`, clear `PWR_ISO(1<<1)`, set
  `PWR_RST_B(1<<0)`, clear SRAM_PDN, poll ACK clear.
* MFG1+ add bus-protection SET/CLR on infracfg (`0x10001000`) with per-domain
  masks at `INFRA_TOPAXI_PROTECTEN_*`.

## Still missing

1. Hardware validation of the implemented `nord2_gpu_scpsys.c` MTCMOS engine.
   This engine and its XNU MMIO adapter exist; regulator/clock callbacks
   prevent the real enable path from reaching it. The host register model
   does not establish successful power control on the device.
2. A PMIC/SPMI path for `mt6315_7_vbuck1` (VGPU) and
   `mt_pmic_vsram_others_ldo_reg` (VSRAM).  The vendor uses
   `regulator_enable()`; the register values/sequence are in the MT6315/PMIC
   drivers, not in the GPU tree.  Writing guessed values is forbidden.
3. Clock gate/mux bring-up for `CLK_TOP_MFG_SEL`, `CLK_TOP_MFGPLL`,
   `CLK_TOP_MAINPLL_D5_D2`, `CLK_MFGCFG_BG3D`, and the SCP_SYS_MFG domains.
   The current bootloader/DVFS state is used read-only when assume-powered.
4. GPU-internal L2/tiler/shader PWRON and bounded READY/transition handling.
   The current board code only waits for READY after reset; it cannot assume
   those blocks were left enabled.
5. Any voltage/frequency operating point.  Start from the bootloader state and
   only later add a bounded DVFS table derived from the vendor OPP tables.

## Qualification order

1. Implement SPM MTCMOS read/restore only; compare `PWR_STATUS` before/after
   and prove no other domain changes.
2. Add PMIC rail reads first (voltage readback), then enable with timeout and
   immediate rollback; verify on a bench supply/current limit if available.
3. Add MFG PLL/mux at a single source-supported point; no DVFS.
4. Only then read GPU_ID and run the compute PROBE.

Source implementation can continue offline. Hardware qualification needs an
authorized exact-image boot; none was performed in the morning review.
