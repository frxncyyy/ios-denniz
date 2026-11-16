# iOS 16 on the OnePlus Nord 2 5G (MT6893) — port source tree

This branch (`nord2-port-20261116`) is a **source export** of the iOS 16.7.x (iPhone X / iPhone10,3/10,6 userland) port to the
OnePlus Nord 2 5G (MediaTek Dimensity 1200, fastboot product `oplus6893`), built on top of the
[ios-sweet](https://github.com/dipa-studio/ios-sweet) SWEET boot shim/loader. It contains the port's own kernel patch series,
the source tree of the final kernel line, host tools, payload scripts, tests and engineering notes, with one commit per
component in the order the work was done (2026-10-26 .. 2026-11-16).

It is **not** an installable release. There are no images, no Apple or vendor binaries, no device dumps and no personal data in
this tree (see [docs/EXCLUDED.md](docs/EXCLUDED.md)). The GPU stack is included: the kernel GPU driver and the Metal userland
(Nord2Metal, libredwoodair glue, Mesa patches), parts of which are **derived from the private ios-tegu reference sources**
(see [THIRD-PARTY.md](THIRD-PARTY.md); check the upstream licence before reusing them).

- How to try it on another Nord 2: [docs/INSTALL-NORD2.md](docs/INSTALL-NORD2.md) (read the status first: that flow has never
  run on a second phone).
- Piece-by-piece status and known limits: [docs/STATUS.md](docs/STATUS.md).
- Engineering log of the whole bring-up: [notes/ENGINEERING-LOG.md](notes/ENGINEERING-LOG.md) (identity values redacted).
- Licences and credits: [LICENSES-AND-CREDITS.md](LICENSES-AND-CREDITS.md), [THIRD-PARTY.md](THIRD-PARTY.md).

## Status labels
- **[verified on one phone]** observed on the author's phone (kernel log over USB, screenshots, or the user using the feature).
- **[host]** built/tested on the host only (unit tests, fake-root or VM rehearsal).
- **[open]** known not to work yet.
Nothing has been run on a second Nord 2.

## What works [verified on one phone]
Kernel line on the phone: **`q10jfinal44ts`** (image sha256 prefix `f55a15ea`).
- **GPU**: Terraria runs in landscape with correct touch (gfxall17 + flip); the GPU fault recovery **0750** is in the line.
- **MDP** (MediaTek media processor) offload: **dual pipe works on Magnifier**.
- **Charging**: the USB/charging fix (**0700-0703**) works.
- **Cellular**: SIM works; **modem firmware restored** and the **Carrier row clean** (nvfile 0715 + simcache, 0710-0712 PIN/APN).
- **Sensors**: accelerometer, gyroscope and compass (magnetometer) data reach iOS; **auto-rotate** works (0760/0761).
- **Bluetooth**: pairing and **AAC streaming stable** in transport mode 2; early start at ~24 s with Wi-Fi (S0e).
- **TrollStore**: opens; the Install IPA File picker opens; ipainbox installed Terraria and Subway Surfers
  (0730 sandbox container answer, 0731 SBLOG logging, 0732 jetsam floor for lsd/installd/pkd/fileproviderd/containermanagerd,
  which fixed the lsd kill and TrollStore registration; the fix2 payload re-registers apps).
- **Files "On My iPhone"** fixed by **0734** (root statfs type name reported as "apfs" to fileproviderd). 0733 (APFS 'J' ioctl
  answers) never fired and is kept as harmless.
- Optional **fplog1** payload makes the FileProvider/DocumentManager logs readable.

Earlier milestones are recorded in [notes/ENGINEERING-LOG.md](notes/ENGINEERING-LOG.md) with their own evidence (boot to
SpringBoard, GPU apps such as Maps/Magnifier/Safari, Wi-Fi auto-join 0790, camera with hardware encoding, audio, haptics
q10hapd6a, GNSS fix); they are not re-stated as verified here.

## What does not work / open
- **5G**: not possible in iOS — capability is keyed on the iPhone X (Mav17) baseband identity.
- **Compass app** is frozen (heading/device motion needs SPU-style sensor fusion) [open].
- **Ambient light** is silent (no Auto-Brightness) [open].
- **Bluetooth** long-run test and airplane-mode boot test [open]; no HFP/SCO voice and no BT HID input.
- **Torch/flash**: not claimed.
- **Memory**: DRAM above the XNU window is not usable as normal RAM; memory pressure kills daemons (0732 mitigates for the
  FileProvider/install daemons).
- **App Store**: genuine download/install does not work (FairPlay); TrollStore/IPA is the path.
- **Second Nord 2**: never tested; per-device pins are zero placeholders here (see docs/INSTALL-NORD2.md).

## What still does not build from this repository, and why
- **The kernel image.** The kernel was produced by a CI build (GitHub Actions of the fork) of Apple's open-source XNU with the
  SWEET/port patches, plus a local integration step that recompiles only the objects touched by pieces 0394+ and links them with
  the CI object archive. That object archive, Apple's XNU source tarball and the Apple/LLVM cross toolchain are not in this
  repository. `xnu/stage-q10jfinal44ts/` is the exact source of the recompiled files (now including `Nord2GPU.cpp`), and
  `xnu/patches/` the pieces; a from-scratch build of the whole kernel from this tree has **not** been attempted.
- **Modem pieces 0260-0276** are withheld (they embedded the author's modem NVRAM); the final line uses the blob-free
  `sweet_nvram_blob.h` in the stage tree, so the final kernel source is complete without them.
- **Per-device pins** (GPT digest/CRC, Wi-Fi calibration digest, sensor calibration) are zero placeholders: a kernel built as is
  refuses Wi-Fi and halts in the two-lane UFS switch until the per-device step writes the target phone's values.
- **GPU userland** builds need the iOS SDK (not redistributable) and a Mesa 25.0.7 tree (apply `components/y2s-stage0/tools-mesa/*.patch`).
- **Build recipes** reference the original work-tree layout (`$NORD2_WORK`) and private build copies; they document how each
  image was made rather than being a turnkey build.

## Layout
| Path | What |
|---|---|
| `xnu/patches/` | every kernel patch of the port (one file per piece, `NNNN-name.patch`), see `xnu/patches/SERIES.md` |
| `xnu/stage-q10jfinal44ts/` | the patched driver/kernel source tree exactly as compiled for the final line |
| `shim/`, `tools/`, `tests/`, `kmod/` | SWEET shim/loader, host tools and tests (from the ios-sweet base, extended) |
| `hwcodec/`, `video/` | hardware codec / camera / GNSS userland and kernel work (VideoToolbox plug-ins, JPEG, decoders) |
| `components/<dir>/` | per-work-item scripts, payload guest scripts, host tests, build recipes and notes, as in the work tree |
| `build/recipes/` | the top-level kernel build/flash chain scripts (historical; reference the original work-tree layout) |
| `host-tools/gpu-tools/` | rootfs/offline-update host tools (fastroot, HFS journal replay, offline Metal update) |
| `gpu/` | GPU userland stack of the work tree (Metal frontend, tegu-derived transport, tests, docs) |
| `notes/` | engineering log (2026-10-14 .. 2026-11-16) and the first GPU morning report |
| `docs/` | install guide, status table, exclusions |

Paths inside scripts were rewritten: `$NORD2_WORK` = the original work tree (`research/nord2/gpu/overnight-20261024/work`),
`$NORD2_REPO` = the ios-sweet checkout, `$HOME` = the author's home. The build chain also needs the CI kernel object archive and
private combiner copies that are not part of this tree: the recipes document how each image was made, they are not a turnkey build.
