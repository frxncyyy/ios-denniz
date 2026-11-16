# Geometry addon installed and verified; waiting for manual fastboot — 24 October 2026

Exact authorized addon80b181dd is installed.77MiB written and read back; full
phone rootfs hash equals verified offline clone:
c141110c70465ea1c3602e7a130e6197d90c4e98943a6bfe0ff5573d83e9a4c3.
Current snapshot66ce5ec2 preserved;1616MiB refreshed from local unchanged seed.
Journal replay matches Apple oracle; HFS checks and catalog preservation pass:
328694 old/328710 new records,21 addon/backup-only changes.
Evidence: gpu/build/hardware-20261024/geometry-test/device-install/verified.json.
No VM/transfer remains active. Phone in RAM ADB rescue; asked user to enter
fastboot manually. Then run already-authorized same15143c05 probe using:
 python3 gpu/build/hardware-20261024/geometry-test/flash_geometry.py probe --evidence gpu/build/hardware-20261024/geometry-test
Start native USB capture and analyze only latest boot with
 gpu/tools/analyze_metal_probe.py (expects batch-geometry-v3).
UI Metal remains OFF; rendering still unqualified. Recoverya5b6e6e3 and
addon rollback authorized if required. Never boot stock Android.

# Metal render batch collected; next geometry addon verified — 24 October 2026

Current phone: boot 15143c05 with installed addon b225a1dd; UI Metal OFF.
User confirmed lockscreen/touch/buttons ("tutto ok"). No device changes in
this continuation. Capture completed with USB disconnect; no panic captured.
The health confirmation preceded that disconnect, so later health is unknown.

Latest log: render-diagnostic-test/probe-usb/xnu-live.log, 404303 bytes,
SHA-256 38d329e12b1b99ecac677073e09bc5eb547b52df29cec8264321ae6b141e0d27.
Compute: 64/64 correct. BGRA clear-only: PASS. Draws leave clear/seed pixels
unchanged (4096/1024 mismatches depending on covered region). Nine completed
jobs, zero faults/timeouts/quarantine at last status. First RGBA draw then
fails in software: generic_plan admits only BGRA, despite encoder support.
Neither rendering nor IOSurface/UI is hardware-qualified.

The RGBA refusal was reproduced with actual AIR/Mesa/Objective-C frontend/JM
adapter on PC. Single-target admission now matches R8/RGBA8/BGRA8/RGBA16F;
MRT remains BGRA-only. Twenty format pass sequences and generated-quad
DontCare/LOAD pass under ASan/UBSan, with ONLY raw GPU execution mocked.
This fixes a proven software gate, not the missing hardware raster output.

Next private addon: gpu/build/device-geometry-v3/nord2-metal-addon.tar.gz
SHA-256 80b181dd6f30012261bd4c6084e67785eb2bd2dedc2910a9c53ea16daa3e8239
8765974 bytes. Not installed or approved yet. Strict native AIR/frontend/
bundle/probe builds pass. Adds four geometry cases (attribute triangle,
in-bounds attribute quad, vertex_id quad, vertex_id quad+LOAD), then the
existing 25 pixel cases. Bounded completed-heap readback covers first four
draws only; no speculative pointers/MMIO. Host bounds/failure tests pass.
The vertex_id shader uses a smooth gradient: a constant output folded to
unsupported flat interpolation in the first host test and was corrected
before packaging. Pixel oracle accounts for Metal viewport Y inversion.

Actual old/new payload update -> restore -> original uninstall passes on a
host directory fixture; launchd preserved and only three payload files change.
No new HFS rehearsal or device test claimed. The proven HFS transaction remains
available; a fresh current-device snapshot must precede any next rootfs write.
Reuse local unchanged chunks; never transfer a historical rehearsal clone.

Next: exact-addon sequence approval, tested RAM ADB rescue d78bf9fe,
fresh snapshot/delta validation, reversible addon update, same probe boot
15143c05, full USB batch collection. Keep UI Metal OFF. Recovery a5b6e6e3 and
addon rollback available; never boot stock Android. No public upload.
Read gpu/docs/RENDER_DIAGNOSTICS_20261024.md and gpu/progress.json.
Earlier entries below are historical checkpoints.

# Native Metal compute passes; batch render diagnostic ready — 24 October 2026

Phone remains on healthy boot 15143c05; user confirmed it still works after USB
disconnected. Native Metal compute: 64/64 correct. First BGRA8 render: all 4096
pixels incorrect. Actual values absent from installed probe; other formats,
IOSurface and UI unqualified. No phone changes in this continuation.

New addon b225a1dd...5a16e3a corrects a separate proven IOSurface CPU-access
refusal and collects batched actual pixels/job writebacks. It does NOT yet fix
the first render mismatch. Strict ARM64 build/link and affected host tests pass.
Actual HFS+ clone update/restore/reapply and fsck pass; baseline audio/runtime
files and launchd preserved. Historical rehearsal clone MUST NOT go to phone.

Read gpu/docs/RENDER_DIAGNOSTICS_20261024.md for exact hashes and update sequence.
Next: exact new sequence approval, RAM rescue, refresh current snapshot using
local unchanged chunks, reversible addon update, checked delta writes, then
reuse current kernel 15143c05. UI Metal stays disabled. Fallback a5b6e6e3 and
RAM rescue d78bf9fe verified. No public push/CI. No active USB collector or VM.

# First native Metal compute succeeds; rendering still fails — 24 October 2026

Current boot15143c05, implementationcdbdc00. Android-derived PLL normalization
reached350MHz, MTCMOS stage11, raw GPU ID90800011. Mandatory TTRX_3485 completed;
normal GPU core ready. Native public-Metal compute checked64 words, zero
mismatches. First BGRA8 CLEAR+varying render command completed but all4096 pixels
mismatch. Installed probe does not report actual pixel values and exits here:
other formats, IOSurface and UI remain UNVERIFIED. Do not enable Metal UI.
User confirmed lockscreen/touch/buttons healthy (call_382fd33444fc4f14924313fd2d24a95a:0).
No new flash/rootfs mutation in this continuation; leave healthy phone running.
Evidence: gpu/build/hardware-20261024/android-init-test/. Collector still active;
final receipt pending. Next: compare actual render path to Mesa arch9, add
independent batched pixel/clear diagnostics before another authorized test.

# Native candidate ready for hardware qualification — 24 October 2026

See `metal/README.nord2.md` for implemented scope and remaining limits,
`docs/METAL_DEVICE_PLAN.md` for installation/rollback, and
`docs/continuation-receipt.json` for exact build/test identities. Native AIR,
Mesa, Metal bundle, full kernel and boot images are now built. The PC release
suite and 28 QuartzCore compiler cases pass. No phone test or accelerated UI
result exists yet. Older reports below describe superseded checkpoints.

# Parent continuation — 24 October 2026

See `metal/README.nord2.md`, `docs/continuation-receipt.json` and the top of
NOTES.md. All final offline tests pass; no hardware validation and no complete
Metal renderer/bundle/boot image yet. The original overnight report below is
historical, not the current candidate state.

# Authoritative morning state — 2026-10-24

Parent review completed after the 08:00 deadline. Continue from
`codex/nord2-metal-review` and [docs/MORNING_REVIEW.md](docs/MORNING_REVIEW.md).
All final host tests and nine ARM64 unit compiles pass, including the AIR
pipeline. New regression tests exposed/corrected several hardware-format bugs.
Full Metal render/presentation, usable power/clock control, safe cache/TLB/DMA
lifetime, isolation and full image packaging remain unfinished. No device
operations. Do not follow the older next-action/flash sequence below.
The old archive in `out/` is not the current candidate.

---

# Nord 2 GPU overnight progress

Started 2026-10-24 01:18 Europe/Rome; candidate
`research/nord2/gpu/overnight-20261024/work`; branch `deepseek/nord2-metal`;
baseline `b2ccaed`.  No device access, no USB/ADB/fastboot, no rootfs install.

## Done

1. **Arch9 descriptor backend** (`gpu/src/nord2_gpu_desc.c`,
   `gpu/include/nord2_gpu_desc.h`).  Explicit 32-bit-word packers for v9 job
   headers, compute/fragment jobs, malloc-vertex job, shader environment,
   framebuffer, RGB render target, tiler heap/context, buffer/resource entries,
   texture, sampler, attribute, shader program, local storage, depth/stencil
   and blend.  Fixed and randomized byte parity against Mesa `81daea6`
   generated `v9_pack.h` passes.
2. **MMU** (`nord2_gpu_mmu.c`): 4-level AArch64-style table, 4 KiB leaves over
   XNU 16 KiB pages, conflict preflight, bounded table registry, unmap,
   teardown, AS fault decode.  ASan/UBSan fixed and 5,000-op model tests pass.
3. **Buffer/quarantine and scheduler** (`nord2_gpu_mem.c`,
   `nord2_gpu_sched.c`): refcounted page lists, owner release, VA allocation,
   quarantine on timeout/fault/close, reset/close, out-of-order refusal, JMF
   fault decode.  Fixed and 3,000-op randomized tests pass.
4. **Board/SCPsys** (`nord2_gpu_board.c`, `nord2_gpu_scpsys.c`): exact MT6893
   DT validation, source-derived rail/MTCMOS/CG order, MFG0..6 MTCMOS with
   MFG1 bus protect, bounded polls, soft reset, staged rollback.  Host tests
   pass; PMIC rails/clock gates still refuse.
5. **Render-plan composition** (`nord2_gpu_render_plan_test.c`): framebuffer,
   RGB target, tiler heap/context and both JM chains for 1125x2436 compose and
   validate.  Host test PASS.  `nord2_gpu_render_plan.c` exposes the same
   composition as a production builder with stride/FAU/buffer validation;
   ASan/UBSan module test passes; the unit is in patch 0084 and the ARM64
   archive, but the transport does not call it yet.
6. **JM chain builders** (`nord2_gpu_jm.c`): compute (slot 2), arch9
   MALLOC_VERTEX (slot 0), FRAGMENT (slot 1), validation and immutability.
   Host tests PASS.
7. **Arch9 Mesa compiler** (`gpu/tools/nord2_mali_compile.c`): real
   `spirv_to_nir` Vulkan + `pan_preprocess_nir`/postprocess + compile.
   Token compute (128 B, FAU store), IDVS vertex (1024 B), fragment (128 B,
   `LD_VAR` + `BLEND`) compile/disassemble.  Negative tests pass.
   `gpu/tests/air_render_test.py` translates a public AIR render-pipeline pair
   through pinned metal2vulkan `720ef1c` and compiles both stages: vertex
   1,572 B SPIR-V -> 1,920 B Mali (`idvs=true`), fragment 5,320 B -> 384 B
   (`outputs_written=0x10`).  Host codegen only.
8. **XNU integration**: `xnu/patches/0084-nord2-gpu.patch` adds the C core,
   `iokit/Kernel/arm/Nord2GPU.cpp`, conf entries and IOKit match.  All new
   units compile for arm64-apple-macos14; `build/libnord2gpu.a` is the partial
   archive.  Patch reverse-applies.
9. **Transport** (`Nord2GPU.cpp`): IOAcceleratorES service, default-off
   `nord2_gpu=1`, SCPsys PWR_STATUS gate for assume-powered, AS0
   translation/TLB programming, STATUS/ALLOC/RELEASE/SUBMIT_COMPUTE/WAIT/
   FAULT/PROBE/RESET/IMPORT_SURFACE, mapping, wired cap, polling, quarantine.
   `PROBE` builds a real compute job from the embedded Mesa token.
10. **Metal compute core** (`gpu/metal/nord2_metal.c/.h`): tested
    device/buffer/pipeline/queue/command/encoder state machine, exact kernel
    request, resource/FAU bindings, commit/wait.  `Nord2Metal.m` is a
    source-only ObjC shim over it; host syntax-checked, not an iOS bundle.
11. **ADT and packaging**: `/arm-io/mali` ADT node/test; manifest, SHA256SUMS,
    candidate tarball and verifier under `gpu/out/`.
12. **iOS bundle ABI/link stub**: real shim/core compiled for
    `arm64-apple-ios16.0` and linked as a Mach-O bundle with the expected
    class/core symbols.  Not loadable (no SDK/Metal runtime, dynamic lookup);
    documented in `gpu/docs/IOS_BUNDLE_STUB.md`.

## Not done / blockers

* MT6893 SCPsys MFG0..6 MTCMOS is implemented and host-tested, but the
  MT6315/PMIC VGPU/VSRAM rails and MFG PLL/clock gates are not.  `nord2_gpu=1`
  therefore refuses at the first rail before MTCMOS is reached;
  `nord2_gpu_assume_powered=1` is an explicit diagnostic that still needs
  morning authorization and may fault if the GPU is not powered.
* `gpu/metal/Nord2Metal.m` is a source-only compute ObjC shim; the tested
  logic is `gpu/metal/nord2_metal.c`.  It is not an iOS bundle, not installed,
  and does not implement render/IOSurface/QuartzCore.  No Metal claim.
* IOSurface import, render/presentation path, panel scanout from Mali pixels
  and sustained BackBoard frames are not implemented/validated.
* Full kernel link/CI image and all device tests are absent by instruction.
  A synthetic `ld64.lld` relink of the preserved CI object archive with
  `build/libnord2gpu.a` force-loaded measured a **65,488-byte delta** against
  the same baseline; this is not a boot image, but it exceeds the 12,288-byte
  boot margin.  The same link with a `-Os` rebuild measured 49,792 bytes, so
  `-Os` alone is not enough; a packaging/size strategy is required before any
  flash.
* No iPhoneOS SDK is present on this host.  The shim now compiles and links as
  an arm64 iOS Mach-O **ABI stub** with builtin headers and
  `-undefined dynamic_lookup`, but it is not loadable/installable and has no
  Metal.framework/Foundation runtime.
* The pinned AIR frontend (`metal2vulkan-air`) builds and the translator passes
  `air_render_test.py` for the public no-resource pair; that is host codegen,
  not a hardware shader run.

## Last commands

```
gpu/tools/run_host_tests.sh              # PASS, all software tests
gpu/tools/check_arch9_contract.sh        # PASS, no CSF/arch11/v10
python3 gpu/tests/air_render_test.py ... # PASS, AIR -> SPIR-V -> arch9
gpu/tools/measure_kernel_delta.sh        # 65,488-byte synthetic delta
gpu/tools/package_candidate.sh           # tarball + manifest
```

## Next exact action

Morning parent review, then:
1. Decide whether to implement the SCPsys/PMIC power path (requires new XNU
   regulator/clock code and source-derived register values) or use a
   user-authorized assume-powered read-only GPU-ID probe first.
2. If approved, rebuild the kernel with patch 0084, verify size/SHA256 and
   boot only with explicit authorization; run the `PROBE` selector and capture
   GPU ID/status/fault lines.
3. Only after that, port the Metal frontend and IOSurface presentation; keep
   the current CPU scanout and audio image intact.
