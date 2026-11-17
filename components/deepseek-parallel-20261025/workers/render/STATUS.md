# render STATUS — 25 October 2026

Worker: render (DeepSeek 4.1 Flash max). No phone/adb/fastboot/USB, no rootfs or
mount writes, no CI/publish, no native plugin rebuild, no commit. Root owns
commits and global NOTES.

## DELTA15 — tile-refusal harness count correction (26 Oct 2026)

- Root C6 run confirmed each binary emits exactly **three** `TEGU_TILE_REFUSED`
  lines, one per tile selector. The harness had incorrectly required six in a
  single process. Changed only `run_checked` from `==6` to `==3`; all
  real-binary async/tile behavior assertions remain.
- New host harness hash `192eb98a4142bae7062170a2bf1cfcf8061b4cd5c44cb343fdf95c941b5a2828`.
- Final release and one ASan pass are **pending root HEAVY GRANT**; root holds
  HEAVY for C6. No release result is claimed yet.
- Production hashes unchanged from C6: encoder `45499802e583808a58d623d38dacfc36d8e8362bc22f869981656777f8a5866a`,
  pipeline `5a361762bdf3cfab737ae75ddf1a4e401c7d4ee0566fc5236f009161c2f8dec0`,
  pass `788f80e70e43701914dadcdb2f9016c3fa454a866fcdd6c71cf19e766d3c5e32`,
  submit `b011443c359e9bebd26992203e52b86643eb3f4139c17bc8d7a8b6019bdfc0cc`.

## DELTA14 — async PSO selector + tile refusals (26 Oct 2026)

### Implemented
- `TeguComputeDevice newRenderPipelineStateWithDescriptor:completionHandler:`
  uses the exact sync creation path and invokes the completion block exactly
  once with pipeline-or-nil and NSError. It therefore inherits the same
  backboardd inert behavior as the sync selector.
- `newRenderPipelineStateWithTileDescriptor:options:reflection:error:` is an
  explicit nil + NSError "Tile render pipelines are unsupported"; reflection
  is cleared and no tile pipeline is fabricated.
- `TeguRenderEncoder dispatchThreadsPerTile:`, `setTileBytes:length:atIndex:`
  and `setTileTexture:atIndex:` invalidate the command and emit bounded
  `TEGU_TILE_REFUSED selector=... reason=no_tile_pipeline` traces.
- Host tests cover async valid/refused completion-once, tile descriptor
  nil/error/reflection, and all three tile selectors.
- Integration C3 fields posted: attachment `resolveDepthPlane` (plus
  resolveLevel/resolveSlice), encoder `resolveTargets[4]` and
  `rasterSampleCount`.

### Verification
- ASan/UBSan real-bridge fake-JM host run PASS with async/tile and MSAA tests;
  both binaries execute the new selectors.
- Final release run was **not completed**: lowering held HEAVY and root
  instructed render to release; no release result is claimed.
- ARM64 default/inert syntax was not rerun after the final host-test-only
  assertion edits; production syntax before those edits passed.
- No native/phone/publish/commit.

### Hashes
- render encoder `45499802e583808a58d623d38dacfc36d8e8362bc22f869981656777f8a5866a`
- pipeline `5a361762bdf3cfab737ae75ddf1a4e401c7d4ee0566fc5236f009161c2f8dec0`
- render pass `788f80e70e43701914dadcdb2f9016c3fa454a866fcdd6c71cf19e766d3c5e32`
- submit `b011443c359e9bebd26992203e52b86643eb3f4139c17bc8d7a8b6019bdfc0cc`
- blit encoder `6fca3a6ab76f07cbc390f3940d12b149bf11511ab612d042dc3762eefeee1db5`
- encoder host test `507cf260bc1233973b40e3a76277d0ca01561fe3f8975778561cbf387c7e13fc`
- host harness `257931e1ed1900298035b75b33521552efab38c4bbe83cc24e0d799596be0027`

### Remaining
- Final release run blocked by HEAVY; rerun release after root grants. C3
  fake needs the posted fields. No hardware claim.

## DELTA13 — generic 4x MSAA + resolve (26 Oct 2026)

### Implemented
- Generic type4/MTLTextureType2DMultisample sample4 colour allocation with
  ordinary linear sample-major backing, bounded by the generic-job capability;
  sample4 target and sample1 resolve storage/retention validation.
- `rasterSampleCount` is carried on the render pipeline (1 or4) and matched by
  the encoder; existing pipelines remain sample1.
- `nord2_render_pass` admits sample4 colour targets with a same-size/format
  sample1 resolve, storeAction 2/3 and clear-load only. Missing resolve,
  sample1 resolve mismatch, sample4 Load and malformed spans are explicit inert
  pass refusals.
- Generic sidecar now carries n2mb v3 `sample_counts[4]` and token-based
  `resolves[4]`; sample4 single-target uses one v4 target job, while genuine
  MRT can carry every target. Resolve/target textures are retained in
  compositorTextures slots 23..30.
- `compositor_submit.inc` selects n2mb v3/v4 for MSAA and preserves v1/v2/v3
  paths otherwise.

### Host evidence
- Real bridge/fake JM: type4 sample4 target + resolve, rasterSampleCount4
  pipeline, sidecar count1/msaa/sample_counts4/resolve token, `n2mb_draw_v4`
  v3 status4 in both binaries.
- Missing resolve sample4 pass is inert/status5.
- Existing blit/texture/backboardd/inert/path/TLS/MRT suites release PASS and
  one ASan/UBSan PASS. ARM64 default/inert syntax PASS.
- No native/phone/publish/commit.

### Explicit gaps
- `device_render_query_caps.h`/`supportsSampleCount:` is compute-owned and still
  reports 4 false. Render direct creation/pipeline execution is wired, but the
  query must be enabled by compute/root before API clients are guaranteed to
  request the class. A-board request posted.
- Sample4 Load remains an explicit refusal; only CLEAR-class first draw is
  supported. True-MRT fetch snapshot remains absent.
- No hardware qualification.

### Hashes
- render encoder `1623cd62f08c4312786ea069f305e220b51df3618769781f3e0c1c779abe6e35`
- pipeline `4ab8b0aa4dc6a8d0764d87cfb27579227343be4b06b8b1001cb26e5505efbebf`
- render pass `788f80e70e43701914dadcdb2f9016c3fa454a866fcdd6c71cf19e766d3c5e32`
- submit `b011443c359e9bebd26992203e52b86643eb3f4139c17bc8d7a8b6019bdfc0cc`
- blit encoder `6fca3a6ab76f07cbc390f3940d12b149bf11511ab612d042dc3762eefeee1db5`
- encoder host test `507cf260bc1233973b40e3a76277d0ca01561fe3f8975778561cbf387c7e13fc`
- host harness `cbef04a3d82bbe404b55675cd14ccfb8a62d6e8dc02ec49fde36f0a151bd1a1a`

## DELTA11/12 — blit completeness + test correction (26 Oct 2026)

### Implemented (render-owned `blit_encoder.inc`)
- Added `copyFromBuffer:...sourceBytesPerRow:sourceBytesPerImage:sourceSize:toTexture:...` and options variant, validating buffer spans, texture level/slice/origin, row/image strides and same-backing aliasing.
- Added `copyFromTexture:...toBuffer:...destinationBytesPerRow:destinationBytesPerImage:` and options variant.
- Reworked texture-to-texture copy to cover regions, nonzero levels, array slices/3D depth, exact row/image strides and `n2t_view_format_check` compatible views; one ordered CPU row job per depth slice.
- Added `synchronizeTexture:slice:level:`, `optimizeContentsForGPUAccess:` and slice/level variant, and ordered `updateFence:`/`waitForFence:` no-ops for shared coherent storage.
- Existing `fillBuffer`, buffer-to-buffer and mipmap box-filter paths retained; errors invalidate the whole command before any partial jobs.

### Test/correction
- The integration abort was a test-order defect, not a production copy defect: the sRGB view aliases `dst`, so its 4x4 overlay ran before the full-region assertion. The view copy now runs after the full comparison and is asserted exactly over its 4x4 overlay.
- Existing blit tests were also corrected for 64-byte row padding (`rowBytes`, not packed width) before asserting texels.
- `test_texture_blits`, `test_mipmaps` and `test_blit_completeness` are now placed before the host split and executed by the real frontend host main.
- Host suite: release PASS and one ASan/UBSan PASS on the final source; full prior DELTA8/9/10 checks green. No native/phone/publish/commit.

### Hashes
- blit encoder `6fca3a6ab76f07cbc390f3940d12b149bf11511ab612d042dc3762eefeee1db5`
- render encoder `e9c948416eeddcc1b376eefc54a6e3f989bc238fade5eb9aee082a369ddf347f`
- encoder host test `507cf260bc1233973b40e3a76277d0ca01561fe3f8975778561cbf387c7e13fc`
- pipeline `6555af8a35885db6fea64941a709ef9c260b4b79b84c9a8f76823e7dc55a18c8`
- render pass `2d879008866c376144e069b0fee00eea413b6a99790fe487466cee8155466536`
- host harness `be45d048c25d5e90db9bd7b181e84e97ac3e445917cc26822be667531e34762c`

### Remaining gaps
- Valid 2D-array/3D blit slices are implemented in the CPU walk but cannot be created by the current generic texture allocation class, so the host test cannot allocate a real array fixture; the helper remains covered by bounds/format and 2D-level tests.
- `synchronizeResource` is the existing shared-coherent no-op category; fences are command-order markers on CPU-coherent storage, not a GPU fence model.
- R2 MSAA/instancing remain cross-owner ABI blockers from DELTA10.

## DELTA10 — texture views applied; MSAA/instancing ABI blocked (26 Oct 2026)

### Implemented
- Applied textures' `texture-view.patch` and `texture-view-tests.patch`:
  - `newTextureViewWithPixelFormat:` and the exact
    `textureType:levels:slices:` variant.
  - Zero-copy format aliases use the shared `n2t_view_format_check` policy;
    views retain the source/backing/imported surface and suppress duplicate
    kernel-handle release.
  - Sub-range levels/slices and mismatched type/range remain explicit nil.
- Texture-view host assertions are now placed before the host script's
  `@class IOSurfaceFixture` split and called immediately after device creation;
  both ASan/UBSan fake-JM binaries execute them and PASS, including the existing
  DELTA8/9 suite.

### Blocked / explicit gaps
- **R2 MSAA 4x + resolve is not implemented in this delta.** Current
  `n2r_render_targets`/`n2mb_render_targets` have no sample-count or resolve
  address/format sidecar, and `n2r` hard-codes single-sample framebuffer/RT
  descriptors. Render will not fabricate a single-sample draw or emulate a
  resolve. Posted an additive version-3 proposal:
  `sample_counts` + `resolves` parallel arrays, fb sample_count/pattern,
  per-target writeback_msaa, target/resolve retention, exact format/row/size
  checks, and a host test unpacking the arch9 descriptors. Awaiting
  textures/compute ACK and implementation.
- **R4 multiple vertex layouts/instancing is not implemented.** `n2r` rejects
  attributes whose `buffer_index` differs from `attributes[0]`, and the
  primitive descriptor has no instance count. Proposed per-layout vertex spans
  and primitive instance count; awaiting textures/compute ABI.
- No native build, phone, publication or commit.

### Hashes
- render encoder `e007df1c088430bf72b88b110c5d940a076fb0e7210228d101edecf9aa053855`
- encoder host test `8eb325da514f42935bb7440487c9cab9a231dee1537f2d9a9bd979f2100a6e16`
- pipeline `6555af8a35885db6fea64941a709ef9c260b4b79b84c9a8f76823e7dc55a18c8`
- render pass `2d879008866c376144e069b0fee00eea413b6a99790fe487466cee8155466536`
- submit `59ad68ab62d3235b5b95bf82261ff1393185309df4460d7ac507817475b5ff95`
- worker harness `28b425993ed7a16c231a9ba4164a2a65d9d4629bddd4da87dcdaff87cd84cba3`

## DELTA9 — duplicate-target guard + complete inert fallbacks (26 Oct 2026)

### Implemented
- Restored the `nord2_render_pass.inc` guard for the same `TeguTexture` bound
  to more than one colour attachment. It is an explicit inert-pass refusal
  (`same color texture bound to multiple attachments`), before any clear/job.
- Extended the backboardd + `TEGU_INERT_PIPELINES` fallback to the descriptor /
  non-Tegu-function failures and the missing
  `TEGU_GPU_CAP_COMPOSITOR | TEGU_GPU_CAP_GENERIC_JOB` path. These now return a
  nonnil inert pipeline with the bounded `TEGU_INERT_PIPELINE` trace. Allocation
  failure may still return nil. Non-backboardd and flag-off behavior is
  unchanged.
- `renderCommandEncoderWithDescriptor:` now returns a nonnil inert encoder in
  backboardd + flag for an already-committed command or an encoder that is
  already open; it logs bounded `TEGU_RENDER_INERT_ENCODER reason=...`. It
  returns nil for those cases elsewhere and when the flag is off.

### Host evidence
`python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
rc0 both ASan/UBSan fake-JM binaries:
- duplicate colour target: nonnil inert encoder, `TEGU_PASS_REFUSED
  reason=same color texture bound to multiple attachments`, status5;
- missing capability bits with an unsupported descriptor: inert in backboardd
  flag-on; nil/error non-backboardd and flag-off;
- non-Tegu function objects: inert in backboardd flag-on; nil/error elsewhere;
- committed command and already-open encoder: inert encoder in backboardd
  flag-on with `TEGU_RENDER_INERT_ENCODER`; nil elsewhere;
- all DELTA8 tests remain green: path preserve, 128/32 traces, path_blit inert,
  TLS16, authored v4, pass/depth/v3/v2/ASTC. ARM64 default+inert syntax PASS.

### Explicit gaps
- Allocation failure remains a permitted nil; non-Tegu functions have no usable
  function names, so the pipeline trace uses `?`.
- Trace caps remain process-wide (128 pipelines/32 draws/encoders).
- Frontend audit blockers outside render ownership remain (texture view
  selectors, compute pipeline inert, specialization/library nil paths).
- No native build, phone, publication or commit.

### Hashes
- encoder `799feca535e3983dbeb7802f31b469e07ac283cf8cd42984c8a5478d083b368b`
- pipeline `6555af8a35885db6fea64941a709ef9c260b4b79b84c9a8f76823e7dc55a18c8`
- render pass `2d879008866c376144e069b0fee00eea413b6a99790fe487466cee8155466536`
- submit `59ad68ab62d3235b5b95bf82261ff1393185309df4460d7ac507817475b5ff95`
- device limits `626d370475906df5f588a8f147d8a3c83779484c508837ebd71dd78c49864f5c`
- worker test `828d90478e25747538f26826625f1eb0fcc380c59bb93856653e06967b1b6853`

## DELTA8 — backboardd-scoped inert PSO + path preservation (26 Oct 2026)

### Implemented
- `TEGU_INERT_PIPELINES` is runtime-scoped through the new shared
  `compositor_process.h` / `tegu_compositor_process_is_backboardd()` helper
  (compute owns the implementation; render-local wrapper only). With the flag
  compiled in, only backboardd gets non-nil inert PSOs; every other process
  keeps the nil/NSError result. Flag-off behavior is unchanged.
- First-128 bounded `TEGU_INERT_PIPELINE v=<vertex> f=<fragment>
  fmt=<descriptor colour formats> reason=<admission message>` trace; no line
  when inert is not returned. `TEGU_INERT_DRAW_REFUSED` is first-32.
- path_* admission now uses the full-MRT compiler plan written mask. Unwritten
  descriptor-active attachments are preserved: single-output paths switch to
  per-RT v3; contiguous multi-output paths cap the v4 target count; holey masks
  switch to per-RT. Pass clear jobs for preserved targets are removed and
  endEncoding never re-clears them. Non-path mask mismatch remains a refusal.
- path_blit true-MRT + attachment2 fetch has no exact multi-target snapshot:
  admission is explicitly inert in backboardd (and nil elsewhere), never a
  generic draw that later invalidates the command.

### Host evidence
`python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
rc0 both fake-JM binaries under ASan+UBSan:
- real path_cubic with all four descriptor masks active while shader writes only
  RT2: pipeline builds per-RT (`active=4`, `main=2`, `preserve=0xb`), command has
  only the RT2 clear + draw (count2), status4;
- real path_blit true-MRT+fetch: inert in backboardd, exact trace
  `TEGU_INERT_PIPELINE ... fmt=80,80,10 reason=true-MRT framebuffer fetch
  unsupported`; non-backboardd and flag-off return nil/error without inert trace;
- inert unsupported descriptor: 130 bounded creations produce exactly 128
  `TEGU_INERT_PIPELINE` traces; 40 draws produce exactly 32
  `TEGU_INERT_DRAW_REFUSED` traces and no draw jobs;
- prior TLS16, authored true-MRT v4, path_cubic RT2-only/unbound, pass/depth,
  v3/flat/v2/ASTC/CPU tests remain green. ARM64 JM syntax passes default and
  `-DTEGU_INERT_PIPELINES`.

### Explicit gaps
- No exact true-MRT per-target attachment snapshot transport; path_blit stays
  inert/refused. Direct target sampling is not substituted.
- True-MRT Load and subsequent true-MRT draws remain explicit refusals.
- Path preservation is exact for the tested single/contiguous/holey compiler
  written masks; any other path shape remains an explicit refusal.
- Trace cap is process-wide (128 pipelines, 32 draws); later events intentionally
  unrecorded. No hardware/native/phone/publish/commit.

### Hashes
- encoder `5dd90110bf0fa51edfd43b2d32cffa68f8f2cbcdf652045df8b1c24d68a8996a`
- pipeline `eb4eb03141c11e71315f5bfebd1043b009229185fe792683834d284da51631fb`
- submit `59ad68ab62d3235b5b95bf82261ff1393185309df4460d7ac507817475b5ff95`
- render pass `b90f79afc2f3b61f075735bdf122ce141a843a301de714f9ffe9a50cffd9679f`
- device limits `626d370475906df5f588a8f147d8a3c83779484c508837ebd71dd78c49864f5c`
- worker test `8f796a2d63fbc43f9884d176d778800cd5f91829cbe1cbc36d11eda552ef4374`
- authored true-MRT probe `6bacb38ae58a0d98205d83de9bd024626735763fa1581e18ab4b395f877dd0cc`

## DELTA7 — true-MRT v4 wiring + graphics TLS (25 Oct 2026)

### Implemented
- Render pipeline compiles with `TEGU_MALI_GENERIC_OPTION_THREAD_STORAGE` (16)
  through the generic options entry. Default pipelines and selected variants
  record `max(vs->tls,fs->tls)`; TLS bytes travel in the render sidecar stored
  on the generic Dispatch `compositorPlan` blob (no Dispatch/header edit).
- `compositor_submit.inc` passes `n2r_draw_options.thread_storage_bytes`, forces
  v3/v4 when TLS is nonzero, and builds the v4 target sidecar only for the
  single-job true-MRT class. Targets are retained in compositorTextures slots
  27..30 and released with the job.
- True-MRT detection: >1 non-zero bound colour mask with every slot present and
  the MULTI_TARGET options2 full-pair compile. RT2-only/single-output remains
  per-RT v3. The plan's written mask must equal the bound/descriptor mask.
  True-MRT execution uses `n2mb_draw_v4`, per-target token/offset/length,
  format/row/width/height, write mask, blend record, clear words and load
  action. Version2 per-target blends are selected when compute exposes
  `N2MB_RENDER_TARGETS_BLEND_VERSION`. True-MRT Load is refused explicitly;
  only the first CLEAR-class job can use v4.
- Attachment/framebuffer-fetch on the true-MRT path is refused before any job;
  per-RT v3 keeps the existing snapshot path.
- Inert PSO flag and all DELTA5/6 behavior preserved.

### Host evidence
`python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
rc0 both fake-JM binaries under ASan+UBSan:
- real `asg_vert_lph|asg_frag_lph` compiles with TLS=16 (`vertexCode.tls=0`,
  `fragmentCode.tls=16`), carries a sidecar `tls=16`, and commits status4
  through the current bridge/fake JM TLS allocation;
- real `path_blit_vert_lpf|path_blit_frag_lpf` now compiles the full
  three-output pair through MULTI_TARGET and admits true-MRT (active=7), but
  its attachment2 fetch is refused by the bridge before submission (status5,
  three clear jobs, no false draw);
- authored `mrt_vertex|mrt_fragment` writes outputs 0,1,2 without fetch,
  carries target sidecar count3/formats 80,80,10 and version2 blends, and
  commits status4 through real n2mb_draw_v4 + fake JM;
- real path_cubic 4-slot RT2-only per-RT path and RT0/RT1-unbound path remain
  status4;
- inert flag-on/off, pass refusals, depth/stencil, v3/flat, v2, ASTC and CPU
  tests remain green.
- ARM64 JM syntax passes with and without `-DTEGU_INERT_PIPELINES`.

### Explicit blockers/gaps
- The Mali generic compiler is now reached with options2 MULTI_TARGET and
  path_blit's full pair compiles. However path_blit reads attachment2; the
  compute bridge still refuses every true-MRT ATTACHMENT binding (no
  multi-target snapshot transport). That exact draw is refused with a trace.
  An exact per-target snapshot load is required before path_blit can use v4;
  it must not be replaced by direct target sampling.
- Per-target blends: textures' n2r v2 ABI (`N2R_RENDER_TARGETS_BLEND_VERSION=2`,
  `blends` pointer) and compute's matching `n2mb_render_targets.blends` field
  are present. Render carries `TeguAIRRenderBlend blends[4]` in the sidecar,
  copies `genericRtBlends[rt]`, checks the blend write mask equals the target
  mask, and selects v2. The authored three-output v4 probe draws status4 with
  target sidecar count3/formats80,80,10 and three blend records through the
  real bridge.
- True-MRT Load/attachment snapshot remains explicitly refused by compute.
- Raw n2r_build_v2 2-level direct fixture still returns BAD while validation
  accepts; worker sidecar/validation coverage retained, raw builder not claimed.
- No native/device/hardware/publish/commit.

### Hashes
- encoder `f98f003138b50c0f6b7219f1c5773b68f9da003ab730d2e010805d2b8e536385`
- pipeline `f87b40bbf52647a2491b06e60fa6753d21981030ccc912f95fd73f588595b82c`
- submit `59ad68ab62d3235b5b95bf82261ff1393185309df4460d7ac507817475b5ff95`
- render pass `b90f79afc2f3b61f075735bdf122ce141a843a301de714f9ffe9a50cffd9679f`
- device limits `626d370475906df5f588a8f147d8a3c83779484c508837ebd71dd78c49864f5c`
- worker test `a0446fa91f28760a878c3e56819afadecfdc3b2093f9e54340892fdfa78bd74a`
- authored true-MRT probe `6bacb38ae58a0d98205d83de9bd024626735763fa1581e18ab4b395f877dd0cc`

## DELTA6 — generic 4-attachment MRT, R8 RT2, inert PSO flag (25 Oct 2026)

Base inspected: `153025f`. Board contract posted first; compute/textures ACKed
the bounded per-RT `SELECT_RT` emulation (the true-MRT v4 bridge exists but is
not required for this path).

### Implemented
- `render_pipeline.inc` fallback now admits up to four contiguous colour
  attachments, including differing formats; it builds the translator pipeline
  with per-attachment formats/write masks/blends and chooses the first
  non-zero-mask attachment as `genericMainRt`. `genericRtCount/Active/Formats/
  Masks/Blends` are retained on the pipeline.
- Per-target shaders use the existing `SELECT_RT` compiler option. RT1/RT2/RT3
  shaders are pipeline-owned; RT2-only path_* pipelines need only the main
  shader, and RT0/RT1 may be unbound.
- `nord2_render_pass` retains four target pointers, per-target load/clear/job
  state and allows holes; formats may differ while dimensions must match.
  RT2-only passes with RT0/RT1 unbound are accepted.
- `render_encoder.inc` emits one generic job per written attachment, with the
  exact target format/row/load/clear and target-specific blend/write mask.
  Cross-target fetch of a lower-index target is explicitly refused once that
  target has already been written; same-target fetch uses the existing n2r
  attachment snapshot. RT0/RT1 cross-fetch retains the old snapshot path.
- True R8 RT2 target format10 is routed through the bridge's R8 v3 path when
  its target is `10`; `n2r_draw_options` now initializes the additive
  `thread_storage_bytes/reserved` fields to zero.
- Published `maxColorAttachments` is now 4 in render-owned `device_limits.h`
  (arch9 supports up to 8; only the implemented 4-target class is published).
  Root-owned `framework_device_probe.inc` still hardcodes 2 and must be updated
  by root with its assertions.
- `TEGU_INERT_PIPELINES` (default off) returns a non-nil pipeline for still
  unsupported descriptors. It is accepted by `setRenderPipelineState`, and
  each draw emits bounded `TEGU_INERT_DRAW_REFUSED` with no job. Flag-off keeps
  the previous nil/error behavior.

### Host evidence
`python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
rc0, both fake-JM binaries under ASan+UBSan:
- real `path_cubic_vert_0_lpf|path_cubic_frag_lpf` with four colour
  attachments (`BGRA8,BGRA8,R8Unorm,BGRA8`; masks `0,0,15,0`) builds a
  generic-mrt PSO and commits RT2-only status4 with four clear jobs + one draw;
- the same pipeline commits with RT0/RT1 unbound (two jobs: RT2 clear + draw);
- inert sampleCount4 PSO: real flag-on binary returns nonnil/inert and refuses
  the draw without a job; stub flag-off binary still returns nil/error;
- prior delta5 pass/depth and delta4 v3/flat/v2 tests remain green.
- ARM64 JM syntax passes both with and without `-DTEGU_INERT_PIPELINES`;
  bridge syntax passes; `git diff --check` clean.
- `gpu/tests/frontend_jm_test.py` remains blocked by its stale
  `research/nord2-host-air` library and the current planner symbols
  (pre-existing harness path, not this code).

### Formats / transport facts
- `n2r_bpp`/bridge execution remain bounded to 10 (R8), 70/80 (8-bit), 115
  (RGBA16F). The gate's other warmup formats (A8, R16F, RG8, RG16F, sRGB,
  RGB10A2, RG11B10F, BGR10A2, RGBA32F, XR) are still refused by the translator/
  n2r surface; no fabricated format is admitted.
- Textures' `n2r_build_v4` and compute's `n2mb_draw_v4` are available but not
  wired here; compute explicitly documented per-RT v3 as the bounded render
  path. True-MRT load/framebuffer-fetch is still refused by that bridge.

### Explicit gaps
- `path_blit_frag_*` (writes RT0/1/2 and fetches attachment2) fails per-target
  shader preparation in this bounded path; it falls into the existing
  observer/nil policy rather than claiming a false draw. True-MRT v4 + target
  snapshots would be required.
- Four-target per-attachment blend/write-mask draw behavior is not host-tested
  with a multi-output path_blit; path_cubic exercises RT2-only and 4 slots.
- Direct `n2r_build_v2` 2-level fixture currently returns BAD while
  `n2r_validate_v2` accepts it; this is an n2r packer gap surfaced by the
  current sources, so the worker test keeps sidecar/validation coverage but
  does not claim the raw builder.
- No native/device/commit/publish.

### Hashes
- encoder `31f9ddc2daa4fa8683d70d409acc46d4ed43457fdcfd19c3492d1f9c5a9de41e`
- pipeline `cf6a51b22d9efe773b1d0132d0eab173d21cc59e32850bad89faac660c31f9b0`
- render pass `b90f79afc2f3b61f075735bdf122ce141a843a301de714f9ffe9a50cffd9679f`
- compositor submit `f69d10a9056065cde0c61a5ca3f5e4cef4ed5f303f3b859ff09946b7ed7311df`
- ws compat `2820cde852cd0d2ac97dd6e06cb4b78b83d8ddee41052f872fb4b32a862d02b0`
- device limits `626d370475906df5f588a8f147d8a3c83779484c508837ebd71dd78c49864f5c`
- worker test `5831896b2dc501808c1d2d9df08c06b4b4974ccb42f25ba849043b52ae7f9449`

## DELTA5 — C2 chronod nil encoder / depth-stencil pass (25 Oct 2026)

Base inspected: `39e7815` (`Record C2 phase-A hardware qualification`).
Render now owns the C2-mandated `gpu/metal/nord2_render_pass.inc` edit (TASK.md
names it; roles.json predates this delta).

### Implemented
- `nord2_render_pass` never returns nil for a non-committed, non-encoding
  descriptor. Every refusal poisons the command with the failing line and
  returns a `TeguRenderEncoder` flagged `inert`/`skipPass`/`ended`. All encoder
  selectors used by RenderBox are safe no-ops; `endEncoding` clears encoding;
  commit reports status Error. The diagnostic is exactly
  `TEGU_PASS_REFUSED line=<line> reason=<bounded reason>` and is capped at the
  first 16 refusals per process.
- Refused shapes: sampleCount>1, color resolveTexture, >2/mismatched color
  attachments, missing/invalid color0, unsupported target span, nonzero
  renderTargetArrayLength, visibilityResultBuffer.
- Depth/stencil attachments are accepted only when unobservable: valid owned
  format 250..255/260, same size, level/slice/depthPlane0, no resolve, and
  storeAction DontCare. They are not executed and no depth/stencil hardware is
  claimed. A DontCare attachment with no state or a state that is always-pass,
  no-write, and stencil-disabled renders color exactly. Any depth test/write or
  enabled stencil face is refused per draw with
  `TEGU_GENERIC_DRAW_REFUSED rc=-14`; no changed-result job is queued.
- JM pipeline creation accepts depth/stencil descriptor formats
  250..255/260 so QuartzCore's PSO retry cannot abort on a nil pipeline; it
  stores them in `TeguRenderPipeline::depthFormat/stencilFormat`. Non-JM
  behavior remains the old explicit refusal.
- `setDepthStencilState` now retains the immutable state and
  `tegu_render_depth_stencil_exact` gates generic draws. Stencil reference,
  bias, clip and residency selectors remain no-ops for the accepted no-test
  case.

### Host evidence
`python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
passes both fake-JM binaries under ASan+UBSan:
- inert descriptor with sampleCount4 is non-nil, all 14 RenderBox selectors +
  viewport are called, endEncoding is safe, commit status5/error;
- sample4, resolveTexture, mismatched color, depth and stencil non-DontCare
  descriptors are non-nil/error; exactly 16 PASS_REFUSED rows observed with all
  expected reasons;
- DontCare depth+stencil pass with no-test state commits status4 and queues the
  color draw; enabled-stencil pass refuses the draw (count1, status4) with
  rc=-14 trace;
- depthFormat252/stencilFormat253 PSO creation succeeds and stores fields;
- all prior delta4 tests (v3 vertex/base, flat, v2, ASTC, retention, CPU)
  remain green.
- ARM64 JM `-Werror -fsyntax-only` passes (no warning suppression); bridge
  syntax passes; `git diff --check` clean. `gpu/tests/frontend_jm_test.py` still
  fails at link because its `research/nord2-host-air/target/release` library
  path is stale and lacks the current planner symbols (pre-existing test
  harness gap, worker host test uses the current bridge/Mesa libs). No native
  build/device/phone/publish/commit.

### Hashes
- encoder `02243322d79c3fbb27f4c740054236784be0cd8fee1480fe643ec256008f0f33`
- pipeline `c68d9b2be189e9d2e9e6a86c8e3e6955942e57725b270e8a82c1a1b90b58862f`
- ws compat `2820cde852cd0d2ac97dd6e06cb4b78b83d8ddee41052f872fb4b32a862d02b0`
- device limits `626d370475906df5f588a8f147d8a3c83779484c508837ebd71dd78c49864f5c`
- render pass `6075e869bfed5a099fc03e8d2289d11fbc4d320c46df0d07eb4b81c3aef6e2dc`
- worker test `89290dd383ef4a35d1419a69c51a44d29b30d3fd9bc28a294c62abd485a30236`

### Remaining explicit gaps
- No real depth/stencil execution. Depth/stencil store actions other than
  DontCare, resolves, depth test/write, any enabled stencil face, MSAA,
  extra/mismatched attachments and render-target arrays are refused/inert as
  above, never silently dropped.
- The accepted depth/stencil state path is exact only for DontCare storage and
  disabled/always-no-write depth plus both stencil faces disabled.
- No device/native evidence; this is a host/fake-JM contract only.

## Claude delta checkpoint (25 October 2026 evening) — current source

This section supersedes earlier statements that mips/arrays were only refused,
and that the v2 bridge symbol was unavailable.

### Specialized TGJ1 path (render_pipeline.inc)
- `tegu_render_generic_prepare` now stores the selected VS/FS SPIR-V and the
  translator vertex-fetch snapshot in the pipeline, so per-draw variants can be
  compiled after the caller's blobs are gone.
- Draw planning calls
  `tegu_air_render_pipeline_plan_generic_specialized`, then
  `tegu_render_generic_variant_select`.
- A bounded pipeline-owned variant cache compiles through
  `tegu_mali_compile_generic_pair_specialized` /
  `_specialized_rt` / `_specialized_tile_rt` using the planner's independent
  VS/FS `{id,size,data}` tables. It does **not** convert to the unspecialized
  compiler when constants change.
- Variant key includes both module byte strings, runtime target, main-RT, tile
  mode, both stage tables, and the complete 3360-byte specialization sidecar
  including its 512-byte planner key. A host assertion proves key changes for
  both a 512-byte key byte and a per-stage constant byte.
- Variants are immutable and pipeline-owned; a Dispatch stores only a 1-based
  index in `renderArgs[0]`. `compositor_submit.inc` selects that shader for the
  job, so queued nearest/linear/nearest draws don't overwrite
  `pipeline->genericShader`. The job retains the pipeline until completion.
- The default-equivalent nearest variant is asserted byte-equal to the default
  vertex/fragment code, preserving the legacy path bytes.
- Immutable sampler v2 still refuses (exact Metal LOD/normalized/bias/integer
  fields absent); one-level legacy path remains.

### Real v2 transport + resources (render_encoder.inc / compositor_submit.inc)
- `n2mb_draw_v2` is now present in the current bridge; compositor_submit calls
  it directly for v2-required draws with the validated N2T sidecar.
- Sidecar now consumes the selected specialization binding's exact Metal view
  format and compare flag, plus the existing layout/spans. Nonzero view
  level/layer, twin/shader-swizzle flags, and immutable state refuse explicitly.
- `tegu_generic_v2_required` also forces v2 for aliased view formats or
  compare-enabled uses.
- Host test commits a 2-level BGRA8 texture with an exact linear sampler through
  the real `n2mb_draw_v2` and fake JM (status4), and commits queued
  nearest/linear/nearest jobs after all app objects are released.
- ASTC 2D still stops in the translator with an explicit unsupported-format
  refusal, never a neutral or level0 substitution.

### CPU array/cube/MS transfer
- `tegu_texture_cpu_layer` resolves layer-major array/cube/MS-array slices and
  3D depth. `getBytes`/`replaceRegion` now copy every array/cube layer and MS
  array slice; MS transfer is sample0 of the selected layer.
- `gpu/tests/iosurface_cpu_test.py` extracts the real helper; PASS.
- Host test verifies distinct bytes through all 3 layers of a 2D array, all 6
  cube faces, and both slices of a 4xMSAA array.

### Exact commands/results
- `python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
  → **PASS twice** (stub variant-selection binary and real-bridge binary).
  Current libs: compute cdylib `8861e0a0…`, Mesa `57db4be4…`; current
  `air_bridge.h` `eea1de13…` has the synced `view_type/aspect` fields.
- ARM64 production frontend `clang … -Wall -Wextra -Werror -fsyntax-only`
  → **0 diagnostics**.
- `python3 tests/iosurface_cpu_test.py` → **PASS**.
- `git diff --check` → clean.
- Source hashes:
  - render_encoder.inc `6b1277a6c4768b0908495cf037fbd406ea243d217565346e106e1f3f5258a681`
  - render_pipeline.inc `cf3e8d051cb8639fb98404063ca3d4850e8f9979149a18c8203c31cd76dbbf7d`
  - compositor_submit.inc `ecd96c7a37cffa38c19a61c7701090ddeb7780e68546a37eac5c2d6315f6adba`
  - device_limits.h `4427b724082f681afad20f5b68e13e8319a474dcacdf9bf4de2da1fc212ed4d0`
  - iosurface_cpu_test.py `02720695ac60e4bc2d3a5167c637bd79906e606dcce085fc43a46b1456da00b3`
  - host_test_vs_texture.py `ac297664d93d47631e401e0aba0bfc0e0e965540d13c00a04b7b2fc001fdd616`
  - current Mesa `57db4be436005ecb3ed1a9da2b8c2f4fa4c6953529cc1f4d203e39cb02ffbfaf`
  - current compute cdylib `8861e0a07d1dea5f42a61193c574982d0e2c0215b13d6e0a8d0e71a6147234f4`
- No native build, phone, publish or commit.

### Root-patch test diagnosis (25 Oct 2026, post-freeze)
- Root applied both private patches plus an atomic/calloc variant-array lifetime
  fix to production. No render production writes were made after FREEZE.
- Root's `host_test_vs_texture.py` SIGABRT at the synthetic stub-submit assert
  was a **test-harness issue**, not a production defect: the stub binary was
  built from `HEAD` bridge source, but `HEAD` d6c8a82 now contains a real
  `n2mb_draw_v2`. The synthetic `Dispatch` is intentionally minimal and is not
  a valid real bridge request, so the real function returned BAD.
- Fixed only the worker test: the stub binary now compiles a test-only copy of
  the current bridge with the production symbol renamed
  `n2mb_draw_v2_test_unused`; the test stub is the sole `n2mb_draw_v2`.
  `real_bin` links the unmodified current bridge and exercises the actual v2
  path. No production weakness was found.
- Latest test: `python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
  → **PASS both binaries**. No production edits by render after FREEZE.
- Current working-tree production hashes (root patches included):
  render_encoder `0163396c668ecae828927c20c44631987806e0a9b722a8d29250c5e9fa6c29d7`,
  render_pipeline `e3ce170c9690f42c7589b180bb176a836c5f5fcf33a0a9f456b2eacf7f40adfc`.
  Test script `ac297664d93d47631e401e0aba0bfc0e0e965540d13c00a04b7b2fc001fdd616`.
### DELTA4 (25 Oct 2026, options/v3)
- `render_pipeline.inc`: generic default and variant compilation now go through
  `tegu_mali_compile_generic_pair_options` with DRAW_PARAMETERS and
  EXTENDED_VARYINGS (plus SELECT_RT/TILE where applicable), and store the exact
  `tegu_mali_varying_info`, pair flags and vertex sysval mask.
- Variant cache keys include the options flags; module bytes, tables, RT/tile,
  and the planner sidecar remain included.
- `render_encoder.inc`: nonzero vertexStart/native BaseVertex with VertexIndex,
  BaseInstance, flat varyings are carried in genericDrawFlags/base fields.
  CPU index expansion with VertexIndex remains explicit refusal. Flat line
  lists are refused because pinned arch9 has no line DRAW.flat_shading_vertex.
- `compositor_submit.inc`: calls additive `n2mb_draw_v3` weakly when draw flags
  are present, after building the N2T sidecar; null symbol is an explicit
  command error. Immutable one-level samplers can stay on the legacy q word for
  v3-only draws; mip/array immutable/draw-fact combinations still refuse.
- Host test exercises:
  - real authored VS VertexIndex with vertexStart3 through v3 (status4);
  - native indexed VertexIndex with baseVertex5 through v3 (status4);
  - real PointWarpVS/FS flat varying through options + v3 (status4);
  - flat line-list explicit refusal;
  - variant key changes with options flags;
  - existing v2 mips/early-release, magma, ASTC, shadow and CPU tests.
- Host binaries now link the current bridge source and current compiler
  (bridge `06a6c6ee...`, Mesa `742cdf3d...`); no snapshot fallback. Current
  production hashes:
  encoder `a1d201fdba9bf105584878d93c72f71d5b36af0ccccefd4fc433fb6175f44168`,
  pipeline `d92aa559801b873b57044fcff2e5093fb02100eb7cc4f8a85a76b1c7d56fbcf8`,
  submit `cf126f7508dc9423bcdb0d0d879f773bae8ce7792a271efbecc01beec08c6267`,
  test `4dd0b6351bb75fd9a1d8a60274933aebd4151b5f782aa0b8d596dc484198316c`;
  current Mesa `742cdf3d8d10364402ff3f1c7896be43a884e93f95684edf891b206db1c7ffbb`,
  bridge cdylib `06a6c6ee4e45f3b5d2bb4c4a7462d2054f81db5f8f9df564acfb4f1eac158b06`.
- ARM64 `-Werror -fsyntax-only` PASS; host test PASS both binaries; diffcheck
  clean. No native/phone/publish/commit.

### DELTA3 (25 Oct 2026, after UNFREEZE)
- `render_encoder.inc`: fail path now releases `expandIndex`, `expandSource`,
  `expandOut`; the outer indexed-expansion path releases prior references
  before replacing them. This fixes the audit 007 B1 retain leak.
- `render_pipeline.inc`: `tegu_render_generic_set_modules` no longer clears or
  frees `genericVariants`. The fixed 32-slot array is stable for queued jobs;
  old-generation variants remain valid until pipeline dealloc and their module
  bytes cannot match a new generation. Dealloc is the only free point.
- `compositor_submit.inc`: `tegu_shadow_sync_jobs` scans `Dispatch.v2Textures`.
  It refreshes every v2 shadow texture and writes back only textures with
  `MTLTextureUsageShaderWrite` bit1. This is conservative until compute exposes
  an exact per-descriptor write mask.
- `host_test_vs_texture.py`: added a module-reset invariant check (array/count/
  old variant code survive) and current production-bridge test still PASS.
- Textures has posted/ACKed the n2r v3 contract: `n2r_draw_options` FLAT_SHADING/
  WRITE_VERTEX_BASE/WRITE_BASE_INSTANCE, FAU bytes64/68, primitive
  base_vertex_offset0, triangle first-provoking via TILER word2 bit18, line-flat
  explicitly unresolved. Item3 remains blocked on compute-owned `n2mb_draw_v3`
  and per-job vertexBase/baseInstance/drawFlags storage; no fake wiring or
  removal of the vertexStart refusal.
- Added pipeline-local `genericVariantLock` around variant lookup+compile+publish;
  execute readers use acquire loads. Non-JM builds keep the exact-sampler
  refusal (the non-JM job builder cannot carry word0 as exact state).
- Added a host shadow test with a fake IOSurface loader: compute `v2Textures`
  are refreshed, ShaderWrite textures are written back, sampled-only usage1
  textures are not. `host_test_vs_texture.py` PASS both binaries.
- Options contract: textures posted/ACKed n2r v3 and the exact mapping. The
  remaining blockers are compute-owned `n2mb_draw_v3` and per-job
  vertexBase/baseInstance/drawFlags storage; no production options wiring or
  vertexStart removal was attempted without them.
- Current production hashes:
  render_encoder `fd043af63408425ddfc503678955d763f6cab857468e7f833342329ea9116327`,
  render_pipeline `5967424be55209d360d7e787996bf8f483731e70ec6eb35ea023f3ba1f17c2b0`,
  compositor_submit `6cb0b4255739bec99339158dbb1d1c738e893102a4255a494bf23a885f966836`.
  Test script `3f9b64341fc14a3cca94642aedf6a8de86fb3ea4bd17a31c8ccb5fdd467fb598`.
  Textures n2r v3 contract/ACK is now posted; bridge v3 is still compute-owned.
- ARM64 `-Werror -fsyntax-only` PASS; `host_test_vs_texture.py` PASS both
  binaries; `iosurface_cpu_test.py` PASS; diffcheck clean. No native build.

### Freeze status (25 Oct 2026)
- FREEZE L4 was followed by root's UNFREEZE and c1 build from 0aaffe3.
  The flags patch is already in production. No render-native build happened.
- Current DELTA3 changes are host-only and still need root commit/review.
### Remaining explicit gaps after delta
- Immutable sampler v2 exact Metal state is still absent from GenericBinding.
- ASTC is planner-side unsupported by the translator's format feature gate.
- Cube/cube-array/MS sampling still needs specialized-planner admission beyond
  the 2D/mip/2D-array classes; render layout/sidecar/CPU paths are present.
- Selected `view_type`/`aspect` fields are now in the synced header but render
  does not yet validate them; aliased view levels/layers/twin/swizzle refuse.
- Specialized MRT `_rt` and tile-fetch variant paths compile in code but have no
  real frontend host fixture yet.
- Nonzero `vertexStart` + VertexIndex still refuses until first_vertex contract.
- Normal sampler factory enable timing remains compute/device-owned.
- `frontend_jm_test.py` still links the stale research/nord2-host-air lib; use
  the current compute/Mesa libs or rebuild that path.

## Done (production edits, solely owned files)

### `gpu/metal/tegu/render_encoder.inc`
- Added a real **v2 sidecar builder** (`tegu_generic_v2_build_job`) from the
  draw request + retained `TeguTexture`/`TeguSampler`: per-binding full backing span and
  N2T layout (kind, format, dimensions, levels, layers/array length, samples,
  per-level relative offsets, row/slice/layer strides), exact runtime sampler
  state through `tegu_sampler_get_state`, and `n2t_layout_validate` /
  `n2t_sampler_layout_check`. Immutable v2 refuses because GenericBinding still
  carries Vulkan enums and omits exact Metal LOD/normalized/bias/integer state;
  the one-level legacy path remains.
- Generic TGJ1 planning now offers **both stages**:
  - vertex textures `vertexTextures[0..30]` and fragment textures
    `fragmentTextures[0..30]`, with real stage/kind/format/width/height/
    levelCount/sampleCount/usage facts (`tegu_generic_texture_facts`);
  - vertex samplers `vertexSamplers[0..15]` and fragment samplers
    `fragmentSamplers[0..15]`.
  - Resource array capacity was 109 and is now 156 (62 buffers, 62 textures,
    32 samplers), still below the Rust planner's 186 bound.
- Native generic bindings now resolve resources by **`b->stage`**:
  `TEGU_AIR_GENERIC_COMBINED`, `_COMBINED_IMMUTABLE`, `_TEXTURE`, and
  `TEGU_AIR_GENERIC_SAMPLER` work in VS and FS. The old code hardcoded
  fragment-only lookup and sampler stage2.
- Retention for generic jobs is keyed by the **native binding** (0..15), not
  the Metal slot (`tegu_generic_retain_texture/sampler`). Same Metal slot in
  VS and FS maps to distinct native bindings and keeps both objects; a binding
  collision with a different object refuses the draw. Existing compositor/R85
  paths keep their Metal-slot indexing.
- Failed-snapshot cleanup still releases every retained texture/sampler.
- Compiler -> planner binding agreement is a hard check
  (`tegu_generic_compile_matches`). Planner-only extra descriptors are recorded
  as `TEGU_GENERIC_UNREFLECTED b=... kind=...` and skipped: compiler
  reflection proves the generated code does not consume them. Neutral
  buffers/images are still forwarded so n2r_build emits a valid zero/neutral
  descriptor instead of a zero word; the constant blob is copied as pipeline
  data and needs no table entry.
- Neutral-image substitution is no longer silent: if an unsupported texture is
  bound and the planner resolves a texture to `TEGU_AIR_GENERIC_NEUTRAL_IMAGE`,
  the draw is refused with stage/slot/kind/format/levels/samples/usage facts.
- Supported shape classes are offered with real facts; unsupported ones
  (non-exact 3D, memoryless/unknown-format, >256 subresources, etc.) are never
  offered as partial descriptors. They are tracked and trigger the neutral
  check when the planner actually needs them.
- ASTC 2D single-level textures are offered with their real format/block
  layout. The frozen baseline Rust planner currently refuses ASTC facts
  (`texture facts outside bounded read-only 2D/sphere-volume classes`); this is
  an explicit refusal, never a neutral substitution. If compute extends the
  planner's format class, the TGJ1 packer already has ASTC block support.
- `tegu_texture_level0_rows` is used for the ASTC block-row span.
- Generic `-13` refusals (unsupported resource / vertex identity) keep the
  existing command-error semantics: `TEGU_GENERIC_DRAW_REFUSED` records the
  exact cause and `tegu_render_trace_silent` invalidates the command under the
  Nord2 transport. No GPU submit is attempted; this is not a successful
  missing draw.
- Trace text for generic texture bindings is stage-aware.
- `compositor_submit.inc` (root-granted to render) now checks
  `tegu_generic_v2_required` and calls additive `n2mb_draw_v2` with a freshly
  validated sidecar; old `n2mb_draw` remains the zero/default path. The bridge
  symbol is declared weak (`weak_import` on Apple) so the tree links before
  compute adds it; a v2-required draw without the symbol is an explicit command
  error, never a level0 fallback. No Dispatch field or new cleanup is needed.
- Texture allocation/layout facts now include:
  - 2D `usage=1` mip chains with 64-byte rows at every level (existing
    `usage=5`/private mip layouts are byte-unchanged);
  - the R55 R8 64x64 3D class with depth/slice facts;
  - shader-read `usage=1`, `options=0` 2D-array, cube, cube-array, 2D-MS and
    2D-MS-array allocations with layer-major levels, bounded layers/samples,
    and per-level layer/slice strides. CPU region copies remain layer0/slice0
    only and are not claimed as complete array upload support.
- Legacy sampler consumers are guarded: `tegu_sampler_legacy_ok` checks exact
  state against `tegu_sampler_legacy_word_ok` (pre-state objects with only
  `linearFilters` remain compatible) in the sphere observer, R85 fixed-UI
  snapshot, compositor snapshot and TGR5 texture path. Nonlegacy states are
  refused there rather than reduced.
- Sampler facts are no longer synthetic: `tegu_generic_sampler_facts` reads the
  exact retained Metal state (filters, mip, address, border preset,
  normalized, anisotropy, LOD clamps, compare/reduction). The Rust bounded
  planner may refuse states outside its current class; that is an explicit
  command error, not nearest substitution. `TeguSampler` objects without a
  v1 N2T state are refused.

### `gpu/metal/tegu/render_pipeline.inc`
- Added `TeguRenderPipeline.genericVertexIndex`, set from a bounded SPIR-V scan
  for `OpDecorate ... BuiltIn VertexIndex` (decoration 11, value 42).
- `tegu_render_generic_snapshot_rt` refuses (rc -13, logged) a draw when the VS
  reads VertexIndex and either `vertexStart != 0` or CPU index expansion is in
  use. The current TGJ1 request does not expose the Valhall primitive
  base-vertex offset, and rebasing the attribute address does not preserve
  `vertex_id`. Attribute-only shaders retain the existing rebased-address path.
  Native index buffers for vertex-pulling shaders are still allowed because the
  hardware ID is the index value.
- Non-default **function constants** already work through the existing
  `tegu_air_library_specialize`/pipeline-create path. Host probe
  `probe_function_constant.py`: red=0 and red=1 produce different fragment
  SPIR-V (`5320/45c9...` vs `5288/3ff7...`), both create pipelines, and both
  generic plans succeed with no extra runtime specialization. Draw-time
  translator specialization entries that differ from the compiled module
  default remain refused by `air_bridge.rs`; lowering now provides the
  additive specialized compiler API, but the Rust generic plan does not expose
  the per-stage specialization bytes yet, so render cannot call it truthfully.

### `gpu/metal/tegu/device_limits.h`
- Published implemented generic-path vertex resources:
  `maxVertexTextures=8`, `maxVertexSamplers=8`,
  `maxVertexInlineDataSize=4096`; kept the qualified compositor fragment
  envelope (`4/1`) and `maxFragmentInlineDataSize=4096` (setBytes limit).
- Comments now distinguish the 2D single-level general sampled path from the
  exact 1D/3D/MSAA allocation classes. No fragment field asserted by
  `framework_device_probe.inc` changed.

## Host evidence (actual commands, no GPU claim)

Frozen baseline library directory used for the independent probes:
`gpu/build/ui-corpus-20261025/baseline/` (`libtegu_air_bridge.so`,
`libnord2_mali_runtime.so`, profile).

1. **Real installed magma VS/FS pair** (`graphics-01523`,
   `magmaVertexShader` / `magmaForegroundFragmentShader`), baseline bridge:
   - with real resource facts, generic plan rc 0, 7 bindings:
     VS combined immutable at native binding 2; FS combined immutable at
     4/5/6; native namespace generic compile rc 0, 7 bindings
     (VS/FS combined kind5, neutral buffers present).
   - command: `python3 workers/render/probe_magma_plan.py`
2. **Real installed Icon VS/FS pair** (`graphics-01134`,
   `Icon::icon_vertex` / `Icon::icon_fragment`):
   - generic plan rc 0: VS runtime combined at native binding 3
     (stage1 slot7/sampler7), FS combined at binding5; native namespace
     generic compile rc 0, 6 bindings.
   - Its linked VkFormat set is [103,83,97,103]; VkFormat83 is the missing
     native fetch gate (the 29/25/27 values are synthetic Metal descriptor
     inputs, not captured layouts).
   - command: `python3 workers/render/probe_icon_plan.py`
3. **49 real jobs with a VS texture declaration** surveyed:
   22 translate + fetch OK; 12 translate but native fetch refuses formats;
   15 need missing function constants.
   - command: `python3 workers/render/survey_vertex_texture_jobs.py`
   - A second pass with explicit buffer/texture/sampler facts for every declared
     stage slot plans 17/22; 5 refuse `generic plan requires a translator
     variant` (Srgb/YCbCr class), 12 fetch refusals, 15 missing constants.
     Binding counts 4..14, within the 16-native-binding job bound.
   - command: `python3 workers/render/survey_vertex_texture_plans.py`
4. **Real frontend host test** (current production `render_pipeline.inc`/
   `render_encoder.inc`, real baseline AIR bridge, real `n2mb` bridge, fake JM
   kernel). Authored VS+FS AIR uses the SAME Metal slot0 texture/sampler in
   both stages with distinct objects:
   - one generic job, 2 distinct native bindings, VS and FS textures/samplers
     retained in the correct binding slots;
   - app references and the autoreleased encoder are dropped before commit;
     the job still owns the resources through completion;
   - start=3 with a VertexIndex VS produces rc -13, **no** draw job, and a
     command error (the requested draw is not silently omitted);
   - a 2-level VS texture produces a planner refusal (command error, no draw
     job); a 3D texture bound to the authored 2D-texture pipeline is not
     submitted as a neutral draw (its exact sidecar facts are covered by the
     direct 3D test below);
   - ASTC 204 facts reach the planner, which refuses explicitly (rc -6,
     command error); no neutral job is submitted;
   - native indexed vertex-pulling with indices 7,2,9 keeps the original
     uint16 index BO (`index_size=2`, token/offset/6 bytes) and no expanded
     vertex buffer;
   - the real installed `graphics-01523` magma VS/FS AIR is translated and
     compiled through this final frontend, creates a generic job with VS
     texture2 and FS textures0/1/2, and completes a simulated JM commit;
   - direct v2 facts + `n2r_build_v2`: a 16x16 BGRA8 2-level texture with a
     **linear** sampler produces a validated sidecar and a packed descriptor
     with level field1 and array field0; the immutable-v2 path refuses
     explicitly; 3D depth2 facts (slice_stride4096/backing8192), 2D-array
     layers2 (layer_stride1024/backing2048), cube layers6/backing6144 and
     4xMSAA (slice_stride1024/backing4096), cube-array layers12/backing12288
     and 4xMSAA-array layers2/backing8192 validate through the same N2T
     layout checker. Cube/array/MSAA CPU region copies remain slice0/layer0
     only and are not claimed complete.
   - the actual `tegu_generic_draw_submit` v2 branch is exercised with a strong
     test stub for the weak bridge entry: the sidecar is rebuilt from the
     dispatch, reaches the stub with levels=2, and returns a successful reply;
     the real bridge only has to provide the same additive symbol.
   - command:
     `python3 build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py`
   - attribute-only nonzero `vertexStart` (repo `varying.bc`, no VertexIndex):
     the generic job is submitted with `vertex.offset == 96` for start=3 and
     `vertex.bytes == 96`, proving the existing rebased-fetch path still works;
   - result: `PASS real frontend: VS+FS same-slot textures/samplers,
     native-binding retention, vertexStart identity refusal, 2-mip/ASTC
     explicit planner refusal, direct v2 sidecar+n2r_build_v2 facts for
     2 mips/3D/2D-array/cube/MSAA; no GPU claim`
5. **Regression**: earlier in this session `python3 tests/frontend_jm_test.py`
   completed (`PASS frontend format/geometry sequences; transport model only`)
   against the then-current libs. After compute added `tegu_runtime_dispatch_v2`,
   that script's hardcoded `research/nord2-host-air` Rust library is stale and
   the link now fails on `tegu_air_pipeline_native_dispatch_v2`; this is not a
   render regression. The host test above compiles the same render frontend
   against the frozen baseline bridge and passes; the real ARM64 target syntax
   compile also passes. Root must rebuild/point the regression script at the
   current compute library before rerunning it.
6. **Limits readback**: temporary C check prints
   `2/8/8/4096/4/1/4096` and asserts `sizeof(struct tegu_device_limits)==264`.
   **Real ARM64 target syntax compile** of the complete production frontend:
   `clang -fblocks --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall
   -Wextra -Werror -fno-stack-protector -fno-objc-arc -DNORD2_JM_TRANSPORT
   -DTEGU_AIR_BRIDGE -DTEGU_MALI_BRIDGE -DTEGU_METAL_BASE
   -DTEGU_GRAPHICS_BRIDGE -DTEGU_METAL_FEATURE_INIT -DTEGU_METAL_PROTOCOLS
   -include build/tegu-frontend/build-identity.h -Ibuild/tegu-frontend
   -fsyntax-only metal/tegu/compute_frontend.m` — no diagnostics.
7. **Function-constant probe**: `probe_function_constant.py` as above.

## Accepted render selectors / capabilities inventory

- Frontend encoder slots: vertex/fragment buffers `0..30`, vertex/fragment
  textures `0..30`, vertex/fragment samplers `0..15`; index31/16 invalidates
  the command. One encoder per command, one open encoder per pass.
- Render kernel selectors in active paths: 8 import (package versions 1..7),
  9 release, 10 legacy direct draw, 11 vertex, 12 texture, 13/14/17 IOSurface
  import/release, 15 target, 16 compositor/UI1/UI2, 19 TGJ1, 20 TGJ2. The
  active Nord2 JM transport uses `n2mb_*` for generic draws, not raw selectors
  9..20; donor selector numbers are not forwarded as aliases.
- Generic TGJ1 accepted classes in render: single-sample 2D level0 formats
  1/10/30/70/80/115 and ASTC 2D 186-200/204-218; combined/separate texture and
  sampler bindings in both stages; immutable sampler metadata; buffers,
  constant blob, framebuffer-fetch attachment and tile-fetch attachment paths
  unchanged. 3D/cube/array/MSAA/multi-mip/unknown-format/private-memoryless
  sampled textures are explicit refusals.
- Native vertex fetch classes currently accepted by the backend:
  97/100/103/106/109 (the 12 refused real jobs are listed in the gap section).
- Native generic binding bound: 16 descriptor bindings per job; planner
  resource bound 186; render offers at most 156 facts.
- Published limits: color attachments2, vertex attributes4, vertex buffers4,
  vertex textures8, vertex samplers8, vertex inline4096, fragment buffers7,
  fragment textures4, fragment samplers1, fragment inline4096, 2D texture
  4096x4096, exact 1D 4096 / 3D 64x64x64 allocation classes, buffer length
  16 MiB. Fragment values remain the qualified compositor envelope.

## Unresolved gaps / cross-owner requests

- **Vertex fetch formats**: `tegu_air_render_pipeline_vertex_fetch` and
  `gpu/metal/tegu/vertex_fetch.c` only map SPIR-V vertex formats
  97/100/103/106/109. 12 of 49 VS-texture jobs fail at that gate. Exact linked
  VkFormat counts from the saved metadata: 106=10, 103=24, 83=32, 97=22; all
  12 contain VkFormat83 (R16G16_SFLOAT), while 97/103/106 already pass. The
  job descriptors use synthetic tightly packed layouts; Metal values 29/25/27
  are descriptor inputs, not captured runtime layouts. Lowering now owns
  `vertex_fetch.c/.h` and has added the independent half2/format83 encoding;
  compute still must allow format83 in `air_bridge.rs` and textures must wire
  it in n2r. Render cannot fix those.
- **Mips / array / cube / 3D / MSAA reachability**: the common helper, render
  layout facts, sidecar builder, submit hook and direct `n2r_build_v2` are
  implemented and host-tested. The remaining transport dependency is compute's
  additive `n2mb_draw_v2` symbol; until it links, v2-required draws return an
  explicit command error instead of sampling LOD0. No Dispatch field is needed.
- The generic Rust planner still refuses all `mipmap_levels>1` except the
  exact 776x24 blur and requires `array_length==1`, `sample_count==1`; it also
  lacks exact view_format/per-use compare facts. General mips/arrays/MS are
  therefore explicit planner refusals until compute extends that bounded class.
- **ASTC planner class**: baseline `compositor_plan_inputs` does not accept
  ASTC formats (186-200/204-218). The render facts/packer are ready; compute
  must extend the bounded class and the translator format variant.
- **Exact sampler factory enable**: textures added
  `tegu_sampler_enable_exact_factory(1)`; it must be called from device/backend
  initialization before first sampler creation once the v2 bridge and planner
  are linked (compute owns device init). Calling it on first v2 draw is too
  late for already-created samplers. Legacy consumers are guarded now.
- **Runtime sampler specialization**: exact state is now supplied to the
  planner and v2 sidecar. The Rust bounded planner and graphics compiler still
  need the additive specialization API lowering proposed to accept linear/
  mip/repeat variants; otherwise those draws are command errors. No nearest
  projection remains in render.
- **Nonzero vertexStart + VertexIndex**: explicit refusal now; real support
  needs a TGJ1/compiler first_vertex or vertex-offset contract (lowering +
  generic job ABI). Native index buffers for vertex-pulling shaders preserve
  identity and remain allowed.
- **Array/cube/MSAA CPU upload/readback**: allocation and v2 layout facts are
  present, but `getBytes`/`replaceRegion` accept only slice0/layer0 for these
  kinds; full layer/slice CPU transfer remains a separate render-owned gap.
- **Multiple vertex layouts / multiple attribute buffers**: the TGJ1 generic
  job has one vertex fetch buffer and `gd_tables` requires all attributes in
  one buffer. Pipelines with several layouts remain on observer/drop paths.
- **Fragment texture/sampler published limits** stay 4/1 because
  `framework_device_probe.inc` asserts them; the generic path can use more.
  Root can update that probe and the limits together.
- **Translator variants**: 5/22 real fetch-OK VS-texture plans refuse
  `generic plan requires a translator variant` (sRGB/YCbCr class). Supporting
  them needs a bound-format pipeline variant keyed by the actual texture/view,
  compiled at/after the draw like compute variants; the render pipeline is
  currently created before texture formats are known. No silent default
  substitution is made.
- **ASTC / unused unsupported bound texture over-refusal**: the neutral check
  is conservative when an unsupported texture is bound but unused and some
  other descriptor is genuinely neutral. A slot-used query from the planner
  would remove this false positive.

## Files touched
- `gpu/metal/tegu/render_encoder.inc`
- `gpu/metal/tegu/render_pipeline.inc`
- `gpu/metal/tegu/device_limits.h`
- `gpu/metal/tegu/compositor_submit.inc` (root-granted v2 submit hook)

## Test/tool files (worker directory, not staged)
- `probe_magma_plan.py`
- `probe_icon_plan.py`
- `probe_icon_compile.py`
- `survey_vertex_texture_jobs.py`
- `vertex_texture_probe.ll`
- `probe_authored_vs_texture.py`
- `probe_function_constant.py`
- `host_test_vs_texture.py` (generates `vs_texture_host_test.m` + binary)
