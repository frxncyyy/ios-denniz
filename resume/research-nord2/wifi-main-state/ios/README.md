# Private main Wi-Fi state-repair image stage

This is a new, unprepared offline stage for
`shim/out/nord2-xnu/nord2-ios-wifi-main-state-battery-stop23.img`. The
preceding map-repair image
`b60dd9bc3935d5e30ac7ef740310e41d5198752fb6d4ce244c5a0617b58065b4`
reached main-Wi-Fi stage 3 with the pool mapping and WFDMA initializer
complete, then boot-looped before the first firmware span. The saved capture
does not expose the exact pre-entry refusal reason. A private source candidate
will revise the SW_SYNC readiness check according to the selected source and
add captured reason diagnostics. Until its code and grammar are reviewed,
`state_candidate_source_*` and `required_new_diagnostic_markers` are unset.
No firmware-copy outcome is inferred from this stage.

The image retains the preceding main image's exact boot command line,
private WIFI calibration ADT injection, e0d3 healthy fallback, SHA-pinned
RAM rescue, and strict ROM/pool/main parsers. The calibration property is the
sole allowed ADT difference from the executed fallback. Parser copies are
byte-for-byte identical to the map stage and pinned in
`parser-dependencies.json`; new diagnostic output must be gated in the
schema after the author freezes it. Existing result fields remain unknown
when a failed or truncated capture cannot prove their values. FW_READY, if
later observed, is not scan or networking.

All source/CI/kernel/schema, prepared-image, and exact-image-authorization
gates start false. The one-use runner requires a new image hash, scope
`boot-with-main-wifi-state-calibration`, and distinct
`--authorized-wifi-main-state-calibration-boot` argument. Earlier approval
for the map image does not apply. No old flash, payload, manifest, approval,
or hardware logs are included here. The parent alone promotes source,
builds, requests exact approval, and runs a boot; this directory performs no
CI or device operation.

The bounded stage guard is `python3 test-stage.py`. Image preflight and the
runner must remain blocked at the false CI identity gate until the parent
fills and independently verifies the new pins.
