# integration_tests STATUS — 25 October 2026 (Claude DELTA 4)

Role: independent integration tests for compute/render resources. Production
read-only; no phone/adb/fastboot/USB/mount, no native build, no publish/commit.
All results below are host-only with a fake JM; **no GPU execution, pixels,
hardware descriptor acceptance or app coverage is claimed**.

## Current result

Command from `work/`:

```bash
python3 gpu/tests/metal_resource_integration_test.py --candidate
```

- **737 checks, 0 required, 0 pending** (release + ASan/UBSan)
- **Candidate release gate PASS**
- Selected receipt: `gpu/build/compute-v2-producer-tls-742/result.json`
  - cdylib `06a6c6ee4e45f3b5d2bb4c4a7462d2054f81db5f8f9df564acfb4f1eac158b06`
  - `air_bridge.rs` `277acb12702d16acb61622337e037f983e412abd06d622f5a7a2673fa925f2d1`
  - compiler `742cdf3d8d10364402ff3f1c7896be43a884e93f95684edf891b206db1c7ffbb`
- Current source hashes in the passing run:
  - `nord2_metal_bridge.c` `bec51a8236d119e14d24b23e768fb4f7cf4e4ab18c236a9d20ba2c61bf567cf7`
  - `nord2_metal_render.c` `a339eae1bff028c98185cde57ba8f96d321198c6023e07574327e3fc57cb3551`
  - `pipeline_format.h` `ff2889fc04deeee17ed23bbfff79910e7b2101cecae5e9cd3640b2716d4eb079`
  - metal manifest `a5ca08ad7c8d7d6b775135c94f2055df41b869445f0ba900ddc57371746b3f6a`
- The PASS includes root's commit `50cfaee` (`n2r_validate_impl` rejects
  `FLAT_SHADING` with `TEGU_GENERIC_DRAW_LINES`); `nord2_metal_render.c`
  `a339eae1…` is now committed. Earlier HEAD `714370c` alone lacks that guard
  and correctly reports the flat-line gate as a required failure.

Report/logs:
- `workers/integration_tests/report.json`
- `workers/integration_tests/runs/report-delta4-full.json`
- `workers/integration_tests/runs/delta4_full_final3.log`

## Delta 4 gates

### 1. ABI oracle and per-level facts
`abi_oracle.py` derives every size/offset from the actual Rust `#[repr(C)]`
declarations and checks them against a C probe compiled against the production
header. The suite no longer pins a handwritten size table. It records resource
size **488** and derives four per-level arrays from the Rust field names:
`level_offset`, `level_row_stride`, `level_slice_stride`, `level_layer_stride`,
each 16 entries, matching Rust `TEGU_AIR_COMPUTE_MAX_LEVELS=16`. A future
resource layout change must continue to satisfy the generated Rust/C check; no
new magic size is hardcoded to make the gate pass.

### 2. TLS/WLS through the real bridge (independent of compute's own test)
`n2mb_v2_job_test.c` drives the production bridge selectors 5/7 and a fake JM:
- TLS-only 64 B: v9 `MALI_LOCAL_STORAGE` unpack gives `tls_size=2`, no-WLS
  marker `0x80000000`, zero WLS fields, TLS base/address-mode set;
- WLS-only 256 B: `wls_instances=1`, `wls_size_base=0`, `wls_size_scale=9`,
  nonzero WLS base;
- mixed 64/256: both TLS and WLS fields valid;
- oversized word17/word18 (`32769`) is refused by the production parser before
  import/submit;
- TLS/WLS handles are present in the submitted handle list and are still alive
  at `NORD2_GPU_SEL_WAIT`; after success they are released;
- a forced command-allocation failure after TLS/WLS allocation returns an
  error, submits nothing, and releases both local BOs.
- D8F18H separately checks that all four appended handles (TLS plus three real
  resource BOs) are alive at wait.

### 3. draw v3 through the real bridge
`n2mb_draw_v3` with `n2r_draw_options` verifies:
- `vertexStart/baseVertex=3` and `baseInstance=2` land in the FAU words at
  command offset `1664+64`/`1664+68`;
- signed `vertexStart/baseVertex=-3`, `baseInstance=5` produce
  `0xfffffffd`/`5`;
- flat triangle sets v9 `TILER_CONTEXT.first_provoking_vertex=1`;
- flat plus line topology is refused (the current render working tree adds the
  required guard; without it the test fails rather than silently accepting).

### 4. D8F18H real packet, TLS 32 B
The lowering `final-742cdf3d` evidence SPIR-V
`build/lowering-claude-20261025/d8f-spot/wls-on/D8F18H-linear1/compute.spv`
(`791bd8cc…`) is recompiled with compiler `742cdf3d` and specialization ID 0
(32-bit value 1), producing the byte-identical TGP2 packet
`a179cc460ca457174dbb72ac6f6e29c474470b486331eaef77a38e18b8d2d6df`
(9920 B, TLS=32, WLS=0). It is embedded as `d8f18h_pkg.h` and its provenance is
checked by the suite against `d8f18h_pkg.json`; regenerate with
`regenerate_d8f18h_packet.py`. The real packet (5 manifest records: UBO,
declared-combined texture/sampler, dummy sampler, storage image) is imported
through the production bridge and dispatched through the fake JM with three
wire descriptors. v9 unpack confirms `tls_size=1` (32 bytes), TLS base/address
mode, no-WLS marker; the TLS BO is held through submit/wait and released after.

### Other retained gates
- Native-macro TDG1/TDG2 frontend, retain/no-alias, shadow refresh.
- VkFormat83 vertex fetch, corpus graphics-00086/00498/01278 no SIGSEGV/abort.
- Render nearest/linear variant cache and ASan two-thread growth/read.
- Legacy sampler exactness/refusal.
- Real Rust planner/exact-thread regions, 59/59 producer checks.

## Limitations / unresolved

- No GPU, pixel, hardware descriptor, TLB/page-fault or app test.
- Fake JM accepts submissions; BO lifetime checks prove handle timing, not
  hardware memory retention.
- The D8F18H dispatch uses a locally reconstructed wire for its real manifest;
  it does not prove the app/frontend would supply those exact descriptor
  values at runtime.
- The render flat-line guard is committed as `50cfaee`; no hardware flat-line
  semantics are claimed, only explicit refusal.
- TLS BO sizing uses bridge's conservative 2048 threads/core; no device proof
  that this is sufficient for every G77 configuration.
- Shadow test uses a mock IOSurface; no hardware coherence claim.
- TSan is not used; the two-thread test is ASan-instrumented.
- Compute mips >1, >16 layers, atomic transport, exact sampler parity and
  render specialized compiler execution remain cross-owner gaps.
- No phone/commit/native build/publish.

## DELTA5 FINAL (26 October 2026)

Production read-only. No phone/adb/fastboot/USB, native build, commit or publish.

Command from `work/`:

```bash
python3 gpu/tests/metal_resource_integration_test.py --candidate
```

**935 checks, 0 required, 0 pending** (release + ASan/UBSan); candidate **PASS**.
Matched source/binary:
- cdylib `9ff66cacdd22775dc1ea121f663537df8e3731cff4453c2fd68a74d10673b4b9`
- `air_bridge.rs` `d8854273a6da75a42fc3f2a720b59456779a89fc156fb9140cb9f21afdc8b6b0`
- compiler `1877804bdbc5f84d5ae5f92cb3783404b3dca1118c90980994ff5aaa4ccfb374`
- `pipeline_format.h` `ff2889fc04deeee17ed23bbfff79910e7b2101cecae5e9cd3640b2716d4eb079`
- bridge `494a027cd7d979b53ed02205b5a5e7c2ec7c9de5387490b9845e7260a0abe4f0`
- bridge.h `8f385cb6de6368c0a8acca27a002056a75c4ff871aa65e1cfe7af9917a1b33b3`
- render `9d787e564a85424bac0ed43d89bb9ba1653230282e3326f484c75a379518e494`
- render.h `82f881b9cd7ed4243dc93cbc4845018909a6010f9f401281e5b96ed624652106`
- render_pipeline.inc `eb4eb03141c11e71315f5bfebd1043b009229185fe792683834d284da51631fb`
- render_encoder.inc `5dd90110bf0fa51edfd43b2d32cffa68f8f2cbcdf652045df8b1c24d68a8996a`
- compositor_submit.inc `59ad68ab62d3235b5b95bf82261ff1393185309df4460d7ac507817475b5ff95`
- compute_frontend.m `9d12b0f8acc4eb3656c839bf41b67b2018588751de7f4e57d5f5e21059fac80b`
- render_pass.inc `b90f79afc2f3b61f075735bdf122ce141a843a301de714f9ffe9a50cffd9679f`
- candidate receipt `build/compute-v2-producer-current-abi/result.json` uses
  compiler `ad62ed9e…`; the newer current `1877804b…` is accepted with an
  explicit warning, not hidden.
- metal manifest `50cd936c4f32bc394aebe97b84effffe4cf0035b9126c72e1dc07c6e81dae771`
  (370 files).

Report/logs:
- `workers/integration_tests/report.json`
- `workers/integration_tests/runs/report-delta5-full.json`
- `workers/integration_tests/runs/delta5_full_final2.log`

### Independent tests added

1. **4-target true-MRT via real bridge/fake JM** (`n2mb_draw_v2_test.c`,
   82 checks): targets `[BGRA8,BGRA8,R8Unorm,BGRA8]`, masks `[15,3,8,15]`,
   v2 per-target blends. v9 unpack verifies `render_target_count=4`, target
   writeback formats (R8 vs RGBA8), base/row/clear, `DRAW.blend_count=4`,
   `render_target_mask=0xf`, four BLEND descriptors with distinct masks and
   `internal.fixed_function.rt=i`; target0 source-over loads destination while
   target3 opaque does not; all four target BOs are write-retained. V2 NULL
   and mismatched-blend arrays refuse before submit with output unchanged.
2. **Graphics TLS** (`n2mb_draw_v2_test.c`): TLS32 through `n2mb_draw_v3`;
   v9 `LOCAL_STORAGE` `tls_size=1`, packed base/address; IDVS position,
   IDVS varying and fragment `thread_storage` all equal command+832; TLS BO
   size equals `n2r_thread_storage_total(32,2048,16)`, read-only retained,
   alive at wait, released after; 32769 bytes refuses before submit.
3. **Independent C3 inert pass safety** (`inert_render_pass_test.m`): compiles
   the real `nord2_render_pass.inc` against a minimal fake ObjC frontend, not
   render's harness. Missing colors, sampleCount4, resolve, >4 attachments,
   unsupported format and repeated refusals return non-nil
   inert/skipPass/ended encoders, poison the command, and emit exactly 16
   bounded `TEGU_PASS_REFUSED` lines. Release+ASan each 50 checks, 0 required.
4. **Render contract update**: `render_cache_regression` now checks the
   DELTA8 contract: path-preserve no-clear, `PATH_BLIT_PIPE` admitted
   (`true=1`) then inert only in backboardd, `PATH_BLIT_NONBBD` nil/error,
   exact path_blit trace, 128 `TEGU_INERT_PIPELINE`, 32
   `TEGU_INERT_DRAW_REFUSED`, authored v4 status4 and TLS16 status4. The old
   path_blit status4 expectation is removed; the attachment-fetch gap remains
   explicit, not waived.

Test counts: TGP2 156, complete job 166, draw/MRT/TLS 82, VkFormat83 13,
independent inert pass 100 (release+ASan), plus producer 59 and retained
native/frontend/corpus/render/variant gates.

### Limitations / unresolved

- No GPU/pixel/hardware descriptor/IOMMU/app/device test; fake JM only.
- Four-target MRT and per-target blend/write-mask behavior are host-tested
  through the real bridge and v9 oracle, not hardware-tested.
- Graphics TLS is transport/LOCAL_STORAGE tested; thread-storage workspace use
  by actual shaders and hardware TLB behavior are not proven.
- `path_blit` still refuses its true-MRT attachment2 fetch; it becomes a
  non-nil inert PSO in backboardd and nil/error elsewhere. No false draw is
  claimed. A per-target snapshot transport is required for the real path.
- Runtime-scoped inert behavior is host-tested with the backboardd seam; no
  daemon/device runtime test has been performed.
- The C3 harness uses minimal fake ObjC objects; it tests the production pass
  admission/inert contract, not the full render encoder selector surface.
- Current compiler `1877804b` is newer than the selected compute receipt
  `ad62ed9e`; candidate passes with a documented warning. A later compiler
  change requires a fresh receipt/suite run.
- Shadow IOSurface remains mock; TSan not used; compute mips >1, >16 layers,
  atomics, exact sampler parity and render specialized compiler execution
  remain cross-owner gaps.
- No phone/commit/native build/publish; root owns integration and commits.


## DELTA6 progress (26 October 2026)

Production read-only. No phone/native/commit/publish.

### Implemented host coverage (integration-owned)
1. **Real QuartzCore default.metallib**: `native_compute_frontend_test.py` now
   loads the local `gpu/build/native/QuartzCore.metallib` (exact 1,014,944 B,
   path only, no copy) through `newLibraryWithURL:error:`, looks up
   `path_cubic_frag_lpf`, and creates a CA-shaped
   `fixed_vert_lph_spc` descriptor with four named constants
   (`fc_generic` bool, `fc_vertex_layout`, `fc_texcoord_count0/1` uchar).
   The specialized alias and source identity are checked. Local audit output:
   `QUARTZCORE_LIBRARY_OK size=1014944 entry=fixed_vert_lph_spc constants=4`.
2. **Selector audit**: `respondsToSelector:` on real device/buffer/texture/
   command/compute-encoder/render-encoder objects and
   `instancesRespondToSelector:` for class-only selectors. Covers the 14
   RenderBox encoder selectors and the audit's device/texture/buffer/command/
   encoder list. Missing selectors print `SELECTOR_MISSING` and fail the native
   frontend gate; no waiver.
   Current audit (using a local-only link stub while the producer cdylib is
   stale): 95 checks, 6 missing rows after duplicate instance/class checks:
   - `TeguComputeDevice newBufferWithBytesNoCopy:length:options:deallocator:`
   - `TeguBuffer newLinearTextureWithDescriptor:offset:bytesPerRow:bytesPerImage:`
   - `TeguComputeDevice newResourceGroupFromResources:count:`
   - `TeguComputeDevice encodeCacheHint:`
   `newTextureViewWithPixelFormat:` and
   `setThreadgroupMemoryLength:atIndex:` are now present in the current tree.
3. **Same-texture-twice pass refusal**: independent C3 harness adds two
   color attachments naming the same `TeguTexture`; expects non-nil
   inert/skipPass/ended, command poison, and the exact
   `same color texture bound to multiple attachments` trace. Current render
   source has the guard; C3 release+ASan now **104 checks, 0 required**
   (52 each), including the reason check.

### Blocker
Current `air_bridge.rs`/`runtime_pipeline.inc` was changed to call the new
`tegu_air_pipeline_native_dispatch_(v2_)tg` planner entry points, but the host
cdylib in `workers/compute/cargo-target/release` is still `9ff66cac…` and lacks
both symbols. The integration native harness cannot link without the rebuilt
cdylib/receipt. A local-only stub was used to exercise the library/selector
audit above; it is not part of the suite, not a production waiver, and does not
claim the missing planner symbols exist. Compute/root is rebuilding and adding
the four missing selectors; rerun full + --candidate after their posted hashes.

Current source at audit time: bridge `494a027c…`, render `9d787e56…`,
render_pipeline `eb4eb031…`, render_encoder `5dd90110…`,
compute_frontend `f65d2501…`, air_bridge `59c796a4…`, cdylib `9ff66cac…`.
No hardware/device claim.


## DELTA7 thermal-resume checkpoint (26 October 2026)

Base commit `22e7174`. Production read-only. No phone/native/commit/publish.

### Completed evidence
- Rebuilt producer cdylib `3f2d275a97fc45a25468d77eb4513a71d71a8ab7ab75b323f2c84a4e7463dd15`
  with `tegu_air_pipeline_native_dispatch_tg` and `_v2_tg`; current
  `air_bridge.rs` `59c796a4…`, compiler `1877804b…`.
- Native QuartzCore/selector gate against that cdylib **without stubs**:
  - `QUARTZCORE_LIBRARY_OK size=1014944 entry=fixed_vert_lph_spc constants=4`
  - `SELECTOR_AUDIT checks=95 missing=0` (no SELECTOR_MISSING lines).
  - Real library path is `gpu/build/native/QuartzCore.metallib`, SHA
    `11c74cc4…`, never copied.
- C3 same-texture guard remains independently tested: release+ASan **104
  checks, 0 required** (52 each), including exact duplicate-texture reason.
- Draw/MRT/TLS suite 82 checks, 0 required.

### Attempted full run (not final)
`python3 gpu/tests/metal_resource_integration_test.py --candidate` was run
once against cdylib `3f2d275a`/manifest `5975fe3894a7d8ce`: **939 checks,
2 required**, candidate PASS. The two failures were:
1. My native gate string expected `SELECTOR_AUDIT missing=0`; output is
   `SELECTOR_AUDIT checks=95 missing=0`. Fixed in the test; no production
   change, no check relaxed.
2. Render-owned harness aborts at generated
   `vs_texture_host_test.m:3332` in `test_blit_completeness` with
   `assert(dp[i]==bp[i])` (ASan process -6), after current render/texture-view
   changes. Current render harness `f7059247…`; production encoder `e9c94841…`,
   pipeline `6555af8a…`, render_pass `2d879008…`. This is not a path_blit or
   selector assertion; render must fix/post a final hash.

**Important:** this run overlapped compute's HEAVY slot after root posted
`HEAVY conflict`; it is retained only as debugging evidence, not as the final
thermal-compliant result. The final single full release+sanitizer run will be
rerun after `HEAVY RELEASE compute` and a render harness fix, with a fresh
HEAVY TAKE/RELEASE.

No heavy jobs are running from integration_tests now. Waiting on board (at
most every 2 minutes), read-only work only.

## DELTA7 final checkpoint (thermal-limited, 26 October 2026)

Base commit `22e7174`; production read-only. **No integration heavy job is
running.** Root asked integration_tests to stop reruns; render currently holds
the one heavy slot for its blit host fix.

### Passed evidence
- Host cdylib `3f2d275a97fc45a25468d77eb4513a71d71a8ab7ab75b323f2c84a4e7463dd15`
  exports both `tegu_air_pipeline_native_dispatch_tg` and `_v2_tg`;
  `air_bridge.rs` `59c796a4…`, compute frontend `3e918bee…`.
- Native QuartzCore/selector gate **without stubs**:
  - real local `gpu/build/native/QuartzCore.metallib` (1,014,944 B, SHA
    `11c74cc4…`, path only)
  - `QUARTZCORE_LIBRARY_OK entry=fixed_vert_lph_spc constants=4`
  - `SELECTOR_AUDIT checks=95 missing=0`; no SELECTOR_MISSING list.
- Draw/MRT/TLS **82 checks, 0 required**.
- Independent C3 pass safety **104 checks, 0 required** (52 release +52 ASan),
  including same-texture-twice reason.
- Candidate provenance **PASS** in the latest full run:
  manifest `eede601a96b8411f…`, cdylib `3f2d275a…`, metal files370.
- Latest full run: **939 checks, 1 required, 0 pending**. The sole required
  failure is `render specialized variant cache`; all other gates pass.

### Remaining external blocker
Render-owned `test_blit_completeness`/v2 layout test currently aborts in the
generated harness:
- after the out/buf scope fix, next failure is `assert(arr)` for
  `new_sample_ex(d,80,16,16,1,3,1,2,1)` with
  `TEGU_TEXTURE_REFUSED ... format=70 ... type=3 ... arrays=2`.
- This is a render test/texture admission mismatch, not an integration
  assertion. Render took the heavy slot at 02:34 and is fixing/testing it.
- The previous full run is retained as `runs/delta7_full_current.log` and
  `runs/report-delta7-current.json`; it is **not** the final green run.

### Test-owned edits in this delta
- `native_compute_frontend_test.py`: real default.metallib URL load + CA
  function descriptor constants + exhaustive selector audit; main gate now
  accepts `SELECTOR_AUDIT checks=… missing=0`.
- `inert_render_pass_test.m`: same-texture-two-slot refusal case; 52 checks
  each mode.
- `metal_resource_integration_test.py`: native selector gate/timing and the
  current render contract checks; no production edits.

No phone, native build, commit or publication. Awaiting render final hash and
root’s single final rerun authorization.

## DELTA8 FINAL GREEN (thermal-limited, 26 October 2026)

Base commit `3415864`. Production read-only. One thermal-compliant heavy run:
release + one sanitizer pass; no rerun loop; no phone/native/commit/publish.

Command from `work/` (taskset CPUs 0,1, nice):

```bash
python3 gpu/tests/metal_resource_integration_test.py --candidate
```

### Result: 939 checks, 0 required, 0 pending — PASS
Candidate gate **PASS**: metal manifest
`6f0729dbb5bf5e0dfe17932ab8866480cc380ebf27d31eb9c91f1404aac78932`
(370 files), selected receipt producer cdylib
`3f2d275a97fc45a25468d77eb4513a71d71a8ab7ab75b323f2c84a4e7463dd15`,
air source `59c796a4…`, compiler `1877804b…`.

QuartzCore/native gate:
- `QUARTZCORE_LIBRARY_OK size=1014944 entry=fixed_vert_lph_spc constants=4`
- `SELECTOR_AUDIT checks=95 missing=0`; no missing selectors.
- Real local `gpu/build/native/QuartzCore.metallib` was read by path only
  (SHA `11c74cc4…`), not copied.

Other green counts:
- TGP2 156; complete job 166; draw/MRT/TLS 82; VkFormat83 13;
  independent C3 inert pass **104** (52 release +52 ASan, including
  same-texture-twice refusal); producer 59/59; render harness and all
  native/frontend/corpus/legacy/variant gates PASS.

Current source hashes recorded by the run:
- bridge `494a027cd7d979b53ed02205b5a5e7c2ec7c9de5387490b9845e7260a0abe4f0`
- render `4042ff433df8fa5f8277a77eabe0641465265400a15947763b52daf1b4bddfd8`
- render pipeline `6555af8a35885db6fea64941a709ef9c260b4b79b84c9a8f76823e7dc55a18c8`
- render encoder `e9c948416eeddcc1b376eefc54a6e3f989bc238fade5eb9aee082a369ddf347f`
- blit encoder `6fca3a6ab76f07cbc390f3940d12b149bf11511ab612d042dc3762eefeee1db5`
- render encoder host test `507cf260bc1233973b40e3a76277d0ca01561fe3f8975778561cbf387c7e13fc`
- render pass `2d879008866c376144e069b0fee00eea413b6a99790fe487466cee8155466536`
- compute frontend `3e918bee82e12053b5ba34a1c57543682c765f2f8275d521255ded4c5909bcc3`
- air_bridge.rs `59c796a4c6cb761d89d80ec3161e9119ec5d0494db32146e11dc65e886dd4d36`
- pipeline_format `ff2889fc04deeee17ed23bbfff79910e7b2101cecae5e9cd3640b2716d4eb079`

Artifacts:
- `workers/integration_tests/report.json`
- `workers/integration_tests/runs/report-delta8-full.json`
  (SHA `03ea4b16a6cd8b43ec11d7096e3f10ef3f92caef1f825a67d50ca562dba5f88e`)
- `workers/integration_tests/runs/delta8_full_once.log`
  (SHA `3290f623471eca3fc22854e1876aea2fd2ff1979f4948778d9a52d191122ebdd`)

### Explicit residual gaps
- Host-only; no GPU/pixels/hardware descriptor/IOMMU/app coverage.
- MRT/per-target blends/TLS are transport and v9 oracle tests, not hardware.
- path_blit true-MRT attachment2 fetch remains unsupported; explicit inert
  backboardd behavior, no false draw.
- Runtime inert scope is a host seam, not device/daemon execution.
- C3 harness uses minimal fake ObjC objects; full render harness covers
  selectors and real frontend paths.
- Compute mips >1, >16 layers, atomics, exact sampler parity and render
  specialized compiler execution remain cross-owner gaps.
- No phone/commit/native build/publish.

## DELTA9 extraction / metallib audit (26 October 2026)

Base commit `4f3ad8e` (C5 build); production read-only. One heavy run under the
thermal slot; no loop reruns. No phone/native/commit/publish.

### Implemented audit inputs
- `delta9_extract_selectors.py` reads, never copies, the local iOS 16.7 dyld
  cache under `gpu/build/frontend-audit-ui-c2-20261025/dsc` and Metal SDK
  headers. It extracts QuartzCore/RenderBox `__objc_selrefs`, maps selectors
  declared by Metal protocols to Tegu classes, and emits:
  - `delta9_selectors.inc` (132 mapped cases, 5 proven-unreachable fence/event
    cases, source names only)
  - `delta9_selectors.json` (provenance/counts; 2,551 unclassified CA/RB
    selectors not present in the Metal headers; not silently claimed as Metal)
  - `delta9_metallib_functions.json`
- `delta9_frontend_audit.inc` adds extracted selector checks
  (`respondsToSelector:`/`instancesRespondToSelector:`) and enumerates **all
  141 functions** in the real local `default.metallib`.

### Function audit result
`tegu_air_library_open` over the real 1,014,944-byte default.metallib found
**141** functions: **135 specialize with the available empty/default constant
set**, **6 nil**:
- `fixed_vert_lph_spc`, `fixed_vert_lph_gen`, `fixed_vert_lpf_spc`,
  `fixed_vert_lpf_gen` — missing required `fc_generic` / `fc_base_color`
- `fixed_frag_lph_cph`, `fixed_frag_lph_cpf` — missing required
  `fc_attachment_count`
Compute `compute_apl`, `compute_apl_fast`, and `sum_apl_and_compute_dimming_factor`
were included in the 135 specialized functions. Nil cases are recorded with
exact reasons; no synthetic defaults were invented.

### Blocked selector run
The single full `--candidate` run completed with candidate PASS (manifest
`6d83446e2db357de…`, cdylib `3f2d275a…`) but **2 required failures**, both from
the same production compile error:
`metal/tegu/render_pipeline.inc:1125` uses undeclared `rasterSamples`
(and line1126 `pipeline->rasterSampleCount`), aborting both the native frontend
and ASan variant-thread harness before the extracted selector audit can execute.
This is a current working-tree render/texture change, not an integration edit.
No selector missing list could be produced from that compile; the extracted
table is ready to run once the variable is defined/fixed.

### Files/hashes
- `delta9_extract_selectors.py`
  `c8cc727249a60678ed7786d030868fb0fd8dcb9a58bb7ee47af2c8abeacd4016`
- `delta9_selectors.inc`
- `delta9_selectors.json`
- `delta9_frontend_audit.inc`
- `delta9_metallib_functions.json`
  `2f8d8f43b8702a70e963a1d10ec65b59a653edc4e78290a88cf5f34a1d778581`
- full run log: `runs/delta9_full_once.log`
- report: `runs/report-delta9-current.json`

No phone, native build, commit or publication. HEAVY slot released; session
ends with the compile blocker and extracted audit artifacts persisted.

## DELTA9 FINAL AUDIT (26 October 2026, base 06b882c)

One thermal-compliant full run; no rerun loop, no phone/native/commit/publish.
Result: **835 checks, 3 required, 0 pending; candidate FAIL**.
Current tree was deliberately tested while compute delta14 edits bridge Rust/ABI
files:

- bridge `e366605197b49a1e…`
- air_bridge.rs `f9db0bf0b95ef8bb…`
- pipeline_format.h `ce462be8cf147750…`
- render encoder `1623cd62f08c4312…`
- render pipeline `4ab8b0aa4dc6a8d0…`
- render_pass `788f80e70e437019…`
- compute_frontend `3e918bee82e12053…`
- compiler `1877804b…`

### Extracted selector audit (executed)
`delta9_extract_selectors.py` read the local QC/RenderBox selrefs and Metal
headers, producing 132 mapped cases + 5 proven-unreachable CA::Fence/event
cases (2,551 unclassified selectors are listed in JSON, not claimed as Metal).
Runtime result: **EXTRACTED_SELECTOR checks=127 missing=11 unreachable=5**.
Required `SELECTOR_MISSING`:
- TeguBuffer `newTextureWithDescriptor:offset:bytesPerRow:`
- TeguCommand `encodeSignalEvent:value:`
- TeguComputeDevice `minimumLinearTextureAlignmentForPixelFormat:`
- TeguComputeDevice `name`
- TeguComputeDevice `newRenderPipelineStateWithDescriptor:completionHandler:`
- TeguComputeDevice `newRenderPipelineStateWithTileDescriptor:options:reflection:error:`
- TeguLibrary `newFunctionWithName:constantValues:completionHandler:`
- TeguQueue `commandBufferWithDescriptor:`
- TeguRenderEncoder `dispatchThreadsPerTile:`
- TeguRenderEncoder `setTileBytes:length:atIndex:`
- TeguRenderEncoder `setTileTexture:atIndex:`
No hand-curated list was used to suppress these.

### default.metallib functions (executed)
141 functions enumerated, `lookup_nil=0`, **135 specialized**, **6 nil**:
- fixed_vert_lph_spc / fixed_vert_lpf_spc: missing `fc_generic`
- fixed_vert_lph_gen / fixed_vert_lpf_gen: missing `fc_base_color`
- fixed_frag_lph_cph / fixed_frag_lph_cpf: missing `fc_attachment_count`
`compute_apl`, `compute_apl_fast`, `sum_apl_and_compute_dimming_factor` are
among the 135 specialized. Records in `delta9_metallib_functions.json`.

### Additional blockers from the same run
- Candidate provenance FAIL: no build receipt matches the current edited
  compute air_bridge/cdylib; expected while compute delta14 is in progress.
- C3 inert pass harness compile fails against render delta13's new
  `TeguPassAttachment.resolveDepthPlane` and
  `TeguRenderEncoder.rasterSampleCount/resolveTargets`; integration harness
  needs updating before the next run. This is not a production assertion.

Artifacts:
- `runs/delta9_full_final_once.log`
  SHA `7221355cf2e5702e00360031228185486a7ebb9185340655fc3a9d5d6027c381`
- `runs/report-delta9-final.json`
  SHA `b28afc3c78e4c2e18329c932a67895f521fa03f4ad7100332502565f2af2e47f`
- selector/function JSON/inc files under the worker directory.

HEAVY slot released. No further runs in this session.

### Post-run integration harness correction (no rerun)
`inert_render_pass_test.m` was updated to the render delta13 pass API after the
single run: fake `TeguPassAttachment` now declares
`resolveLevel`/`resolveSlice`/`resolveDepthPlane`, and fake
`TeguRenderEncoder` has `rasterSampleCount`/`resolveTargets[4]`.  A
syntax-only (`-fsyntax-only`) compile passes.  The C3 test was **not**
reexecuted in this session per the one-run rule.

`delta9_metallib_functions.json` is the exact 141-row function audit; extracted
selector artifacts are in `delta9_selectors.inc/json`.  No further heavy runs.

## DELTA10 (C6 freeze) — integration harness ready, awaiting grant

- C3 fake updated exactly to render delta13/14 API: `TeguPassAttachment`
  resolveLevel/resolveSlice/resolveDepthPlane; `TeguRenderEncoder`
  renderTargets/resolveTargets[4], renderLoads/renderJobs/renderClears,
  rasterSampleCount. Syntax-only check passed; no execution during C6 freeze.
- Extracted selector/function audit artifacts from DELTA9 remain in this
  directory. Awaiting root `HEAVY GRANT integration_tests`; then one full
  `--candidate` run only. No heavy job from integration_tests.
- Current observed C6 source base: `0ffccc0` (render selector changes) and
  lowering compiler `d91bb485…`; root native build owns the heavy slot.

## DELTA10 C6-grant run (26 October 2026)

One authorized full `--candidate` run under root HEAVY GRANT. Production
read-only; no rerun loop; no phone/native/commit/publish.

Current tree at run: render encoder `45499802…`, pipeline `5a361762…`,
render_pass `788f80e7…`; compute bridge `24011dbc…`, air_bridge.rs
`f9db0bf0…`, pipeline_format `ce462be8…`; cdylib `43b875…`; compiler
`d91bb485…`.

### Result: 939 checks, 2 required, candidate FAIL
- **Native extracted audit PASS**: `EXTRACTED_SELECTOR checks=127 missing=0
  unreachable=5`; the 11 earlier gaps (async PSO, tile refusals, device/queue/
  command/buffer selectors) are now present/refusing explicitly.
- **All 141 default.metallib functions**: lookup_nil=0, **135 specialized**,
  **6 nil** due required constants:
  `fixed_vert_lph_spc`/`fixed_vert_lpf_spc` (`fc_generic`),
  `fixed_vert_lph_gen`/`fixed_vert_lpf_gen` (`fc_base_color`),
  `fixed_frag_lph_cph`/`fixed_frag_lph_cpf` (`fc_attachment_count`).
  compute_apl/compute_apl_fast/sum_apl... are among the 135.
- **C3 harness actual**: 52 checks release + 52 ASan, 0 required. The C3
  main gate was stale after render delta13 added MSAA support: it still
  expected the old “multisample render pass unsupported” and “resolveTexture
  unsupported” reasons. The actual reasons are
  “single-sample color attachment outside exact JM class” and
  “multisample color attachment outside exact resolve class”; I corrected the
  integration gate after the run. No production change.
- **Candidate provenance FAIL**: no build receipt matches current edited
  compute air_bridge/cdylib; expected while compute delta14 is in progress.
- Everything else passed: ABI, TGP2, job, draw/MRT/TLS, render harness, corpus,
  producer, native selectors, variant threads.

### Artifacts
- `runs/delta10_full_current.log`
  SHA `315d3126911a5818c6ae2b861a9e7d1a37efd117ecdb4119b6b8c23ff80520d6`
- `runs/report-delta10-current.json`
  SHA `db744fb0bb8248fd6e5c0ddaf76f53d3c75337cea86271b5247fa911d539d619`
- `delta9_selectors.inc/json`, `delta9_metallib_functions.json`.

HEAVY slot released. No further run in this session.

## DELTA11 (26 October 2026) — independent CoreAnimation-shaped red test

Production read-only. New files:
- `coreanimation_render_pass_test.m`
- `coreanimation_render_pass_test.py`
- `COREANIMATION_RENDER_PASS_TEST.md`

The harness compiles the real `gpu/metal/nord2_render_pass.inc` with a minimal
fake ObjC frontend and a fake JM executor returning status 4.  It writes
independently calculated BGRA8/RGBA16F target bytes and checks first/mid/last
row pixels, natural row stride, imported-surface padding, non-inert pass,
unpoisoned command, clear/draw jobs, and slot1-mask0 untouched storage.

Coverage: 1080x2400 BGRA8 stride4320; 1170x2532 BGRA8 stride4680;
IOSurface-imported BGRA8; CAMetalLayer-shaped usage5 drawables; format115;
two attachments 80/80 and 80/115 with pipeline descriptor masks [15,0]; load
Load/Clear/DontCare; store Unknown=4 plus setColorStoreAction; fake JM status4.

### Current tree (uncommitted worktree store-Unknown patch present)
`python3 workers/integration_tests/coreanimation_render_pass_test.py`:
- release: **33 checks, 21 required failures, rc1**
- ASan/UBSan: **33 checks, 21 required failures, rc1**
- every natural-stride case logs
  `TEGU_PASS_REFUSED line=70 reason=color attachment outside exact JM class ... row=4320/4680` (the source diagnostic line moved with the C6f value trace; c6ec reported line=58)
- aligned 4352-stride case now passes the deferred Unknown store and its
  clear/draw assertions.
This is the expected red state for the C6f admission fix.

### Test-only validation of the oracle (NOT production)
A temporary copy of `nord2_render_pass.inc` with only the two
`rowBytes&63` clauses removed was compiled with `-I/tmp/...`:
- **100 checks, 0 failures, rc0** release and ASan/UBSan.
This validates that the test can go green when the pass admits natural display
strides; no production file was changed, built, committed or claimed green.
  Logs: `runs/delta11_coreanimation_testfix_{release,asan}.log`.
  Pre-delta worktree red log: `runs/delta11_head_pass_red.log`.

### Main integration hook
`gpu/tests/metal_resource_integration_test.py` now calls the dedicated runner
and records required label `CoreAnimation render pass acceptance`.  The runner
logs are `runs/delta11_coreanimation_{release,asan}.log`.

### Explicit limitation
The fake pipeline is only a 2-attachment/mask0 **descriptor shape**; the
production `render_pipeline.inc` PSO builder and full frontend draw are not
compiled by this test.  Render's own harness owns that path.  No GPU, pixels,
phone, native rebuild, commit or publication.
