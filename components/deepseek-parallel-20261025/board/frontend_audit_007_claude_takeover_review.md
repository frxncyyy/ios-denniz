# frontend_audit 007 - Claude takeover review (read-only production)

Reviewer: metal-frontend-audit role (Claude Sonnet 5.5 high). Date 2026-10-25 19:10-19:55 CET.
Host-only. No cargo/Mesa/native build, no phone, no production edit. Probes and
frozen copies of every reviewed file live in `gpu/build/claude-audit-20261025/`
(`snap/` + `hashes_snapshot_1913.txt`; `takeover/` = reconstructed takeover tree
from `working-tree.patch`). Owners are editing live: line numbers below are
for the hashes shown; re-grep before editing.

## Reviewed hashes (sha256 prefix, 12 hex)

| file | hash | note |
| --- | --- | --- |
| metal/nord2_metal_bridge.c | ae293c432144 | unchanged since compute's final message |
| metal/tegu/runtime_pipeline.inc | cad3849bf2b5 | unchanged |
| metal/tegu/compute_frontend.m | 2b1076be48d0 | only change vs takeover: `if(&tegu_mali_compile_compute_v2)` |
| metal/tegu/pipeline_format.h | 51cf599d58f2 | TGP2 max code 1 MiB (was 4 KiB shared) |
| metal/tegu/air_bridge.h | eea1de13738f | GSB view_type/aspect authoritative |
| metal/tegu/air_bridge.rs | 590ff6b743e7 | (7b1c83c8 at takeover; 5cb4912e transient) |
| metal/nord2_texture_resources.h | 23642ffd6dfa | swizzle_valid layout field |
| metal/tegu/render_encoder.inc | 6b1277a6c476 | |
| metal/tegu/render_pipeline.inc | cf3e8d051cb8 -> 46f41ec3b5f3 | changed during review |
| metal/tegu/compositor_submit.inc | ecd96c7a37cf | |
| metal/nord2_metal_render.c / nord2_render_resources.inc | 3cf8d97698f9 / cabb5fadce46 | unchanged |
| metal/tegu/sampler_state.inc | 283980a4d234 | |
| metal/tegu/mali_compute_compiler.c | 1ac90e94cc6a | scratch/TLS refusal added after takeover |
| metal/tegu/mali_graphics_compiler.c | 00ffb2aa2a1f -> 5a2b029e9532 | changed during review |

## Ranked actionable blockers

### B1 (compute) - native-config buffer-only compute sends TDG2 on a TGP1 pipeline; bridge refuses
* `compute_frontend.m:1776` routes everything except `packageVersion==1` to
  `tegu_runtime_dispatch_v2`. Function-created pipelines never set
  `packageVersion` (only `newTeguPipelineWithPackage`, line 867), so in the
  native macro set (`-DTEGU_GRAPHICS_BRIDGE -DTEGU_AIR_BRIDGE`) every ordinary
  `dispatchThreadgroups` takes the v2 path.
* For a buffer-only kernel `require_v2` is 0 (`runtime_pipeline.inc:309`), the
  variant is TGP1 (`tegu_mali_compile_specialized`), yet the wire is always
  TDG2 (`runtime_pipeline.inc:329-332`). `nord2_metal_bridge.c:261`
  `dispatch_v2` requires `p->version==2`, `:625-626` selects by wire size.
* HEAD used the legacy 272-byte wire for these pipelines. This regresses the
  qualified 64-compute baseline once natively linked.
* Why tests missed it: `runtime_frontend_host_test.m` /
  `check_runtime_v1_aligned.py` define `TEGU_AIR_BRIDGE` + `TEGU_MALI_BRIDGE`
  only, so they compile the legacy `tegu_compute_encoder_dispatch`; the 610/610
  integration report lists `runtime_pipeline.inc` and `compute_frontend.m` as
  `context_sources`, never executed.
* Repro (real bridge, fake kernel, 1 s):
  `cc -std=c11 -w -Iinclude -Imetal -Imetal/tegu -Ibuild/tegu-frontend gpu/build/claude-audit-20261025/tgp1_tdg2_probe.c metal/nord2_metal_bridge.c src/nord2_gpu_jm.c src/nord2_gpu_desc.c metal/nord2_metal_render.c metal/tegu/blend_state.c -o p && ./p`
  prints `TDG1 wire on TGP1 pipeline rc=0x0 submits=1` and
  `TDG2 wire on TGP1 pipeline rc=0xe00002c2 submits_delta=0`.
* Fix: emit unchanged TDG1 for a selected TGP1 single-region variant (or have the
  bridge accept TDG2 on TGP1), plus a host test built with the native macro set.

### B2 (compute) - `Dispatch::v2Buffers[8]` indexed by Metal slot 0..30 (retain aliasing / UAF)
* `compute_frontend.m:323` `TeguBuffer *v2Buffers[8]; id v2Textures[31]...`;
  `runtime_pipeline.inc:407` `job->v2Buffers[desc->metal_index]` where
  `metal_index` is a Metal slot (Rust accepts buffer slot <31, only native
  binding <8, `air_bridge.rs` buffer arm).
* Compile-time proof (passes): `v2Buffers[9]` aliases `v2Textures[1]`,
  `v2Buffers[30]` aliases `v2Textures[22]`:
  `clang <native flags> -fsyntax-only gpu/build/claude-audit-20261025/v2buffers_alias_probe.m`.
* Effect: buffer retain stored in a texture slot; `!job->v2X[i]` guards then skip
  the texture's (or the buffer's) retain. The job no longer owns that object, so
  an app release after commit can free it before the queue thread executes.
* Fix: key by native binding (<8) or `v2Buffers[31]`; add retain-count test.

### B3 (render) - `genericVariants` array grown with malloc/memcpy/free while the execute thread reads it
* `render_pipeline.inc:514-520` (grow 4->8->16->32, no lock) versus
  `compositor_submit.inc:241-242` and `render_encoder.inc` plan_binding /
  required, which dereference `p->genericVariants[selected-1]` on the queue
  thread at execute time, and `n2mb_draw_v2` receives a pointer into the array.
  App-thread growth frees the array under a running draw (heap UAF).
* Fix: fixed `[32]` array (no realloc) or a stable chunk list; lock/atomic
  publication of `genericVariantCount`. Test: ASan 2-thread (growing 4->5 while
  reading element 0).

### B4 (root/native) - no current native artifact matches current sources
See "Source/binary identity". Notably the in-progress native Mali build and the
frontend objects will bake B1/B2 if linked before they are fixed.

## Testable risks (not blockers for the first image, but must be ledgered)

1. compute v2 textures are not shadow-synced: `tegu_shadow_sync_jobs`
   (`compositor_submit.inc:203`) scans only `compositorTextures`/`renderTexture`;
   `v2Textures` is never scanned. A kernel sampling/writing a BGRA8 IOSurface
   shadow texture reads stale data / never writes back. Owner compute+render.
2. `render_encoder.inc:2367` `job->expandIndex=[ib retain]` is not released at
   `fail:` (`:2524`) (pre-existing; now hit more often by new -13 refusals).
3. Unused-binding strictness: `compute_frontend.m:1733/1764` return 0 if
   resources>32 or a bound texture has neither read nor write usage; Rust
   re-validates every bound texture (levels<=13, stride, access). A stale binding
   invalidates the whole command. Filter to shader-referenced slots.
4. `n2c_resolve_translated_sampler` (`bridge.c:213-232`) forces `normalized=1`
   first, then derives `lod_algorithm` from `max_anisotropy`: a Metal
   pixel-coordinate sampler with anisotropy >1 becomes anisotropic natively while
   the translator treats it isotropic (`anisotropy_degree` returns 0).
5. Rust accepts `mipmap_levels` 1..13 but `dispatch_v2` refuses `levels!=1`
   (`bridge.c` after swizzle_valid), so compile cost is paid and the command then
   fails; refuse in the planner. `compute_texture_span` (`air_bridge.rs` ~2766) uses
   pixel height x row_stride x layers (over-estimates block formats, ignores
   layer_stride): safe refusal but compressed/padded-layer kernels over-refuse.
6. Groups bound mismatch: v2 accepts 65536, legacy bridge 65535 (HEAD).
7. Known and unchanged: no ATOMIC (0x1000) flag is ever produced (bridge rejects
   it) and TDG2 `SWIZZLE_VALID` is never produced (frontend always sends 0, no
   texture-view API). Refusal is not support.

## Verified OK (do not re-open)

* ABI: C and Rust agree field-for-field: resource 160, descriptor 64, region 1184,
  dispatch 11592; `descriptors@72 regions@2120 flags@24 constant_count@28
  constants@32 push@1056` (`abi_probe.c`, Rust const asserts). Wire TDG2 =
  32+32*192+128 = 6304 bytes (pipeline_format.h). Every exported
  `tegu_air_*` in air_bridge.h exists in air_bridge.rs and vice versa.
* Per-region `local[3]`: `runtime_pipeline.inc` compares package local to each
  region's local; constants include the LocalSize spec ids; variant cache key =
  hash+constants (+packageVersion when v2 required); eviction safe (jobs retain
  variants).
* sampler_index 0..15 enforced in frontend (setSamplerState), Rust, and bridge
  (both SAMPLER and COMBINED). Declared span: `backing_length = level_offset+span`
  (never BO size). Explicit/nonzero swizzle plumbing and class check by
  effective view are in the bridge.
* normalized=1 matches the pinned translator: `vk/structs.rs:204`
  (`unnormalizedCoordinates` is never set; pixel samplers handled in-shader).
  ClampZero == descriptor border colour (`metal/sampler.rs:455-475`), so
  ClampZero->ClampBorder for opaque black/white is consistent; integer->nearest/
  aniso 1 consistent with packer refusals. `lodAverage`/argument-buffer samplers
  are refused (`sampler_state.inc`).
* Draw path lifetimes: all texture/sampler/buffer objects are retained in the
  `Dispatch` until command dealloc (`compositorTextures[native binding]`,
  collision -> refusal); `n2mb_draw_v2`/`dispatch_v2` are synchronous (submit +
  wait 5 s). `resolve`/`add_ref` merge write masks; >32 handles refuse.
  Legacy-sampler guards in render are present (`tegu_sampler_legacy_ok` at
  render_encoder.inc:1621,1751,2632,2937).
* Stale-library behaviour: `tegu_mali_compile_compute_v2` and `n2mb_draw_v2`
  are weak (explicit refusal); `tegu_air_render_pipeline_plan_generic_specialized`
  and `tegu_mali_compile_generic_pair_specialized*` are strong, so a stale
  Rust/Mali archive fails the strict link instead of running. There is no runtime
  ABI handshake: a partially rebuilt set that still links (same names, changed
  layout, e.g. `n2t_texture_layout`) would not be caught.
* ARM64 `-Werror -fsyntax-only` with the build script flags passes for
  compute_frontend.m (2b1076be), nord2_metal_bridge.c (ae293c43) and
  nord2_metal_render.c (3cf8d976) (syntax only, not link).

## Source/binary identity (as of ~19:45)

| artifact | hash | status |
| --- | --- | --- |
| host Rust cdylib `workers/compute/cargo-target/release/libtegu_air_bridge.so` | 8861e0a0 (19:25) | matches air_bridge.rs 590ff6b7 |
| host Mali `build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so` | 57db4be4 (19:36) | rebuilt repeatedly by lowering (ea4454b7 -> ec828d0f -> 57db4be4); matches compiler sources only if no edit after 19:36 (mali_graphics_compiler.c changed 19:36:05) |
| iOS Rust `research/nord2-native-air/.../libtegu_air_bridge.a` | d23b57b1 (19:40) | built from air_bridge.rs 590ff6b7; `native_dispatch_v2` + `plan_generic_specialized` present |
| `gpu/build/native/libNord2Mali.a` / `.dylib` | 4a90eab4 / 844cca16 (Oct 24 12:16) | STALE: no `compile_compute_v2`, no `generic_pair_specialized*`, no TLS refusal |
| `build/tegu-frontend/libNord2MetalFrontend.a` | 6529e2d2 (13:54) | STALE: no `n2mb_draw_v2`, `n2r_build_v2`, `tegu_runtime_dispatch_v2` |
| `build/native/Nord2Metal.bundle/Nord2Metal` | 4afb6d30 (13:54), receipt `device_load_tested:false` | STALE; manifest.json/bundle-receipt.json also stale |

Rebuild set for a coherent SpringBoard candidate (after B1/B2 fixed, one lease,
jobs1): (1) `build_native_compilers.py mali` (running now; lowering must freeze
mali_*.c first); (2) `build_native_compilers.py air` (already built 19:40, rebuild
only if air_bridge.rs/h change); (3) `tools/build_tegu_frontend_objects.sh`
(regenerates nord2_mesa_pack.h, build-identity.h; compiles compute_frontend.m,
bridge, render, desc, jm); (4) `tools/link_metal_bundle.py` strict link
(undefined symbols not allowed); (5) re-run host suite against the same hashes
and write the receipt. Verify before linking that Mali .a, Rust .a and frontend
objects are all newer than every source (build-identity.h hashes all `metal/**`
plus both archives).

## Test requests (posted to integration_tests/compute/render)
See board message `TEST REQUESTS from audit`: native-macro frontend host test
(buffer-only through real bridge), retain-count test for slots 9/30, ASan
2-thread variant-growth test, compute shadow-texture test.
