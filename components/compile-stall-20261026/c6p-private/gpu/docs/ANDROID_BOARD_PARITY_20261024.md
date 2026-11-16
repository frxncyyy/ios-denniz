# Android board initialization parity, 24 October 2026

The corrected ADT boots iOS and native USB. GPU initialization stops at
`stage=1 error=3` (`N2CLK_E_PLL_RANGE`). The raw inherited PLL was not logged;
the actual current register value remains unmeasured. Historical Android logs
from this same device report preloader frequency **349900kHz**, just below the
old 350000kHz minimum, and GPU **arch9.0.8 r0p1 status1**. Both rescue captures
agree (`research/nord2/rescue/test/dmesg.txt` lines1004-1024 and
`research/nord2/wifi/rescue-dmesg.txt` lines2064-2083 in the main checkout).
These are historical device observations, not new XNU register reads.
This is not evidence of
GPU hardware failure, and no GPU job has executed successfully yet.

## Source and concrete difference

Local Android tree: `../../../../../../nord2-current-kernel`, commit
`04d00c2565764c3922c9b4826b5acff1106e533f`.

`drivers/gpu/mediatek/gpufreq/v1/mt6893/mtk_gpufreq_plat.c`:

- `__mt_gpufreq_init_volt_by_freq` (3223) bounds/rounds the preloader clock
  against its OPP table, then calls the voltage/frequency setter. The native
  driver previously refused an inherited rate outside that table instead.
- `__mt_gpufreq_calculate_dds` (2565) and `__mt_gpufreq_get_cur_freq` (2922)
  provide the exact 26MHz reference/14 fractional bit arithmetic.
- `__mt_gpufreq_clock_switch` (2637) parks the mux, writes CON1 with PCW_CHG,
  waits 20 microseconds, and switches back. Its comment describing the parking
  clock is stale: `arch/arm64/boot/dts/mediatek/mt6893.dts` supplies
  `CLK_TOP_MAINPLL_D5_D2` for `clk_sub_parent`, mux index3 in clk-mt6885.c.
- `mt_gpufreq_external_cg_control` (673) is compared by executing its unchanged
  code against the native implementation over 128 different register states.

`drivers/clk/mediatek/clk-mt6885.c`: MFGPLL CON0/1/3 are 0x268/26c/274,
mfg_sel is CLK_CFG_5 bits9:8, PDN15, update bit21. MFGPLL has no tuner and no
HAVE_RST_BAR. `clk-pll.c` preparation powers CON3 bit0, waits1us, clears ISO
bit1, waits1us, enables CON0 bit0, waits20us.

`drivers/clk/mediatek/clk-mt6885-pg.c` confirms MFG0..6 control offsets
0x308..320, status bits2..8 and SRAM PDN8/ACK12. MFG1 bus-protection release
order is step2.0,2.1,2.2,1.0,1.1; the existing native order matches. The source
explicitly skips ACK checks on release. This was source-reviewed, not a new
hardware verification or exhaustive executable comparison of SCPsys.

## Native initialization and deliberate bounds

A valid inherited OPP remains unchanged. For an invalid or disabled PLL,
normalization is allowed only when **both** SPM status words confirm all seven
MFG domains off. The status is checked again before writes and before changing
the clock. An inherited live domain is refused; live DVFS is not implemented.

The target is the lowest Android OPP: 350000kHz, divider /8. The rail table
takes the greater voltage from the two Android segments: VGPU600000uV and
VSRAM750000uV at this frequency. Existing higher in-range voltages are retained.
The PLL is changed only after rail verification, while domains remain off.
This is a deliberately restricted cold-start implementation of Android's
initialization, not the complete Linux DVFS/thermal/frequency-hopping stack.

CLK_CFG_5 SET/CLR aliases preserve unrelated consumers. Register readback
must confirm the target DDS/rate, power/isolation and mux. Only after that
can MTCMOS and GPU MMIO proceed. Restoring the original rate requires every
MFG domain off; failed restoration retains supplies and marks ownership
uncertain. A foreign domain appearing during rail enable also retains supplies.
An inherited open BG3D gate is no longer closed by our release path.

## Verification performed on the PC

`gpu/tests/android_clock_oracle.py` extracts unchanged Android DDS, decode and
external-CG functions into ignored build output and executes them with register
models under ASan/UBSan. It checks:

- all41 table rows against the higher voltage of the two vendor segments;
- 10000 PLL decode inputs and all41 OPP round trips;
- the actual programmed 350MHz CON1 against Android's DDS calculation;
- VGPU/VSRAM register addresses against the actual MT6315/MT6359P headers;
- 128 external-CG starting states against the unchanged Android function.

The integrated board tests additionally cover cold PLL off/under-range/over-range/
unsupported divider, ignored programming writes, ignored restoration writes,
inconsistent/foreign power state, preservation of unrelated mux bits, and
inherited BG3D ownership. These are protocol/model tests, not GPU operation.
Pinned source/function hashes are recorded in `gpu/build/android-clock-oracle/receipt.json`.

## Observability and artifacts

Cached clock, rail, PMIF, reset, job-slot and power/rollback fields are logged
without adding diagnostic access to unpowered GPU banks. User selector11 uses
IOLog rather than DEVELOPMENT-disabled kprintf, with the same 240B/call and
16KiB/client limits. The installed addon and userland probe remain unchanged.

`gpu/tools/build_board_candidate.py` compiles exactly Nord2GPU.cpp,
nord2_gpu_board.c, nord2_gpu_clk.c, nord2_gpu_hw.c and nord2_gpu_core.c, then links against the preserved startup
kernel archive. Other kernel link inputs are unchanged and hashed before/after.
Full boot checks and source-sync/baseline checks govern the final artifact.
Private derivatives must not be uploaded or built with public Actions.

The unflashed log-only image bbdcc01d is superseded. The new board image needs
its own exact authorization. System Metal remains disabled; the old UI image
has an invalid ADT and must not be used. No Metal compute, pixel, IOSurface or
UI test has passed on hardware.

## r0p1 workaround from the actual Android source

The historical ID exposed a second guaranteed software refusal: the previous
native identify function rejected r0p0/r0p1 because TTRX_3485 was unimplemented.
The Android r0p1 issue list differs from r0p2 only by this entry. The other
listed issues have the existing quirk handling; 9435 concerns scheduler timeout
policy, and TSIX_2033/TTRX_1337/3414/3083/3470/3464 have no conditional kernel
uses in this tree outside the issue tables. This does not certify userspace
shader errata or complete Metal conformance. SHADER_PWR_CTL_WA is enabled only
for MT6877/project6877, not this MT6893 configuration.

`firmware/valhall-1691526.wa` exists in the Android tree, 440 bytes, SHA256
1a52a7f3c7c8b15e13e226d0956091621a83baafd80a3f93debb8271110e5951.
Build preparation requires this exact hash and validates header/version2,
info offset416, JS0, chain VA0x100000000, flags6 and one 384-byte payload.
The generated header and firmware stay in ignored local build output.
Supply NORD2_ANDROID_WA to apply_stage_overlay.sh for another source location.
No additional rootfs file or addon replacement is needed.

The board powers L2/tiler without shaders on r0p1. The core creates a private
AS0 MMU, maps the vendor bytes at the original fixed VA, GPU read-only and NX
(map_flags6 exactly as Android), and makes them visible before AS UPDATE.
The workaround powers shaders, waits, submits with descriptor writeback disabled,
waits for DONE, checks faults/idleness, powers shaders off, and restores IRQ
masks. Only then do normal shaders and client jobs become eligible. Every
verified reset clears qualification and reruns the workaround. The private
AS0 cannot be opened by clients. Memory is retained on timeout/uncertain AS;
it is released only after acknowledged AS disable. No failure enables Metal UI.

The unchanged Android execute/run_job/wait helpers are compiled into an oracle:
all16 ordered register writes match (plus our explicit stale-IRQ clear before
submission). Our waits also enforce failures that the vendor caller ignores.
Integration tests use the actual native core/MMU/hardware modules and cover
NX/read-only payload, source bytes, AS0 isolation, first start, reset/restart,
all five allocation failures, job error/timeout, MMU fault, shader on/off timeout,
AS configuration/disable failure, and retained DMA ownership. ASan/UBSan pass.
An additional compile-time oracle compares **113 register offsets/commands**
against the actual Android JM headers; all match. Evidence and source hashes:
`gpu/build/android-{clock,register,wa}-oracle/receipt.json`.

Unflashed board-only image3fb0ef71 is superseded: it still rejects r0p1.
The combined candidate is the only next test under consideration. Hardware
GPU completion remains unproven; passing source/model tests is not that proof.
