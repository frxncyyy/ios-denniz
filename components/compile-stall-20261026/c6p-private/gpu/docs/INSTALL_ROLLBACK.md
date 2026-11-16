# Installation and rollback plan (future; NOT executed)

No install of any kind was performed tonight.  This is the reversible plan for
the morning, after the parent reviews the artifacts and explicitly authorizes
each step.

## Kernel image

1. Keep the tested audio image
   `nord2-ios-audio-calibrated-playback-stop23.img`
   (`a5b6e6e3…1fb6be41`) and the verified RAM ADB rescue available.
2. Rebuild CI from the baseline plus `0084`, download the kernel, and build the
   exact boot image with `tools/nord2-image-from-kernel.sh`.
3. Record image size/SHA-256.  If the image exceeds 32 MiB, stop; do not flash.
4. Back up the current `boot` partition (verified read-back) before writing.
5. Flash **only** the exact prepared image to `boot` after explicit approval;
   never touch bootloader/recovery/modem/persist/GPT/userdata.
6. Boot with the default args first: GPU service is registered but disabled;
   confirm audio/Wi-Fi/display.  Then, with a second explicit approval, boot
   `nord2_gpu=1 nord2_gpu_assume_powered=1` and capture the SCPsys/GPU-ID lines.

## Metal bundle (not yet built)

The future bundle path is `/System/Library/Extensions/Nord2Metal.bundle`
(the donor's local convention; no Apple path is claimed).  A reversible
installer must:

1. Refuse unless the target is the exact reviewed iOS rootfs and the bundle's
   executable SHA-256 matches the manifest.
2. Move any existing bundle to a timestamped backup directory on the same
   filesystem, never delete it.
3. Copy the new bundle, set ownership/mode, and re-verify the executable and
   `Info.plist` (`MetalPluginName`/`MetalPluginClassName`) bytes.
4. Keep CoreAnimation/Metal disabled by default; activation is an explicit
   boot arg/registry property after a renderer probe passes.
5. Uninstall restores the backup bundle and removes only the new files; it
   must not touch the audio plugin or preferences.
6. Never install while the GPU kernel image is not the reviewed candidate.

Because the frontend is not an iOS bundle and no render path exists, this
installs nothing tonight.
