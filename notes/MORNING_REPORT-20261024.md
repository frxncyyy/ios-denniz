# Superseded by parent continuation

The current implementation is `00dcb4d` on `codex/nord2-metal-complete`.
Read `gpu/metal/README.nord2.md`, `gpu/docs/continuation-receipt.json` and the
current `gpu/progress.json`. Native bundle, full kernel and both boot images
now exist and pass PC checks. Hardware qualification and exact-artifact
approval remain outstanding; no phone operation or GPU result is claimed.
The original morning report below is historical.

> **Morning review supersedes the state below.** Read
> [gpu/docs/MORNING_REVIEW.md](gpu/docs/MORNING_REVIEW.md).
> Candidate branch: `codex/nord2-metal-review`. Critical MMU, compute-packet,
> power-sequence, ABI, lifecycle and ADT defects were corrected and retested.
> The real Metal renderer/presentation and power/clock path remain incomplete;
> no boot image is ready. The overnight tarball is historical and stale.

# Morning report — Nord 2 native Metal overnight

**Date:** 2026-10-24 (Europe/Rome)
**Candidate:** `research/nord2/gpu/overnight-20261024/work`
**Branch:** `deepseek/nord2-metal` (no remotes, local commits only)
**Baseline:** `b2ccaed`
**Main checkout observed:** `43238d7` (two parent docs commits after
`b2ccaed`; `b2ccaed` is an ancestor).  The candidate did not modify main;
its pre-existing untracked `kmod/` was left untouched.
**Device actions:** none. Phone disconnected; no USB, ADB, fastboot, block
device, rootfs mount, flash, reboot or install was attempted. No GPU hardware
success is claimed.

## Bottom line

The offline arch9 backend is implemented and tested: Mesa v9 descriptor byte
parity, 4 KiB GPU MMU over XNU 16 KiB pages, ownership/quarantine scheduling,
MT6893 board/power gating, arch9 JM chain builders, real Mesa arch9 shader
compilation, ARM64 XNU compilation of every new unit, and kernel patch 0084.
A default-off IOKit transport and one-shot compute-token path are compiled but
never executed on hardware.

Two completion-critical layers remain **incomplete**, and the measured size
delta makes the current kernel candidate unable to fit the existing boot
margin without a packaging strategy.  This is not ready for hardware
validation as a complete Metal stack:

1. MT6893 MT6315/PMIC VGPU/VSRAM rails and MFG PLL/clock gates in XNU.
   `nord2_gpu_scpsys.c` now implements the source-derived MFG0..6 MTCMOS
   sequence (including MFG1 bus protection) and is host-tested, but the
   regulator/PMIC and clock drivers do not exist.  The IOKit rail callbacks
   therefore still refuse rather than write guessed values, so power-on never
   reaches the tested MTCMOS sequence.
2. Metal.framework frontend/plugin and Mali presentation.
   The kernel can import a retained `SweetSurface` into a GPU BO (selector 8)
   but there is no userland surface mapping or presentation.
   A small compute-only frontend source (`gpu/metal/Nord2Metal.m`) now covers
   device discovery, buffers, one precompiled compute pipeline, queue/command
   buffer/encoder, submit and wait.  It links as an arm64 iOS Mach-O ABI stub,
   but is not a loadable/installable Metal bundle, does not compile AIR, and
   does not implement render, IOSurface, QuartzCore or scanout.

Consequently there is no GPU-ID, token, triangle, IOSurface fence, pixel or
sustained BackBoard frame result.  Existing CPU presentation, audio and Wi-Fi
were not modified.

## Implemented

| Area | Files | Evidence |
| --- | --- | --- |
| v9 descriptors | `gpu/src/nord2_gpu_desc.c`, `gpu/include/nord2_gpu_desc.h` | byte-for-byte parity with Mesa `81daea6` generated v9 packers for job header, shader env, compute/fragment job, malloc-vertex job, framebuffer, RGB render target, tiler heap/context, buffer + resource table entry, texture, sampler, attribute, shader program, local storage, depth/stencil, blend |
| GPU MMU | `nord2_gpu_mmu.c` | 4-level AArch64-style, 4 KiB leaves, 16 KiB XNU pages split, conflict preflight, table bound, unmap; ASan/UBSan PASS |
| BOs/quarantine | `nord2_gpu_mem.c` | page-list ownership, GPU VA bump allocator, refcounts, cache sync hook, quarantine on unknown completion; ASan/UBSan PASS |
| Scheduler | `nord2_gpu_sched.c` | slots 0/1/2, queue, timeout, hard stop, fault blocking, out-of-order completion refusal, close/reset; ASan/UBSan PASS |
| Board/power | `nord2_gpu_board.c`, `nord2_gpu_scpsys.c` | exact DT validation, source-derived VSRAM→VGPU→MTCMOS→CG order, SCPsys MFG0..6 MTCMOS sequence with bounded polls and MFG1 bus-protect RMW, soft reset, staged rollback; host tests PASS; PMIC rails still refuse |
| Render plan | `nord2_gpu_render_plan.c`, `nord2_gpu_desc.c` + `nord2_gpu_jm.c` | production builder validates stride/FAU/buffers and composes framebuffer/RT/tiler heap/context + vertex/fragment chains for 1125x2436; ASan/UBSan PASS; compiled into patch 0084/ARM64 archive (not yet called by transport) |
| JM chains | `nord2_gpu_jm.c` | arch9 MALLOC_VERTEX (384 B), FRAGMENT (128 B), COMPUTE (128 B), validation/immutability; host tests PASS |
| Compiler | `gpu/tools/nord2_mali_compile.c`, `gpu/tests/air_render_test.py` | Mesa `81daea6`; token compute 128 B (FAU global store), IDVS vertex 1024 B (`idvs=true`, `secondary_enable=true`), fragment 128 B (`LD_VAR` + `BLEND`); disassembled.  AIR render-pipeline oracle: pinned metal2vulkan `720ef1c` translated an authored no-resource AIR-IR vertex + constant-color fragment pair, then arch9 emitted 1920 B vertex / 384 B fragment (host only; not an Apple metallib) |
| XNU patch | `xnu/patches/0084-nord2-gpu.patch` | new C/C++ units incl. render-plan builder + `bsd/conf/files`, `iokit/conf/files.arm64`, `iokit/KernelConfigTables.cpp`; reverse-apply check PASS |
| ADT | `tools/mkadt-nord2.py` | `/arm-io/mali` reg `0x12000000/0x4000` (range-relative), SPIs 362..366, mfgcfg/apmixed |
| Metal compute core | `gpu/metal/nord2_metal.c/.h`, `nord2_metal_host_test.c` | ASan/UBSan state-machine test PASS; exact kernel request, bounds/refcounts, commit/wait |
| Native diagnostic | `gpu/metal/nord2_gpu_client.c` | IOKit STATUS/`--probe` source; build with iOS SDK, not run |
| Metal ObjC shim | `gpu/metal/Nord2Metal.m` | host syntax-checked; arm64 iOS Mach-O ABI/link stub PASS (`build_ios_bundle_stub.sh`); not loadable/installable, no render/IOSurface/presentation |
| IOSurface import | `Nord2GPU.cpp` selector 8, `SweetSurface` page walk | compiles; maps retained surface pages into a non-cacheable BO; presentation not implemented |
| Transport | `shim/nord2-xnu/Nord2GPU.cpp` | IOAcceleratorES subclass, default-off `nord2_gpu=1`, STATUS/ALLOC/RELEASE/SUBMIT_COMPUTE/WAIT/FAULT/PROBE/RESET/IMPORT_SURFACE, AS0 translation/TLB programming, FAU/resource bindings, mapping, wired cap, polling, fault snapshot |
| Packaging | `gpu/tools/package_candidate.sh` | tarball + manifest + SHA256SUMS under `gpu/out/` |

## Exact commands and results

```
NORD2_AIR_LLVM_BIN=/path/to/llvm-15/bin gpu/tools/run_host_tests.sh
  descriptor parity: all PASS
  randomized descriptor parity (2,000 iterations): all PASS
  MMU: all PASS
  randomized MMU model (5,000 ops): all PASS
  buffer/quarantine: all PASS
  scheduler: all PASS
  randomized scheduler (3,000 ops): all PASS
  SCPsys MTCMOS: all PASS
  board/power: all PASS
  JM builders: all PASS
  render-plan composition: all PASS
  production render-plan builder: PASS
  Metal compute core: PASS
  Metal ObjC syntax: PASS
  iOS arm64 bundle ABI/link stub: PASS (not installable)
  arch9 compiler: PASS (software codegen)
  AIR render-pipeline oracle: PASS (optional; needs translator + LLVM 15)
  ARM64 XNU compile of all new units: PASS
  patch reverse-apply: PASS
arch9 compiler summary:
  token 128 B sha256 f63c621c4a25b2325f57d9f29622b22722214daea38c04cebe2b828d973eab52
  vertex 1024 B, idvs true, fau_words 9
  fragment 128 B, outputs 0x10
AIR render-pipeline oracle:
  python3 gpu/tests/air_render_test.py \
    --translator gpu/build/cargo-target/release/metal2vulkan \
    --llvm-bin /path/to/llvm-15/bin
  PASS; vertex SPIR-V 1572 B -> 1920 B Mali (idvs true)
  fragment SPIR-V 5320 B -> 384 B Mali (outputs 0x10)
  vertex sha256 3e2ebfac057b9aa4282309e9d8524ba6171e02f8eef9b9adb794f07a2ae016cc
  fragment sha256 1c6c413495a4644c26b289a2dbb90cf0bcad38df4b2eccfc57d8e44fd8505914
synthetic kernel link size:
  gpu/tools/measure_kernel_delta.sh
  baseline 17,849,472 B; +GPU 17,914,960 B; delta 65,488 B
  (all-load ld64.lld relink of the CI archive; not a bootable image)
```

Mesa build: pinned `81daea66703eeea1a232446e3a36902353fc3045`; G77 ID
`0x90000000`; tool `gpu/build/mesa-build/src/panfrost/clc/nord2_mali_compile`.

XNU compile: cached CI command line from the audio build, mutable stage under
`gpu/build/xnu-stage`, objects in `gpu/build/objs`, partial archive
`gpu/build/libnord2gpu.a`.  Full kernel link could not be produced on this
Linux host (requires the macOS XNU/KDK link environment); exact inputs are the
patch plus the compile script and build tree.

AIR bridge: the pinned `metal2vulkan` translator
(`720ef1ce9ae2a2068999a8675717418b9416283e`) built with its `cli` feature in
7m43s.  `gpu/tests/air_render_test.py` assembles the public no-resource
`vertex_amplification_id_layer.ll` and `fragment_constant_color.ll` fixtures
(runtime copies; no Apple artifact is committed), links them, translates the
render pipeline through the pinned translator, and feeds both SPIR-V modules to
the real Mesa arch9 compiler.  Result: translator PASS; vertex 1,572 B SPIR-V
-> 1,920 B Mali (`idvs=true`, `fau_words=9`); fragment 5,320 B SPIR-V -> 384 B
Mali (`outputs_written=0x10`, `fau_words=17`).  This is a host codegen oracle,
not GPU execution.

Native iOS bundle build: an arm64 Mach-O **ABI/link stub** is now produced by
`gpu/tools/build_ios_bundle_stub.sh` using Clang's builtin freestanding headers
and a minimal `Info.plist`; `file` reports
`Mach-O 64-bit arm64 bundle`, and `_n2m_device_init` /
`_OBJC_CLASS_$_Nord2MetalDevice` are present.  This is **not installable**: the
host has no iPhoneOS SDK or Metal/Foundation headers, the link uses
`-undefined dynamic_lookup`, and the shim declares its own Objective-C/Metal
surface.  It proves target/ABI/link only; no bundle load or Metal runtime claim.

## Artifacts

* `xnu/patches/0084-nord2-gpu.patch` (SHA-256 `fa8f51a266496ba067db7e306ee98ccc7682cd98136e38f9c7de97e7c6da0d4b`)
* `gpu/out/nord2-gpu-candidate.tar.gz` + sibling `.sha256` (authoritative hash)
* candidate HEAD: see `git log` (final documentation commits follow the code)
* `gpu/build/host-tests/full.log`
* `gpu/build/compiler-test/{summary.json,token.bin,token.dis,fullscreen-*}`
* `gpu/build/air-test/result.log`, `gpu/build/air-test/render.{vert,frag}.{spv,bin,json,dis}`
* `gpu/tests/air_render_test.py`, `gpu/docs/AIR_BRIDGE.md`
* `gpu/tools/build_ios_bundle_stub.sh`, `gpu/docs/IOS_BUNDLE_STUB.md`
* `gpu/tools/measure_kernel_delta.sh`, `gpu/build/kernel-link/delta.log`
* `gpu/build/libnord2gpu.a`
* `gpu/PROGRESS.md`, `gpu/progress.json`, `gpu/docs/*.md`

## Failures / corrections during the night

* Initial descriptor packers had layout mistakes (compute task axis,
  framebuffer tie-break, RT swizzle); the fixed examples corrected them.
* Randomized 2,000-iteration parity then found two more real bugs: the
  framebuffer sample-pattern field was always 0 for 4x MSAA, and the texture
  descriptor wrote `levels-1` into maximum LOD.  Both are fixed and the fuzz
  passes.
* The MMU table registry initially pointed at caller stack ops; ops are copied
  by value and the table registry is allocated through a host hook.
* The first XNU C++ syntax pass used the wrong include order (`bsd` before
  `libkern`) and a freestanding-Clang builtin gap; both were fixed in the
  staged compile script.  The final unit compiles with the cached flags.
* `probeToken` was initially a stub; it now builds a real COMPUTE job from the
  embedded Mesa token and waits 5 s.  It has not run on hardware.
* Late audits fixed three path-critical issues: AS registers need the
  `0x2000` MMU base and AS0 was never programmed (now TRANSTAB/MEMATTR/
  TRANSCFG + UPDATE + FLUSH_PT); a shader at offset zero was wrongly rejected;
  and user-client buffer references were double-released/the BO object leaked
  on quarantine drain.  All are fixed and recompiled; none is hardware-tested.

## What must happen before any hardware claim

1. Review the candidate and `0084`; rebuild CI without pushing private code.
   Confirm the image does not exceed the 32 MiB boot partition (baseline is
   only 12 KiB below it) and preserve the tested audio image/userdata.
2. Resolve power: either implement SCPsys/MTCMOS + PMIC rails from source or
   explicitly authorize the `nord2_gpu_assume_powered=1` read-only probe.
   It checks SCPsys MFG PWR_STATUS first and refuses without GPU MMIO if the
   domains are off.  Capture GPU ID/features/status.
3. Only then run `PROBE` (compute token), fault/timeout/quarantine tests and a
   render/presentation path.
4. Keep CPU scanout, audio and Wi-Fi as the working transport until the Metal
   renderer probe passes.

## Boot partition budget risk

The tested audio image is 33,542,144 bytes; the 32 MiB boot partition is
33,554,432 bytes, leaving only **12,288 bytes**.  The new driver is not in that
image.  Object sections estimate **29,836 bytes text + 11,777 bytes
const/strings (~40.6 KiB)** at `-O2`, and 36,458 bytes (35.6 KiB) with
`-Os`; see
`gpu/docs/BOOT_BUDGET.md`.  In addition, a synthetic `ld64.lld` relink of the
preserved CI all-object archive measured **65,488 bytes** larger with the GPU
archive force-loaded (dead-strip baseline 17,849,472 -> 17,914,960 bytes).  That output is
not the CI kernel/boot image, but both sides use the same archive and flags, so
the delta is real.  It is about five times the remaining boot margin: applying 65,488 B to the
tested 33,542,144 B image projects 33,607,632 B, 53,200 B over the 32 MiB
limit.  A separate `-Os` rebuild
measured 49,792 bytes, so `-Os` alone does not fit.  The
morning CI build must still produce and measure the exact kernel/boot image
before any flash.  If it is over, remove diagnostic strings/tables, split the
private transport, or move optional probes out of the boot image; `-Os` alone
was measured above and is insufficient.  Do not silently delete rescue/audio/
Wi-Fi/display code and do not flash an oversized image.

**This is not `READY_FOR_REVIEW`: the PMIC rail/clock path, Metal
frontend/presentation, exact CI kernel/boot-image measurement and all device
results are missing.  The synthetic 65,488-byte link delta and 12,288-byte
margin already show size must be solved before a flash.**
