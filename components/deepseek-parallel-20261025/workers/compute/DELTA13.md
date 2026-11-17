# Compute DELTA13 — n2mb_render_targets v3 MSAA/resolve mirror (host only)

Base: commit 4f3ad8e (textures DELTA8 n2r v3 landed). No phone/native/publish.

## ABI

`metal/nord2_metal_bridge.h` adds:

```c
#define N2MB_RENDER_TARGETS_MSAA_VERSION 3u
struct n2mb_resolve_target {
    uint64_t token, offset, length;
    uint32_t format, row_bytes, width, height, reserved;
};
struct n2mb_render_targets {
    uint32_t version, count;
    const struct n2mb_render_target *targets;
    const struct tegu_blend_inputs *blends;
    const uint32_t *sample_counts;             /* v3 only */
    const struct n2mb_resolve_target *resolves; /* v3 only, token0 = none */
};
```

Version 1/2 semantics and emitted packets are unchanged. Version 3 requires
`sample_counts[count]` all equal 1 or 4. Every target span is
`row_bytes*height*samples`; sample4 targets use four times the single-sample
span. `resolves` may be NULL or count entries; token0 means no resolve.
Nonzero resolve entries require sample4, format 0 or exact target format,
exact q width/height, 64-byte row/address alignment, `offset <= length <= BO`,
and exact non-aliasing against every target and earlier resolve. Resolve BOs
are looked up through the bridge BO table and retained with the target BOs in
the JM request using write=1; raw addresses are never accepted. v3 blends are
optional and validated when present; NULL keeps the shared shader blend.

`n2r_build_v4` remains the packet owner: it supplies the sample LUT, fb
sample_count/pattern, per-RT tile offsets, writeback_msaa and resolve
base/row/stride from the bridge-resolved records.

## Host tests

`python3 workers/compute/check_mrt_bridge.py`:

- release: **83 checks, 0 required failures**
- one ASan/UBSan: **83 checks, 0 required failures**

New v3 cases cover:
- 4x targets, two resolves; fb sample_count4/ROTATED_4X_GRID; resolve
  writeback_msaa AVERAGE with resolve VA/row; no-resolve target LAYERED;
  rotated LUT at the expected span; target and resolve BOs retained write.
- v3 sample4 without resolves; v3 sample1 without resolves.
- refusal before submit for NULL sample_counts, mixed counts, sample1+resolve,
  resolve format mismatch, resolve width mismatch, resolve/target alias,
  short resolve span.
- Existing v1/v2 legacy and v1 blend-pointer identity remain green.

## Hashes

- `metal/nord2_metal_bridge.c` `c14819755a07611f382378ae4d84b9ad42c69f98af2f0344e6bf64b23f8af830`
- `metal/nord2_metal_bridge.h` `9e4972169dd5601961fcfa4662ad85a08a1d1f6495808e85deb07a2d7561325c`
- worker test `check_mrt_bridge.c` `77faa873d78b697065fdc0188eef1301053f01233ddf797ecc4049dc7f06fa82`
- worker runner `check_mrt_bridge.py` `56bada3d2dd914433b4169b6668c3cac098dc430e2e0bbf402686adee609c2f4`

## Explicit gaps

- No native/device execution. Root L4 qualification remains.
- True-MRT LOAD (`load_action==1`) remains refused as before; v3 does not add
  per-target snapshots.
- Resolve targets are bridge-resolved and retained, but no hardware command
  execution is claimed.
