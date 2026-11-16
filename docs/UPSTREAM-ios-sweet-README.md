# iOS 16 on the OnePlus Nord 2 5G (MT6893) — port source tree

This branch (`nord2-port-20261116`) is a **source export** of the iOS 16.7.x (iPhone X / iPhone10,3/10,6 userland) port to the
OnePlus Nord 2 5G (MediaTek Dimensity 1200, fastboot product `oplus6893`), built on top of the
[ios-sweet](https://github.com/dipa-studio/ios-sweet) SWEET boot shim/loader. It contains the port's own kernel patch series,
the source tree of the final kernel line, host tools, payload scripts, tests and engineering notes, with one commit per
component in the order the work was done (2026-10-26 .. 2026-11-16).

It is **not** an installable release. There are no images, no Apple or vendor binaries, no device dumps and no personal data in
this tree (see [docs/EXCLUDED.md](docs/EXCLUDED.md)). Some source is held back pending a licence decision (GPU userland and every
file derived from private reference material, see below), so **the kernel cannot be rebuilt from this tree alone**.

- How to try it on another Nord 2: [docs/INSTALL-NORD2.md](docs/INSTALL-NORD2.md) (read the status first: that flow has never
  run on a second phone).
- Piece-by-piece status and known limits: [docs/STATUS.md](docs/STATUS.md).
- Licences and credits: [LICENSES-AND-CREDITS.md](LICENSES-AND-CREDITS.md), [THIRD-PARTY.md](THIRD-PARTY.md).

## Status labels used in this document
- **[verified]** observed on the author's phone (kernel log over the USB console, screenshots, or the user using the feature).
- **[host]** built and tested on the host only (unit tests, fake-root or VM rehearsal); never observed on hardware.
- **[inferred]** concluded from code/disassembly/logs, not directly observed.
All [verified] items were verified on **one** phone (the author's). Nothing has been run on a second Nord 2.

## What works [verified, one phone]
Last kernel line run on the phone: `q10jfinal44ts` (image sha256 prefix `f55a15ea`), with the Metal userland payload `gfxall16`
(last recorded configuration).
- **Boot to SpringBoard** from `boot` via the SWEET stub/loader, iOS root on `userdata` (HFS+ root volume, grown to 107 GiB),
  16K MMU, SMP, display + touch, status bar placement (sbshift2, +22 px).
- **GPU** (Mali-G77, own XNU kernel driver + Mesa-based Metal userland): SpringBoard, Settings, Photos, Maps (16-gesture stress,
  0 GPU faults), Magnifier ~80 presents/s, App Library blur, Safari pages (wikipedia, apple.com) after the gfxall13-16 fixes,
  Subway Surfers ~90 presents/s. Live GPU DVFS 654<->886 MHz. (Terraria/landscape surfaces, gfxall17: [host] only.)
- **Graphics memory**: RAM8 graphics heap (0650/0651/0653) — 1577 MiB pool, 6665 MiB usable by XNU+heap, canaries clean.
- **MDP** (MediaTek display/media processor): copy/rotate/flip/NV12->BGRA/scale in hardware; Apple's `AppleM2ScalerCSCDriver`
  service backed by it handles 100 % of the camera's scaler requests (Magnifier preview).
- **Wi-Fi**: connects, auto-joins ~32-37 s after boot (0790, RSSI bounds); Safari/network/TLS work.
- **Cellular** (author's SIM/operator only): the modem boots from the phone's own NVRAM blob, registers and carries data.
  0715 (blob read from a root-volume file instead of the raw region) ran on q10jfinal19 and later; the Settings check of
  "Modem Firmware"/IMEI after 0715 is not recorded in the notes this export was made from.
- **Camera**: photos (JPEG/HEIC stills saved), 1080p30 and 1080p60 video with H.264/HEVC hardware encoding, hardware JPEG paths.
  (Torch/flash pieces 0495/0496/0501/0502/0560: built; a phone result is not recorded in the notes used for this export.)
- **Audio**: speaker, persistent volume; **haptics**: AW8697 short tick at fixed gain (q10hapd6a) confirmed by the user.
- **GNSS**: valid fix (first on 2026-11-14, kernel q10jfin + payload gnssfix22).
- **Sensors**: proximity; accelerometer streams to iOS; auto-rotate works in apps (0761); gyro/magnetometer stream on request.
- **Bluetooth**: controller up from the kernel (0720-0722 + n2btd), bluetoothd stays up, A2DP audio with AAC streamed stably
  (transport mode 2, run bt39a).
- **jbts** (rootless Procursus /var/jb + Sileo + OpenSSH + TrollStore): TrollStore opens and installs, the document picker opens;
  "On My iPhone" in Files works on q10jfinal44ts (0730-0734).

## What does not work / open items
- **5G**: blocked by design — iOS keys 5G on the baseband identity (iPhone X = Mav17, LTE-only); CommCenter reports
  "5G Switch support: kNotValid". Showing 5G would mean emulating an NR baseband under an NR iPhone identity [inferred]; 0714 is an
  experiment only (off).
- **Compass app** frozen: heading/device motion on this model comes from SPU sensor fusion; needs an emulated 0xFF00/0x10
  device-motion service + kernel 9-axis fusion (not written) [inferred].
- **Ambient light / Auto-Brightness**: the sensor hub never pushes light samples; 0762 adds a polled fallback [host]; MobileGestalt
  "ambient-light-sensor" is cached false [inferred].
- **GPU faults**: a translation fault used to kill the compositor's context permanently (flashing display); 0750 recovers the
  context and logs the faulting address [host, not yet observed on the phone after the fix].
- **Memory**: the remaining DRAM above the XNU window cannot be used as normal RAM (phase-2 images ram8c1/c1t/c1u/c1w do not boot);
  memory pressure kills daemons (0732 raises the memory-limit floor for lsd/installd/pkd/fileproviderd/containermanagerd [host]).
- **Bluetooth**: no HFP/SCO voice, no HID input (kernel lacks IOHIDUserDevice), no low-power controller states [inferred].
- **USB / charging** (0700-0702: boot without cable, MP2650 charge policy, gauge reading) and **SIM swap / PIN** fixes (0710-0712),
  **simcache1** no-SIM carrier cleanup: built and host-tested; no phone result is recorded in the notes this export was made from.
- **App Store**: genuine download/install is NO-GO (FairPlay); TrollStore/IPA install is the working path.
- **Thermal**: at ~40 C battery the governor caps CPU/GPU (therm=1).
- **Second Nord 2**: never tested. The kernel source pins values of the author's phone (GPT digest/CRC, Wi-Fi calibration digest,
  sensor calibration); in this export they are replaced by **zero placeholders**, so a kernel built from it without the per-device
  step would refuse Wi-Fi and halt in the UFS two-lane switch. See docs/INSTALL-NORD2.md.

## Layout
| Path | What |
|---|---|
| `xnu/patches/` | every kernel patch of the port (one file per piece, `NNNN-name.patch`), see `xnu/patches/SERIES.md` |
| `xnu/stage-q10jfinal44ts/` | the patched driver/kernel source tree exactly as compiled for the final line (minus withheld files) |
| `shim/`, `tools/`, `tests/`, `kmod/` | SWEET shim/loader, host tools and tests (from the ios-sweet base, extended) |
| `hwcodec/`, `video/` | hardware codec / camera / GNSS userland and kernel work (VideoToolbox plug-ins, JPEG, decoders) |
| `components/<dir>/` | per-work-item scripts, payload guest scripts, host tests, build recipes and notes, as in the work tree |
| `build/recipes/` | the top-level kernel build/flash chain scripts (historical; reference the original work-tree layout) |
| `host-tools/gpu-tools/` | rootfs/offline-update host tools (fastroot, HFS journal replay, offline Metal update) |
| `docs/` | install guide, status table, exclusions |

Paths inside scripts were rewritten: `$NORD2_WORK` = the original work tree (`research/nord2/gpu/overnight-20261024/work`),
`$NORD2_REPO` = the ios-sweet checkout, `$HOME` = the author's home. The build chain also needs the CI kernel object archive and
private combiner copies that are not part of this tree: the recipes document how each image was made, they are not a turnkey build.
