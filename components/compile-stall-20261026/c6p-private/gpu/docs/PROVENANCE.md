# Provenance and donor-reuse ledger

All donor material stayed local.  No public commit, push, PR, issue, paste or
CI upload was made.  The candidate checkout has no remotes.

## Read-only references (exact snapshots)

| Reference | Commit | Reuse |
| --- | --- | --- |
| ios-tegu (private) | `7f6f7ca9c58ab840cb1a9c306e7cba3bddd2d572` | frontend/ownership ideas, user-client/buffer patterns, compiler-driver ordering, generic-job bounded-span lessons |
| Mesa | `81daea66703eeea1a232446e3a36902353fc3045` | v9 genxml layouts, JM job-chain rules, arch9 compiler backend, aarch64 MMU mode, gpufreq source is vendor kernel not Mesa |
| metal2vulkan (teg u pin) | `720ef1ce9ae2a2068999a8675717418b9416283e` | translator interface/provenance only; runtime Vulkan backend not used |
| metal2vulkan (latest) | `a2c440b8bcdcf20d34aa1694b61ae22c2a7ea68b` | not used |
| ios-topaz | `e4772911e116ab4ab6c4ac1f38e6106fefe8b483` | IOSurface/accelerator discovery reference only |
| HackiOS kernel/userland | `d82c64a3…` / `a79ff077…` | AIR corpus/ABI reference only |
| Snapintosh/SnapMetal | `2c60b47a…` | ownership/completion patterns; no Adreno/PM4 code |
| reims-vgpu | `2ce32093…` | API/lifetime/failure-contract tests only |
| Android kernel | `04d00c25…` | MT6893 DT, gpufreq order, r32p1 JM/MMU register and descriptor facts |
| ios-sweet baseline | `b2ccaed` | audio/Wi-Fi/display preserved unchanged |

## What was transcribed rather than transplanted

* v9 descriptor field positions: transcribed into explicit 32-bit word setters
  in `gpu/src/nord2_gpu_desc.c`; byte parity is tested against the generated
  Mesa `v9_pack.h` packers, not against donor words.
* JM chain rules: one tiler/vertex chain in slot 0, fragment chain in slot 1
  submitted after completion, based on Mesa `pan_jm.c` and `pan_jc.h`.
* MMU entry format: vendor `mmu/mali_kbase_mmu_mode_aarch64.c`.
* Power order: vendor `mtk_gpufreq_plat.c`; no voltage/frequency values are
  invented and no regulator/SCPSYS writes are attempted when the engine is
  absent.
* Compiler: donor ordering of `pan_preprocess_nir` / descriptor/push lowering /
  `pan_postprocess_nir` / `pan_shader_compile`, but with G77 `gpu_id =
  0x90000000` and arch9 options.  Donor hardcoded `0xb8a21030` and arch11
  options; those were **not** copied as a GPU-ID rename.

## Explicitly rejected from the donor

* CSF/CSG queues, v10 descriptors, 0xb8a21030 GPU ID, arch11 compiler options.
* Tegu GB34 physical aperture limits, S2MPU/ACPM registers, Samsung/Tensor
  power sequencing.
* `patch0223` retained-target L3 cache (donor says finish/release robustness
  is unfinished); it remains OFF and is not imported.
* Donor test assertions that validate Tegu rather than Nord 2.

## Kernel patch

`xnu/patches/0084-nord2-gpu.patch` adds only the new files and three
integration edits.  It is generated from the mutable `gpu/build/xnu-stage`
tree (which carries `0082`/`0083`) and reverse-apply checked.  The patch is
local and must not be pushed to the public fork without review.
