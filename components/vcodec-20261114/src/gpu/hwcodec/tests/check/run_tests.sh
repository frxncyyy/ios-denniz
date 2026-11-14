#!/bin/bash
set -euo pipefail
# T02-check: host test of the H.264/HEVC/JPEG validators and gen_yuv.py.
# All products under /tmp/n2hw-T02/check.  The streams are made by the
# installed ffmpeg/x264/x265; the "bad" streams are truncated, wrong-size and
# all-black, and every one of them must make the checker exit 1.
ID=T02-check
export PYTHONDONTWRITEBYTECODE=1
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SB="/tmp/n2hw-T02/check"
W=320
H=240
N=12
pass=0
total=0
fail() { echo "N2TEST $ID FAIL $*"; exit 1; }
ok() { pass=$((pass + 1)); total=$((total + 1)); echo "ok $pass: $*"; }

rm -rf "$SB"
mkdir -p "$SB"

command -v ffmpeg >/dev/null || fail "ffmpeg missing"
command -v ffprobe >/dev/null || fail "ffprobe missing"
command -v djpeg >/dev/null || fail "djpeg missing"
ffmpeg -hide_banner -encoders 2>/dev/null | grep libx264 >/dev/null || fail "libx264 missing"
ffmpeg -hide_banner -encoders 2>/dev/null | grep libx265 >/dev/null || fail "libx265 missing"

# ---- deterministic generator ------------------------------------------
python3 "$HERE/gen_yuv.py" "$SB/src.nv12" ${W}x${H} --frames $N >"$SB/gen.log" || fail "gen_yuv moving"
python3 "$HERE/gen_yuv.py" "$SB/src2.nv12" ${W}x${H} --frames $N >/dev/null || fail "gen_yuv repeat"
cmp "$SB/src.nv12" "$SB/src2.nv12" || fail "gen_yuv not deterministic"
ok "gen_yuv deterministic ($N frames ${W}x${H})"
[ "$(stat -c%s "$SB/src.nv12")" -eq $((W * H * 3 / 2 * N)) ] || fail "gen_yuv size"
ok "gen_yuv size = frames*W*H*3/2"
python3 - "$SB/src.nv12" $W $H <<'EOF' || fail "gen_yuv frames identical"
import sys
d = open(sys.argv[1], 'rb').read()
w, h = int(sys.argv[2]), int(sys.argv[3])
fs = w * h * 3 // 2
assert d[:fs] != d[fs:2 * fs], "frame 0 == frame 1 (no motion)"
EOF
ok "gen_yuv moving frames differ"

# frame 0 of static == hwc.py pattern for a 16-aligned size
python3 - "$ROOT" "$SB" $W $H <<'EOF' || fail "static pattern != hwc.py"
import sys
sys.path.insert(0, sys.argv[1] + '/tools')
import hwc
_, _, data = hwc.pattern(int(sys.argv[3]), int(sys.argv[4]))
open(sys.argv[2] + '/hwc.nv12', 'wb').write(data)
EOF
python3 "$HERE/gen_yuv.py" "$SB/static.nv12" ${W}x${H} --pattern static --align16 >/dev/null
cmp "$SB/hwc.nv12" "$SB/static.nv12" || fail "static pattern differs from hwc.py"
ok "static gen_yuv == hwc.py n2j_pattern"

python3 "$HERE/gen_yuv.py" "$SB/black.nv12" ${W}x${H} --frames $N --pattern black >/dev/null

# ---- H.264 ------------------------------------------------------------
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -color_range pc -s ${W}x${H} -i "$SB/src.nv12" \
	-c:v libx264 -preset veryfast -crf 18 -g $N -f h264 "$SB/good.h264" || fail "x264 encode"
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -color_range pc -s ${W}x${H} -i "$SB/black.nv12" \
	-c:v libx264 -preset veryfast -crf 18 -g $N -f h264 "$SB/black.h264" || fail "x264 black encode"
sz=$(stat -c%s "$SB/good.h264")
head -c $((sz / 2)) "$SB/good.h264" >"$SB/trunc.h264"

run_check() { # expect_rc name command...
	local want="$1" name="$2"; shift 2
	set +e
	"$@" >"$SB/$name.log" 2>&1
	local rc=$?
	set -e
	if [ "$rc" -ne "$want" ]; then
		cat "$SB/$name.log"
		fail "$name: exit $rc, wanted $want"
	fi
	ok "$name (exit $rc)"
}

run_check 0 h264-good python3 "$HERE/h264_check.py" "$SB/good.h264" $W $H $N --source "$SB/src.nv12" --json "$SB/good.json"
grep -q 'verdict: PASS' "$SB/h264-good.log" || fail "h264-good verdict"
grep -q 'profile=' "$SB/h264-good.log" || fail "h264-good no SPS profile"
grep -q 'key_frames: 0' "$SB/h264-good.log" || fail "h264-good no key frame 0"
grep -q 'frame 0 key=1' "$SB/h264-good.log" || fail "h264-good frame 0 not key"
run_check 1 h264-truncated python3 "$HERE/h264_check.py" "$SB/trunc.h264" $W $H $N --source "$SB/src.nv12" --quiet
run_check 1 h264-wrong-size python3 "$HERE/h264_check.py" "$SB/good.h264" 640 480 $N --source "$SB/src.nv12" --quiet
run_check 1 h264-wrong-frames python3 "$HERE/h264_check.py" "$SB/good.h264" $W $H $((N + 1)) --source "$SB/src.nv12" --quiet
run_check 1 h264-all-black python3 "$HERE/h264_check.py" "$SB/black.h264" $W $H $N --source "$SB/src.nv12" --quiet

# ---- HEVC -------------------------------------------------------------
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -color_range pc -s ${W}x${H} -i "$SB/src.nv12" \
	-c:v libx265 -preset veryfast -crf 20 -g $N -x265-params log-level=error -f hevc "$SB/good.h265" || fail "x265 encode"
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -color_range pc -s ${W}x${H} -i "$SB/black.nv12" \
	-c:v libx265 -preset veryfast -crf 20 -g $N -x265-params log-level=error -f hevc "$SB/black.h265" || fail "x265 black encode"
sz=$(stat -c%s "$SB/good.h265")
head -c $((sz / 2)) "$SB/good.h265" >"$SB/trunc.h265"

run_check 0 hevc-good python3 "$HERE/hevc_check.py" "$SB/good.h265" $W $H $N --source "$SB/src.nv12" --json "$SB/good265.json"
grep -q 'verdict: PASS' "$SB/hevc-good.log" || fail "hevc-good verdict"
grep -q 'profile=' "$SB/hevc-good.log" || fail "hevc-good no SPS profile"
grep -q 'key_frames: 0' "$SB/hevc-good.log" || fail "hevc-good no key frame 0"
run_check 1 hevc-truncated python3 "$HERE/hevc_check.py" "$SB/trunc.h265" $W $H $N --source "$SB/src.nv12" --quiet
run_check 1 hevc-wrong-size python3 "$HERE/hevc_check.py" "$SB/good.h265" 640 480 $N --source "$SB/src.nv12" --quiet
run_check 1 hevc-all-black python3 "$HERE/hevc_check.py" "$SB/black.h265" $W $H $N --source "$SB/src.nv12" --quiet

# ---- planar I420 source option ----------------------------------------
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -color_range pc -s ${W}x${H} -i "$SB/src.nv12" \
	-pix_fmt yuv420p -f rawvideo "$SB/src.i420" || fail "i420 conversion"
ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p -color_range pc -s ${W}x${H} -i "$SB/src.i420" \
	-c:v libx264 -preset veryfast -crf 18 -g $N -f h264 "$SB/good-i420.h264" || fail "i420 x264 encode"
run_check 0 h264-i420-source python3 "$HERE/h264_check.py" "$SB/good-i420.h264" $W $H $N \
	--source "$SB/src.i420" --source-format i420 --quiet

# ---- JPEG -------------------------------------------------------------
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -color_range pc -s ${W}x${H} -i "$SB/src.nv12" \
	-frames:v 1 -q:v 2 "$SB/good.jpg" || fail "mjpeg encode"
cp "$SB/good.jpg" "$SB/padded.jpg"
head -c 4096 /dev/zero >>"$SB/padded.jpg"
sz=$(stat -c%s "$SB/good.jpg")
head -c $((sz / 2)) "$SB/good.jpg" >"$SB/trunc.jpg"

run_check 0 jpeg-good python3 "$HERE/jpeg_check.py" "$SB/good.jpg" --width $W --height $H \
	--source "$SB/src.nv12" --json "$SB/good-jpeg.json"
grep -q 'verdict: PASS' "$SB/jpeg-good.log" || fail "jpeg-good verdict"
grep -q 'sampling=420' "$SB/jpeg-good.log" || fail "jpeg-good sampling"
grep -q 'djpeg:' "$SB/jpeg-good.log" || fail "jpeg-good no djpeg line"
run_check 0 jpeg-padded python3 "$HERE/jpeg_check.py" "$SB/padded.jpg" --width $W --height $H \
	--source "$SB/src.nv12" --quiet
run_check 1 jpeg-truncated python3 "$HERE/jpeg_check.py" "$SB/trunc.jpg" --width $W --height $H \
	--source "$SB/src.nv12" --quiet
run_check 1 jpeg-wrong-size python3 "$HERE/jpeg_check.py" "$SB/good.jpg" --width 640 --height 480 \
	--source "$SB/src.nv12" --quiet
run_check 1 jpeg-all-black python3 "$HERE/jpeg_check.py" "$SB/good.jpg" --width $W --height $H \
	--source "$SB/black.nv12" --quiet
ffmpeg -v error -y -f rawvideo -pix_fmt nv12 -color_range pc -s ${W}x${H} -i "$SB/src.nv12" \
	-frames:v 1 -pix_fmt yuvj444p -q:v 2 "$SB/good444.jpg" || fail "mjpeg 444 encode"
run_check 1 jpeg-444-default python3 "$HERE/jpeg_check.py" "$SB/good444.jpg" --width $W --height $H \
	--source "$SB/src.nv12" --quiet
run_check 0 jpeg-444-any python3 "$HERE/jpeg_check.py" "$SB/good444.jpg" --width $W --height $H \
	--source "$SB/src.nv12" --sampling any --quiet

# ---- a real cjpeg file too (independent encoder) ----------------------
python3 - "$SB/src.nv12" "$SB/first.ppm" $W $H <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
w, h = int(sys.argv[3]), int(sys.argv[4])
y = d[:w * h]
c = d[w * h:w * h + w * h // 2]
rgb = bytearray()
for j in range(h):
    for i in range(w):
        yy = y[j * w + i]
        u = c[(j // 2) * w + (i // 2) * 2] - 128
        v = c[(j // 2) * w + (i // 2) * 2 + 1] - 128
        r = yy + 1.402 * v
        g = yy - 0.344136 * u - 0.714136 * v
        b = yy + 1.772 * u
        rgb += bytes((max(0, min(255, int(r + 0.5))), max(0, min(255, int(g + 0.5))), max(0, min(255, int(b + 0.5)))))
open(sys.argv[2], 'wb').write(b'P6\n%d %d\n255\n' % (w, h) + bytes(rgb))
EOF
cjpeg -quality 90 -outfile "$SB/cjpeg.jpg" "$SB/first.ppm" || fail "cjpeg"
run_check 0 jpeg-cjpeg python3 "$HERE/jpeg_check.py" "$SB/cjpeg.jpg" --width $W --height $H \
	--source "$SB/src.nv12" --quiet

echo "N2TEST $ID PASS $pass/$pass"
