# Compute delta7 — host cdylib/producer refresh + graphics TLS (host only)

Date: 25 October 2026. No phone/adb/fastboot/USB/mount/writer/publish/CI or commit.

## 1. L1 host cdylib / real producer refresh

- Copied production `metal/tegu/air_bridge.rs` (d8854273) into worker
  `air-work/src/lib.rs`; no content difference.
- Rebuilt the worker host cdylib with `cargo build --release`,
  `CARGO_BUILD_JOBS=1`:
  `9ff66cacdd22775dc1ea121f663537df8e3731cff4453c2fd68a74d10673b4b9`.
- Full worker Rust suite: **29/29 PASS** (including
  `generic_mrt_plan_tests` and all prior compute/render planner tests).
- Real producer against current fixed compiler
  `ad62ed9e405feb8fa85b1b3d641b6dc1ee6357c9059dbc73ff2c79c58e052e72`:
  **7/7 PASS**.  Receipt refreshed at
  `build/compute-v2-producer-current-abi/result.json` with compiler ad62ed9e,
  cdylib 9ff66cac, source d8854273.
- Integration `--candidate` from overnight/work: candidate provenance
  **PASSES** (737 checks, 1 required remaining, render-owned harness).

## 2. Graphics TLS bridge wiring

Textures' n2r TLS ABI is live:
`n2r_draw_options.thread_storage_bytes/reserved`,
`n2r_addresses.tls`, `n2r_thread_storage_total`.

`metal/nord2_metal_bridge.c` now, inside `draw_locked`:

- Rejects an invalid draw-options version, reserved word, or TLS bytes over
  `N2R_THREAD_STORAGE_MAX` before allocation.
- Queries the GPU core-ID range via the existing `n2c_core_range` helper.
- Calls `n2r_thread_storage_total(bytes,2048,cores,&total)`; refuses zero/
  overflow/total over 64 MiB or allocation failure before submit.
- Allocates one internal BO (alloc's 16 KiB alignment is at least the required
  4096), sets `a.tls`, and retains it **read-only** in the JM request.
- Releases the TLS BO after completion or in failure cleanup.
- TLS bytes0 leaves the old packet untouched; no allocation or status query.
- The LOAD preload copies `n2r_addresses`; n2r ignores `a.tls` when the byte
  count is zero, so the preload remains TLS0.

## Tests

`python3 workers/compute/check_mrt_bridge.py` (release + ASan/UBSan):

- **59 checks, 0 required failures** each.
- New TLS cases: 64 B/thread with one core -> 131072-byte BO; v9
  `MALI_LOCAL_STORAGE_unpack` checks `tls_size=2`, PACKED, `base>>8`, no-WLS
  marker; descriptor pointer shared by DRAW and SHADER_ENVIRONMENT; TLS BO is
  present read-only in the submitted request and released after completion;
  32 KiB (over the fake 64 MiB context) and 32769 B refuse without submit.
- Prior MRT/R8 cases remain green; existing integration draw source is
  33 checks/0 required for v3 defaults.

## Hashes

- `metal/nord2_metal_bridge.c`
  `7d9016b94d4c0facf40395100975e1317f794af201b4c70e256537ec22d46233`
- `metal/nord2_metal_bridge.h` unchanged
  `e99f226fe0e88bd0bc6302d2ae29e5a0194886e7d7b809bdb6aeb711489eb116`
- `metal/tegu/air_bridge.rs`
  `d8854273a6da75a42fc3f2a720b59456779a89fc156fb9140cb9f21afdc8b6b0`
- host cdylib
  `9ff66cacdd22775dc1ea121f663537df8e3731cff4453c2fd68a74d10673b4b9`
- compiler
  `ad62ed9e405feb8fa85b1b3d641b6dc1ee6357c9059dbc73ff2c79c58e052e72`
- worker test
  `79920db9c01b6570e10f3b5f783ff569b6ba3aea15f8c1c6f5638aa9d76476f3`

## Explicit unresolved

- Graphics TLS sizing uses the conservative 2048 threads/core and a 64 MiB
  allocation cap; exact hardware thread/TLS limits are not device-proven.
  Large per-thread TLS can explicitly refuse.
- TLS BO lifetime is host-tested only; no native/device execution.
- Integration's remaining required failure is the render-owned
  specialized-variant harness/binary, not candidate provenance or compute.
- True-MRT LOAD/framebuffer fetch and R8 attachment fetch remain explicit
  refusals; render's bounded path is per-RT v3.
