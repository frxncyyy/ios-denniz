# Compute delta6 — bridge true-MRT/R8 path and planner admission (host only)

Date: 25 October 2026. No phone/adb/fastboot/USB/mount/writer/publish/CI or commit.

## Contract followed

Render DELTA6 chose per-RT `SELECT_RT` jobs for the bounded QuartzCore path_*
case and requested no mandatory n2r bridge ABI change.  Textures already added
`n2r_render_targets` / `n2r_build_v4` (current working tree).  I kept v1/v2/v3
default bytes and added an additive bridge target-token entry plus the one
missing R8 per-RT path.

## Production changes

### `metal/nord2_metal_bridge.h`
- Added `N2MB_RENDER_TARGETS_VERSION=1`, `N2MB_MAX_RENDER_TARGETS=4`,
  `struct n2mb_render_target` (BO token + allocation-relative offset/length,
  format, load_action, write_mask, row_bytes,width,height, clear_words) and
  `struct n2mb_render_targets`.
- Added `n2mb_draw_v4`; v3 remains unchanged for callers.

### `metal/nord2_metal_bridge.c`
- `n2c_mrt_prepare` resolves every target token through the owned BO table,
  rejects internal/alias/overflow/span/shape errors, resolves each target
  address, and adds every target to the JM request with write retention.
- `n2mb_draw_v4` passes 1..4 resolved targets to `n2r_build_v4`; target
  write masks/clear actions/rows/formats are per-target.  Count>2 gets the
  bounded sample-locations span at `command+12288`.
- R8 per-RT path: v3 with target format10 (the path_* RT2-only case) is
  routed through a one-target v4 build with the actual R8 row.  The legacy
  generic preflight's four-byte row assumption is not applied to that path;
  the q copy is only a validation copy and target descriptors use the real
  row.  Attachment/framebuffer-fetch on a true-MRT job is explicitly refused
  (no per-target snapshot transport yet); per-RT v3 attachment path remains.
- Other formats 70/80/115 take the original v3 path byte-for-byte.

### `metal/tegu/air_bridge.rs`
- `generic_target_formats` now admits the bounded explicit MRT classes
  `[80,80,10]`, `[80,80,10,10]`, `[80,80,10,80]` (up to4, first two BGRA8,
  remaining R8/BGRA8).  Earlier single/[80,80] admissions are unchanged;
  other combinations remain explicit refusals.
- Added `generic_mrt_plan_tests` covering admitted/refused classes.

## Tests

- `python3 build/deepseek-parallel-20261025/workers/compute/check_mrt_bridge.py`
  - release: **59 checks, 0 required failures**
  - ASan+UBSan: **59 checks, 0 required failures**
  - covers true 4-target v4 (RT0/RT1 BGRA8 + RT2 R8 + zero-write RT3), per-target
    writeback addresses/formats/rows, RT2-only mask, sample-locations payload,
    all target BO retention/write masks, R8 v3 one-target path, duplicate
    address, short span and width-mismatch refusals.
- Existing integration draw_v2 source compiled against the current bridge:
  **33 checks, 0 required failures**, confirming default v1/v2/v3 bytes are
  unchanged.
- Rust full suite in worker air-work: **29/29 PASS**, including the new
  bounded MRT planner admission test.  No production cdylib rebuild was done
  (root owns the lease/build).
- No GPU/native/device test.

## Integration status

The full integration run against the current bridge and the old worker cdylib
reported 737 checks with 2 required failures: the candidate provenance gate
(air_bridge.rs is now d8854273; the cdylib/receipt are still 06a6c6ee and must
be rebuilt by root), and the render specialized-variant harness (clang-23
built-in UTF-8/parse failure in a render-owned test).  Both are outside this
change; no 737-pass claim is made for delta6.

## Hashes

- `metal/nord2_metal_bridge.c` `7d9016b94d4c0facf40395100975e1317f794af201b4c70e256537ec22d46233`
- `metal/nord2_metal_bridge.h` `e99f226fe0e88bd0bc6302d2ae29e5a0194886e7d7b809bdb6aeb711489eb116`
- `metal/tegu/air_bridge.rs` `d8854273a6da75a42fc3f2a720b59456779a89fc156fb9140cb9f21afdc8b6b0`
- worker test `check_mrt_bridge.c` `79920db9c01b6570e10f3b5f783ff569b6ba3aea15f8c1c6f5638aa9d76476f3`
- runner `check_mrt_bridge.py` `885e61046152fb0e6ce5625011fad93f6262e423ede60090a252259f53c11d3f`

## Explicit unresolved

- True-MRT `load_action=1`/framebuffer-fetch does not yet have per-target
  snapshots; it refuses rather than substituting.  Render's chosen per-RT
  jobs continue to use the established v3 snapshot path.
- Public `n2mb_draw_v4` is available/tested but render has not wired it; the
  per-RT RT2-only path is the bounded delta6 path.  R8 attachment fetch is
  refused on the true-MRT path.
- The planner admission is format/count bounded as above, not arbitrary 8-RT
  support.  No native/device claim.
