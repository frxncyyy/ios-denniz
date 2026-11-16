# Viewport success and correction of the direct IOSurface oracle

## Observed on the phone

Boot `1e9d810a` with addon `6b085a91` completed all 50 pixel comparisons with
zero mismatches, all 11 tiler variants, compute 64/64, four geometry tests and
19 CPU texture roundtrips. There were 104 GPU completions and zero faults,
timeouts or quarantined allocations. The user confirmed lockscreen, touch and
buttons. UI Metal remains disabled. The 240-second capture completed without
transport error or capacity truncation; no collector remains active.

Evidence: `gpu/build/hardware-20261025/viewport-test`. The 418066-byte log has
SHA256 `665de2b5c88295335cf682193577d609a0ad80af9828de4344e01a00d55dfd3e`.
See `viewport-hardware-result.json` for the compact result. The analyzer still
correctly reports **not qualified** because direct IOSurface validation failed.

## Demonstrable probe bug, not a new GPU fix

When extending the probe from five to ten passes, the parent agent left the
final direct IOSurface centre check expecting the old final blended colour:
BGRA approximately `(64,64,143,255)`. The new final empty-LOAD pass deliberately
preserves the independently seeded background `(191,128,64,255)`. Thus even a
perfect IOSurface fails that stale centre check. The existing log does not
contain the actual direct-memory centre value, so this explanation must not
be used to retroactively mark the device qualified.

The corrected `batch-surface-v8` probe checks all 4096 direct IOSurface pixels
after **each** of the ten passes. It uses the current pass's expected image,
validates width, height, stride, allocation and BGRA format, checks Lock and
Unlock returns, and logs the centre word. The existing texture-readback pixel
oracle was factored without changing its expected values or tolerances.
The analyzer requires all ten distinct direct reads, successful locking and
layout validation, and every pixel comparison. Missing, duplicated or failing
results cannot pass. It does not qualify UI Metal automatically.

## Verification and artifact

`probe_surface_test.py` compiles the production pixel/direct-surface functions
under ASan/UBSan with mocked IOSurface storage. Its independently constructed
reference images cover four formats, all ten passes and padded rows, plus
single-pixel corruption, stale blend contents, lock/unlock failures, null base,
bad dimensions, allocation, format and overflow-sized stride: 129 checks pass.
Six analyzer test groups pass, including the new mandatory direct-read gates.
Strict ARM64 probe compilation and actual-archive selective update/restore/
uninstall rehearsal pass. Baseline and source synchronization checks pass.
These are host results; the new direct reads have not run on the phone.

Only `usr/local/libexec/nord2-metal-probe` changes. The bundle, shader bitcode,
Info.plist and kernel are byte-for-byte unchanged from the current phone.
Addon: `gpu/build/device-surface-probe-v8/nord2-metal-addon.tar.gz`, 8772182 bytes,
SHA256 `86e0a04255d87498bcdc16e0e84fc0146190f7da244a06f9afde16becca8421b`.
Probe SHA256 `7d9ede704f9c8f886b55ac4bcafef320168fc17dbd5a4d04020d4b2ce1e38bd4`.
Host receipts: `gpu/build/probe-surface-test` and `device-surface-probe-v8`.

New exact authorization is required before installing this probe. Helpers and
unapproved plan are in `gpu/build/hardware-20261025/surface-test`. Refresh from
the latest local `viewport-test/offline-update/metal-rootfs.img`, verify the
selective update, then return to the same `1e9d810a` probe boot. Keep UI Metal
OFF until actual direct-memory evidence passes. Rollback is current addon
`6b085a91` and/or the audio/Wi-Fi baseline `a5b6e6e3`. Private source stays local.
