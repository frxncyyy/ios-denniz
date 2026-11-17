Audit dispositions + ABI field addition (textures)

All seven findings are fixed and tested in the current production header/n2r:

1. ETC2 RGB8/RGB8A1 (180..183): bytes_per_block was 16; now 8. Tests 180 and 182 use 64-byte block rows / 512-byte levels and Mesa v10 plane goldens; ETC2 RGBA8 (178) remains 16.
2. swizzle ambiguity: added explicit `uint32_t swizzle_valid` to `struct n2t_texture_layout` immediately before `swizzle_word`. `swizzle_valid=0` (zero-init) means format default; `swizzle_valid=1, swizzle_word=0x000` is a valid RRRR descriptor; bit12/high bits refuse. Tests cover RRRR and bit12. This is an additive field; no consumer had committed code against the previous struct, but it changes sizeof/offset after the structural freeze, so compute/render must rebuild against the current header.
3. Public `n2t_layout_pack_sampled/storage` now rejects zero backing address or zero backing_length. Tests add zero-length and 2^48-aligned backing refusal. `n2t_layout_validate` still allows 0/0 for shape-only preflight by design; pack does not.
4. Cube: ordinary CUBE requires exactly 6 layers and width==height; CUBE_ARRAY requires multiples of 6 and square dimensions. Tests cover cube12, nonsquare, cube6 positive.
5. first_layer/first_level: explicit view-relative convention. `level[].offset` and `layer_stride` are already view-adjusted; first_layer/first_level are physical metadata and are never added again. Header documents it; nonzero-first_layer test with distinct addresses confirms.
6. `nord2_metal_render.h` now says `n2r_addresses.binding[i]` is the request POSITION, not the native binding number. Sparse test q.bindings[0].binding=3 / [1].binding=9 uses distinct BOs and checks descriptors/payload.
7. Shared `n2r_texture_resources_check` preflight now validates version/count/pointer, duplicates, orphan entries, request kind agreement, layout shape via n2t_layout_check, backing_length <= memory.bytes, sampler validity and sampler/layout consistency. `n2r_validate_v2`, `n2r_build_impl` and `resources` use it before any submit.

Legacy format whitelist: `gd_request_ok` now skips the old non-ASTC format/span whitelist only when an authoritative sidecar exists for that binding. Identical legacy requests still refuse. Tests add v2-positive format125 (RGBA32Float), ETC2 RGB8 and depth16, with `n2r_build` legacy refusal for the same request. No format0 bypass and no gd_format broadening.

Correction accepted: final lowerer mapping is table1[N] texture/image, table2[N] sampler, internal sampler2[256]. My earlier wiring message's table3 sentence is superseded; ignore it.

Diagnostic NUL: production source has a single `\0` escape at the error copy; verified with cat -A and strict compile, no multichar literal.

Current tests: helper 724 checks PASS release + ASan/UBSan; n2r suite PASS release + ASan/UBSan including formats and sparse positions; existing n2r oracle PASS; ARM64 render/bridge/compute_frontend compile PASS; freestanding object has no undefined symbols. Full list in workers/textures/STATUS.md. No phone/build lease/commit/publish. Render/bridge reachability and ordinary sampler factory guard remain cross-owner gaps.
