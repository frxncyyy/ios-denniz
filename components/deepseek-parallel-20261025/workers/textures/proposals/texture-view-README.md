# Texture-view methods proposal

Base: `gpu/metal/tegu/render_encoder.inc`
sha256 `799feca535e3983dbeb7802f31b469e07ac283cf8cd42984c8a5478d083b368b`
and `gpu/metal/tegu/render_encoder_host_test.inc`
sha256 `453dc80f610854285312d57a3d86552e093f4e216e2bbe7aae5935ae32f11a18`.

Current tree: render has already applied the implementation, test-placement
fix and host-main call. Current hashes:
`render_encoder.inc e007df1c088430bf72b88b110c5d940a076fb0e7210228d101edecf9aa053855`,
`render_encoder_host_test.inc 8eb325da514f42935bb7440487c9cab9a231dee1537f2d9a9bd979f2100a6e16`,
`host_test_vs_texture.py 28b425993ed7a16c231a9ba4164a2a65d9d4629bddd4da87dcdaff87cd84cba3`.
Do not reapply the patches to current tree.

Files:
- `texture-view.patch` sha256
  `35456bd8e71ea765392cd3d278730ac914b40fa12fdeee668748999655f90ef4`
- `texture-view-tests.patch` sha256
  `5d89fd46fa0bbdf376109df0009f3631611365761d36eb66517fa0ab3b216ea7`

Apply from `work/`:

Clean base original (implementation + first test patch) remains for provenance:
```
patch -p1 < .../texture-view.patch
patch -p1 < .../texture-view-tests.patch
```

Host-main call patch (base `host_test_vs_texture.py`
`828d90478e25747538f26826625f1eb0fcc380c59bb93856653e06967b1b6853`):
`texture-view-host-call.patch` sha256
`c4cbacbb64bc16476bb2401098344de5be66adb98df5f029ba13187b427c5d71`.

Implementation:
- TeguTexture implements `-newTextureViewWithPixelFormat:` and the
  `textureType:levels:slices:` variant.
- Format aliases use the existing audited
  `n2t_view_format_check(allocation, view, ...)`; RGB/BGR, depth, size/block
  mismatch and compressed LDR/HDR mismatch return nil.
- The view shares the exact source backing (buffer/transient/imported/shadow),
  copies mip/level/slice/layer metadata, and retains the source so an imported
  kernel handle or heap charge cannot be released while a view lives. It owns
  independent retains of the imported surface/shadow Surface and suppresses a
  second kernel-handle release with `importedHandleShared`.
- The second method currently admits only the trivially exact case: same
  `textureType`, `levels.location == 0`, `levels.length == levelCount`,
  `slices.location == 0`, `slices.length == full slice count` (depth for 3D,
  layers for array/cube/MS); anything else returns nil. This is deliberate:
  subviews need a separately qualified address/slice convention.
- No allocation, copy, descriptor rewrite or backing mutation is performed.

Tests:
- `texture-view-tests.patch` adds a real-frontend test to
  `render_encoder_host_test.inc`: valid RGBA8 -> sRGB alias, shared backing/
  metadata, source-outlives-view, invalid RGB/BGR/depth/size formats, exact
  levels/slices and mismatched type/range nil paths.
- It is called from `test_render_commands` and, with the host-call patch, from
  the render host harness immediately after device construction.
- The render owner applied the placement and host-call corrections. Running
  `workers/render/host_test_vs_texture.py` from the current tree returns rc0
  and emits
  `PASS real frontend: ...`; the generated source contains and calls
  `test_texture_views`. Log evidence:
  `workers/textures/evidence-texture-view-host.log`
  sha256 `0eeb60009f5a39d3a08bc073571a976d6e660478577229d9d60df82e74ae12c2`.
  A private overlay also emitted `VIEW_TEST_PASS` independently under ASan.
  No assertion was suppressed.

Limitations:
- Sub-range levels/slices and 3D depth-plane views are explicit nil refusals.
- Views are aliases only; no new allocation, swizzle, storage-mode change, or
  format conversion.
- No GPU/native/device claim.
