# Compute DELTA18 — generic 1..4 color-target admission (host only)

C6f item A. No render files or public ABI/header were edited. The only
production change is the private generic-plan admission in
`metal/tegu/air_bridge.rs`.

## Change

`generic_target_formats` now accepts exactly one to four color targets when
every format is one of `{10,70,80,115}` (R8, RGBA8Unorm, BGRA8Unorm,
RGBA16Float), independent of order. The old single/`[80,80]`/`[80,80,rest]`
special cases are removed. This admits the c6ec `[115,115]` and `[80,115]`
blur pipelines and mixed 2-4 target sets the render per-target/true-MRT paths
already handle per target.

Empty target lists, more than four targets, format 0 gaps, and every format
outside the set remain exact refusals. The error text now states the actual
rule.

## Host evidence

- Rust: `cargo test --release` from worker `air-work` (updated production
  source copied byte-for-byte): **30/30 PASS**.
  New exhaustive test
  `generic_mrt_plan_tests::generic_target_sets_are_exact_and_mix_supported_formats`
  checks all 340 ordered sets of lengths 1..4 over the four formats, explicit
  `[115,115]`/`[80,115]`, every unsupported format in every position, empty,
  five targets, and mixed five-target refusal.
- Host cdylib rebuilt from that source:
  `0ee83580bd6c34b972c73536ea2901f19f934037ebc983f0a7f561695c226af9`.
- Real producer: **7/7 PASS** against compiler
  `d91bb48507662f2a5581f898301a2e1c46c7e56c5c4cae5ac551852b0e9e63e3`.
  Receipt `build/compute-v2-producer-current-abi/result.json` refreshed,
  sha256 `5b400c504e90f1ed5268a672c802ccfc3e96306fd2b74986fdcc2cb8961a23da`;
  it records source `849bc8bc...`, producer `0ee83580...`, compiler
  `d91bb485...`.
- C6f real-host harness (prebuilt lowering-opus release binary, C6f source
  unchanged, loaded the new cdylib through `LD_LIBRARY_PATH`): **19 checks
  PASS, rc0, no `C6F_PENDING`**. It reports
  `C6F_PIPE narrow_blur_11 115,115 ... err=(nil)` and
  `C6F_PASS 3 fmt=115 ... invalid=0`, so the previously pending two-target
  RGBA16F pass now reaches the draw path. This is a host plan/encoder result,
  not GPU execution and not a render-file claim.
- Logs: `delta18-cargo-test.log`, `delta18-cargo-build.log`,
  `delta18-producer.log`, `delta18-c6f-cdylib.log`.

## Hashes

- `metal/tegu/air_bridge.rs`
  `849bc8bcd3e5521fbab3cceca985408374657c9801f2a01dfe61d4657ad3c383`
- host cdylib
  `0ee83580bd6c34b972c73536ea2901f19f934037ebc983f0a7f561695c226af9`
- receipt
  `5b400c504e90f1ed5268a672c802ccfc3e96306fd2b74986fdcc2cb8961a23da`
- worker `air-work/src/lib.rs`
  `849bc8bcd3e5521fbab3cceca985408374657c9801f2a01dfe61d4657ad3c383`
  (byte-equal to production)

## Explicit gaps

- No phone, native rebuild, device execution, publication, or hardware claim.
- The admission is format/count only. It does not add an MSAA tile-buffer
  budget guard (C6 audit item 2); single-sample 4-target 32-bpp is at the
  existing n2r allocation boundary. No new hardware behavior is claimed.
- Render-owned C6f files remain uncommitted by lowering-opus; this worker did
  not edit them.
