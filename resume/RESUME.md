# Resuming the Nord 2 port on another PC

This runbook tells you how to recreate the author's working environment from this repository, rebuild what
was built, and continue testing on a phone. It complements [../docs/INSTALL-NORD2.md](../docs/INSTALL-NORD2.md)
(fresh install on another phone), [../docs/COMPLETENESS.md](../docs/COMPLETENESS.md) (what is and is not in git) and
[../docs/LARGE-ARTIFACTS.md](../docs/LARGE-ARTIFACTS.md) (what must be fetched or rebuilt).

> Honest limits: the scripts were written for the author's host and phone and have never been run from a fresh
> clone. Many recipes reference absolute work-tree locations through `$NORD2_WORK` / `$NORD2_REPO`; recreate that
> layout instead of editing them. Everything that identifies the author's phone was removed and must be
> regenerated from **your** phone (section 6).

## 1. Host environment (as used, 2026-11)

Arch Linux, kernel 7.2.x, x86_64 with KVM. Versions on the author's host (`pacman -Q`):

| Tool | Version | Used by |
| --- | --- | --- |
| python | 3.14.9 | every tool and recipe (stdlib only, plus `capstone`/`unicorn` for a few RE scripts) |
| clang / llvm / lld (incl. `ld64.lld`) | 23.1.4 | host tests, Mach-O links of the integration step |
| gcc | 16.2.1 | host tests |
| qemu-base + qemu-system-x86 | 11.1.3 | offline root edits and payload rehearsals (`generic_update_offline.py`, `rehearse*.sh`) |
| e2fsprogs, cpio, dtc, jq, rsync, git, cmake, meson, ninja, ffmpeg, rust | 1.47.4, 2.15, 1.8.1, 1.8.2, 3.5.2, 2.56.0, 4.4.5, 1.12.1, 1.13.2, 9.0.2, 1.99.1 | packing, device trees, codec oracles, the AIR host tools |
| Android platform-tools (adb, fastboot) | SDK copy in `~/Android/Sdk/platform-tools` | scripts call `$HOME/Android/Sdk/platform-tools/adb` |
| zopfli, hfsprogs (`mkfs/fsck.hfsplus`), apfs-fuse, busybox, payload-dumper-go | built locally (not from pacman) | boot image packing, the VM initramfs, root image build |
| An OpenHarmony/LLVM `llvm-ar` | path in `components/autotest/build_p1_candidate.py` (`AR=`) | archive member replacement; any recent `llvm-ar` works, edit `AR` |
| iOS SDK (not redistributable) | - | building the GPU userland, vcodecd, n2btd and other iOS binaries |
| macOS 14 runner with Xcode 15.x | GitHub Actions | the XNU kernel build (`.github/workflows/xnu.yml`) |

## 2. Directory layout to recreate

```
$NORD2_REPO = <your ios-sweet checkout>                 (this repository's base; contains tools/, shim/, xnu/)
$NORD2_WORK = $NORD2_REPO/research/nord2/gpu/overnight-20261024/work
B           = $NORD2_WORK/gpu/build                     (one folder per work item = components/<name>/ here)
```
Mapping from this repository back to the work tree:

| In this repository | Original location |
| --- | --- |
| `components/<dir>/` | `B/<dir>/` |
| `build/recipes/*` | `B/*` (top-level chain scripts) |
| `xnu/patches/NNNN-*.patch` | the `piece-NNNN/` or patch directory named in `xnu/patches/SERIES.md` |
| `xnu/stage-q10jfinal44ts/` | `B/device-mmu-q10jfinal44ts-candidate/k/kernel/stage/` |
| `hwcodec/`, `video/`, `gpu/`, `host-tools/gpu-tools/` | `$NORD2_WORK/gpu/{hwcodec,video,…}`, `$NORD2_WORK/gpu/tools` |
| `xnu/work-notes/` | `$NORD2_WORK/xnu` |
| `resume/research-nord2/` | `$NORD2_REPO/research/nord2/*` (outside the GPU tree) |
| `resume/agents/claude/` | `$NORD2_WORK/.claude/` (agent briefs, CLAUDE.md) |
| `resume/work-top/`, `notes/ENGINEERING-LOG.md` | `$NORD2_WORK/README.md`, `$NORD2_WORK/NOTES.md` |

Recreate it with symlinks or copies, e.g.:
```sh
export NORD2_REPO=~/ios-sweet NORD2_WORK=$NORD2_REPO/research/nord2/gpu/overnight-20261024/work
mkdir -p $NORD2_WORK/gpu/build
cp -a components/. $NORD2_WORK/gpu/build/ && cp -a build/recipes/. $NORD2_WORK/gpu/build/
cp -a hwcodec video gpu $NORD2_WORK/gpu/ ; cp -a host-tools/gpu-tools $NORD2_WORK/gpu/tools
cp -a resume/research-nord2/. $NORD2_REPO/research/nord2/
```
Paths written as `$HOME`, `user` or `user@host` in scripts were the author's home and account; replace if needed.

## 3. Rebuilding, in order

1. **Inputs**: `tools/fetch-ipsw.sh`, `tools/fetch-oxygenos.sh verify …`, `tools/fetch-xnu-build.sh`; jbts inputs per
   `components/jbts-20261115/inputs/README-INPUTS.md`; Mesa 25.0.7 + `components/y2s-stage0/tools-mesa/*.patch`.
2. **iOS root**: `tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ios-root.img` (then the 107 GiB grow, see
   `components/storage-20261114`).
3. **CI kernel** (macOS runner): run `.github/workflows/xnu.yml` on the patch series; download `xnu-kernel` and
   `xnu-buildtree`. This gives the object archive (`kernel.startup.a`) and the patched XNU tree that the local
   integration uses (see LARGE-ARTIFACTS for the exact files the author used).
4. **Kernel lines** (local integration on Linux): base builder `build/recipes/build_q10jfi.sh` →
   `components/gpusched-20261114/fin3/build_q10jfinal3.sh` → per-line recipes `components/*/build_q10jfinal*.sh`.
   The final line: `bash components/jbts-20261115/build_q10jfinal44ts.sh` (it lists every piece directory it uses;
   `xnu/patches/SERIES-q10jfinal44ts.tsv` records how each piece entered). Expect `FAILURES none`, `BUILD_EXIT 1`
   (the known 0399 image-check stop), `SCANPATH_OK`, `WIFI_MEMBERS_OK`. Kernel history and hashes: docs/STATUS.md.
5. **Per-device values**: the kernel must carry **your** phone's pins: either rebuild the recipe after writing your
   values (the sources hold zero placeholders), or use the published q10jfinal19 template in `components/release-20261115/dist/nord2-ios-release-20261115/kernel/`
   (or a newer one from `components/release-20261115/tools/make_release.sh`) and apply your profile
   (`n2_kernel_pins.py apply`, step 7 of the install guide).
6. **Userland payloads**: GPU `components/gfx200-20261114/shipall/` (gfxall17; built binaries are committed),
   codecs `components/vcodec-20261114` + `components/apps-20261104/payload-vcodec-prod9`, GNSS
   `components/gnss-20261114/pl22` (gnssfix22), Bluetooth `components/bt-20261115/pl` (bt1), SIM
   `components/sim-20261115/simcache`, jbts `components/jbts-20261115` (build_jbts.py, ipainbox-fix2, fplog1), haptics
   / flash / status bar / heat (`final3`, `camflash`, `sbshift2-22`, `heat1`). The order used on the phone is in
   docs/INSTALL-NORD2.md step 9.

## 4. VM rehearsal and a transaction

- Build the offline-VM initramfs once on the new host: `python3 $NORD2_REPO/research/nord2/rootfs-vm/prepare.py`
  (copies busybox, python, hfsprogs and the host's 9p/hfsplus modules into `rootfs-vm/initramfs`).
- Rehearse a payload before touching the phone: each payload folder has `rehearse*.sh` / `rehearse_vm.sh` (VM round
  trip install → no-op → undo → no-op, catalog check `outside_owned_paths []`).
- One transaction = `txn_*.sh KERNEL_IMG TAG` (e.g. `VARIANT=gfxall17 READS=600
  components/gfx200-20261114/shipall/txn_gfxall.sh <img> <new-tag>`; `UNDO=1` to revert). It reaches fastboot,
  flashes the rescue to `boot`, snapshots the root into the mirror, edits it offline in the VM, commits only the
  changed chunks, reflashes the iOS boot image and captures the USB log for `READS` seconds. The generic version for
  other phones is `components/release-20261115/tools/n2dev/n2_txn.sh` (`~/.config/nord2/txn.env`).
- Kernel-only change: `components/autocycle/flash_boot.sh <img>` with the phone already in fastboot.

## 5. Phone procedure rules (learned the hard way)

- Run `components/autotest/phone.py reset fastboot` before a transaction; one log reader at a time; a new TAG for
  every transaction; never reuse an output directory.
- **Screenshot before every tap or swipe**; no fixed-coordinate scenarios (`resume/claude-memory/`).
- One heavy job at a time on the host; keep >15 GB free; builds may run without the CPU quota, the thermal guard stays.
- Never boot stock Android over the iOS `userdata`; never write bootloaders, modem, persist, nvdata/nvram, protect, GPT.
- Keep a known-good kernel image and the rescue image at hand; every payload has an `-undo` variant.
- Full list: `resume/claude-memory/nord2-phone-test-procedure.md`.

## 6. Device-specific: regenerate from the new phone

| Item | Script |
| --- | --- |
| Rescue image with your adb key | `tools/nord2-mkrescue.py` |
| Read-only dump (NVRAM, Wi-Fi/sensor calibration, GPT, LP) | `components/release-20261115/tools/n2dev/n2_read_device.sh` |
| Profile (GPT digest/CRC, Wi-Fi digest, sensor calibration, layout verdict) | `n2_device_profile.py` |
| Modem NVRAM blob + nvfile payload | `mk_nvdisk_dev.py`, `n2_check_nvdisk.py`, `mk_nvfile_payload.py` |
| Kernel pins + device ADT + boot image | `n2_kernel_pins.py`, `mk_device_adt.py`, `n2_pack_device.sh` |
| BT firmware into payload-bt1; BT address | copy from your vendor image; n2btd generates the address on first run |
| SSH host keys / password (jbts) | `build_jbts.py` |
| Root mirror | first transaction snapshot / `components/autocycle/backup_ios.sh` |
| Sanitizer needle list before sharing anything | `components/release-20261115/tools/collect_private_ids.py` + `sanitize_release.py` |

## 7. Assistant context

`resume/claude-memory/` holds the assistant's standing rules and project history; `resume/agents/claude/` the agent
briefs used in the work tree; `notes/ENGINEERING-LOG.md` is the full log (newest entries at the end for November,
reverse order for October).
