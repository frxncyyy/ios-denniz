# Private SWINT main Wi-Fi image stage

This fresh **offline template** targets
`shim/out/nord2-xnu/nord2-ios-wifi-main-swint-battery-stop23.img`.
The executed ownerdiag image SHA-256
`e011b66da1b49367f22dfb1d01cedc7712f306f98673752c46c05fa9c08fb4b7`
is explicitly rejected by the new verifier and runner. No old image,
payload, manifest, logs, result, run state or authorization was copied.

The intended runtime delta is narrow late-CID41 ENA29 handling with
fail-stop behavior on an actual pending software interrupt. The frozen
private candidate `mcu-swint-enable-v2/manifest.json` is pinned at
`fbb8a9cf5b791d8cd3674c85bff59eee259a23963fe985251bb5c40be57672d8`:
only `wifi_main_wfdma_bind.inc` changes, with no new header, coordinator,
ABI or marker. Its independent review and canonical promotion are pending.
`native-schema-required.json` keeps `parent_verified=false`, even though it
now records the exact expected candidate and current-binder SHA. Historical
ownerdiag review evidence remains separate and cannot satisfy the SWINT
source gate. The parent must verify the review and canonical promotion before
opening it. This template does not infer firmware acknowledgment or a new
diagnostic marker from the ENA29 change.

The command line and private calibration ADT injection remain identical to
the executed ownerdiag image: eight-core UFS2 iOS, `nord2wifimain=1`, the
30-second bounded failure drain, the SHA-pinned e0d3 fallback and the
RAM-rescue recovery path. The runner has a new
`--authorized-wifi-main-swint-calibration-boot` argument and exact
`boot-with-main-wifi-swint-calibration` scope. CI identity, native schema,
prepared image, human, RF-calibration and conditional-recovery gates are all
false or null. The old human approval is not reusable.

Run `python3 research/nord2/wifi-main-swint/ios/test-stage.py` for bounded
offline checks. It verifies the closed gates, copied parser hashes, unchanged
command line, new image/scope and exact rejection of the executed image.
Both `verify-image.py --preflight` and the new runner refuse at the missing
CI identity before any build or device action. This directory does not
build, flash, run CI or inspect a device.
