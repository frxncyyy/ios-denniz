# Compute delta9 — true-MRT per-target blends bridge v2 (host only)

Date: 25 October 2026. No phone/adb/fastboot/USB/mount/writer/publish/CI or commit.

## Contract

Followed render DELTA7 follow-up and textures DELTA6:
`n2r_render_targets` version 2 carries a `const struct tegu_blend_inputs *`
per-target array.  The bridge mirrors that pointer at the end of
`n2mb_render_targets`, version 1 remains byte-identical, and version 2 passes
the caller-owned array unchanged to n2r.

## Production changes

### `metal/nord2_metal_bridge.h`
- Added `N2MB_RENDER_TARGETS_BLEND_VERSION=2`.
- Added `const struct tegu_blend_inputs *blends` at the end of
  `n2mb_render_targets`; version 1 ignores it.

### `metal/nord2_metal_bridge.c`
- `n2c_mrt_prepare` accepts version 1 or 2.
- Version 2 requires a non-NULL blends array and, before any target BO is
  retained, validates each `blends[i].write_mask == targets[i].write_mask`
  and `tegu_blend_build_format(target.format, &blends[i], &checked)`.
- The resolved `n2r_render_targets` carries version/blends unchanged; the
  pointer is never copied.
- Version 1 leaves `blends` NULL in the n2r sidecar, so a non-NULL caller
  pointer is ignored and old packets are unchanged.
- All existing target span, alias, write-mask, TLS, R8, and retention behavior
  is unchanged.

## Host tests

`python3 workers/compute/check_mrt_bridge.py` (release + ASan/UBSan):

- **67 checks, 0 required failures** each.
- New version2 cases:
  - 4-target true MRT with per-target blend inputs (BGRA/BGRA/R8/BGRA,
    distinct disabled/source-alpha records); v9 `MALI_BLEND_unpack` checks RT2
    color mask 15/enable and RT0 mask 0; RT0/RT2 blend bytes differ.  Current
    n2r stamps `internal.fixed_function.rt` per version2 target.
  - version1 with a non-NULL blends pointer produces byte-identical BLEND
    descriptors to version1 with NULL.
  - version2 NULL blends, target/blend mask mismatch and invalid equation all
    refuse without submit.
- Existing MRT/R8/TLS/default v3 checks remain green; integration draw source
  still 33/0.

## Hashes

- `metal/nord2_metal_bridge.c`
  `494a027cd7d979b53ed02205b5a5e7c2ec7c9de5387490b9845e7260a0abe4f0`
- `metal/nord2_metal_bridge.h`
  `8f385cb6de6368c0a8acca27a002056a75c4ff871aa65e1cfe7af9917a1b33b3`
- worker test
  `0cb6bfa384cba7bd3225fddbc54798228b8fbd35d79cf5711b17adf0311d31af`
- n2r headers remain textures-owned:
  `nord2_metal_render.h 82f881b9...`, `nord2_metal_render.c 9d787e56...`
  (current textures version2 internal-rt stamp)

## Integration status

The delta9 integration run is 737 checks with **1 required** outside compute:
the render-owned specialized-variant harness (current render work, assertion
in `vs_texture_host_test.m`) fails.  Candidate provenance passes with cdylib
9ff66cac / compiler ad62ed9e / source d8854273.  No delta9 full-pass claim.
Evidence: `workers/compute/evidence-integration-delta9.{log,json}`.

## Explicit unresolved

- Render must set `n2mb_render_targets.version=2` and point `.blends` at its
  job-lifetime per-target blend array; the bridge does not copy or retain that
  array.
- True-MRT LOAD/framebuffer-fetch remains refused; per-RT v3 remains the
  single-output path.
- No native build, device execution, phone, or publication.
