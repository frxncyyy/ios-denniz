# Licences and credits

This tree contains the port's own source plus modified files of open-source projects. It contains **no** Apple iOS software, no
OnePlus/MediaTek firmware and no device data. See [THIRD-PARTY.md](THIRD-PARTY.md) for the per-directory licence notes and
[docs/EXCLUDED.md](docs/EXCLUDED.md) for what was withheld.

## Licences of the code in this tree
- **XNU kernel modifications** (`xnu/patches/`, `xnu/stage-q10jfinal44ts/`, `shim/nord2-xnu/`): files derived from Apple's XNU are
  under the **Apple Public Source License 2.0** (APSL-2.0, see the headers). The APSL requires that the source of modifications to
  Covered Code that are deployed externally be made available; this tree is that publication for the pieces it contains.
  Kernel files withheld for the licence decision (docs/EXCLUDED.md) are not covered by this publication yet.
- **SWEET shim/loader and tools** (`shim/`, `tools/`, `tests/`, `kmod/`): from ios-sweet (upstream of this repository, see its
  licence) and the port author's additions.
- **Port tools, payload scripts, host tests** (`components/`, `hwcodec/`, `video/`, `host-tools/`, `build/`): the port author's
  own work, except files that carry their own licence header (kept unchanged).
- **MediaTek MDP3 reference headers** (`components/mdp-20261114/upstream/`): copied from the Linux kernel (GPL-2.0, MediaTek
  authors named in the headers); used as register reference.
- **Camera userland** (`components/camera-userland-*`, `components/camprev-*`, `components/camflash-*`): contains code based on
  HakiOS (see `LICENSE.hakios` next to the sources).

## Not in this tree (obtain yourself from the original source)
| Item | Owner / licence | How to get it |
|---|---|---|
| iOS 16.7.x for iPhone10,3/10,6 (root filesystem, dyld caches, frameworks, apps) | Apple Inc., proprietary | your own IPSW download; root built with `tools/mkiosroot.sh` |
| Stock OxygenOS boot/recovery/vendor (Linux kernel, DTB, AOSP init/adbd/toybox for the rescue) | OnePlus/OPLUS; Linux GPL-2.0; AOSP Apache-2.0/BSD | official full OTA of the supported build |
| Vendor firmware: Wi-Fi/BT (`soc3_0_ram_*_hdr.bin`), modem (`md1img`), SCP sensor hub, GNSS, audio parameters, `/vendor/etc/md*` | MediaTek / OPLUS, proprietary | read by the kernel from your own partitions at boot; copied by you from your own vendor image where a payload needs it |
| Your phone's NVRAM (IMEI, RF calibration), Wi-Fi calibration + MAC, sensor calibration, GPT | yours | read-only dump of your own phone (`components/release-20261115/tools/n2dev/n2_read_device.sh`) |
| jbts inputs: Procursus .debs, Sileo, OpenSSH, TrollStore 2.1.1, ldid | ProcursusTeam (per package), Sileo Team (BSD-4-Clause), OpenSSH (BSD), opa334 (MIT) | download yourself; `build_jbts.py` verifies signatures/hashes |
| Offline VM initramfs (busybox/python/hfsprogs...) | GPL-2.0 and others | built by the release tooling; provide source/offer when sharing |

## Credits
- **ios-sweet** (dipa-studio) — SWEET boot shim/loader this port is built on.
- **HakiOS / hackios** by oosexgang (hakios-xnu-kernel, hackios-xnu-userland, hakios-mt6768) — ideas and reference for the audio
  HAL plug-in base, the MediaTek Bluetooth transport (0721/0722 derive from its APSL kernel files, credited in the sources) and
  the camera userland base.
- **Mesa** (MIT) — the Valhall (arch9) compiler used by the GPU userland (the userland itself is not in this tree).
- **MediaTek** GPL Linux drivers (MiCode/MTK_kernel_modules connectivity/conninfra, mtk-mdp3, mtk_nanohub) and **OPLUS** kernel
  sources — read as hardware references.
- **DenialOfGravity / ios-redfin** — description used for the HFS 'J' fsctl answer (0733, own code).
- Procursus, Sileo, OpenSSH, TrollStore (opa334) — jbts userland, used unmodified.
