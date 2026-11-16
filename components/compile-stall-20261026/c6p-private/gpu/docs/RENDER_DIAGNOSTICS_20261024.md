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

# Metal render diagnostics after first native compute pass — 24 October 2026

## Hardware evidence

Boot `15143c05f7c7365c9f501aae6b4a693b29297223291eab714755547071cbecc1`
completed Android-derived PLL initialization at 350 MHz, MTCMOS stage 11,
GPU ID `90800011` and mandatory TTRX_3485. Public Metal compute checked 64
words with zero mismatches. The first BGRA8 CLEAR+varying command completed,
but **4096 of 4096 pixels failed**. The installed probe does not print their
values and exits at the first failed comparison. No other format, IOSurface
or accelerated UI is hardware-qualified.

User confirmed lockscreen/touch/buttons, then confirmed the phone still worked
after USB disappeared. Capture ended with a USB error, not a captured panic.
Evidence: `gpu/build/hardware-20261024/android-init-test/`:
`result-analysis.json`, `probe-usb/xnu-live.log`, `probe-usb/result.json`.
408168-byte log SHA-256:
`aeed5c704c182de4404bc67db7c6c4ae9e0f5f5c6f9c8b8987b32d19bb4ae7d2`.
No phone mutations in the diagnostic-preparation continuation.

## Changes and their limits

* The reused frontend rejected getBytes/replaceRegion for every imported
  IOSurface v1. Nord2's existing bounded/locked direct-backing path now runs.
  A host test extracts and executes the actual production Objective-C methods;
  it fails on the previous source, passes after the change, and covers stride,
  subregions, canaries and eight refusal cases under ASan/UBSan. **This does not
  explain the first failure on an ordinary texture.**
* Probe `batch-pixels-v2` verifies CPU shared-texture round trips first, then
  runs CLEAR+varying, independently seeded LOAD+scissor, clear-only, viewport
  and blending/channel-mask for BGRA8, RGBA8, R8, RGBA16F and IOSurface BGRA8.
  Pixel mismatch continues the batch; a failed GPU command or CPU ABI check
  aborts it. Every mismatch records four actual pixel samples. Final status
  remains failure if any comparison fails; no CPU shader substitutes GPU work.
* Opt-in bounded bridge logs capture completed job writebacks, polygon-list,
  vertex-packet and heap allocation values. Normal frontend use has tracing
  disabled. Probe tracing is capped at 64 draws; the kernel's 16 KiB/client
  quota remains unchanged. GPU status reports faults/timeouts/quarantine.
* No speculative change to drawing descriptors or shader code generation.
  Mesa v9 framebuffer, jobs, resource and shader setup were compared, but the
  primary pixel failure is not yet identified. Actual samples/writebacks are
  needed to distinguish empty tiling, shader/color and readback failures.

## Verified artifacts

New private addon:
`gpu/build/device-render-diagnostic-v2/nord2-metal-addon.tar.gz`
SHA-256 `b225a1ddc4cb6cf9b687092a388cd14fb6bbbf33df874784837ded72e5a16e3a`
8762651 bytes.
Bundle SHA-256 `619164d9a3d832ab57cbe259bd7eb2680c8a608352c3fecb10f3f3c4bff673d9`.
Probe SHA-256 `8492ec7ee5b4caf007197dc8e81dc5589e743cfa78b3d954f0a51a64835cd0fa`.
The previous installed addon `dece8311...437ce1c` remains byte-identical.
The earlier preliminary diagnostic package is superseded and must not be used.

Strict ARM64 frontend compile, bundle link and public-API probe build PASS.
Affected bridge tests PASS under ASan/UBSan including CLEAR/LOAD, MRT,
allocation/map/submit/wait failures and 80 concurrent dispatches.
Actual IOSurface method tests PASS. Installer and update tests PASS including
five injected write failures, four tamper refusals, seven delta-receipt refusals
and unknown-device-preimage refusal. No full unchanged-kernel suite rerun.
Evidence: `gpu/build/render-diagnostic-review/`.

## Reversible update rehearsal and next device sequence

The offline updater verifies the old installed receipt, original launchd backup,
every owned file, new payload, and exact path set. It makes verified backups,
journals replacement, verifies final inventory and preserves the original full
uninstall. Restore refuses unexpected edits. The VM wrapper operates only on a
separate local clone. An optional journal replay uses the existing Apple oracle.

The actual HFS+ clone passed update -> restore -> update, before/after fsck,
unchanged launchd database and eight existing runtime/audio files. A catalog comparison covered 328473 old and 328506 updated records: all 38
changed paths belong to the addon or its backup state. No unrelated file fork,
permissions or Finder type changed. Original
16 GiB clone hash remains `10e403da...83b182`. This is a **historical PC rehearsal**,
not current phone contents. The delta committer explicitly refuses rehearsal
receipts. Never write that clone back to the phone.

Exact new addon/device-sequence approval is still REQUIRED by AGENTS.md:

1. Flash only boot with tested RAM ADB rescue `d78bf9fe...3d03bac5`.
2. Refresh the local snapshot by reading only changed 16 MiB chunks from the
   unmounted phone; verify independent full device/local hashes. Reuse local
   unchanged data. Never substitute a historical image for current userdata.
3. Clone that current verified snapshot, replay the HFS journal if needed,
   update addon with exact b225a1dd hash, and verify offline.
4. Commit only changed 1 MiB ranges with preimage and readback checks, followed
   by full device hash verification, using `commit_root_delta.py` with
   `--expected-addon-sha256 b225a1ddc4cb6cf9b687092a388cd14fb6bbbf33df874784837ded72e5a16e3a`.
5. Manual fastboot, then reuse the exact current boot `15143c05...71cbecc1`.
   No new kernel compilation. Collect the entire batch over native USB.

If needed, boot-only fallback is calibrated audio/Wi-Fi `a5b6e6e3...1fb6be41`;
addon update rollback is available in the verified backup. Rescue stays in RAM;
never boot stock Android over iOS userdata. Keep `nord2_metal=0`. Do not flash
old UI image `25ed71a4` (bad ADT and unqualified graphics). Do not publish private
ios-tegu-derived sources/artifacts or send them to public CI.
