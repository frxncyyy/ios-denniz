# Completeness audit (2026-11-17)

Question: does this repository contain everything of the port that can be published, and enough to resume the
work on another PC? Short answer: **all of the port's own source, scripts, notes and recipe inputs that are not
device-specific, secret or third-party are now in the tree**. An audit found files that the first export had
dropped **by accident**; they were added in the commits of 2026-11-17. What is still missing is missing **by design**
(personal/device data, Apple/vendor files, build outputs over the size limit) and is listed below with how to get
or rebuild it. **The kernel still cannot be rebuilt from git alone**: it needs a CI run (macOS runner) to produce the
object archive.

## How it was checked

1. **Independent content-hash pass** over the original work tree (`$NORD2_WORK`, ~380 GB) and over
   `research/nord2` outside it: every text/source-like file (the export's extension allowlist, <= 2 MB, not binary),
   skipping only `runs/`, `cc-txn-*`, `cc-fetch-*`, `mirror/`, `secrets/`, `private/`, `IOS-BACKUP*`, `backup/`, `ipsw/`,
   `dist/`, `overlay*`, `offline/`, `scratchpad-backup*` (the latter re-checked separately). Each file was looked up in
   the repository **by sha256 of its content**, raw and after the export's path/pin rewrite (paths changed during the
   export, so paths cannot be compared).
2. Every file not found was classified by the export's own rules (allowlist, name filter, component selection,
   vendor/Apple/blob rules). Files with no valid reason were "missing by accident".
3. Specific checks: kernel piece numbers named in the log and docs vs `xnu/patches/`; every `txn_*.sh` /
   `build_q10jfinal*.sh` named in the docs; the final stage tree vs the original; every command path in
   `docs/INSTALL-NORD2.md`.
4. A separate inventory of non-text files (payloads, binaries, images) for the "resume from another PC" scope.

## Numbers

Work tree (`$NORD2_WORK`), text/source-like files: **334 068**.

| Component | in repo before | added 2026-11-17 | left out by design |
| --- | ---: | ---: | ---: |
| `gpu/build/*` (all work items, incl. the final stage tree) | 122 269 | 4 993 | 202 003 |
| `gpu/hwcodec` | 975 | 872 | 247 |
| `gpu/video` + `gpu/videotest` | 362 | 153 | 516 |
| `gpu/metal`, `src`, `include`, `tests`, `docs`, `y2s`, `tools`, `out` | 633 | 1 | 66 |
| `tools`, `shim`, `tests`, `docs`, `xnu` (work-tree copies) | 730 | 0 | 1 |
| `.claude` agent briefs, top-level files, `research` | 5 | 6 | 232 |
| **Total** | **124 977** | **6 025** | **203 066** |

`research/nord2` outside the work tree (early bring-up, 2026-10-20..24): 101 000 text files, of which 86 770 are
copies of the XNU source tree (in the fork's CI history) and ~1 700 logs; **4 459 notes/tools/manifests were added**
under `resume/research-nord2/`.

What the 203 066 "by design" files are:

| Reason | Files | Notes |
| --- | ---: | --- |
| Not selected work dirs: shader corpus `ui-corpus-20261025` (27 491), XNU source copies `xnu*`, `patchtest`, `xnu-obj` (~21 000), `apple-toolchain` (5 158), Python venvs, per-line `device-mmu-*-candidate` stage copies (~130 each) | 95 483 | regenerable or third-party; the per-line stage trees are rebuilt by the recipes; the final one is in `xnu/stage-q10jfinal44ts/` |
| `.txt` that are logs or generated lists (`misses.txt` 64 859, `pso-known-good-*.txt` 5 969, Mesa CI lists) | ~73 600 | generated test data and logs |
| Upstream Mesa trees | 16 286 | only the port's patches are kept |
| Build output dirs (`build/`, `dsh*`) | 15 746 | generated |
| Name filter, true positives (logs, receipts, `verified.json`, dmesg/pstore dumps) | ~640 | runtime output of the owner's phone |
| Generated JSON (rehearsal journals, cargo fingerprints, large catalogs) | ~1 150 | generated |
| Vendor extracts, Apple private headers / plists, blobs, large `owned-*.json` | ~250 | see docs/EXCLUDED.md |

## Missing by accident (found and fixed)

The export's file-name deny filter was too broad. `^WIFI` (meant for the Wi-Fi calibration file) is case-insensitive,
so it matched **every file whose name starts with `wifi`**; `capture`, `snapshot`, `ulog` and `cmdline` matched source
files too. Some subdirectories were also pruned without a reason. Added on 2026-11-17:

| What | Files |
| --- | ---: |
| Final kernel stage tree: 104 `wifi_*.c/.h` + 2 `wifi_ios_*.cpp` + `bsd/conf/files` (+103 files already present elsewhere, now also under the stage path) | 210 |
| Kernel pieces with no file in `xnu/patches/`: **0320** (mic capture, name filter), **0640** (overlay snapshot, name filter), **0278**, **0280** (call audio), **0284** (SCP sensors) from the 11-06 scratchpad | 5 |
| `wifi*`/`snapshot`/`capture`/`ulog`-named sources elsewhere (unique contents) | 300 |
| `video/oracle`, `video/research`, `video/vectors` tools | 31 |
| GPU work dirs of 2026-10-25 (`hardware-20261025`, `deepseek-parallel-20261025`, `lowering-claude-20261025`) | 3 453 |
| Call-audio, HAL plug-in, sensor, slider sources from `scratchpad-backup-20261106` (code only) | 153 |
| Recipe inputs and data: `.txt` lists (e.g. `scanpath_extra*.txt`, `stages.txt`) and `.json` contracts/catalogs/plans | 1 978 |
| Agent briefs (`.claude/`), work-tree README | 6 |
| research/nord2 early-phase notes and tools | 4 459 |

## Specific checks

1. **Kernel piece numbers** named in the engineering log, STATUS, README and INSTALL: 204 distinct numbers; all have a
   file in `xnu/patches/` except: **0260-0276** (withheld, embedded the author's modem NVRAM), **0277** (baseband bridge,
   2.1 MB, embeds the modem NVRAM tree: withheld), and numbers that never existed as separate files locally (0412 "not
   done", 0510 "no kernel patch", 0654/0657/0661/0662/0669/0723: folded into other pieces or only planned). Several
   matches were file modes (0600/0644/0666/0755), not pieces. The withheld code of 0260-0277 is not needed for the final
   line: its kernel loads the modem blob from the phone (`sweet_nvram_blob.h` in the stage tree has no contents).
2. **Scripts named in the docs**: all 77 `txn_*.sh` / `build_q10jfinal*.sh` referenced exist in the tree.
3. **Final stage tree** `xnu/stage-q10jfinal44ts/`: now 465 files = the original 466 minus one `.nord2orig` backup. Files
   the recipe needs that are **not** in git: the CI object archive (`kernel.startup.a`, 140 MB, over GitHub's limit and
   built with per-device constants), the CI-patched XNU tree used for the compile flags, the Apple/LLVM cross tools, and
   the withheld modem pieces (not needed, see 1).
4. **Install guide paths**: every command path in `docs/INSTALL-NORD2.md` exists. The guide now also states that
   `tools/nord2-install-root.py` checks the author's frozen root hashes (constants to update for your own root).
5. **Privacy**: one real GNSS position (the first fix of 2026-11-14) was in the engineering log; it was redacted. If
   an earlier version of this branch was already pushed, that line is still in the published history.

## Item table (resume scope)

| Item | In repo | Regenerate / get it | Reason |
| --- | --- | --- | --- |
| Port kernel pieces `xnu/patches/` | yes (except 0260-0277) | - | 0260-0277 embed the owner's modem NVRAM |
| Final kernel source `xnu/stage-q10jfinal44ts/` | yes (pins = zero placeholders) | your pins: step 7 of the install guide | per-device constants removed |
| Recipes, payload scripts, guests, rehearsal scripts, host tests | yes | - | - |
| CI workflow `.github/workflows/xnu.yml` | yes | run it on a fork | - |
| CI object archive, patched XNU tree | no | CI run (`xnu-kernel`, `xnu-buildtree` artifacts) | > 100 MB; contains pins |
| Kernel/boot images of every line | no | recipes or step 7 | embed the owner's pins |
| Release kernel template (`kernel.tpl`, `pins-map.json`, `template.adt`) | no | `make_release.sh` | owner decision pending (Apple-built kernel code, Apple device-tree data); hashes in LARGE-ARTIFACTS |
| GPU userland binaries gfxall17 (`Nord2Metal`, `libredwoodair.dylib`) | yes | `components/gfx200-20261114` (iOS SDK) | - |
| n2btd, n2simcache, n2mnld, n2locbridge, Slim VideoToolbox plug-ins, camera/audio boot packages | yes | sources in `components/` | - |
| vcodecd binary | no | `components/vcodec-20261114` | embeds build-host paths |
| Seeded payloads, shader caches | no | rebuild unseeded | reflect the owner's usage |
| jbroot tar, jbts payload, SSH keys | no | `build_jbts.py` | secrets (host keys, password) |
| Decrypted game IPAs | no | your own | third-party copyright |
| VM initramfs | no | `resume/research-nord2/rootfs-vm/prepare.py` | host-specific, GPL binaries |
| Rescue image | no | `tools/nord2-mkrescue.py` | carries the owner's adb key |
| Root mirror, backups | no | first transaction snapshot | the owner's data |
| Modem blob, Wi-Fi/sensor calibration, GPT, BT address | no | `n2dev/` tools | identify the phone |
| iOS IPSW, OxygenOS, MediaTek firmware/libs | no | `tools/fetch-*.sh`, your phone | Apple / vendor copyright |
| Logs, captures, receipts | no | every test run | runtime data of the owner's phone |
| Engineering log, agent briefs, assistant memory | yes (redacted) | - | - |

## The 30 most important things not in git

By design (privacy/secrets): 1 rescue image with the owner's adb key; 2 root mirror and iOS backups; 3 modem NVRAM
blob + nvfile payload; 4 Wi-Fi calibration + MAC; 5 sensor calibration; 6 GPT/LP dumps; 7 BT address; 8 jbts SSH host
keys/password and `jbroot-jbts1.tar`; 9 the sanitizer's private identifier list; 10 all phone logs, captures and
receipts; 11 kernel pieces 0260-0277 (embedded NVRAM).
By design (Apple/vendor/third-party): 12 iOS IPSW and the root image; 13 OxygenOS boot/recovery/vendor; 14 MediaTek
BT firmware, codec libraries, `vpud`, GNSS `libmnl`, modem/SCP firmware; 15 patched Apple daemons in payloads
(`locationd`); 16 Apple private headers / `fakeroot`; 17 decrypted game IPAs; 18 upstream Mesa trees.
Build outputs / too big: 19 CI object archive `kernel.startup.a`; 20 CI-patched XNU tree; 21 kernel and boot images
of every line; 22 release kernel template + template ADT (owner decision); 23 vcodecd binary; 24 VM initramfs; 25 VM
rehearsal images; 26 seeded GPU payloads and shader caches; 27 historical GPU dylib builds (`y2s*` iterations);
28 shader corpus `ui-corpus` and `hardware-20261024` captures; 29 per-line `device-mmu-*-candidate` stage copies;
30 the Apple/LLVM cross toolchain used for the integration step.

## Does anything block building or installing?

- **Building the kernel**: yes, partly. The local integration step needs the CI object archive and the CI-patched XNU
  tree: both come from running the CI workflow (a GitHub macOS runner), not from git. A from-scratch build of the
  whole kernel from this tree has not been attempted.
- **Building userland**: needs the iOS SDK (not redistributable) and Mesa 25.0.7; the current binaries are committed.
- **Installing on another phone**: steps 0-6 of the install guide work from git; step 7 needs a kernel template; the
  port's root overlay is not packaged (step 8). The second-phone flow has never been tested.
