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

Nothing in this tree is a binary. No Apple iOS binary, header from the iOS SDK, vendor firmware or Android partition content is
included; private iOS SDK headers were removed by a filter (files carrying "Apple Inc. All rights reserved" without an APSL
notice were excluded).

The GPU userland (Nord2Metal driver, `libredwoodair.dylib` with Mesa's Valhall compiler) and kernel files derived from private
reference material are **not** in this tree; see docs/EXCLUDED.md.
