# Proposal: TDG2 mip/layer transport patch

Target owner: compute (`gpu/metal/nord2_metal_bridge.c`).
Textures did **not** edit the production bridge.

- Base production file SHA256:
  `ae293c432144be8b5915ca5f2ff8f184d000be446c4f1c56cdfe19bee511da76`
- Private patched copy SHA256:
  `e3c0434975c54fabc85ab883602faf93114a1fd70a3096f6b82a52cae910a055`
- Unified diff: `proposals/tdg2-mip-layer.patch`
  SHA256 `c87816797fe04d8a54c5803dd64789b6b3cf5b0668703fc7c51faf74e8f65817`
- Applied cleanly with `patch -p1` to a pristine copy of the base in
  `private/applytest/`.

## What it changes

1. Replaces the literal `N2C_MAX_PLANES_PER_DESC 16` with a capacity derived
   from the remaining command BO after table2:
   `(N2C_V2_BYTES - N2C_PLANES) / (N2C_PLANE_CAPACITY*32)` = 23 planes per
   resident descriptor in the current 40 KiB BO. No wire/decode layout changes.
2. Adds `n2c_fill_levels`, implementing the active producer's contiguous linear
   mip convention:
   - `LEVEL_OFFSET` + the descriptor's single `ROW_STRIDE`, `SLICE_STRIDE`,
     `LAYER_STRIDE` apply to every view level;
   - level L begins after the full preceding level span;
   - level dimensions are minified with `n2t_minify`;
   - compressed block dimensions are honored;
   - 3D/MS span includes the slice/sample stride;
   - `SPAN` is from `LEVEL_OFFSET` and must cover the derived chain.
   The shared `n2t_layout_check` still validates every row/stride/overlap/
   alignment/span. A producer that cannot meet this convention must extend the
   wire with per-level facts or refuse; it must not bind level0 only.
3. Removes the levels!=1 refusal and the 16-layer refusal. Replaces them with
   `layers>N2C_MAX_PLANES_PER_DESC || layers*levels>N2C_MAX_PLANES_PER_DESC`.
4. Packs all layers and levels through
   `n2t_layout_pack_sampled`/`n2t_layout_pack_storage` in layer-major,
   level-minor order. Payload and descriptor are staged in local buffers and
   copied into the command BO only after the helper succeeds (per-resource
   output preservation on refusal).
5. Reads `VIEW_LEVEL`/`VIEW_LAYER` into `first_level`/`first_layer` metadata;
   offsets remain view-relative and are not shifted twice.

## Host evidence

Harness copied from integration_tests into `tests/n2mb_v2_job_test.c` and run
against the private patched bridge; no production bridge change.

```
cc ... tests/n2mb_v2_job_test.c private/nord2_metal_bridge.c \
  src/nord2_gpu_jm.c src/nord2_gpu_desc.c metal/nord2_metal_render.c \
  metal/tegu/blend_state.c metal/tegu/texture_state_mesa_golden.c \
  tests/n2mb_v9_golden.c
=> n2mb v2 job: 134 checks, 0 required failures, 0 pending failures
```

The same run under `-fsanitize=address,undefined` passes 126/126. New cases:

- 3-level 16x16 RGBA8 with nonzero view level offset:
  planes at base+1024/+2048/+2560, descriptor levels field 2, maxLOD 2.0,
  plane word7 zero (arch9 v9 oracle).
- 3D depth2 x 2 levels: two planes at base/base+1024, descriptor depth1,
  levels field1, slice-stride span checked.
- 17-layer 2D array: 17 plane entries, descriptor array-size 16, last plane
  address base+16*1024; no 16-layer wire refusal.
- Existing 113-check harness still passes, including the 2-layer array and
  view-level case.

## Limitations

- This is a **proposal**, not applied production. Compute must ACK the
  contiguous-linear-mip convention or replace it with explicit per-level wire
  facts.
- No hardware/GPU run. No phone/commit/publish.
- Current wire cannot express arbitrary per-level row/slice strides or
  non-contiguous mip offsets; those need new wire fields and remain explicit
  gaps.
- 23 planes/descriptor is bounded by the existing command BO; larger arrays
  need a separate payload BO/ABI.
