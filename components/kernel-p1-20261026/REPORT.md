# Kernel P1 (offline prepared, NOT flashed) - 2026-10-26

No phone/adb/fastboot, no publication. Flashed artifacts and gpu/src, shim/, xnu/patches untouched; the stage
(gpu/build/xnu-stage) was edited temporarily to compile and then restored byte-for-byte (`orig/` = pristine copies).

## Files
- `0085-sweet-fb-surface-retention.patch` - xnu series patch (applies after 0084): `iokit/Kernel/arm/SweetFramebuffer.cpp`
  + new host-testable `iokit/Kernel/arm/sweet_fb_pending_surfaces.h`.
- `0086-nord2-gpu-import-budget.patch` - xnu series patch (after 0085/0084): `bsd/dev/nord2_gpu_{core.c,core.h,transport.h}`, `iokit/Kernel/arm/Nord2GPU.cpp`.
- `candidate-gpu-source-0086.patch` - same 0086 change for the candidate source of truth (gpu/include, gpu/src, shim/nord2-xnu, gpu/tests);
  apply from `work/` with `patch -p1`. Keeps tools/check_sync.sh valid. Not applied yet.
- `tests-new/`, `patched-sources/`, `objs/*.o`, `*.compile.log`.
To adopt: copy 0085/0086 into `xnu/patches/` and apply the candidate patch, then regenerate 0084-aware overlay (apply_stage_overlay copies gpu/src).

## 1. Surface retention (0142 model)
SwapEnd (`AppleCLCD::swap`) now retains (`sweet_surface_copy`) the surface of every layer enabled in the mask, queues the refs with the
record under fLock; a newer swap releases the superseded refs (outside the lock). `vsyncFired` takes the refs with the record and releases
them only after `present()` (copy + nord2_display_refresh). `present()` uses the captured ref; if none was resolvable at swap time it falls back
to the old late lookup and releases that ref itself. Error paths: ownership failure (`nord2_display_begin`) returns false and the refs are
re-queued for the retry unless a newer swap superseded them (then released); record alloc failure leaves refs queued; new `stop()` marks
stopped, releases queued refs, later swaps release immediately, and vsync no longer re-arms. Copy-bound checks unchanged (0142's separate
copy-rows helper was not ported; the stage already has bounds checks). Worst case holds 4 surfaces (~45 MB shared with IOSurface, no new copy).

## 2. GPU memory budget
Cause: `nord2_gpu_allocation_size` capped private+imported at 64 MiB, so 3 x 11239424 B display surfaces (33.7 MiB) + pipelines/textures hit
the cap. Now: private cap stays 64 MiB (`NORD2_GPU_PRIVATE_LIMIT`, counted on wired-imported); imported IOSurface pages use a separate
`NORD2_GPU_IMPORT_LIMIT` 128 MiB (11 display surfaces, 4 KiB granularity, `nord2_gpu_import_size`); ctx gets `imported` counter
(maintained in bo_map/teardown). Hard bound kept: ctx `wired_limit` 256 MiB >= 64+128 (static assert in test). Also `importSurface` now rejects `!core`/bad ctx.
ABI/status unchanged; user-visible maxBufferLength (16 MiB) unchanged.

## Checks (all local, -j1, nice 19)
- ARM64 XNU-flag compile (xnu_staged_compile.py, CI clang flags): SweetFramebuffer.cpp, Nord2GPU.cpp, nord2_gpu_core.c -> 0 errors (objs/).
- Host ASan/UBSan: transport test (+import-budget asserts) PASS; core test 15 scenarios (+imported accounting) PASS, original 14 PASS on new code;
  sweet_fb_pending_surfaces_test (capture/mask, supersede, failed-retain, requeue, stop, post-stop swap, zero leaks) PASS.
- Both xnu patches apply cleanly in order against pristine stage copies and reproduce the compiled sources.
- NOT done: full kernel link/pack (link_kernel_local.py / pack_gpu_image.py not run), run_host_tests.sh full suite, any hardware test.

## Risks
- Behavioural, hardware-unverified: refs delay surface free by <= one flush (100 ms) - bounded.
- 0085 context is the CI-tree SweetFramebuffer (patches 0046..0082); regenerate if upstream FB edits land.
- 4 KiB import charge vs old 16 KiB rounding; a surface-import flood is bounded by 128 MiB per client, which is physical memory pinned by IOSurface, not by the GPU driver.
- stop() is untested on real teardown (AppleCLCD is normally never stopped).
