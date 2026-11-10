#!/bin/bash
# SPDX-License-Identifier: MIT
# LEAD. Reads, from the root file system MIRROR (as the last transaction's snapshot left it; nothing touches the phone), the
# camera roll and the probe's files, and judges them on the build machine.
#   usage: fetch_media.sh NAME        -> gpu/video/build/phone/NAME/{DCIM,probe}/..., report on stdout
# Files over 150 MB are listed, not copied. Old copies under build/phone are yours to delete.
set -uo pipefail
V=$(cd "$(dirname "$0")/.." && pwd); B=$(cd "$V/../build" && pwd); H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
O=$V/build/phone/$1; mkdir -p "$O/DCIM" "$O/probe"
for d in $($H ls /private/var/mobile/Media/DCIM 2>/dev/null | awk '$NF ~ /\/$/ {gsub("/","",$NF); print $NF}'); do
  echo "== DCIM/$d"; $H ls /private/var/mobile/Media/DCIM/$d 2>/dev/null | grep -v '^journal'
  names=$($H ls /private/var/mobile/Media/DCIM/$d 2>/dev/null | awk '$1+0 > 0 && $1+0 < 150000000 {print $NF}')
  [ -n "$names" ] && { mkdir -p "$O/DCIM/$d"; $H get /private/var/mobile/Media/DCIM/$d "$O/DCIM/$d" $names >/dev/null 2>&1; }
done
$H get /private/var/db/nord2-video "$O/probe" >/dev/null 2>&1
echo "== judged on the build machine"
find "$O" -type f \( -iname '*.mov' -o -iname '*.mp4' \) -size +0 | sort | while read -r f; do
  info=$(ffprobe -v error -show_entries stream=codec_name,profile,width,height,nb_frames,avg_frame_rate,bit_rate,pix_fmt,color_range,color_space -of csv=p=0 "$f" 2>&1 | tr '\n' ' ')
  err=$(ffmpeg -nostdin -v error -i "$f" -f null - 2>&1 | head -3 | tr '\n' ' ')
  ffmpeg -nostdin -v error -y -i "$f" -frames:v 1 "$f.first.png" 2>/dev/null
  echo "MOVIE $(stat -c%s "$f") ${f#$O/} | $info | decode: ${err:-OK}"
done
find "$O" -type f \( -iname '*.heic' -o -iname '*.jpg' -o -iname '*.jpeg' \) -size +0 | sort | while read -r f; do
  if heif-convert "$f" "$f.png" >/dev/null 2>&1 || ffmpeg -nostdin -v error -y -i "$f" "$f.png" 2>/dev/null; then
    st=$(python3 - "$f.png" <<'PY'
import sys
from PIL import Image, ImageStat
im = Image.open(sys.argv[1]).convert('RGB'); s = ImageStat.Stat(im)
print('%dx%d mean RGB %.0f,%.0f,%.0f stddev %.0f,%.0f,%.0f' % (im.size[0], im.size[1], *s.mean, *s.stddev))
PY
)
    echo "PHOTO $(stat -c%s "$f") ${f#$O/} | host decode OK | $st"
  else echo "PHOTO $(stat -c%s "$f") ${f#$O/} | host decode FAILED"; fi
done
du -sh "$O" | awk '{print "kept: "$1" in '"$O"'"}'
