# Installing the port on ANOTHER OnePlus Nord 2 (from scratch)

> **Read this first.**
> - Everything below has run only on the author's computer and the author's phone. The per-device flow for a second Nord 2 is
>   **[designed, never tested on a second Nord 2]**. Expect to be the first tester, and expect to stop at a `REFUSED` or
>   `REBUILD_NEEDED` verdict.
> - **This source tree alone cannot produce a bootable kernel.** The final kernel was built from a CI kernel object archive plus
>   local pieces; some kernel files (the GPU driver `Nord2GPU.cpp` and two headers) and the GPU userland are withheld pending a
>   licence decision (docs/EXCLUDED.md). The installer steps 7+ need a **kernel template** (`kernel.tpl` + `pins-map.json`) and an
>   **ADT template** that only the author's release builder (`components/release-20261115/tools/make_release.sh`) produces from a
>   built kernel. Until such a release is published, a recipient can run steps 0-6 (rescue, read-only dump, profile, own modem
>   blob) and must stop before step 7. **This is the current blocker.**
> - The kernel compiled for the author's phone pins that phone's GPT digest + CRC, Wi-Fi calibration digest and sensor
>   calibration as constants. A kernel built for one unit **halts in the live two-lane UFS switch on any other unit** (partition
>   GUIDs are random per unit) and refuses that unit's own Wi-Fi calibration. In this source tree those constants are replaced
>   by zero placeholders. The per-device pipeline (step 7) writes **your** phone's values into the template; nothing is ever
>   borrowed from another phone.
> - Every step that writes to the phone is marked ⚠. Unlocking erases all Android data.

Status tags: **[V]** verified on the author's phone/host, **[D]** designed only (syntax-checked or host-tested on synthetic data),
**[B]** blocked until a second Nord 2 or a published kernel template exists.

## 0. What you need
- OnePlus Nord 2 5G (`oplus6893`, Dimensity 1200, 128 GB storage layout; another layout ends in `REBUILD_NEEDED` at step 5).
- The OxygenOS build the kernel constants were made for. Its boot/recovery sha256 are part of a release manifest
  (`release-manifest.json`, produced by `make_release.sh`; not in this tree).
- Linux PC with python3, adb + fastboot (Android platform-tools), zopfli, hfsprogs (`fsck.hfsplus`), qemu-system-x86_64 with KVM,
  apfs-fuse and rsync (only for the iOS root), payload-dumper-go (to unpack the OTA).
- Downloaded by **you**, from the original vendors (never from this repository):
  - the official OxygenOS full OTA of the supported build (OnePlus);
  - the iOS 16.7.x IPSW for iPhone10,3/10,6 (Apple's update CDN; public IPSW indexes list the URL);
  - for jbts: the Procursus .debs, TrollStore 2.1.1 and ldid listed in `components/jbts-20261115/inputs/README-INPUTS.md`.

## 1. ⚠ Unlock the bootloader [V on the author's phone]
Settings > About > tap Build number 7 times > Developer options > enable OEM unlocking and USB debugging, then
```
adb reboot bootloader
fastboot flashing unlock        # confirm on the phone; this wipes it
```

## 2. Your stock images and your own RAM rescue [D]
```
payload-dumper-go -p boot,recovery,vendor OxygenOS-full-OTA.zip
sha256sum boot.img recovery.img          # compare with the release manifest's supported_firmware
adb keygen ~/.android/adbkey             # only if you have no adb key yet
python3 tools/nord2-mkrescue.py boot.img recovery.img ~/nord2-private/my-rescue.img --adb-key ~/.android/adbkey.pub
```
The rescue is a RAM-only Android with adbd that accepts only your key. (On the author's phone the rescue built the same way
from the author's key is the tested recovery path [V].)

## 3. ⚠ Boot the rescue [V procedure on the author's phone]
`fastboot boot` does not work on the Nord 2; the rescue is flashed to `boot`:
```
fastboot flash boot ~/nord2-private/my-rescue.img && fastboot reboot
adb devices                              # shows "recovery"
```
Back to Android at any time: `fastboot flash boot boot.img` (only before the iOS root overwrites userdata).

## 4. Read-only dump of your phone [D: syntax-checked, never run on hardware]
```
components/release-20261115/tools/n2dev/n2_read_device.sh ~/nord2-private/dump
```
It sets every used partition read-only (`blockdev --setro`), mounts ext4 `ro,noload`, uses `dd if=` only, and saves your modem
NVRAM files, Wi-Fi calibration, sensor calibration (oplus_custom), GPT and LP headers. Keep `~/nord2-private/` private.

## 5. Profile and checks [V on the author's data, D on any other phone]
```
python3 components/release-20261115/tools/n2dev/n2_device_profile.py ~/nord2-private/dump \
    --out ~/nord2-private/dump/device-profile.json --expect release-manifest.json
```
- `READY`: continue.
- `REBUILD_NEEDED`: your storage/firmware layout differs from the kernel's compiled layout constants (LUN size, super/vendor
  extents, LP metadata): stop; the kernel must be rebuilt with your layout [B].
- `REFUSED`: missing/invalid data (no IMEI files, invalid Wi-Fi calibration...): stop.
On the author's real dump the profile reproduces exactly the kernel's compiled GPT digest, entry-array digest and CRC, and the
author's Wi-Fi calibration passes the Python port of the kernel's check [V].

## 6. Modem NVRAM blob from YOUR partitions [V tools on host, D on another phone]
```
mkdir -p ~/nord2-private/nvtree/vendor/etc && cp -a ~/nord2-private/dump/tree/* ~/nord2-private/nvtree/
cp -a <your-vendor-extract>/etc/md <your-vendor-extract>/etc/mdota ~/nord2-private/nvtree/vendor/etc/
python3 components/release-20261115/tools/n2dev/mk_nvdisk_dev.py --tree ~/nord2-private/nvtree --out ~/nord2-private/n2nvdisk.bin --check
python3 components/release-20261115/tools/n2dev/n2_check_nvdisk.py ~/nord2-private/n2nvdisk.bin --dump ~/nord2-private/dump   # NVDISK_OWN_OK
python3 components/release-20261115/tools/n2dev/mk_nvfile_payload.py --blob ~/nord2-private/n2nvdisk.bin --dump ~/nord2-private/dump --out ~/nord2-private/nvfile
```
The IMEI the modem reports is the one already stored in your phone; no tool creates, edits or copies an IMEI.

## 7. Kernel + device tree + boot image for YOUR phone [B: needs kernel.tpl/pins-map.json/template.adt]
With a published release (author side: `make_release.sh`, which runs `n2_kernel_pins.py template` on a built kernel):
```
python3 components/release-20261115/tools/n2dev/n2_kernel_pins.py apply kernel/kernel.tpl --map kernel/pins-map.json \
    --profile ~/nord2-private/dump/device-profile.json --out ~/nord2-private/kernel.dev
python3 components/release-20261115/tools/n2dev/n2_kernel_pins.py check ~/nord2-private/kernel.dev --map kernel/pins-map.json
python3 components/release-20261115/tools/n2dev/mk_device_adt.py kernel/template.adt ~/nord2-private/device.adt \
    --profile ~/nord2-private/dump/device-profile.json --wifi ~/nord2-private/dump/wifi/WIFI
ZOPFLI=$(command -v zopfli) bash components/release-20261115/tools/n2dev/n2_pack_device.sh ~/nord2-private/boot-ios.img \
    ~/nord2-private/kernel.dev ~/nord2-private/device.adt ~/nord2-private/my-rescue.img
```
`n2_pack_device.sh` reproduced the author's q10jfinal19 image byte for byte from the author's inputs [V]; with a synthetic phone's
values it builds an image that passes the checker and holds no owner identifier [V host]. It has never booted on another unit [B].
The long-term fix (a kernel that reads the per-device pins from the device tree, `/chosen nord2-device-pins` and
`/chosen nord2-sensor-cal`) is designed only and needs a CI kernel build [D].

## 8. ⚠ iOS root filesystem [B: port overlay not packaged]
Build the pristine root from your own IPSW: `tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ios-root.img`, then write it to
userdata from the rescue (`tools/nord2-install-root.py`). **The port's own changes to the root (launchd jobs, audio/camera/codec
plug-ins, Metal userland, receipts) are not packaged as an overlay yet**; on the author's phone they were applied over weeks by
payload transactions. Until an overlay builder exists this step cannot be completed by a recipient.

## 9. ⚠ Payloads (one transaction at a time, a new TAG each time) [D for the generic tool]
The generic transaction tool is `components/release-20261115/tools/n2dev/n2_txn.sh` (configuration in `~/.config/nord2/txn.env`:
`ADB FASTBOOT RESCUE_IMG MIRROR HT INITRAMFS WORK`; read the script header). It boots the rescue, snapshots/edits the root offline
in a VM and reflashes your boot image. `HT` (host tools) and `INITRAMFS` (the offline VM's Linux initramfs, GPL userland) come
from a release build; the initramfs binaries are not in this source tree. Payload order used on the author's phone:
1. modem blob file: `n2_txn.sh ~/nord2-private/nvfile/payload-nvfile1 ~/nord2-private/nvfile/guest-nvfile1.sh ~/nord2-private/nvfile/owned-nvfile1.json NVFILE1_OK ~/nord2-private/boot-ios.img nv1`
2. no-SIM carrier cleanup (optional): `components/sim-20261115/simcache/` (`guest-simcache1.sh`, `owned-simcache1.json`, see
   `INSTALL_TEST.txt`).
3. Bluetooth: `components/bt-20261115/pl/` (`guest-bt1.sh`, `owned-bt1.json`, n2btd source in `components/bt-20261115/n2btd/`).
   Copy **your** vendor BT firmware (`soc3_0_ram_bt_1a_1_hdr.bin`, `soc3_0_ram_mcu_1a_1_hdr.bin` from your vendor image) into the
   payload; the BT address is generated randomly on the first run on your phone.
4. jbts (Sileo/OpenSSH/TrollStore): `components/jbts-20261115/build_jbts.py` after you populated `inputs/` (see README-INPUTS.md);
   it generates **your** SSH host keys and password; follow `components/jbts-20261115/SETUP.md`.
The GPU userland payload (Metal driver) is not in this tree (licence decision pending).

## 10. First-boot checks [V on the author's phone]
- USB console log (`tools/nord2-usb-console.py`): `NORD2 MODEM: fs disk blob source: rootfs file` + a file count, no
  `no valid header`; `NORD2 UFS X2 LIVE: rc=0`; Wi-Fi calibration accepted; `NORD2 SCP: calibration for sensor ... taken`.
- Settings > General > About: IMEI = the one on your box; Modem Firmware not empty; Wi-Fi address = your phone's original MAC.

## Wi-Fi, Bluetooth, SIM notes
- Wi-Fi uses your phone's own calibration file and MAC (from the dump, via the device ADT); auto-join needs 0790 (in the final
  line). SSH from jbts is Wi-Fi only (no USB networking on the Nord 2).
- Bluetooth firmware is read from your own vendor image; HFP/SCO and BT HID input do not work.
- SIM: tested only with the author's operator. Another operator's APN/IMS settings depend on 0710-0712 [host]. 5G is not
  available in iOS (iPhone X baseband identity).

## Recovery
- Phone stops in fastboot or boot-loops: `fastboot flash boot ~/nord2-private/my-rescue.img && fastboot reboot`, then pull logs.
- Return to the last good iOS image: `fastboot flash boot <your previous boot-ios.img>`.
- Never boot stock Android over the iOS userdata; never write bootloaders, modem, persist, nvdata/nvram, protect, GPT.

## Privacy
`~/nord2-private/` (dump, profile, modem blob, device ADT/kernel/boot image, logs) identifies your phone. Before sharing any
folder run `python3 -I components/release-20261115/tools/sanitize_release.py DIR` (structural checks; the owner-side identifier
list comes from `collect_private_ids.py`).
