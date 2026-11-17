# Compute DELTA12 — finish DELTA11 (thermal-limited, host only)

Base: commit 22e7174. No phone/adb/fastboot/USB/mount/writer/publish/CI.
Cargo lease was taken and released before this delta; cdylib 3f2d275a was
built from production `air_bridge.rs` 59c796a4 (Rust 30/30, producer 7/7).

## Finished

### 1. Full dynamic threadgroup memory (`setThreadgroupMemoryLength:atIndex:`)

- `TeguEncoder` stores 31 per-index lengths, clears them on encoder creation,
  rejects index>=31, non-16-byte multiples and >32768, and reports
  `maxThreadgroupMemoryLength=32768`.
- Both dispatch entry points forward the complete table through the additive
  `tegu_air_pipeline_native_dispatch_tg` and
  `tegu_air_pipeline_native_dispatch_v2_tg` ABI. Any nonzero length disables
  the buffer-only TGP1 shortcut, forcing the TGP2/TDG2 path so header word18
  WLS and the per-region `TgLength` constants are exact. No guessed WLS.
- Rust copies the sparse nonzero lengths into `StageTable.threadgroup_lengths`
  before plan/shape, validates them there too, and the existing TGP2 bridge
  allocates/retains WLS and TLS. Old planner symbols keep NULL-length behavior.
- Real host proof (`tests/check_threadgroup_planner.py`):
  real `reduce_buffers_nd_mode_argmax_f16` AIR, lengths index0=1024,
  index1=256 -> planner emitted TgLength SpecId4=1024, SpecId5=256 for its one
  region; real compiler 1877804b compiled that SPIR-V with
  `wls_bytes=1280`, `tls_bytes=0`; malformed 17-byte length was refused.
  JSON: `{"planner_rc":0,"regions":1,"tg_length_hex":
  ["00040000","00010000"],"compiler_rc":0,"wls_bytes":1280,
  "tls_bytes":0,"gpu_executed":false}`.

### 2. Inert compute pipeline behind `TEGU_INERT_PIPELINES`

- `TeguPipeline` has an internal inert bit. Under the flag and when
  `tegu_compositor_process_is_backboardd()` is true, failed compute pipeline
  creation (invalid/foreign function, compile/lowering/limits failure, or
  unsupported reflection/descriptor options) returns a nonnil inert pipeline
  with error cleared and a bounded `TEGU_INERT_COMPUTE_PIPELINE` trace.
  Other processes and flag-off builds keep nil/NSError.
- Inert dispatch (both threadgroups and exact threads) is dropped before any
  job, does not poison the command and emits a bounded
  `TEGU_INERT_COMPUTE_DISPATCH ... dynamic=<0|1>` trace. No fake GPU job.
- Host test covers inert dispatch, actual invalid-function fallback in
  backboardd with flag on, and nil/error in non-backboardd and flag-off.

### 3. Four QuartzCore selector-gate methods

- `TeguComputeDevice`:
  - `newBufferWithBytesNoCopy:length:options:deallocator:` returns nil with a
    bounded `TEGU_SELECTOR_REFUSED` trace. The driver has no exact arbitrary
    CPU-page BO import, so it never copies silently.
  - `newResourceGroupFromResources:count:` returns nil with the same explicit
    trace (no residency model).
  - `encodeCacheHint:` is a semantic no-op.
- `TeguBuffer`:
  - `newLinearTextureWithDescriptor:offset:bytesPerRow:bytesPerImage:` returns
    nil with an explicit trace; there is no exact buffer-backed linear-texture
    representation yet. `deviceLinearReadOnlyTextureAlignmentBytes` remains 0,
    consistent with this and no-copy support.
- All four selector names are now present for the integration QuartzCore
  selector gate. Nil outcomes are explicit unsupported content, not crashes.

### Host-build isolation

The shared `blit_encoder.inc` gained texture-copy methods that require render
types. Delta12 guards that include for `TEGU_FRONTEND_HOST` builds without
`TEGU_GRAPHICS_BRIDGE`; native device builds (including compute transport)
still include it. This restores compute-only host compilation without
changing device behavior.

## Tests and exact results

- `python3 workers/compute/check_frontend_delta5.py`
  release configs: default-off, default-on, inert, inert+compositor; then one
  ASan/UBSan inert config. **5/5 PASS**. Logs under
  `workers/compute/runtime/delta10_*.log`, `delta11_*.log`.
- `python3 workers/compute/tests/check_threadgroup_planner.py` PASS (above).
- `python3 workers/compute/check_mrt_bridge.py` earlier delta11: 67/67
  release+ASan; no delta12 bridge change.
- Cargo (earlier lease): Rust **30/30**; real producer **7/7** against compiler
  1877804b; receipt `build/compute-v2-producer-current-abi/result.json`.

## Hashes (current)

- `metal/tegu/compute_frontend.m` 3e918bee82e12053b5ba34a1c57543682c765f2f8275d521255ded4c5909bcc3
- `metal/tegu/runtime_pipeline.inc` a20e43f241a9a1bc87ecad032db8d3d2fa3bcff9c2cd0f7e2fbaf3c3aa5ed298
- `metal/tegu/air_bridge.rs` 59c796a4c6cb761d89d80ec3161e9119ec5d0494db32146e11dc65e886dd4d36
- `metal/tegu/air_bridge.h` 1612aa8790fc8dddc49f8653f3ea06e513a3ac45e346e8fc503cb744d6dcc2e5
- `check_frontend_delta5.py` b5041a45ac18b2b50e39d8e27c13a280bb840543050e05b86ee5cefe18188790
- `tests/check_frontend_delta5.m` f03f43fcc6d01d62bc71ed0097b64a867fb0a4e7f10cf7d96378a33937069f3e
- `tests/check_threadgroup_planner.py` 67f359ec746071d1276110c8f65d1b8b764b7723b817e06553418f6860e16b6a
- host cdylib `3f2d275a97fc45a25468d77eb4513a71d71a8ab7ab75b323f2c84a4e7463dd15`

## Unresolved / explicit

- No hardware execution; root L4 native/device qualification remains.
- The host test exercises the inert fallback through an invalid/foreign
  function. The compile/limits-failure branch is implemented but not separately
  forced in this host binary; no hardware/native claim.
- No-copy buffers, resource groups and exact linear texture views remain nil
  refusals; no silent copies or fabricated layouts.
- Compute host tests now skip shared blit texture methods; graphics host builds
  still compile them. Native compute transport build still includes blit.
- `TeguTexture` view support is render-owned and was applied by render; not
  edited by compute.
- Working tree has only the compute_frontend host-build guard plus an
  integration-owned test edit; root owns commit/stage.
