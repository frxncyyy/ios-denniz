# gpu/hwcodec -- install layout (package IP)

Status: **host-tested only** (the payload is generated under `/tmp` and applied to a fake root by
`install/stage.sh --host-test`); the main session installs it on the phone. Nothing here has run on the phone.

## 1. What is installed where

**The real phone install is a rootfs transaction, not a shell script on the phone** (opus-ios owns it:
`gpu/build/apps-20261104/guest-vcodec.sh` + `guest-vcodec-undo.sh` + `owned-vcodec.json`). Verified on the rootfs
mirror: iOS has no `/bin/sh`, no `launchctl`, no `python3`, and daemons start only from the launchd job cache
`/System/Library/xpc/launchd.plist`; a standalone `/System/Library/LaunchDaemons` plist is not read. The transaction
copies the binaries, stages the vendor set under `/usr/local/lib/n2vendor/{bin,lib}`, adds the job to the cache,
creates `/private/var/db/nord2-vcodec-run`, and passes `--vendor /usr/local/lib/n2vendor --gate
/private/var/db/nord2-vcodec-run` to the daemon.

The files this package produces are:

| Path | Mode | Source | Purpose |
|---|---|---|---|
| `/usr/local/bin/vcodecd` | 0755 | built by `gpu/hwcodec/build_ios.sh` | the daemon: minor 0 of `/dev/redwood-vcodec`, the relay, the hosted vendor set |
| `/usr/local/bin/n2vcd_cli` | 0755 | P03's `client/n2vcd_cli.c` (built now) | encode a YUV file to Annex B over the relay; phone-side test client |
| `/usr/local/bin/rwjpeg_tool` | 0755 | J02's `jpeg/rwjpeg_tool.c` (same script) | hardware-JPEG selftest/bench; independent of vcodecd |
| `/usr/local/lib/n2vendor/bin/vpud` | 0755 | the phone's own `/vendor` (transaction) | the Android daemon binary (not executed by us: its libraries are) |
| `/usr/local/lib/n2vendor/lib/libvpud_vcodec.so` | 0755 | the phone's own `/vendor` (transaction) | IPI service loops (loaded by the hosted set) |
| `.../lib/libvcodecdrv.so`, `.../libvcodec_utility.so`, `.../libvcodec_oal.so` | 0644 | the phone's own `/vendor` (transaction) | the codec HAL, eVideo/OAL helpers, pthread/memory wrappers |
| `/private/var/db/nord2-vcodec-run` | 0644 | transaction | `--gate` marker; absent = vcodecd logs and exits 0 |
| `/var/log/nord2-vcodecd.log` | 0644 | launchd job | the daemon's stderr/stdout (`N2VCD ...` lines); the main session fetches it through the USB log channel |

This repository also contains an **offline/fake-root** installer (`install/install.py`, `install/undo.py`,
`install/stage.sh`) used by `stage.sh --host-test` to verify modes, receipts, `--force`, full→partial carry-forward,
undo and `--purge-vendor` without a phone. It writes the same binaries/plist and a vendor tree under
`/private/var/db/nord2-vcodec/vendor/{bin,lib}` for that test; it is not the mechanism used on the live phone (the iOS
root has no python3).

## 2. Install procedure (main session)

1. Build the kernel with 0299 (K/IK) and boot it; `/dev/redwood-vcodec` and `/dev/redwood-vcodec-client` must exist.
2. Build the user space on the PC: `bash gpu/hwcodec/build_ios.sh` -> `/tmp/n2hw-ip/ios`. The script builds IV's
   arm64 archive itself when the iPhoneOS SDK is present (`make ios-lib`, output under `<outdir>/../iv-ios`), compiles
   P03's client, and links `vcodecd` and `n2vcd_cli` (the state of this tree: `vcodecd: linked` 409312 bytes,
   `n2vcd_cli 70000 bytes`). Set `N2VHOST_IOS_LIB` to use a prebuilt IV archive. `bash gpu/hwcodec/ios/build.sh`
   (opus-ios) also builds `vcodecd` plus `n2vcd_probe`/`n2vcd_selftest` against the real SDK libSystem; either
   `vcodecd` is shim-complete and fake-free. Both vcodecd binaries carry `--selftest`.
3. Stage the payload under `/tmp` (optional for the transaction, required for the fake-root test):
   `bash gpu/hwcodec/install/stage.sh <tag> [outdir]` -> `/tmp/n2hw-ip/stage/payload` (files + `plan.json`).
4. Install through the opus-ios rootfs transaction: `gpu/build/apps-20261104/guest-vcodec.sh` (its own staging,
   HFS+/catalog checks, launchd-cache edit, full changed-range readback and undo receipt). Give it the three binaries
   from step 2 and the phone's own vendor files; it copies the vendor objects to `/usr/local/lib/n2vendor/{bin,lib}`
   and creates the gate marker.
5. **First phone run: `vcodecd --selftest`** (as the installed user, no hardware, no vendor files, no daemon start).
   Expected: five `N2VCD selftest PASS ...` lines and the last line
   `N2VCD selftest PASS 20/20 (4 GiB, 2 threads, 1000000 insns, fault, TLS)`, exit status 0. Any `FAIL <step>` line is
   the whole result: stop there and report that step; do not start the daemon on a failed selftest.
6. The daemon is started by launchd at the next boot (`RunAtLoad` true). With the gate absent it logs
   `N2VCD ... gate ... absent: not starting` and exits 0, so a boot without the transaction does nothing. With the
   gate present the first lines are:

```
N2VCD 0.000 start dev=/dev/redwood-vcodec vendor=/usr/local/lib/n2vendor fake=0 socket=-
N2VCD 0.000 host set: vcu=yes vendor_hw=yes unresolved=... milestone=M-b
N2VCD 0.000 serving /dev/redwood-vcodec
```

`milestone=M-a/M-b` means the vendor set loaded and its init ran but no codec session has started. Milestone changes
after bring-up are logged once each (`N2VCD ... milestone M-d ...`). The first successful `OPEN` logs
`N2VCD ... session N open ...` followed by `N2VCD ... session N sequence header B bytes (separate job)`: the
stock separate-header job whose cached SPS/PPS(/VPS) the daemon prepends before every IDR. A guest stop logs one
`N2VCD ... fatal guest stop ... addr=0x... (<n2elf_describe result>) ...` line, powers the blocks off (best effort)
and exits **70**; launchd restarts the daemon because `KeepAlive { SuccessfulExit: false }`. Hardware timeouts
(`E_HW`) are not fatal and the daemon keeps serving.

## 3. Undo

Use the transaction's `guest-vcodec-undo.sh` (owned by opus-ios), which removes exactly the files in its
`owned-vcodec.json`, restores the launchd-cache entry and removes the gate marker. For an **offline fake root only**,
`install/undo.py` removes the paths in `install.json` (`--purge-vendor` also removes the vendor copies). It never
touches the kernel, the VideoToolbox plug-in, or any other package's files.

The daemon exits 70 on a guest stop and 0 on SIGTERM. There is no shell on the phone, so the main session stops it by
removing the gate marker + rebooting, or by using the transaction undo.

## 4. launchd job

`install/com.nord2.vcodecd.plist` is the reference content for the cache entry the transaction writes:

- `Label com.nord2.vcodecd`, `ProgramArguments /usr/local/bin/vcodecd --vendor /usr/local/lib/n2vendor --gate
  /private/var/db/nord2-vcodec-run`.
- `RunAtLoad` **true**; the gate makes a boot without the transaction a no-op (exit 0).
- `KeepAlive { SuccessfulExit: false }`, `ThrottleInterval 10`: exit 70 (guest stop) restarts; exit 0 (gate absent or
  SIGTERM) does not.
- `Standard{Out,Error}Path /var/log/nord2-vcodecd.log`.
- `EnvironmentVariables N2HW_VENDOR_DIR` points at the same vendor directory for a manual start.

The transaction edits the existing `/System/Library/xpc/launchd.plist` cache (532 jobs, including the working
`com.nord2.videoprobe` / `org.nord2.metal-probe` precedents) and preserves the metal receipt's
`installed_launchd_sha256`; the standalone plist file is documentation, not installed.

## 5. Slim.videoencoder integration (proposal, owned by hw-lead / P04 / video lead)

The plug-in (`Slim.videoencoder`, built by `gpu/video/build_ios.sh`) is the client of vcodecd:

- P04's `n2hwavc_vt_create` / `n2hwhevc_vt_create` register as `'avc1'` / `'hvc1'` encoders through the existing weak-symbol
  mechanism in `gpu/video/vt/n2vt_shim.c` (same pattern as `n2avc_vt_create` / `n2jpeg_vt_create`). `build_ios.sh`
  compiles P04's four plug-in objects (`vt/annexb.c`, `vt/n2hwenc_vt.c`, `vt/n2hwenc_client.c`,
  `vt/n2hwenc_register.c`) for arm64-apple-ios16.0 and hands them to gpu/video; they are not linked into vcodecd.
  Until M4c has encoded on the VENC, the shim should call `n2hwenc_register_all()` only when the operator has created
  `N2VT_SWITCH_CLAIM_HW` (P04/R2 agreement, filed as F32), so a daemon that is absent or wedged cannot affect the
  software `'avc1'`/`'hvc1'` fallback.
- The input NV12 buffer must use `stride == align16(width)` (the VENC's `buf_width`) and chroma at
  `stride * align16(height)`; `OPEN_OK.in_buf_bytes` is exactly that size. The output buffer must be at least
  `OPEN_OK.out_buf_bytes`.- The object connects through P03's `n2vcd_transport_relay("/dev/redwood-vcodec-client")`, sends `HELLO` + `OPEN`, and
  forwards frames as `N2VCD_ENC_FRAME` with the client's DMA buffers (allocated with the share flag) and gets Annex B +
  `header_bytes` back. It converts to AVCC and builds the format description from the parameter sets in the first key frame.
- If `OPEN` fails because vcodecd is absent (`ENXIO`/`ENOENT` on the relay, or `N2VCD_E_SESSION`/`E_HW`), StartSession must
  return the "try the next encoder" error so VideoToolbox falls back to the software encoder. This is a plug-in requirement,
  not something the daemon can enforce.
- A guest stop that happens between requests arrives as an **unsolicited** `N2VCD_ERROR` with `seq = 0`,
  `status = N2VCD_E_GUEST` and the `n2a32_stop` code in `detail`; the client must treat `seq 0` as "session dead",
  not as the reply to any outstanding request. seq 0 is reserved: a request with seq 0 gets `E_ARGS`.
- Order: kernel 0299 first, then this payload (vcodecd + vendor copies), then the plug-in with P04 linked. Starting the
  plug-in without the daemon is safe: it just falls back to software.

vcodecd only ever opens `/dev/redwood-vcodec` (minor 0). The plug-in only opens `/dev/redwood-vcodec-client` (minor 1,
0666). The daemon never opens the JPEG device; J02's tool is separate.

## 6. Known gaps (honest)

1. **iOS link is complete on this tree.** `build_ios.sh` builds IV's `make ios-lib` archive itself when the
   iPhoneOS16.5 SDK is present (writes `<outdir>/../iv-ios`), extracts IV's runtime objects and links them directly
   (the test-only fake hardware and fake VENC members are excluded, and the V32/V33 shim tables are checked present with `llvm-nm`),
   compiles P03's client and links `n2vcd_cli` plus `vcodecd` from P03's non-CLI objects as a static archive. It
   reports `vcodecd: linked`, `n2vcd_cli 70000 bytes`, `vcodecd 409312 bytes` here. `daemon/sdk_symbols.txt` lists
   every supplemental libSystem symbol, each checked against the SDK's own `usr/lib/libSystem.tbd`. If a dependency
   is missing the script falls back to an object-only report and proves the rest with a throwaway `-ENODEV` stub
   (deleted, not a product).
2. **Client DMA physical addresses are complete in the tree, untested on the phone.** `RWVC_DMA_INFO` is in
   header rev 2, K02 implements it (`kern/bsd/dev/nord2_vcodec.inc:633,1036`), P03 allocates client buffers with
   `RWVC_DMA_SHARED`, and vcodecd re-queries each handle and registers it pa-only with V41. No host test can
   exercise the real ioctl; the first phone encode is the proof.
3. `daemon/test/p03_absent.c` supplies `n2vc_hw_open_dev` only for the IP-REAL test binary (it runs `--fake` and does
   not link P03's client). It is test-only and cannot reach a device.
4. The job is installed by the opus-ios transaction into `/System/Library/xpc/launchd.plist`; the standalone plist
   in `install/` is reference content, not read by iOS. The transaction owns the cache edit and its receipt.
5. `install.py`/`undo.py` are tested only against a fake root (see `install/stage.sh --host-test`): file modes, sha256
   checks, receipt, idempotent re-install, `--force`, full→partial receipt carry-forward, undo,
   `undo --purge-vendor`, and that an unrelated file survives. Real root-image permissions and launchd behaviour
   are untested.

## 7. Files

```
gpu/hwcodec/daemon/          vcodecd.c, n2vcd_srv.[ch], n2ip_deps.h, n2vcd_selftest.[ch], n2vcd_selftest_prog.h,
                             sdk_symbols.txt, test/
gpu/hwcodec/build_ios.sh     arm64-apple-ios16.0 build of vcodecd, rwjpeg_tool, the plug-in objects, n2vcd_cli
gpu/hwcodec/install/         LAYOUT.md, stage.sh, install.py, undo.py, test_install.py, com.nord2.vcodecd.plist
gpu/hwcodec/ios/             opus-ios probe/selftest and its alternative vcodecd build (not package IP)
```
