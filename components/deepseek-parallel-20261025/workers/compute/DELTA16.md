# Compute DELTA16 — host cdylib/producer receipt refresh (granted heavy only)

No production source changed. Base commit 0ffccc0f.

- Copied committed `metal/tegu/air_bridge.rs` (`f9db0bf0...`) to the worker
  `air-work/src/lib.rs`; byte-equal.
- `cargo test --release`: **30/30 PASS**.
- `cargo build --release` with `CARGO_BUILD_JOBS=1`: worker host cdylib
  `43b87524868ada134ef9a360f0ef9bb2795bbc9d289e8c00bc1a4ba4e92a3aae`.
- Real producer against the new shared compiler
  `d91bb48507662f2a5581f898301a2e1c46c7e56c5c4cae5ac551852b0e9e63e3`:
  **7/7 PASS**. Refreshed
  `build/compute-v2-producer-current-abi/result.json`
  (`7d68ad9b...`), now recording compiler d91bb485, producer 43b875, and
  source f9db0bf.
- No production file was edited; the only git change remains the
  integration-owned `tests/metal_resource_integration_test.py` edit from
  integration_tests.
- No phone/native/publication.
