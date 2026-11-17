# The values GNR.videoprocessor (iOS 16.7, shared cache 0x1f205b000) asks its 52 functions with, read from the disassembly of its
# stages (11 November 2026): the index and type from each -setConstantValue:type:atIndex: (29 = int, 53 = bool), the values from the
# loops and arguments around it. Where a value is an argument whose callers were not all followed (fusionType, lga) the range is wider
# than the code can ask. A function that is not here takes no constants.
#   +[PyramidStageShared compileShader:lumaWrite:chromaType:] 0x1f207e398          pyramid_downsample: 2 = lumaWrite, 1 = chromaType 0..2
#   -[BilateralGridShaders initWithMetal:normalizeGridConfidence:] 0x1f207a6b4     bg_splat 3 = 0,1; bg_blur 2 = 0..2 and 1; bg_norm 1;
#                                                                                BilateralUpsample_frag 3 = 0,1
#   -[DenoiseRemixFragmentShader initWithMetal:vertFunc:fragName:useLGA:useGDGNR:...] 0x1f2071400     0 = lga, 1 = gdgnr (both int)
#   -[FuseRemixFragmentShader initWithMetal:fragName:pixelFormat:fusionType:...] 0x1f20737e0          0 = fusionType (int), 2, 3, 4 bool
#   -[FusionRemixStage initBandFusion:] 0x1f2060df8                                1 = 256 (HistogramSize)
pyramid_downsample: 2=0 1=0
pyramid_downsample: 2=0 1=1
pyramid_downsample: 2=0 1=2
pyramid_downsample: 2=1 1=0
pyramid_downsample: 2=1 1=1
pyramid_downsample: 2=1 1=2
bg_splat: 3=0
bg_splat: 3=1
bg_blur: 2=0 1=0
bg_blur: 2=0 1=1
bg_blur: 2=1 1=0
bg_blur: 2=1 1=1
bg_blur: 2=2 1=0
bg_blur: 2=2 1=1
bg_norm: 1=0
bg_norm: 1=1
BilateralUpsample_frag: 3=0
BilateralUpsample_frag: 3=1
DenoiseChroma_frag: 0=0 1=0
DenoiseChroma_frag: 0=0 1=1
DenoiseChroma_frag: 0=1 1=0
DenoiseChroma_frag: 0=1 1=1
DenoiseChroma_frag: 0=2 1=0
DenoiseChroma_frag: 0=2 1=1
DenoiseLumaDenoiseRemixChroma_frag: 0=0 1=0
DenoiseLumaDenoiseRemixChroma_frag: 0=0 1=1
DenoiseLumaDenoiseRemixChroma_frag: 0=1 1=0
DenoiseLumaDenoiseRemixChroma_frag: 0=1 1=1
DenoiseLumaDenoiseRemixChroma_frag: 0=2 1=0
DenoiseLumaDenoiseRemixChroma_frag: 0=2 1=1
DenoiseRemixLumaChroma_frag: 0=0 1=0
DenoiseRemixLumaChroma_frag: 0=0 1=1
DenoiseRemixLumaChroma_frag: 0=1 1=0
DenoiseRemixLumaChroma_frag: 0=1 1=1
DenoiseRemixLumaChroma_frag: 0=2 1=0
DenoiseRemixLumaChroma_frag: 0=2 1=1
DenoiseLumaAlpha_frag: 0=0 1=0
DenoiseLumaAlpha_frag: 0=0 1=1
DenoiseLumaAlpha_frag: 0=1 1=0
DenoiseLumaAlpha_frag: 0=1 1=1
DenoiseLumaAlpha_frag: 0=2 1=0
DenoiseLumaAlpha_frag: 0=2 1=1
RemixLuma_frag: 0=0
RemixLuma_frag: 0=1
RemixLuma_frag: 0=2
WriteOutGainMap_frag: 0=0
WriteOutGainMap_frag: 0=1
WriteOutGainMap_frag: 0=2
calculateBlackWhiteStrengthCenter: 1=256
LTM: 1=256
FusionVert: 0=0 2=0
FusionVert: 0=0 2=1
FusionVert: 0=1 2=0
FusionVert: 0=1 2=1
FusionVert: 0=2 2=0
FusionVert: 0=2 2=1
FusionVert: 0=3 2=0
FusionVert: 0=3 2=1
FuseLumaTopBand_Frag: 0=0 2=0 3=0 4=0
FuseLumaTopBand_Frag: 0=0 2=0 3=0 4=1
FuseLumaTopBand_Frag: 0=0 2=0 3=1 4=0
FuseLumaTopBand_Frag: 0=0 2=0 3=1 4=1
FuseLumaTopBand_Frag: 0=0 2=1 3=0 4=0
FuseLumaTopBand_Frag: 0=0 2=1 3=0 4=1
FuseLumaTopBand_Frag: 0=0 2=1 3=1 4=0
FuseLumaTopBand_Frag: 0=0 2=1 3=1 4=1
FuseLumaTopBand_Frag: 0=1 2=0 3=0 4=0
FuseLumaTopBand_Frag: 0=1 2=0 3=0 4=1
FuseLumaTopBand_Frag: 0=1 2=0 3=1 4=0
FuseLumaTopBand_Frag: 0=1 2=0 3=1 4=1
FuseLumaTopBand_Frag: 0=1 2=1 3=0 4=0
FuseLumaTopBand_Frag: 0=1 2=1 3=0 4=1
FuseLumaTopBand_Frag: 0=1 2=1 3=1 4=0
FuseLumaTopBand_Frag: 0=1 2=1 3=1 4=1
FuseLumaTopBand_Frag: 0=2 2=0 3=0 4=0
FuseLumaTopBand_Frag: 0=2 2=0 3=0 4=1
FuseLumaTopBand_Frag: 0=2 2=0 3=1 4=0
FuseLumaTopBand_Frag: 0=2 2=0 3=1 4=1
FuseLumaTopBand_Frag: 0=2 2=1 3=0 4=0
FuseLumaTopBand_Frag: 0=2 2=1 3=0 4=1
FuseLumaTopBand_Frag: 0=2 2=1 3=1 4=0
FuseLumaTopBand_Frag: 0=2 2=1 3=1 4=1
FuseLumaTopBand_Frag: 0=3 2=0 3=0 4=0
FuseLumaTopBand_Frag: 0=3 2=0 3=0 4=1
FuseLumaTopBand_Frag: 0=3 2=0 3=1 4=0
FuseLumaTopBand_Frag: 0=3 2=0 3=1 4=1
FuseLumaTopBand_Frag: 0=3 2=1 3=0 4=0
FuseLumaTopBand_Frag: 0=3 2=1 3=0 4=1
FuseLumaTopBand_Frag: 0=3 2=1 3=1 4=0
FuseLumaTopBand_Frag: 0=3 2=1 3=1 4=1
FuseLumaBand_Frag: 0=0 2=0 3=0 4=0
FuseLumaBand_Frag: 0=0 2=0 3=0 4=1
FuseLumaBand_Frag: 0=0 2=0 3=1 4=0
FuseLumaBand_Frag: 0=0 2=0 3=1 4=1
FuseLumaBand_Frag: 0=0 2=1 3=0 4=0
FuseLumaBand_Frag: 0=0 2=1 3=0 4=1
FuseLumaBand_Frag: 0=0 2=1 3=1 4=0
FuseLumaBand_Frag: 0=0 2=1 3=1 4=1
FuseLumaBand_Frag: 0=1 2=0 3=0 4=0
FuseLumaBand_Frag: 0=1 2=0 3=0 4=1
FuseLumaBand_Frag: 0=1 2=0 3=1 4=0
FuseLumaBand_Frag: 0=1 2=0 3=1 4=1
FuseLumaBand_Frag: 0=1 2=1 3=0 4=0
FuseLumaBand_Frag: 0=1 2=1 3=0 4=1
FuseLumaBand_Frag: 0=1 2=1 3=1 4=0
FuseLumaBand_Frag: 0=1 2=1 3=1 4=1
FuseLumaBand_Frag: 0=2 2=0 3=0 4=0
FuseLumaBand_Frag: 0=2 2=0 3=0 4=1
FuseLumaBand_Frag: 0=2 2=0 3=1 4=0
FuseLumaBand_Frag: 0=2 2=0 3=1 4=1
FuseLumaBand_Frag: 0=2 2=1 3=0 4=0
FuseLumaBand_Frag: 0=2 2=1 3=0 4=1
FuseLumaBand_Frag: 0=2 2=1 3=1 4=0
FuseLumaBand_Frag: 0=2 2=1 3=1 4=1
FuseLumaBand_Frag: 0=3 2=0 3=0 4=0
FuseLumaBand_Frag: 0=3 2=0 3=0 4=1
FuseLumaBand_Frag: 0=3 2=0 3=1 4=0
FuseLumaBand_Frag: 0=3 2=0 3=1 4=1
FuseLumaBand_Frag: 0=3 2=1 3=0 4=0
FuseLumaBand_Frag: 0=3 2=1 3=0 4=1
FuseLumaBand_Frag: 0=3 2=1 3=1 4=0
FuseLumaBand_Frag: 0=3 2=1 3=1 4=1
FuseChromaBand_Frag: 0=0 2=0 3=0 4=0
FuseChromaBand_Frag: 0=0 2=0 3=0 4=1
FuseChromaBand_Frag: 0=0 2=0 3=1 4=0
FuseChromaBand_Frag: 0=0 2=0 3=1 4=1
FuseChromaBand_Frag: 0=0 2=1 3=0 4=0
FuseChromaBand_Frag: 0=0 2=1 3=0 4=1
FuseChromaBand_Frag: 0=0 2=1 3=1 4=0
FuseChromaBand_Frag: 0=0 2=1 3=1 4=1
FuseChromaBand_Frag: 0=1 2=0 3=0 4=0
FuseChromaBand_Frag: 0=1 2=0 3=0 4=1
FuseChromaBand_Frag: 0=1 2=0 3=1 4=0
FuseChromaBand_Frag: 0=1 2=0 3=1 4=1
FuseChromaBand_Frag: 0=1 2=1 3=0 4=0
FuseChromaBand_Frag: 0=1 2=1 3=0 4=1
FuseChromaBand_Frag: 0=1 2=1 3=1 4=0
FuseChromaBand_Frag: 0=1 2=1 3=1 4=1
FuseChromaBand_Frag: 0=2 2=0 3=0 4=0
FuseChromaBand_Frag: 0=2 2=0 3=0 4=1
FuseChromaBand_Frag: 0=2 2=0 3=1 4=0
FuseChromaBand_Frag: 0=2 2=0 3=1 4=1
FuseChromaBand_Frag: 0=2 2=1 3=0 4=0
FuseChromaBand_Frag: 0=2 2=1 3=0 4=1
FuseChromaBand_Frag: 0=2 2=1 3=1 4=0
FuseChromaBand_Frag: 0=2 2=1 3=1 4=1
FuseChromaBand_Frag: 0=3 2=0 3=0 4=0
FuseChromaBand_Frag: 0=3 2=0 3=0 4=1
FuseChromaBand_Frag: 0=3 2=0 3=1 4=0
FuseChromaBand_Frag: 0=3 2=0 3=1 4=1
FuseChromaBand_Frag: 0=3 2=1 3=0 4=0
FuseChromaBand_Frag: 0=3 2=1 3=0 4=1
FuseChromaBand_Frag: 0=3 2=1 3=1 4=0
FuseChromaBand_Frag: 0=3 2=1 3=1 4=1
FuseChromaTopBand_Frag: 0=0 2=0 3=0
FuseChromaTopBand_Frag: 0=0 2=0 3=1
FuseChromaTopBand_Frag: 0=0 2=1 3=0
FuseChromaTopBand_Frag: 0=0 2=1 3=1
FuseChromaTopBand_Frag: 0=1 2=0 3=0
FuseChromaTopBand_Frag: 0=1 2=0 3=1
FuseChromaTopBand_Frag: 0=1 2=1 3=0
FuseChromaTopBand_Frag: 0=1 2=1 3=1
FuseChromaTopBand_Frag: 0=2 2=0 3=0
FuseChromaTopBand_Frag: 0=2 2=0 3=1
FuseChromaTopBand_Frag: 0=2 2=1 3=0
FuseChromaTopBand_Frag: 0=2 2=1 3=1
FuseChromaTopBand_Frag: 0=3 2=0 3=0
FuseChromaTopBand_Frag: 0=3 2=0 3=1
FuseChromaTopBand_Frag: 0=3 2=1 3=0
FuseChromaTopBand_Frag: 0=3 2=1 3=1
blackSubtractAndAddingContrast: 2=0
blackSubtractAndAddingContrast: 2=1
# The render pipelines, as the stages pair the functions; the formats are the ones the code can ask (R8Unorm 10, R16Float 25,
# RG8Unorm 30, RG16Float 65, RGBA16Float 115), all tried.
pipeline: BilateralUpsample_vert BilateralUpsample_frag 10,25,30,65,115
pipeline: RemixDenoise_vert DenoiseChroma_frag 10,25,30,65,115
pipeline: RemixDenoise_vert DenoiseLumaDenoiseRemixChroma_frag 10,25,30,65,115
pipeline: RemixDenoise_vert DenoiseRemixLumaChroma_frag 10,25,30,65,115
pipeline: RemixDenoise_vert RemixLuma_frag 10,25,30,65,115
pipeline: RemixDenoise_vert DenoiseLumaAlpha_frag 10,25,30,65,115
pipeline: RemixDenoise_vert CalculateLocalGain_frag 10,25,30,65,115
pipeline: RemixDenoise_vert WriteOutGainMap_frag 10,25,30,65,115
pipeline: FusionVert FuseLumaTopBand_Frag 10,25,30,65,115
pipeline: FusionVert FuseChromaTopBand_Frag 10,25,30,65,115
pipeline: FusionVert FuseLumaBand_Frag 10,25,30,65,115
pipeline: FusionVert FuseChromaBand_Frag 10,25,30,65,115
pipeline: FusionVert blackSubtractAndAddingContrast 10,25,30,65,115
pipeline: regpyr_vert regpyr_initial_downscale_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_bilinear_downscale_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_deriv_sobel_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_basic_search_luma_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_fusion_luma_x_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_fusion_luma_y_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_smooth_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_selection_luma_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_confidence_stage_one_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_confidence_erode_frag 10,25,30,65,115
pipeline: regpyr_vert regpyr_confidence_dilate_frag 10,25,30,65,115
pipeline: regpyr_vert pyramidConfidence 10,25,30,65,115
