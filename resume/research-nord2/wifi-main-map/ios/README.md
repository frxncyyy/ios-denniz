# Private main Wi-Fi map-repair image stage

This is a new, unprepared image stage. The preceding main Wi-Fi image
`368062de75a2bcf8b44954c61a1a924cbd01ebd77083d4691144c7604af73521`
boot-looped before the main-radio report. The preserved panic identifies its
15.125 MiB pool mapping through `ml_io_map` as too large for the hardcoded
8 MiB `io_submap`. The parent owns a narrow source change to
`ml_io_map_unmappable` for the exact claimed pool while preserving
`VM_WIMG_IO` device attributes. This template neither claims that change
works on hardware nor authorizes a new boot.

The target image is
`shim/out/nord2-xnu/nord2-ios-wifi-main-map-battery-stop23.img`. Its boot
command line, private WIFI calibration injection, ROM/DMA/battery gates,
telemetry grammar, and requested main firmware sequence match the executed
main Wi-Fi image. The source, CI, kernel, parser/schema, prepared-image, and
exact-image-authorization gates are **false** until the parent fills them
with independently reviewed identities. The old flash, approval, payload,
manifest, logs, and hardware observations have not been copied. The runner
requires this stage's distinct approval scope and the exact new image hash;
authorization for `368062de` cannot be reused.

The known healthy fallback remains the executed
`e0d3a50f7a706f27a8d9fb19bbd6951c85f016e8b179b47b726c331331ec65d5`
image, with the SHA-pinned RAM rescue. Calibration bytes remain in the
existing private WIFI file, never in source. The target ADT must differ from
the executed fallback ADT only by `/chosen/nord2-wifi-calibration`.

The parser sources in this directory are byte-for-byte copies of the
executed stage's strict parsers. `parser-dependencies.json` pins those copies,
their executed-stage originals, and the historical ROM parser used by the DMA
wrapper. These parse a future capture; they do not turn the boot-loop logs
into FW_READY evidence. A complete main-radio report proves neither scan nor
network connectivity.

Offline checks: `python3 test-stage.py`, `python3 test-calibration-adt.py`,
`python3 test-preflight-profile.py`, and `python3 test-evidence.py` from this
directory. `verify-image.py --preflight` must remain blocked while gates are
false. Only the parent may promote source, build, request exact approval, or
run the one-use device runner.
