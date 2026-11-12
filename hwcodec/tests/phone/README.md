# T02 phone tests — hardware JPEG, /dev/redwood-vcodec, VENC, daemon

These are the **main session's** tests. T02 never runs them, never flashes and
never touches the phone. Everything here is written so that a failed step
leaves a named log file and a kernel-log slice, and so that no step writes a
codec register unless the kernel has powered its block first.

## Safety rules (unchanged from AGENTS.md)

- Flash **boot only** and only the exact image the main session prepared.
- iOS lives in `userdata`; never boot or flash stock Android over it.
- Recovery: the last good `q10ik`/`q10hw1` boot image, or the RAM ADB rescue.
  See `50-recovery.md`.
- Do not run the JPEG device test and autotest command 7 (`hwc.py`) at the
  same time: they share the VEN domain model.

## What is needed

| Item | Where it comes from | Guest path used by the scripts |
|---|---|---|
| kernel with 0299 (`/dev/redwood-jpeg`, `/dev/redwood-vcodec`, relay) | hw-ik `kern/INTEGRATION.md` build command | — |
| `rwjpeg_tool` | hw-j02 `jpeg/rwjpeg_tool.c` | `/usr/local/bin/rwjpeg_tool` |
| `rwvc_probe` | this directory, `rwvc_probe.c` | `/usr/local/bin/rwvc_probe` |
| `vcodecd` | hw-ip `daemon/` | `/usr/local/bin/vcodecd` |
| `n2vcd_cli` | hw-p03 `client/` | `/usr/local/bin/n2vcd_cli` |
| `n2hw_phone_tests` | this directory, `guest-n2hw-tests.sh` | `/usr/local/bin/n2hw_phone_tests` |

Build `rwvc_probe` for iOS with the same toolchain as the other hwcodec
tools; it includes only `redwood_vcodec.h` and libSystem:

```sh
clang -c --target=arm64-apple-ios16.0 -I gpu/hwcodec/include \
      gpu/hwcodec/tests/phone/rwvc_probe.c -o /tmp/n2hw-T02/rwvc_probe.o
ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -adhoc_codesign \
      -o /tmp/n2hw-T02/rwvc_probe /tmp/n2hw-T02/rwvc_probe.o gpu/video/sdk/lib/*.tbd
# (the TBD glob is the one J02 used; if a symbol is missing, hw-ip adds it to
#  gpu/video/sdk/symbols.txt and regenerates the TBDs)
```

Install the five files with the main session's normal root-file-system
transaction. Do **not** install a launchd job that runs the tests at every
boot; run `n2hw_phone_tests` on demand (or from a one-shot launchd job) so a
failed test does not loop.

## Run

The guest runner is one script; it writes `/var/tmp/n2hw/results.txt` and one
log per test:

```sh
# on the phone, as root
STAGE=all /usr/local/bin/n2hw_phone_tests
cat /var/tmp/n2hw/results.txt
```

The host wrapper reads the kernel log over the existing autotest channel and
runs the guest script through `$PHONE_EXEC`:

```sh
PHONE_EXEC="ssh root@nord2" \
PHONE_GET="ssh root@nord2 cat" \
  bash gpu/hwcodec/tests/phone/run_phone_tests.sh q10hw2 all
```

If `$PHONE_EXEC` is not set, the wrapper prints the guest commands and exits 2
rather than guessing how the main session reaches the phone. Results land in
`/tmp/n2hw-T02/phone/q10hw2/`.

When there is no shell/exec path at all, use the one-shot launchd job
`com.nord2.hwcodec.tests.plist`; `INSTALL.md` lists the files, modes and the
load/unload commands. The job is `RunAtLoad`, not `KeepAlive`, so a failed
test does not loop.

Stages can be run one at a time: `preflight`, `jpeg`, `vcodec`, `daemon`,
`all`. The daemon stage is **off by default** (`ENABLE_DAEMON=1` to enable)
until hw-ip/hw-p03 confirm the binary names and CLI; see
`40-daemon-encode.md`.

## Boot log, once, before any test

Expected from `nord2_input_init` (with `nord2input=1`):

```text
NORD2 VCODEC: /dev/redwood-vcodec made, /dev/redwood-vcodec-client made, JPEG VEN sharing on (VENC 0x17020000, VDEC 0x16000000; pool 0xda800000+0x5600000; polled, irq_mode 0)
NORD2 JPEG: /dev/redwood-jpeg made (major N, 0666), 24 MiB source + 16 MiB output at 0xd8000000
```

No `NORD2 JPG:` or `NORD2 VCODEC:` register line should appear at boot. The
three nodes must be `redwood-jpeg` 0666, `redwood-vcodec` 0600 root, and
`redwood-vcodec-client` 0666 (`00-preflight.md`).

## Milestones

| File | What it proves | Pass line |
|---|---|---|
| `00-preflight.md` | nodes, RWVC_INFO, no register touched | `PASS device nodes`, `PASS rwvc-info`, `PASS rwvc-client`, no `NORD2 VCODEC:` error |
| `10-jpeg.md` | `/dev/redwood-jpeg` through J02's tool | `N2JPG selftest 640x480 PASS 12/12`; PC `jpeg_check.py` PASS |
| `20-vcodec-readonly.md` | RWVC_INFO on minor 0, relay absent | `N2VC info abi=1`; no `NORD2 VCODEC:` error |
| `30-venc-power.md` | each block's power-on/off and first VENC read | `N2VC all PASS`, `N2VC regs VENC done=6/6`, `N2VC verdict PASS` |
| `40-daemon-encode.md` | daemon + client produce H.264 | `N2VCD ...` and PC `h264_check.py` PASS (tool names pending) |

`50-recovery.md` is the recovery recipe, not a milestone.

## What the host checks afterwards

Copy `hw640.jpg` and, for the daemon, `out.h264` to the PC, then:

```sh
# 640x480 hardware JPEG against the J02 pattern (stride 640, rows 480)
python3 gpu/hwcodec/tests/check/jpeg_check.py hw640.jpg --width 640 --height 480 \
        --source src640.nv12 --stride 640 --rows 480

# H.264 Annex B from the daemon, 640x480, 30 frames
python3 gpu/hwcodec/tests/check/h264_check.py out.h264 640 480 30 --source src640.nv12
```

The J02 pattern is 16-padded: for 1920x1080 pass `--stride 1920 --rows 1088`;
`jpeg_check.py` reads only the first `height` rows and ignores the padding.
If the pattern file is not fetched, regenerate it on the PC (same formula):
`python3 gpu/hwcodec/tests/check/gen_yuv.py src640.nv12 640x480 --pattern static --align16`.
