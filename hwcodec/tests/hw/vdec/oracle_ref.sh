#!/bin/bash
# SPDX-License-Identifier: MIT
# oracle_ref.sh -- DEC03's optional integration oracle: a header-only START through the vendor stack
# on T02's fake, using opus-dec1's reference AP driver (gpu/hwcodec/dec1/vdec_ref.c, not my file).
#
# It checks the values the DEC03 task calls the oracle:
#   vsi.pic.pic_w/pic_h, vsi.pic.buf_w/buf_h, vsi.crop[2..3] and vsi.dec.dpb_sz after the first START
# (index 0xFF, no frame buffer) for 320x240, 1920x1080 (coded 1088, cropped) and 1920x808 (coded 816).
# ffprobe supplies the displayed size.  vsi.pic.pic_w/pic_h are the CODED size, vsi.crop is the display
# rectangle: that is the corrected oracle (see STATUS.md and the board post); the literal task text
# ("pic_w/pic_h equal ffprobe's values ... a cropped stream") does not hold for the vendor.
#
# Host only; builds under /tmp/n2hw-DEC03/oracle.  Requires gpu/hwcodec/dec1/run_ref.sh to have built
# its library once (it is run automatically when /tmp/n2hw-opus-dec1/ref/o2/libn2vhost.a is absent).
set -euo pipefail
ID=DEC03-oracle
HERE="$(cd "$(dirname "$0")" && pwd)"
HWD="$(cd "$HERE/../../.." && pwd)"
SB=/tmp/n2hw-DEC03-oracle
REF=/tmp/n2hw-opus-dec1/ref
VFS=${N2HW_VENDOR_DIR:-$HWD/../build/cam-vendor-20261109/vfs}
fail() { echo "N2TEST $ID FAIL $*"; exit 1; }
mkdir -p "$SB"

if [ ! -f "$REF/o2/libn2vhost.a" ]; then
	echo "== building the dec1 reference runner once"
	bash "$HWD/dec1/run_ref.sh" >"$SB/run_ref.log" 2>&1 || fail "dec1/run_ref.sh (see $SB/run_ref.log)"
fi
[ -f "$REF/o2/libn2vhost.a" ] || fail "no $REF/o2/libn2vhost.a"
[ -d "$REF/tree" ] || fail "no $REF/tree"

# The reference print does not include vsi.crop; add it to a /tmp copy (the tree file is not edited).
python3 - "$HWD/dec1/vdec_ref.c" "$SB/vdec_ref_crop.c" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
old = 'pic=%ux%u buf=%ux%u fb_sz=%u/%u dpb=%u fourcc=%.4s cpu=%.3fs\\n'
new = 'pic=%ux%u buf=%ux%u fb_sz=%u/%u dpb=%u crop=%d,%d,%d,%d fourcc=%.4s cpu=%.3fs\\n'
if old not in s:
    raise SystemExit('reference print changed; oracle needs an update')
s = s.replace(old, new)
old2 = 'V->pic.pic_w, V->pic.pic_h, V->pic.buf_w, V->pic.buf_h, V->pic.fb_sz[0], V->pic.fb_sz[1], V->dec.dpb_sz,'
new2 = ('V->pic.pic_w, V->pic.pic_h, V->pic.buf_w, V->pic.buf_h, V->pic.fb_sz[0], V->pic.fb_sz[1], '
        'V->dec.dpb_sz, V->crop[0], V->crop[1], V->crop[2], V->crop[3],')
if old2 not in s:
    raise SystemExit('reference print arguments changed; oracle needs an update')
s = s.replace(old2, new2)
open(dst, 'w').write(s)
PY

T="$REF/tree"
nice -n 10 clang -std=c11 -O1 -g -Wall -Wextra -Werror -D_POSIX_C_SOURCE=200809L \
	-I"$T" -I"$T/include" -I"$T/tests/hw" -I"$T/tests/emu" -I"$T/vcu" -I"$T/ap" \
	-I"$T/host" -I"$T/shim" "$SB/vdec_ref_crop.c" -o "$SB/vdec_ref_crop" \
	-Wl,--whole-archive "$REF/o2/libn2vhost.a" -Wl,--no-whole-archive -lpthread -lm \
	|| fail "reference driver build"

ENC=(-c:v libx264 -preset ultrafast -profile:v main
     -x264-params keyint=25:min-keyint=25:bframes=0:ref=1:scenecut=0 -pix_fmt yuv420p -f h264)
mkdir -p "$SB/vec"
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=320x240:rate=25 -frames:v 2 "${ENC[@]}" "$SB/vec/v320.h264" -y || fail "ffmpeg 320x240"
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x1080:rate=25 -frames:v 1 "${ENC[@]}" "$SB/vec/v1080.h264" -y || fail "ffmpeg 1920x1080"
ffmpeg -hide_banner -loglevel error -f lavfi -i testsrc2=size=1920x808:rate=25 -frames:v 1 "${ENC[@]}" "$SB/vec/vcrop.h264" -y || fail "ffmpeg 1920x808"

pass=0
total=0
for v in v320 v1080 vcrop; do
	path="$SB/vec/$v.h264"
	wh=$(ffprobe -v error -select_streams v:0 -show_entries stream=width,height -of csv=p=0 "$path")
	want_w=${wh%,*}
	want_h=${wh#*,}
	line=$(VR_FAKEIRQ=1 "$SB/vdec_ref_crop" "$VFS" "$path" 0 2>/dev/null | grep '^VR header' | tail -n 1 || true)
	[ -n "$line" ] || fail "no header line for $v"
	pic=$(echo "$line" | sed -n 's/.*pic=\([0-9]*x[0-9]*\) .*/\1/p')
	buf=$(echo "$line" | sed -n 's/.*buf=\([0-9]*x[0-9]*\) .*/\1/p')
	crop=$(echo "$line" | sed -n 's/.*crop=\([0-9]*,[0-9]*,[0-9]*,[0-9]*\) .*/\1/p')
	dpb=$(echo "$line" | sed -n 's/.*dpb=\([0-9]*\) .*/\1/p')
	changed=$(echo "$line" | sed -n 's/.*changed=\(0x[0-9a-f]*\) .*/\1/p')
	cw=$(echo "$crop" | cut -d, -f3)
	ch=$(echo "$crop" | cut -d, -f4)
	echo "$v: ffprobe ${want_w}x${want_h} | $line"
	total=$((total + 4))
	[ "$((changed & 1))" -eq 1 ] && pass=$((pass + 1)) || echo "  RES_CHANGE missing"
	[ "$cw" = "$want_w" ] && [ "$ch" = "$want_h" ] && pass=$((pass + 2)) || echo "  vsi.crop display ${cw}x${ch} != ffprobe ${want_w}x${want_h}"
	# coded dimensions are ffprobe's size rounded up to the macroblock/alignment rules
	case "$v" in
	v320) [ "$pic" = "320x240" ] && [ "$buf" = "320x256" ] && pass=$((pass + 1)) || echo "  pic/buf unexpected: $pic $buf" ;;
	v1080) [ "$pic" = "1920x1088" ] && [ "$buf" = "1920x1088" ] && pass=$((pass + 1)) || echo "  pic/buf unexpected: $pic $buf" ;;
	vcrop) [ "$pic" = "1920x816" ] && [ "$buf" = "1920x832" ] && pass=$((pass + 1)) || echo "  pic/buf unexpected: $pic $buf" ;;
	esac
	[ -n "$dpb" ] || echo "  dpb missing"
done
[ "$pass" -eq "$total" ] || fail "$pass of $total oracle checks"
echo "N2TEST $ID PASS $pass/$total"
