Sender: corpus (Codex metal_shader_corpus)
No new compiler batch/build. Existing evidence independently classified; see ui-corpus-20261025/translation-failure-classification.json (every3023compute +939graphics first failure, exact source/evidence and missing constant name/type/index where container provided it).

Compute3023translation stops:
-2926missing FunctionConstant,20VisibleFunctionTable,4TileAttachments(QuartzCore tile_downsample_*),3LinkedFunction(CC_InlineCompositing32x32/32x16/16x16). These2953need exact request/link inputs; zero filling is not evidence.
-56AIR signature mismatch, all MTLReplayer asv[Atomic]MetalIntersector_*; actual legacy air.intersect.* declarations have older operand forms. For example air.intersect.triangle_data has15args in installed AIR, versus18in current intrinsics/intersect_tests.rs. It is a decoder compatibility family, not byte corruption (LLVM decode passes).
-14explicit AIR semantics NotImplemented:8Imageblocks,4ArgumentBufferObjects(indirect command buffer kernels),2Lowering(Access) (seamCutDescend,srlV3Apply).

Graphics939translation stops:
-325missing FunctionConstant.
-406AvatarKit post-tessellation quad patch pipelines: everyVS has air.patch metadata. air_bridge.rs:400 refuses expanded pipeline stages.len()!=2 AFTER translator compile. This is unsupported bridge/backend stage ABI, not AIR decode failure.
-197ColorAttachment descriptor failures: the audit's synthetic BGRA8slot0 is insufficient;140declare additional slots and150declare integer target types(overlap). Need matching target formats/MRT states before claiming shader semantics unsupported.
-11VertexAttribute failures: everyVS declares air.patch; synthetic ordinary vertex layout lacks patch control-point descriptor.
Thus the208synthetic-descriptor failures are now explicitly marked audit_descriptor_insufficient in coverage.jsonl/audit-summary.json; not unsupported AIR. Four compute CPU-limit kills remain unknown.

Resource declaration bounds (resource-bounds.json includes exact metadata and source examples):
Stage       texture highest slot/distinct count    sampler highest/distinct   buffer highest/distinct
kernel      31 /22                                11 /12                     31 /31
vertex       7 / 2                                 7 / 2                     25 /18
fragment    31 /22                                14 /15                     20 /21
These are maxima across unspecialized declared AIR modules, NOT actual bound/active counts; resource rows include conditional/nested metadata. Declared location-span max:kernel texture16,fragment texture4,fragment sampler8;other listed spans1. Static array type max seen array<texture2d<float,sample>,16>,sampler array8; array_ref forms carry no fixed length and must not be assigned one. Some buffer slot31 declarations exceed current public range0..30; preserve/refuse until actual request contract is established.
No buffer-texture declaration type or texture_buffer intrinsic observed among15568ordinary MTLB modules; this does not cover709AGX runtime helpers, nativeGPU archives or dynamic generated code. Type1D textures are present and not silently equated to buffer textures.
