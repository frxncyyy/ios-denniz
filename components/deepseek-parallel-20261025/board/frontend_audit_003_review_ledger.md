# Frontend audit coordination ledger

Read-only production review; no build or device activity. This records the
initial interface review, not completion of implementation. Timestamped board
messages are authoritative for later replies and fixes.

## Agreements established

- Active compute transport is `nord2_metal_bridge.c` method 7; package import
  is method 5. The old TGP1 path remains byte-compatible.
- Root's existing ownership grant covers the declared common texture helper
  owned by textures and new compute ABI helpers owned by compute. Compute
  calls the common packer; it must not create a competing format/plane packer.
- Lowering explicitly ACKed a new `tegu_mali_compile_compute_v2` entry,
  preserving old compile entries. Native set-0 binding N maps directly to
  table 1[N] for textures/images and table 2[N] for samplers. Metal slots are
  separate, mapped by the Rust descriptor plan. No table 3 or Metal-slot-based
  handles. First proposed range is N 0..255.
- Samplerless fetch/query uses internal table 2[256]. No application sampler
  is required. Highest referenced index plus one determines table size.
- TGP2 requires a serialized resource manifest and checked header, manifest,
  code offsets. Counts alone do not identify resources. Kind, access, shape,
  storage format, table and entry must match the selected bound-format plan.
- First v2 buffer FAU policy remains bindings 0..7 unless separately extended.
  Worker limits such as 8 textures/4 samplers are capacity policies, not the
  full corpus coverage claim. Real declared maxima are in resource-bounds.json.
- For existing generic render jobs, retention keyed by native binding in
  compositorTextures/compositorSamplers is compatible with current cleanup,
  shadow iteration and reorder iteration. Fixed/compositor jobs retain their
  existing Metal-slot indexing; slot 30 MRT retention remains distinct.

## Required cross-owner integration

- Texture/sampler full definitions occur after runtime_pipeline.inc. Use
  early helper declarations and later definitions; avoid reading undefined
  Objective-C ivars from runtime.
- Texture allocation, upload, readback, mip arrays and texture views are in
  render_encoder.inc (render-owned). Packer support alone cannot expose new
  array/cube/view behavior through Metal.
- A sampler needs per-use compare/integer-border facts. Image descriptors
  need the translator's actual view format, level, components and shader-
  swizzle semantics, not just allocation pixelFormat.
- Neutral binding provenance is already available from
  pipeline.layout.sets[].bindings[].source. Compute can fill existing
  GenericBinding.stage/index from Texture/CombinedTextureSampler/
  SeparateTexture/RawTexture sources; render can then distinguish an active
  unsupported texture from an unrelated unused binding.
- VertexIndex identity is distinct from attribute rebasing. Indexed attribute
  expansion can lose original IDs. Direct indexed vertex pulling already has
  an index-buffer transport. Lowering confirmed original hardware IDs are
  preserved there: do not refuse just because drawIndexSize is nonzero.
- Valhall hardware has signed primitive.base_vertex_offset; TGJ1 omits it.
  Lowering cited v9.xml and pan_jm.c 740-749. A proposed additive render
  parameter can set actual first vertex/index bias while attribute descriptors
  retain their original base (no double rebase). Owners must coordinate the
  snapshot/bridge/builder and range tests; this is not yet implemented.

## Initial draft findings sent to owners

Texture draft SHA256 reviewed before promotion:
`785f2fec83a4a79534ef834836fc64458d3eb2064cc5dff5bb5fad8e91d2d546`.
These are pending until owner reports fixes and independent tests.

1. Uncompressed formats left clump format at RAW8; BGRA8 requires RAW32,
   RGBA16F RAW64. Pinned v9 XML defines RAW8/16/32/64/128.
2. Cube kind 5 was required to have both one layer and a multiple of six,
   making every ordinary cube invalid. Cube width and height must agree.
3. ETC2 RGB8/RGB8A1 used 16 bytes per block; pinned format YAML defines eight.
4. first_layer was checked but not applied to addresses; choose and document
   whether offsets already contain view origin, then test nonzero views.
5. swizzle_word zero meant default, but zero is valid Mali RRRR. Use an
   explicit default sentinel, validate high bits, and convert Metal component
   IDs to Mali IDs rather than copying them.
6. Public pack calls accepted zero backing length/address because internal
   geometry validation allowed omitted bounds. Real pack calls need a bounded
   resolved allocation, with overflow/end-address checks before output.
7. Convenience diagnostic helpers used a two-backslash-zero multichar literal
   instead of NUL at promoted header lines 998/1028/1067.
8. Render draft exempted constant extras in one reflection check but rejected
   them again in the later snapshot loop. Identify the exact plan/native extra
   binding in the real probe and test the final snapshot, not isolated stages.
9. Initial n2r_build_v2 still used legacy gd_request_ok, requiring the texture
   memory span to equal one row-stride times height. Real multi-mip/layer spans
   need a distinct v2 validator and complete bridge-resolved window; a sidecar
   must never authorize access beyond the resolved request span.
10. Initial sidecar documentation incorrectly indexed n2r_addresses.binding
    by native binding N. The actual array is indexed by q.bindings position i.
    Test sparse native bindings 3/9 in request positions 0/1 with different BOs.
11. **Resolved at source review:** render removed the new rc == -13 elision;
    generic refusals again call tegu_render_trace_silent and invalidate the
    Nord2 command. Worker updated the actual frontend test to require command
    error and no draw job, and reported PASS. No independent rerun by this
    read-only reviewer. The stale nearby skipped-draw comment still needs
    correction to describe the Nord2 error path.

Correction to an earlier audit message: pinned PanVK returns MALI_FUNC_NEVER
when compareEnable is false, not ALWAYS. The texture draft's disabled-compare
value zero is correct; a distinct per-use compare-enabled fact is still needed.

## Review and qualification gates still pending

- Exact TGP2 header/manifest/wire offsets and parser implementation.
- Compiler implementation of the agreed v2 entry and resource lowering.
- Actual Rust variant planning, C frontend resource snapshots and active n2mb
  table assembly/lifetime/coherence.
- Texture helper fixes and independent Mesa-generated descriptor tests.
- Final render snapshot tests for VS/FS same Metal slot, unused unsupported
  binding plus legal neutral, actual bound sampler filters and resource release.
- Root-controlled integration build and frozen-hash corpus replay.

No new path is hardware-qualified by this ledger, and no refusal is counted
as implemented shader support.

## Subsequent review evidence

- Render reports actual final frontend magma snapshot and same-slot VS/FS
  lifetime tests passing, plus existing frontend_jm_test and ARM64 syntax
  check. Tests use a fake kernel and provide no hardware qualification.
- Corrected the Icon format namespace: input Metal values29/25/27 translate
  to native VkFormats103/83/97/103. Saved metadata for all12 fetch-refused
  VS-texture jobs includes83 (R16G16_SFLOAT); other formats97/103/106 already
  pass. Their vertex descriptors are inferred synthetic layouts, not captured
  runtime layouts. Worker corrected STATUS and survey.
- Private compute Rust build.log reports EXIT=0 after7m07s; process inspection
  found no remaining cargo/rustc/Mesa processes. The narrow lease must be
  formally released by compute before another heavy build is started.
- Actual linear sampler state still needs exact Rust planning and compiler
  specialization. Nearest facts with a linear hardware descriptor are not an
  implementation of that contract. Compiler owner was asked for additive
  graphics specialization API guidance.

## Integration review gate (current worker drafts)

Root has now assigned vertex_fetch.c/h to lowering and authorized implementation
of the agreed graphics-specialization API. The render/texture sidecar signature
is structurally agreed, but this is not correctness approval or a release gate.

- Textures reports 687 helper checks plus n2r/legacy descriptor tests passing.
  Independent source review still finds ETC2 RGB8/A1 block size16 instead of8,
  swizzle0/default collision and unchecked high bits, public pack accepting
  zero backing length, nonsquare/overlayered ordinary cubes, ambiguous layer
  view origin, and wrong positional-address documentation. Clump/NUL/orphan
  fixes are now present. Additional tests were explicitly requested.
- The n2r v2 preflight still uses the legacy sampled-format whitelist, so many
  helper-supported formats cannot reach it. A per-binding v2 validator must
  validate the authoritative sidecar while preserving old checks for bindings
  without one; do not bypass this with legacy format0/default substitution.
- Current TGP2 draft still documents storage table3 and has only an8-byte
  binding/kind manifest. It conflicts with the compiler ACK. Compiler v2
  implementation remains contingent on corrected concrete serialization.
- Current C/Rust ComputeRegion layout differs: C1168 bytes versus Rust1172
  due to a Rust-only flags field. Computed LP64 dispatch sizes are10696/10728
  respectively, not the asserted8676. This was independently checked with
  ctypes field alignment; no build was run.
- New ComputeDescriptor omits ImageViewDesc and SamplerUse properties needed
  for exact descriptor aliases and comparison/integer semantics. Its backing
  span helper also ignores mips/MS/block rows/cube faces. Use the shared layout
  contract and exact per-use facts, not guessed allocation metadata.
- Full sampler behavior is not reachable through an invented exact-state
  Objective-C selector alone. The ordinary Metal factory and every active
  consumer must retain/route the full state without dropping fields.

Lowering closed the completed private Rust build lease after EXIT=0 and process
verification. No new heavy build is authorized by that closure.

## Superseding review at 15:22 UTC

- Textures fixed the seven requested helper issues and the v2 format gate.
  Source and new tests are verified; worker reports724 checks plus sanitizers.
  One new NULL-layout read before validation was found and sent for correction.
  The new swizzle_valid field changes the local layout: rebuild all consumers.
- Compute has replaced the old8-byte requirements with a128-byte header and
  64-byte manifest records using native table1/2. The original manifest
  complaint is superseded. Remaining ACK questions are exact shape/format enum
  domains, bound image-view facts unavailable to the compiler, and collision/
  irrelevant-field rejection. No compiler implementation approval yet.
- The current gate matrix is historical evidence for each finding; owner
  fixes and review confirmations override its initial current-finding text.
