#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Self-proof of the WP10 judges: each judge must PASS on a known-good input
# and FAIL, for the stated reason, on a broken one.
#
#   bash gpu/video/oracle/test/run_tests.sh [outdir]
#
# Default outdir: gpu/video/build/WP10.  Last line on success:
#   N2TEST WP10 PASS <n>/<n>
set -euo pipefail

HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ORACLE=$(cd "$HERE/.." && pwd)
VIDEO=$(cd "$ORACLE/.." && pwd)
BUILD=${1:-$VIDEO/build/WP10}
TMP=$BUILD/test
PY=${PYTHON:-python3}
FFMPEG=${N2_FFMPEG:-ffmpeg}

rm -rf "$TMP"
mkdir -p "$TMP"

PASS=0
FAIL=0

ok() { PASS=$((PASS + 1)); echo "TEST $1 PASS"; }
bad() { FAIL=$((FAIL + 1)); echo "TEST $1 FAIL: ${2:-}"; }

expect_pass() { # expect_pass <name> <cmd...>
    local name=$1
    shift
    if "$@" >"$TMP/last.log" 2>&1; then
        ok "$name"
    else
        bad "$name" "$(tail -n 1 "$TMP/last.log" 2>/dev/null || true)"
    fi
}

expect_fail() { # expect_fail <name> <cmd...>
    local name=$1
    shift
    if "$@" >"$TMP/last.log" 2>&1; then
        bad "$name" "command unexpectedly passed"
    else
        ok "$name"
    fi
}

expect_fail_grep() { # expect_fail_grep <name> <pattern> <cmd...>
    local name=$1 pat=$2
    shift 2
    if "$@" >"$TMP/last.log" 2>&1; then
        bad "$name" "command unexpectedly passed"
    elif grep -q -- "$pat" "$TMP/last.log"; then
        ok "$name"
    else
        bad "$name" "failed but did not mention '$pat': $(tail -n 1 "$TMP/last.log" 2>/dev/null || true)"
    fi
}

# ---------------------------------------------------------------- fixtures
echo "== fixtures in $TMP"
"$FFMPEG" -v error -y -f lavfi -i testsrc=size=176x144:rate=30 -frames:v 10 \
    -pix_fmt yuv420p "$TMP/src.yuv"
x264 --quiet --profile baseline --bframes 0 --ref 1 --no-deblock \
    --keyint 60 --min-keyint 60 --qp 18 --input-res 176x144 \
    --output "$TMP/x264.264" "$TMP/src.yuv" >"$TMP/x264.log" 2>&1
"$FFMPEG" -v error -y -i "$TMP/x264.264" -f rawvideo -pix_fmt yuv420p "$TMP/x264dec.yuv"
"$PY" - "$TMP/stats.json" <<'EOF'
import json
import sys
json.dump([{"bytes": 0, "qp": 18, "keyframe": i == 0} for i in range(10)],
          open(sys.argv[1], "w"))
EOF

# one-bit corruption of the recon at frame 2, luma (17, 9)
"$PY" - "$TMP/x264dec.yuv" "$TMP/flip.yuv" <<'EOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
fs = 176 * 144 * 3 // 2
off = 2 * fs + 9 * 176 + 17
data[off] ^= 0x01
open(sys.argv[2], "wb").write(data)
EOF

# one-bit corruption in the middle of the stream
"$PY" - "$TMP/x264.264" "$TMP/flip.264" <<'EOF'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
data[len(data) // 2] ^= 0x40
open(sys.argv[2], "wb").write(data)
EOF

head -c $(( $(stat -c %s "$TMP/x264.264") - 20 )) "$TMP/x264.264" >"$TMP/trunc.264"

# a coarse stream judged against a claimed QP of 10 (PSNR floor must reject)
x264 --quiet --profile baseline --bframes 0 --ref 1 --no-deblock \
    --keyint 60 --min-keyint 60 --qp 50 --input-res 176x144 \
    --output "$TMP/x264q50.264" "$TMP/src.yuv" >"$TMP/x264q50.log" 2>&1
"$FFMPEG" -v error -y -i "$TMP/x264q50.264" -f rawvideo -pix_fmt yuv420p "$TMP/x264q50dec.yuv"
"$PY" - "$TMP/stats_claim10.json" <<'EOF'
import json
import sys
json.dump([{"bytes": 0, "qp": 10, "keyframe": i == 0} for i in range(10)],
          open(sys.argv[1], "w"))
EOF

# hand-made NALs for the Annex-B scanner
"$PY" - "$TMP" <<'EOF'
import sys
import os
d = sys.argv[1]
open(os.path.join(d, "good_esc.264"), "wb").write(
    b"\x00\x00\x00\x01\x65\x88\x84\x00\x00\x03\x01\x67")
open(os.path.join(d, "bad_start.264"), "wb").write(
    b"\x00\x00\x00\x01\x65\x88\x84\x00\x00\x01\x00\x21")
open(os.path.join(d, "bad_000002.264"), "wb").write(
    b"\x00\x00\x00\x01\x65\x88\x00\x00\x02\x21")
EOF

"$FFMPEG" -v error -y -f lavfi -i testsrc=size=320x240:rate=30 \
    -f lavfi -i sine -t 1 -c:v libx264 -c:a aac -pix_fmt yuv420p "$TMP/av.mp4"
"$FFMPEG" -v error -y -f lavfi -i testsrc=size=320x240:rate=30 -t 1 \
    -c:v libx265 -tag:v hvc1 -pix_fmt yuv420p "$TMP/hv.mp4"
head -c $(( $(stat -c %s "$TMP/av.mp4") / 2 )) "$TMP/av.mp4" >"$TMP/av_trunc.mp4"

# ------------------------------------------------------------ yuv helpers
echo "== yuv.py"
expect_pass yuv.helpers "$PY" "$HERE/t_yuv.py"

# ------------------------------------------------------- check_h264 judge
echo "== check_h264.py"
H264=("$PY" "$ORACLE/check_h264.py")
expect_pass h264.x264-good "${H264[@]}" --stream "$TMP/x264.264" \
    --size 176x144 --frames 10 --recon "$TMP/x264dec.yuv" \
    --source "$TMP/src.yuv" --stats "$TMP/stats.json" --expect skip-headers
expect_fail_grep h264.recon-flip "frame=2 plane=0 x=17 y=9" "${H264[@]}" \
    --stream "$TMP/x264.264" --size 176x144 --frames 10 \
    --recon "$TMP/flip.yuv" --expect skip-headers
expect_fail h264.stream-flip "${H264[@]}" --stream "$TMP/flip.264" \
    --size 176x144 --frames 10 --recon "$TMP/x264dec.yuv" --expect skip-headers
expect_fail h264.truncated "${H264[@]}" --stream "$TMP/trunc.264" \
    --size 176x144 --frames 10 --recon "$TMP/x264dec.yuv" --expect skip-headers
expect_fail h264.wrong-frame-count "${H264[@]}" --stream "$TMP/x264.264" \
    --size 176x144 --frames 11 --recon "$TMP/x264dec.yuv" --expect skip-headers
expect_fail_grep h264.psnr-floor "below the 44.0 dB floor" "${H264[@]}" \
    --stream "$TMP/x264q50.264" --size 176x144 --frames 10 \
    --recon "$TMP/x264q50dec.yuv" --source "$TMP/src.yuv" \
    --stats "$TMP/stats_claim10.json" --expect skip-headers

expect_pass h264.scan-good "${H264[@]}" --scan "$TMP/x264.264"
expect_pass h264.scan-escaped "${H264[@]}" --scan "$TMP/good_esc.264"
expect_fail_grep h264.scan-inner-start "nal_unit_type 0" "${H264[@]}" \
    --scan "$TMP/bad_start.264"
expect_fail_grep h264.scan-000002 "00 00 02" "${H264[@]}" \
    --scan "$TMP/bad_000002.264"

"$PY" "$ORACLE/avc_convert.py" annexb2avcc "$TMP/x264.264" "$TMP/x.avcc" >/dev/null
"$PY" "$ORACLE/avc_convert.py" avcc2annexb "$TMP/x.avcc" "$TMP/x.264" >/dev/null
expect_pass h264.avcc-roundtrip "$PY" "$HERE/nals_equal.py" \
    "$TMP/x264.264" "$TMP/x.264"

# AVCC without parameter sets (what the VideoToolbox glue gets): the
# converter must take the SPS/PPS from an Annex-B reference and prove that
# the slice NALs agree; a wrong reference must be rejected.
"$PY" - "$TMP/x264.264" "$TMP/x264_nosei.264" "$TMP/vcl.avcc" \
    "$TMP/x264q50.264" "$TMP/q50_vcl.avcc" <<'EOF'
import sys


def split(data):
    starts = []
    i, n = 0, len(data)
    while i + 3 <= n:
        if data[i] == 0 and data[i + 1] == 0:
            if data[i + 2] == 1:
                starts.append((i, 3)); i += 3; continue
            if data[i + 2] == 0 and i + 4 <= n and data[i + 3] == 1:
                starts.append((i, 4)); i += 4; continue
        i += 1
    out = []
    for k, (off, sc) in enumerate(starts):
        a = off + sc
        b = starts[k + 1][0] if k + 1 < len(starts) else n
        while b > a and data[b - 1] == 0:
            b -= 1
        out.append(data[a:b])
    return out


def write_nals(path, nals):
    with open(path, "wb") as f:
        for n in nals:
            f.write(len(n).to_bytes(4, "big") + n)


src = split(open(sys.argv[1], "rb").read())
nosei = [n for n in src if (n[0] & 0x1F) != 6]
open(sys.argv[2], "wb").write(b"".join(b"\x00\x00\x00\x01" + n for n in nosei))
write_nals(sys.argv[3], [n for n in src if (n[0] & 0x1F) in (1, 5)])
q50 = split(open(sys.argv[4], "rb").read())
write_nals(sys.argv[5], [n for n in q50 if (n[0] & 0x1F) in (1, 5)])
EOF
"$PY" "$ORACLE/avc_convert.py" avcc2annexb "$TMP/vcl.avcc" "$TMP/joined.264" \
    --params-from "$TMP/x264_nosei.264" >/dev/null
expect_pass h264.avcc-params-from "$PY" "$HERE/nals_equal.py" \
    "$TMP/x264_nosei.264" "$TMP/joined.264"
expect_fail_grep h264.avcc-params-mismatch "slice NALs differ" \
    "$PY" "$ORACLE/avc_convert.py" avcc2annexb "$TMP/q50_vcl.avcc" \
    "$TMP/badjoined.264" --params-from "$TMP/x264_nosei.264"

# a real 4-slice stream: exercises the whole header parser and the per-picture
# slice count (x264 emits SEI and its own level/chroma offsets, hence the
# explicit expectations; everything else matches n2avc.h).
x264 --quiet --profile baseline --bframes 0 --ref 1 --no-deblock \
    --keyint 60 --min-keyint 60 --qp 26 --chroma-qp-offset 0 --slices 4 \
    --input-res 176x144 --output "$TMP/xs.264" "$TMP/src.yuv" >"$TMP/xs.log" 2>&1
"$FFMPEG" -v error -y -i "$TMP/xs.264" -f rawvideo -pix_fmt yuv420p "$TMP/xsdec.yuv"
expect_pass h264.headers-multislice "${H264[@]}" --stream "$TMP/xs.264" \
    --size 176x144 --frames 10 --recon "$TMP/xsdec.yuv" \
    --expect level=11 --expect slices=4 --expect allow-sei --expect chroma-qp-index=-2
expect_fail_grep h264.slice-count "slices per picture" "${H264[@]}" \
    --stream "$TMP/xs.264" --size 176x144 --frames 10 --recon "$TMP/xsdec.yuv" \
    --expect level=11 --expect slices=1 --expect allow-sei --expect chroma-qp-index=-2

# 178x146: the frame-cropping path (right/bottom = 7 chroma units) and the
# level check driven by the SPS's own VUI timing (x264 --level 3.0 -> 30).
"$FFMPEG" -v error -y -f lavfi -i testsrc=size=178x146:rate=30 -frames:v 5 \
    -pix_fmt yuv420p "$TMP/src178.yuv"
x264 --quiet --profile baseline --level 3.0 --bframes 0 --ref 1 --no-deblock \
    --keyint 60 --min-keyint 60 --qp 26 --chroma-qp-offset 0 \
    --input-res 178x146 --output "$TMP/x178.264" "$TMP/src178.yuv" \
    >"$TMP/x178.log" 2>&1
"$FFMPEG" -v error -y -i "$TMP/x178.264" -f rawvideo -pix_fmt yuv420p "$TMP/x178dec.yuv"
expect_pass h264.headers-cropping "${H264[@]}" --stream "$TMP/x178.264" \
    --size 178x146 --frames 5 --recon "$TMP/x178dec.yuv" \
    --expect allow-sei --expect chroma-qp-index=-2

# -------------------------------------------------------- check_mp4 judge
echo "== check_mp4.py"
MP4=("$PY" "$ORACLE/check_mp4.py")
expect_pass mp4.avc1-good "${MP4[@]}" "$TMP/av.mp4" \
    --expect-video avc1 --size 320x240 --min-frames 30 --audio aac
expect_fail_grep mp4.wrong-codec "stsd video entry is avc1" "${MP4[@]}" \
    "$TMP/av.mp4" --expect-video hvc1 --size 320x240 --min-frames 30 --audio aac
expect_fail mp4.truncated "${MP4[@]}" "$TMP/av_trunc.mp4" \
    --expect-video avc1 --size 320x240 --min-frames 30
expect_fail_grep mp4.wrong-size "expected 640x480" "${MP4[@]}" \
    "$TMP/av.mp4" --expect-video avc1 --size 640x480 --min-frames 30
expect_pass mp4.hvc1-good "${MP4[@]}" "$TMP/hv.mp4" \
    --expect-video hvc1 --size 320x240 --min-frames 30

# -------------------------------------------------------- check_abi judge
echo "== check_abi.py"
expect_pass abi.frozen-headers "$PY" "$ORACLE/check_abi.py"
mkdir -p "$TMP/bad_include"
cp "$VIDEO/include/n2_apple.h" "$VIDEO/include/n2vt_abi.h" "$TMP/bad_include/"
grep -v 'reserved\[9\]' "$VIDEO/include/n2vt_abi.h" >"$TMP/bad_include/n2vt_abi.h"
if cmp -s "$VIDEO/include/n2vt_abi.h" "$TMP/bad_include/n2vt_abi.h"; then
    bad abi.removed-member "fixture did not change n2vt_abi.h"
else
    expect_fail abi.removed-member "$PY" "$ORACLE/check_abi.py" \
        --headers "$TMP/bad_include"
fi

# --dylib mode against a synthetic Mach-O with the two required exports
mkdir -p "$TMP/sydyl" "$TMP/emptytbd"
cat >"$TMP/sydyl/good.c" <<'EOF'
__attribute__((visibility("default"))) void SLIMEncoderRegister(void) {}
__attribute__((visibility("default"))) int n2vt_version(void) { return 1; }
EOF
cat >"$TMP/sydyl/extra.c" <<'EOF'
__attribute__((visibility("default"))) void SLIMEncoderRegister(void) {}
__attribute__((visibility("default"))) int n2vt_version(void) { return 1; }
__attribute__((visibility("default"))) void ExtraExport(void) {}
EOF
cat >"$TMP/sydyl/hostpath.c" <<'EOF'
__attribute__((visibility("default"))) void SLIMEncoderRegister(void) {}
__attribute__((visibility("default"))) int n2vt_version(void) { return 1; }
__attribute__((used)) static const char n2BuildPath[] = "/home/someone/n2build/x";
EOF
dylib_ok=1
for name in good extra hostpath; do
    if ! clang --target=arm64-apple-ios16.0 -c -O2 -ffreestanding -nostdinc \
            -o "$TMP/sydyl/$name.o" "$TMP/sydyl/$name.c" \
            >"$TMP/sydyl/$name.log" 2>&1; then
        dylib_ok=0
        break
    fi
    if ! ld64.lld -arch arm64 -platform_version ios 16.0 17.0 -dylib \
            -install_name /System/Library/VideoEncoders/Slim.videoencoder \
            -adhoc_codesign -o "$TMP/sydyl/$name.dylib" "$TMP/sydyl/$name.o" \
            >>"$TMP/sydyl/$name.log" 2>&1; then
        dylib_ok=0
        break
    fi
done
if [ "$dylib_ok" = 1 ]; then
    expect_pass abi.dylib-good "$PY" "$ORACLE/check_abi.py" \
        --dylib "$TMP/sydyl/good.dylib" --sdk-lib "$TMP/emptytbd"
    expect_fail_grep abi.dylib-extra-export "expected exactly" \
        "$PY" "$ORACLE/check_abi.py" \
        --dylib "$TMP/sydyl/extra.dylib" --sdk-lib "$TMP/emptytbd"
    expect_fail_grep abi.dylib-hostpath "/home/" \
        "$PY" "$ORACLE/check_abi.py" \
        --dylib "$TMP/sydyl/hostpath.dylib" --sdk-lib "$TMP/emptytbd"
else
    bad abi.dylib-good "cannot cross-compile a synthetic Mach-O"
    bad abi.dylib-extra-export "cannot cross-compile a synthetic Mach-O"
    bad abi.dylib-hostpath "cannot cross-compile a synthetic Mach-O"
fi

# -------------------------------------------------------- parse_probe.py
echo "== parse_probe.py"
cat >"$TMP/probe-1.jsonl" <<'EOF'
{"probe":"wp07-1.0","kind":"header","version":"test"}
{"test":"vt.encoders","result":"pass","signal":0,"ms":3,"fields":{"status":0,"count":2,"e0":"avc1|com.apple.x|Apple H.264|H.264|hw=0","e1":"hvc1|com.nord2.y|n2 HEVC|HEVC|hw=1"}}
{"test":"vt.decode.avc1","result":"pass","signal":0,"ms":40,"fields":{"frames":"3/3","match":1}}
{"test":"vt.decode.hvc1","result":"fail","signal":0,"ms":41,"fields":{"frames":"0/3","create":-12902}}
{"test":"vt.encode.avc1.640x480","result":"pass","signal":0,"ms":90,"fields":{"frames":30,"bytes":12345,"keyframes":1}}
{"test":"img.write/read.heic","result":"pass","signal":0,"ms":10,"fields":{"finalize":1,"bytes":999}}
{"test":"audio.aac","result":"pass","signal":0,"ms":20,"fields":{"packets":44,"rms_diff_db":"0.4"}}
{"test":"vt.encode.hvc1.1280x720","result":"pa
EOF
cat >"$TMP/probe-0.jsonl" <<'EOF'
{"probe":"wp07-1.0","kind":"header","version":"baseline"}
{"test":"vt.decode.hvc1","result":"pass","signal":0,"ms":41,"fields":{"frames":3,"md5_ok":1}}
{"test":"audio.aac","result":"fail","signal":0,"ms":20,"fields":{"status":-1}}
{"kind":"end","tests":2}
EOF

probe_ok=1
"$PY" "$ORACLE/parse_probe.py" "$TMP/probe-1.jsonl" >"$TMP/probe.out" 2>&1 || probe_ok=0
for pat in \
    "VERDICT decode avc1: yes" \
    "VERDICT decode hvc1: no" \
    "VERDICT encode avc1: yes" \
    "VERDICT encode hvc1: MISSING" \
    "VERDICT heic write: yes" \
    "VERDICT aac: yes" \
    "truncated=1" \
    "avc1|com.apple.x|Apple H.264|H.264|hw=0"; do
    if ! grep -qF -- "$pat" "$TMP/probe.out"; then
        probe_ok=0
        echo "  missing: $pat"
    fi
done
if [ "$probe_ok" = 1 ]; then
    ok probe.parse
else
    bad probe.parse "$(tail -n 1 "$TMP/probe.out" 2>/dev/null || true)"
fi

diff_ok=1
"$PY" "$ORACLE/parse_probe.py" "$TMP/probe-1.jsonl" \
    --baseline "$TMP/probe-0.jsonl" >"$TMP/probe_diff.out" 2>&1 || diff_ok=0
grep -qF "vt.decode.hvc1: result pass -> fail" "$TMP/probe_diff.out" || diff_ok=0
grep -qF "audio.aac: result fail -> pass" "$TMP/probe_diff.out" || diff_ok=0
if [ "$diff_ok" = 1 ]; then
    ok probe.baseline-diff
else
    bad probe.baseline-diff "$(tail -n 1 "$TMP/probe_diff.out" 2>/dev/null || true)"
fi

# --------------------------------------------------------------------- done
TOTAL=$((PASS + FAIL))
if [ "$FAIL" -ne 0 ]; then
    echo "N2TEST WP10 FAIL $((TOTAL - FAIL))/$TOTAL" >&2
    exit 1
fi
echo "N2TEST WP10 PASS $TOTAL/$TOTAL"
