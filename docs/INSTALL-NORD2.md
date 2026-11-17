# Installing the port on another OnePlus Nord 2

> **Read this first**
> - Everything here has run only on the author's computer and the author's phone. The flow for a **second Nord 2 has
>   never been tested**. Expect to be the first tester, and expect to stop at a `REFUSED` or `REBUILD_NEEDED` verdict.
> - **Kernel for your phone: a published template.** Step 7 uses the kernel template `kernel.tpl` + `pins-map.json` and
>   the ADT template `template.adt` in `components/release-20261115/dist/nord2-ios-release-20261115/kernel/` (published 2026-11-17). The template was made from
>   the **q10jfinal19** line (2026-11-15): it predates the later Bluetooth iterations (bt5..bt22), the TrollStore/Files, GPU-fault and
>   sensor pieces (0730-0734, 0750, 0760-0762) of the current line q10jfinal44ts. A newer template needs a kernel build
>   (CI + `make_release.sh`), see [COMPLETENESS.md](COMPLETENESS.md).
> - The kernels built so far contain constants of the author's unit (GPT digest + CRC, Wi-Fi calibration digest, sensor
>   calibration). Such a kernel **halts in the two-lane UFS switch on any other unit** and refuses that unit's Wi-Fi
>   calibration. In this tree those constants are zero placeholders; step 7 writes **your** phone's values. Nothing is ever
>   borrowed from another phone.
> - Steps marked ⚠ write to the phone. Unlocking erases all Android data; installing the iOS root replaces `userdata`.

What the tags mean: **[V]** verified on the author's phone or host, **[D]** designed only (syntax-checked or host-tested on
synthetic data), **[B]** blocked (needs a second Nord 2 or a missing piece).

What works once installed (on the author's phone): see the table in the [README](../README.md#what-works) and
[STATUS.md](STATUS.md).

## Prerequisites checklist

Phone
- [ ] OnePlus Nord 2 5G, fastboot product `oplus6893`, 128 GB storage layout (another layout ends in `REBUILD_NEEDED`).
- [ ] The OxygenOS build the kernel constants were made for: its boot/recovery sha256 are in
      `components/release-20261115/dist/nord2-ios-release-20261115/release-manifest.json` (`supported_firmware`); `tools/fetch-oxygenos.sh verify boot.img recovery.img` checks them.
- [ ] Battery charged; a good USB cable.

PC (Linux)
- [ ] `python3`, `adb` + `fastboot` (Android platform-tools)
- [ ] `zopfli`, `hfsprogs` (`fsck.hfsplus`), `qemu-system-x86_64` with KVM
- [ ] `apfs-fuse` and `rsync` (only for building the iOS root), `payload-dumper-go` (to unpack the OTA)
- [ ] A private folder outside git, e.g. `~/nord2-private/` (everything in it identifies your phone)

Downloads (by **you**, from the original vendors, never from this repository)
- [ ] The official OxygenOS full OTA of the supported build (OnePlus)
- [ ] The iOS 16.7.x IPSW for iPhone10,3/10,6 (Apple's update CDN)
- [ ] For Sileo/SSH/TrollStore: the Procursus .debs, TrollStore 2.1.1 and ldid listed in
      `components/jbts-20261115/inputs/README-INPUTS.md`

## The steps at a glance

| Step | What | Writes to phone | Status |
| --- | --- | --- | --- |
| 1 | Unlock the bootloader | ⚠ yes (wipes) | [V] |
| 2 | Build your RAM rescue image | no | [D] (built the same way for the author [V]) |
| 3 | Boot the rescue | ⚠ `boot` only | [V] procedure |
| 4 | Read-only dump | no (read-only) | [D] |
| 5 | Profile and checks | no | [V] on the author's data, [D] elsewhere |
| 6 | Your modem NVRAM blob | no | [V] tools on host, [D] elsewhere |
| 7 | Kernel + device tree + boot image for your phone (from the published template) | no | [D] (template published; never booted on another unit) |
| 8 | iOS root filesystem | ⚠ `userdata` | **[B]** overlay not packaged |
| 9 | Payloads (modem, SIM, Bluetooth, Sileo/TrollStore, GPU userland) | ⚠ iOS root | [D] generic tool |
| 10 | First-boot checks | no | [V] on the author's phone |

## 1. ⚠ Unlock the bootloader [V]

On the phone: Settings > About > tap Build number 7 times > Developer options > enable **OEM unlocking** and **USB
debugging**. Then:
```sh
adb reboot bootloader
fastboot flashing unlock        # confirm on the phone; this wipes it
```
**Check:** the phone shows the unlocked-bootloader warning at every boot; `fastboot getvar unlocked` says `yes`.

## 2. Your stock images and your own RAM rescue [D]

```sh
payload-dumper-go -p boot,recovery,vendor OxygenOS-full-OTA.zip
tools/fetch-oxygenos.sh verify boot.img recovery.img   # OXYGENOS_SUPPORTED, else stop
adb keygen ~/.android/adbkey             # only if you have no adb key yet
python3 tools/nord2-mkrescue.py boot.img recovery.img ~/nord2-private/my-rescue.img --adb-key ~/.android/adbkey.pub
```
The rescue is a RAM-only Android with adbd that accepts only your key. On the author's phone a rescue built the same way is
the tested recovery path [V].

**Check:** `~/nord2-private/my-rescue.img` exists; keep `boot.img` (your way back to Android while `userdata` is untouched).

## 3. ⚠ Boot the rescue [V procedure]

`fastboot boot` does not work on the Nord 2, so the rescue is flashed to `boot`:
```sh
fastboot flash boot ~/nord2-private/my-rescue.img && fastboot reboot
adb devices                              # shows "recovery"
```
**Check:** `adb devices` lists the phone as `recovery`. Back to Android at any time: `fastboot flash boot boot.img` (only
before the iOS root overwrites `userdata`).

## 4. Read-only dump of your phone [D: syntax-checked, never run on hardware]

```sh
components/release-20261115/tools/n2dev/n2_read_device.sh ~/nord2-private/dump
```
It sets every partition it reads read-only (`blockdev --setro`), mounts ext4 `ro,noload`, uses `dd if=` only, and saves your
modem NVRAM files, Wi-Fi calibration, sensor calibration (`oplus_custom`), GPT and LP headers.

**Check:** `~/nord2-private/dump/` contains `tree/`, `wifi/WIFI` and the GPT/LP headers; the script ends without errors.
Keep the folder private.

## 5. Profile and checks [V on the author's data, D on any other phone]

```sh
python3 components/release-20261115/tools/n2dev/n2_device_profile.py ~/nord2-private/dump \
    --out ~/nord2-private/dump/device-profile.json --expect components/release-20261115/dist/nord2-ios-release-20261115/release-manifest.json
```
**Expected output:** one verdict line.
- `READY`: continue.
- `REBUILD_NEEDED`: your storage/firmware layout differs from the kernel's compiled layout constants (LUN size, super/vendor
  extents, LP metadata). Stop; the kernel must be rebuilt with your layout [B].
- `REFUSED`: missing or invalid data (no IMEI files, invalid Wi-Fi calibration...). Stop.

On the author's real dump the profile reproduces exactly the kernel's compiled GPT digest, entry-array digest and CRC, and the
author's Wi-Fi calibration passes the Python port of the kernel's check [V].

## 6. Modem NVRAM blob from YOUR partitions [V tools on host, D on another phone]

```sh
mkdir -p ~/nord2-private/nvtree/vendor/etc && cp -a ~/nord2-private/dump/tree/* ~/nord2-private/nvtree/
cp -a <your-vendor-extract>/etc/md <your-vendor-extract>/etc/mdota ~/nord2-private/nvtree/vendor/etc/
python3 components/release-20261115/tools/n2dev/mk_nvdisk_dev.py --tree ~/nord2-private/nvtree --out ~/nord2-private/n2nvdisk.bin --check
python3 components/release-20261115/tools/n2dev/n2_check_nvdisk.py ~/nord2-private/n2nvdisk.bin --dump ~/nord2-private/dump
python3 components/release-20261115/tools/n2dev/mk_nvfile_payload.py --blob ~/nord2-private/n2nvdisk.bin --dump ~/nord2-private/dump --out ~/nord2-private/nvfile
```
**Expected output:** `n2_check_nvdisk.py` prints `NVDISK_OWN_OK`; `~/nord2-private/nvfile/` contains `payload-nvfile1`,
`guest-nvfile1.sh` and `owned-nvfile1.json`. The IMEI the modem reports is the one already stored in your phone; no tool
creates, edits or copies an IMEI.

## 7. Kernel + device tree + boot image for YOUR phone [D: template published, never booted on another unit]

The template files are in the repository (sha256: `kernel.tpl` 6dfce954…, `pins-map.json` 99322d01…,
`template.adt` 5f76af4b…; full hashes in [LARGE-ARTIFACTS.md](LARGE-ARTIFACTS.md)). The pins are written from **your**
dump's profile (step 5); nothing comes from another phone.
```sh
R=components/release-20261115/dist/nord2-ios-release-20261115
sha256sum $R/kernel/*                       # compare with the hashes above / pins-map.json kernel_template_sha256
python3 components/release-20261115/tools/n2dev/n2_kernel_pins.py check $R/kernel/kernel.tpl --map $R/kernel/pins-map.json
python3 components/release-20261115/tools/n2dev/n2_kernel_pins.py apply $R/kernel/kernel.tpl --map $R/kernel/pins-map.json \
    --profile ~/nord2-private/dump/device-profile.json --out ~/nord2-private/kernel.dev
python3 components/release-20261115/tools/n2dev/n2_kernel_pins.py check ~/nord2-private/kernel.dev --map $R/kernel/pins-map.json
python3 components/release-20261115/tools/n2dev/mk_device_adt.py $R/kernel/template.adt ~/nord2-private/device.adt \
    --profile ~/nord2-private/dump/device-profile.json --wifi ~/nord2-private/dump/wifi/WIFI
ZOPFLI=$(command -v zopfli) bash components/release-20261115/tools/n2dev/n2_pack_device.sh ~/nord2-private/boot-ios.img \
    ~/nord2-private/kernel.dev ~/nord2-private/device.adt ~/nord2-private/my-rescue.img
```
**Expected output:**
- first `check` on the template: `PINS_LEFT wifi_cal_sha256, gpt5_sha256, vendor_gpt_sha256, vendor_geometry_sha256,
  vendor_lp_sha256, sensor_cal, gpt_entries_crc` (exit 1: the template is intact; it must never be flashed);
- `apply`: `device kernel …kernel.dev: 7 per-device values written from this phone's profile, sha256 …` (it refuses
  unless the profile says `READY`);
- second `check`: `PINS_LEFT none`;
- `mk_device_adt.py`: `device ADT …device.adt: … bytes (Wi-Fi calibration + MAC of this phone…)`;
- `n2_pack_device.sh`: `PACK_OK …boot-ios.img … (private: carries this phone's Wi-Fi data)`.

Honest limits: `n2_pack_device.sh` reproduced the author's q10jfinal19 image byte for byte from the author's inputs [V];
with a synthetic phone's values it builds an image that passes the checker and holds no owner identifier [V host]. The
whole step has **never been run on a second Nord 2**, and the resulting kernel is the q10jfinal19 line (see the note at the
top). The long-term fix (a kernel that reads the per-device values from the device tree, `/chosen nord2-device-pins` and
`/chosen nord2-sensor-cal`) is designed only [D].

## 8. ⚠ iOS root filesystem [B: port overlay not packaged]

Build the pristine root from your own IPSW and write it to `userdata` from the rescue:
```sh
tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ios-root.img
python3 tools/nord2-install-root.py ios-root.img ~/nord2-private/install-evidence --serial <adb-serial>   # read-only preflight
python3 tools/nord2-install-root.py ios-root.img ~/nord2-private/install-evidence2 --serial <adb-serial> --write-userdata
```
Note: `nord2-install-root.py` checks the size and sha256 of the author's frozen root image and recovery backups
(constants at the top of the script). For your own root image you must update those constants first; it never writes the GPT
or other partitions and verifies a full read-back.
**The port's own changes to the root (launchd jobs, audio/camera/codec plug-ins, Metal userland) are not packaged as an
overlay yet**; on the author's phone they were applied over weeks by payload transactions. Until an overlay builder exists
a recipient cannot complete this step.

## 9. ⚠ Payloads (one transaction at a time, a new TAG each time) [D for the generic tool]

The generic transaction tool is `components/release-20261115/tools/n2dev/n2_txn.sh` (configuration in
`~/.config/nord2/txn.env`: `ADB FASTBOOT RESCUE_IMG MIRROR HT INITRAMFS WORK`; read the script header). It boots the
rescue, edits the root offline in a VM and reflashes your boot image. `HT` (host tools) and `INITRAMFS` (the offline VM's
Linux initramfs) come from a release build; the initramfs binaries are not in this tree.

Order used on the author's phone:
1. **Modem blob file**:
   `n2_txn.sh ~/nord2-private/nvfile/payload-nvfile1 ~/nord2-private/nvfile/guest-nvfile1.sh ~/nord2-private/nvfile/owned-nvfile1.json NVFILE1_OK ~/nord2-private/boot-ios.img nv1`
2. **No-SIM carrier cleanup** (optional): `components/sim-20261115/simcache/` (`guest-simcache1.sh`, `owned-simcache1.json`;
   see `INSTALL_TEST.txt`).
3. **Bluetooth**: `components/bt-20261115/pl/` (`guest-bt1.sh`, `owned-bt1.json`; n2btd source in
   `components/bt-20261115/n2btd/`). Copy **your** vendor BT firmware (`soc3_0_ram_bt_1a_1_hdr.bin`,
   `soc3_0_ram_mcu_1a_1_hdr.bin` from your vendor image) into the payload; the BT address is generated randomly on the first run.
4. **Sileo / OpenSSH / TrollStore (jbts)**: `components/jbts-20261115/build_jbts.py` after you populated `inputs/` (see
   `README-INPUTS.md`); it generates **your** SSH host keys and password; follow `components/jbts-20261115/SETUP.md`.
5. **GPU userland**: sources in `gpu/` and `components/gfx200-20261114/all17-src/`, install script
   `components/gfx200-20261114/shipall/txn_gfxall.sh`. Building it needs the iOS SDK (not included); no built payload is shipped.

**Check after each transaction:** the tool prints the payload's `*_OK` token (e.g. `NVFILE1_OK`) and the phone boots back to
iOS.

## 10. First-boot checks [V on the author's phone]

- USB console log (`python3 tools/nord2-usb-console.py`): `NORD2 MODEM: fs disk blob source: rootfs file` plus a file count
  and no `no valid header`; `NORD2 UFS X2 LIVE: rc=0`; Wi-Fi calibration accepted; `NORD2 SCP: calibration for sensor ...
  taken`.
- Settings > General > About: IMEI = the one on your box; Modem Firmware not empty; Wi-Fi address = your phone's original MAC.
- Wi-Fi joins a known network by itself about 30-40 s after boot (0790); Bluetooth comes up about 24 s after boot.

## Troubleshooting and recovery

| Symptom | Likely cause | What to do |
| --- | --- | --- |
| `fastboot boot` fails | Not supported by the Nord 2 bootloader | Flash to `boot` instead (step 3) |
| Profile says `REBUILD_NEEDED` | Different storage/firmware layout | Stop; a kernel with your layout is needed |
| Profile says `REFUSED` | Missing IMEI files or invalid Wi-Fi calibration in the dump | Re-run step 4; do not continue |
| Phone stops in fastboot or boot-loops | Kernel halted (often a per-device value) | `fastboot flash boot ~/nord2-private/my-rescue.img && fastboot reboot`, then pull logs |
| Halt during the UFS two-lane switch | Kernel built for another unit (GPT pins) | Rescue as above; the kernel must carry your pins (step 7) |
| About shows empty "Modem Firmware", no IMEI | Modem NVRAM blob missing or overwritten | Re-install the nvfile payload (step 9.1); the log shows `no valid header` |
| No Wi-Fi networks / Wi-Fi refused | Wi-Fi calibration digest does not match the kernel | Kernel/ADT must be built from your dump (step 7) |
| A payload breaks iOS | Bad transaction | Run the same payload's `-undo` variant, or return to the previous boot image |
| Want the last good iOS back | — | `fastboot flash boot <your previous boot-ios.img>` |

Never boot stock Android over the iOS `userdata`; never write bootloaders, modem, persist, nvdata/nvram, protect or the GPT.

## Wi-Fi, Bluetooth, SIM notes

- Wi-Fi uses your phone's own calibration file and MAC (from the dump, via the device ADT); auto-join needs 0790 (in the
  final line). SSH from jbts works over Wi-Fi only (no USB networking on the Nord 2).
- Bluetooth firmware is read from your own vendor image; HFP/SCO calls and Bluetooth keyboards/mice do not work.
- SIM: tested only with the author's operator. Another operator's APN/IMS settings depend on 0710-0712 [host]. 5G is not
  available in iOS (iPhone X baseband identity).

## Privacy

`~/nord2-private/` (dump, profile, modem blob, device ADT/kernel/boot image, logs) identifies your phone. Before sharing any
folder run `python3 -I components/release-20261115/tools/sanitize_release.py DIR` (structural checks; the owner-side
identifier list comes from `collect_private_ids.py`).
