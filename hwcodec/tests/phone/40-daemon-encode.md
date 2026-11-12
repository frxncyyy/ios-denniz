# 40 — daemon encode (`vcodecd` + `n2vcd_cli`)

**Status: tool names and CLI still pending from hw-ip / hw-p03.** The guest
runner keeps this stage off by default (`ENABLE_DAEMON=1` turns it on) and
`SKIP`s it when the binaries are absent. This file records the intended
sequence and the pass criteria; update the two commands when hw-ip/hw-p03
confirm them, without changing the milestones above.

## Preconditions

- `30-venc-power.md` passed (each block powers on and off; VENC reads work).
- `vcodecd` is installed and can read the vendor objects from the phone's own
  `/vendor` (hw-ip's install layout).
- The daemon runs as **root** (minor 0 is 0600); the client may run as any
  user that can open `/dev/redwood-vcodec-client` (0666).

## Intended steps

```sh
OUT=/var/tmp/n2hw
/usr/local/bin/rwjpeg_tool pattern 640x480 $OUT/src640.nv12   # source picture
/usr/local/bin/vcodecd $VCODEC_ARGS >$OUT/vcodecd.log 2>&1 &
pid=$!
sleep 2
/usr/local/bin/n2vcd_cli info $CLI_ARGS
/usr/local/bin/n2vcd_cli enc $OUT/src640.nv12 640x480 30 2000000 $OUT/out.h264 $CLI_ARGS
kill $pid
```

The guest runner does exactly this when `ENABLE_DAEMON=1`; `VCODEC_ARGS` and
`CLI_ARGS` are passed through (for example `--socket /var/run/n2vcd.sock`).

## Expected

- `n2vcd_cli info` prints the protocol/version and a `N2VCD ...` line; exit 0.
- `n2vcd_cli enc` prints one line per frame (`N2VCD frame ... size=...` once
  hw-p03 confirms the text), exits 0, and writes a non-empty Annex B file.
- `vcodecd` log has the `N2VCD ...` session lines and no guest stop
  (`UNDEF`, missing import, fault) or hardware timeout.
- Kernel log: no `NORD2 VCODEC:` error line; the first VENC register access
  happens through `/dev/redwood-vcodec` (K02 has no success print, so the
  absence of an error is the kernel-side evidence).

## Pass criteria

- The daemon and client exit 0.
- `out.h264` starts with an Annex B start code (`00 00 00 01` or `00 00 01`).
- PC check (fetch `out.h264` and `src640.nv12`):

  ```sh
  python3 gpu/hwcodec/tests/check/h264_check.py out.h264 640 480 30 \
          --source src640.nv12
  ```

  must print `verdict: PASS`: SPS profile/level printed, ffprobe geometry
  640x480, 30 frames, first frame key, per-frame PSNR >= 30 dB.
- Record the per-frame sizes and the hardware time (the client prints them).

## If it fails

| Symptom | Meaning |
|---|---|
| `n2vcd_cli` exits with `E_BUSY` | another session is open; the daemon allows one encoder session in v1 |
| `E_HW` / `E_GUEST` | the daemon's watchdog tore the session down; its stderr names the guest stop or the 2 s timeout |
| `E_UNSUPPORTED` | this is the decoder path, not implemented yet |
| no `out.h264` | the client failed before the first key frame; check the daemon log |
| PC checker fails geometry | the client/daemon wrote a stream for a different size; check the `enc` arguments |

Do not run this stage while the JPEG device test or `hwc.py` command 7 is
active: both use the same VEN domain and the same register windows.
