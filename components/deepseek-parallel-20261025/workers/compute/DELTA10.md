# Compute delta10 — compositor default-on experiment + shared process helper

Date: 26 October 2026. No phone/adb/fastboot/USB/mount/writer/publish/CI or commit.

## Production changes

- `metal/tegu/compute_frontend.m`:
  - `tegu_compositor_opt_in_exists()` returns 1 when the compile-time
    `TEGU_COMPOSITOR_DEFAULT_ON` macro is present, before consulting the host
    test seam or the rootfs file.  Without the macro the previous file-based
    default (off) is unchanged.
  - Added exported helper
    `int tegu_compositor_process_is_backboardd(void)` implemented by the same
    `getprogname()`/host-seam check used by the device constructor.
- `metal/tegu/compositor_process.h`:
  - Shared declaration for render DELTA8:
    `int tegu_compositor_process_is_backboardd(void);`
  - Render can include this header or declare the same prototype.

## Host tests

`python3 workers/compute/check_frontend_delta5.py` now builds and runs the same
real frontend twice:

- default build: `TEGU_COMPOSITOR_DEFAULT_ON` absent; backboardd with no file is
  denied, and the file-based opt-in enables it.
- experiment build: `-DTEGU_COMPOSITOR_DEFAULT_ON`; backboardd with no file is
  enabled without consulting the host file.
- Both runs also retain delta5/delta8 coverage: bounded NOTREADY retry,
  compositor gate log, feature-set mapping and exactly 64 `TEGU_DEVQ` lines.
- The exported process helper is asserted for host backboardd true/false.

Result: **both binaries PASS** under ASan/UBSan.

## Hashes

- `metal/tegu/compute_frontend.m`
  `9d12b0f8acc4eb3656c839bf41b67b2018588751de7f4e57d5f5e21059fac80b`
- `metal/tegu/compositor_process.h`
  `f8b296e9907756aee0faf90f73c6d513794bc7d055c89d0c550ede43df86209b`
- worker test `check_frontend_delta5.m`
  `a79aa76d4168399543defccc5494a7fd8117c75540ec64c7eae6910bf67ed269`
- runner `check_frontend_delta5.py`
  `64fcb513b564cfc753e1019cd47e75de1fd67fdd481f0011e6623dc555557ce0`

## Integration

Integration from the overnight/work root after the delta10 frontend edit:
**737/737 checks, 0 required, 0 pending, release+ASan**, candidate provenance
OK with cdylib 9ff66cac / compiler ad62ed9e / source d8854273.
Evidence: `workers/compute/evidence-integration-delta10.{log,json}`.

## Explicit unresolved

- `TEGU_COMPOSITOR_DEFAULT_ON` is an explicit experiment build switch; it is
  not enabled in the default build.
- The helper name is posted to the board for render integration; render still
  needs to call it for its DELTA8 gating path.
- Host-only; no native build, device run, phone or publication.
