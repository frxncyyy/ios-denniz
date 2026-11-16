# Nord 2 native GPU stack — implementation architecture

Candidate: `research/nord2/gpu/overnight-20261024/work`, branch
`deepseek/nord2-metal`, baseline `b2ccaed`.  All device interaction is
forbidden tonight; every claim below is host-side unless explicitly marked
device-UNTESTED.

## Hardware target

* SoC MT6893, Mali-G77 (Valhall arch **9**, minor 0), 4 KiB GPU pages,
  Job Manager (JM) submission, no CSF.
* MMIO `0x13000000` size `0x4000`; MFG clock/control `0x13fbf000` size
  `0x1000`; IRQ SPIs 362 GPU, 363 MMU, 364 JOB, 365 EVENT, 366 PWR.
* 4 KiB MMU leaf entries: valid `3`, next-level `1`, invalid `2`; access bit
  10, RW bits 7:6, memattr 4:2, NX bit 54 (vendor r32p1 aarch64 mode).

## Layers and files

| Layer | Implementation | Offline evidence |
| --- | --- | --- |
| A board/power | `gpu/src/nord2_gpu_board.c`, `nord2_gpu_scpsys.c`, `Nord2GPU.cpp` DT parse, `tools/mkadt-nord2.py` mali node | board/SCPsys tests PASS; source-derived rail/MTCMOS/CG order; MFG0..6 MTCMOS implemented; PMIC rails/clock gates refuse rather than guess; default off |
| B memory/scheduling | `nord2_gpu_mmu.c`, `nord2_gpu_mem.c`, `nord2_gpu_sched.c` | ASan/UBSan MMU + scheduler tests PASS; 4 KiB over 16 KiB host pages, conflict preflight, bounded waits, quarantine, close/reset |
| C arch9 descriptors/JM | `nord2_gpu_desc.c`, `nord2_gpu_jm.c` | byte-parity with Mesa v9 genxml for job header, shader env, compute/fragment jobs, framebuffer, RT, tiler heap/context, buffer/resource, texture, sampler, attribute, shader program, local storage, depth/stencil |
| D compiler | `tools/nord2_mali_compile.c`, `tools/build_mesa_arch9.sh` | real Mesa `81daea6`, G77 arch9; token compute, IDVS vertex, fragment with varying load + blend compile and disassemble |
| E presentation | not implemented | CPU scanout path untouched; no fake Metal claim |
| F packaging/diagnostics | `Nord2GPU.cpp` private user client, `tools/package_candidate.sh` | bounded STATUS/ALLOC/RELEASE/SUBMIT_COMPUTE/WAIT/PROBE/RESET; compile-tested; no install |

## Kernel source layout

* `shim/nord2-xnu/nord2_gpu_*.{c,h}` — kernel copies, canonical for the patch
  (`gpu/include` and `gpu/src` are the host-test copies; `check_sync.sh`
  verifies they match).
* `shim/nord2-xnu/Nord2GPU.cpp` — IOKit service + private user client.
* `xnu/patches/0084-nord2-gpu.patch` — new files plus `bsd/conf/files`,
  `iokit/conf/files.arm64`, `iokit/KernelConfigTables.cpp`.
* `tools/mkadt-nord2.py` — adds `mali@13000000` with range-relative reg and
  the five SPI numbers.

## Submission model

* Compute: one 128-byte COMPUTE job in slot 2.
* Render: one 384-byte MALLOC_VERTEX job in slot 0 (arch9 IDVS, as asserted by
  Mesa `pan_jm.c`), one 128-byte FRAGMENT job in slot 1.  The scheduler only
  submits the fragment chain after the vertex/tiler chain reports completion;
  no cross-chain scoreboard dependency is invented.
* Job-slot affinity is programmed from `SHADER_PRESENT_LO/HI` (fallback MP9
  mask only if the register read is zero).
* Job headers use the v9 fields: type bits 1..7, index bits 16..31,
  dependency 1/2 in word 5, next in words 6..7.
* `MALLOC_VERTEX` carries position env at 256 and varying env at 320; the
  DRAW section at 128 carries the fragment shader env at +64.
* The embedded one-shot token uses the exact 128-byte binary produced by the
  compiler test (`0x4e4f5244` store) and a 64-bit FAU address.

## What is intentionally not claimed

* GPU power/clock/regulator control: the MT6893 SCPsys + MT6315/PMIC engine is
  not yet implemented in XNU.  `nord2_gpu=1` without
  `nord2_gpu_assume_powered=1` refuses at the first rail.  The assume-powered
  diagnostic still only reads/programs MMIO, so it must be authorized
  explicitly in the morning.
* Metal frontend and IOSurface presentation are not host-validated; this
  repository contains the descriptor/compiler/kernel contracts they must use,
  not a shipped `Metal.framework` plugin.
* No device GPU ID, compute token, pixel or frame result exists.
