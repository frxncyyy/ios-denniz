#!/bin/bash
# SPDX-License-Identifier: MIT
# gen_dec_inputs.sh OUTDIR -- the hardware-DECODE test inputs of the JPEG phone test (opus-jpeg).  Deterministic: ffmpeg
# testsrc2 frames (synthetic, no camera/vendor content) encoded by libjpeg-turbo cjpeg, baseline, Huffman-optimised off.
#   dec640.jpg    640x480   4:2:0 q90                  (small, first hardware decode)
#   dec4032.jpg   4032x3024 4:2:0 q92                  (12 MP camera-like geometry)
#   dec1920r.jpg  1920x1080 4:2:2 q85 restart 1 row     (4:2:2 output + DRI/RSTn)
#   dec640n.jpg   640x480   4:2:0 q100 + noise         (jp8 hypothesis 2: small picture, bitstream > 64 KiB, ~421 KB)
#   dec1920f.jpg  1920x1088 4:2:0 q80 gradient, no DRI (jp8 hypothesis 2: large picture, bitstream < 64 KiB, whole MCU rows)
set -euo pipefail
OUT=${1:?usage: gen_dec_inputs.sh OUTDIR}; mkdir -p "$OUT"
T=$(mktemp -d /tmp/n2jpgp-gen.XXXXXX); trap 'rm -rf "$T"' EXIT
mk() {  # NAME WxH CJPEG-ARGS...   (SRC = lavfi source graph, default testsrc2)
	local n=$1 s=$2; shift 2
	ffmpeg -v error -y -f lavfi -i "${SRC:-testsrc2=size=$s:rate=1}" -frames:v 1 -pix_fmt rgb24 "$T/$n.ppm"
	cjpeg "$@" -outfile "$OUT/$n.jpg" "$T/$n.ppm"
	djpeg -outfile /dev/null "$OUT/$n.jpg"
	printf '%-14s %9d bytes sha256 %s\n' "$n.jpg" "$(stat -c %s "$OUT/$n.jpg")" "$(sha256sum "$OUT/$n.jpg" | cut -c1-16)"
}
mk dec640 640x480 -quality 90 -sample 2x2 -dct int
mk dec4032 4032x3024 -quality 92 -sample 2x2 -dct int
mk dec1920r 1920x1080 -quality 85 -sample 2x1 -restart 1 -dct int
SRC="testsrc2=size=640x480:rate=1,noise=alls=60:allf=u" mk dec640n 640x480 -quality 100 -sample 2x2 -dct int
SRC="color=c=gray:s=1920x1088:rate=1,format=rgb24,geq=r='X*255/W':g='Y*255/H':b='128'" mk dec1920f 1920x1088 -quality 80 -sample 2x2 -dct int
