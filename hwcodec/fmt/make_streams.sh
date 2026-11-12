#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# opus-fmt: small test clips, one per decoder format (10 frames, testsrc).  Usage: make_streams.sh [OUTDIR]  (default fmt/streams)
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
O=${1:-$HERE/streams}
mkdir -p "$O"
F=(ffmpeg -nostdin -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 -frames:v 10 -pix_fmt yuv420p)
[ -s "$O/f320-vp8.ivf" ]    || "${F[@]}" -c:v libvpx -b:v 400k -g 10 -auto-alt-ref 0 "$O/f320-vp8.ivf"
[ -s "$O/f320-vp9.ivf" ]    || "${F[@]}" -c:v libvpx-vp9 -b:v 400k -g 10 -row-mt 0 -frame-parallel 0 -auto-alt-ref 0 "$O/f320-vp9.ivf"
[ -s "$O/f320-vp9p2.ivf" ]  || ffmpeg -nostdin -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 -frames:v 10 \
	-pix_fmt yuv420p10le -c:v libvpx-vp9 -profile:v 2 -b:v 400k -g 10 -auto-alt-ref 0 "$O/f320-vp9p2.ivf"
[ -s "$O/f320-av1.ivf" ]    || "${F[@]}" -c:v libaom-av1 -cpu-used 8 -b:v 400k -g 10 -lag-in-frames 0 -strict experimental "$O/f320-av1.ivf"
[ -s "$O/f320-mpeg2.m2v" ]  || "${F[@]}" -c:v mpeg2video -b:v 1M -g 10 -bf 2 -f mpeg2video "$O/f320-mpeg2.m2v"
[ -s "$O/f320-mpeg4.m4v" ]  || "${F[@]}" -c:v mpeg4 -b:v 600k -g 10 -bf 0 -f m4v "$O/f320-mpeg4.m4v"
[ -s "$O/f352-h263.h263" ]  || ffmpeg -nostdin -loglevel error -y -f lavfi -i testsrc=size=352x288:rate=30 -frames:v 10 \
	-pix_fmt yuv420p -c:v h263 -b:v 600k -g 10 -f h263 "$O/f352-h263.h263"
[ -s "$O/f320-hevc10.h265" ] || ffmpeg -nostdin -loglevel error -y -f lavfi -i testsrc=size=320x240:rate=30 -frames:v 10 \
	-pix_fmt yuv420p10le -c:v libx265 -profile:v main10 -x265-params log-level=error:bframes=0 "$O/f320-hevc10.h265"
# Reference: decint/compare_dec.py decodes each clip with ffmpeg itself (per-picture FNV-1a of Y and UV, as n2vcd_dprobe logs).
# VP8/VP9/AV1 must be bit-exact; MPEG-2/MPEG-4/H.263 use an IDCT with an accuracy tolerance (IEEE 1180), so the hardware
# output may differ from ffmpeg by +-1 and drift within a GOP: judge those by PSNR (compare_dec.py with the YUV file).
ls -la "$O"
