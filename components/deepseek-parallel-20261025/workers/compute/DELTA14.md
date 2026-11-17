# Compute DELTA14 — TGP2 extended buffer table (host only)

Base: commit 4f3ad8e + current tree. No phone/native/publication.
Status: **READY FOR REVIEW**. One release set and one ASan/UBSan set pass.

## ABI

`metal/tegu/pipeline_format.h` is byte-identical to lowering's
`proposed-header-buftable/pipeline_format.h`:
`ce462be8cf147750b25b2a475f08f2141c6dd8af6bb4e74f4d2e5a6189358df6`.
It adds MAX_BUFFERS=31, FAU_BUFFERS=8, TABLE_BUFFER=3, FLAG_BUFFER_TABLE=8,
BUFFER_TABLE_FAU=272 (FAU byte offset), BUFFER_TABLE_RECORD=16,
WORD_BUFFER_MASK=19, and renames `tegu_pipeline_info2.reserved` to
`buffer_table_mask`.

`air_bridge.h`/`air_bridge.rs` expose `TEGU_AIR_COMPUTE_MAX_BUFFERS=31`,
permit native buffer bindings 0..30 in the v2 planner, and assert 31 fits the
existing 32-record TDG2 capacity. TDG2 remains 32×192-byte descriptors; no
wire-size change was needed or invented.

## Bridge

`metal/nord2_metal_bridge.c`:
- accepts descriptor buffer bindings 0..30; arrays are 31 entries;
- for w19-masked extension bindings writes a 16-byte
  `{u64 va, u32 bytes, u32 0}` record at `table + 16*binding` for every mask
  bit; missing/extra extension records refuse before submit;
- places the 496-byte table in the per-dispatch command allocation at offset
  40960 (owned for the command lifetime; no extra N2C_JOB_BOS handle), writes
  `{table_va,496,0}` at FAU byte272, and raises FAU count to cover bytes
  272..287;
- retains every referenced buffer in the JM request (and command/code as
  before, code only when a handle slot remains). The existing 32-handle
  transport cap is unchanged and is an explicit limit for >32 unique handles.

## Host tests — actual result

`python3 workers/compute/check_compute_v2.py` from `gpu/`:

```
PASS pipeline_format v2 ...                 release
PASS compute v2 bridge ...                  release
PASS real corpus buffer table: manifests=11 ext=700
PASS pipeline_format v2 ...                 ASan/UBSan
PASS compute v2 bridge ...                  ASan/UBSan
PASS real corpus buffer table: manifests=11 ext=700
PASS compute v2 host suites (release + ASan/UBSan)
```

The real half compiles corpus SPV
`ci-3d730e2e38ded1afdebb` with lowering's private
`lib-buftable` compiler (`614e9b56...`), then feeds the resulting TGP2 package
(11 manifests, w19=0x700) through the real bridge/fake JM and verifies table
FAU pointer, records, spans and retained-write handles. Package SHA-256
`f0fb3fa4978b2f543109d181bec0dc5baf340275cbcaf0985110edf5efb4a589`.
`v2-out/result.json` records all binaries and hashes; no GPU execution.

Synthetic suite also covers ext bindings 8/9/10 table records and a
missing-extension-descriptor refusal.

Cargo host check: `cargo test --release` **30/30 PASS**; rebuilt worker cdylib
`43b87524868ada134ef9a360f0ef9bb2795bbc9d289e8c00bc1a4ba4e92a3aae`
from production `air_bridge.rs` `f9db0bf0...`.

## Hashes

- bridge `metal/nord2_metal_bridge.c`
  `24011dbcba9d0d244709aba39b828051d3bf22fb21590160824a6163c14a4181`
- header `metal/nord2_metal_bridge.h`
  `9e4972169dd5601961fcfa4662ad85a08a1d1f6495808e85deb07a2d7561325c`
  (DELTA13 MSAA content; only compute buffer-table changes are in .c)
- pipeline_format `metal/tegu/pipeline_format.h`
  `ce462be8cf147750b25b2a475f08f2141c6dd8af6bb4e74f4d2e5a6189358df6`
- air_bridge.rs `f9db0bf0b95ef8bbff5104febf2036c96035944cf044d7c0c4487fc8e20a1841`
- air_bridge.h `180eabf998f0a1abfccaed6293d9215765d086be4c2e4a0793136b2133eab43d`
- test `tests/check_compute_v2_bridge.c` `adcec4ea27b1bd652df9e2cd51456878ee1e14cec673a7bc1b578e3a7dbd3c70`
- runner `check_compute_v2.py` `169510f590431815d2a97bab56aaba82f8160c879bef37515414da3e7ebc6fa1`

## Explicit gaps

- No hardware/native execution; lowering must rebuild the shared compiler from
  ce462be8 and root must perform native integration.
- N2C_JOB_BOS=32 is an existing hard transport cap. The table consumes no
  handle (embedded in command), but a kernel with >32 unique retained
  resources still cannot be submitted; code is best-effort retained last.
- Descriptor arrays and physical-pointer kernels remain lowering refusals;
  this delta adds the bounded extended buffer table only.
