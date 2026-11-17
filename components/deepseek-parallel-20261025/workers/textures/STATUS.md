# textures worker status

State: implementation and host tests ready for review; integration gaps listed below.

## Done

- Read HANDOFF/TASK/AGENTS, FRONTEND_SHADER_AUDIT_20261025, all board messages,
  current texture/sampler/render code and pinned Mesa 81daea6
  `pan_texture.c`, `pan_format.c`, `pan_layout.c`, `genxml/v9.xml`/`v10.xml`,
  `panvk_vX_sampler.c`.
- Confirmed active render path is `render_encoder -> n2mb_draw -> n2r_build`;
  the old binding has one 2D level-0 span. The arch11 `tegu_texture_state_build`
  path remains byte-preserved and is marked non-arch9 evidence.
- Added `gpu/metal/nord2_texture_resources.h` (header-only, dependency-free,
  freestanding C11; root granted new shared helper ownership):
  - format oracle for MTL formats 1/10/11/12/13/14/20/22/23/24/25/30-34/40/42/43/
    53-55/60/62-65/70-74/80/81/90-94/103-105/110/112-115/123-125/170/172/174/
    176/178-183/186-200/204-218/222-236/250/252; unknown formats refuse;
  - Metal kinds 0,1,2,3,4,5,6,7,8 (1D/1DArray/2D/2DArray/2DMS/Cube/CubeArray/
    3D/2DMSArray), 1..16 view levels, view base level/layer, cube faces as
    layers, 3D depth/slice stride, MS fused sample plane, array layer stride;
  - per-subresource validation of offsets, row/slice/layer strides, extents,
    overlap, backing span, 2^48 address wrap, 64-byte alignment, view/format
    block geometry and audited view-format aliases;
  - arch9/v10 TEXTURE + Generic Plane/ASTC 2D Plane packers for sampled and
    storage images; level count/LOD, array size, depth, sample count, swizzle,
    texel interleave, layer-outer/level-inner payload order;
  - full sampler packer: filters, mip mode, S/T/R wrap incl border/zero/mirror,
    normalized, per-use compare_enabled + flipped compare, anisotropy, LOD
    min/max/bias, border presets/custom float/custom raw uint, sRGB override,
    seamless cube, magnify cutoff, clamp flags; arch9 reduction average only;
  - explicit stable errors, output unchanged on every refusal, no substitution.
- Added `n2r_build_v2`/`n2r_validate_v2` sidecar in
  `nord2_metal_render.{h,c}`. Old `n2r_build`/`n2r_addresses` and their default
  packet bytes are unchanged; an empty sidecar is byte-identical. A sidecar
  entry is authoritative and never falls back. Texture payload uses the free
  8 KiB command-block tail, max 256 plane entries.
- `nord2_render_resources.inc`: optional larger texture span for v2 only;
  old path still requires the exact historical span. Bindings without a
  sidecar keep exact validation even in a v2 request.
- `sampler_state.inc`: retained exact descriptor state, added opt-in
  `newSamplerStateWithExactDescriptor:` and per-use
  `tegu_sampler_pack_state`/`tegu_sampler_get_state`; legacy constructor keeps
  its old narrow admission so old consumers cannot silently lose fields.
- `texture_state.h`: clarified arch11-only status and pointed to the shared
  arch9 helper; no bytes changed.

## Tests run (host only, no GPU)

All commands from `work/gpu`:

1. Shared helper + Mesa-v10 golden test (v10 pack macros and the production
   `texture_state_mesa_golden.c`), release and ASan/UBSan:
   `clang -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter
   -DUTIL_ARCH_LITTLE_ENDIAN=1 -DUTIL_ARCH_BIG_ENDIAN=0
   -Ibuild/deepseek-parallel-20261025/workers/textures -Imetal -Imetal/tegu
   -Ibuild/tegu-frontend -Ibuild/mesa-build/src/panfrost
   -Ibuild/mesa-src/src/panfrost -Ibuild/mesa-build/src -Ibuild/mesa-src/src
   -Ibuild/mesa-build/include -Ibuild/mesa-src/include
   build/deepseek-parallel-20261025/workers/textures/test_texture_resources.c
   metal/tegu/texture_state_mesa_golden.c -lm -o /tmp/.../test_texture_resources`
   -> `PASS: 806 checks, 0 failures`; same under
   `-fsanitize=address,undefined -fno-sanitize-recover=all`.
2. n2r v2 sidecar test:
   same Mesa include set plus `test_n2r_v2.c metal/nord2_metal_render.c
   metal/tegu/blend_state.c -lm`
   -> `PASS n2r old packet preserved and v2 sidecar authoritative`,
   `PASS n2r v2 mip table levels/lod/plane order`,
   `PASS n2r v2 cube face plane order`,
   `PASS n2r v2 formats 125/ETC2/depth accepted while legacy refuses`,
   `PASS n2r v2 sparse binding positions 3/9 and orphan refusal`,
   `PASS n2r v2 sampler-only baseline equality`,
   `PASS n2r v2 host suite`; same suite under ASan/UBSan.
3. Existing `tests/nord2_metal_render_test.c` + new `nord2_metal_render.c`
   -> both existing PASS lines (format/clear/blend/row-stride and G77
   graphics semantic oracle), proving old path regression parity.
4. Production `nord2_metal_render.c` strict compile:
   `clang -std=c11 -O1 -g -Wall -Wextra -Werror -Imetal -Ibuild/tegu-frontend -c`
   -> OK.
5. Production `nord2_metal_bridge.c` ARM64 freestanding compile
   (`-Wall -Wextra -Werror -Iinclude -Imetal -Ibuild/tegu-frontend`) -> OK.
6. Full `compute_frontend.m` freestanding ARM64 syntax check with the new
   sampler include -> OK (with `-Wno-unused-function` because an unrelated
   render_encoder function is currently unused in that worker's in-progress
   file).
7. Freestanding no-libc link check: `clang -ffreestanding -nostdlib
   -fno-stack-protector -O2` produced a helper object with no undefined
   symbols.
8. Existing `metal/tegu/check_texture_state.py` C portions (arch11):
   `PASS: 12213 checks, 0 failures` twice (release/ASan/UBSan), then the
   script's unrelated final `gpu_texture_image` donor import is missing from
   this workspace. The C test itself passes.

Not run/not claimed: GPU pixels, hardware descriptor acceptance, compare
operand semantics, ASTC/ETC/storage hardware behavior, or full frontend host
harness. No phone, adb, USB, flash, mount, CI, build lease, commit or publish.

## Files

- New: `gpu/metal/nord2_texture_resources.h`
- `gpu/metal/nord2_metal_render.c`
- `gpu/metal/nord2_metal_render.h`
- `gpu/metal/nord2_render_resources.inc`
- `gpu/metal/tegu/sampler_state.inc`
- `gpu/metal/tegu/texture_state.h` (comment/contract clarification only)
- Worker tests/evidence:
  `workers/textures/test_texture_resources.c`,
  `test_n2r_v2.c`, `nord2_texture_resources.h` source copy, this STATUS.

## Integration gaps / unresolved

- `render_encoder.inc` now has a v2 sidecar builder and reports a direct
  n2r_build_v2 PASS for mips/3D/2D-array/cube/MS. `nord2_metal_bridge.c` still
  needs the additive `n2mb_draw_v2` transport (compute-owned); until then the
  v2 API is not reachable from the installed UI. I reviewed the builder and
  asked render/compute to use `swizzle_valid` and the view-format sampler
  info. Current bridge sets `swizzle_valid = swizzle_word != 0`, which still
  cannot represent explicit RRRR (valid zero word), and queries allocation
  format for the sampler; both need correction before integration.
- Render reports allocation/creation support for the above kinds; I have not
  independently verified those TeguTexture facts, and the helper itself does
  not create resources. This remains a cross-owner reachability claim, not
  GPU/pixel evidence.
- Render must set `binding.memory.bytes` to the full span from level0 and
  `n2r_texture_binding.backing_length <= memory.bytes`; bridge must retain the
  whole BO (current `resolve` retains the BO but only validates
  `memory.bytes`). This is required for mips/layers.
- `sampler_state_host_test.inc` (existing harness) still asserts the legacy
  narrow rejection set; it is untouched and should pass because the ordinary
  factory remains narrow by default. The ordinary
  `newSamplerStateWithDescriptor:` now switches to exact-state admission only
  after the v2 submit owner calls `tegu_sampler_enable_exact_factory(1)`;
  until then it refuses states it cannot express. Render already guards the
  old word with `tegu_sampler_legacy_word_ok`; v2 transport is still absent, so
  installed-UI sampler support is not claimed. `newSamplerStateWithExactDescriptor:`
  and `tegu_sampler_pack_state` are compile-checked foundations; they have no
  frontend-harness test yet.
- No hardware verification: compare operand order (Mesa panvk flip),
  clamp-to-border, anisotropy, ASTC/ETC/HDR, depth sampling, storage image
  backends and MS remain unqualified. I added a v2-only HDR admission range
  and the n2r attribute format 83 (RG16F) descriptor word; old n2r still
  rejects HDR and vertex_fetch/air_bridge gates are not mine. Reduction min/max is an explicit arch9
  refusal.
- Explicit refusals: texture buffer (kind 9), PVRTC/BC/YUV, combined
  depth/stencil, stencil-only, A1BGR5 (format 41), XR formats, custom border
  for integer formats without raw words, >256 subresources, >16 levels,
  >2048 layers, memoryless sampled textures, storage images that are
  compressed/depth/sRGB.
- View format aliases are limited to audited same-block/same-order
  reinterpretations (sRGB toggle, signed/unsigned/float same width). The
  planner's `shader_swizzle` must remain in the compiled shader; it is not
  folded into the descriptor to avoid double swizzling.
- `n2t_texture_pack`/`n2t_storage_pack` convenience wrappers use an 8 KiB
  temporary; compute may call `n2t_layout_pack_*` with its own buffer if stack
  is constrained.
- No commits: root owns commits and global NOTES.

Final regression run (production header, release and ASan/UBSan):
- helper `PASS: 806 checks, 0 failures` both modes.
- n2r v2 suite all eight PASS lines both modes, including format125/ETC2/
  depth16 positive with identical legacy refusal, sparse positions 3/9,
  orphan refusal, mips/cube and oversized q.binding_count and sidecar counts (17/UINT32_MAX)
  refusal with output preserved before any embedded-array indexing.
- existing `tests/nord2_metal_render_test.c` both PASS lines.
- ARM64 freestanding render/bridge and full compute_frontend syntax check OK.

## Delta 8 (Claude root, overnight)

- MSAA v3 in n2r:
  - `N2R_RENDER_TARGETS_MSAA_VERSION=3`;
    `sample_counts` (1/4, all equal) and optional `resolves` arrays added at
    the end of `n2r_render_targets`; `struct n2r_resolve_target`.
  - framebuffer sample_count/pattern, rotated-4x Mesa sample LUT, per-RT
    offsets and tile color allocation multiplied by sample count.
  - `writeback_msaa=SINGLE` sample1, `LAYERED` sample4/no resolve,
    `AVERAGE` sample4/resolve; resolve target supplies writeback
    address/format/row/surface stride.
  - Exact preflight: sample count 1/4 and uniform, resolve only with sample4,
    target/resolve format+dimensions+row/span/alignment, no target/resolve
    address aliasing; failures leave output unchanged.
  - v1/v2 packets remain byte-identical.
- T2 formats:
  - `n2r_bpp` admits 25 R16F, 30 RG8, 55 R32F, 65 RG16F, 70/80, 90/94
    10-bit, 110 RGBA16Unorm, 115 RGBA16F, 125 RGBA32F.
  - RT descriptor mapping follows Mesa arch9: 90/94
    R10G10B10A2 (BGR swizzle for94), 65/25 float, 55/125/30/110 raw
    writeback/internal; 10-bit/float/raw clear packing added.
  - Sampling remains through the existing n2t format table; XR 552/553/554/555
    have no exact arch9 mapping and remain explicit `n2r_bpp=0` refusals.
  - Enabled blending on the newly admitted 10-bit/raw T2 targets is an
    explicit refusal; only the disabled/opaque descriptor path is represented.
- Host tests: **15 PASS lines** release + ASan/UBSan, including
  `test_mrt_msaa` and `test_t2_formats`; v9 `FRAMEBUFFER_PARAMETERS`,
  `RGB_RENDER_TARGET`, `MALI_BLEND` unpack and Mesa LUT checks. Old single-target
  semantics PASS; ARM64 n2r and full compute_frontend syntax PASS.
- Evidence: `evidence-delta8-release.log`/`evidence-delta8-asan.log`
  sha256 `9fff6785...`; `evidence-delta8.txt`
  sha256 `3633c95f30668903abd0e2e15f2451fc137d44620c405dd12f15e2fdbb1182bd`.
  Current source hashes above; release+ASan rc0.
- Current committed-tree hashes after C6h tile-buffer sizing:
  n2r C `ebb019f28a017445f55af9e2dcf8a0edde2463ca01f44ef70d4e797610e86934`,
  header `58e1d9fe052a152be57420c11d1af1a468ee0348d07bcdb56553f7b4b35f3381`,
  test `1e365a660fced33850043dacda6c606886741027a0640469e7757be64b62621b`.
  Current tree 15/15 PASS release + ASan; C6h changed `n2r_tib_bytes` and
  sample-location defaults, so the earlier delta8 release log is historical.
- Cross-owner unresolved: compute bridge must add v3 sample_counts/resolves to
  `n2mb_render_targets`, retain resolve BOs, pass sidecar version3; render must
  wire true-MRT MSAA/resolve and T2 attachment admission. No GPU/native claim.

## Delta 7 (Claude root)

- Texture view methods: root handoff confirmed `TeguTexture` lives in
  render-owned `metal/tegu/render_encoder.inc`, not compute. Textures supplied
  `proposals/texture-view.patch`; render applied it (current encoder hash
  `e007df1c...`).
- Implemented `-newTextureViewWithPixelFormat:` and
  `-newTextureViewWithPixelFormat:textureType:levels:slices:`:
  - format aliases use `n2t_view_format_check` (same block geometry/order;
    sRGB/UNORM/SNORM/UINT/SINT same-layout only; BGR/RGB swap, depth, bit-width
    and compressed LDR/HDR mismatches return nil);
  - views are zero-copy: same buffer/transient/imported/shadow backing, copied
    mip/level/slice/layer metadata, same dimensions/usage/options, view format
    substituted;
  - views retain the source object (so imported kernel handle/heap charge
    outlive every view); independent CF retains of imported/shadow surfaces;
    `importedHandleShared` prevents a second kernel-handle release;
  - the ranged method admits only the exact full type/levels/slices case;
    subranges and 3D slice views are explicit nil refusals.
- Proposals and hashes:
  - `proposals/texture-view.patch` base `799feca5...` sha `35456bd8...`
  - `proposals/texture-view-tests.patch` clean-base sha `5d89fd46...`
  - `proposals/texture-view-tests-fix.patch` base `3f321fca...`
    sha `8a981a1e...` (moves test before host split and fixes assert compound
    literals); applied by render, current test hash `8eb325da...`
  - `proposals/texture-view-host-call.patch` base host script `828d9047...`
    sha `c4cbacbb...`; applied by render, current host script `28b42599...`
  - `proposals/texture-view-README.md` sha `68e5fa0c...`
- Host: render applied the test-placement and host-call corrections (current
  test `8eb325da...`, host script `28b42599...`). Ran the full real-frontend
  host suite from `workers/render`: rc0, first line
  `PASS real frontend: ...`; generated source contains and calls
  `test_texture_views`. Log `evidence-texture-view-host.log`
  sha256 `0eeb60009f5a39d3a08bc073571a976d6e660478577229d9d60df82e74ae12c2`.
  The private overlay also separately emitted `VIEW_TEST_PASS` under ASan.
  No hardware/device claim.
- Unresolved: imported/shadow-view retention is code-reviewed but not directly
  exercised in the regular view test; subranges/3D slices and non-exact texture
  types are explicit nil; no GPU validation.

## Delta 6 (Claude root)

- Added additive per-target blend state to n2r v4:
  - `n2r_render_targets` now has an optional `const struct tegu_blend_inputs *blends`.
  - `N2R_RENDER_TARGETS_BLEND_VERSION = 2`; version 1 is unchanged and never
    reads the new pointer, so existing four-field/aggregate callers remain safe.
  - Version 2 requires `blends != NULL` with `count` entries, validates each
    entry with `tegu_blend_build_format(target.format,...)`, and requires the
    blend write mask to equal the target write mask.
  - BLEND descriptor `m+928+16*i` is built from `blends[i]`, so equations,
    factors, enable/load-destination and format conversion are per RT. Version 2
    also stamps `MALI_BLEND.internal.fixed_function.rt=i` (Mesa
    pan_blend_get_internal_desc); version 1 keeps the historical shared
    `shader->blend` plus target write-mask overlay byte-identically.
  - Invalid version2 NULL/mismatched-mask/unsupported-equation shapes refuse
    before output mutation; no fallback to shared state.
- Host `test_mrt_per_target_blends`: 3 targets BGRA/BGRA/R8 with distinct
  source-over, disabled/no-write and opaque R8 states; v9 `MALI_BLEND_unpack`
  checks color masks/load_destination/internal rt/distinct equations, `MALI_BLEND_pack`
  round-trips every descriptor, version1 remains byte-identical even with a
  non-NULL blends pointer, and version2 invalid cases preserve output.
- n2r suite now **13 PASS lines** release + ASan/UBSan; existing single-target
  semantics and 806-check helper remain green. ARM64 n2r syntax PASS.
- Hashes: n2r C `9d787e564a85424bac0ed43d89bb9ba1653230282e3326f484c75a379518e494`,
  header `82f881b9cd7ed4243dc93cbc4845018909a6010f9f401281e5b96ed624652106`,
  test `e838067ac4e2361c138807d9a39f30b7dedec62a3084a899319d058e153108ed`.
- Worker evidence: `evidence-delta6.txt`
  `64e2fb267f848b6df9609efe1decd4491ae548af5891695cc5fdcaa2c39ca3e5`.
- Unresolved: compute must add the same blends pointer to
  `n2mb_render_targets` and pass it through `n2c_mrt_prepare`; render must fill
  its true-MRT sidecar from `genericRtBlends[0..count-1]` and set version2.
  Per-target blends are host-only; true-MRT Load/fetch and hardware acceptance
  remain unqualified.

## Delta 5 (Claude root)

- Added additive graphics THREAD_STORAGE transport:
  - `n2r_draw_options.thread_storage_bytes` = `max(vs->tls,fs->tls)` from the
    compiled generic plan; zero is the historical packet.
  - `n2r_addresses.tls` = resolved, 4096-aligned TLS BO base. It is ignored
    when the byte count is zero (so the bridge can reuse one address struct
    for the LOAD preload, whose shader has no TLS).
  - Added shared sizing helpers `n2r_thread_storage_shift()` and
    `n2r_thread_storage_total()` matching Mesa `pan_get_stack_shift` /
    `pan_get_total_stack_size` for the bridge's allocation.
  - n2r packs one `MALI_LOCAL_STORAGE` at `m+832` with
    `tls_size=pan_get_stack_shift(bytes)` (ceil_log2(ceil(bytes/16))),
    `tls_address_mode=PACKED`, `tls_base_pointer=base>>8`, and
    `wls_instances=NO_WORKGROUP_MEM`; the same descriptor is used by DRAW and
    both SHADER_ENVIRONMENT records (IDVS position/secondary and fragment).
  - Explicit refusals: bytes >32768, nonzero reserved field, missing/non-4096
    base, base>>8 beyond 48 bits. Output is unchanged on refusal.
- Bridge/render wiring is intentionally not edited here: compute owns
  `nord2_metal_bridge.c` BO allocation/retention and render owns the
  `Dispatch` TLS source and v3 selection. Contract posted on the board.
- Host test `test_thread_storage`: TLS sizes 16/32/64/192/32768 unpack through
  pinned v9 `MALI_LOCAL_STORAGE_unpack`, compare exactly against
  `MALI_LOCAL_STORAGE_pack` with the expected Mesa shift/PACKED/base, check
  DRAW + both SHADER_ENVIRONMENT thread-storage pointers, and verify
  shift/total helper values (including align-up 17->32 and invalid/overflow
  cases). TLS0 (including a
  stale nonzero `a.tls`) is byte-identical to the old packet. Invalid shapes
  refuse with output preserved.
- n2r suite now **12 PASS lines** release + ASan/UBSan; existing single-target
  semantic oracle PASS release + ASan; ARM64 n2r syntax PASS.
- Hashes: n2r C `15b4de156c5bdf1c06060b9c1592b7035a084f1e7d1635b56388fc6039e13bc1`,
  header `ef8c9b0ac9cf499782b060c40ca008667c22633967f89552ad4309a52e07dbec`,
  test `2d89a1877664f687cd950955c5b7408bb4db4a0dc54a191254f531b70e207516`.
- Worker evidence: `evidence-delta5.txt`
  `fb0d157d02bb4a18f27d9e08b82830cd8e8c3728a59d54c0bb691da4609199d1`.
- Unresolved: bridge TLS BO allocation/retention and render TLS source are
  cross-owner; no device execution. The 2048-threads/core BO bound is
  conservative (same as compute delta4) and may overallocate; 32 KiB/thread
  can exceed the 64 MiB context budget and must be an explicit bridge refusal.
- Cross-owner compile note: current render `metal/tegu/compositor_submit.inc:285`
  still uses the old four-field `n2r_draw_options` initializer, so a full
  frontend syntax build reports one `-Wmissing-field-initializers` warning
  until render adds the two zero TLS fields. n2r and the bridge compile
  independently; this is not a packet/ABI refusal. Compute also needs the
  bridge-side TLS BO allocation/retention and a `Dispatch` TLS byte source.

## Delta 4 (Claude root)

- Added additive `n2r_render_targets`/`n2r_build_v4` for true 1..4 attachment MRT:
  per-target Metal format (80/70/10/115), load_action, write_mask, resolved
  address, row_bytes, width/height and packed clear words; optional
  sample_locations.
- Implemented RT descriptor array at command+128 (64 bytes each), per-RT
  internal tile offsets/cumulative rounded color allocation, per-RT
  blend descriptors at 928, `render_target_count`, fragment pointer count and
  `DRAW.render_target_mask` (bits for nonzero write masks). R8 uses the existing
  n2r_bpp/clear/writeback path. Sample locations can be relocated for 3..4
  targets; the path is checked for payload overlap.
- Default v1/v2/v3 calls pass NULL and are byte-identical; v4 NULL sidecar was
  compared against the old packet.
- Host test `test_mrt_targets`: 3 targets BGRA8/BGRA8/R8, v9 `pan_unpack`
  checks `render_target_count=3`, color allocation 3072, RT bases/internal
  offsets 0/1024/2048, R8 writeback format, blend_count=3, render_target_mask
  bit2, sample slots at the supplied location; invalid target shape refuses
  before output mutation.
- Current n2r suite 11 PASS lines release+ASan/UBSan. No hardware claim; true
  simultaneous MRT remains unqualified. Render's per-target SELECT_RT
  emulation is still the bounded production path; v4 is available if true MRT
  is selected.

## Delta 3 (Claude root)

- Added additive n2r draw options in `nord2_metal_render.h`:
  `N2R_DRAW_OPTIONS_VERSION`, flags `FLAT_SHADING=1`, `WRITE_VERTEX_BASE=2`,
  `WRITE_BASE_INSTANCE=4`, and `struct n2r_draw_options {version,flags,
  int32 vertex_base,uint32 base_instance}`; `n2r_build_v3`/`n2r_validate_v3`.
  v1/v2 calls pass NULL and are byte-identical.
- Implemented: TILER_CONTEXT.first_provoking_vertex (word2 bit18) from
  FLAT_SHADING; vertex FAU bytes64/68 from VB/BI when requested; per-instance
  ATTRIBUTE offset rebased by BI*stride; MALI_PRIMITIVE.base_vertex_offset
  stays0; vertex FAU word-count guards (17/18). Per-vertex q->vertex is already
  vertexStart-rebased by render and n2r deliberately does not add VB twice.
- Host test `test_draw_options` vs v9 pack oracle: tiler bit, FAU -3/5,
  primitive base offset0, per-vertex attr offset unchanged, per-instance attr
  offset16+5*4; default v1/v2 has no tiler bit/FAU writes and no BI rebase.
- Flat line lists: pinned v9.xml has no `DRAW.flat_shading_vertex` field;
  Mesa pan_jm sets the legacy bit only PAN_ARCH<=7, while arch9 uses the tiler
  bit. n2r does not fabricate a non-existent/overloaded DRAW bit. This remains
  an explicit contract discrepancy and line-flat hardware gap; triangle flat
  uses the arch9 tiler control.
- Current n2r suite 10 PASS lines release+ASan/UBSan; helper 806 unchanged.

## Delta 2 (Claude root)

- Compute mip/layer proposal: `proposals/tdg2-mip-layer.patch`
  - base `gpu/metal/nord2_metal_bridge.c` SHA256
    `ae293c432144be8b5915ca5f2ff8f184d000be446c4f1c56cdfe19bee511da76`
  - private patched copy SHA256
    `e3c0434975c54fabc85ab883602faf93114a1fd70a3096f6b82a52cae910a055`
  - patch SHA256 `c87816797fe04d8a54c5803dd64789b6b3cf5b0668703fc7c51faf74e8f65817`
  - applied cleanly to a pristine copy with `patch -p1`
  - integration_tests harness copied to `tests/n2mb_v2_job_test.c`; private
    patched bridge run: **134 checks, 0 required failures, 0 pending**, same
    under ASan/UBSan. New 3-level 16x16 (nonzero view level origin), 3D depth2
    x2 levels, and 17-layer 2D array cases pass; existing 113-check harness
    remains green. All planes are layer-major/level-minor and arch9 word7 zero.
  - Descriptors/planes and combined/standalone samplers are staged locally and
    copied only after helper success; a refusal does not commit that resource.
  - The patch defines the only representable wire convention: contiguous
    linear mip levels using the single row/slice/layer strides and
    `LEVEL_OFFSET`; `SPAN` is from level_offset. Arbitrary per-level strides or
    non-contiguous offsets still require new wire fields and are an explicit
    gap. 23 planes/descriptor is the helper-checked command-BO bound.
- Arch11 `texture_state` audit: `texture_state.c` is not compiled by
  `tools/build_tegu_frontend_objects.sh`, and `render_client.inc` is not included
  by the active `NORD2_JM_TRANSPORT` frontend. Added an explicit
  `#ifdef NORD2_JM_TRANSPORT` body returning -2 with unchanged output if a
  future build links it. Dedicated `-DNORD2_JM_TRANSPORT` test returns -2 with
  unchanged output (PASS); normal arch11 `texture_state.c` object compile PASS.

## Delta task (Claude root)

- VkFormat83/RG16F n2r attribute descriptor now has an independent v9
  ATTRIBUTE oracle test: `test_vk83_attribute` compares the full descriptor
  words0..4 to `pan_cast_and_pack(ATTRIBUTE)`, checks word5..7 zero and stride4.
  `nord2_metal_render.c` case83 is `(207<<12)|16`, matching
  `vertex_fetch.c:case83` and lowering's `0xcf010`; compute's air_bridge change
  is separate.
- Mip/layer transport helper coverage: 5-level 2D chain, 3 layers x 4 levels,
  cube6 x 3 levels, 3D depth4 x 3 levels, positive 17-layer array, explicit
  level>16, layer>2048, subresource>256 refusals. The shared helper emits layer-major, level-minor
  payloads and validates each per-subresource offset/row/slice/layer span.
  Compute TDG2 currently refuses levels>1 and has a 16-plane wire bound; the
  exact integration call is:
    fill layout.level[0..levels-1] with view-relative offsets/strides/dims;
    n2t_layout_check(&layout, 0, declared_span);
    n2t_layout_pack_sampled/storage(&layout, base, declared_span, desc_va,
      payload_va, payload, capacity, &packed);  // all layers x levels
  or per entry `n2t_layout_subresource` + `n2t_plane_pack`. Do not pass a
  level0-only row*height surrogate. `n2t_layout_check` returns -5 for >16
  levels, -6 for >2048 layers, -19 for >256 subresources.
- Sampler exactness added/refused explicitly:
  - compare_enabled is accepted only with a depth view; color compare returns
    -15. Depth compare packs and is tested; depth border modes refuse.
  - integer views require nearest min/mag, non-linear mip and anisotropy1;
    otherwise -15. Integer custom border requires raw uint32 RGBA words and is
    tested; float presets refuse.
  - custom float border (normalized/float), opaque black/white presets, LOD
    min/max/bias fixed-point, anisotropy/LOD algorithm and wrap modes tested
    against v9/v10 Mesa pack words.
  - gradients are a shader/NIR operation, not an arch9 sampler descriptor
    field; no descriptor claim is made. Shader gradient support remains
    compiler/lowering scope.

## Audit dispositions (frontend_audit a6ee6962 / c31df19d)

- ETC2 RGB8/RGB8A1 (180..183) corrected to 8-byte 4x4 blocks; tests for
  180/182 now compare 64-byte block rows and 512-byte levels.
- `swizzle_word=0` ambiguity removed by explicit `swizzle_valid`. Zero-init
  still means format default; `swizzle_valid=1, swizzle_word=0x000` is a valid
  RRRR descriptor, and bit12/high bits are refused. Tests cover both.
- NULL layout is rejected before every `l->` read in pack/plane wrappers;
  public NULL tests cover sampled, storage, plane, pack and subresource.
- Single-layer 1D/2D/2DMS views with `first_layer>0` and physical
  `array_length` are now accepted; offsets remain view-relative. A view with
  first_layer>0 and no array_length refuses. 3D first_layer remains refused.
- Public sampled/storage pack now requires a nonzero backing address and
  nonzero backing_length; zero-length and 2^48-aligned backing spans refuse.
- Ordinary cube requires exactly six layers and square dimensions; cube array
  requires multiples of six and square dimensions; both tested.
- `first_layer`/`first_level` are documented as physical metadata only.
  `level[].offset`/`layer_stride` are view-relative and are not shifted again;
  a nonzero first_layer test with distinct addresses confirms this.
- `nord2_metal_render.h` now documents `n2r_addresses.binding[i]` as the
  request POSITION, not the sparse native binding number; sparse 3/9 test uses
  distinct BOs and confirms descriptor/payload addresses.
- `n2r_validate_v2` now calls a shared sidecar preflight that validates
  version/count/pointer, duplicates, orphans, request-kind agreement,
  allocation/view shape via `n2t_layout_check`, backing_length <=
  memory.bytes, sampler validity and per-use sampler/layout consistency.
- `gd_request_ok` bypasses the legacy non-ASTC format whitelist only for a
  supplied authoritative sidecar binding; identical legacy requests still
  refuse (tested with formats 125, ETC2 RGB8 and depth16).
- The NUL terminator is a single `\0` escape in the production header; no
  multichar literal remains.
- Shared v2 plane word7 is now zero for both Generic and ASTC 2D planes, per
  pinned arch9 v9.xml. The helper tests use v9 pack macros for all 32 plane
  bytes. The old qualified n2r/arch11 v10 tail is preserved separately: the
  n2r test asserts words0..6 equal, v2 word7 zero, legacy word7 nonzero.
- Final lowerer ACK: table1[N] texture/image, table2[N] sampler, internal
  sampler2[256]. My earlier wiring message's table3 storage sentence is
  superseded and should not be used.

## Current source hashes (SHA256)

- `gpu/metal/nord2_texture_resources.h`: `23642ffd6dfa29229d129d1adbe8044f961143df8fd6d39f1971f3cdaba13457`
- `gpu/metal/nord2_metal_render.c`: `8f553a7ceeb958d4c5751fe1628a668e8868f5187df470ee547cc782c0757e45`
- `gpu/metal/nord2_metal_render.h`: `e6713c5bcf1a901bd1f77eaffce83c8257604aa1ff567dd6df65379ec101a3a6`
- `gpu/metal/nord2_render_resources.inc`: `cabb5fadce467785654245cc2a62bf3932ef48c95e50df1c772889d308a40978`
- `gpu/metal/tegu/sampler_state.inc`: `283980a4d2343c56fe6577b375714b32b133cc13b85a69457dad1339e254f5e8`
- `gpu/metal/tegu/texture_state.c`: `6bb41848609a687069538812cfe4f0270436f3cd4e6b7d74686567e9384025b2`
- `gpu/metal/tegu/texture_state.h`: `153175c3f3f80b91109f3a9a750d7bbbf1abc84ab452c69c20cce7a3c1b5f043`
