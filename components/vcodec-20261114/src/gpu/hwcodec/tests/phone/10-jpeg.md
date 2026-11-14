# 10 — hardware JPEG through `/dev/redwood-jpeg` (J02 tool)

**Goal:** the block produces complete JPEG files from an NV12 picture, every
reachable quality code works, and the picture decodes on the PC with PSNR
>= 30 dB against the same generated source.

## Steps (on the phone, as root)

```sh
OUT=/var/tmp/n2hw
mkdir -p $OUT
/usr/local/bin/rwjpeg_tool info
/usr/local/bin/rwjpeg_tool selftest
/usr/local/bin/rwjpeg_tool bench 1920x1080 30
/usr/local/bin/rwjpeg_tool pattern 640x480 $OUT/src640.nv12
/usr/local/bin/rwjpeg_tool enc $OUT/src640.nv12 640x480 90 $OUT/hw640.jpg
```

The guest runner (`STAGE=jpeg n2hw_phone_tests`) does the same and records
`PASS`/`FAIL` per command in `$OUT/results.txt`.

## Expected stdout (`N2JPG ...`)

```text
N2JPG info abi=1 max_pixels=12582912 jobs=0 caps=0x1
N2JPG selftest q=97 code=0x10 bytes=... hw_us=... PASS
N2JPG selftest q=95 code=0x3  bytes=... hw_us=... PASS
... one line per quality code 0x10,3,7,2,6,0xf,1,0xa,0xe,0,4 ...
N2JPG selftest q=97 (...) > q=30 (...) PASS
N2JPG selftest 640x480 PASS 12/12
N2JPG bench 1920x1080 n=30 q=90 code=0x7 bytes=... wall_ms_frame=... hw_ms_frame=... wall_ms_per_mp=... hw_ms_per_mp=... wall_fps=...
N2JPG enc 640x480 ... bytes=... hw_us=... wall_us=... -> /var/tmp/n2hw/hw640.jpg
```

`rwjpeg_tool` exits 0 on pass, 1 on a failed check, 2 when the device is
absent (`no device (fallback)`); the guest runner maps exit 2 to `SKIP`.

## Expected kernel log

For the 640x480 encode:

```text
NORD2 JPEG: 640x480 fmt 2 q 90 -> N bytes in M us
```

The exact `N`/`M` are the evidence, not a pass threshold. K01's idle
power-off is silent on success; if the idle path logs, it is a `NORD2 JPG:`
line. No `NORD2 JPEG:` timeout/stall/`no FF D9`/`no thread call` line.

## Pass criteria

- `rwjpeg_tool selftest` last line exactly `N2JPG selftest 640x480 PASS 12/12`
  (11 quality codes + the q97>q30 ladder).
- `rwjpeg_tool bench 1920x1080 30` exits 0; record `hw_ms_per_mp`. M1 measured
  4.6 ms/frame at 1920x1080 Q85 (~2.2 ms/MP); a much larger number is a
  finding, not a fail by itself.
- `rwjpeg_tool enc ...` exits 0 and `hw640.jpg` starts `FF D8`, ends `FF D9`.
- PC check (fetch `hw640.jpg` and `src640.nv12`):

  ```sh
  python3 gpu/hwcodec/tests/check/jpeg_check.py hw640.jpg --width 640 --height 480 \
          --source src640.nv12 --stride 640 --rows 480
  ```

  must print `verdict: PASS` with Y/U/V PSNR >= 30 dB and a `djpeg:` line.
  (`src640.nv12` is the J02 pattern, 16-padded; for 1920x1080 use
  `--stride 1920 --rows 1088`. On the PC it can be regenerated with
  `python3 gpu/hwcodec/tests/check/gen_yuv.py src640.nv12 640x480 --pattern static --align16`.)

## Optional block-limit test (K01's open question)

```sh
/usr/local/bin/rwjpeg_tool selftest 4032x3024
```

Expect the same `PASS 12/12` lines with larger `bytes`/`hw_us`. If it stalls
or times out, capture `out_status` (`RWJ_E_*`) and the `NORD2 JPEG:` line;
that number decides whether strips are needed.

## Do not

- Do not run `hwc.py` autotest command 7 at the same time: both share the VEN
  domain model.
- Do not run the selftest in a loop for minutes; the 3 s idle power-off is
  part of what is being tested.
