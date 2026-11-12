# T02 bit-stream / JPEG validators

Four command-line tools, no numpy, ffmpeg/ffprobe/djpeg as black-box
decoders. All exit **0 = PASS**, **1 = FAIL** (the stream/picture is bad),
**2 = usage or missing tool**. Reports can be written as JSON with `--json`.

## H.264 / HEVC

```sh
python3 h264_check.py FILE WIDTH HEIGHT FRAMES [--source src.nv12]
                      [--source-format nv12|i420] [--min-psnr 30]
                      [--min-psnr-chroma 30] [--json out.json] [--quiet]
python3 hevc_check.py ...same...
```

Checks:

- the SPS parses (H.264 profile/level, HEVC profile/tier/level; geometry,
  chroma format, bit depth printed);
- ffprobe reports the expected codec, geometry and frame count, and a 4:2:0
  pixel format (unless `--allow-any-pix-fmt`);
- ffmpeg decodes every frame (exit 0) to exactly `FRAMES*W*H*3/2` bytes;
- the first frame is a key frame and ffprobe reports at least one key frame;
  the key-frame positions are printed;
- with `--source`, every frame's Y/U/V PSNR against the raw source is at least
  the threshold. `--source` is NV12 by default (the VENC input), `i420` for a
  planar source. The source must be exactly `FRAMES*W*H*3/2` bytes.

## JPEG

```sh
python3 jpeg_check.py FILE --width W --height H [--source pic.nv12]
                      [--stride S] [--chroma-stride CS] [--rows R]
                      [--min-psnr 30] [--sampling 420|422|444|any]
                      [--no-djpeg] [--json out.json] [--quiet]
```

Checks the marker walk (SOI, APPn, DQT, SOF0/1/2, DHT, SOS, EOI; padding after
EOI is accepted and counted), SOF geometry, component sampling (4:2:0 by
default), DQT/DHT presence, ffmpeg decode to `yuvj420p` and per-plane PSNR
against the NV12 source, and an independent `djpeg -ppm` decode whose luma
PSNR must also pass. The source can be 16-padded (`--stride`, `--rows`);
only the first `height` rows are read.

## Deterministic pictures

```sh
python3 gen_yuv.py OUT WxH [--frames N] [--start K] [--align16]
                   [--pattern moving|static|black]
```

NV12 frame-major. `--pattern static` is byte-identical to the kernel's
`n2j_pattern` / `gpu/hwcodec/tools/hwc.py` for a tight picture; frame 0 of
`moving` is the same and later frames shift the gradient/checkerboard.
`--align16` rounds the stride up to 16 and replicates the row end (the J02
pattern layout). `black` is all zero.

## Test

```sh
bash gpu/hwcodec/tests/check/run_tests.sh
# N2TEST T02-check PASS 22/22
```

It makes good H.264/HEVC streams with the installed libx264/libx265 and a good
JPEG with ffmpeg and cjpeg, then checks that truncated, wrong-size and
all-black streams/pictures fail (exit 1).
