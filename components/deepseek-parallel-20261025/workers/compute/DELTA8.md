# Compute delta8 — MTLFeatureSet + capability trace (host only)

Date: 25 October 2026. No phone/adb/fastboot/USB/mount/writer/publish/CI or commit.

## Production changes (`metal/tegu/compute_frontend.m`)

### `supportsFeatureSet:`

Added the deprecated iOS `MTLFeatureSet` selector on `TeguComputeDevice`.
The mapping is exactly the Apple1/Apple2 tier already reported by
`supportsFamily:`:

- iOS GPU Family1 v1/v2/v3/v4/v5 (`0,2,5,8,12`) -> true
- iOS GPU Family2 v1/v2/v3/v4/v5 (`1,3,6,9,13`) -> true
- iOS GPU Family3/4/5 (`4,7,10,11,14,15,16`) -> false
- macOS/tvOS/unknown feature sets -> false
- Render backend not built -> false, matching `supportsFamily:`

The SDK numeric values and the cumulative Family1/2 tier mapping are
documented beside the helper, not encoded in a generated table.

### Capability trace

Added an always-on, process-wide bounded `TEGU_DEVQ` trace for:

- `supportsFeatureSet:`
- `supportsFamily:`
- `supportsTextureSampleCount:`
- `isDepth24Stencil8PixelFormatSupported` (the public pixel-format query)
- `supportsSampleCount:` (the private sample-count query)

Each line has exactly the requested form:

`TEGU_DEVQ sel=<selector> arg=<n> ret=<v>`

At most the first 64 queries per process are emitted.  The old
`TEGU_DEVICE_QUERY` trace remains separately gated by
`TEGU_GRAPHICS_BRIDGE` + `TEGU_RENDER_TRACE`.

## Host test

`python3 workers/compute/check_frontend_delta5.py` now also covers delta8:

- Every supported Family1/Family2 feature-set value true; Family3/4/5,
  macOS/tvOS and unknown values false; non-graphics host backend false.
- Actual device-method calls for feature set/family/sample count and
  depth-format query.
- Exactly 64 `TEGU_DEVQ` lines after 85 queries; all five selector classes are
  present; the feature-set line format is asserted.
- Existing delta5 retry/gate/pipeline-library checks remain.

Result: **PASS** (release + ASan/UBSan), existing
`check_runtime_v1_aligned.py` still PASS.

ARM64 Air+Mali+Metal-base syntax compile of the changed frontend: PASS.

Integration from the overnight/work root after the final frontend edit:
**737/737 checks, 0 required, 0 pending, release+ASan**, candidate OK with
cdylib 9ff66cac / compiler ad62ed9e / source d8854273.  Evidence:
`workers/compute/evidence-integration-delta8.{log,json}`.

## Hashes

- `metal/tegu/compute_frontend.m`
  `0d7d3f28d08ed07f3647a8f844aa082ae3d2c0d72c38a6af45da79654494ebd2`
- worker test `check_frontend_delta5.m`
  `6388dd2e5bd96ebb05b15a89321ceea03366749738afb4ae586a1a9a535b9374`
- runner `check_frontend_delta5.py`
  `bd5a4975d523ba91b2cd85d1e11ab551bb492f47d02fa5f5e3b5a5255acfea23`

## Explicit unresolved

- Feature-set support is deliberately limited to Apple1/Apple2 compatibility;
  Family3+ remains false until implemented/qualified.
- The trace is capped at 64 lines per process by design; later queries are not
  observable.
- Host-only; no native build, device run or hardware claim.
