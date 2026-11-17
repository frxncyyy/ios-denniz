# Configuration reference (q10jfinal44ts)

Everything you can configure in this port, taken from the source in this repository: kernel boot-args, kernel sysctls, the marker files and environment variables the userland pieces read, and the host transaction-script settings. It describes the final kernel line, **q10jfinal44ts**.

**Where the facts come from**

- Kernel: `xnu/stage-q10jfinal44ts/` (the stage tree exactly as compiled) and `xnu/patches/*.patch`. The `wifi_*` sources of that stage were added to the repository on 2026-11-17; their knobs are included.
- Pieces that change `osfmk/`, `pexpert/`, `bsd/vfs` or `IOStartIOKit.cpp` are compiled into the CI base kernel. Their source exists only as a patch, so they are marked "CI base".
- Userland: `components/*/` (payload `guest-*.sh`, `txn_*.sh`, daemons), `hwcodec/`, `video/` and `gpu/`. For each knob the newest copy of the source is cited.
- The default boot line comes from the image receipt of the q10jfinal44ts build and from the recipe chain described below. It contains no owner-specific values.

**Columns**

- **piece/patch/file**: the patch that introduced the knob (first `xnu/patches/NNNN-*.patch` that adds it), then the file and line in the final stage.
- **RW / RD**: RW sysctls can be changed at run time; RD sysctls are read-only.
- **default**: the initializer in the source, plus what the q10jfinal44ts boot line sets when that differs.
- **verified?**: "yes (user, 2026-11-14..17)" only where `docs/STATUS.md` or the README "What works" table says the owner checked that behaviour on the phone. For default-line boot-args it means "part of the line that runs on the phone". Everything else is "source only".

A change you make on one phone is untested on any other; the per-device pins in this tree are zero placeholders.

## Contents

- [Summary and key knobs](#summary-and-key-knobs)
- [The default boot line](#the-default-boot-line-of-q10jfinal44ts)
- [How to change a setting](#how-to-change-a-setting)
- [The n2gdv bit word](#the-n2gdv-bit-word)
- [Kill switches](#kill-switches-and-their-defaults)
- [Per-subsystem reference](#per-subsystem-reference)
- [Boot-args no longer in the final kernel](#boot-args-no-longer-in-the-final-kernel)
- [Stock XNU boot-args present in the stage files](#stock-xnu-boot-args-present-in-the-stage-files)
- [Undocumented in source](#undocumented-in-source)

## Summary and key knobs

| what | count |
|---|---|
| boot-arg names in the subsystem tables | 170 (140 rows; aliases and families share a row; includes the few stock args the port sets or checks) |
| boot-args parsed only by superseded code | 17 (listed separately) |
| other stock XNU boot-args in the stage files | 15 (named only) |
| port sysctls | 295 (287 in the final stage + 8 CI-base scheduler sysctls of 0099); 198 RW, 97 RD |
| removed (superseded) sysctls | 12 (listed separately) |
| userland knobs (markers, env, prefs, config files, host txn settings) | 125 rows covering about 266 names |
| kill-switch table rows | 36 |
| undocumented-in-source rows | 7 (plus the obsolete-marker list) |

| subsystem | key knob (default) |
|---|---|
| boot | boot line `nord2stopat=23` (full iOS); `n2ck` 1 (clock floor); `n2rtc` / `kern.n2rtc` 1 (RTC read+write) |
| memory | `kern.n2memfloor` 128 MB (0732); `kern.n2memgov` 1 (log only); `kern.n2heap` 1 (low-bank graphics heap); `kern.n2mem_budget_mib` 2240 |
| storage | `nord2ufs2live=1` (two-lane UFS); `kern.n2hfsapfs` 1 (0733) |
| display+GPU | `kern.n2fbhw` 3 (overlay composition); `n2fbhz` 60; `kern.n2panelhz` 90; `kern.n2gfrev` 1 (GPU fault recovery); `kern.n2bl` 1 |
| MDP | `kern.n2mdp_tune` 7 (dual pipe); `kern.n2mdp_svc` 1; marker `/var/db/nord2-mdp-off` absent |
| touch | `kern.n2tpace` 1 (touch pacing, boot `n2tp`); `nord2input=1` |
| haptics | `kern.n2hap` 1 (boot `n2hap`); `kern.n2hap_drvmax` 60 |
| audio | `nord2audio=1`; `kern.n2mic.enable` 1; `kern.n2call.route` 0 (handset) |
| camera+codecs | `kern.n2torch` 1, `kern.n2torch_strobe` 1; `kern.n2vc.venc_fast` 0 (vcodecd `venc-clock` auto); marker `nord2-vcodec-run` present |
| Wi-Fi | `kern.n2wifi_sae` 1; `kern.n2wifi_rssithr` -80 dBm (auto-join); `kern.n2wifi_8021x` 0x11 |
| Bluetooth | `kern.n2bt_mode` 2; `kern.n2bt_early` 1; `kern.n2bt` 1; marker `/var/db/nord2-bt/enable` present |
| cellular+SIM | `kern.n2sim_fix` 15; `kern.n2sim_apn` 1; `kern.n2nr` 1; `kern.n2nr_ui` 0; `n2bb` 1; marker `nord2-simcache/disabled` absent |
| GNSS | `kern.n2gnss` 0 (all on); `nord2gnss` on; markers `nord2-gnssd` + `nord2-locbridge` present |
| sensors | `kern.n2scp_motion` 2 (on demand); `kern.n2scp_orient` 1; `kern.n2scp_lightpoll` 1; `nord2scp` 2 |
| USB+charging | `kern.n2chg_ctl` 1; `kern.n2usb_late` 1; `kern.n2chg_ichg_ma` 1100; `kern.n2bat_poll_ms` 10000 |
| thermal+DVFS | `n2gdv=73738` (GPU DVFS acts, fast ramp, 886 MHz); `kern.n2dvfs_mode` 2; `nord2_lvts` 4 (auto); `n2dvfs=3000` |
| TrollStore+Files | `kern.n2sbcont` 1; `kern.n2fsapfs` 1; `kern.n2sblog` 1; `kern.n2memfloor` 128 |
| diagnostics+logging | `sweetlog` 1; `kern.n2sblog_left` 800; fplog1 Logging/Subsystems plists (optional) |

## The default boot line of q10jfinal44ts

This is the exact boot line packed into the q10jfinal44ts image. It is 607 characters long, which is the maximum: the loader buffer `BOOT_LINE_LENGTH` is 608 bytes including the terminating NUL. It has 43 tokens and no owner-specific values.

```
debug=0xc cpus=8 -noprogress nord2stop=0 nord2vm=1 nord2fb=1 nord2stopat=23 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin launchd_unsecure_cache=1 wdt=-1 nord2display=1 nord2input=1 nord2wifi=2 nord2fastlog=1 nord2ufsprofile=1 nord2wififw=1 nord2nortc=1 nord2ufs2live=1 nord2wifipersist=1 nord2wifirom=1 nord2battery=1 nord2wifidma=1 nord2wifimain=1 nord2wificap=1 nord2wifibasic=1 nord2wifiscan=1 nord2wifivcn33=1 nord2wifiios=1 nord2wifilive=1 nord2audio=1 nord2_gpu=1 nord2_metal=1 n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37 n2gdv=73738
```

How it is put together: the packer `components/cam60-20261112/pack_with_args_nosyms.py` starts from the reference command line in the CI `build-result.json`. It removes ` nord2fbdiag=1` and the `--drop-arg` tokens (`nord2wifiemi=0 nord2audioinstall=1 keepsyms=1`) and appends `nord2_gpu=1 nord2_metal=1`. Then come the recipe's `XA` tokens (`n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37`, set in `build/recipes/build_q10jfi.sh:152`) and the `EXTRA_ARGS` token (`n2gdv=73738`, set in `components/jbts-20261115/fin/build_q10jfinal3.sh:20` as `EXTRA_ARGS_OVERRIDE`). The packer refuses duplicate keys and any line of 608 characters or more. The build log reports `cmdlen 607 (0 free)`.

| token | parsed by | meaning |
|---|---|---|
| `debug=0xc` | stock XNU | debug flags; 0x8 = kernel printf to the console, 0x4 = NMI/debugger hook (stock meaning) |
| `cpus=8` | stock + port | start all 8 CPUs; the port's USB/SMP code requires cpus=8 for stop 21-23 |
| `-noprogress` | stock | no boot progress UI |
| `nord2stop=0` | port, CI base (0082) | do not return to Android after cpu_init |
| `nord2vm=1` | port, CI base (0082) | run the port's early VM path |
| `nord2fb=1` | port, CI base (0082) | early framebuffer console |
| `nord2stopat=23` | port (0082) | full iOS userland mode (no early stop); gates input, USB, Wi-Fi, UFS live mode |
| `rd=ufsroot0` | stock + port | root device = the port's UFS root |
| `ufsroot=userdata` | port (0028) | root lives in the userdata partition |
| `ufsrw=1` | port (0030) | UFS driver writable |
| `rootrw=1` | port (0031) | root mounted read-write |
| `unentitled_ios_sim_launch=1` | stock XNU TUNABLE | spawn without the sandbox-entitlement requirement (inherited from ios-sweet) |
| `ignition_level=1` | stock iOS userland (not in port source) | inherited from the ios-sweet line; per tools/mkiosroot.sh it makes the system look in the cryptex shared-cache location |
| `wdt_test=no_user_checkin` | stock iOS userland (not in port source) | inherited; watchdog test mode without the userland check-in |
| `launchd_unsecure_cache=1` | stock iOS userland (not in port source) | inherited; launchd accepts an unsecured cache |
| `wdt=-1` | stock (not in port source) | watchdog timer disabled |
| `nord2display=1` | port, CI base (0082) | hand the panel framebuffer to XNU |
| `nord2input=1` | port (0082) | start touch/keys |
| `nord2wifi=2` | port (0082) | Wi-Fi bring-up mode 2 (register snapshot + VCN18/VCN13 power prep) |
| `nord2fastlog=1` | port, CI base (0082) | fast early framebuffer log |
| `nord2ufsprofile=1` | port (0082) | UFS link observation; required by nord2ufs2live |
| `nord2wififw=1` | port (0082) | allow the Wi-Fi vendor firmware read from UFS |
| `nord2nortc=1` | port, CI base (0082) | do not wait for a missing IORTC provider |
| `nord2ufs2live=1` | port (0082) | UFS two-lane live mode (exact companion tokens required) |
| `nord2wifipersist=1` | port (0082) | Wi-Fi persistent power |
| `nord2wifirom=1` | port (0082) | Wi-Fi ROM patch stage |
| `nord2battery=1` | port (0082) | real read-only battery provider |
| `nord2wifidma=1` | port (0082) | Wi-Fi WFDMA stage |
| `nord2wifimain=1` | port (0082) | Wi-Fi main firmware stage |
| `nord2wificap=1` | port (0082) | Wi-Fi capability worker |
| `nord2wifibasic=1` | port (0082) | Wi-Fi basic configuration |
| `nord2wifiscan=1` | port (0082) | Wi-Fi scan backend |
| `nord2wifivcn33=1` | port (0082) | Wi-Fi VCN33 rail |
| `nord2wifiios=1` | port (0082) | Wi-Fi iOS interface (Apple80211) |
| `nord2wifilive=1` | port (0082) | Wi-Fi live data path |
| `nord2audio=1` | port (0082) | audio device node |
| `nord2_gpu=1` | port (0084), added by the packer | enable the GPU driver |
| `nord2_metal=1` | port (0084/0103), added by the packer | UI image: GPU cold start at 654 MHz |
| `n2fbhz=60` | port (0097), recipe XA | present on every vsync (default 30) |
| `n2dvfs=3000` | port (0116), recipe XA | CPU DVFS hand-over + ramp of the A78 clusters up to a 3000 MHz cap (i.e. their top OPP) |
| `n2pf=1` | port (0122), recipe XA | Mali hardware counters sampled per stats window |
| `n2schpri=37` | port (0099), recipe XA | lowest priority eligible for big-core placement = 37 (default 47) |
| `n2gdv=73738` | port, recipe EXTRA_ARGS | bit word = 2 + 8 + 8192 + 65536: GPU DVFS acts with the UI profile, fast-ramp profile, 886 MHz OPP allowed (see the n2gdv table) |

## How to change a setting

### At run time: sysctl over SSH

1. Wi-Fi must be up and the jbts payload (Procursus, OpenSSH) installed.
2. Connect with `ssh root@<phone-ip>`. The password and key come from the jbts build (see TrollStore+Files below).
3. Read a value with `sysctl kern.n2bt_mode`; change it with `sysctl -w kern.n2bt_mode=-1`.

- Only RW sysctls can be written. Most handlers log the change to the kernel log (a `NORD2 ...` line).
- **Values are lost at reboot.** The next boot uses the source default, or the boot-arg that seeds it.
- Some sysctls are read by their consumer only at a fixed moment. Examples: `kern.n2fbscaling` applies at the next backboardd start, `kern.n2wifi_rssithr` is read when wifid starts, `kern.n2vc.venc_fast` applies at the next encoder power-on, and the `kern.n2mdp_*` stages run at +45 s. The description column says when this applies.
- Userland marker files are created or removed with `touch` / `rm` over SSH, or with the matching payload variant. Nord2Metal markers apply when the app restarts; daemon gates apply when the daemon restarts or at the next reboot.

### At boot: boot-args in the pack step

Boot-args are baked into the boot image. To change one you rebuild the image (re-pack it):

- **Recipe chain of the final line:**
  1. `components/jbts-20261115/build_q10jfinal44ts.sh` calls `jbts-20261115/fin/build_q10jfinal3.sh`.
  2. That sets `EXTRA_ARGS_OVERRIDE` (default `n2gdv=73738`) and calls `fin/build_base.sh`.
  3. Then `fin/integ/build_all.sh` (`export EXTRA_ARGS=${EXTRA_ARGS_OVERRIDE:-n2gdv=9}`) and `fin/run_integ_cam_f10.sh` run.
  4. That generates `.builder-<NAME>.sh` from `build/recipes/build_q10jfi.sh`, which appends `XA` + `EXTRA_ARGS` and calls the packer.
- **To add or change a token:** set `EXTRA_ARGS_OVERRIDE="n2gdv=73738 newarg=1"` when calling `build_q10jfinal3.sh`. Or edit `XA` in the builder template for tokens every image should carry. To remove a token inherited from the CI line, pass `--drop-arg`.
- **The 607-character limit:** the q10jfinal44ts line already uses all of it ("cmdlen 607 (0 free)" in the build log), so any new token needs room made first.
  - Short aliases exist for exactly this reason: `n2fbhz` for `sweetfb_hz`, `n2fp` for `n2fbhw_pool`, `n2lvts`, `n2trk`, `n2wm`, `n2resv`.
  - Many pieces were given **bits in `n2gdv`** instead of their own token (changing the number costs no extra characters).
  - Many newer pieces are on by default and have only a long-form kill-switch boot-arg plus a sysctl.
  - The image check in `build_q10jfi.sh` asserts that the line equals the reference line minus `keepsyms=1` plus `EXTRA_ARGS`, and that it is at most 607 characters.
- **Tokens you must not touch:** `nord2ufs2live=1` arms only when `nord2stopat=23 cpus=8 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 nord2ufsprofile=1` are present exactly and `nord2ufs2lane` is absent. Otherwise the kernel halts and needs a manual reset (`ufs_two_lane_live.inc:37-50`).
- **Number format:** use decimal or explicit hex (`0x08`); XNU reads zero-padded decimal as octal (shim/nord2-xnu/README.md).

## The n2gdv bit word

`n2gdv` is one integer that many pieces read as a set of bits. It exists because the boot line has no room left. q10jfinal44ts uses **73738 = 2 + 8 + 8192 + 65536**. Bit 64 is shared by two pieces (0530 and 0501). The code comments say all bits up to 65536 are taken.

| value | effect when set | read in | in 73738 |
|---|---|---|---|
| 1..15 (low 4 bits) | GPU DVFS mode = kern.n2gdvfs: 0 off, 1 decisions logged, 2 act; +4 no wait for a gap between jobs; +8 UI profile (never below the cold-start OPP) | Nord2GPU.cpp:931 (0461/0492) | 2+8 = 10: GPU DVFS acts with the UI profile |
| 16 | CPU/SoC thermal: LVTS off + battery-only thermal ladder; CPU platform map = CCI column only (no prime-first, no B floors); DRAM rate probe | Nord2GPU.cpp:925, nord2_cpufreq.c:1088 | off |
| 32 | n2gctx=0: static GPU address spaces (7 clients) | Nord2GPU.cpp:408 (0500) | off |
| 64 | n2ck=0 (no calendar clock floor) AND n2bl=2 (new backlight architecture) - two pieces share this bit | kern_exec.c:8385 (0530), SweetFramebuffer.cpp:1186 (0501) | off |
| 128 | n2scpm=1: motion sensors always on (old) | nord2_scp.inc:922 (0531) | off |
| 256 | n2rtc=0: no RTC access | nord2_rtc.inc:111 (0540) | off |
| 512 | n2cut=1: punch-hole band rotation on | SweetFramebuffer.cpp:2438 (0551) | off |
| 1024 | n2hap=0: haptics off | nord2_haptic.inc:683 (0610) | off |
| 2048 | n2tp=0: touch pacing off | sweet_touch.c:927 (0620) | off |
| 4096 | n2jn=0: GPU JS NEXT pipelining off | Nord2GPU.cpp:415 (0630) | off |
| 8192 | GPU DVFS profile 1 = fast ramp (kern.n2gprof) | Nord2GPU.cpp:931 (0631) | ON |
| 16384 | GPU DVFS profile 2 = performance (wins over 8192) | Nord2GPU.cpp:931 (0631) | off |
| 32768 | n2gp=0: GPU pass overlap off | Nord2GPU.cpp:418 (0632) | off |
| 65536 | kern.n2gvolt=1: GPU 886 MHz OPP with a live VGPU/VSRAM raise to 800 mV | Nord2GPU.cpp:931 (0633) | ON |
| 262144 (bit 18) | none of the new 0760 SweetHID sensor services | SweetHID.cpp:778 (0760) | off |

## Kill switches and their defaults

Every switch that turns a piece off, in one place. "Boot" switches go in the boot line (or an `n2gdv` bit); "run" switches are sysctls; "file" switches are markers on the phone. More detail is in the per-subsystem tables.

| piece / feature | boot | run time | file | default |
|---|---|---|---|---|
| Bluetooth (0720-0722) | - | kern.n2bt=0 (also n2bt_publish=0, n2bt_early=0) | delete /var/db/nord2-bt/enable | on |
| Haptics (0610) | n2hap=0 / n2gdv +1024 | kern.n2hap=0 | - | on |
| Torch / flash (0496/0560) | n2torch=0 | kern.n2torch=0, kern.n2torch_strobe=0 | /var/db/nord2-camera-noflash (photo flash only) | on |
| Touch pacing (0620) | n2tp=0 / n2gdv +2048 | kern.n2tpace=0 | - | on |
| RTC (0540) | n2rtc=0 / n2gdv +256 | kern.n2rtc=0 | - | on (read+write) |
| Clock floor (0530) | n2ck=0 / n2gdv +64 | - | - | on |
| GPU dynamic AS (0500) | n2gctx=0 / n2gdv +32 | - | - | on |
| GPU JS NEXT (0630) | n2jn=0 / n2gdv +4096 | kern.n2gjsnext=0 | - | on |
| GPU pass overlap (0632) | n2gp=0 / n2gdv +32768 | kern.n2gpipe=0 | - | on |
| GPU step A (0660-0666) | n2gpc=0, n2gv4=0, n2gpr=0, n2girq=0, n2guv=0, n2gpg=0 | kern.n2gpreclean, n2gv4, n2gprio, n2guv, n2gpg = 0 | - | on |
| GPU fault revive (0750) | n2gfrev=0 | kern.n2gfrev=0 | - | on |
| GPU DVFS (0461/0631/0633) | n2gdv low bits 0 | kern.n2gdvfs=0 (or 1 = log only), n2gprof=0, n2gvolt=0 | - | acts (10), fast ramp, 886 MHz |
| CPU/SoC thermal ladder (0643/0641/0642) | n2gdv +16 / nord2_lvts=0 | kern.n2cpuplat=1 | - | on |
| Display IOMMU (0401/0411) | n2fbiommu=0 | kern.n2fbiommu=0 | - | 3 (all) |
| Overlay retain (0392) | n2fbretain=0 | - | - | on |
| Punch-hole rotation (0550) | n2cut=1 / n2gdv +512 turns it ON | kern.n2cut | - | off |
| Brightness (0495) | n2bl=0 | kern.n2bl=0 (record only) | - | acts |
| MDP scaler (0670-0691) | - | kern.n2mdp_svc=0, kern.n2mdp_probe=-1, kern.n2mdp_scale_off=1 | /var/db/nord2-mdp-off | on |
| Sensors (0531/0760-0762) | nord2scp=0, nord2sensors=0, n2gdv bit 18, n2scpm / n2gdv +128 | kern.n2scp_motion=0, kern.n2scp_orient=0, kern.n2scp_lightpoll=0 | - | on |
| GNSS (0580) | nord2gnss=0 | kern.n2gnss bit 0 | delete /var/db/nord2-gnssd (+ nord2-locbridge) | on |
| SIM fixes (0710-0714) | - | kern.n2sim_fix=0, kern.n2sim_apn=0, kern.n2nr=0 | /var/db/nord2-simcache/disabled | on (n2nr_ui off) |
| Cellular bridge / data | n2bb=0, nord2_net=0 | - | - | on |
| USB late check (0700) | - | kern.n2usb_late=0, kern.n2usb_strict=1 (old stop) | - | on |
| Charger control (0702) | - | kern.n2chg_ctl=0 (observe only) | - | on |
| Battery publishing (0701) | - | kern.n2bat_legacy=1 | - | new |
| Sandbox container (0730) | n2sbcont=0 | kern.n2sbcont=0 | - | 1 |
| Sandbox log (0731) | - | kern.n2sblog=0 | - | on |
| Memory floor (0732) | - | kern.n2memfloor=0 | - | 128 MB |
| HFS APFS fsctls (0733) | n2hfsapfs=0 | kern.n2hfsapfs=0 | - | on |
| Root reported as apfs (0734) | n2fsapfs=0 | kern.n2fsapfs=0 | - | on |
| Memory governor (0420) | - | kern.n2memgov=0 | - | 1 (log only) |
| Graphics heap (0440) | - | kern.n2heap=0 | - | on |
| Wi-Fi auto-join RSSI (0790) | - | kern.n2wifi_rssithr=0 (unsupported, as before) | - | -80 dBm |
| Hardware codecs | - | - | delete nord2-vcodec-run / nord2-video-claim-hw / nord2-video-hwdec; create nord2-video-off | on |
| Nord2Metal features | - | - | no-final, no-gfx200, no-gfx100a, no-api99a, no-shader99, ... (display+GPU) | on |
| Jailbreak / SSH / ipainbox | - | - | /var/jb/etc/nord2-jb/disabled, ssh-disabled, ipainbox-disabled | on |

## Per-subsystem reference

Each subsystem has up to three tables: boot-args, sysctls, and userland (marker files, environment, preferences, host tooling). Sysctl rows come straight from the `SYSCTL_*` macros. The "what it does" column is the macro's own description string, which starts with the piece number. Rows marked "(no description string in the macro)" are live counters or node children; see the file and line.

### boot

**Boot-args (16 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `nord2stop` | 0/1 | 1 (line: 0) | 1 = return to stock Android right after cpu_init (bring-up probe); 0 = keep running XNU | 0082 / osfmk/arm64/sweet_early.c (CI base, patch only) | must be 0 for iOS; 1 only for early bring-up tests | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2vm` | 0/1 | 0 (line: 1) | run the port's early VM path past arm_vm_init (console remap, cached chain-back values) | 0082 / sweet_early.c (CI base) | leave 1 | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2vmstop` | gate number | 0 (no gate) | stop at numbered gate N inside arm_vm_init and log NORD2-NEXT (regression walker) | 0082 / sweet_early.c (CI base) | bring-up only; any value stops the boot | source only |
| `nord2vmloop` | 0/1 | 0 | loop instead of returning at a nord2vmstop gate | 0082 / sweet_early.c (CI base) | bring-up only | source only |
| `nord2fb` | 0/1 | 0 (line: 1) | early framebuffer console from the start of XNU | 0082 / sweet_early.c (CI base) | leave 1 (logs on screen during boot) | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2display` | 0/1 | 0 (line: 1) | hand the panel framebuffer (address/size/pitch) to XNU display code; checked together with nord2stopat | 0082 / sweet_early.c (CI base) | leave 1 | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2stopat` | stop point 1..23 | 0 (line: 23) | 23 = full iOS userland (8 CPUs, read-write root, input/USB/Wi-Fi workers allowed); 1..13 = numbered early stops that return to Android (shim/nord2-xnu/README.md table); 16/21/22 older lanes | 0082 / sweet_early.c, sweet_touch.c:1395, sweet_ufs.c, sweet_usb.c:120 | must stay 23 for iOS; many drivers refuse to start otherwise | yes (user, 2026-11-14..17; in the line on the phone) |
| `cpus` | 1..8 | all (line: 8) | stock XNU CPU count; the port also checks it (stop 21/22/23 need cpus=8, stop 16 needs 1) | stock + 0065/0082 / SweetCPU.cpp:130, sweet_usb.c:186 | keep 8; 1 only for early stops | yes (user, 2026-11-14..17; in the line on the phone) |
| `cpumask` | bit mask | all | stock-style CPU mask honoured by the SMP bring-up | 0065 / osfmk/arm64/machine_routines.c (CI base) | debug only | source only |
| `rd` | device name | "" (line: ufsroot0) | root device; ufsroot0 = the port's UFS root. Nord2Baseband also uses it to find the preboot volume | stock + 0082 / Nord2Baseband.cpp:5217 | do not change | yes (user, 2026-11-14..17; in the line on the phone) |
| `rootrw` | 0/1 | 0 (line: 1) | mount root read-write; with stop 23 the early code expects 1 | 0031/0082 / bsd/vfs/vfs_subr.c, sweet_early.c (CI base) | keep 1 (iOS needs a writable root) | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2nortc` | 0/1 | 0 (line: 1) | skip the wait for an IORTC provider at IOKit start (the board has none; calendar setup still runs) | 0082 / iokit/Kernel/IOStartIOKit.cpp (CI base) | keep 1, else boot waits for a missing resource | yes (user, 2026-11-14..17; in the line on the phone) |
| `n2ck` | 0/1 | 1 | 0 = no calendar clock floor at the first exec (0530: date from patch date / root volume / RTC) | 0530 / kern_exec.c:8385 | kill switch; also n2gdv +64 | source only |
| `n2rtc` | 0/1/2 | 1 | MT6359P RTC: 0 off (no RTC access), 1 read + write the calendar, 2 read only | 0540 / nord2_rtc.inc:111 | kill switch; also n2gdv +256; sysctl kern.n2rtc | source only |
| `n2hwmodel` | NAME=MODEL[,...] | "" (build-time NORD2_HWMODEL_DEFAULT) | answer hw.model=MODEL to the process named NAME (e.g. Camera ultra-wide tables); nothing else changes | 0291 / kern_mib.c:294 | safe; costs boot-line characters (line is full) | source only |
| `unentitled_ios_sim_launch` | 0/1 | 0 (line: 1) | stock XNU TUNABLE in kern_exec.c: skip the sandbox-entitlement requirement on spawn | stock / kern_exec.c:258 | inherited from the ios-sweet line; keep | yes (user, 2026-11-14..17; in the line on the phone) |

**Sysctls (4)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2rtc` | int (handler), RW | 1 | 0540 MT6359P RTC: 0 off, 1 read + write the calendar into it (default), 2 read only | 0540 / `nord2_rtc.inc`:320 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2rtcstat` | string (handler), RD | - | 0540 MT6359P RTC state and counters | 0540 / `nord2_rtc.inc`:322 | read-only status/counter | source only |
| `kern.n2clockfloor` | u64, RD | status | NORD2 CLOCK: calendar seconds set at the first exec (0 = left alone) | 0530 / `kern_exec.c`:8367 | read-only status/counter | source only |
| `kern.n2clockfloor_src` | uint, RD | status | NORD2 CLOCK: 0 none, 1 patch date, 2 root volume, 3.. directories, 16 RTC (0540) | 0530 / `kern_exec.c`:8368 | read-only status/counter | source only |

### memory

**Boot-args (7 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `maxmem` | bytes | all | stock: limit usable RAM; the port's arm_init also logs/clamps it | 0010/0082 / osfmk/arm/arm_init.c (CI base) | debug only | source only |
| `n2vc` | compressor mode | 4 (compressor + swap) | vm_compressor mode chosen by the port when the stock vm_compressor arg is absent | 0482 / nord2_vmcomp.h:47 | tuning | source only |
| `vm_compressor` | stock mode | - | stock arg; when present it wins over n2vc | stock + 0482 / nord2_vmcomp.h:46 | tuning | source only |
| `n2wod` | 0/1 | 0 | new IOSurfaces pageable and wired on demand (sysctl kern.n2surf_wod) | 0480 / sweet_surface_wod_kern.inc:151 | experimental | source only |
| `n2madv` | 0/1 | 0 | GPU buffer MADVISE + purge under pressure (sysctl kern.n2gpu_madv) | 0481 / nord2_gpu_madv_kern.inc:28 | experimental | source only |
| `n2sp` | 0/1/2 | 2 (purge) | idle volatile IOSurfaces under pressure: 0 off, 1 log, 2 purge (sysctl kern.n2surf_purge) | 0417 / SweetSurface.cpp:1904 | tuning | source only |
| `n2mb` | MiB | 2240 | one budget for surfaces + GPU private memory (sysctl kern.n2mem_budget_mib; 0 = off) | 0417 / SweetSurface.cpp:1907 | tuning | source only |

**Sysctls (45)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2memfloor` | uint, RW | 128 | 0732: minimum per-process memory limit (MB) for lsd/installd/pkd/fileproviderd/containermanagerd, 0 off | 0732 / `kern_exec.c`:196 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | yes (user, 2026-11-14..17: fixed lsd kill + TrollStore registration) |
| `kern.n2memgov` | int, RW | 1 (log only) | NORD2 MEMGOV: 0 off, 1 log only, 2 kill | 0420 / `kern_exec.c`:8115 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2memgov_low` | int, RW | 200 permille | NORD2 MEMGOV: purge mark, permille of RAM | 0420 / `kern_exec.c`:8116 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2memgov_crit` | int, RW | 120 permille | NORD2 MEMGOV: kill mark, permille of RAM | 0420 / `kern_exec.c`:8117 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2memgov_maxband` | int, RW | 30 | NORD2 MEMGOV: highest victim band (< 100) | 0420 / `kern_exec.c`:8118 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2memgov_kills` | uint, RD | - | NORD2 MEMGOV: processes killed | 0420 / `kern_exec.c`:8119 | read-only status/counter | source only |
| `kern.n2memgov_would` | uint, RD | - | NORD2 MEMGOV: victims chosen in log-only mode | 0420 / `kern_exec.c`:8120 | read-only status/counter | source only |
| `kern.n2memgov_pauses` | uint, RD | - | NORD2 MEMGOV: kill-budget pauses (3 per 60 s, then 120 s) | 0484 / `kern_exec.c`:8127 | read-only status/counter | source only |
| `kern.n2surf_purge` | int, RW | 2 (boot-arg n2sp) | 0417: idle volatile surfaces under pressure: 0 off, 1 log only, 2 purge | 0417 / `SweetSurface.cpp`:241 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2surf_purge_mib` | uint, RW | 1228 | 0417: surface wired MiB above which idle volatile surfaces go | 0417 / `SweetSurface.cpp`:242 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2surf_purge_avail` | uint, RW | 40000 | 0417: available pages below which idle volatile surfaces go | 0417 / `SweetSurface.cpp`:243 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2surf_purge_idle` | uint, RW | 3 | 0417: s without a swap before a volatile surface may go | 0417 / `SweetSurface.cpp`:244 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mem_budget_mib` | uint, RW | 2240 (boot-arg n2mb) | 0417: one budget for surfaces + GPU private (MiB, 0 off) | 0417 / `SweetSurface.cpp`:245 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2surf_proc_mib` | uint, RW | 896 | 0417: surface MiB one process may make (0 off) | 0417 / `SweetSurface.cpp`:246 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2mem_floor` | uint, RW | 18000 | 0418: available pages under which allocations are refused | 0418 / `SweetSurface.cpp`:247 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mem_gpu_cap_mib` | uint, RW | 1536 | 0418: GPU private cap MiB | 0418 / `SweetSurface.cpp`:248 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2surf_cap_mib` | uint, RW | 2048 | 0418: surface wired cap MiB | 0418 / `SweetSurface.cpp`:249 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2surf_purged` | uint, RD | - | 0417: surfaces purged | 0417 / `SweetSurface.cpp`:250 | read-only status/counter | source only |
| `kern.n2surf_unpurged` | uint, RD | - | 0417: purged surfaces made non-volatile again | 0417 / `SweetSurface.cpp`:251 | read-only status/counter | source only |
| `kern.n2surf_purge_failed` | uint, RD | - | 0417: purges refused at the re-check or remap | 0417 / `SweetSurface.cpp`:1364 | read-only status/counter | source only |
| `kern.n2surf_unpurge_failed` | uint, RD | - | 0417: NonVolatile requests left on the sink (no memory) | 0417 / `SweetSurface.cpp`:1365 | read-only status/counter | source only |
| `kern.n2surf_purge_remap_bad` | uint, RD | - | 0417: client mappings left on the old pages | 0417 / `SweetSurface.cpp`:1366 | read-only status/counter | source only |
| `kern.n2gpu_madv` | int, RW | 0 (boot-arg n2madv) | 0481: GPU buffer MADVISE + purge under pressure: 0 off, 1 on | 0481 / `nord2_gpu_madv_kern.inc`:16 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2gpu_madv_thr` | uint, RW | 0 (= 20 % of RAM) | 0481: free + file-backed pages under which DONTNEED buffers go (0 = 20 % of RAM) | 0481 / `nord2_gpu_madv_kern.inc`:17 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2gpu_madv_now` | uint, RW | 0 | 0481: 1 = purge every DONTNEED buffer at the next tick | 0481 / `nord2_gpu_madv_kern.inc`:18 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2gpu_madv_marked` | uint, RD | - | 0481: buffers marked DONTNEED now | 0481 / `nord2_gpu_madv_kern.inc`:19 | read-only status/counter | source only |
| `kern.n2gpu_madv_purged` | uint, RD | - | 0481: buffers purged | 0481 / `nord2_gpu_madv_kern.inc`:20 | read-only status/counter | source only |
| `kern.n2heap` | int, RW | 1 | 0440: low-bank graphics heap: 0 off, 1 on (SAFE regions) | 0440 / `sweet_gfx_heap_kern.inc`:58 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2heap_check` | int, RW | 1 | 0440: 1 = also the CHECK regions mb11b mb12 mb13b | 0440 / `sweet_gfx_heap_kern.inc`:59 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2heap_gpu` | int, RW | 1 | 0440: 1 = GPU-only buffers >= 64 KiB from the heap | 0440 / `sweet_gfx_heap_kern.inc`:60 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2heap_mask` | uint, RW | all regions | 0440: region mask (bit = table index) | 0440 / `sweet_gfx_heap_kern.inc`:61 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2heap_cap_mib` | uint, RW | 2048 | 0440: heap MiB in use at most | 0440 / `sweet_gfx_heap_kern.inc`:62 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2heap_ready` | uint, RD | - | 0440: regions mapped and checked | 0440 / `sweet_gfx_heap_kern.inc`:63 | read-only status/counter | source only |
| `kern.n2heap_off` | uint, RD | - | 0440: regions switched off for this boot | 0440 / `sweet_gfx_heap_kern.inc`:64 | read-only status/counter | source only |
| `kern.n2heap_used_kib` | uint, RD | - | (no description string in the macro) | 0440 / `sweet_gfx_heap_kern.inc`:65 | read-only status/counter | source only |
| `kern.n2heap_live` | uint, RD | - | (no description string in the macro) | 0440 / `sweet_gfx_heap_kern.inc`:66 | read-only status/counter | source only |
| `kern.n2heap_full` | uint, RD | - | (no description string in the macro) | 0440 / `sweet_gfx_heap_kern.inc`:67 | read-only status/counter | source only |
| `kern.n2heap_foreign` | uint, RD | - | (no description string in the macro) | 0440 / `sweet_gfx_heap_kern.inc`:68 | read-only status/counter | source only |
| `kern.n2surf_wod` | int, RW | 0 (boot-arg n2wod) | 0480: new IOSurfaces pageable, wired on demand: 0 off, 1 on | 0480 / `sweet_surface_wod_kern.inc`:27 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2surf_wod_idle` | uint, RW | 5 | 0480: s without a holder before the unwire | 0480 / `sweet_surface_wod_kern.inc`:28 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2surf_wod_min` | uint, RW | 65536 | 0480: smallest pageable surface (bytes) | 0480 / `sweet_surface_wod_kern.inc`:29 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2surf_wod_live` | uint, RD | - | 0480: pageable surfaces alive | 0480 / `sweet_surface_wod_kern.inc`:30 | read-only status/counter | source only |
| `kern.n2surf_wod_wired` | uint, RD | - | 0480: ... wired now | 0480 / `sweet_surface_wod_kern.inc`:31 | read-only status/counter | source only |
| `kern.n2surf_wod_fail` | uint, RD | - | 0480: prepare() failures | 0480 / `sweet_surface_wod_kern.inc`:32 | read-only status/counter | source only |
| `kern.n2surf_wod_unmatched` | uint, RD | - | 0480: holds given back that were never taken (must stay 0) | 0480 / `sweet_surface_wod_kern.inc`:33 | read-only status/counter | source only |

### storage

**Boot-args (8 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `ufsroot` | partition name | none (line: userdata) | partition the UFS root driver accepts (Nord 2 port: userdata only) | 0028/0082 / sweet_ufs.c:1598 | do not change | yes (user, 2026-11-14..17; in the line on the phone) |
| `ufsrw` | 0/1 | 0 (line: 1) | allow writes in the UFS driver | 0030/0082 / sweet_ufs.c:1606 | keep 1 | yes (user, 2026-11-14..17; in the line on the phone) |
| `ufscache` | 0/1 | 0 | UFS write cache mode | 0069 / sweet_ufs.c:1619 | tuning; data-safety risk | source only |
| `ufswait` | 0/1 | 0 | wait for each UFS request to complete | 0069 / sweet_ufs.c:1620 | tuning | source only |
| `nord2ufsprofile` | 0/1 | 0 (line: 1) | UFS link observation/profile (UIC DME_GET reads, request counters); required by nord2ufs2live | 0082 / sweet_ufs.c:1625 | keep 1 | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2ufs2lane` | 0/1 | 0 | older two-lane stop-mode test; must be ABSENT when nord2ufs2live=1 | 0082 / sweet_ufs.c:1583,1714 | do not add to the line | source only |
| `nord2ufs2live` | exact token =1 | absent (line: 1) | UFS two-lane live mode; armed only if the exact companion tokens are present (nord2stopat=23 cpus=8 rd=ufsroot0 ufsroot=userdata ufsrw=1 rootrw=1 nord2ufsprofile=1), else halts with "manual reset" | 0082 / ufs_two_lane_live.inc:37 (custom parser) | do not edit the companion tokens | yes (user, 2026-11-14..17; in the line on the phone) |
| `hfs_corruption_panics` | 0/1 | DEBUG builds only | stock HFS: panic on detected corruption | 0026 / bsd/sweet_hfs (CI base) | debug only | source only |

**Sysctls (1)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2hfsapfs` | uint, RW | 1 (boot-arg n2hfsapfs) | 0733: HFS answers APFS 'J' fsctls of the file-provider stack: 0 off, 1 on | 0733 / `sweet_amfi.c`:279 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |

### display+GPU

**Boot-args (40 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `nord2_gpu` | 0/1 | 0 (line: 1) | enable the Nord2GPU (Mali-G77) driver | 0084 / Nord2GPU.cpp:921 | required for the UI | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2_metal` | 0/1 | 0 (line: 1) | UI/Metal image: GPU cold-start OPP 654 MHz instead of the minimum | 0084/0103 / Nord2GPU.cpp:444,964 | keep 1 | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2_gpu_khz` | 350000..886000 | 0 (policy) | explicit GPU cold-start clock (only when the boot chain left MFGPLL off) | 0084/0091 / Nord2GPU.cpp:439 | thermal risk | source only |
| `n2mhz` | MHz alias | 0 | short alias of nord2_gpu_khz (n2mhz=350 = old minimum) | 0095 / Nord2GPU.cpp:442 | thermal risk | source only |
| `nord2_gpu_cache` | 0/1 | 0 | 1 = whole-context cache maintenance for every job (old, slow) | 0084/0087 / Nord2GPU.cpp:391 | debug only | source only |
| `n2ext` | bits 0..7 | 0 | GPU allocation extensions: 1 GPU-only, 2 write-combine | 0136/0137 / Nord2GPU.cpp:392 | experimental | source only |
| `nord2_gpu_track (alias n2trk)` | 0..2 | 2 | buffer tracking level for cache maintenance (1 = old default) | 0084/0096 / Nord2GPU.cpp:397 | tuning | source only |
| `nord2_gpu_wmask (alias n2wm)` | 0/1 | 0 | trust a job's write mask for the post-job invalidate | 0110 / Nord2GPU.cpp:401 | risk of stale data | source only |
| `nord2_gpu_resv (alias n2resv)` | 0/1 | 1 | display-surface reservation selectors (0 = off) | 0112 / Nord2GPU.cpp:404 | kill switch | source only |
| `n2gctx` | 0/1 | 1 | dynamic GPU address spaces (any number of Metal clients); 0 = static, 7 clients | 0500 / Nord2GPU.cpp:408 | kill switch; also n2gdv +32 | source only |
| `n2jn` | 0/1 | 1 | GPU JS NEXT pipelining (kern.n2gjsnext) | 0630 / Nord2GPU.cpp:415 | kill switch; also n2gdv +4096 | source only |
| `n2gp` | 0/1 | 1 | pass overlap for precise ABI-v3 jobs (kern.n2gpipe) | 0632 / Nord2GPU.cpp:418 | kill switch; also n2gdv +32768 | source only |
| `n2gpc` | 0/1 | 1 | CPU cache clean at submit outside the GPU lock (kern.n2gpreclean) | 0660/0661 / Nord2GPU.cpp:421 | kill switch | source only |
| `n2gv4` | 0/1 | 1 | accept GPU submit ABI v4 (128 buffers) (kern.n2gv4) | 0660/0661 / Nord2GPU.cpp:422 | kill switch | source only |
| `n2gpr` | 0/1 | 1 | compositor-first GPU scheduling (kern.n2gprio) | 0660/0662 / Nord2GPU.cpp:423 | kill switch | source only |
| `n2girq` | 0/1 | 1 | big GPU IRQ path (0663) | 0660 / Nord2GPU.cpp:424 | kill switch | source only |
| `n2guv` | 0/1 | 1 | per-OPP vendor GPU voltage (kern.n2guv) | 0665 / Nord2GPU.cpp:425 | kill switch; voltage | source only |
| `n2gpg` | 0/1 | 1 | shader-core power gating when idle (kern.n2gpg) | 0665/0666 / Nord2GPU.cpp:427 | kill switch | source only |
| `n2gfrev` | 0/1 | 1 | revive a GPU client after its MMU fault (kern.n2gfrev) | 0750 / Nord2GPU.cpp:426 | kill switch | yes (user, 2026-11-14..17; in the line on the phone) |
| `n2gpool` | 0/1 | 1 | GPU import of display-pool pages + write fences (0 = refused) | 0395 / Nord2GPU.cpp:935 | kill switch | source only |
| `nord2_gpu_irq` | 0/1 | 1 | GPU job interrupt (polling stays the fallback) | 0092/0132 / Nord2GPU.cpp:932 | kill switch | source only |
| `nord2_gpu_irq_spi` | 32..900 | built-in SPI | override the GPU job interrupt number | 0092 / Nord2GPU.cpp:933 | debug only | source only |
| `nord2_gpu_priv_mb / nord2_gpu_glob_mb` | MiB | driver maximum on >=1.5 GiB RAM | per-client / global GPU private memory caps | 0088/0125 / Nord2GPU.cpp:937,941 | tuning | source only |
| `n2lat` | bits 0..15 | 0 | GPU latency changes: 1 BH thread, 2 late release, 4 zero skip, 8 deferred owner free (kern.n2gpu_lat) | 0400 / Nord2GPU.cpp:954 | experimental | source only |
| `sweetfb_native` | 0/1 | 0 | show the panel at native size instead of the iPhone X logical size | 0061 / SweetFramebuffer.cpp:1036, SweetHID.cpp:935 | breaks the iPhone X layout | source only |
| `n2fbretain` | 0/1 | 1 | keep layers outside a swap's mask (0392) | 0392 / SweetFramebuffer.cpp:1067 | kill switch | source only |
| `sweetfb_hz (alias n2fbhz)` | Hz; 0 or >=60 = every vsync | 30 (line: 60) | framebuffer present rate (flush ticks) | 0056/0097 / SweetFramebuffer.cpp:1081 | keep 60 | yes (user, 2026-11-14..17; in the line on the phone) |
| `n2panelhz` | 60/90 | 90 (panel default) | panel refresh at boot; a reduced present rate forces 60 (kern.n2panelhz at run time) | 0155 / SweetFramebuffer.cpp:1086 | safe | source only |
| `n2telead` | 0..4000 us | 0 | tearing-effect lead time | 0156 / SweetFramebuffer.cpp:1100 | tuning | source only |
| `sweetfb_swapwait (alias n2swait)` | 0..2 | 1 | SwapWait semantics (0 = always success, calls logged) | 0102/0393 / SweetFramebuffer.cpp:1106 | tuning | source only |
| `n2bl` | 0/1/2 | 1 | backlight: 1 act on CoreBrightness, 0 record only, 2 also publish the new backlight architecture (nits) | 0495/0501 / SweetFramebuffer.cpp:1186 | also n2gdv +64; kern.n2bl | source only |
| `sweetfb_noconsole` | 0/1 | 0 | mute the console once the framebuffer is taken by iOS | 0046/0413 / SweetFramebuffer.cpp:1513 | safe | source only |
| `n2fbhwpm / n2fbhwcsw` | 0..3 | 0 / 0 | overlay upper-layer colour format / R-B byte swap at boot (kern.n2fbhwpm, kern.n2fbhwcsw) | 0414 / SweetFramebuffer.cpp:2432,2434 | wrong colours if changed | source only |
| `n2cut` | 0/1 | 0 | punch-hole band rotation fallback (off since 0551; iOS moves the status bar via sbshift1) | 0550/0551 / SweetFramebuffer.cpp:2438 | also n2gdv +512 | source only |
| `nord2zoomreset` | exact token =1 | absent | once before launchd, rewrite the display-zoom plist (com.apple.iokit.IOMobileGraphicsFamily.plist) to a fixed 83-byte value, with backup | zoom_reset.c:45 (custom parser) | repair tool; one boot only | source only |
| `n2su` | 0..2 | 2 | IOSurface use count: 0 off, 1 user traps, 2 + display/GPU holds (kern.n2surfuse) | 0398 / SweetSurface.cpp:132 | tuning | source only |
| `n2fbhw_pool (alias n2fp)` | 0/1 | 0 | big BGRA IOSurfaces from the display pool (kern.n2fbhw_pool) | 0394 / SweetSurface.cpp:267 | experimental | source only |
| `n2fbpoolc` | 0/1 | 0 | display-pool copy-back | 0394 / SweetSurface.cpp:271 | experimental | source only |
| `n2fbiommu` | 0..3 | 3 (all) | display IOMMU: 0 off, 1 map-only, 2 single, 3 all (kern.n2fbiommu) | 0401/0411 / sweet_fb_iommu_hw.h:869 | kill switch | source only |
| `n2fbiommu_delay` | 0..600 s | 3 | boot arming delay of the display IOMMU | 0411/0470 / sweet_fb_iommu_hw.h:872 | tuning | source only |

**Sysctls (60)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2gpreclean` | int, RW | 1 (boot-arg n2gpc) | 0661: CPU cleans at submit outside the GPU lock | 0660 / `Nord2GPU.cpp`:222 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2gv4` | int, RW | 1 (boot-arg n2gv4) | 0661: accept GPU submit ABI v4 (128 buffers) | 0660 / `Nord2GPU.cpp`:223 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2gprio` | int, RW | 1 (boot-arg n2gpr) | 0662: compositor-first GPU scheduling | 0660 / `Nord2GPU.cpp`:224 | hardware limit: keep within the range in the description | source only |
| `kern.n2guv` | int, RW | 1 (boot-arg n2guv) | 0665: GPU per-OPP vendor voltage | 0665 / `Nord2GPU.cpp`:235 | hardware limit: keep within the range in the description | source only |
| `kern.n2guvm` | int, RW | 12500 | 0665: margin above the vendor VGPU, uV (0..50000) | 0665 / `Nord2GPU.cpp`:236 | hardware limit: keep within the range in the description | source only |
| `kern.n2gpg` | int, RW | 1 (boot-arg n2gpg) | 0666: GPU shader-core power gating when idle | 0665 / `Nord2GPU.cpp`:237 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2gpgus` | int, RW | 10000 | 0666: idle time before the shader cores are powered down, us | 0665 / `Nord2GPU.cpp`:238 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2gfrev` | int, RW | 1 (boot-arg n2gfrev) | 0750: revive a GPU client after its MMU fault (0 = stays faulted until close) | 0750 / `Nord2GPU.cpp`:243 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: GPU fault recovery) |
| `kern.n2gpipe` | int, RW | 1 (boot-arg n2gp) | 0632: precise GPU jobs may overlap older fragment jobs of their context (1 on, 0 off) | 0632 / `Nord2GPU.cpp`:244 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2gjsnext` | int, RW | 1 (boot-arg n2jn) | 0630: GPU JS NEXT pipelining (1 on, 0 no new NEXT writes) | 0630 / `Nord2GPU.cpp`:245 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2gpu_lat` | int, RW | 0 (boot-arg n2lat) | 0400: GPU latency changes off-mask: 1 BH thread, 2 late release, 4 zero skip, 8 deferred owner free | 0400 / `Nord2GPU.cpp`:246 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bl` | int (handler), RW | 1 | 0495 panel brightness: 1 act on CoreBrightness requests (DCS 0x51), 0 record only | 0495 / `SweetFramebuffer.cpp`:798 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bl_floor` | int (handler), RW | 16 | 0495 lowest DBV sent (4..2047, default 16) | 0495 / `SweetFramebuffer.cpp`:800 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bl_max` | int (handler), RW | 2047 | 0495 highest DBV sent (4..2047, default 2047; HBM never) | 0495 / `SweetFramebuffer.cpp`:802 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bl_nits` | int (handler), RW | 400 | 0495 nits that map to DBV 2047 (100..1000, default 400) | 0495 / `SweetFramebuffer.cpp`:804 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bl_safe` | int (handler), RW | 300 | 0501 lowest DBV shown until a request above it has been seen (0..2047, default 300; 0 = off) | 0501 / `SweetFramebuffer.cpp`:806 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2blstat` | string (handler), RD | - | 0495 brightness state and counters | 0495 / `SweetFramebuffer.cpp`:808 | read-only status/counter | source only |
| `kern.n2fbhw` | int (handler), RW | 3 | 0396/0416 overlay composition: 2 single layer, 3 multi-layer up to kern.n2fbhwlayers (no CPU path) | 0396 / `SweetFramebuffer.cpp`:900 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhwpm` | int (handler), RW | 0 | 0396/0399 upper-layer CON: 0 straight 0x21ff, 1 legacy 0x008031ff, 2 premultiplied BGRA 0x028031ff, 3 DRM-table 0x038031ff | 0396 / `SweetFramebuffer.cpp`:902 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhwlayers` | int (handler), RW | 6 | 0402 overlay slots per pipe 2..6 | 0402 / `SweetFramebuffer.cpp`:904 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbh6` | int (handler), RW | 1 | 0402b H6 fence 0/1 | 0402 / `SweetFramebuffer.cpp`:906 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbscaling` | int (handler), RW | 0 | 0402b IOMFBScalingLimits 0/1 | 0402 / `SweetFramebuffer.cpp`:907 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhwcsw` | int (handler), RW | 0 | 0399 flip CON BYTE_SWAP (R<->B): bit 0 layer 1, bit 1 layer 0 | 0399 / `SweetFramebuffer.cpp`:908 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhwstat` | string (handler), RD | - | 0396 HW composition counters | 0396 / `SweetFramebuffer.cpp`:910 | read-only status/counter | source only |
| `kern.n2cut` | int (handler), RW | 0 (off since 0551) | 0550 punch-hole band rotation 0/1 | 0550 / `SweetFramebuffer.cpp`:956 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2cut_band` | int (handler), RW | panel constant | 0550 band rows | 0550 / `SweetFramebuffer.cpp`:957 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2cut_ear` | int (handler), RW | panel constant | 0550 left ear end x | 0550 / `SweetFramebuffer.cpp`:958 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2cut_dx` | int (handler), RW | 110 | 0550 left ear shift right | 0550 / `SweetFramebuffer.cpp`:959 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2cut_tear` | int (handler), RW | panel constant | 0550 right ear start x | 0550 / `SweetFramebuffer.cpp`:960 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2cut_tdx` | int (handler), RW | 32 | 0550 right ear shift left | 0550 / `SweetFramebuffer.cpp`:961 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2cutstat` | string (handler), RD | - | 0550 counters | 0550 / `SweetFramebuffer.cpp`:962 | read-only status/counter | source only |
| `kern.n2panelhz` | int (handler), RW | 90 at boot (read = current rate) | 0397 panel refresh: read the rate now, write 90 or 60 to switch | 0397 / `SweetFramebuffer.cpp`:999 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbpace` | string (handler), RD | - | 0397 present pacing since boot or kern.n2fbpacereset | 0397 / `SweetFramebuffer.cpp`:1001 | read-only status/counter | source only |
| `kern.n2fbpacereset` | int (handler), RW | action | 0397 write 1: reset the kern.n2fbpace counters (at the next present) | 0397 / `SweetFramebuffer.cpp`:1003 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2surfuse` | int, RW | 2 (boot-arg n2su) | 0398: IOSurface use count: 0 off, 1 user traps, 2 + display/GPU holds | 0398 / `SweetSurface.cpp`:119 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2surfuse_report` | int, RW | 0 | 0398: 1 = log the use-count report at the next trap | 0398 / `SweetSurface.cpp`:120 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2surfuse_live` | u64, RD | - | 0398: use-count holds now | 0398 / `SweetSurface.cpp`:121 | read-only status/counter | source only |
| `kern.n2surfuse_inc` | u64, RD | - | 0398: user increments | 0398 / `SweetSurface.cpp`:122 | read-only status/counter | source only |
| `kern.n2surfuse_dec` | u64, RD | - | 0398: user decrements | 0398 / `SweetSurface.cpp`:123 | read-only status/counter | source only |
| `kern.n2fbhw_pool` | int, RW | 0 (boot-arg n2fbhw_pool/n2fp) | 0394: big BGRA IOSurfaces from the display pool | 0394 / `SweetSurface.cpp`:182 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhw_pool_live` | uint, RD | - | (no description string in the macro) | 0394 / `SweetSurface.cpp`:183 | read-only status/counter | source only |
| `kern.n2fbhw_pool_used_kib` | uint, RD | - | (no description string in the macro) | 0394 / `SweetSurface.cpp`:184 | read-only status/counter | source only |
| `kern.n2fbhw_pool_full` | uint, RD | - | (no description string in the macro) | 0394 / `SweetSurface.cpp`:185 | read-only status/counter | source only |
| `kern.n2fbhw_pool_foreign` | uint, RD | - | (no description string in the macro) | 0394 / `SweetSurface.cpp`:186 | read-only status/counter | source only |
| `kern.n2fbhw_pool_policy` | int, RW | 1 (not SpringBoard) | 0398: pool admission 0 any, 1 not SpringBoard, 2 backboardd only | 0398 / `SweetSurface.cpp`:192 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhw_pool_age` | uint, RW | 30 | 0398: leak-candidate age (s) | 0398 / `SweetSurface.cpp`:193 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhw_pool_denied` | uint, RD | - | 0398: eligible creations kept out by the policy | 0398 / `SweetSurface.cpp`:194 | read-only status/counter | source only |
| `kern.n2fbhw_pool_reclaim` | int, RW | 1 (report) | 0406: full pool: 0 off, 1 log what would be evicted, 2 evict idle volatile surfaces | 0406 / `SweetSurface.cpp`:207 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2fbhw_pool_reclaim_grace` | uint, RW | 2 | 0406: s before an evicted range goes back | 0406 / `SweetSurface.cpp`:208 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbhw_pool_evicted` | uint, RD | - | 0406: surfaces moved out of the pool | 0406 / `SweetSurface.cpp`:209 | read-only status/counter | source only |
| `kern.n2fbhw_pool_reclaim_given` | uint, RD | - | 0406: evicted ranges given back to the pool | 0406 / `SweetSurface.cpp`:210 | read-only status/counter | source only |
| `kern.n2fbhw_pool_remap_failed` | uint, RD | - | 0406: client mappings left on the old range | 0406 / `SweetSurface.cpp`:211 | read-only status/counter | source only |
| `kern.n2fbiommu` | int (handler), RW | 3 (all) | 0401: display IOMMU 0 off 1 map-only 2 single 3 all | 0401 / `sweet_fb_iommu_hw.h`:562 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2fbiommu_probe` | int (handler), RW | 0 (action) | 0401: 1 = log M4U0/larb/clock registers (read-only) | 0401 / `sweet_fb_iommu_hw.h`:563 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2fbiommu_stage` | int (handler), RW | 0 (action) | 0403: staged arming 1 arm 2 one port 3 both (watchdog) | 0403 / `sweet_fb_iommu_hw.h`:630 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbiommu_wdms` | uint, RW | 2000 | 0403: stage 2/3 watchdog ms | 0403 / `sweet_fb_iommu_hw.h`:631 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbiommu_sub` | int (handler), RW | 0 (action) | 0409: stage-1 sub-steps 1 prep 2 PT_BASE 4 TFRP 8 TLB 16 dump 128 undo | 0409 / `sweet_fb_iommu_hw.h`:782 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2fbiommu_delay` | uint (handler), RW | 3 s | 0470: boot arming delay s (default 3) | 0470 / `sweet_fb_iommu_hw.h`:860 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbyuv` | int (handler), RW | 0 | 0490 packed 4:2:2 overlay layers: bit 0 on, bits 7..4 matrix code (0 auto), bit 8 byte swap, bit 9 uyvy/yuyv exchanged | 0490 / `sweet_fb_yuv_kern.inc`:37 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2fbyuvstat` | string (handler), RD | - | 0490 YUV overlay layer counters | 0490 / `sweet_fb_yuv_kern.inc`:39 | read-only status/counter | source only |

**Userland (37 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/var/db/nord2-metal/no-final` | marker | absent | master switch: thumbfix5 behaviour (no texture holds, photosperf and gamepipe off) | components/gfx200-20261114/all17-src/redwoodmetal.m:984 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/final-camera` | marker | absent | mediaserverd / cameracaptured / Camera get holds and the pass anatomy back | components/gfx200-20261114/all17-src/redwoodmetal.m:1004 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gamepipe` | marker | absent | no pipeline reuse, no async builds | components/gfx200-20261114/all17-src/redwoodmetal.m:1037 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfxall4-asyncall` | marker | absent | async pipeline builds only in third-party apps | components/gfx200-20261114/all17-src/redwoodmetal.m:1076 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-photosperf / no-photosperf-log` | marker | absent | Photos perf changes off / PERF PHOTOS counters silenced | components/gfx200-20261114/all17-src/redwoodmetal.m:1867 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-px-dump` | marker | absent | Photos shader modules not dumped to the log | components/gfx200-20261114/all17-src/redwoodmetal.m:2730 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-photos-probe` | marker | absent | Photos-only probes and rules off | components/gfx200-20261114/all17-src/redwoodmetal.m:12303 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-compute-constants` | marker | absent | function-constant values not passed | components/gfx200-20261114/all17-src/redwoodmetal.m:2848 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-compute-cache` | marker | absent | identical compute kernels not reused | components/gfx200-20261114/all17-src/redwoodmetal.m:8718 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/surf-al / surf-all / trace-al / trace-cc` | marker | absent | diagnostic logs (App Library surfaces, every IOSurface texture, backdrop chain, 3 command buffers every 15 s) | components/gfx200-20261114/all17-src/redwoodmetal.m:3448/3474/12864/6102 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/mt-nodeep / matsnap-log / matsnap-allow / no-matsnap` | marker | absent | App Library snapshot experiments in SpringBoard (default: snapshot refused, live blur kept) | components/gfx200-20261114/all17-src/redwoodmetal.m:3685-3692 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/usecount-off / usecount-keepfreeze / usecount-yuv` | marker | absent | IOSurface use-count holds off / keep Photos freeze rule / holds on YCbCr outside Photos | components/gfx200-20261114/all17-src/redwoodmetal.m:3762 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gpu-ycbcr-odd / photos-ycbcr-cpu / no-odd-check` | marker | absent | YCbCr conversion on the CPU for odd sizes / for Photos / no GPU-vs-CPU check | components/gfx200-20261114/all17-src/redwoodmetal.m:4035 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/photos-ycbcr-live / no-young-reconvert / ycbcr-flush / no-gpu-touch` | marker | absent | Photos texture freeze rules; ycbcr-flush is opt-in (default off) | components/gfx200-20261114/all17-src/redwoodmetal.m:10790/10879/10907/12292 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfx100a (+ no-astcedge, no-slotdiag, no-rtcopy, no-filelog)` | marker | absent | gfx100a features off (all or one part) | components/gfx200-20261114/all17-src/redwoodmetal.m:4220 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-texlayout` | marker | absent | block-compressed no-copy texture selectors answer nil again | components/gfx200-20261114/all17-src/redwoodmetal.m:4366 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-thumbgrid (+ 10 part switches no-thumbgrid-*, thumbgrid-*-all)` | marker | absent | Photos thumbnail-grid features off, or widened beyond Photos | components/gfx200-20261114/all17-src/redwoodmetal.m:4710 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfx200 (+ no-gfx200-msaartt/-zsdc/-rtpool/-pools)` | marker | absent | gfx200 features off (all or one part) | components/gfx200-20261114/all17-src/redwoodmetal.m:5399 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfxall3-uploadbatch / no-gfxall3-bytesring / no-gfxall4-budget` | marker | absent | upload batching / 64 KiB bytes ring / shared pool budget off | components/gfx200-20261114/all17-src/redwoodmetal.m:16161/14596/5796 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-shader99 (+ no-shader99-xlat, no-blendcolor, no-bordercolor, no-tgmem, no-vertextex)` | marker | absent | shader99 translator additions off (all or one) | components/gfx200-20261114/all17-src/redwoodmetal.m:5903 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-rt1 / rt1-deadstore / no-singletarget` | marker | absent | framebuffer DontCare tricks off / opt-in dead-store / every attachment kept | components/gfx200-20261114/all17-src/redwoodmetal.m:5988-5991 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/pressure-on / no-pressure / pressure-dryrun` | marker | absent | memory-pressure release: opt-in (default OFF) / force off / log only | components/gfx200-20261114/all17-src/redwoodmetal.m:6514 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-camera-thumbnail / no-vt-selftest / vt-alignment-64` | marker | absent | camera thumbnail CPU fallback off / no VideoToolbox self-test / 64-byte alignment | components/gfx200-20261114/all17-src/redwoodmetal.m:6806/6823/8979 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-api99a (+ no-plib, no-subrect, no-fence, no-events, no-faces, no-famlog, no-emptyslot) / plib-all` | marker | absent | API-gap features off (all or one) / take QuartzCore, RenderBox, VectorKit pipeline libraries too | components/gfx200-20261114/all17-src/redwoodmetal.m:6857 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/crashlog / no-msltimeout` | marker | absent | crash logs for every process / no 8 s Metal-source compile limit (WebKit) | components/gfx200-20261114/all17-src/redwoodmetal.m:7234/7269 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-b3a8 / unknown-selectors-throw` | marker | absent | QuartzCore Apple3 site back to NO / unknown selectors raise again | components/gfx200-20261114/all17-src/redwoodmetal.m:9071/9967 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfxall17-rot` | marker | absent | rotated display-surface textures refused (as before gfxall17) | components/gfx200-20261114/all17-src/redwoodmetal.m:10009 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/gfxall17-rot-flip` | marker | absent (phone: present) | rotations 1 and 3 swapped (the deployed "gfxall17 + flip" set) | components/gfx200-20261114/all17-src/redwoodmetal.m:10011 | per process, read lazily at first use; restart the app to apply | yes (user, 2026-11-14..17) (Terraria landscape with gfxall17 + flip) |
| `/var/db/nord2-metal/no-webxr / no-xrsurf` | marker | absent | XR formats refused again | components/gfx200-20261114/all16-src/redwooddrawable.m:616/646 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfxall3-statecache` | marker | absent | library per-draw state cache off | components/gfx200-20261114/all17-src/air_metal.c:1164 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfxall3-shadercache` | marker | absent | library shader file cache off | components/gfx200-20261114/all10-src/pan_disk_cache.c:93 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gfxall4-push` | marker | absent | constant buffers not pushed into uniforms | components/gfx200-20261114/all10-src/pan_shader.c:119 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-metal/no-gpupipe / no-gpuv4 / no-bigpool` | marker | absent | old submit ABI (one heap) / no ABI v4 / big buffers not pooled | components/gpusched-20261114/userland/src4/redwood-drm/nord2_pan.c:709/842/1144 | per process, read lazily at first use; restart the app to apply | source only |
| `/var/db/nord2-gpu-gnr1-trace` | marker | absent | log every compute dispatch (library) | components/gfx200-20261114/all17-src/air_metal.c:5426 | diagnostic | source only |
| `/var/db/nord2-metal/{log,refused,kept,cache}/` | output dirs | log only if writable; refused/kept only if they exist; cache made by gfxall17 | per-process logs and crashes / rejected AIR modules / accepted compute modules / shader file cache | redwoodmetal.m:4271, air_metal.c:4003,4018, pan_disk_cache.c:55 | outputs, not switches; pull logs before an undo | source only |
| `env RWM_SYNC / RWM_SLOW_MS / RWM_SLOW_DRAW_US` | env | unset / 6 / 300 | commit waits for the GPU / slow-commit and slow-draw log thresholds | redwoodmetal.m:759,12944,13693 | diagnostic | source only |
| `env RWA_PACKED16 / RWA_LINK_LOG / RWA_UBO_EXTENT / RWA_PRINT_NIR / PAN_REDWOOD_FAULTS / HOME` | env | unset | library diagnostics; HOME = fallback shader cache in $HOME/Library/Caches/com.nord2.pancache | air_metal.c:1766,7117,7314,7375, pan_jm.c:375, pan_disk_cache.c:99 | diagnostic | source only |

### MDP

**Sysctls (24)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2mdp_probe` | int, RW | 0 | 0670: run the MDP probe once (1 read-only, 2 + open/close engine clock gates); -1 = never (also cancels the boot probe) | 0670 / `nord2_mdp.inc`:2069 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2mdp_boot` | int, RW | 1 | 0670: level of the automatic probe at +45 s (0 = none) | 0670 / `nord2_mdp.inc`:2071 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2mdp_state` | uint, RD | - | 0670: verdict bits of the last probe | 0670 / `nord2_mdp.inc`:2073 | read-only status/counter | source only |
| `kern.n2mdp_stages` | int, RW | 0x103 | 0671/0673: bits 7:0 highest functional stage to run (0..3), bit 8 (0x100) = 0673 MDP bring-up (bus protection + gates) | 0671 / `nord2_mdp.inc`:2074 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mdp_pwron` | uint, RD | - | 0673: bring-up result (0 not run, 1 OK, 2 domain off, 3 PWR_CON, 4 mdp_sel gated, 5 shared bit set, 6 ack timeout restored, 7 restore failed, 8 gates, 9 maps, 10 owned bit after release) | 0673 / `nord2_mdp.inc`:2075 | read-only status/counter | source only |
| `kern.n2mdp_stage` | uint, RD | - | 0671: highest stage passed in this boot | 0671 / `nord2_mdp.inc`:2077 | read-only status/counter | source only |
| `kern.n2mdp_caps` | uint, RD | - | 0671: capability bits that passed | 0671 / `nord2_mdp.inc`:2078 | read-only status/counter | source only |
| `kern.n2mdp_s1` | uint, RD | - | 0671: stage 1 result (1 pass, >1 failure code) | 0671 / `nord2_mdp.inc`:2079 | read-only status/counter | source only |
| `kern.n2mdp_s2` | uint, RD | - | 0671: stage 2 tests passed (bitmask) | 0671 / `nord2_mdp.inc`:2080 | read-only status/counter | source only |
| `kern.n2mdp_s3` | uint, RD | - | 0671: stage 3 result (1 pass) | 0671 / `nord2_mdp.inc`:2081 | read-only status/counter | source only |
| `kern.n2mdp_tune` | int, RW | 7 | 0690: 1 vendor M4U1 write setup at S3 (boot only), 2 second pipe for unscaled service requests, 4 S3 rotation tile width (default 7) | 0690 / `nord2_mdp.inc`:2082 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: dual pipe on Magnifier) |
| `kern.n2mdp_svc_tile` | int, RW | 0 | 0690: rotation tile width in px for unscaled service requests (64..512, multiple of 16); 0 = S3 TIMING choice | 0690 / `nord2_mdp.inc`:2084 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mdp_auto_tile` | uint, RD | - | 0690: S3 TIMING tile choice (16-px units, 0 = 512 px) | 0690 / `nord2_mdp.inc`:2086 | read-only status/counter | source only |
| `kern.n2mdp_m4u` | uint, RD | status | 0690: M4U1 vendor write setup: 0 not tried, 1 applied, 2 already set, 3 deferred (other masters powered), 4 reverted, 5 off | 0690 / `nord2_mdp.inc`:2087 | read-only status/counter | source only |
| `kern.n2mdp_overlap` | uint, RD | - | 0690: service jobs that ran while the other pipe was busy | 0690 / `nord2_mdp.inc`:2089 | read-only status/counter | source only |
| `kern.n2mdp_scale_off` | uint, RW | 0 | 0691: 1 = scaled requests refused (set by a scaled failure; write 0 to retry) | 0691 / `nord2_mdp.inc`:2090 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mdp_lane_dead` | uint, RD | - | 0691: pipes stuck after recovery (bit 0 pipe 0, bit 1 pipe 2) | 0691 / `nord2_mdp.inc`:2091 | read-only status/counter | source only |
| `kern.n2mdp_recoveries` | uint, RD | - | 0691: engine recoveries (aborts) | 0691 / `nord2_mdp.inc`:2092 | read-only status/counter | source only |
| `kern.n2mdp_mmsys_resets` | uint, RD | - | 0691: MDPSYS SW0_RST_B lane resets | 0691 / `nord2_mdp.inc`:2093 | read-only status/counter | source only |
| `kern.n2mdp_publish` | int, RW | 0 | 0683: 1 = offer AppleM2ScalerCSCDriver again (after kern.n2mdp_svc=1), handled by the MDP thread within 1 s | 0683 / `nord2_mdp.inc`:2139 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mdp_svc` | int, RW | 1 | 0672: register AppleM2ScalerCSCDriver when the MDP stages pass (0 = never) | 0672 / `sweet_m2scaler.inc`:58 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2m2_ok` | uint, RD | - | 0672: transforms done by the MDP | 0672 / `sweet_m2scaler.inc`:59 | read-only status/counter | source only |
| `kern.n2m2_refused` | uint, RD | - | 0672: transforms refused (caller falls back) | 0672 / `sweet_m2scaler.inc`:60 | read-only status/counter | source only |
| `kern.n2m2_failed` | uint, RD | - | 0672: transforms that failed in the MDP | 0672 / `sweet_m2scaler.inc`:61 | read-only status/counter | source only |

**Userland (1 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/var/db/nord2-mdp-off` | marker (kernel reads it) | absent | the kernel runs only the MDP probe (no scaler service) | nord2_mdp.inc (0671/0672), components/mdp-20261114/guest-mdpoff.sh | kill switch, read once at +45 s | source only |

### touch

**Boot-args (7 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `nord2input` | 0/1 | 0 (line: 1) | start the Nord 2 input thread (FT3518 touch, keys); needs nord2stopat=23 | 0082 / sweet_touch.c:1394 | required for touch | yes (user, 2026-11-14..17; in the line on the phone) |
| `n2tp` | 0/1 | 1 | touch pacing (fast reads + fixed period while a finger is down) | 0620 / sweet_touch.c:927 | kill switch; also n2gdv +2048; kern.n2tpace | source only |
| `sweettouch / sweettouchdelay / sweettouchirq` | 0/1, s, irq | 0 / 45 / - | old SWEET (Qualcomm) touch driver: enable, start delay, IRQ hint | 0060 / sweet_touch.c:1971,2060 | not used on the Nord 2 path | source only |
| `sweetkeys` | 0/1 | 1 | SWEET key driver | 0060 / sweet_keys.c:134 | kill switch | source only |
| `nord2slider` | 0/1 | 1 | alert slider (GPIO bus + HID ringer switch) | sweet_touch.c:871, SweetHID.cpp:814, nord2_slider.inc | kill switch | source only |
| `hidq_no_full_msg / hidq_force_usage_pair / hidq_size` | bool / page<<16/usage / entries | false / 0 / property | HID event queue options (stock HID family code bundled by 0058) | 0058 / IOHIDEventServiceQueue.cpp:56, IOHIDEventServiceUserClient.cpp:281,299 | debug only | source only |
| `sweethidtest` | seconds | 0 | synthetic HID button test timer | 0058 / SweetHID.cpp:762 | debug only | source only |

**Sysctls (3)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2tpace` | uint, RW | 1 | 0620 touch pacing: 0 old timing, 1 fast reads + fixed period | 0620 / `sweet_touch.c`:1353 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: touch in Terraria landscape) |
| `kern.n2tp_half_us` | uint, RW | 2 | 0620: half-bit delay of the touch reads, 1..3 us (4 = old) | 0620 / `sweet_touch.c`:1354 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2tp_period_us` | uint, RW | 8000 | 0620: loop period while a finger is down, 4000..10000 us | 0620 / `sweet_touch.c`:1355 | tuning: change only to test, values are lost at reboot | source only |

### haptics

**Boot-args (1 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `n2hap` | 0/1 | 1 | AW8697 haptics: 0 = no /dev/redwood-haptic node, chip untouched | 0610 / nord2_haptic.inc:683, SweetFramebuffer.cpp:1263 | kill switch; also n2gdv +1024; kern.n2hap | source only |

**Sysctls (5)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2hap` | int (handler), RW | 1 | 0610 haptics: 1 allow, 0 motor off and kept off | 0610 / `nord2_haptic.inc`:667 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | yes (user, 2026-11-14..17: haptic ticks "perfect" (2026-11-14)) |
| `kern.n2hap_drvmax` | int (handler), RW | 60 | 0610 haptics: AW8697 DRV_LVL at full scale, 18..60 (60 = Android) | 0610 / `nord2_haptic.inc`:669 | hardware limit: keep within the range in the description | source only |
| `kern.n2hap_max_ms` | int (handler), RW | 1000 | 0610 haptics: longest continuous run, ms (50..3000, default 1000; then a rest as long) | 0610 / `nord2_haptic.inc`:671 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2hap_test` | int (handler), RW | action | 0610 haptics: play effect N once (1..15, nord2_haptic.h N2H_FX_*) | 0610 / `nord2_haptic.inc`:673 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2hapstat` | string (handler), RD | - | 0610 haptics state and counters | 0610 / `nord2_haptic.inc`:675 | read-only status/counter | source only |

### audio

**Boot-args (2 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `nord2audio` | 0/1 | 0 (line: 1) | create the audio device node and stream engine | 0082 / audio_device.c:405 | required for sound | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2audioinstall` | exact =1 / =2 | absent (dropped by the packer) | 1 = install the audio package files into the root volume at boot, 2 = roll them back | audio_install.c:267 (custom parser) | one-shot installer; the packer removes it from the line | source only |

**Sysctls (38)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2call` | node, RW | - | Nord2 call audio | 0280 / `audio_call.c`:123 | node only | source only |
| `kern.n2call.enable` | uint (handler), RW | 1 | (no description string in the macro) | 0280 / `audio_call.c`:124 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.route` | uint (handler), RW | 0 (handset) | 0 handset (top), 1 speaker (bottom), 2 both | 0280 / `audio_call.c`:125 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.top` | uint (handler), RW | 1024 (Q15 ceiling) | (no description string in the macro) | 0280 / `audio_call.c`:127 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.bottom` | uint (handler), RW | 5793 (Q15) | (no description string in the macro) | 0280 / `audio_call.c`:128 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.pga` | uint (handler), RW | 3 | (no description string in the macro) | 0280 / `audio_call.c`:129 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.force` | uint (handler), RW | 0 | (no description string in the macro) | 0280 / `audio_call.c`:130 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.aif` | uint (handler), RW | 0 | 0 protocol 2 CLK_P2 + calibration, 1 protocol 2, 2 protocol 1 (next call) | 0280 / `audio_call.c`:131 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.ul_mute` | uint (handler), RW | 0 | (no description string in the macro) | 0280 / `audio_call.c`:133 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2call.phase` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:134 | read-only status/counter | source only |
| `kern.n2call.md_alive` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:135 | read-only status/counter | source only |
| `kern.n2call.on_acks` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:136 | read-only status/counter | source only |
| `kern.n2call.dl_frames` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:137 | read-only status/counter | source only |
| `kern.n2call.mic_frames` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:138 | read-only status/counter | source only |
| `kern.n2call.mic_peak` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:139 | read-only status/counter | source only |
| `kern.n2call.cal_ok` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:140 | read-only status/counter | source only |
| `kern.n2call.acks_sent` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:141 | read-only status/counter | source only |
| `kern.n2call.on_ack_p16` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:142 | read-only status/counter | source only |
| `kern.n2call.last_rc` | int, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:143 | read-only status/counter | source only |
| `kern.n2call.ios_volumes` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:214 | read-only status/counter | source only |
| `kern.n2call.ul_mutes` | uint, RD | - | (no description string in the macro) | 0280 / `audio_call.c`:261 | read-only status/counter | source only |
| `kern.n2mic` | node, RW | - | Nord2 microphone (/dev/redwood-mic) | 0321 / `audio_mic.inc`:127 | node only | source only |
| `kern.n2mic.enable` | uint, RW | 1 | 0: open refused | 0321 / `audio_mic.inc`:128 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mic.prime` | uint, RW | 2048 | samples before the first read (next open) | 0321 / `audio_mic.inc`:129 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mic.max` | uint, RW | 6144 | most samples kept (next open) | 0321 / `audio_mic.inc`:130 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mic.gain` | uint, RW | 256 | digital gain, Q8 (256 = 0 dB) | 0321 / `audio_mic.inc`:131 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mic.upgrade` | uint, RW | 0 | 1: speaker sound during a silent recording restarts the path with the amplifiers | 0321 / `audio_mic.inc`:132 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2mic.users` | uint, RD | - | opens since the node was last closed by all | 0321 / `audio_mic.inc`:133 | read-only status/counter | source only |
| `kern.n2mic.opens` | uint, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:134 | read-only status/counter | source only |
| `kern.n2mic.reads` | uint, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:135 | read-only status/counter | source only |
| `kern.n2mic.state` | uint, RD | - | 0 idle 1 up 2 active 3/4 down 5 failed | 0321 / `audio_mic.inc`:136 | read-only status/counter | source only |
| `kern.n2mic.sessions` | uint, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:137 | read-only status/counter | source only |
| `kern.n2mic.rc` | int, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:138 | read-only status/counter | source only |
| `kern.n2mic.in` | u64, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:139 | read-only status/counter | source only |
| `kern.n2mic.out` | u64, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:140 | read-only status/counter | source only |
| `kern.n2mic.dropped` | u64, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:141 | read-only status/counter | source only |
| `kern.n2mic.underruns` | u64, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:142 | read-only status/counter | source only |
| `kern.n2mic.peak` | uint, RD | - | (no description string in the macro) | 0321 / `audio_mic.inc`:143 | read-only status/counter | source only |

### camera+codecs

**Boot-args (1 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `n2torch` | 0/1 | 1 | MT6360 torch/flash driver (/dev/redwood-torch); 0 = off and kept off | 0496/0502 / sweet_touch.c:1363, SweetFramebuffer.cpp:1227 | kill switch; kern.n2torch | source only |

**Sysctls (12)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2vc` | node, RW | - | nord2 vcodec | 0312 / `nord2_vcodec.inc`:137 | node only | source only |
| `kern.n2vc.spin_us` | uint, RW | 2000 | 0314: spin-poll span of a wait (us) | 0314 / `nord2_vcodec.inc`:138 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2vc.sleep_us` | uint, RW | 200 | 0314: sleep step after it (50..1000 us) | 0314 / `nord2_vcodec.inc`:139 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2vc.venc_fast` | int (handler), RW | 0 | vcodecd VENC clock: 0 = 273 MHz (default), 1 = 624 MHz; applied at the next power-on while VEN is off | 0312 / `nord2_vcodec.inc`:140 | hardware limit: keep within the range in the description | source only |
| `kern.n2vctrace` | int (handler), RW | counter | vcodec node entries to log | 0299 / `nord2_vcodec_relay.inc`:185 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2torch` | int (handler), RW | 1 | 0496 torch: 1 allow, 0 off and kept off | 0496 / `sweet_touch.c`:1341 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | yes (user, 2026-11-14..17: torch works (2026-11-17)) |
| `kern.n2torch_maxsel` | int (handler), RW | 6 | 0496 torch current ceiling, ITOR 0..14 (25 mA + 12.5 mA x n per channel), default 6 | 0496 / `sweet_touch.c`:1343 | hardware limit: keep within the range in the description | source only |
| `kern.n2torch_flashsel` | int (handler), RW | 14 | 0496 flash pulse current ceiling, ITOR 0..14, default 14 | 0496 / `sweet_touch.c`:1345 | hardware limit: keep within the range in the description | source only |
| `kern.n2torch_max_s` | int (handler), RW | 300 | 0496 torch on-time limit, s (10..1800, default 300) | 0496 / `sweet_touch.c`:1347 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2torch_strobe` | int (handler), RW | 1 | 0560 camera flash: 1 strobe requests use the MT6360 STROBE mode, 0 they become torch pulses | 0560 / `sweet_touch.c`:1349 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: camera flash works (2026-11-17)) |
| `kern.n2torch_strobesel` | int (handler), RW | 76 (500 mA) | 0560 strobe current ceiling, ISTRB 0..116 (25 mA + 6.25 mA x n per channel), default 76 = 500 mA | 0560 / `sweet_touch.c`:1351 | hardware limit: keep within the range in the description | source only |
| `kern.n2torchstat` | string (handler), RD | - | 0496 torch state and counters | 0496 / `sweet_touch.c`:1356 | read-only status/counter | source only |

**Userland (54 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/var/db/nord2-camera-noflash` | marker | absent | flash photographs refused (photo taken without flash) | components/camprev-20261114/userland/src/rwcam_core.c:972 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-flash-always` | marker | absent | force the flash on every photo (test; no notifications) | components/camprev-20261114/userland/src/rwcam_device.c:1195 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-dualwide` | marker | absent | ask for the dual-wide device when the capture device has one | components/camprev-20261114/userland/src/rwcam_camui.c:137 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-n104-all` | marker | absent | iPhone 11 feature set (night mode, deferred processing, responsive shutter, over-capture, stereo audio) | components/camprev-20261114/userland/src/rwcam_camui.c:140 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-ctm` | marker | absent | iPhone 11 viewfinder (CAMFullscreenViewfinder, QuickTake) instead of the iPhone X one | components/camprev-20261114/userland/src/rwcam_camui.c:143 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-noultrawide` | marker | absent | leave CAMCaptureCapabilities as it is (no ultra-wide patch) | components/camprev-20261114/userland/src/rwcam_camui.c:171 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-nokick` | marker | absent | no stalled-session kick (the kernel's own 3 s decision only) | components/camprev-20261114/userland/src/rwcam_core.c:1994 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-coldstart` | marker | absent | no warm start while mediaserverd lives | components/camprev-20261114/userland/src/rwcam_core.c:2003 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-cpu-stats` | marker | absent | never use GPU statistics for AE; processor measure only | components/camprev-20261114/userland/src/rwcam_core.c:355 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-serial` | marker | absent | a frame's pictures made on the reader thread (old serial pipeline) | components/camprev-20261114/userland/src/rwcam_core.c:1224 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-30fps / 10fps` | marker | absent | long-exposure frame skipping: 30fps = never, 10fps = three times | components/camprev-20261114/userland/src/rwcam_core.c:2002 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-awb-grayworld` | marker | absent | grey-world white balance (runs up to q10hg) | components/camprev-20261114/userland/src/rwcam_core.c:2004 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-nofalloff` | marker | absent | no lens-falloff correction | components/camprev-20261114/userland/src/rwcam_core.c:2005 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-noalign` | marker | absent | the two back cameras' pictures are not compared/aligned | components/camprev-20261114/userland/src/rwcam_core.c:2006 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-noantiflicker / 60hz` | marker | absent | anti-flicker exposure: off / 60 Hz mains (default 50 Hz) | components/camprev-20261114/userland/src/rwcam_core.c:2008 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-uw-ratio` | marker | absent | text, 1001..1999: back-lens ratio in thousandths, set by hand | components/camprev-20261114/userland/src/rwcam_device.c:630 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-uw-offset` | marker | absent | text "dx dy": ultra-wide offset by hand | components/camprev-20261114/userland/src/rwcam_device.c:652 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-no60 / 60-custom2` | marker | absent | 60 fps sensor mode: never / custom mode 2 | components/camprev-20261114/userland/src/rwcam_device.c:908 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-focusprops` | marker | absent | list CMCapture focus properties per stream (diagnostic) | components/camprev-20261114/userland/src/rwcam_device.c:2864 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-mark` | marker | absent | draw moving bars over BLACK pictures (diagnostic) | components/camprev-20261114/userland/src/rwcam_device.c:2398 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-still-all / still-sync` | marker | absent | StillImage from every frame / made on the reader thread | components/camprev-20261114/userland/src/rwcam_device.c:2399 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-notwin` | marker | absent | each stream output makes its own picture (no shared twin) | components/camprev-20261114/userland/src/rwcam_device.c:2401 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-fullsize` | marker | absent | outputs at the size asked (no small-picture path) | components/camprev-20261114/userland/src/rwcam_device.c:2402 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-flip-{wide,ultra,front}-{h,v}` | marker | absent | 6 files: mirror that camera horizontally/vertically | components/camprev-20261114/userland/src/rwcam_device.c:2415 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-testpattern` | marker | absent | 16 moving bars on the PrimaryScaler picture (test) | components/camprev-20261114/userland/src/rwcam_device.c:2426 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-allcores` | marker | absent | one picture piece per core instead of work-stealing | components/camprev-20261114/userland/src/rwcam_device.c:1413 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-nometadata` | marker | absent | no per-frame metadata | components/camprev-20261114/userland/src/rwcam_device.c:3518 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-cpu-isp / gpu-isp` | marker | absent | ISP on the processor only / ask for the GPU ISP (version-1 library) | components/camprev-20261114/userland/src/rwcam_device.c:3519 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-zerocopy / nozerocopy` | marker | absent | zero-copy frames on (only with zerocopy) / forced off | components/camprev-20261114/userland/src/rwcam_device.c:3549 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-backonly` | marker | absent | one stream (back wide camera, stock D22 plists) - A/B test | components/camprev-20261114/userland/src/rwcam_device.c:3623 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-nodual` | marker | absent | never build the dual-wide group | components/camprev-20261114/userland/src/rwcam_device.c:3648 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-keephevc` | marker | absent | do not touch the sources' HEVC attributes | components/camprev-20261114/userland/src/rwcam_diag.c:130 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-fullvdo / vdo-quarter` | marker | absent | video data output at the size asked / a quarter as wide (default half) | components/camprev-20261114/userland/src/rwcam_diag.c:261 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-notap / nopropdiag / noflow / nognrdiag` | marker | absent | diagnostic wrappers off (tap, property log, flow log, GNR log) | components/camprev-20261114/userland/src/rwcam_diag.c:1587-1603 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-noschemefix / nostillfix / nilconnect-throw / emptysettings-throw` | marker | absent | the respective CMCapture fixes off | components/camprev-20261114/userland/src/rwcam_diag.c:585,758,1558,1564 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-nosinglenumber / nom2mstub` | marker | absent | single-number and M2M stub fixes off | components/camprev-20261114/userland/src/rwcam_diag.c:982,1254 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-keepstitcher / keepzsl / keepvis / keepbravoshift / keepquicktake` | marker | absent | leave that client flag as set (no override) | components/camprev-20261114/userland/src/rwcam_diag.c:1569-1584 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-noiris` | marker | absent | Live Photo (iris) movie capture forced off | components/camprev-20261114/userland/src/rwcam_diag.c:1572 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-prewarm` | marker | absent | leave the portrait prewarm alone | components/camprev-20261114/userland/src/rwcam_init.c:397 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-nodenoise` | marker | absent | no noise reduction in the render stage | components/camprev-20261114/userland/src/rwcam_render.c:258 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-camera-console` | marker | absent | camera log also to /dev/console | components/camprev-20261114/userland/src/rwcam_log.h:1 | camera test switch; read when mediaserverd starts the camera | source only |
| `/var/db/nord2-isp-features` | text: fold, r32, mrt, all | absent (all off) | optional GPU ISP passes; env RWA_ISP_FEATURES overrides | components/webxr-20261114/src/air_isp.c:164 | experimental | source only |
| `/var/db/nord2-isp-noexpand / -nrcpu / -nolinear` | marker | absent | ISP over every pixel / NR statistics on the CPU / readbacks via the driver (env RWA_ISP_NOEXPAND, RWA_ISP_NRCPU) | air_isp.c:2830-2832 | experimental | source only |
| `/var/db/nord2-vcodec-run` | gate marker | present (vcodec-prod9) | hardware codec daemon vcodecd runs only while it exists | hwcodec/daemon/vcodecd.c:1907; components/vcodec-20261114/rh/guest-chain9.sh:133 | kill switch: delete = no hardware codecs | source only |
| `/var/db/nord2-vcodec/venc-clock` | text: auto, auto:N, slow, fast, keep | absent = auto | VENC clock policy (sets kern.n2vc.venc_fast: 273 or 624 MHz); auto = fast above 1080p30 | hwcodec/daemon/n2vcd_vclock.h:36, vcodecd.c:359 | heat vs speed | source only |
| `/var/db/nord2-video-claim-hw` | marker | present (prod9) | encoders registered as hardware accelerated + hardware encoders registered | video/vt/n2vt_shim.c:134,210 | kill switch: delete = software encode | source only |
| `/var/db/nord2-video-hwdec` | marker | present (prod9) | hardware decoders registered | hwcodec/vt/dec/n2vtd_register.c:92 | kill switch | source only |
| `/var/db/nord2-video-off` | marker | absent | master switch: nothing registered (encoders and decoders) | video/vt/n2vt_shim.c:121, n2vtd_register.c:90 | kill switch | source only |
| `/var/db/nord2-video-no-avc / -no-hevc / -no-jpeg / -hwdec-no-hevc` | marker | absent | per-codec kill switches (encode; hwdec-no-hevc = AVC-only decode) | n2vt_shim.c:137,177,190, n2vtd_register.c:94 | kill switch | source only |
| `/var/db/nord2-video-quiet / -hwenc-sync / -trace` | marker | absent | no console lines / 1 frame in flight / logging proxy to nord2-video/trace/ | n2vt_log.c:99, n2hwenc_vt.c:72, n2vt_trace.c:47 | diagnostic | source only |
| `/var/db/nord2-video-no-deblock / -deblock-all` | marker | absent (deblock at <= 1280x720) | software AVC deblocking off / on at every size | video/vt/n2avc_vt.c:357 | quality vs speed | source only |
| `/var/db/nord2-video-probe (+ -always, -camera, -camera-hevc), nord2-vcodec-selftest/-probe/-dprobe/-jpeg` | gate markers | absent | one-shot codec probes / self-tests (results in nord2-video/ and nord2-vcodec/out/) | video/probe/probe_main.c:27, guest-chain9.sh:114-127 | test only | source only |
| `env N2HW_VENDOR_DIR / N2VCLK_FILE_PATH / N2VCLK_TEST_FILE / N2VCD_FMT_TRACE` | env | job sets N2HW_VENDOR_DIR=/usr/local/lib/n2vendor | vendor object dir / venc-clock file path / test hooks | hwcodec/daemon/vcodecd.c:321-2109 | do not change | source only |
| `env N2HW_TRACE, N2HW_DUMP_DIR, N2A32_MEM_LOG, N2A32_MEM_GRANULE, N2VIDEO_CONSOLE, N2_PROBE_DB, N2RWJ_DEV, N2T_SWITCH, N2T_DIR` | env | unset | emulator / probe / JPEG / trace diagnostics (not set by any job) | hwcodec/emu, hwcodec/shim/n2vfs.c, video/probe, hwcodec/jpeg, hwcodec/heicm | diagnostic | source only |

### Wi-Fi

**Boot-args (18 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `nord2wifi` | 0/1/2 | 0 (line: 2) | Wi-Fi bring-up: 1 AP-side register snapshot, 2 also VCN18/VCN13 RC-mode power preparation | 0082 / sweet_touch.c:454,695 | required for Wi-Fi | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wififw` | 0/1 | 0 (line: 1) | allow the vendor firmware read from UFS (Wi-Fi firmware files) | 0082 / sweet_ufs.c:1628, wifi_firmware_probe.c:52 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifipersist` | 0/1 | 0 (line: 1) | keep the Wi-Fi power domain on (persistent power) | 0082 / sweet_touch.c:735, wifi_persistent_bind.inc:60 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifirom` | 0/1 | 0 (line: 1) | ROM patch download stage | 0082 / pool_bind.inc:194, wifi_rom_*.c | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifidma` | 0/1 | 0 (line: 1) | WFDMA (DMA rings) stage | 0082 / pool_bind.inc:190, wifi_main_* | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifimain` | 0/1 | 0 (line: 1) | main firmware stage (needs cap/persist) | 0082 / wifi_capability_bind.inc:141 and others | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wificap` | 0/1 | 0 (line: 1) | capability worker | 0082 / wifi_capability_bind.inc:139 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifibasic` | 0/1 | 0 (line: 1) | basic configuration stage | 0082 / wifi_basic_config_bind.inc:47 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifiscan` | 0/1 | 0 (line: 1) | normal scan backend | 0082 / wifi_normal_backend_bind.inc:75 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifivcn33` | 0/1 | 0 (line: 1) | VCN33 rail retain (needs nord2wifimain=1 and nord2wifipersist=1, else panic) | 0082 / wifi_vcn33_bind.inc:41 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifiios` | 0/1 | 0 (line: 1) | iOS-facing Wi-Fi interface (Apple80211 registry, scan service) | 0082 / wifi_ios_registry.cpp:242, wifi_scan_service_bind.inc:52 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wifilive` | 0/1 | 0 (line: 1) | live control worker (data path) | 0082 / wifi_live_control_bind.inc:50 | required | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2wificommon / nord2wifiadieid / nord2wifiwake / nord2wifiwfsys` | 0/1 | 0 | older diagnostic bring-up stages (common power window, A-die ID, AP/WF wake, WFSYS) | 0082 / wifi_common_adapter.inc:133, wifi_adie_adapter.inc:115, wifi_wake_bind.inc:52, wifi_wfsys_bind.inc:77 | diagnostic; not in the line | source only |
| `nord2wifiemi / nord2wifiemilegacy` | 0/1 | absent (packer drops nord2wifiemi=0) | EMI layout probe: legacy vs new layout selection | 0082 / wifi_emi_probe.c:26 | do not add | source only |
| `n2wpm` | 0..3 | 2 (kern.n2wifi_privmac) | private Wi-Fi address mode (2 = deactivate + activate; 3 = accept and ignore) | 0430/0431/0434 / wifi_ios_join.c:33 | see sysctl | source only |
| `n2sae` | 0..2 | 1 (kern.n2wifi_sae) | WPA3-SAE mode | 0434 / wifi_ios_join.c:34 | see sysctl | source only |
| `n2pmf` | 0..3 | 0 (kern.n2wifi_pmf) | PMF experiments | 0434 / wifi_ios_join.c:34 | experimental | source only |
| `n2wsr` | 0/1 | 1 (kern.n2wifi_scanrelax) | serve directed scans with the passive scan | 0430 / wifi_ios_policy.c:32 | safe | source only |

**Sysctls (7)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2wifi_8021x` | uint, RW | 0x11 | 0450/0452 WPA2-Enterprise bits: 1 802.1X joins (clear: profile refused), 2 PTK rekey + EAP re-authentication on the link, 0x10 handshake-complete message for eapolclient | 0450 / `wifi_8021x_glue.c`:96 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2wifi_sae` | uint, RW | 1 (boot-arg n2sae) | 0434: WPA3-Personal (SAE), PSK-SHA256, PMF-required joins: 0 off, 1 on (SAE when requested), 2 SAE whenever possible; capability bits follow the value at wifid start (boot-arg n2sae) | 0434 / `wifi_ios_join.c`:35 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2wifi_pmf` | uint, RW | 0 (boot-arg n2pmf) | 0434: PMF experiments (not hardware verified): 1 ucMgmtProtection in the pairwise key command, 2 ignore unprotected deauth/disassoc after key install | 0434 / `wifi_ios_join.c`:36 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2wifi_rssithr` | int, RW | -80 | 0790: RSSI_THRESHOLD (dBm) answered to APPLE80211_IOC_RSSI_BOUNDS = wifid auto-join minimum RSSI; read by wifid at start; 0 = unsupported as before | 0790 / `wifi_rssi_bounds.c`:62 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2wifi_privmac` | uint, RW | 2 (boot-arg n2wpm) | 0436/0491: private Wi-Fi address: 0 legacy refuse, 1 use it with a second activate only (ignored by the firmware), 2 use it with deactivate + activate (default), 3 accept and ignore (kill switch) | 0431 / `wifi_station_service_bind.inc`:14 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2wifi_scanrelax` | uint, RW | 1 (boot-arg n2wsr) | 0430: serve directed scan requests with the passive scan (0 = refuse them) | 0431 / `wifi_station_service_bind.inc`:15 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2wifi_privmac_state` | uint, RW | 0 | 0491: private Wi-Fi address fallback: 0 in use, 1 suspected after a failed authentication (board address next), 2 confirmed not working (board address); write 0 to try again | 0491 / `wifi_station_service_bind.inc`:121 | tuning: change only to test, values are lost at reboot | source only |

### Bluetooth

**Sysctls (33)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2bt` | uint, RW | 1 | 0720 Bluetooth: 1 bring-up allowed, 0 kill switch | 0720 / `nord2_bt.c`:2126 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | yes (user, 2026-11-14..17: BT pairing + AAC) |
| `kern.n2bt_publish` | uint, RW | 1 | 0720: 1 expose the controller to iOS, 0 probe only | 0720 / `nord2_bt.c`:2127 | diagnostic: safe to toggle, may add log volume | yes (user, 2026-11-14..17: BT pairing + AAC) |
| `kern.n2bt_quirks` | uint, RW | 0x9ff | 0721 answer rewrites (N2BT_Q_*) | 0720 / `nord2_bt.c`:2128 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_trace` | uint, RW | 160 | 0721 HCI packets still to log | 0720 / `nord2_bt.c`:2129 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2bt_adie` | uint, RW | 1 | 0720 A-die top clock keeper | 0720 / `nord2_bt.c`:2130 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_recover` | uint, RW | 1 | 0720 bt8: automatic controller restart when it stops answering | 0720 / `nord2_bt.c`:2131 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_skywalk_grant` | uint, RW | 1 | 0720 bt14: grant PRIV_SKYWALK_REGISTER_USER_PIPE to entitled bluetoothd | 0720 / `nord2_bt.c`:2132 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_a2dp_filter` | uint, RW | 0 | 0720 bt18: DIAGNOSTIC AVDTP codec filter: 0 off (default), 1 hide AAC, 2 only SBC | 0720 / `nord2_bt.c`:2133 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2bt_early` | uint, RW | 1 | 0720 bt22: start the bring-up from the kernel at boot (en0 attached), not n2btd | 0720 / `nord2_bt.c`:2134 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: early BT start ~24 s) |
| `kern.n2bt_poll_idle_us` | uint, RW | 100 | 0720 bt21: poll period (us) with a link up but quiet | 0720 / `nord2_bt.c`:2135 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_mode` | int, RW | 2 | 0720 bt21: transport mode (2 default, -1 auto-bisect at each restart, 0..5 fixed) | 0720 / `nord2_bt.c`:2136 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: transport mode 2 stable) |
| `kern.n2bt_tx_mode` | uint, RD | status | 0720 bt20: transport mode in effect | 0720 / `nord2_bt.c`:2137 | read-only status/counter | source only |
| `kern.n2bt_tx_timeout_ms` | uint, RW | 200 | 0720 bt19: per-packet BTIF TX deadline (ms) | 0720 / `nord2_bt.c`:2138 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_tx_bps` | uint, RW | 1000000 | 0720 bt17: ACL bit/s cap to the controller (0 = none) | 0720 / `nord2_bt.c`:2139 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_poll_us` | uint, RW | 100 | 0720 bt11: RX poll period (us) with a link up / recent traffic; 0 = old 1 ms | 0720 / `nord2_bt.c`:2140 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_wak` | uint, RW | 0 | 0720 bt10: BTIF_WAK before TX: 0 never (Linux), 1 old 2 us pulse, 2 150 us pulse | 0720 / `nord2_bt.c`:2141 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_swirq` | uint, RW | 1 | 0720 bt10: poll/acknowledge the firmware's software interrupts | 0720 / `nord2_bt.c`:2142 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_keepvote` | uint, RW | 1 | 0720 bt9: keep the conn_infra BT wake vote while BT runs | 0720 / `nord2_bt.c`:2143 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_ccif` | uint, RW | 1 | 0720 bt9: tell the modem BT is on (MD coex CCIF) before BGF power-on | 0720 / `nord2_bt.c`:2144 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_diag_bgf` | uint, RW | 1 | 0720 bt9: on a hang also read BGF sw irq status and the firmware assert text | 0720 / `nord2_bt.c`:2145 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_wait_s` | uint, RW | 120 | 0720 conninfra wait (s) | 0720 / `nord2_bt.c`:2146 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_force` | uint, RW | 0 | 0720 skip the AP-side predicates (diagnosis) | 0720 / `nord2_bt.c`:2147 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2bt_maxstep` | uint, RW | 10 | 0720 bt2: stop after step N (1 = AP-side reads only, 9 = controller up not published, 10 = full) | 0720 / `nord2_bt.c`:2148 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_rearm` | uint, RW | 0 | 0720 bt2: 1 = ignore the previous-boot-reset guard once | 0720 / `nord2_bt.c`:2149 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_stop_mode` | uint, RW | 0 | 0720 bt3: stop after S6+: 0 keep BT powered (fw own handed back), 1 graceful off | 0720 / `nord2_bt.c`:2150 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_monitor_s` | uint, RW | 600 | 0720 bt3: post-stop monitor seconds (END written after it) | 0720 / `nord2_bt.c`:2151 | hardware limit: keep within the range in the description | source only |
| `kern.n2bt_tx_gap_us` | uint, RW | 0 | 0720 bt6: us pause per 16-byte chunk of long packets | 0720 / `nord2_bt.c`:2152 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_tries` | uint, RW | 3 | 0720 bring-up attempts allowed per boot | 0720 / `nord2_bt.c`:2153 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_up` | int (handler), RW | action | 0720 write 1: bring the controller up | 0720 / `nord2_bt.c`:2154 | one-shot action | source only |
| `kern.n2bt_addr` | string (handler), RW | board address | 0720 BD address aa:bb:cc:dd:ee:ff | 0720 / `nord2_bt.c`:2155 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bt_state` | string (handler), RD | - | 0720 state | 0720 / `nord2_bt.c`:2156 | read-only status/counter | source only |
| `kern.n2bt_stats` | string (handler), RD | - | 0720 counters | 0720 / `nord2_bt.c`:2157 | read-only status/counter | source only |
| `kern.n2bt_note` | string (handler), RW | write-only | 0720 daemon log line | 0720 / `nord2_bt.c`:2158 | diagnostic: safe to toggle, may add log volume | source only |

**Userland (5 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/var/db/nord2-bt/enable` | gate marker | present (bt1) | Bluetooth bring-up allowed (n2btd and the kernel early thread) | components/bt-20261115/n2btd/n2btd.c:37; nord2_bt.c | kill switch: delete + reboot | yes (user, 2026-11-14..17) |
| `/var/db/nord2-bt/bdaddr` | text xx:xx:xx:xx:xx:xx | generated once, kept | Bluetooth address written to kern.n2bt_addr | n2btd.c:97-104; nord2_bt.c | per-phone value (<placeholder>) | source only |
| `/var/db/nord2-bt/stage` | text: probe or 1..10 | absent = 10 (full) | limits the bring-up (kern.n2bt_maxstep) | n2btd.c:240-246; nord2_bt.c | diagnostic | source only |
| `/var/db/nord2-bt/guard, enable.off-reset, rearm, mg-rebuilds` | state files | automatic | reset guard (renames enable -> enable.off-reset after a mid-bring-up reset); rearm once (kern.n2bt_rearm); MobileGestalt rebuild counter (max 2) | n2btd.c:123-393 | automatic kill switch; rename back to re-arm | source only |
| `bluetoothd job env OS_ACTIVITY_DT_MODE=YES + log to nord2-bt/bluetoothd.log` | env | set by bt1 | bluetoothd log forwarded (350 lines/boot) to the kernel log | components/bt-20261115/pl/guest-bt1.sh:47 | diagnostic | source only |

### cellular+SIM

**Boot-args (2 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `n2bb` | flag word | 1 (NBB_ON) | baseband bridge flags: bit0 on, bit1 core only, ... bit29 = no BlastDoor sandbox exception; NBB_NO_DATA = CommCenter does not own data | Nord2Baseband.cpp:5406, sweet_amfi.c:495 | 0 = no modem | source only |
| `nord2_net` | bits | 1 | cellular data path: bit0 on, bit1 skip MD remap, bit2 no self-test, bit3 no unscoped default route, bit4 DPMAIF L2 IRQs unmasked | sweet_amfi.c:5059 | kill switch (0 = no data path) | source only |

**Sysctls (11)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `net.nord2` | node, RW | - | Nord2 cellular data | 0082 / `sweet_amfi.c`:6457 | node only | source only |
| `kern.n2sim_fix` | uint, RW | 15 | 0710: SIM/PIN fixes bit mask, 0 = 0277 | 0710 / `sweet_amfi.c`:6458 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: SIM works) |
| `kern.n2sim_state` | uint, RD | - | 0710: raw / settled<<8 / shown<<16 / identity<<24 | 0710 / `sweet_amfi.c`:6459 | read-only status/counter | source only |
| `kern.n2sim_apn` | uint, RW | 1 | 0711: 1 = the SIM's APN, 0 = internet.it for every SIM | 0710 / `sweet_amfi.c`:6460 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: SIM works) |
| `kern.n2nr` | uint, RW | 1 | 0712: 1 = NR (5G NSA) reporting from the modem | 0710 / `sweet_amfi.c`:6461 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2nr_state` | uint, RD | - | 0712: ENRCABAND state / band<<8 / ECAPABILITY<<24 | 0710 / `sweet_amfi.c`:6462 | read-only status/counter | source only |
| `kern.n2bb_link` | uint, RD | - | 0713: published / clients<<8 / sahara<<16 / power-ons<<24 | 0713 / `sweet_amfi.c`:6464 | read-only status/counter | source only |
| `kern.n2sim_helper` | uint (handler), RW | action | 0713: userland helper step codes, printed | 0713 / `sweet_amfi.c`:6477 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2nv_src` | uint, RD | status | 0715: modem NVRAM blob source: 1 raw userdata+64GiB, 2 rootfs file, +0x100 file invalid | 0715 / `sweet_amfi.c`:6479 | read-only status/counter | yes (user, 2026-11-14..17: modem firmware restored) |
| `kern.n2nr_ui` | uint, RW | 0 | 0714: 1 = NR5G next to LTE in the NAS serving system while the NR leg is up (experiment) | 0714 / `sweet_amfi.c`:6480 | tuning: change only to test, values are lost at reboot | source only |
| `net.nord2.pdp` | string, RD | "down" | PDN of cid 1 | `sweet_amfi.c`:6481 | read-only status/counter | source only |

**Userland (4 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/var/db/nord2-simcache/disabled` | marker | absent | n2simcache does nothing | components/sim-20261115/simcache/n2simcache.c:119 | kill switch (VARIANT=simcache1-off) | yes (user, 2026-11-14..17) |
| `/var/db/nord2-simcache/norestart / keepkey / dryrun` | marker | absent (norestart auto-written if the link is not back in 120 s) | do not restart CommCenter / keep the carrier key / log only | n2simcache.c:136-172 | safe | source only |
| `CFPreferences com.apple.commcenter key com.apple.carrier_1 (user _wireless)` | pref key | read then cleared | stale last-SIM carrier key removed (plus Carrier1Bundle/Operator1Bundle links) | n2simcache.c:99-108 | automatic | source only |
| `/var/db/nord2-modem/n2nvdisk.bin` | binary modem NVRAM blob (0600) | installed by nvfile1 | read first by the kernel (0715); kern.n2nv_src shows the source | sweet_amfi.c (0715); components/sim-20261115/nvfile/guest-nvfile1.sh | this phone's own blob only | yes (user, 2026-11-14..17) |

### GNSS

**Boot-args (1 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `nord2gnss` | 0/1 | 1 (on) | 0 = GNSS device nodes not made; else nodes made (hardware reached only by the gnssfix payload daemon) | 0350/0580 / nord2_gpsdl.inc:2029 | kill switch; also kern.n2gnss bit0 | source only |

**Sysctls (1)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2gnss` | int, RW | 0 | Nord2 GNSS: bit0 off, bit1 pipe gate observe-only | 0580 / `nord2_gpsdl.inc`:67 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |

**Userland (5 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/var/db/nord2-gnssd` | gate marker ("run") | present (gnssfix22) | n2mnld GNSS engine runs (job com.nord2.gnssd) | hwcodec/gnss/n2mnld/native/n2mnld.c:634; components/gnss-20261114/pl22/guest-gnssfix22.sh:53 | kill switch | source only |
| `/var/db/nord2-locbridge` | gate marker ("run") | present | NMEA-to-locationd bridge runs (job com.nord2.locbridge) | hwcodec/locbridge/n2locbridge.c:175 | kill switch | source only |
| `/var/db/n2gnss/MNL_Config.xml (+ MNL_Config.vendor.xml, engine state files)` | XML + libmnl state (13 files) | installed by gnssfix22 | engine configuration and state; vendor copy unused | guest-gnssfix22.sh:72-96 | replace only with a known config | source only |
| `n2mnld / n2locbridge arguments` | plan.json gnssd_args | gnssfix22 set (gnssfix22n = without --pmtk) | engine start delay, polling, PMTK commands; bridge FIFO/log | components/gnss-20261114/pl22/payload-gnssfix22/plan.json; n2mnld.c:611 | payload choice | source only |
| `/var/db/nord2-gnssstep(.off-gnssd1)` | gate marker | moved aside (off) | old com.nord2.gnssstep probe | guest-gnssfix22.sh:54,80 | keep off | source only |

### sensors

**Boot-args (3 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `nord2scp` | 0..2 | 2 (all) | SCP sensor hub: 0 off, 1 read only, 2 all | nord2_scp.inc:921 | kill switch | source only |
| `n2scpm` | 0..2 | 2 (on demand) | motion sensors: 0 off, 1 always on (old), 2 on demand (kern.n2scp_motion) | 0531 / nord2_scp.inc:922 | also n2gdv +128 = 1 | source only |
| `nord2sensors` | 0/1 | 1 | publish the SweetHID sensor services (0 = none); n2gdv bit 18 drops only the new 0760 services | SweetHID.cpp:773 | kill switch | source only |

**Sysctls (11)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2scp_motion` | int, RW | 2 (boot-arg n2scpm) | NORD2 SCP: accelerometer/gyroscope/magnetometer: 0 off, 1 always on (50/50/25 Hz), 2 on demand (iOS ReportInterval) | 0531 / `nord2_scp.inc`:572 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | yes (user, 2026-11-14..17: acc/gyro/compass data) |
| `kern.n2scp_axflip` | int, RW | 0 | NORD2 SCP 0760: negate axes handed to iOS: bits 0-2 accel x/y/z, 3-5 gyro, 6-8 compass | 0760 / `nord2_scp.inc`:574 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2scp_lightrate` | int, RW | 5120 (5 Hz) | NORD2 SCP 0760: light sensor rate (Hz*1024, 0 = on-change as 0284); re-sent to the hub on change | 0760 / `nord2_scp.inc`:577 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2scp_alslcd` | int, RW | 0 | NORD2 SCP 0760: write a brightness (1..2047) to send OPLUS SET_LCD_INFO for the light sensor once | 0760 / `nord2_scp.inc`:580 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2scp_orient` | int, RW | 1 | NORD2 SCP 0761: 1 = device orientation events from the accelerometer (0x20/0x8A service), 0 = none | 0761 / `nord2_scp.inc`:583 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: auto-rotate) |
| `kern.n2scp_orient_force` | int, RW | -1 | NORD2 SCP 0761: -1 = detector; 0..6 = send this UIDeviceOrientation (1 portrait 2 upside down 3 landscape left 4 right 5 up 6 down) | 0761 / `nord2_scp.inc`:585 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2scp_lighttype` | int, RW | 5 | NORD2 SCP 0761: sensor type of the light enable (5 Android light; re-sent on change, the old type is disabled) | 0761 / `nord2_scp.inc`:588 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2scp_alslcd_auto` | int, RW | 1023 | NORD2 SCP 0761: brightness for OPLUS SET_LCD_INFO sent after the light enable and every 20 s (6 times) while no light record came; 0 = never | 0761 / `nord2_scp.inc`:591 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2scp_raw` | string (handler), RD | - | NORD2 SCP 0760: last raw and iOS-unit samples, rates, counters, HID service state | 0760 / `nord2_scp.inc`:617 | read-only status/counter | source only |
| `kern.n2scp_lightpoll` | int, RW | 1 | NORD2 SCP 0762: 1 = if no light record arrives, poll CUST GET_RAW_DATA for light once a second and hand the value to iOS as lux; 0 = never | 0762 / `nord2_scp.inc`:660 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2scp_summary_s` | int, RW | 30 | NORD2 SCP 0762: seconds between the 'sensors summary' log lines (0 = off) | 0762 / `nord2_scp.inc`:663 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |

### USB+charging

**Boot-args (2 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `sweetusb` | 0..2 | 1 | USB: 0 off, 1 report state only, 2 also start the USB device | 0072 / sweet_usb.c:933 | safe | source only |
| `nord2battery (and sweetbattery)` | 0/1 | 0 (line: 1) | real (read-only) battery provider instead of a fake one | 0062/0082 / SweetBattery.cpp:491,493 | keep 1 | yes (user, 2026-11-14..17; in the line on the phone) |

**Sysctls (21)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2usb_strict` | uint, RW | 0 | 0700: 1 = mode 23 leaves XNU when USB is unavailable (old behaviour) | 0700 / `sweet_usb.c`:108 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2usb_late` | uint, RW | 1 | 0700: 0 off, 1 passive USB re-check every 2 s, 2 + power the USB block up when VBUS is seen (experimental) | 0700 / `sweet_usb.c`:110 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2usb_state` | uint, RD | status | 0700: USB console state | 0700 / `sweet_usb.c`:112 | read-only status/counter | source only |
| `kern.n2usb_reason` | uint, RD | status | 0700: why USB was unavailable | 0700 / `sweet_usb.c`:113 | read-only status/counter | source only |
| `kern.n2bat_poll_ms` | uint, RW | 10000 | 0701: battery poll period ms (2000..60000) | 0701 / `nord2_battery.c`:13 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bat_hold_s` | uint, RW | 120 | 0701: hold the last valid gauge sample s | 0701 / `nord2_battery.c`:14 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bat_filter` | uint, RW | 1 | 0701: 1 = hide one-step SOC flicker against the current | 0701 / `nord2_battery.c`:15 | hardware limit: keep within the range in the description | source only |
| `kern.n2bat_log_s` | uint, RW | 60 | 0701: NORD2 BAT14 log period s | 0701 / `nord2_battery.c`:16 | diagnostic: safe to toggle, may add log volume | source only |
| `kern.n2bat_legacy` | uint, RW | 0 | 0701: 1 = pre-0701 IOPMPowerSource publishing | 0701 / `nord2_battery.c`:17 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2bat_fast_ms` | uint, RW | 500 | 0703: VBUS fast check period ms, 0 = off | 0703 / `nord2_battery.c`:18 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2chg_pg_active_low` | uint, RW | 0 | 0701: 1 = old MP2650 power-good polarity | 0701 / `nord2_battery.c`:19 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2chg_ctl` | uint, RW | 1 | 0702: 1 = MP2650 charge control, 0 = observe only | 0702 / `nord2_battery.c`:134 | tuning: change only to test, values are lost at reboot | yes (user, 2026-11-14..17: charging works) |
| `kern.n2chg_sdp_ma` | uint, RW | 500 | 0702: input limit mA with a USB host (<= 1500) | 0702 / `nord2_battery.c`:135 | hardware limit: keep within the range in the description | source only |
| `kern.n2chg_dcp_ma` | uint, RW | 2000 | 0702: input limit mA for a non-host source (<= 2000) | 0702 / `nord2_battery.c`:136 | hardware limit: keep within the range in the description | source only |
| `kern.n2chg_unk_ma` | uint, RW | 1500 | 0702: input limit mA when the source type is unknown (<= 2000) | 0702 / `nord2_battery.c`:137 | hardware limit: keep within the range in the description | source only |
| `kern.n2chg_ichg_ma` | uint, RW | 1100 | 0702: battery charge current mA at 10..45 C (<= 2000) | 0702 / `nord2_battery.c`:138 | hardware limit: keep within the range in the description | source only |
| `kern.n2chg_aicl_mv` | uint, RW | 4500 | 0702: software AICL VIN floor mV (4300..4800) | 0702 / `nord2_battery.c`:139 | hardware limit: keep within the range in the description | source only |
| `kern.n2chg_type` | uint, RD | - | 0702: 0 none, 1 host, 2 deciding, 3 non-host, 4 unknown | 0702 / `nord2_battery.c`:140 | read-only status/counter | source only |
| `kern.n2chg_target_ma` | uint, RD | - | 0702: input limit target mA | 0702 / `nord2_battery.c`:141 | read-only status/counter | source only |
| `kern.n2chg_writes` | uint, RD | - | 0702: MP2650 register writes | 0702 / `nord2_battery.c`:142 | read-only status/counter | source only |
| `kern.n2chg_errors` | uint, RD | - | 0702: failed/unverified writes | 0702 / `nord2_battery.c`:143 | read-only status/counter | source only |

### thermal+DVFS

**Boot-args (16 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `n2gdv` | bit word | 0 (line: 73738) | shared kill-switch / mode word for many pieces: see the n2gdv table | 0461/0492/0493 + many / Nord2GPU.cpp:931 and others | see table | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2_lvts (alias n2lvts)` | 0..4 | 4 (auto) | LVTS thermal sensing: 4 = auto on CPU big/big2/little + GPU controllers; 0 = off | 0094/0643 / Nord2GPU.cpp:926 | thermal safety: keep 4 | source only |
| `nord2_lvts_mask` | bit mask | 0x17 in mode 4 | which LVTS controllers | 0094 / Nord2GPU.cpp:928 | keep | source only |
| `nord2_lvts_max_c` | 30..100 C | built-in | cold-start temperature guard | 0094 / Nord2GPU.cpp:929 | thermal safety | source only |
| `nord2_lvts_golden` | calibration value | from efuse | override the LVTS golden temperature calibration | 0094 / Nord2GPU.cpp:1296 | wrong readings if misused | source only |
| `n2dvfs` | 0, 1, MHz >= 500 | 0 (line: 3000) | CPU DVFS bring-up: 1 = hand-over at bootloader rows; MHz = then ramp the A78 clusters up to that cap with the battery guard, plus 0099 placement | 0116 / nord2_cpufreq.c:39 | keep; 0 = CPUs stay at bootloader clocks | yes (user, 2026-11-14..17; in the line on the phone) |
| `n2dvst` | 0..3 | 3 | DVFS bring-up depth: 0 preflight, 1 + share buffer, 2 + platform init, 3 everything | 0118 / nord2_cpufreq.c | bring-up only | source only |
| `n2dvm` | 0..3 | 2 | kern.n2dvfs_mode: 0 old governor, 1 + new policy logged, 2 new policy acts, 3 pinned at the cap | 0460 / nord2_cpufreq.c:47 | 3 = heat | source only |
| `n2gov` | 0/1 | 1 | 0172 governor: 1 load-based, 0 fixed cap with the old 2 s guard | 0172 / nord2_cpufreq.c:43 | tuning | source only |
| `n2cpu / n2mcu / n2mcuping` | int | 0 | older CPU frequency / MCUPM experiments (ignored when n2dvfs is set) | 0104/0109/0115 / nord2_cpufreq.c | obsolete paths | source only |
| `n2dvplt` | 0/1 | 0 | send the P20 platform init again during DVFS bring-up | 0119 / nord2_cpufreq.c:1057 | bring-up only | source only |
| `n2sch` | N2S_* bits | 0 | core-aware scheduler mode (kern.nord2_sched_mode); when given, n2dvfs does not enable the 0099 placement itself | 0099 / osfmk/kern/nord2_sched.c (CI base), nord2_cpufreq.c:1053 | tuning | source only |
| `n2schpri` | priority | 47 (line: 37) | lowest thread priority eligible for big-core placement (kern.nord2_sched_pri) | 0099 / osfmk/kern/nord2_sched.c (CI base) | tuning | yes (user, 2026-11-14..17; in the line on the phone) |
| `n2big / n2schst / n2schab` | int | 0 / 1 / 0 | scheduler fast path / per-switch statistics / in-boot A/B alternation every N s | 0099 / nord2_sched.c (CI base) | diagnostic | source only |
| `n2clk / n2csram` | int | 300 / 0 | CPUCLK log period (s) / CSRAM dump | 0099 / nord2_sched.c (CI base) | diagnostic | source only |
| `cluster_power / kernPOST / enable_skstb / enable_skstsct` | stock | stock | stock AppleARMSMP cluster power switches | stock / AppleARMSMP.cpp:181-193 | do not use | source only |

**Sysctls (14)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2dvfs_mode` | int, RW | 2 (boot-arg n2dvm) | (no description string in the macro) | 0460 / `nord2_cpufreq.c`:48 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2dramprobe` | int, RW | 0 | (no description string in the macro) | 0462 / `nord2_cpufreq.c`:939 | tuning: change only to test, values are lost at reboot | source only |
| `kern.n2cpuplat` | int, RW | 7 at boot (-1 before init) | 0641: 1 CCI follows the faster big cluster, 2 LL at 2000 MHz, 4 CCI perf map while animating/touched | 0641 / `nord2_cpufreq.c`:982 | hardware limit: keep within the range in the description | source only |
| `kern.n2gdvfs` | int, RW | 1 (boot line sets 10) | 0461: GPU DVFS 0 off, 1 decisions logged, 2 act, +4 no wait for a gap between jobs, +8 UI profile: never below the cold-start OPP, up at 80 % while the UI animates (0492) | 0461 / `Nord2GPU.cpp`:257 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |
| `kern.n2gvolt` | int, RW | 0 (boot line sets 1) | 0633: 1 = GPU 886 MHz with a live VGPU/VSRAM raise to 800 mV (acts only with kern.n2gdvfs mode 2) | 0633 / `Nord2GPU.cpp`:283 | hardware limit: keep within the range in the description | source only |
| `kern.n2gprof` | int, RW | 0 (boot line sets 1) | 0631: GPU DVFS profile 0 = 0461/0492, 1 = fast ramp, 2 = performance (acts only with kern.n2gdvfs mode 2) | 0631 / `Nord2GPU.cpp`:284 | tuning: change only to test, values are lost at reboot | source only |
| `kern.nord2_sched_mode` | int, RW | 0 (boot-arg n2sch) | core-aware placement mode: 0 off, bits of N2S_* (0xf recommended) | 0099 / bsd/dev/nord2_sched_sysctl.c (CI base, patch only) | tuning | source only |
| `kern.nord2_sched_pri` | int, RW | 47 (line: 37 via n2schpri) | lowest priority eligible for big-core placement | 0099 / nord2_sched_sysctl.c (CI base) | tuning | yes (user, 2026-11-14..17; in the line on the phone) |
| `kern.nord2_sched_fast` | int, RW | 0 = from ADT (cortex-a78) (boot-arg n2big) | big-CPU mask | 0099 / nord2_sched_sysctl.c (CI base) | tuning | source only |
| `kern.nord2_sched_stats` | int, RW | 1 (boot-arg n2schst) | per-CPU accounting | 0099 / nord2_sched_sysctl.c (CI base) | diagnostic | source only |
| `kern.nord2_sched` | string, RD | - | core-aware placement totals (nord2 0099) | 0099 / nord2_sched_sysctl.c (CI base) | read-only status/counter | source only |
| `kern.nord2_cpuclk` | string, RD | - | measured clock of every CPU in MHz (nord2 0099) | 0099 / nord2_sched_sysctl.c (CI base) | read-only status/counter | source only |
| `kern.nord2_sched_watch` | string, RW | - | watched thread name or id (nord2 0099) | 0099 / nord2_sched_sysctl.c (CI base) | diagnostic | source only |
| `kern.nord2_sched_reset` | int, RW | action | write 1 to zero the totals (nord2 0099) | 0099 / nord2_sched_sysctl.c (CI base) | one-shot action | source only |

### TrollStore+Files

**Boot-args (4 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `n2sbcont` | 0..2 | 1 | Sandbox container path: 0 none, 1 .appex processes, 2 + containerized HOME (kern.n2sbcont) | 0730 / sweet_amfi.c:714 | kill switch | source only |
| `n2fsapfs` | 0/1 | 1 | report the HFS root as "apfs" to statfs once fileproviderd runs (kern.n2fsapfs) | 0734 / sweet_amfi.c:411 | kill switch | source only |
| `n2hfsapfs` | 0/1 | 1 | HFS answers APFS "J" fsctls of the file-provider stack (kern.n2hfsapfs) | 0733 / sweet_amfi.c:349 | kill switch | source only |
| `sweet_amfi_debug` | 0/1 | 0 | log the entitlements attached to each signature | 0035 / sweet_amfi.c:50 | log volume | source only |

**Sysctls (4)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2fsapfs` | int (handler), RW | 1 | 0734: report the HFS root as apfs to statfs once fileproviderd runs (0 off) | 0734 / `sweet_amfi.c`:392 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | yes (user, 2026-11-14..17: Files "On My iPhone") |
| `kern.n2sbcont` | uint, RW | 1 (boot-arg n2sbcont) | 0730: Sandbox container path for 0 none, 1 .appex processes, 2 + containerized HOME | 0730 / `sweet_amfi.c`:639 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | yes (user, 2026-11-14..17: TrollStore opens) |
| `kern.n2sblog` | uint, RW | 1 | 0731: Sandbox/AMFI call log for the file-provider stack | 0731 / `sweet_amfi.c`:771 | diagnostic: safe to toggle, may add log volume | yes (user, 2026-11-14..17: part of the verified line) |
| `kern.n2sblog_left` | int, RW | 800 | 0731: log lines left | 0731 / `sweet_amfi.c`:772 | diagnostic: safe to toggle, may add log volume | source only |

**Userland (9 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/var/mobile/Documents/ipa-inbox/` | drop folder (*.ipa, *.tipa) | created by ipainbox payload | auto-install through trollstorehelper after 120 s uptime (900 s per IPA); outputs done/, failed/, result.log | components/jbts-20261115/device/ipainbox.sh:13 | safe | yes (user, 2026-11-14..17) |
| `ipa-inbox/force` | marker | absent | install with force (replaces a non-TrollStore app) | ipainbox.sh:7,106 | replaces apps | source only |
| `/var/jb/etc/nord2-jb/ipainbox-disabled` | marker | absent | inbox job exits | ipainbox.sh:10 | kill switch | source only |
| `/var/jb/etc/nord2-jb/disabled` | marker | absent | whole jailbreak off (jbinit, sshd, ipainbox) | components/jbts-20261115/device/jbinit.sh:21, sshd-run.sh:7 | kill switch (VARIANT=jbts1-off, nord2-jb disable) | yes (user, 2026-11-14..17) |
| `/var/jb/etc/nord2-jb/ssh-disabled` | marker | absent | sshd stays down | sshd-run.sh:7 | kill switch (VARIANT=jbts1-sshoff, nord2-jb ssh off) | source only |
| `/var/jb/etc/nord2-jb/install-trollstore / postinst-done` | one-shot markers | present after install / made on first boot | install TrollStore + ldid once / bootstrap postinst already ran | jbinit.sh:29-85 | delete postinst-done to re-run | source only |
| `/var/jb/etc/ssh/sshd_config + authorized_keys` | config | password auth yes, root login yes (build env) | SSH policy; edit with nord2-jb password-auth on/off | components/jbts-20261115/build_jbts.py:253-267 | security: set a password or key | source only |
| `SpringBoard pref SBShowNonDefaultSystemApps=true` | pref key | set by jbts1 | show Sileo / TrollStore Helper icons | guest-jbts1.sh:110 | safe | source only |
| `build env SSH_PASSWORD / SSH_PASSWORD_FILE / SSH_PUBKEY / SSH_PASSWORD_AUTH / SSH_ROOT_LOGIN / NO_TROLLSTORE_AUTOINSTALL` | host env (payload build) | random password / - / yes / yes / unset | SSH credentials and policy baked into the jbts payload | build_jbts.py:240-297 | security | source only |

### diagnostics+logging

**Boot-args (12 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `sweetlog / ufslogsecs` | 0/1, 1..60 s | 1 / 10 | persistent console log on UFS and its flush period | 0047/0069 / sweet_ufs.c:1907,1911 | safe | source only |
| `sweetnull` | process list | backboardd,SpringBoard | tee these processes' /dev/null output into the kernel log | 0048 / mem.c:203 | log volume | source only |
| `sweetsample` | process list | SpringBoard,backboardd | process sampler targets | 0054 / sweet_sample.c:67 | log volume | source only |
| `applog / applog_name` | 0..2 / name list | 0 / "" (build-time default) | 1: stdout/stderr on /dev/null go to /dev/console for every program; 2: + OS_ACTIVITY_DT_MODE; applog_name=names tees only those | 0285 / kern_exec.c:2715 | log volume; 2 is slow | source only |
| `nord2fastlog` | 0/1 | 0 (line: 1) | fast early framebuffer log | 0082 / sweet_early.c (CI base) | safe | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2fbdiag` | 0/1 | 0 | framebuffer diagnostics | 0082 / SweetFramebuffer.cpp:1064 | log volume | source only |
| `sweetusblog` | 0/1 | 1 | live kernel log over USB | 0077 / sweet_usb.c:1775 | safe | source only |
| `n2pf` | 0..2 | 0 (line: 1) | Mali hardware counters sampled once per stats window (2 = memory stats and clock only) | 0122 / Nord2GPU.cpp:923 | small overhead | yes (user, 2026-11-14..17; in the line on the phone) |
| `nord2_gpu_stats` | 0/1 | 1 | GPU job statistics | 0089 / Nord2GPU.cpp:922 | safe | source only |
| `n2membw / n2dram` | 0..2 | 0 | CPU memory bandwidth test / read-only DRAM register dump at boot | 0120/0121 / nord2_cpufreq.c | diagnostic | source only |
| `sweetiolog / sweetscan` | process list | backboardd | IOKit user-client trace / crash heap-string scan targets | 0049/0051 / IOUserClient.cpp, osfmk/kern/exception.c (CI base) | log volume | source only |
| `disable-uart-irq` | 0/1 | 0 | serial console without UART IRQ | 0003 / pexpert/arm/pe_serial.c (CI base) | debug only | source only |

**Sysctls (1)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `kern.n2snap` | uint, RW | 1 | 0640 debug screenshot of the overlay plan (USB autotest) 1 on / 0 off | 0640 / `SweetFramebuffer.cpp`:1305 | kill switch / mode: 0 turns the feature off; change only to isolate a fault | source only |

**Userland (1 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `/Library/Preferences/Logging/Subsystems/com.apple.FileProvider.plist, com.apple.DocumentManager.plist, com.apple.pluginkit.plist` | plist (Level Info, Public) + marker nord2=fplog1 | absent unless fplog1 installed | Info-level, public-argument logging for the FileProvider stack | components/jbts-20261115/guest-fplog1.sh:14-45 | log volume; UNDO=1 removes | yes (user, 2026-11-14..17) |

### host tooling

**Userland (9 rows)**

| name | type/values | default | what it does | piece/patch/file | risk / when to change | verified? |
|---|---|---|---|---|---|---|
| `NORD2_WORK / NORD2_REPO` | path | required | work tree and main repo for every owner txn_*.sh | e.g. components/jbts-20261115/txn_jbts.sh:13 | - | source only |
| `VARIANT` | payload name | per script (jbts1, bt1, simcache1, nvfile1, gnssfix22, gfxall2...) | which payload; -off/-on/-sw* variants only write switch files | txn_jbts.sh:19, txn_bt1.sh:17, txn_simcache.sh:19, txn_gfxall.sh:48 | - | source only |
| `UNDO` | 0/1 | 0 | run the payload undo instead of the install (45 scripts) | txn_jbts.sh:25 | - | source only |
| `READS` | 1..600 s | 600 (12 older scripts: 900, above the console cap) | USB log capture time after the flash | txn_bt1.sh:16 | - | source only |
| `FB_WAIT` | seconds | 1800 | wait for fastboot | autocycle/to_fastboot.sh | - | source only |
| `P, PX, ONLY, GROW, ALLOW_OTHER_KERNEL, PULL_ODM, STOPON, OWNED, PAYLOAD, GUEST, ENTER` | various | see scripts | older-stage txn options (payload pick, HFS grow, pinned-kernel bypass, odm pull) | camflash/camprev/sbshift2/wallpaper/stock/storage/haptic2/autocycle txn scripts | older stages only | source only |
| `~/.config/nord2/txn.env (override path N2_TXN_ENV)` | sourced shell file | - | keys ADB, FASTBOOT, RESCUE_IMG, MIRROR, HT, INITRAMFS, WORK, READS for the release tool n2_txn.sh (owner scripts do not read it) | components/release-20261115/tools/n2dev/n2_txn.sh:10; docs/INSTALL-NORD2.md:166 | - | source only |
| `NORD2_PHONE_STATE` | dir | ~/.cache/nord2-phone | log-offset state of phone.py logs | components/autotest/phone.py:45 | - | source only |
| `TAG (6th argument of n2_txn.sh)` | string | required | names the run directory $WORK/cc-txn-TAG (must not exist); it is a positional argument, not an env var | components/release-20261115/tools/n2dev/n2_txn.sh:11-13 | pick a new TAG per run | source only |

## Boot-args no longer in the final kernel

These 17 names are parsed in a patch but not in the q10jfinal44ts stage source. A later piece rewrote or removed the code; most of them sat in `SweetFramebuffer.cpp`, `Nord2GPU.cpp` or the removed `sweet_fb_heap_probe.inc`. Putting them on the boot line has no effect.

| name | introduced in | what it did | replaced by |
|---|---|---|---|
| `n2fbblend`, `n2fbinval` | 0390-0392, 0416 | framebuffer blend / invalidate modes | overlay composition (`kern.n2fbhw` 3) |
| `n2pace`, `n2teminlead` | 0163-0165 | present pacing / TE minimum lead | 0397 pacing (`kern.n2fbpace`), `n2telead` |
| `n2scan` | 0101, 0416 | framebuffer scan mode | removed |
| `n2gl2` | 0664/0665 | GPU L2 flush policy (also sysctl `kern.n2gl2`) | removed from the final GPU driver |
| `n2gps` | 0350 | GNSS nodes opt-in | 0580: GNSS on by default (`nord2gnss=0` to disable) |
| `n2hp`, `n2hpm` | 0418/0419 | low-bank heap probe / EMI MPU words | the 0440 graphics heap (`kern.n2heap*`) |
| `n2impat`, `n2mapfm` | 0361 | GPU import pattern / map flags | removed |
| `n2memscan`, `n2memwrite` | 0176/0177 | GPU memory scan / write tests | removed |
| `n2proc` | 0180 | GPU per-process stats | removed |
| `n2st`, `n2stress` | 0188/0189 | GPU stress test | removed |
| `n2drp` | 0493 | DRAM rate probe at boot | `kern.n2dramprobe` (sysctl) and `n2gdv` +16 |

Sysctls that were also removed (12): `kern.n2fbhwtest`, `kern.n2fbhwhold` (0396), `kern.n2fbhwselftest` (0399; its image-check marker still shows up as the known "0399 IMAGECHECK stop" in the build log), `kern.n2fbiommutest` (0401), `kern.n2fbpoolalias` (0397), `kern.n2fbcpuemerg` (0413), `kern.n2gl2` (0664), and `kern.n2heap_probe`, `_step`, `_free_mib`, `_bad`, `_mpu` (0418/0419).

## Stock XNU boot-args present in the stage files

The stage contains whole stock XNU files (`kern_mib.c`, `kern_exec.c`, `unix_startup.c`, `mem.c`, `AppleARMSMP.cpp`), so it also parses 19 stock boot-args the port does not use or change. They are listed here for completeness and not documented further: `kmem`, `ncl`, `mbuf_pool`, `launchdsuffix`, `kernelmanagerd`, `serial`, `serverperfmode`, `legacy_footprint_entitlement_mode`, `dext_panic_coredump`, `execfailurecrashes`, `-arm64e_preview_abi`, `vm_shared_region_per_team_id`, `vm_shared_region_by_entitlement`, `vm_shared_region_reslide_aslr`, `vm_shared_region_reslide_restrict`, plus `cluster_power`, `kernPOST`, `enable_skstb` and `enable_skstsct` (the last four are in the thermal table). These files also carry 193 stock sysctls (`hw.*`, `kern.monotonic`, `kern.nbuf`, `vm.shared_region_*` and so on), which are left out of the sysctl tables.

## Undocumented in source

These are named in the docs, the logs or the boot line but have no source in this repository (or the source does not show what they do):

| name | where it is named | status |
|---|---|---|
| `kern.n2bt_coex` | `components/bt-20261115/TEST_PROCEDURE.txt:206` | explicitly "not added" (nothing real to send); no such sysctl |
| `n2prime` (TUNABLE, prime-first placement, default 1) | `notes/ENGINEERING-LOG.md:13764`, `components/cpu-20261114/NOTES-section2.md:15`; printed by `nord2_cpufreq.c:1095` | lives in the prebuilt `nord2_sched.o` (sched0099 object); its source is not in the repo |
| `nord2_gpu_assume_powered` | `notes/MORNING_REPORT-20261024.md:195` | proposed read-only probe; never in any patch or stage |
| `ignition_level`, `wdt_test`, `launchd_unsecure_cache`, `wdt`, `-noprogress`, `debug` | default boot line | consumed by stock iOS / XNU components, not by port code |
| `keepsyms` | removed by the packer (`--drop-arg keepsyms=1`) | stock; a no-op because the packer strips the symbol table |
| markers checked by `nord2_srcprobe.o`, `nord2_fscache.o`, `nord2_ycbcr.o` | linked into Nord2Metal | objects without source in the export; whether they read markers is unknown |
| gfxall17 `-seeded` / `-flip-seeded` payloads | `components/gfx200-20261114/shipall/txn_gfxall.sh` comments, `owned-gfxall17-flip-seeded.json` | payload directories not in the export (per the comments: read-only shader cache, plus `gfxall17-rot-flip`) |

### Obsolete userland markers (older stages only)

- **Nord2Metal (9):** `no-photos-ycbcr-cpu` (y2s62), `no-ycbcr-flush` (y2s61-62; now the opt-in `ycbcr-flush`), `no-ycbcr-stage` (y2s64-69), `no-gpu-dirty` (y2s65-69), `no-view-check` (y2s71), `bb-frame-probe` (y2s80-81), `no-backdrop` and `backdrop-rename` (y2s97), and `/private/var/db/nord2-metal/compositor-enable` (the October "tegu" compositor front end).
- **Camera (3):** `nord2-camera-nomark` (replaced by the opt-in `nord2-camera-mark`), `nord2-camera-clientprobe` (runs q10g*), and the AE2-era `nord2-camera-cpu-isp`/`gpu-isp` behaviour of version-1 libraries (both still read; see the camera table).
- **GNSS (6 families):** `nord2-gnssprobe`, `nord2-gnsspipe`, `nord2-locdiag` gates, and the `nord2-locfix/`, `nord2-locsim/`, `nord2-locationd/` payload directories. locsim1 also wrote the locationd preferences `SimulatedLocation*`.
- **Display:** `/private/var/db/nord2-fbhw-mode`, the one-line config of the old `n2fbhwctl` sysctl scheduler (`components/hwmulti-20261113-on0401c/fbm0402ctl/n2fbhwctl.c:3-27`).
- **Payload bookkeeping, not knobs:** `/private/var/db/nord2-stock/*` (about 15 receipts and backups), `nord2-*.json` receipts, `nord2-storage/disabled/`, `nord2-ssclean1/`. Payloads also write these preferences: `BKEnableALS=false` (com.apple.backboardd, bright1), `effects-haptic=true` (com.apple.preferences.sounds, haptic1), and removal of a pre-2020 `lastModulation` (com.apple.dasd, dasdmod1).
