# Morning device-test plan (NOT executed tonight)

Phone is disconnected; no USB/ADB/fastboot/block-device action is authorized
by this document.  The current working audio boot
`nord2-ios-audio-calibrated-playback-stop23.img`
(SHA-256 `a5b6e6e3c86a825e2828018597e687922496f82dbd1ff705113a99db1fb6be41`)
and iOS `userdata` must be preserved.

## Preconditions

1. Parent reviews `MORNING_REPORT.md`, `gpu/PROGRESS.md`, the local commits and
   `gpu/build/host-tests/full.log`.
2. Rebuild CI with `xnu/patches/0084-nord2-gpu.patch` on top of baseline
   `b2ccaed` after `0082`/`0083`.  No public upload; the candidate has no
   remotes.  Use the existing local/GitHub Actions workflow only with explicit
   parent authorization.
3. Confirm the new image size and SHA-256, and that it is at most the 32 MiB
   boot partition budget.  The tested baseline image is 33,542,144 bytes;
   the limit is 33,554,432, so the margin is only 12,288 bytes.  The new
   driver's linked size is not known locally.  If the image exceeds the
   budget, do not flash: compile the new units with `-Os`, remove diagnostic
   strings/tables, or move optional probes out of the boot image; never delete
   rescue/audio/Wi-Fi/display features.
4. Preserve the tested audio image and verified RAM ADB rescue.  Never boot
   stock Android over iOS.

## Stage 1 — read-only identification (no GPU write)

Boot the candidate with `nord2_gpu=1 nord2_gpu_assume_powered=1` ONLY after
explicit authorization.  Before any GPU MMIO, the service reads SCPsys
`PWR_STATUS`/`PWR_STATUS_2ND` (`0x10006000+0x16c/0x170`) and requires all
seven MFG domain bits (0x1fc).  If they are not on it refuses with
`mfg-not-powered` and does not touch GPU registers.  If they are on, it reads
`GPU_ID`, features, present/ready masks and publishes `Nord2GPUState`.
Expected: arch major 9, minor 0.  Capture the exact console lines.  This is a
read-only diagnostic, not a power implementation.

## Stage 2 — compute token

Call the private user client selector 6 (`PROBE`).  It builds a real arch9
COMPUTE job (embedded Mesa-generated binary), submits to slot 2, waits with a
5 s bound, syncs the token page and returns success only when the value is
`0x4e4f5244`.  Host tests prove the descriptor/compiler bytes, not the GPU.

## Stage 3 — fault/rollback

Inject a known bad shader address through `SUBMIT_COMPUTE`; expect a job fault,
slot quarantine, `JS_STATUS`/`MMU` snapshot and no buffer reuse until `RESET`.
A timeout must hard-stop the slot and quarantine; a second tick must not fire.

## Stage 4 — render/presentation

Blocked on the Metal frontend and IOSurface import, which are not complete.
Do not advertise Metal or change CoreAnimation defaults.  The existing CPU
presentation remains the transport.

## Recovery

* Any hang: power-cycle to the tested audio boot or RAM ADB rescue; do not
  flash over `userdata`.
* Preserve `persist`, modem, bootloader, GPT and verified backups.
* Record exact image hash, boot result, `NORD2 GPU` lines, job status/fault
  words and whether the panel still presents the iOS UI.
