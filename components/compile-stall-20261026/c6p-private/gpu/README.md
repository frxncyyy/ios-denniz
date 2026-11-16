# Nord 2 native GPU stack (candidate)

Read in this order:

1. `../MORNING_REPORT.md` — what was implemented, what was not, exact next
   action, and the no-device caveats.
2. `PROGRESS.md` and `progress.json` — milestone state.
3. `docs/ARCHITECTURE.md` — layers/files.
4. `docs/DEVICE_TEST_PLAN.md` — the only authorized way to test on hardware.
5. `docs/POWER_GAP.md`, `docs/METAL_GAP.md`, `docs/BOOT_BUDGET.md`, `docs/INSTALL_ROLLBACK.md` — precise remaining blockers, the 12 KiB boot margin and the reversible install plan.

## Quick host validation

```
gpu/tools/stage_xnu.sh          # copy cached CI XNU + apply GPU overlay
gpu/tools/build_mesa_arch9.sh   # first time only; pinned Mesa 81daea6
gpu/tools/run_host_tests.sh     # ASan/UBSan + compiler + ARM64 XNU compile
gpu/tools/check_sync.sh         # gpu/ vs shim/nord2-xnu copies
gpu/tools/check_arch9_contract.sh
gpu/tools/package_candidate.sh  # tarball + manifest under gpu/out/
```

The compiler build needs Python `mako`, `pyyaml`, `setuptools`, `meson` and
`ninja`; `build_mesa_arch9.sh` creates a local venv and installs them. No
device, USB, block device or rootfs access is used.

## Kernel integration

`../xnu/patches/0084-nord2-gpu.patch` adds `bsd/dev/nord2_gpu_*` and
`iokit/Kernel/arm/Nord2GPU.cpp`, plus the two conf entries and the IOKit match.
Apply after the baseline `0082`/`0083` series. The patch is local and has no
public destination. Default boot is unmodified: `nord2_gpu=1` is required to
power/probe; the display/audio/Wi-Fi paths are not touched.

## Device tests

Forbidden tonight. See `docs/DEVICE_TEST_PLAN.md`; the `assume_powered` path
checks SCPsys MFG status before any GPU MMIO, but still requires explicit
morning authorization.
