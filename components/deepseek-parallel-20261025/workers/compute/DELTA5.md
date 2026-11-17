# Compute delta5 — device startup and compositor gate (host only)

Date: 25 October 2026. No phone/adb/fastboot/USB/mount/writer/publish/CI or commit.

## Production changes (`metal/tegu/compute_frontend.m`)

1. **Bounded NOTREADY retry in `initWithAcceleratorPort`**
   - `tegu_capabilities_with_retry` performs at most `TEGU_CAP_RETRY_ATTEMPTS`
     (250) capability queries, waiting `TEGU_CAP_RETRY_US` (20,000 us) between
     attempts: about 5 s.
   - It retries only `kIOReturnNotReady` (`0xe00002d8`) and returns failure
     immediately for any other error or a malformed successful reply.
   - A valid reply must have four words, word0=1 and capability bit8.
   - The existing `capabilities` field is set only from the accepted reply.

2. **Compositor gate**
   - Non-backboardd processes retain normal device creation.
   - `backboardd` is denied before IOKit/GPU setup unless
     `/private/var/db/nord2-metal/compositor-enable` exists. Default off.
   - The process name comes from `getprogname()`; the opt-in check uses
     `access(path, F_OK)`.
   - One bounded line is emitted for backboardd:
     `TEGU_COMPOSITOR_GATE enabled=0|1`.

3. **Pipeline-library override**
   - `TeguComputeDevice` implements
     `-newPipelineLibraryWithFilePath:error:` itself. It returns nil and sets
     an `NSError` domain `TeguPipelineLibrary` code 1; it never forwards to
     the inherited `_MTLDevice`.

## Host tests

- `python3 build/deepseek-parallel-20261025/workers/compute/check_frontend_delta5.py`
  compiles the real frontend with the current Rust bridge and Mali runtime
  under ASan/UBSan and runs:
  - two NOTREADY replies then ready -> device created after exactly 3 attempts;
  - hard non-NOTREADY error -> exactly one attempt, no retry;
  - never ready -> nil after exactly 250 attempts;
  - actual `initWithAcceleratorPort` through the host transport seam for the
    ready, never-ready and gate-denied cases;
  - backboardd denied without the opt-in and allowed with it;
  - exactly formatted `TEGU_COMPOSITOR_GATE enabled=0` and `enabled=1` lines;
  - `newPipelineLibraryWithFilePath:error:` returns nil and a non-nil error.
- Existing `python3 workers/compute/check_runtime_v1_aligned.py` still PASS.
- Integration from `.../overnight-20261024/work` after the final edit:
  `737 checks, 0 required, 0 pending`, release+ASan; evidence copied to
  `workers/compute/evidence-integration-delta5.{log,json}`.
- Native ARM64 syntax for the changed frontend with AIR+Mali+Metal-base passed
  in isolation (`clang --target=arm64-apple-ios16.0 ... -c`); full native
  bundle build/device execution is root/L4 work and was not run.

## Current hashes

- `metal/tegu/compute_frontend.m`
  `cc692fa78f3ddbe6a9a964018d9aac9dc3a0267d9251d91161fb0dcd5e2be27f`
- `workers/compute/tests/check_frontend_delta5.m`
  `a235a7a8be8bd8f75a0ad368a77d629586d8f6f9cb30437516d049fd8ffea104`
- `workers/compute/check_frontend_delta5.py`
  `06dc2d91e2630ee1b55b9c89d2c75e6c8d746ebdf573155b7176fbb866a98fb5`
- Delta4 bridge remains
  `40aab80c141f25847064b62f62caabd819f940b3aa7538e761eb1c47e9d857f1`.

## Explicit unresolved

- No native bundle rebuild or device execution. The 5 s retry, gate file
  lookup and inherited-selector override are proven on the host fake transport
  only.
- Process identification depends on `getprogname()` returning `backboardd`;
  the file is checked with `access`, not parsed.
- Full graphics native syntax is outside this test (the current graphics tree
  has unrelated work in progress); the compute frontend syntax passed without
  `TEGU_GRAPHICS_BRIDGE`.
