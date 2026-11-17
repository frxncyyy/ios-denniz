# Source export notes

Notes moved here from the first README of this branch (2026-11-16), still valid.

## What still does not build from this repository, and why

- **The kernel image.** The kernel was produced by a CI build (GitHub Actions of the fork) of Apple's open-source XNU
  with the SWEET/port patches, plus a local integration step that recompiles only the objects touched by pieces 0394+
  and links them with the CI object archive. That object archive, Apple's XNU source tarball and the Apple/LLVM cross
  toolchain are not in this repository. `xnu/stage-q10jfinal44ts/` is the exact source of the recompiled files
  (including `Nord2GPU.cpp` and all `wifi_*` sources), and `xnu/patches/` the pieces; a from-scratch build of the whole
  kernel from this tree has **not** been attempted. How to fetch/rebuild the missing inputs:
  [LARGE-ARTIFACTS.md](LARGE-ARTIFACTS.md), [../resume/RESUME.md](../resume/RESUME.md).
- **Modem pieces 0260-0277** are withheld (they embedded the author's modem NVRAM); the final line uses the blob-free
  `sweet_nvram_blob.h` in the stage tree, so the final kernel source is complete without them.
- **Per-device pins** (GPT digest/CRC, Wi-Fi calibration digest, sensor calibration) are zero placeholders: a kernel built
  as is refuses Wi-Fi and halts in the two-lane UFS switch until the per-device step writes the target phone's values.
- **GPU userland** builds need the iOS SDK (not redistributable) and a Mesa 25.0.7 tree (apply
  `components/y2s-stage0/tools-mesa/*.patch`). The current built binaries (gfxall17) are committed.
- **Build recipes** reference the original work-tree layout (`$NORD2_WORK`) and private build copies; they document how
  each image was made rather than being a turnkey build.

## Path rewriting

Paths inside scripts were rewritten during the export: `$NORD2_WORK` = the original work tree
(`research/nord2/gpu/overnight-20261024/work` inside the ios-sweet checkout), `$NORD2_REPO` = the ios-sweet checkout,
`$HOME` = the author's home, `user` / `user@host` = the author's account. The build chain also needs the CI kernel object
archive and private combiner copies that are not part of this tree.

## Status labels used in older docs

- **[verified on one phone]** observed on the author's phone (kernel log over USB, screenshots, or the user using the
  feature).
- **[host]** built/tested on the host only (unit tests, fake-root or VM rehearsal).
- **[open]** known not to work yet.
