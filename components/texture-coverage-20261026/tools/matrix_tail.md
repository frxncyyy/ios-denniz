
Note: the translator columns come from translator-format-audit-p2.jsonl, measured before the
mali_texture_profile colour-attachment additions (R4G4B4A4, R8/RG8 sRGB), so their
`render`/`blend` cells still read `profile` there; the status column is the current host result.

## Private / IOSurface-only formats (not in the public SDK enum)

| fmt | name | status | test |
|---|---|---|---|
| 500, 502, 503, 505-510 | YCBCR8/10 4:2:0/4:2:2/4:4:4 2P (+packed) | EMULATED: sampled IOSurface decode shadow, Metal's integer conversion (range from OSType, matrix from kIOSurfaceYCbCrMatrix), 8-bit fast path 16 ms per 1125x2436 snapshot; RGB10A2 internal (<= half a 10-bit step) | ycbcr_decode_test (bit-exact vs reference), YCBCR_IMPORTS, 420v draw status 4 |
| 520-530 | YCbCr _sRGB twins | EMULATED: decode shadow RGBA16F (1/8192 steps rounded to half, ties away) | same |
| 540-542 | RGB8 2P codes | EMULATED: RGBA8 codes shadow | YCBCR_IMPORTS (540) |
| 543-545 | RGB10 2P codes | REFUSED: RGB10A2 codes shadow allocation pending | - |
| 550/551 | RGB10A8_2P_XR10 ('b3a8') | sampled per plane (plane0 as BGR10_XR decode shadow, plane1 A8); render target REFUSED (CPU encode of a full display per frame would be too slow; needs a native path) | PLANE_IMPORTS |

## IOSurface import classes (newTextureWithDescriptor:iosurface:plane:)

| class | status | evidence / test |
|---|---|---|
| BGRA8 render target (usage incl. RT), 64-byte stride | native kernel import | existing; CA test |
| BGRA8 render target, unaligned stride or kernel refusal | owned shadow, write-back | existing; CA test |
| sampled-only (usage==1) single-plane, bpp(format)==bytesPerElement | byte-exact owned shadow, seed-gated refresh | RGhA/115 case |
| BGR10_XR/_sRGB ('w30r'), BGRA10_XR/_sRGB ('w40a','w40m'), usage 1 or 5 | EMULATED: RGBA16F decode shadow; usage 5 encoded back to XR codes after GPU writes | c6k 14x / c6l 2x refusals; XR_IMPORTS, XR_RENDER_WRITEBACK (all codes), XR_TARGETS |
| plane N of a multi-plane surface | per-plane owned shadow | PLANE_IMPORTS |
| whole two-plane YCbCr surface as format 500-542 | decode shadow (see above) | YCBCR_IMPORTS |
| Apple lossless-compressed ('&...') | REFUSED (no linear bytes) | PLANE_IMPORTS |

## Texture types (allocation -> planner -> n2t sidecar), host draw status

| type | status |
|---|---|
| 2D | native, all n2t sampled formats (79 draw on the float probe) |
| 2DArray, Cube, CubeArray, 3D, 1D, 1DArray | native on the specialized path, with mip chains (TEXTURE_TYPES 8/8 through authored AIR) |
| 2DMultisample(+Array) | allocated + planner-admitted; texture2d_ms read (get_num_samples + per-sample read, authored ms_read_probe.ll) draws status 4 for RGBA8/RGBA16F (MS_READS 2/2; contents only via MSAA rendering, no CPU upload in Metal) |
| TextureBuffer | REFUSED (no allocation class yet) |
| views | Metal type casts + level/slice sub-ranges (VIEWS); render into mip levels via level views |

## Usages

| usage | status |
|---|---|
| sample (filter/address/LOD/aniso/compare via exact n2t sampler) | native; constexpr samplers use the translator's exact VkSampler (variable_blur c6l fix). NOTE: the Metal sampler factory keeps the narrow legacy admission (compare/mip/repeat/aniso/LOD-range samplers return nil) until the v2 owner calls tegu_sampler_enable_exact_factory(1); no device refusal logged in c6k/c6l |
| depth sampling (Depth16Unorm 250, Depth32Float 252) | native on the specialized path: depth2d sample (translator DEPTH_AS_COLOR (d,d,d,1) as descriptor swizzle R,R,R,1) and sample_compare (n2t compare sampler); compare without a compare function refuses; depth attachment stores stay refused by pass admission, so sampled data comes from CPU/blit (DEPTH_SAMPLING 5/5); packed depth-stencil / stencil sampling REFUSED |
| 8-bit sRGB sampling (11/31/71/81) | native via translator variant (VariantKey::srgb_textures: raw UNORM view + Metal's 12-bit decode table in-shader); compiled variants are serialized with the pipeline, so caches/packs carry them (SRGB_VARIANTS drawn=2 carried=1) |
| integer textures through integer or float slots | native; float-slot reads run the translator's cross-class variant; integer sampler use = vk_sampler integer rules (nearest, no aniso, lod min 0, integer border words; INTEGER_SAMPLER) |
| render target, blendable | native (opaque + blend): A8, R8(+sRGB), R16F, RG8(+sRGB), B5G6R5, ABGR4, BGR5A1, RG16F, RGBA8(+sRGB), BGRA8(+sRGB), RGB10A2, RG11B10F, BGR10A2, RGBA16F (RENDER_TARGETS 36/36; descriptors = Mesa v9 tables, rt_format_oracle_test 49 formats); sRGB opaque writes bind the translator's ColorWriteTable (Metal's exact encode); B5G6R5/RG11B10F refuse destination-alpha blend factors (no alpha in the tile) |
| render target, raw | native opaque output for R8/R16/RG8/RG16/R32/RG32/RGBA8/RGBA16/RGBA32 Uint/Sint, R8/R16/RG8/RG16/RGBA8/RGBA16 Snorm, R16/RG16/RGBA16 Unorm, R32/RG32/RGBA32 Float, RGB10A2Uint (BLEND.u32/.s32/.f32 register formats, Mesa raw tile + format conversion); blending, partial write masks and MSAA resolve REFUSED; LOAD and clear-only passes REFUSED for integer/snorm (the float preload cannot round-trip them), exact for float32/unorm16; Clear folded into a draw's frame uses Metal's clear conversion (clear_value.rs rules) |
| generateMipmaps | CPU per-format box filter, 29 formats (MIPMAPS 6/6, mip_codec_test) |
| replaceRegion/getBytes | uncompressed: rows; block-compressed: replaceRegion block rows; XR shadows reach the surface |
| blit copy | uncompressed equal-bpp formats |

## Next steps (in order)
1. Typed (uint/sint) LOAD/clear preload shaders so integer targets keep contents across jobs.
2. Enable the exact sampler factory once every legacy consumer guards (needed for depth compare and app samplers on device).
3. 2DMS reads (6 corpus uses), TextureBuffer (0 corpus uses): ledger only.
4. A1BGR5 (Mali A1_BGR5_UNORM exists but no Mesa mapping to verify against: refused), GBGR422/BGRG422 (YUYV8/VYUY8 + swizzle unverified: refused), PVRTC (refused).
5. GPU mip/YCbCr passes only if device logs show cost (c6l: CPU blits 0.9% of exec).
