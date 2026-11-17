# Compute DELTA19 — BGR5A1Unorm (MTLPixelFormat 43) planner admission

Lowering-opus C6g request. No render files or shared headers were edited. The
only production change is `metal/tegu/air_bridge.rs`.

## Change

- Explicit render-attachment class now includes
  `PixelFormat::BGR5A1Unorm` (Metal 43, translator VK
  `A1R5G5B5_UNORM_PACK16`) alongside R8Unorm/RGBA8Unorm/BGRA8Unorm/RGBA16Float.
- `generic_target_formats` now admits format 43 in the exact 1..4-target set:
  `{10,43,70,80,115}`. Empty, more than four, format-0 gaps, and all other
  formats remain exact refusals.
- The blend admission in the same Rust file also includes BGR5A1Unorm. C6g's
  real F43 pipeline descriptor enables source-over blending; without that
  admission the pipeline was still rejected before its attachment reached the
  planner. The blend equations themselves are unchanged and were verified by
  the Rust test and the C6g host harness.

## Host evidence

- Rust: **31/31 PASS** (`cargo test --release` from updated worker `air-work`).
  - `bgr5a1_attachment_and_blend_are_admitted`: format 43 maps to
    `BGR5A1Unorm`, and the captured source-over blend is accepted.
  - `generic_target_sets_are_exact_and_mix_supported_formats`: exhaustive
    780 ordered sets of lengths 1..4 over `{10,43,70,80,115}`, explicit
    `[43]`, `[43,43]`, `[80,43]`, `[115,115]`, `[80,115]`, and unsupported
    formats in every position.
- Host cdylib rebuilt from source:
  `449db4c3014b93088eb4048c6e3dacfbcd35d147f07ccc4f8e076a01c32a4842`.
- Real producer: **7/7 PASS** against compiler
  `d91bb48507662f2a5581f898301a2e1c46c7e56c5c4cae5ac551852b0e9e63e3`.
  Receipt `build/compute-v2-producer-current-abi/result.json`
  sha256 `ee2acc417200c80ce75ec9838f025d0e78d9b3331199fa5b24acb05a450c1bf7`
  records source `29e161ac...`, producer `449db4c3...`, compiler
  `d91bb485...`, `gpu_executed=false`.
- C6g prebuilt host harness (C6g render source unchanged, new cdylib loaded
  through `LD_LIBRARY_PATH`): release and ASan/UBSan both **33 checks PASS,
  0 pending**. Observed:
  - `C6F_F43_PIPE ... inert=0 draw=1 mrt=1 fmt=43`
  - `C6F_F43_DRAW invalid=0 count=3`
  - all three F43 Load/Clear/DontCare passes status 4 with no error.
  This is a host encode/plan result, not GPU execution and not a render-file
  claim.

## Hashes

- `metal/tegu/air_bridge.rs`
  `29e161ace1518eda3fa041b474f051db380bafb140136bbdd3fb3d8c772f27e9`
- host cdylib
  `449db4c3014b93088eb4048c6e3dacfbcd35d147f07ccc4f8e076a01c32a4842`
- receipt
  `ee2acc417200c80ce75ec9838f025d0e78d9b3331199fa5b24acb05a450c1bf7`
- worker `air-work/src/lib.rs`
  `29e161ace1518eda3fa041b474f051db380bafb140136bbdd3fb3d8c772f27e9`
  (byte-equal to production)

## Explicit gaps

- No phone, native rebuild, device execution, publication, or hardware claim.
- Format 43 is handled by the generic per-target host path here; the native
  fixed-function format map was not widened and no hardware semantics are
  claimed.
- Render-owned C6g files remain owned by lowering-opus/root and were not
  edited.
