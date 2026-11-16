# AIR -> NIR direct compile for render pipelines (design, 2026-10-28)

Owner: lowering (Claude Opus, mali_*_compiler.c/.h). Source of the frontend: ansuz-metal
`userland/rottenapple-native-metal/air/` (MIT, notices kept), vendored unchanged as
`gpu/metal/tegu/air/` (upstream hashes in `air-upstream.sha256`). Private: never published.

## What ansuz does (reference)

`nm/rottenapple_nm.c nm_compile_air_locked`: `air_to_nir()` (bitcode + function constants by
name) -> early NIR passes -> `pan_preprocess_nir` -> `ra_nm_lower_res_indices` (flat Metal
slots: buffer n = UBO/SSBO n, texture n, sampler n, constexpr samplers at 16+k) ->
`pan_postprocess_nir` -> sysvals into draw UBO 31 -> `pan_shader_compile`. Varyings linked by
name (FS input names passed to both stages). Their encoder binds by Metal slot directly, and
any refusal falls back to m2v per pipeline.

## Our constraint

Our draw side (render_encoder / Rust generic planner) binds by the **m2v binding plan**:
set-0 binding N -> resource table 1 entry N (buffers, textures, input attachments) and table 2
entry N (samplers); Metal buffer offsets arrive as a descriptor base plus a 4-byte residual in
an m2v push word (`SlotBindOffset`, AIR push space, FAU byte 128+); per-variant runtime facts
(pixel-coordinate samplers, ...) arrive as m2v SpecId values. Changing that ABI would touch
render/compute/encode owners and the pipeline cache, so the AIR path adopts it (adapter).

## Adapter (compiler side, one place: `mali_graphics_air.inc`)

Input per compile: both AIR functions (bitcode or metallib+entry, function constants by
name/index), the Metal vertex attributes, the **m2v metadata JSON of the same translation**
(the `meta` blob the bridge already returns with the SPIR-V), the m2v SPIR-V (only for the
existing SpecId-table validation), the same vc/fc tables and options as
`tegu_mali_compile_generic_pair_options`. Output: exactly the same structs (code, pair2,
generic plan, varying info), so render_pipeline.inc/encoder/cache need no new draw logic.

Mapping, from `layout.sets[0].bindings`, `interface.push`, `stages[].specialization`,
`vertex_input` of the meta:
- `load_ubo/load_ssbo(Metal buffer i)` -> handle table1[N] of the `Buffer{stage,i}` binding,
  offset += push word of `SlotBindOffset{stage,i}`; plan kind UBO (ty 6) / RO storage (ty 7).
  Stores -> refusal (m2v's class is read-only).
- `tex(texture t, sampler s)` -> `CombinedTextureSampler{texture t, sampler Slot s}` binding N
  (table1[N]/table2[N]); constexpr sampler -> the `Constexpr` combined binding of texture t.
  Separate Texture/Sampler bindings likewise. txf without sampler -> table2[N] as today.
- Pixel-coordinate samplers: m2v specializes `SamplerState{slot,fields}`; bit0
  (PIXEL_COORDINATES) of the per-variant value (or a constexpr sampler's coord::pixel) scales
  the coordinate by 1/size(level 0) in NIR (frontend sampler descriptors stay normalized).
- `[[color(n)]]` reads -> the input-attachment binding of attachment n as a subpass load, then
  the existing memory/tile fetch lowering in `lower_generic_resources`.
- Vertex attributes: Metal attribute index -> m2v location by (buffer_index, offset) match.
- 16-bit colour outputs widened to f32 (the blend conversion words are built for f32).
- Everything else is a refusal with a reason (fallback to m2v): variant_bits != 0 (sRGB raw
  decode, YCbCr, cubic, cross-class...), push fields other than SlotBindOffset/ColorWriteRule,
  spec keys other than SamplerState/ConstantLanes(=0), constant blobs, pulled attributes,
  argument buffers, storage images, RUNTIME_SPEC (stand-in keeps m2v), any NIR the backend
  checks refuse. ColorWriteRule is m2v's own colour rounding emulation and is not read.

## Frontend (TEGU_AIR_DIRECT, default off)

`tegu_render_compile_generic_options` (render_pipeline.inc) tries the AIR entry first when the
pipeline carries AIR sources + meta, then the unchanged SPIR-V entry on any nonzero status.
Pipeline cache key gets an `air` salt. `TEGU_PERF` line counts air-direct vs m2v compiles and
refusal reasons. Needs (board claims first): a Rust export of a function's AIR module bytes and
constant values (air_bridge.rs, compute owner), meta retained beside genericVsBytes
(render_pipeline.inc), build lists (build_native_compilers.py; host meson/private script).

## Proof (host only)

`tools/air_vs_m2v.py`: all unique `TPC_MISS v1 air` records of pipeline-corpus/corpus.txt,
both paths through the same entry options, Valhall stats (`BIFROST_MESA_DEBUG=shaderdb`),
refusal ledger. Then pixel checks through the render harness / CA host test with
TEGU_AIR_DIRECT=1, and the standard gates.
