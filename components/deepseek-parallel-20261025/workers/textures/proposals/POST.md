TDG2 mip/layer proposal + arch11 texture_state disposition

PATCH (ready for compute owner; production bridge untouched by textures):
  gpu/build/deepseek-parallel-20261025/workers/textures/proposals/tdg2-mip-layer.patch
  base gpu/metal/nord2_metal_bridge.c SHA256 ae293c432144be8b5915ca5f2ff8f184d000be446c4f1c56cdfe19bee511da76
  patched private copy SHA256 e3c0434975c54fabc85ab883602faf93114a1fd70a3096f6b82a52cae910a055
  patch SHA256 c87816797fe04d8a54c5803dd64789b6b3cf5b0668703fc7c51faf74e8f65817
  applies cleanly with patch -p1 to a pristine base copy.

What it does:
- replaces the literal 16-plane cap with a command-BO-derived, helper-checked cap (23 in the current 40KiB BO);
- removes levels!=1; adds n2c_fill_levels for contiguous linear mip levels using the wire's single ROW/SLICE/LAYER stride and LEVEL_OFFSET (SPAN measured from LEVEL_OFFSET);
- packs all layers x levels via n2t_layout_pack_sampled/storage, layer-major/level-minor, stage-then-copy so a helper refusal does not commit that descriptor/payload;
- reads VIEW_LEVEL/VIEW_LAYER as metadata; offsets stay view-relative.

HOST RESULT (private patched bridge + copied integration_tests harness):
  tests/n2mb_v2_job_test.c
  n2mb v2 job: 134 checks, 0 required failures, 0 pending failures
  same under -fsanitize=address,undefined.
New cases: 3-level 16x16 with nonzero view level origin, 3D depth2 x2 levels, 17-layer 2D array; existing 113 checks remain green. Planes checked layer-major/level-minor and arch9 word7 zero via v9 oracle.

LIMITATION: the wire has no per-level offsets/strides, so this proposes the only representable contiguous-linear convention. Arbitrary per-level Metal mip layouts require additive wire facts or explicit refusal; do not silently bind level0. 23 planes/descriptor is the current command-BO bound.

ARCH11 texture_state audit:
- texture_state.c is not compiled by tools/build_tegu_frontend_objects.sh; render_client.inc is not included by the active NORD2_JM_TRANSPORT frontend.
- Added an explicit #ifdef NORD2_JM_TRANSPORT body returning -2 with output unchanged if a future build links it; dedicated guard test passes; normal arch11 host test remains unchanged.

No phone/build/commit/publish by textures.
