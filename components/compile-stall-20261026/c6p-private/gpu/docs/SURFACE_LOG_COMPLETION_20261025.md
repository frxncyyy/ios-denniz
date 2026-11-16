# Recovered final evidence: complete probe PASS

The authorized read-only rescue roundtrip recovered the 95517-byte persisted
log (SHA256 `2649b4292b1622746f34548663f50f5dc231a1df5b61970ebb7fd3ca23819d15`).
All 197 USB probe records match its exact prefix, including memory samples;
four recovered suffix records confirm failed-targets=0, IOSurface PASS,
104 completions/zero faults/timeouts/quarantine and final footprint 21758352.
Peak footprint is 22233488 bytes. The unchanged analyzer now qualifies this
probe completely. Dirty-volume observation is recorded, with all read pages
reread equal; no disk writes, mounts or journal replay. Only 376832 physical
bytes were read, including verification. No whole-rootfs transfer/checksum.

Exact UI124eb19b was then authorized and flashed after manual fastboot. Its
read-only capture is active; UI qualification remains pending. The historical
investigation and prepared plan below are retained as provenance.

# Direct pixel success; recover the truncated diagnostic summary

The 240-second read-only capture ended normally. The user confirmed lockscreen,
touch and buttons. Boot is `1e9d810a`, installed addon `86e0a042`, UI Metal OFF.
All 50 texture pixel comparisons, ten direct IOSurface reads (4096 pixels each,
valid layout and successful lock/unlock), compute, geometry and tiler variants
passed. The last GPU status is 104 completions, zero faults/timeouts/quarantine.
The final batch summary and final memory sample are absent from USB, so the
unchanged analyzer correctly leaves `batch_hardware_qualified=false`.

Raw evidence: `gpu/build/hardware-20261025/surface-test/probe-usb/xnu-live.log`,
421797 bytes, SHA256
`d3f4a27a0c20e91a9da431e4702b2f65417f4c37880038bb0c6d0c8a6490220a`.
See `surface-hardware-result.json`. No device writes occurred during analysis.

## Why the USB summary is missing

`Nord2GPUUserClient::externalMethod`, selector LOG, permits 16384 bytes per
client and returns `kIOReturnNoSpace` beyond that. Probe `report()` always
flushes stdout first, then ignores the diagnostic selector's return. Its
accepted messages total **16343 bytes**, leaving **41**. Each remaining summary
record exceeds that allowance. Bridge `tm` and `draw` messages use a separate
client and must not be charged to the probe. The new ten direct surface lines
made this probe exceed the existing allowance. This demonstrates truncation;
it does not itself prove successful process completion.

## Complete the evidence without another addon update

`read_metal_probe_log.py` reuses the pinned, tested HFSX reader and verified
rescue preflight. It reads only `/private/var/db/nord2-metal/probe.log`, at most
4 MiB, using bounded aligned read-only requests, with a 120-second deadline.
It rereads all relevant pages and requires equality. It does not mount, replay
the journal, repair, write userdata or hash the whole volume. Dirty catalog
observations are marked as such; absence/incompleteness cannot qualify a run.

The reader passed against the current local pre-boot clone (77851-byte older
log, 344064 physical bytes including repeated reads). This validates extraction
only; that old log is not the current phone result. Source pins remain intact.

`reconcile_metal_probe.py` requires the latest persisted batch to be v8 and
every probe record already observed over USB to match an exact ordered prefix,
including runtime memory measurements. It appends only actual recovered suffix
records to a new derived file, preserves both originals and hashes provenance.
The existing complete analyzer then runs unchanged. Four regression groups
reject stale/mixed/duplicate/missing prefixes, new boots, missing suffixes and
failed final results; all six existing qualification test groups also pass.

## Conditional UI test, awaiting exact authorization

Prepared stage: `gpu/build/hardware-20261025/ui-after-log`.
Sequence: tested rescue `d78bf9fe`, read the fixed log, reconcile it; only on a
complete PASS proceed to the UI image, otherwise return to current probe.
No addon changes or filesystem transfers are proposed. Both transitions need
manual fastboot. The runner checks authorization and recomputes reconciliation
before any UI flash, including input hashes and read-only extraction receipt.

UI image: `gpu/build/device-mmu-shared-ui-candidate/nord2-ios-metal-shared-ui-stop23.img`
SHA256 `124eb19b924825b1bce5d785018f7ade1c95b0ebc6dd7abc2779a25f3fe16a19`.
33165312 bytes; kernel/ADT identical to the current probe; exactly one payload
byte changes `nord2_metal=0` to `1`. Image and rescue/probe/baseline hashes pass.
Keep audio/Wi-Fi baseline `a5b6e6e3` for recovery. No UI test is authorized yet.

Actual UI acceleration still requires evidence of compositor Metal work and
functional interaction. Visible lockscreen alone is insufficient. Existing
kernel logs do not attribute every submission to its process, so additional
persisted compositor diagnostics may be needed. Do not claim full Metal API
conformance or UI acceleration from successful standalone pixel probes.
