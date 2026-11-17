# iOS 16 port for the OnePlus Nord 2: installing it on YOUR phone
# Port iOS 16 per OnePlus Nord 2: installazione sul TUO telefono

**Status / Stato.** The tools are tested only on the author's computer. They have **never** run on a second Nord 2.
Expect to be the first tester. Everything that writes the phone is marked with ⚠.
Gli strumenti sono provati solo sul computer dell'autore. **Non** sono mai stati eseguiti su un secondo Nord 2.
Sarai il primo a provarli. Ogni passo che scrive sul telefono è segnato con ⚠.

**Your phone keeps its own identity.** Your IMEI, Wi-Fi/BT addresses, modem RF calibration, Wi-Fi calibration and sensor
calibration are read from **your** phone (read-only) and used only for **your** phone. The release contains nobody's
identity, and the tools refuse to build anything from another phone's data. Never share your `nord2-private/` folder.
**Il telefono mantiene la sua identità.** IMEI, indirizzi Wi-Fi/BT, calibrazione RF del modem, calibrazione Wi-Fi e dei
sensori vengono letti dal **tuo** telefono (sola lettura) e usati solo per il **tuo** telefono. La release non contiene
l'identità di nessuno e gli strumenti rifiutano di usare dati di un altro telefono. Non condividere mai `nord2-private/`.

## 0. Requirements / Requisiti
- OnePlus Nord 2 5G (MediaTek Dimensity 1200, fastboot product `oplus6893`). The kernel constants were made for the 128 GB
  layout; a different storage layout gives `REBUILD_NEEDED` at step 5 (stop there).
- The OxygenOS build listed in `release-manifest.json` → `supported_firmware` (sha256 of boot and recovery).
- Linux PC: python3, adb + fastboot (platform-tools), zopfli, hfsprogs (`fsck.hfsplus`), qemu-system-x86_64 with KVM, apfs-fuse
  and rsync (only to build the iOS root), payload-dumper-go (to extract the OTA).
- **All Android data is erased** (unlock + the iOS root overwrites userdata). Back up first.
- Requisiti uguali: Nord 2 5G, la build OxygenOS indicata, PC Linux con gli strumenti sopra. **Tutti i dati Android vengono
  cancellati.** Fai prima un backup.

## 1. ⚠ Unlock the bootloader / Sblocco del bootloader
EN: Settings → About → tap Build number 7× → Developer options → enable OEM unlocking and USB debugging.
`adb reboot bootloader`, then `fastboot flashing unlock` and confirm on the phone (this wipes the phone).
IT: Impostazioni → Info → tocca 7 volte Numero build → Opzioni sviluppatore → attiva Sblocco OEM e Debug USB.
`adb reboot bootloader`, poi `fastboot flashing unlock` e conferma sul telefono (cancella tutto).

## 2. Your stock images and your rescue / Immagini originali e rescue personale
```
payload-dumper-go -p boot,recovery,vendor OxygenOS-full-OTA.zip      # official full OTA of the supported build
sha256sum boot.img recovery.img                                     # must equal release-manifest.json supported_firmware
adb keygen ~/.android/adbkey  # only if you have no adb key yet / solo se non hai ancora una chiave adb
python3 host-tools/nord2-mkrescue.py boot.img recovery.img ~/nord2-private/my-rescue.img --adb-key ~/.android/adbkey.pub
```
EN: the rescue is a RAM-only Android with adbd that accepts only **your** key. IT: la rescue è un Android solo in RAM con
adbd che accetta solo la **tua** chiave.

## 3. ⚠ Boot the rescue / Avvio della rescue
`fastboot flash boot ~/nord2-private/my-rescue.img && fastboot reboot` → `adb devices` shows `recovery`.
(To go back to Android: `fastboot flash boot boot.img`. / Per tornare ad Android: `fastboot flash boot boot.img`.)

## 4. Read-only dump of your phone / Lettura (sola lettura) del tuo telefono
```
tools/n2dev/n2_read_device.sh ~/nord2-private/dump
```
EN: sets every partition read-only first, mounts ext4 with `ro,noload`, uses `dd if=` only. It saves your modem NVRAM
files, Wi-Fi calibration, sensor calibration, GPT and LP headers. IT: imposta prima le partizioni in sola lettura, monta ext4
con `ro,noload`, usa solo `dd if=`. Salva i file NVRAM del modem, calibrazioni Wi-Fi e sensori, GPT e intestazioni LP.

## 5. Profile and checks / Profilo e controlli
```
python3 tools/n2dev/n2_device_profile.py ~/nord2-private/dump --out ~/nord2-private/dump/device-profile.json --expect release-manifest.json
```
- `READY`: continue / continua.
- `REBUILD_NEEDED`: your storage/firmware layout differs from the kernel's compiled constants: **stop**, send the author the
  *list of mismatching names* printed (not the values). / il layout differisce dalle costanti del kernel: **fermati** e manda
  all'autore solo i *nomi* delle differenze.
- `REFUSED`: missing/invalid data (e.g. no IMEI files, invalid Wi-Fi calibration): **stop**. Nothing is ever borrowed from
  another phone. / dati mancanti o non validi: **fermati**. Non si prende mai nulla da un altro telefono.

## 6. Modem NVRAM from YOUR partitions / NVRAM del modem dalle TUE partizioni
```
mkdir -p ~/nord2-private/nvtree/vendor/etc && cp -a ~/nord2-private/dump/tree/* ~/nord2-private/nvtree/
# /vendor/etc/md and /vendor/etc/mdota from YOUR vendor.img (same OxygenOS build; EROFS, e.g. extract.erofs / 7z):
cp -a <vendor-extract>/etc/md <vendor-extract>/etc/mdota ~/nord2-private/nvtree/vendor/etc/
python3 tools/n2dev/mk_nvdisk_dev.py --tree ~/nord2-private/nvtree --out ~/nord2-private/n2nvdisk.bin --check
python3 tools/n2dev/n2_check_nvdisk.py ~/nord2-private/n2nvdisk.bin --dump ~/nord2-private/dump     # must print NVDISK_OWN_OK
python3 tools/n2dev/mk_nvfile_payload.py --blob ~/nord2-private/n2nvdisk.bin --dump ~/nord2-private/dump --out ~/nord2-private/nvfile
```
EN: the IMEI the modem reports is the one already stored in your phone; the tools never create or edit an IMEI.
IT: l'IMEI che il modem riporta è quello già memorizzato nel tuo telefono; gli strumenti non creano né modificano mai un IMEI.

## 7. Kernel and boot image for YOUR phone / Kernel e immagine di boot per il TUO telefono
```
python3 tools/n2dev/n2_kernel_pins.py apply kernel/kernel.tpl --map kernel/pins-map.json --profile ~/nord2-private/dump/device-profile.json --out ~/nord2-private/kernel.dev
python3 tools/n2dev/mk_device_adt.py kernel/template.adt ~/nord2-private/device.adt --profile ~/nord2-private/dump/device-profile.json --wifi ~/nord2-private/dump/wifi/WIFI
ZOPFLI=$(command -v zopfli) bash tools/n2dev/n2_pack_device.sh ~/nord2-private/boot-ios.img ~/nord2-private/kernel.dev ~/nord2-private/device.adt ~/nord2-private/my-rescue.img
```
EN: `kernel.tpl` contains placeholders where per-device values go (Wi-Fi calibration hash, GPT hashes, GPT CRC, sensor
calibration); `apply` writes yours; the packer refuses a kernel with placeholders left (it would not boot).
IT: `kernel.tpl` ha dei segnaposto dove vanno i valori del singolo telefono; `apply` scrive i tuoi; il packer rifiuta un
kernel con segnaposto rimasti (non si avvierebbe).

## 8. ⚠ iOS root filesystem / File system root di iOS
EN/IT: build it from **your own** IPSW download: `host-tools/mkiosroot.sh OS.dmg SystemOS.dmg AppOS.dmg ios-root.img` and
write it to userdata from the rescue. **The port's root overlay (system changes the port needs) is not packaged in this
release yet** (see the author's RELEASE_PLAN, item "rootfs"): until it is, this step cannot be completed by a recipient.
/ **L'overlay del port sul root non è ancora incluso in questa release**: finché non lo è, questo passo non è completabile.

## 9. ⚠ Payloads with the transaction tool / Payload con lo strumento di transazione
Create `~/.config/nord2/txn.env` with `ADB FASTBOOT RESCUE_IMG MIRROR HT INITRAMFS WORK` (see `tools/n2dev/n2_txn.sh`), then e.g.:
```
tools/n2dev/n2_txn.sh ~/nord2-private/nvfile/payload-nvfile1 ~/nord2-private/nvfile/guest-nvfile1.sh ~/nord2-private/nvfile/owned-nvfile1.json NVFILE1_OK ~/nord2-private/boot-ios.img nv1
tools/n2dev/n2_txn.sh payloads/simcache/payload-simcache1 payloads/simcache/guest-simcache1.sh payloads/simcache/owned-simcache1.json SIMCACHE1_OK ~/nord2-private/boot-ios.img sc1
```
Bluetooth (bt1): build the payload with YOUR vendor firmware (same OxygenOS build; sha256 must match `plan-bt1.json`):
```
mkdir -p ~/nord2-private/payload-bt1 && cp payloads/bt/n2btd ~/nord2-private/payload-bt1/ && cp payloads/bt/plan-bt1.json ~/nord2-private/payload-bt1/plan.json
cp <vendor-extract>/firmware/soc3_0_ram_bt_1a_1_hdr.bin <vendor-extract>/firmware/soc3_0_ram_mcu_1a_1_hdr.bin ~/nord2-private/payload-bt1/
tools/n2dev/n2_txn.sh ~/nord2-private/payload-bt1 payloads/bt/guest-bt1.sh payloads/bt/owned-bt1.json BT1_OK ~/nord2-private/boot-ios.img bt1
```
(The BT address is generated randomly on the first run on your phone and kept there.)

jbts (Sileo/OpenSSH/TrollStore): `cd payloads/jbts && python3 build_jbts.py` makes **your** SSH host keys and password, then
follow `payloads/jbts/SETUP.md`. One transaction at a time, a new TAG each time. / Una transazione per volta, TAG nuovo ogni volta.

## 10. First-boot checks / Controlli al primo avvio
- USB log (`cc-txn-TAG/ui-usb/xnu-live.log`): `NORD2 MODEM: fs disk blob source: rootfs file` and a file count; no
  `no valid header`; `NORD2 UFS X2 LIVE: rc=0`; Wi-Fi calibration accepted (no `EINVAL` from the Wi-Fi prepare step);
  `NORD2 SCP: calibration for sensor` lines `taken`.
- Settings → General → About: IMEI = the one printed on your box / `*#06#` under Android; Modem Firmware not empty;
  Wi-Fi Address = your phone's original Wi-Fi MAC.
- If the phone stops in fastboot or loops: `fastboot flash boot ~/nord2-private/my-rescue.img` and report the log.
- Controlli: IMEI = quello della confezione; Firmware modem non vuoto; indirizzo Wi-Fi = il MAC originale del telefono.

## Privacy
Your `~/nord2-private/` (dump, profile, blob, device ADT/kernel/boot image, logs) identifies your phone: never publish it.
`tools/sanitize_release.py DIR` checks any folder before you share it (structural checks: NVRAM blobs, calibration files,
device ADTs, private keys, root images, databases, logs).
