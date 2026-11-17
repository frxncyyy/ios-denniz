# Current integration gates — frontend audit

Read-only review of live drafts, 25 October 2026. This is a checkpoint, not a
claim that later owner changes remain defective. Update evidence/status against
timestamped board replies and final source. No build/device work by this reviewer.
Root's signature/ownership agreement does not clear these correctness gates.

| Gate | Owner | Current finding / required proof |
| --- | --- | --- |
| C/Rust ABI | compute | ComputeRegion C lacks Rust flags; sizes1168/1172 and dispatch10696/10728 differ. Rust assertion now10728 alone does not fix C. Match fields/offsets and independent ABI assertions. |
| TGP2 manifest | compute + lowering | Current8-byte binding/kind records omit agreed shape/view/access/table fields and nativeN bound differs. Freeze corrected serialized header+manifest, then implement explicit compile_compute_v2. Old calls remainTGP1. |
| Compute native identity | compute | Current bridge indexes descriptors by Metal slot and uses image table3. Must table1[N] texture/image, table2[N] sampler, combined both at sameN, internal2[256]. Test Metal30->native0 and sparse native3/9. |
| One texture packer | compute + textures | Current n2mb_compute_pack_* is a second handwritten packer. Replace with common n2t helpers, preserving exact view/sampler facts. Do not keep independent format/wrap maps. |
| Complete compute job | compute | Current v2 zeroes command memory but never initializes SHADER_PROGRAM or LOCAL_STORAGE; only v1 calls those emitters. Package controls codeVA/registers/preload/FAUcount/local size. Decode full job in host oracle. |
| Exact resource uses | compute | Current Rust ignores ImageViewDesc, forces READ on write-only storage, requires SHADER_WRITE for any STORAGE, applies FAU8 cap to Metal slot and emits samplerREAD while parser forbidsit. Derive actual access/namespace consistently; test read-only and write-only images. |
| Full texture span | compute + textures | Current compute row*height*depth*array omits mip/sample/block/cube details. Use common layout spans and retained full allocation. No level0 surrogate for a richer image. |
| Exact thread regions | compute | Runtime compares every specialized package local size with original requested local; edge regions may shrink it. Validate each region's actual specialization/local size. Test17x9 with8x4 groups and distinct edge packages. |
| Variant lifetime | compute + render | Dispatch must retain selected shader/variant/code until completion. Current graphics submit reads shared pipeline->genericShader at execution; do not mutate it per draw. Test nearest/linear/nearest in one command plus eviction and early app release. |
| Texture helper geometry | textures | Current readiness snapshot still has ETC2RGB8/A1 bytes16 instead8; swizzle0/default collision/highbits; zero backing-length bypass; ordinaryCube nonsquare/>6faces; ambiguous view origin. Tests named in board1792940691731620824. |
| n2r v2 preflight | textures | Legacy format whitelist still rejects richer sidecar formats. Sidecar-aware per-binding preflight must check same complete resolved span, version/kind/identity and format, while legacy bindings retain exact checks. Address array is requestPOSITION, not nativeN. |
| Immutable sampler enums | render + compute | GenericBinding holds Vulkan enums; new n2t sidecar copies them as Metal enums. Map explicitly and expose omitted immutable LOD/normalized/per-use facts. Test repeat+linearMip+opaqueWhite against Mesa. |
| Real sampler factory | textures + render + compute | Invented newSamplerStateWithExactDescriptor selector is not enough. Ordinary Metal factory must reach exact state; all legacy consumers prove representability or route v2/refuse. |
| Exact neutral provenance | compute + render | Current unsupported-any+neutral-any check over-refuses unused unsupported slots. Existing static BindingSource identifies stage/index; expose it and test unusedVS5 plus legal neutralFS0. |
| Vk83 vertex fetch | lowering + compute + textures | Source-derived hardware format0xcf010,4bytes; gates Rust list, vertex_fetch.h bytes, vertex_fetch.c pack, n2r pack. Lowering owns c/h now. Test exact-end vs one-byte-short and12 saved synthetic-descriptor cases. |
| Graphics specialization | lowering + compute + render | Source review found no new blocker in per-stage scalar validation/spirv_to_nir placement. Await tests: empty/default equivalence, independent sameIDs, resource removal, real exactsampler plan. It is not wired solely by exporting compiler API. |
| Vertex identity | textures + compute + render | Signed hardware base_vertex_offset exists. Additive snapshot/bridge/builder must keep original attribute base and carry actualstart/bias exactly once; direct indexed pulling already preserves IDs. Test uint16/32 arbitraryindices, start3 and overflow. |

Resolved at source review: render removed rc-13 successful draw elision and
restored command error; worker reports actual frontend test PASS. Clump formats,
diagnostic NUL and orphan-sidecar detection have been fixed since first review.
Do not count refusals, uncalled helpers or successful mock submits as full
shader execution support. Keep baseline pixel behavior unchanged, and replay
the full corpus only after root/lowering grants the batch/build lease.

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
