# Focused linked review preparation

**PENDING_ARTIFACT. No verdict, source-identity approval, image gate or device gate.**
Expected source: `27717fffcbbd0221334a4b18df6565551f877ab3`.
Expected CI: `39750474116`. Root will supply completion and exact kernel identity.
No CI polling/download or device access is performed here.

The scope is four functions: `niwi_scan_admit`, `niwi_power_on_admit`,
`NiwlLifecycleClient::scanLocked` and `NiwlLifecycleClient::dispatchLocked`.
Prior compatibility linked evidence is pinned only as context. Registry,
owner/DMA and unchanged event/result paths are not re-audited.

`review.py` with no arguments verifies the current source commit/body hashes,
the exact generated NORD2 wrappers and the already successful focused native
compile records/object hashes, then writes `pending.json` and the source delta.
It neither builds nor modifies a canonical/generated source file.

When root provides the exact kernel size/hash and successful CI metadata, use:

```
python3 research/nord2/wifi-ios-profile/ios/linked-review/review.py \
 --kernel research/ci-39750474116/xnu-kernel/kernel.development.sweet \
 --run-json research/ci-39750474116/run.json \
 --ci-run 39750474116 --sha256 ROOT_VERIFIED_SHA256 --bytes ROOT_VERIFIED_SIZE
```

Artifact mode rejects a mismatching CI run/conclusion/source/hash/size, parses
Mach-O with bounds, extracts only the four complete function extents and records
the actual ADMISSION/POWERSET/SCANREQ C strings and their mapped addresses.
The two policy helpers have no relocations in the native object: compare their
**entire code bytes** against the linked kernel, in addition to reviewing their
branches. A mismatch is recorded for investigation, never silently ignored.
The C++ methods contain relocation changes and require manual review of direct
branches and actual log-string references. Extraction status remains
`EXTRACTED_PENDING_MANUAL_REVIEW`; a string or symbol alone is not proof of a
reached branch.

The final linked report will cover only:

- Exact5456 extent, owner/request channel bounds, default zero/INT32_MIN RSSI
  acceptance, active semantic rejection and whole-output write after validation.
  Confirm the previous blanket unused-byte rejection is absent.
- Power helper checks24-byte extent/version1/count1/state1; no inactive-slot
  checks. In actual dispatch, retain outer carrier extent, request19, no output,
  value0 and real owner-ready dependency before success. OFF still refuses.
- Actual `scanLocked` calls `niwi_scan_admit`, logs ADMISSION with its result,
  and reaches existing enqueue only after successful admission. The metadata
  strings contain numeric lengths/counts/flags/RSSI/classifications only.
- POWERSET is reached only for request19/24-byte shape; it does not expose
  identity bytes or change physical power. The same bounded trace budget is used.

Host faults/lifecycle/IOUC behavior is supported by the existing root-run
fixtures; this review checks the exact changed branches survive link. Passing
static linked review will still not prove future Settings behavior on hardware.
