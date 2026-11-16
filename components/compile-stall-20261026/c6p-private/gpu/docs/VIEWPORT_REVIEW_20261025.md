# Viewport clipping after the first real Metal pixels

## Hardware milestone

Boot1e9d810a/addon2972a959 restores actual rasterisation. All11 matrix variants
produce4096 green pixels; compute64/64,20/25 pixel cases,4 geometry cases and
14 texture roundtrips pass.59 completions,zero faults/timeouts/quarantine.
User confirms normal lockscreen/touch/buttons.409080-byte native USB log:
SHA2569c88011e248ae2ddad4074820dc2ce579e70261d079dec62a82b392d979d5ad0.
Read `gpu-shared-hardware-result.json`. The UI is still using the working
non-Metal path. No full Metal/UI qualification has been claimed.

The five remaining failures are the same viewport case in five targets,
including direct IOSurface. Each has752 incorrect pixels. The driver supplied
viewport scale/offset, but left the raster scissor at the full framebuffer.
For the probe's oversized triangle, viewport(16,16,32,32) produces vertices
(16,48),(80,48),(16,-16). With the configured edge tie rule, framebuffer-only
clipping covers1776 pixel centres instead of1024: exactly752 extra pixels.
This model agrees with the observed count; the corrected hardware result is
still pending.

## Correction and coverage

`nord2_metal_render.c` now intersects viewport, framebuffer and Metal scissor,
then emits inclusive hardware maxima. This follows the pinned Mesa arch9
`panfrost_emit_viewport`. Fully clipped draws emit NULL jobs, preserving the
job dependency chain and fragment CLEAR/LOAD work without underflowing bounds.
Normal and reversed depth bounds are emitted along with their vertex transform.
No kernel, shader bitcode, Info.plist, launchd or audio/Wi-Fi change is required.

`mesa_viewport_oracle.py` extracts the unchanged Mesa function into an ignored
host fixture with minimal state/mock helpers. It compares decoded production
packets for48 combinations: negative/fractional/off-target viewports, explicit
scissors and reversed depth,28 fully clipped. ASan/UBSan checks retain job
ordering and fragment work. The CPU pixel-centre model reproduces the observed
752-pixel difference (the first version incorrectly included the diagonal tie;
correcting that test edge rule gives1776/1024 as expected).

The real native probe is now `batch-viewport-v7`, with50 pixel cases instead
of25: existing tests plus empty CLEAR, negative origin, viewport/scissor
intersection, reversed depth and empty LOAD, across the five target instances.
It retains the eleven tiler variants,compute,geometry and scoped256MiB budget;
19 shared-texture roundtrips are expected. The analyzer requires every new
case exactly once per target occurrence, rejects omissions/duplicates/failures
and still never qualifies the UI automatically. No weakening of the pixel gate.

Host bridge lifetime/concurrency/CLEAR/LOAD/feedback and IOSurface CPU access
checks pass. Strict native frontend,bundle and probe builds pass. The actual
archive was installed, updated, restored and uninstalled in a host fixture;
only the bundle executable and probe change, and launchd is preserved. This
fixture is not a device or HFS result. See gpu/build/viewport-review and
viewport-oracle, and device-viewport-probe-v7/update-rehearsal.json.

## Ready artifact; not installed

Addon6b085a9172a31d8d2c4825a884122c9a849bbc5113f85966c9beb4c98c2dec6d,
8771986bytes: gpu/build/device-viewport-probe-v7/nord2-metal-addon.tar.gz.
Same proven probe kernel1e9d810a55db3bfe8391a86f669bd91f0f64abe98e722213107f723b93ae3bd9.
Private local artifacts only. The earlier device-viewport-v7 package95f5f8d7
contains the old probe and is superseded; do not install it.

Plan/helpers: gpu/build/hardware-20261025/viewport-test. Exact new approval
required before rescue/update. Refresh current device blocks from the latest
local seed tiler-memory-test/offline-update/metal-rootfs.img; never write a
stale rootfs. Verify changed blocks and full device hash, then return manually
to fastboot and boot the same1e9d810a kernel. Capture240seconds and analyze only
latest batch-viewport-v7. Keep UI Metal OFF until the actual pixel gate passes.
Rollback current addon2972a959 and audio/Wi-Fi a5b6e6e3 remain available.
