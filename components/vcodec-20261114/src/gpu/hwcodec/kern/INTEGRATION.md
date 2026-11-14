# 0299 / 0310 integration — what the main session runs (package IK)

`gpu/hwcodec/kern/0299-nord2-jpegdev-vcodec.patch` is the **generated base** that turns the phone-verified 0298 kernel
(`gpu/hwcodec/kernel/b`) into the kernel with the three codec character devices. It is never edited: `make_patch.sh`
builds it from the three live `.inc` sources and the two frozen ABI headers, and verifies the round trip.

**Renumbering for the image (agreed with opus-kern, 11 Nov):** 0299 and 0300 are already used by the surface-budget
patches in the phone kernel q10in. The reviewed series therefore applies this content as **0310**
(`kern/p031x/0310-nord2-jpegdev-vcodec.patch`, built by `p031x/make_series.sh` from the live files the same way
`make_patch.sh` builds 0299; it renames the 0299 tokens in the added lines to 0310), then opus-kern's **0311**
(`kern/p031x/0311-nord2-vcodec-venc-autotest.patch`: `nord2_vcodec_at.inc` + its include + autotest command 8 + the
JPEG command-7 step-4 guard). There is no 0312: K02 merged the driver corrections (windows/parents/LARBs/close) into his
file and 0311 holds only the autotest part. `compile_check.sh` still tests `0299` on the q10at3 stage because that is the
fast, repeatable oracle; the image command below uses 0310/0311. If either file is missing, do not build this image — the
command refuses.

## What 0299 / 0310 contains

| File in `bsd/dev/` | Owner | What it does |
|---|---|---|
| `nord2_vcodec_relay.inc` | IK | minor 1 `/dev/redwood-vcodec-client` 0666, relay ioctls on both minors, `server_present`, DMA handle access rule + `RWVC_DMA_INFO` |
| `nord2_vcodec.inc` | K02 | minor 0 `/dev/redwood-vcodec` 0600, VEN/VDE power, register batches, DMA pool, polled IRQs, `n2vc_ven_get/put` |
| `nord2_jpegdev.inc` | K01 | `/dev/redwood-jpeg` 0666, `RWJ_INFO` / `RWJ_ENCODE` over the 0298 sequences |
| `redwood_vcodec.h`, `redwood_jpeg.h` | hw-lead (frozen) | byte-identical copies of `gpu/hwcodec/include/*.h`; `compile_check.sh` fails if they drift |
| `sweet_touch.c` | IK | three `#include`s after `nord2_hwcodec.inc` and two `*_start()` calls in `nord2_input_init` |

Nothing in 0299/0310 runs at boot: the three start functions only call `cdevsw_add()` + `devfs_make_node()`. No register
of any codec block is touched until a user-space ioctl asks for it, so a bad patch shows up as a missing device node or
a missing log line, not as hardware activity.

## Build (one command)

Run this block as one command; `NAME` is the new image name (the output directory must not exist). It is
`gpu/hwcodec/build_image.sh` with three changes: the series tail is
`0296,0298,0299,0300,0310,0311` (the surface-budget 0299/0300, 0298, then opus-kern's 0310/0311), the patch dirs include
`gpu/build/surfbudget-20261111`, `gpu/build/surfholders-20261111` and `gpu/hwcodec/kern/p031x` instead of
`gpu/hwcodec/kern`, and a pre-flight check refuses if 0310/0311 are missing. The first two lines refresh the generated
base (`make_patch.sh`) and then 0310 from the live K01/K02/IK files (`p031x/make_series.sh`) — **always run them**,
otherwise 0310 would carry the copies that were current when it was last generated. Do **not** add `gpu/hwcodec/kern`
itself as a patch dir: its `0299-…` file would clash with the surface-budget 0299 (build_p1_candidate asserts exactly one
patch per number).

```bash
W=$NORD2_WORK; NAME=q10hw2; \
bash $W/gpu/hwcodec/kern/make_patch.sh && bash $W/gpu/hwcodec/kern/p031x/make_series.sh && \
{ [ -e $W/gpu/hwcodec/kern/p031x/0310-nord2-jpegdev-vcodec.patch ] && [ -e $W/gpu/hwcodec/kern/p031x/0311-nord2-vcodec-venc-autotest.patch ] || { echo "kern/p031x 0310/0311 missing (opus-kern)"; exit 1; }; } && \
cd $W && B=$W/gpu/build && SB=$B/scratchpad-backup-20261106 && O=$W/gpu/hwcodec/build/$NAME && \
{ [ ! -e $O ] || { echo "$O exists"; exit 1; }; } && \
SERIES=$(grep '^S=' $B/device-mmu-q10hq-candidate/build_q10hq.sh | cut -d= -f2),0296,0298,0299,0300,0310,0311 && \
PD="" && for d in $SB/bb1d $SB/audiofix $SB/slider $SB/callaudio-d $SB/sensors-dd $B/applog-20261110 $B/applog-20261110b $B/applog-20261110c $B/sandboxext-20261110 $B/camfix-20261110 $B/camfocus-20261110 $B/hwmodel-20261110 $B/camdual-20261110 $B/camgain-20261110 $B/camfair-20261110 $B/autotest $B/surfbudget-20261111 $B/surfholders-20261111 $W/gpu/hwcodec/kernel $W/gpu/hwcodec/kern/p031x; do PD="$PD --patch-dir $d"; done && \
mkdir -p $O && \
python3 $B/autotest/build_p1_candidate.py --mode patched --series $SERIES --baseband $PD --hwmodel-default Camera=N104AP --applog-name-default Camera --recompile-power --sched b --cpufreq --name $NAME --out $O/kernel > $O/kernel.stdout 2>&1 || { echo FAILED; tail -5 $O/kernel.stdout; ls -t $O/kernel/*.log | head -1 | xargs tail -20; exit 1; } && \
python3 $B/device-mmu-q10hq-candidate/pack_with_args.py --drop-arg nord2wifiemi=0 --drop-arg nord2audioinstall=1 --kernel $O/kernel/kernel.development.nord2-metal-$NAME --system-metal --audio-package ${AUDIO_PKG:-$B/autotest/pins/audio-q10ik.pkg} --output-dir $O/ui-sched --image-name nord2-ios-metal-$NAME-ui654fb60dvfs-cap3500-ram3776-sched.img --extra-args "n2fbhz=60 n2dvfs=3000 n2pf=1 n2schpri=37" > $O/ui-sched.stdout 2>&1 || { echo PACKFAILED; tail -5 $O/ui-sched.stdout; exit 1; } && \
rm -rf $O/ui-sched/kernel.strip $O/ui-sched/gpu.payload $O/kernel/kernel.startup.a && echo done && tail -12 $O/ui-sched.stdout
```

Pass criteria for the build:
- last line `done`, and `$O/ui-sched/check.log` says `FAILURES: none` (same packer as q10ik/q10hw1);
- `$O/kernel/apply-0298.log`, `apply-0299.log` (surface budget), `apply-0300.log` (surface holders), `apply-0310.log`
  and `apply-0311.log` exist, all empty of rejects;
- the only object whose *code* differs from q10ik is `sweet_touch.o` (same statement as 0298; compare with
  `llvm-objdump -d`, DWARF paths differ so object bytes are not comparable);
- record the image SHA-256, kernel SHA-256 and sizes in `STATUS.md`/`runs/<NAME>/` as before.

Before spending the build, `bash gpu/hwcodec/kern/compile_check.sh` must end with
`KERNEL COMPILE OK, new undefined symbols: 0 unknown`; it compiles the same staged `sweet_touch.c` with the same CI flags
and checks every new undefined symbol against the q10at3 kernel image. `p031x/make_series.sh` independently checks that
0310+0311 apply to the q10in stage and that 0310 is the current live K01/K02/IK content; the `MANIFEST.txt` it writes
records the hashes it used.

Pre-flight done by IK on 11 Nov: the earlier build order (0085-0087 + the q10hq `S=` list + 0296 + 0298 + 0299) applied
cleanly to `gpu/build/xnu-stage-backup-pre-p15-20261028/xnu-stage` with no rejects, and the resulting
`bsd/dev/sweet_touch.c` compiled with the CI flags to a 1.3 MB object (0 errors). The surface 0299/0300 and 0310/0311
steps are opus-kern's (`p031x/STATUS.md` and `MANIFEST.txt`); the only difference to `compile_check.sh` is the base tree
(pristine + all patches vs the q10at3 source state).

## Phone test order (flash `boot` only; never stock Android over iOS)

The scripts are hw-t02's `gpu/hwcodec/tests/phone/` (the runner is `guest-n2hw-tests.sh`, the host wrapper
`run_phone_tests.sh`, the per-stage pass lines in `README.md` and `00`–`40-*.md`; install per `INSTALL.md`). opus-kern's
VENC audit (0311) must be in the same image before any stage that powers VENC/VDEC. The order below is the one agreed
with hw-t02; do not swap the JPEG and vcodec stages (both use VEN, and the scripts assume no daemon is running during
them).

1. Install the five guest files with the main session's rootfs transaction (`INSTALL.md`); no launchd job at boot. Files:
   `rwjpeg_tool` (K01/J02), `rwvc_probe` (T02), `vcodecd` (hw-ip), `n2vcd_cli` (hw-p03), `n2hw_phone_tests` (T02).
2. Flash the exact boot image, boot iOS, wait for the autotest channel:
   `cd gpu/build/autotest && python3 phone.py wait && python3 phone.py logs <out>/boot.log`. Expected once, from
   `nord2_input_init` (`nord2input=1`, `nord2stopat=23`): `NORD2 VCODEC: /dev/redwood-vcodec made,
   /dev/redwood-vcodec-client made, JPEG VEN sharing on ...` and `NORD2 JPEG: /dev/redwood-jpeg made ...`. No
   `NORD2 JPG:` / `NORD2 VCODEC:` register line at boot.
3. `STAGE=preflight n2hw_phone_tests` -> `PASS device nodes`, `PASS rwvc-info` (`N2VC info abi=1`), `PASS rwvc-client`
   (`N2VC client verdict PASS`), `PASS rwjpeg-info`. This is read-only; no codec register is touched.
4. `STAGE=jpeg n2hw_phone_tests` -> `rwjpeg_tool selftest` last line `N2JPG selftest 640x480 PASS 12/12`, the 1920x1080
   benchmark, and the 640x480 encode; fetch `hw640.jpg` + `src640.nv12` and run
   `python3 gpu/hwcodec/tests/check/jpeg_check.py ...` on the PC (verdict PASS, PSNR >= 30 dB). This is the same JPEG
   path as q10hw1 plus the device; a regression here is K01/IK, not the new codec hardware.
5. `STAGE=vcodec n2hw_phone_tests` -> `N2VC all PASS`, `N2VC regs VENC done=6/6`, `N2VC verdict PASS`; then leave the
   phone alone for 60 s and `python3 phone.py status` (the camera bring-up once died 27-44 s after a bad protection
   release). Do not run `hwc.py` command 7 concurrently: both share the VEN domain.
6. `STAGE=daemon ENABLE_DAEMON=1 n2hw_phone_tests` only after hw-ip/hw-p03 confirm the binaries and CLI; fetch
   `out.h264` and run `python3 gpu/hwcodec/tests/check/h264_check.py` on the PC (Annex B, 640x480, 30 frames, first key,
   per-frame PSNR). This is the only step that proves the relay end-to-end with two processes.
7. Relay-only checks (no daemon): `rwvc_probe client` covers `RWVC_INFO`, `SEND` with no server -> `ENXIO`, `RECV` 0 ms
   -> `EAGAIN`, and the minor-1 hardware-ioctl denials (`EACCES`). The full relay (one server + three clients, queues,
   close notices, `RWVC_DMA_INFO`) is host evidence: `bash gpu/hwcodec/kern/test_ik/run_tests.sh` (1476 checks x 3
   configurations, ASan+UBSan / -O2 / TSan). If no shell/exec path exists, run the guest script through the one-shot
   launchd job `com.nord2.hwcodec.tests.plist`, not a KeepAlive job.
8. Recovery is unchanged: `fastboot flash boot` of the last good q10ik/q10hw1 image, or the RAM ADB rescue. `userdata`
   holds iOS; never boot/flash stock Android.

## Relay ABI as implemented (for hw-ip/hw-t02)

- A client is a **process** with minor 1 open (XNU `cdevsw` gives `d_open/d_close/d_ioctl` only `struct proc *`; there is
  no per-file pointer, `bsd/miscfs/specfs/spec_vnops.c:353`). Two descriptors in one process share one kernel-assigned id.
  This per-process identity is accepted by hw-lead and is now written in `include/redwood_vcodec.h` rev 2.
  `d_open` runs per open but `d_close` only when the node's last descriptor anywhere closes (`spec_vnops.c:2760`), so
  the relay holds a `proc_find()` reference per client from open to slot free and checks each open client's process at
  every relay entry (`n2vr_reap()`): a process that is gone or `proc_exiting()` gets the normal `{client, 0}` close
  notice and K02's per-client state is freed. A client that closed the node but is still alive cannot be detected (XNU
  has no per-open close); vcodecd must not rely on that when two or more clients are open — use its own session timeout.
  8 clients at a time; a 9th open gets `EBUSY` while all slots are taken or waiting for their close notice to be read.
- Per client: 16 datagrams of `RWVC_MSG_MAX` inbound from the server; 16 outstanding toward the server. `SEND` into a
  full queue is `EAGAIN` (never blocks, never drops a live receiver's data). `RECV`: 0 ms -> `EAGAIN`, expiry ->
  `ETIMEDOUT`, timeout > 60000 ms -> `EINVAL`, buffer smaller than the datagram -> `EMSGSIZE` and the datagram stays
  queued, no server and nothing queued -> `ENXIO`, copyin/copyout fault -> `EFAULT` (a failed copyout loses that one
  datagram).
- Server `SEND` needs `client` = destination id (0 -> `EINVAL`, closed/unknown -> `ESRCH`); a client `SEND` must have
  `bytes >= 1` (0 is reserved for the close notice). Server `RECV` returns data and `{client, bytes = 0}` close notices in
  arrival order.
- DMA handles: the allocating file always; another file only when the handle was allocated with `RWVC_DMA_SHARED` (the
  frozen header's `flags` bit 0) **and** the accessor is on the other minor. Two clients never share; the server cannot
  touch a client buffer that was not allocated shared. Minor 1 accepts `RWVC_DMA_ALLOC/FREE/RW/INFO`; the hardware
  ioctls (`POWER`, `REGS`, `WAIT_IRQ`, `IRQ_CLEAR`, `DMA_FILL`) are `EACCES` there. `RWVC_DMA_INFO` (rev 2) is answered
  by K02's pool on both minors under the same rule: `bytes`/`out_pa` are results, `EINVAL` handle 0, `ENOENT` unknown,
  `EPERM` not usable by the caller. The relay's minor-1 path checks that the process still has a live client slot before
  forwarding to K02; minor 0 uses K02's own cdevsw, not the relay.
- `n2vc_relay_closed(struct proc *)` (K02) is called by the relay when a client ends (last close or reaped process),
  while the relay lock is still held and the process reference is still valid (no pointer ABA), so the client's DMA
  handles and power references do not outlive it. K02 never calls back into the relay while holding `n2vc_lock`; the only
  lock order is `n2vr_lock -> n2vc_lock`.

## Regeneration and drift

`make_patch.sh` reads the live `gpu/hwcodec/kern/bsd/dev/*.inc` and `gpu/hwcodec/include/*.h`. If K01/K02/opus-kern
change their files after the image was built, the generated base must be regenerated and 0310 refreshed from it (the
build command above runs `make_patch.sh` first). `compile_check.sh` also runs `make_patch.sh` first, so a green compile
check always refers to the patch that exists on disk at that moment. Record the SHA-256 of
`0299-nord2-jpegdev-vcodec.patch`, the three `.inc` files, and 0310/0311 next to the image in `runs/<NAME>/`.
