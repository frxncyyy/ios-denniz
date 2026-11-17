# Frontend audit 002 — additive interface proposal / decisions needed

Author: frontend_audit. This is a concrete coordination proposal, NOT a declaration that APIs below already exist. Each owner should publish ACK or an exact replacement before consumers code against it. Root integrates and builds. No production edits by audit.

## Sole ownership / collision boundaries

- Compute owns compute_frontend.m, runtime_pipeline.inc, air_bridge.rs/.h, pipeline_format.h AND nord2_metal_bridge.c/.h (actual active transport).
- Texture owns texture_state.c/.h, sampler_state.inc, nord2_render_resources.inc, nord2_metal_render.c/.h; prefer NEW common pure texture/sampler descriptor files for compute reuse.
- Render owns render_encoder.inc/render_pipeline.inc and its new tests. It must request edits to air_bridge.rs/.h and n2mb bridge through the compute owner, and n2r builder edits through texture owner.
- Native compiler agent owns mali_graphics_compiler.c, mali_compute_compiler.c/.h, resources.inc. Ask root before changing those files or their compiler APIs. Coordinate generic-render specialization support explicitly.
- generic_job_ui.h currently belongs to no worker. Keep its existing structs byte-compatible. Propose new sidecar/versioned request header if extra mip/layer/sampler/base-ID facts are needed; request ownership before any edit to shared legacy header.

## Common texture descriptor API (texture owner, proposed)

Use a NEW `gpu/metal/nord2_texture_resources.h` (and .c or header-only implementation) with an explicit `N2T_RESOURCE_VERSION=1`. Both compute and render call the same validated builder. Do not reuse arch11-only tegu_texture_state_build as arch9 evidence; its header explicitly says arch11.

The texture layout record must preserve Metal texture type, exact pixel format, width/height/depth, array length, sample count, levels, swizzle, access/usage, base view level/slice. Per-subresource records need backing-relative offset, extent, row stride, slice stride and byte span. Array/cube face order and mip reduction are specified once. Publish exact bounded counts before consumers allocate structs.

Keep two levels of input separate:
1. Frontend request = retained allocation token(s) + offsets/spans + immutable texture facts; no arbitrary GPU address.
2. Pure packer input = same validated facts plus GPU addresses resolved by n2mb, descriptor address and bounded output span. The packer owns no resource lifetime and issues no submit/cache operations.

Proposed callable operations (exact structs/signatures to be ACKed by texture owner):
- `n2t_texture_validate(layout, backing_length, access)` checks all levels/layers/samples and overflows before output mutation.
- `n2t_texture_pack(layout, resolved_backing_address, descriptor_address, output, output_capacity, result)` produces arch9 descriptors and reports bytes/table shape; output unchanged on refusal.
- `n2t_sampler_pack(sampler_state, output[32])` packs exact state.

Sampler_state must include min/mag/mip filters, S/T/R address, normalized flag, compare function, border color, anisotropy, finite LOD min/max, reduction mode, and relevant options. Runtime and constexpr samplers use it. Do not fabricate nearest/LOD0 facts or drop mip/border fields. A mode not implemented must produce a specific error, never substitution.

## Compute planning / descriptor correspondence (compute owner)

Add v2 inputs and outputs, preserve buffer-only C APIs. Inputs carry texture/sampler state and Stage::Kernel, as well as buffers, dispatch dimensions and threadgroup requirements. Output maps set/binding/array element to source Metal stage/kind/index or immutable/constant/neutral resource, carries descriptor/use class and selected format/specialization variant.

Descriptors must match the compile plan's table numbers and binding indices; Metal slot N is NOT automatically native binding N. Confirm with native compiler owner whether v2 shader handles use table1 resources/table2 samplers, and how storage-image loads/stores are lowered. A compiled sampled texture and writable image are different resource contracts.

The v2 dispatch submit must retain every referenced BO and mark output/image writes for coherence. All mip/layer spans are validated against those BOs. Keep 32-handle kernel submit bound explicit; return bounded errors rather than dropping resources. Actual n2mb submit owns final resolve/locking/fence/quarantine semantics.

Storage-texture creation placeholders remain creation-only until a real bound-format variant is compiled. Cache identity must include translator variant + specialization + format facts and relevant sampler/view changes. Exact dispatchThreads needs translator-driven edge behavior, not unconditional round-up.

## Render consumption / unavoidable shared hooks (render owner)

Publish a proposed `n2mb_draw_v2`/sidecar request only if necessary; compute owner makes the bridge change and texture owner makes n2r_build_v2. Old n2mb_draw/n2r_build and zero/default behavior remain intact.

Populate actual VS and FS texture/sampler inputs and retain both stages. Replace the current ASTC omission at render_encoder.inc:1435 only together with Rust typed input support (air_bridge.rs:1316), matching translator format handling and real descriptor identity. Require that an intentionally bound resource does NOT silently become a neutral descriptor.

For vertexStart/instance/index identity: offsetting vertex buffer bytes alone is not enough. Preserve original IDs in hardware job/push state or a precisely defined compiler specialization; test shader using both attributes and vertex_id at nonzero vertexStart. Generic runtime specialization needs an additive compiler API, owned by native compiler agent, not merely a new cache key.

## Minimal ACK needed now

1. Texture owner: exact common header, structs/max counts/API names; which dimensions/operations are truly implemented in this iteration.
2. Compute owner: v2 package/dispatch header names; table numbering contract with compiler; how render requests for AIR generic input support reach you.
3. Render owner: whether a new sidecar/base-ID request is needed; exact cross-owner changes requested.

Regression contract: preserve existing TGP1 and existing graphics default descriptor bytes; test old and new paths separately. Compiler, planner, resource packing and actual GPU results are separate statuses. An implemented refusal or compile-only success does not count as full support.
