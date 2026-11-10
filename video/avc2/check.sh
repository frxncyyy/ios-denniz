#!/bin/bash
# SPDX-License-Identifier: MIT
# LEAD. Bit-exactness of avc2 against ffmpeg's decoder: usage check.sh [quick|full]. Last line: N2TEST AVC2 PASS n/n.
set -uo pipefail
V=$(cd "$(dirname "$0")/.." && pwd); T=$V/build/lead/avc2/run.$$; mkdir -p $T; cd $T; trap 'rm -rf "$T"' EXIT     # a directory of its own: several agents run this at once
bash $V/avc2/build_cli.sh $T/n2avc_cli ${AVC2_CFLAGS:--fsanitize=address,undefined -fno-sanitize-recover=undefined} || { echo "N2TEST AVC2 FAIL build"; exit 1; }
MODE=${1:-quick}; pass=0; total=0; fails=""
SIZES="176x144 178x146 16x16"; [ $MODE = full ] && SIZES="$SIZES 640x480 1280x720"
for size in $SIZES; do
 for pat in gradient noise squares pan static; do
  n=6; [ $size = 1280x720 ] && n=3
  python3 $V/vectors/gen_yuv.py --pattern $pat --size $size --frames $n --format i420 --out in.yuv >/dev/null
  for qp in 10 20 26 32 40 51; do for mode in "--intra-only" "" "--deblock" "--deblock --intra-only"; do for th in 1 4; do
   total=$((total+1))
   timeout 120 ./n2avc_cli --size $size --frames $n --qp $qp $mode --threads $th --annexb --in in.yuv --out o.264 --recon r.yuv >c.log 2>&1; rc=$?
   ffmpeg -v error -y -i o.264 -f rawvideo -pix_fmt yuv420p d.yuv 2>e.txt
   if [ $rc = 0 ] && [ ! -s e.txt ] && cmp -s d.yuv r.yuv && [ -s d.yuv ]; then pass=$((pass+1)); else fails="$fails | $size $pat qp$qp ${mode:-P} t$th rc=$rc $(head -c 80 e.txt | tr '\n' ' ')"; fi
  done; done; done
 done
done
rm -f in.yuv d.yuv r.yuv o.264
[ -n "$fails" ] && echo "$fails" | tr '|' '\n' | head -30
[ $pass = $total ] && echo "N2TEST AVC2 PASS $pass/$total" || { echo "N2TEST AVC2 FAIL $pass/$total"; exit 1; }
