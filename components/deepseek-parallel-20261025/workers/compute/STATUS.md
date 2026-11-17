# Compute worker status — 26 October 2026 (DELTA19)

- C6g item C: BGR5A1Unorm (Metal 43) admitted in the explicit attachment
  class, `generic_target_formats` `{10,43,70,80,115}`, and the source-over
  blend admission. No render/header files changed.
- Rust 31/31 PASS including exhaustive 780 1..4-target sets and a direct
  format-43 blend test. Host cdylib `449db4c3`; producer 7/7 against compiler
  `d91bb485`; receipt `ee2acc41` with source `29e161ac`.
- Prebuilt C6g host harness loaded the new cdylib: release+ASan/UBSan 33
  checks PASS, 0 pending; F43 pipe is non-inert (`inert=0 draw=1 fmt=43`) and
  its draw encodes (`invalid=0 count=3`). Host-only; no hardware claim.
- No phone/native/publish. HEAVY release posted. See DELTA19.md.

# Compute worker status — 26 October 2026 (DELTA18)

- C6f item A: private `generic_target_formats` now admits exactly 1..4 color
  targets from `{10,70,80,115}` in any order, including `[115,115]` and
  `[80,115]`; empty, >4, format0 gaps and all other formats refuse. No render
  files or headers changed.
- Rust 30/30 PASS including exhaustive 1..4-set test. Host cdylib
  `0ee83580`; real producer 7/7 against compiler `d91bb485`; refreshed receipt
  `5b400c50` with source `849bc8bc`. Prebuilt C6f host harness loaded the new
  cdylib: 19 checks PASS, no PENDING, `narrow_blur_11 115,115` draws.
- No phone/native/publish. HEAVY release posted. See DELTA18.md.

# Compute worker status — 26 October 2026 (DELTA17)

- C6 audit fixes: table-fill error now sets BAD before cleanup; fau_count>128
  cleans up TLS/WLS/command; command execution preserves the first NSError and
  encodeSignalEvent retains it without signaling foreign events.
- Host: frontend 4 release + one ASan PASS; compute v2 release + one ASan PASS
  including real corpus buffer table. No production behavior change otherwise.
- Hashes: bridge 9deda834, compute_frontend 2902428c, test 203bd546, runner
  4f07ad08. No phone/native/publish.

# Compute worker status — 26 October 2026 (DELTA16 receipt refresh)

- No production edits. Committed air_bridge.rs f9db0bf -> worker cdylib
  43b87524; cargo 30/30; real producer 7/7 against compiler d91bb485.
  Receipt build/compute-v2-producer-current-abi/result.json refreshed
  (7d68ad9b), matching source f9db0bf / cdylib 43b87524 / compiler d91bb485.
- HEAVY RELEASE compute posted. No phone/native/publish.

# Compute worker status — 26 October 2026 (DELTA15 thermal)

- DELTA15 selector gaps implemented: stable device name (+non-METAL fallback),
  minimum linear alignment 64/0 documented with buffer-texture/linear selectors
  remaining explicit nil, queue commandBufferWithDescriptor (same command),
  command encodeSignalEvent explicit sticky invalid+NSError, library async
  completion-once wrapper.
- Host: four release configs + one ASan/UBSan PASS; selector trace bounded at4.
- Hashes: compute_frontend 7ea1fb65, library_frontend 22ef99c0, test a5fd7b89,
  runner 4f07ad08. Explicit: exact buffer-backed textures still nil; no
  device/native; deviceLinearReadOnlyTextureAlignmentBytes remains0.
- See DELTA15.md.

# Compute worker status — 26 October 2026 (DELTA14 thermal)

- DELTA14 extended TGP2 buffer table landed: header ce462be8, bridge arrays
  0..30, 496-byte table at FAU byte272 with w19 records, planner 0..30,
  TDG2 32-record capacity asserted. v1/v2 unchanged.
- Host release and one ASan/UBSan: pipeline format + synthetic bridge +
  real corpus buffer-table package all PASS; 11 manifests/ext=0x700.
- Cargo 30/30; cdylib 43b87524.
- Explicit: no hardware/native; N2C_JOB_BOS=32 handle cap remains; lowering
  shared compiler rebuild required. See DELTA14.md.

# Compute worker status — 26 October 2026 (DELTA13 thermal-limited)

- DELTA13 base 4f3ad8e. Mirrored textures' n2r MSAA v3 into
  `n2mb_render_targets` version3: sample_counts/resolves token ABI, exact
  sample4 spans, resolve BO lookup/retention/write, format/dimension/alias
  validation, and v1/v2 packet identity.
- Host bridge: release and one ASan/UBSan both **83 checks, 0 required**.
- Production hashes: bridge c14819755a07611f382378ae4d84b9ad42c69f98af2f0344e6bf64b23f8af830,
  header 9e4972169dd5601961fcfa4662ad85a08a1d1f6495808e85deb07a2d7561325c.
- Explicit gaps: no hardware/native execution; true-MRT LOAD still refused.
- See DELTA13.md. Render can use the token-based resolve array.

# Compute worker status — 26 October 2026 (DELTA12 thermal resume)

- DELTA12 base commit 22e7174. Finished dynamic WLS via `*_tg` planner,
  backboardd-only inert compute PSO, and the four QuartzCore selector-gate
  methods. Real planner/compiler proof: reducer AIR lengths 1024/256 ->
  TgLength constants 1024/256 -> WLS 1280. Host frontend suite 5/5 (4 release
  configs + final ASan); Rust 30/30 and producer 7/7 from lease.
- Explicit gaps: no hardware execution; no-copy/linear/resource-group remain
  nil explicit refusals; compute host build guards shared blit include.
- See DELTA12.md for exact hashes, commands and logs.

# Compute worker status — 25 October 2026

Role: compute (DeepSeek v4.1 Flash max). No phone/adb/fastboot/USB/mount/writer/publish/CI.
No production commit; root owns commits. All tests are host tests; no hardware claim.

## Current result

- Delta10 adds the optional compile-time `TEGU_COMPOSITOR_DEFAULT_ON` experiment
  switch and exports `tegu_compositor_process_is_backboardd()` through
  `compositor_process.h` for render DELTA8.  Default build behavior is
  unchanged; host tests pass both macro states under ASan/UBSan.
- Delta9 adds additive `n2mb_render_targets` version 2 with per-target blends,
  validated before target retention and passed unchanged to n2r's BLEND
  descriptors.  Version1/default bytes are unchanged.  Host MRT/TLS test is
  now **67/67** release+ASan; no native/device claim.
- Delta8 adds `supportsFeatureSet:` mapped exactly to the Apple1/Apple2
  compatibility tier already reported by `supportsFamily:`, plus an always-on
  bounded (first 64/process) `TEGU_DEVQ` trace for feature-set/family/sample
  and pixel-format capability queries.  Host test PASS release+ASan; no
  native/device claim.
- Delta7 L1 completed: host cdylib rebuilt from current `air_bridge.rs`
  `d8854273` as **9ff66cac**; Rust suite 29/29; real producer 7/7 against
  fixed compiler **ad62ed9e**, receipt refreshed at
  `build/compute-v2-producer-current-abi/result.json`.  Integration candidate
  provenance now passes.  Graphics TLS bridge allocation is implemented:
  per=next_pow2(align16(bytes)), total=per*2048*cores, 64 MiB cap,
  4096-aligned, retained read-only and released after wait; TLS0 unchanged.
  Host MRT/TLS test is 59/59 release+ASan.
- Delta6 (Claude root) adds the bridge side of the bounded QuartzCore MRT
  path.  Render chose per-RT `SELECT_RT` jobs; the bridge now also has an
  additive true-MRT target-token entry using textures' `n2r_build_v4`, plus a
  v3 R8 route that avoids the legacy four-byte-row preflight.  Default
  v1/v2/v3 BGRA bytes are unchanged.  Host C test 59/59 release+ASan; Rust
  29/29; no native/device claim.
- Delta5 (Claude root) adds the device-creation race retry, the explicit
  backboardd compositor opt-in gate, and the inherited pipeline-library
  refusal.  Host tests exercise the real frontend init body with a fake
  transport: NOTREADY twice then ready succeeds, never-ready is nil after
  exactly 250 attempts (20 ms each, ~5 s production bound), backboardd is
  denied without `/private/var/db/nord2-metal/compositor-enable` and allowed
  with it, and `newPipelineLibraryWithFilePath:error:` returns nil plus an
  NSError.  No device/native build was performed.
- The three independent integration-suite blockers against the final bridge are
  fixed: combined sampler_index range, declared-span layout bound, and wire
  swizzle validity. Delta3 adds 16 per-level facts to the producer resource
  ABI and the TDG2 mip/layer transport. The independent integration suite
  delta10 is **737/737 PASS**, 0 required, 0 pending, release+ASan, with
  candidate provenance matching cdylib 9ff66cac/compiler ad62ed9e/source
  d8854273.  Delta8 was also 737/737; delta9 was 737/1 while the render
  harness was mid-edit.
  TGP2 156, job113, draw_v2 23, Vk83 13, producer59, corpus graphics,
  native compute frontend, render cache, legacy sampler and variant-thread
  ASan. The native test resolves `gpu/build/native/probe-compute.bc` from that
  cwd; no production defect remains.
- Delta4 adds real TGP2 TLS/WLS allocation in the bridge.  The lowering
  compiler is now `libnord2_mali_runtime.so 742cdf3d...` with words17/18 live
  (D8F18H carries TLS32, WLS0).  The host bridge emits the packed LOCAL_STORAGE
  descriptor, retains TLS/WLS BOs through the JM job, and refuses before submit
  if allocation fails.  No hardware execution.
- The current compiler `742cdf3d...` plus producer `06a6c6ee...` compile all
  seven real producer cases (prior receipt used 57db4be4; source unchanged):
  TestFastMath nearest, linear, four exact-thread regions and accumulateKernel.
  The fresh 742cdf3d proof is persisted under
  `gpu/build/compute-v2-producer-tls-742/result.json` (7/7; prior
  `compute-v2-producer-current-abi/result.json` was 57db4be4).
- ComputeRegion carries exact per-region `local[3]`; the frontend no longer
  compares edge regions against the original 8x4 local.
- Additive graphics specialization sidecar now exposes exact selected view
  format/type/aspect/level, per-use compare, twin/shader-swizzle flags, real
  linear sampler facts, bounded 2D mip/2D-array facts and new-only ASTC. The
  old default-only entry remains byte/behavior identical.
- Rust vertex fetch admits VkFormat83 (Metal Half2) matching lowering's
  MALI_RG16F|RGB1 encoding; an unrelated format still refuses.

## Implemented

### `gpu/metal/tegu/pipeline_format.h`
- TGP1 unchanged (64-byte header, 272-byte wire).
- TGP2: 128-byte header, 64-byte resource manifest records, **64 record max**,
  a separate 1 MiB code bound (`TEGU_PIPELINE2_MAX_CODE`), and the lowering-ACKed
  word17/18 TLS/WLS contract (max32768 each); TGP1 remains 4 KiB.
  (`TEGU_PIPELINE2_MAX_MANIFEST`), exact parser `tegu_pipeline_parse2` /
  `tegu_pipeline_parse_any`.
- Manifest: set/binding/kind/array_count/access/table/entry/flags, canonical
  Vk storage format for IMAGE only (42-value SPIR-V map), reserved view words
  9..11 zero, named shape/numeric domains, declared-combined TEX+SAM pair,
  sampler compare flag, dummy sampler table2[256], exact access/collision
  rules. Atomic wire remains an explicit refusal.
- TDG2 wire: 32-byte header + 32 x 192-byte descriptors + 128 push bytes.
  Descriptor carries native binding, Metal slot, access flags, retained
  token/offset/span, Metal allocation/view formats, separate Vk view format,
  exact sampler state, level/slice/layer strides and swizzle.
- Additive `TEGU_PIPELINE2_DESC_SWIZZLE_VALID` (0x2000) descriptor flag:
  zero-valued RRRR is explicitly representable; a nonzero word without the
  flag remains accepted for compatibility. The flag is refused on image
  descriptors where the swizzle is not observable. Lowering ACKed this as a
  wire-only change; the compiler manifest is unchanged.

### `gpu/metal/tegu/air_bridge.h` / `air_bridge.rs`
- Compute v2 ABI: resource **488** (additive per-level facts and explicit
  `swizzle_valid`; all original offsets unchanged), descriptor64, region
  **1184** (exact `local[3]`), NativeDispatchV2 **11592**, variant_hash@36,
  descriptors@72, regions@2120, region flags@24/constant_count@28/
  constants@32/push@1056.
  Print line: `r=488 d=64 reg=1184 v=11592 vh=36 desc=72 regions@2120
  reg_flags=24 reg_cc=28 reg_const=32 reg_push=1056`.
- `tegu_air_pipeline_native_dispatch_v2` returns every region's exact local
  derived from the pipeline's `SpecKey::LocalSize(Axis)` declarations against
  that region's complete specialization; no hardcoded SpecIds.
- Compute planner supports real sampled/sampler/image resources, bound-format
  variants, exact `dispatchThreads` regions, Metal/Vk view domains, access
  spans, and the v2 ABI.
- Additive graphics sidecar:
  `tegu_air_render_pipeline_plan_generic_specialized` returns the unchanged
  `TeguAIRGenericPlan` plus `TeguAIRGraphicsSpecializationPlan` (3360 bytes):
  per-stage `{id,size,data[8]}` tables (64 max each), exact selected Vk view
  format/type/aspect/level and Metal alias, per-use compare, twin/shader
  swizzle flags, and the planner key bytes. It admits real linear/mip/address/
  compare/anisotropy/LOD sampler facts, bounded 2D mip chains and 2D arrays
  (1..16 levels, 1..256 layers, product<=256), and installed ASTC LDR/sRGB 2D
  facts (new-only). Same SpecId in VS and FS stays independent; an ambiguous
  id that the shared plan cannot distinguish refuses. The old `plan_generic`
  path keeps its default-specialization-only behavior and bytes and refuses
  the richer class.
- Real authored-probe tests: old `plan_generic` refuses linear sampler and
  4-level mip facts; new specialized planner returns exact per-stage constants
  and view fields. A synthetic 2DArray 4x4-level state test reaches the
  translator table only on the new path. This is not a nearest/level0
  substitution.
- Rust vertex fetch: VkFormat83 is admitted with the same width/bounds path as
  the already-qualified formats; test emits format83 and refuses RGBA8.

### Delta10: compositor experiment switch + shared process helper
- `TEGU_COMPOSITOR_DEFAULT_ON` makes `tegu_compositor_opt_in_exists()` return 1
  for an explicit experiment build.  Without the macro, the existing host seam
  or `/private/var/db/nord2-metal/compositor-enable` check remains.
- `tegu_compositor_process_is_backboardd()` is exported and declared in the new
  `metal/tegu/compositor_process.h`; render must use this same process check.
- The frontend host test builds once without the macro (backboardd denied with
  no file, enabled only by file) and once with it (backboardd enabled with no
  file).  Both retain delta5/delta8 coverage.

### Delta9: true-MRT per-target blends bridge v2
- `nord2_metal_bridge.h`: `N2MB_RENDER_TARGETS_BLEND_VERSION=2` and an
  additive `const struct tegu_blend_inputs *blends` field at the end of
  `n2mb_render_targets`.
- `nord2_metal_bridge.c`: version2 requires a non-NULL array and validates
  every blend write mask and `tegu_blend_build_format` before retaining target
  BOs; the resolved n2r sidecar carries the version and pointer unchanged.
  Version1 explicitly ignores a non-NULL blends pointer.
- Host tests cover distinct per-target blend descriptors (v9 MALI_BLEND
  unpack), version1 blend bytes with NULL/non-NULL pointer, and version2
  NULL/mask/equation refusals before submit.

### Delta8: MTLFeatureSet compatibility + capability trace
- `supportsFeatureSet:` is implemented on `TeguComputeDevice`.  iOS GPU
  Family1/2 feature-set values (0,1,2,3,5,6,8,9,12,13) are true; Family3/4/5,
  macOS/tvOS and unknown values are false, exactly matching the Apple1/Apple2
  supportsFamily claim.  Non-graphics builds return false.
- `tegu_device_query_trace` emits bounded `TEGU_DEVQ sel=... arg=... ret=...`
  for supportsFeatureSet:, supportsFamily:, supportsTextureSampleCount:,
  isDepth24Stencil8PixelFormatSupported and supportsSampleCount:.  First 64
  per process, always on; the old TEGU_DEVICE_QUERY remains render-trace gated.
- `check_frontend_delta5.py` now also tests all feature-set mappings, the
  actual device methods, and exactly 64 TEGU_DEVQ lines with all selectors.

### Delta7: host cdylib/producer refresh + graphics TLS
- Rebuilt the worker host cdylib from production `air_bridge.rs` d8854273:
  `9ff66cacdd22775dc1ea121f663537df8e3731cff4453c2fd68a74d10673b4b9`.
  Rust suite 29/29.  Real producer 7/7 against compiler
  `ad62ed9e405feb8fa85b1b3d641b6dc1ee6357c9059dbc73ff2c79c58e052e72`;
  receipt `build/compute-v2-producer-current-abi/result.json` now records
  compiler ad62ed9e, cdylib 9ff66cac and source d8854273.
- Graphics TLS in `draw_locked`: when `n2r_draw_options.thread_storage_bytes`
  is nonzero, the bridge queries the core range, calls
  `n2r_thread_storage_total(bytes,2048,cores)`, refuses over 64 MiB or any
  helper/status/allocation failure before submit, allocates a 4096-aligned
  internal BO, sets `n2r_addresses.tls`, retains it read-only in the JM
  request, and releases it after completion/failure.  Bytes0 leaves the old
  packet untouched.

### Delta6: bounded generic MRT bridge / R8
- `nord2_metal_bridge.h`: additive `n2mb_render_target(s)` sidecar
  (1..4 BO tokens, offset/length, format/load/write mask, row/width/height,
  clear words) and `n2mb_draw_v4`; no change to v3 callers.
- `nord2_metal_bridge.c`: `n2c_mrt_prepare` resolves/validates each target BO
  and retains all targets write in the JM request; `n2mb_draw_v4` calls
  `n2r_build_v4`.  R8 (format10) v3 per-RT jobs get a one-target v4 build with
  the actual R8 row; attachment/framebuffer-fetch on true-MRT jobs is refused
  until per-target snapshots exist.  Non-R8 v3 stays on the exact old path.
- `air_bridge.rs`: `generic_target_formats` admits bounded `[80,80,10]`,
  `[80,80,10,10]`, `[80,80,10,80]` (up to4; first two BGRA8, rest R8/BGRA8);
  single and `[80,80]` remain.  Other target classes refuse explicitly.
- Worker test `tests/check_mrt_bridge.c` (derived from the independent
  integration draw harness) covers true 4-target v4, per-target addresses/
  formats/rows/write masks, RT2-only mask, sample locations, target retention,
  R8 v3, and duplicate/span/shape refusals.

### Delta5: compositor/device startup hardening
- `compute_frontend.m` `initWithAcceleratorPort` now queries capabilities
  through a bounded retry helper: 250 attempts at 20 ms (about 5 s), retrying
  only `kIOReturnNotReady` (0xe00002d8) and failing immediately for other
  errors or malformed success.  The first valid 4-word capability reply sets
  the device capabilities.
- A process-name/file gate runs before IOKit setup.  Non-backboardd processes
  keep their normal device behavior.  `backboardd` is denied unless
  `/private/var/db/nord2-metal/compositor-enable` exists; denial returns nil
  before any Nord2 connection/GPU work.  The bounded diagnostic line is
  `TEGU_COMPOSITOR_GATE enabled=0|1`.
- `newPipelineLibraryWithFilePath:error:` is overridden on
  `TeguComputeDevice`; it never forwards to `_MTLDevice`, returns nil and sets
  `NSError` domain `TeguPipelineLibrary` code1.
- Delta4 TLS/WLS allocation remains in `nord2_metal_bridge.c` with its
  v9_pack host oracle; no change in this delta.

### Delta3: mip/layer producer facts and swizzle validity
- `TeguAIRComputeResource` appended `swizzle_valid/reserved2` and
  `level_offset[16]`, `level_row_stride[16]`, `level_slice_stride[16]`,
  `level_layer_stride[16]` (488 bytes; original160 offsets unchanged).
- Rust validates a contiguous linear mip chain: exact offsets, level-0
  row/slice/layer strides, minified dimensions and layer stride; levels1
  ignores the arrays and remains byte-identical. Non-contiguous/arbitrary
  per-level strides refuse explicitly.
- `compute_frontend.m` fills the facts from `TeguTexture.mip[]`; the TDG2 v2
  wire remains 192-byte descriptors/6304 bytes. The bridge derives levels
  layer-major/level-minor using the textures helper; ordinary levels1 uses the
  established 16-plane layout, mips/>16 layers use 23 planes only for that
  command.
- `swizzle_valid` is copied to TDG2 flag 0x2000 by runtime_pipeline; producer
  explicit-zero RRRR is representable.
- Atomic wire flags remain an explicit refusal with the precise log reason;
  transport/write tracking would require JM hazard metadata.

### `gpu/metal/nord2_metal_bridge.c/.h`
- Selector 5 imports TGP1/TGP2; TGP2 code is copied from parsed `code_offset`.
- Selector 7 dispatches by exact wire size; TGP1 path byte-identical.
- TGP2 dispatch:
  - exact manifest/wire correspondence, combined pair flags, compare flag,
    shape/table/entry/access checks; bound Metal slot ranges (buffer/texture
    0..30, sampler 0..15) and combined sampler identity 0..15;
  - declared wire span plus relative level offset is the n2t layout bound, so
    a span one byte short of the actual layout refuses instead of receiving
    the remainder of a larger BO;
  - nonzero and explicit-zero swizzles set `swizzle_valid`; storage-image
    swizzles are refused because they are not observable;
  - SRT pointer count wrapper `n2mb_compute_srt_pointer` validates alignment,
    count and table span, then encodes count4; v1 leaves resources0;
  - all level0 layers packed up to 16 per descriptor; nonzero base + relative
    level offset applied exactly once;
  - SHADER_PROGRAM emitted exactly as v1; ordinary no-local-memory
    LOCAL_STORAGE remains byte-identical; write masks for writable
    buffers/images; texture reads not invalidated;
  - **Delta4**: package TLS/WLS words now allocate owned BOs per dispatch.
    TLS uses `pan_get_stack_shift` + `pan_get_total_stack_size` with the
    arch9 512 threads/core and `shader_present` core-ID range; WLS uses
    `pan_wls_adjust_size` scale and `min(next_power_of_two(grid),
    core_id_range*512)` instances (conservative upper bound because status
    lacks `THREAD_FEATURES`), 4096-aligned/non-crossing-4GiB base, merging
    still off.  Both handles are added to the final JM request and released
    only after completion/failure.  Allocation failure is BAD before submit;
    no zero LOCAL_STORAGE is ever emitted for a nonzero TLS/WLS package;
  - sampled numeric class is checked from the effective view format (view
    alias, else allocation), not just the allocation; IMAGE class likewise;
  - native sampler uses the translated use resolution: normalized=1 always,
    integer-use (`sampler_flags&2`) forces nearest min/mag/mip and no
    anisotropy, and the format passed to the shared sampler packer is the
    effective view format. Original Metal state remains in the wire and
    specialization key;
  - additive `n2mb_draw_v2` for render; old `n2mb_draw` bytes unchanged.
- `n2mb_compute_srt_pointer` remains isolated; frontend_audit confirmed the
  real selector9 path accepts the count4 pointer.

### `gpu/metal/tegu/compute_frontend.m` / `runtime_pipeline.inc`
- TeguEncoder retains textures[31] and samplers[16], exact sampler setters,
  v2 wire lifetime and object retains.
- `dispatchThreads` enqueues the translator's exact regions; no padding.
- Per-region local check uses `plan.regions[r].local`, not the original
  requested local. v1 path unchanged.
- Exact sampler factory is enabled from device construction (before first app
  sampler creation) when the v2 compiler is linked, not at first v2 submit.
  The call is guarded for non-graphics host builds.
- PSO falls back to weak `tegu_mali_compile_compute_v2` only if TGP1 refuses;
  missing v2 is an explicit refusal.
- `tegu_v2_variant` validates package version/manifest on every cache hit and
  returns owned references.

## Tests actually run (host only)

1. Rust full suite: 28/28 PASS.
   Command:
   `TEGU_COMPUTE_FIXTURES=<worker>/air-work/fixtures TEGU_RENDER_FIXTURES=<worker>/render/authored CARGO_TARGET_DIR=<worker>/cargo-target CARGO_BUILD_JOBS=1 cargo test --release`
   Includes real TestFastMath sampled resources, exact 17x9 region locals
   `{8x4,8x1,1x4,1x1}` cross-checked against per-region constants,
   accumulateKernel bound-format variant, and the real linear-sampler
   specialized-plan test. ABI test prints the exact offsets above.
2. New matching Rust cdylib: `build --release` SHA256
   `06a6c6ee4e45f3b5d2bb4c4a7462d2054f81db5f8f9df564acfb4f1eac158b06`.
3. C suite: `python3 workers/compute/check_compute_v2.py` release +
   ASan/UBSan PASS. Covers TGP1 regression, TGP2 code_offset import, manifest
   flags/collisions/domains/formats, SRT count, tables/dummy, span1023 refusal,
   swizzle nonzero and explicit-zero flag, view-format numeric class, sampler
   view format, 2-layer planes, native sampler31, combined/compare mismatches,
   atomic refusal, draw_v2 null path, and Delta4 TLS-only/WLS-only/mixed
   allocation against the generated `v9_pack.h` oracle (including exact
   pointer/shift/scale/instances, no-local byte identity and partial-allocation
   release on refusal).
4. Delta6/7 MRT+TLS host test:
   `python3 workers/compute/check_mrt_bridge.py` PASS release and ASan/UBSan:
   **67 checks, 0 required failures** each.  Covers true 4-target MRT,
   per-target version2 blends, R8 v3 Clear/Load, and graphics TLS
   64B/2048/core with v9 LOCAL_STORAGE unpack, shared DRAW/SHADER_ENVIRONMENT
   pointer, read-only request retention, post-wait release, over-64MiB and
   over-32KiB refusals.  Existing integration draw source: **33 checks/0
   required** for v3 defaults.
   Rust full worker suite: **29/29 PASS**, including bounded MRT admission.
5. Delta5/8/10 frontend host test:
   `python3 workers/compute/check_frontend_delta5.py` PASS release+ASan/UBSan
   for **both** default-off and `-DTEGU_COMPOSITOR_DEFAULT_ON` builds.  Covers
   250-attempt retry, both compositor gate states, exported process helper,
   pipeline-library nil/NSError, all MTLFeatureSet mappings, and exactly 64
   TEGU_DEVQ lines.  Existing `check_runtime_v1_aligned.py` still PASS.
6. Real compiler producer: `metal/tegu/check_compute_v2_real_producer.py`
   with compiler `libnord2_mali_runtime.so ad62ed9e...`, producer cdylib
   `9ff66cac...`, producer source `metal/tegu/air_bridge.rs d8854273...`:
   **7/7 PASS**. Evidence persisted at
   `gpu/build/compute-v2-producer-current-abi/result.json` (the older TLS/742
   receipt remains at `compute-v2-producer-tls-742/result.json`):
   TestFastMath nearest/linear, four exact-thread edge packages, accumulate.
   Each live manifest binding/kind/access matches the planner; package local
   headers are 8x4,8x1,1x4,1x1.
7. Independent integration suite: delta10 is **737/737 PASS**, 0 required,
   0 pending, release+ASan; candidate provenance matches cdylib 9ff66cac /
   compiler ad62ed9e / source d8854273.  Delta8 was also 737/737; delta9 was
   737/1 while the render harness was mid-edit.  The run was invoked from
   the overnight/work root. ABI now accepts resource488 and TLS/WLS word17/18;
   TGP2 156/156; job113; draw_v2 23; VkFormat83 13; real producer59/59; corpus
   graphics no signals; native compute frontend; render variant cache; legacy
   sampler guards and two-thread variant ASan. Report:
   `workers/compute/evidence-integration-delta10.{log,json}`; Delta4 rerun is
   `evidence-integration-delta4.{log,json}`; Delta5 rerun is
   `evidence-integration-delta5.{log,json}`.
   Note: the native subtest's `gpu/build/native/probe-compute.bc` path is
   cwd-relative; run the suite from overnight/work, not from the worker dir.
8. Textures private bridge harness copied from integration: **134/134 PASS**
   release + ASan/UBSan, including 3-level mip, 3D depth2x2 and 17-layer
   arrays; ordinary levels1 16-plane bytes remain green.
9. Frontend v1 aligned host harness: PASS
   `python3 workers/compute/check_runtime_v1_aligned.py` (real frontend, real
   Rust bridge, baseline Mali compiler; 256/512-aligned worker fixture).
10. Full native build, device boot, CI, publish: NOT run. No hardware claim.

## Render handoff facts
- `n2mb_draw_v2` is a strong non-static export at
  `metal/nord2_metal_bridge.c:833`, declared at
  `metal/nord2_metal_bridge.h:33`; integration decodes/retains its sidecar.
- Exact sampler factory enable is `metal/tegu/compute_frontend.m:736`, inside
  `TeguComputeDevice initWithAcceleratorPort`, guarded by
  `TEGU_GRAPHICS_BRIDGE`/`TEGU_AIR_BRIDGE` and
  `if(&tegu_mali_compile_compute_v2)` (the `&` avoids the ARM64 -Werror
  pointer-bool conversion).
- `air_bridge.h eea1de13...` is the authoritative
  `GraphicsSpecializationBinding` field layout:
  `binding,stage,index,kind, view_format_vk,view_format_metal,
  compare_enabled,view_type, view_level,view_layer,flags,aspect`.
  flags 1=twin, 2=shader-swizzle. Render must pass both per-stage tables to
  `tegu_mali_compile_generic_pair_specialized(_rt)` and include both tables in
  cache identity.

## Hashes (current working tree)

- `pipeline_format.h` `ff2889fc...` (compiler manifest part unchanged from
  `dfc0a410...`; differences are the additive TDG2 swizzle-valid flag, the
  separate 1 MiB TGP2 code bound, and word17/18 TLS/WLS fields). The 15 captured generic TGP2 refusals are
  all code>4096 and parse rc0 with this header; evidence
  `workers/compute/evidence-tgp2-code-cap.json`. The current compiler
  `57db4be4...` was built from the pre-TLS header (51cf599d); replay of all 15
  failing SPIR-V compiles 15/15 and the real producer 7/7 uses it. Lowering
  still must rebuild L2 with ff2889fc to include word17/18 TLS/WLS.
- `air_bridge.h` `9f0fa751...`
- `air_bridge.rs` `d8854273...` (Delta6 bounded MRT planner admission)
- `nord2_metal_bridge.c` `494a027c...` (Delta9 per-target blends)
- `nord2_metal_bridge.h` `8f385cb6...` (Delta9 v2 blends sidecar)
- `compute_frontend.m` `9d12b0f8...` (Delta10 default-on/helper)
- `runtime_pipeline.inc` `92dc2c11...`
- Private Rust cdylib `9ff66cac...`; worker `air-work/src/lib.rs` is a
  byte-identical copy of `air_bridge.rs`.
- Compiler `ad62ed9e...`; producer receipt
  `build/compute-v2-producer-current-abi/result.json`.

## Unresolved gaps (explicit, not counted as support)

1. TDG2 mip/layer transport is a bounded contiguous-linear convention: the
   texture helper packs all levels/layers layer-major/level-minor up to the
   per-command plane-slot bound (16 ordinary, 23 when mips or >16 layers).
   Arbitrary per-level row/slice strides or non-contiguous offsets refuse.
   Larger arrays need a separate payload ABI. `TeguAIRComputeResource` carries
   the exact 16-level producer facts; Rust validates them before dispatch.
2. Atomic RMW transport/write tracking is not implemented; atomic wire flags
   refuse and atomic-only images are never downgraded to read-only.
3. Sampler border resolution is only partially complete: opaque
   black/white ClampToZero is mapped to ClampBorder and the shared packer
   handles it, but custom raw/float borders, integer custom borders and some
   depth/integer ClampZero combinations still refuse explicitly. Do not claim
   exact sampler parity for those states.
4. `render`'s generic transport still needs to call the new specialized
   compiler entry with the sidecar tables. The ABI/logic is implemented and
   tested; the render-side compile/cache wiring is a cross-owner remaining
   step. Mips/array/MS generic planning remains refused until that transport
   is wired.
5. Producer explicit-zero swizzle is represented by
   `TeguAIRComputeResource.swizzle_valid` and TDG2 flag0x2000; the current
   frontend leaves the bit clear for ordinary textures (format default), while
   a future swizzle source can set it. Nonzero words remain accepted for
   compatibility.
6. Lowering rebuilt L2 at 742cdf3d with TLS/WLS live; native use still needs
   root L4 rebuild/device verification.  The host bridge now allocates and
   packs TLS/WLS, but no GPU has executed a TLS/WLS packet.
7. WLS instance count is a safe upper bound, not the exact Mesa
   `pan_calc_wls_instances`: `nord2_gpu_status` exposes the shader-core mask
   but not `THREAD_FEATURES`, so `max_tasks_per_core` is unavailable.  TLS
   uses 512 threads/core (arch9) and WLS may overallocate; adding
   thread_features/max_tasks to the status ABI would allow exact sizing.
8. TLS/WLS BO ownership is host-tested only; native completion/fault quarantine
   behavior still requires root device evidence.
9. The startup sampler factory is now at device construction gated on the v2
   compiler symbol; it has not been exercised in a real process/device.
10. The integration native subtest uses a cwd-relative probe path; run the
   suite from overnight/work. It is test-only and currently passes there.
11. `check_runtime_frontend.py`'s 128/384 fixture alignment issue remains
   outside compute ownership; the 256/512 worker copy passes.
12. No GPU execution/pixel/IOSurface-coherence evidence.
13. Delta6 true-MRT `n2mb_draw_v4` is tested but not wired by render; the
    bounded render path is per-RT `n2mb_draw_v3` (including R8).  True-MRT
    LOAD/framebuffer-fetch snapshots and public v4 integration remain explicit
    gaps.  Planner admission is limited to the posted format/count classes.
14. Delta9 version2 blends are a caller-owned pointer for the draw call; the
    bridge does not copy or retain that array.  Render must set version2 only
    for true-MRT jobs and keep the sidecar alive through submission.
15. Graphics TLS uses the conservative 2048 threads/core bound and a 64 MiB
    per-allocation cap; exact hardware thread/TLS allocation limits are not
    device-proven, and large per-thread TLS can explicitly refuse.  TLS BO
    lifetime is host-tested only.
15. Delta8 feature-set support is limited to the Apple1/Apple2 compatibility
    tier; Family3+ is false until implemented.  The DEVQ trace is intentionally
    capped at 64/process, and all feature-set/trace evidence is host-only.
16. Delta10's `TEGU_COMPOSITOR_DEFAULT_ON` is an experiment build switch, not
    the default; render must call the exported process helper for DELTA8.
17. Delta5 retry/gate/pipeline-library are host-tested with a fake transport
    and real frontend code; native ARM64 build, device SpringBoard/backboardd
    behavior and the `/private/var/db/nord2-metal/compositor-enable` file
    remain root/L4 device work.  `getprogname()` is the process-name source.

## Files changed
- `gpu/metal/tegu/pipeline_format.h`
- `gpu/metal/tegu/air_bridge.h`
- `gpu/metal/tegu/air_bridge.rs`
- `gpu/metal/nord2_metal_bridge.c`
- `gpu/metal/nord2_metal_bridge.h`
- `gpu/metal/tegu/compute_frontend.m`
- `gpu/metal/tegu/compositor_process.h`
- `gpu/metal/tegu/runtime_pipeline.inc`
- worker tests: `workers/compute/tests/check_pipeline_format_v2.c`,
  `workers/compute/tests/check_compute_v2_bridge.c`,
  `workers/compute/check_compute_v2.py`,
  `workers/compute/check_runtime_v1_aligned.py`,
  `workers/compute/check_frontend_delta5.py`,
  `workers/compute/tests/check_frontend_delta5.m`,
  `workers/compute/check_mrt_bridge.py`,
  `workers/compute/tests/check_mrt_bridge.c`,
  `workers/compute/DELTA4.md`..`workers/compute/DELTA10.md`,
  `workers/compute/air-work/*` (private Rust harness; no Apple artifacts
  committed).
