# iOS 16 on the OnePlus Nord 2 5G

**Apple's iOS 16.7 userland running on a OnePlus Nord 2 5G (MediaTek Dimensity 1200 / MT6893).**

This branch is the source of a port of Apple's open-source XNU kernel to the Nord 2 (fastboot product `oplus6893`). On top of
it runs the **unmodified iOS 16.7.x userland of the iPhone X** (iPhone10,3/10,6): launchd, the system daemons, backboardd,
SpringBoard and the stock apps. It grew out of [ios-sweet](https://github.com/dipa-studio/ios-sweet) (the same idea on a
Redmi Note 10 Pro) and reuses its boot shim and loader.

As of **2026-11-17** the phone boots to the **iOS home screen** with **GPU-accelerated graphics** (Metal on the Mali-G77),
touch, sound, Wi-Fi, Bluetooth, phone calls, SMS/MMS, front and rear cameras with hardware video encoding, GPS and sensors. Games such as Terraria
and Subway Surfers run, and IPA files can be installed with TrollStore.

> **Experimental. Not a daily driver.** It has been run on **one** phone only (the author's). Apple's security model is
> mostly switched off (permissive code signing and sandbox, no SEP). The iOS root lives in the `userdata` partition, so
> installing it **erases Android's user data**. Do not put anything personal on it.

> **This is the port's full working tree, not an installable release.** It holds all sources, recipes, payload scripts,
> notes and the port's own built userland binaries, so the work can be resumed on another PC
> ([resume/RESUME.md](resume/RESUME.md)). There are no kernel/boot images, no Apple or vendor files, no device dumps and
> no personal data (see [What is not in this repo](#what-is-not-in-this-repo-and-why)). A bootable kernel still needs a
> CI build and your phone's own values.

## What works

Kernel line on the phone: **`q10jfinal44ts`** (image sha256 prefix `f55a15ea`). Evidence levels used below:

- **user, this session** = checked on the phone by the owner between 2026-11-14 and 2026-11-17.
- **log** = recorded with measurements or the user's confirmation in [notes/ENGINEERING-LOG.md](notes/ENGINEERING-LOG.md)
  or the build notes (date given). These were **not re-checked** on the current kernel line for this README.

| Area | State | Evidence | Main knobs |
| --- | --- | --- | --- |
| Boot | Stock LK (unlocked) → boot image with our loader → XNU → iOS. First lockscreen 2026-10-20 | log (2026-10-20, user: "I see the lockscreen") | `nord2stopat=23` |
| CPU | All **8 cores** (4× Cortex-A55 + 4× Cortex-A78) running XNU; CPU DVFS, prime-first placement (0641/0642) | log (2026-10-20 8-core test; 0641-0643 2026-11-14) | `n2dvfs=3000`, `n2schpri=37` |
| Memory | ~**6.5 GiB** usable: 5088 MiB XNU window + 1577 MiB graphics pool (0650/0651/0653), canaries clean | log (2026-11-15 day report) | `kern.n2heap`, `kern.n2mem_budget_mib` |
| Storage | Own UFS driver, **two lanes**; iOS root = journaled HFS+ in `userdata`, grown to 107 GiB | log (2026-10-21 two-lane test; 2026-11-14 grow) | `nord2ufs2live=1` |
| Display | 1080×2400 panel shown as an iPhone X (1125×2436), **90 Hz** (89-90 frames/s measured); hardware overlay composition (95-100 % of frames, no CPU copy) | log (2026-11-13 night report) | `kern.n2fbhw`, `kern.n2panelhz` |
| Brightness | Brightness slider drives the panel (DBV 16→1290) | log (2026-11-14 night report) | `kern.n2bl` |
| GPU / Metal | Mali-G77 driver + Metal userland: SpringBoard, Maps, Safari, Photos, Magnifier, games; live GPU clock 654↔886 MHz; GPU fault recovery (0750) | user, this session (Terraria, 0750); log (Maps 0 faults, Magnifier ~80 fps, Safari 2026-11-15) | `n2gdv=73738`, `kern.n2gfrev` |
| MDP (media processor) | Scaling/rotation/colour conversion offloaded from the GPU; iOS scaler service handles 100 % of camera requests; **dual pipe** | user, this session (dual pipe on Magnifier); log (2026-11-15) | `kern.n2mdp_tune`, `kern.n2mdp_svc` |
| Touch | FT3518 touchscreen, multitouch, gestures; touch pacing 0620 | log (confirmed by the user many times since 2026-10-20); user, this session (Terraria landscape touch) | `kern.n2tpace` |
| Buttons | Power, volume up/down, alert slider | log (2026-10-20 onward; alert slider 2026-11-06) | `nord2input=1` |
| Haptics | AW8697 motor, short "tick" taps (q10hapd6a, fixed gain) | log (2026-11-14, user: "perfect") | `kern.n2hap` |
| Audio | Loudspeakers with calibrated volume; earpiece and microphone in calls; microphone in video recordings | log (speakers 2026-10-24; calls 2026-11-06; recordings 2026-11-12) | `nord2audio=1`, `kern.n2mic.enable` |
| Camera | **Rear and front cameras**: photos, video up to **4K**, 1080p60, Magnifier preview | user, 2026-11-17 (front camera, 4K); log (1080p30/60 2026-11-12) | `kern.n2vc.venc_fast` |
| Hardware codecs | **H.264 / HEVC encode** (camera recording up to 1080p60, HEVC recording) and decode through VideoToolbox; standalone decode of VP8/VP9/AV1/MPEG-2/4; **hardware JPEG** encode/decode | log (2026-11-11/12 codec report) | `/var/db/nord2-vcodec-run` |
| Flash / torch | **Torch and camera flash work** (MT6360 flash LEDs, 0496/0501/0502/0560) | **user, 2026-11-17** | `kern.n2torch`, `kern.n2torch_strobe` |
| Wi-Fi | WPA2 networks on **2.4 and 5 GHz**, DHCP, Safari; **auto-join at boot** (0790, ~32-37 s after boot); private address | user, 2026-11-17 (5 GHz); log (2026-10-23 IP/Safari test; 0790 2026-11-15) | `kern.n2wifi_rssithr` (-80), `kern.n2wifi_privmac` |
| Bluetooth | Pairing; **AAC audio streaming** stable (transport mode 2); starts ~24 s after boot together with Wi-Fi | user, this session | `kern.n2bt_mode` (2), `kern.n2bt_early` |
| Cellular / SIM | SIM works; modem firmware restored, clean Carrier row (0715 + nvfile, simcache); 4G data; **voice calls fully work** (VoLTE in/out, earpiece, microphone); **SMS and MMS** | user, this session and 2026-11-17 (calls, MMS); log (data/SMS 2026-11-06) | `kern.n2sim_fix`, `kern.n2sim_apn`, `kern.n2nr` |
| GNSS / GPS | MediaTek GNSS engine with a real fix (7 satellites); location services work for apps | log (first valid fix 2026-11-14; Location Services 2026-11-12) | `kern.n2gnss` |
| Sensors | Accelerometer, gyroscope and magnetometer data reach iOS; **auto-rotate** works; **proximity sensor** works; SCP sensor hub | user, this session (acc/gyro/compass data, auto-rotate); user, 2026-11-17 (proximity) | `kern.n2scp_motion`, `kern.n2scp_orient`, `kern.n2scp_lightpoll` |
| USB | USB device mode for the live kernel log; boots without a cable (0700) | log (2026-11-15); user, this session (charging) | `kern.n2usb_late` |
| Charging / battery | **Charging works** (MP2650 charger, 0700-0703); battery percentage from the fuel gauge (0701 adds current and time to full/empty) | user, this session (charging); 0701 gauge details: not separately verified | `kern.n2chg_ctl`, `kern.n2chg_ichg_ma` |
| Thermal / power | CPU/GPU DVFS, SoC thermal ladder (0643), hardware RTC (no standby heat) | log (2026-11-14/15) | `kern.n2dvfs_mode`, `nord2_lvts` |
| Device identity | Presented as an iPhone X (gesture UI); local activation (no Apple servers); IMEI is the phone's own | log (inherited from ios-sweet; IMEI/modem 2026-11-15) | - |
| Sileo / SSH / TrollStore | Procursus bootstrap, Sileo, OpenSSH (over Wi-Fi), TrollStore 2.1.1; **Install IPA File** picker; `ipainbox` installed Terraria and Subway Surfers | user, this session | `kern.n2sbcont`, `kern.n2memfloor` |
| Files app | "On My iPhone" location works (0734) | user, this session | `kern.n2fsapfs` |
| Apps | Settings, Photos, Maps, Safari, Magnifier, Camera, Control Center, App Library, Terraria (landscape), Subway Surfers (~89 fps) | user, this session (Terraria, Subway, Magnifier); log (others) | - |

Details per piece: [docs/STATUS.md](docs/STATUS.md). Every knob: [docs/CONFIGURATION.md](docs/CONFIGURATION.md).

## What does not work (yet)

- **5G**: not possible in iOS. The capability is keyed on the iPhone X baseband identity; LTE is the ceiling.
- **Compass app** freezes: iOS wants a fused "device motion" service (Apple's SPU), not raw magnetometer data.
- **Ambient light sensor** stays silent, so there is no Auto-Brightness.
- **Bluetooth**: a long-running test and a boot in airplane mode are still open; no hands-free (HFP/SCO) calls, no Bluetooth
  keyboards/mice.
- **NFC**: no driver; never worked on.
- **Not verified**: WPA3 / Enterprise Wi-Fi (built, never tested on a real network); hardware JPEG/HEIC inside the
  Camera app (photos are encoded in software).
- **Memory**: the DRAM above the XNU window is not usable as normal RAM; under memory pressure iOS kills daemons (0732 keeps
  the install/Files daemons alive).
- **Not part of this work and not supported**: SEP, Face ID/Touch ID, Apple Pay, App Store downloads (FairPlay), Apple ID
  services that need device attestation. Apps come from already-decrypted IPAs through TrollStore.
- **Another Nord 2**: never tested. The kernels built so far contain per-phone constants of the author's unit (see
  [docs/INSTALL-NORD2.md](docs/INSTALL-NORD2.md)); in this tree they are zero placeholders.
- **Building**: the kernel cannot be rebuilt from this repository alone (see [docs/COMPLETENESS.md](docs/COMPLETENESS.md)).
- **Licence note**: parts of the GPU stack are derived from a private reference project (ios-tegu); check
  [THIRD-PARTY.md](THIRD-PARTY.md) before reusing them.

## Configuration

Every kernel boot-arg, sysctl, marker file and environment variable of the port, with defaults, kill switches and the
exact default boot line of `q10jfinal44ts`, is in **[docs/CONFIGURATION.md](docs/CONFIGURATION.md)** (taken from the
source). In short:

- **At run time**: `sysctl -w kern.<name>=<value>` over SSH (jbts, Wi-Fi only). Changes are lost at reboot.
- **At boot**: boot-args are part of the boot image (the pack step of the recipes, e.g. `EXTRA_ARGS` in
  `build/recipes/build_q10jfi.sh`); the Nord 2 command line is full (607 characters), so adding one means removing one.
- **Userland switches**: marker files under `/var/db/nord2-*` and the payload variants (`VARIANT=… UNDO=1` of each
  `txn_*.sh`).

## How it boots

```
Preloader + LK (stock OnePlus bootloader, unlocked)
  -> boot partition: stock boot.img v2 whose kernel stream is replaced by
       [ loader (shim) | stock Android Image, byte-identical | SWEETXNU payload ]
     (fastboot boot does not work on the Nord 2, so the image is flashed to `boot`)
     -> loader: maps a 992 MiB window in the upper DDR bank, places XNU + Apple Device Tree + boot-args
        -> XNU (8 cores) -> UFS driver (two lanes) -> HFS+ iOS root in the userdata partition
           -> dyld + shared cache -> launchd -> daemons -> backboardd + SpringBoard
                                               \-> port daemons: vcodecd (codecs), n2mnld (GNSS), n2btd (Bluetooth)
```

There is no serial console. The kernel streams its log over **USB** (`tools/nord2-usb-console.py`) and keeps a ramoops
record. A RAM-only Android rescue image (with adbd that accepts only your key) is the way back: flash it to `boot` and the
port's transaction tools edit the iOS root offline from it. Details: [shim/nord2-xnu/README.md](shim/nord2-xnu/README.md).

## How to do it yourself

Short version. The full guide, with what to check after each step and a recovery table, is
**[docs/INSTALL-NORD2.md](docs/INSTALL-NORD2.md)**.

> Honest limit: **steps 0-6 can be done with this repository. Step 7 needs a kernel template** (`kernel.tpl`,
> `pins-map.json`, `template.adt`) that only comes from a full kernel build, which this repository cannot produce on its own.
> The whole flow for a second phone has **never been tested**.

0. **Get the inputs yourself**: a OnePlus Nord 2 5G (`oplus6893`), a Linux PC (python3, adb/fastboot, zopfli, hfsprogs,
   qemu with KVM, apfs-fuse, payload-dumper-go), the official OxygenOS full OTA and the iPhone X iOS 16.7.x IPSW.
1. **Unlock the bootloader** (erases the phone):
   ```sh
   adb reboot bootloader
   fastboot flashing unlock
   ```
2. **Build your own RAM rescue** from your stock images:
   ```sh
   payload-dumper-go -p boot,recovery,vendor OxygenOS-full-OTA.zip
   python3 tools/nord2-mkrescue.py boot.img recovery.img ~/nord2-private/my-rescue.img --adb-key ~/.android/adbkey.pub
   ```
3. **Boot the rescue**:
   ```sh
   fastboot flash boot ~/nord2-private/my-rescue.img && fastboot reboot
   adb devices        # -> "recovery"
   ```
4. **Read-only dump** of your phone's calibration and layout:
   ```sh
   components/release-20261115/tools/n2dev/n2_read_device.sh ~/nord2-private/dump
   ```
5. **Profile and check** (must say `READY`):
   ```sh
   python3 components/release-20261115/tools/n2dev/n2_device_profile.py ~/nord2-private/dump \
       --out ~/nord2-private/dump/device-profile.json --expect release-manifest.json
   ```
6. **Your own modem NVRAM blob** (from your partitions, never someone else's):
   ```sh
   python3 components/release-20261115/tools/n2dev/mk_nvdisk_dev.py --tree ~/nord2-private/nvtree --out ~/nord2-private/n2nvdisk.bin --check
   ```
7. **Kernel + device tree + boot image for your phone**: `n2_kernel_pins.py apply`, `mk_device_adt.py`, `n2_pack_device.sh`
   (blocked until a kernel template exists).
8. **iOS root** from your IPSW (`tools/mkiosroot.sh`) and the port's payloads (`n2_txn.sh`): see the guide; the port's root
   changes are not yet packaged as one overlay.

## Repository map

| Path | What |
| --- | --- |
| `xnu/patches/` | Every kernel piece of the port, one file per number (`NNNN-name.patch`); index in `xnu/patches/SERIES.md` |
| `xnu/stage-q10jfinal44ts/` | The patched kernel/driver source files exactly as compiled for the final line |
| `shim/` | Boot shim/loader and the Nord 2 driver sources and notes (`shim/nord2-xnu/`) |
| `tools/`, `tests/`, `kmod/` | Host tools (rescue builder, root image builder, USB console, packers) and tests |
| `components/<work-item>/` | One folder per work item, as in the original work tree: payload scripts, guest scripts, host tests, build recipes, notes |
| `components/release-20261115/` | Per-device tools for another phone (`n2dev/`), the release builder and the sanitizer |
| `hwcodec/`, `video/` | Hardware codecs, camera, JPEG, GNSS and location bridge (userland and kernel side) |
| `gpu/` | GPU userland stack (Metal frontend, transport, tests, docs) |
| `host-tools/gpu-tools/` | Root-filesystem tools used offline (HFS journal replay, offline updates) |
| `build/recipes/` | Top-level kernel build and flash chains (historical; they reference the original work-tree layout) |
| `notes/` | Engineering log 2026-10-14 .. 2026-11-16 (identity values redacted) |
| `resume/` | How to resume on another PC (`RESUME.md`), the assistant's project memory, agent briefs, early-phase research notes |
| `docs/` | Install guide, status, configuration, completeness audit, large artifacts, exclusions, licences |

Paths inside scripts were rewritten: `$NORD2_WORK` = the original work tree, `$NORD2_REPO` = the ios-sweet checkout,
`$HOME` = the author's home. More on what builds and what does not: [docs/SOURCE-EXPORT-NOTES.md](docs/SOURCE-EXPORT-NOTES.md).

## What is not in this repo and why

- **Personal data of the author's phone**: dumps, modem NVRAM, Wi-Fi/Bluetooth addresses, calibration values, logs, SSH keys.
  The kernel pieces 0260-0276 are withheld because they embedded the author's modem NVRAM; the final kernel loads the
  blob from the phone instead.
- **Apple and vendor files**: IPSW content, iOS SDK headers, Android/MediaTek firmware and partitions. Get them yourself.
- **Build outputs that are too big or carry per-phone values**: kernel and boot images, the CI kernel object archive
  (140 MB), VM images. Their sizes, sha256 and how to rebuild them: [docs/LARGE-ARTIFACTS.md](docs/LARGE-ARTIFACTS.md).
- **Secrets and third-party apps**: SSH host keys and the jbts bootstrap with its password, decrypted game IPAs,
  seeded shader caches (they reflect the owner's usage).

Full lists: [docs/EXCLUDED.md](docs/EXCLUDED.md) (what was withheld), [docs/COMPLETENESS.md](docs/COMPLETENESS.md)
(audit of this tree against the original work tree; what blocks a build) and
[docs/LARGE-ARTIFACTS.md](docs/LARGE-ARTIFACTS.md). Downloads of Apple/vendor inputs: `tools/fetch-ipsw.sh`,
`tools/fetch-xnu-build.sh`, `tools/fetch-oxygenos.sh`.

## Credits and licences

Built on Apple's open-source releases (xnu, IOHIDFamily, libpthread, hfs), [ios-sweet](https://github.com/dipa-studio/ios-sweet),
MediaTek's and OPLUS's published Linux kernel sources (as hardware references), Mesa, HakiOS/hackios (audio, Bluetooth and
camera references), Chlorophyll (AIR frontend), Procursus, Sileo, OpenSSH, TrollStore and ldid. iOS itself is Apple's and is not distributed here. See
[LICENSES-AND-CREDITS.md](LICENSES-AND-CREDITS.md) and [THIRD-PARTY.md](THIRD-PARTY.md).
