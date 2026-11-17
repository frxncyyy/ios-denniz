# Integration test brief — 25 October 2026

This read-only checkpoint supersedes initial defects already corrected in the earlier ledger. No native or host build was run by this reviewer. Source is live; re-hash before interpreting results.

## Stable agreements

- Active transport is nord2_metal_bridge.c, method5 import and method7 dispatch.
- Keep legacy TGP1/dispatch bytes and existing graphics exports unchanged.
- New compiler v2 success must be TGP2: 128-byte header, 64-byte manifest records, exact checked lengths. Manifest enum domains/view facts still await final compiler-owner ACK.
- Native set0 binding N0..255 is separate from Metal index. Table1[N] texture/image, table2[N] sampler, internal sampler2[256]. First-version buffers nativeN0..7 map FAU16*N. Arrays require explicit shape/arrayspan support, not aliasing.
- Shared arch9 n2t is the only packer. swizzle_valid is explicit; RRRR0 is valid data. View base and level offsets are already resolved relative to selected view; first_layer/first_level are metadata, not a second address increment.
- All real native resources must match selected translator variant and compiler manifest; numeric/format domains must be defined before mapping. Table counts alone are insufficient.

## Highest-value executable checks

1. Real C/Rust sizeof/offsetof and field round trips, including ComputeRegion.flags, all offsets and tails. Current C and Rust draft previously differed by4bytes per region.
2. Actual selector7 producer with fakeJM imports/submits: decode complete COMPUTE_JOB, SHADER_PROGRAM, LOCAL_STORAGE, FAU and SRT through independent pinned Mesa arch9 oracle. Positive local/groups/register/codeVA/preload and negative package/wire mismatch. At this snapshot v2 shader/LS memory is still zeroed.
3. NativeN distinct from Metal slot: buffer Metal30->native0; standalone sampler Metal0->native31 and255; combined binding9; texture-only dummy sampler256; reject namespace collisions except genuine combined image/sampler representation.
4. Whole layout addressing: nonzero BO range offset; one-byte-short span; 2-layer texture followed by another binding; cube6faces; 1DArray vs 2D and ordinaryCube vs CubeArray shape. Current bridge drops D2_OFFSET and emits just one plane although helper descriptor may require several.
5. NULL texture layout and render q.binding_count above fixed capacity must refuse without out-of-bounds accesses or output mutation. Current helper initializers/preflight have source-visible hazards; owners notified.
6. Write-only/read-only/atomic storage usage and submit write masks, plus exact alias image view numeric/format facts; no forcedREAD or fabricated format. Exact enum map is an owner prerequisite.
7. Graphics queued nearest/linear/nearest variants, early object release and cache eviction preserve per-job shader, sidecar, and resources. Immutable Vulkan enums cannot be copied as Metal enums. Correct unused-slot neutral provenance.

Successful fakeJM completion only proves transport/control flow. The existing50pixel baseline is earlier hardware evidence and not validation of these new paths. Full corpus replay requires root/lowering lease; do not classify refusal as implementation.

## Snapshot hashes

- gpu/metal/nord2_metal_bridge.c: f5c71dabda3ed1023619dd5acea27ef5a1abb4f295d28a2b27a5c7d9bcd8a1c0
- gpu/metal/nord2_texture_resources.h: edbd468874ea1f3514f8fb1ae99d0386a2f80b209bdc152508418003cd9c23bb
- gpu/metal/nord2_metal_render.c: 1a45a4b513ebbe8cb4a595b6dd7511e65c13278b90617edc126e4cb1be1fb532
- gpu/metal/tegu/pipeline_format.h: e7c33cddf4f86d561f65df9ab85615915040a5d3dee7caf0102f7f0d7a761712
- gpu/metal/tegu/air_bridge.h: 66ff9c081c858019003ed31777ad2547f133451c833d7390530eef38da477883
- gpu/metal/tegu/air_bridge.rs: b1007167dadc5d2db4cbeb01bb196842ace0c79a8cad4c53052f306564710bf2


## Immediate follow-up

See board1792942377944838352: Rust view_format is Vulkan but n2t expects Metal;
add a real producer/consumer alias-format round trip. Offset clarification:
runtime currently writes the same range_offset into BOTH D2_OFFSET and
LEVEL_OFFSET, so its level0 caller gets the offset through the latter today.
Fix producer and consumer together; do not double-add. Variant cache ownership
and TGP1/v2 cache-hit mode checks are also required.

Textures subsequently fixed both NULL-layout reads and added public negative
tests (owner771check report). Oversized q.binding_count preflight still awaits
owner confirmation. Re-hash before reproducing because sources remain live.


## Source progress at 15:43 UTC (test approval still pending)

- Compute C region now includes flags, descriptor is64bytes on both source declarations, and Rust asserts NativeDispatchV2=11496. Reproduce against matching source hashes and fresh library.
- import_pipeline now copies v2 ISA from parsed info2.code_offset.
- Cache hits retain under lock and every selected package is version/manifest checked; caller releases owned selection after job retain. Cross-mode cache behavior still needs testing.
- Runtime wire now separates Vk view identity and Metal view format, with Rust translator-table conversion. Base and relative-level offsets are separate, and all level0 layers up to16 now emit planes in reserved per-binding slots. Full mips remain refused.
- Remaining declared-span issue: bridge validates wire span against BO but passes whole remaining BO to n2t. Test a wire span one byte shorter than a valid layout within a larger BO; it must not silently authorize the larger layout.
- Textures fixed public NULL-layout cases; oversized render request binding_count still needs correction. Exact factory enable must occur before first ordinary sampler creation, and fixed-UI/compositor consumers still need legacy-state guards before global enable.
- Complete shader-program/local-storage emission, shader numeric/format manifest contract, per-region local dimensions and matching Rust binary remain open.


### Arch9 resource-table pointer gate

The active compute bridge passes a plain64-aligned SRT address. The shared
compute-chain validator (gpu/src/nord2_gpu_jm.c:20) requires that alignment and
passes the pointer unchanged to shader-environment packing. Arch9 Resources
also encodes the table count in its low6bits: pinned Mesa pan_cmdstream.h
panfrost_emit_resources returns address|aligned_table_count; v9 requires count
alignment4. Existing Nord2 graphics emits SRT|4. Compute table1/table2 need four
allocated/zeroed SRT records, hence encoded count4. Plain SRT has count0.

This needs an additive count-aware chain contract or validated wrapper; merely
ORing4 into the current chain fails its address check. Root owns scope assignment
for gpu/include/nord2_gpu_jm.h and gpu/src/nord2_gpu_jm.c/mirrors. The integration
worker must decode both the base address and count from the actual complete job.
Legacy no-resource compute remains unchanged. Board1792943374099175193 has exact
source evidence and required positive/negative cases. No production fix by audit.
