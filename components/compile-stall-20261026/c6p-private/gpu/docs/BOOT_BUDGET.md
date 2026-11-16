# Exact private build supersedes the estimates below (24 October 2026)

`link_kernel_local.py` now produces a genuine strict kernel link with real
firehose/profiling/stack-probe support and no undefined symbols. The exact
`pack_gpu_image.py` outputs fit the 32 MiB partition using the preserved
recompressed rescue. Current bytes, margin and SHA-256 are recorded in
`build/device-candidate/image-receipt.json` and
`build/device-ui-candidate/image-receipt.json`. Audio/Wi-Fi/display objects
were not removed to make space. These images are device-untested and require
exact-image approval. The synthetic dynamic-lookup outputs below remain
uninstallable historical measurements.

# Boot-partition budget for the GPU patch

The tested audio image is **33,542,144 bytes**; the boot partition limit is
**33,554,432 bytes** (32 MiB), leaving **12,288 bytes**.  The GPU driver is not
in that image.  A full CI link was not available locally, but the CI kernel
archive and link inputs are preserved; a synthetic `ld64.lld` relink of the
same all-object archive measured a **65,488-byte delta** with the candidate
GPU archive force-loaded.  This is not a bootable image and its absolute size
is not the CI size, but the delta alone is over four times the remaining boot
margin.  Object-level estimates agree.

## Object-level section sizes

Measured with `gpu/tools/size_report.py` from the exact arm64 Mach-O objects
(`-O2`, the CI recipe):

| object | `__text` | const/strings |
| --- | ---: | ---: |
| Nord2GPU.o | 13,500 | 9,995 |
| nord2_gpu_desc.o | 2,584 | 0 |
| nord2_gpu_mmu.o | 2,528 | 264 |
| nord2_gpu_sched.o | 3,812 | 1,158 |
| nord2_gpu_scpsys.o | 2,636 | 257 |
| nord2_gpu_board.o | 2,212 | 103 |
| nord2_gpu_mem.o | 1,448 | 0 |
| nord2_gpu_jm.o | 1,056 | 0 |
| **total** | **29,836** | **11,777** |

Sum ≈ **40.6 KiB** before final alignment/dead-strip.  Rebuilding the new units
with `-Os` gives **36,458 bytes** (25,404 text + 11,054 const/strings,
measured by `size_report.py --dir build/objs-os`), still well above 12 KiB.  A full kernel link may
dead-strip some code and align differently, but this is a real risk, not a
theoretical one.

## Synthetic link delta

`gpu/tools/measure_kernel_delta.sh` links the preserved CI object archive
(`ci-40034353275/xnu-kernel/kernel.development.sweet.link/
kernel.development.sweet.a`) twice with system `ld64.lld`: baseline, and with
`build/libnord2gpu.a` force-loaded.  Observed 2026-10-24:

```
synthetic baseline: 17,849,472 bytes
synthetic +GPU:     17,914,960 bytes
delta:                  65,488 bytes
```

The command uses `-dead_strip`, `-all_load`, and `-undefined dynamic_lookup`
because the CI link needs macOS kext libraries that are not on this host;
therefore the absolute output is not the CI kernel and must not be flashed.
The baseline synthetic size (17,849,472 B) is within 44 KiB of the actual CI
kernel (17,893,416 B), which supports using the delta.  Both sides load the
same archive and flags, so the 65,488 B delta is a measured candidate
contribution, not a guess.  It exceeds the 12,288-byte margin by ~53 KiB.  Applying the delta to the
tested 33,542,144-byte boot image projects **33,607,632 bytes**, which is
**53,200 bytes over** the 33,554,432-byte partition limit.  Rebuilding the same seven
units with `-Os` and repeating the dead-strip link gave a **49,792-byte**
delta, so `-Os` alone does not fit either.

## Do not flash an oversized image

The morning build must:
1. Link the full kernel and build the exact boot image.
2. Compare its size to 32 MiB and record SHA-256.
3. If it exceeds the limit, stop.  Do not delete audio/Wi-Fi/display/rescue
   code to make room.

## Mitigations, in order

1. Compile the seven new units with `-Os` (and `-Oz` if supported) and strip
   verbose `IOLog` strings; re-measure.
2. Remove nonessential diagnostics and unused descriptor helpers from the
   kernel build (keep the host-test copies).  The compile-time contract can
   still be validated separately.
3. Split the private user-client transport into a smaller selector set or
   defer it behind a build option; core MMU/JM/board code is only ~12 KiB.
4. If a loadable kext path is proven safe on this iOS16 userland, move the
   driver out of the boot kernel; this is not proven tonight.
5. If none fits, do not claim a flashable candidate; keep the host-validated
   stack and request a reviewed image-size strategy from the parent.

The `12,288`-byte margin is a hard constraint from the handoff; the current
object estimates are provided so the parent does not discover this only after
a failed flash.
