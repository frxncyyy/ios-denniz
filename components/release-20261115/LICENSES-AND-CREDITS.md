# Licences, redistribution limits and credits / Licenze, limiti di ridistribuzione e crediti

This release ships only the port's own tools, a kernel *template* and generic payloads. Everything below marked
**NOT SHIPPED** must come from the recipient's own phone or from the original vendor download.
Questa release contiene solo gli strumenti del port, un *modello* di kernel e payload generici. Tutto ciò che è marcato
**NON INCLUSO** va preso dal proprio telefono o dal download originale del produttore.

## Not shipped / Non incluso (obtain yourself / da procurarsi da sé)

| Item | Owner / licence | How the recipient gets it |
|---|---|---|
| iOS 16.7.16 (20H392) for iPhone10,3/10,6: OS.dmg, SystemOS.dmg, AppOS.dmg (the whole iOS root filesystem, dyld caches, frameworks, apps) | Apple Inc., proprietary (iOS software licence) | download the IPSW yourself from Apple's update CDN (the file name/URL is listed by public IPSW indexes); build the root with `host-tools/mkiosroot.sh` |
| Apple-derived binary patches | Apple originals | shipped only as patches/scripts that need your own IPSW extraction (no Apple binary is in this release) |
| Stock OxygenOS boot.img / recovery.img (Linux kernel, DTB, AOSP init/adbd/toybox used by the rescue) | OnePlus/OPLUS; Linux kernel GPL-2.0 (OnePlus publishes the source); AOSP components Apache-2.0/BSD | extract from the official full OTA of the supported build (`payload.bin`, e.g. with payload-dumper-go); check sha256 against `release-manifest.json` |
| Vendor firmware: Wi-Fi/BT (`soc3_0_ram_*_hdr.bin`), modem (`md1img`), SCP sensor hub, audio parameters, `/vendor/etc/md*` modem model files | MediaTek / OPLUS, proprietary | never copied by the release: the kernel reads Wi-Fi/BT/modem firmware from **your** vendor/md1img partitions at boot; for the BT payload and the modem blob tree, copy the files from **your** vendor image (same OxygenOS build) |
| Your phone's NVRAM (IMEI, RF calibration), Wi-Fi calibration + MAC, sensor calibration, GPT | yours | read-only dump of your own phone (`tools/n2dev/n2_read_device.sh`) |

## Shipped components and their licences / Componenti inclusi

- **XNU kernel (modified)**: Apple Public Source License 2.0. The APSL requires that the source of modifications to Covered Code
  that is deployed externally be made available: the release owner must publish (or offer) the port's XNU patch series together
  with the kernel template. *Owner action required before sharing.*
- **Port tools** (`tools/`, `tests/`, `host-tools/*.py` written for this port): the port author's own work.
- **Offline VM initramfs** (`vm/initramfs`): Linux userland (busybox/python/hfsprogs and others) under their own licences
  (GPL-2.0 and others): provide the corresponding source or a written offer when sharing.
- **jbts** (rootless jailbreak userland, built by `payloads/jbts/build_jbts.py` from signed upstream .debs):
  Procursus (ProcursusTeam; licence per package, https://github.com/ProcursusTeam/Procursus), Sileo (Sileo Team, BSD-4-Clause),
  OpenSSH (BSD), TrollStore and the ldid fork by opa334 (MIT; https://github.com/opa334/TrollStore). Shipped unmodified.
  Your SSH host keys and password are generated on **your** computer by `build_jbts.py` (never the owner's).
- **HakiOS / hackios** by oosexgang (https://github.com/oosexgang/hakios-xnu-kernel, hackios-xnu-userland; licences per
  directory, see their NOTICE / MIT where stated): ideas and the audio HAL plugin base are credited; nothing was copied verbatim
  into the jbts scripts.
- **Mesa** (MIT; https://mesa3d.org): the arch9 (Mali Valhall) compiler pieces linked into the GPU userland library.
- **ios-sweet** (upstream of this port's repository): credited as the base of the SWEET boot shim/loader.

## Owner decision items (not resolved by the tooling)
- GPU userland (`payload-gfxall17`: Nord2Metal, libredwoodair.dylib) was built with private reference material (ios-tegu);
  it is **disabled in `tools/release-files.txt`** until the owner confirms it may be redistributed.
- APSL source publication for the modified XNU (see above).
