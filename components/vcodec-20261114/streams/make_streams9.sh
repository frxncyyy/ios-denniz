#!/usr/bin/env bash
# vcodec-20261114: real-size decoder test clips (testsrc2), one per format, for the on-phone dprobe run (payload-vcodec-fmt9).
set -euo pipefail
O=${1:-$(dirname "$0")}
F() { local s=$1 n=$2; shift 2; nice ffmpeg -nostdin -loglevel error -y -f lavfi -i testsrc2=size=$s:rate=30 -frames:v $n -pix_fmt yuv420p "$@"; }
[ -s $O/r1080-vp9.ivf ]     || F 1920x1080 30 -c:v libvpx-vp9 -b:v 4M -g 30 -row-mt 0 -frame-parallel 0 -auto-alt-ref 0 -threads 2 $O/r1080-vp9.ivf
[ -s $O/r1080-vp9t1.ivf ]   || F 1920x1080 30 -c:v libvpx-vp9 -b:v 4M -g 30 -row-mt 0 -frame-parallel 0 -auto-alt-ref 0 -tile-columns 0 -threads 1 $O/r1080-vp9t1.ivf
[ -s $O/r2160-vp9t1.ivf ]   || F 3840x2160 10 -c:v libvpx-vp9 -b:v 12M -g 10 -auto-alt-ref 0 -deadline realtime -cpu-used 8 -tile-columns 0 -threads 1 $O/r2160-vp9t1.ivf
[ -s $O/r1080-av1.ivf ]     || F 1920x1080 30 -c:v libaom-av1 -cpu-used 8 -b:v 3M -g 30 -lag-in-frames 0 -threads 2 -strict experimental $O/r1080-av1.ivf
[ -s $O/r720-vp8.ivf ]      || F 1280x720 30 -c:v libvpx -b:v 3M -g 30 -auto-alt-ref 0 -threads 2 $O/r720-vp8.ivf
[ -s $O/r1080-mpeg2.m2v ]   || F 1920x1080 30 -c:v mpeg2video -b:v 12M -g 15 -bf 2 -f mpeg2video $O/r1080-mpeg2.m2v
[ -s $O/r720-mpeg4.m4v ]    || F 1280x720 30 -c:v mpeg4 -b:v 4M -g 30 -bf 0 -f m4v $O/r720-mpeg4.m4v
ls -la $O
