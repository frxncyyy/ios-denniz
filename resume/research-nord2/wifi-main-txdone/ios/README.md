# Private main Wi-Fi TX descriptor image stage

This fresh offline template targets `shim/out/nord2-xnu/nord2-ios-wifi-main-txdone-battery-stop23.img`. The preceding executed drain image is SHA-256 `ebfa71f63e4218a2613b6e9734c63bbcbf5c89fbc0afa418e22894cbb31a1cfd` and is explicitly rejected by the new runner and verifier. No previous logs, results, payload, image, manifest, approval or run state are copied.

The intended runtime delta is a source-reviewed DDONE-before-echo check order, with strict post-DDONE descriptor validation and captured `NORD2 WIFI MAIN TXDESC:` diagnostics. The existing 30-second bounded failure drain and PRESPAN/PREORDER diagnostics remain. The exact transport, header, and coordinator source hashes, telemetry grammar, CI identity, image identity and outcome are pending. This template does not assert that the source change is implemented or that main firmware succeeded.

The command line, private calibration ADT input, eight-core UFS2 iOS boot mode, and SHA-pinned e0d3/RAM-rescue fallback remain the same. The new flag is only the **runner approval argument** `--authorized-wifi-main-txdone-calibration-boot`; the boot argument stays `nord2wifimain=1`. The new approval scope is `boot-with-main-wifi-txdone-calibration`. Previous user authorization cannot be reused.

All CI/source/schema, prepared-image and exact-authorization gates start false or null. The schema requires PRESPAN, PREORDER, DRAIN and TXDESC markers in the loaded kernel and pins the new transport C, header and coordinator sources before preflight can pass. The copied success/evidence parser is unchanged; a failure or truncated record remains unknown until actual descriptor telemetry is reviewed.

`python3 test-stage.py` checks these offline guards. The parent alone reviews new source and telemetry, supplies CI and image identities, seeks exact approval and runs any hardware test. This directory performs no CI, image build, flashing or device access.
