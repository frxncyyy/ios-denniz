# Private main Wi-Fi RX-format image stage

This fresh offline template targets `shim/out/nord2-xnu/nord2-ios-wifi-main-rxformat-battery-stop23.img`. The preceding executed RXHEADER image SHA-256 is `0a4b7ade57f35b2cd1932696f3e3bf7ccd10ab0685d7e69b757db0f19996b13b`; the new verifier and runner reject that exact image. No previous logs, results, manifest, payload, approval, image, or device run state is copied.

The RXHEADER hardware report captured actual type `0x0007`, EID1 and sequence1, while the current strict guard expected type `0xe000`. The reviewed source correction accepts INIT type `0x0007` or `0xe000` with EID, sequence and status checks retained; it updates PHY parser admission and treats CID as captured metadata under the selected source policy. Its frozen manifest is `e174323a5dd5605c3a8ea7569e34ebd29e733fecb5e493de432daa7dbf89901d`, and all four promoted canonical hashes match it. The stage asserts no hardware outcome from the correction. It preserves the existing RXDESC/RXWORDS/RXHEADER and TXDESC/TXSUBMIT/TXOBSERVED/TXWB diagnostics, the 30-second bounded failure drain, and real RX/EID/sequence/status gates. A copied RX frame is not FW_READY.

The command line, private calibration ADT, eight-core UFS2 iOS mode, and SHA-pinned e0d3/RAM-rescue fallback are unchanged. The new runner argument is `--authorized-wifi-main-rxformat-calibration-boot`; the boot argument remains `nord2wifimain=1`. New exact approval under `boot-with-main-wifi-rxformat-calibration` is required.

The source/schema gate pins the exact four reviewed promoted files: transport C, PHY force C, layout C and native WFDMA binding. No header or coordinator changed, and no new diagnostic marker was added. CI identity, prepared-image and authorization gates remain false or null. The copied success parser remains byte-identical. No additional runtime marker is required until a frozen candidate actually emits one.

`python3 test-stage.py` checks this offline template. The parent alone reviews source, supplies exact CI/image identities, seeks separate authorization and handles any device test. This directory starts no CI, build, flash or device access.
