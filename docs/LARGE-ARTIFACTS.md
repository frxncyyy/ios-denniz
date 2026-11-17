# Artifacts that are not committed (too big, secret, device-specific or third-party)

The repository is meant to hold everything needed to resume the work on another PC. These artifacts are **not** in
git; for each one: why, its size and sha256 on the author's host (2026-11-17), and how to get or rebuild it.
Paths: `$NORD2_REPO` = the ios-sweet checkout, `$NORD2_WORK` = `$NORD2_REPO/research/nord2/gpu/overnight-20261024/work`,
`B` = `$NORD2_WORK/gpu/build` (see [../resume/RESUME.md](../resume/RESUME.md)).

## Downloads from the original source

| Artifact | Size | sha256 | Get it |
| --- | --- | --- | --- |
| `iPhone10,3,iPhone10,6_16.7.16_20H392_Restore.ipsw` (Apple) | 6 171 035 648 B | `ece1194f7e233e9109c5790a8d23a4564a10a254e15ec5b09978a343265568fb` | `tools/fetch-ipsw.sh` (Apple CDN via the public ipsw.me API, hash checked) |
| OxygenOS stock `boot.img` / `recovery.img` of the supported build (OnePlus) | - | boot `04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01`, recovery `45a8e566822ce28aea1bfec19747609a76882c99b039e9ed5a7103ff5f88bdf4` | official full OTA or your own phone; `tools/fetch-oxygenos.sh verify boot.img recovery.img` (no URL is recorded, so none is guessed) |
| blacktop/darwin-xnu-build @ `663906528d1cd5ea8fde7de6f16de73db9511549` + Apple xnu-8796 (macOS 13.5) | - | git commit pin | `tools/fetch-xnu-build.sh` or the CI workflow `.github/workflows/xnu.yml` |
| Procursus debs, TrollStore 2.1.1, ldid (jbts inputs) | ~33 MB | verified by `build_jbts.py` | names in `components/jbts-20261115/inputs/README-INPUTS.md` |
| Mesa 25.0.7 source | - | upstream tarball | mesa.freedesktop.org; apply `components/y2s-stage0/tools-mesa/*.patch` |
| MediaTek vendor files (BT firmware `soc3_0_ram_*_hdr.bin`, `vendor/lib/libvcodec*.so`, `vpud`, GNSS `libmnl`, modem/SCP firmware) | - | per phone/firmware | from **your** phone's vendor partition (read-only dump, step 4 of docs/INSTALL-NORD2.md) |

## Build outputs (rebuild them)

| Artifact | Size | sha256 | Why not in git | Rebuild |
| --- | --- | --- | --- | --- |
| CI kernel object archive `B/startup-diagnostic/kernel.startup.a` (1191 members) | 140 638 264 B | `08a3190a2e31640510b8ed6262a0e1a04ba21204d9135c3b9b0cf9ea7d388edf` | > 100 MB GitHub limit; compiled with the author's per-device constants in some objects | run the CI workflow on the patch series (`xnu-buildtree` / `xnu-kernel` artifacts), then the local integration of `build/recipes/build_q10jfi.sh` (`components/autotest/build_p1_candidate.py` replaces/links members) |
| Patched XNU source used by the integration step `B/xnu-stage-backup-pre-p15-20261028/xnu-stage` | ~150 MB (text, Apple APSL source + patches) | directory | Apple's open-source XNU, reproducible: it is the CI's patched tree | `xnu-buildtree` artifact of the CI run (`buildtree.tgz` contains `xnu/`) |
| Kernel/boot images `device-mmu-*-candidate/ui-sched/*.img` (every line, e.g. q10jfinal44ts `f55a15ea…`) | ~15 MB each | listed in docs/STATUS.md | embed the author's per-device pins | recipes `components/*/build_q10jfinal*.sh` (needs the archive above), or step 7 for your phone |
| `vcodecd` (hardware codec daemon, payload-vcodec-prod9) | 446 112 B | `1580fbbebcf944540c7872d4d64a5a30f0a4c06188ef1e1d75093d34b5f20fd9` | embeds build-host paths | `components/vcodec-20261114` build scripts (iOS SDK needed) |
| Offline-VM initramfs (`research/nord2/rootfs-vm/initramfs`, busybox/python/hfsprogs + host kernel modules) | ~45 MB, 714 files | directory | host-specific (kernel modules must match the VM kernel); GPL userland | `resume/research-nord2/rootfs-vm/prepare.py` on the new host (see RESUME.md) |
| VM rehearsal images (`*/rehearsal-*/overlay.qcow2`, `delta.bin`, `gfx200-20261114/shipall/vm`) | 20-530 MB | - | per-run scratch, built from the owner's root mirror | rerun the `rehearse*.sh` of each payload |
| GPU userland build trees (`build-*`, `anchor-*`, every `y2s*` iteration's dylibs) | GBs | - | intermediate builds; only the current gfxall17 binaries are committed | `components/gfx200-20261114` / `gpu/` build scripts (iOS SDK + Mesa) |
| Shader corpus `ui-corpus-20261025`, pipeline-pack data, `hardware-20261024` (134 GB), `render-diagnostic-review` | 1.6-134 GB | - | derived from captures of Apple shaders/apps on the owner's phone; huge | regenerate with the corpus tools in `components/` |

## Now committed (2026-11-17, owner decision)

The generic release-builder output is in `components/release-20261115/dist/nord2-ios-release-20261115/` (see its `NOTICE-TEMPLATE.md`):

| File | Size | sha256 |
| --- | ---: | --- |
| `kernel/kernel.tpl` (q10jfinal19, 7 pins = fixed placeholders) | 14 604 336 B | `6dfce9541aeeadc6cee4389f7c5d4362e21d7e7191dfd0a4416bb1827c9e686a` |
| `kernel/pins-map.json` | 726 B | `99322d019b4c4cf775b9e705e020d8a825f973c4fe614c1771a1c8ceecd81c9d` |
| `kernel/template.adt` | 273 324 B | `5f76af4bfe074f576f7622a1e60ebef0230c1109e56e72c8188964931a6244f2` |
| `host-tools/loader-Image` | 26 456 B | `cd060cae19fad9ac048b1a00c6f8530a732a11b91ca009b95dff7d1ac8ca07ee` |
| `host-tools/stub-Image` | 9 193 B | `4dac6db2f648e3559182c17a19fc52414364c633c7f9428803ac1dce40cd1678` |
| `payloads/bt/n2btd` | 53 376 B | `339f32a2603b23b444f98d55b45ed2518f951bb2108b2966345f9b2506818fec` |

Plus `release-manifest.json`, `MANIFEST.sha256`, `README-RECIPIENT.md`, the host tools, `tools/n2dev/`, the sanitizer and
the jbts/bt payload scripts. From that output **not** committed: the jbts input debs and TrollStore (third-party binaries:
fetch and verify with `build_jbts.py`) and `vm/initramfs/` (GPL binaries of the build host: rebuild with `prepare.py`).

## Secrets and device-specific data (never committed)

| Artifact | Why | Regenerate for a new phone |
| --- | --- | --- |
| `jbts-20261115/jbroot-jbts1.tar` (129 484 800 B, `b9b6ad80…`) and `payload-jbts1*` | contain the generated SSH host keys and password | `components/jbts-20261115/build_jbts.py` (generates new keys/password) |
| Rescue image `research/nord2/rescue/nord2-adb-rescue.img` | carries the author's adb key | `tools/nord2-mkrescue.py boot.img recovery.img out.img --adb-key ~/.android/adbkey.pub` |
| Root mirror `B/mirror/rootfs-mirror.img` (~107 GiB volume), `IOS-BACKUP-*` | the owner's iOS data | built by the first `snapshot_current_root_fast.py` run of a transaction (`components/autocycle/backup_ios.sh`) |
| Modem NVRAM blob (`n2nvdisk*.bin`, `payload-nvfile1*`), Wi-Fi `WIFI` calibration, sensor calibration, GPT dumps, BT address | identify the phone | steps 4-6 of docs/INSTALL-NORD2.md (`n2_read_device.sh`, `mk_nvdisk_dev.py`, `mk_nvfile_payload.py`) |
| `release-20261115/private/ids.json`, `release.env` | the owner's identifier list for the sanitizer | `components/release-20261115/tools/collect_private_ids.py` on the new owner's data |
| Logs, captures, receipts (`autocycle/runs/`, `cc-txn-*`, `cc-fetch-*`, `ui-usb/`, `PARKED-*`, `ANDROID-IMS-LOG`) | runtime output of the owner's phone (identifiers, location, networks) | produced again by every test run |

## Third-party apps and content

| Artifact | Why |
| --- | --- |
| `payload-ipainbox-games2` (410 MB: Terraria, Subway Surfers IPAs) | copyrighted apps; bring your own decrypted IPAs (`/var/mobile/Documents/ipa-inbox/`) |
| `*-seeded` payloads (gfxall shader caches) | reflect the owner's app usage; the unseeded variants are committed or rebuildable |
| Patched Apple binaries in payloads (e.g. `payload-locfix2/locationd`) | Apple binaries; the payload scripts and the patch tools are committed, re-create from your own IPSW |
