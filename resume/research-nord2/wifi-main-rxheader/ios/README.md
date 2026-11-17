# Private main Wi-Fi RX-header diagnostic image stage

This fresh offline template targets `shim/out/nord2-xnu/nord2-ios-wifi-main-rxheader-battery-stop23.img`. The preceding executed TXWRITEBACK image is SHA-256 `a8cc8050f041ef423343576a22c965d8592618061ae030d6ce3111892fb04ad6` and is explicitly rejected by the runner and verifier. No previous logs, results, manifest, payload, image, approval, or device run state are copied.

The TXWRITEBACK hardware record showed TX completion and a copied 36-byte RX frame, then the existing header guard refused with error21/events0. The reviewed, promoted diagnostic source is **captured-only**: it adds `NORD2 WIFI MAIN RXDESC:`, `NORD2 WIFI MAIN RXWORDS:`, and `NORD2 WIFI MAIN RXHEADER:` observations. Its frozen private source manifest is `6d8d00f8b76246b8fe6b02acf9f3f8704847118c65d8858ac186e92342e95013`. No acceptance, rearm, polling, ownership, firmware-acknowledgement or calibration rule is relaxed. The existing 30-second bounded failure drain and TXDESC/TXSUBMIT/TXOBSERVED/TXWB diagnostics remain.

The command line, private calibration ADT input, eight-core UFS2 iOS boot mode and SHA-pinned e0d3/RAM-rescue fallback are unchanged. The distinct runner approval argument is `--authorized-wifi-main-rxheader-calibration-boot`; boot argument remains `nord2wifimain=1`. A new exact-image approval under `boot-with-main-wifi-rxheader-calibration` is required.

The source/schema gate now pins the promoted transport C/header and coordinator to the frozen reviewed candidate. CI identity, prepared-image and authorization gates remain false or null. The schema requires the exact three diagnostic format strings in source and their markers in the loaded kernel. The copied strict success parser is unchanged, and no FW_READY observation is inferred from a copied RX header.

`python3 test-stage.py` checks the bounded offline template. The parent alone reviews promotion, supplies exact source/CI/image identities, seeks new authorization and handles any hardware test. This directory starts no CI, build, flash or device access.
