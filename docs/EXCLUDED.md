# What was deliberately NOT published, and why

This export was made by an allowlist (text source/script/doc types, <= 2 MB, no binaries), then filtered and scanned with the
port's release sanitizer (`components/release-20261115/tools/sanitize_release.py`) using the owner's private identifier list
(99 needles: IMEI/ICCID/IMSI/serial/MAC/SSID/SSH/calibration digests/owner strings, in ASCII, UTF-16, BCD, raw and hex forms) plus
extra scans (private keys, tokens, e-mail addresses, MAC/IMEI/ICCID patterns, owner account name). Result on the final tree:
**SANITIZE_CLEAN**. Values are never printed by these tools.

## 1. Private data of the author / the author's phone (never published)
- Device dumps and partitions: GPT dumps, persist/nvdata/nvram/protect/proinfo/seccfg/otp images, `oplus_custom`, Android
  cmdline (serial), dmesg/pstore/kallsyms/iomem, `backup/`, `IOS-BACKUP-*`, `scratchpad-backup-*`.
- Modem NVRAM blobs (`N2NVDSK1`, `n2nvdisk*.bin`, `payload-nvfile1*`) and **kernel patches that embedded the author's modem NVRAM
  tree as C arrays** (`0260`-`0276`, `sweet_nvram_blob.h` with contents; the final kernel loads the blob from the phone instead).
- Wi-Fi calibration file (`WIFI`, `WIFI_CUSTOM`) and MAC; Bluetooth address; sensor calibration values.
- **Per-device pins compiled into kernel sources** (Wi-Fi calibration sha256, GPT LBA1..5 sha256, GPT entry-array sha256 + CRC,
  sensor calibration acc/gyro/ps): replaced by zero placeholders in every file of this tree (including files already present in
  the public ios-sweet history, where they were scrubbed by the first commit of this branch).
- Rootfs mirror (`rootfs-mirror.img`) and anything extracted from the phone's iOS root (CommCenter/lockdown/keychain/Wi-Fi
  plists, unified logs, crash reports, app data), SSIDs.
- SSH host keys, the jbts password and `payload-jbts1*` (contain key hashes), `secrets/`, adb keys and rescue images built with
  the author's adb key.
- All logs and captures: `autocycle/runs/`, `cc-txn-*`, `cc-fetch-*`, `ui-usb/`, `*.log`, `fpdiag*`, `fp-boot*`, receipts,
  reports, `ANDROID-IMS-LOG/`, `PARKED-audio-logs-*`.
- Large `owned-*.json` payload ownership lists (> 128 KiB): they enumerate paths of the author's root volume (app container
  UUIDs, user files).
- One host test that contained the author's Bluetooth address (`components/bt-20261115/test/t_addr.c`).
- Shader caches and `*-seeded` payloads (they reflect the owner's app usage); generated pipeline-pack JSON data; GPU build
  directories (`build-*`, `anchor-*` snapshots) and byte-identical copies of the GPU source tree (deduplicated by content).
- Upstream Mesa source trees (`mesa-25.0.7/`, `mesa-src/` ...): only the port's Mesa patches (e.g.
  `components/y2s-stage0/tools-mesa/*.patch`) and the port's own sources are included.
- The engineering log is included as `notes/ENGINEERING-LOG.md` with identity values redacted in place (one SSID occurrence
  replaced by `<redacted-ssid>`; on 2026-11-17 the position of the first GNSS fix was redacted too).
- The wider "resume" scope (2026-11-17) and what is still out by design: [COMPLETENESS.md](COMPLETENESS.md),
  [LARGE-ARTIFACTS.md](LARGE-ARTIFACTS.md).
- Owner host paths were rewritten (`$NORD2_WORK`, `$NORD2_REPO`, `$HOME`, `user`).

## 2. Apple and vendor content (never published; get it yourself)
- iOS IPSW/OTA content, dyld shared caches, anything copied from the iOS root (e.g. `com.apple.*` plists, evidence files),
  iOS SDK private headers (files with "Apple Inc. All rights reserved" and no APSL notice), Apple open-source headers copied into
  a fake root (`fakeroot/`).
- Android/vendor partitions and their extracts (`system-x/`, `vendor-x/`, `vfs/`, `system.img`, `vendor.img`, `super-head.bin`),
  MediaTek firmware (Wi-Fi/BT/GNSS/modem/SCP), GNSS boot data header `n2gpsdl_bootdata.h` (embedded vendor data).
- Kernel images, boot images, ramdisks, ADTs, Mach-O binaries, payload binaries, `.img`/`.qcow2`, ISO/VM images.

## 3. Licence-sensitive material that IS included
At the owner's decision the GPU stack is published: the kernel GPU driver (`Nord2GPU.cpp`, `nord2_gpu_transport.h`,
`sweet_fb_pending_surfaces.h`, patches 0084/0085/0111/0113/0188/0189), the work-tree `gpu/` stack and the GPU userland work
directories. Parts are derived from the private ios-tegu reference sources; see THIRD-PARTY.md.



### 1a. Withheld: files with embedded data blobs (modem NVRAM tree, vendor boot data)

- `gpu/build/kernel-p1-20261026/0260-nord2-modem-fs-server.patch`
- `gpu/build/kernel-p1-20261026/0261-nord2-modem-device-maps-early-rings.patch`
- `gpu/build/kernel-p1-20261026/0262-nord2-modem-nvdisk-fallback.patch`
- `gpu/build/kernel-p1-20261026/0263-nord2-modem-at-client.patch`
- `gpu/build/kernel-p1-20261026/0265-nord2-modem-full-start3s.patch`
- `gpu/build/kernel-p1-20261026/0266-nord2-modem-full-start3s-nopause.patch`
- `gpu/build/kernel-p1-20261026/0267-nord2-modem-full-start10s.patch`
- `gpu/build/kernel-p1-20261026/0268-nord2-modem-one-ring-mapping.patch`
- `gpu/build/kernel-p1-20261026/0269-nord2-modem-ccb-ctrl.patch`
- `gpu/build/kernel-p1-20261026/0270-nord2-modem-at-registration-poll.patch`
- `gpu/build/kernel-p1-20261026/0271-nord2-modem-at-data.patch`
- `gpu/build/kernel-p1-20261026/0272-nord2-modem-persistent-p1-test.patch`
- `gpu/build/kernel-p1-20261026/0273-nord2-modem-daily.patch`
- `gpu/build/kernel-p1-20261026/0274-nord2-modem-dpmaif-datapath.patch`
- `gpu/build/kernel-p1-20261026/0275-nord2-modem-p2t.patch`
- `gpu/build/kernel-p1-20261026/0276-nord2-modem-p2f.patch`
- `gpu/hwcodec/gnss/probe/n2gpsdl_bootdata.h`

### Other exclusion counts (by rule)

```
excluded {'ext': 158480, 'prunedir': 2575, 'denyname': 8047, 'json-not-config': 2410, 'txt-not-doc': 74424, 'prune-component': 3, 'apple-plist': 26, 'notfile': 662, 'empty': 45, 'toobig': 7, 'mesa-upstream': 16419, 'kernel-source-copy': 7989, 'owned-json-large': 23, 'binary': 13, 'vendor-extract': 138, 'skip-explicit': 155, 'dup-content': 9922}
```

