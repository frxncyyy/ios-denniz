# Nord 2 iOS port: release plan for ANOTHER person's Nord 2 (2026-11-15, host only)

Owner-side document (stays with the owner; the recipient gets README-RECIPIENT.md + LICENSES-AND-CREDITS.md inside the release).
[V] verified on the host, [D] designed only, [B] blocked (needs a second Nord 2). No phone was touched for this work.

## 1. Verdict
- The current install set is **not shareable as is**: the kernel image, the ADT, the rescue/base ramdisk, the modem blob, the
  jbts payload and the rootfs mirror all carry THIS phone's or THIS owner's data (section 3).
- More important than leakage: **the current kernel would not even boot on another Nord 2**. It pins (as compiled constants)
  the sha256 of THIS phone's GPT (LBA1..5) for the live two-lane UFS switch; with `nord2ufs2live=1` a mismatch HALTS before root
  (manual fastboot recovery). Partition unique GUIDs in this phone's GPT are random v4 (58 entries, 58 distinct) [V], so every
  unit's GPT differs. It also pins this phone's Wi-Fi calibration file hash (Wi-Fi would refuse the other phone's own
  calibration) and the GPT entry-array hash + CRC in the vendor read path (Wi-Fi/BT firmware source).
- Implemented: a per-device pipeline (read-only dump -> profile -> own modem blob -> own ADT -> kernel pins patched with the
  phone's own values -> boot image) plus an owner-side release builder with a sanitizer that fails on any owner identifier.
  Built release: `dist/nord2-ios-release-20261115/` (94 MB, SANITIZE_CLEAN with the owner's 99 identifier needles, 862 files).
- Not yet shippable to a recipient as a complete install: the **iOS root filesystem overlay** (the port's changes on top of a
  pristine IPSW root) is not packaged [D] (section 7), and the GPU userland is held back pending a licence decision.

## 2. Classification rules
- generic (ship): same bytes for every Nord 2 on the supported OxygenOS build and free of owner data (proven by the sanitizer).
- device-specific (regenerate on the target from ITS OWN partitions): IMEI/NVRAM/modem RF calibration, Wi-Fi calibration +
  MAC, sensor calibration, GPT/LP pins, BT address, SSH host keys/passwords, CommCenter/lockdown/keychain/activation data,
  Wi-Fi networks.
- this-user-private (never ship): everything of the owner's phone, logs, mirrors, backups, host account strings, secrets.
- vendor/Apple content (never ship; recipient extracts from own phone/downloads): IPSW, OTA images, vendor firmware.

## 3. Inventory (install/boot set) and identifier scan
Identifier list: `private/ids.json` (mode 600) built by `tools/collect_private_ids.py` (wrapper `private/collect.sh`) from the
owner's own sources: classes iccid 2, imei 1, imsi 1, mac_wifi 1, mac_bt 1, serial 1, ssid 3, ssh_pub 3, ssh_secret 7,
sensor_cal 3, wifi_cal_sha256 1, gpt_fingerprint 2, nv_identity_sha256 6, owner_string 3 (values never printed).
Note: only one IMEI was found in the owner's sources (CommCenter device_specific plist); the second SIM slot's IMEI lives only
in the NVRAM blob (covered by the blob/identity-file checks, not as a text needle).

| Artifact | Class | Evidence (paths/classes/counts only) | Action |
|---|---|---|---|
| Boot image `device-mmu-q10jfinal19-candidate/ui-sched/*.img` (and every earlier image) | device-specific + private | sanitizer: device ADT props x2, mac_wifi x2, sensor_cal x3, wifi_cal_sha256 x1, owner_string x2 (kernel build path + adb key comment in the ramdisk) | never ship; recipient packs own image (n2_pack_device.sh) |
| Kernel Mach-O (unstripped 1175 build-path strings; stripped 1) | device-specific + private | wifi_cal_sha256, gpt_fingerprint x2, sensor_cal x3, owner_string | ship only `kernel.tpl` (placeholders + scrubbed), [V] clean |
| `gpu.adt` | device-specific | /chosen nord2-wifi-calibration (6146 B = this phone's nvdata WIFI), mac-address-wifi0; random-seed is a fixed 64x0x5a and unique-chip-id a fixed fake ECID (generic, not unit data) | ship `template.adt` [V] clean |
| Rescue `research/nord2/rescue/nord2-adb-rescue.img`, `recompressed/base.img` (also embedded in every iOS boot image) | private | owner's `adb_keys` (host user@host) inside the ramdisk cpio | recipient builds own rescue with nord2-mkrescue.py + own key [V test rescue clean] |
| Modem NVRAM blob (`scratchpad-backup-20261106/nvbuild/n2nvdisk-android-20261106.bin`, `sim-20261115/nvfile/payload-nvfile1*`, `autocycle/n2nvdisk*.bin`, `IOS-BACKUP-20261106/userdata-64GiB-64MiB.bin`) | device-specific (IMEI + 155 CALIBRAT files) | modem_nvram_blob, nv_identity_sha256 | never ship; target builds own (mk_nvdisk_dev.py + n2_check_nvdisk.py) |
| Raw blob region userdata+64 GiB | device-specific | since the 107 GiB root grow it lies INSIDE the HFS root (free space); `portable/nvdisk_from_device.sh --write` would now corrupt HFS free space and be overwritten | do NOT use `--write`; the release uses the nvfile path (0715) |
| `research/nord2/wifi/calibration/{WIFI,WIFI_CUSTOM}` | device-specific | wifi_calibration_file | never ship |
| Kernel source constants: `wifi_main_phy_force.c` + `wifi_normal_config.c` (WIFI sha), `ufs_two_lane_adapter.inc` (GPT5 sha), `sweet_ufs.c` (vendor GPT/LP sha, CRC, LUN/super/vendor extents), `nord2_scp.inc` (sensor cal acc/gyro/ps/als) | device-specific | 5 x 32-byte pins + CRC immediate + 76-byte sensor block, each found exactly once in the kernel [V] | placeholders + per-device apply (section 5) |
| Rootfs mirror `mirror/rootfs-mirror.img` (107 GiB sparse) + manifest | private + Apple | CommCenter plists (ICCID/IMSI/IMEI/carrier), lockdown data_ark, keychain-2.db, sms.db, Accounts3.sqlite, Wi-Fi plists (3 SSIDs), MobileGestalt cache, nord2-bt/bdaddr, jb ssh host keys, app prefs | never ship |
| `IOS-BACKUP-20261106/`, `ios-sweet/backup/nord2/` (partition dumps: nvdata, nvram, protect1/2, proinfo, persist, seccfg, cmdline with serialno, dmesg, kallsyms, ufs-baseline GPT) | private | see scan summary (section 3b) | never ship |
| Logs: `autocycle/runs/*`, `cc-txn-*`, `cc-fetch-*` (unified logs), `ANDROID-IMS-LOG/`, pstore, `PARKED-audio-logs-*` | private | ICCID/IMSI/MAC lines possible (section 3b) | never ship |
| jbts: `secrets/` (host keys, password), `payload-jbts1*` (host keys + key hashes in plan.json), `jbroot*.tar` | private (secrets) / generic (jbroot.tar [V] clean after allowlisting 3 upstream binaries with public embedded PEMs) | private_key, ssh_pub, ssh_secret | ship build_jbts.py + inputs + guest scripts; recipient generates own secrets |
| bt1 payload (`bt-20261115/pl/payload-bt1*`) | generic + vendor | MediaTek BT firmware inside (vendor blob); BD address generated on the phone | ship n2btd + plan + guests; firmware from recipient's vendor |
| simcache1 payload | generic | [V] clean | ship |
| gfxall17 payload (Nord2Metal, libredwoodair.dylib with Mesa) | generic, licence question | [V] scanned (see 3b); built with private ios-tegu references | OFF by default (owner decision) |
| `*-seeded` payloads (shader cache seeded from the owner's usage) | private-ish (reflects apps used) | | never ship; recipient seeds own cache |
| txn scripts `*/txn_*.sh`, `autocycle/*.sh`, rehearsal logs, image receipts/check logs | private (owner paths) | owner_string in nearly every script/log | replaced by generic `n2dev/n2_txn.sh`; host tools copied with the adb default rewritten |
| Host tools (mkpayload, pack/check nostock, loader, stub, mkrescue, snapshot/commit/catalog, fastroot, hfsx-read, usb console, nord2-install-root, mkiosroot) | generic | [V] clean in the release | ship |
| VM initramfs `research/nord2/rootfs-vm/initramfs` | generic (GPL userland) | [V] clean | ship + source offer |
| IPSW (`research/nord2/ipsw/...`), OTA/stock images (`backup/nord2/boot.img`, `recovery.img`, `vendor_boot.img`) | Apple / vendor | stock boot+recovery [V] contain no owner identifiers (test rescue built from them is clean) | never ship; recipient downloads/extracts |

### 3b. Scan counts (files containing each class; text scan of gpu/build files < 64 MiB, binary-aware scan of key dirs)
See `private/scan/text-counts.txt`, `private/scan/bin-summary.txt` and the per-target JSON reports (paths + classes only).
Text scan (984k files < 64 MiB under gpu/build, one fixed-string pass; files containing the class):
- owner_string (home path / user name) 21366 files (compile-stall 2697, hardware-20261025 1396, deepseek-parallel 793, gfx200 416, ...):
  practically every script, log and receipt -> nothing is shipped from gpu/build except the allowlist, rewritten/scanned.
- serial (Android serialno = also the adb serial) 40 files (gnss-20261114 12, autocycle 6, ANDROID-IMS-LOG 3, autotest 2, queue*.out, ...).
- ICCID 5, IMEI 2, IMSI 3, Wi-Fi MAC 3 files (ANDROID-IMS-LOG, cc-fetch-q10cj/q10cm, autocycle); BT address 4 (autocycle, bt-20261115);
  SSH host public keys 9 (jbts-20261115, cc-txn-jbts18a); home SSIDs 5 (weather-20261114, autojoin-20261115).
Binary-aware scan (sanitizer, files with the class):
- ANDROID-IMS-LOG: ICCID 5, IMEI 3, IMSI 4, Wi-Fi MAC 6, serial 5, Wi-Fi calibration file 2 (+ its hash 2), NV identity file hash 2.
- ios-sweet/backup/nord2: serial 88, sensor calibration values 15, Wi-Fi MAC 3, ICCID/IMEI/IMSI 1 each, 5 ext4 partition images.
- scratchpad-backup-20261106: owner_string 150, NV identity file hashes 11, modem NVRAM blobs 4, sensor cal 6, ICCID 4, IMSI 2.
- IOS-BACKUP-20261106: HFS root volume 1, modem NVRAM blobs 2. autocycle/n2nvdisk.bin: modem NVRAM blob (an older one).
- research/nord2/wifi/calibration: Wi-Fi calibration file + MAC. jbts secrets: 3 private keys, 3 public keys, password.
- q10jfinal19 ui-sched: see the boot-image row. Clean: payload-gfxall17, payload-gfxall17-seeded (no identifiers; seeded still
  excluded as usage-derived), autocycle/boot-rom-stock.img, boot-ksu-android.img, jbroot.tar (after the upstream-PEM allowlist).
Release dist/nord2-ios-release-20261115: SANITIZE_CLEAN (1292 files/members, 99 needles). Negative test: the same build with
an owner SSH public key and the WIFI file added to the allowlist -> RELEASE_REJECTED (key, WIFI file, MAC, hash, even the hash
line in MANIFEST.sha256 caught).

## 4. Install-time generation on the target (implemented; host-tested on synthetic phones + owner data)
1. `n2_read_device.sh OUT` (rescue, adb): `blockdev --setro` on every partition used, ext4 `ro,noload`, `dd if=` only ->
   tree/{nvdata,protect_f,protect_s}/md, wifi/WIFI, oplus_custom.bin, gpt/lun-lba0-5.bin + capacity, super-head.bin,
   partitions.sha256. [D on hardware: syntax-checked only]
2. `n2_device_profile.py DUMP --expect release-manifest.json` -> READY / REBUILD_NEEDED / REFUSED:
   WIFI checked exactly like the kernel's nfa_prepare (Python port; the owner's real WIFI passes [V]); MAC = WIFI[4:10];
   sensor cal from oplus_custom (`sensor_config_info_t`: 80-byte header + Sensor[256], v1/v2 layouts from the OPLUS kernel
   source, plausibility-checked; none -> defaults acc/gyro 0, ps 0, als 1000 + warning); GPT pins and LP pins/vendor extent;
   NVD_IMEI/CALIBRAT presence (never decoded). [V] On the owner's real GPT baseline the profile reproduces exactly the
   kernel's compiled GPT5 pin, vendor entry-array pin and CRC immediate; the owner's WIFI hash equals the kernel's pin.
3. Modem: `mk_nvdisk_dev.py` (same N2NVDSK1 format as before) from the dump + the target's vendor md files, then
   `n2_check_nvdisk.py` refuses unless every per-device file in the blob is byte-identical to the target's own dump (and
   optionally carries none of the owner's identity files); `mk_nvfile_payload.py` makes the nvfile payload (kernel 0715 reads
   /private/var/db/nord2-modem/n2nvdisk.bin first). No IMEI is ever invented, edited or copied: it is whatever the target's
   own protect/nvdata partitions hold.
4. ADT: `mk_device_adt.py` adds the target's WIFI file (sha-checked against the profile) and MAC to the generic template.
5. Kernel: `n2_kernel_pins.py apply` writes the target's 7 values into the placeholders of `kernel.tpl`; `check` fails if any
   placeholder is left; the packer refuses such a kernel.
6. `n2_pack_device.sh`: same steps as the owner's pack_with_args_nosyms.py (nostock layout). [V] With the owner's kernel/ADT/
   base it reproduces q10jfinal19 byte-for-byte (sha256 65bc8d2b...); with a synthetic phone's kernel/ADT and a test rescue
   built from stock boot/recovery + a test adb key the image passes the nostock checker and contains no owner identifier.
7. Payloads with the generic `n2_txn.sh` (same sequence as the owner's txn scripts; config from env). [D: not run]

## 5. Kernel: rebuild or patch?
Per-device values compiled into the kernel (all in `__TEXT,__const` or code, each found exactly once [V]):
wifi_cal_sha256 (merged const of 2 files), gpt5_sha256, vendor_gpt_sha256, vendor_geometry_sha256, vendor_lp_sha256,
gpt_entries_crc (movz/movk pair in sweet_ufs.c), sensor_cal (acc[6] gyr[6] ps[6] als[1]).
Decision: **patch at install time** (binary placeholders, offsets recorded in `kernel/pins-map.json`), because these objects come
from the CI kernel archive and cannot be rebuilt by the local integ build (only selected members such as sweet_touch.o are
recompiled locally). Constants that are layout-, not unit-specific (LUN blocks 31240192, GPT last usable 31240183, super entry
index 54, super first/size, vendor extent offset/size, LP metadata size/backup offset) are compared by the profile; any
difference -> REBUILD_NEEDED (kernel must be rebuilt in CI with that unit's layout). [B] whether other 128 GB units share the
layout and the LP metadata bytes (LP metadata hash is pinned; it is per firmware build if OnePlus writes identical metadata).
Long-term source fix [D]: piece "0730 device pins from the ADT": wifi_digest/digest(), nord2_x2_run's expected digest,
the vendor validator's constants and the SCP calibration read `/chosen nord2-device-pins` (struct N2DP v1: gpt5 sha, entries
sha/CRC, LUN blocks, first/last usable, super index/first/bytes, LP geometry/metadata sha, metadata max, backup offset,
vendor offset/bytes) and `/chosen nord2-sensor-cal` (N2SC v1, valid bits); absent -> Wi-Fi/vendor refuse, SCP sends no
calibration, and the packer drops `nord2ufs2live=1`. Needs a CI kernel build; then mk_device_adt.py fills both properties.

## 6. Owner workflow
`private/release.env` (paths, owner GPT CRC, scrub strings, supported firmware hashes) -> `tools/make_release.sh OUT`:
strip + pins template + scrub, ADT template, allowlist copy (`tools/release-files.txt`), manifest + MANIFEST.sha256,
sanitizer with `private/ids.json` (FAIL -> OUT renamed .REJECTED), host tests from inside the release.
`tests/run_host_tests.sh` (OWNER_ENV=private/owner-test.env for the owner-artifact steps): 28/28 PASS [V].

## 7. Status
Done and host-tested [V]: identifier collection; sanitizer (identity needles in ASCII/UTF-16/BCD/raw/reversed/int32/hash,
structural detectors, nested gzip/xz/lz4/tar/zip, never prints values); profile; nvdisk own-check; nvfile payload; ADT
template/device; kernel pins template/apply/check; pack wrapper (byte-exact reproduction); make_release (clean release built).
Designed only [D]: n2_read_device.sh on hardware; n2_txn.sh on hardware (first run without a mirror untested); rootfs for a
fresh phone (IPSW -> mkiosroot.sh -> port overlay: the overlay must be produced as the union of the port's owned paths from
the ship-set payloads + binary patches against the recipient's IPSW for modified Apple binaries; NOT built); BT/modem vendor
file extraction from the OTA vendor.img; kernel source piece 0730; per-device random-seed (option exists, off).
Blocked [B] (needs a second Nord 2): any boot of a per-device image on another unit; whether other units share the GPT layout
and LP metadata; sensor-cal layout v1 vs v2 on another unit; whether OTA boot partitions hash identically (firmware check
is an error, `--allow-firmware-mismatch` downgrades it).

## 8. NEVER share (owner)
- `gpu/build/mirror/` (rootfs-mirror.img + manifest), `gpu/build/IOS-BACKUP-20261106/`, `ios-sweet/backup/nord2/` (all of it),
  `gpu/build/scratchpad-backup-20261106/`, `gpu/build/autocycle/n2nvdisk*.bin`, `gpu/build/autocycle/runs/`,
  `gpu/build/cc-txn-*/`, `gpu/build/cc-fetch-*/`, `gpu/build/ANDROID-IMS-LOG/`, `gpu/build/PARKED-audio-logs-*/`, any pstore/dmesg.
- `gpu/build/sim-20261115/nvfile/payload-nvfile1*`, any `n2nvdisk*.bin`, `research/nord2/wifi/calibration/`.
- `gpu/build/jbts-20261115/secrets/`, `payload-jbts1*` (host keys), `jbroot-jbts1.tar` is generic but keep with the build.
- every `device-mmu-*-candidate/` (kernel with pins + build paths, gpu.adt, images, receipts), every built `.img`,
  `research/nord2/rescue/*.img` and `recompressed/` (owner adb key).
- `*-seeded` gfx payloads; `release-20261115/private/` (ids.json, env files, scan reports, pattern files).
- Apple/vendor content: `research/nord2/ipsw/`, OTA/stock images, vendor extracts (`vend/`), md1img.
