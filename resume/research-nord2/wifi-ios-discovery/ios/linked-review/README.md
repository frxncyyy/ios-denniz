# Bounded discovery linked review

**Completed: [bounded manual review PASS](REPORT.md), no actionable findings.**
Exact source `ee2e6fd9516b5eb7a5c41cabf1a077c93e53ccc7`, CI39760685458 attempt2.
The following documents the reusable extractor; its automatic status never supplies a semantic PASS. No source edit, compilation, CI polling/download or device access is
performed by this package. Previous linked PASS reports and image/device gates
are neither copied nor promoted.

`review.py` reuses only the bounded Mach-O reader from the preceding profile
review, pinning that script's digest. It prepares source hashes, validates the
already-built focused objects and exact generated source/header bodies, and
extracts focused DWARF for four changed data types across available objects.
Nine functions are selected: scan admission, BSS metadata, snapshot preparation,
backend decode/add, client scan/completion and allocation/free. The snapshot
row helper is inlined in the focused object and is reviewed inside prepare.
Allocation/free are size sanity checks only, without a redundant callgraph.
Quiesce is owned by a separate reviewer; registry/owner/DMA baseline, unchanged
event/result machinery and full-kernel ISA/disassembly are not repeated.

Run now, without assigning an expected source/CI:

```
python3 research/nord2/wifi-ios-discovery/ios/linked-review/review.py
```

The root-supplied source/CI binding is reproduced with:

```
python3 research/nord2/wifi-ios-discovery/ios/linked-review/review.py \
 --source-commit ee2e6fd9516b5eb7a5c41cabf1a077c93e53ccc7 --ci-run 39760685458
```

This rejects source bodies differing from the named commit. It records the
source delta from the preceding27717ff source baseline and validates the focused objects' canonical wrappers,
target genericARMv8.2 flags and generated changed-header bodies. Focused source
hashes alone do not establish CI source identity; root's independent exact CI
source/flag audit remains required.

When the successful artifact and root-verified identity exist:

```
python3 research/nord2/wifi-ios-discovery/ios/linked-review/review.py \
 --source-commit ee2e6fd9516b5eb7a5c41cabf1a077c93e53ccc7 --ci-run 39760685458 \
 --kernel research/ci-39760685458/xnu-kernel/kernel.development.sweet \
 --run-json research/ci-39760685458/run.json \
 --sha256 ROOT_KERNEL_SHA256 --bytes ROOT_KERNEL_SIZE
```

Artifact mode rejects wrong run/conclusion/head/hash/size. It indexes symbols
once, disassembles only the nine complete function extents with a64KiB aggregate
code ceiling, records exact linked code and focused-object comparisons, maps
bounded numeric diagnostic strings and resolves direct call targets. It writes
`EXTRACTED_PENDING_MANUAL_LINKED_REVIEW`, never PASS. Byte mismatch with a
focused object can be normal compiler/link variation; manual constants, bounds,
branches and targets decide semantics. No identical recompilation is pursued.

Root may additionally supply exact CI object paths with repeated
`--schema-object UNIT=PATH`, e.g units `wifi_ios_snapshot`, `wifi_scan_bss`,
`wifi_ios_client`, `wifi_normal_rx`, `wifi_normal_transport`, `sweet_touch`.
The extractor retains only the four named DWARF structures and direct members,
compares available size/member layouts with focused expectations, and explicitly
records absent types. It does not claim an absent debug type was checked.
Exact object-to-CI provenance must come from the root CI artifact audit;
arbitrary local objects are not accepted as CI authority merely by this option.

Focused expectations from the current successful compiler objects:

|Type|Bytes|Selected layout check|
|---|---:|---|
|nsb_record|1112 /0x458|IE array1024 at offset88; per-record stride1112|
|nsb_scan|18336 /0x47a0|16 records at offset544|
|niwb_bss|1104 /0x450|IE array1024 at offset80|
|niwa_snapshot|18696 /0x4908|Rows at offset72;16×1164 bytes; omitted/mask fields|

These focused expectations were correlated with exact CI source/header bodies and actual linked constants in the final report; exact CI object/DWARF is unavailable. They are not a direct CI DWARF approval. Linked init allocation, scan reset and free must all use the
same18696-byte snapshot; backend producer/consumer record strides and IE limits
must agree with the enlarged schema. `CHECKLIST.md` names the required changed
branches. The combined image still needs independent source/schema/ISA evidence
and genuine future hardware scan delivery; static linked review alone proves
neither Settings behavior nor a real WPA connection.
