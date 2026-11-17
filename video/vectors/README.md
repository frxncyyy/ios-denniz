# WP09: deterministic test vectors

Everything here is generated from pure Python 3 (no numpy, no Pillow) and the
installed black-box tools.  No downloaded media, no Apple or GPL source.

## Use

```
python3 gpu/video/vectors/gen_yuv.py --pattern squares --size 640x480 \
        --frames 10 --format i420 --out /tmp/squares.yuv

bash gpu/video/vectors/make_vectors.sh quick     # 11.3 MiB of raw + 6 streams
bash gpu/video/vectors/make_vectors.sh full      # quick + 1280x720/1920x1080/1918x1078
bash gpu/video/vectors/make_vectors.sh clean     # delete vectors/data
bash gpu/video/vectors/check_vectors.sh          # N2TEST WP09 PASS n/n
```

`vectors/data/` is generated, git-ignored and deletable.  The committed
`manifest.json` and `embedded_vectors.h` are reproducible byte for byte from
`make_vectors.sh quick`; `check_vectors.sh` proves it (two regenerations, every
sha256 and decoded md5, the 40 MB directory cap).

## Patterns

`gen_yuv.py` documents the exact integer definitions of `gradient`, `squares`,
`pan`, `static`, `noise` and `text`.  The work package defines luma only for
`pan`/`static`; their chroma is neutral 128 (noted in the manifest too).

## manifest.json

`version` 1.  Every `path` is relative to `vectors/data`:

* `raw[]`: name, pattern, width, height, frames, format, path, sha256.
* `streams[]`: name, codec, profile, width, height, frames, path, sha256,
  made_with {commands, tools}, frame_md5[], luma_md5_first_frame,
  frame_md5_normative.  `frame_md5` is ffmpeg's `-f framemd5 -pix_fmt yuv420p`
  output; H.264/HEVC/AV1 are normative, JPEG/HEIC are recorded but decoder
  dependent.  `luma_md5_first_frame` is md5 of frame 0's luma only, width bytes
  per row: exactly what the probe computes.
* `stream_sources[]`: the 8-frame squares sequence the streams came from.

Quick set: 176x144 x10 for all six patterns, 178x146 x10 squares, 640x480 x10
squares and pan.  Full adds 1280x720 x10 squares, 1920x1080 x4 pan and
1918x1078 x4 text (about 50 MiB in total, so clean before the 40 MB check).

## embedded_vectors.h (for the probe, WP07)

Include it (it has a guard and everything is `static const`):

* `n2vec_streams[]` (`struct n2vec_stream`): H.264 and HEVC, width/height,
  `nal_length_size` 4, parameter sets separately in `ps[]` (H.264: SPS, PPS;
  HEVC: VPS, SPS, PPS) and AVCC access units in `frames[]`.
  `n2vec_stream_count()` = 2.
* `n2vec_images[]` (`struct n2vec_image`): `public.jpeg` and `public.heic`
  files with their UTIs and sizes.  `n2vec_image_count()` = 2.
* `n2vec_count()` = 4 (2 streams + 2 images), `n2vec_name(i)` for a short name.
* Per-stream explicit arrays and constants remain available:
  `n2vec_h264_sps/pps/frames`, `n2vec_hevc_vps/sps/pps/frames`,
  `n2vec_h264_frame_count`, `n2vec_hevc_frame_count`,
  `N2VEC_H264_WIDTH/HEIGHT`, `N2VEC_HEVC_WIDTH/HEIGHT`,
  `n2vec_h264_luma_md5_first_frame`, ...; `n2vec_jpeg[]`, `n2vec_heic[]`.
* Data budget: 12 618 bytes (limit 48 KiB).  `make_embedded.py --verify`
  checks the AVCC round trip and regenerates the header.
