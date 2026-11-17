# Third-party code in this tree

| Path | Origin | Licence |
|---|---|---|
| `xnu/stage-q10jfinal44ts/**` (XNU files: `bsd/`, `iokit/` headers and sources carrying Apple headers) | Apple XNU (xnu-8792, open source) as modified by ios-sweet and this port | APSL-2.0 (file headers) |
| `xnu/patches/*.patch` | diffs against the XNU/SWEET tree | APSL-2.0 for hunks in XNU files; new files: port author |
| `shim/`, `tools/`, `tests/` | ios-sweet + port additions | ios-sweet licence; port additions by the port author |
| `shim/font.h`, `shim/gen_font.py` | ios-sweet | ios-sweet licence |
| `components/mdp-20261114/upstream/` | Linux kernel `drivers/media/platform/mediatek/mdp3` | GPL-2.0 (headers name MediaTek authors) |
| `components/camera-userland-*/src/`, `components/camprev-20261114/userland/`, `components/camflash-20261114/userland/` | port camera userland based on HakiOS | see `LICENSE.hakios` beside the sources |
| `tools/fixtures/` | test vectors (public specification examples) | test data |
| `gpu/metal/tegu/`, `components/tegu-frontend/`, `components/ios-tegu-compare-20261026/`, kernel GPU driver (`xnu/stage-q10jfinal44ts/iokit/Kernel/arm/Nord2GPU.cpp`, `bsd/dev/nord2_gpu_transport.h`, `iokit/Kernel/arm/sweet_fb_pending_surfaces.h`, `shim/nord2-xnu/Nord2GPU.cpp`, patches 0084/0085/0111/0113/0188/0189) and every other file that mentions `tegu` | **derived from ios-tegu** (private reference sources) | **check the upstream licence**; not verified by this export. Some copies carry a `NOTICE.md`/`LICENSE` (e.g. `components/y2s-stage0/NOTICE.md`, MIT for that project's original work, with APSL/MIT notices for its own third-party parts) — read them, do not assume they cover every file |
| `gpu/metal/tegu/air/`, `components/air-compute-20261028/src*/air/` | AIR (Metal bitcode) -> NIR frontend from hackios-xnu-userland (oosexgang) via Chlorophyll | MIT, per-file SPDX notices + `LICENSE.hackios-userland` / `NOTICE.md` |
| `components/y2s-stage0/tools-mesa/*.patch`, `components/*/hostmesa/`, `components/*/mesa-rel/`, Mesa-derived files of the Metal userland (libredwoodair glue) | Mesa 25.0.7 (and patches to it) | MIT (Mesa; some Mesa files carry other permissive licences in their headers) |
| `components/air-plan-20261028/m2v/` | metal2vulkan | GPL-2.0-or-later or LGPL-3.0-or-later (its `LICENSE*` files) |

Nothing in this tree is a binary (pipeline-pack data, shader caches and build outputs were not copied). No Apple iOS binary, header from the iOS SDK, vendor firmware or Android partition content is
included; private iOS SDK headers were removed by a filter (files carrying "Apple Inc. All rights reserved" without an APSL
notice were excluded).

The GPU userland (Nord2Metal Metal driver, libredwoodair glue around Mesa's Valhall compiler) **is** in this tree as source; the
built `libredwoodair.dylib`/Nord2Metal binaries are not.

## Release kernel template (`components/release-20261115/dist/nord2-ios-release-20261115/`)
- `kernel/kernel.tpl`: built from Apple's open-source XNU (xnu-8796; Apple Public Source License 2.0) plus this port's
  patches (`xnu/patches/`, `xnu/stage-q10jfinal44ts/`; the XNU version is pinned in `.github/workflows/xnu.yml`). The CI
  build may link Apple-provided kernel support libraries; **check the upstream licences** (not verified here).
- `kernel/template.adt`: contains the iPhone X `/product` device-tree node data (Apple-originated); **check upstream licence**.
- Details: `components/release-20261115/dist/nord2-ios-release-20261115/NOTICE-TEMPLATE.md`.
