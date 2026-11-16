# Nord2 Metal qualification and recovery

This is a private, untested-on-device candidate. Do not upload it or the Tegu
source to public CI. Native code and successful PC tests do not establish
working Metal hardware or UI acceleration.

## Artifacts

- `gpu/build/device-candidate/nord2-ios-metal-probe-stop23.img`: GPU enabled,
  system Metal registration off. Existing UI stays on its current path. The
  addon launches a separate public-Metal-API test once each boot.
- `gpu/build/device-candidate/nord2-metal-addon.tar.gz`: actual arm64 bundle,
  compiler libraries statically linked, probe, authored AIR, manifest and
  offline installer. Exact digests are in the adjacent receipts.
- `gpu/build/device-ui-candidate/nord2-ios-metal-ui-stop23.img`: same kernel,
  `nord2_metal=1` publishes the Metal plugin for iOS. Only test after readback
  qualification succeeds; this image is not a proof that backboardd works.
- Preserve working audio/Wi-Fi image `nord2-ios-audio-calibrated-playback-stop23.img`,
  SHA256 `a5b6e6e3c86a825e2828018597e687922496f82dbd1ff705113a99db1fb6be41`.
  RAM ADB rescue `d78bf9fe…3d03bac5` remains the established recovery route.
  Never boot stock Android over the iOS userdata filesystem.

## Offline addon preparation

No phone commands are part of the build or installer. First obtain explicit
approval for the exact boot image and the addon/rollback sequence. This sequence
was approved on 24 October (call_85b549e707694654a0b968af3fb3933f:0), conditional
UI boot included. The deployed rootfs has changed since the original install:
never flash the old local v1 image. In the tested ADB rescue, with userdata
unmounted, save and hash a complete current image of the HFS volume and preserve
it. `snapshot_current_root.py` can seed a separate clone from the old local v1
image, download only mismatching 16 MiB chunks and then require independent
whole-device SHA-256 equality. This avoids copying unchanged bytes over USB;
the historical seed remains untouched. The historical
volume is 17.2 GB; derive its current size from the checked volume header.

Work on a separate clone in the existing HFS-capable Linux guest. Check/repair
that clone offline as needed, mount it read/write as guest root, and extract the
addon outside its mount. Run:

```
python3 stage_metal_root.py apply /mounted/offline-clone --payload ./payload
```

This adds only the native bundle, the test executable, authored AIR and one
job in `/System/Library/xpc/launchd.plist`. It preserves all existing jobs and
backs up the original plist under `/private/var/db/nord2-metal/`. No existing
plugin path is overwritten. Unmount cleanly; hash the resulting volume and
save its changed-block manifest and before/after hashes. Return only verified
changed ranges while still in rescue, checking each original preimage before
writing and each resulting range after writing. Keep both full images until
hardware validation and cleanup finish. This transfer step is still a device
operation, not authorized by preparing these artifacts.

## Device qualification

1. Flash the exact approved **probe** boot image. Collect USB logs before reboot.
   Driver power/ID failures remain visible even if the Metal bundle cannot load.
2. The probe reports via a bounded IOKit diagnostic selector into kernel USB
   logs (`NORD2 GPU USER:`); full stdout/stderr including compiler messages is
   also kept at `/private/var/db/nord2-metal/probe.log`.
3. Require compute readback (64 words), 4096-pixel checks per pass for R8,
   RGBA8, BGRA8 and RGBA16F, CLEAR/varying, LOAD/scissor, clear-only, viewport,
   blending/channel masks, plus imported IOSurface/direct backing. Any failed
   command or mismatching pixel is a failed qualification, not a success.
4. If all succeed and the user authorizes the exact **UI** image, register the
   plugin and verify actual backboardd draw submissions, on-screen frames,
   interaction, display off/on, Wi-Fi and audio. Do not infer Metal UI from a
   visible lockscreen alone: the previous UI already worked without this GPU.
5. Hardware faults, colors, timing and API requests may require more work.
   The current backend is a bounded UI/compute implementation, not full Metal
   conformance (depth/stencil, MSAA, indirect execution and later Apple GPU
   families are not implemented). Live pixel results remain essential.

## Recovery

On failure, restore the exact known working iOS boot or use the tested RAM ADB
rescue, within the approval already given for that test. With system registration
off, the addon cannot replace the working UI renderer. To remove it, use a fresh
current offline clone, not an old backup that discards intervening iOS data:

```
python3 stage_metal_root.py rollback /mounted/offline-clone
```

Rollback verifies all owned hashes, rejects unrelated changes, restores the
original launchd bytes and removes only the addon files. Preserve its receipt
and probe log. Unmount, verify and return the changed ranges as above. No
bootloader, modem, persist, recovery or GPT changes are needed.
