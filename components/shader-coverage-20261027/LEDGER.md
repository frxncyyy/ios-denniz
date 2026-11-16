# Installed iOS 16.5 shader coverage ledger (host replay, shader-coverage-opus)

Host replay only: AIR -> SPIR-V (libtegu_air_bridge) -> NIR -> Mali arch9 (libnord2_mali_runtime). No GPU execution, no device. "ok" = translated AND Mali code generated through the production entry (compute: tegu_mali_compile then compute_v2; graphics: tegu_mali_compile_generic_pair_options with DRAW_PARAMETERS|EXTENDED_VARYINGS|THREAD_STORAGE, true-MRT/SELECT_RT as render_pipeline.inc).

| run | bridge | Mali runtime | profile |
|---|---|---|---|
| before | d4917c28bf7e | 2430cc8070e1 | db3e8f0fd919 |
| after | a43bd2acdc27 | 125b33824229 | db3e8f0fd919 |

## Functions ok per process class (unique AIR functions; vertex/fragment ok if any replayed pair with it compiled)

| class | stage | functions | ok before | ok after | refused after | timeout after | crash after |
|---|---|---|---|---|---|---|---|
| coreanimation | kernel | 7 | 3 | 7 | 0 | 0 | 0 |
| coreanimation | vertex | 39 | 39 | 39 | 0 | 0 | 0 |
| coreanimation | fragment | 95 | 73 | 93 | 2 | 0 | 0 |
| ui_frameworks | kernel | 366 | 364 | 364 | 2 | 0 | 0 |
| ui_frameworks | vertex | 113 | 92 | 111 | 2 | 0 | 0 |
| ui_frameworks | fragment | 417 | 144 | 169 | 248 | 0 | 0 |
| camera_video | kernel | 1126 | 1103 | 1103 | 22 | 1 | 0 |
| camera_video | vertex | 19 | 15 | 19 | 0 | 0 | 0 |
| camera_video | fragment | 118 | 39 | 115 | 3 | 0 | 0 |
| mps_ml | kernel | 5187 | 4918 | 5014 | 76 | 97 | 0 |
| mps_ml | vertex | 2 | 2 | 2 | 0 | 0 | 0 |
| mps_ml | fragment | 8 | 8 | 8 | 0 | 0 | 0 |
| 3d_maps_ar | kernel | 511 | 443 | 453 | 58 | 0 | 0 |
| 3d_maps_ar | vertex | 877 | 362 | 588 | 289 | 0 | 0 |
| 3d_maps_ar | fragment | 1086 | 501 | 724 | 362 | 0 | 0 |
| other | kernel | 307 | 163 | 163 | 132 | 12 | 0 |
| other | vertex | 10 | 9 | 9 | 1 | 0 | 0 |
| other | fragment | 20 | 15 | 18 | 2 | 0 | 0 |
| **all** | | 10308 | 8293 | 8999 | | | |

## Captured device pipelines (gpu/build/pipeline-corpus/corpus.txt, CoreAnimation/backboardd TPC_MISS records)

142 records: before {'ok': 142}, after {'ok': 142} (exact captured constants, targets, blends, vertex layouts).

## Provenance of the "ok after" functions

- installed_air: 4425
- installed_air+synthetic_constants: 2585
- inferred+synthetic_descriptor: 1115
- inferred+synthetic_descriptor+tessellation_eval_as_vs: 422
- inferred+synthetic_descriptor+synthetic_constants: 343
- installed_air+synthetic_linked_functions+synthetic_constants: 80
- installed_air+synthetic_stage_input+synthetic_constants: 10
- captured_donor: 8
- inferred+synthetic_descriptor+captured_donor_constants: 5
- installed_air+synthetic_tile_descriptor: 4
- inferred+synthetic_descriptor+captured_donor_constants+synthetic_constants: 2

synthetic_constants = corpus_inputs audit plan values (origin synthetic_diagnostic:canonical, bool/int 1, float 0.5) or the same rule for constants the plan did not list (origin ...(completion)); they prove one reachable specialization translates/compiles, never application behaviour. inferred+synthetic_descriptor = same-library ABI pairing and synthetic vertex/attachment descriptors (attachment formats chosen by declared target type).

## Compile-budget reruns

110 functions exceeded the 30 s CPU / 45 s wall replay budget (all in the Mali phase: Bifrost register allocation). Rerun with 240 s CPU / 300 s wall: {'ok': 98, 'timeout': 12}. They count as "timeout" in the tables, not as ok and not as refusals.

## Remaining failures: unsupported semantics vs wrong diagnostic inputs (after)

| category | functions | coreanimation | ui_frameworks | other classes |
|---|---|---|---|---|
| renderer/ABI feature not implemented (formats, depth, viewport arrays, layers, FS storage images, argument buffers, bindings>15) | 996 | 2 | 248 | 746 |
| compile_budget_exceeded (>30 s CPU / 45 s wall; not a refusal) | 110 | 0 | 0 | 110 |
| installed AIR uses a legacy intrinsic signature (decoder compatibility) | 56 | 0 | 0 | 56 |
| empty program after specialization (backend emits no code) | 43 | 0 | 0 | 43 |
| runtime_input_not_expressible_in_bridge_API (MTLLinkedFunctions / function tables / tile render pass) | 39 | 0 | 0 | 39 |
| diagnostic_input_mismatch (synthetic descriptor/constants/pairing, not shader semantics) | 29 | 0 | 4 | 25 |
| translator semantics not implemented (AIR->SPIR-V) | 26 | 0 | 0 | 26 |
| other: translate: limit exceeded: LedgerBytes | 4 | 0 | 0 | 4 |
| other: translate: unsupported: UnknownIntrinsic (air.get_imageblock | 3 | 0 | 0 | 3 |
| other: translate: unsupported: UnknownIntrinsic (air.store.implicit | 1 | 0 | 0 | 1 |
| other: translate: unsupported: UnknownIntrinsic (llvm.trap); needs  | 1 | 0 | 0 | 1 |
| other: mali: private memory exceeds the Valhall TLS immediate range | 1 | 0 | 0 | 1 |

## Remaining failures by root cause (after), ranked CoreAnimation, UI, then count

| functions | by class | cause | example |
|---|---|---|---|
| 6 | {'coreanimation': 2, '3d_maps_ar': 4} | mali: Fragment shader interlock (Metal raster order groups) is unsupported: Valhall has no pinned Mesa lowering | inplace_copy_lpf |
| 238 | {'ui_frameworks': 238} | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | clearFragmentE6 |
| 438 | {'3d_maps_ar': 433, 'ui_frameworks': 3, 'camera_video': 2} | mali: Generic binding index above N | commonprofile_frag084FE8984D4623D65688537E85C86821 |
| 17 | {'3d_maps_ar': 14, 'ui_frameworks': 3} | mali: Unsupported graphics output: FS FragDepth | downsample_depth |
| 15 | {'3d_maps_ar': 12, 'ui_frameworks': 2, 'other': 1} | translate: invalid descriptor: ColorAttachment | particle_quad_deferred_frag |
| 5 | {'ui_frameworks': 2, '3d_maps_ar': 3} | mali: TGP2 descriptor arrays or indirect resource pointers are unsupported | SiriBlur |
| 2 | {'ui_frameworks': 2} | mali: Generic storage images are unsupported | uber_resolve_fragment |
| 3 | {'ui_frameworks': 1, 'other': 2} | mali: Selected colour output is not written | sixChannelBlend_fragment |
| 1 | {'ui_frameworks': 1} | translate: unsupported: UnmatchedVarying; needs NotRepresentable | paper_fragment |
| 146 | {'3d_maps_ar': 146} | mali: Unsupported graphics output: VS ViewportIndex (viewport arrays) | fsVignette |
| 115 | {'3d_maps_ar': 44, 'other': 51, 'mps_ml': 16, 'camera_video': 4} | mali: TGP2 requires logical descriptor addressing; physical pointers have no manifest binding | particle_set_mass |
| 110 | {'mps_ml': 97, 'other': 12, 'camera_video': 1} | timeout | MPSRNNLSTMRecursionfloat00_81_1 |
| 56 | {'other': 56} | translate: library_open: invalid input: Intrinsic(SignatureMismatch) at bitstream bit N in blocks [8, N], record N | asvMetalIntersector_BoundingBox_primitiveMotion |
| 43 | {'mps_ml': 38, 'camera_video': 4, '3d_maps_ar': 1} | mali: Mali backend emitted no code | tileArrayData |
| 20 | {'other': 20} | translate: needs pipeline input: VisibleFunctionTable { buffer: Some(3) } | heatMap_BoundingBox_instancing_triangleData_primit |
| 16 | {'3d_maps_ar': 16} | mali: Unsupported graphics output: VS Layer (layered rendering) | scn_convert_equi_from_cube_frag |
| 10 | {'3d_maps_ar': 9, 'camera_video': 1} | translate: needs pipeline input: FunctionConstant | fsRealityPbr |
| 7 | {'3d_maps_ar': 7} | mali: Generic storage buffers must be read-only | white_frag |
| 4 | {'3d_maps_ar': 4} | mali: Unsupported graphics output: FS SampleMask | particle_mesh_frag |
| 4 | {'mps_ml': 4} | translate: invalid descriptor: LinkedFunction { name: "prefixPrimary_i", problem: Signature } | ndArrayIdentityIntTranspose |
| 4 | {'mps_ml': 4} | translate: limit exceeded: LedgerBytes | scan_min |
| 3 | {'3d_maps_ar': 3} | translate: needs pipeline input: LinkedFunction { symbol: "geometryModifierWithCustomParams" } | realitykit::fsSurfacePbr |
| 2 | {'3d_maps_ar': 2} | translate: invalid input: Canon(CallTypeMismatch) at function fsUnlit, block N, instruction N | fsUnlit |
| 2 | {'3d_maps_ar': 2} | translate: invalid input: Canon(CallTypeMismatch) at function fsVideoColorPlayback, block N, instruction N | fsVideoColorPlayback |
| 2 | {'mps_ml': 2} | translate: needs pipeline input: LinkedFunction { symbol: "postfixPrimary_i" } | reduce_argmin |
| 2 | {'mps_ml': 2} | translate: invalid descriptor: LinkedFunction { name: "prefixPrimary_u64", problem: Signature } | ndArrayIdentityULongTranspose |
| 2 | {'mps_ml': 2} | translate: unsupported: StaticInitializer; needs NotImplemented at function _GLOBAL__sub_I_MPSNDArrayConvolutionAMD.metal, block N, instruction N | AMDConvolution::ndArrayConvolution2DAMD_NCHW |
| 2 | {'mps_ml': 2} | translate: needs pipeline input: LinkedFunction { symbol: "prefixPrimary_cf" } | ndArrayIdentityComplexF32Transpose |
| 2 | {'mps_ml': 2} | translate: invalid descriptor: LinkedFunction { name: "prefixPrimary_i64", problem: Signature } | ndArrayIdentityLongTranspose |
| 1 | {'3d_maps_ar': 1} | mali: Fragment builtin inputs are unsupported | fsMeshDepth |
| 1 | {'camera_video': 1} | mali: Graphics code or resource footprint unsupported: code exceeds stage limit | fragmentBlur |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function ggFusion, block N, instruction N | ggFusion |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function mstmsApply, block 7, instruction N | mstmsApply |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function red_face_fix, block 5, instruction N | red_face_fix |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function mstmV2First, block N, instruction N | mstmV2First |
| 1 | {'camera_video': 1} | translate: unsupported: Lowering(Access); needs NotImplemented at function RawDFSyntheticReference::kernelRawDFDeepShadowRecovery, block 0, instruction N | RawDFSyntheticReference::kernelRawDFDeepShadowReco |
| 1 | {'mps_ml': 1} | translate: unsupported: StaticInitializer; needs NotImplemented at function _GLOBAL__sub_I_MPSNDArrayWinogradAMD.metal, block 8, instruction N | ndArrayWinogradAMD_NCHW |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function maskedCubeApply, block 5, instruction N | maskedCubeApply |
| 1 | {'mps_ml': 1} | translate: invalid descriptor: LinkedFunction { name: "prefixTertiary_i", problem: Signature } | cropResize_impl |
| 1 | {'3d_maps_ar': 1} | translate: needs pipeline input: LinkedFunction { symbol: "CC_InlineCompositing16x16" } | ccInline16x16 |
| 1 | {'other': 1} | translate: unsupported: ArgumentBufferObjects; needs NotImplemented at function ICB_Inherit_Both_Kernel, block 3, instruction N | ICB_Inherit_Both_Kernel |
| 1 | {'other': 1} | translate: unsupported: Lowering(Access); needs NotImplemented at function seamCutDescend, block 8, instruction N | seamCutDescend |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function warpFrameWithBlendingWeight, block N, instruction N | warpFrameWithBlendingWeight |
| 1 | {'3d_maps_ar': 1} | translate: unsupported: UnknownIntrinsic (air.store.implicit_imageblock.mask.v4f16); needs NotImplemented at function TAATiledKernel, block 3, instruction N | TAATiledKernel |
| 1 | {'3d_maps_ar': 1} | translate: needs pipeline input: LinkedFunction { symbol: "CC_InlineCompositing32x16" } | ccInline32x16 |
| 1 | {'3d_maps_ar': 1} | translate: unsupported: UnknownIntrinsic (air.get_imageblock_num_colors); needs NotImplemented at function ColorPipeTiledCombinedKernel, block 2, instruction 2 | ColorPipeTiledCombinedKernel |
| 1 | {'other': 1} | translate: unsupported: ArgumentBufferObjects; needs NotImplemented at function ICB_Inherit_Buffers_Kernel, block 1, instruction N | ICB_Inherit_Buffers_Kernel |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function mstmV2Second, block N, instruction N | mstmV2Second |
| 1 | {'camera_video': 1} | translate: unsupported: Lowering(Access); needs NotImplemented at function srlV3Apply, block 1, instruction N | srlV3Apply |
| 1 | {'mps_ml': 1} | translate: unsupported: StaticInitializer; needs NotImplemented at function _GLOBAL__sub_I_MPSNDArrayConvolutionA14.metal, block 5, instruction N | ndArrayConvolution2DGradientWithWeightsA14 |
| 1 | {'3d_maps_ar': 1} | translate: unsupported: UnknownIntrinsic (air.get_imageblock_num_colors); needs NotImplemented at function compositeTransferKernel, block 2, instruction 2 | compositeTransferKernel |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function LTM, block N, instruction N | LTM |
| 1 | {'3d_maps_ar': 1} | translate: unsupported: UnknownIntrinsic (air.get_imageblock_num_colors); needs NotImplemented at function TAATiledKernelTemporalBlur, block 5, instruction N | TAATiledKernelTemporalBlur |
| 1 | {'mps_ml': 1} | translate: unsupported: UnknownIntrinsic (llvm.trap); needs NotImplemented at function ndArrayConvolution2DA14, block N, instruction N | ndArrayConvolution2DA14 |
| 1 | {'3d_maps_ar': 1} | translate: needs pipeline input: LinkedFunction { symbol: "CC_InlineCompositing32x32" } | ccInline32x32 |
| 1 | {'other': 1} | translate: unsupported: ArgumentBufferObjects; needs NotImplemented at function ICB_Inherit_PSO_Kernel, block 3, instruction N | ICB_Inherit_PSO_Kernel |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function mixingFusedReference, block N, instruction N | mixingFusedReference |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function warpFrameWithBlendingWeightAndConfidence, block N, instruction N | warpFrameWithBlendingWeightAndConfidence |
| 1 | {'other': 1} | translate: unsupported: ArgumentBufferObjects; needs NotImplemented at function ICB_Inherit_None_Kernel, block 1, instruction N | ICB_Inherit_None_Kernel |
| 1 | {'3d_maps_ar': 1} | mali: private memory exceeds the Valhall TLS immediate range (scratch plus spills) | rayTracedAmbientOcclusionRaysKernel |
| 1 | {'camera_video': 1} | translate: unsupported: Imageblocks; needs NotImplemented at function inpainting, block N, instruction N | inpainting |

## Every remaining CoreAnimation / UI-framework function (after)

| class | stage | function | library | cause | provenance |
|---|---|---|---|---|---|
| coreanimation | fragment | inplace_copy_lpf | QuartzCore.framework | mali: Fragment shader interlock (Metal raster order groups) is unsupported: Valhall has no pinned Mesa lowerin | inferred+synthetic_descriptor |
| coreanimation | fragment | inplace_copy_lph | QuartzCore.framework | mali: Fragment shader interlock (Metal raster order groups) is unsupported: Valhall has no pinned Mesa lowerin | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment06 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment07 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment09 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment0A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment0B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment0D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment0E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment0F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment12 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment13 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment16 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment17 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment18 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment19 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment1A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment1B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment1C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment1D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment1E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment1F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment21 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment22 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment23 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment24 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment25 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment26 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment27 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment28 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment29 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment2A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment2B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment2C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment2D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment2E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment2F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment31 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment32 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment33 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment34 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment35 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment36 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment37 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment38 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment39 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment3A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment3B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment3C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment3D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment3E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment3F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment42 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment43 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment46 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment47 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment48 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment49 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment4A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment4B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment4C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment4D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment4E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment4F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment52 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment53 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment56 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment57 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment58 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment59 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment5A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment5B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment5C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment5D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment5E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment5F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment60 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment61 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment62 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment63 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment64 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment65 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment66 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment67 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment68 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment69 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment6A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment6B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment6C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment6D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment6E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment6F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment70 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment71 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment72 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment73 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment74 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment75 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment76 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment77 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment78 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment79 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment7A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment7B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment7C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment7D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment7E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment7F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment81 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment82 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment83 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment84 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment85 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment86 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment87 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment88 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment89 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment8A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment8B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment8C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment8D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment8E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment8F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment90 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment91 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment92 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment93 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment94 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment95 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment96 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment97 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment98 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment99 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment9A | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment9B | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment9C | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment9D | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment9E | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragment9F | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA0 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA1 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA2 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA3 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA4 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA5 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA6 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA7 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA8 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentA9 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentAA | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentAB | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentAC | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentAD | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentAE | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentAF | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB0 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB1 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB2 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB3 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB4 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB5 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB6 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB7 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB8 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentB9 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentBA | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentBB | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentBC | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentBD | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentBE | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentBF | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC1 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC2 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC3 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC4 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC5 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC6 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC7 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC8 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentC9 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentCA | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentCB | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentCC | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentCD | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentCE | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentCF | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD0 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD1 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD2 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD3 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD4 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD5 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD6 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD7 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD8 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentD9 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentDA | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentDB | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentDC | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentDD | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentDE | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentDF | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE0 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE1 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE2 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE3 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE4 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE5 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE6 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE7 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE8 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentE9 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentEA | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentEB | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentEC | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentED | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentEE | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentEF | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF0 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF1 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF2 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF3 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF4 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF5 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF6 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF7 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF8 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentF9 | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentFA | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentFB | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentFC | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentFD | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentFE | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | clearFragmentFF | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment2DDepth | AppleMetalGLRenderer.bundle | mali: Unsupported graphics output: FS FragDepth | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment2DDepthMultisample | AppleMetalGLRenderer.bundle | mali: Unsupported graphics output: FS FragDepth | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment2DInt | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment2DIntMultisample | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment2DUInt | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment2DUIntMultisample | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment3DDepth | AppleMetalGLRenderer.bundle | mali: Unsupported graphics output: FS FragDepth | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment3DInt | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | texturedFragment3DUInt | AppleMetalGLRenderer.bundle | mali: options refused: true-MRT target format outside mrt_format_ok (integer/raw targets not in n2r yet) | inferred+synthetic_descriptor |
| ui_frameworks | fragment | copy_framebuffer_fragment | PencilKit.framework | translate: invalid descriptor: ColorAttachment | inferred+synthetic_descriptor+synthetic_constants |
| ui_frameworks | fragment | paper_fragment | PencilKit.framework | translate: unsupported: UnmatchedVarying; needs NotRepresentable | inferred+synthetic_descriptor |
| ui_frameworks | fragment | sixChannelBlend_fragment | PencilKit.framework | mali: Selected colour output is not written | inferred+synthetic_descriptor+synthetic_constants |
| ui_frameworks | vertex | PointLightWithVertexTransform_VertexFunc | SpriteKit.framework | mali: Generic binding index above N | inferred+synthetic_descriptor |
| ui_frameworks | fragment | PointLight_FragFunc | SpriteKit.framework | mali: Generic binding index above N | inferred+synthetic_descriptor |
| ui_frameworks | vertex | PointLight_VertexFunc | SpriteKit.framework | mali: Generic binding index above N | inferred+synthetic_descriptor |
| ui_frameworks | kernel | Blur::downsample | NTKColtanFaceBundleCompanion.bundle | mali: TGP2 descriptor arrays or indirect resource pointers are unsupported | installed_air |
| ui_frameworks | fragment | resolve_color_fragment | RenderBox.framework | mali: Generic storage images are unsupported | inferred+synthetic_descriptor+synthetic_constants |
| ui_frameworks | fragment | resolve_sample_fragment | RenderBox.framework | translate: invalid descriptor: ColorAttachment | inferred+synthetic_descriptor+synthetic_constants |
| ui_frameworks | fragment | uber_resolve_fragment | RenderBox.framework | mali: Generic storage images are unsupported | inferred+synthetic_descriptor+synthetic_constants |
| ui_frameworks | kernel | SiriBlur | SiriUICore.framework | mali: TGP2 descriptor arrays or indirect resource pointers are unsupported | installed_air |
| ui_frameworks | fragment | (0 AppleMetalGLRenderer integer-target clear/blit functions) | AppleMetalGLRenderer.bundle | translate: attachment format or write mask outside bounded class | inferred+synthetic_descriptor |

## Not replayed (outside the pipeline ABI) - kept, not counted as covered

- visible: 5283 entries / 5236 unique AIR (visible/intersection functions are linked into a pipeline only through function tables; no standalone pipeline ABI)
- other-5: 1064 entries / 10 unique AIR (visible/intersection functions are linked into a pipeline only through function tables; no standalone pipeline ABI)
- exported: 83 entries / 2 unique AIR (visible/intersection functions are linked into a pipeline only through function tables; no standalone pipeline ABI)
- intersection: 3 entries / 3 unique AIR (visible/intersection functions are linked into a pipeline only through function tables; no standalone pipeline ABI)
- AGX runtime helpers: 709; native Apple GPU archive symbols: 578 (no AIR)
- graphics entries with no same-library pair: 9 (graphics-jobs.json unpaired)
- vertex/fragment AIR functions never placed in a replayed pair: 4 vertex, 5 fragment

## Fixes made tonight (gpu/metal/tegu/mali_graphics_compiler.c, generic pair path only)

| # | where | what | functions unblocked (corpus) |
|---|---|---|---|
| 1 | `zero_undef_read_varyings` (above `compile_pair_specialized`) + call in its generic block | A Metal vertex function may return a struct member it never assigns (AIR `insertvalue` into undef, e.g. QuartzCore `std_vert0_lpf` texcoord0). The translator stores OpUndef, Mesa drops the store and the pair had no layout slot for a varying the FS reads ("Vertex varying layout does not cover interface"). Now a zero is stored, only for FS-read locations whose every store is undefined, so no compiling pair changes. | 18 QuartzCore pairs, 3 UI, 2 watch faces |
| 2 | `mrt_format_ok` | True-MRT compile accepted only 10/70/80/115 while `air_bridge.rs` and n2r accept the blendable class; `render_pipeline.inc` sets genericTrueMrt for any >1 active bound targets, so RenderBox RGBA16F+RG16F and QuartzCore BGR5A1+RGBA16F MRT returned rc1 with an EMPTY message. Widened to the bridge class (texture-opus later appended 40/42/71/81/92). FS code is format independent (per-target blend word is runtime FAU). lowering-opus ACKed n2r. | 12 RenderBox + camera pairs |
| 3 | `lower_clip_distance_varyings` + call | Metal `[[clip_distance]]` (SPIR-V ClipDistance, compact float[n<=8]): Valhall has no clip-distance hardware; same approach as Mesa `nir_lower_clip_fs`: distances go to free generic vec4 varyings and the FS discards where any is negative. Only when the VS actually writes ClipDistance (an unwritten gl_PerVertex member is left alone; an f3 build without that guard regressed check_tile_fetch_compiler and was replaced). The renderer already draws with FORCE_LATE pixel kill and shader_modifies_coverage, so a discarding FS needs no new flag. | 9 VectorKit (Maps) pairs |
| 4 | noperspective check after the FS compile | Mesa reports only noperspective loads that survive FS optimisation (`pan_nir_lower_noperspective_fs`); a declared noperspective input whose load folded away made the exact-equality check refuse ("Fragment varying access mode unsupported"). Now refuses only an FS noperspective read that the VS did not prepare. | PridePoster ribbon, SolidRibbon |
| 5 | `zero_undef_read_varyings` (same pass) | VS whose every declared varying is undefined while the FS reads none (VideoStabilization vertexDrawLines): Mesa dropped them all, leaving no IDVS varying program ("Missing or misaligned IDVS varying program"); the lowest one now gets a zero store. Only applies when the pair was refused before. | 1 camera pair |
| 6 | `mrt_format_integer` + `select_mrt_outputs` type check | Preparation for texture-opus RT step 2b: integer Metal colour formats in a true-MRT target list require a uint/int output of matching signedness (BLEND register format follows the output type); float-class targets keep the exact old refusal. Inert until `mrt_format_ok` lists integer formats; verified with a scratch build listing 123/124: [BGRA8, RGBA32Uint] with vec4+uvec4 compiles, a signedness mismatch refuses. | 0 now; AppleMetalGLRenderer MRT clears once n2r has integer targets |
| 7 | air_bridge.rs `ComputeExtra`, `compile_compute_creation_ex`, `compute_options`, NEW export `tegu_air_pipeline_create_ex` (+ air_bridge.h) | MTLComputePipelineDescriptor inputs the bridge could not carry: `linkedFunctions` (functions/private/binary lists, any library handle), `stageInputDescriptor`, `maxCallStackDepth`. Dispatch-time variants compile under the same inputs. `tegu_air_pipeline_create` = `_ex(NULL)`: 400 random compiling kernels produce byte-identical SPIR-V and Mali packages. | 10 SceneKit `[[stage_in]]` kernels (synthetic stage input descriptor). Linked functions: no installed kernel validates, because MPS/CoreRE reference specializedName symbols that exist in no metallib |
| 8 | air_bridge.rs NEW export `tegu_air_render_pipeline_create_tessellated` + `render_outputs_for` branch; mali_graphics_compiler.c `tess_eval_spirv_as_vertex` + call at the top of `translate_specialized` | Tessellation emulation, compile side. The translator's evaluation stage already pulls control points and patch attributes from buffers. Its remaining inputs (TessCoord; patch inputs Location0 patch index, Location1 instance, Location2.. control-point element indices) become ordinary vertex inputs (TessCoord at Location 30), so it compiles as a VS with the normal generic pair. The existing create entries still refuse 4-stage pipelines (no behaviour change). The runtime tessellator (drawPatches) is api-surface's (a) and is not done, so no draw is claimed. | 214 post-tessellation pairs: 8 FaceID/BiometricKit UI, 206 AvatarKit/RoomPlan; 204 more AvatarKit pairs then stop at "Generic binding index above 15" (23 bindings > plan ABI 16) |
| 9 | air_bridge.rs NEW export `tegu_air_tile_pipeline_create` | MTLTileRenderPipelineDescriptor via the translator's `compile_tile_pipeline`: the tile function becomes a compute pipeline that runs once per tile. QuartzCore `tile_downsample_*` refused "needs pipeline input: TileAttachments" (a wrong diagnostic input: the attachment formats were missing); with a synthetic BGRA8 colour 0 they compile through compute_v2. Runtime per-tile dispatch is not implemented, and these kernels were not seen in the device logs. | 4 QuartzCore kernels (CoreAnimation compute 3/7 -> 7/7) |
| 10 | translator (references/metal2vulkan-tegu-pin, uncommitted, patch `translator-specialized-name.patch` 84ca910d): `LinkedFunction.link_name` + `with_link_name`, `LinkedSource.source`, `LinkedName.source`, and `plan()` looking up the module's own name; bridge `TeguAIRLinkedFunction.link_name` | MTLFunctionDescriptor.specializedName for linked visible functions. MPS kernels call `prefixPrimary_f`, `postfixPrimary_f`, ...; those symbols exist in no installed metallib, while MPSCore's visible functions are `prefixPrimary_f_to_f` and the like. The replay links `<symbol>_to_<type>` under the symbol (synthetic choice, origin recorded). No effect without link_name: 400 kernels and all 142 captured CA pipelines are byte-identical; bridge tests 34/34; metal2vulkan-air link tests 15/15. | 80 MPS kernels; 46 more link and then emit no code under synthetic constants; 9 have a signature mismatch (a wrong synthetic variant guess) |

`check_graphics_draw_varyings.py`: `varyings/undefined_read_output_refused` became `..._zeroed` (deliberate contract change of fix 1); `mrt/invalid/bad_format` uses format 1, because 81 is now a valid MRT format.

No translator (metal2vulkan) source change tonight: the translator-side refusals that were in the way turned out to be wrong diagnostic inputs (see below) or need runtime features owned elsewhere.

## Replay harness corrections (wrong diagnostic inputs, not shader semantics)

- Function constants are specialized against the WHOLE metallib, as production does. The single extracted AIR module has no MTLB CNST table, and `tegu_air_library_specialize` then refused with `Reflection` (QuartzCore fixed_frag_*: this was a harness artefact, not a translator gap).
- QuartzCore pairs: constants whose name and type match a captured device record in corpus.txt take that record's value (origin `captured_donor(corpus.txt)`); this made all QuartzCore pairs except the 2 raster-order-group ones compile with donor values.
- Compute kernels that the canonical synthetic values reduce to an EMPTY program ("Mali backend emitted no code") are retried once with the audit tool's alternate policy (bool 0, uint 2, sint -1, float 1.0, origin `synthetic_diagnostic:alternate`). 16 of 61 then compile; 43 stay empty. An empty kernel is a valid no-op, and the runtime could treat it as one (compute owner).
- Values otherwise: the corpus_inputs plan (`compile-stall-20261026/censusall-base/variants.jsonl`, origin `synthetic_diagnostic:canonical`). Constants the plan does not list get the same rule (bool/int 1, float 0.5, origin `...(completion)`), iteratively, from the declared CNST table. No value is presented as captured.
- Attachments: derived from the declared `air.render_target` outputs AND framebuffer reads. The format is chosen by declared type (float/half: 25/65/80/115, integer: the matching Sint/Uint format). An undeclared slot below the highest one is bound but masked, as `render_pipeline.inc` does (`fmt?fmt:80`, mask 0).
- The compile entry mirrors `render_pipeline.inc`: options v1 `DRAW_PARAMETERS|EXTENDED_VARYINGS|THREAD_STORAGE`; options v2 `MULTI_TARGET` when more than one target is active and all are bound (at most 4); `SELECT_RT` on the first active target otherwise.
- Compute: 593 kernels compile through TGP1 (`tegu_mali_compile`) and 6395 through `compute_v2` after TGP1 refused them, matching runtime_pipeline.inc. 6 kernels are creation-only storage-texture stand-ins (`dispatchable`=0); production compiles those at dispatch with the real formats, and the replay compiles the stand-in SPIR-V with v2.
- CoreAnimation device pipelines: all 142 `TPC_MISS` records in `gpu/build/pipeline-corpus/corpus.txt` are replayed with their exact captured constants, targets, blends and vertex layouts (`p-NNN` cases).

## Gates (host only; GPU_EXECUTED=0; nothing touched on the phone)

Final sources at the C6m freeze: mali_graphics_compiler.c 4b7f6fb2 (private Mali f8 125b3382), air_bridge.rs e70125bb, air_bridge.h 16d1b8ab, check_graphics_draw_varyings.py d4274821, and the translator diff 84ca910d.


- Pipeline-pack identity (`tools/pack_identity.sh`): the pack replay binary of `pipeline-pack-aa4073ac` runs the 142 records with base Mali (`be411e7a` source) vs f5 and f6. Result: 142/142 built, and all 463 cache entries are byte-identical. Codegen of the shipped pack is unchanged.
- Corpus graphics: of 1016 pairs that compiled before, all 1016 still compile with byte-identical VS and FS code; 0 regressions; 63 more pairs compile.
- Bridge (private cdylib 105fee5a from air_bridge.rs e70125bb + translator diff 84ca910d): cargo test 34/34, metal2vulkan-air link tests 15/15, metal2vulkan engine_linking/argument_tables/recursion 31/31. 400 random compiling kernels and all 142 captured CA pipelines produce byte-identical SPIR-V and code.
- Compiler checks with f4/f5/f6/f7/f8: check_graphics_draw_varyings 81/81 (with `--baseline` byte identity), check_tile_fetch_compiler PASS, check_generic_texture_lowering 58/58, check_graphics_specialization 66/66.
- run_host_tests.sh / metal_resource_integration_test.py --candidate / texture_format_frontend_test.py: see `gates/` (they run against the shared tree, which has other agents' in-flight edits).

## Not done, with owner and evidence

- Tessellation, 830 functions (AvatarKit Memoji, FaceID UI `LocalAuthenticationPrivateUI` tessellation_vertex_*, BiometricKitUI MTLSpline, RoomPlan). Post-tessellation vertex functions translate to expanded stages; `air_bridge.rs` render_outputs refuses `stages.len()!=2`. arch9 Mali has no tessellator. This needs a CPU or compute tessellator at drawPatches, plus a translator mode that turns position_in_patch/patch_id/patch_control_point into ordinary VS inputs and buffer fetch. Multi-day work across translator, bridge and runtime.
- Integer and non-blendable colour targets, 330 functions (248 AppleMetalGLRenderer clears/blits, about 70 camera/video). The bridge's bounded format class refuses them. This is texture-opus "RT step 2b"; once n2r has the formats, the compiler side also needs `select_mrt_outputs` to accept int/uint outputs (offered).
- Runtime inputs the bridge API cannot carry, 174 functions: MTLLinkedFunctions (98 MPS), visible function tables (20), tile render pass attachments (QuartzCore `tile_downsample_*`; not seen in device logs), compute stage_in descriptors (10).
- Argument-buffer device pointers, 115 functions (Metal.framework BVH builder, VFX, MPS RayIntersector). `air.indirect_buffer` structs of device pointers. TGP2 needs physical addressing and `gpuAddress` (it returns 0 in api_surface.inc).
- Viewport arrays (142 CoreRE `*_multiple_viewports_*` variants) and layered rendering (16 SceneKit/SceneIntelligence). The runtime refuses amplification above 1. Correct refusal.
- FS depth output (17) and sample mask (4). The renderer has no depth-stencil path yet (`depth_function=ALWAYS`).
- FS storage-image writes (RenderBox resolve_*; 2 UI) and generic binding index >15 (31, e.g. SpriteKit PointLight). The generic plan ABI has 16 bindings (`TEGU_MALI_GENERIC_MAX_BINDINGS`), which renderer/encode own.
- Dynamic-mip storage image writes (SiriUICore `SiriBlur`, NTK Blur::downsample, 5). The translator emits a storage-image array[16] indexed by LOD; TGP2 refuses descriptor arrays even though the manifest has `array_count`. Needs runtime per-level views plus a dynamic table index; compute owner.
- Compile budget, 110 functions (97 MPS). All are in the Mali phase (more than 30 s CPU, many more than 240 s). The rerun with a 240 s budget is in `runs/timeouts-long`. This is a compiler-speed item, not a refusal.
- Translator: Imageblocks / UnknownIntrinsic `air.get_imageblock*` (about 18 camera kernels), Lowering(Access) (3), StaticInitializer (1), CallTypeMismatch (4), and the legacy `air.intersect` signature (56 MTLReplayer).

## Reproduce

```
cd gpu/build/shader-coverage-20261027
python3 tools/replay.py --run NAME [--kind compute|graphics|captured|all] [--filter ids.txt] --jobs N \
   --bridge <libtegu_air_bridge.so> --compiler <libnord2_mali_runtime.so> --profile <tegu-air-profile.h>
bash tools/build_mali_so.sh <dir with mali_*.c/.h> libs/<name>      # private host Mali runtime (shared libs untouched)
tools/pack_identity.sh libs/<name> <tag>; diff pack-gate/base/entries.sha256 pack-gate/<tag>/entries.sha256
python3 tools/ledger.py runs/before runs/after .
```
runs/before = runs/base compute + runs/base-g2 graphics/captured (shared libs). runs/after = the same compute rows + runs/after-f7 graphics/captured. Compute rows are shared because mali_compute_compiler.c is byte-identical (419f8227) in the shared build, base and f5, and the bridge is the same.

## Attribution of the final after-run

The final graphics after-run (`runs/after-f8`) uses a private cdylib built from the working-tree air_bridge.rs. That tree also carries texture-opus's newer colour-target formats in the bridge, and 83 pairs that the f7 run refused with "attachment format ... outside bounded class" compile because of those formats, not because of my changes. Relative to f7: +214 tessellation pairs (fix 8), +10 stage_in kernels (fix 7), +83 from texture-opus formats, 0 regressions. All 1019 pairs that compiled before have byte-identical code. Pipeline pack: 463/463 entries identical (f8).

## Binding index >15 (root item 3): not cheap

TEGU_MALI_GENERIC_MAX_BINDINGS 16 sizes `struct tegu_mali_generic_plan` (stored inside pipeline-cache entries), and TEGU_GENERIC_DRAW_MAX_BINDINGS 16 sizes the render-side draw records (generic_job_ui.h, draw_reorder.inc, render_encoder.inc:2360/2933, nord2_metal_render.c:211-216/328, nord2_metal_render.h `binding[16]`). AvatarKit needs 23 distinct bindings across VS+FS, so this is a count limit and not only an index limit; remapping indices cannot fix it. Widening means a versioned cache key/pack rebuild plus render-lead transport changes, which is not a one-owner edit.

## Post-C6m (after 672108e, translator pin 33ce982a)

- air_bridge.rs `tegu_air_function_patch`: MTLFunction.patchType/patchControlPointCount (FaceID tessellation_vertex_main quad/4, AvatarKit quad/16, CA none/-1).
- air_bridge.rs `tegu_air_pipeline_function_handle`: functionHandleWithFunction: for visible-function tables (MPS linked prefixPrimary_4xf -> word 2; the same function without its specializedName -> not placeable).
- Translator diff 5277f32c (uncommitted, `translator-function-handle-linkname.patch`): `LinkedFunctions::function_handle` compares the link_name.
- Gates: bridge 34/34; metal2vulkan table/linking/argument-table tests 140/140; 400 kernels plus 142 captured CA pipelines byte-identical; pack 463/463 byte-identical.
- Not done: drawPatches end to end. render_pipeline.inc does not yet create tessellation pipelines (see STABLE 6 on the board for the four-step plan); tegu_pipeline_tess_info stays NULL outside the api-surface host spy.

